#pragma once

struct ImGuiViewport;

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

void RenderForegroundNotifications(squarestar::application::AppState& state,
                                   ImGuiViewport* viewport);

}
