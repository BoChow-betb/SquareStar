#include "modules/gui_shell.hpp"
#include "modules/gui_shell_internal.hpp"
#include "modules/interface_modals.hpp"
#include "modules/core.hpp"
#include "modules/ui_focus.hpp"
#include "modules/platform.hpp"
#include "modules/market_data.hpp"
#include "modules/views.hpp"
#include "modules/settings_view.hpp"
#include "modules/terminal_stock_surface.hpp"
#include "modules/stock_surface_feedback.hpp"
#include "modules/window_chrome.hpp"

#include "application/app_state.hpp"
#include "services/config_save_queue.hpp"
#include "application/app_limits.hpp"
#include "application/contextual_keybind_policy.hpp"
#include "application/key_bindings.hpp"
#include "application/main_loop_signal.hpp"
#include "application/navigation_state.hpp"
#include "application/screener_controller.hpp"
#include "application/stock_request_state.hpp"
#include "application/theme_profiles.hpp"
#include "application/ui_animation.hpp"
#include "domain/market_symbol.hpp"
#include "domain/chart_ranges.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace squarestar::shell {

using squarestar::application::RequestGuiRedraw;
using squarestar::application::UiRounding;
using squarestar::application::AppState;

void SynchronizeTerminalNavigationState(AppState& state, size_t openStockTabs) {
    if (!state.navigation.pureMonitorMode && openStockTabs == 0 &&
        state.navigation.activeSidebarTab == squarestar::application::SidebarTab::Stock) {
        state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Home;
    }
    if (state.navigation.activeSidebarTab != state.navigation.previousActiveSidebarTab) {
        const bool enteringOverview =
            state.navigation.activeSidebarTab == squarestar::application::SidebarTab::Overview &&
            state.navigation.previousActiveSidebarTab != squarestar::application::SidebarTab::Overview;
        if (enteringOverview) {
            // Treat every explicit Overview entry as a fresh presentation
            // session. In-market entry must not briefly resurrect an older
            // screener snapshot before the live request completes. Periodic
            // refreshes while Overview stays open still preserve visible rows.
            squarestar::application::ResetScreenerFetch(state);
        }
        state.navigation.mainSearch.ResetUi();
        for (auto& context : state.marketData.activeContexts) {
            if (context)
                context->navigation.headerSearch.ResetUi();
        }
        state.navigation.watchlistOpen = false;
        state.navigation.previousActiveSidebarTab = state.navigation.activeSidebarTab;
    }
}

