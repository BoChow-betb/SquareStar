#include "platform/window_centering.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

}

int main() {
    using squarestar::platform::CenterVisualWindowBounds;
    using squarestar::platform::NativeWindowBounds;

    const auto centered = CenterVisualWindowBounds(
        NativeWindowBounds{100, 100, 1396, 916},
        NativeWindowBounds{108, 108, 1388, 908},
        NativeWindowBounds{0, 0, 1920, 1040});
    Check(centered.x == 312 && centered.y == 112,
          "window visual frame centers after compensating for invisible DWM borders");

    const auto secondary = CenterVisualWindowBounds(
        NativeWindowBounds{0, 0, 1280, 800},
        NativeWindowBounds{0, 0, 1280, 800},
        NativeWindowBounds{-1920, 40, 0, 1080});
    Check(secondary.x == -1600 && secondary.y == 160,
          "window centering honors a secondary monitor with negative coordinates");

    const auto oversized = CenterVisualWindowBounds(
        NativeWindowBounds{0, 0, 2400, 1200},
        NativeWindowBounds{6, 6, 2394, 1194},
        NativeWindowBounds{0, 0, 1920, 1040});
    Check(oversized.x == -6 && oversized.y == -6,
          "an oversized window remains anchored to the work-area origin");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All window centering policy tests passed\n";
    return EXIT_SUCCESS;
}
