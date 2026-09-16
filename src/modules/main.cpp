#include "modules/main.hpp"
#include "benchmark_runner.hpp"
#include "modules/core.hpp"
#include "modules/market_data.hpp"
#include "modules/lite_gui.hpp"
#include "modules/app_services.hpp"
#include "modules/main_loop_runtime.hpp"

#include "services/config_save_queue.hpp"
#include "application/alert_service.hpp"
#include "application/debug_diagnostics.hpp"
#include "application/screener_executor.hpp"
#include "domain/stock_data.hpp"
#include "platform/audio_runtime.hpp"
#include "platform/glfw_runtime.hpp"
#include "platform/application_paths.hpp"
#include "platform/single_instance_guard.hpp"
#include "presentation/chart_export_jobs.hpp"
#include "presentation/gui_renderer_context.hpp"
#include "services/api_key_store.hpp"
#include "services/config_persistence.hpp"
#include "services/diagnostic_log.hpp"
#include "services/network_runtime.hpp"
namespace squarestar::shell {


using squarestar::application::ApplicationRuntime;
using squarestar::application::AppState;
using squarestar::application::ShutdownLatestScreenerJobExecutor;
using squarestar::application::UserFeedback;
using squarestar::application::UserFeedbackDestination;
using squarestar::application::UserFeedbackSound;
using squarestar::application::UserFeedbackType;
using squarestar::application::PersistedStateOf;
using squarestar::config::ConfigLoadStatus;
using squarestar::config::LoadConfig;
using squarestar::config::DeletePersistentApplicationState;
using squarestar::config::HasPersistedConfig;
using squarestar::config::PreserveRejectedConfigForRecovery;
using squarestar::secrets::HasFinnhubApiKey;
using squarestar::secrets::SetFinnhubApiKey;
using squarestar::presentation::MainGuiImGuiContext;
using squarestar::presentation::WaitForChartExportJob;
using squarestar::platform::CenterGlfwWindowInWorkArea;
using squarestar::platform::ShutdownAppAudio;

static int ShutdownApplicationRuntime(GLFWwindow* window,
                                      AppState& state) {
    ShutdownLatestScreenerJobExecutor();
    WaitForChartExportJob();
    ShutdownNetworkRuntime();
    WaitForStockStateRequests(state);
    const bool factoryResetRequested =
        ApplicationRuntime().FactoryResetRequested();
    bool factoryResetSucceeded = true, configSaveSucceeded = true;
    const bool hadPersistedConfig = HasPersistedConfig();
    if (factoryResetRequested) {
        ImGui::SetCurrentContext(MainGuiImGuiContext());
        if (ImGui::GetCurrentContext()) {
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::GetIO().WantSaveIniSettings = false;
        }
        factoryResetSucceeded = DeletePersistentApplicationState();
        SetFinnhubApiKey("");
    } else if (!squarestar::benchmark::GuiProbeActive()) {


if (hadPersistedConfig)
            squarestar::config::RequestConfigSave();
        configSaveSucceeded = squarestar::config::FlushPendingConfigSave(
            PersistedStateOf(state), true);
    }
    ShutdownGuiRuntime(window, state);
    if (!factoryResetRequested && !squarestar::benchmark::GuiProbeActive())
        configSaveSucceeded =
            squarestar::config::FlushConfigWrites() && configSaveSucceeded;
    ShutdownAppAudio();
    ShutdownGlfwRuntime();
    if (factoryResetRequested && !factoryResetSucceeded) {
        MessageBoxA(nullptr,
                    "SquareStar could not remove one or more saved-state files. Close any "
                    "program using SquareStar state files, then try again.",
                    "SquareStar reset incomplete",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
        return 1;
    }
    if (!configSaveSucceeded)
        return squarestar::platform::ReportConfigPersistenceFailure();
    return 0;
}

int RunSquareStar() {
    squarestar::application::SetBackgroundFailureReporter(
        &squarestar::diagnostics::ReportBackgroundFailureEvent);
    squarestar::platform::SingleInstanceGuard instanceGuard;
    if (!instanceGuard.Acquire()) {
        MessageBoxA(nullptr,
                    "SquareStar is already running, or its state-writer lock could not be acquired.",
                    "SquareStar already running",
                    MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return 73;
    }
    const bool benchmarkProbe = squarestar::benchmark::GuiProbeActive();
    AppState state;
    if (benchmarkProbe) {


squarestar::benchmark::PrepareGuiProbeState(state);
    } else {
        ConfigLoadStatus loadStatus = LoadConfig(PersistedStateOf(state));
        if (loadStatus == ConfigLoadStatus::Rejected) {
            const int recoveryChoice = MessageBoxA(
                nullptr,
                "SquareStar could not safely read the saved configuration.\n\n"
                "This can happen when data\\config.json is damaged, uses an older unsupported "
                "schema, or contains DPAPI-protected data from another Windows user or "
                "machine.\n\n"
                "Choose Yes to preserve the unreadable config file as a backup and start "
                "with clean defaults. Choose No to leave it untouched and exit.",
                "SquareStar configuration recovery",
                MB_YESNO | MB_ICONWARNING | MB_TOPMOST | MB_DEFBUTTON2);
            if (recoveryChoice != IDYES)
                return 74;

            const auto backupFilename = PreserveRejectedConfigForRecovery();
            if (!backupFilename) {
                MessageBoxA(
                    nullptr,
                    "SquareStar could not move the unreadable config file aside.\n\n"
                    "Close programs that may be using data\\config.json beside SquareStar.exe "
                    "or move that file manually, then start SquareStar again.",
                    "SquareStar configuration recovery failed",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
                return 74;
            }

            loadStatus = LoadConfig(PersistedStateOf(state));
            if (loadStatus == ConfigLoadStatus::Rejected) {
                MessageBoxA(
                    nullptr,
                    "SquareStar preserved the unreadable config file, but a new invalid "
                    "config file appeared before recovery completed. Exit SquareStar and "
                    "inspect the data folder beside SquareStar.exe.",
                    "SquareStar configuration recovery failed",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
                return 74;
            }

            const std::string recoveryMessage =
                "The unreadable config file was preserved as:\n\n" +
                *backupFilename +
                "\n\nSquareStar will start with clean defaults. Re-enter the Finnhub "
                "API key if needed.";
            MessageBoxA(nullptr,
                        recoveryMessage.c_str(),
                        "SquareStar configuration recovered",
                        MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        }
    }

    state.config.lastOpenMode = state.config.lastOpenMode == 2 ? 2 : 0;
    state.alerts.SetAlertPresentationNotBefore(
        std::chrono::steady_clock::now() + std::chrono::milliseconds(250));
    squarestar::benchmark::RecordMemoryLayer(
        "A", "process + deterministic AppState (no GLFW/D3D11)");
    if (!benchmarkProbe && state.config.lastOpenMode == 0 && HasFinnhubApiKey())
        state.render.notifications.firstFetchWarmup.pending = true;


if (!benchmarkProbe)
        PrimeNetworkRuntimeWithoutIo();

    GLFWwindow* window = nullptr;
    std::string guiFailure;
    if (!InitializeGuiRuntime(window, state, true, guiFailure)) {
        MessageBoxA(nullptr,
                    guiFailure.c_str(),
                    "SquareStar initialization error",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
        ShutdownNetworkRuntime();
        ShutdownGlfwRuntime();
        return -1;
    }
    if (squarestar::benchmark::MemoryLayerProbeActive()) {
        PrimeNetworkRuntimeWithoutIoForBenchmark();
        squarestar::benchmark::RecordMemoryLayer(
            "G", "+ curl runtime + network worker executors (no outbound I/O)");
    }
    if (state.config.lastOpenMode == 2) {
        glfwSetWindowSize(window, LITE_GUI_WIDTH, LITE_GUI_SEARCH_HEIGHT);
        CenterGlfwWindowInWorkArea(window, LITE_GUI_WIDTH, LITE_GUI_SEARCH_HEIGHT);
    } else {
        glfwSetWindowSize(window, GUI_WINDOW_WIDTH, GUI_WINDOW_HEIGHT);
        CenterGlfwWindowInWorkArea(window, GUI_WINDOW_WIDTH, GUI_WINDOW_HEIGHT);
    }
    glfwPollEvents();
    if (!state.UiAnimationsEnabled())
        SnapAllUiAnimations(state);
    if (!benchmarkProbe && !HasFinnhubApiKey()) {
        UserFeedback feedback;
        feedback.type = UserFeedbackType::Warning;
        feedback.title = "No Finnhub API key detected";
        feedback.body = "Add a valid key in Settings to enable Finnhub data.";
        feedback.duration = std::chrono::seconds(9);
        feedback.destination = UserFeedbackDestination::Foreground;
        feedback.sound = UserFeedbackSound::None;
        feedback.actionLabel = "Open finnhub.io";
        feedback.actionUrl = "https://finnhub.io/";
        PublishUserFeedback(state, std::move(feedback));
    }
    if (!benchmarkProbe && HasFinnhubApiKey() && state.config.lastOpenMode == 0) {
        state.navigation.previousActiveSidebarTab = state.navigation.activeSidebarTab;
        if (!state.navigation.guiWorkspace.stockTabs.empty())
            RestoreSavedGuiStockTabs(state);
        else if (!state.navigation.lastActiveTab.empty())
            OpenStock(state, state.navigation.lastActiveTab);
    }

    StartConfiguredApplicationMode(state, window);
    RunApplicationMainLoop(window, state);
    return ShutdownApplicationRuntime(window, state);
}

}
