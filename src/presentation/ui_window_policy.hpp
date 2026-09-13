#pragma once

namespace squarestar::presentation {

struct UiPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct UiSize {
    float width = 0.0f;
    float height = 0.0f;
};

struct UiRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

UiPoint ClampWindowToBounds(UiPoint desired,
                            UiSize window,
                            UiRect bounds,
                            float margin) noexcept;
} // namespace squarestar::presentation
