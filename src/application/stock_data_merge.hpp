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


[[nodiscard]] bool ApplyStockQuotePatch(
    squarestar::market::StockData& current,
    const squarestar::market::StockData& patch,
    int timeRangeIndex);


[[nodiscard]] squarestar::market::StockData MergeStockFetchPatch(
    const squarestar::market::StockData& current,
    squarestar::market::StockData patch,
    squarestar::market::FetchKind kind,
    int timeRangeIndex);

}
