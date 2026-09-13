#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <type_traits>
#include <vector>

#include "domain/trading_status_fwd.hpp"

namespace squarestar::market {

struct NewsItem {
    std::string headline, source, url, summary;
    std::time_t datetime = 0;
};

enum class InstrumentNature : uint8_t {
    Unknown,
    PublicMarketSecurity,
    DerivativeContract,
};

struct StockData {
    std::vector<double> timestamps, opens, highs, lows, closes, volumes;
    std::vector<NewsItem> news;
    // Authoritative previous regular-session close used by session-level metrics.
    double previousClose = 0;
    // Previous close for the displayed Yahoo chart range. Keep it separate from
    // previousClose so range changes do not alter session metrics.
    double chartPreviousClose = 0;
    double currentPrice = 0, openPrice = 0, dayHigh = 0, dayLow = 0;
    // Timestamp of the provider snapshot that actually supplied currentPrice.
    // The UI uses this for the neutral "As of" label without inferring feed quality.
    std::time_t quoteTimestamp = 0;
    double peRatio = 0, avgVolume = 0, fiftyTwoWeekHigh = 0, fiftyTwoWeekLow = 0;
    double marketCap = 0, dividendYield = 0, beta = 0;
    double regularMarketVolume = 0.0;
    double fiftyTwoWkChangePercent = 0.0;
    bool hasRegularMarketVolume = false;
    bool hasFiftyTwoWkChangePercent = false;
    std::string companyName = "Fetching...";
    std::string exchange;
    std::string currency = "USD";
    std::string industry = "N/A";
    std::string weburl = "N/A";
    std::string errorMessage;
    bool success = false;
    uint32_t resolvedDetailMask = 0;
    InstrumentNature instrumentNature = InstrumentNature::Unknown;
    TradingStatus tradingStatus;
};

enum class StockDataInvariantError : uint8_t {
    None,
    ChartVectorSizeMismatch,
    InvalidTimestamp,
    NonIncreasingTimestamp,
    InvalidPrice,
    InvalidOhlcRange,
    InvalidVolume
};

inline StockDataInvariantError ValidateStockDataInvariants(const StockData& data) noexcept {
    const size_t count = data.timestamps.size();
    const bool hasAnyChartValues = count != 0 || !data.opens.empty() || !data.highs.empty() ||
                                   !data.lows.empty() || !data.closes.empty() ||
                                   !data.volumes.empty();
    if (!hasAnyChartValues)
        return StockDataInvariantError::None;
    if (data.opens.size() != count || data.highs.size() != count || data.lows.size() != count ||
        data.closes.size() != count || data.volumes.size() != count) {
        return StockDataInvariantError::ChartVectorSizeMismatch;
    }
    for (size_t i = 0; i < count; ++i) {
        const double timestamp = data.timestamps[i];
        const double open = data.opens[i];
        const double high = data.highs[i];
        const double low = data.lows[i];
        const double close = data.closes[i];
        const double volume = data.volumes[i];
        if (!std::isfinite(timestamp) || timestamp <= 0.0)
            return StockDataInvariantError::InvalidTimestamp;
        if (i != 0 && timestamp <= data.timestamps[i - 1])
            return StockDataInvariantError::NonIncreasingTimestamp;
        if (!std::isfinite(open) || !std::isfinite(high) || !std::isfinite(low) ||
            !std::isfinite(close) || open <= 0.0 || high <= 0.0 || low <= 0.0 ||
            close <= 0.0) {
            return StockDataInvariantError::InvalidPrice;
        }
        if (high < std::max(open, close) || low > std::min(open, close) || high < low)
            return StockDataInvariantError::InvalidOhlcRange;
        if (!std::isfinite(volume) || volume < 0.0)
            return StockDataInvariantError::InvalidVolume;
    }
    return StockDataInvariantError::None;
}

inline bool StockDataInvariantsHold(const StockData& data) noexcept {
    return ValidateStockDataInvariants(data) == StockDataInvariantError::None;
}

inline size_t ApproximateStockDataHeapBytes(const StockData& data) noexcept {
    size_t bytes = 0;
    const auto vectorBytes = [&](const auto& values) {
        using Value = typename std::decay_t<decltype(values)>::value_type;
        bytes += values.capacity() * sizeof(Value);
    };
    vectorBytes(data.timestamps);
    vectorBytes(data.opens);
    vectorBytes(data.highs);
    vectorBytes(data.lows);
    vectorBytes(data.closes);
    vectorBytes(data.volumes);
    vectorBytes(data.news);
    for (const NewsItem& item : data.news) {
        bytes += item.headline.capacity() + item.source.capacity() + item.url.capacity() +
                 item.summary.capacity();
    }
    bytes += data.companyName.capacity() + data.exchange.capacity() + data.currency.capacity() +
             data.industry.capacity() + data.weburl.capacity() + data.errorMessage.capacity() +
             data.tradingStatus.evidenceHeadline.capacity();
    return bytes;
}

enum class FetchKind : uint8_t {
    Full,
    Chart,
    LiveQuote,
    AlertQuote,
    Details
};

struct StockFetchResult {
    StockData marketData;
    std::string errorMessage;
    bool success = false;
    bool rateLimited = false;
    int resolvedTimeRangeIndex = -1;

    void FinalizeFromMarketData(bool wasRateLimited = false) {
        success = marketData.success;
        errorMessage = marketData.errorMessage;
        rateLimited = wasRateLimited;
    }
};

} // namespace squarestar::market
