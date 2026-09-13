#pragma once

namespace squarestar::application {
struct AppState;
struct StockContext;
}

namespace squarestar::shell {

void RenderStockNews(squarestar::application::AppState& state,
                     squarestar::application::StockContext& ctx);
void RenderStockMetrics(squarestar::application::AppState& state,
                        squarestar::application::StockContext& ctx,
                        double previousClose);

} // namespace squarestar::shell
