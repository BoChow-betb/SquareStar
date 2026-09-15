#include "modules/stock_comparison.hpp"
#include "modules/chart_controls.hpp"
#include "modules/core.hpp"
#include "modules/ui_focus.hpp"
#include "modules/platform.hpp"
#include "modules/market_data.hpp"
#include "application/stock_render_cache.hpp"
#include "application/main_loop_signal.hpp"
#include "domain/chart_ranges.hpp"
#include "domain/market_runtime.hpp"
#include "presentation/chart_axis.hpp"
#include "presentation/chart_comparison.hpp"
#include "presentation/chart_export.hpp"
#include "presentation/chart_export_jobs.hpp"
#include "presentation/chart_series.hpp"
#include "presentation/chart_style.hpp"
#include "presentation/chart_types.hpp"

namespace squarestar::shell {

using squarestar::platform::Win32AppRuntime;
using squarestar::market::TIME_RANGES;
using squarestar::market::CachedMarketOpen;
using squarestar::application::UiRounding;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::AppState;
using squarestar::application::UserFeedbackType;
using squarestar::application::ComparisonHoverSample;
using squarestar::application::ComparisonSeries;
using squarestar::application::PrepareComparisonSelection;
using squarestar::application::StockContext;
using squarestar::application::TimeAxisTickCache;
using squarestar::application::IsLightGuiTheme;
using squarestar::presentation::NotificationCardWidth;
using squarestar::presentation::ChartVisualType;
using squarestar::presentation::ChartExportMethod;
using squarestar::presentation::ExportComparisonData;
using squarestar::presentation::EnsureConfiguredTimeAxisTicks;
using squarestar::presentation::FormatTimeAxisValue;
using squarestar::presentation::OneDayMarketSessionBounds;
using squarestar::presentation::TimeAxisFormatContext;
using squarestar::presentation::NearestSortedIndex;
using squarestar::presentation::SampleSortedSeriesAtX;
using squarestar::application::IsComparisonSymbolSelected;
using squarestar::presentation::BuildComparisonSeries;
using squarestar::presentation::EnsureComparisonYAxisTicks;
using squarestar::presentation::CrosshairDotColor;
using squarestar::presentation::QueueChartExportJob;

void PrepareStockComparisonMode(AppState& state, StockContext& primary, bool requestPicker) {
    PrepareComparisonSelection(
        state, primary, requestPicker,
        [&](StockContext& candidate, int rangeIndex) {
            SelectChartRange(state, candidate, rangeIndex);
        });
}
static bool QueueComparisonFileExport(std::vector<ComparisonSeries> series,
                                      int rangeIndex,
                                      std::string path) {
    const std::string resultPath = path;
    return QueueChartExportJob(
        resultPath,
        [series = std::move(series),
         rangeIndex,
         path = std::move(path)]() mutable {
            return ExportComparisonData(series, TIME_RANGES[rangeIndex].label, path);
        });
}
static float RenderComparisonStockPicker(AppState& state,
                                         StockContext& primary,
                                         float comparisonLegendY,
                                         bool showComparisonControls) {
    float comparisonButtonWidth = 156.0f;
    if (!showComparisonControls)
        return comparisonButtonWidth;

    ImGui::SetCursorPosY(comparisonLegendY);
    const ImGuiStyle& comparisonStyle = ImGui::GetStyle();
    char buttonLabel[64]{};
    snprintf(buttonLabel,
             sizeof(buttonLabel),
             "Choose stocks (%zu)",
             primary.navigation.comparisonSymbols.size());
    const float contentLeft = ImGui::GetWindowContentRegionMin().x;
    const float contentRight = ImGui::GetWindowContentRegionMax().x;
    const float maxControlWidth = std::max(1.0f, contentRight - contentLeft - 12.0f);
    const float labelButtonWidth =
        std::ceil(ImGui::CalcTextSize(buttonLabel).x + comparisonStyle.FramePadding.x * 2.0f);
    comparisonButtonWidth =
        std::min(std::max(comparisonButtonWidth, labelButtonWidth), maxControlWidth);
    ImGui::SetCursorPosX(
        std::max(contentLeft, contentRight - comparisonButtonWidth - 12.0f));
    bool togglePicker = primary.navigation.comparisonPickerRequested;
    primary.navigation.comparisonPickerRequested = false;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ThemeVec(state.config.theme.floatingBorder, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ThemeVec(state.config.theme.floatingBorder, 0.22f));
    const bool pickerButtonPressed = ImGui::Button(
        buttonLabel,
        ImVec2(comparisonButtonWidth, std::max(30.0f, ImGui::GetFrameHeight())));
    ImGui::PopStyleColor(3);
    if (pickerButtonPressed) {
        PlayUISound("click.wav", state);
        togglePicker = true;
    }
    const bool buttonHovered = ImGui::IsItemHovered();
    const ImVec2 buttonMin = ImGui::GetItemRectMin();
    const ImVec2 buttonMax = ImGui::GetItemRectMax();
    ImGuiViewport* pickerViewport = ImGui::GetWindowViewport();
    int pickerRows = 0;
    float widestTicker = 0.0f;
    for (const auto& candidate : state.marketData.activeContexts) {
        if (!candidate || !candidate->navigation.open || !candidate->RawData().success)
            continue;
        ++pickerRows;
        widestTicker = std::max(widestTicker, ImGui::CalcTextSize(candidate->navigation.ticker).x);
    }
    const float pickerRowHeight = std::max(26.0f, ImGui::GetTextLineHeight() + 4.0f);
    const float pickerFooterHeight = std::max(26.0f, ImGui::GetFrameHeight());
    const float pickerViewportWidth =
        pickerViewport ? pickerViewport->WorkSize.x : ImGui::GetMainViewport()->WorkSize.x;
    const float pickerViewportHeight =
        pickerViewport ? pickerViewport->WorkSize.y : ImGui::GetMainViewport()->WorkSize.y;
    const float pickerMaximumWidth = std::max(1.0f, pickerViewportWidth - 28.0f);
    const float pickerMaximumHeight = std::max(1.0f, pickerViewportHeight - 28.0f);
    const float pickerDesiredWidth =
        std::max(240.0f,
                 std::ceil(widestTicker + ImGui::GetFrameHeight() +
                           comparisonStyle.WindowPadding.x * 2.0f + 16.0f));
    const float pickerWidth = std::min(pickerDesiredWidth, pickerMaximumWidth);
    constexpr int maximumVisiblePickerRows = 8;
    const int visiblePickerRows = std::max(1, std::min(pickerRows, maximumVisiblePickerRows));
    const float requestedPickerListHeight =
        static_cast<float>(visiblePickerRows) * pickerRowHeight +
        static_cast<float>(std::max(0, visiblePickerRows - 1)) * 4.0f;
    const float pickerChromeHeight =
        24.0f + pickerFooterHeight + comparisonStyle.ItemSpacing.y * 2.0f + 8.0f;
    const float pickerListHeight =
        std::min(requestedPickerListHeight,
                 std::max(1.0f, pickerMaximumHeight - pickerChromeHeight));
    const ImVec2 estimatedPickerSize(
        pickerWidth,
        std::min(pickerMaximumHeight, pickerChromeHeight + pickerListHeight));
    const ImVec2 pickerPosition = ClampPopupPosition(
        pickerViewport,
        ImVec2(buttonMax.x - estimatedPickerSize.x, buttonMax.y + 6.0f),
        estimatedPickerSize);
    const bool nativeLitePicker = state.navigation.liteGuiActive;
    bool pickerOpen = false;
    bool litePickerStylePushed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
    if (nativeLitePicker) {
        constexpr const char* popupName = "##LiteComparisonStockPicker";
        if (togglePicker) {
            ImGui::OpenPopup(popupName);
            RequestGuiRedraw();
        }
#ifdef IMGUI_HAS_VIEWPORT
        if (pickerViewport)
            ImGui::SetNextWindowViewport(pickerViewport->ID);
#endif
        ImGui::SetNextWindowPos(pickerPosition, ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(estimatedPickerSize.x, 0.0f),
                                            estimatedPickerSize);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiRounding(state, 8.0f));
        litePickerStylePushed = true;
        pickerOpen = ImGui::BeginPopup(
            popupName,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    } else {
        pickerOpen = BeginAnimatedFloatingMenu(state,
                                               "Comparison stock tabs",
                                               togglePicker,
                                               pickerPosition,
                                               ImVec2(0.0f, 0.0f),
                                               state.UiAnimationsEnabled(),
                                               ImVec2(estimatedPickerSize.x, 0.0f),
                                               estimatedPickerSize);
    }
    if (pickerOpen) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiRounding(state, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 4.0f));
        // ImGui 1.92 gives selected checkboxes their own background color.
        // Clear that state as well as the ordinary frame colors so only
        // the neutral check mark remains, with no blue fill or gradient.
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_CheckboxSelectedBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ThemeVec(state.config.theme.text, 0.88f));
        const bool pickerListNeedsScroll =
            pickerRows > maximumVisiblePickerRows ||
            requestedPickerListHeight > pickerListHeight + 0.5f;
        const ImGuiWindowFlags pickerListFlags =
            pickerListNeedsScroll
                ? ImGuiWindowFlags_AlwaysVerticalScrollbar
                : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::BeginChild("##ComparisonStockList",
                          ImVec2(0.0f, pickerListHeight),
                          false,
                          pickerListFlags);
        for (auto& candidate : state.marketData.activeContexts) {
            if (!candidate || !candidate->navigation.open || !candidate->RawData().success)
                continue;
            const bool isPrimary = candidate.get() == &primary;
            bool selected = IsComparisonSymbolSelected(primary, candidate->navigation.ticker);
            const bool lastRequired = selected && primary.navigation.comparisonSymbols.size() <= 2;
            ImGui::PushID(candidate.get());
            if (isPrimary || lastRequired)
                ImGui::BeginDisabled();
            if (ImGui::Checkbox(candidate->navigation.ticker, &selected)) {
                PlayUISound("click.wav", state);
                const std::string symbol = candidate->navigation.ticker;
                if (selected) {
                    if (!IsComparisonSymbolSelected(primary, candidate->navigation.ticker))
                        primary.navigation.comparisonSymbols.push_back(symbol);
                    PrepareStockComparisonMode(state, primary, false);
                } else {
                    std::erase(primary.navigation.comparisonSymbols, symbol);
                }
            }
            if (isPrimary || lastRequired)
                ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(3);
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ThemeVec(state.config.theme.floatingBorder, 0.14f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ThemeVec(state.config.theme.floatingBorder, 0.22f));
        const bool donePressed =
            ImGui::Button("Done",
                          ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x),
                                 std::max(26.0f, ImGui::GetFrameHeight())));
        ImGui::PopStyleColor(3);
        if (donePressed) {
            PlayUISound("click.wav", state);
            if (nativeLitePicker)
                ImGui::CloseCurrentPopup();
            else
                CloseAnimatedFloatingMenu(true);
        }
        DrawCurrentWindowFocusOutline(state, 4);
        if (nativeLitePicker)
            ImGui::EndPopup();
        else
            EndAnimatedFloatingMenu();
    }
    if (litePickerStylePushed)
        ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    DrawObjectFocusOutline(state, buttonMin, buttonMax, buttonHovered, 3);
    return comparisonButtonWidth;
}

