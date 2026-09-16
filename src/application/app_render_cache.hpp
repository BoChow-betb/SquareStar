#pragma once

#include <cstddef>

#include "imgui.h"
#include "application/notification_render_state.hpp"

namespace squarestar::application {

struct AppRenderCache {
    float sidebarAnim = 64.0f;


int overviewRowsRevealScreenerIndex = -1;
    int overviewRowsRevealPage = -1;
    std::size_t overviewRowsRevealStartIndex = 0;
    std::size_t overviewRowsRevealFirstIndex = 0;
    std::size_t overviewRowsRevealKnownCount = 0;
    float overviewRowsRevealElapsedSeconds = 0.0f;
    float navigationTransition = 1.0f;
    NotificationRenderState notifications;
    float monitorModeTimer = 0.0f;
    int appliedThemeModeIndex = -1;
    bool appliedZeroGraphics = false;
    ImFont* fontNormal = nullptr;
    ImFont* fontData = nullptr;
    ImFont* fontLarge = nullptr;
    ImFont* fontGiant = nullptr;
    ImFont* fontQuote = nullptr;
    float objectFocusAnim = 0.0f;

};

}
