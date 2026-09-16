#include "application/main_loop_signal.hpp"

#include <atomic>
#include <limits>
#include <mutex>
#include <utility>

namespace squarestar::application {
namespace {

std::mutex g_MainLoopWakeNotifierMutex;
MainLoopWakeNotifier g_MainLoopWakeNotifier;
std::atomic_uint64_t g_GuiRedrawRevision{1};
std::atomic<std::int64_t> g_GuiWakeDeadlineNanoseconds{0};

std::int64_t SteadyNanoseconds(std::chrono::steady_clock::time_point point) noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               point.time_since_epoch())
        .count();
}

}

void SetMainLoopWakeNotifier(MainLoopWakeNotifier notifier) {
    std::lock_guard<std::mutex> lock(g_MainLoopWakeNotifierMutex);
    g_MainLoopWakeNotifier = std::move(notifier);
}

void WakeMainLoop() {
    MainLoopWakeNotifier notifier;
    {
        std::lock_guard<std::mutex> lock(g_MainLoopWakeNotifierMutex);
        notifier = g_MainLoopWakeNotifier;
    }
    if (notifier)
        notifier();
}

void RequestGuiRedraw() {
    g_GuiRedrawRevision.fetch_add(1, std::memory_order_release);
    WakeMainLoop();
}

std::uint64_t GuiRedrawRevision() noexcept {
    return g_GuiRedrawRevision.load(std::memory_order_acquire);
}

void RequestGuiWakeAt(std::chrono::steady_clock::time_point deadline) {
    if (deadline == std::chrono::steady_clock::time_point{})
        return;
    const std::int64_t deadlineNs = SteadyNanoseconds(deadline);
    const std::int64_t nowNs = SteadyNanoseconds(std::chrono::steady_clock::now());
    if (deadlineNs <= nowNs) {
        RequestGuiRedraw();
        return;
    }

    std::int64_t current = g_GuiWakeDeadlineNanoseconds.load(std::memory_order_acquire);
    while (current == 0 || deadlineNs < current) {
        if (g_GuiWakeDeadlineNanoseconds.compare_exchange_weak(
                current,
                deadlineNs,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            WakeMainLoop();
            return;
        }
    }
}

double SecondsUntilGuiWakeDeadline() noexcept {
    const std::int64_t deadlineNs =
        g_GuiWakeDeadlineNanoseconds.load(std::memory_order_acquire);
    if (deadlineNs == 0)
        return std::numeric_limits<double>::infinity();
    const std::int64_t nowNs = SteadyNanoseconds(std::chrono::steady_clock::now());
    if (deadlineNs <= nowNs)
        return 0.0;
    return static_cast<double>(deadlineNs - nowNs) / 1'000'000'000.0;
}

void PromoteDueGuiWakeDeadline() {
    const std::int64_t nowNs = SteadyNanoseconds(std::chrono::steady_clock::now());
    std::int64_t deadlineNs =
        g_GuiWakeDeadlineNanoseconds.load(std::memory_order_acquire);
    while (deadlineNs != 0 && deadlineNs <= nowNs) {
        if (g_GuiWakeDeadlineNanoseconds.compare_exchange_weak(
                deadlineNs,
                0,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            RequestGuiRedraw();
            return;
        }
    }
}

} // namespace squarestar::application
