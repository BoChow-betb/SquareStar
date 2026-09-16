#pragma once

#include "modules/stock_surface_feedback.hpp"

namespace squarestar::application {
struct AppState;
struct StockContext;
}

namespace squarestar::shell {

void RenderSingleStockWindowContent(squarestar::application::AppState& state,
                                    squarestar::application::StockContext& ctx,
                                    bool compactLite = false);

}
