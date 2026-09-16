#include "presentation/screener_sparkline.hpp"

#include "presentation/chart_y_axis.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace squarestar::presentation {

float ScreenerRowRevealSequenceSeconds(std::size_t rowCount) noexcept {
    const std::size_t delayedRows = rowCount > 0 ? rowCount - 1 : 0;
    return kScreenerRowRevealSeconds +
           static_cast<float>(delayedRows) * kScreenerRowRevealDelaySeconds;
}

float ScreenerRowRevealProgress(float elapsedSeconds,
                                std::size_t rowIndex) noexcept {
    const float rowElapsed =
        std::max(0.0f, elapsedSeconds) -
        static_cast<float>(rowIndex) * kScreenerRowRevealDelaySeconds;
    const float linear =
        std::clamp(rowElapsed / kScreenerRowRevealSeconds, 0.0f, 1.0f);
    const float inverse = 1.0f - linear;
    return 1.0f - inverse * inverse * inverse;
}

double ScreenerRowPerformanceDirection(bool hasChangePercent,
                                       double changePercent,
                                       std::span<const float> sparkline) noexcept {
    if (hasChangePercent && std::isfinite(changePercent))
        return changePercent;
    if (sparkline.size() >= 2 && std::isfinite(sparkline.front()) &&
        std::isfinite(sparkline.back())) {
        return static_cast<double>(sparkline.back()) -
               static_cast<double>(sparkline.front());
    }
    return 0.0;
}

void BuildSparklineUnitGeometry(std::span<const float> values,
                                std::vector<ImVec2>& unitPoints) {
    unitPoints.clear();
    if (values.size() < 2)
        return;

    const auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
    const UnifiedYAxis yAxis = CalculateUnifiedYAxis(static_cast<double>(*minIt),
                                                     static_cast<double>(*maxIt),
                                                     0.06,
                                                     true);
    const float lo = static_cast<float>(yAxis.bottom);
    const float span = std::max(static_cast<float>(yAxis.top) - lo,
                                std::numeric_limits<float>::epsilon());
    unitPoints.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        unitPoints.emplace_back(static_cast<float>(i) /
                                    static_cast<float>(values.size() - 1),
                                (values[i] - lo) / span);
    }
}

}
