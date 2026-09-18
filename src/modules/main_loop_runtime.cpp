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
#include "modules/currency_display.hpp"
#include "modules/lite_gui.hpp"
#include "modules/main_loop_render.hpp"
#include "modules/main_loop_schedule.hpp"
#include "modules/market_data.hpp"
#include "modules/platform.hpp"
#include "modules/stock_request_runtime.hpp"
#include "platform/application_paths.hpp"
#include "platform/frame_pacer.hpp"
#include "platform/glfw_runtime.hpp"
#include "platform/win32_app_state.hpp"
#include "presentation/gui_renderer_context.hpp"
#include "presentation/gui_shell_runtime_state.hpp"
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


namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::application::AppState;
using squarestar::application::AppUiMode;
using squarestar::application::GuiFrameRateUsesVSync;
using squarestar::application::PersistedStateOf;
using squarestar::application::ShouldPollGuiEvents;
using squarestar::platform::Win32AppRuntime;
using squarestar::presentation::EnsureGuiRendererContext;

static void InitializeStartupPresentationState(AppState& state) {
    // Startup UI state is initialized to its resting values before the window
    // becomes visible. This is not an animation snap and is not used after launch.
    state.render.navigationTransition = 1.0f;
    state.render.sidebarAnim = state.config.theme.sidebarCollapsed;

    if (state.render.notifications.firstFetchWarmup.pending)
        state.render.notifications.firstFetchWarmup.presentation.animation = 1.0f;
    if (!state.render.notifications.interaction.title.empty())
        state.render.notifications.interaction.presentation.animation = 1.0f;

    for (auto& context : state.marketData.activeContexts) {
        if (!context)
            continue;
        context->render.animProgress = 1.0f;
        context->render.tabFadeAnim = 1.0f;
        context->render.openTransitionProgress = 1.0f;
        context->render.chartRevealProgress = 1.0f;
        context->render.loadingBlockAnim = context->requests.isLoading ? 1.0f : 0.0f;
        context->render.fadeAlpha = context->requests.isLoading ? 0.0f : 1.0f;
        context->render.suppressInitialLoadPresentation = context->requests.isLoading;
    }
}

void StartConfiguredApplicationMode(AppState& state,
                                    GLFWwindow* window) {
    if (state.config.lastOpenMode == 2) {
        ApplicationRuntime().SetUiMode(AppUiMode::LiteGui);
        EnterLiteGuiWorkspace(window, state, true);


if (!squarestar::benchmark::SuppressExternalWork() &&
            state.marketData.activeContexts.empty())
            state.navigation.liteSearch.focusRequested = true;
    } else {
        ApplicationRuntime().SetUiMode(AppUiMode::Gui);


if (!squarestar::benchmark::SuppressExternalWork() &&
            state.navigation.activeSidebarTab == squarestar::application::SidebarTab::Home &&
            state.marketData.activeContexts.empty())
            state.navigation.mainSearch.focusRequested = true;
    }

    InitializeStartupPresentationState(state);
    glfwShowWindow(window);
    glfwPollEvents();
    if (!squarestar::benchmark::SuppressExternalWork()) {


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
            PumpDisplayCurrency(state);
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


                glfwWaitEventsTimeout(settledWaitSeconds);
            }
        }
        squarestar::application::PromoteDueGuiWakeDeadline();
        if (squarestar::benchmark::PollGuiProbe())
            continue;


const bool guiInputQueuedAfterEventPump =
            GImGui && GImGui->InputEventsQueue.Size > 0;
        if (guiInputQueuedAfterEventPump)
            loop.inputSettleFramesRemaining = 2;

        PumpCompletedStockRequests(state, windowSuspended);

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


}
