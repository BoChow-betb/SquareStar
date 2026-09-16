#pragma once

namespace squarestar::application {
struct AppState;
struct StockContext;
}

namespace squarestar::shell {


void RenderStockTabBar(squarestar::application::AppState& state,
                       squarestar::application::StockContext& context,
                       bool cleanGuiCapture);

}
