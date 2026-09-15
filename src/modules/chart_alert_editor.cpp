#include "modules/chart_alert_editor.hpp"

#include "application/app_state.hpp"
#include "application/price_alert_policy.hpp"
#include "application/ui_animation.hpp"
#include "modules/core.hpp"
#include "modules/currency_display.hpp"
#include "modules/ui_focus.hpp"
#include "services/config_save_queue.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace squarestar::shell {

using squarestar::application::AppState;
using squarestar::application::ConfiguredPriceAlert;
using squarestar::application::RemoveConfiguredPriceAlert;
using squarestar::application::StockContext;
using squarestar::application::UserFeedbackType;

void RenderPriceAlertEditor(AppState& state,
                            StockContext& context,
                            bool openEditor,
                            ImVec2 anchorMin,
                            ImVec2 anchorMax,
                            double currentPrice) {
    const std::string popupId =
        "Price Alert##" + std::string(context.navigation.ticker);
    const bool compactLite = state.navigation.liteGuiActive;
    const bool nativePopup = compactLite;
    if (openEditor) {
        const auto configuredAlert = ConfiguredPriceAlert(state.alerts, context);
        const double rawSeed = configuredAlert ? *configuredAlert : currentPrice;
        double displaySeed = rawSeed;
        double usdPerDisplayUnit = 1.0;
        if (TryConvertUsdForDisplay(state, rawSeed, displaySeed) &&
            TryConvertDisplayToUsd(state, 1.0, usdPerDisplayUnit)) {
            context.alerts.priceAlertEditorValue = displaySeed;
            context.alerts.priceAlertEditorUsdPerDisplayUnit = usdPerDisplayUnit;
            context.alerts.priceAlertEditorCurrency =
                std::string(DisplayCurrencyCode(state));
        } else {
            context.alerts.priceAlertEditorValue = rawSeed;
            context.alerts.priceAlertEditorUsdPerDisplayUnit = 1.0;
            context.alerts.priceAlertEditorCurrency = "USD";
        }
        PlayUISound(compactLite ? "click.wav" : "transition.wav", state);
        if (nativePopup)
            ImGui::OpenPopup(popupId.c_str());
    }

    int nativePopupStyleVars = 0;
    if (nativePopup) {
        ImGui::SetNextWindowPos(ImVec2(anchorMax.x, anchorMin.y),
                                ImGuiCond_Always,
                                ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(278.0f, 0.0f),
                                            ImVec2(278.0f, 210.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,
                            squarestar::application::UiRounding(state, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 9.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        nativePopupStyleVars = 4;
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
                                       ImVec2(316.0f, 0.0f),
                                       ImVec2(316.0f, 220.0f))) {
            return;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    }

    DrawObjectFocusOutline(state, anchorMin, anchorMax, true, 4);
    ImGui::PushFont(state.render.fontData);
    ImGui::Text("%s price alert", context.navigation.ticker);
    ImGui::PopFont();
    const char* currencyLabel = context.alerts.priceAlertEditorCurrency.c_str();
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
        const double savedDisplay =
            *configuredAlert /
            std::max(context.alerts.priceAlertEditorUsdPerDisplayUnit, 1e-12);
        ImGui::TextDisabled("Saved: %.2f %s", savedDisplay, currencyLabel);
    }

    const float buttonWidth = compactLite ? 66.0f : 70.0f;
    const float buttonHeight = compactLite ? 28.0f : 30.0f;
    const bool validAlert = std::isfinite(context.alerts.priceAlertEditorValue) &&
                            context.alerts.priceAlertEditorValue > 0.0;
    if (!validAlert)
        ImGui::BeginDisabled();
    const bool setClicked =
        ImGui::Button("Set", ImVec2(buttonWidth, buttonHeight));
    if (!validAlert)
        ImGui::EndDisabled();
    if ((setClicked || enterPressed) && validAlert) {
        const double thresholdUsd =
            context.alerts.priceAlertEditorValue *
            context.alerts.priceAlertEditorUsdPerDisplayUnit;
        if (!state.alerts.SetThreshold(context.navigation.ticker,
                                       thresholdUsd)) {
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
                ImGui::PopStyleVar(2);
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
        ImGui::PopStyleVar(2);
        EndAnimatedFloatingMenu();
    }
}

} // namespace squarestar::shell
