#include "modules/stock_surface_feedback.hpp"

#include "modules/core.hpp"
#include "application/app_state.hpp"
#include "application/main_loop_signal.hpp"
#include "domain/world_clock_zones.hpp"
#include "platform/world_clock_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <string>

namespace squarestar::shell {

using squarestar::application::AppState;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::StockContext;
using squarestar::market::WORLD_ZONES;

void DrawSelectedWorldClockTooltip(const AppState& state, const std::time_t now) {
    const auto& visibleClocks = state.VisibleWorldClocks();
    if (visibleClocks.empty())
        return;

    // Tooltips belong to the stock surface that produced them. Clamp to that
    // owning surface so a tooltip cannot escape a dense Monitor-mode tile.
    const ImGuiStyle& style = ImGui::GetStyle();
    float labelWidth = 0.0f;
    float timeWidth = 0.0f;
    int validClockCount = 0;
    for (const int zoneIndex : visibleClocks) {
        if (zoneIndex < 0 || zoneIndex >= IM_ARRAYSIZE(WORLD_ZONES))
            continue;
        const squarestar::platform::WorldClockReading reading =
            squarestar::platform::ReadWorldClock(zoneIndex, now);
        const std::string clockTime =
            squarestar::platform::FormatClockTime(
                reading.calendar, !state.navigation.liteGuiActive);
        labelWidth =
            std::max(labelWidth, ImGui::CalcTextSize(WORLD_ZONES[zoneIndex].label).x);
        timeWidth = std::max(timeWidth, ImGui::CalcTextSize(clockTime.c_str()).x);
        ++validClockCount;
    }
    const float headerWidth = ImGui::CalcTextSize("World clocks").x;
    const float tableWidth = labelWidth + timeWidth + style.CellPadding.x * 4.0f;
    const ImVec2 tooltipEstimate(
        std::max(headerWidth, tableWidth) + style.WindowPadding.x * 2.0f + 4.0f,
        style.WindowPadding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() *
                                                 static_cast<float>(validClockCount + 1) +
            style.ItemSpacing.y + 4.0f);
    const ImVec2 anchorMin = ImGui::GetItemRectMin();
    const ImVec2 anchorMax = ImGui::GetItemRectMax();
    const ImVec2 desiredPosition(anchorMax.x - tooltipEstimate.x,
                                 anchorMin.y - tooltipEstimate.y - style.ItemSpacing.y);
    ImGui::SetNextWindowPos(
        ClampTooltipPositionToOwningWindow(desiredPosition, tooltipEstimate),
        ImGuiCond_Always);

    ImGui::BeginTooltip();
    ImGui::TextDisabled("World clocks");
    ImGui::Separator();
    if (ImGui::BeginTable("##SelectedWorldClocks",
                          2,
                          ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_NoSavedSettings)) {
        for (const int zoneIndex : visibleClocks) {
            if (zoneIndex < 0 || zoneIndex >= IM_ARRAYSIZE(WORLD_ZONES))
                continue;

            const squarestar::platform::WorldClockReading reading =
                squarestar::platform::ReadWorldClock(zoneIndex, now);
            const std::string clockTime =
                squarestar::platform::FormatClockTime(
                reading.calendar, !state.navigation.liteGuiActive);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(WORLD_ZONES[zoneIndex].label);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(clockTime.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndTooltip();
}

void RenderStockErrorOverlay(AppState& state, StockContext& ctx) {
    const std::string message =
        ctx.RawData().errorMessage.empty() ? "Ticker not found" : ctx.RawData().errorMessage;
    const std::string subtitle = "Refresh this tab or close it and try the search again.";
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushFont(state.render.fontGiant ? state.render.fontGiant : ImGui::GetFont());
    const ImVec2 mainSize = ImGui::CalcTextSize(message.c_str());
    ImGui::PopFont();
    ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
    const ImVec2 subSize = ImGui::CalcTextSize(subtitle.c_str());
    ImGui::PopFont();
    const float totalHeight = mainSize.y + 16.0f + subSize.y;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                         std::max(0.0f, (avail.y - totalHeight) * 0.5f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, (avail.x - mainSize.x) * 0.5f));
    ImGui::PushFont(state.render.fontGiant ? state.render.fontGiant : ImGui::GetFont());
    ImGui::TextColored(ThemeVec(state.config.theme.negative), "%s", message.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, (avail.x - subSize.x) * 0.5f));
    ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
    ImGui::TextDisabled("%s", subtitle.c_str());
    ImGui::PopFont();
}

void ShowStockModeNotice(AppState& state,
                         StockContext& ctx,
                         const char* title,
                         const char* message) {
    if (state.navigation.liteGuiActive || !UseForegroundNotificationBlocks()) {
        (void)ctx;
        (void)title;
        (void)message;
        PlayUISound("decline.wav", state);
        return;
    }
    ctx.render.transientNoticeTitle = title;
    ctx.render.transientNoticeMessage = message;
    ctx.render.transientNoticeUntil =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    PlayUISound("decline.wav", state);
    RequestGuiRedraw();
}

} // namespace squarestar::shell
