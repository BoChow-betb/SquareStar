#pragma once

#include "application/runtime_decisions.hpp"
#include "platform/frame_pacer.hpp"
#include "imgui.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>

struct GLFWwindow;

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

struct MainLoopState {
    int appliedSwapInterval = 1;
    GLFWwindow* swapIntervalWindow = nullptr;
    std::chrono::steady_clock::time_point lastStockServicePump =
        std::chrono::steady_clock::now();
    double lastGuiRenderAt = 0.0;
    std::time_t lastRenderedWallClockSecond = 0;
    std::uint64_t renderedGuiRevision = 0;
    squarestar::application::GuiWindowGeometry renderedGeometry{-1, -1, -1, -1};
    std::optional<squarestar::platform::GuiFramePacer> guiFramePacer;


    unsigned inputSettleFramesRemaining = 0;
    std::time_t lastPriceAlertMuteCheckSecond = 0;
};

void RenderMainGuiFrame(GLFWwindow* window,
                        squarestar::application::AppState& state,
                        MainLoopState& loop,
                        bool guiInputQueuedAfterEventPump,
                        bool visualWorkPending,
                        std::chrono::steady_clock::time_point frameStart);

}
