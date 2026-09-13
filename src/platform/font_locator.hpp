#pragma once

#include <string>
#include <string_view>

namespace squarestar::platform {

enum class OutfitWeight {
    Regular,
    Medium,
    SemiBold,
    Bold,
};

bool FileExistsForFont(std::string_view path);
std::string ResolveOutfitFontPath(OutfitWeight weight);

} // namespace squarestar::platform
