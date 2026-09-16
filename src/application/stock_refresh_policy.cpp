#include "stock_refresh_policy.hpp"

#include "domain/market_symbol.hpp"

#include <algorithm>

namespace squarestar::application {

float QuoteRefreshSeconds(bool alertConfigured, bool foregroundSurface) noexcept {
    if (alertConfigured)
        return kAlertQuoteRefreshSeconds;
    return foregroundSurface ? kForegroundQuoteRefreshSeconds
                             : kBackgroundQuoteRefreshSeconds;
}

float ChartRefreshSeconds(bool foregroundSurface) noexcept {
    return foregroundSurface ? kForegroundChartRefreshSeconds
                             : kBackgroundChartRefreshSeconds;
}

float AlertContextRefreshSeconds(bool currentDataSuccess) noexcept {
    return currentDataSuccess ? kAlertQuoteRefreshSeconds : kFailedQuoteRetrySeconds;
}

double SecondsUntilRefresh(float timer, float deadline) noexcept {
    return std::max(0.01, static_cast<double>(deadline - timer));
}

StockAutoRefreshAction SelectStockRefreshAction(float autoRefreshTimer,
                                                float chartRefreshTimer,
                                                bool alertConfigured,
                                                bool foregroundSurface) noexcept {
    if (!alertConfigured && chartRefreshTimer >= ChartRefreshSeconds(foregroundSurface))
        return StockAutoRefreshAction::Chart;
    if (autoRefreshTimer >= QuoteRefreshSeconds(alertConfigured, foregroundSurface))
        return StockAutoRefreshAction::LiveQuote;
    return StockAutoRefreshAction::None;
}

bool AlertContextRefreshDue(float autoRefreshTimer, bool currentDataSuccess) noexcept {
    return autoRefreshTimer >= AlertContextRefreshSeconds(currentDataSuccess);
}

float ClampStockRefreshElapsed(double elapsedSeconds) noexcept {
    if (!(elapsedSeconds > 0.0))
        return 0.0f;
    return static_cast<float>(std::min(elapsedSeconds, 60.0));
}

bool ShouldWatchHiddenStockSurface(const HiddenStockSurfaceInputs& inputs) noexcept {
    if (inputs.pureMonitorMode)
        return true;
    if (inputs.appMinimized)
        return inputs.contextOpen;
    return inputs.isLastActiveTab;
}

bool StockAutoRefreshSessionEligible(std::string_view ticker,
                                     bool regularEquityMarketOpen) {
    const auto symbol = squarestar::market::MarketSymbol::Parse(ticker);
    if (!symbol)
        return false;
    return symbol->IsYahooFutures() || regularEquityMarketOpen;
}

}
