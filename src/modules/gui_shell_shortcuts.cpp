#include "modules/gui_shell.hpp"
#include "modules/gui_shell_internal.hpp"
#include "modules/interface_modals.hpp"
#include "modules/core.hpp"
#include "modules/ui_focus.hpp"
#include "modules/platform.hpp"
#include "modules/market_data.hpp"
#include "modules/views.hpp"
#include "modules/settings_view.hpp"
#include "modules/terminal_stock_windows.hpp"
#include "modules/stock_surface_feedback.hpp"
#include "modules/window_chrome.hpp"

#include "services/config_save_queue.hpp"
#include "application/app_limits.hpp"
#include "application/contextual_keybind_policy.hpp"
#include "application/key_bindings.hpp"
#include "application/main_loop_signal.hpp"
#include "application/navigation_state.hpp"
#include "application/screener_controller.hpp"
#include "application/stock_request_state.hpp"
#include "domain/market_symbol.hpp"
#include "domain/chart_ranges.hpp"

#include <vector>
namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::UiRounding;
using squarestar::application::AppState;
using squarestar::application::FindStockModeNoticeTarget;
using squarestar::application::CountMonitorStockTiles;
using squarestar::application::StockContext;
using squarestar::application::TerminalAction;
using squarestar::application::IsLightGuiTheme;
using squarestar::application::UserFeedbackType;
using squarestar::market::TIME_RANGES;
using squarestar::presentation::ChartExportMethod;
using squarestar::presentation::ChartVisualType;


void ExitPureMonitorMode(AppState& state) {
    if (!state.navigation.pureMonitorMode)
        return;
    PlayUISound("transition.wav", state);
    state.navigation.pureMonitorMode = false;
    state.render.notifications.monitorModeHint.presentation.until = {};
    state.render.notifications.monitorModeHint.dismissed = false;
    for (auto& monitorCtx : state.marketData.activeContexts) {
        if (!monitorCtx)
            continue;
        monitorCtx->navigation.upperTabIndex =
            std::clamp(monitorCtx->navigation.preMonitorUpperTabIndex, 0, 3);
        monitorCtx->navigation.nextUpperTab = -1;
        monitorCtx->render.tabFadeAnim = 1.0f;
    }
    if (state.marketData.activeContexts.empty()) {
        state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Home;
    } else {
        state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Stock;
        for (auto& context : state.marketData.activeContexts) {
            if (context && context->navigation.ticker == state.navigation.lastActiveTab) {
                context->navigation.justOpened = true;
                break;
            }
        }
    }
    CloseAllAnimatedFloatingMenus();
    RequestGuiRedraw();
}

