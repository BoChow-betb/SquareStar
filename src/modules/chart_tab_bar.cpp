#include "modules/chart_tab_bar.hpp"

#include "application/app_state.hpp"
#include "application/main_loop_signal.hpp"
#include "application/runtime_state.hpp"
#include "application/stock_context.hpp"
#include "application/stock_tab_policy.hpp"
#include "application/ui_animation.hpp"
#include "modules/core.hpp"
#include "modules/stock_comparison.hpp"
#include "modules/stock_surface_feedback.hpp"
#include "modules/ui_focus.hpp"
#include "presentation/gui_shell_runtime_state.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <limits>

namespace squarestar::shell {

using squarestar::application::AppState;
using squarestar::application::CanEnterStockComparison;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::StockContext;
using squarestar::application::UiRounding;

void RenderStockTabBar(AppState& state,
                       StockContext& ctx,
                       bool cleanGuiCapture) {
    if (cleanGuiCapture)
        return;
    if (state.navigation.liteGuiActive) {
        constexpr float gap = 8.0f;
        const float tabWidth =
            std::max(90.0f, (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f);
        const ImVec2 tabSize(tabWidth, 42.0f);
        ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
        if (AnimatedButton(state, "Chart##LiteChartTab", tabSize,
                           ctx.navigation.upperTabIndex == 0, false)) {
            ctx.navigation.nextUpperTab = 0;
            RequestGuiRedraw();
        }
        ImGui::SameLine(0.0f, gap);
        if (AnimatedButton(state, "News##LiteNewsTab", tabSize,
                           ctx.navigation.upperTabIndex == 1, false)) {
            ctx.navigation.nextUpperTab = 1;
            RequestGuiRedraw();
        }
        ImGui::SameLine(0.0f, gap);
        if (AnimatedButton(state, "Metrics##LiteMetricsTab", tabSize,
                           ctx.navigation.upperTabIndex == 2, false)) {
            ctx.navigation.nextUpperTab = 2;
            RequestGuiRedraw();
        }
        ImGui::PopFont();
        return;
    }

    const float tabRowRight =
        ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    const ImVec2 buttonSize(54.0f, 32.0f);
    const auto drawTabButton = [&](const char* id, int iconType, bool active) {
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton(id, buttonSize);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::GetWindowDrawList()->AddRectFilled(
            position,
            ImVec2(position.x + buttonSize.x, position.y + buttonSize.y),
            ImGui::GetColorU32(active    ? ImGuiCol_ButtonActive
                               : hovered ? ImGuiCol_ButtonHovered
                                         : ImGuiCol_Button),
            UiRounding(state, 12.0f));
        const ImU32 iconColor =
            active ? ImGui::GetColorU32(ImGuiCol_Text)
                   : (hovered ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                              : ImGui::ColorConvertFloat4ToU32(
                                    ThemeVec(state.config.theme.textDisabled, 0.30f)));
        DrawIcon(ImGui::GetWindowDrawList(),
                 ImVec2(position.x + buttonSize.x * 0.5f,
                        position.y + buttonSize.y * 0.5f),
                 buttonSize.y * 0.5f,
                 iconType,
                 iconColor);
        DrawObjectFocusRegion(
            state,
            {position, ImVec2(position.x + buttonSize.x, position.y + buttonSize.y)},
            hovered,
            3);
        return pressed;
    };
    if (drawTabButton("##ChartBtn", 0, ctx.navigation.upperTabIndex == 0))
        ctx.navigation.nextUpperTab = 0;
    ImGui::SameLine();
    if (drawTabButton("##NewsBtn", 1, ctx.navigation.upperTabIndex == 1))
        ctx.navigation.nextUpperTab = 1;
    ImGui::SameLine();
    if (drawTabButton("##MetricsBtn", 2, ctx.navigation.upperTabIndex == 2))
        ctx.navigation.nextUpperTab = 2;

    if (!state.navigation.liteGuiActive && !state.navigation.pureMonitorMode) {
        ImGui::SameLine();
        ImVec2 position = ImGui::GetCursorScreenPos();
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        position.x = std::max(position.x,
                              tabRowRight - buttonSize.x * 2.0f - gap);
        ImGui::SetCursorScreenPos(position);
        const bool pressed = ImGui::InvisibleButton("##MultiTabMonitorBtn", buttonSize);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::GetWindowDrawList()->AddRectFilled(
            position,
            ImVec2(position.x + buttonSize.x, position.y + buttonSize.y),
            ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button),
            UiRounding(state, 12.0f));
        const ImU32 iconColor = hovered
                                    ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                                    : ImGui::ColorConvertFloat4ToU32(
                                          ThemeVec(state.config.theme.textDisabled, 0.42f));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 center(position.x + buttonSize.x * 0.5f,
                            position.y + buttonSize.y * 0.5f);
        draw->AddRect(ImVec2(center.x - 9.0f, center.y - 7.0f),
                      ImVec2(center.x + 4.0f, center.y + 5.0f),
                      iconColor,
                      UiRounding(state, 2.0f),
                      0,
                      1.7f);
        draw->AddRect(ImVec2(center.x - 3.0f, center.y - 3.0f),
                      ImVec2(center.x + 10.0f, center.y + 9.0f),
                      iconColor,
                      UiRounding(state, 2.0f),
                      0,
                      1.7f);
        DrawObjectFocusRegion(
            state,
            {position, ImVec2(position.x + buttonSize.x, position.y + buttonSize.y)},
            hovered,
            3);
        if (pressed) {
            state.navigation.monitorModeButtonRequested = true;
            RequestGuiRedraw();
        }
    }

    ImGui::SameLine();
    ImVec2 position = ImGui::GetCursorScreenPos();
    position.x = std::max(position.x, tabRowRight - buttonSize.x);
    ImGui::SetCursorScreenPos(position);
    const bool pressed = ImGui::InvisibleButton("##CompareBtn", buttonSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ctx.navigation.upperTabIndex == 3;
    ImGui::GetWindowDrawList()->AddRectFilled(
        position,
        ImVec2(position.x + buttonSize.x, position.y + buttonSize.y),
        ImGui::GetColorU32(active    ? ImGuiCol_ButtonActive
                           : hovered ? ImGuiCol_ButtonHovered
                                     : ImGuiCol_Button),
        UiRounding(state, 12.0f));
    const ImU32 color =
        active ? ImGui::GetColorU32(ImGuiCol_Text)
               : (hovered ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                          : ImGui::ColorConvertFloat4ToU32(
                                ThemeVec(state.config.theme.textDisabled, 0.30f)));
    ImFont* versusFont = state.render.fontData ? state.render.fontData : ImGui::GetFont();
    const float versusFontSize =
        state.render.fontData ? state.config.theme.fontData : ImGui::GetFontSize();
    const ImVec2 versusSize = versusFont->CalcTextSizeA(
        versusFontSize, std::numeric_limits<float>::max(), 0.0f, "VS");
    ImGui::GetWindowDrawList()->AddText(
        versusFont,
        versusFontSize,
        ImVec2(position.x + (buttonSize.x - versusSize.x) * 0.5f,
               position.y + (buttonSize.y - versusSize.y) * 0.5f),
        color,
        "VS");
    DrawObjectFocusRegion(
        state,
        {position, ImVec2(position.x + buttonSize.x, position.y + buttonSize.y)},
        hovered,
        3);
    if (pressed) {
        PlayUISound("click.wav", state);
        if (active) {
            CloseAllAnimatedFloatingMenus();
            ctx.navigation.comparisonPickerRequested = false;
            ctx.navigation.nextUpperTab = -1;
            ctx.navigation.upperTabIndex = 0;
            ctx.render.tabFadeAnim = state.UiAnimationsEnabled() ? 0.0f : 1.0f;
            RequestGuiRedraw();
        } else if (!CanEnterStockComparison(state)) {
            ShowStockModeNotice(state,
                                ctx,
                                "Comparison needs another tab",
                                "Open one more stock tab to compare.");
        } else {
            PrepareStockComparisonMode(state, ctx, true);
            ctx.navigation.nextUpperTab = -1;
            ctx.navigation.upperTabIndex = 3;
            ctx.render.tabFadeAnim = state.UiAnimationsEnabled() ? 0.0f : 1.0f;
            RequestGuiRedraw();
        }
    }
    ImGui::EndGroup();
}

} // namespace squarestar::shell
