#include "presentation/chart_style.hpp"

#include <algorithm>

namespace squarestar::presentation {

ImVec4 CrosshairDotColor(bool lightTheme, const ImVec4& source, float alpha) noexcept {
    const float blend = lightTheme ? 0.24f : 0.30f;
    const float target = lightTheme ? 0.0f : 1.0f;
    return ImVec4(source.x + (target - source.x) * blend,
                  source.y + (target - source.y) * blend,
                  source.z + (target - source.z) * blend,
                  source.w * std::clamp(alpha, 0.0f, 1.0f));
}

} // namespace squarestar::presentation
