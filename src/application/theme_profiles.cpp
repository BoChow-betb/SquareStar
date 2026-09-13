#include "theme_profiles.hpp"

#include <algorithm>
#include <cstddef>

namespace squarestar::application {

void SetBuiltInThemePreset(AppConfig& state, int mode) {
    mode = std::clamp(mode, 0, 1);
    state.themeModeIndex = mode;
    if (!IsLightGuiTheme(mode)) {
        state.theme = {{0.10f, 0.10f, 0.12f, 1.f},
                       {0.90f, 0.90f, 0.91f, 1.f},
                       {0.58f, 0.58f, 0.61f, 1.f},
                       {0.20f, 0.20f, 0.22f, 1.f},
                       {0.30f, 0.30f, 0.32f, 1.f},
                       {0.40f, 0.40f, 0.42f, 1.f},
                       {0.15f, 0.15f, 0.18f, 1.f},
                       {0.20f, 0.20f, 0.23f, 1.f},
                       {0.25f, 0.25f, 0.28f, 1.f},
                       {0.20f, 0.20f, 0.20f, 1.f},
                       {0.12f, 0.12f, 0.14f, 1.0f},
                       {0.60f, 0.60f, 0.60f, 1.f},
                       {0.10f, 0.10f, 0.12f, 1.f},
                       {0.20f, 0.20f, 0.22f, 1.f},
                       {0.30f, 0.30f, 0.32f, 1.f}};
    } else {
        state.theme = {{0.96f, 0.96f, 0.96f, 1.f},
                       {0.10f, 0.10f, 0.11f, 1.f},
                       {0.38f, 0.38f, 0.41f, 1.f},
                       {0.82f, 0.82f, 0.82f, 1.f},
                       {0.72f, 0.72f, 0.72f, 1.f},
                       {0.62f, 0.62f, 0.62f, 1.f},
                       {0.90f, 0.90f, 0.90f, 1.f},
                       {0.85f, 0.85f, 0.85f, 1.f},
                       {0.80f, 0.80f, 0.80f, 1.f},
                       {0.80f, 0.80f, 0.80f, 1.f},
                       {0.98f, 0.98f, 0.98f, 1.0f},
                       {0.40f, 0.40f, 0.40f, 1.f},
                       {0.96f, 0.96f, 0.96f, 1.f},
                       {0.82f, 0.82f, 0.82f, 1.f},
                       {0.72f, 0.72f, 0.72f, 1.f}};
    }
    auto setColor = [](float(&dst)[4], float r, float g, float b, float a = 1.0f) {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = a;
    };
    if (!IsLightGuiTheme(mode)) {
        setColor(state.theme.positive, 0.35f, 0.72f, 0.55f);
        setColor(state.theme.negative, 0.86f, 0.43f, 0.45f);
        setColor(state.theme.accent, 0.35f, 0.65f, 0.95f);
        setColor(state.theme.panel, 0.10f, 0.10f, 0.12f);
        setColor(state.theme.panelAlt, 0.15f, 0.15f, 0.18f);
        setColor(state.theme.sidebarBg, 0.12f, 0.12f, 0.14f);
        setColor(
            state.theme.monitorBg, 0.07f, 0.07f, 0.08f); // black/dark stays exclusive to dark mode
        setColor(state.theme.grid, 1.0f, 1.0f, 1.0f, 0.055f);
        setColor(state.theme.searchBg, 0.20f, 0.20f, 0.20f);
        setColor(state.theme.searchHover, 0.25f, 0.25f, 0.25f);
        setColor(state.theme.searchActive, 0.30f, 0.30f, 0.30f);
        setColor(state.theme.buttonHover, 0.28f, 0.28f, 0.30f);
        setColor(state.theme.buttonActive, 0.15f, 0.15f, 0.18f);
        setColor(state.theme.floatingBg, 0.15f, 0.15f, 0.18f);
        setColor(state.theme.floatingBorder, 0.25f, 0.25f, 0.28f);
        setColor(state.theme.inverseBg, 0.84f, 0.84f, 0.86f);
        setColor(state.theme.inverseText, 0.08f, 0.08f, 0.09f);
        setColor(state.theme.wipeBg, 0.12f, 0.12f, 0.14f);
        setColor(state.theme.startupBg, 0.10f, 0.10f, 0.12f);
        setColor(state.theme.startupText, 0.90f, 0.90f, 0.90f);
        setColor(state.theme.sidebarText, 0.70f, 0.70f, 0.70f);
        setColor(state.theme.tabActive, 0.20f, 0.20f, 0.24f);
        setColor(state.theme.tabInactive, 0.11f, 0.11f, 0.13f);
        setColor(state.theme.tabHovered, 0.26f, 0.26f, 0.30f);
        setColor(state.theme.dimOverlay, 0.0f, 0.0f, 0.0f, 0.70f);
        setColor(state.theme.modalBg, 0.15f, 0.15f, 0.15f);
        setColor(state.theme.overviewActive, 0.82f, 0.82f, 0.84f);
        setColor(state.theme.overviewActiveText, 0.08f, 0.08f, 0.09f);
        setColor(state.theme.overviewIdleText, 0.55f, 0.55f, 0.58f);
        setColor(state.theme.overviewIdle, 1.0f, 1.0f, 1.0f, 0.06f);
        setColor(state.theme.overviewHover, 1.0f, 1.0f, 1.0f, 0.12f);
        setColor(state.theme.overviewHeader, 0.18f, 0.18f, 0.20f);
        setColor(state.theme.overviewAltRow, 1.0f, 1.0f, 1.0f, 0.02f);
        setColor(state.theme.exitBg, 0.12f, 0.12f, 0.14f);
        setColor(state.theme.exitText, 0.85f, 0.85f, 0.85f);
        setColor(state.theme.exitHover, 0.25f, 0.25f, 0.25f);
        setColor(state.theme.exitActive, 0.35f, 0.35f, 0.35f);
        setColor(state.theme.danger, 0.72f, 0.20f, 0.22f);
        setColor(state.theme.dangerHover, 0.86f, 0.30f, 0.32f);
        setColor(state.theme.dangerActive, 0.60f, 0.15f, 0.17f);
        setColor(state.theme.dangerText, 1.0f, 1.0f, 1.0f);
        setColor(state.theme.plotBg, 0.10f, 0.10f, 0.12f);
        state.theme.chartShadeAlpha = 0.12f;
    } else {
        setColor(state.theme.positive, 0.12f, 0.44f, 0.30f);
        setColor(state.theme.negative, 0.62f, 0.22f, 0.24f);
        setColor(state.theme.accent, 0.18f, 0.43f, 0.78f);
        setColor(state.theme.panel, 1.0f, 1.0f, 1.0f);
        setColor(state.theme.panelAlt, 0.96f, 0.96f, 0.97f);
        setColor(state.theme.sidebarBg, 0.92f, 0.92f, 0.92f);
        setColor(state.theme.monitorBg, 1.0f, 1.0f, 1.0f); // monitor light mode is solid white
        setColor(state.theme.grid, 0.0f, 0.0f, 0.0f, 0.055f);
        setColor(state.theme.searchBg, 0.85f, 0.85f, 0.85f);
        setColor(state.theme.searchHover, 0.80f, 0.80f, 0.80f);
        setColor(state.theme.searchActive, 0.75f, 0.75f, 0.75f);
        setColor(state.theme.buttonHover, 0.88f, 0.89f, 0.90f);
        setColor(state.theme.buttonActive, 0.82f, 0.83f, 0.85f);
        setColor(state.theme.floatingBg, 0.95f, 0.95f, 0.95f);
        setColor(state.theme.floatingBorder, 0.80f, 0.80f, 0.80f);
        setColor(state.theme.inverseBg, 0.16f, 0.16f, 0.18f);
        setColor(state.theme.inverseText, 0.96f, 0.96f, 0.97f);
        setColor(state.theme.wipeBg, 0.92f, 0.92f, 0.92f);
        setColor(state.theme.startupBg, 0.96f, 0.96f, 0.96f);
        setColor(state.theme.startupText, 0.08f, 0.08f, 0.08f);
        setColor(state.theme.sidebarText, 0.30f, 0.30f, 0.30f);
        setColor(state.theme.tabActive, 0.985f, 0.985f, 0.99f);
        setColor(state.theme.tabInactive, 0.93f, 0.94f, 0.96f);
        setColor(state.theme.tabHovered, 0.96f, 0.965f, 0.975f);
        setColor(state.theme.dimOverlay, 1.0f, 1.0f, 1.0f, 0.60f);
        setColor(state.theme.modalBg, 0.95f, 0.95f, 0.95f);
        setColor(state.theme.overviewActive, 0.24f, 0.24f, 0.26f);
        setColor(state.theme.overviewActiveText, 0.96f, 0.96f, 0.97f);
        setColor(state.theme.overviewIdleText, 0.55f, 0.55f, 0.58f);
        setColor(state.theme.overviewIdle, 0.0f, 0.0f, 0.0f, 0.03f);
        setColor(state.theme.overviewHover, 0.0f, 0.0f, 0.0f, 0.08f);
        setColor(state.theme.overviewHeader, 0.92f, 0.92f, 0.95f);
        setColor(state.theme.overviewAltRow, 0.0f, 0.0f, 0.0f, 0.015f);
        setColor(state.theme.exitBg, 0.94f, 0.94f, 0.95f);
        setColor(state.theme.exitText, 0.15f, 0.15f, 0.15f);
        setColor(state.theme.exitHover, 0.85f, 0.85f, 0.85f);
        setColor(state.theme.exitActive, 0.75f, 0.75f, 0.75f);
        setColor(state.theme.danger, 0.72f, 0.20f, 0.22f);
        setColor(state.theme.dangerHover, 0.84f, 0.27f, 0.29f);
        setColor(state.theme.dangerActive, 0.60f, 0.14f, 0.16f);
        setColor(state.theme.dangerText, 1.0f, 1.0f, 1.0f);
        setColor(state.theme.plotBg, 0.985f, 0.985f, 0.99f);
        state.theme.chartShadeAlpha = 0.085f;
    }
    state.theme.border[3] = 0.58f;
    state.theme.button[3] = 0.48f;
    state.theme.buttonHover[3] = 0.72f;
    state.theme.buttonActive[3] = 0.88f;
}
void InitializeThemeProfiles(AppConfig& state) {
    SetBuiltInThemePreset(state, 0);
}
void SetThemePreset(AppConfig& state, int mode) {
    SetBuiltInThemePreset(state, mode);
}

} // namespace squarestar::application