static void RenderComparisonEmptyNotice(AppState& state, const StockContext& primary) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float noticeWidth =
        std::max(1.0f, std::min(NotificationCardWidth(avail.x), avail.x));
    const char* title = primary.navigation.comparisonSymbols.size() < 2
                            ? "Comparison needs another tab"
                            : "Preparing comparison";
    const char* body = primary.navigation.comparisonSymbols.size() < 2
                           ? "Open one more stock tab to compare."
                           : "Aligning the selected tabs to one time range.";
    ImFont* titleFont = state.render.fontData ? state.render.fontData : ImGui::GetFont();
    ImFont* bodyFont = ImGui::GetFont();
    const float titleFontSize =
        state.render.fontData ? state.config.theme.fontData : ImGui::GetFontSize();
    const float bodyFontSize = ImGui::GetFontSize();
    const float contentWidth = std::max(40.0f, noticeWidth - 36.0f);
    const ImVec2 titleSize = titleFont->CalcTextSizeA(titleFontSize,
                                                     std::numeric_limits<float>::max(),
                                                     contentWidth,
                                                     title);
    const ImVec2 bodySize = bodyFont->CalcTextSizeA(bodyFontSize,
                                                   std::numeric_limits<float>::max(),
                                                   contentWidth,
                                                   body);
    const float noticeHeight = 14.0f + titleSize.y + 8.0f + bodySize.y + 14.0f;
    const bool lightNotice = IsLightGuiTheme(state.config.themeModeIndex);
    ImGui::SetCursorPos(
        ImVec2(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - noticeWidth) * 0.5f),
               ImGui::GetCursorPosY() + std::max(0.0f, (avail.y - noticeHeight) * 0.42f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiRounding(state, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        lightNotice ? ImVec4(0.975f, 0.975f, 0.98f, 0.995f)
                    : ImVec4(0.055f, 0.055f, 0.06f, 0.985f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        lightNotice ? ImVec4(0.55f, 0.55f, 0.58f, 1.0f)
                    : ImVec4(0.34f, 0.34f, 0.36f, 1.0f));
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        lightNotice ? ImVec4(0.08f, 0.08f, 0.09f, 1.0f)
                    : ImVec4(0.96f, 0.96f, 0.97f, 1.0f));
    ImGui::PushStyleColor(
        ImGuiCol_TextDisabled,
        lightNotice ? ImVec4(0.36f, 0.36f, 0.39f, 1.0f)
                    : ImVec4(0.70f, 0.70f, 0.73f, 1.0f));
    ImGui::BeginChild("##ComparisonNotice",
                      ImVec2(noticeWidth, noticeHeight),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushFont(titleFont);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::TextDisabled("%s", body);
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);
}

