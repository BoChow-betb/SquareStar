#include "provider_payload_parser.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "yyjson.h"

namespace squarestar::providers {
namespace {

// The parser later groups samples by converting Unix seconds to int64_t. Keep
// provider-controlled values inside that conversion's defined range; 2^63 is
// exactly representable as a double and is therefore a convenient exclusive
// upper bound.
constexpr double kEpochSecondsUpperBoundExclusive = 9223372036854775808.0;

struct JsonDocumentDeleter {
    void operator()(yyjson_doc* document) const noexcept {
        if (document)
            yyjson_doc_free(document);
    }
};

using JsonDocument = std::unique_ptr<yyjson_doc, JsonDocumentDeleter>;

JsonDocument ParseJson(std::string_view payload) {
    if (payload.empty())
        return {};
    return JsonDocument(yyjson_read(payload.data(), payload.size(), YYJSON_READ_NOFLAG));
}

JsonDocument ParseJsonInSitu(std::string& payload) {
    if (payload.empty())
        return {};
    const size_t payloadSize = payload.size();
    payload.resize(payloadSize + YYJSON_PADDING_SIZE, '\0');
    return JsonDocument(yyjson_read_opts(
        payload.data(), payloadSize, YYJSON_READ_INSITU, nullptr, nullptr));
}

yyjson_val* ObjectMember(yyjson_val* object, const char* key) {
    return object && yyjson_is_obj(object) ? yyjson_obj_get(object, key) : nullptr;
}

bool ReadNumber(yyjson_val* object, const char* key, double& destination) {
    yyjson_val* value = ObjectMember(object, key);
    if (!value || !yyjson_is_num(value))
        return false;
    const double number = yyjson_get_num(value);
    if (!std::isfinite(number))
        return false;
    destination = number;
    return true;
}

bool ReadString(yyjson_val* object,
                const char* key,
                std::string& destination,
                size_t maxBytes) {
    yyjson_val* value = ObjectMember(object, key);
    if (!value || !yyjson_is_str(value))
        return false;
    const size_t length = yyjson_get_len(value);
    if (length > maxBytes)
        return false;
    destination.assign(yyjson_get_str(value), length);
    return true;
}

bool ReadNumberAt(yyjson_val* array, size_t index, double& destination) {
    if (!array || !yyjson_is_arr(array) || index >= yyjson_arr_size(array))
        return false;
    yyjson_val* value = yyjson_arr_get(array, index);
    if (!value || !yyjson_is_num(value))
        return false;
    const double number = yyjson_get_num(value);
    if (!std::isfinite(number))
        return false;
    destination = number;
    return true;
}

bool HasResolvedCompanyName(const std::string& companyName) {
    return !companyName.empty() && companyName != "Fetching...";
}

struct SplitEvent {
    double timestamp = 0.0;
    double factor = 1.0;
};

std::vector<SplitEvent> ReadSplitEvents(yyjson_val* response) {
    std::vector<SplitEvent> result;
    yyjson_val* splits = ObjectMember(ObjectMember(response, "events"), "splits");
    if (!splits || !yyjson_is_obj(splits))
        return result;
    result.reserve(yyjson_obj_size(splits));
    yyjson_obj_iter iterator = yyjson_obj_iter_with(splits);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator)) {
        yyjson_val* event = yyjson_obj_iter_get_val(key);
        double timestamp = 0.0;
        double numerator = 0.0;
        double denominator = 0.0;
        if (!ReadNumber(event, "date", timestamp) || timestamp <= 0.0 ||
            timestamp >= kEpochSecondsUpperBoundExclusive ||
            !ReadNumber(event, "numerator", numerator) || numerator <= 0.0 ||
            !ReadNumber(event, "denominator", denominator) || denominator <= 0.0) {
            continue;
        }
        const double factor = numerator / denominator;
        if (std::isfinite(factor) && factor >= 0.0001 && factor <= 10000.0 &&
            std::abs(factor - 1.0) > 0.000001) {
            result.push_back({timestamp, factor});
        }
    }
    std::sort(result.begin(), result.end(), [](const SplitEvent& left, const SplitEvent& right) {
        return left.timestamp < right.timestamp;
    });
    return result;
}

