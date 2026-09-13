#include "application/main_loop_signal.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "main_loop_signal_tests: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace std::chrono_literals;
    using squarestar::application::GuiRedrawRevision;
    using squarestar::application::PromoteDueGuiWakeDeadline;
    using squarestar::application::RequestGuiWakeAt;
    using squarestar::application::SecondsUntilGuiWakeDeadline;
    using squarestar::application::SetMainLoopWakeNotifier;

    std::atomic_int wakeCount{0};
    SetMainLoopWakeNotifier([&] { wakeCount.fetch_add(1, std::memory_order_relaxed); });

    const auto initialRevision = GuiRedrawRevision();
    const auto now = std::chrono::steady_clock::now();
    RequestGuiWakeAt(now + 35ms);
    Require(wakeCount.load(std::memory_order_relaxed) == 1,
            "registering the first deadline should wake the sleeping main loop");

    const double firstRemaining = SecondsUntilGuiWakeDeadline();
    Require(std::isfinite(firstRemaining) && firstRemaining > 0.0 && firstRemaining <= 0.10,
            "the registered GUI deadline should be visible to the scheduler");

    // A later deadline must not replace the earlier one or cause another wake.
    RequestGuiWakeAt(now + 250ms);
    Require(wakeCount.load(std::memory_order_relaxed) == 1,
            "a later deadline should not disturb the current nearest deadline");

    std::this_thread::sleep_for(50ms);
    PromoteDueGuiWakeDeadline();
    Require(GuiRedrawRevision() > initialRevision,
            "a due deadline should become a normal GUI redraw request");
    Require(!std::isfinite(SecondsUntilGuiWakeDeadline()),
            "the consumed deadline should be cleared");

    SetMainLoopWakeNotifier({});
    return 0;
}
