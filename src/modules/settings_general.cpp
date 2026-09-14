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


static constexpr const char* kFinnhubHomeUrl = "https://finnhub.io/";

enum class ApiKeyInputAction { None, Clear, Validate };

static ApiKeyInputAction ResolveApiKeyInputAction(
    bool hasSavedKey,
    bool clearShortcutPressed,
    bool inputEdited,
    bool submittedOrDeactivated,
    bool showingSavedMask,
    std::string_view input) noexcept {
    if (hasSavedKey && (clearShortcutPressed || (inputEdited && input.empty())))
        return ApiKeyInputAction::Clear;
    if (submittedOrDeactivated && !showingSavedMask)
        return ApiKeyInputAction::Validate;
    return ApiKeyInputAction::None;
}

static UserFeedback FinnhubValidationFailureFeedback(ApiKeyValidation validation) {
    UserFeedback feedback;
    feedback.type = UserFeedbackType::Error;
    feedback.duration = std::chrono::seconds(8);
    feedback.destination = UserFeedbackDestination::Foreground;
    feedback.sound = UserFeedbackSound::Decline;
    feedback.actionLabel = "Open finnhub.io";
    feedback.actionUrl = kFinnhubHomeUrl;
    switch (validation) {
    case ApiKeyValidation::Empty:
        feedback.title = "Finnhub API key required";
        feedback.body = "Enter a key before validation.";
        break;
    case ApiKeyValidation::Forbidden:
        feedback.title = "Finnhub API key not accepted";
        feedback.body = "Finnhub rejected the key or its account access.";
        break;
    case ApiKeyValidation::NetworkError:
        feedback.title = "Finnhub validation unavailable";
        feedback.body = "Finnhub could not be reached. Check your connection and try again.";
        break;
    case ApiKeyValidation::Invalid:
    case ApiKeyValidation::Valid:
    default:
        feedback.title = "Invalid Finnhub API key";
        feedback.body = "The entered key did not return a valid Finnhub response.";
        break;
    }
    return feedback;
}

static UserFeedback FinnhubKeyClearedFeedback() {
    UserFeedback feedback;
    feedback.type = UserFeedbackType::Warning;
    feedback.title = "Finnhub API key cleared";
    feedback.body = "Finnhub-backed news, profiles, metrics, and quote fallback are now unavailable. Symbol search will continue through Yahoo Finance.";
    feedback.duration = std::chrono::seconds(8);
    feedback.destination = UserFeedbackDestination::Foreground;
    feedback.sound = UserFeedbackSound::Decline;
    feedback.actionLabel = "Open finnhub.io";
    feedback.actionUrl = kFinnhubHomeUrl;
    return feedback;
}

static UserFeedback FinnhubKeyPersistenceFailureFeedback() {
    UserFeedback feedback;
    feedback.type = UserFeedbackType::Error;
    feedback.title = "Finnhub API key not changed";
    feedback.body =
        "SquareStar could not commit this API key change. The current key was preserved; retry if needed.";
    feedback.duration = std::chrono::seconds(10);
    feedback.destination = UserFeedbackDestination::Foreground;
    feedback.sound = UserFeedbackSound::Error;
    return feedback;
}

static UserFeedback FinnhubKeySavedFeedback() {
    UserFeedback feedback;
    feedback.type = UserFeedbackType::Success;
    feedback.title = "Finnhub API key saved";
    feedback.body = "Finnhub-backed news, profiles, metrics, quote fallback, and additional symbol search coverage are ready.";
    feedback.duration = std::chrono::seconds(5);
    feedback.destination = UserFeedbackDestination::Foreground;
    feedback.sound = UserFeedbackSound::Confirmation;
    return feedback;
}

static bool PersistFinnhubApiKeyChange(AppState& state, const std::string& replacement) {
    auto current = squarestar::secrets::GetFinnhubApiKeySnapshot();
    const std::uint64_t expectedRevision = current.revision;
    squarestar::secrets::SecureClear(current.value);
    const auto result = CommitFinnhubApiKeyChange(
        expectedRevision,
        replacement,
        [&](std::uint64_t installedRevision) {
            return squarestar::config::PersistConfigSynchronously(
                PersistedStateOf(state), installedRevision);
        });
    const bool committed = result == squarestar::secrets::ApiKeyCommitResult::Committed;
    if (committed) {
        ClearStockMemoryCache();
        ClearSymbolSearchCache();
    }
    return committed;
}

