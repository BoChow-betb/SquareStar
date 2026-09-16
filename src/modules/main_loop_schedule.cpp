#include "modules/main_loop_schedule.hpp"

#include "application/app_state.hpp"
#include "application/frame_rate.hpp"
#include "application/main_loop_signal.hpp"
#include "modules/app_services.hpp"
#include "modules/core.hpp"
#include "services/config_save_queue.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace squarestar::shell {

using squarestar::application::AppState;
using squarestar::application::GuiPageKind;
using squarestar::application::SecondsUntilGuiWakeDeadline;
using squarestar::application::SecondsUntilNextWallClockSecond;

GuiPageKind CurrentGuiPage(const AppState& state) {
    if (state.navigation.liteGuiActive)
        return state.marketData.activeContexts.empty() ? GuiPageKind::Home
                                                       : GuiPageKind::Stock;
    if (state.navigation.activeSidebarTab == squarestar::application::SidebarTab::Home)
        return GuiPageKind::Home;
    if (state.navigation.activeSidebarTab == squarestar::application::SidebarTab::Overview)
        return GuiPageKind::Overview;
    if (state.navigation.activeSidebarTab == squarestar::application::SidebarTab::Settings)
        return GuiPageKind::Settings;
    for (const auto& ctx : state.marketData.activeContexts) {
        if (ctx && ctx->navigation.open && ctx->navigation.refreshSurfaceVisible)
            return ctx->navigation.upperTabIndex == 3 ? GuiPageKind::Comparison
                                                      : GuiPageKind::Stock;
    }
    return GuiPageKind::Stock;
}

double GuiPeriodicRefreshSeconds(const AppState&, GuiPageKind page) {
    switch (page) {
    case GuiPageKind::Stock:
    case GuiPageKind::Comparison:
        return 1.0;
    default:
        return 30.0;
    }
}

bool GuiPageShowsSecondClock(GuiPageKind page) noexcept {
    return page == GuiPageKind::Stock ||
           page == GuiPageKind::Comparison;
}

double GuiSettledWaitSeconds(AppState& state,
                             double lastGuiRenderAt,
                             bool windowSuspended,
                             bool priceAlertAudioPending) {
    if (priceAlertAudioPending)
        return 0.05;
    double waitSeconds = SecondsUntilNextStockAutoRefresh(state, windowSuspended);
    const auto nowSteady = std::chrono::steady_clock::now();
    if (state.alerts.NextMarketOpenCheckAt() !=
        std::chrono::steady_clock::time_point{}) {
        waitSeconds = std::min(
            waitSeconds,
            std::max(0.01,
                     std::chrono::duration<double>(state.alerts.NextMarketOpenCheckAt() - nowSteady)
                         .count()));
    }
    waitSeconds = std::min(waitSeconds, SecondsUntilPendingTrayPriceMoveFlush());
    waitSeconds = std::min(waitSeconds, squarestar::config::SecondsUntilConfigSaveDue());
    if (!windowSuspended) {

        waitSeconds = std::min(waitSeconds, SecondsUntilGuiWakeDeadline());

        const GuiPageKind page = CurrentGuiPage(state);
        const double periodicSeconds = GuiPeriodicRefreshSeconds(state, page);
        const double periodicRemaining =
            periodicSeconds - (glfwGetTime() - lastGuiRenderAt);
        waitSeconds = std::min(waitSeconds, std::max(0.01, periodicRemaining));
        if (GuiPageShowsSecondClock(page)) {
            waitSeconds = std::min(
                waitSeconds,
                SecondsUntilNextWallClockSecond(std::chrono::system_clock::now()));
        }
        if (page == GuiPageKind::Overview) {
            const std::int64_t refreshAfter =
                state.requests.screenerRefreshAfterEpochSeconds.load(
                    std::memory_order_acquire);
            if (refreshAfter > 0) {
                const std::int64_t nowSeconds =
                    static_cast<std::int64_t>(std::time(nullptr));
                waitSeconds = std::min(
                    waitSeconds,
                    std::max(0.01,
                             static_cast<double>(refreshAfter - nowSeconds)));
            }
        }
    }
    return std::clamp(waitSeconds, 0.01, 30.0);
}

}
