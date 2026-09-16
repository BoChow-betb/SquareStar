#include "modules/interface_modals.hpp"
#include "modules/lite_gui.hpp"
#include "modules/core.hpp"
#include "modules/ui_focus.hpp"
#include "modules/platform.hpp"
#include "modules/market_data.hpp"
#include "modules/views.hpp"
#include "modules/integrated_search_bar.hpp"
#include "modules/settings_view.hpp"
#include "modules/charts.hpp"
#include "presentation/stock_display_text.hpp"
#include "services/http_client.hpp"
#include "application/contextual_keybind_policy.hpp"
#include "application/key_bindings.hpp"
#include "application/navigation_state.hpp"
#include "application/price_alert_policy.hpp"
#include "application/runtime_decisions.hpp"
#include "application/stock_tab_policy.hpp"
#include "application/stock_request_state.hpp"
#include "application/workspace_lifecycle.hpp"
#include "domain/chart_ranges.hpp"

namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::application::UiModeRequest;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::UiRounding;
using squarestar::application::AppState;
using squarestar::application::FetchStartResult;
using squarestar::application::StockContext;
using squarestar::application::TerminalAction;
using squarestar::application::IsLightGuiTheme;
using squarestar::application::IsStockPriceAlertActive;
using squarestar::application::ComputeMonitorTileRect;
using squarestar::application::UserFeedbackType;
using squarestar::market::TIME_RANGES;
using squarestar::presentation::ChartExportMethod;
using squarestar::presentation::ChartVisualType;
using squarestar::application::ResolveLiteMonitorStockTabs;
using squarestar::platform::CenterGlfwWindowInWorkArea;


static void SyncLiteGuiStockTabs(AppState& state) {
    CaptureOpenGuiStockTabs(state);
    state.navigation.guiWorkspace.lastActiveTab = state.navigation.lastActiveTab;
}
static StockContext* FindLiteGuiActiveStock(AppState& state,
                                            std::size_t* resolvedIndex = nullptr) {
    const auto activeIndex = ResolveActiveStockTabIndex(state);
    if (!activeIndex)
        return nullptr;
    StockContext* active = state.marketData.activeContexts[*activeIndex].get();
    state.navigation.lastActiveTab = active->navigation.ticker;
    if (resolvedIndex)
        *resolvedIndex = *activeIndex;
    return active;
}
static void OpenLiteGuiStock(AppState& state, const std::string& rawTicker) {
    const StockOpenResult result = OpenStock(state, rawTicker);
    if (result == StockOpenResult::InvalidSymbol) {
        state.navigation.liteSearch.focusRequested = true;
        return;
    }
    if (result == StockOpenResult::SelectedExisting ||
        result == StockOpenResult::OpenedNew)
        PlayUISound("key.wav", state);
}
static std::string ResolveLiteGuiHoverCompanyName(const AppState& state,
                                                 const StockContext& ctx) {
    std::string company = squarestar::presentation::CleanCompanyDisplayName(
        ctx.RawData().companyName);
    if (company.empty()) {
        const auto cached = state.config.searchHistoryNames.find(ctx.navigation.ticker);
        if (cached != state.config.searchHistoryNames.end())
            company = squarestar::presentation::CleanCompanyDisplayName(cached->second);
    }
    if (company == "Fetching..." || company == ctx.navigation.ticker)
        company.clear();
    return company;
}

