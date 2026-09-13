#include "modules/notification_view.hpp"
#include "modules/notification_view_internal.hpp"

#include "modules/core.hpp"
#include "modules/platform.hpp"
#include "modules/views.hpp"

#include "application/app_state.hpp"
#include "application/contextual_keybind_policy.hpp"
#include "application/key_bindings.hpp"
#include "application/main_loop_signal.hpp"
#include "application/monitor_stock_policy.hpp"
#include "application/notification_text.hpp"
#include "application/runtime_state.hpp"
#include "application/ui_animation.hpp"
#include "domain/market_calendar.hpp"
#include "platform/windows_path.hpp"
#include "presentation/notification_layout.hpp"
#include "presentation/notification_stack.hpp"
#include "services/http_client.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace squarestar::shell {

using squarestar::application::RequestGuiRedraw;
using squarestar::application::RequestGuiWakeAt;
using squarestar::application::UiRounding;
using squarestar::application::AppState;
using squarestar::application::CountMonitorStockTiles;
using squarestar::application::ShouldHoldMonitorModeHint;
using squarestar::application::ResolveContextualKeybindSurface;
using squarestar::application::HasContextualKeybindHint;
using squarestar::application::ContextualKeybindSurface;
using squarestar::application::FormatKeyBind;
using squarestar::application::StockContext;
using squarestar::application::TerminalAction;
using squarestar::application::IsLightGuiTheme;
using squarestar::presentation::NotificationBlockStack;
using squarestar::presentation::NotificationCardWidth;
using squarestar::presentation::ShouldStackNotificationRowValues;

