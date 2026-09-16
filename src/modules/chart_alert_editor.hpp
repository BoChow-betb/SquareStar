#pragma once

#include "imgui.h"

namespace squarestar::application {
struct AppState;
struct StockContext;
}

namespace squarestar::shell {

void RenderPriceAlertEditor(squarestar::application::AppState& state,
                            squarestar::application::StockContext& context,
                            bool openEditor,
                            ImVec2 anchorMin,
                            ImVec2 anchorMax,
                            double currentPrice);

}
