#include "presentation/chart_comparison.hpp"

#include "application/app_state.hpp"
#include "application/stock_context.hpp"
#include "application/stock_tab_policy.hpp"
#include "application/theme_profiles.hpp"
#include "domain/market_calendar.hpp"
#include "presentation/chart_axis.hpp"
#include "presentation/chart_lod.hpp"
#include "presentation/chart_y_axis.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace squarestar::presentation {
namespace {

std::size_t ComparisonHashCombine(std::size_t seed, std::size_t value) {
    return seed ^ (value + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) +
                   (seed << 6) + (seed >> 2));
}

void BuildComparisonPlotDecimation(application::ComparisonSeries& item) {
    constexpr std::size_t targetPoints = 1600;
    const std::size_t count = std::min(item.x.size(), item.percent.size());
    if (count <= targetPoints)
        return;
    BuildLttbSeries(item.x, item.percent, targetPoints, item.plotX, item.plotPercent);
}

}

const std::vector<application::ComparisonSeries>&
BuildComparisonSeries(const application::AppState& state,
                      application::StockContext& primary) {
    constexpr double comparisonSecondsPerUnit = 86400.0;
    std::size_t signature = static_cast<std::size_t>(0xcbf29ce484222325ULL);
    signature = ComparisonHashCombine(signature, std::hash<int>{}(state.config.graphTimeZone));
    signature = ComparisonHashCombine(signature, std::hash<int>{}(state.config.themeModeIndex));
    signature = ComparisonHashCombine(
        signature, std::hash<int>{}(primary.navigation.displayedTimeRangeIndex));
    for (const auto& candidate : state.marketData.activeContexts) {
        if (!candidate || !candidate->RawData().success ||
            !application::IsComparisonSymbolSelected(primary, candidate->navigation.ticker))
            continue;
        signature = ComparisonHashCombine(
            signature, std::hash<std::string>{}(candidate->navigation.ticker));
        signature = ComparisonHashCombine(
            signature, std::hash<int>{}(candidate->navigation.displayedTimeRangeIndex));
        signature = ComparisonHashCombine(
            signature, std::hash<std::uint64_t>{}(candidate->marketData.dataRevision));
        signature = ComparisonHashCombine(
            signature, std::hash<std::size_t>{}(candidate->RawData().timestamps.size()));
        signature = ComparisonHashCombine(
            signature, std::hash<std::size_t>{}(candidate->RawData().closes.size()));
        if (!candidate->RawData().timestamps.empty()) {
            signature = ComparisonHashCombine(
                signature, std::hash<double>{}(candidate->RawData().timestamps.front()));
            signature = ComparisonHashCombine(
                signature, std::hash<double>{}(candidate->RawData().timestamps.back()));
        }
        if (!candidate->RawData().closes.empty()) {
            signature = ComparisonHashCombine(
                signature, std::hash<double>{}(candidate->RawData().closes.front()));
            signature = ComparisonHashCombine(
                signature, std::hash<double>{}(candidate->RawData().closes.back()));
        }
    }
    if (primary.render.comparisonCacheSignature == signature)
        return primary.render.comparisonCache;

    primary.render.comparisonDataMinX = std::numeric_limits<double>::infinity();
    primary.render.comparisonDataMaxX = -std::numeric_limits<double>::infinity();
    primary.render.comparisonDataMinY = std::numeric_limits<double>::infinity();
    primary.render.comparisonDataMaxY = -std::numeric_limits<double>::infinity();
    primary.render.comparisonYAxisValid = false;
    primary.render.comparisonYAxisSignature = 0;
    primary.render.comparisonTimeAxisCache.valid = false;

    static constexpr ImVec4 lightPalette[] = {{0.11f, 0.34f, 0.56f, 1.0f},
                                               {0.58f, 0.35f, 0.10f, 1.0f},
                                               {0.39f, 0.25f, 0.58f, 1.0f},
                                               {0.08f, 0.45f, 0.39f, 1.0f},
                                               {0.48f, 0.25f, 0.31f, 1.0f},
                                               {0.35f, 0.39f, 0.43f, 1.0f}};
    static constexpr ImVec4 darkPalette[] = {{0.40f, 0.67f, 0.91f, 1.0f},
                                              {0.91f, 0.68f, 0.36f, 1.0f},
                                              {0.70f, 0.55f, 0.91f, 1.0f},
                                              {0.35f, 0.78f, 0.67f, 1.0f},
                                              {0.86f, 0.48f, 0.57f, 1.0f},
                                              {0.68f, 0.71f, 0.75f, 1.0f}};

    const bool marketAxis = state.config.graphTimeZone == 1;
    const int comparisonRange = primary.navigation.displayedTimeRangeIndex;
    std::size_t validSeriesCount = 0;
    double earliestAxisTimestamp = std::numeric_limits<double>::infinity();

    for (const auto& candidate : state.marketData.activeContexts) {
        if (!candidate || !candidate->RawData().success ||
            !application::IsComparisonSymbolSelected(primary, candidate->navigation.ticker) ||
            candidate->navigation.displayedTimeRangeIndex != comparisonRange)
            continue;
        const std::size_t count = std::min(candidate->RawData().timestamps.size(),
                                           candidate->RawData().closes.size());
        if (count < 2)
            continue;
        std::size_t validCount = 0;
        double firstAxisTimestamp = 0.0;
        for (std::size_t i = 0; i < count && validCount < 2; ++i) {
            const double timestamp = candidate->RawData().timestamps[i];
            const double close = candidate->RawData().closes[i];
            if (!std::isfinite(timestamp) || timestamp <= 0.0 || !std::isfinite(close) ||
                close <= 0.0)
                continue;
            if (validCount == 0) {
                const std::time_t rawTime =
                    static_cast<std::time_t>(std::llround(timestamp));
                firstAxisTimestamp =
                    timestamp + (marketAxis ? market::NewYorkUtcOffsetSeconds(rawTime) : 0);
            }
            ++validCount;
        }
        if (validCount < 2)
            continue;
        ++validSeriesCount;
        earliestAxisTimestamp = std::min(earliestAxisTimestamp, firstAxisTimestamp);
    }

    if (validSeriesCount < 2 || !std::isfinite(earliestAxisTimestamp)) {
        primary.render.comparisonCache.clear();
        primary.render.comparisonAxisOrigin = 0.0;
        primary.render.comparisonCacheSignature = signature;
        return primary.render.comparisonCache;
    }

    primary.render.comparisonAxisOrigin =
        std::floor(earliestAxisTimestamp / comparisonSecondsPerUnit) *
        comparisonSecondsPerUnit;
    std::vector<application::ComparisonSeries> series;
    series.reserve(validSeriesCount);
    for (const auto& candidate : state.marketData.activeContexts) {
        if (!candidate || !candidate->RawData().success ||
            !application::IsComparisonSymbolSelected(primary, candidate->navigation.ticker) ||
            candidate->navigation.displayedTimeRangeIndex != comparisonRange)
            continue;
        const std::size_t count = std::min(candidate->RawData().timestamps.size(),
                                           candidate->RawData().closes.size());
        if (count < 2)
            continue;
        application::ComparisonSeries item;
        item.symbol = candidate->navigation.ticker;
        item.x.reserve(count);
        item.rawX.reserve(count);
        item.percent.reserve(count);
        double base = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            const double timestamp = candidate->RawData().timestamps[i];
            const double close = candidate->RawData().closes[i];
            if (!std::isfinite(timestamp) || timestamp <= 0.0 || !std::isfinite(close) ||
                close <= 0.0)
                continue;
            if (base <= 0.0)
                base = close;
            const std::time_t rawTime =
                static_cast<std::time_t>(std::llround(timestamp));
            const double axisTimestamp =
                timestamp + (marketAxis ? market::NewYorkUtcOffsetSeconds(rawTime) : 0);
            item.x.push_back(
                (axisTimestamp - primary.render.comparisonAxisOrigin) / comparisonSecondsPerUnit);
            item.rawX.push_back(timestamp);
            const double percent = (close / base - 1.0) * 100.0;
            item.percent.push_back(percent);
            item.minPercent = std::min(item.minPercent, percent);
            item.maxPercent = std::max(item.maxPercent, percent);
        }
        if (item.x.size() < 2)
            continue;
        BuildComparisonPlotDecimation(item);
        const std::size_t colorIndex = series.size() % std::size(lightPalette);
        item.color = application::IsLightGuiTheme(state.config.themeModeIndex)
                         ? lightPalette[colorIndex]
                         : darkPalette[colorIndex];
        primary.render.comparisonDataMinX = std::min(primary.render.comparisonDataMinX, item.x.front());
        primary.render.comparisonDataMaxX = std::max(primary.render.comparisonDataMaxX, item.x.back());
        primary.render.comparisonDataMinY = std::min(primary.render.comparisonDataMinY, item.minPercent);
        primary.render.comparisonDataMaxY = std::max(primary.render.comparisonDataMaxY, item.maxPercent);
        series.push_back(std::move(item));
    }
    primary.render.comparisonCache = std::move(series);
    primary.render.comparisonCacheSignature = signature;
    return primary.render.comparisonCache;
}

