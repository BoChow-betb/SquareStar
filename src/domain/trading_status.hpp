#pragma once

#include <string_view>
#include <vector>

#include "domain/chart_ranges.hpp"
#include "domain/stock_data.hpp"
#include "domain/trading_status_fwd.hpp"

namespace squarestar::market {

struct ChartFallbackDecision {
    int rangeIndex = -1;
    ChartAvailability availability = ChartAvailability::NoChartHistory;
    CorporateActionStatus defaultCorporateAction = CorporateActionStatus::None;
};

[[nodiscard]] constexpr ChartFallbackDecision ResolveProgressiveChartRange(
    bool hasOneDay,
    bool hasFiveDay,
    bool hasOneMonth,
    bool hasAllHistory) noexcept {
    if (hasOneDay)
        return {0, ChartAvailability::ActiveSelectedRange,
                CorporateActionStatus::None};
    if (hasFiveDay)
        return {1, ChartAvailability::NoTradingToday,
                CorporateActionStatus::None};
    if (hasOneMonth)
        return {2, ChartAvailability::TradingStoppedWithHistory,
                CorporateActionStatus::SuspectedStopped};
    if (hasAllHistory)
        return {ALL_TIME_RANGE_INDEX, ChartAvailability::TradingStoppedWithHistory,
                CorporateActionStatus::SuspectedStopped};
    return {-1, ChartAvailability::NoChartHistory,
            CorporateActionStatus::None};
}

[[nodiscard]] CorporateActionStatus ClassifyCorporateActionNews(
    const std::vector<NewsItem>& news,
    std::string* evidenceHeadline = nullptr);
[[nodiscard]] constexpr bool HasInactiveTradingStatus(
    const TradingStatus& status) noexcept {
    return status.corporateAction != CorporateActionStatus::None;
}
[[nodiscard]] std::string TradingStatusExchangeLabel(
    std::string_view exchange,
    const TradingStatus& status);

} // namespace squarestar::market
