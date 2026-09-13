#include "platform/embedded_resource.hpp"
#include "platform/embedded_resource_ids.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace squarestar::platform {
namespace {

#ifdef _WIN32
int ResourceIdForName(std::string_view name) noexcept {
    if (name == "key.wav")
        return SQUARESTAR_RESOURCE_KEY_WAV;
    if (name == "click.wav")
        return SQUARESTAR_RESOURCE_CLICK_WAV;
    if (name == "decline.wav")
        return SQUARESTAR_RESOURCE_DECLINE_WAV;
    if (name == "error.wav")
        return SQUARESTAR_RESOURCE_ERROR_WAV;
    if (name == "gain.wav")
        return SQUARESTAR_RESOURCE_GAIN_WAV;
    if (name == "launch.wav")
        return SQUARESTAR_RESOURCE_LAUNCH_WAV;
    if (name == "loading.wav")
        return SQUARESTAR_RESOURCE_LOADING_WAV;
    if (name == "loss.wav")
        return SQUARESTAR_RESOURCE_LOSS_WAV;
    if (name == "marketopenclose.wav")
        return SQUARESTAR_RESOURCE_MARKET_OPEN_CLOSE_WAV;
    if (name == "off.wav")
        return SQUARESTAR_RESOURCE_OFF_WAV;
    if (name == "on.wav")
        return SQUARESTAR_RESOURCE_ON_WAV;
    if (name == "transition.wav")
        return SQUARESTAR_RESOURCE_TRANSITION_WAV;
    if (name == "Outfit-Bold.ttf")
        return SQUARESTAR_RESOURCE_OUTFIT_BOLD_TTF;
    if (name == "Outfit-Medium.ttf")
        return SQUARESTAR_RESOURCE_OUTFIT_MEDIUM_TTF;
    if (name == "Outfit-Regular.ttf")
        return SQUARESTAR_RESOURCE_OUTFIT_REGULAR_TTF;
    if (name == "Outfit-SemiBold.ttf")
        return SQUARESTAR_RESOURCE_OUTFIT_SEMIBOLD_TTF;
    if (name == "LICENSE")
        return SQUARESTAR_RESOURCE_PROJECT_LICENSE;
    if (name == "THIRD_PARTY_NOTICES.md")
        return SQUARESTAR_RESOURCE_THIRD_PARTY_NOTICES;
    if (name == "ASSET_CREDITS.md")
        return SQUARESTAR_RESOURCE_ASSET_CREDITS;
    if (name == "PRIVACY.md")
        return SQUARESTAR_RESOURCE_PRIVACY_NOTICE;
    if (name == "DATA_PROVIDER_NOTICE.md")
        return SQUARESTAR_RESOURCE_DATA_PROVIDER_NOTICE;
    if (name == "licenses/Dear-ImGui.txt")
        return SQUARESTAR_RESOURCE_DEAR_IMGUI_LICENSE;
    if (name == "licenses/ImPlot.txt")
        return SQUARESTAR_RESOURCE_IMPLOT_LICENSE;
    if (name == "licenses/yyjson.txt")
        return SQUARESTAR_RESOURCE_YYJSON_LICENSE;
    if (name == "licenses/GLFW.txt")
        return SQUARESTAR_RESOURCE_GLFW_LICENSE;
    if (name == "licenses/curl.txt")
        return SQUARESTAR_RESOURCE_CURL_LICENSE;
    if (name == "licenses/Outfit-OFL-1.1.txt")
        return SQUARESTAR_RESOURCE_OUTFIT_LICENSE;
    if (name == "licenses/CC-BY-4.0.txt")
        return SQUARESTAR_RESOURCE_CC_BY_4_LICENSE;
    return 0;
}
#endif

} // namespace

EmbeddedResourceView FindEmbeddedResource(std::string_view name) noexcept {
#ifdef _WIN32
    const int resourceId = ResourceIdForName(name);
    if (resourceId == 0)
        return {};

    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource =
        FindResourceA(module, MAKEINTRESOURCEA(resourceId), MAKEINTRESOURCEA(10));
    if (!resource)
        return {};

    const DWORD size = SizeofResource(module, resource);
    const HGLOBAL loaded = LoadResource(module, resource);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    if (!bytes || size == 0)
        return {};
    return {static_cast<const unsigned char*>(bytes), static_cast<std::size_t>(size)};
#else
    (void)name;
    return {};
#endif
}

} // namespace squarestar::platform
