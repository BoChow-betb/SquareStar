#pragma once

#include <cstddef>
#include "imgui.h"

struct GLFWwindow;

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

// Internal terminal-shell sections. Public entry points remain in gui_shell.hpp.
void RenderStartupOverlay(squarestar::application::AppState& state,
                          ImGuiViewport* viewport);
void SynchronizeTerminalNavigationState(squarestar::application::AppState& state,
                                        std::size_t openStockTabs);
float RenderTerminalSidebar(squarestar::application::AppState& state,
                            std::size_t openStockTabs);
void RenderTerminalMainContent(squarestar::application::AppState& state,
                               float currentSidebarWidth,
                               ImU32 appBg);
void HandleTerminalKeyboardShortcuts(squarestar::application::AppState& state,
                                     GLFWwindow* window);
void RenderMonitorStockPicker(squarestar::application::AppState& state,
                              ImGuiViewport* viewport,
                              const ImVec2& workPos,
                              const ImVec2& workSize);
void RenderMonitorExitButton(squarestar::application::AppState& state,
                             ImGuiViewport* viewport,
                             const ImVec2& workPos,
                             const ImVec2& workSize);

} // namespace squarestar::shell
