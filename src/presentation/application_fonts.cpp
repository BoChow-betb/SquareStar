#include "presentation/render_memory.hpp"

#include "application/app_state.hpp"
#include "platform/embedded_resource.hpp"
#include "platform/font_locator.hpp"
#include "platform/windows_headers.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>

namespace squarestar::presentation {
namespace {

ImFont* AddEmbeddedFont(ImFontAtlas* atlas,
                        const char* resourceName,
                        float sizePixels,
                        ImFontConfig& config) {
    const squarestar::platform::EmbeddedResourceView resource =
        squarestar::platform::FindEmbeddedResource(resourceName);
    if (!resource || resource.size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return nullptr;
    config.FontDataOwnedByAtlas = false;
    return atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(resource.data),
                                       static_cast<int>(resource.size),
                                       sizePixels,
                                       &config);
}

ImFont* AddEmbeddedOrFileFont(ImFontAtlas* atlas,
                              const char* resourceName,
                              const std::string& filePath,
                              float sizePixels,
                              ImFontConfig& config) {
    if (ImFont* font = AddEmbeddedFont(atlas, resourceName, sizePixels, config))
        return font;
    config.FontDataOwnedByAtlas = true;
    return filePath.empty()
               ? nullptr
               : atlas->AddFontFromFileTTF(filePath.c_str(), sizePixels, &config);
}

#ifdef _WIN32
struct ProcessLifetimeMappedFont {
    const unsigned char* data = nullptr;
    std::size_t size = 0;
};

const ProcessLifetimeMappedFont& MappedSegoeUiSymbolFont() {
    static const ProcessLifetimeMappedFont mapped = [] {
        ProcessLifetimeMappedFont view;
        // Keep this read-only file mapping for the process lifetime. ImGui 1.92
        // requires source font bytes to remain valid, and a mapped system font
        // stays file-backed instead of adding another multi-megabyte private
        // heap allocation. Windows releases the mapping/handles at process exit.
        const HANDLE file = CreateFileW(L"C:\\Windows\\Fonts\\seguisym.ttf",
                                        GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE |
                                            FILE_SHARE_DELETE,
                                        nullptr,
                                        OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL,
                                        nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return view;
        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0 ||
            fileSize.QuadPart > 32 * 1024 * 1024) {
            CloseHandle(file);
            return view;
        }
        const HANDLE mapping =
            CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping) {
            CloseHandle(file);
            return view;
        }
        const void* address = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        if (!address) {
            CloseHandle(mapping);
            CloseHandle(file);
            return view;
        }
        // The mapped view remains valid after both handles are closed; keep the
        // read-only view itself for process lifetime so the bytes remain
        // file-backed rather than copied into the private heap.
        CloseHandle(mapping);
        CloseHandle(file);
        view.data = static_cast<const unsigned char*>(address);
        view.size = static_cast<std::size_t>(fileSize.QuadPart);
        return view;
    }();
    return mapped;
}
#endif

void MergeFinancialSymbolFallback(ImFontAtlas* atlas, ImFont* baseFont, float sizePixels) {
    if (!baseFont)
        return;
    static constexpr const char* kSegoeUiSymbolPath =
        "C:\\Windows\\Fonts\\seguisym.ttf";
    if (!squarestar::platform::FileExistsForFont(kSegoeUiSymbolPath))
        return;
    // Outfit misses some symbols used in news copy. Merge Segoe UI Symbol so
    // operators and arrows do not fall back to '?'.
    static constexpr ImWchar kFinancialSymbolRanges[] = {
        0x20A0, 0x20CF, // Currency symbols
        0x2100, 0x214F, // Letterlike symbols
        0x2190, 0x21FF, // Arrows
        0x2200, 0x22FF, // Mathematical operators
        0,
    };
    ImFontConfig fallbackConfig{};
    fallbackConfig.MergeMode = true;
    fallbackConfig.OversampleH = 1;
    fallbackConfig.OversampleV = 1;
    fallbackConfig.PixelSnapH = true;
    fallbackConfig.DstFont = baseFont;
#ifdef _WIN32
    const ProcessLifetimeMappedFont& mapped = MappedSegoeUiSymbolFont();
    if (mapped.data && mapped.size <=
                           static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        fallbackConfig.FontDataOwnedByAtlas = false;
        atlas->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(mapped.data),
            static_cast<int>(mapped.size),
            sizePixels,
            &fallbackConfig,
            kFinancialSymbolRanges);
        return;
    }
#endif
    atlas->AddFontFromFileTTF(kSegoeUiSymbolPath,
                              sizePixels,
                              &fallbackConfig,
                              kFinancialSymbolRanges);
}

} // namespace