static void RenderComparisonLegend(const std::vector<ComparisonSeries>& series,
                                   float comparisonLegendY,
                                   bool showComparisonControls,
                                   float comparisonButtonWidth) {
    ImGui::SetCursorPosY(comparisonLegendY);
    const float legendRight =
        ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x -
        (showComparisonControls ? comparisonButtonWidth + 28.0f : 8.0f);
    bool legendContinuesLine = false;
    for (size_t i = 0; i < series.size(); ++i) {
        if (!legendContinuesLine)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(18.0f, ImGui::GetTextLineHeight()));
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(p.x, p.y + ImGui::GetTextLineHeight() * 0.5f),
            ImVec2(p.x + 16.0f, p.y + ImGui::GetTextLineHeight() * 0.5f),
            ImGui::ColorConvertFloat4ToU32(series[i].color),
            2.5f);
        ImGui::SameLine(0.0f, 5.0f);
        ImGui::TextUnformatted(series[i].symbol.c_str());
        if (i + 1 < series.size()) {
            const float nextWidth =
                18.0f + 5.0f + ImGui::CalcTextSize(series[i + 1].symbol.c_str()).x + 18.0f;
            legendContinuesLine = ImGui::GetItemRectMax().x + nextWidth < legendRight;
            if (legendContinuesLine)
                ImGui::SameLine(0.0f, 18.0f);
        } else {
            legendContinuesLine = false;
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 7.0f));
}


