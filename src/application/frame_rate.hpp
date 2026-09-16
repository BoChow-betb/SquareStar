#pragma once

#include <chrono>

namespace squarestar::application {

enum class GuiFrameRateMode : int {
    Cap48 = 0,
    VSync = 1,
    Cap240 = 2,
    Eco30 = 3,
};

inline constexpr int GUI_FRAME_RATE_MODE_COUNT = 4;

int NormalizeGuiFrameRateMode(int mode) noexcept;
bool GuiFrameRateUsesVSync(int mode) noexcept;
double GuiFrameTargetSeconds(int mode) noexcept;
double GuiPassiveFrameTargetSeconds(int mode) noexcept;
double SecondsUntilNextWallClockSecond(
    std::chrono::system_clock::time_point now) noexcept;

}
