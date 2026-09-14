#include "application/ui_animation.hpp"
#include "application/app_state.hpp"

#include <algorithm>
#include <cmath>

namespace squarestar::application {

float UiRounding(const AppState& state, float normalRounding) noexcept {
    return state.ZeroGraphicsEnabled() ? 0.0f : normalRounding;
}

float EaseOutCubic(float value) noexcept {
    value = 1.0f - std::clamp(value, 0.0f, 1.0f);
    return 1.0f - value * value * value;
}

double SmoothValue(double current,
                   double target,
                   float dt,
                   bool animEnabled,
                   float speed) noexcept {
    if (!animEnabled)
        return target;
    return current + (target - current) * (1.0 - std::exp(-(double)speed * (double)dt));
}

TransientNoticeAnimationStep AdvanceTransientNoticeAnimation(float current,
                                                              bool holding,
                                                              float dt,
                                                              bool animEnabled,
                                                              float speed) noexcept {
    const float target = holding ? 1.0f : 0.0f;
    if (animEnabled) {
        const float response = 1.0f - std::exp(-speed * std::max(0.0f, dt));
        current += (target - current) * std::clamp(response, 0.0f, 1.0f);
    } else {
        current = target;
    }
    current = std::clamp(current, 0.0f, 1.0f);
    const bool clearExpired = !holding && current <= 0.001f;
    return {clearExpired ? 0.0f : current, clearExpired};
}

} // namespace squarestar::application
