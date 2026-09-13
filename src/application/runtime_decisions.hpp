#pragma once

#include <algorithm>
#include <cmath>

namespace squarestar::application {

struct GuiWindowGeometry {
    int logicalWidth = 0;
    int logicalHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;

    friend constexpr bool operator==(const GuiWindowGeometry&,
                                     const GuiWindowGeometry&) = default;
};


struct MonitorTileRect {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
};

[[nodiscard]] inline MonitorTileRect ComputeMonitorTileRect(float workX,
                                                             float workY,
                                                             float workWidth,
                                                             float workHeight,
                                                             int itemIndex,
                                                             int itemCount) noexcept {
    const int count = std::max(1, itemCount);
    const int cols = std::max(1, (int)std::ceil(std::sqrt((double)count)));
    const int rows = std::max(1, (count + cols - 1) / cols);
    const int clampedIndex = std::clamp(itemIndex, 0, count - 1);
    const int row = clampedIndex / cols;
    const int col = clampedIndex % cols;
    const float fCols = static_cast<float>(cols);
    const float fRows = static_cast<float>(rows);
    const float fCol = static_cast<float>(col);
    const float fRow = static_cast<float>(row);
    return {std::floor(workX + workWidth * fCol / fCols),
            std::floor(workY + workHeight * fRow / fRows),
            std::floor(workX + workWidth * static_cast<float>(col + 1) / fCols),
            std::floor(workY + workHeight * static_cast<float>(row + 1) / fRows)};
}

struct GuiRenderDecisionInputs {
    bool inputQueued = false;
    // Keep exactly one small follow-up render window after an input event.
    // This lets hover-driven state changes settle without treating a single
    // mouse/key event as hundreds of milliseconds of continuous activity.
    bool inputSettleFramePending = false;
    bool mouseHeld = false;
    bool visualWorkPending = false;
    bool revisionChanged = false;
    bool periodicRefreshDue = false;
    bool wallClockRefreshDue = false;
    bool windowGeometryChanged = false;
};

[[nodiscard]] constexpr bool ShouldRenderGuiFrame(
    const GuiRenderDecisionInputs& inputs) noexcept {
    return inputs.inputQueued || inputs.inputSettleFramePending ||
           inputs.mouseHeld || inputs.visualWorkPending || inputs.revisionChanged ||
           inputs.periodicRefreshDue || inputs.wallClockRefreshDue ||
           inputs.windowGeometryChanged;
}

[[nodiscard]] constexpr bool ShouldPollGuiEvents(
    bool visualWorkPending,
    bool mouseHeld,
    bool inputSettleFramePending) noexcept {
    return visualWorkPending || mouseHeld || inputSettleFramePending;
}

[[nodiscard]] constexpr bool ShouldSuspendGuiFramePump(
    bool mainWindowSuspended,
    bool independentFloatingWindowInteractive) noexcept {
    return mainWindowSuspended && !independentFloatingWindowInteractive;
}

} // namespace squarestar::application