void HandleTerminalKeyboardShortcuts(AppState& state, GLFWwindow* window) {
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        if (IsActionPressed(state.config, TerminalAction::ToggleGuiMode)) {
            PlayUISound("transition.wav", state);
            RequestLiteGuiMode();
            return;
        }
        if (IsActionPressed(state.config, TerminalAction::ToggleSidebar) &&
            !state.navigation.pureMonitorMode) {
            state.config.sidebarHidden = !state.config.sidebarHidden;
            if (!state.config.sidebarHidden)
                state.render.navigationTransition = 0.0f;
            state.navigation.isSidebarHovered = false;
            state.render.sidebarAnim = state.config.theme.sidebarCollapsed;
            PlayUISound(state.config.sidebarHidden ? "off.wav" : "on.wav", state);
        }
        if (IsActionPressed(state.config, TerminalAction::ToggleFullscreen)) {
            ToggleApplicationFullscreen(window, state);
            PlayUISound("click.wav", state);
        }
        auto normalMode = [&] {
            if (state.navigation.pureMonitorMode) {
                state.navigation.pureMonitorMode = false;
            }
        };
        auto navPage = [&](TerminalAction a, squarestar::application::SidebarTab page) {
            if (!IsActionPressed(state.config, a))
                return false;
            normalMode();
            state.navigation.activeSidebarTab = page;
            PlayUISound("transition.wav", state);
            return true;
        };
        auto focusActiveStockSearch = [&] {
            auto active = std::find_if(
                state.marketData.activeContexts.begin(), state.marketData.activeContexts.end(), [&](const auto& context) {
                    return context && context->navigation.ticker == state.navigation.lastActiveTab;
                });
            if (active == state.marketData.activeContexts.end()) {
                active = std::find_if(state.marketData.activeContexts.begin(),
                                      state.marketData.activeContexts.end(),
                                      [](const auto& context) { return context != nullptr; });
            }
            if (active == state.marketData.activeContexts.end())
                return false;
            StockContext* context = active->get();
            state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Stock;
            state.navigation.lastActiveTab = context->navigation.ticker;
            context->navigation.justOpened = true;
            context->navigation.headerSearch.focusRequested = true;
            return true;
        };
        auto focusMainSearch = [&] {
            state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Home;
            state.navigation.mainSearch.focusRequested = true;
        };
        auto cycleStockTab = [&](int direction) {
            std::vector<StockContext*> contexts;
            contexts.reserve(state.marketData.activeContexts.size());
            for (const auto& context : state.marketData.activeContexts) {
                if (context)
                    contexts.push_back(context.get());
            }
            if (contexts.size() <= 1)
                return;
            auto active = std::find_if(contexts.begin(), contexts.end(), [&](const StockContext* context) {
                return context->navigation.ticker == state.navigation.lastActiveTab;
            });
            if (direction < 0) {
                if (active == contexts.begin() || active == contexts.end())
                    active = contexts.end();
                --active;
            } else {
                if (active != contexts.end())
                    ++active;
                if (active == contexts.end())
                    active = contexts.begin();
            }
            state.navigation.lastActiveTab = (*active)->navigation.ticker;
            (*active)->navigation.justOpened = true;
            PlayUISound("transition.wav", state);
        };
        navPage(TerminalAction::OpenHome, squarestar::application::SidebarTab::Home);
        navPage(TerminalAction::OpenOverview, squarestar::application::SidebarTab::Overview);
        if (IsActionPressed(state.config, TerminalAction::OpenTerminal)) {
            const auto firstContext = std::find_if(state.marketData.activeContexts.begin(),
                                                   state.marketData.activeContexts.end(),
                                                   [](const auto& context) { return context != nullptr; });
            if (firstContext != state.marketData.activeContexts.end()) {
                normalMode();
                state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Stock;
                if (state.navigation.lastActiveTab.empty())
                    state.navigation.lastActiveTab = (*firstContext)->navigation.ticker;
                for (auto& context : state.marketData.activeContexts) {
                    if (context && context->navigation.ticker == state.navigation.lastActiveTab) {
                        context->navigation.justOpened = true;
                        break;
                    }
                }
                PlayUISound("transition.wav", state);
            }
        }
        if (IsActionPressed(state.config, TerminalAction::FocusSearch)) {
            // Settings owns keyboard focus for controls and keybind editing. Do
            // not let the global search shortcut surface a hidden stock search
            // window on top of it.
            if (state.navigation.activeSidebarTab != squarestar::application::SidebarTab::Settings) {
                normalMode();
                if (state.navigation.activeSidebarTab != squarestar::application::SidebarTab::Stock ||
                    !focusActiveStockSearch())
                    focusMainSearch();
                PlayUISound("click.wav", state);
            }
        }
        if (IsActionPressed(state.config, TerminalAction::StockTabPrev))
            cycleStockTab(-1);
        if (IsActionPressed(state.config, TerminalAction::StockTabNext))
            cycleStockTab(1);
        if (IsActionPressed(state.config, TerminalAction::TogglePureMonitorMode) ||
            state.navigation.monitorModeButtonRequested) {
            state.navigation.monitorModeButtonRequested = false;
            state.render.monitorModeTimer = 0.0f;
            if (!state.navigation.pureMonitorMode) {
                std::size_t openMonitorTabs = 0;
                std::size_t includedMonitorTabs = 0;
                for (auto& monitorCtx : state.marketData.activeContexts) {
                    if (!monitorCtx || !monitorCtx->navigation.open)
                        continue;
                    ++openMonitorTabs;
                    monitorCtx->navigation.monitorExcluded =
                        includedMonitorTabs >= squarestar::application::kMaxMonitorStockTiles;
                    if (!monitorCtx->navigation.monitorExcluded)
                        ++includedMonitorTabs;
                }
                if (includedMonitorTabs < squarestar::application::kMinMonitorStockTiles) {
                    if (StockContext* noticeTarget = FindStockModeNoticeTarget(state)) {
                        ShowStockModeNotice(state,
                                            *noticeTarget,
                                            "Monitor needs another tab",
                                            "Open one more stock tab to use Monitor mode.");
                    }
                } else {
                    PlayUISound("transition.wav", state);
                    state.navigation.pureMonitorMode = true;
                    state.render.notifications.monitorModeHint.dismissed = false;
                    if (openMonitorTabs > squarestar::application::kMaxMonitorStockTiles) {
                        PublishUserFeedback(
                            state,
                            UserFeedbackType::Information,
                            "Monitor shows 9 tabs",
                            std::to_string(openMonitorTabs -
                                           squarestar::application::kMaxMonitorStockTiles) +
                                " additional tab(s) are available from the right-click picker.");
                    }
                    if (state.config.monitorModeHintDisabled)
                        state.render.notifications.monitorModeHint.presentation.until = {};
                    else
                        state.render.notifications.monitorModeHint.presentation.until =
                            std::chrono::steady_clock::now() + std::chrono::seconds(4);
                    CloseAllAnimatedFloatingMenus();
                    state.navigation.watchlistOpen = false;
                    state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Stock;
                    for (auto& monitorCtx : state.marketData.activeContexts) {
                        if (!monitorCtx || monitorCtx->navigation.monitorExcluded)
                            continue;
                        monitorCtx->navigation.comparisonPickerRequested = false;
                        monitorCtx->navigation.preMonitorUpperTabIndex = monitorCtx->navigation.upperTabIndex;
                        monitorCtx->navigation.upperTabIndex = 0;
                        monitorCtx->navigation.nextUpperTab = -1;
                        monitorCtx->render.tabFadeAnim = 1.0f;
                    }
                }
            } else {
                ExitPureMonitorMode(state);
            }
        }
        if (IsActionPressed(state.config, TerminalAction::CloseTab)) {
            if (!state.navigation.lastActiveTab.empty()) {
                for (auto& ctx : state.marketData.activeContexts) {
                    if (ctx->navigation.ticker == state.navigation.lastActiveTab) {
                        ctx->navigation.open = false;
                        PlayUISound("click.wav", state);
                        break;
                    }
                }
            }
        }
        if (IsActionPressed(state.config, TerminalAction::RefreshData)) {
            if (!state.navigation.lastActiveTab.empty()) {
                for (auto& ctx : state.marketData.activeContexts) {
                    if (ctx->navigation.ticker == state.navigation.lastActiveTab) {
                        RequestManualStockRefresh(state, *ctx);
                        break;
                    }
                }
            }
        }
    }
}


} // namespace squarestar::shell
