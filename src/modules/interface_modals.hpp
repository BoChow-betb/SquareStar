#pragma once

struct GLFWwindow;

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

void RenderClosingModal(squarestar::application::AppState& state, GLFWwindow* window);
void RenderInterfaceSavePrompt(squarestar::application::AppState& state);

} // namespace squarestar::shell
