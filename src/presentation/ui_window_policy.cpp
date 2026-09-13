#include "presentation/ui_window_policy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace squarestar::presentation {
namespace {

float NonNegative(float value) noexcept {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

float Overlap(float firstMin, float firstLength, float secondMin, float secondLength) noexcept {
    const float firstMax = firstMin + NonNegative(firstLength);
    const float secondMax = secondMin + NonNegative(secondLength);
    return std::max(0.0f, std::min(firstMax, secondMax) - std::max(firstMin, secondMin));
}

} // namespace

UiPoint ClampWindowToBounds(UiPoint desired,
                            UiSize window,
                            UiRect bounds,
                            float margin) noexcept {
    const float safeMargin = NonNegative(margin);
    const float minimumX = bounds.x + safeMargin;
    const float minimumY = bounds.y + safeMargin;
    const float maximumX = std::max(
        minimumX,
        bounds.x + NonNegative(bounds.width) - NonNegative(window.width) - safeMargin);
    const float maximumY = std::max(
        minimumY,
        bounds.y + NonNegative(bounds.height) - NonNegative(window.height) - safeMargin);
    desired.x = std::clamp(std::isfinite(desired.x) ? desired.x : minimumX,
                           minimumX,
                           maximumX);
    desired.y = std::clamp(std::isfinite(desired.y) ? desired.y : minimumY,
                           minimumY,
                           maximumY);
    return desired;
}

UiSize FitWindowToBounds(UiSize desired, UiRect bounds, float margin) noexcept {
    const float safeMargin = NonNegative(margin);
    const float availableWidth =
        std::max(1.0f, NonNegative(bounds.width) - safeMargin * 2.0f);
    const float availableHeight =
        std::max(1.0f, NonNegative(bounds.height) - safeMargin * 2.0f);
    desired.width = std::clamp(NonNegative(desired.width), 1.0f, availableWidth);
    desired.height = std::clamp(NonNegative(desired.height), 1.0f, availableHeight);
    return desired;
}

std::size_t SelectOwningWorkArea(std::span<const UiRect> workAreas,
                                 UiRect owner) noexcept {
    if (workAreas.empty())
        return 0;

    const float ownerCenterX = owner.x + NonNegative(owner.width) * 0.5f;
    const float ownerCenterY = owner.y + NonNegative(owner.height) * 0.5f;
    std::size_t bestIndex = 0;
    float bestOverlap = -1.0f;
    double bestDistanceSquared = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < workAreas.size(); ++index) {
        const UiRect area = workAreas[index];
        const float overlapArea = Overlap(owner.x, owner.width, area.x, area.width) *
                                  Overlap(owner.y, owner.height, area.y, area.height);
        const double deltaX = static_cast<double>(ownerCenterX) -
                              (static_cast<double>(area.x) +
                               static_cast<double>(NonNegative(area.width)) * 0.5);
        const double deltaY = static_cast<double>(ownerCenterY) -
                              (static_cast<double>(area.y) +
                               static_cast<double>(NonNegative(area.height)) * 0.5);
        const double distanceSquared = deltaX * deltaX + deltaY * deltaY;
        if (overlapArea > bestOverlap ||
            (overlapArea >= bestOverlap && distanceSquared < bestDistanceSquared)) {
            bestIndex = index;
            bestOverlap = overlapArea;
            bestDistanceSquared = distanceSquared;
        }
    }
    return bestIndex;
}

} // namespace squarestar::presentation
