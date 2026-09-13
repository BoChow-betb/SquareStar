#include "application/native_notification_timing.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

bool Check(bool condition, const char* message) {
    if (condition)
        return true;
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main() {
    using squarestar::application::EvaluateNativeNotificationTimeline;
    using squarestar::application::NativeNotificationPhase;

    constexpr std::uint64_t enterMs = 220;
    constexpr std::uint64_t holdMs = 4500;
    constexpr std::uint64_t exitMs = 240;
    constexpr std::uint32_t tickMs = 16;

    bool ok = true;
    const auto enter =
        EvaluateNativeNotificationTimeline(100, enterMs, holdMs, exitMs, tickMs);
    ok &= Check(enter.phase == NativeNotificationPhase::Enter &&
                    enter.nextWakeMs == tickMs && enter.visibility > 0.0 &&
                    enter.visibility < 1.0,
                "enter phase must use animation cadence");

    const auto hold =
        EvaluateNativeNotificationTimeline(enterMs, enterMs, holdMs, exitMs, tickMs);
    ok &= Check(hold.phase == NativeNotificationPhase::Hold &&
                    hold.nextWakeMs == holdMs && std::abs(hold.visibility - 1.0) < 1e-9,
                "hold phase must sleep directly to the exit deadline");
    ok &= Check(hold.nextWakeMs != tickMs,
                "hold phase must not retain the 16 ms animation timer");

    const auto holdTail = EvaluateNativeNotificationTimeline(
        enterMs + holdMs - 1, enterMs, holdMs, exitMs, tickMs);
    ok &= Check(holdTail.phase == NativeNotificationPhase::Hold &&
                    holdTail.nextWakeMs == 1,
                "hold tail must schedule only the remaining deadline");

    const auto exit = EvaluateNativeNotificationTimeline(
        enterMs + holdMs, enterMs, holdMs, exitMs, tickMs);
    ok &= Check(exit.phase == NativeNotificationPhase::Exit &&
                    exit.nextWakeMs == tickMs && std::abs(exit.visibility - 1.0) < 1e-9,
                "exit phase must restore animation cadence");

    const auto complete = EvaluateNativeNotificationTimeline(
        enterMs + holdMs + exitMs, enterMs, holdMs, exitMs, tickMs);
    ok &= Check(complete.phase == NativeNotificationPhase::Complete &&
                    complete.nextWakeMs == 0 && std::abs(complete.visibility) < 1e-9,
                "completed notification must have no timer wake");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