static void DrawKeybindSettings(AppState& state) {
    const float launchWidth = 160.0f;
    if (ImGui::Button("Edit keybindings", ImVec2(launchWidth, kControlHeight))) {
        PlayUISound("transition.wav", state);
        ImGui::OpenPopup("Keybind Settings Modal");
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 center(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                        viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);
    const ImVec2 modalSize(
        std::max(1.0f, std::min(620.0f, viewport->WorkSize.x - 40.0f)),
        std::max(1.0f, std::min(500.0f, viewport->WorkSize.y - 40.0f)));
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    const bool lightKeybindPalette = IsLightGuiTheme(state.config.themeModeIndex);
    const ImVec4 monoPopup = ThemeVec(state.config.theme.modalBg);
    const ImVec4 monoText = ThemeVec(state.config.theme.text);
    const ImVec4 monoDisabled = ThemeVec(state.config.theme.textDisabled);
    const ImVec4 monoButton = lightKeybindPalette
                                  ? ImVec4(0.90f, 0.90f, 0.92f, 1.0f)
                                  : ImVec4(0.17f, 0.17f, 0.19f, 1.0f);
    const ImVec4 monoHover = lightKeybindPalette
                                 ? ImVec4(0.83f, 0.83f, 0.86f, 1.0f)
                                 : ImVec4(0.23f, 0.23f, 0.26f, 1.0f);
    const ImVec4 monoActive = lightKeybindPalette
                                  ? ImVec4(0.76f, 0.76f, 0.80f, 1.0f)
                                  : ImVec4(0.29f, 0.29f, 0.33f, 1.0f);
    const ImVec4 monoBorder = ThemeVec(state.config.theme.floatingBorder, 0.42f);
    const ImVec4 clear = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 scrollbarGrab = lightKeybindPalette
                                     ? ImVec4(0.57f, 0.57f, 0.61f, 0.62f)
                                     : ImVec4(0.48f, 0.48f, 0.53f, 0.55f);
    const float keybindRounding = UiRounding(state, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, keybindRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, keybindRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiRounding(state, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 7.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, monoPopup);
    ImGui::PushStyleColor(ImGuiCol_Border, monoBorder);
    ImGui::PushStyleColor(ImGuiCol_Text, monoText);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, monoDisabled);
    ImGui::PushStyleColor(ImGuiCol_Button, monoButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, monoHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, monoActive);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, clear);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, clear);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, scrollbarGrab);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, scrollbarGrab);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, scrollbarGrab);
    if (ImGui::BeginPopupModal("Keybind Settings Modal", NULL, flags)) {
        DrawRoundedCurrentWindowPanel(keybindRounding, ImGuiCol_PopupBg);
        std::map<std::string, int> bindCounts;
        for (const auto& [action, bind] : state.config.KeyBinds) {
            bindCounts[FormatKeyBind(bind)]++;
        }
        ImGui::TextUnformatted("Keyboard shortcuts");
        ImGui::TextDisabled(state.navigation.IsRebinding
                                ? "Press a key combination. Esc cancels."
                                : "Select a shortcut to change it.");
        ImGui::Spacing();
        ImGui::Separator();

        const float footerHeight = ImGui::GetFrameHeight() +
                                   ImGui::GetStyle().ItemSpacing.y * 3.0f + 2.0f;
        const ImVec2 listSize(
            0.0f,
            std::max(1.0f, ImGui::GetContentRegionAvail().y - footerHeight));
        const ImGuiTableFlags listFlags = ImGuiTableFlags_ScrollY |
                                               ImGuiTableFlags_SizingStretchProp |
                                               ImGuiTableFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 6.0f));
        if (ImGui::BeginTable("##KeybindList", 2, listFlags, listSize)) {
            ImGui::TableSetupColumn("##Action", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("##Shortcut", ImGuiTableColumnFlags_WidthFixed, 218.0f);
            for (auto& [action, bind] : state.config.KeyBinds) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 42.0f);
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                const std::string actionName = GetActionName(action);
                ImGui::TextUnformatted(actionName.c_str());

                ImGui::TableSetColumnIndex(1);
                const float shortcutCellWidth = ImGui::GetContentRegionAvail().x;
                const float shortcutWidth =
                    std::max(1.0f, std::min(210.0f, shortcutCellWidth - 8.0f));
                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    std::max(0.0f, shortcutCellWidth - shortcutWidth - 8.0f));
                ImGui::PushID(static_cast<int>(action));
                if (state.navigation.IsRebinding && state.navigation.ActionBeingRebound == action) {
                    ImGui::Button("Press keys...", ImVec2(shortcutWidth, 0.0f));
                    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END;
                         ++key) {
                        if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(key)))
                            continue;
                        if (key == ImGuiKey_Escape) {
                            PlayUISound("loss.wav", state);
                            state.navigation.IsRebinding = false;
                            break;
                        }
                        if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
                            key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
                            key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt ||
                            key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper) {
                            continue;
                        }
                        const char* keyName = ImGui::GetKeyName(static_cast<ImGuiKey>(key));
                        if (keyName) {
                            const std::string name(keyName);
                            if (name.find("Mouse") != std::string::npos ||
                                name.find("Mod") != std::string::npos) {
                                continue;
                            }
                        }
                        bind.key = static_cast<ImGuiKey>(key);
                        bind.ctrl = ImGui::GetIO().KeyCtrl;
                        bind.shift = ImGui::GetIO().KeyShift;
                        bind.alt = ImGui::GetIO().KeyAlt;
                        state.navigation.IsRebinding = false;
                        CommitUiSetting(state, "gain.wav");
                        break;
                    }
                } else {
                    const std::string label = FormatKeyBind(bind);
                    const bool isDuplicate = bindCounts[label] > 1;
                    if (isDuplicate) {
                        const ImVec4 duplicateText = lightKeybindPalette
                                                         ? ImVec4(0.72f, 0.08f, 0.10f, 1.0f)
                                                         : ImVec4(1.0f, 0.38f, 0.40f, 1.0f);
                        ImGui::PushStyleColor(ImGuiCol_Text, duplicateText);
                    }
                    if (ImGui::Button(label.c_str(), ImVec2(shortcutWidth, 0.0f))) {
                        PlayUISound("click.wav", state);
                        state.navigation.IsRebinding = true;
                        state.navigation.ActionBeingRebound = action;
                    }
                    if (isDuplicate)
                        ImGui::PopStyleColor();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();

        ImGui::Separator();
        ImGui::Spacing();
        const ImVec2 footerPadding(12.0f, 7.0f);
        const float footerGap = ImGui::GetStyle().ItemSpacing.x;
        const float resetWidth = ImGui::CalcTextSize("Reset defaults").x + footerPadding.x * 2.0f;
        const float doneWidth = ImGui::CalcTextSize("Done").x + footerPadding.x * 2.0f;
        const float footerButtonsWidth = resetWidth + footerGap + doneWidth;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max(0.0f,
                                      ImGui::GetContentRegionAvail().x - footerButtonsWidth));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, footerPadding);
        if (ImGui::Button("Reset defaults")) {
            state.navigation.IsRebinding = false;
            InitializeDefaultKeybinds(state.config);
            CommitUiSetting(state, "gain.wav");
        }
        ImGui::SameLine();
        if (ImGui::Button("Done")) {
            state.navigation.IsRebinding = false;
            PlayUISound("click.wav", state);
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(12);
    ImGui::PopStyleVar(6);
}

