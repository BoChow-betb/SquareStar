#include "modules/app_services.hpp"
#include "benchmark_runner.hpp"
#include "application/theme_profiles.hpp"
#include "modules/core.hpp"
#include "modules/platform.hpp"
#include "modules/market_data.hpp"
#include "modules/views.hpp"
#include "modules/window_chrome.hpp"

#include <type_traits>
#include <utility>

#include "services/config_save_queue.hpp"
#include "application/alert_service.hpp"
#include "application/app_config.hpp"
#include "application/contextual_keybind_policy.hpp"
#include "application/debug_diagnostics.hpp"
#include "application/lite_context_lifecycle.hpp"
#include "application/screener_controller.hpp"
#include "application/stock_refresh_policy.hpp"
#include "application/workspace_lifecycle.hpp"
#include "domain/chart_ranges.hpp"
#include "domain/market_runtime.hpp"
#include "domain/screener_routes.hpp"
#include "domain/stock_data.hpp"
#include "platform/audio_runtime.hpp"
#include "presentation/gui_renderer_context.hpp"
#include "presentation/render_memory.hpp"
#include "services/stock_data_service.hpp"
#include "imgui_impl_dx11.h"
#include "imgui_impl_glfw.h"
namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::platform::Win32AppRuntime;
using squarestar::market::TIME_RANGES;
using squarestar::market::CachedMarketOpen;
using squarestar::market::kScreenerRoutes;
using squarestar::application::AppUiMode;
using squarestar::application::UiModeRequest;
using squarestar::application::RequestGuiRedraw;
using squarestar::presentation::RebuildApplicationFonts;
using squarestar::application::ConfiguredPriceAlert;
using squarestar::application::AppState;
using squarestar::application::UserFeedbackType;
using squarestar::application::HiddenStockSurfaceInputs;
using squarestar::application::ShouldWatchHiddenStockSurface;
using squarestar::application::StockAutoRefreshAction;
using squarestar::application::QuoteRefreshSeconds;
using squarestar::application::ChartRefreshSeconds;
using squarestar::application::AlertContextRefreshSeconds;
using squarestar::application::SecondsUntilRefresh;
using squarestar::application::SelectStockRefreshAction;
using squarestar::application::AlertContextRefreshDue;
using squarestar::application::ClampStockRefreshElapsed;
using squarestar::application::StockAutoRefreshSessionEligible;
using squarestar::application::PersistedStateOf;
using squarestar::application::StockContext;
using squarestar::application::ReapRetiredLiteContexts;
using squarestar::application::MergeStockFetchPatch;
using squarestar::market::FetchKind;
using squarestar::market::StockData;
using squarestar::market::StockFetchResult;
using squarestar::marketdata::ClearStockMemoryCache;
using squarestar::presentation::SetGuiRendererContexts;
using squarestar::presentation::SetGuiRendererBackendData;
using squarestar::presentation::ClearGuiRendererContextRegistry;
using squarestar::presentation::MainGuiImGuiContext;
using squarestar::presentation::MainGuiImPlotContext;
using squarestar::presentation::EnsureGuiRendererContext;
using squarestar::presentation::PrimeGuiRendererForStartup;
using squarestar::presentation::ClearApplicationFontPointers;
using squarestar::presentation::ReleaseContextRenderMemory;
using squarestar::presentation::ReleaseGuiStateRenderMemory;
using squarestar::presentation::InitializeGuiD3D11;
using squarestar::presentation::ShutdownGuiD3D11;
using squarestar::presentation::GuiD3D11Device;
using squarestar::presentation::GuiD3D11DeviceContext;
using squarestar::platform::ApplyFixedGlfwWindowLayout;
using squarestar::platform::ApplyResizableGlfwWindowLayout;
using squarestar::platform::ShutdownAppAudio;

// Release vector capacity that is not useful across GUI/LiteGUI transitions.
template <typename T>
static void ReleaseVectorStorage(std::vector<T>& values) {
    std::vector<T>().swap(values);
}

