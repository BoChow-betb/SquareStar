#pragma once

struct GLFWwindow;

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

void RenderCustomTitleBar(GLFWwindow* window,
                          squarestar::application::AppState& state);
void ToggleApplicationFullscreen(GLFWwindow* window,
                                 squarestar::application::AppState& state);

} // namespace squarestar::shell
