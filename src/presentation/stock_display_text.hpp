#pragma once

#include <string>
#include <string_view>

#include "domain/stock_data.hpp"
#include "imgui.h"

namespace squarestar::presentation {

std::string CleanExchangeLabel(std::string_view raw);
std::string CleanCompanyDisplayName(std::string_view raw);
void DrawStablePrevCloseLine(ImDrawList* drawList,
                             ImVec2 start,
                             ImVec2 end,
                             ImU32 color,
                             float thickness);
std::string EllipsizeTextBinary(const std::string& text, float maxWidth);

}
