#pragma once

#include "application/app_config.hpp"

namespace squarestar::application {

constexpr bool IsLightGuiTheme(int mode) noexcept {
    return mode == 1;
}

void SetBuiltInThemePreset(AppConfig& config, int mode);
void InitializeThemeProfiles(AppConfig& config);
void SetThemePreset(AppConfig& config, int mode);

}
