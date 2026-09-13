#include "presentation/ui_window_policy.hpp"

#include <algorithm>
#include <cmath>

namespace squarestar::presentation {
namespace {

float NonNegative(float value) noexcept {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
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

} // namespace squarestar::presentation
