#pragma once

#include <cstddef>

namespace squarestar::application {

struct AppMarketData;

std::size_t CountMonitorStockTiles(const AppMarketData& marketData) noexcept;

// One policy owns every render/wake decision for the monitor reminder. A
// session dismissal suppresses both the timed introduction and the "needs
// tabs" variant until Monitor mode is entered again.
[[nodiscard]] constexpr bool ShouldHoldMonitorModeHint(bool pureMonitorMode,
                                                       bool needsMoreTabs,
                                                       bool permanentlyDisabled,
                                                       bool dismissedThisSession,
                                                       bool timedIntroductionActive) noexcept {
    return pureMonitorMode && !permanentlyDisabled && !dismissedThisSession &&
           (needsMoreTabs || timedIntroductionActive);
}

} // namespace squarestar::application
