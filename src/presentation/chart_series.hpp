#pragma once

#include <cstddef>
#include <limits>
#include <vector>

namespace squarestar::presentation {

std::size_t NearestSortedIndex(
    const std::vector<double>& values,
    double target,
    std::size_t count = std::numeric_limits<std::size_t>::max());

bool SampleSortedSeriesAtX(const std::vector<double>& x,
                           const std::vector<double>& y,
                           double target,
                           double& value);

}
