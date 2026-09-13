#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "imgui.h"

namespace squarestar::presentation {

inline constexpr float kScreenerRowRevealSeconds = 0.28f;
inline constexpr float kScreenerRowRevealDelaySeconds = 0.04f;

float ScreenerRowRevealSequenceSeconds(std::size_t rowCount) noexcept;
float ScreenerRowRevealProgress(float elapsedSeconds,
                                std::size_t rowIndex) noexcept;

// Overview rows use one performance color for the displayed CHANGE value and
// its sparkline. Prefer the displayed percentage change when available so a
// positive row cannot be painted red merely because its 5-day trace slopes
// down (and vice versa). The sparkline direction is only a fallback.
double ScreenerRowPerformanceDirection(bool hasChangePercent,
                                       double changePercent,
                                       std::span<const float> sparkline) noexcept;

void BuildSparklineUnitGeometry(std::span<const float> values,
                                std::vector<ImVec2>& unitPoints);

} // namespace squarestar::presentation
