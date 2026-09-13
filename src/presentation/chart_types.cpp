#include "chart_types.hpp"

namespace squarestar::presentation {

const char* ChartVisualTypeName(ChartVisualType type) noexcept {
    return type == ChartVisualType::Candlestick ? "Candlestick" : "Line + shaded area";
}

const char* ChartVisualTypeSlug(ChartVisualType type) noexcept {
    return type == ChartVisualType::Candlestick ? "candlestick" : "line";
}

} // namespace squarestar::presentation