static void RenderObjectFocusSetting(AppState& state) {
    if (NeutralCheckbox("Object focus", state.config.objectFocus, state))
        CommitUiSetting(state, state.config.objectFocus ? "on.wav" : "off.wav");
}

static void RenderMinimalEffectsSetting(AppState& state) {
    if (!NeutralCheckbox("Minimal effects", state.config.zeroGraphics, state))
        return;
    PlayUISound(state.config.zeroGraphics ? "on.wav" : "off.wav", state);
    EnforceZeroGraphicsMode(state, true);
    ApplyTheme(state);
    squarestar::config::RequestConfigSave();
}

static void RenderSoundEffectsSetting(AppState& state) {
    if (!NeutralCheckbox("Sound effects", state.config.soundEnabled, state))
        return;
    if (state.config.soundEnabled) {
        PlayUISound("on.wav", state);
    } else {
        StopAllAppAudio();
        PlayAppSoundRuntime("off.wav");
    }
    squarestar::config::RequestConfigSave();
}

static void RenderUiAnimationsSetting(AppState& state) {
    if (state.config.zeroGraphics)
        ImGui::BeginDisabled();
    if (NeutralCheckbox("UI animations", state.config.animEnabled, state)) {
        PlayUISound(state.config.animEnabled ? "on.wav" : "off.wav", state);
        if (!state.UiAnimationsEnabled())
            SnapAllUiAnimations(state);
        squarestar::config::RequestConfigSave();
    }
    if (state.config.zeroGraphics)
        ImGui::EndDisabled();
}

