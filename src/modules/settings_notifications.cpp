#include "application/contextual_keybind_policy.hpp"
#include "modules/core.hpp"
#include "modules/market_data.hpp"
#include "modules/settings_modals.hpp"
#include "modules/settings_view.hpp"
#include "modules/settings_view_internal.hpp"
#include "services/api_key_store.hpp"
#include "services/config_persistence.hpp"
#include "services/network_runtime.hpp"
#include "services/secret_protection.hpp"
#include "services/stock_data_service.hpp"
#include "services/symbol_search_service.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <string>
#include <string_view>

#include "services/config_save_queue.hpp"
#include "application/frame_rate.hpp"
#include "application/key_bindings.hpp"
#include "platform/application_paths.hpp"
#include "platform/audio_runtime.hpp"
namespace squarestar::shell {

using squarestar::application::AppState;


void RenderNotificationSettingsPage(AppState& state,
                                           float bodyContentWidth) {
    static constexpr double thresholdValues[] = {0.0, 0.5, 1.0, 2.0, 3.0, 5.0, 7.5, 10.0};
    static constexpr const char* thresholdLabels[] = {
        "Any price change", "0.5% daily move", "1% daily move", "2% daily move",
        "3% daily move", "5% daily move", "7.5% daily move", "10% daily move"};
    static constexpr int cooldownValues[] = {0, 1, 5, 10, 15, 30, 60};
    static constexpr const char* cooldownLabels[] = {"No cooldown",
                                                     "1 minute",
                                                     "5 minutes",
                                                     "10 minutes",
                                                     "15 minutes",
                                                     "30 minutes",
                                                     "1 hour"};
    int thresholdIndex = 0;
    double bestThresholdDistance = std::numeric_limits<double>::max();
    for (int i = 0; i < IM_ARRAYSIZE(thresholdValues); ++i) {
        const double distance = std::abs(state.config.marketMoveThresholdPct - thresholdValues[i]);
        if (distance < bestThresholdDistance) {
            bestThresholdDistance = distance;
            thresholdIndex = i;
        }
    }
    int cooldownIndex = 0;
    int bestCooldownDistance = std::numeric_limits<int>::max();
    for (int i = 0; i < IM_ARRAYSIZE(cooldownValues); ++i) {
        const int distance = std::abs(state.config.marketMoveCooldownMinutes - cooldownValues[i]);
        if (distance < bestCooldownDistance) {
            bestCooldownDistance = distance;
            cooldownIndex = i;
        }
    }

    const bool notificationTwoColumns = bodyContentWidth >= 760.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                        ImVec2(notificationTwoColumns ? 8.0f : 0.0f, 8.0f));
    if (ImGui::BeginTable("MarketNotificationSettingsColumns",
                          notificationTwoColumns ? 2 : 1,
                          ImGuiTableFlags_SizingStretchSame |
                              ImGuiTableFlags_NoSavedSettings,
                          ImVec2(bodyContentWidth, 0.0f))) {
        auto DrawNotificationEventsCard = [&] {
            BeginSettingsCard(state, "MarketNotificationEvents", "Notification events");
            ImGui::TextDisabled("MASTER");
            if (NeutralCheckbox("Price changes", state.config.marketMoveNotifications, state))
                CommitUiSetting(state, state.config.marketMoveNotifications ? "on.wav" : "off.wav");

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 7.0f));
            if (!state.config.marketMoveNotifications)
                ImGui::BeginDisabled();
            ImGui::TextDisabled("EVENT TYPES");
            if (NeutralCheckbox("52wk high / low", state.config.marketMove52WeekEvents, state))
                CommitUiSetting(state, state.config.marketMove52WeekEvents ? "on.wav" : "off.wav");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                DrawContainedTooltip(
                    "Notify once when price crosses a 52-week high or low.");
            if (NeutralCheckbox("Open / prev-close crossings", state.config.marketMoveStateChanges, state))
                CommitUiSetting(state, state.config.marketMoveStateChanges ? "on.wav" : "off.wav");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                DrawContainedTooltip(
                    "Notify when price crosses today's open or previous close (including red/green flips).");

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextDisabled("DELIVERY");
            if (NeutralCheckbox("Batch nearby", state.config.marketMoveBatching, state))
                CommitUiSetting(state, state.config.marketMoveBatching ? "on.wav" : "off.wav");
            if (!state.config.marketMoveNotifications)
                ImGui::EndDisabled();
            EndSettingsCard();
        };

        auto DrawNotificationRulesCard = [&] {
            BeginSettingsCard(state, "MarketNotificationRules", "Trigger rules");
            if (!state.config.marketMoveNotifications)
                ImGui::BeginDisabled();
            ImGui::TextDisabled("THRESHOLD");
            ImGui::SetNextItemWidth(-1.0f);
            const bool thresholdChanged =
                UiCombo(state, "##MarketMoveThreshold",
                             &thresholdIndex,
                             thresholdLabels,
                             IM_ARRAYSIZE(thresholdLabels));
            if (thresholdChanged) {
                state.config.marketMoveThresholdPct = thresholdValues[thresholdIndex];
                state.alerts.ClearMarketMoveCooldowns();
                squarestar::config::RequestConfigSave();
            }

            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::TextDisabled("COOLDOWN");
            ImGui::SetNextItemWidth(-1.0f);
            const bool cooldownChanged =
                UiCombo(state, "##MarketMoveCooldown",
                             &cooldownIndex,
                             cooldownLabels,
                             IM_ARRAYSIZE(cooldownLabels));
            if (cooldownChanged) {
                state.config.marketMoveCooldownMinutes = cooldownValues[cooldownIndex];
                state.alerts.ClearMarketMoveCooldowns();
                squarestar::config::RequestConfigSave();
            }
            if (!state.config.marketMoveNotifications)
                ImGui::EndDisabled();
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.textDisabled));
            ImGui::TextWrapped(
                "Threshold applies to daily-move notifications. Cooldown applies per ticker to all enabled market-move events; 52-week and reference-crossing events use their own crossing rules.");
            ImGui::PopStyleColor();
            EndSettingsCard();
        };

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawNotificationEventsCard();
        if (notificationTwoColumns) {
            ImGui::TableSetColumnIndex(1);
            DrawNotificationRulesCard();
        } else {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            DrawNotificationRulesCard();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}


} // namespace squarestar::shell