bool SplitAdjustmentImprovesContinuity(double before,
                                       double after,
                                       double factor) noexcept {
    if (!(before > 0.0) || !(after > 0.0) || !(factor > 0.0))
        return false;
    const double rawDistance = std::abs(std::log(before / after));
    const double adjustedDistance = std::abs(std::log((before / factor) / after));
    // A corporate action must make the boundary materially more coherent.
    // This protects already-adjusted payloads from being adjusted a second time.
    return std::isfinite(adjustedDistance) && adjustedDistance + 0.08 < rawDistance;
}

void NormalizeSplitBoundaries(const std::vector<SplitEvent>& splits,
                              const std::vector<double>& timestamps,
                              std::vector<double>& opens,
                              std::vector<double>& highs,
                              std::vector<double>& lows,
                              std::vector<double>& closes,
                              std::vector<double>& volumes,
                              double& previousClose,
                              bool hasPreviousClose) {
    if (timestamps.empty() || closes.empty())
        return;

    struct Adjustment {
        size_t index = 0;
        double factor = 1.0;
    };
    std::vector<Adjustment> accepted;
    accepted.reserve(splits.size());

    // Decide which boundaries really need adjustment without rewriting the
    // whole prefix after every split. Earlier split boundaries cannot change
    // the sample immediately before a later boundary.
    for (const SplitEvent& split : splits) {
        const auto boundary =
            std::lower_bound(timestamps.begin(), timestamps.end(), split.timestamp);
        const size_t index = static_cast<size_t>(boundary - timestamps.begin());
        if (index == 0) {
            if (hasPreviousClose &&
                SplitAdjustmentImprovesContinuity(previousClose, closes.front(), split.factor)) {
                previousClose /= split.factor;
            }
            continue;
        }
        if (index >= closes.size())
            continue;

        double before = closes[index - 1];
        if (!accepted.empty() && accepted.back().index == index)
            before /= accepted.back().factor;
        if (!SplitAdjustmentImprovesContinuity(before, closes[index], split.factor))
            continue;

        if (!accepted.empty() && accepted.back().index == index)
            accepted.back().factor *= split.factor;
        else
            accepted.push_back({index, split.factor});
    }

    if (accepted.empty())
        return;

    // Walk the samples once from newest to oldest. A sample is adjusted by
    // every accepted split whose boundary lies to its right.
    size_t adjustment = accepted.size();
    long double factor = 1.0L;
    for (size_t sample = closes.size(); sample-- > 0;) {
        while (adjustment > 0 && accepted[adjustment - 1].index > sample) {
            factor *= static_cast<long double>(accepted[adjustment - 1].factor);
            --adjustment;
        }
        if (factor == 1.0L)
            continue;
        opens[sample] = static_cast<double>(static_cast<long double>(opens[sample]) / factor);
        highs[sample] = static_cast<double>(static_cast<long double>(highs[sample]) / factor);
        lows[sample] = static_cast<double>(static_cast<long double>(lows[sample]) / factor);
        closes[sample] = static_cast<double>(static_cast<long double>(closes[sample]) / factor);
        volumes[sample] = static_cast<double>(static_cast<long double>(volumes[sample]) * factor);
    }
}

} // namespace