static void RenderKeybindRemindersSetting(AppState& state) {
    if (!NeutralCheckbox("Keybind reminders", state.config.keybindReminders, state))
        return;
    PlayUISound(state.config.keybindReminders ? "on.wav" : "off.wav", state);
    if (!state.config.keybindReminders) {
        state.render.notifications.keybindHint.ClearActive();
    }
    squarestar::config::RequestConfigSave();
}

using BehaviorSettingRenderer = void (*)(AppState&);
static constexpr std::array<BehaviorSettingRenderer, 5> kBehaviorSettingRenderers = {
    RenderObjectFocusSetting,
    RenderMinimalEffectsSetting,
    RenderSoundEffectsSetting,
    RenderUiAnimationsSetting,
    RenderKeybindRemindersSetting};

static void AlignNextSettingsControlRight(float width) {
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() +
        std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
}

static void RenderAppearanceSettingsCard(AppState& state, float bodyContentWidth) {
    const char* themes[] = {"Dark Mode", "Light Mode"};
    const char* fpsModes[] = {
        "48 FPS Cap", "VSync (60 FPS, Recommended)", "240 FPS Cap",
        "Eco 30 FPS (Low power)"};
    static_assert(IM_ARRAYSIZE(fpsModes) == GUI_FRAME_RATE_MODE_COUNT);
    BeginSettingsCard(state,
                      "AppearanceSettings",
                      "Appearance & behavior",
                      0.0f,
                      false,
                      bodyContentWidth);

    const bool primaryTwoColumns = ImGui::GetContentRegionAvail().x >= 560.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                        ImVec2(primaryTwoColumns ? 6.0f : 0.0f, 2.0f));
    if (ImGui::BeginTable("AppearancePrimaryGrid",
                          primaryTwoColumns ? 2 : 1,
                          ImGuiTableFlags_SizingStretchSame |
                              ImGuiTableFlags_NoSavedSettings)) {
        auto DrawThemeControl = [&] {
            ImGui::TextDisabled("APP THEME");
            ImGui::SetNextItemWidth(-1.0f);
            const bool themeChanged = UiCombo(state,
                                              "##GUITheme",
                                              &state.config.themeModeIndex,
                                              themes,
                                              IM_ARRAYSIZE(themes));
            if (themeChanged) {
                PlayUISound("transition.wav", state);
                SetThemePreset(state.config, state.config.themeModeIndex);
                state.requests.fontReloadRequested = true;
                RequestGuiRedraw();
                squarestar::config::RequestConfigSave();
            }
        };
        auto DrawFrameRateControl = [&] {
            ImGui::TextDisabled("FRAME RATE");
            ImGui::SetNextItemWidth(-1.0f);
            const bool fpsChanged = UiCombo(state,
                                            "##FPSLimit",
                                            &state.config.fpsMode,
                                            fpsModes,
                                            IM_ARRAYSIZE(fpsModes));
            if (fpsChanged)
                CommitUiSetting(state, "transition.wav");
        };

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawThemeControl();
        if (primaryTwoColumns) {
            ImGui::TableSetColumnIndex(1);
            DrawFrameRateControl();
        } else {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawFrameRateControl();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGui::TextDisabled("OPTIONS");
    const float optionWidth = ImGui::GetContentRegionAvail().x;

    // Five equal columns only work when the longest toggle has enough room
    // for its 48 px switch, 9 px label gap, full label, and some breathing
    // room.  Using the rendered label width also keeps this safe if the
    // UI/font scale changes.
    const float minBehaviorCellWidth =
        48.0f + 9.0f + ImGui::CalcTextSize("Keybind reminders").x + 24.0f;
    const int behaviorColumns =
        optionWidth >= minBehaviorCellWidth * 5.0f ? 5 :
        optionWidth >= 720.0f ? 3 :
        optionWidth >= 390.0f ? 2 : 1;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 2.0f));
    if (ImGui::BeginTable("BehaviorToggleGrid",
                          behaviorColumns,
                          ImGuiTableFlags_SizingStretchSame |
                              ImGuiTableFlags_NoSavedSettings)) {
        for (size_t i = 0; i < kBehaviorSettingRenderers.size(); ++i) {
            if (i % static_cast<size_t>(behaviorColumns) == 0)
                ImGui::TableNextRow(ImGuiTableRowFlags_None, kDialogButtonHeight);
            ImGui::TableSetColumnIndex(static_cast<int>(i % behaviorColumns));
            kBehaviorSettingRenderers[i](state);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0.0f, 1.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("SHORTCUTS");
    ImGui::SameLine();
    constexpr float shortcutButtonWidth = 160.0f;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                  ImGui::GetWindowContentRegionMax().x -
                                      shortcutButtonWidth));
    DrawKeybindSettings(state);
    EndSettingsCard();
}

