#include "application/monitor_stock_policy.hpp"

#include "application/app_limits.hpp"
#include "application/app_market_data.hpp"

#include <algorithm>

namespace squarestar::application {

std::size_t CountMonitorStockTiles(const AppMarketData& marketData) noexcept {
    const std::size_t eligible = static_cast<std::size_t>(std::count_if(
        marketData.activeContexts.begin(), marketData.activeContexts.end(), [](const auto& context) {
            return context && context->navigation.open && !context->navigation.monitorExcluded;
        }));
    return std::min(eligible, kMaxMonitorStockTiles);
}

} // namespace squarestar::application
