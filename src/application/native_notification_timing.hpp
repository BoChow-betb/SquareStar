#pragma once

#include <cstdint>

namespace squarestar::application {

enum class NativeNotificationPhase {
    Enter,
    Hold,
    Exit,
    Complete,
};

struct NativeNotificationTimeline {
    NativeNotificationPhase phase = NativeNotificationPhase::Complete;
    double visibility = 0.0;
    std::uint32_t nextWakeMs = 0;
};

// Pure scheduling policy for the Win32 notification adapter. A non-zero
// nextWakeMs is the only time the platform layer needs a timer. During Hold it
// points directly at the exit deadline rather than requesting animation ticks.
[[nodiscard]] NativeNotificationTimeline EvaluateNativeNotificationTimeline(
    std::uint64_t elapsedMs,
    std::uint64_t enterMs,
    std::uint64_t holdMs,
    std::uint64_t exitMs,
    std::uint32_t animationTickMs) noexcept;

} // namespace squarestar::application
