#pragma once

#include "imgui.h"

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

void PruneClosedTerminalStocks(squarestar::application::AppState& state);
void RenderTerminalStockTabs(squarestar::application::AppState& state);
void RenderMonitorStockWindows(squarestar::application::AppState& state,
                               const ImVec2& workPosition,
                               const ImVec2& workSize);

} // namespace squarestar::shell
