#pragma once

#include <algorithm>

namespace squarestar::platform {

struct NativeWindowBounds {
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
};

struct NativeWindowPosition {
    long x = 0;
    long y = 0;
};

inline NativeWindowPosition CenterVisualWindowBounds(
    const NativeWindowBounds& placementBounds,
    const NativeWindowBounds& visualBounds,
    const NativeWindowBounds& workArea) noexcept {
    const long visualWidth = std::max(1L, visualBounds.right - visualBounds.left);
    const long visualHeight = std::max(1L, visualBounds.bottom - visualBounds.top);
    const long workWidth = std::max(1L, workArea.right - workArea.left);
    const long workHeight = std::max(1L, workArea.bottom - workArea.top);
    const long visualOffsetX = visualBounds.left - placementBounds.left;
    const long visualOffsetY = visualBounds.top - placementBounds.top;
    const long centeredVisualX =
        workArea.left + std::max(0L, (workWidth - visualWidth) / 2L);
    const long centeredVisualY =
        workArea.top + std::max(0L, (workHeight - visualHeight) / 2L);
    return {centeredVisualX - visualOffsetX, centeredVisualY - visualOffsetY};
}

} // namespace squarestar::platform
