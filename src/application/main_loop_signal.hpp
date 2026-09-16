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


void RequestGuiWakeAt(std::chrono::steady_clock::time_point deadline);
[[nodiscard]] double SecondsUntilGuiWakeDeadline() noexcept;
void PromoteDueGuiWakeDeadline();

}
