#pragma once

#include <cstdint>

namespace squarestar::application {

// Used by the render loop to choose clock and idle-refresh cadence.
// Profiler state lives elsewhere.
enum class GuiPageKind : std::uint8_t {
    Home,
    Overview,
    Settings,
    Stock,
    Comparison,
    Count
};

} // namespace squarestar::application
