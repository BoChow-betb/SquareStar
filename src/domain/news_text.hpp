#pragma once

#include <string>
#include <string_view>

namespace squarestar::news {


[[nodiscard]] std::string NormalizeNewsText(std::string_view text);

}
