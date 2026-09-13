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

// Handles SquareStar's opt-in benchmark command-line modes. Offline benchmarks
// return handled=true and exit immediately. GUI probes configure process-lifetime
// state and return handled=false so the normal application startup path continues.
CommandLineDispatch HandleBenchmarkCommandLine(
    int argc,
    char** argv,
    std::chrono::steady_clock::time_point mainEntry);

[[nodiscard]] bool GuiProbeActive() noexcept;
[[nodiscard]] bool MemoryLayerProbeActive() noexcept;
[[nodiscard]] bool SuppressExternalWork() noexcept;
[[nodiscard]] int GuiProbeExitCode() noexcept;

// The memory-layer probe follows the normal GUI startup path but records
// Private Bytes/working set after each subsystem boundary. Calls are no-ops
// unless --benchmark-memory is active.
void RecordMemoryLayer(const char* stage, const char* label);

// GUI probes use fixed in-memory defaults and skip provider/network, audio,
// animation, config load/save, and layout I/O.
void PrepareGuiProbeState(squarestar::application::AppState& state);

// Called after the visible host window's Direct3D swap-chain Present() completes.
void OnGuiFramePresented(GLFWwindow* window);

// Called from the main loop after the native event wait. Returns true once the
// probe has requested process shutdown, allowing the caller to skip another
// render that would otherwise contaminate the idle sample.
[[nodiscard]] bool PollGuiProbe();

} // namespace squarestar::benchmark
