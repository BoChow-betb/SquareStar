#include "modules/gui_shell.hpp"
#include "modules/gui_shell_internal.hpp"
#include "modules/interface_modals.hpp"
#include "modules/core.hpp"
#include "modules/ui_focus.hpp"
#include "modules/platform.hpp"
#include "modules/market_data.hpp"
#include "modules/views.hpp"
#include "modules/settings_view.hpp"
#include "modules/terminal_stock_windows.hpp"
#include "modules/stock_surface_feedback.hpp"
#include "modules/window_chrome.hpp"

#include "services/config_save_queue.hpp"
#include "application/app_limits.hpp"
#include "application/contextual_keybind_policy.hpp"
#include "application/key_bindings.hpp"
#include "application/main_loop_signal.hpp"
#include "application/navigation_state.hpp"
#include "application/screener_controller.hpp"
#include "application/stock_request_state.hpp"
#include "domain/market_symbol.hpp"
#include "domain/chart_ranges.hpp"

#include <vector>
namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::UiRounding;
using squarestar::application::AppState;
using squarestar::application::FindStockModeNoticeTarget;
using squarestar::application::CountMonitorStockTiles;
using squarestar::application::StockContext;
using squarestar::application::TerminalAction;
using squarestar::application::IsLightGuiTheme;
using squarestar::application::UserFeedbackType;
using squarestar::market::TIME_RANGES;
using squarestar::presentation::ChartExportMethod;
using squarestar::presentation::ChartVisualType;


