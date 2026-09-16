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


double ScreenerRowPerformanceDirection(bool hasChangePercent,
                                       double changePercent,
                                       std::span<const float> sparkline) noexcept;

void BuildSparklineUnitGeometry(std::span<const float> values,
                                std::vector<ImVec2>& unitPoints);

}