static void ClearLiteGuiStock(AppState& state) {
    const auto activeIndex = ResolveActiveStockTabIndex(state);
    if (!activeIndex)
        return;
    const auto nextIndex =
        FindAdjacentOpenStockTabIndex(state, *activeIndex, 1);
    const auto previousIndex =
        FindAdjacentOpenStockTabIndex(state, *activeIndex, -1);
    std::string replacementTicker;
    if (nextIndex)
        replacementTicker = state.marketData.activeContexts[*nextIndex]->navigation.ticker;
    else if (previousIndex)
        replacementTicker = state.marketData.activeContexts[*previousIndex]->navigation.ticker;

    std::unique_ptr<StockContext>& closing = state.marketData.activeContexts[*activeIndex];
    if (closing) {
        closing->navigation.open = false;
        SilencePriceAlertForClosedTab(state, *closing);
        state.marketData.retiredLiteContexts.push_back(std::move(closing));
    }
    state.marketData.activeContexts.erase(state.marketData.activeContexts.begin() +
                               static_cast<std::ptrdiff_t>(*activeIndex));
    state.navigation.lastActiveTab = std::move(replacementTicker);
    if (state.marketData.activeContexts.empty())
        state.navigation.liteSearch.focusRequested = true;
    PlayUISound("key.wav", state);
    RequestGuiRedraw();
}
static bool RenderLiteTabStepButton(AppState& state,
                                    const char* id,
                                    ImGuiDir direction,
                                    ImVec2 size,
                                    ImDrawFlags roundingFlags) {
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const bool lightPalette = IsLightGuiTheme(state.config.themeModeIndex);
    const ImVec4 idleFill = lightPalette ? ImVec4(0.82f, 0.82f, 0.84f, 1.0f)
                                         : ImVec4(0.18f, 0.18f, 0.21f, 1.0f);
    const ImVec4 hoverFill = lightPalette ? ImVec4(0.90f, 0.90f, 0.92f, 1.0f)
                                          : ImVec4(0.26f, 0.26f, 0.30f, 1.0f);
    const ImVec4 activeFill = lightPalette ? ImVec4(0.96f, 0.96f, 0.97f, 1.0f)
                                           : ImVec4(0.34f, 0.34f, 0.38f, 1.0f);
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(held ? activeFill
                                                        : hovered ? hoverFill
                                                                  : idleFill);
    const ImU32 border = ImGui::ColorConvertFloat4ToU32(
        ThemeVec(state.config.theme.floatingBorder, lightPalette ? 0.90f : 0.78f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(itemMin, itemMax, fill, UiRounding(state, 4.0f), roundingFlags);
    draw->AddRect(itemMin, itemMax, border, UiRounding(state, 4.0f), roundingFlags, 1.0f);
    const ImVec2 center((itemMin.x + itemMax.x) * 0.5f,
                        (itemMin.y + itemMax.y) * 0.5f);
    constexpr float halfWidth = 4.0f;
    constexpr float halfHeight = 2.8f;
    const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
    if (direction == ImGuiDir_Up) {
        draw->AddTriangleFilled(ImVec2(center.x, center.y - halfHeight),
                                ImVec2(center.x - halfWidth, center.y + halfHeight),
                                ImVec2(center.x + halfWidth, center.y + halfHeight),
                                ink);
    } else {
        draw->AddTriangleFilled(ImVec2(center.x, center.y + halfHeight),
                                ImVec2(center.x + halfWidth, center.y - halfHeight),
                                ImVec2(center.x - halfWidth, center.y - halfHeight),
                                ink);
    }
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    DrawLastItemFocusOutline(state, hovered || held, 4);
    return pressed;
}

bool SetLiteMonitorMode(AppState& state, bool enabled) {
    if (enabled && ResolveLiteMonitorStockTabs(state).count < 2)
        return false;
    if (state.navigation.liteMonitorMode == enabled)
        return true;
    state.navigation.liteMonitorMode = enabled;
    CloseAllAnimatedFloatingMenus();
    PlayUISound("transition.wav", state);
    RequestGuiRedraw();
    return true;
}

static bool ToggleLiteMonitorMode(AppState& state) {
    return SetLiteMonitorMode(state, !state.navigation.liteMonitorMode);
}

static std::vector<MonitorExportData> CaptureLiteMonitorExportData(AppState& state) {
    const auto selection = ResolveLiteMonitorStockTabs(state);
    std::vector<MonitorExportData> snapshots;
    snapshots.reserve(selection.count);
    for (std::size_t tileIndex = 0; tileIndex < selection.count; ++tileIndex) {
        const std::size_t contextIndex = selection.indices[tileIndex];
        if (contextIndex >= state.marketData.activeContexts.size())
            continue;
        const auto& context = state.marketData.activeContexts[contextIndex];
        if (!context || !context->navigation.open)
            continue;
        if (!context->RawData().success || context->RawData().timestamps.empty() ||
            context->RawData().closes.empty())
            continue;
        const auto cachedName = state.config.searchHistoryNames.find(context->navigation.ticker);
        const std::string fallbackCompany =
            cachedName != state.config.searchHistoryNames.end() ? cachedName->second : std::string{};
        snapshots.push_back(MakeMonitorExportSnapshot(
            context->RawData(),
            context->navigation.ticker,
            TIME_RANGES[context->navigation.displayedTimeRangeIndex].label,
            fallbackCompany));
    }
    return snapshots;
}

static void ExportLiteMonitor(AppState& state,
                              ImGuiViewport* viewport,
                              const ImVec2& gridMin,
                              const ImVec2& gridMax,
                              ChartExportMethod method) {
    std::string path;
    HWND owner = viewport && viewport->PlatformHandleRaw
                     ? (HWND)viewport->PlatformHandleRaw
                     : nullptr;
    if (!ChooseChartExportPathForMethod(owner,
                                        "LiteMonitor",
                                        "workspace",
                                        ChartVisualType::LineShaded,
                                        "USD",
                                        state.config.exportDirectory,
                                        method,
                                        path))
        return;

    bool queued = false;
    if (method == ChartExportMethod::GuiCapture) {
        queued = QueueGuiPresentationExport(path, viewport, gridMin, gridMax, 0.0f);
    } else {
        std::vector<MonitorExportData> snapshots = CaptureLiteMonitorExportData(state);
        if (snapshots.empty()) {
            PublishUserFeedback(state,
                                UserFeedbackType::Warning,
                                "Nothing to export",
                                "Monitor stocks do not have chart data yet.");
            return;
        }
        queued = QueueMonitorFileExport(std::move(snapshots), path);
    }
    if (!queued) {
        PublishUserFeedback(state,
                            UserFeedbackType::Warning,
                            "Export busy",
                            "Wait for the current export to finish, then try again.");
    }
}

static void RenderLiteMonitorContextMenu(AppState& state,
                                         ImGuiViewport* viewport,
                                         const ImVec2& gridMin,
                                         const ImVec2& gridMax) {
    if (IsCleanGuiCaptureFrame())
        return;

    constexpr ImVec2 menuEstimate(232.0f, 128.0f);
    constexpr const char* menuId = "##LiteMonitorContextMenu";
    const bool menuTrigger =
        ImGui::IsMouseHoveringRect(gridMin, gridMax, false) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    ImGuiStorage* ownerStorage = ImGui::GetStateStorage();
    const ImGuiID popupId = ImGui::GetID(menuId);
    float* popupX = ownerStorage->GetFloatRef(popupId + 4, ImGui::GetIO().MousePos.x);
    float* popupY = ownerStorage->GetFloatRef(popupId + 5, ImGui::GetIO().MousePos.y);
    if (menuTrigger) {
        const ImVec2 popupPosition =
            ClampPopupPosition(viewport, ImGui::GetIO().MousePos, menuEstimate);
        *popupX = popupPosition.x;
        *popupY = popupPosition.y;
        ImGui::OpenPopup(menuId);
        PlayUISound("transition.wav", state);
        RequestGuiRedraw();
    } else {
        const ImVec2 popupPosition =
            ClampPopupPosition(viewport, ImVec2(*popupX, *popupY), menuEstimate);
        *popupX = popupPosition.x;
        *popupY = popupPosition.y;
    }
    ImGui::SetNextWindowPos(ImVec2(*popupX, *popupY), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(menuEstimate.x, 0.0f), menuEstimate);

    const bool lightMenu = IsLightGuiTheme(state.config.themeModeIndex);
    const ImVec4 popupBg = lightMenu ? ImVec4(0.97f, 0.97f, 0.97f, 1.0f)
                                     : ImVec4(0.07f, 0.07f, 0.07f, 1.0f);
    const ImVec4 popupBorder = lightMenu ? ImVec4(0.22f, 0.22f, 0.22f, 1.0f)
                                         : ImVec4(0.56f, 0.56f, 0.56f, 1.0f);
    const ImVec4 popupText = lightMenu ? ImVec4(0.05f, 0.05f, 0.05f, 1.0f)
                                       : ImVec4(0.96f, 0.96f, 0.96f, 1.0f);
    const ImVec4 popupDim = lightMenu ? ImVec4(0.38f, 0.38f, 0.38f, 1.0f)
                                      : ImVec4(0.64f, 0.64f, 0.64f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, popupBg);
    ImGui::PushStyleColor(ImGuiCol_Border, popupBorder);
    ImGui::PushStyleColor(ImGuiCol_Separator, popupBorder);
    ImGui::PushStyleColor(ImGuiCol_Text, popupText);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, popupDim);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 7.0f));
    if (ImGui::BeginPopup(menuId,
                          ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        ImGui::TextDisabled("Export monitor");
        if (ImGui::MenuItem("GUI image (PNG/JPEG/PDF)")) {
            ExportLiteMonitor(state, viewport, gridMin, gridMax, ChartExportMethod::GuiCapture);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Source data (CSV/TXT/JSON)")) {
            ExportLiteMonitor(state, viewport, gridMin, gridMax, ChartExportMethod::SourceData);
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit Monitor Mode")) {
            ImGui::CloseCurrentPopup();
            SetLiteMonitorMode(state, false);
        }
        DrawCurrentWindowFocusOutline(state, 4);
        ImGui::PopStyleVar(2);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
}

static void RenderLiteMonitorGrid(AppState& state) {
    const auto selection = ResolveLiteMonitorStockTabs(state);
    if (selection.count < 2) {
        SetLiteMonitorMode(state, false);
        return;
    }

    const ImVec2 origin = ImGui::GetCursorPos();
    const ImVec2 gridMin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 gridMax(gridMin.x + available.x, gridMin.y + available.y);


    constexpr float tileInset = 0.0f;
    for (std::size_t tileIndex = 0; tileIndex < selection.count; ++tileIndex) {
        const std::size_t contextIndex = selection.indices[tileIndex];
        if (contextIndex >= state.marketData.activeContexts.size() ||
            !state.marketData.activeContexts[contextIndex]) {
            continue;
        }
        StockContext& ctx = *state.marketData.activeContexts[contextIndex];
        ctx.navigation.refreshSurfaceVisible = true;
        const auto tile = ComputeMonitorTileRect(0.0f,
                                                 0.0f,
                                                 available.x,
                                                 available.y,
                                                 static_cast<int>(tileIndex),
                                                 static_cast<int>(selection.count));
        ImGui::SetCursorPos(ImVec2(origin.x + tile.minX + tileInset,
                                  origin.y + tile.minY + tileInset));
        const ImVec2 tileSize(std::max(1.0f, tile.maxX - tile.minX - tileInset * 2.0f),
                              std::max(1.0f, tile.maxY - tile.minY - tileInset * 2.0f));
        ImGui::PushID(&ctx);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ThemeVec(state.config.theme.monitorBg));
        const bool childVisible = ImGui::BeginChild(
            "##LiteMonitorTile",
            tileSize,
            ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor();
        if (childVisible) {
            const int savedUpperTab = ctx.navigation.upperTabIndex;
            const int savedNextUpperTab = ctx.navigation.nextUpperTab;
            const bool savedMonitorMode = state.navigation.pureMonitorMode;
            ctx.navigation.upperTabIndex = 0;
            ctx.navigation.nextUpperTab = -1;
            state.navigation.pureMonitorMode = true;
            RenderSingleStockWindowContent(state, ctx, false);
            state.navigation.pureMonitorMode = savedMonitorMode;
            ctx.navigation.upperTabIndex = savedUpperTab;
            ctx.navigation.nextUpperTab = savedNextUpperTab;

        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            state.navigation.lastActiveTab = ctx.navigation.ticker;
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopID();
    }
    RenderLiteMonitorContextMenu(state, ImGui::GetWindowViewport(), gridMin, gridMax);
}

static void HandleLiteGuiKeybinds(AppState& state) {
    if (state.navigation.showExitModal || state.navigation.showInterfaceSavePrompt)
        return;
    if (IsActionPressed(state.config, TerminalAction::ToggleGuiMode)) {
        ApplicationRuntime().RequestUiMode(UiModeRequest::Gui);
        PlayUISound("transition.wav", state);
        RequestGuiRedraw();
        return;
    }
    const bool focusSearch = IsActionPressed(state.config, TerminalAction::FocusSearch);
    if (focusSearch) {
        state.navigation.liteSearch.focusRequested = true;
        PlayUISound("key.wav", state);
        RequestGuiRedraw();
    }
    StockContext* active = FindLiteGuiActiveStock(state);
    if (ImGui::GetIO().WantTextInput || !active)
        return;
    if (IsActionPressed(state.config, TerminalAction::TogglePureMonitorMode)) {
        ToggleLiteMonitorMode(state);
        return;
    }
    if (IsActionPressed(state.config, TerminalAction::CloseTab) ||
        IsActionPressed(state.config, TerminalAction::OpenHome)) {
        ClearLiteGuiStock(state);
        return;
    }
    const bool tabUp = IsActionPressed(state.config, TerminalAction::LiteStockTabUp) ||
                       IsActionPressed(state.config, TerminalAction::StockTabPrev);
    const bool tabDown = IsActionPressed(state.config, TerminalAction::LiteStockTabDown) ||
                         IsActionPressed(state.config, TerminalAction::StockTabNext);
    const int tabDirection = tabUp ? -1 : tabDown ? 1 : 0;
    if (tabDirection != 0 && SelectAdjacentStockTab(state, tabDirection)) {
        active = FindLiteGuiActiveStock(state);
        PlayUISound("click.wav", state);
        RequestGuiRedraw();
    }
    if (IsActionPressed(state.config, TerminalAction::RefreshData)) {
        if (RequestManualStockRefresh(state, *active) == FetchStartResult::Started)
            PlayUISound("key.wav", state);
        RequestGuiRedraw();
    }
}
void RenderLiteGui(AppState& state, GLFWwindow* window) {
    EnforceZeroGraphicsMode(state, true);
    state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Stock;
    StockContext* activeLiteStock = FindLiteGuiActiveStock(state);
    const bool hasLiteStock = activeLiteStock != nullptr;
    const bool priceAlertVisible = state.alerts.ToastCount() != 0;
    const bool marketMoveVisible = !state.render.notifications.marketMoves.notices.empty();


constexpr int compactLiteHeight = LITE_GUI_SEARCH_HEIGHT;


    const bool foregroundCardNeedsRoom = priceAlertVisible || marketMoveVisible;
    const int desiredLiteHeight =
        hasLiteStock ? LITE_GUI_STOCK_HEIGHT
                     : (foregroundCardNeedsRoom
                            ? std::max(compactLiteHeight, LITE_GUI_NOTIFICATION_HEIGHT)
                            : compactLiteHeight);
    if (window && state.navigation.liteGuiAppliedHeight != desiredLiteHeight) {
        glfwSetWindowSizeLimits(window,
                                LITE_GUI_WIDTH,
                                desiredLiteHeight,
                                LITE_GUI_WIDTH,
                                desiredLiteHeight);
        glfwSetWindowSize(window, LITE_GUI_WIDTH, desiredLiteHeight);
        CenterGlfwWindowInWorkArea(window, LITE_GUI_WIDTH, desiredLiteHeight);
        state.navigation.liteGuiAppliedHeight = desiredLiteHeight;
        RequestGuiRedraw();
    }
    state.navigation.previousActiveSidebarTab = squarestar::application::SidebarTab::Stock;
    if (state.render.appliedThemeModeIndex != state.config.themeModeIndex ||
        state.render.appliedZeroGraphics != state.ZeroGraphicsEnabled()) {
        ApplyTheme(state);
        state.render.appliedThemeModeIndex = state.config.themeModeIndex;
        state.render.appliedZeroGraphics = state.ZeroGraphicsEnabled();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 workPos = viewport->WorkPos;
    workPos.y += APP_TITLE_BAR_HEIGHT;
    ImVec2 workSize = viewport->WorkSize;
    workSize.y -= APP_TITLE_BAR_HEIGHT;
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        workPos,
        ImVec2(workPos.x + workSize.x, workPos.y + workSize.y),
        ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.bg)));

    const ImVec4 dynamicDimColor = ThemeVec(state.config.theme.dimOverlay);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dynamicDimColor);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 24.0f));
    RenderClosingModal(state, window);
    RenderInterfaceSavePrompt(state);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    const auto monitorSelection = ResolveLiteMonitorStockTabs(state);
    if (state.navigation.liteMonitorMode && monitorSelection.count < 2)
        SetLiteMonitorMode(state, false);
    const bool liteMonitorActive =
        state.navigation.liteMonitorMode && monitorSelection.count >= 2;

    ImGui::SetNextWindowPos(workPos);
    ImGui::SetNextWindowSize(workSize);
