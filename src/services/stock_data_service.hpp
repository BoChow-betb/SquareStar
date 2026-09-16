#pragma once

#include <cstdint>
#include <future>
#include <string>
#include <string_view>

#include "domain/chart_ranges.hpp"
#include "domain/market_symbol.hpp"
#include "domain/stock_data.hpp"

namespace squarestar::marketdata {

enum class StockFetchInputError {
    None,
    InvalidTickerOrRange,
    UnsupportedMarket,
};

inline StockFetchInputError ValidateStockFetchInputs(
    std::string_view ticker,
    int timeRangeIndex,
    squarestar::market::FetchKind) {
    const auto symbol = squarestar::market::MarketSymbol::Parse(ticker);
    if (!symbol || timeRangeIndex < 0 ||
        timeRangeIndex >= squarestar::market::TIME_RANGE_COUNT) {
        return StockFetchInputError::InvalidTickerOrRange;
    }
    if (!symbol->HasSupportedUsClassSuffix())
        return StockFetchInputError::UnsupportedMarket;
    return StockFetchInputError::None;
}

void ClearStockMemoryCache();

std::future<squarestar::market::StockFetchResult> QueueAlertQuoteRequest(
    const std::string& ticker);
void ShutdownAlertQuoteBatchScheduler() noexcept;
void ResetAlertQuoteBatchScheduler() noexcept;

squarestar::market::StockFetchResult FetchStockData(
    const std::string& ticker,
    int timeRangeIndex,
    bool isBackground,
    bool tolerateOpeningNoChart,
    std::uint32_t detailMask,
    squarestar::market::FetchKind kind = squarestar::market::FetchKind::Full);

}
