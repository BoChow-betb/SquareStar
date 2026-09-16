#pragma once

#include "imgui.h"

namespace squarestar::presentation {

ImVec4 CrosshairDotColor(bool lightTheme,
                         const ImVec4& source,
                         float alpha = 1.0f) noexcept;

}
