#pragma once

#include <cstddef>

namespace squarestar::application {

struct AppMarketData;

std::size_t CountMonitorStockTiles(const AppMarketData& marketData) noexcept;


[[nodiscard]] constexpr bool ShouldHoldMonitorModeHint(bool pureMonitorMode,
                                                       bool needsMoreTabs,
                                                       bool permanentlyDisabled,
                                                       bool dismissedThisSession,
                                                       bool timedIntroductionActive) noexcept {
    return pureMonitorMode && !permanentlyDisabled && !dismissedThisSession &&
           (needsMoreTabs || timedIntroductionActive);
}

}