void ShutdownGuiRuntime(GLFWwindow*& window,
                        AppState& state) {
    if (!window)
        return;

    ReleaseGuiStateRenderMemory(state);
    CloseAllAnimatedFloatingMenus();
    RemoveTrayIcon();
    ImGuiContext* imguiContext = MainGuiImGuiContext();
    ImPlotContext* implotContext = MainGuiImPlotContext();
    if (imguiContext)
        ImGui::SetCurrentContext(imguiContext);
    if (implotContext)
        ImPlot::SetCurrentContext(implotContext);
    if (imguiContext && ImGui::GetIO().BackendRendererUserData)
        ImGui_ImplDX11_Shutdown();
    if (imguiContext && ImGui::GetIO().BackendPlatformUserData) {
        ImGui_ImplGlfw_Shutdown();
    }
    if (implotContext)
        ImPlot::DestroyContext(implotContext);
    if (imguiContext)
        ImGui::DestroyContext(imguiContext);
    ClearGuiRendererContextRegistry();
    ClearApplicationFontPointers(state.render);

    if (Win32AppRuntime().MainWindow() && Win32AppRuntime().OriginalWindowProc())
        SetWindowLongPtr(Win32AppRuntime().MainWindow(),
                         GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(Win32AppRuntime().OriginalWindowProc()));
    ShutdownGuiD3D11();
    glfwDestroyWindow(window);
    window = nullptr;
    Win32AppRuntime().ClearWindowBinding();
}
bool InitializeGuiRuntime(GLFWwindow*& window,
                                 AppState& state,
                                 bool buildFonts,
                                 std::string& failureReason) {
    failureReason.clear();
    if (!EnsureGlfwRuntimeInitialized()) {
        failureReason = "The GLFW runtime could not be initialized.";
        return false;
    }
    if (window) {
        if (EnsureGuiRendererContext(window))
            return true;
        failureReason = "The existing GUI renderer context is invalid.";
        return false;
    }

    auto fail = [&](const char* message) {
        failureReason = message;
        ShutdownGuiRuntime(window, state);
        return false;
    };
    // GLFW owns Win32 window/input integration; Direct3D 11 owns rendering.
    // Create the hidden host at its final startup size to avoid a first-frame
    // swap-chain resize.
    const bool startLiteGui = state.config.lastOpenMode == 2;
    const int initialWindowWidth = startLiteGui ? LITE_GUI_WIDTH : GUI_WINDOW_WIDTH;
    const int initialWindowHeight =
        startLiteGui ? LITE_GUI_SEARCH_HEIGHT : GUI_WINDOW_HEIGHT;

    // GLFW window hints are process-global and sticky. Build the host from a
    // known default baseline, then clear every host-specific hint immediately
    // after creation instead of leaking creation policy to later windows.
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    window = glfwCreateWindow(
        initialWindowWidth, initialWindowHeight, "SquareStar", nullptr, nullptr);
    glfwDefaultWindowHints();
    // Dear ImGui creates secondary platform windows later in the process.
    // They are rendered by Direct3D 11 through native HWNDs, so keep GLFW's
    // process-global client-API policy at NO_API after clearing the host-only
    // hints above. Otherwise secondary viewports fall back to an OpenGL client
    // API and can present as black owned windows.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (!window)
        return fail("The GLFW host window could not be created.");

    glfwSetWindowSizeLimits(window,
                            initialWindowWidth,
                            initialWindowHeight,
                            initialWindowWidth,
                            initialWindowHeight);

    if (GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor()) {
        if (const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor))
            glfwSetWindowPos(window,
                             (mode->width - initialWindowWidth) / 2,
                             (mode->height - initialWindowHeight) / 2);
    }
    Win32AppRuntime().SetMainWindow(glfwGetWin32Window(window));
    if (!Win32AppRuntime().MainWindow())
        return fail("The native GUI window handle is unavailable.");
    squarestar::benchmark::RecordMemoryLayer("B", "+ GLFW window (no graphics API)");
    int initialFramebufferWidth = 0;
    int initialFramebufferHeight = 0;
    glfwGetFramebufferSize(window, &initialFramebufferWidth, &initialFramebufferHeight);
    if (!InitializeGuiD3D11(Win32AppRuntime().MainWindow(),
                            initialFramebufferWidth,
                            initialFramebufferHeight,
                            &failureReason)) {
        if (failureReason.empty())
            failureReason = "The Direct3D 11 renderer could not be initialized.";
        const std::string rendererFailure = failureReason;
        ShutdownGuiRuntime(window, state);
        failureReason = rendererFailure;
        return false;
    }
    squarestar::benchmark::RecordMemoryLayer("C", "+ D3D11 device/swap-chain/RTV");
    LONG_PTR nativeStyle = GetWindowLongPtr(Win32AppRuntime().MainWindow(), GWL_STYLE);
    // Keep the primary SquareStar window fixed-size.
    nativeStyle = (nativeStyle | WS_MINIMIZEBOX) &
                  ~(WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX);
    SetWindowLongPtr(Win32AppRuntime().MainWindow(), GWL_STYLE, nativeStyle);
    SetWindowPos(Win32AppRuntime().MainWindow(),
                 nullptr,
                 0,
                 0,
                 0,
                 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    Win32AppRuntime().SetOriginalWindowProc(reinterpret_cast<WNDPROC>(SetWindowLongPtr(
        Win32AppRuntime().MainWindow(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TrayWndProc))));
    ApplyRoundedWindowCorners(Win32AppRuntime().MainWindow());
    SetupTrayIcon(Win32AppRuntime().MainWindow());
    if (HICON icon = LoadSquareStarIcon()) {
        SendMessage(Win32AppRuntime().MainWindow(), WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessage(Win32AppRuntime().MainWindow(), WM_SETICON, ICON_SMALL, (LPARAM)icon);
        SetClassLongPtr(Win32AppRuntime().MainWindow(), GCLP_HICON, (LONG_PTR)icon);
        SetClassLongPtr(Win32AppRuntime().MainWindow(), GCLP_HICONSM, (LONG_PTR)icon);
    }
    glfwSetWindowCloseCallback(window, [](GLFWwindow* target) {
        glfwSetWindowShouldClose(target, GLFW_FALSE);
        if (AppState* appState = static_cast<AppState*>(glfwGetWindowUserPointer(target))) {
            appState->navigation.showExitModal = true;
            RequestGuiRedraw();
        }
    });

    const bool layoutMatches = ImGui::DebugCheckVersionAndDataLayout(IMGUI_VERSION,
                                                                     sizeof(ImGuiIO),
                                                                     sizeof(ImGuiStyle),
                                                                     sizeof(ImVec2),
                                                                     sizeof(ImVec4),
                                                                     sizeof(ImDrawVert),
                                                                     sizeof(ImDrawIdx));
    if (!layoutMatches)
        return fail("ImGui was built from incompatible headers or compile settings.");

    ImGuiContext* imguiContext = ImGui::CreateContext();
    if (!imguiContext)
        return fail("The ImGui context could not be allocated.");
    // SquareStar owns Ctrl+Tab / Ctrl+Shift+Tab for stock-tab cycling, so the
    // built-in ImGui window switcher must not consume the same shortcuts.
    imguiContext->ConfigNavWindowingKeyNext = ImGuiKey_None;
    imguiContext->ConfigNavWindowingKeyPrev = ImGuiKey_None;
    SetGuiRendererContexts(imguiContext, nullptr);
    ImGui::SetCurrentContext(imguiContext);
    ImPlotContext* implotContext = nullptr;
    if (!squarestar::benchmark::MemoryLayerProbeActive()) {
        implotContext = ImPlot::CreateContext();
        if (!implotContext)
            return fail("The ImPlot context could not be allocated.");
        SetGuiRendererContexts(imguiContext, implotContext);
        ImPlot::SetCurrentContext(implotContext);
    }
    ImGuiIO& io = ImGui::GetIO();
#ifdef IMGUI_HAS_VIEWPORT
    // LiteGUI's search suggestions can live in a small, owned platform
    // viewport so the compact host window never has to grow just to show the
    // History/Recommended list. Keep multi-viewport support available for
    // that detached surface; ordinary SquareStar windows still stay merged
    // into the main viewport unless they explicitly opt out.
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoTaskBarIcon = true;
    io.ConfigViewportsNoDecoration = true;
#endif
    io.ConfigMemoryCompactTimer = 5.0f;
    // Text glyphs are coverage masks, not RGBA images. SquareStar's compact
    // Direct3D 11 backend maps Alpha8 atlases to DXGI_FORMAT_R8_UNORM, keeping
    // both the CPU atlas and GPU font texture at one byte per pixel.
    io.Fonts->TexDesiredFormat = ImTextureFormat_Alpha8;
    // The ImGui atlas supplies the transparent texel required by its bilinear
    // sampler, so the default one-pixel glyph padding is sufficient.
    io.Fonts->TexGlyphPadding = 1;
    io.Fonts->Flags |= ImFontAtlasFlags_NoMouseCursors;
    // Window placement is owned by SquareStar, so ImGui does not write an .ini file.
    io.IniFilename = nullptr;
    io.WantSaveIniSettings = false;
    const bool platformReady = ImGui_ImplGlfw_InitForOther(window, true);
    const bool rendererReady =
        platformReady &&
        ImGui_ImplDX11_Init(GuiD3D11Device(), GuiD3D11DeviceContext()) &&
        ImGui_ImplDX11_CreateDeviceObjects();
    SetGuiRendererBackendData(io.BackendPlatformUserData, io.BackendRendererUserData);
    if (!platformReady || !rendererReady || !io.BackendPlatformUserData ||
        !io.BackendRendererUserData)
        return fail("The ImGui GLFW/Direct3D 11 backends could not be initialized.");
    squarestar::benchmark::RecordMemoryLayer("D", "+ ImGui context + GLFW/DX11 backends");

    glfwSetWindowUserPointer(window, &state);
    if (buildFonts) {
        RebuildApplicationFonts(state);
        if (squarestar::benchmark::MemoryLayerProbeActive())
            ImGui::GetIO().Fonts->Build();
        squarestar::benchmark::RecordMemoryLayer("E", "+ application fonts + built CPU atlas");
    }
    if (!implotContext) {
        implotContext = ImPlot::CreateContext();
        if (!implotContext)
            return fail("The ImPlot context could not be allocated.");
        SetGuiRendererContexts(imguiContext, implotContext);
        ImPlot::SetCurrentContext(implotContext);
    }
    squarestar::benchmark::RecordMemoryLayer("F", "+ ImPlot context");
    EnforceZeroGraphicsMode(state);
    // A newly created ImGui context starts with Dear ImGui's default palette.
    // Reapply SquareStar's theme before the renderer is primed so the first
    // visible frame uses the configured palette.
    ApplyTheme(state);
    state.render.appliedThemeModeIndex = state.config.themeModeIndex;
    state.render.appliedZeroGraphics = state.ZeroGraphicsEnabled();
    glfwPollEvents();
    const bool primed = buildFonts && PrimeGuiRendererForStartup(window, &failureReason);
    if (buildFonts && !primed) {
        if (failureReason.empty())
            failureReason = "The first GUI renderer frame could not be created.";
        ShutdownGuiRuntime(window, state);
        return false;
    }
    return true;
}
void WaitForStockStateRequests(AppState& state) {
    if (state.navigation.mainSearch.searchFuture.valid())
        state.navigation.mainSearch.searchFuture.wait();
    if (state.navigation.liteSearch.searchFuture.valid())
        state.navigation.liteSearch.searchFuture.wait();
    const auto waitForContexts = [](auto& contexts) {
        for (auto& ctx : contexts) {
            if (!ctx)
                continue;
            if (ctx->requests.pendingRequest.valid())
                ctx->requests.pendingRequest.wait();
            if (ctx->requests.pendingDetailsRequest.valid())
                ctx->requests.pendingDetailsRequest.wait();
            if (ctx->navigation.headerSearch.searchFuture.valid())
                ctx->navigation.headerSearch.searchFuture.wait();
        }
    };
    waitForContexts(state.marketData.activeContexts);
    waitForContexts(state.marketData.retiredLiteContexts);
    waitForContexts(state.alerts.Monitors());
}
static void ApplyLiteGuiWindowStyle(GLFWwindow* window, AppState& state) {
    if (!window)
        return;
    constexpr int width = LITE_GUI_WIDTH;
    constexpr int height = LITE_GUI_SEARCH_HEIGHT;
    ApplyFixedGlfwWindowLayout(window, width, height);
    state.navigation.liteGuiAppliedHeight = height;
    ApplyRoundedWindowCorners(Win32AppRuntime().MainWindow());
}
static void ApplyNormalGuiWindowStyle(GLFWwindow* window) {
    if (!window)
        return;
    ApplyFixedGlfwWindowLayout(window, GUI_WINDOW_WIDTH, GUI_WINDOW_HEIGHT);
    ApplyRoundedWindowCorners(Win32AppRuntime().MainWindow());
}
void RestoreSavedGuiStockTabs(AppState& state, bool keepSavedTabs) {
    squarestar::application::RestoreSavedGuiStockTabs(
        state,
        IM_ARRAYSIZE(TIME_RANGES) - 1,
        [&](StockContext& restored) {
            TriggerFetch(state, restored, true, false, FetchKind::Full);
        },
        keepSavedTabs);
}
void EnterLiteGuiWorkspace(GLFWwindow* window, AppState& state) {
    if (state.navigation.liteGuiActive)
        return;
    state.navigation.guiWorkspace.wasFullscreen = state.navigation.isFullscreen;
    if (state.navigation.isFullscreen)
        ToggleApplicationFullscreen(window, state);
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(Win32AppRuntime().MainWindow(), &placement)) {
        const RECT& normal = placement.rcNormalPosition;
        state.navigation.guiWorkspace.restoreX = normal.left;
        state.navigation.guiWorkspace.restoreY = normal.top;
        state.navigation.guiWorkspace.restoreWidth = std::max(520L, normal.right - normal.left);
        state.navigation.guiWorkspace.restoreHeight = std::max(360L, normal.bottom - normal.top);
        state.navigation.guiWorkspace.restoreMaximized = placement.showCmd == SW_SHOWMAXIMIZED ||
                                        glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;
    } else {
        glfwGetWindowPos(window, &state.navigation.guiWorkspace.restoreX, &state.navigation.guiWorkspace.restoreY);
        glfwGetWindowSize(window, &state.navigation.guiWorkspace.restoreWidth, &state.navigation.guiWorkspace.restoreHeight);
        state.navigation.guiWorkspace.restoreMaximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;
    }
    state.navigation.guiWorkspace.pureMonitorMode = state.navigation.pureMonitorMode;
    state.navigation.guiWorkspace.activeSidebarTab = state.navigation.activeSidebarTab;
    state.navigation.guiWorkspace.previousActiveSidebarTab = state.navigation.previousActiveSidebarTab;
    state.navigation.guiWorkspace.lastActiveTab = state.navigation.lastActiveTab;
    if (!state.marketData.activeContexts.empty() || state.navigation.guiWorkspace.stockTabs.empty())
        CaptureOpenGuiStockTabs(state);
    // In-flight futures remain valid in the retire list until their copied
    // worker inputs finish. Their full-workspace render and result payloads are
    // no longer needed while LiteGUI is active, so release those immediately.
    state.marketData.RetireActiveContexts();
    ReapRetiredLiteContexts(state);
    for (auto& retired : state.marketData.retiredLiteContexts) {
        if (!retired)
            continue;
        ReleaseContextRenderMemory(*retired);
        retired->ClearRawData();
        ReleaseVectorStorage(retired->navigation.headerSearch.results);
        ReleaseVectorStorage(retired->navigation.comparisonSymbols);
    }
    squarestar::application::ResetScreenerFetch(state);
    ClearStockMemoryCache();
    // Keep the bounded screener cache across in-process interface switches.
    // Returning to Overview can then paint cached rows immediately instead of
    // repeating the complete list fetch after every LiteGUI visit.
    ShutdownAppAudio();
    state.navigation.lastActiveTab.clear();
    state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Stock;
    state.navigation.previousActiveSidebarTab = squarestar::application::SidebarTab::Stock;
    state.navigation.pureMonitorMode = false;
    state.navigation.liteMonitorMode = false;
    state.navigation.liteGuiActive = true;
    // Drop FullGUI-only transient presentation state exactly at the mode
    // boundary. The Lite renderer can then stay side-effect free and render
    // only the two notification classes its policy allows.
    state.render.notifications.ClearLiteGuiSuppressed();
    state.navigation.lastActiveTab = state.navigation.guiWorkspace.lastActiveTab;
    if (!state.navigation.guiWorkspace.stockTabs.empty())
        RestoreSavedGuiStockTabs(state, true);
    // Keep the compact mode-switch reminder visible in LiteGUI. It occupies a
    // stable title-bar position and remains useful after the first launch.
    state.config.liteGuiHintShown = true;
    // LiteGUI exposes two clock slots without rewriting the persistent list.
    // Hidden desktop clocks stay canonical in AppConfig while Lite edits are
    // merged back through AppState::SetWorldClockEnabled().
    state.navigation.liteWorldClocks.assign(
        state.config.activeWorldClocks.begin(),
        state.config.activeWorldClocks.begin() +
            std::min<std::size_t>(2, state.config.activeWorldClocks.size()));
    state.navigation.liteHiddenWorldClocks.assign(
        state.config.activeWorldClocks.begin() +
            std::min<std::size_t>(2, state.config.activeWorldClocks.size()),
        state.config.activeWorldClocks.end());
    state.navigation.showExitModal = false;
    state.navigation.liteSearch.focusRequested = false;
    state.render.appliedZeroGraphics = false;
    CloseAllAnimatedFloatingMenus();
    SnapAllUiAnimations(state);
    ReleaseGuiStateRenderMemory(state);
    ApplyLiteGuiWindowStyle(window, state);
}
static void LeaveLiteGuiWorkspace(GLFWwindow* window,
                                  AppState& state,
                                  bool restoreStockTabs = true) {
    if (!state.navigation.liteGuiActive)
        return;
    CaptureOpenGuiStockTabs(state);
    state.navigation.guiWorkspace.lastActiveTab = state.navigation.lastActiveTab;
    state.marketData.RetireActiveContexts();
    state.navigation.pureMonitorMode = state.navigation.guiWorkspace.pureMonitorMode;
    state.navigation.activeSidebarTab = state.navigation.guiWorkspace.activeSidebarTab;
    state.navigation.previousActiveSidebarTab = state.navigation.guiWorkspace.previousActiveSidebarTab;
    state.navigation.lastActiveTab = state.navigation.guiWorkspace.lastActiveTab;
    state.navigation.liteWorldClocks.clear();
    state.navigation.liteHiddenWorldClocks.clear();
    state.navigation.liteGuiActive = false;
    state.navigation.liteMonitorMode = false;
    state.navigation.liteGuiAppliedHeight = 0;
    state.navigation.showExitModal = false;
    CloseAllAnimatedFloatingMenus();
    state.render.appliedZeroGraphics = !state.ZeroGraphicsEnabled();
    ApplyNormalGuiWindowStyle(window);
    if (restoreStockTabs)
        RestoreSavedGuiStockTabs(state);
    if (restoreStockTabs && state.navigation.activeSidebarTab == squarestar::application::SidebarTab::Overview &&
        state.navigation.activeScreenerIndex >= 0 &&
        state.navigation.activeScreenerIndex < (int)std::size(kScreenerRoutes))
        squarestar::application::StartScreenerFetch(
            state,
            std::string(kScreenerRoutes[state.navigation.activeScreenerIndex].guiId));
    // The primary GUI is fixed-size, so do not restore a maximized state.
    if (state.navigation.guiWorkspace.wasFullscreen)
        ToggleApplicationFullscreen(window, state);
}
static bool PumpDeferredInterfaceSwitch(AppState& state) {
    if (state.navigation.deferredInterfaceSwitchTarget > (int)UiModeRequest::None &&
        state.navigation.deferredInterfaceSwitchTarget <= (int)UiModeRequest::LiteGui &&
        ApplicationRuntime().RequestedUiMode() == UiModeRequest::None) {
        ApplicationRuntime().RequestUiMode(
            static_cast<UiModeRequest>(state.navigation.deferredInterfaceSwitchTarget));
        state.navigation.deferredInterfaceSwitchTarget = -1;
        // Render one normal frame with the modal closed before changing the
        // workspace state. The popup must finish its frame before the mode switch.
        RequestGuiRedraw();
        return true;
    } else if (state.navigation.deferredInterfaceSwitchTarget != -1 &&
               (state.navigation.deferredInterfaceSwitchTarget <= (int)UiModeRequest::None ||
                state.navigation.deferredInterfaceSwitchTarget > (int)UiModeRequest::LiteGui)) {
        state.navigation.deferredInterfaceSwitchTarget = -1;
        state.navigation.interfaceSwitchDecisionReady = false;
    }
    return false;
}

