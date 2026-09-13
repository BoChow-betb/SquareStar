#pragma once

struct ImVec2;
struct ImVec4;
struct ImGuiViewport;

namespace squarestar::application {
struct AppState;
struct StockContext;
}

namespace squarestar::shell {

// Private cross-translation-unit helpers for the stock chart surface.
void RenderStockHeader(squarestar::application::AppState& state,
                       squarestar::application::StockContext& ctx,
                       float dt,
                       bool cleanGuiCapture,
                       double currentPrice,
                       double referencePrice,
                       double change,
                       double changePercent,
                       const ImVec4& changeColor);

void RenderStockChartContextMenu(squarestar::application::AppState& state,
                                 squarestar::application::StockContext& ctx,
                                 bool contextMenuInteractive,
                                 ImGuiViewport* chartViewport,
                                 ImVec2 stockCaptureMin,
                                 ImVec2 chartCaptureMin,
                                 ImVec2 chartCaptureMax);

void RenderStockChart(squarestar::application::AppState& state,
                      squarestar::application::StockContext& ctx,
                      float dt,
                      bool shortcutClock,
                      bool cleanGuiCapture,
                      ImVec2 stockCaptureMin,
                      double previousClose,
                      const ImVec4& themeCol);

void RenderStockWipeOverlay(squarestar::application::AppState& state,
                            squarestar::application::StockContext& ctx);
void RenderStockModeNotice(squarestar::application::AppState& state,
                           squarestar::application::StockContext& ctx,
                           float dt);

} // namespace squarestar::shell