static void RenderComparisonContextMenu(AppState& state,
                                        StockContext& primary,
                                        const std::vector<ComparisonSeries>& series,
                                        ImGuiViewport* comparisonViewport,
                                        bool showComparisonControls,
                                        const ImVec2& stockCaptureMin,
                                        const ImVec2& comparisonPlotMin,
                                        const ImVec2& comparisonPlotSize) {
    const ImVec2 comparisonPlotMax(comparisonPlotMin.x + comparisonPlotSize.x,
                                   comparisonPlotMin.y + comparisonPlotSize.y);
    const bool compactLiteMenu = state.navigation.liteGuiActive;
    const bool nativePopupMenu = compactLiteMenu;
    bool menuOpen = false;
    bool liteMenuStylePushed = false;

    if (nativePopupMenu) {
        const ImVec2 menuEstimate(240.0f, 240.0f);
        constexpr const char* popupName = "##LiteComparisonContextMenu";
        const bool menuTrigger =
            showComparisonControls &&
            ImGui::IsMouseHoveringRect(comparisonPlotMin, comparisonPlotMax, false) &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Right);
        ImGuiStorage* ownerStorage = ImGui::GetStateStorage();
        const ImGuiID popupId = ImGui::GetID(popupName);
        float* popupX = ownerStorage->GetFloatRef(popupId + 4, ImGui::GetIO().MousePos.x);
        float* popupY = ownerStorage->GetFloatRef(popupId + 5, ImGui::GetIO().MousePos.y);
        if (menuTrigger) {
            const ImVec2 popupPosition =
                ClampPopupPosition(comparisonViewport, ImGui::GetIO().MousePos, menuEstimate);
            *popupX = popupPosition.x;
            *popupY = popupPosition.y;
            ImGui::OpenPopup(popupName);
            PlayUISound("transition.wav", state);
            RequestGuiRedraw();
        } else {
            const ImVec2 popupPosition = ClampPopupPosition(
                comparisonViewport, ImVec2(*popupX, *popupY), menuEstimate);
            *popupX = popupPosition.x;
            *popupY = popupPosition.y;
        }
        ImGui::SetNextWindowPos(ImVec2(*popupX, *popupY), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(menuEstimate.x, 0.0f),
                                            menuEstimate);

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
        liteMenuStylePushed = true;
        menuOpen = ImGui::BeginPopup(
            popupName,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    } else {
        const bool contextMenuTriggered =
            showComparisonControls &&
            ImGui::IsMouseHoveringRect(comparisonPlotMin, comparisonPlotMax, false) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right, false);
        if (contextMenuTriggered)
            PlayUISound("transition.wav", state);
        menuOpen = BeginClampedContextMenu(state,
                                           "ComparisonPlotMenu",
                                           comparisonViewport,
                                           ImVec2(260.0f, 270.0f),
                                           showComparisonControls,
                                           state.UiAnimationsEnabled(),
                                           comparisonPlotMin,
                                           comparisonPlotMax,
                                           ImVec2(260.0f, 330.0f));
    }

    if (menuOpen) {
        if (compactLiteMenu) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
            static const char* crosshairModes[] = {
                "Static", "Dynamic (Follow)", "Dynamic (Top)"};
            static const char* timeLabelModes[] = {
                "Endpoints only", "Tooltip only", "Show on hover"};
            ImGui::TextDisabled("Crosshair");
            ImGui::SetNextItemWidth(-1.0f);
            if (UiCombo(state,
                        "##LiteVsCrosshairMode",
                        &state.config.crosshairMode,
                        crosshairModes,
                        IM_ARRAYSIZE(crosshairModes)))
                PlayUISound("click.wav", state);
            ImGui::TextDisabled("Time labels");
            ImGui::SetNextItemWidth(-1.0f);
            if (UiCombo(state,
                        "##LiteVsTimeLabels",
                        &state.config.chartXAxisMode,
                        timeLabelModes,
                        IM_ARRAYSIZE(timeLabelModes)))
                CommitUiSetting(state, "click.wav");
            ImGui::Separator();
        } else {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
            DrawCrosshairPositionMenuItems(state);
            DrawTimeAxisLabelMenuItems(state);
            ImGui::Separator();
        }

        ImGui::TextDisabled(compactLiteMenu ? "Export" : "Export comparison");
        auto exportComparison = [&](ChartExportMethod method) {
            std::string path;
            constexpr ChartVisualType comparisonType = ChartVisualType::LineShaded;
            HWND owner = comparisonViewport && comparisonViewport->PlatformHandleRaw
                             ? (HWND)comparisonViewport->PlatformHandleRaw
                             : Win32AppRuntime().MainWindow();
            if (ChooseChartExportPathForMethod(
                    owner,
                    std::string(primary.navigation.ticker) + "_VS",
                    TIME_RANGES[primary.navigation.displayedTimeRangeIndex].label,
                    comparisonType,
                    "USD",
                    state.config.exportDirectory,
                    method,
                    path)) {
                if (method == ChartExportMethod::GuiCapture) {
                    if (!QueueGuiPresentationExport(
                            path,
                            comparisonViewport,
                            stockCaptureMin,
                            comparisonPlotMax)) {
                        PublishUserFeedback(state,
                                            UserFeedbackType::Warning,
                                            "Export busy",
                                            "Wait for the current chart export to finish.");
                    }
                } else if (!QueueComparisonFileExport(series,
                                                      primary.navigation.displayedTimeRangeIndex,
                                                      path)) {
                    PublishUserFeedback(state,
                                        UserFeedbackType::Warning,
                                        "Export busy",
                                        "Wait for the current chart export to finish.");
                }
            }
            if (nativePopupMenu)
                ImGui::CloseCurrentPopup();
            else
                CloseAnimatedFloatingMenu(false);
        };
        if (compactLiteMenu) {
            if (ImGui::MenuItem("GUI image (PNG/JPEG/PDF)"))
                exportComparison(ChartExportMethod::GuiCapture);
            if (ImGui::MenuItem("Source data (CSV/TXT/JSON)"))
                exportComparison(ChartExportMethod::SourceData);
            ImGui::PopStyleVar(2);
        } else {
            DrawChartExportMenuItems(exportComparison, "Comparison data (CSV/TXT/JSON)");
            ImGui::PopStyleVar(2);
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

static void PlotComparisonSeries(const std::vector<ComparisonSeries>& series) {
    for (const auto& item : series) {
        const bool decimated = !item.plotX.empty();
        if (decimated) {
            ImPlot::PlotLine(item.symbol.c_str(),
                             item.plotX.data(),
                             item.plotPercent.data(),
                             (int)item.plotPercent.size(),
                             {ImPlotProp_LineColor,
                              item.color,
                              ImPlotProp_LineWeight,
                              2.3f});
        } else {
            ImPlot::PlotLine(item.symbol.c_str(),
                             item.x.data(),
                             item.percent.data(),
                             (int)item.percent.size(),
                             {ImPlotProp_LineColor,
                              item.color,
                              ImPlotProp_LineWeight,
                              2.3f});
        }
    }
}

static void RenderComparisonCrosshair(AppState& state,
                                      StockContext& primary,
                                      const std::vector<ComparisonSeries>& series,
                                      const TimeAxisFormatContext& xAxisFormat,
                                      bool comparisonPlotFocused,
                                      double minX,
                                      double maxX,
                                      double minY,
                                      double comparisonAxisMin,
                                      double comparisonAxisMax,
                                      const ImVec2& manualAxisPlotPos,
                                      const ImVec2& manualAxisPlotSize) {
    const ImVec2 comparisonMouse = ImGui::GetMousePos();
    const bool comparisonTooltipHovered =
        primary.render.comparisonTooltipRectVisible &&
        comparisonMouse.x >= primary.render.comparisonTooltipRectMin.x &&
        comparisonMouse.x <= primary.render.comparisonTooltipRectMax.x &&
        comparisonMouse.y >= primary.render.comparisonTooltipRectMin.y &&
        comparisonMouse.y <= primary.render.comparisonTooltipRectMax.y;
    const bool comparisonCrosshairActive = comparisonPlotFocused || comparisonTooltipHovered;
    if (comparisonPlotFocused && !comparisonTooltipHovered) {
        const double mouseX = std::clamp(ImPlot::GetPlotMousePos().x, minX, maxX);
        primary.render.comparisonCrosshairX =
            series.front().x[NearestSortedIndex(series.front().x, mouseX)];
        primary.render.comparisonCrosshairValid = true;
    }
    if (!comparisonCrosshairActive)
        primary.render.comparisonTooltipRectVisible = false;
    if (!comparisonCrosshairActive || !primary.render.comparisonCrosshairValid)
        return;

    const double displayX = primary.render.comparisonCrosshairX;
    char timeLabel[40]{};
    FormatTimeAxisValue(displayX,
                        timeLabel,
                        (int)sizeof(timeLabel),
                        const_cast<TimeAxisFormatContext*>(&xAxisFormat));

    std::vector<ComparisonHoverSample>& samples = primary.render.comparisonHoverSamples;
    samples.clear();
    if (samples.capacity() < series.size())
        samples.reserve(series.size());
    double highestValue = -std::numeric_limits<double>::max();
    ImVec2 highestPoint{};
    for (size_t seriesIndex = 0; seriesIndex < series.size(); ++seriesIndex) {
        const ComparisonSeries& item = series[seriesIndex];
        double value = 0.0;
        if (!SampleSortedSeriesAtX(item.x, item.percent, displayX, value))
            continue;
        const ImVec2 point = ImPlot::PlotToPixels(displayX, value);
        ComparisonHoverSample sample;
        sample.seriesIndex = seriesIndex;
        sample.point = point;
        snprintf(sample.valueText.data(), sample.valueText.size(), "%+.2f%%", value);
        samples.push_back(sample);
        if (value > highestValue) {
            highestValue = value;
            highestPoint = point;
        }
    }

    const ImVec2 timeSize = ImGui::CalcTextSize(timeLabel);
    const float lineHeight = ImGui::GetTextLineHeight();
    constexpr float symbolValueGap = 8.0f;
    float maxSymbolWidth = 0.0f;
    float maxValueWidth = 0.0f;
    for (const auto& sample : samples) {
        const ComparisonSeries& item = series[sample.seriesIndex];
        maxSymbolWidth =
            std::max(maxSymbolWidth, ImGui::CalcTextSize(item.symbol.c_str()).x);
        maxValueWidth =
            std::max(maxValueWidth, ImGui::CalcTextSize(sample.valueText.data()).x);
    }
    const float contentWidth =
        std::max(timeSize.x, maxSymbolWidth + symbolValueGap + maxValueWidth);
    const ImVec2 boxSize(contentWidth + 20.0f,
                         16.0f + lineHeight * (1.0f + (float)samples.size()));
    const ImVec2 plotMax(manualAxisPlotPos.x + manualAxisPlotSize.x,
                         manualAxisPlotPos.y + manualAxisPlotSize.y);
    const float hoverX = ImPlot::PlotToPixels(displayX, minY).x;
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    draw->AddLine(ImVec2(hoverX, manualAxisPlotPos.y),
                  ImVec2(hoverX, plotMax.y),
                  ImGui::ColorConvertFloat4ToU32(
                      ThemeVec(state.config.theme.textDisabled, 0.55f)),
                  1.0f);
    for (const auto& sample : samples) {
        const ComparisonSeries& item = series[sample.seriesIndex];
        draw->AddCircleFilled(
            sample.point,
            5.5f,
            ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.bg, 0.96f)));
        draw->AddCircleFilled(
            sample.point,
            3.4f,
            ImGui::ColorConvertFloat4ToU32(
                CrosshairDotColor(IsLightGuiTheme(state.config.themeModeIndex), item.color)));
    }
    ImPlot::PopPlotClipRect();

    const double plotMidX = (comparisonAxisMin + comparisonAxisMax) * 0.5;
    ImVec2 boxMin{};
    if (state.config.crosshairMode == 0) {
        boxMin = ImVec2(manualAxisPlotPos.x + 10.0f, manualAxisPlotPos.y + 10.0f);
    } else {
        boxMin.x = displayX > plotMidX ? hoverX - boxSize.x - 14.0f : hoverX + 14.0f;
        if (state.config.crosshairMode == 1 && !samples.empty()) {
            const float above = highestPoint.y - boxSize.y - 14.0f;
            boxMin.y = above >= manualAxisPlotPos.y + 4.0f
                           ? above
                           : highestPoint.y + 14.0f;
        } else {
            boxMin.y = manualAxisPlotPos.y + 10.0f;
        }
    }
    boxMin.x = std::clamp(boxMin.x,
                          manualAxisPlotPos.x + 4.0f,
                          std::max(manualAxisPlotPos.x + 4.0f,
                                   plotMax.x - boxSize.x - 4.0f));
    boxMin.y = std::clamp(boxMin.y,
                          manualAxisPlotPos.y + 4.0f,
                          std::max(manualAxisPlotPos.y + 4.0f,
                                   plotMax.y - boxSize.y - 4.0f));
    const ImVec2 boxMax(boxMin.x + boxSize.x, boxMin.y + boxSize.y);
    draw->AddRectFilled(boxMin,
                        boxMax,
                        ImGui::ColorConvertFloat4ToU32(
                            ThemeVec(state.config.theme.floatingBg, 0.97f)),
                        UiRounding(state, 6.0f));
    draw->AddRect(boxMin,
                  boxMax,
                  ImGui::ColorConvertFloat4ToU32(
                      ThemeVec(state.config.theme.floatingBorder, 0.95f)),
                  UiRounding(state, 6.0f));
    ImVec2 textPos(boxMin.x + 10.0f, boxMin.y + 8.0f);
    draw->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), timeLabel);
    textPos.y += lineHeight;
    for (const auto& sample : samples) {
        const ComparisonSeries& item = series[sample.seriesIndex];
        const ImU32 seriesColor = ImGui::ColorConvertFloat4ToU32(item.color);
        draw->AddText(textPos, seriesColor, item.symbol.c_str());
        draw->AddText(ImVec2(textPos.x + maxSymbolWidth + symbolValueGap, textPos.y),
                      ImGui::GetColorU32(ImGuiCol_Text),
                      sample.valueText.data());
        textPos.y += lineHeight;
    }
    primary.render.comparisonTooltipRectMin = boxMin;
    primary.render.comparisonTooltipRectMax = boxMax;
    primary.render.comparisonTooltipRectVisible = true;
}

