#include "application/debug_diagnostics.hpp"

#include <atomic>
#include <cstdio>

namespace squarestar::application {
namespace {

std::atomic<BackgroundFailureReporter> g_backgroundFailureReporter{nullptr};

}

void SetBackgroundFailureReporter(BackgroundFailureReporter reporter) noexcept {
    g_backgroundFailureReporter.store(reporter, std::memory_order_release);
}

void ReportBackgroundFailure(const char* boundary) noexcept {
    if (!boundary || !*boundary)
        boundary = "background task";

    if (const BackgroundFailureReporter reporter =
            g_backgroundFailureReporter.load(std::memory_order_acquire)) {
        reporter(boundary);
    }

#ifndef NDEBUG
    std::fprintf(stderr,
                 "[SquareStar][debug] %s failed; user-facing fallback applied\n",
                 boundary);
    std::fflush(stderr);
#endif
}

}
