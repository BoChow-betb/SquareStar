#pragma once

namespace squarestar::application {
struct AppState;
struct StockContext;
}

namespace squarestar::shell {

// Internal chart tab control. Callers render complete stock content instead of
// reaching into the tab bar directly.
void RenderStockTabBar(squarestar::application::AppState& state,
                       squarestar::application::StockContext& context,
                       bool cleanGuiCapture);

} // namespace squarestar::shell