void EnsureComparisonYAxisTicks(application::StockContext& primary,
                                double& minY,
                                double& maxY) {
    if (!primary.render.comparisonYAxisValid ||
        primary.render.comparisonYAxisSignature != primary.render.comparisonCacheSignature) {
        const UnifiedYAxis yAxis = CalculateUnifiedYAxis(minY, maxY);
        primary.render.comparisonYAxisTicks.clear();
        primary.render.comparisonYAxisLabels.clear();
        primary.render.comparisonYAxisLabelPtrs.clear();
        for (double value : yAxis.ticks) {
            std::string label = FormatAxisTickValue(value) + "%";
            if (value > 0.0)
                label.insert(label.begin(), '+');
            primary.render.comparisonYAxisTicks.push_back(value);
            primary.render.comparisonYAxisLabels.push_back(std::move(label));
        }
        RefreshLabelPointers(primary.render.comparisonYAxisLabels,
                             primary.render.comparisonYAxisLabelPtrs);
        primary.render.comparisonYAxisValid = true;
        primary.render.comparisonYAxisSignature = primary.render.comparisonCacheSignature;
    }
    if (!primary.render.comparisonYAxisTicks.empty()) {
        minY = primary.render.comparisonYAxisTicks.front();
        maxY = primary.render.comparisonYAxisTicks.back();
    }
}

}
