#pragma once

#include <string>

namespace squarestar::market {

enum class ChartAvailability {
    Unknown,
    ActiveSelectedRange,
    NoTradingToday,
    TradingStoppedWithHistory,
    NoChartHistory,
};

enum class CorporateActionStatus {
    None,
    SuspectedStopped,
    Suspended,
    Delisted,
    Acquired,
};

struct TradingStatus {
    ChartAvailability chartAvailability = ChartAvailability::Unknown;
    CorporateActionStatus corporateAction = CorporateActionStatus::None;
    std::string evidenceHeadline;
};

}
