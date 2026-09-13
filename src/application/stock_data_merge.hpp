#pragma once

#include <cstdint>
#include <string>

#include "domain/stock_data.hpp"

namespace squarestar::application {

enum StockFetchDetail : std::uint32_t {
    StockFetchMetrics = 1u << 0,
    StockFetchProfile = 1u << 1,
    StockFetchNews = 1u << 2,
    StockFetchAll = StockFetchMetrics | StockFetchProfile | StockFetchNews,
};

[[nodiscard]] bool HasResolvedCompanyName(const std::string& companyName) noexcept;
[[nodiscard]] bool HasAnyMarketMetricData(
    const squarestar::market::StockData& data) noexcept;

// Applies the scalar quote fields and optional current-candle tail update in place.
// This is used by StockMarketDataState so a quote refresh stays O(1) when no
// immutable snapshot is retained by a reader.
[[nodiscard]] bool ApplyStockQuotePatch(
    squarestar::market::StockData& current,
    const squarestar::market::StockData& patch,
    int timeRangeIndex);

// Returns a new immutable raw-market snapshot. The published source object is
// never modified in place, so readers cannot observe a partially merged patch.
[[nodiscard]] squarestar::market::StockData MergeStockFetchPatch(
    const squarestar::market::StockData& current,
    squarestar::market::StockData patch,
    squarestar::market::FetchKind kind,
    int timeRangeIndex);

} // namespace squarestar::application