static bool IsContextualKeybindSurfaceReady(AppState& state,
                                            ContextualKeybindSurface surface) {
    if (!HasContextualKeybindHint(surface))
        return false;
    if (state.render.navigationTransition < 0.999f)
        return false;
    if (surface == ContextualKeybindSurface::Overview) {
        const auto screener = state.marketData.LoadScreenerSnapshot();
        if (screener && screener->IsLoading())
            return false;
        return true;
    }
    if (surface != ContextualKeybindSurface::Stock)
        return true;

    const StockContext* visibleStock = nullptr;
    for (const auto& context : state.marketData.activeContexts) {
        if (!context || !context->navigation.open)
            continue;
        if (context->navigation.ticker == state.navigation.lastActiveTab ||
            context->navigation.refreshSurfaceVisible) {
            visibleStock = context.get();
            break;
        }
    }
    if (!visibleStock || visibleStock->requests.isLoading)
        return false;
    return !state.UiAnimationsEnabled() ||
           (visibleStock->render.openTransitionProgress >= 0.98f &&
            visibleStock->render.tabFadeAnim >= 0.98f);
}
void ReserveVisibleStockModeNotice(const AppState& state,
                                          NotificationBlockStack& stack) {
    const bool visible = std::any_of(
        state.marketData.activeContexts.begin(),
        state.marketData.activeContexts.end(),
        [](const auto& context) {
            return context && context->navigation.refreshSurfaceVisible &&
                   context->render.transientNoticeAnim > 0.001f;
        });
    if (visible)
        static_cast<void>(stack.ReserveSpace(132.0f));
}
void UpdateContextualKeybindHint(AppState& state) {
    auto& hint = state.render.notifications.keybindHint;
    const ContextualKeybindSurface surface = ResolveContextualKeybindSurface(
        {false,
         !state.marketData.activeContexts.empty(),
         state.navigation.pureMonitorMode,
         state.navigation.activeSidebarTab});
    if (!state.config.keybindReminders) {
        hint.surface = surface;
        hint.ClearActive();
        return;
    }
    if (state.StartupAnimationVisible() || IsCleanGuiCaptureFrame())
        return;

    const auto now = std::chrono::steady_clock::now();
    if (surface != hint.surface) {
        hint.surface = surface;
        hint.ClearActive();
        if (HasContextualKeybindHint(surface))
            hint.showAt = now + std::chrono::milliseconds(650);
        RequestGuiRedraw();
        return;
    }
    if (hint.showAt == std::chrono::steady_clock::time_point{})
        return;
    if (!IsContextualKeybindSurfaceReady(state, surface)) {
        // Require a short settled interval after the page's own load/reveal
        // completes so the reminder never races the content onto the screen.
        hint.showAt = now + std::chrono::milliseconds(250);
        RequestGuiWakeAt(hint.showAt);
        return;
    }
    if (now < hint.showAt) {
        RequestGuiWakeAt(hint.showAt);
        return;
    }

    hint.showAt = {};
    hint.context = surface;
    hint.presentation.until = now + std::chrono::seconds(6);
    hint.presentation.animation = state.UiAnimationsEnabled() ? 0.0f : 1.0f;
    RequestGuiRedraw();
}
void RenderContextualKeybindHint(AppState& state,
                                        ImGuiViewport* viewport,
                                        NotificationBlockStack& stack) {
    auto& hint = state.render.notifications.keybindHint;
    auto& presentation = hint.presentation;
    if (!viewport || IsCleanGuiCaptureFrame() || !HasContextualKeybindHint(hint.context))
        return;

    const auto now = std::chrono::steady_clock::now();
    const bool holding = presentation.Holding(now);
    const float target = holding ? 1.0f : 0.0f;
    if (holding)
        RequestGuiWakeAt(presentation.until);
    if (state.UiAnimationsEnabled()) {
        const float duration = target > presentation.animation ? 0.28f : 0.24f;
        const float step = UiFrameDelta() / duration;
        if (target > presentation.animation)
            presentation.animation = std::min(target, presentation.animation + step);
        else
            presentation.animation = std::max(target, presentation.animation - step);
    } else {
        presentation.animation = target;
    }
    presentation.animation = std::clamp(presentation.animation, 0.0f, 1.0f);
    if (!holding && presentation.animation <= 0.001f) {
        presentation.Clear();
        return;
    }
    if (presentation.animation <= 0.001f)
        return;

    const auto binding = [&](TerminalAction action) {
        const auto found = state.config.KeyBinds.find(action);
        return found == state.config.KeyBinds.end() ? std::string("Unbound")
                                             : FormatKeyBind(found->second);
    };
    const auto bindingPair = [&](TerminalAction first, TerminalAction second) {
        return binding(first) + "  /  " + binding(second);
    };
    const char* title = "Shortcuts";
    std::array<std::pair<const char*, std::string>, 6> rows{};
    size_t rowCount = 0;
    switch (hint.context) {
    case ContextualKeybindSurface::Home:
        title = "App shortcuts";
        rows[0] = {"Fullscreen", binding(TerminalAction::ToggleFullscreen)};
        rows[1] = {"Search", binding(TerminalAction::FocusSearch)};
        rows[2] = {"Overview", binding(TerminalAction::OpenOverview)};
        rows[3] = {"Sidebar", binding(TerminalAction::ToggleSidebar)};
        rowCount = 4;
        break;
    case ContextualKeybindSurface::Overview:
        title = "Overview shortcuts";
        rows[0] = {"Lists",
                   bindingPair(TerminalAction::ScreenerPrev,
                               TerminalAction::ScreenerNext)};
        rows[1] = {"Pages",
                   bindingPair(TerminalAction::ScreenerPagePrev,
                               TerminalAction::ScreenerPageNext)};
        rows[2] = {"Fullscreen", binding(TerminalAction::ToggleFullscreen)};
        rowCount = 3;
        break;
    case ContextualKeybindSurface::Stock:
        title = "Stock shortcuts";
        rows[0] = {"Refresh / close",
                   bindingPair(TerminalAction::RefreshData, TerminalAction::CloseTab)};
        rows[1] = {"Views",
                   bindingPair(TerminalAction::UpperTabPrev,
                               TerminalAction::UpperTabNext)};
        rows[2] = {"Ranges",
                   bindingPair(TerminalAction::TimeRangePrev,
                               TerminalAction::TimeRangeNext)};
        rows[3] = {"Time menu", binding(TerminalAction::ToggleClock)};
        rows[4] = {"Search", binding(TerminalAction::FocusSearch)};
        rowCount = 5;
        break;
    default:
        return;
    }

    const float width = NotificationCardWidth(viewport->WorkSize.x);
    ImFont* titleFont = state.render.fontData ? state.render.fontData : ImGui::GetFont();
    ImFont* bodyFont = state.render.fontNormal ? state.render.fontNormal : ImGui::GetFont();
    const float titleFontSize =
        state.render.fontData ? state.config.theme.fontData : ImGui::GetFontSize();
    const float bodyFontSize =
        state.render.fontNormal ? state.config.theme.fontBody : ImGui::GetFontSize();
    const float titleHeight = titleFontSize;
    std::array<bool, 6> stackRows{};
    std::array<float, 6> rowHeights{};
    float rowsHeight = 0.0f;
    for (size_t rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
        const float labelWidth =
            bodyFont->CalcTextSizeA(bodyFontSize,
                                    std::numeric_limits<float>::max(),
                                    0.0f,
                                    rows[rowIndex].first)
                .x;
        const float keyWidth =
            bodyFont->CalcTextSizeA(bodyFontSize,
                                    std::numeric_limits<float>::max(),
                                    0.0f,
                                    rows[rowIndex].second.c_str())
                .x;
        // Stack only the row whose shortcut exceeds the available width.
        stackRows[rowIndex] =
            ShouldStackNotificationRowValues(labelWidth, keyWidth, width, 18.0f);
        rowHeights[rowIndex] = stackRows[rowIndex] ? bodyFontSize * 2.0f + 6.0f
                                                   : bodyFontSize + 7.0f;
        rowsHeight += rowHeights[rowIndex];
    }
    const float height = 14.0f + titleHeight + 9.0f + rowsHeight + 12.0f;
    const float bottomOffset = stack.Reserve(height);
    const float smooth = presentation.animation * presentation.animation *
                         (3.0f - 2.0f * presentation.animation);
    const float restingX = viewport->WorkPos.x + viewport->WorkSize.x - width - 16.0f;
    ImGui::SetNextWindowPos(
        ImVec2(restingX + (1.0f - smooth) * 56.0f,
               viewport->WorkPos.y + viewport->WorkSize.y - height - bottomOffset),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    const bool light = IsLightGuiTheme(state.config.themeModeIndex);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiRounding(state, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, smooth);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          light ? ImVec4(0.975f, 0.975f, 0.98f, 0.995f)
                                : ImVec4(0.055f, 0.055f, 0.06f, 0.985f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          light ? ImVec4(0.55f, 0.55f, 0.58f, 1.0f)
                                : ImVec4(0.34f, 0.34f, 0.36f, 1.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav;
    ImGui::Begin("##ContextualKeybindHint", nullptr, flags);
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    if (ImGui::InvisibleButton("##DismissContextualKeybindHint", ImVec2(width, height))) {
        presentation.until = {};
        PlayUISound("click.wav", state);
        RequestGuiRedraw();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const ImVec2 cardMin = ImGui::GetWindowPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 titleColor = ImGui::ColorConvertFloat4ToU32(
        light ? ImVec4(0.08f, 0.08f, 0.09f, smooth)
              : ImVec4(0.96f, 0.96f, 0.97f, smooth));
    const ImU32 labelColor = ImGui::ColorConvertFloat4ToU32(
        light ? ImVec4(0.36f, 0.36f, 0.39f, smooth)
              : ImVec4(0.70f, 0.70f, 0.73f, smooth));
    draw->AddText(titleFont,
                  titleFontSize,
                  ImVec2(cardMin.x + 18.0f, cardMin.y + 14.0f),
                  titleColor,
                  title);
    float rowY = cardMin.y + 14.0f + titleHeight + 9.0f;
    for (size_t rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
        const auto& [label, keys] = rows[rowIndex];
        draw->AddText(bodyFont,
                      bodyFontSize,
                      ImVec2(cardMin.x + 18.0f, rowY),
                      labelColor,
                      label);
        const ImVec2 keySize = bodyFont->CalcTextSizeA(bodyFontSize,
                                                       std::numeric_limits<float>::max(),
                                                       0.0f,
                                                       keys.c_str());
        draw->AddText(bodyFont,
                      bodyFontSize,
                      ImVec2(cardMin.x + width - 18.0f - keySize.x,
                             rowY + (stackRows[rowIndex] ? bodyFontSize + 1.0f : 0.0f)),
                      titleColor,
                      keys.c_str());
        rowY += rowHeights[rowIndex];
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    if (state.UiAnimationsEnabled() && std::abs(presentation.animation - target) > 0.001f)
        RequestGuiRedraw();
}
void RenderMonitorModeHint(AppState& state,
                                  ImGuiViewport* viewport,
                                  NotificationBlockStack& stack) {
    if (!viewport)
        return;

    auto& hint = state.render.notifications.monitorModeHint;
    auto& presentation = hint.presentation;
    const size_t monitorTileCount =
        state.navigation.pureMonitorMode ? CountMonitorStockTiles(state.marketData) : 0;
    const auto now = std::chrono::steady_clock::now();
    const bool needsTabs = state.navigation.pureMonitorMode && monitorTileCount < 2;
    const bool holding = ShouldHoldMonitorModeHint(state.navigation.pureMonitorMode,
                                                   needsTabs,
                                                   state.config.monitorModeHintDisabled,
                                                   hint.dismissed,
                                                   presentation.Holding(now));
    const float target = holding ? 1.0f : 0.0f;
    if (holding)
        RequestGuiWakeAt(presentation.until);

    if (state.UiAnimationsEnabled()) {
        const float response = 1.0f - std::exp(-18.0f * UiFrameDelta());
        presentation.animation +=
            (target - presentation.animation) * std::clamp(response, 0.0f, 1.0f);
    } else {
        presentation.animation = target;
    }
    presentation.animation = std::clamp(presentation.animation, 0.0f, 1.0f);

    if (holding || presentation.animation > 0.001f) {
        std::string exitKey = "Unbound";
        if (const auto bind =
                state.config.KeyBinds.find(TerminalAction::TogglePureMonitorMode);
            bind != state.config.KeyBinds.end())
            exitKey = FormatKeyBind(bind->second);
        const std::string body = needsTabs
                                     ? "Open one more stock tab for a multi-tab view."
                                     : "Your open stock tabs are shown together.";
        const std::string closeHelp = "Press " + exitKey + " to exit.";
        const float hintWidth = NotificationCardWidth(viewport->WorkSize.x);
        ImFont* hintFont = state.render.fontData ? state.render.fontData : ImGui::GetFont();
        const float hintFontSize =
            state.render.fontData ? state.config.theme.fontData : ImGui::GetFontSize();
        const float contentWidth = std::max(
            40.0f,
            hintWidth - squarestar::presentation::kNotificationContentHorizontalPadding);
        const ImVec2 titleSize = hintFont->CalcTextSizeA(hintFontSize,
                                                        std::numeric_limits<float>::max(),
                                                        contentWidth,
                                                        "Monitor mode");
        const ImVec2 bodySize = hintFont->CalcTextSizeA(hintFontSize,
                                                       std::numeric_limits<float>::max(),
                                                       contentWidth,
                                                       body.c_str());
        const ImVec2 closeSize = hintFont->CalcTextSizeA(hintFontSize,
                                                        std::numeric_limits<float>::max(),
                                                        contentWidth,
                                                        closeHelp.c_str());
        const bool showDismiss = holding && !state.config.monitorModeHintDisabled;
        const float dismissHeight = showDismiss ? ImGui::GetFrameHeight() + 10.0f : 0.0f;
        const float hintHeight =
            14.0f + titleSize.y + 8.0f + bodySize.y + 5.0f + closeSize.y +
            dismissHeight + 14.0f;
        const float bottomOffset = stack.Reserve(hintHeight);
        const float smooth = presentation.animation * presentation.animation *
                             (3.0f - 2.0f * presentation.animation);
        const float restingX =
            viewport->WorkPos.x + viewport->WorkSize.x - hintWidth - 16.0f;
        ImGui::SetNextWindowPos(
            ImVec2(restingX + (1.0f - smooth) * 56.0f,
                   viewport->WorkPos.y + viewport->WorkSize.y - hintHeight - bottomOffset),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(hintWidth, hintHeight), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiRounding(state, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, smooth);
        const bool light = IsLightGuiTheme(state.config.themeModeIndex);
        ImGui::PushStyleColor(
            ImGuiCol_WindowBg,
            light ? ImVec4(0.975f, 0.975f, 0.98f, 0.995f)
                  : ImVec4(0.055f, 0.055f, 0.06f, 0.985f));
        ImGui::PushStyleColor(
            ImGuiCol_Border,
            light ? ImVec4(0.55f, 0.55f, 0.58f, 1.0f)
                  : ImVec4(0.34f, 0.34f, 0.36f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            light ? ImVec4(0.08f, 0.08f, 0.09f, 1.0f)
                  : ImVec4(0.96f, 0.96f, 0.97f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_TextDisabled,
            light ? ImVec4(0.36f, 0.36f, 0.39f, 1.0f)
                  : ImVec4(0.70f, 0.70f, 0.73f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            light ? ThemeVec(state.config.theme.buttonHover)
                  : ImVec4(0.24f, 0.24f, 0.26f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            light ? ThemeVec(state.config.theme.buttonActive)
                  : ImVec4(0.31f, 0.31f, 0.33f, 1.0f));
        ImGuiWindowFlags hintFlags = ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoFocusOnAppearing;
        if (!holding)
            hintFlags |= ImGuiWindowFlags_NoInputs;
        ImGui::Begin("##MonitorModeKeybindHint", nullptr, hintFlags);
        ImGui::PushFont(hintFont);
        ImGui::TextUnformatted("Monitor mode");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        ImGui::TextDisabled("%s", body.c_str());
        ImGui::TextDisabled("%s", closeHelp.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        bool dismissedPermanently = false;
        if (showDismiss && ImGui::Button("Don't remind me again")) {
            state.config.monitorModeHintDisabled = true;
            dismissedPermanently = true;
            CommitUiSetting(state, "click.wav");
        }
        const bool cardClicked =
            holding &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (dismissedPermanently || cardClicked) {
            hint.dismissed = true;
            presentation.until = {};
            if (!dismissedPermanently)
                PlayUISound("click.wav", state);
            RequestGuiRedraw();
        }
        ImGui::End();
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(4);
    }

    if (state.UiAnimationsEnabled() &&
        std::abs(presentation.animation - target) > 0.001f)
        RequestGuiRedraw();
}


} // namespace squarestar::shell
