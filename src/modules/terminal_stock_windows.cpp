#include "modules/terminal_stock_windows.hpp"

#include "application/app_limits.hpp"
#include "application/app_state.hpp"
#include "application/main_loop_signal.hpp"
#include "application/monitor_stock_policy.hpp"
#include "application/price_alert_policy.hpp"
#include "application/runtime_decisions.hpp"
#include "application/stock_context.hpp"
#include "application/theme_profiles.hpp"
#include "application/ui_animation.hpp"
#include "domain/trading_status.hpp"
#include "modules/charts.hpp"
#include "modules/core.hpp"
#include "modules/integrated_search_bar.hpp"
#include "modules/market_data.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace squarestar::shell {

using squarestar::application::AppState;
using squarestar::application::ComputeMonitorTileRect;
using squarestar::application::CountMonitorStockTiles;
using squarestar::application::IsLightGuiTheme;
using squarestar::application::IsStockPriceAlertActive;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::StockContext;
using squarestar::market::HasInactiveTradingStatus;

static int PushStockTabStyle(AppState& state,
                             const StockContext& ctx,
                             bool activeTab) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          activeTab ? ImGui::GetStyle().Colors[ImGuiCol_Text]
                                    : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    int colorCount = 1;
    if (IsStockPriceAlertActive(state.alerts, ctx)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.dangerText));
        ImGui::PushStyleColor(ImGuiCol_Tab, ThemeVec(state.config.theme.danger));
        ImGui::PushStyleColor(ImGuiCol_TabHovered, ThemeVec(state.config.theme.dangerHover));
        ImGui::PushStyleColor(ImGuiCol_TabSelected, ThemeVec(state.config.theme.danger));
        ImGui::PushStyleColor(ImGuiCol_TabDimmed, ThemeVec(state.config.theme.danger, 0.86f));
        ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected,
                              ThemeVec(state.config.theme.dangerActive));
        return colorCount + 6;
    }
    if (!HasInactiveTradingStatus(ctx.RawData().tradingStatus))
        return colorCount;

    const bool light = IsLightGuiTheme(state.config.themeModeIndex);
    const ImVec4 inactiveTab = light ? ImVec4(0.28f, 0.28f, 0.30f, 1.0f)
                                     : ImVec4(0.74f, 0.74f, 0.77f, 1.0f);
    const ImVec4 inactiveHover = light ? ImVec4(0.36f, 0.36f, 0.39f, 1.0f)
                                       : ImVec4(0.84f, 0.84f, 0.87f, 1.0f);
    const ImVec4 inactiveText = light ? ImVec4(0.96f, 0.96f, 0.97f, 1.0f)
                                      : ImVec4(0.08f, 0.08f, 0.09f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, inactiveText);
    ImGui::PushStyleColor(ImGuiCol_Tab, inactiveTab);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, inactiveHover);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, inactiveHover);
    ImGui::PushStyleColor(ImGuiCol_TabDimmed, inactiveTab);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, inactiveHover);
    return colorCount + 6;
}

static void RenderStockTabBody(AppState& state, StockContext& ctx) {
    ctx.navigation.refreshSurfaceVisible = true;
    SearchBarOptions searchBarOptions;
    searchBarOptions.notificationCenterVisibleCardLimit = 3;
    RenderIntegratedSearchBar(state,
                              ctx.navigation.headerSearch,
                              ctx.navigation.ticker,
                              0.0f,
                              [&](const std::string& symbol) {
                                  PlayUISound("click.wav", state);
                                  OpenStock(state, symbol);
                                  ctx.navigation.headerSearch.inputBuffer[0] = '\0';
                                  ctx.navigation.headerSearch.lastQuery.clear();
                                  ctx.navigation.headerSearch.results.clear();
                              },
                              searchBarOptions);
    ImGui::Separator();
    RenderSingleStockWindowContent(state, ctx);
}