#ifdef IMGUI_HAS_VIEWPORT
    ImGui::SetNextWindowViewport(viewport->ID);
#endif
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        liteMonitorActive ? ImVec2(0.0f, 0.0f)
                                          : ImVec2(18.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##LiteGuiSurface", nullptr, flags);
    ImGui::PopStyleVar(3);


    const bool lightLitePalette = IsLightGuiTheme(state.config.themeModeIndex);
    const ImVec4 liteControl = lightLitePalette ? ImVec4(0.88f, 0.88f, 0.89f, 1.0f)
                                                : ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    const ImVec4 liteHover = lightLitePalette ? ImVec4(0.93f, 0.93f, 0.94f, 1.0f)
                                              : ImVec4(0.20f, 0.20f, 0.23f, 1.0f);
    const ImVec4 liteActive = lightLitePalette ? ImVec4(0.985f, 0.985f, 0.99f, 1.0f)
                                               : ImVec4(0.30f, 0.30f, 0.34f, 1.0f);
    const ImVec4 liteInk = lightLitePalette ? ImVec4(0.08f, 0.08f, 0.09f, 1.0f)
                                            : ImVec4(0.94f, 0.94f, 0.95f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, liteControl);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, liteHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, liteActive);
    ImGui::PushStyleColor(ImGuiCol_Button, liteControl);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, liteHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, liteActive);
    ImGui::PushStyleColor(ImGuiCol_Header, liteActive);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, liteHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, liteActive);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, liteInk);
    ImGui::PushStyleColor(ImGuiCol_CheckboxSelectedBg, liteActive);
    ImGui::PushStyleColor(ImGuiCol_Tab, liteControl);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, liteHover);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, liteActive);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, liteInk);

    const bool hasStock = hasLiteStock;


