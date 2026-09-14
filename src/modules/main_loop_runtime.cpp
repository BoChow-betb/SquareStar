#include "modules/main_loop_runtime.hpp"

#include "benchmark_runner.hpp"

#include "application/app_state.hpp"
#include "application/frame_rate.hpp"
#include "application/main_loop_signal.hpp"
#include "application/persisted_state.hpp"
#include "application/runtime_state.hpp"
#include "application/screener_executor.hpp"
#include "application/screener_item.hpp"
#include "domain/screener_data.hpp"
#include "domain/stock_data.hpp"
#include "modules/app_services.hpp"
#include "modules/core.hpp"
#include "modules/lite_gui.hpp"
#include "modules/main_loop_render.hpp"
#include "modules/main_loop_schedule.hpp"
#include "modules/market_data.hpp"
#include "modules/platform.hpp"
#include "modules/stock_request_runtime.hpp"
#include "platform/application_paths.hpp"
#include "platform/audio_runtime.hpp"
#include "platform/frame_pacer.hpp"
#include "platform/glfw_runtime.hpp"
#include "platform/process_memory_trim.hpp"
#include "platform/win32_app_state.hpp"
#include "presentation/gui_renderer_context.hpp"
#include "presentation/gui_shell_runtime_state.hpp"
#include "presentation/render_memory.hpp"
#include "services/config_save_queue.hpp"
#include "services/network_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imgui_impl_dx11.h"

namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::application::AppState;
using squarestar::application::AppUiMode;
using squarestar::application::GuiFrameRateUsesVSync;
using squarestar::application::PersistedStateOf;
using squarestar::application::ShouldPollGuiEvents;
using squarestar::platform::Win32AppRuntime;
using squarestar::presentation::EnsureGuiRendererContext;

void StartConfiguredApplicationMode(AppState& state,
                                    GLFWwindow* window) {
    if (state.config.lastOpenMode == 2) {
        ApplicationRuntime().SetUiMode(AppUiMode::LiteGui);
        EnterLiteGuiWorkspace(window, state);
        // Launch into the LiteGUI home/search surface ready for typing. This is
        // a one-shot request consumed by RenderIntegratedSearchBar, so focus is
        // not stolen again after the user clicks elsewhere.
        if (!squarestar::benchmark::SuppressExternalWork() &&
            state.marketData.activeContexts.empty())
            state.navigation.liteSearch.focusRequested = true;
    } else {
        ApplicationRuntime().SetUiMode(AppUiMode::Gui);
        // Match command-palette style startup UX on Home: the first printable
        // key should go straight into stock search without an initial click.
        // Preserve restored Overview/Settings/stock sessions by requesting
        // focus only when the launch destination is actually Home.
        if (!squarestar::benchmark::SuppressExternalWork() &&
            state.navigation.activeSidebarTab == squarestar::application::SidebarTab::Home &&
            state.marketData.activeContexts.empty())
            state.navigation.mainSearch.focusRequested = true;
    }
    glfwShowWindow(window);
    glfwPollEvents();
    if (!squarestar::benchmark::SuppressExternalWork()) {
        // Keep the network runtime lazy. Pre-creating the HTTP/background pools
        // here leaves worker stacks plus libcurl/TLS connection state resident
        // even when the user opens SquareStar and simply leaves it idle. The
        // first real request creates exactly the pools it needs.
        if (ApplicationRuntime().CurrentUiMode() == AppUiMode::Gui &&
            state.config.soundEnabled)
            PlayUISound("launch.wav", state);
    }
}

