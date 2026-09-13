#pragma once

#include "imgui.h"

namespace squarestar::application {
struct AppState;
}
namespace squarestar::presentation {
struct ObjectFocusRegion;
}

namespace squarestar::shell {

void BeginObjectFocusFrame();
void DrawCurrentWindowFocusOutline(const squarestar::application::AppState& state,
                                   int priority = 3);
void DrawHoveredLastItemFocusOutline(const squarestar::application::AppState& state,
                                     int priority = 0);
void DrawLastItemFocusOutline(const squarestar::application::AppState& state,
                              bool focused,
                              int priority = 0);
void DrawObjectFocusOutline(const squarestar::application::AppState& state,
                            ImVec2 min,
                            ImVec2 max,
                            bool focused,
                            int priority = 0);
void DrawObjectFocusRegion(const squarestar::application::AppState& state,
                           const squarestar::presentation::ObjectFocusRegion& region,
                           bool focused,
                           int priority = 0);
void RenderObjectFocusOverlay(squarestar::application::AppState& state,
                              ImVec2 viewportMin,
                              ImVec2 viewportMax);

} // namespace squarestar::shell
