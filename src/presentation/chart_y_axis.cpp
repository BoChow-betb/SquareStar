#include "presentation/chart_y_axis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace squarestar::presentation {
namespace {

double NiceAxisStepCeil(double rawStep) {
    if (!std::isfinite(rawStep) || rawStep <= 0.0)
        return 1.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));
    const double fraction = rawStep / magnitude;
    const double niceFraction = fraction <= 1.0    ? 1.0
                                : fraction <= 1.25 ? 1.25
                                : fraction <= 2.0  ? 2.0
                                : fraction <= 2.5  ? 2.5
                                : fraction <= 5.0  ? 5.0
                                                   : 10.0;
    return niceFraction * magnitude;
}

} // namespace

std::string FormatAxisTickValue(double value) {
    if (!std::isfinite(value))
        return "--";
    if (std::abs(value) < 5e-12)
        value = 0.0;
    char text[512]{};
    const double magnitude = std::abs(value);
    if (magnitude >= 1e12)
        std::snprintf(text, sizeof(text), "%.1fT", value / 1e12);
    else if (magnitude >= 1e9)
        std::snprintf(text, sizeof(text), "%.1fB", value / 1e9);
    else if (magnitude >= 1e6)
        std::snprintf(text, sizeof(text), "%.1fM", value / 1e6);
    else if (magnitude >= 1e4)
        std::snprintf(text, sizeof(text), "%.1fK", value / 1e3);
    else if (magnitude >= 100.0)
        std::snprintf(text, sizeof(text), "%.2f", value);
    else if (magnitude >= 10.0)
        std::snprintf(text, sizeof(text), "%.1f", value);
    else if (magnitude >= 1.0)
        std::snprintf(text, sizeof(text), "%.2f", value);
    else if (magnitude >= 0.01)
        std::snprintf(text, sizeof(text), "%.3f", value);
    else
        std::snprintf(text, sizeof(text), "%.4f", value);
    std::string result = text;
    const size_t suffix = result.find_last_not_of("0123456789.-");
    const size_t numberEnd = suffix == std::string::npos ? result.size() : suffix;
    const size_t dot = result.find('.');
    if (dot != std::string::npos && dot < numberEnd) {
        size_t trim = numberEnd;
        while (trim > dot + 1 && result[trim - 1] == '0')
            --trim;
        if (trim == dot + 1)
            --trim;
        result.erase(trim, numberEnd - trim);
    }
    return result;
}

UnifiedYAxis CalculateUnifiedYAxis(double lowestValue,
                                   double highestValue,
                                   double paddingFraction,
                                   bool nonNegative) {
    if (!std::isfinite(lowestValue) || !std::isfinite(highestValue)) {
        lowestValue = 0.0;
        highestValue = 1.0;
    }
    if (highestValue < lowestValue)
        std::swap(lowestValue, highestValue);
    double span = highestValue - lowestValue;
    if (!(span > 0.0) || !std::isfinite(span)) {
        const double center = std::isfinite(lowestValue) ? lowestValue : 0.0;
        span = std::max(std::abs(center) * 0.005, 0.01);
        lowestValue = center - span * 0.5;
        highestValue = center + span * 0.5;
    }
    const double safePaddingFraction = std::clamp(paddingFraction, 0.0, 0.50);
    const double padding = std::max(
        span * safePaddingFraction,
        std::max(std::abs(lowestValue), std::abs(highestValue)) * 1e-9);
    const double paddedLow = lowestValue - padding;
    const double paddedHigh = highestValue + padding;
    const double rawMedian = (lowestValue + highestValue) * 0.5;
    const double requiredHalfSpan = std::max(rawMedian - paddedLow, paddedHigh - rawMedian);
    // Price charts use four intervals. A half-unit floor keeps flat and nearly
    // flat quotes readable without emitting unstable sub-cent tick labels.
    constexpr double minimumPriceStep = 0.5;
    double step = std::max(minimumPriceStep,
                           NiceAxisStepCeil(requiredHalfSpan / 2.0));
    UnifiedYAxis axis;
    for (int attempt = 0; attempt < 10; ++attempt) {
        const double roundedMedian = std::round(rawMedian / step) * step;
        axis.bottom = roundedMedian - step * 2.0;
        axis.top = roundedMedian + step * 2.0;
        const double epsilon = step * 1e-9;
        if (axis.bottom <= paddedLow + epsilon && axis.top + epsilon >= paddedHigh)
            break;
        step = std::max(minimumPriceStep, NiceAxisStepCeil(step * 1.01));
    }
    if (!(axis.top > axis.bottom) || !std::isfinite(axis.bottom) || !std::isfinite(axis.top)) {
        axis.bottom = lowestValue - span * 0.1;
        axis.top = highestValue + span * 0.1;
        step = std::max(minimumPriceStep, (axis.top - axis.bottom) / 4.0);
    }
    if (nonNegative && axis.bottom < 0.0) {
        axis.bottom = 0.0;
        step = std::max(
            minimumPriceStep,
            NiceAxisStepCeil(
                std::max(paddedHigh, 0.01) / (double)(axis.ticks.size() - 1)));
        axis.top = step * (double)(axis.ticks.size() - 1);
    }
    for (size_t i = 0; i < axis.ticks.size(); ++i)
        axis.ticks[i] = axis.bottom + step * (double)i;
    return axis;
}

} // namespace squarestar::presentation
