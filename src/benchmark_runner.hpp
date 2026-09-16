#pragma once

#include <chrono>

struct GLFWwindow;

namespace squarestar::application {
struct AppState;
}

namespace squarestar::benchmark {

struct CommandLineDispatch {
    bool handled = false;
    int exitCode = 0;
};


CommandLineDispatch HandleBenchmarkCommandLine(
    int argc,
    char** argv,
    std::chrono::steady_clock::time_point mainEntry);

[[nodiscard]] bool GuiProbeActive() noexcept;
[[nodiscard]] bool MemoryLayerProbeActive() noexcept;
[[nodiscard]] bool SuppressExternalWork() noexcept;
[[nodiscard]] int GuiProbeExitCode() noexcept;


void RecordMemoryLayer(const char* stage, const char* label);


void PrepareGuiProbeState(squarestar::application::AppState& state);


void OnGuiFramePresented(GLFWwindow* window);


[[nodiscard]] bool PollGuiProbe();

}
