#pragma once

#include <algorithm>

namespace squarestar::presentation {

inline constexpr float kNotificationCardPreferredWidth = 410.0f;
inline constexpr float kNotificationCardMinimumWidth = 280.0f;
inline constexpr float kNotificationViewportHorizontalInset = 24.0f;
inline constexpr float kNotificationContentHorizontalPadding = 36.0f;
inline constexpr float kLinkedNotificationCardMinimumHeight = 112.0f;
inline constexpr float kNotificationViewportTopGap = 4.0f;

[[nodiscard]] inline float NotificationVerticalCapacity(float viewportWorkHeight,
                                                        float reservedTopInset) noexcept {
    return std::max(0.0f,
                    viewportWorkHeight - std::max(0.0f, reservedTopInset) -
                        kNotificationViewportTopGap);
}

[[nodiscard]] inline float NotificationCardWidth(float viewportWorkWidth) noexcept {
    return std::min(kNotificationCardPreferredWidth,
                    std::max(kNotificationCardMinimumWidth,
                             viewportWorkWidth - kNotificationViewportHorizontalInset));
}

[[nodiscard]] inline bool ShouldStackNotificationRowValues(float labelWidth,
                                                           float valueWidth,
                                                           float cardWidth,
                                                           float columnGap = 24.0f) noexcept {
    const float contentWidth =
        std::max(0.0f, cardWidth - kNotificationContentHorizontalPadding);
    return std::max(0.0f, labelWidth) + std::max(0.0f, columnGap) +
               std::max(0.0f, valueWidth) >
           contentWidth;
}

[[nodiscard]] inline float LinkedNotificationCardHeight(float naturalHeight) noexcept {
    return std::max(kLinkedNotificationCardMinimumHeight,
                    std::max(0.0f, naturalHeight));
}

} // namespace squarestar::presentation
