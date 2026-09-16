#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

namespace squarestar::market {

struct TimeAxisCalendarDate {
    int year = 0;
    int month = 0;
    int day = 0;
    int dayOfYear = 0;
    bool valid = false;
};

struct SparseTimeAxisTick {
    double value = 0.0;
    std::string label;
};

inline std::string FormatSparseTimeAxisLabel(const TimeAxisCalendarDate& date,
                                             int rangeIndex) {
    static constexpr std::array<const char*, 12> kMonths = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (!date.valid || date.month < 1 || date.month > 12)
        return "--";
    if (rangeIndex >= 5)
        return std::to_string(date.year);
    if (rangeIndex >= 3)
        return kMonths[(size_t)date.month - 1];
    return std::string(kMonths[(size_t)date.month - 1]) + " " + std::to_string(date.day);
}

template <typename CalendarResolver>
std::vector<SparseTimeAxisTick> BuildSparseTimeAxisTicks(
    const std::vector<double>& values,
    int rangeIndex,
    double axisOrigin,
    double axisSecondsPerUnit,
    CalendarResolver&& resolveCalendar,
    size_t maxTickCount = 5) {
    std::vector<SparseTimeAxisTick> result;
    if (maxTickCount == 0 || values.size() < 2 || !std::isfinite(values.front()) ||
        !std::isfinite(values.back()) || !(values.back() > values.front()) ||
        !std::isfinite(axisOrigin) || !std::isfinite(axisSecondsPerUnit) ||
        axisSecondsPerUnit <= 0.0) {
        return result;
    }
    result.reserve(maxTickCount);

    const auto resolveValue = [&](double value) -> TimeAxisCalendarDate {
        const double timestamp = axisOrigin + value * axisSecondsPerUnit;
        if (!std::isfinite(timestamp))
            return {};
        const double minTime = (double)std::numeric_limits<std::time_t>::lowest();
        const double maxTime = (double)std::numeric_limits<std::time_t>::max();
        if (timestamp < minTime || timestamp > maxTime)
            return {};
        return resolveCalendar((std::time_t)std::llround(timestamp));
    };
    const auto append = [&](double value) {
        const TimeAxisCalendarDate date = resolveValue(value);
        if (!date.valid)
            return;
        result.push_back({value, FormatSparseTimeAxisLabel(date, rangeIndex)});
    };

    constexpr int64_t kDayKeyScale = 400;
    if (rangeIndex == 1) {
        std::vector<size_t> candidates;
        candidates.reserve(std::min(values.size(), (size_t)32));
        size_t dayBegin = 0;
        int64_t activeDayKey = std::numeric_limits<int64_t>::min();
        for (size_t i = 0; i <= values.size(); ++i) {
            int64_t dayKey = std::numeric_limits<int64_t>::max();
            if (i < values.size()) {
                const TimeAxisCalendarDate date = resolveValue(values[i]);
                if (date.valid)
                    dayKey = (int64_t)date.year * kDayKeyScale + date.dayOfYear;
            }
            if (i == 0)
                activeDayKey = dayKey;
            if (i == values.size() || dayKey != activeDayKey) {
                if (i > dayBegin)
                    candidates.push_back(dayBegin + (i - dayBegin - 1) / 2);
                dayBegin = i;
                activeDayKey = dayKey;
            }
        }
        if (candidates.size() >= maxTickCount) {
            if (maxTickCount == 1) {
                append(values[candidates.front()]);
            } else {
                for (size_t i = 0; i < maxTickCount; ++i) {
                    const size_t sample = (size_t)std::llround(
                        (double)i * (double)(candidates.size() - 1) /
                        (double)(maxTickCount - 1));
                    append(values[candidates[sample]]);
                }
            }
            return result;
        }
    }

    if (rangeIndex >= 3) {
        struct CalendarBucket {
            double value = 0.0;
        };
        const bool yearly = rangeIndex >= 5;
        std::vector<CalendarBucket> candidates;
        candidates.reserve(std::min(values.size(), (size_t)64));
        int64_t activeKey = std::numeric_limits<int64_t>::min();
        for (double value : values) {
            if (!std::isfinite(value) || value < values.front() || value > values.back())
                continue;
            const TimeAxisCalendarDate date = resolveValue(value);
            if (!date.valid)
                continue;
            const int64_t key = yearly ? (int64_t)date.year
                                       : (int64_t)date.year * 12 + (date.month - 1);
            if (key == activeKey)
                continue;
            candidates.push_back({value});
            activeKey = key;
        }
        if (!candidates.empty()) {
            const size_t desired = std::min(maxTickCount, candidates.size());
            if (desired == 1) {
                append(candidates.front().value);
            } else {
                size_t previous = std::numeric_limits<size_t>::max();
                for (size_t i = 0; i < desired; ++i) {
                    const size_t sample = (size_t)std::llround(
                        (double)i * (double)(candidates.size() - 1) / (double)(desired - 1));
                    if (sample == previous)
                        continue;
                    append(candidates[sample].value);
                    previous = sample;
                }
            }
            if (!result.empty())
                return result;
        }
    }

    result.clear();
    if (maxTickCount == 1) {
        append(values.front());
        return result;
    }
    for (size_t i = 0; i < maxTickCount; ++i) {
        append(values.front() + (values.back() - values.front()) * (double)i /
                                  (double)(maxTickCount - 1));
    }
    return result;
}

}
