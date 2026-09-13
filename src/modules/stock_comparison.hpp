#pragma once

#include "imgui.h"

namespace squarestar::application {
struct AppState;
struct StockContext;
}

namespace squarestar::shell {

void PrepareStockComparisonMode(squarestar::application::AppState& state,
                                squarestar::application::StockContext& primary,
                                bool requestPicker);
void RenderStockComparison(squarestar::application::AppState& state,
                           squarestar::application::StockContext& primary,
                           const ImVec2& stockCaptureMin);

} // namespace squarestar::shell