void RenderStockComparison(AppState& state, StockContext& primary, const ImVec2& stockCaptureMin) {
    constexpr double comparisonSecondsPerUnit = 86400.0; // elapsed days on the plot axis
    PrepareStockComparisonMode(state, primary, false);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ThemeVec(state.config.theme.plotBg));
    ImGui::BeginChild("ComparisonRegion",
                      ImVec2(0, 0),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    const bool cleanGuiCapture = IsCleanGuiCaptureFrame();
    const bool showComparisonControls = !cleanGuiCapture && !state.navigation.pureMonitorMode;
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    const ImGuiStyle& comparisonStyle = ImGui::GetStyle();
    const ImGuiWindow* comparisonWindow = ImGui::GetCurrentWindow();
    const float visibleContentTop =
        comparisonWindow->ClipRect.Min.y - comparisonWindow->Pos.y + comparisonWindow->Scroll.y;
    const float comparisonLegendY =
        std::max({ImGui::GetCursorPosY(),
                  ImGui::GetWindowContentRegionMin().y + comparisonStyle.FramePadding.y,
                  visibleContentTop + comparisonStyle.FramePadding.y});
    const float comparisonButtonWidth = RenderComparisonStockPicker(
        state, primary, comparisonLegendY, showComparisonControls);

    const std::vector<ComparisonSeries>& series = BuildComparisonSeries(state, primary);
    if (series.size() < 2) {
        RenderComparisonEmptyNotice(state, primary);
        ImGui::EndChild();
        return;
    }
    RenderComparisonLegend(
        series, comparisonLegendY, showComparisonControls, comparisonButtonWidth);

    double minX = primary.render.comparisonDataMinX;
    double maxX = primary.render.comparisonDataMaxX;
    double minY = primary.render.comparisonDataMinY;
    double maxY = primary.render.comparisonDataMaxY;
    if (primary.navigation.displayedTimeRangeIndex == 0 &&
        primary.RawData().instrumentNature !=
            squarestar::market::InstrumentNature::DerivativeContract) {
        std::time_t rawAxisAnchor =
            !primary.RawData().timestamps.empty()
                ? (std::time_t)std::llround(primary.RawData().timestamps.back())
                : std::time(nullptr);
        if (CachedMarketOpen())
            rawAxisAnchor = std::time(nullptr);
        const auto session = OneDayMarketSessionBounds(
            rawAxisAnchor, state.config.graphTimeZone == 1);
        minX = (session.first - primary.render.comparisonAxisOrigin) /
               comparisonSecondsPerUnit;
        maxX = (session.second - primary.render.comparisonAxisOrigin) /
               comparisonSecondsPerUnit;
    }
    if (!std::isfinite(minX) || !std::isfinite(maxX) || !(maxX > minX) ||
        !std::isfinite(minY) || !std::isfinite(maxY)) {
        ImGui::TextDisabled("Comparison data is temporarily unavailable.");
        ImGui::EndChild();
        return;
    }
    EnsureComparisonYAxisTicks(primary, minY, maxY);

    const bool marketAxis = state.config.graphTimeZone == 1;
    const int effectiveXAxisMode =
        cleanGuiCapture ? 2 : state.config.chartXAxisMode;
    EnsureConfiguredTimeAxisTicks(primary.render.comparisonTimeAxisCache,
                                  (uint64_t)primary.render.comparisonCacheSignature,
                                  series.front().x,
                                  minX,
                                  maxX,
                                  primary.navigation.displayedTimeRangeIndex,
                                  marketAxis,
                                  effectiveXAxisMode,
                                  primary.render.comparisonAxisOrigin,
                                  comparisonSecondsPerUnit);
    const TimeAxisTickCache& xAxisCache = primary.render.comparisonTimeAxisCache;
    TimeAxisFormatContext xAxisFormat{primary.navigation.displayedTimeRangeIndex,
                                      marketAxis,
                                      primary.render.comparisonAxisOrigin,
                                      comparisonSecondsPerUnit};
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(32.0f, 36.0f));
    ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2(7.0f, 14.0f));
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotBorderSize, 0.0f);
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0, 0, 0, 0));
    ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ThemeVec(state.config.theme.grid));
    const ImVec2 comparisonPlotMin = ImGui::GetCursorScreenPos();
    ImVec2 comparisonPlotSize = ImGui::GetContentRegionAvail();
    comparisonPlotSize.y = std::max(120.0f, comparisonPlotSize.y - 10.0f);
    bool comparisonPlotFocused = false;
    const double xPadding =
        std::max(60.0 / comparisonSecondsPerUnit, (maxX - minX) * 0.006);
    const double comparisonAxisMin = minX - xPadding;
    const double comparisonAxisMax = maxX + xPadding;
    ImVec2 manualAxisPlotPos{};
    ImVec2 manualAxisPlotSize{};
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    if (ImPlot::BeginPlot("##ComparisonPlot", comparisonPlotSize, kFinancialPlotFlags)) {
        SetupLockedFinancialPlotAxes();
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatTimeAxisValue, &xAxisFormat);
        ImPlot::SetupAxisLimits(
            ImAxis_X1, comparisonAxisMin, comparisonAxisMax, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, minY, maxY, ImGuiCond_Always);
        if (!xAxisCache.ticks.empty())
            ImPlot::SetupAxisTicks(
                ImAxis_X1,
                xAxisCache.ticks.data(),
                (int)xAxisCache.ticks.size(),
                xAxisCache.labelPtrs.data(),
                false);
        if (!primary.render.comparisonYAxisTicks.empty())
            ImPlot::SetupAxisTicks(
                ImAxis_Y1,
                primary.render.comparisonYAxisTicks.data(),
                (int)primary.render.comparisonYAxisTicks.size(),
                primary.render.comparisonYAxisLabelPtrs.data(),
                false);

        comparisonPlotFocused = showComparisonControls && ImPlot::IsPlotHovered();
        ImGuiViewport* comparisonViewport = ImGui::GetWindowViewport();
        RenderComparisonContextMenu(state,
                                    primary,
                                    series,
                                    comparisonViewport,
                                    showComparisonControls,
                                    stockCaptureMin,
                                    comparisonPlotMin,
                                    comparisonPlotSize);
        PlotComparisonSeries(series);
        manualAxisPlotPos = ImPlot::GetPlotPos();
        manualAxisPlotSize = ImPlot::GetPlotSize();
        if (!cleanGuiCapture) {
            const float axisTarget =
                state.config.chartXAxisMode == 2 && comparisonPlotFocused ? 1.0f : 0.0f;
            if (state.UiAnimationsEnabled())
                primary.render.comparisonXAxisHoverAlpha +=
                    (axisTarget - primary.render.comparisonXAxisHoverAlpha) *
                    (1.0f - std::exp(-16.0f * UiFrameDelta()));
            else
                primary.render.comparisonXAxisHoverAlpha = axisTarget;
            primary.render.comparisonXAxisHoverAlpha =
                std::clamp(primary.render.comparisonXAxisHoverAlpha, 0.0f, 1.0f);
        }
        RenderComparisonCrosshair(state,
                                  primary,
                                  series,
                                  xAxisFormat,
                                  comparisonPlotFocused,
                                  minX,
                                  maxX,
                                  minY,
                                  comparisonAxisMin,
                                  comparisonAxisMax,
                                  manualAxisPlotPos,
                                  manualAxisPlotSize);
        ImPlot::EndPlot();
    }
    ImGui::PopStyleVar();
    if (effectiveXAxisMode != 1)
        DrawFadingXAxisLabels(state,
                              xAxisCache.ticks,
                              xAxisCache.labels,
                              comparisonAxisMin,
                              comparisonAxisMax,
                              manualAxisPlotPos,
                              manualAxisPlotSize,
                              comparisonPlotMin.x,
                              comparisonPlotMin.x + comparisonPlotSize.x,
                              cleanGuiCapture || effectiveXAxisMode == 0
                                  ? 1.0f
                                  : primary.render.comparisonXAxisHoverAlpha);
    DrawObjectFocusOutline(state,
                           comparisonPlotMin,
                           ImVec2(comparisonPlotMin.x + comparisonPlotSize.x,
                                  comparisonPlotMin.y + comparisonPlotSize.y),
                           comparisonPlotFocused,
                           2);
    ImPlot::PopStyleColor(3);
    ImPlot::PopStyleVar(3);
    ImGui::EndChild();
}

} // namespace squarestar::shell
