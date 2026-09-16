#pragma once

#include <optional>
#include <string_view>

#include "domain/stock_data.hpp"

namespace squarestar::marketdata {

std::optional<squarestar::market::StockFetchResult> ConsumeStockMemoryCache(
    std::string_view key,
    bool activeSession);
void StoreStockMemoryCache(
    std::string_view key,
    const squarestar::market::StockFetchResult& result);

}
