#include "services/screener_payload.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <utility>

#include "domain/market_symbol.hpp"
#include "domain/screener_data.hpp"
#include "services/json_access.hpp"

namespace squarestar::providers {
namespace {

using squarestar::json::JsonNumber;
using squarestar::json::JsonPath;
using squarestar::json::JsonString;
using squarestar::json::JsonUint;
using squarestar::json::ParseJsonInSitu;
using squarestar::market::YahooSymbolKey;

void ExtractFiveDaySparkline(yyjson_val* response, std::vector<float>& out) {
    out.clear();
    if (!response || !yyjson_is_obj(response))
        return;
    yyjson_val* quotes = JsonPath(response, {"indicators", "quote"});
    yyjson_val* quote = quotes && yyjson_is_arr(quotes) ? yyjson_arr_get_first(quotes) : nullptr;
    yyjson_val* closes = quote && yyjson_is_obj(quote) ? yyjson_obj_get(quote, "close") : nullptr;
    if (!closes || !yyjson_is_arr(closes))
        return;
    const std::size_t count =
        std::min(yyjson_arr_size(closes), kMaxScreenerSparklineSamples);
    for (std::size_t i = 0; i < count; ++i) {
        yyjson_val* point = yyjson_arr_get(closes, i);
        if (point && !yyjson_is_null(point) && yyjson_is_num(point)) {
            const double value = yyjson_get_num(point);
            if (std::isfinite(value) && value > 0.0) {
                const float narrowed = static_cast<float>(value);
                if (std::isfinite(narrowed))
                    out.push_back(narrowed);
            }
        }
    }
    // Preserve the provider series. Overview uses daily closes; other chart
    // callers may provide denser points.
}

void ReadYahooName(yyjson_val* object, ScreenerItem& item, bool preferLongName) {
    std::string name;
    if (preferLongName) {
        if (!JsonString(object, "longName", name, 512))
            JsonString(object, "shortName", name, 512);
    } else if (!JsonString(object, "shortName", name, 512)) {
        JsonString(object, "longName", name, 512);
    }
    if (!name.empty())
        item.name = std::move(name);
}

void ApplyYahooQuote(yyjson_val* quote, ScreenerItem& item, bool preferLongName) {
    if (!quote || !yyjson_is_obj(quote))
        return;
    item.resolved = true;
    ReadYahooName(quote, item, preferLongName);
    double price = 0.0;
    double previous = 0.0;
    const bool hasFreshPrice =
        JsonNumber(quote, "regularMarketPrice", price) && price > 0.0;
    if (hasFreshPrice) {
        item.price = price;
        item.hasPrice = true;
    }
    if (JsonNumber(quote, "regularMarketChange", item.change))
        item.hasChange = true;
    if (JsonNumber(quote, "regularMarketChangePercent", item.changePercent))
        item.hasChangePercent = true;
    if (JsonNumber(quote, "regularMarketPreviousClose", previous) && previous > 0.0 &&
        item.hasPrice) {
        if (!item.hasChange) {
            item.change = item.price - previous;
            item.hasChange = true;
        }
        if (!item.hasChangePercent) {
            item.changePercent = item.change / previous * 100.0;
            item.hasChangePercent = true;
        }
    }

    std::uint64_t whole = 0;
    if (JsonUint(quote, "regularMarketVolume", whole)) {
        item.volume = static_cast<double>(whole);
        item.hasVolume = true;
    }
    if (JsonUint(quote, "averageDailyVolume3Month", whole)) {
        item.avgVol3M = static_cast<double>(whole);
        item.hasAvgVol3M = true;
    }
    double value = 0.0;
    if (JsonNumber(quote, "marketCap", value) && std::isfinite(value) && value > 0.0) {
        item.marketCap = value;
        item.hasMarketCap = true;
    }
    if (JsonNumber(quote, "trailingPE", value)) {
        item.peRatio = value;
        item.hasPeRatio = true;
    }
    if (JsonNumber(quote, "fiftyTwoWeekChangePercent", value)) {
        item.fiftyTwoWkChange = value;
        item.hasFiftyTwoWkChange = true;
    } else if (JsonNumber(quote, "fiftyTwoWeekChange", value)) {
        item.fiftyTwoWkChange = value * 100.0;
        item.hasFiftyTwoWkChange = true;
    }
    if (hasFreshPrice && JsonUint(quote, "regularMarketTime", whole))
        item.lastMarketTime = static_cast<std::time_t>(whole);
}

} // namespace

namespace {

std::size_t ApplyYahooQuoteResponsePayloadImpl(
    std::string payload,
    const ScreenerItemIndex& itemIndex,
    std::vector<ScreenerItem>& items,
    std::unordered_set<std::string>* equitySymbols) {
    auto document = ParseJsonInSitu(payload);
    yyjson_val* results = document
                              ? JsonPath(yyjson_doc_get_root(document.get()),
                                         {"quoteResponse", "result"})
                              : nullptr;
    if (!results || !yyjson_is_arr(results))
        return 0;

    std::size_t applied = 0;
    std::size_t quoteIndex = 0;
    std::size_t quoteCount = 0;
    yyjson_val* quote = nullptr;
    yyjson_arr_foreach(results, quoteIndex, quoteCount, quote) {
        std::string symbol;
        if (!JsonString(quote, "symbol", symbol, 64))
            continue;
        const std::string yahooSymbol = YahooSymbolKey(symbol);
        const auto found = itemIndex.find(yahooSymbol);
        if (found == itemIndex.end() || found->second >= items.size())
            continue;
        ApplyYahooQuote(quote, items[found->second], true);
        if (equitySymbols) {
            std::string quoteType;
            if (JsonString(quote, "quoteType", quoteType, 32) && quoteType == "EQUITY")
                equitySymbols->insert(yahooSymbol);
        }
        ++applied;
    }
    return applied;
}

} // namespace

std::size_t ApplyYahooQuoteResponsePayload(std::string payload,
                                           const ScreenerItemIndex& itemIndex,
                                           std::vector<ScreenerItem>& items) {
    return ApplyYahooQuoteResponsePayloadImpl(
        std::move(payload), itemIndex, items, nullptr);
}

std::size_t ApplyYahooEquityQuoteResponsePayload(
    std::string payload,
    const ScreenerItemIndex& itemIndex,
    std::vector<ScreenerItem>& items,
    std::unordered_set<std::string>& equitySymbols) {
    return ApplyYahooQuoteResponsePayloadImpl(
        std::move(payload), itemIndex, items, &equitySymbols);
}

std::vector<std::string> ParseYahooTrendingSymbols(std::string payload, std::size_t limit) {
    std::vector<std::string> symbols;
    if (limit == 0)
        return symbols;
    auto document = ParseJsonInSitu(payload);
    yyjson_val* results = document
                              ? JsonPath(yyjson_doc_get_root(document.get()),
                                         {"finance", "result"})
                              : nullptr;
    yyjson_val* result =
        results && yyjson_is_arr(results) ? yyjson_arr_get_first(results) : nullptr;
    yyjson_val* quotes =
        result && yyjson_is_obj(result) ? yyjson_obj_get(result, "quotes") : nullptr;
    if (!quotes || !yyjson_is_arr(quotes))
        return symbols;

    symbols.reserve(std::min<std::size_t>(limit, yyjson_arr_size(quotes)));
    std::unordered_set<std::string> seen;
    seen.reserve(symbols.capacity());
    std::size_t quoteIndex = 0;
    std::size_t quoteCount = 0;
    yyjson_val* quote = nullptr;
    yyjson_arr_foreach(quotes, quoteIndex, quoteCount, quote) {
        std::string symbol;
        if (!JsonString(quote, "symbol", symbol, 64) || symbol.empty())
            continue;
        const std::string yahooSymbol = YahooSymbolKey(std::move(symbol));
        if (!seen.insert(yahooSymbol).second)
            continue;
        symbols.push_back(yahooSymbol);
        if (symbols.size() >= limit)
            break;
    }
    return symbols;
}

bool ApplyYahooScreenerChartPayload(std::string payload,
                                    ScreenerItem& item,
                                    bool includeSparkline) {
    auto document = ParseJsonInSitu(payload);
    yyjson_val* results =
        document ? JsonPath(yyjson_doc_get_root(document.get()), {"chart", "result"}) : nullptr;
    yyjson_val* response =
        results && yyjson_is_arr(results) ? yyjson_arr_get_first(results) : nullptr;
    if (!response || !yyjson_is_obj(response))
        return false;

    item.resolved = true;
    yyjson_val* meta = yyjson_obj_get(response, "meta");
    ReadYahooName(meta, item, true);
    double price = 0.0;
    double previous = 0.0;
    const bool hasFreshPrice =
        JsonNumber(meta, "regularMarketPrice", price) && price > 0.0;
    if (hasFreshPrice) {
        item.price = price;
        item.hasPrice = true;
    }
    if (!JsonNumber(meta, "chartPreviousClose", previous))
        JsonNumber(meta, "previousClose", previous);
    if (item.hasPrice && previous > 0.0) {
        item.change = item.price - previous;
        item.hasChange = true;
        item.changePercent = item.change / previous * 100.0;
        item.hasChangePercent = true;
    }

    double value = 0.0;
    if (JsonNumber(meta, "regularMarketVolume", value) && value >= 0.0) {
        item.volume = value;
        item.hasVolume = true;
    }
    if (JsonNumber(meta, "marketCap", value) && std::isfinite(value) && value > 0.0) {
        item.marketCap = value;
        item.hasMarketCap = true;
    }
    std::uint64_t marketTime = 0;
    if (hasFreshPrice && JsonUint(meta, "regularMarketTime", marketTime))
        item.lastMarketTime = static_cast<std::time_t>(marketTime);
    if (includeSparkline && item.sparkline.size() < 2) {
        std::vector<float> parsedSparkline;
        ExtractFiveDaySparkline(response, parsedSparkline);
        squarestar::market::ReplaceScreenerSparkline(item, std::move(parsedSparkline));
    }
    return true;
}

void AppendYahooScreenerPayload(std::string payload,
                                std::size_t limit,
                                std::vector<ScreenerItem>& items) {
    if (items.size() >= limit)
        return;
    auto document = ParseJsonInSitu(payload);
    yyjson_val* results =
        document ? JsonPath(yyjson_doc_get_root(document.get()), {"finance", "result"}) : nullptr;
    if (!results || !yyjson_is_arr(results))
        return;
    std::size_t resultIndex = 0;
    std::size_t resultCount = 0;
    yyjson_val* result = nullptr;
    yyjson_arr_foreach(results, resultIndex, resultCount, result) {
        yyjson_val* quotes =
            result && yyjson_is_obj(result) ? yyjson_obj_get(result, "quotes") : nullptr;
        if (!quotes || !yyjson_is_arr(quotes))
            continue;
        std::size_t quoteIndex = 0;
        std::size_t quoteCount = 0;
        yyjson_val* quote = nullptr;
        yyjson_arr_foreach(quotes, quoteIndex, quoteCount, quote) {
            ScreenerItem item;
            JsonString(quote, "symbol", item.symbol, 64);
            if (item.symbol.empty())
                continue;
            ApplyYahooQuote(quote, item, false);
            items.push_back(std::move(item));
            if (items.size() >= limit)
                return;
        }
    }
}

void MarkInactiveScreenerItems(std::vector<ScreenerItem>& items, std::time_t now) noexcept {
    for (ScreenerItem& item : items) {
        item.suspectedInactive =
            !item.hasPrice ||
            (item.lastMarketTime > 0 && now - item.lastMarketTime > 7 * 24 * 3600);
    }
}

} // namespace squarestar::providers