bool ApplyYahooChartPayload(std::string payload,
                            bool aggregateLatestTradingDayVolume,
                            squarestar::market::StockData& destination) {
    if (payload.size() > kMaxProviderPayloadBytes)
        return false;
    JsonDocument document = ParseJsonInSitu(payload);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    yyjson_val* chart = ObjectMember(root, "chart");
    yyjson_val* results = ObjectMember(chart, "result");
    yyjson_val* response =
        results && yyjson_is_arr(results) ? yyjson_arr_get_first(results) : nullptr;
    if (!response || !yyjson_is_obj(response))
        return false;

    yyjson_val* timestamps = ObjectMember(response, "timestamp");
    yyjson_val* indicators = ObjectMember(response, "indicators");
    yyjson_val* quotes = ObjectMember(indicators, "quote");
    yyjson_val* quote = quotes && yyjson_is_arr(quotes) ? yyjson_arr_get_first(quotes) : nullptr;
    yyjson_val* closes = ObjectMember(quote, "close");
    if (!timestamps || !yyjson_is_arr(timestamps) || !quote || !yyjson_is_obj(quote) ||
        !closes || !yyjson_is_arr(closes)) {
        return false;
    }

    yyjson_val* opens = ObjectMember(quote, "open");
    yyjson_val* highs = ObjectMember(quote, "high");
    yyjson_val* lows = ObjectMember(quote, "low");
    yyjson_val* volumes = ObjectMember(quote, "volume");

    std::vector<double> parsedTimestamps;
    std::vector<double> parsedOpens;
    std::vector<double> parsedHighs;
    std::vector<double> parsedLows;
    std::vector<double> parsedCloses;
    std::vector<double> parsedVolumes;
    const size_t count = std::min(yyjson_arr_size(timestamps), yyjson_arr_size(closes));
    if (count > kMaxProviderChartSamples)
        return false;
    parsedTimestamps.reserve(count);
    parsedOpens.reserve(count);
    parsedHighs.reserve(count);
    parsedLows.reserve(count);
    parsedCloses.reserve(count);
    parsedVolumes.reserve(count);

    for (size_t index = 0; index < count; ++index) {
        double timestamp = 0.0;
        double close = 0.0;
        if (!ReadNumberAt(timestamps, index, timestamp) ||
            timestamp <= 0.0 || timestamp >= kEpochSecondsUpperBoundExclusive ||
            !ReadNumberAt(closes, index, close) || close <= 0.0) {
            continue;
        }
        if (!parsedTimestamps.empty() && timestamp <= parsedTimestamps.back())
            continue;

        double open = close;
        double high = close;
        double low = close;
        double volume = 0.0;
        ReadNumberAt(opens, index, open);
        ReadNumberAt(highs, index, high);
        ReadNumberAt(lows, index, low);
        ReadNumberAt(volumes, index, volume);
        if (open <= 0.0)
            open = close;
        if (high <= 0.0)
            high = std::max(open, close);
        if (low <= 0.0)
            low = std::min(open, close);
        high = std::max({high, open, close});
        low = std::min({low, open, close});
        if (volume < 0.0 || !std::isfinite(volume))
            volume = 0.0;

        parsedTimestamps.push_back(timestamp);
        parsedOpens.push_back(open);
        parsedHighs.push_back(high);
        parsedLows.push_back(low);
        parsedCloses.push_back(close);
        parsedVolumes.push_back(volume);
    }

    if (parsedCloses.size() < 2)
        return false;

    yyjson_val* meta = ObjectMember(response, "meta");
    std::string companyName;
    if (!ReadString(meta, "longName", companyName, 512))
        ReadString(meta, "shortName", companyName, 512);
    std::string exchange;
    ReadString(meta, "exchangeName", exchange, 64);
    std::string currency;
    ReadString(meta, "currency", currency, 16);
    double chartPreviousClose = 0.0;
    const bool hasChartPreviousClose =
        ReadNumber(meta, "chartPreviousClose", chartPreviousClose) &&
        chartPreviousClose > 0.0;
    double regularMarketVolume = 0.0;
    bool hasRegularMarketVolume =
        ReadNumber(meta, "regularMarketVolume", regularMarketVolume) &&
        regularMarketVolume >= 0.0;

    NormalizeSplitBoundaries(ReadSplitEvents(response),
                             parsedTimestamps,
                             parsedOpens,
                             parsedHighs,
                             parsedLows,
                             parsedCloses,
                             parsedVolumes,
                             chartPreviousClose,
                             hasChartPreviousClose);

    if (regularMarketVolume <= 0.0 && !parsedVolumes.empty()) {
        if (aggregateLatestTradingDayVolume) {
            regularMarketVolume = 0.0;
            hasRegularMarketVolume = false;
            const int64_t latestDay = static_cast<int64_t>(parsedTimestamps.back()) / 86400;
            for (size_t index = 0; index < parsedVolumes.size(); ++index) {
                const double volume = parsedVolumes[index];
                hasRegularMarketVolume = hasRegularMarketVolume || volume > 0.0;
                if (static_cast<int64_t>(parsedTimestamps[index]) / 86400 == latestDay)
                    regularMarketVolume += volume;
            }
        } else {
            const auto lastVolume =
                std::find_if(parsedVolumes.rbegin(), parsedVolumes.rend(), [](double volume) {
                    return volume > 0.0;
                });
            if (lastVolume != parsedVolumes.rend()) {
                regularMarketVolume = *lastVolume;
                hasRegularMarketVolume = true;
            }
        }
    }

    squarestar::market::StockData chartSeries;
    chartSeries.timestamps = std::move(parsedTimestamps);
    chartSeries.opens = std::move(parsedOpens);
    chartSeries.highs = std::move(parsedHighs);
    chartSeries.lows = std::move(parsedLows);
    chartSeries.closes = std::move(parsedCloses);
    chartSeries.volumes = std::move(parsedVolumes);
    if (!squarestar::market::StockDataInvariantsHold(chartSeries))
        return false;

    if (!HasResolvedCompanyName(destination.companyName) && !companyName.empty())
        destination.companyName = std::move(companyName);
    if (!currency.empty())
        destination.currency = std::move(currency);
    if (!exchange.empty())
        destination.exchange = std::move(exchange);
    // If the quote endpoint is unavailable, chart metadata can still provide a
    // previous-close fallback. A later quote merge may replace it.
    if (hasChartPreviousClose) {
        destination.chartPreviousClose = chartPreviousClose;
        destination.previousClose = chartPreviousClose;
    }
    destination.regularMarketVolume = regularMarketVolume;
    destination.hasRegularMarketVolume = hasRegularMarketVolume;
    destination.timestamps = std::move(chartSeries.timestamps);
    destination.opens = std::move(chartSeries.opens);
    destination.highs = std::move(chartSeries.highs);
    destination.lows = std::move(chartSeries.lows);
    destination.closes = std::move(chartSeries.closes);
    destination.volumes = std::move(chartSeries.volumes);
    destination.currentPrice = destination.closes.back();
    destination.quoteTimestamp = static_cast<std::time_t>(destination.timestamps.back());
    destination.openPrice = destination.opens.front();
    destination.dayHigh =
        *std::max_element(destination.highs.begin(), destination.highs.end());
    destination.dayLow = *std::min_element(destination.lows.begin(), destination.lows.end());
    destination.success = true;
    return true;
}