static void RenderExportSettingsCard(AppState& state) {
    BeginSettingsCard(state, "ExportSettings", "Chart & exports");
    ImGui::TextDisabled("EXPORT LOCATION");
    constexpr float resetWidth = 96.0f;
    constexpr float controlGap = 8.0f;
    const float inputWidth =
        std::max(120.0f, ImGui::GetContentRegionAvail().x - resetWidth - controlGap);
    ImGui::SetNextItemWidth(inputWidth);
    if (ImGui::InputText("##DefaultExportFolder",
                         state.config.exportDirectory,
                         IM_ARRAYSIZE(state.config.exportDirectory)))
        squarestar::config::RequestConfigSave();
    ImGui::SameLine(0.0f, controlGap);
    if (ImGui::Button("Reset", ImVec2(resetWidth, kControlHeight))) {
        std::snprintf(state.config.exportDirectory,
                      sizeof(state.config.exportDirectory),
                      "%s",
                      "default");
        squarestar::config::RequestConfigSave();
    }
    EndSettingsCard();
}

static void RenderStorageCleanupSettingsCard(AppState& state,
                                             bool& openSavedDataModal) {
    BeginSettingsCard(state, "StorageCleanupSettings", "Storage & cleanup");

    // Match the Export card's vertical layout exactly: one label line, the
    // normal ItemSpacing gap, then one fixed-height control line. A table
    // adds row/cell padding to the child content height, which made this
    // card extend below its neighbor even though the controls looked aligned.
    constexpr float storageGap = 8.0f;
    const float storageStartX = ImGui::GetCursorPosX();
    const float storageWidth = ImGui::GetContentRegionAvail().x;
    const float storageColumnWidth =
        std::max(1.0f, (storageWidth - storageGap) * 0.5f);
    const float storageSecondColumnX =
        storageStartX + storageColumnWidth + storageGap;

    ImGui::TextDisabled("CACHE");
    ImGui::SameLine();
    ImGui::SetCursorPosX(storageSecondColumnX);
    ImGui::TextDisabled("APPLICATION DATA");

    if (ImGui::Button("Clear cached data",
                      ImVec2(storageColumnWidth, kControlHeight))) {
        const TemporaryDataCleanupResult result = ClearSquareStarTemporaryData();
        std::string status =
            "Removed " + std::to_string(result.filesRemoved) + " temporary file" +
            (result.filesRemoved == 1 ? "" : "s");
        if (result.filesInUse > 0)
            status += "; " + std::to_string(result.filesInUse) + " still in use";
        status += ".";
        PublishUserFeedback(
            state,
            result.filesInUse == 0 ? UserFeedbackType::Success
                                   : UserFeedbackType::Warning,
            result.filesInUse == 0 ? "Cleanup complete" : "Cleanup incomplete",
            std::move(status));
    }

    ImGui::SameLine(0.0f, storageGap);
    if (ImGui::Button("Reset SquareStar",
                      ImVec2(storageColumnWidth, kControlHeight))) {
        PlayUISound("transition.wav", state);
        openSavedDataModal = true;
    }
    EndSettingsCard();
}

