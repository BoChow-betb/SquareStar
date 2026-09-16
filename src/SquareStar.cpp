


#if __cplusplus < 202002L
#error SquareStar requires C++20. Compile with C++20 or newer.
#endif

#include "benchmark_runner.hpp"
#include "modules/main.hpp"

#include <chrono>

int main(int argc, char** argv) {
    const auto mainEntry = std::chrono::steady_clock::now();
    const squarestar::benchmark::CommandLineDispatch benchmarkDispatch =
        squarestar::benchmark::HandleBenchmarkCommandLine(argc, argv, mainEntry);
    if (benchmarkDispatch.handled)
        return benchmarkDispatch.exitCode;
    const int appExitCode = squarestar::shell::RunSquareStar();
    if (appExitCode != 0)
        return appExitCode;
    return squarestar::benchmark::GuiProbeExitCode();
}