bool YahooChartPayloadHasUsableSeries(std::string payload) {
    squarestar::market::StockData parsed;
    return ApplyYahooChartPayload(std::move(payload), false, parsed);
}

std::optional<YahooFxRateSnapshot> ParseYahooFxRatePayload(std::string payload) {
    if (payload.empty() || payload.size() > kMaxProviderPayloadBytes)
        return std::nullopt;

    JsonDocument document = ParseJsonInSitu(payload);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    yyjson_val* chart = ObjectMember(root, "chart");
    yyjson_val* results = ObjectMember(chart, "result");
    yyjson_val* response =
        results && yyjson_is_arr(results) ? yyjson_arr_get_first(results) : nullptr;
    if (!response || !yyjson_is_obj(response))
        return std::nullopt;

    YahooFxRateSnapshot snapshot;
    yyjson_val* meta = ObjectMember(response, "meta");
    if (ReadNumber(meta, "regularMarketPrice", snapshot.rate) &&
        std::isfinite(snapshot.rate) && snapshot.rate > 0.0) {
        double timestamp = 0.0;
        if (ReadNumber(meta, "regularMarketTime", timestamp) && timestamp > 0.0 &&
            timestamp < kEpochSecondsUpperBoundExclusive) {
            snapshot.timestamp = static_cast<std::time_t>(timestamp);
        }
        return snapshot;
    }

    yyjson_val* timestamps = ObjectMember(response, "timestamp");
    yyjson_val* indicators = ObjectMember(response, "indicators");
    yyjson_val* quotes = ObjectMember(indicators, "quote");
    yyjson_val* quote =
        quotes && yyjson_is_arr(quotes) ? yyjson_arr_get_first(quotes) : nullptr;
    yyjson_val* closes = ObjectMember(quote, "close");
    if (!closes || !yyjson_is_arr(closes))
        return std::nullopt;

    const size_t closeCount = yyjson_arr_size(closes);
    for (size_t reverse = 0; reverse < closeCount; ++reverse) {
        const size_t index = closeCount - reverse - 1;
        double close = 0.0;
        if (!ReadNumberAt(closes, index, close) || !std::isfinite(close) || close <= 0.0)
            continue;
        snapshot.rate = close;
        double timestamp = 0.0;
        if (timestamps && yyjson_is_arr(timestamps) &&
            ReadNumberAt(timestamps, index, timestamp) && timestamp > 0.0 &&
            timestamp < kEpochSecondsUpperBoundExclusive) {
            snapshot.timestamp = static_cast<std::time_t>(timestamp);
        }
        return snapshot;
    }
    return std::nullopt;
}