static void RenderPrivacySettingsCard(AppState& state) {
    BeginSettingsCard(state, "PrivacySettings", "Privacy");
    ImGui::TextDisabled("LOCAL SEARCH HISTORY");
    ImGui::AlignTextToFramePadding();
    if (NeutralCheckbox("Save search history", state.config.saveSearchHistory, state)) {
        if (!state.config.saveSearchHistory) {
            state.config.searchHistory.clear();
            state.config.searchHistoryNames.clear();
        }
        PlayUISound(state.config.saveSearchHistory ? "on.wav" : "off.wav", state);
        squarestar::config::RequestConfigSave();
    }

    ImGui::SameLine();
    constexpr float clearHistoryWidth = 132.0f;
    AlignNextSettingsControlRight(clearHistoryWidth);
    const bool hasSearchHistory = !state.config.searchHistory.empty() ||
                                  !state.config.searchHistoryNames.empty();
    if (!hasSearchHistory)
        ImGui::BeginDisabled();
    if (ImGui::Button("Clear history", ImVec2(clearHistoryWidth, kControlHeight))) {
        state.config.searchHistory.clear();
        state.config.searchHistoryNames.clear();
        squarestar::config::RequestConfigSave();
        PlayUISound("click.wav", state);
    }
    if (!hasSearchHistory)
        ImGui::EndDisabled();
    EndSettingsCard();
}

