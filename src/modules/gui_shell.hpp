#pragma once

struct GLFWwindow;

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

void ExitPureMonitorMode(squarestar::application::AppState& state);
void RenderStockTerminal(squarestar::application::AppState& state, GLFWwindow* window);

}
