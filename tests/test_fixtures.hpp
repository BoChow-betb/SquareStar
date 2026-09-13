#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "domain/screener_data.hpp"
#include "domain/stock_data.hpp"

namespace squarestar::test {

[[nodiscard]] squarestar::market::StockData BuildTestStockData(
    std::string_view symbol = "TEST",
    int variant = 0);
[[nodiscard]] std::vector<squarestar::market::ScreenerData> BuildTestScreenerData(
    std::size_t count = 24);

} // namespace squarestar::test
