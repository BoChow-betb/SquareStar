#pragma once

#include "imgui.h"

#ifndef IMGUI_DISABLE
struct ID3D11Device;
struct ID3D11DeviceContext;

IMGUI_IMPL_API bool ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* device_context);
IMGUI_IMPL_API void ImGui_ImplDX11_Shutdown();
IMGUI_IMPL_API void ImGui_ImplDX11_NewFrame();
IMGUI_IMPL_API void ImGui_ImplDX11_RenderDrawData(ImDrawData* draw_data);
IMGUI_IMPL_API bool ImGui_ImplDX11_CreateDeviceObjects();
IMGUI_IMPL_API void ImGui_ImplDX11_InvalidateDeviceObjects();
IMGUI_IMPL_API void ImGui_ImplDX11_UpdateTexture(ImTextureData* tex);
// SquareStar-specific: release only transient vertex/index high-water buffers.
IMGUI_IMPL_API void ImGui_ImplDX11_CompactBufferMemory();
#endif
