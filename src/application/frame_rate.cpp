#include "frame_rate.hpp"

#include <algorithm>

namespace squarestar::application {

int NormalizeGuiFrameRateMode(int mode) noexcept {
    switch (static_cast<GuiFrameRateMode>(mode)) {
    case GuiFrameRateMode::Cap48:
    case GuiFrameRateMode::VSync:
    case GuiFrameRateMode::Cap240:
    case GuiFrameRateMode::Eco30:
        return mode;
    default:
        return static_cast<int>(GuiFrameRateMode::VSync);
    }
}

bool GuiFrameRateUsesVSync(int mode) noexcept {
    return mode == static_cast<int>(GuiFrameRateMode::VSync);
}

double GuiFrameTargetSeconds(int mode) noexcept {
    switch (static_cast<GuiFrameRateMode>(mode)) {
    case GuiFrameRateMode::Eco30:
        return 1.0 / 30.0;
    case GuiFrameRateMode::Cap48:
        return 1.0 / 48.0;
    case GuiFrameRateMode::VSync:
        // VSync can otherwise run the complete UI at 120/144/240 FPS. Keep a
        // 60 FPS software ceiling while swap interval 1 prevents tearing.
        return 1.0 / 60.0;
    case GuiFrameRateMode::Cap240:
        return 1.0 / 240.0;
    default:
        return 1.0 / 60.0;
    }
}

double GuiPassiveFrameTargetSeconds(int mode) noexcept {
    // Keep passive animation work smooth without letting the high-refresh
    // 240 FPS mode turn every fade/loading indicator into a 240 Hz render
    // loop. VSync therefore remains a true 60 FPS animation mode, 48 FPS
    // stays at 48, Eco stays at 30, and only the 240 FPS mode is reduced to
    // 60 FPS for non-interactive animation work. Settled/idle UI still sleeps
    // in the event-driven main loop, so this does not restore continuous idle
    // rendering.
    return std::max(GuiFrameTargetSeconds(mode), 1.0 / 60.0);
}

double SecondsUntilNextWallClockSecond(
    std::chrono::system_clock::time_point now) noexcept {
    using namespace std::chrono;
    // A tiny post-boundary cushion avoids an early timer wake rendering the old
    // second and then leaving it visible for almost another full second.
    const auto next = floor<seconds>(now) + seconds(1) + milliseconds(2);
    return std::clamp(duration<double>(next - now).count(), 0.001, 1.002);
}

} // namespace squarestar::application
