#pragma once

#include <array>
#include <string>

namespace squarestar::presentation {

struct UnifiedYAxis {
    double bottom = 0.0;
    double top = 1.0;
    std::array<double, 5> ticks{};
};

std::string FormatAxisTickValue(double value);

UnifiedYAxis CalculateUnifiedYAxis(double lowestValue,
                                   double highestValue,
                                   double paddingFraction = 0.06,
                                   bool nonNegative = false);

} // namespace squarestar::presentation
