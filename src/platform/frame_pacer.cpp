#include "frame_pacer.hpp"

#include "platform/windows_headers.hpp"
#include <algorithm>
#include <cstdint>
#include <thread>


namespace squarestar::platform {

GuiFramePacer::GuiFramePacer() {
    // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION is 0x2. Spell out the value so
    // older MinGW headers can still build while Windows 10/11 use the
    // high-resolution scheduler.
    constexpr DWORD highResolutionTimerFlag = 0x00000002;
    timer_ = CreateWaitableTimerExW(nullptr,
                                    nullptr,
                                    highResolutionTimerFlag,
                                    TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!timer_)
        timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
}

GuiFramePacer::~GuiFramePacer() {
    if (timer_)
        CloseHandle(timer_);
}

void GuiFramePacer::WaitFor(std::chrono::steady_clock::duration duration) const {
    if (duration <= std::chrono::steady_clock::duration::zero())
        return;
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    LARGE_INTEGER dueTime{};
    dueTime.QuadPart = -std::max<int64_t>(1, nanoseconds / 100);
    if (timer_ && SetWaitableTimer(timer_, &dueTime, 0, nullptr, nullptr, FALSE) &&
        WaitForSingleObject(timer_, INFINITE) == WAIT_OBJECT_0)
        return;
    std::this_thread::sleep_for(duration);
}

} // namespace squarestar::platform
