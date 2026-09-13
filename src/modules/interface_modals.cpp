#include "modules/interface_modals.hpp"
#include "modules/core.hpp"
#include "modules/ui_focus.hpp"
#include "modules/platform.hpp"
#include "modules/market_data.hpp"
#include "modules/views.hpp"
#include "modules/settings_view.hpp"
#include "modules/charts.hpp"
#include "services/http_client.hpp"
#include "application/contextual_keybind_policy.hpp"
#include "application/key_bindings.hpp"
#include "application/navigation_state.hpp"
#include "application/stock_request_state.hpp"
#include "domain/market_symbol.hpp"

namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::platform::Win32AppRuntime;
using squarestar::application::UiModeRequest;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::UiRounding;
using squarestar::presentation::kDialogButtonHeight;
using squarestar::application::AppState;

// ============================================================================
void RenderClosingModal(AppState& state, GLFWwindow* window) {
    constexpr const char* popupId = "CloseTerminalPopup";
    if (state.navigation.showExitModal) {
        if (!ImGui::IsPopupOpen(popupId)) {
            ImGui::OpenPopup(popupId);
            PlayUISound("transition.wav", state);
        }
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 center = viewport->GetCenter();
    const float modalWidth =
        std::min(430.0f, std::max(380.0f, viewport->WorkSize.x - 32.0f));
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(modalWidth, 0.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiRounding(state, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 22.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ThemeVec(state.config.theme.modalBg));
    ImGui::PushStyleColor(ImGuiCol_Border, ThemeVec(state.config.theme.floatingBorder, 0.78f));
    ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.text));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::BeginPopupModal(popupId, nullptr, flags)) {
        auto dismiss = [&] {
            state.navigation.showExitModal = false;
            ImGui::CloseCurrentPopup();
        };
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            PlayUISound("click.wav", state);
            dismiss();
        }

        ImGui::PushFont(state.render.fontLarge);
        ImGui::TextUnformatted("Exit SquareStar?");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        constexpr float buttonHeight = 40.0f;
        constexpr float gap = 10.0f;
        constexpr float cancelWidth = 86.0f;
        constexpr float exitWidth = 86.0f;
        const float backgroundWidth = availableWidth - cancelWidth - exitWidth - gap * 2.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiRounding(state, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.dangerText));
        ImGui::PushStyleColor(ImGuiCol_Button, ThemeVec(state.config.theme.danger));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ThemeVec(state.config.theme.dangerHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ThemeVec(state.config.theme.dangerActive));
        ImGui::PushStyleColor(ImGuiCol_Border, ThemeVec(state.config.theme.dangerActive));
        if (ImGui::Button("Exit", ImVec2(exitWidth, buttonHeight))) {
            PlayUISound("click.wav", state);
            dismiss();
            ApplicationRuntime().RequestQuit();
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        ImGui::PopStyleColor(5);

        ImGui::SameLine(0.0f, gap);
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.inverseText));
        ImGui::PushStyleColor(ImGuiCol_Button, ThemeVec(state.config.theme.inverseBg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ThemeVec(state.config.theme.overviewActive));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive, ThemeVec(state.config.theme.overviewActive, 0.84f));
        ImGui::PushStyleColor(ImGuiCol_Border, ThemeVec(state.config.theme.inverseBg));
        if (ImGui::Button("Run in background", ImVec2(backgroundWidth, buttonHeight))) {
            PlayUISound("click.wav", state);
            MinimizeToTray(Win32AppRuntime().MainWindow());
            dismiss();
        }
        ImGui::PopStyleColor(5);

        ImGui::SameLine(0.0f, gap);
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.text));
        ImGui::PushStyleColor(ImGuiCol_Button, ThemeVec(state.config.theme.button));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ThemeVec(state.config.theme.buttonHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ThemeVec(state.config.theme.buttonActive));
        ImGui::PushStyleColor(ImGuiCol_Border, ThemeVec(state.config.theme.border));
        if (ImGui::Button("Cancel", ImVec2(cancelWidth, buttonHeight))) {
            PlayUISound("click.wav", state);
            dismiss();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);
}
void RenderInterfaceSavePrompt(AppState& state) {
    constexpr const char* popupId = "Save current tab(s)?";
    if (!state.navigation.showInterfaceSavePrompt)
        return;
    // A tab can close between the mode-switch request and this render pass.
    // Never surface a stale save prompt when no real live or parked stock tab
    // remains, and clear every piece of the deferred decision atomically.
    if (!HasActualStockTabs(state)) {
        state.navigation.showInterfaceSavePrompt = false;
        state.navigation.pendingInterfaceSwitchTarget = -1;
        state.navigation.interfaceSwitchDecisionReady = false;
        return;
    }
    if (state.navigation.pendingInterfaceSwitchTarget <= (int)UiModeRequest::None ||
        state.navigation.pendingInterfaceSwitchTarget > (int)UiModeRequest::LiteGui) {
        state.navigation.showInterfaceSavePrompt = false;
        state.navigation.pendingInterfaceSwitchTarget = -1;
        state.navigation.interfaceSwitchDecisionReady = false;
        return;
    }
    if (!ImGui::IsPopupOpen(popupId)) {
        // The control/keybind that requested the interface switch already owns
        // the transition cue. Opening this confirmation is a programmatic
        // follow-up to that same action, so do not replay the sound here.
        ImGui::OpenPopup(popupId);
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(450.0f, 0.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiRounding(state, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 22.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ThemeVec(state.config.theme.modalBg));
    ImGui::PushStyleColor(ImGuiCol_Border, ThemeVec(state.config.theme.floatingBorder, 0.78f));
    if (ImGui::BeginPopupModal(
            popupId,
            nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        auto cancel = [&] {
            state.navigation.showInterfaceSavePrompt = false;
            state.navigation.pendingInterfaceSwitchTarget = -1;
            state.navigation.interfaceSwitchDecisionReady = false;
            ImGui::CloseCurrentPopup();
        };
        auto complete = [&](bool saveData) {
            const UiModeRequest target =
                static_cast<UiModeRequest>(state.navigation.pendingInterfaceSwitchTarget);
            state.navigation.interfaceSwitchSaveCurrentData = saveData;
            state.navigation.interfaceSwitchDecisionReady = true;
            // Defer until ImGui has unwound this popup; switching immediately
            // can invalidate popup state.
            state.navigation.deferredInterfaceSwitchTarget = (int)target;
            state.navigation.showInterfaceSavePrompt = false;
            state.navigation.pendingInterfaceSwitchTarget = -1;
            ImGui::CloseCurrentPopup();
            RequestGuiRedraw();
        };
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            PlayUISound("click.wav", state);
            cancel();
        }
        ImGui::PushFont(state.render.fontLarge ? state.render.fontLarge : ImGui::GetFont());
        ImGui::TextUnformatted("Save current tab(s)?");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        const float width = (ImGui::GetContentRegionAvail().x - 10.0f) * 0.5f;
        int selection = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            selection = 0;
        else if (ImGui::IsKeyPressed(ImGuiKey_N, false))
            selection = 1;

        // Make the two decisions visually distinct instead of presenting two
        // identical neutral buttons. The labels also expose the keyboard shortcut.
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.text));
        ImGui::PushStyleColor(ImGuiCol_Button, ThemeVec(state.config.theme.positive, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ThemeVec(state.config.theme.positive, 0.46f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ThemeVec(state.config.theme.positive, 0.58f));
        if (ImGui::Button("Yes (Y)", ImVec2(width, kDialogButtonHeight)))
            selection = 0;
        ImGui::PopStyleColor(4);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.text));
        ImGui::PushStyleColor(ImGuiCol_Button, ThemeVec(state.config.theme.negative, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ThemeVec(state.config.theme.negative, 0.42f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ThemeVec(state.config.theme.negative, 0.54f));
        if (ImGui::Button("No (N)", ImVec2(width, kDialogButtonHeight)))
            selection = 1;
        ImGui::PopStyleColor(4);
        if (selection >= 0) {
            PlayUISound("transition.wav", state);
            complete(selection == 0);
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

} // namespace squarestar::shell