std::vector<YahooQuoteSnapshot> ParseYahooQuoteBatchPayload(std::string payload) {
    std::vector<YahooQuoteSnapshot> quotes;
    if (payload.empty() || payload.size() > kMaxProviderPayloadBytes)
        return quotes;
    JsonDocument document = ParseJsonInSitu(payload);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    yyjson_val* response = ObjectMember(root, "quoteResponse");
    yyjson_val* results = ObjectMember(response, "result");
    if (!results || !yyjson_is_arr(results) || yyjson_arr_size(results) > 256)
        return quotes;
    quotes.reserve(yyjson_arr_size(results));
    size_t index = 0;
    size_t count = 0;
    yyjson_val* item = nullptr;
    yyjson_arr_foreach(results, index, count, item) {
        if (!item || !yyjson_is_obj(item))
            continue;
        YahooQuoteSnapshot quote;
        if (!ReadString(item, "symbol", quote.symbol, 64) || quote.symbol.empty() ||
            !ReadNumber(item, "regularMarketPrice", quote.currentPrice) ||
            quote.currentPrice <= 0.0) {
            continue;
        }
        ReadNumber(item, "regularMarketPreviousClose", quote.previousClose);
        ReadNumber(item, "regularMarketOpen", quote.openPrice);
        ReadNumber(item, "regularMarketDayHigh", quote.dayHigh);
        ReadNumber(item, "regularMarketDayLow", quote.dayLow);
        double timestamp = 0.0;
        if (ReadNumber(item, "regularMarketTime", timestamp) && timestamp > 0.0 &&
            timestamp < kEpochSecondsUpperBoundExclusive) {
            quote.timestamp = static_cast<std::time_t>(timestamp);
        }
        quotes.push_back(std::move(quote));
    }
    return quotes;
}

std::optional<FinnhubQuote> ParseFinnhubQuotePayload(std::string_view payload) {
    if (payload.size() > kMaxProviderPayloadBytes)
        return std::nullopt;
    JsonDocument document = ParseJson(payload);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (!root || !yyjson_is_obj(root))
        return std::nullopt;

    FinnhubQuote quote;
    ReadNumber(root, "c", quote.currentPrice);
    ReadNumber(root, "pc", quote.previousClose);
    ReadNumber(root, "h", quote.dayHigh);
    ReadNumber(root, "l", quote.dayLow);
    ReadNumber(root, "o", quote.openPrice);
    double timestamp = 0.0;
    if (ReadNumber(root, "t", timestamp) && timestamp > 0.0 &&
        timestamp < kEpochSecondsUpperBoundExclusive) {
        quote.timestamp = static_cast<std::time_t>(timestamp);
    }
    return quote;
}

bool FinnhubQuotePayloadHasPrice(std::string_view payload) {
    const std::optional<FinnhubQuote> quote = ParseFinnhubQuotePayload(payload);
    return quote && quote->currentPrice > 0.0;
}

std::optional<double> ParseFinnhubMetricMarketCap(std::string_view payload) {
    if (payload.size() > kMaxProviderPayloadBytes)
        return std::nullopt;
    JsonDocument document = ParseJson(payload);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    yyjson_val* metric = ObjectMember(root, "metric");
    double marketCapMillions = 0.0;
    if (!ReadNumber(metric, "marketCapitalization", marketCapMillions) ||
        marketCapMillions <= 0.0 ||
        marketCapMillions > std::numeric_limits<double>::max() / 1e6) {
        return std::nullopt;
    }
    return marketCapMillions * 1e6;
}

} // namespace squarestar::providers
