#pragma once

#include <string>
#include <vector>

#include "imgui.h"
#include "implot.h"

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

inline constexpr ImPlotFlags kFinancialPlotFlags =
    ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText | ImPlotFlags_NoLegend |
    ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect;

void DrawTimeAxisLabelMenuItems(squarestar::application::AppState& state);
void DrawCrosshairPositionMenuItems(squarestar::application::AppState& state);
void SetupLockedFinancialPlotAxes();
void DrawFadingXAxisLabels(const squarestar::application::AppState& state,
                           const std::vector<double>& ticks,
                           const std::vector<std::string>& labels,
                           double axisMin,
                           double axisMax,
                           const ImVec2& plotPos,
                           const ImVec2& plotSize,
                           float widgetMinX,
                           float widgetMaxX,
                           float alpha);

}