float RenderTerminalSidebar(AppState& state, size_t openStockTabs) {
    const float navDt = UiFrameDelta();
    state.render.navigationTransition =
        state.UiAnimationsEnabled() ? std::min(1.0f, state.render.navigationTransition + navDt * 6.0f) : 1.0f;
    if (state.UiAnimationsEnabled() && state.render.navigationTransition < 0.999f)
        RequestGuiRedraw();
    struct NavigationItem {
        const char* name = "";
        int icon = 0;
        squarestar::application::SidebarTab value = squarestar::application::SidebarTab::Home;
    };
    static constexpr std::array navigation = {
        NavigationItem{"Home", 7, squarestar::application::SidebarTab::Home},
        NavigationItem{"Overview", 8, squarestar::application::SidebarTab::Overview},
        NavigationItem{"Settings", 11, squarestar::application::SidebarTab::Settings},
        NavigationItem{"Terminal", 0, squarestar::application::SidebarTab::Stock}};
    const int navCount = openStockTabs == 0 ? 3 : (int)navigation.size();
    auto ActivateNavigation = [&](squarestar::application::SidebarTab value) {
        if (state.navigation.activeSidebarTab != value)
            PlayUISound("transition.wav", state);
        state.navigation.activeSidebarTab = value;
    };
    float currentSidebarWidth = 0.0f;
    if (!state.navigation.pureMonitorMode) {
        const float targetWidth = (!state.config.sidebarHidden && state.navigation.isSidebarHovered)
                                      ? state.config.theme.sidebarExpanded
                                      : state.config.theme.sidebarCollapsed;
        state.render.sidebarAnim = state.UiAnimationsEnabled()
                                ? state.render.sidebarAnim + (targetWidth - state.render.sidebarAnim) *
                                                          (1.0f - std::exp(-18.0f * navDt))
                                : targetWidth;
        const float railWidth = std::round(state.render.sidebarAnim);
        if (state.UiAnimationsEnabled() &&
            std::abs(state.render.sidebarAnim - targetWidth) > 0.1f)
            RequestGuiRedraw();
        currentSidebarWidth = railWidth + 16.0f;
        const float railHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - 16.0f);
        ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.35f + state.render.navigationTransition * 0.65f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiRounding(state, 18.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ThemeVec(state.config.theme.sidebarBg, 0.72f));
        ImGui::PushStyleColor(ImGuiCol_Border, ThemeVec(state.config.theme.floatingBorder, 0.66f));
        ImGui::BeginChild("GlassSidebar",
                          ImVec2(railWidth, railHeight),
                          true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        state.navigation.isSidebarHovered =
            !state.config.sidebarHidden &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                                   ImGuiHoveredFlags_ChildWindows);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float textAlpha = std::clamp(
            (railWidth - state.config.theme.sidebarCollapsed) /
                std::max(1.0f, state.config.theme.sidebarExpanded - state.config.theme.sidebarCollapsed),
            0.0f,
            1.0f);
        auto DrawRailItem = [&](int index, float y) {
            const NavigationItem& item = navigation[(size_t)index];
            const bool selected = state.navigation.activeSidebarTab == item.value;
            ImGui::SetCursorPos(ImVec2(6.0f, y));
            ImGui::PushID(index);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 size(std::max(1.0f, railWidth - 12.0f), 48.0f);
            const bool pressed = ImGui::InvisibleButton("##Nav", size);
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            DrawObjectFocusOutline(state, p, ImVec2(p.x + size.x, p.y + size.y), hovered, 3);
            if (hovered || selected)
                dl->AddRectFilled(
                    p,
                    ImVec2(p.x + size.x, p.y + size.y),
                    ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_ButtonActive),
                    UiRounding(state, 14.0f));
            const ImU32 ink = ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            DrawIcon(dl, ImVec2(p.x + 26.0f, p.y + 24.0f), 23.0f, item.icon, ink);
            if (textAlpha > 0.01f)
                dl->AddText(state.render.fontLarge,
                            state.config.theme.fontTitle * 0.68f,
                            ImVec2(p.x + 58.0f, p.y + 11.0f),
                            ImGui::ColorConvertFloat4ToU32(
                                selected ? ThemeVec(state.config.theme.text, textAlpha)
                                         : ThemeVec(state.config.theme.sidebarText, textAlpha)),
                            item.name);
            if (pressed)
                ActivateNavigation(item.value);
        };
        ImGui::SetCursorPos(ImVec2(6.0f, 6.0f));
        const ImVec2 collapsePos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##CollapseRail",
                                   ImVec2(std::max(1.0f, railWidth - 12.0f), 42.0f))) {
            state.config.sidebarHidden = !state.config.sidebarHidden;
            state.navigation.isSidebarHovered = false;
            PlayUISound("click.wav", state);
        }
        const bool collapseHovered = ImGui::IsItemHovered();
        DrawObjectFocusOutline(state,
                               collapsePos,
                               ImVec2(collapsePos.x + std::max(1.0f, railWidth - 12.0f),
                                      collapsePos.y + 42.0f),
                               collapseHovered,
                               3);
        const ImU32 railInk =
            ImGui::GetColorU32(collapseHovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        const float arrowCenterX = collapsePos.x + 26.0f;
        const float direction = state.config.sidebarHidden ? 1.0f : -1.0f;
        dl->AddLine(ImVec2(arrowCenterX - direction * 3.5f, collapsePos.y + 14.0f),
                    ImVec2(arrowCenterX + direction * 3.5f, collapsePos.y + 21.0f),
                    railInk,
                    1.8f);
        dl->AddLine(ImVec2(arrowCenterX + direction * 3.5f, collapsePos.y + 21.0f),
                    ImVec2(arrowCenterX - direction * 3.5f, collapsePos.y + 28.0f),
                    railInk,
                    1.8f);
        for (int i = 0; i < navCount; ++i)
            DrawRailItem(i, 56.0f + static_cast<float>(i) * 58.0f);
        const float navEndY = 56.0f + static_cast<float>(navCount) * 58.0f;
        const float modeWidth = std::max(1.0f, railWidth - 12.0f);
        const float liteY = std::max(navEndY, ImGui::GetWindowHeight() - 58.0f);
        ImGui::SetCursorPos(ImVec2(6.0f, liteY));
        const ImVec2 litePos = ImGui::GetCursorScreenPos();
        const ImVec2 liteSize(modeWidth, 48.0f);
        const bool litePressed = ImGui::InvisibleButton("##LiteGuiMode", liteSize);
        const bool liteHovered = ImGui::IsItemHovered();
        DrawObjectFocusOutline(state,
                               litePos,
                               ImVec2(litePos.x + liteSize.x, litePos.y + liteSize.y),
                               liteHovered,
                               3);
        if (liteHovered)
            dl->AddRectFilled(litePos,
                              ImVec2(litePos.x + liteSize.x, litePos.y + liteSize.y),
                              ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                              UiRounding(state, 14.0f));
        DrawIcon(dl,
                 ImVec2(litePos.x + 26.0f, litePos.y + 24.0f),
                 23.0f,
                 14,
                 ImGui::GetColorU32(liteHovered ? ImGuiCol_Text : ImGuiCol_TextDisabled));
        if (textAlpha > 0.01f)
            dl->AddText(
                state.render.fontLarge,
                state.config.theme.fontTitle * 0.68f,
                ImVec2(litePos.x + 58.0f, litePos.y + 11.0f),
                ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.sidebarText, textAlpha)),
                "Lite GUI");
        if (litePressed) {
            PlayUISound("transition.wav", state);
            RequestLiteGuiMode();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }
    return currentSidebarWidth;
}

