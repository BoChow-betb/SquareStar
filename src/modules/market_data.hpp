#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "domain/stock_data.hpp"
#include "imgui.h"

namespace squarestar::application {
struct AppState;
struct StockContext;
enum class FetchStartResult : std::uint8_t;
}

namespace squarestar::shell {

enum class StockOpenResult {
    InvalidSymbol,
    SelectedExisting,
    OpenedNew,
    TabLimitReached,
};

bool AddTickerToWatchlist(squarestar::application::AppState& state,
                          const std::string& ticker);
void DrawFinancialRow(const char* label,
                      const std::string& value,
                      bool underline = true,
                      const ImVec4* valueColor = nullptr);
bool IsTickerInWatchlist(const squarestar::application::AppState& state,
                         const std::string& ticker);
bool NeutralCheckbox(const char* label,
                     bool& value,
                     const squarestar::application::AppState& state);
StockOpenResult OpenStock(squarestar::application::AppState& state,
                          const std::string& rawTicker);
void PumpStockRequestQueue(squarestar::application::AppState& state,
                           squarestar::application::StockContext& ctx);
void ReconcilePriceAlertContexts(squarestar::application::AppState& state);
bool RemoveTickerFromWatchlist(squarestar::application::AppState& state,
                               const std::string& ticker);
void RenderLoadingSpinner(const squarestar::application::AppState& state,
                          const char* label,
                          float radius,
                          int thickness,
                          const ImU32& color);
void RenderStockRangeButtons(squarestar::application::AppState& state,
                             squarestar::application::StockContext& ctx);
squarestar::application::FetchStartResult RequestManualStockRefresh(
    squarestar::application::AppState& state,
    squarestar::application::StockContext& ctx);
void SelectChartRange(squarestar::application::AppState& state,
                      squarestar::application::StockContext& ctx,
                      int rangeIndex);
void SelectStockViewRange(squarestar::application::AppState& state,
                          squarestar::application::StockContext& ctx,
                          int rangeIndex);
bool StockRequestIdle(const squarestar::application::StockContext& ctx);
void TriggerDetailsFetch(squarestar::application::AppState& state,
                         squarestar::application::StockContext& ctx,
                         std::uint32_t requestedMask);
squarestar::application::FetchStartResult TriggerFetch(
    squarestar::application::AppState& state,
    squarestar::application::StockContext& ctx,
    bool forceUpdate,
    bool background,
    squarestar::market::FetchKind kind = squarestar::market::FetchKind::Full);

}
