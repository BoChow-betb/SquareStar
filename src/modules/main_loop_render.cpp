#include "modules/main_loop_render.hpp"

#include "benchmark_runner.hpp"

#include "application/app_state.hpp"
#include "application/frame_rate.hpp"
#include "application/main_loop_signal.hpp"
#include "application/runtime_state.hpp"
#include "modules/app_services.hpp"
#include "modules/core.hpp"
#include "modules/currency_display.hpp"
#include "modules/gui_shell.hpp"
#include "modules/lite_gui.hpp"
#include "modules/main_loop_schedule.hpp"
#include "modules/notification_view.hpp"
#include "modules/platform.hpp"
#include "modules/window_chrome.hpp"
#include "platform/frame_pacer.hpp"
#include "presentation/gui_renderer_context.hpp"
#include "presentation/gui_shell_runtime_state.hpp"
#include "presentation/render_memory.hpp"

#include <chrono>
#include <cmath>
#include <ctime>

#include "imgui_impl_dx11.h"
#include "imgui_impl_glfw.h"

namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::application::AppState;
using squarestar::application::AppUiMode;
using squarestar::application::GuiFrameTargetSeconds;
using squarestar::application::GuiPassiveFrameTargetSeconds;
using squarestar::application::GuiRedrawRevision;
using squarestar::application::GuiRenderDecisionInputs;
using squarestar::application::GuiWindowGeometry;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::ShouldRenderGuiFrame;
using squarestar::presentation::RebuildApplicationFonts;
using squarestar::application::GuiPageKind;

namespace {

void RenderGuiSurface(GLFWwindow* window, AppState& state) {
    RenderCustomTitleBar(window, state);
    if (ApplicationRuntime().CurrentUiMode() == AppUiMode::LiteGui)
        RenderLiteGui(state, window);
    else
        RenderStockTerminal(state, window);

    RenderCurrencyPickerPopup(state);

    // Foreground feedback is a shell overlay, not a feature owned by either
    // full GUI or LiteGUI. Keeping the single call here guarantees both modes
    // render the exact same notification cards and interaction actions.
    RenderForegroundNotifications(state, ImGui::GetMainViewport());
}

} // namespace

void RenderMainGuiFrame(GLFWwindow* window,
                               AppState& state,
                               MainLoopState& loop,
                               bool guiInputQueuedAfterEventPump,
                               bool visualWorkPending,
                               std::chrono::steady_clock::time_point frameStart) {
    if (state.requests.fontReloadRequested) {
        RebuildApplicationFonts(state);
        state.requests.fontReloadRequested = false;
        RequestGuiRedraw();
    }
    const GuiPageKind page = CurrentGuiPage(state);
    const double guiNow = glfwGetTime();
    const bool inputSettleFramePending = loop.inputSettleFramesRemaining != 0;
    const bool revisionChanged = GuiRedrawRevision() != loop.renderedGuiRevision;
    const bool periodicRefreshDue =
        guiNow - loop.lastGuiRenderAt >= GuiPeriodicRefreshSeconds(state, page);
    const bool wallClockRefreshDue =
        GuiPageShowsSecondClock(page) &&
        std::time(nullptr) != loop.lastRenderedWallClockSecond;
    const bool mouseHeld = ImGui::IsAnyMouseDown();
    GuiWindowGeometry currentGeometry;
    glfwGetWindowSize(window, &currentGeometry.logicalWidth, &currentGeometry.logicalHeight);
    glfwGetFramebufferSize(
        window, &currentGeometry.framebufferWidth, &currentGeometry.framebufferHeight);
    const bool windowGeometryChanged = currentGeometry != loop.renderedGeometry;
    const GuiRenderDecisionInputs renderInputs{guiInputQueuedAfterEventPump,
                                               inputSettleFramePending,
                                               mouseHeld,
                                               visualWorkPending,
                                               revisionChanged,
                                               periodicRefreshDue,
                                               wallClockRefreshDue,
                                               windowGeometryChanged};
    if (!ShouldRenderGuiFrame(renderInputs)) {
        ApplicationRuntime().RequestGuiFrameDeltaReset();
        return;
    }

    const uint64_t revisionBeingRendered = GuiRedrawRevision();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    if (ApplicationRuntime().ConsumeGuiFrameDeltaReset())
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    RenderGuiSurface(window, state);
    // ImGui's GLFW backend normally updates the native cursor before
    // ImGui::NewFrame(), so an event-driven loop can expose a one-frame cursor
    // delay. Apply the cursor selected by this frame immediately.
    ImGui_ImplGlfw_UpdateMouseCursor();
    ImGui::Render();
    int display_w = 0, display_h = 0;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    const ImVec4 clearColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const float clear[4] = {clearColor.x, clearColor.y, clearColor.z, clearColor.w};
    const bool mainTargetReady =
        squarestar::presentation::BeginGuiD3D11MainFrame(display_w, display_h, clear);
    if (mainTargetReady) {
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (squarestar::presentation::PresentGuiD3D11MainFrame(true))
            squarestar::benchmark::OnGuiFramePresented(window);
    }
#ifdef IMGUI_HAS_VIEWPORT
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        // Detached LiteGUI search results are real platform windows. Create,
        // size and render those after the main swap chain, matching Dear
        // ImGui's standard multi-viewport frame order.
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
#endif
    PumpGuiCapture(window, state, RenderGuiSurface);
    loop.renderedGuiRevision = revisionBeingRendered;
    loop.lastGuiRenderAt = glfwGetTime();
    loop.lastRenderedWallClockSecond = std::time(nullptr);
    loop.renderedGeometry = currentGeometry;
    const bool directInteraction = guiInputQueuedAfterEventPump ||
                                   inputSettleFramePending || mouseHeld;
    const double targetSeconds =
        visualWorkPending && !directInteraction
            ? GuiPassiveFrameTargetSeconds(state.config.fpsMode)
            : GuiFrameTargetSeconds(state.config.fpsMode);
    const double elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count();
    if (elapsedSeconds < targetSeconds) {
        if (!loop.guiFramePacer)
            loop.guiFramePacer.emplace();
        loop.guiFramePacer->WaitFor(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(targetSeconds - elapsedSeconds)));
    }
    if (loop.inputSettleFramesRemaining != 0)
        --loop.inputSettleFramesRemaining;
}


} // namespace squarestar::shell
