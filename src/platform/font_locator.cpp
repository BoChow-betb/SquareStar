#include "platform/font_locator.hpp"

#include "platform/application_paths.hpp"
#include "platform/windows_path.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

namespace squarestar::platform {

bool FileExistsForFont(std::string_view path) {
#ifdef _WIN32
    std::error_code error;
    return std::filesystem::is_regular_file(Utf8FilesystemPath(path), error);
#else
    std::ifstream input(std::string(path), std::ios::binary);
    return input.good();
#endif
}

std::string ResolveOutfitFontPath(OutfitWeight weight) {
    static const std::vector<std::string> directories = [] {
        std::vector<std::string> paths;
        const std::string executableDirectory = GetExecutableDirectory();
        if (!executableDirectory.empty()) {
            paths.emplace_back(JoinPath(executableDirectory, ""));
            paths.emplace_back(JoinPath(executableDirectory, "fonts"));
        }
        paths.emplace_back("");
        paths.emplace_back("fonts");
#ifdef _WIN32
        const std::wstring localAppData = EnvironmentPathWide(L"LOCALAPPDATA");
        if (!localAppData.empty())
            paths.emplace_back(JoinPath(WidePathToUtf8(localAppData), "Microsoft\\Windows\\Fonts"));
        paths.emplace_back("C:\\Windows\\Fonts");
#endif
        return paths;
    }();

    static constexpr std::array<std::array<const char*, 6>, 4> names = {{
        {{"Outfit-Regular.ttf",
          "Outfit-Medium.ttf",
          "Outfit-SemiBold.ttf",
          "Outfit-VariableFont_wght.ttf",
          "Outfit[wght].ttf",
          nullptr}},
        {{"Outfit-Medium.ttf",
          "Outfit-Regular.ttf",
          "Outfit-SemiBold.ttf",
          "Outfit-VariableFont_wght.ttf",
          "Outfit[wght].ttf",
          nullptr}},
        {{"Outfit-SemiBold.ttf",
          "Outfit-Semibold.ttf",
          "Outfit-Bold.ttf",
          "Outfit-Medium.ttf",
          "Outfit-VariableFont_wght.ttf",
          "Outfit[wght].ttf"}},
        {{"Outfit-Bold.ttf",
          "Outfit-SemiBold.ttf",
          "Outfit-Semibold.ttf",
          "Outfit-VariableFont_wght.ttf",
          "Outfit[wght].ttf",
          nullptr}},
    }};

    const auto weightIndex = static_cast<std::size_t>(weight);
    if (weightIndex >= names.size())
        return {};

    for (const std::string& directory : directories) {
        for (const char* name : names[weightIndex]) {
            if (!name)
                continue;
            const std::string candidate = JoinPath(directory, name);
            if (FileExistsForFont(candidate))
                return candidate;
        }
    }
    return {};
}

}
