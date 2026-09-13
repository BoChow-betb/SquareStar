#include "presentation/chart_axis.hpp"

#include "domain/market_calendar.hpp"
#include "domain/time_axis_ticks.hpp"

#include <cmath>
#include <compare>
#include <cstdio>
#include <cstring>

namespace squarestar::presentation {

void RefreshLabelPointers(const std::vector<std::string>& labels,
                          std::vector<const char*>& labelPtrs) {
    labelPtrs.clear();
    labelPtrs.reserve(labels.size());
    for (const auto& label : labels)
        labelPtrs.push_back(label.c_str());
}

namespace {

std::tm SafeCalendarTime(std::time_t raw, bool utc) {
    std::tm result{};
#ifdef _WIN32
    if (utc)
        gmtime_s(&result, &raw);
    else
        localtime_s(&result, &raw);
#else
    if (utc)
        gmtime_r(&raw, &result);
    else
        localtime_r(&raw, &result);
#endif
    return result;
}

void BuildRoundedHourTicks(double minX,
                           double maxX,
                           bool marketTime,
                           std::vector<double>& ticks,
                           std::vector<std::string>& labels,
                           std::vector<const char*>& labelPtrs,
                           double axisOrigin,
                           double axisSecondsPerUnit) {
    ticks.clear();
    labels.clear();
    labelPtrs.clear();
    if (!std::isfinite(minX) || !std::isfinite(maxX) || !(maxX > minX))
        return;
    constexpr int tickCount = 5;
    for (int i = 0; i < tickCount; ++i) {
        const double tick = minX + (maxX - minX) * (double)i / (tickCount - 1);
        const double timestamp = axisOrigin + tick * axisSecondsPerUnit;
        const std::tm htm = SafeCalendarTime((std::time_t)std::llround(timestamp), marketTime);
        const int hour12 = htm.tm_hour % 12 ? htm.tm_hour % 12 : 12;
        char label[24]{};
        std::snprintf(label,
                      sizeof(label),
                      "%d:%02d %s",
                      hour12,
                      htm.tm_min,
                      htm.tm_hour < 12 ? "AM" : "PM");
        ticks.push_back(tick);
        labels.emplace_back(label);
    }
    RefreshLabelPointers(labels, labelPtrs);
}

void BuildSparseDateTicks(const std::vector<double>& values,
                          int rangeIndex,
                          bool marketTime,
                          std::vector<double>& ticks,
                          std::vector<std::string>& labels,
                          std::vector<const char*>& labelPtrs,
                          double axisOrigin,
                          double axisSecondsPerUnit) {
    ticks.clear();
    labels.clear();
    labelPtrs.clear();
    const auto sparseTicks = squarestar::market::BuildSparseTimeAxisTicks(
        values,
        rangeIndex,
        axisOrigin,
        axisSecondsPerUnit,
        [marketTime](std::time_t timestamp) {
            const std::tm tm = SafeCalendarTime(timestamp, marketTime);
            return squarestar::market::TimeAxisCalendarDate{
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_yday, true};
        });
    ticks.reserve(sparseTicks.size());
    labels.reserve(sparseTicks.size());
    for (const auto& tick : sparseTicks) {
        ticks.push_back(tick.value);
        labels.push_back(tick.label);
    }
    RefreshLabelPointers(labels, labelPtrs);
}

bool SameCacheDouble(double left, double right) noexcept {
    return (left <=> right) == std::partial_ordering::equivalent;
}

int FormatTimeAxisValueForContext(double value,
                                  char* buffer,
                                  int size,
                                  const TimeAxisFormatContext* context) {
    if (!buffer || size <= 0 || !std::isfinite(value))
        return 0;
    const int rangeIndex = context ? context->rangeIndex : 0;
    const bool marketTime = context ? context->marketTime : true;
    const double timestamp =
        context ? context->axisOrigin + value * context->axisSecondsPerUnit : value;
    const std::tm tm = SafeCalendarTime((std::time_t)std::llround(timestamp), marketTime);
    size_t written = 0;
    if (rangeIndex == 0)
        written = std::strftime(buffer, (size_t)size, "%I:%M %p", &tm);
    else if (rangeIndex <= 2)
        written = std::strftime(buffer, (size_t)size, "%b %d", &tm);
    else if (rangeIndex <= 4)
        written = std::strftime(buffer, (size_t)size, "%b %Y", &tm);
    else
        written = std::strftime(buffer, (size_t)size, "%Y", &tm);
    if (written == 0) {
        if (size >= 3) {
            buffer[0] = '-';
            buffer[1] = '-';
            buffer[2] = '\0';
            return 2;
        }
        buffer[0] = '\0';
        return 0;
    }
    if (rangeIndex == 0 && buffer[0] == '0') {
        std::memmove(buffer, buffer + 1, written);
        return (int)written - 1;
    }
    return (int)written;
}

} // namespace

std::pair<double, double> OneDayMarketSessionBounds(std::time_t rawAnchor,
                                                    bool marketTime) {
    const int offset = market::NewYorkUtcOffsetSeconds(rawAnchor);
    const std::time_t nyWall = rawAnchor + offset;
    std::tm ny = SafeCalendarTime(nyWall, true);
    ny.tm_hour = 9;
    ny.tm_min = 30;
    ny.tm_sec = 0;
    const std::time_t shiftedStart = market::UtcTmToTimeT(ny);
    const std::time_t rawStart = shiftedStart - offset;
    const double start = (double)(marketTime ? shiftedStart : rawStart);
    return {start, start + 6.5 * 3600.0};
}

void UpdateStableChartAxisLock(application::StockRenderCache& cache,
                               int rangeIndex,
                               double firstTs,
                               double lastTs,
                               bool marketTime,
                               std::time_t rawAnchor,
                               bool regularEquitySession) {
    if (!std::isfinite(firstTs) || !std::isfinite(lastTs) || !(lastTs > firstTs)) {
        const double anchor = std::isfinite(lastTs) ? lastTs : (double)rawAnchor;
        firstTs = anchor - (rangeIndex == 0 ? 6.5 * 3600.0 : 86400.0);
        lastTs = anchor;
    }
    if (rangeIndex == 0) {
        const std::pair<double, double> session =
            regularEquitySession
                ? OneDayMarketSessionBounds(rawAnchor, marketTime)
                : std::pair<double, double>{firstTs, lastTs};
        const bool differentSession = cache.lockedAxisRangeIndex != 0 ||
                                      std::abs(cache.lockedAxisMinX - session.first) > 60.0 ||
                                      std::abs(cache.lockedAxisMaxX - session.second) > 60.0;
        if (differentSession) {
            cache.lockedAxisRangeIndex = 0;
            cache.lockedAxisMinX = session.first;
            cache.lockedAxisMaxX = session.second;
        }
        return;
    }
    if (cache.lockedAxisRangeIndex != rangeIndex ||
        !(cache.lockedAxisMaxX > cache.lockedAxisMinX)) {
        if (!(lastTs > firstTs))
            lastTs = firstTs + 86400.0;
        cache.lockedAxisRangeIndex = rangeIndex;
        cache.lockedAxisMinX = firstTs;
        cache.lockedAxisMaxX = lastTs;
        return;
    }
    const double span = cache.lockedAxisMaxX - cache.lockedAxisMinX;
    const double dataSpan = lastTs - firstTs;
    if (dataSpan > span * 1.001) {
        cache.lockedAxisMinX = firstTs;
        cache.lockedAxisMaxX = lastTs;
    } else if (lastTs > cache.lockedAxisMaxX) {
        cache.lockedAxisMaxX = lastTs;
        cache.lockedAxisMinX = lastTs - span;
    } else if (firstTs < cache.lockedAxisMinX) {
        cache.lockedAxisMinX = firstTs;
        cache.lockedAxisMaxX = firstTs + span;
    }
}

int FormatTimeAxisValue(double value, char* buffer, int size, void* userData) {
    return FormatTimeAxisValueForContext(
        value, buffer, size, static_cast<const TimeAxisFormatContext*>(userData));
}

void BuildConfiguredTimeAxisTicks(const std::vector<double>& values,
                                  double minX,
                                  double maxX,
                                  int rangeIndex,
                                  bool marketTime,
                                  int xAxisMode,
                                  std::vector<double>& ticks,
                                  std::vector<std::string>& labels,
                                  std::vector<const char*>& labelPtrs,
                                  double axisOrigin,
                                  double axisSecondsPerUnit) {
    ticks.clear();
    labels.clear();
    labelPtrs.clear();
    if (!std::isfinite(minX) || !std::isfinite(maxX) || !(maxX > minX) || xAxisMode == 1)
        return;
    if (xAxisMode == 2) {
        if (rangeIndex == 0)
            BuildRoundedHourTicks(minX,
                                  maxX,
                                  marketTime,
                                  ticks,
                                  labels,
                                  labelPtrs,
                                  axisOrigin,
                                  axisSecondsPerUnit);
        else
            BuildSparseDateTicks(values,
                                 rangeIndex,
                                 marketTime,
                                 ticks,
                                 labels,
                                 labelPtrs,
                                 axisOrigin,
                                 axisSecondsPerUnit);
        return;
    }
    TimeAxisFormatContext context{rangeIndex, marketTime, axisOrigin, axisSecondsPerUnit};
    for (double tick : {minX, maxX}) {
        char label[32]{};
        FormatTimeAxisValue(tick, label, (int)sizeof(label), &context);
        ticks.push_back(tick);
        labels.emplace_back(label);
    }
    RefreshLabelPointers(labels, labelPtrs);
}

void EnsureConfiguredTimeAxisTicks(application::TimeAxisTickCache& cache,
                                   std::uint64_t dataRevision,
                                   const std::vector<double>& values,
                                   double minX,
                                   double maxX,
                                   int rangeIndex,
                                   bool marketTime,
                                   int xAxisMode,
                                   double axisOrigin,
                                   double axisSecondsPerUnit) {
    const size_t sourceCount = values.size();
    const double sourceFirst = sourceCount ? values.front() : 0.0;
    const double sourceLast = sourceCount ? values.back() : 0.0;
    if (cache.valid && cache.dataRevision == dataRevision && cache.sourceCount == sourceCount &&
        SameCacheDouble(cache.sourceFirst, sourceFirst) && SameCacheDouble(cache.sourceLast, sourceLast) && SameCacheDouble(cache.minX, minX) &&
        SameCacheDouble(cache.maxX, maxX) && SameCacheDouble(cache.axisOrigin, axisOrigin) &&
        SameCacheDouble(cache.axisSecondsPerUnit, axisSecondsPerUnit) && cache.rangeIndex == rangeIndex &&
        cache.marketTime == marketTime && cache.mode == xAxisMode)
        return;
    cache.valid = true;
    cache.dataRevision = dataRevision;
    cache.sourceCount = sourceCount;
    cache.sourceFirst = sourceFirst;
    cache.sourceLast = sourceLast;
    cache.minX = minX;
    cache.maxX = maxX;
    cache.axisOrigin = axisOrigin;
    cache.axisSecondsPerUnit = axisSecondsPerUnit;
    cache.rangeIndex = rangeIndex;
    cache.marketTime = marketTime;
    cache.mode = xAxisMode;
    BuildConfiguredTimeAxisTicks(values,
                                 minX,
                                 maxX,
                                 rangeIndex,
                                 marketTime,
                                 xAxisMode,
                                 cache.ticks,
                                 cache.labels,
                                 cache.labelPtrs,
                                 axisOrigin,
                                 axisSecondsPerUnit);
}

} // namespace squarestar::presentation