static std::vector<MonitorExportData> CaptureFullMonitorExportData(AppState& state) {
    std::vector<MonitorExportData> snapshots;
    snapshots.reserve(CountMonitorStockTiles(state.marketData));
    for (const auto& context : state.marketData.activeContexts) {
        if (!context || !context->navigation.open || context->navigation.monitorExcluded)
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

static void ExportFullMonitor(AppState& state,
                              ImGuiViewport* viewport,
                              const ImVec2& workPos,
                              const ImVec2& workMax,
                              ChartExportMethod method) {
    std::string path;
    HWND owner = viewport && viewport->PlatformHandleRaw
                     ? (HWND)viewport->PlatformHandleRaw
                     : nullptr;
    if (!ChooseChartExportPathForMethod(owner,
                                        "Monitor",
                                        "workspace",
                                        ChartVisualType::LineShaded,
                                        "USD",
                                        state.config.exportDirectory,
                                        method,
                                        path))
        return;

    bool queued = false;
    if (method == ChartExportMethod::GuiCapture) {
        queued = QueueGuiPresentationExport(path, viewport, workPos, workMax, 0.0f);
    } else {
        std::vector<MonitorExportData> snapshots = CaptureFullMonitorExportData(state);
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

void RenderMonitorStockPicker(AppState& state,
                                     ImGuiViewport* viewport,
                                     const ImVec2& workPos,
                                     const ImVec2& workSize) {
    if (!state.navigation.pureMonitorMode || IsCleanGuiCaptureFrame())
        return;

    const ImVec2 workMax(workPos.x + workSize.x, workPos.y + workSize.y);
    const float menuWidth = std::min(320.0f, std::max(240.0f, workSize.x - 28.0f));
    const std::size_t openCount = static_cast<std::size_t>(std::count_if(
        state.marketData.activeContexts.begin(),
        state.marketData.activeContexts.end(),
        [](const auto& context) { return context && context->navigation.open; }));
    const int columnCount = menuWidth >= 300.0f && openCount > 6 ? 2 : 1;
    const float menuHeight = 126.0f +
        static_cast<float>((openCount + static_cast<std::size_t>(columnCount) - 1) /
                           static_cast<std::size_t>(columnCount)) * 28.0f;
    const ImVec4 menuLine = ThemeVec(state.config.theme.floatingBorder, 0.58f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 9.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, menuLine);
    ImGui::PushStyleColor(ImGuiCol_Separator, menuLine);
    const bool mouseToggle =
        ImGui::IsMouseHoveringRect(workPos, workMax, false) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right, false);
    if (mouseToggle)
        PlayUISound("click.wav", state);
    if (!BeginClampedContextMenu(
            state,
            "Monitor stock picker",
            viewport,
            ImVec2(menuWidth, menuHeight),
            true,
            state.UiAnimationsEnabled(),
            workPos,
            workMax,
            false,
            ImVec2(menuWidth, std::max(160.0f, workSize.y - 28.0f)))) {
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        return;
    }

    ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
    ImGui::TextUnformatted("Stocks");
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiRounding(state, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_CheckboxSelectedBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ThemeVec(state.config.theme.text, 0.88f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 3.0f));
    std::size_t selectedCount = CountMonitorStockTiles(state.marketData);
    if (ImGui::BeginTable("##MonitorStockChoices",
                          columnCount,
                          ImGuiTableFlags_SizingStretchSame |
                              ImGuiTableFlags_NoSavedSettings,
                          ImVec2(0.0f, 0.0f))) {
        int itemIndex = 0;
        for (auto& context : state.marketData.activeContexts) {
            if (!context || !context->navigation.open)
                continue;
            if (itemIndex % columnCount == 0)
                ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(itemIndex % columnCount);
            ImGui::PushID(context.get());
            bool shown = !context->navigation.monitorExcluded;
            const bool minimumReached =
                shown && selectedCount <= squarestar::application::kMinMonitorStockTiles;
            const bool maximumReached =
                !shown && selectedCount >= squarestar::application::kMaxMonitorStockTiles;
            if (minimumReached || maximumReached)
                ImGui::BeginDisabled();
            if (ImGui::Checkbox(context->navigation.ticker, &shown)) {
                context->navigation.monitorExcluded = !shown;
                if (shown)
                    ++selectedCount;
                else
                    --selectedCount;
                if (shown) {
                    context->navigation.upperTabIndex = 0;
                    context->navigation.nextUpperTab = -1;
                    context->render.tabFadeAnim = 1.0f;
                }
                state.render.monitorModeTimer = 0.0f;
                PlayUISound(shown ? "on.wav" : "off.wav", state);
                RequestGuiRedraw();
            }
            if (minimumReached || maximumReached)
                ImGui::EndDisabled();
            ImGui::PopID();
            ++itemIndex;
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
    ImGui::Separator();
    ImGui::TextDisabled("Export monitor");
    if (ImGui::MenuItem("Image (PNG/JPEG/PDF)...")) {
        ExportFullMonitor(state, viewport, workPos, workMax, ChartExportMethod::GuiCapture);
        CloseAnimatedFloatingMenu(false);
    }
    if (ImGui::MenuItem("Data (CSV/TXT/JSON)...")) {
        ExportFullMonitor(state, viewport, workPos, workMax, ChartExportMethod::SourceData);
        CloseAnimatedFloatingMenu(false);
    }
    EndAnimatedFloatingMenu();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void RenderMonitorExitButton(AppState& state,
                             ImGuiViewport* viewport,
                             const ImVec2& workPos,
                             const ImVec2& workSize) {
    (void)viewport;
    if (!state.navigation.pureMonitorMode || IsCleanGuiCaptureFrame())
        return;
    constexpr ImVec2 buttonSize(38.0f, 34.0f);
    const ImVec2 buttonPos(
        workPos.x + workSize.x - buttonSize.x - 12.0f,
        workPos.y + 10.0f);
    ImGui::SetNextWindowPos(buttonPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(buttonSize, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##MonitorExitOverlay", nullptr, flags);
    const bool pressed = ImGui::InvisibleButton("##ExitMonitorMode", buttonSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    if (hovered || held) {
        draw->AddRectFilled(
            min,
            max,
            ImGui::ColorConvertFloat4ToU32(
                ThemeVec(state.config.theme.panelAlt, held ? 0.62f : 0.42f)),
            UiRounding(state, 8.0f));
    }
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const ImU32 ink = ImGui::ColorConvertFloat4ToU32(
        ThemeVec(state.config.theme.text, hovered ? 0.96f : 0.68f));
    draw->AddLine(
        ImVec2(center.x - 7.0f, center.y),
        ImVec2(center.x + 8.0f, center.y),
        ink,
        1.8f);
    draw->AddLine(
        ImVec2(center.x - 7.0f, center.y),
        ImVec2(center.x - 1.0f, center.y - 6.0f),
        ink,
        1.8f);
    draw->AddLine(
        ImVec2(center.x - 7.0f, center.y),
        ImVec2(center.x - 1.0f, center.y + 6.0f),
        ink,
        1.8f);
    draw->AddLine(
        ImVec2(center.x + 8.0f, center.y - 8.0f),
        ImVec2(center.x + 8.0f, center.y + 8.0f),
        ink,
        1.5f);
    if (pressed)
        ExitPureMonitorMode(state);
    ImGui::End();
    ImGui::PopStyleVar();
}


} // namespace squarestar::shell
