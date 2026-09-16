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

using squarestar::application::ApiKeyValidation;
using squarestar::application::ApplicationRuntime;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::UiRounding;
using squarestar::presentation::kControlHeight;
using squarestar::presentation::kDialogButtonHeight;
using squarestar::presentation::GuiShellRuntime;
using squarestar::application::GUI_FRAME_RATE_MODE_COUNT;
using squarestar::application::AppState;
using squarestar::application::GetActionName;
using squarestar::application::InitializeDefaultKeybinds;
using squarestar::application::FormatKeyBind;
using squarestar::application::PersistedStateOf;
using squarestar::application::SetThemePreset;
using squarestar::application::IsLightGuiTheme;
using squarestar::application::UserFeedback;
using squarestar::application::UserFeedbackDestination;
using squarestar::application::UserFeedbackSound;
using squarestar::application::UserFeedbackType;
using squarestar::config::ClearSquareStarTemporaryData;
using squarestar::config::TemporaryDataCleanupResult;
using squarestar::secrets::CommitFinnhubApiKeyChange;
using squarestar::secrets::HasFinnhubApiKey;
using squarestar::secrets::SecureClear;
using squarestar::marketdata::ClearStockMemoryCache;
using squarestar::search::ClearSymbolSearchCache;
using squarestar::platform::StopAllAppAudio;
using squarestar::platform::PlayAppSoundRuntime;


void BeginSettingsCard(AppState& state,
                              const char* id,
                              const char* heading,
                              float fixedHeight,
                              bool inlineHeaderControl,
                              float fixedWidth) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ThemeVec(state.config.theme.panel));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ThemeVec(state.config.theme.floatingBorder, 0.70f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiRounding(state, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(ImGui::GetStyle().ItemSpacing.x, 8.0f));
    const ImGuiChildFlags childFlags =
        ImGuiChildFlags_Borders |
        (fixedHeight > 0.0f ? ImGuiChildFlags_None : ImGuiChildFlags_AutoResizeY);
    ImGui::BeginChild(id,
                      ImVec2(fixedWidth > 0.0f ? fixedWidth : 0.0f,
                             fixedHeight > 0.0f ? fixedHeight : 0.0f),
                      childFlags,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushFont(state.render.fontData);
    if (inlineHeaderControl)
        ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(heading);
    ImGui::PopFont();
    if (!inlineHeaderControl) {
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
    }
}

void EndSettingsCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

static void RenderSettingsFooter(AppState& state,
                                 int& settingsPage,
                                 bool& openCreditsModal) {
    constexpr float pageButtonWidth = 116.0f;
    constexpr float notificationButtonWidth = 132.0f;
    constexpr float creditsButtonWidth = 88.0f;
    constexpr float pageGap = 6.0f;
    constexpr float creditsGap = 12.0f;
    constexpr float footerControlWidth = creditsButtonWidth + creditsGap +
                                         pageButtonWidth + pageGap +
                                         notificationButtonWidth;
    const float startX = ImGui::GetCursorPosX() +
                         std::max(0.0f,
                                  ImGui::GetContentRegionAvail().x - footerControlWidth);
    ImGui::SetCursorPosX(startX);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiRounding(state, 8.0f));
    if (ImGui::Button("Credits", ImVec2(creditsButtonWidth, 32.0f))) {
        PlayUISound("transition.wav", state);
        openCreditsModal = true;
    }
    ImGui::SameLine(0.0f, creditsGap);
    auto pageButton = [&](const char* label, int page, float width) {
        const bool active = settingsPage == page;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              active ? ThemeVec(state.config.theme.inverseBg, 0.92f)
                                     : ThemeVec(state.config.theme.button));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              active ? ThemeVec(state.config.theme.inverseBg, 0.92f)
                                     : ThemeVec(state.config.theme.buttonHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              active ? ThemeVec(state.config.theme.inverseBg, 0.92f)
                                     : ThemeVec(state.config.theme.buttonActive));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              active ? ThemeVec(state.config.theme.inverseText)
                                     : ThemeVec(state.config.theme.text));
        if (ImGui::Button(label, ImVec2(width, 32.0f)) && !active) {
            settingsPage = page;
            GuiShellRuntime().SetSettingsPage(page);
            PlayUISound("transition.wav", state);
        }
        ImGui::PopStyleColor(4);
    };
    pageButton("General", 0, pageButtonWidth);
    ImGui::SameLine(0.0f, pageGap);
    pageButton("Notifications", 1, notificationButtonWidth);
    ImGui::PopStyleVar();
}

void RenderSettingsContent(AppState& state) {
    int settingsPage = std::clamp(GuiShellRuntime().SettingsPage(), 0, 1);
    GuiShellRuntime().SetSettingsPage(settingsPage);
    bool openSavedDataModal = false;
    bool openCreditsModal = false;


const float footerHeight = 32.0f;
    const float footerGapY = 10.0f;
    const float settingsStartX = ImGui::GetCursorPosX();
    const float settingsStartY = ImGui::GetCursorPosY();
    const float availableHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    const float bodyHeight = std::max(1.0f, availableHeight - footerHeight - footerGapY);

    const float bodyAvailableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    ImGui::BeginChild("SettingsPageBody",
                      ImVec2(bodyAvailableWidth, bodyHeight),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);


    ImGui::SetScrollY(0.0f);
    const float bodyInnerStartX = ImGui::GetCursorPosX();
    const float bodyInnerWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);


    const float bodySideGutter = std::clamp(bodyInnerWidth * 0.060f, 24.0f, 76.0f);
    const float bodyContentWidth =
        std::min(1320.0f, std::max(1.0f, bodyInnerWidth - bodySideGutter * 2.0f));
    const float bodyStartX =
        bodyInnerStartX + std::max(0.0f, (bodyInnerWidth - bodyContentWidth) * 0.5f);
    ImGui::SetCursorPosX(bodyStartX);

    if (settingsPage == 0)
        RenderGeneralSettingsPage(state, bodyContentWidth, openSavedDataModal);
    else
        RenderNotificationSettingsPage(state, bodyContentWidth);
    ImGui::EndChild();

    ImGui::SetCursorPosX(settingsStartX);
    ImGui::SetCursorPosY(settingsStartY + bodyHeight + footerGapY);
    RenderSettingsFooter(state, settingsPage, openCreditsModal);

    RenderCreditsModal(state, openCreditsModal);
    RenderClearDataModal(state, openSavedDataModal);
}


}
