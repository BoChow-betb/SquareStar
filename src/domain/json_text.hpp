#pragma once

#include <string>
#include <string_view>

namespace squarestar::text {

std::string EscapeJsonStringValue(std::string_view value);

} // namespace squarestar::text
