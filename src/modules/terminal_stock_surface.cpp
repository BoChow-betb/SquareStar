#include "modules/terminal_stock_surface.hpp"

#include "application/app_limits.hpp"
#include "application/app_state.hpp"
#include "application/main_loop_signal.hpp"
#include "application/monitor_stock_policy.hpp"
#include "application/price_alert_policy.hpp"
#include "application/runtime_decisions.hpp"
#include "application/stock_context.hpp"
#include "application/stock_tab_policy.hpp"
#include "application/theme_profiles.hpp"
#include "application/ui_animation.hpp"
#include "domain/trading_status.hpp"
#include "modules/charts.hpp"
#include "modules/core.hpp"
#include "modules/integrated_search_bar.hpp"
#include "modules/market_data.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace squarestar::shell {

using squarestar::application::AppState;
using squarestar::application::ComputeMonitorTileRect;
using squarestar::application::CountMonitorStockTiles;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::StockContext;
using squarestar::application::UiRounding;

namespace {

StockContext* FindOpenStock(AppState& state, std::string_view ticker) {
    const auto found = std::find_if(
        state.marketData.activeContexts.begin(),
        state.marketData.activeContexts.end(),
        [&](const auto& context) {
            return context && context->navigation.open &&
                   context->navigation.ticker == ticker;
        });
    return found == state.marketData.activeContexts.end() ? nullptr : found->get();
}

StockContext* FirstOpenStock(AppState& state) {
    const auto found = std::find_if(
        state.marketData.activeContexts.begin(),
        state.marketData.activeContexts.end(),
        [](const auto& context) { return context && context->navigation.open; });
    return found == state.marketData.activeContexts.end() ? nullptr : found->get();
}

void RemoveClosedStocks(AppState& state) {
    for (auto it = state.marketData.activeContexts.begin();
         it != state.marketData.activeContexts.end();) {
        if (*it && !(*it)->navigation.open) {
            SilencePriceAlertForClosedTab(state, **it);
            it = state.marketData.activeContexts.erase(it);
        } else {
            ++it;
        }
    }

    if (state.marketData.activeContexts.empty()) {
        state.navigation.lastActiveTab.clear();
        if (state.navigation.activeSidebarTab ==
            squarestar::application::SidebarTab::Stock) {
            state.navigation.activeSidebarTab =
                squarestar::application::SidebarTab::Home;
        }
        return;
    }

    if (!FindOpenStock(state, state.navigation.lastActiveTab)) {
        if (StockContext* first = FirstOpenStock(state))
            state.navigation.lastActiveTab = first->navigation.ticker;
    }
}

void CloseStockTab(AppState& state, std::string_view ticker) {
    auto found = std::find_if(
        state.marketData.activeContexts.begin(),
        state.marketData.activeContexts.end(),
        [&](const auto& context) {
            return context && context->navigation.open &&
                   context->navigation.ticker == ticker;
        });
    if (found == state.marketData.activeContexts.end())
        return;

    const std::size_t closingIndex = static_cast<std::size_t>(
        std::distance(state.marketData.activeContexts.begin(), found));
    (*found)->navigation.open = false;

    if (state.navigation.lastActiveTab == ticker) {
        StockContext* replacement = nullptr;
        for (std::size_t offset = 1;
             offset < state.marketData.activeContexts.size();
             ++offset) {
            const std::size_t right = closingIndex + offset;
            if (right < state.marketData.activeContexts.size()) {
                const auto& candidate = state.marketData.activeContexts[right];
                if (candidate && candidate->navigation.open) {
                    replacement = candidate.get();
                    break;
                }
            }
            if (closingIndex >= offset) {
                const auto& candidate = state.marketData.activeContexts[closingIndex - offset];
                if (candidate && candidate->navigation.open) {
                    replacement = candidate.get();
                    break;
                }
            }
        }
        state.navigation.lastActiveTab =
            replacement ? replacement->navigation.ticker : "";
    }

    PlayUISound("click.wav", state);
    RemoveClosedStocks(state);
    RequestGuiRedraw();
}

void DrawCloseGlyph(ImDrawList* drawList,
                    ImVec2 center,
                    ImU32 color,
                    float halfSize = 3.6f) {
    drawList->AddLine(ImVec2(center.x - halfSize, center.y - halfSize),
                      ImVec2(center.x + halfSize, center.y + halfSize),
                      color,
                      1.35f);
    drawList->AddLine(ImVec2(center.x + halfSize, center.y - halfSize),
                      ImVec2(center.x - halfSize, center.y + halfSize),
                      color,
                      1.35f);
}

void RenderStockTabStrip(AppState& state) {
    std::array<StockContext*, squarestar::application::kMaxActiveStockTabs> tabs{};
    std::size_t tabCount = 0;
    for (const auto& context : state.marketData.activeContexts) {
        if (context && context->navigation.open && tabCount < tabs.size())
            tabs[tabCount++] = context.get();
    }
    if (tabCount == 0)
        return;

    const float gap = 4.0f;
    const float tabHeight = 32.0f;
    const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float fitted =
        (available - gap * static_cast<float>(tabCount - 1)) /
        static_cast<float>(tabCount);
    const float tabWidth = std::clamp(fitted, 58.0f, 132.0f);
    const float rounding = UiRounding(state, 7.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool lightPalette =
        squarestar::application::IsLightGuiTheme(state.config.themeModeIndex);
    // Inactive/delisted symbols get their own neutral semantic treatment.
    // Keep price-alert red as the highest-priority state, but otherwise make a
    // stopped-trading tab visibly different from an ordinary active symbol.
    const ImVec4 stoppedTabInactive =
        lightPalette ? ImVec4(0.80f, 0.84f, 0.88f, 1.0f)
                     : ImVec4(0.19f, 0.22f, 0.28f, 1.0f);
    const ImVec4 stoppedTabActive =
        lightPalette ? ImVec4(0.58f, 0.64f, 0.72f, 1.0f)
                     : ImVec4(0.28f, 0.33f, 0.41f, 1.0f);
    const ImVec4 stoppedTabHovered =
        lightPalette ? ImVec4(0.68f, 0.73f, 0.80f, 1.0f)
                     : ImVec4(0.32f, 0.38f, 0.46f, 1.0f);
    const ImVec4 stoppedTabText =
        lightPalette ? ImVec4(0.06f, 0.09f, 0.14f, 1.0f)
                     : ImVec4(0.94f, 0.96f, 0.98f, 1.0f);
    const ImVec4 stoppedTabBorder =
        lightPalette ? ImVec4(0.39f, 0.46f, 0.55f, 0.86f)
                     : ImVec4(0.39f, 0.46f, 0.55f, 0.90f);

    std::string closeTicker;
    for (std::size_t index = 0; index < tabCount; ++index) {
        StockContext& context = *tabs[index];
        const bool active =
            context.navigation.ticker == state.navigation.lastActiveTab;
        const bool priceAlertActive =
            squarestar::application::IsStockPriceAlertActive(state.alerts, context);
        const bool stoppedTrading =
            squarestar::market::HasInactiveTradingStatus(
                context.RawData().tradingStatus);
        const ImVec2 tabMin = ImGui::GetCursorScreenPos();
        const ImVec2 tabMax(tabMin.x + tabWidth, tabMin.y + tabHeight);

        ImGui::PushID(context.navigation.ticker);
        ImGui::InvisibleButton("##StockTab", ImVec2(tabWidth, tabHeight));
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool closeAllowed = active && tabWidth >= 82.0f;
        const float closeZone = closeAllowed ? 28.0f : 0.0f;
        const bool closeHovered =
            closeAllowed && hovered && ImGui::GetIO().MousePos.x >= tabMax.x - closeZone;

        const ImVec4 fill = priceAlertActive
                                ? ThemeVec(hovered ? state.config.theme.dangerHover
                                                   : active ? state.config.theme.dangerActive
                                                            : state.config.theme.danger)
                                : stoppedTrading
                                      ? (hovered ? stoppedTabHovered
                                                 : active ? stoppedTabActive
                                                          : stoppedTabInactive)
                                      : hovered
                                            ? ThemeVec(state.config.theme.tabHovered)
                                            : ThemeVec(active ? state.config.theme.tabActive
                                                              : state.config.theme.tabInactive);
        drawList->AddRectFilled(tabMin,
                                tabMax,
                                ImGui::ColorConvertFloat4ToU32(fill),
                                rounding);
        if (active) {
            drawList->AddRect(
                tabMin,
                tabMax,
                ImGui::ColorConvertFloat4ToU32(
                    priceAlertActive
                        ? ThemeVec(state.config.theme.dangerText, 0.72f)
                        : stoppedTrading
                              ? stoppedTabBorder
                              : ThemeVec(state.config.theme.floatingBorder, 0.72f)),
                rounding,
                0,
                1.0f);
        }

        const ImVec4 textColor = priceAlertActive
                                     ? ThemeVec(state.config.theme.dangerText)
                                     : stoppedTrading
                                           ? stoppedTabText
                                           : active
                                                 ? ThemeVec(state.config.theme.text)
                                                 : ThemeVec(state.config.theme.textDisabled,
                                                            0.92f);
        const ImVec2 textSize = ImGui::CalcTextSize(context.navigation.ticker);
        const ImVec2 textPos(tabMin.x + 10.0f,
                             tabMin.y + (tabHeight - textSize.y) * 0.5f);
        drawList->PushClipRect(
            ImVec2(tabMin.x + 8.0f, tabMin.y),
            ImVec2(tabMax.x - std::max(8.0f, closeZone), tabMax.y),
            true);
        drawList->AddText(textPos,
                          ImGui::ColorConvertFloat4ToU32(textColor),
                          context.navigation.ticker);
        drawList->PopClipRect();

        if (closeAllowed) {
            const ImVec2 closeCenter(tabMax.x - 14.0f,
                                     tabMin.y + tabHeight * 0.5f);
            const ImVec4 closeColor = priceAlertActive
                                          ? ThemeVec(state.config.theme.dangerText,
                                                     closeHovered ? 1.0f : 0.90f)
                                          : stoppedTrading
                                                ? ImVec4(stoppedTabText.x,
                                                         stoppedTabText.y,
                                                         stoppedTabText.z,
                                                         closeHovered ? 1.0f : 0.90f)
                                                : closeHovered
                                                      ? ThemeVec(state.config.theme.text)
                                                      : ThemeVec(
                                                            state.config.theme.textDisabled,
                                                            0.90f);
            DrawCloseGlyph(drawList,
                           closeCenter,
                           ImGui::ColorConvertFloat4ToU32(closeColor));
        }

        if (pressed) {
            if (closeHovered) {
                closeTicker = context.navigation.ticker;
            } else if (!active) {
                state.navigation.lastActiveTab = context.navigation.ticker;
                state.navigation.activeSidebarTab =
                    squarestar::application::SidebarTab::Stock;
                context.render.tabFadeAnim = 0.0f;
                context.render.openTransitionProgress = 0.0f;
                PlayUISound("click.wav", state);
                RequestGuiRedraw();
            }
        }
        ImGui::PopID();

        if (index + 1 < tabCount)
            ImGui::SameLine(0.0f, gap);
    }

    if (!closeTicker.empty())
        CloseStockTab(state, closeTicker);
}

void RenderActiveStock(AppState& state) {
    RemoveClosedStocks(state);
    StockContext* active = FindOpenStock(state, state.navigation.lastActiveTab);
    if (!active)
        active = FirstOpenStock(state);
    if (!active)
        return;

    state.navigation.lastActiveTab = active->navigation.ticker;
    active->navigation.refreshSurfaceVisible = true;
    active->navigation.justOpened = false;

    SearchBarOptions searchBarOptions;
    searchBarOptions.notificationCenterVisibleCardLimit = 3;
    RenderIntegratedSearchBar(
        state,
        active->navigation.headerSearch,
        active->navigation.ticker,
        0.0f,
        [&](const std::string& symbol) {
            PlayUISound("click.wav", state);
            OpenStock(state, symbol);
            active->navigation.headerSearch.ResetUi();
        },
        searchBarOptions);
    ImGui::Separator();
    RenderSingleStockWindowContent(state, *active);
}

void RenderMonitorStocks(AppState& state) {
    std::array<StockContext*, squarestar::application::kMaxMonitorStockTiles> tiles{};
    std::size_t tileCount = 0;
    for (const auto& context : state.marketData.activeContexts) {
        if (!context || !context->navigation.open ||
            context->navigation.monitorExcluded) {
            continue;
        }
        if (tileCount >= tiles.size())
            break;
        tiles[tileCount++] = context.get();
    }
    if (tileCount == 0)
        return;

    state.render.monitorModeTimer += state.UiAnimationsEnabled()
                                         ? UiFrameDelta()
                                         : 1.0f;
    if (state.UiAnimationsEnabled() && state.render.monitorModeTimer < 0.24f)
        RequestGuiRedraw();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float width = std::max(1.0f, available.x);
    const float height = std::max(1.0f, available.y);
    const int renderedTileCount = static_cast<int>(tileCount);

    for (int index = 0; index < renderedTileCount; ++index) {
        StockContext& context = *tiles[static_cast<std::size_t>(index)];
        context.navigation.refreshSurfaceVisible = true;
        context.navigation.justOpened = false;

        const auto rect = ComputeMonitorTileRect(origin.x,
                                                 origin.y,
                                                 width,
                                                 height,
                                                 index,
                                                 renderedTileCount);
        const ImVec2 position(rect.minX, rect.minY);
        const ImVec2 size(std::max(1.0f, rect.maxX - rect.minX),
                          std::max(1.0f, rect.maxY - rect.minY));
        const bool dense = renderedTileCount >= 9;

        ImGui::SetCursorScreenPos(position);
        ImGui::PushID(context.navigation.ticker);
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              ThemeVec(state.config.theme.monitorBg));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            dense ? ImVec2(12.0f, 10.0f)
                                  : ImVec2(16.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::BeginChild("##MonitorTile",
                          size,
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        RenderSingleStockWindowContent(state, context);
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, height));
}

} // namespace

void RenderTerminalStockSurface(AppState& state) {
    for (auto& context : state.marketData.activeContexts) {
        if (context)
            context->navigation.refreshSurfaceVisible = false;
    }
    RemoveClosedStocks(state);

    if (state.navigation.pureMonitorMode) {
        RenderMonitorStocks(state);
        return;
    }

    if (state.navigation.activeSidebarTab !=
        squarestar::application::SidebarTab::Stock) {
        for (auto& context : state.marketData.activeContexts) {
            if (context)
                context->navigation.headerSearch.ResetUi();
        }
        return;
    }

    if (!state.config.stockTabBarHidden) {
        RenderStockTabStrip(state);
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
    }

    RenderActiveStock(state);
}

} // namespace squarestar::shell