if (!liteMonitorActive) {
        constexpr float topRowGap = 10.0f;
        constexpr float tabStepGap = 4.0f;
        constexpr float tickerButtonWidth = 112.0f;
        constexpr float tabStepButtonWidth = 28.0f;
        const float searchWidth = hasStock
                                      ? std::max(220.0f,
                                                 ImGui::GetContentRegionAvail().x -
                                                     tickerButtonWidth - topRowGap -
                                                     tabStepButtonWidth - tabStepGap)
                                      : 0.0f;
        ImGui::BeginGroup();
        SearchBarOptions searchBarOptions;


searchBarOptions.fixedDropdownHeight = hasStock;
        searchBarOptions.detachedDropdown = !hasStock;
        searchBarOptions.showNotificationCenter = hasStock;
        searchBarOptions.notificationCenterVisibleCardLimit = 2;
        RenderIntegratedSearchBar(
            state,
            state.navigation.liteSearch,
            "LITE",
            searchWidth,
            [&](const std::string& target) {
                OpenLiteGuiStock(state, target);
            },
            searchBarOptions);
        ImGui::EndGroup();
        if (hasStock) {
            activeLiteStock = FindLiteGuiActiveStock(state);
            ImGui::SameLine(0.0f, topRowGap);
            ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiRounding(state, 8.0f));
            const std::string tickerLabel =
                std::string(activeLiteStock->navigation.ticker) + "##ClearLiteStock";
            const bool priceAlertActive =
                IsStockPriceAlertActive(state.alerts, *activeLiteStock);
            if (priceAlertActive) {


                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ThemeVec(state.config.theme.danger));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      ThemeVec(state.config.theme.dangerHover));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ThemeVec(state.config.theme.dangerActive));
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ThemeVec(state.config.theme.dangerText));
            }
            const bool clearTicker =
                ImGui::Button(tickerLabel.c_str(), ImVec2(tickerButtonWidth, 40.0f));
            if (priceAlertActive)
                ImGui::PopStyleColor(4);
            const bool tickerHovered = ImGui::IsItemHovered();
            if (tickerHovered) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                const std::string companyName =
                    ResolveLiteGuiHoverCompanyName(state, *activeLiteStock);
                if (!companyName.empty())
                    DrawContainedTooltip(companyName.c_str(), 320.0f);
            }
            if (clearTicker)
                ClearLiteGuiStock(state);
            ImGui::PopStyleVar();
            ImGui::PopFont();

            const auto activeIndex = ResolveActiveStockTabIndex(state);
            const bool showUp = activeIndex &&
                                FindAdjacentOpenStockTabIndex(state, *activeIndex, -1).has_value();
            const bool showDown = activeIndex &&
                                  FindAdjacentOpenStockTabIndex(state, *activeIndex, 1).has_value();
            if (showUp || showDown) {
                ImGui::SameLine(0.0f, tabStepGap);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
                ImGui::BeginGroup();
                constexpr ImVec2 stepSize(tabStepButtonWidth, 20.0f);
                bool stepUp = false;
                bool stepDown = false;
                if (showUp) {
                    stepUp = RenderLiteTabStepButton(state,
                                                     "##LiteTabStepUp",
                                                     ImGuiDir_Up,
                                                     stepSize,
                                                     ImDrawFlags_RoundCornersTop);
                } else {
                    ImGui::Dummy(stepSize);
                }
                if (showDown) {
                    stepDown = RenderLiteTabStepButton(state,
                                                       "##LiteTabStepDown",
                                                       ImGuiDir_Down,
                                                       stepSize,
                                                       ImDrawFlags_RoundCornersBottom);
                } else {
                    ImGui::Dummy(stepSize);
                }
                ImGui::EndGroup();
                ImGui::PopStyleVar();
                if ((stepUp && SelectAdjacentStockTab(state, -1)) ||
                    (stepDown && SelectAdjacentStockTab(state, 1))) {
                    PlayUISound("click.wav", state);
                    RequestGuiRedraw();
                }
            }
        }
    }

    activeLiteStock = FindLiteGuiActiveStock(state);
    for (auto& candidate : state.marketData.activeContexts) {
        if (candidate)
            candidate->navigation.refreshSurfaceVisible = false;
    }
    if (activeLiteStock && liteMonitorActive) {
        RenderLiteMonitorGrid(state);
    } else if (activeLiteStock) {
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        StockContext& ctx = *activeLiteStock;
        ctx.navigation.refreshSurfaceVisible = true;
        ImVec2 liteContentSize = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##LiteStockContent",
                          liteContentSize,
                          false,
                          ImGuiWindowFlags_NoScrollbar);
        RenderSingleStockWindowContent(state, ctx, true);
        ImGui::EndChild();

        if (!ctx.navigation.open)
            ClearLiteGuiStock(state);
    }
    ImGui::PopStyleColor(15);
    ImGui::End();
    HandleLiteGuiKeybinds(state);
    SyncLiteGuiStockTabs(state);
}

}
