#pragma once

struct GLFWwindow;

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

void StartConfiguredApplicationMode(squarestar::application::AppState& state,
                                    GLFWwindow* window);
void RunApplicationMainLoop(GLFWwindow*& window,
                            squarestar::application::AppState& state);

}