void RunApplicationMainLoop(GLFWwindow*& window, AppState& state) {
    MainLoopState loop;
    const double initialGuiClock =
        squarestar::platform::GlfwPlatformRuntimeInitialized() ? glfwGetTime() : 0.0;
    loop.lastGuiRenderAt = initialGuiClock;
    loop.lastRenderedWallClockSecond = std::time(nullptr);

    while ((!window || !glfwWindowShouldClose(window)) &&
           !ApplicationRuntime().QuitRequested()) {
        const auto frameStart = std::chrono::steady_clock::now();
        const double stockServiceElapsed =
            std::chrono::duration<double>(frameStart - loop.lastStockServicePump).count();
        loop.lastStockServicePump = frameStart;
        {
            PumpInterfaceTransitionsOnMainThread(window, state);
            (void)squarestar::config::FlushPendingConfigSave(PersistedStateOf(state));
            if (const auto failure =
                    squarestar::config::ConsumeConfigSaveFailureNotice()) {
                const std::string diagnosticsPath =
                    squarestar::platform::GetDiagnosticLogPath();
                squarestar::application::UserFeedback saveFailureFeedback;
                saveFailureFeedback.type =
                    squarestar::application::UserFeedbackType::Error;
                saveFailureFeedback.title = "Settings could not be saved";
                saveFailureFeedback.body =
                    "Your current changes remain in memory. SquareStar will retry "
                    "automatically with bounded backoff.";
                saveFailureFeedback.duration = std::chrono::seconds(9);
                saveFailureFeedback.destination =
                    squarestar::application::UserFeedbackDestination::Automatic;
                saveFailureFeedback.actionLabel =
                    diagnosticsPath.empty() ? std::string{} : "Open diagnostics";
                saveFailureFeedback.actionPath = diagnosticsPath;
                PublishUserFeedback(state, std::move(saveFailureFeedback));
            }
            if (ExpirePriceAlertSettlementMutes(
                    state, loop.lastPriceAlertMuteCheckSecond))
                squarestar::config::RequestConfigSave();
            ReconcilePriceAlertContexts(state);
            PumpMarketOpenSound(state);
            PumpPriceAlertMonitorRequests(state);
            PumpPriceAlertSounds(state);
        }
        if (!EnsureGuiRendererContext(window)) {
            MessageBoxA(
                nullptr,
                "The ImGui/ImPlot context or backend userdata became invalid. SquareStar will close without entering an unsafe frame.",
                "SquareStar renderer error",
                MB_OK | MB_ICONERROR | MB_TOPMOST);
            ApplicationRuntime().RequestQuit();
            continue;
        }
        PollChartFileExport(state);
        const int desiredSwapInterval = GuiFrameRateUsesVSync(state.config.fpsMode) ? 1 : 0;
        if (window != loop.swapIntervalWindow || desiredSwapInterval != loop.appliedSwapInterval) {
            glfwSwapInterval(desiredSwapInterval);
            loop.appliedSwapInterval = desiredSwapInterval;
            loop.swapIntervalWindow = window;
        }
        const bool windowIconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0;
        const bool mainWindowSuspended =
            Win32AppRuntime().MinimizedToTray() || windowIconified ||
            !glfwGetWindowAttrib(window, GLFW_VISIBLE);
        const bool windowSuspended = mainWindowSuspended;
        const bool priceAlertAudioPending =
            std::any_of(state.marketData.activeContexts.begin(), state.marketData.activeContexts.end(), [](const auto& ctx) {
                return ctx && ctx->alerts.priceAlertSoundPlaysRemaining > 0;
            }) ||
            std::any_of(state.alerts.Monitors().begin(),
                        state.alerts.Monitors().end(),
                        [](const auto& ctx) {
                            return ctx && ctx->alerts.priceAlertSoundPlaysRemaining > 0;
                        });
        PumpStockAutoRefresh(state, stockServiceElapsed, windowSuspended);
        squarestar::application::PromoteDueGuiWakeDeadline();
        const bool visualWorkPending =
            squarestar::application::GuiRedrawRevision() != loop.renderedGuiRevision;
        const double settledWaitSeconds = GuiSettledWaitSeconds(
            state, loop.lastGuiRenderAt, windowSuspended, priceAlertAudioPending);
        if (windowSuspended) {
            ApplicationRuntime().RequestGuiFrameDeltaReset();
            glfwWaitEventsTimeout(settledWaitSeconds);
        } else {
            const bool inputSettleFramePending = loop.inputSettleFramesRemaining != 0;
            if (ShouldPollGuiEvents(visualWorkPending,
                                    ImGui::IsAnyMouseDown(),
                                    inputSettleFramePending)) {
                glfwPollEvents();
            } else {
                // Native input and background completions wake this immediately; only explicit
                // redraw work keeps the loop active afterward.
                glfwWaitEventsTimeout(settledWaitSeconds);
            }
        }
        squarestar::application::PromoteDueGuiWakeDeadline();
        if (squarestar::benchmark::PollGuiProbe())
            continue;

        // Inspect the queue after both polling paths. Secondary ImGui viewport
        // windows use backend-owned callbacks, so their activation click may
        // not update the host window's activity timestamps even though the
        // input event is ready. Rendering it immediately avoids combining the
        // press and release after another idle wait.
        const bool guiInputQueuedAfterEventPump =
            GImGui && GImGui->InputEventsQueue.Size > 0;
        if (guiInputQueuedAfterEventPump)
            loop.inputSettleFramesRemaining = 2;

        PumpCompletedStockRequests(state, windowSuspended);

        // After a quiet period, ask the LFH to release unused backing pages.
        // Skip this while animation or input is waiting for a frame.
        const auto memoryTrimNow = std::chrono::steady_clock::now();
        const bool memoryTrimQuiet =
            !squarestar::application::ScreenerJobBusy() &&
            !guiInputQueuedAfterEventPump && !ImGui::IsAnyMouseDown() &&
            loop.inputSettleFramesRemaining == 0 &&
            squarestar::application::GuiRedrawRevision() ==
                loop.renderedGuiRevision;
        if (!memoryTrimQuiet) {
            loop.memoryTrimIdleSince = memoryTrimNow;
        } else {
            const bool idleLongEnough =
                memoryTrimNow - loop.memoryTrimIdleSince >=
                std::chrono::seconds(5);
            const bool trimDue =
                loop.lastMemoryTrimAt == std::chrono::steady_clock::time_point{} ||
                memoryTrimNow - loop.lastMemoryTrimAt >=
                    std::chrono::seconds(20);
            if (idleLongEnough && trimDue) {
                // First drop application/renderer high-water buffers, then ask
                // the Windows LFH to decommit pages they occupied. Avoid
                // compacting a visible stock/comparison surface that is about
                // to redraw its clock/live labels every second; that would only
                // force the same draw buffers to be reallocated immediately.
                const auto trimPage = CurrentGuiPage(state);
                const bool compactRenderer =
                    windowSuspended ||
                    GuiPeriodicRefreshSeconds(state, trimPage) >= 5.0;
                if (compactRenderer) {
                    // Compact only transient draw/scratch storage. Deeper
                    // device-object and per-stock cache teardown would create
                    // avoidable reallocation churn on the next visit.
                    squarestar::presentation::CompactGuiTransientMemory();
                    ImGui_ImplDX11_CompactBufferMemory();
                }
                squarestar::platform::TrimIdleAppAudioMemory();
                (void)squarestar::platform::TrimProcessPrivateMemory();
                loop.lastMemoryTrimAt = memoryTrimNow;
            }
        }

        if (windowSuspended)
            continue;
        RenderMainGuiFrame(window,
                           state,
                           loop,
                           guiInputQueuedAfterEventPump,
                           visualWorkPending,
                           frameStart);
    }
}


} // namespace squarestar::shell
