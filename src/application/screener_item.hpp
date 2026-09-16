#pragma once

#include <cstddef>
#include <vector>

#include "domain/screener_data.hpp"
#include "application/screener_render_cache.hpp"

namespace squarestar::application {


struct ScreenerItem : squarestar::market::ScreenerData,
                      ScreenerRenderCache {};

inline size_t ApproximateScreenerItemsHeapBytes(
    const std::vector<ScreenerItem>& items) noexcept {
    size_t bytes = items.capacity() * sizeof(ScreenerItem);
    for (const ScreenerItem& item : items) {
        bytes += item.sparkline.capacity() * sizeof(float);
        bytes += item.sparklineUnitPoints.capacity() * sizeof(ImVec2);
        bytes += item.symbol.capacity() + item.name.capacity();
    }
    return bytes;
}

}
