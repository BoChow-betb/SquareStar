#pragma once

#include <string>
#include <string_view>

namespace squarestar::news {

// Provider text is untrusted display input. Decode common HTML entities,
// repair the frequent UTF-8-as-Windows-1252 round trip, remove MARC-like
// field debris, and return valid, compact UTF-8 for ImGui.
[[nodiscard]] std::string NormalizeNewsText(std::string_view text);

} // namespace squarestar::news
