#pragma once

#include <vector>

#include "imgui.h"

namespace squarestar::application {

struct ScreenerRenderCache {
    std::vector<ImVec2> sparklineUnitPoints;
};

} // namespace squarestar::application
