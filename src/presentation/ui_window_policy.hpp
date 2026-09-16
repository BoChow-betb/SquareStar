#pragma once

#include <cstddef>
#include <span>

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
UiSize FitWindowToBounds(UiSize desired,
                         UiRect bounds,
                         float margin) noexcept;


std::size_t SelectOwningWorkArea(std::span<const UiRect> workAreas,
                                 UiRect owner) noexcept;

}