void PruneClosedTerminalStocks(AppState& state) {
    for (auto& context : state.marketData.activeContexts) {
        if (context)
            context->navigation.refreshSurfaceVisible = false;
    }

    for (auto it = state.marketData.activeContexts.begin();
         it != state.marketData.activeContexts.end();) {
        if (!*it || !(*it)->navigation.open) {
            if (*it)
                SilencePriceAlertForClosedTab(state, **it);
            it = state.marketData.activeContexts.erase(it);
        } else {
            ++it;
        }
    }

    const auto active = std::find_if(
        state.marketData.activeContexts.begin(),
        state.marketData.activeContexts.end(),
        [&](const auto& context) {
            return context && context->navigation.ticker == state.navigation.lastActiveTab;
        });
    if (active == state.marketData.activeContexts.end()) {
        if (!state.marketData.activeContexts.empty() && state.marketData.activeContexts.front())
            state.navigation.lastActiveTab = state.marketData.activeContexts.front()->navigation.ticker;
        else
            state.navigation.lastActiveTab.clear();
    }
}

void RenderTerminalStockTabs(AppState& state) {
    if (state.marketData.activeContexts.empty())
        return;

    std::vector<StockContext*> contexts;
    contexts.reserve(state.marketData.activeContexts.size());
    for (const auto& context : state.marketData.activeContexts) {
        if (context && context->navigation.open)
            contexts.push_back(context.get());
    }
    if (contexts.empty())
        return;

    if (state.config.stockTabBarHidden) {
        StockContext* active = nullptr;
        for (StockContext* context : contexts) {
            if (context->navigation.justOpened)
                active = context;
        }
        if (!active) {
            const auto selected = std::find_if(
                contexts.begin(), contexts.end(), [&](const StockContext* context) {
                    return context->navigation.ticker == state.navigation.lastActiveTab;
                });
            active = selected != contexts.end() ? *selected : contexts.front();
        }
        state.navigation.lastActiveTab = active->navigation.ticker;
        for (StockContext* context : contexts) {
            context->navigation.justOpened = false;
            if (context != active)
                context->navigation.headerSearch.ResetUi();
        }
        RenderStockTabBody(state, *active);
        return;
    }

    const ImVec4 tabActive = ThemeVec(state.config.theme.tabActive);
    const ImVec4 tabInactive = ThemeVec(state.config.theme.tabInactive);
    const ImVec4 tabHovered = ThemeVec(state.config.theme.tabHovered);
    ImGui::PushStyleColor(ImGuiCol_Tab, tabInactive);
    ImGui::PushStyleColor(ImGuiCol_TabDimmed, tabInactive);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, tabActive);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, tabActive);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, tabHovered);

    constexpr ImGuiTabBarFlags tabBarFlags =
        ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll;
    if (ImGui::BeginTabBar("StockTabs", tabBarFlags)) {
        for (StockContext* context : contexts) {
            bool open = context->navigation.open;
            const bool selectTab = context->navigation.justOpened;
            const bool wasActive = state.navigation.lastActiveTab == context->navigation.ticker;
            const int colorCount = PushStockTabStyle(state, *context, wasActive || selectTab);
            const ImGuiTabItemFlags tabFlags =
                selectTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            const bool visible =
                ImGui::BeginTabItem(context->navigation.ticker, &open, tabFlags);
            const bool leftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            ImGui::PopStyleColor(colorCount);
            context->navigation.justOpened = false;

            if (rightClicked)
                open = false;
            if (visible) {
                if (state.navigation.lastActiveTab != context->navigation.ticker) {
                    state.navigation.lastActiveTab = context->navigation.ticker;
                    if (leftClicked)
                        PlayUISound("click.wav", state);
                }
                RenderStockTabBody(state, *context);
                ImGui::EndTabItem();
            } else {
                context->navigation.headerSearch.ResetUi();
            }

            if (context->navigation.open && !open) {
                context->navigation.open = false;
                PlayUISound("click.wav", state);
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleColor(5);
}

static void UpdateMonitorWindowOpenTransition(AppState& state, StockContext& ctx) {
    if (state.UiAnimationsEnabled()) {
        ctx.render.openTransitionProgress =
            std::min(1.0f, ctx.render.openTransitionProgress + UiFrameDelta() / 0.28f);
    } else {
        ctx.render.openTransitionProgress = 1.0f;
    }
    if (state.UiAnimationsEnabled() && ctx.render.openTransitionProgress < 0.999f)
        RequestGuiRedraw();
    if (ctx.render.openTransitionProgress < 1.0f) {
        const float alpha = 1.0f -
                            (1.0f - ctx.render.openTransitionProgress) *
                                (1.0f - ctx.render.openTransitionProgress);
        ImGui::SetNextWindowBgAlpha(alpha);
    }
}

static void ConfigureMonitorTileWindow(AppState& state,
                                       const ImVec2& workPos,
                                       const ImVec2& workSize,
                                       int monitorStockIndex,
                                       int monitorStockCount,
                                       const std::string& windowTitle) {
    const auto tile = ComputeMonitorTileRect(workPos.x,
                                             workPos.y,
                                             workSize.x,
                                             workSize.y,
                                             monitorStockIndex,
                                             monitorStockCount);
    const ImVec2 targetPos(tile.minX, tile.minY);
    const ImVec2 targetSize(std::max(1.0f, tile.maxX - tile.minX),
                            std::max(1.0f, tile.maxY - tile.minY));
    if (state.UiAnimationsEnabled() && state.render.monitorModeTimer < 0.24f) {
        if (ImGuiWindow* window = ImGui::FindWindowByName(windowTitle.c_str())) {
            const float response = 1.0f - std::exp(-24.0f * UiFrameDelta());
            const ImVec2 nextPos(window->Pos.x + (targetPos.x - window->Pos.x) * response,
                                 window->Pos.y + (targetPos.y - window->Pos.y) * response);
            const ImVec2 nextSize(window->Size.x + (targetSize.x - window->Size.x) * response,
                                  window->Size.y + (targetSize.y - window->Size.y) * response);
            ImGui::SetNextWindowPos(nextPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(nextSize, ImGuiCond_Always);
            return;
        }
    }
    ImGui::SetNextWindowPos(targetPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(targetSize, ImGuiCond_Always);
}

void RenderMonitorStockWindows(AppState& state,
                               const ImVec2& workPos,
                               const ImVec2& workSize) {
    if (!state.navigation.pureMonitorMode)
        return;

    state.render.monitorModeTimer += state.UiAnimationsEnabled() ? UiFrameDelta() : 1.0f;
    if (state.UiAnimationsEnabled() && state.render.monitorModeTimer < 0.24f)
        RequestGuiRedraw();

    const int monitorStockCount =
        std::max(1, static_cast<int>(CountMonitorStockTiles(state.marketData)));
    int monitorStockIndex = 0;
    int monitorEligibleIndex = 0;
    for (const auto& context : state.marketData.activeContexts) {
        if (!context || !context->navigation.open)
            continue;
        StockContext& ctx = *context;
        ctx.navigation.refreshSurfaceVisible = false;
        const bool monitorEligible = !ctx.navigation.monitorExcluded;
        const bool monitorOverflow =
            monitorEligible &&
            monitorEligibleIndex >= static_cast<int>(squarestar::application::kMaxMonitorStockTiles);
        if (monitorEligible)
            ++monitorEligibleIndex;
        if (!monitorEligible || monitorOverflow) {
            ctx.navigation.headerSearch.ResetUi();
            continue;
        }

        ctx.navigation.justOpened = false;
        ctx.navigation.refreshSurfaceVisible = true;
        const std::string windowTitle =
            std::string(ctx.navigation.ticker) + "###Monitor_" + ctx.navigation.ticker;
        UpdateMonitorWindowOpenTransition(state, ctx);
        ConfigureMonitorTileWindow(
            state, workPos, workSize, monitorStockIndex, monitorStockCount, windowTitle);

        const bool denseMonitorGrid = monitorStockCount >= 9;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            denseMonitorGrid ? ImVec2(12.0f, 10.0f)
                                             : ImVec2(16.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ThemeVec(state.config.theme.monitorBg));
        const ImGuiWindowFlags stockFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin(windowTitle.c_str(), nullptr, stockFlags)) {
            ctx.navigation.headerSearch.ResetUi();
            RenderSingleStockWindowContent(state, ctx);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
        ++monitorStockIndex;
    }
}

} // namespace squarestar::shell
