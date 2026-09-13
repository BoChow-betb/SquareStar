#include "presentation/stock_display_text.hpp"

#include <algorithm>
#include <cmath>

namespace squarestar::presentation {

void DrawStablePrevCloseLine(ImDrawList* drawList,
                             ImVec2 start,
                             ImVec2 end,
                             ImU32 color,
                             float thickness) {
    if (!drawList || std::isnan(start.x) || std::isnan(end.x) || start.x >= end.x)
        return;
    const float snappedY = std::floor(start.y) + 0.5f;
    constexpr float dashLength = 4.0f;
    constexpr float spaceLength = 3.0f;
    constexpr float step = dashLength + spaceLength;
    constexpr int maxDashes = 2000;
    int count = 0;
    for (float x = start.x; x < end.x; x += step) {
        if (++count > maxDashes)
            break;
        const float nextX = std::min(x + dashLength, end.x);
        drawList->AddLine(
            ImVec2(x, snappedY), ImVec2(nextX, snappedY), color, thickness);
    }
}

std::string EllipsizeTextBinary(const std::string& text, float maxWidth) {
    if (maxWidth <= 0.0f)
        return {};
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
        return text;
    constexpr const char* suffix = "...";
    const float suffixWidth = ImGui::CalcTextSize(suffix).x;
    if (suffixWidth >= maxWidth)
        return suffix;
    std::size_t lo = 0;
    std::size_t hi = text.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo + 1) / 2;
        const float width =
            ImGui::CalcTextSize(text.data(), text.data() + mid).x + suffixWidth;
        if (width <= maxWidth)
            lo = mid;
        else
            hi = mid - 1;
    }
    return text.substr(0, lo) + suffix;
}

} // namespace squarestar::presentation
