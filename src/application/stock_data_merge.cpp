#include "application/stock_data_merge.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace squarestar::application {

using squarestar::market::FetchKind;
using squarestar::market::StockData;

namespace {

bool HasNonZeroMetricValue(double value) noexcept {
    return value < 0.0 || value > 0.0;
}

} // namespace

bool HasResolvedCompanyName(const std::string& companyName) noexcept {
    return !companyName.empty() && companyName != "Fetching...";
}

bool HasAnyMarketMetricData(const StockData& data) noexcept {
    return data.peRatio > 0.0 || data.avgVolume > 0.0 ||
           data.fiftyTwoWeekHigh > 0.0 || data.fiftyTwoWeekLow > 0.0 ||
           data.marketCap > 0.0 || HasNonZeroMetricValue(data.dividendYield) ||
           HasNonZeroMetricValue(data.beta) || data.hasFiftyTwoWkChangePercent;
}

bool ApplyStockQuotePatch(StockData& current,
                          const StockData& patch,
                          int timeRangeIndex) {
    if (!patch.success)
        return false;
    if (patch.currentPrice > 0.0) {
        current.currentPrice = patch.currentPrice;
        if (patch.quoteTimestamp > 0)
            current.quoteTimestamp = patch.quoteTimestamp;
    }
    if (patch.previousClose > 0.0)
        current.previousClose = patch.previousClose;
    if (patch.openPrice > 0.0)
        current.openPrice = patch.openPrice;
    if (patch.dayHigh > 0.0)
        current.dayHigh = patch.dayHigh;
    if (patch.dayLow > 0.0)
        current.dayLow = patch.dayLow;
    if (timeRangeIndex == 0 && !current.closes.empty() && patch.currentPrice > 0.0) {
        current.closes.back() = patch.currentPrice;
        if (!current.highs.empty())
            current.highs.back() = std::max(current.highs.back(), patch.currentPrice);
        if (!current.lows.empty())
            current.lows.back() = current.lows.back() > 0.0
                                      ? std::min(current.lows.back(), patch.currentPrice)
                                      : patch.currentPrice;
    }
    current.success = current.currentPrice > 0.0;
    current.errorMessage.clear();
    assert(squarestar::market::StockDataInvariantsHold(current));
    return true;
}

StockData MergeStockFetchPatch(const StockData& current,
                               StockData patch,
                               FetchKind kind,
                               int timeRangeIndex) {
    if (kind == FetchKind::Full) {
        assert(squarestar::market::StockDataInvariantsHold(patch));
        return patch;
    }

    StockData merged = current;
    if (!patch.success)
        return merged;

    if (kind == FetchKind::Chart) {
        merged.timestamps = std::move(patch.timestamps);
        merged.opens = std::move(patch.opens);
        merged.highs = std::move(patch.highs);
        merged.lows = std::move(patch.lows);
        merged.closes = std::move(patch.closes);
        merged.volumes = std::move(patch.volumes);
        // Chart refreshes own the range-specific previous-close reference but
        // must never replace the authoritative regular-session previous close.
        merged.chartPreviousClose = patch.chartPreviousClose;
        if (patch.hasRegularMarketVolume) {
            merged.regularMarketVolume = patch.regularMarketVolume;
            merged.hasRegularMarketVolume = true;
        }
        if (merged.currentPrice <= 0.0 && patch.currentPrice > 0.0) {
            merged.currentPrice = patch.currentPrice;
            if (patch.quoteTimestamp > 0)
                merged.quoteTimestamp = patch.quoteTimestamp;
        }
        merged.success = !merged.closes.empty() || merged.currentPrice > 0.0;
        merged.errorMessage.clear();
    } else if (kind == FetchKind::LiveQuote || kind == FetchKind::AlertQuote) {
        (void)ApplyStockQuotePatch(merged, patch, timeRangeIndex);
    } else if (kind == FetchKind::Details) {
        if (patch.instrumentNature != squarestar::market::InstrumentNature::Unknown)
            merged.instrumentNature = patch.instrumentNature;
        if ((patch.resolvedDetailMask & StockFetchMetrics) ||
            HasAnyMarketMetricData(patch)) {
            if (patch.peRatio > 0.0)
                merged.peRatio = patch.peRatio;
            if (patch.avgVolume > 0.0)
                merged.avgVolume = patch.avgVolume;
            if (patch.fiftyTwoWeekHigh > 0.0)
                merged.fiftyTwoWeekHigh = patch.fiftyTwoWeekHigh;
            if (patch.fiftyTwoWeekLow > 0.0)
                merged.fiftyTwoWeekLow = patch.fiftyTwoWeekLow;
            if (patch.marketCap > 0.0)
                merged.marketCap = patch.marketCap;
            if (HasNonZeroMetricValue(patch.dividendYield))
                merged.dividendYield = patch.dividendYield;
            if (HasNonZeroMetricValue(patch.beta))
                merged.beta = patch.beta;
            if (patch.hasFiftyTwoWkChangePercent) {
                merged.fiftyTwoWkChangePercent = patch.fiftyTwoWkChangePercent;
                merged.hasFiftyTwoWkChangePercent = true;
            }
        }
        if (patch.resolvedDetailMask & StockFetchProfile) {
            if (HasResolvedCompanyName(patch.companyName))
                merged.companyName = std::move(patch.companyName);
            if (!patch.industry.empty() && patch.industry != "N/A")
                merged.industry = std::move(patch.industry);
            if (!patch.weburl.empty() && patch.weburl != "N/A")
                merged.weburl = std::move(patch.weburl);
        }
        if (patch.resolvedDetailMask & StockFetchNews)
            merged.news = std::move(patch.news);
        merged.resolvedDetailMask |= patch.resolvedDetailMask;
        merged.errorMessage.clear();
    }

    assert(squarestar::market::StockDataInvariantsHold(merged));
    return merged;
}

} // namespace squarestar::application
