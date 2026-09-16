#pragma once

#include <ctime>

namespace squarestar::application {
struct AppState;
struct StockContext;
}

namespace squarestar::shell {

void DrawSelectedWorldClockTooltip(const squarestar::application::AppState& state,
                                   std::time_t now);
void RenderStockErrorOverlay(squarestar::application::AppState& state,
                             squarestar::application::StockContext& ctx);
void ShowStockModeNotice(squarestar::application::AppState& state,
                         squarestar::application::StockContext& ctx,
                         const char* title,
                         const char* message);

}
