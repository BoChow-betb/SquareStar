#include "application/native_notification_timing.hpp"

#include <algorithm>
#include <limits>

namespace squarestar::application {
namespace {

double SmoothStep(double value) noexcept {
    value = std::clamp(value, 0.0, 1.0);
    return value * value * (3.0 - 2.0 * value);
}

std::uint32_t ClampWake(std::uint64_t milliseconds) noexcept {
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(
        milliseconds,
        1,
        std::numeric_limits<std::uint32_t>::max()));
}

}

NativeNotificationTimeline EvaluateNativeNotificationTimeline(
    std::uint64_t elapsedMs,
    std::uint64_t enterMs,
    std::uint64_t holdMs,
    std::uint64_t exitMs,
    std::uint32_t animationTickMs) noexcept {
    const std::uint32_t tick = std::max<std::uint32_t>(1, animationTickMs);
    const std::uint64_t holdAt = enterMs;
    const std::uint64_t exitAt = enterMs + holdMs;
    const std::uint64_t completeAt = exitAt + exitMs;

    if (elapsedMs >= completeAt)
        return {NativeNotificationPhase::Complete, 0.0, 0};

    if (elapsedMs < holdAt) {
        const double progress = enterMs == 0
                                    ? 1.0
                                    : static_cast<double>(elapsedMs) /
                                          static_cast<double>(enterMs);
        return {NativeNotificationPhase::Enter, SmoothStep(progress), tick};
    }

    if (elapsedMs < exitAt) {
        return {NativeNotificationPhase::Hold,
                1.0,
                ClampWake(exitAt - elapsedMs)};
    }

    const double progress = exitMs == 0
                                ? 1.0
                                : static_cast<double>(elapsedMs - exitAt) /
                                      static_cast<double>(exitMs);
    return {NativeNotificationPhase::Exit, 1.0 - SmoothStep(progress), tick};
}

}
