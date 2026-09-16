#pragma once

struct GLFWwindow;

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

bool SetLiteMonitorMode(squarestar::application::AppState& state, bool enabled);
void RenderLiteGui(squarestar::application::AppState& state, GLFWwindow* window);

}
