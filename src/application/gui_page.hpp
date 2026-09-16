#pragma once

#include <cstdint>

namespace squarestar::application {


enum class GuiPageKind : std::uint8_t {
    Home,
    Overview,
    Settings,
    Stock,
    Comparison,
    Count
};

}