void RebuildApplicationFonts(squarestar::application::AppState& state) {
    ImGuiIO& io = ImGui::GetIO();
    io.FontDefault = nullptr;
    const std::array<ImFont*, 4> fonts = {
        state.render.fontNormal, state.render.fontData, state.render.fontLarge, state.render.fontGiant};
    for (std::size_t index = 0; index < fonts.size(); ++index) {
        ImFont* font = fonts[index];
        if (font && std::find(fonts.begin(), fonts.begin() + index, font) ==
                        fonts.begin() + index)
            io.Fonts->RemoveFont(font);
    }
    ClearApplicationFontPointers(state.render);

    std::string outfitRegular =
        squarestar::platform::ResolveOutfitFontPath(squarestar::platform::OutfitWeight::Regular);
    std::string outfitMedium =
        squarestar::platform::ResolveOutfitFontPath(squarestar::platform::OutfitWeight::Medium);
    std::string outfitSemiBold =
        squarestar::platform::ResolveOutfitFontPath(squarestar::platform::OutfitWeight::SemiBold);
    std::string outfitBold =
        squarestar::platform::ResolveOutfitFontPath(squarestar::platform::OutfitWeight::Bold);
    if (outfitRegular.empty() &&
        squarestar::platform::FileExistsForFont("C:\\Windows\\Fonts\\segoeui.ttf"))
        outfitRegular = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (outfitMedium.empty())
        outfitMedium = outfitRegular;
    if (outfitSemiBold.empty() &&
        squarestar::platform::FileExistsForFont("C:\\Windows\\Fonts\\seguisb.ttf"))
        outfitSemiBold = "C:\\Windows\\Fonts\\seguisb.ttf";
    if (outfitSemiBold.empty())
        outfitSemiBold = outfitMedium;
    if (outfitBold.empty() &&
        squarestar::platform::FileExistsForFont("C:\\Windows\\Fonts\\segoeuib.ttf"))
        outfitBold = "C:\\Windows\\Fonts\\segoeuib.ttf";
    if (outfitBold.empty())
        outfitBold = outfitSemiBold;

    ImFontConfig regularConfig{};
    regularConfig.OversampleH = 1;
    regularConfig.OversampleV = 1;
    regularConfig.PixelSnapH = true;
    ImFontConfig mediumConfig = regularConfig;
    ImFontConfig semiBoldConfig = regularConfig;
    ImFontConfig boldConfig = regularConfig;
    state.render.fontNormal = AddEmbeddedOrFileFont(
        io.Fonts,
        "Outfit-Regular.ttf",
        outfitRegular,
        state.config.theme.fontBody,
        regularConfig);
    MergeFinancialSymbolFallback(
        io.Fonts, state.render.fontNormal, state.config.theme.fontBody);
    state.render.fontData = AddEmbeddedOrFileFont(io.Fonts,
                                                  "Outfit-Medium.ttf",
                                                  outfitMedium,
                                                  state.config.theme.fontData,
                                                  mediumConfig);
    // Keep the large Segoe UI Symbol fallback only on the body/news font.
    // ImGui 1.92 retains font source data for the atlas lifetime, so merging
    // the same system font into every size duplicates both source allocations
    // and baked fallback glyphs. Numeric market-data surfaces do not need that
    // broad arrows/math block and remain on the compact Outfit face.
    state.render.fontLarge = AddEmbeddedOrFileFont(io.Fonts,
                                                   "Outfit-SemiBold.ttf",
                                                   outfitSemiBold,
                                                   state.config.theme.fontTitle,
                                                   semiBoldConfig);
    state.render.fontGiant = AddEmbeddedOrFileFont(io.Fonts,
                                                   "Outfit-Bold.ttf",
                                                   outfitBold,
                                                   state.config.theme.fontHero,
                                                   boldConfig);
    if (!state.render.fontNormal) {
        ImFontConfig cfg{};
        cfg.SizePixels = state.config.theme.fontBody;
        state.render.fontNormal = io.Fonts->AddFontDefault(&cfg);
    }
    if (!state.render.fontData)
        state.render.fontData = state.render.fontNormal;
    if (!state.render.fontLarge)
        state.render.fontLarge = state.render.fontData;
    if (!state.render.fontGiant)
        state.render.fontGiant = state.render.fontLarge;
    state.render.fontQuote = state.render.fontGiant;
    state.render.fontLaunch = state.render.fontGiant;
    io.FontDefault = state.render.fontNormal;
    io.Fonts->CompactCache();
}

} // namespace squarestar::presentation
