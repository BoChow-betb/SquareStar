#pragma once

namespace squarestar::application {

struct AppState;

float UiRounding(const AppState& state, float normalRounding) noexcept;
float EaseOutCubic(float value) noexcept;
double SmoothValue(double current,
                   double target,
                   float dt,
                   bool animEnabled,
                   float speed = 14.0f) noexcept;

struct TransientNoticeAnimationStep {
    float value = 0.0f;
    bool clearExpired = false;
};

TransientNoticeAnimationStep AdvanceTransientNoticeAnimation(float current,
                                                              bool holding,
                                                              float dt,
                                                              bool animEnabled,
                                                              float speed = 18.0f) noexcept;

}
