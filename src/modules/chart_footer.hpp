#pragma once

#include "imgui.h"

namespace squarestar::application {
struct AppState;
struct StockContext;
}

namespace squarestar::shell {

void RenderStockChartFooter(squarestar::application::AppState& state,
                            squarestar::application::StockContext& ctx,
                            bool shortcutClock,
                            bool cleanGuiCapture,
                            double previousClose,
                            const ImVec2& chartCaptureMin,
                            const ImVec2& chartCaptureMax,
                            float clockStripHeight);

}