static void RenderDataSourcesSettingsCard(AppState& state) {
    BeginSettingsCard(state, "SettingsApi", "Data sources");
    ImGui::TextDisabled("FINNHUB API KEY");
    auto& apiKeyBuffer = state.settings.apiKeyBuffer;
    bool& apiKeyInputInvalid = state.settings.apiKeyInputInvalid;
    auto& validationFuture = state.settings.validationFuture;
    static constexpr char savedKeyMask[] = "********";
    const bool validationPending = validationFuture.valid();
    const bool hasSavedKey = HasFinnhubApiKey();
    if (hasSavedKey && !apiKeyInputInvalid && apiKeyBuffer[0] == '\0')
        std::copy(std::begin(savedKeyMask), std::end(savedKeyMask), apiKeyBuffer);
    const bool lightTheme = IsLightGuiTheme(state.config.themeModeIndex);
    const ImVec4 invalidTextColor =
        lightTheme ? ImVec4(0.58f, 0.08f, 0.10f, 1.0f)
                   : ImVec4(1.00f, 0.46f, 0.49f, 1.0f);
    const char* hint = hasSavedKey
                           ? "Saved - type to replace or clear"
                           : "Enter Finnhub API key - press Enter to validate";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          apiKeyInputInvalid
                              ? invalidTextColor
                              : ThemeVec(state.config.theme.text));
    if (validationPending)
        ImGui::BeginDisabled();
    const bool submitted = ImGui::InputTextWithHint(
        "##FinnhubApiKey",
        validationPending ? "Validating Finnhub API key..." : hint,
        apiKeyBuffer,
        IM_ARRAYSIZE(apiKeyBuffer),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll);
    const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
    const bool inputEdited = ImGui::IsItemEdited();
    if (validationPending)
        ImGui::EndDisabled();
    const bool showingSavedMask =
        hasSavedKey && !apiKeyInputInvalid &&
        std::string_view(apiKeyBuffer) == savedKeyMask;
    const bool clearRequested =
        hasSavedKey && ImGui::IsItemActive() && ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false);
    const ApiKeyInputAction inputAction = validationPending
                                              ? ApiKeyInputAction::None
                                              : ResolveApiKeyInputAction(
                                                    hasSavedKey,
                                                    clearRequested,
                                                    inputEdited,
                                                    submitted || deactivatedAfterEdit,
                                                    showingSavedMask,
                                                    apiKeyBuffer);
    ImGui::PopStyleColor();
    const auto resetApiKeyBuffer = [&] {
        std::fill(std::begin(apiKeyBuffer), std::end(apiKeyBuffer), '\0');
        if (HasFinnhubApiKey())
            std::copy(std::begin(savedKeyMask), std::end(savedKeyMask), apiKeyBuffer);
    };

    if (validationFuture.valid() &&
        validationFuture.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
        const ApiKeyValidation validation = validationFuture.get();
        if (validation == ApiKeyValidation::Valid) {
            apiKeyInputInvalid = false;
            std::string candidate(apiKeyBuffer);
            const bool persisted = PersistFinnhubApiKeyChange(state, candidate);
            SecureClear(candidate);
            if (persisted) {
                PublishUserFeedback(state, FinnhubKeySavedFeedback());
            } else {
                PublishUserFeedback(
                    state, FinnhubKeyPersistenceFailureFeedback());
            }
            resetApiKeyBuffer();
        } else {
            apiKeyInputInvalid = true;
            PublishUserFeedback(state, FinnhubValidationFailureFeedback(validation));
        }
    }

    if (inputAction == ApiKeyInputAction::Clear) {
        const bool cleared = PersistFinnhubApiKeyChange(state, "");
        resetApiKeyBuffer();
        apiKeyInputInvalid = false;
        PublishUserFeedback(
            state,
            cleared ? FinnhubKeyClearedFeedback()
                    : FinnhubKeyPersistenceFailureFeedback());
    } else if (inputAction == ApiKeyInputAction::Validate) {
        if (apiKeyBuffer[0] == '\0') {
            apiKeyInputInvalid = true;
            PublishUserFeedback(
                state, FinnhubValidationFailureFeedback(ApiKeyValidation::Empty));
        } else {
            apiKeyInputInvalid = false;
            validationFuture =
                QueueFinnhubApiKeyValidation(std::string(apiKeyBuffer));
        }
    }
    EndSettingsCard();
}

void RenderGeneralSettingsPage(AppState& state,
                                      float bodyContentWidth,
                                      bool& openSavedDataModal) {
    const bool useTwoColumns = bodyContentWidth >= 760.0f;

    const float pageContentStartX = ImGui::GetCursorPosX();
    RenderAppearanceSettingsCard(state, bodyContentWidth);
    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    // BeginChild can restore the parent's default X after the full-width card.
    // Re-anchor the lower grid explicitly so all Settings cards share the
    // exact same left/right bounds.
    ImGui::SetCursorPosX(pageContentStartX);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                        ImVec2(useTwoColumns ? 8.0f : 0.0f, 4.0f));
    if (ImGui::BeginTable("SettingsGrid",
                          useTwoColumns ? 2 : 1,
                          ImGuiTableFlags_SizingStretchSame |
                              ImGuiTableFlags_NoSavedSettings,
                          ImVec2(bodyContentWidth, 0.0f))) {
        if (useTwoColumns) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            RenderDataSourcesSettingsCard(state);
            ImGui::TableSetColumnIndex(1);
            RenderPrivacySettingsCard(state);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            RenderExportSettingsCard(state);
            ImGui::TableSetColumnIndex(1);
            RenderStorageCleanupSettingsCard(state, openSavedDataModal);
        } else {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            RenderDataSourcesSettingsCard(state);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            RenderPrivacySettingsCard(state);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            RenderExportSettingsCard(state);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            RenderStorageCleanupSettingsCard(state, openSavedDataModal);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}


} // namespace squarestar::shell
