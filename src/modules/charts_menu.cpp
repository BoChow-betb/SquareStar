#include "modules/charts.hpp"
#include "modules/charts_internal.hpp"
#include "modules/core.hpp"
#include "modules/lite_gui.hpp"
#include "modules/market_data.hpp"
#include "modules/ui_focus.hpp"
#include "modules/platform.hpp"
#include "modules/stock_details.hpp"
#include "modules/chart_controls.hpp"
#include "modules/chart_footer.hpp"
#include "modules/chart_alert_editor.hpp"
#include "modules/chart_tab_bar.hpp"
#include "modules/stock_comparison.hpp"
#include "domain/trading_status.hpp"

#include "services/config_save_queue.hpp"
#include "application/navigation_state.hpp"
#include "application/main_loop_signal.hpp"
#include "domain/chart_ranges.hpp"
#include "domain/market_runtime.hpp"
#include "domain/world_clock_zones.hpp"
#include "platform/world_clock_runtime.hpp"
#include "presentation/chart_axis.hpp"
#include "presentation/chart_export.hpp"
#include "presentation/chart_lod.hpp"
#include "presentation/chart_price_axis.hpp"
#include "presentation/chart_series.hpp"
#include "presentation/chart_style.hpp"
#include "presentation/chart_types.hpp"
#include "presentation/stock_display_text.hpp"
namespace squarestar::shell {


using squarestar::platform::Win32AppRuntime;
using squarestar::market::TIME_RANGES;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::AppState;
using squarestar::application::UserFeedbackType;
using squarestar::application::CanEnterStockComparison;
using squarestar::application::StockContext;
using squarestar::application::IsLightGuiTheme;
using squarestar::presentation::ChartVisualType;
using squarestar::presentation::ChartExportMethod;

void RenderStockChartContextMenu(AppState& state,
                                        StockContext& ctx,
                                        bool contextMenuInteractive,
                                        ImGuiViewport* chartViewport,
                                        ImVec2 stockCaptureMin,
                                        ImVec2 chartCaptureMin,
                                        ImVec2 chartCaptureMax) {
    const bool compactLiteMenu = state.navigation.liteGuiActive;
    const bool nativePopupMenu = compactLiteMenu;
    bool chartMenuOpen = false;
    bool liteMenuStylePushed = false;
    if (nativePopupMenu) {
        // LiteGUI opens the compact popup in the same frame as its trigger.
        const ImVec2 menuEstimate(252.0f, 414.0f);
        const char* popupName = "##LiteChartContextMenu";
        const bool menuTrigger =
            contextMenuInteractive &&
            ImGui::IsMouseHoveringRect(chartCaptureMin, chartCaptureMax, false) &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Right);
        ImGuiStorage* ownerStorage = ImGui::GetStateStorage();
        const ImGuiID popupId = ImGui::GetID(popupName);
        float* popupX = ownerStorage->GetFloatRef(popupId + 4,
                                                  ImGui::GetIO().MousePos.x);
        float* popupY = ownerStorage->GetFloatRef(popupId + 5,
                                                  ImGui::GetIO().MousePos.y);
        if (menuTrigger) {
            const ImVec2 popupPosition = ClampPopupPosition(
                chartViewport, ImGui::GetIO().MousePos, menuEstimate);
            *popupX = popupPosition.x;
            *popupY = popupPosition.y;
            ImGui::OpenPopup(popupName);
            PlayUISound("transition.wav", state);
            RequestGuiRedraw();
        } else {
            const ImVec2 popupPosition = ClampPopupPosition(
                chartViewport, ImVec2(*popupX, *popupY), menuEstimate);
            *popupX = popupPosition.x;
            *popupY = popupPosition.y;
        }
        ImGui::SetNextWindowPos(ImVec2(*popupX, *popupY), ImGuiCond_Appearing);
        const float maxPopupHeight = menuEstimate.y;
        ImGui::SetNextWindowSizeConstraints(ImVec2(menuEstimate.x, 0.0f),
                                            ImVec2(menuEstimate.x, maxPopupHeight));

        if (compactLiteMenu) {
            // Keep LiteGUI controls strictly monochrome even when the full
            // desktop theme uses a colored popup or accent palette.
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
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 9.0f));
            liteMenuStylePushed = true;
        }
        chartMenuOpen = ImGui::BeginPopup(
            popupName,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    } else {
        const bool contextMenuTriggered =
            contextMenuInteractive &&
            ImGui::IsMouseHoveringRect(chartCaptureMin, chartCaptureMax, false) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right, false);
        if (contextMenuTriggered)
            PlayUISound("transition.wav", state);
        chartMenuOpen = BeginClampedContextMenu(
            state,
            "CustomPlotMenu",
            chartViewport,
            ImVec2(300.0f, 500.0f),
            contextMenuInteractive,
            state.UiAnimationsEnabled(),
            chartCaptureMin,
            chartCaptureMax,
            false,
            ImVec2(std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max()));
    }
    if (chartMenuOpen) {
        if (compactLiteMenu) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
            static const char* chartStyles[] = {
                "Candlestick", "Line + Shaded Area"};
            static const char* crosshairModes[] = {
                "Static", "Dynamic (Follow)", "Dynamic (Top)"};
            static const char* timeLabelModes[] = {
                "Endpoints only", "Tooltip only", "Show on hover"};
            ImGui::TextDisabled("Line Types");
            ImGui::SetNextItemWidth(-1.0f);
            if (UiCombo(state, "##LiteChartStyle",
                             &state.config.plotLineType,
                             chartStyles,
                             IM_ARRAYSIZE(chartStyles)))
                PlayUISound("click.wav", state);
            ImGui::TextDisabled("Crosshair");
            ImGui::SetNextItemWidth(-1.0f);
            if (UiCombo(state, "##LiteCrosshairMode",
                             &state.config.crosshairMode,
                             crosshairModes,
                             IM_ARRAYSIZE(crosshairModes)))
                PlayUISound("click.wav", state);
            ImGui::TextDisabled("Time labels");
            ImGui::SetNextItemWidth(-1.0f);
            if (UiCombo(state, "##LiteTimeLabels",
                             &state.config.chartXAxisMode,
                             timeLabelModes,
                             IM_ARRAYSIZE(timeLabelModes))) {
                CommitUiSetting(state, "click.wav");
            }
            ImGui::Separator();
            const bool alreadySaved = IsTickerInWatchlist(state, ctx.navigation.ticker);
            const std::string ticker(ctx.navigation.ticker);
            const std::string watchlistLabel =
                alreadySaved ? ticker + " is in Watchlist"
                             : "Add " + ticker + " to watchlist";
            if (ImGui::MenuItem(
                    watchlistLabel.c_str(), nullptr, false, !alreadySaved)) {
                AddTickerToWatchlist(state, ctx.navigation.ticker);
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            ImGui::TextDisabled("Modes");
            if (ImGui::MenuItem("VS Mode")) {
                if (!CanEnterStockComparison(state)) {
                    ShowStockModeNotice(state,
                                        ctx,
                                        "Comparison needs another tab",
                                        "Open one more stock tab to compare.");
                } else {
                    ctx.navigation.nextUpperTab = 3;
                    PrepareStockComparisonMode(state, ctx, true);
                    PlayUISound("transition.wav", state);
                    RequestGuiRedraw();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (ImGui::MenuItem("Monitor Mode")) {
                if (!SetLiteMonitorMode(state, true)) {
                    ShowStockModeNotice(state,
                                        ctx,
                                        "Monitor needs another tab",
                                        "Open one more stock tab to monitor.");
                } else {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::Separator();
        } else {
            ImGui::TextDisabled("Line Types");
            bool chartTypeChanged = false;
            chartTypeChanged |=
                ImGui::RadioButton("Candlestick", &state.config.plotLineType, 0);
            chartTypeChanged |=
                ImGui::RadioButton("Line + Shaded Area", &state.config.plotLineType, 1);
            if (chartTypeChanged)
                PlayUISound("click.wav", state);
            ImGui::Separator();
            DrawCrosshairPositionMenuItems(state);
            ImGui::Separator();
            DrawTimeAxisLabelMenuItems(state);
            ImGui::Separator();
        }
        ImGui::TextDisabled("Export");
        auto exportChart = [&](ChartExportMethod exportMethod) {
            std::string path;
            const ChartVisualType exportType =
                state.config.plotLineType == 0 ? ChartVisualType::Candlestick
                                        : ChartVisualType::LineShaded;
            HWND owner = chartViewport && chartViewport->PlatformHandleRaw
                             ? (HWND)chartViewport->PlatformHandleRaw
                             : Win32AppRuntime().MainWindow();
            if (ChooseChartExportPathForMethod(
                    owner,
                    ctx.navigation.ticker,
                    TIME_RANGES[ctx.navigation.displayedTimeRangeIndex].label,
                    exportType,
                    "USD",
                    state.config.exportDirectory,
                    exportMethod,
                    path)) {
                if (exportMethod == ChartExportMethod::GuiCapture) {
                    if (!QueueGuiPresentationExport(
                            path,
                            chartViewport,
                            stockCaptureMin,
                            chartCaptureMax)) {
                        PublishUserFeedback(
                            state,
                            UserFeedbackType::Warning,
                            "Export busy", "Wait for the current chart export to finish.");
                    }
                } else {
                    const auto cachedName = state.config.searchHistoryNames.find(ctx.navigation.ticker);
                    const std::string fallbackCompany =
                        cachedName != state.config.searchHistoryNames.end()
                            ? cachedName->second
                            : std::string{};
                    ChartExportData exportData =
                        MakeChartExportSnapshot(ctx.RawData(), fallbackCompany);
                    if (!QueueChartFileExport(
                            std::move(exportData),
                            ctx.navigation.ticker,
                            TIME_RANGES[ctx.navigation.displayedTimeRangeIndex].label,
                            path,
                            exportType)) {
                        PublishUserFeedback(
                            state,
                            UserFeedbackType::Warning,
                            "Export busy", "Wait for the current chart export to finish.");
                    }
                }
            }
            if (nativePopupMenu)
                ImGui::CloseCurrentPopup();
            else
                CloseAnimatedFloatingMenu(false);
        };
        if (compactLiteMenu) {
            if (ImGui::MenuItem("Image (PNG/JPEG/PDF)..."))
                exportChart(ChartExportMethod::GuiCapture);
            if (ImGui::MenuItem("Data (CSV/TXT/JSON)..."))
                exportChart(ChartExportMethod::SourceData);
            ImGui::PopStyleVar(2);
        } else {
            DrawChartExportMenuItems(exportChart);
        }
        DrawCurrentWindowFocusOutline(state, 4);
        if (nativePopupMenu)
            ImGui::EndPopup();
        else
            EndAnimatedFloatingMenu();
    }
    if (liteMenuStylePushed) {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);
    }
}


} // namespace squarestar::shell