#ifdef _WIN32
static void PumpNativeNotificationStockOpen(AppState& state) {
    const std::string ticker = ApplicationRuntime().ConsumeNotificationStockOpen();
    if (ticker.empty())
        return;

    if (HWND hwnd = Win32AppRuntime().MainWindow())
        RestoreFromTray(hwnd);
    (void)OpenNotificationStock(state, ticker);
}
#endif

static void TransitionGuiToLiteGui(GLFWwindow* window, AppState& state) {
    ApplicationRuntime().ClearUiModeRequest();
    ApplicationRuntime().SetUiMode(AppUiMode::LiteGui);
    EnterLiteGuiWorkspace(window, state);
    glfwShowWindow(window);
    state.config.lastOpenMode = 2;
    squarestar::config::RequestConfigSave();
    RequestGuiRedraw();
}

static void TransitionLiteGuiToGui(GLFWwindow* window, AppState& state) {
    ApplicationRuntime().ClearUiModeRequest();
    ApplicationRuntime().SetUiMode(AppUiMode::Gui);
    LeaveLiteGuiWorkspace(window, state);
    glfwShowWindow(window);
    state.config.lastOpenMode = 0;
    squarestar::config::RequestConfigSave();
    RequestGuiRedraw();
}

void PumpInterfaceTransitionsOnMainThread(GLFWwindow*& window, AppState& state) {
#ifdef _WIN32
    PumpNativeNotificationStockOpen(state);
#endif
    ReapRetiredLiteContexts(state);
    if (PumpDeferredInterfaceSwitch(state))
        return;

    const UiModeRequest request = ApplicationRuntime().RequestedUiMode();
    const AppUiMode currentMode = ApplicationRuntime().CurrentUiMode();
    if (request == UiModeRequest::None)
        return;
    if ((request == UiModeRequest::Gui && currentMode == AppUiMode::Gui) ||
        (request == UiModeRequest::LiteGui && currentMode == AppUiMode::LiteGui)) {
        ApplicationRuntime().ClearUiModeRequest();
        return;
    }

    const bool hasInterfaceTabs = HasActualStockTabs(state);
    bool saveInterfaceData = state.config.saveLastUsedUiData;
    const bool decisionReady = state.navigation.interfaceSwitchDecisionReady;
    if (decisionReady) {
        saveInterfaceData = state.navigation.interfaceSwitchSaveCurrentData;
        state.navigation.interfaceSwitchDecisionReady = false;
    }
    if (hasInterfaceTabs && state.config.askBeforeInterfaceSwitch && !decisionReady) {
        state.navigation.pendingInterfaceSwitchTarget = static_cast<int>(request);
        state.navigation.showInterfaceSavePrompt = true;
        ApplicationRuntime().ClearUiModeRequest();
        RequestGuiRedraw();
        return;
    }
    if (hasInterfaceTabs && !saveInterfaceData) {
        state.marketData.RetireActiveContexts();
        state.navigation.guiWorkspace.stockTabs.clear();
        state.navigation.lastActiveTab.clear();
        state.navigation.guiWorkspace.lastActiveTab.clear();
        state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Home;
        state.navigation.guiWorkspace.activeSidebarTab = squarestar::application::SidebarTab::Home;
        state.navigation.guiWorkspace.previousActiveSidebarTab = squarestar::application::SidebarTab::Stock;
    }

    if (request == UiModeRequest::LiteGui && currentMode == AppUiMode::Gui) {
        TransitionGuiToLiteGui(window, state);
        return;
    }
    if (request == UiModeRequest::Gui && currentMode == AppUiMode::LiteGui) {
        TransitionLiteGuiToGui(window, state);
        return;
    }
    ApplicationRuntime().ClearUiModeRequest();
}

