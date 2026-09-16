#include "presentation/chart_series.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace squarestar::presentation {

std::size_t NearestSortedIndex(const std::vector<double>& values,
                               double target,
                               std::size_t count) {
    count = std::min(count, values.size());
    if (count < 2)
        return 0;
    const auto begin = values.begin();
    const auto end = begin + static_cast<std::ptrdiff_t>(count);
    const auto right = std::lower_bound(begin, end, target);
    if (right == begin)
        return 0;
    if (right == end)
        return count - 1;
    const std::size_t rightIndex = static_cast<std::size_t>(std::distance(begin, right));
    return std::abs(values[rightIndex - 1] - target) <=
                   std::abs(values[rightIndex] - target)
               ? rightIndex - 1
               : rightIndex;
}

bool SampleSortedSeriesAtX(const std::vector<double>& x,
                           const std::vector<double>& y,
                           double target,
                           double& value) {
    const std::size_t count = std::min(x.size(), y.size());
    if (count == 0 || target < x.front() || target > x[count - 1])
        return false;
    const auto begin = x.begin();
    const auto end = begin + static_cast<std::ptrdiff_t>(count);
    const auto right = std::lower_bound(begin, end, target);
    if (right == begin) {
        value = y.front();
        return std::isfinite(value);
    }
    if (right == end) {
        value = y[count - 1];
        return std::isfinite(value);
    }
    const std::size_t rightIndex = static_cast<std::size_t>(std::distance(begin, right));
    const std::size_t leftIndex = rightIndex - 1;
    const double span = x[rightIndex] - x[leftIndex];
    if (!(span > 0.0)) {
        value = y[rightIndex];
        return std::isfinite(value);
    }
    const double mix = std::clamp((target - x[leftIndex]) / span, 0.0, 1.0);
    value = y[leftIndex] + (y[rightIndex] - y[leftIndex]) * mix;
    return std::isfinite(value);
}

}