void RenderTerminalMainContent(AppState& state,
                               float currentSidebarWidth,
                               ImU32 appBg) {
    ImGui::SetCursorPos(ImVec2(currentSidebarWidth, 0.0f));
    const ImVec2 mainContentSize = ImGui::GetContentRegionAvail();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("MainContent",
                      mainContentSize,
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    if (state.navigation.pureMonitorMode) {
        RenderTerminalStockSurface(state);
    } else if (squarestar::application::IsSidebarPage(state.navigation.activeSidebarTab)) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(state.config.theme.contentPadding, state.config.theme.contentPadding));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 15.0f));
        ImGui::BeginChild("SidebarOverlay",
                          ImVec2(0, 0),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 pMin = ImGui::GetWindowPos();
        const ImVec2 pMax(pMin.x + ImGui::GetWindowSize().x,
                          pMin.y + ImGui::GetWindowSize().y);
        ImGui::GetWindowDrawList()->AddRectFilled(pMin, pMax, appBg);
        static constexpr std::array<void (*)(AppState&), 3> sidebarPages = {
            RenderHomePage, RenderOverviewFirstPage, RenderSettingsContent};
        const int sidebarIndex =
            squarestar::application::SidebarTabIndex(state.navigation.activeSidebarTab);
        if (sidebarIndex >= 0 && sidebarIndex < static_cast<int>(sidebarPages.size()))
            sidebarPages[static_cast<std::size_t>(sidebarIndex)](state);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    } else {
        const float horizontalPadding = std::max(12.0f, state.config.theme.contentPadding * 0.55f);
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(horizontalPadding, state.config.theme.contentPadding * 0.35f));
        ImGui::BeginChild("StockSurface",
                          ImVec2(0, 0),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        RenderTerminalStockSurface(state);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    ImGui::EndChild();
}

} // namespace squarestar::shell