bool ShouldWatchHiddenStockContext(const AppState& state, const StockContext& ctx) {
    HiddenStockSurfaceInputs inputs;
    inputs.pureMonitorMode = state.navigation.pureMonitorMode;
    inputs.appMinimized = IsAppMinimizedForNotifications();
    inputs.contextOpen = ctx.navigation.open;
    inputs.isLastActiveTab = state.navigation.lastActiveTab == ctx.navigation.ticker;
    return ShouldWatchHiddenStockSurface(inputs);
}

double SecondsUntilNextStockAutoRefresh(AppState& state, bool windowSuspended) {
    const bool regularEquityMarketOpen = CachedMarketOpen();
    double delaySeconds = 30.0;
    const auto includeDelay = [&](float timer, float deadline) {
        delaySeconds = std::min(delaySeconds, SecondsUntilRefresh(timer, deadline));
    };
    {
        for (const auto& candidate : state.marketData.activeContexts) {
            if (!candidate)
                continue;
            const StockContext& ctx = *candidate;
            if (!StockAutoRefreshSessionEligible(
                    ctx.navigation.ticker, regularEquityMarketOpen))
                continue;
            const bool alertConfigured =
                ConfiguredPriceAlert(state.alerts, ctx).has_value();
            if (windowSuspended && !alertConfigured &&
                !ShouldWatchHiddenStockContext(state, ctx))
                continue;
            if (!ctx.navigation.hasSearched || ctx.requests.isLoading || ctx.requests.isBackgroundFetching ||
                !ctx.RawData().success || !StockRequestIdle(ctx))
                continue;
            const bool foregroundSurface = !windowSuspended && ctx.navigation.refreshSurfaceVisible;
            includeDelay(ctx.requests.autoRefreshTimer,
                         QuoteRefreshSeconds(alertConfigured, foregroundSurface));
            if (!alertConfigured)
                includeDelay(ctx.requests.chartRefreshTimer,
                             ChartRefreshSeconds(foregroundSurface));
        }
    }
    for (const auto& candidate : state.alerts.Monitors()) {
        if (!candidate ||
            !StockAutoRefreshSessionEligible(
                candidate->navigation.ticker, regularEquityMarketOpen) ||
            candidate->requests.isLoading || candidate->requests.isBackgroundFetching ||
            !StockRequestIdle(*candidate))
            continue;
        includeDelay(candidate->requests.autoRefreshTimer,
                     AlertContextRefreshSeconds(candidate->RawData().success));
    }
    return delaySeconds;
}
void PumpStockAutoRefresh(AppState& state,
                                 double elapsedSeconds,
                                 bool windowSuspended) {
    if (!(elapsedSeconds > 0.0))
        return;
    const bool regularEquityMarketOpen = CachedMarketOpen();
    const float refreshDt = ClampStockRefreshElapsed(elapsedSeconds);
    {
        for (auto& candidate : state.marketData.activeContexts) {
            if (!candidate)
                continue;
            StockContext& ctx = *candidate;
            if (!StockAutoRefreshSessionEligible(
                    ctx.navigation.ticker, regularEquityMarketOpen))
                continue;
            const bool alertConfigured =
                ConfiguredPriceAlert(state.alerts, ctx).has_value();
            if (windowSuspended && !alertConfigured && !ShouldWatchHiddenStockContext(state, ctx))
                continue;
            if (!ctx.navigation.hasSearched || ctx.requests.isLoading || ctx.requests.isBackgroundFetching ||
                !ctx.RawData().success || !StockRequestIdle(ctx))
                continue;
            const bool foregroundSurface = !windowSuspended && ctx.navigation.refreshSurfaceVisible;
            ctx.requests.autoRefreshTimer += refreshDt;
            ctx.requests.chartRefreshTimer += refreshDt;
            switch (SelectStockRefreshAction(ctx.requests.autoRefreshTimer,
                                             ctx.requests.chartRefreshTimer,
                                             alertConfigured,
                                             foregroundSurface)) {
            case StockAutoRefreshAction::Chart:
                TriggerFetch(state, ctx, true, true, FetchKind::Chart);
                break;
            case StockAutoRefreshAction::LiveQuote:
                TriggerFetch(state, ctx, true, true, FetchKind::LiveQuote);
                break;
            case StockAutoRefreshAction::None:
            case StockAutoRefreshAction::AlertQuote:
                break;
            }
        }
    }
    for (const auto& candidate : state.alerts.Monitors()) {
        if (!candidate ||
            !StockAutoRefreshSessionEligible(
                candidate->navigation.ticker, regularEquityMarketOpen) ||
            candidate->requests.isLoading || candidate->requests.isBackgroundFetching ||
            !StockRequestIdle(*candidate))
            continue;
        candidate->requests.autoRefreshTimer += refreshDt;
        if (AlertContextRefreshDue(candidate->requests.autoRefreshTimer,
                                   candidate->RawData().success))
            TriggerFetch(state, *candidate, true, true, FetchKind::AlertQuote);
    }
}
void PumpPriceAlertMonitorRequests(AppState& state) {
    for (const auto& candidate : state.alerts.Monitors()) {
        if (!candidate || !candidate->requests.pendingRequest.valid() ||
            candidate->requests.pendingRequest.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready)
            continue;
        StockContext& ctx = *candidate;
        StockFetchResult completed;
        try {
            completed = ctx.requests.pendingRequest.get();
        } catch (...) {
            squarestar::application::ReportBackgroundFailure(
                "price-alert quote completion");
            completed.success = false;
            completed.errorMessage = "The alert quote request ended unexpectedly";
        }
        const FetchKind completedKind = ctx.requests.pendingFetchKind;
        const int completedRangeIndex = ctx.requests.pendingFetchRangeIndex;
        const bool generationWasDesired =
            ctx.requests.tracker.CanApply(ctx.requests.pendingChannel, ctx.requests.pendingGeneration);
        (void)ctx.requests.tracker.Complete(
            ctx.requests.pendingChannel, ctx.requests.pendingGeneration, completed.success);
        ctx.requests.isLoading = false;
        ctx.requests.isBackgroundFetching = false;
        ctx.requests.autoRefreshTimer = completed.rateLimited ? -60.0f : 0.0f;
        if (!generationWasDesired || !completed.success) {
            if (generationWasDesired)
                ctx.requests.tracker.CancelDesired(ctx.requests.pendingChannel);
            continue;
        }
        StockData newData = std::move(completed.marketData);
        const bool hadPreviousPrice = ctx.RawData().success &&
                                      std::isfinite(ctx.RawData().currentPrice) &&
                                      ctx.RawData().currentPrice > 0.0;
        const double previousPrice = ctx.RawData().currentPrice;
        if (completedKind == FetchKind::LiveQuote ||
            completedKind == FetchKind::AlertQuote) {
            (void)ctx.ApplyQuotePatchAtRevision(
                newData, completedRangeIndex, ctx.marketData.dataRevision + 1);
        } else {
            ctx.PublishRawDataAtRevision(
                MergeStockFetchPatch(
                    ctx.RawData(), std::move(newData), completedKind, completedRangeIndex),
                ctx.marketData.dataRevision + 1);
        }
        const bool wasTriggered = ctx.alerts.priceAlertTriggered;
        EvaluateStockPriceAlert(state, ctx);
        if (!wasTriggered && ctx.alerts.priceAlertTriggered) {
            if (UseBackgroundNotificationBlock()) {
                const auto alert = ConfiguredPriceAlert(state.alerts, ctx);
                QueueTrayPriceMove(state,
                                   ctx.navigation.ticker,
                                   hadPreviousPrice
                                       ? previousPrice
                                       : std::numeric_limits<double>::quiet_NaN(),
                                   ctx.RawData().currentPrice,
                                   true,
                                   alert.value_or(0.0),
                                   "USD");
            }
        }
    }
}

} // namespace squarestar::shell
