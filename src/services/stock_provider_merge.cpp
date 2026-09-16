#include "services/stock_provider_merge.hpp"

#include "application/stock_data_merge.hpp"
#include "domain/news_text.hpp"
#include "domain/trading_status.hpp"
#include "services/json_access.hpp"
#include "services/provider_payload_parser.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ctime>
#include <utility>

namespace squarestar::marketdata {

using squarestar::application::StockFetchMetrics;
using squarestar::application::StockFetchNews;
using squarestar::application::StockFetchProfile;
using squarestar::json::JsonNumber;
using squarestar::json::JsonPath;
using squarestar::json::JsonString;
using squarestar::json::JsonUint;
using squarestar::json::ParseJsonInSitu;
using squarestar::market::ClassifyCorporateActionNews;
using squarestar::market::CorporateActionStatus;
using squarestar::market::FetchKind;
using squarestar::market::InstrumentNature;
using squarestar::market::NewsItem;
using squarestar::providers::ParseFinnhubQuotePayload;
using YyjsonDoc = squarestar::json::Document;

StockProviderMergeResult ApplyStockProviderPayloads(
    StockProviderPayloads payloads,
    const StockProviderMergeOptions& options,
    squarestar::market::StockFetchResult& destination) {
    auto& result = destination.marketData;
    if (!payloads.yahooQuote.empty()) {
        YyjsonDoc document = ParseJsonInSitu(payloads.yahooQuote);
        yyjson_val* array = document
                                ? JsonPath(yyjson_doc_get_root(document.get()),
                                           {"quoteResponse", "result"})
                                : nullptr;
        yyjson_val* quote =
            array && yyjson_is_arr(array) ? yyjson_arr_get_first(array) : nullptr;
        if (quote && yyjson_is_obj(quote)) {
            std::string companyName;
            if (!JsonString(quote, "longName", companyName, 512))
                JsonString(quote, "shortName", companyName, 512);
            if (!companyName.empty())
                result.companyName = std::move(companyName);
            JsonString(quote, "fullExchangeName", result.exchange, 64);
            std::uint64_t quoteTimestamp = 0;
            const bool hasQuoteTimestamp =
                JsonUint(quote, "regularMarketTime", quoteTimestamp) &&
                quoteTimestamp <= static_cast<std::uint64_t>(std::numeric_limits<std::time_t>::max());
            double value = 0.0;
            if (JsonNumber(quote, "regularMarketPrice", value) && value > 0.0) {
                result.currentPrice = value;
                result.success = true;
                if (hasQuoteTimestamp)
                    result.quoteTimestamp = static_cast<std::time_t>(quoteTimestamp);
            }
            if (JsonNumber(quote, "regularMarketPreviousClose", value) && value > 0.0)
                result.previousClose = value;
            if (JsonNumber(quote, "regularMarketOpen", value) && value > 0.0)
                result.openPrice = value;
            if (JsonNumber(quote, "regularMarketDayHigh", value) && value > 0.0)
                result.dayHigh = value;
            if (JsonNumber(quote, "regularMarketDayLow", value) && value > 0.0)
                result.dayLow = value;
            JsonNumber(quote, "fiftyTwoWeekHigh", result.fiftyTwoWeekHigh);
            JsonNumber(quote, "fiftyTwoWeekLow", result.fiftyTwoWeekLow);
            if (JsonNumber(quote,
                           "fiftyTwoWeekChangePercent",
                           result.fiftyTwoWkChangePercent)) {
                result.hasFiftyTwoWkChangePercent = true;
            } else {
                double change = 0.0;
                if (JsonNumber(quote, "fiftyTwoWeekChange", change)) {
                    const double yearAgoPrice = result.currentPrice - change;
                    if (std::isfinite(yearAgoPrice) && yearAgoPrice > 0.0) {
                        result.fiftyTwoWkChangePercent = change / yearAgoPrice * 100.0;
                        result.hasFiftyTwoWkChangePercent = true;
                    }
                }
            }
            double volume = 0.0;
            if (JsonNumber(quote, "regularMarketVolume", volume) && volume >= 0.0) {
                result.regularMarketVolume = volume;
                result.hasRegularMarketVolume = true;
            }
            double averageVolume = 0.0;
            double marketCap = 0.0;
            double peRatio = 0.0;
            if (JsonNumber(quote, "averageDailyVolume3Month", averageVolume) &&
                averageVolume >= 0.0)
                result.avgVolume = averageVolume;
            if (JsonNumber(quote, "marketCap", marketCap) && marketCap > 0.0)
                result.marketCap = marketCap;
            if (JsonNumber(quote, "trailingPE", peRatio) && peRatio > 0.0)
                result.peRatio = peRatio;
        }
    }

    StockProviderMergeResult merge;
    if (!payloads.finnhubProfile.empty()) {
        YyjsonDoc document = ParseJsonInSitu(payloads.finnhubProfile);
        yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
        if (root && yyjson_is_obj(root) && yyjson_obj_size(root) > 0)
            result.resolvedDetailMask |= StockFetchProfile;
        merge.finnhubProfileExists =
            root && yyjson_is_obj(root) &&
            JsonString(root, "name", result.companyName, 512) &&
            !result.companyName.empty();
        if (merge.finnhubProfileExists)
            result.instrumentNature = InstrumentNature::PublicMarketSecurity;
        JsonString(root, "country", merge.finnhubProfileCountry, 64);
        JsonString(root, "finnhubIndustry", result.industry, 512);
        JsonString(root, "weburl", result.weburl, 4096);
    }

    if (!payloads.finnhubMetrics.empty()) {
        YyjsonDoc document = ParseJsonInSitu(payloads.finnhubMetrics);
        yyjson_val* metrics = document
                                  ? JsonPath(yyjson_doc_get_root(document.get()),
                                             {"metric"})
                                  : nullptr;
        if (metrics && yyjson_is_obj(metrics) && yyjson_obj_size(metrics) > 0)
            result.resolvedDetailMask |= StockFetchMetrics;
        JsonNumber(metrics, "52WeekHigh", result.fiftyTwoWeekHigh);
        JsonNumber(metrics, "52WeekLow", result.fiftyTwoWeekLow);
        JsonNumber(metrics, "peBasicExclExtraTTM", result.peRatio);
        double averageVolume = 0.0;
        double marketCap = 0.0;
        if (JsonNumber(metrics, "10DayAverageTradingVolume", averageVolume))
            result.avgVolume = averageVolume * 1e6;
        if (JsonNumber(metrics, "marketCapitalization", marketCap))
            result.marketCap = marketCap * 1e6;
        JsonNumber(metrics, "dividendYieldIndicatedAnnual", result.dividendYield);
        JsonNumber(metrics, "beta", result.beta);
        if (!result.hasFiftyTwoWkChangePercent &&
            JsonNumber(metrics,
                       "52WeekPriceReturnDaily",
                       result.fiftyTwoWkChangePercent)) {
            result.hasFiftyTwoWkChangePercent = true;
        }
    }

    if (!payloads.finnhubQuote.empty()) {
        const auto quote = ParseFinnhubQuotePayload(payloads.finnhubQuote);


if (quote && quote->currentPrice > 0.0 && result.currentPrice <= 0.0) {
            result.instrumentNature = InstrumentNature::PublicMarketSecurity;
            result.currentPrice = quote->currentPrice;
            result.success = true;
            result.quoteTimestamp = quote->timestamp;
            if (options.kind == FetchKind::Full &&
                destination.resolvedTimeRangeIndex == 0 && !result.closes.empty()) {
                result.closes.back() = quote->currentPrice;
                result.dayHigh = std::max(result.dayHigh, quote->currentPrice);
                result.dayLow = result.dayLow > 0.0
                                    ? std::min(result.dayLow, quote->currentPrice)
                                    : quote->currentPrice;
            }
        }
        if (quote && (options.requestedTimeRangeIndex == 0 ||
                      options.kind == FetchKind::LiveQuote ||
                      options.kind == FetchKind::AlertQuote)) {
            if (quote->previousClose > 0.0 && result.previousClose <= 0.0)
                result.previousClose = quote->previousClose;
            if (quote->dayHigh > 0.0 && result.dayHigh <= 0.0)
                result.dayHigh = quote->dayHigh;
            if (quote->dayLow > 0.0 && result.dayLow <= 0.0)
                result.dayLow = quote->dayLow;
            if (quote->openPrice > 0.0 && result.openPrice <= 0.0)
                result.openPrice = quote->openPrice;
        }
    }

    if ((options.wantNews || options.needsCorporateActionEvidence) &&
        (result.success || options.detailsLoad) && !payloads.finnhubNews.empty()) {
        YyjsonDoc document = ParseJsonInSitu(payloads.finnhubNews);
        yyjson_val* array = document ? yyjson_doc_get_root(document.get()) : nullptr;
        if (array && yyjson_is_arr(array)) {
            result.resolvedDetailMask |= StockFetchNews;
            result.news.clear();
            size_t index = 0;
            size_t count = 0;
            yyjson_val* item = nullptr;
            yyjson_arr_foreach(array, index, count, item) {
                NewsItem news;
                JsonString(item, "headline", news.headline, 512);
                JsonString(item, "source", news.source, 256);
                JsonString(item, "url", news.url, 4096);
                JsonString(item, "summary", news.summary, 4096);
                news.headline = squarestar::news::NormalizeNewsText(news.headline);
                news.source = squarestar::news::NormalizeNewsText(news.source);
                news.summary = squarestar::news::NormalizeNewsText(news.summary);
                std::uint64_t datetime = 0;
                if (JsonUint(item, "datetime", datetime))
                    news.datetime = static_cast<std::time_t>(datetime);
                if (!news.headline.empty())
                    result.news.push_back(std::move(news));
                if (result.news.size() >= 8)
                    break;
            }
        }
    }
    if (options.needsCorporateActionEvidence) {
        std::string evidence;
        const CorporateActionStatus classified =
            ClassifyCorporateActionNews(result.news, &evidence);
        if (classified != CorporateActionStatus::None) {
            result.tradingStatus.corporateAction = classified;
            result.tradingStatus.evidenceHeadline = std::move(evidence);
        }
    }
    return merge;
}

}
