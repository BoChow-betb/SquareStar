#include "modules/chart_controls.hpp"

#include "application/app_state.hpp"
#include "modules/core.hpp"

namespace squarestar::shell {

using squarestar::application::AppState;

void DrawTimeAxisLabelMenuItems(AppState& state) {
    static const char* xAxisModes[] = {
        "Endpoints only", "Tooltip only", "Show on hover"};
    ImGui::TextDisabled("Time Axis Labels");
    ImGui::SetNextItemWidth(-1.0f);
    if (UiCombo(state,
                "##ChartXAxisModeContext",
                &state.config.chartXAxisMode,
                xAxisModes,
                IM_ARRAYSIZE(xAxisModes))) {
        CommitUiSetting(state, "click.wav");
    }
}
void DrawCrosshairPositionMenuItems(AppState& state) {
    static const char* crosshairModes[] = {
        "Static", "Dynamic (Follow)", "Dynamic (Top)"};
    ImGui::TextDisabled("Crosshair");
    ImGui::SetNextItemWidth(-1.0f);
    if (UiCombo(state,
                "##ChartCrosshairMode",
                &state.config.crosshairMode,
                crosshairModes,
                IM_ARRAYSIZE(crosshairModes))) {
        CommitUiSetting(state, "click.wav");
    }
}

void SetupLockedFinancialPlotAxes() {
    ImPlot::SetupAxis(ImAxis_X1,
                      nullptr,
                      ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoGridLines |
                          ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoTickLabels |
                          ImPlotAxisFlags_Lock);
    ImPlot::SetupAxis(ImAxis_Y1,
                      nullptr,
                      ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoTickMarks |
                          ImPlotAxisFlags_Lock);
}
void DrawFadingXAxisLabels(const AppState& state,
                           const std::vector<double>& ticks,
                           const std::vector<std::string>& labels,
                           double axisMin,
                           double axisMax,
                           const ImVec2& plotPos,
                           const ImVec2& plotSize,
                           float widgetMinX,
                           float widgetMaxX,
                           float alpha) {
    if (ticks.empty() || ticks.size() != labels.size() || !(axisMax > axisMin) || alpha <= 0.01f)
        return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(
        ThemeVec(state.config.theme.text, 0.92f * std::clamp(alpha, 0.0f, 1.0f)));
    const float labelY = plotPos.y + plotSize.y + 14.0f;
    const float safeMinX = std::max(widgetMinX + 12.0f, plotPos.x + 2.0f);
    const float safeMaxX =
        std::min(widgetMaxX - 12.0f, plotPos.x + plotSize.x - 2.0f);
    struct LabelPlacement {
        size_t sourceIndex = 0;
        float x = 0.0f;
        float width = 0.0f;
    };
    // Every time-axis builder emits at most five labels. Fixed storage keeps
    // this per-frame drawing path free of heap allocations.
    constexpr size_t maxLabels = 5;
    std::array<LabelPlacement, maxLabels> rawPlacements{};
    const size_t rawCount = std::min(ticks.size(), maxLabels);
    for (size_t i = 0; i < rawCount; ++i) {
        const float fraction = (float)std::clamp((ticks[i] - axisMin) / (axisMax - axisMin), 0.0, 1.0);
        const float centerX = plotPos.x + plotSize.x * fraction;
        const ImVec2 textSize = ImGui::CalcTextSize(labels[i].c_str());
        // Endpoint labels extend inward instead of being centered across the
        // plot boundary. This keeps both strings fully visible at narrow widths
        // and high DPI without moving their corresponding time positions.
        const bool first = i == 0;
        const bool last = i + 1 == ticks.size();
        const float naturalX = first   ? centerX
                               : last  ? centerX - textSize.x
                                       : centerX - textSize.x * 0.5f;
        const float x = std::clamp(naturalX,
                                   safeMinX,
                                   std::max(safeMinX, safeMaxX - textSize.x));
        rawPlacements[i] = {i, x, textSize.x};
    }

    // Keep the latest occurrence of a repeated date, then reject labels whose
    // clamped boxes would overlap. This is especially important for short 5D
    // datasets whose final intraday ticks can all land against the right edge.
    std::array<LabelPlacement, maxLabels> reversePlacements{};
    size_t placementCount = 0;
    for (size_t i = rawCount; i-- > 0;) {
        const LabelPlacement& placement = rawPlacements[i];
        bool alreadySeen = false;
        for (size_t existing = 0; existing < placementCount; ++existing) {
            if (labels[reversePlacements[existing].sourceIndex] ==
                labels[placement.sourceIndex]) {
                alreadySeen = true;
                break;
            }
        }
        if (!alreadySeen)
            reversePlacements[placementCount++] = placement;
    }
    if (placementCount == 0)
        return;
    std::array<LabelPlacement, maxLabels> placements{};
    for (size_t i = 0; i < placementCount; ++i)
        placements[i] = reversePlacements[placementCount - 1 - i];

    constexpr float minimumGap = 8.0f;
    std::array<bool, maxLabels> visible{};
    visible.front() = true;
    if (placementCount > 1) {
        visible[placementCount - 1] =
            placements[placementCount - 1].x >=
            placements.front().x + placements.front().width + minimumGap;
    }
    float occupiedRight = placements.front().x + placements.front().width + minimumGap;
    const float reservedRight = visible[placementCount - 1]
                                    ? placements[placementCount - 1].x - minimumGap
                                    : safeMaxX;
    for (size_t i = 1; i + 1 < placementCount; ++i) {
        const LabelPlacement& placement = placements[i];
        if (placement.x < occupiedRight || placement.x + placement.width > reservedRight)
            continue;
        visible[i] = true;
        occupiedRight = placement.x + placement.width + minimumGap;
    }
    for (size_t i = 0; i < placementCount; ++i) {
        if (visible[i])
            draw->AddText(ImVec2(placements[i].x, labelY),
                          color,
                          labels[placements[i].sourceIndex].c_str());
    }
}

} // namespace squarestar::shell
