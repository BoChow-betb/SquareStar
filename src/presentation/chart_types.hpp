#pragma once

#include <string>

namespace squarestar::presentation {

enum class ChartVisualType { Candlestick, LineShaded };

const char* ChartVisualTypeName(ChartVisualType type) noexcept;
const char* ChartVisualTypeSlug(ChartVisualType type) noexcept;

}
