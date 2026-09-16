#pragma once

#include <string_view>

namespace squarestar::application {

inline constexpr float kForegroundQuoteRefreshSeconds = 15.0f;
inline constexpr float kAlertQuoteRefreshSeconds = 5.0f;
inline constexpr float kBackgroundQuoteRefreshSeconds = 60.0f;
inline constexpr float kForegroundChartRefreshSeconds = 120.0f;
inline constexpr float kBackgroundChartRefreshSeconds = 300.0f;
inline constexpr float kFailedQuoteRetrySeconds = 30.0f;

struct HiddenStockSurfaceInputs {
    bool pureMonitorMode = false;
    bool appMinimized = false;
    bool contextOpen = false;
    bool isLastActiveTab = false;
};

enum class StockAutoRefreshAction {
    None,
    LiveQuote,
    Chart,
    AlertQuote,
};

float QuoteRefreshSeconds(bool alertConfigured, bool foregroundSurface) noexcept;
float ChartRefreshSeconds(bool foregroundSurface) noexcept;
float AlertContextRefreshSeconds(bool currentDataSuccess) noexcept;
double SecondsUntilRefresh(float timer, float deadline) noexcept;
StockAutoRefreshAction SelectStockRefreshAction(float autoRefreshTimer,
                                                float chartRefreshTimer,
                                                bool alertConfigured,
                                                bool foregroundSurface) noexcept;
bool AlertContextRefreshDue(float autoRefreshTimer, bool currentDataSuccess) noexcept;
float ClampStockRefreshElapsed(double elapsedSeconds) noexcept;
bool ShouldWatchHiddenStockSurface(const HiddenStockSurfaceInputs& inputs) noexcept;


bool StockAutoRefreshSessionEligible(std::string_view ticker,
                                     bool regularEquityMarketOpen);

}
