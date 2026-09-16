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


        return 1.0 / 60.0;
    case GuiFrameRateMode::Cap240:
        return 1.0 / 240.0;
    default:
        return 1.0 / 60.0;
    }
}

double GuiPassiveFrameTargetSeconds(int mode) noexcept {


return std::max(GuiFrameTargetSeconds(mode), 1.0 / 60.0);
}

double SecondsUntilNextWallClockSecond(
    std::chrono::system_clock::time_point now) noexcept {
    using namespace std::chrono;


    const auto next = floor<seconds>(now) + seconds(1) + milliseconds(2);
    return std::clamp(duration<double>(next - now).count(), 0.001, 1.002);
}

}
