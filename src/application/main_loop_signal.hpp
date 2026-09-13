#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace squarestar::application {

using MainLoopWakeNotifier = std::function<void()>;

void SetMainLoopWakeNotifier(MainLoopWakeNotifier notifier);
void WakeMainLoop();
void RequestGuiRedraw();
std::uint64_t GuiRedrawRevision() noexcept;

// Components that have a future visual state transition register only their
// nearest wake-up deadline. The main loop does not need to know which feature
// owns the timer; it sleeps until the earliest registered deadline and then
// promotes that wake-up into a normal redraw request.
void RequestGuiWakeAt(std::chrono::steady_clock::time_point deadline);
[[nodiscard]] double SecondsUntilGuiWakeDeadline() noexcept;
void PromoteDueGuiWakeDeadline();

} // namespace squarestar::application
