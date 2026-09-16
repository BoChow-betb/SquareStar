#pragma once

#include <algorithm>
#include <array>
#include <utility>

#include "application/stock_context.hpp"

namespace squarestar::application {

constexpr int DetailRetryDelaySeconds(int retryAttempts, bool rateLimited) noexcept {
    constexpr std::array<int, 5> retryDelaySeconds = {2, 5, 15, 30, 60};
    const std::size_t retryIndex = std::min<std::size_t>(
        static_cast<std::size_t>(std::max(retryAttempts, 1) - 1),
        retryDelaySeconds.size() - 1);
    return std::max(retryDelaySeconds[retryIndex], rateLimited ? 60 : 0);
}

template <typename StartRefresh, typename OnBusy>
FetchStartResult RequestManualStockRefreshIntent(StockContext& context,
                                                 StartRefresh&& startRefresh,
                                                 OnBusy&& onBusy) {
    (void)context.requests.tracker.Request(StockRequestChannel::Refresh);
    const FetchStartResult result = std::forward<StartRefresh>(startRefresh)();
    if (result == FetchStartResult::Busy) {
        std::forward<OnBusy>(onBusy)();
    }
    return result;
}

template <typename TriggerChart, typename TriggerFull>
void PumpQueuedStockRequest(StockContext& context,
                            TriggerChart&& triggerChart,
                            TriggerFull&& triggerFull) {
    if (context.requests.pendingRequest.valid())
        return;
    if (context.RawData().success &&
        context.navigation.selectedTimeRangeIndex != context.navigation.displayedTimeRangeIndex) {
        std::forward<TriggerChart>(triggerChart)();
        return;
    }
    const StockRequestGeneration& refreshGeneration =
        context.requests.tracker.State(StockRequestChannel::Refresh);
    if (!refreshGeneration.IsSettled())
        std::forward<TriggerFull>(triggerFull)();
}

}
