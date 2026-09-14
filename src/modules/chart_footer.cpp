#include "modules/chart_footer.hpp"

#include "application/app_state.hpp"
#include "application/theme_profiles.hpp"
#include "application/ui_animation.hpp"
#include "domain/chart_ranges.hpp"
#include "domain/world_clock_zones.hpp"
#include "modules/core.hpp"
#include "modules/chart_alert_editor.hpp"
#include "modules/market_data.hpp"
#include "modules/stock_surface_feedback.hpp"
#include "modules/ui_focus.hpp"
#include "platform/world_clock_runtime.hpp"
#include "services/config_save_queue.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <string>

namespace squarestar::shell {

using squarestar::application::AppState;
using squarestar::application::StockContext;
using squarestar::application::UiRounding;
using squarestar::application::IsLightGuiTheme;
using squarestar::market::TIME_RANGES;
using squarestar::market::WORLD_ZONES;

static void RenderLiteChartRangeSelector(AppState& state, StockContext& ctx) {
        const bool rangePending =
            ctx.navigation.selectedTimeRangeIndex != ctx.navigation.displayedTimeRangeIndex;
        const std::string rangeLabel =
            std::string("Range ") + TIME_RANGES[ctx.navigation.displayedTimeRangeIndex].label +
            (rangePending ? " ·" : "");
        const std::string rangeButtonLabel = rangeLabel + "##LiteRangeMenu";
        const bool toggleRangeMenu =
            ImGui::Button(rangeButtonLabel.c_str(), ImVec2(104.0f, 32.0f));
        const bool rangeButtonHovered = ImGui::IsItemHovered();
        const bool rangeButtonHeld = ImGui::IsItemActive();
        const ImVec2 rangeButtonMin = ImGui::GetItemRectMin();
        const ImVec2 rangeButtonMax = ImGui::GetItemRectMax();
        if (rangeButtonHovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        DrawLastItemFocusOutline(state, rangeButtonHovered || rangeButtonHeld, 4);
        if (toggleRangeMenu) {
            ImGui::OpenPopup("##LiteRangePopup");
            PlayUISound("transition.wav", state);
        }
        ImGuiViewport* litePopupViewport = ImGui::GetWindowViewport();
        const float rangeRowHeight = std::max(24.0f, ImGui::GetTextLineHeightWithSpacing());
        const float rangePopupWidth = 188.0f;
        const float rangePopupHeight =
            22.0f + ImGui::GetTextLineHeight() + 10.0f +
            static_cast<float>(IM_ARRAYSIZE(TIME_RANGES)) * rangeRowHeight + 16.0f;
        const float rangePopupGap = 0.0f;
        const float rangePopupMargin = 8.0f;
        const ImVec2 rangePopupSize(rangePopupWidth, rangePopupHeight);
        const ImGuiViewport* effectiveRangeViewport =
            litePopupViewport ? litePopupViewport : ImGui::GetMainViewport();
        const float spaceAbove =
            rangeButtonMin.y - effectiveRangeViewport->WorkPos.y - rangePopupMargin;
        const float spaceBelow =
            effectiveRangeViewport->WorkPos.y + effectiveRangeViewport->WorkSize.y -
            rangeButtonMax.y - rangePopupMargin;
        const bool placeAbove =
            spaceAbove >= rangePopupHeight || spaceAbove > spaceBelow;
        const float rangePopupX = std::clamp(
            rangeButtonMin.x,
            effectiveRangeViewport->WorkPos.x + rangePopupMargin,
            std::max(effectiveRangeViewport->WorkPos.x + rangePopupMargin,
                     effectiveRangeViewport->WorkPos.x +
                         effectiveRangeViewport->WorkSize.x - rangePopupWidth -
                         rangePopupMargin));
        const ImVec2 rangePopupAnchor =
            placeAbove ? ImVec2(rangePopupX, rangeButtonMin.y - rangePopupGap)
                       : ImVec2(rangePopupX, rangeButtonMax.y + rangePopupGap);
        ImGui::SetNextWindowPos(rangePopupAnchor,
                                ImGuiCond_Appearing,
                                placeAbove ? ImVec2(0.0f, 1.0f)
                                           : ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(rangePopupSize, ImGuiCond_Appearing);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiRounding(state, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
        if (ImGui::BeginPopup("##LiteRangePopup",
                              ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoScrollbar)) {
            ImGui::TextDisabled("Chart range");
            ImGui::Separator();
            for (int rangeIndex = 0; rangeIndex < IM_ARRAYSIZE(TIME_RANGES);
                 ++rangeIndex) {
                const bool rangeSelected = ImGui::Selectable(
                    TIME_RANGES[rangeIndex].label,
                    ctx.navigation.displayedTimeRangeIndex == rangeIndex,
                    0,
                    ImVec2(rangePopupWidth - 20.0f, 0.0f));
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (rangeSelected) {
                    SelectStockViewRange(state, ctx, rangeIndex);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(3);

        ImGui::SameLine(0.0f, 12.0f);
}

static void RenderChartClockMenu(AppState& state,
                                 std::time_t currentTime,
                                 const std::string& localClockText,
                                 const std::string& desktopClockText,
                                 bool shortcutClock) {
    ImGui::PushID("ClockTimeText");
    if (state.navigation.liteGuiActive) {
        // Match the adjacent controls exactly; the old hover underline
        // made this block look one pixel taller than its neighbors.
        ImGui::Button(localClockText.c_str(), ImVec2(210.0f, 32.0f));
    } else {
        ImGui::TextDisabled("%s", desktopClockText.c_str());
    }
    const bool clockHovered = ImGui::IsItemHovered();
    if (clockHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (state.navigation.liteGuiActive) {
            // LiteGUI permits this one informational hover surface: the
            // clock block shows the currently selected world-clock times.
            DrawSelectedWorldClockTooltip(state, currentTime);
        } else {
            const ImVec2 clockTextMin = ImGui::GetItemRectMin();
            const ImVec2 clockTextMax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(clockTextMin.x, clockTextMax.y + 1.0f),
                ImVec2(clockTextMax.x, clockTextMax.y + 1.0f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                1.0f);
        }
    }
    bool toggleClockMenu = ImGui::IsItemClicked(ImGuiMouseButton_Left) || shortcutClock;
    if (toggleClockMenu)
        PlayUISound("transition.wav", state);
    ImGui::PopID();
    const ImVec2 clockMenuPosition = ImGui::GetItemRectMin();
    bool liteClockPopupOpen = false;
    bool animatedClockPopupOpen = false;
    const ImVec4 clockMenuLine = ThemeVec(state.config.theme.floatingBorder, 0.58f);
    const bool compactLiteClockMenu = state.navigation.liteGuiActive;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, clockMenuLine);
    ImGui::PushStyleColor(ImGuiCol_Separator, clockMenuLine);
    ImGuiViewport* clockViewport = ImGui::GetWindowViewport();
    const ImGuiViewport* effectiveClockViewport =
        clockViewport ? clockViewport : ImGui::GetMainViewport();
    const float clockPopupMargin = compactLiteClockMenu ? 8.0f : 14.0f;
    const float clockPopupMaxWidth =
        std::max(1.0f,
                 effectiveClockViewport->WorkSize.x - clockPopupMargin * 2.0f);
    const float clockPopupDesiredWidth = compactLiteClockMenu ? 292.0f : 500.0f;
    const float clockPopupWidth =
        std::min(clockPopupDesiredWidth, clockPopupMaxWidth);
    const float clockPopupDesiredMinWidth = compactLiteClockMenu ? 276.0f : 440.0f;
    const float clockPopupMinWidth =
        std::min(clockPopupDesiredMinWidth, clockPopupWidth);
    const float clockPopupGap = compactLiteClockMenu ? 2.0f : 6.0f;
    const float clockPopupMaxHeight =
        std::max(1.0f,
                 std::min(390.0f,
                          clockMenuPosition.y - effectiveClockViewport->WorkPos.y -
                              clockPopupMargin - clockPopupGap));
    const ImVec2 clockPopupMaximum(clockPopupWidth, clockPopupMaxHeight);
    const float clockPopupX = std::clamp(
        clockMenuPosition.x,
        effectiveClockViewport->WorkPos.x + clockPopupMargin,
        std::max(effectiveClockViewport->WorkPos.x + clockPopupMargin,
                 effectiveClockViewport->WorkPos.x +
                     effectiveClockViewport->WorkSize.x - clockPopupWidth -
                     clockPopupMargin));
    // Anchor the popup by its bottom edge so it stays immediately above the
    // clock label even when the auto-sized menu is shorter than its max height.
    const ImVec2 clockPopupAnchor(clockPopupX, clockMenuPosition.y - clockPopupGap);
    if (state.navigation.liteGuiActive) {
        if (toggleClockMenu)
            ImGui::OpenPopup("##LiteWorldClockPicker");
        ImGui::SetNextWindowPos(
            clockPopupAnchor, ImGuiCond_Appearing, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(clockPopupMinWidth, 0.0f),
                                            clockPopupMaximum);
        liteClockPopupOpen =
            ImGui::BeginPopup("##LiteWorldClockPicker",
                              ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoSavedSettings);
    } else {
        animatedClockPopupOpen =
            BeginAnimatedFloatingMenu(state,
                                      "WorldClockPicker",
                                      toggleClockMenu,
                                      clockPopupAnchor,
                                      ImVec2(0.0f, 1.0f),
                                      state.UiAnimationsEnabled(),
                                      ImVec2(clockPopupMinWidth, 0.0f),
                                      clockPopupMaximum);
    }
    if (liteClockPopupOpen || animatedClockPopupOpen) {
        ImGui::TextDisabled("Chart time axis");
        bool timeZoneChanged = false;
        if (ImGui::RadioButton("Local Time", &state.config.graphTimeZone, 0)) {
            PlayUISound("click.wav", state);
            timeZoneChanged = true;
        }
        ImGui::SameLine();
        const char* marketTimeLabel =
            compactLiteClockMenu ? "Market (NY)" : "Market Time (NY)";
        if (ImGui::RadioButton(marketTimeLabel, &state.config.graphTimeZone, 1)) {
            PlayUISound("click.wav", state);
            timeZoneChanged = true;
        }
        if (timeZoneChanged) {
            for (auto& candidate : state.marketData.activeContexts) {
                if (candidate)
                    candidate->marketData.needsPlotDataUpdate = true;
            }
            squarestar::config::RequestConfigSave();
        }
        ImGui::Separator();
        ImGui::TextDisabled("World clocks");
        const size_t clockLimit = state.navigation.liteGuiActive ? 2u : 5u;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        const bool lightClockMenu = IsLightGuiTheme(state.config.themeModeIndex);
        const ImVec4 clockFrame = lightClockMenu
                                      ? ImVec4(0.82f, 0.82f, 0.84f, 1.0f)
                                      : ImVec4(0.20f, 0.20f, 0.23f, 1.0f);
        const ImVec4 clockFrameHovered = lightClockMenu
                                             ? ImVec4(0.74f, 0.74f, 0.77f, 1.0f)
                                             : ImVec4(0.28f, 0.28f, 0.32f, 1.0f);
        const ImVec4 clockFrameActive = lightClockMenu
                                            ? ImVec4(0.66f, 0.66f, 0.70f, 1.0f)
                                            : ImVec4(0.36f, 0.36f, 0.41f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, clockFrame);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, clockFrameHovered);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, clockFrameActive);
        ImGui::PushStyleColor(ImGuiCol_CheckboxSelectedBg, clockFrameActive);
        ImGui::PushStyleColor(ImGuiCol_Border, clockMenuLine);
        ImGui::PushStyleColor(
            ImGuiCol_CheckMark,
            lightClockMenu ? ImVec4(0.03f, 0.03f, 0.03f, 1.0f)
                           : ImVec4(0.97f, 0.97f, 0.97f, 1.0f));
        const int clockColumns =
            !compactLiteClockMenu && clockPopupWidth >= 460.0f ? 2 : 1;
        ImGui::PushStyleVar(
            ImGuiStyleVar_CellPadding,
            compactLiteClockMenu ? ImVec2(2.0f, 1.0f) : ImVec2(3.0f, 2.0f));
        static constexpr const char* liteClockNames[] = {
            "UTC",
            "New York (ET)",
            "London (UK)",
            "Frankfurt",
            "Hong Kong (HKT)",
            "Beijing",
            "Tokyo (JST)",
            "Sydney (AET)",
        };
        static_assert(IM_ARRAYSIZE(liteClockNames) == IM_ARRAYSIZE(WORLD_ZONES));
        if (ImGui::BeginTable("##WorldClockChoices",
                              clockColumns,
                              ImGuiTableFlags_SizingStretchSame |
                                  ImGuiTableFlags_NoSavedSettings)) {
            for (int zoneIndex = 0; zoneIndex < IM_ARRAYSIZE(WORLD_ZONES); ++zoneIndex) {
                if (zoneIndex % clockColumns == 0)
                    ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(zoneIndex % clockColumns);
                const auto& visibleClocks = state.VisibleWorldClocks();
                bool active = std::find(visibleClocks.begin(),
                                        visibleClocks.end(),
                                        zoneIndex) != visibleClocks.end();
                const squarestar::platform::WorldClockReading reading =
                    squarestar::platform::ReadWorldClock(zoneIndex, currentTime);
                const std::string optionLabel =
                    std::string(liteClockNames[zoneIndex]) + "  \xC2\xB7  " +
                    squarestar::platform::FormatUtcOffset(reading.utcOffsetMinutes);
                const bool clockLimitReached =
                    !active && visibleClocks.size() >= clockLimit;
                if (clockLimitReached)
                    ImGui::BeginDisabled();
                if (ImGui::Checkbox(optionLabel.c_str(), &active)) {
                    PlayUISound("click.wav", state);
                    state.SetWorldClockEnabled(zoneIndex, active);
                    squarestar::config::RequestConfigSave();
                }
                if (clockLimitReached)
                    ImGui::EndDisabled();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar();
        DrawCurrentWindowFocusOutline(state);
        if (liteClockPopupOpen)
            ImGui::EndPopup();
        else
            EndAnimatedFloatingMenu();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

static void RenderLiteQuoteSummary(AppState& state,
                                   StockContext& ctx,
                                   double previousClose,
                                   const ImVec2& chartCaptureMin,
                                   const ImVec2& chartCaptureMax,
                                   float clockStripHeight,
                                   float footerRowTop) {
    const double quotePrice = ctx.RawData().currentPrice;
    const double rangeReference = previousClose;
    const double quoteChange =
        rangeReference > 0.0 ? quotePrice - rangeReference : 0.0;
    const double quotePercent =
        rangeReference > 0.0 ? quoteChange / rangeReference * 100.0 : 0.0;
    const double quoteEpsilon =
        std::max(1e-8, std::abs(rangeReference) * 1e-8);
    const ImVec4 quoteMoveColor =
        quoteChange < -quoteEpsilon  ? ThemeVec(state.config.theme.negative)
        : quoteChange > quoteEpsilon ? ThemeVec(state.config.theme.positive)
                                      : ThemeVec(state.config.theme.textDisabled);
    char quotePriceText[80]{};
    char quoteMoveText[80]{};
    snprintf(quotePriceText,
             sizeof(quotePriceText),
             "%.2f %s",
             quotePrice,
             "USD");
    snprintf(quoteMoveText,
             sizeof(quoteMoveText),
             "%+.2f (%+.2f%%)",
             quoteChange,
             quotePercent);
    ImFont* summaryFont = state.render.fontData ? state.render.fontData : ImGui::GetFont();
    const float summarySize =
        state.render.fontData ? state.config.theme.fontData : ImGui::GetFontSize();
    const ImVec2 priceTextSize = summaryFont->CalcTextSizeA(
        summarySize,
        std::numeric_limits<float>::max(),
        0.0f,
        quotePriceText);
    const ImVec2 moveTextSize = summaryFont->CalcTextSizeA(
        summarySize,
        std::numeric_limits<float>::max(),
        0.0f,
        quoteMoveText);
    const float summaryRight = chartCaptureMax.x - 18.0f;
    const float summaryTop =
        footerRowTop + std::max(0.0f, (32.0f - priceTextSize.y) * 0.5f);
    const float safeLeft = chartCaptureMin.x + 8.0f;
    constexpr float summaryGap = 14.0f;
    const float summaryWidth = priceTextSize.x + summaryGap + moveTextSize.x;
    const float summaryLeft = std::max(safeLeft, summaryRight - summaryWidth);
    const ImVec2 pricePosition(summaryLeft, summaryTop);
    ImDrawList* footerDraw = ImGui::GetWindowDrawList();
    footerDraw->PushClipRect(ImVec2(chartCaptureMin.x, chartCaptureMax.y),
                             ImVec2(chartCaptureMax.x,
                                    chartCaptureMax.y + clockStripHeight),
                             true);
    footerDraw->AddText(summaryFont,
                        summarySize,
                        pricePosition,
                        ImGui::GetColorU32(ImGuiCol_Text),
                        quotePriceText);
    footerDraw->AddText(summaryFont,
                        summarySize,
                        ImVec2(summaryLeft + priceTextSize.x + summaryGap,
                               summaryTop +
                                   std::max(0.0f,
                                            (priceTextSize.y - moveTextSize.y) * 0.5f)),
                        ImGui::ColorConvertFloat4ToU32(quoteMoveColor),
                        quoteMoveText);
    footerDraw->PopClipRect();

    // The Lite quote is drawn directly into the footer, so hit-test its
    // screen rectangle directly instead of moving ImGui's layout cursor.
    // This avoids Dear ImGui 1.92's SetCursorPos parent-boundary assert
    // and keeps every click independent of the previous activation.
    const ImVec2 priceFocusMin = pricePosition;
    const ImVec2 priceFocusMax(pricePosition.x + priceTextSize.x,
                               pricePosition.y + priceTextSize.y);
    // This text is draw-list content rather than a normal ImGui item.
    // Accept the whole Lite surface hierarchy instead of requiring the
    // exact child window to own hover, while still preventing clicks
    // through unrelated popup/notification windows.
    const bool priceHovered =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        ImGui::IsMouseHoveringRect(priceFocusMin, priceFocusMax, false);
    const bool priceClicked =
        priceHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left, false);
    if (priceHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        footerDraw->AddLine(ImVec2(priceFocusMin.x, priceFocusMax.y + 1.0f),
                            ImVec2(priceFocusMax.x, priceFocusMax.y + 1.0f),
                            ImGui::GetColorU32(ImGuiCol_Text),
                            1.0f);
    }
    DrawObjectFocusOutline(state,
                           priceFocusMin,
                           priceFocusMax,
                           priceHovered,
                           4);
    RenderPriceAlertEditor(state,
                           ctx,
                           priceClicked,
                           priceFocusMin,
                           priceFocusMax,
                           quotePrice,
                           "USD");
}

void RenderStockChartFooter(AppState& state,
                            StockContext& ctx,
                            bool shortcutClock,
                            bool cleanGuiCapture,
                            double previousClose,
                            const ImVec2& chartCaptureMin,
                            const ImVec2& chartCaptureMax,
                            float clockStripHeight) {
    if (state.navigation.pureMonitorMode || cleanGuiCapture)
        return;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
    const float footerRowTop = ImGui::GetCursorScreenPos().y;
    ImGui::BeginGroup();

    const std::time_t currentTime = std::time(nullptr);
    const std::tm localTime = squarestar::platform::SafeTimeTm(currentTime, false);
    const std::string localClockText =
        "Local: " + squarestar::platform::FormatClockTime(localTime, false);
    std::string desktopClockText =
        "Local time " + squarestar::platform::FormatClockTime(localTime, true);
    if (!state.navigation.liteGuiActive) {
        static constexpr const char* compactClockNames[] = {
            "UTC", "New York", "London", "Frankfurt",
            "Hong Kong", "Beijing", "Tokyo", "Sydney"};
        const size_t clockCount =
            std::min<size_t>(5, state.config.activeWorldClocks.size());
        for (size_t i = 0; i < clockCount; ++i) {
            const int zoneIndex = state.config.activeWorldClocks[i];
            if (zoneIndex < 0 || zoneIndex >= IM_ARRAYSIZE(WORLD_ZONES))
                continue;
            const squarestar::platform::WorldClockReading reading =
                squarestar::platform::ReadWorldClock(zoneIndex, currentTime);
            desktopClockText += " | ";
            desktopClockText += compactClockNames[zoneIndex];
            desktopClockText += " ";
            desktopClockText +=
                squarestar::platform::FormatClockTime(reading.calendar, false);
        }
    }

    if (state.navigation.liteGuiActive) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiRounding(state, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 7.0f));
        RenderLiteChartRangeSelector(state, ctx);
    }
    RenderChartClockMenu(
        state, currentTime, localClockText, desktopClockText, shortcutClock);
    if (state.navigation.liteGuiActive)
        ImGui::PopStyleVar(2);
    ImGui::EndGroup();

    if (state.navigation.liteGuiActive) {
        RenderLiteQuoteSummary(state,
                               ctx,
                               previousClose,
                               chartCaptureMin,
                               chartCaptureMax,
                               clockStripHeight,
                               footerRowTop);
    }
}


} // namespace squarestar::shell
