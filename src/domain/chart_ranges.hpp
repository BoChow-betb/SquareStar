#pragma once

namespace squarestar::market {

struct TimeRangeConfig {
    const char* label = "";
    const char* rangeStr = "";
    const char* intervalStr = "";
};

inline constexpr int TIME_RANGE_COUNT = 7;
inline constexpr int ALL_TIME_RANGE_INDEX = TIME_RANGE_COUNT - 1;
extern const TimeRangeConfig TIME_RANGES[TIME_RANGE_COUNT];

} // namespace squarestar::market
