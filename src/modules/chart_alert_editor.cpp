#include "modules/chart_alert_editor.hpp"

#include "application/app_state.hpp"
#include "application/persisted_state.hpp"
#include "application/price_alert_policy.hpp"
#include "application/ui_animation.hpp"
#include "modules/core.hpp"
#include "modules/ui_focus.hpp"
#include "presentation/ui_metrics.hpp"
#include "services/config_persistence.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "services/config_save_queue.hpp"
namespace squarestar::shell {

using squarestar::application::AppState;
using squarestar::application::ConfiguredPriceAlert;
using squarestar::application::RemoveConfiguredPriceAlert;
using squarestar::application::StockContext;
using squarestar::application::UserFeedbackType;
using squarestar::presentation::kControlHeight;

void RenderPriceAlertEditor(AppState& state,
                            StockContext& context,
                            bool openEditor,
                            ImVec2 anchorMin,
                            ImVec2 anchorMax,
                            double currentPrice,
                            const char* currencyLabel) {
    const std::string popupId =
        "Price Alert##" + std::string(context.navigation.ticker);
    const bool compactLite = state.navigation.liteGuiActive;
    const bool nativePopup = compactLite;
    if (openEditor) {
        const auto configuredAlert = ConfiguredPriceAlert(state.alerts, context);
        context.alerts.priceAlertEditorValue =
            configuredAlert ? *configuredAlert : currentPrice;
        PlayUISound(compactLite ? "click.wav" : "transition.wav", state);
        if (nativePopup)
            ImGui::OpenPopup(popupId.c_str());
    }

    int nativePopupStyleVars = 0;
    if (nativePopup) {
        // LiteGUI uses a compact native popup pinned above the price text.
        ImGui::SetNextWindowPos(ImVec2(anchorMax.x, anchorMin.y),
                                ImGuiCond_Always,
                                ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(292.0f, 0.0f),
                                            ImVec2(292.0f, 260.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,
                            squarestar::application::UiRounding(state, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        nativePopupStyleVars = 2;
        if (compactLite) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 4.0f));
            nativePopupStyleVars += 2;
        }
        if (!ImGui::BeginPopup(popupId.c_str(),
                               ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize)) {
            ImGui::PopStyleVar(nativePopupStyleVars);
            return;
        }
    } else {
        if (!BeginAnimatedFloatingMenu(state,
                                       popupId.c_str(),
                                       openEditor,
                                       ImVec2(anchorMin.x, anchorMax.y + 6.0f),
                                       ImVec2(0.0f, 0.0f),
                                       state.UiAnimationsEnabled(),
                                       ImVec2(360.0f, 0.0f),
                                       ImVec2(std::numeric_limits<float>::max(),
                                              std::numeric_limits<float>::max()))) {
            return;
        }
    }

    DrawObjectFocusOutline(state, anchorMin, anchorMax, true, 4);
    ImGui::PushFont(state.render.fontData);
    ImGui::Text("%s price alert", context.navigation.ticker);
    ImGui::PopFont();
    if (compactLite)
        ImGui::TextDisabled("Alert at or below this price.");
    else
        ImGui::TextDisabled("Alert when SquareStar receives a price at or below this value.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize(currencyLabel).x -
                            ImGui::GetStyle().ItemSpacing.x);
    const bool enterPressed = ImGui::InputDouble("##PriceAlertValue",
                                                  &context.alerts.priceAlertEditorValue,
                                                  0.0,
                                                  0.0,
                                                  "%.2f",
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::TextUnformatted(currencyLabel);
    const auto configuredAlert = ConfiguredPriceAlert(state.alerts, context);
    if (configuredAlert) {
        if (compactLite)
            ImGui::TextDisabled("Saved: %.2f %s", *configuredAlert, currencyLabel);
        else
            ImGui::TextDisabled("Saved alert: %.2f %s", *configuredAlert, currencyLabel);
    }

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttonWidth =
        std::max(compactLite ? 72.0f : 80.0f,
                 (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f);
    const float buttonHeight = compactLite ? 30.0f : kControlHeight;
    const bool validAlert = std::isfinite(context.alerts.priceAlertEditorValue) &&
                            context.alerts.priceAlertEditorValue > 0.0;
    if (!validAlert)
        ImGui::BeginDisabled();
    const bool setClicked =
        ImGui::Button("Set alert", ImVec2(buttonWidth, buttonHeight));
    if (!validAlert)
        ImGui::EndDisabled();
    if ((setClicked || enterPressed) && validAlert) {
        if (!state.alerts.SetThreshold(context.navigation.ticker,
                                       context.alerts.priceAlertEditorValue)) {
            PublishUserFeedback(
                state,
                UserFeedbackType::Warning,
                "Price alert limit reached",
                "Clear an existing price alert before adding another one.");
            if (nativePopup)
                ImGui::CloseCurrentPopup();
            else
                CloseAnimatedFloatingMenu(false);
            if (nativePopup) {
                ImGui::EndPopup();
                ImGui::PopStyleVar(nativePopupStyleVars);
            } else {
                EndAnimatedFloatingMenu();
            }
            return;
        }
        context.alerts.priceAlertTriggered = false;
        EvaluateStockPriceAlert(state, context);
        squarestar::config::RequestConfigSave();
        PlayUISound("click.wav", state);
        if (nativePopup)
            ImGui::CloseCurrentPopup();
        else
            CloseAnimatedFloatingMenu(true);
    }

    ImGui::SameLine();
    if (!configuredAlert)
        ImGui::BeginDisabled();
    if (ImGui::Button("Clear", ImVec2(buttonWidth, buttonHeight)) && configuredAlert) {
        (void)RemoveConfiguredPriceAlert(state, context.navigation.ticker);
        EvaluateStockPriceAlert(state, context);
        squarestar::config::RequestConfigSave();
        PlayUISound("click.wav", state);
        if (nativePopup)
            ImGui::CloseCurrentPopup();
        else
            CloseAnimatedFloatingMenu(true);
    }
    if (!configuredAlert)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, buttonHeight))) {
        PlayUISound("click.wav", state);
        if (nativePopup)
            ImGui::CloseCurrentPopup();
        else
            CloseAnimatedFloatingMenu(false);
    }

    DrawCurrentWindowFocusOutline(state, 4);
    if (nativePopup) {
        ImGui::EndPopup();
        ImGui::PopStyleVar(nativePopupStyleVars);
    } else {
        EndAnimatedFloatingMenu();
    }
}

} // namespace squarestar::shell
