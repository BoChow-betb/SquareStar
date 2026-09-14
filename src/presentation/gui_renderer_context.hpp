#pragma once

#include <string>

#include "platform/windows_headers.hpp"

struct GLFWwindow;
struct ImGuiContext;
struct ImPlotContext;
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace squarestar::presentation {

void SetGuiRendererContexts(ImGuiContext* imguiContext, ImPlotContext* implotContext) noexcept;
void SetGuiRendererBackendData(void* platformBackend, void* rendererBackend) noexcept;
void ClearGuiRendererContextRegistry() noexcept;
ImGuiContext* MainGuiImGuiContext() noexcept;
ImPlotContext* MainGuiImPlotContext() noexcept;
void SetGuiRendererPrimed(bool primed) noexcept;
bool GuiRendererPrimed() noexcept;
bool EnsureGuiRendererContext(GLFWwindow* window);
bool PrimeGuiRendererForStartup(GLFWwindow* window, std::string* failureReason = nullptr);

// Single-device Direct3D 11 runtime shared by the main GLFW window and
// offscreen GUI export.
bool InitializeGuiD3D11(HWND hwnd,
                        int framebufferWidth,
                        int framebufferHeight,
                        std::string* failureReason = nullptr);
void ShutdownGuiD3D11() noexcept;
bool BeginGuiD3D11MainFrame(int framebufferWidth,
                            int framebufferHeight,
                            const float clearColor[4]);
bool PresentGuiD3D11MainFrame(bool vsync = true);
void FlushGuiD3D11() noexcept;
ID3D11Device* GuiD3D11Device() noexcept;
ID3D11DeviceContext* GuiD3D11DeviceContext() noexcept;
std::string GuiD3D11AdapterName();
std::string GuiD3D11FeatureLevelName();

} // namespace squarestar::presentation
