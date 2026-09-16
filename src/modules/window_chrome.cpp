#include "modules/window_chrome.hpp"

#include "application/app_limits.hpp"
#include "application/app_state.hpp"
#include "application/main_loop_signal.hpp"
#include "application/runtime_state.hpp"
#include "modules/core.hpp"
#include "modules/platform.hpp"
#include "services/config_save_queue.hpp"

#include <algorithm>
#include <cmath>

namespace squarestar::shell {

using squarestar::application::AppState;
using squarestar::application::ApplicationRuntime;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::UiModeRequest;

void ToggleApplicationFullscreen(GLFWwindow* window, AppState& state) {
    if (!window)
        return;
    if (!state.navigation.isFullscreen) {
        glfwGetWindowPos(window, &state.config.windowedX, &state.config.windowedY);
        glfwGetWindowSize(window, &state.config.windowedWidth, &state.config.windowedHeight);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!monitor || !mode)
            return;
        ApplicationRuntime().SetGuiFullscreenSizeOverride(true);
        glfwSetWindowMonitor(
            window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        state.navigation.isFullscreen = true;
    } else {
        glfwSetWindowMonitor(window,
                             nullptr,
                             state.config.windowedX,
                             state.config.windowedY,
                             std::max(640, state.config.windowedWidth),
                             std::max(480, state.config.windowedHeight),
                             GLFW_DONT_CARE);
        state.navigation.isFullscreen = false;
        ApplicationRuntime().SetGuiFullscreenSizeOverride(false);
        ApplyRoundedWindowCorners(glfwGetWin32Window(window));
    }
}

void RenderCustomTitleBar(GLFWwindow* window, AppState& state) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x, APP_TITLE_BAR_HEIGHT));
    ImGuiWindowFlags titleFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    const ImVec4 background = ThemeVec(state.config.theme.exitBg);
    const ImVec4 text = ThemeVec(state.config.theme.exitText);
    const ImVec4 hover = ThemeVec(state.config.theme.exitHover);
    const ImVec4 active = ThemeVec(state.config.theme.exitActive);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, background);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::Begin("##CustomTitleBar", nullptr, titleFlags);

    constexpr float buttonWidth = APP_TITLE_BAR_BUTTON_WIDTH;
    const bool liteGui = state.navigation.liteGuiActive;
    const float controlCount = static_cast<float>(
        liteGui ? LITE_TITLE_BAR_CONTROL_COUNT : APP_TITLE_BAR_CONTROL_COUNT);
    const float dragAreaWidth = ImGui::GetWindowWidth() - buttonWidth * controlCount;


    ImGui::Dummy(ImVec2(dragAreaWidth, APP_TITLE_BAR_HEIGHT));
    ImGui::PushFont(state.render.fontNormal);
    const float titleY =
        std::floor((APP_TITLE_BAR_HEIGHT - ImGui::GetFontSize()) * 0.5f);
    ImGui::SetCursorPos(ImVec2(10.0f, std::max(0.0f, titleY)));
    ImGui::TextUnformatted(liteGui ? "SquareStar Lite" : "SquareStar");
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(
        ImGui::GetWindowWidth() - buttonWidth * controlCount, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 iconColor = ImGui::ColorConvertFloat4ToU32(text);
    if (liteGui) {
        const bool returnPressed =
            ImGui::Button("##LiteReturnToGui", ImVec2(buttonWidth, APP_TITLE_BAR_HEIGHT));
        const bool returnHovered = ImGui::IsItemHovered();
        const ImVec2 returnCenter(
            ImGui::GetItemRectMin().x + buttonWidth * 0.5f,
            ImGui::GetItemRectMin().y + APP_TITLE_BAR_HEIGHT * 0.5f);
        draw->AddLine(ImVec2(returnCenter.x - 7.0f, returnCenter.y),
                      ImVec2(returnCenter.x + 7.0f, returnCenter.y),
                      iconColor,
                      1.5f);
        draw->AddLine(ImVec2(returnCenter.x - 7.0f, returnCenter.y),
                      ImVec2(returnCenter.x - 1.0f, returnCenter.y - 6.0f),
                      iconColor,
                      1.5f);
        draw->AddLine(ImVec2(returnCenter.x - 7.0f, returnCenter.y),
                      ImVec2(returnCenter.x - 1.0f, returnCenter.y + 6.0f),
                      iconColor,
                      1.5f);
        draw->AddLine(ImVec2(returnCenter.x + 7.0f, returnCenter.y - 7.0f),
                      ImVec2(returnCenter.x + 7.0f, returnCenter.y + 7.0f),
                      iconColor,
                      1.25f);
        if (returnHovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        if (returnPressed) {
            ApplicationRuntime().RequestUiMode(UiModeRequest::Gui);
            PlayUISound("transition.wav", state);
            RequestGuiRedraw();
        }
        ImGui::SameLine();
    }
    if (!liteGui) {
        if (ImGui::Button("##ToggleStockTabs",
                          ImVec2(buttonWidth, APP_TITLE_BAR_HEIGHT))) {
            state.config.stockTabBarHidden = !state.config.stockTabBarHidden;
            squarestar::config::RequestConfigSave();
            PlayUISound(state.config.stockTabBarHidden ? "off.wav" : "on.wav", state);
        }
        const ImVec2 center(ImGui::GetItemRectMin().x + buttonWidth * 0.5f,
                            ImGui::GetItemRectMin().y + APP_TITLE_BAR_HEIGHT * 0.5f);
        const float direction = state.config.stockTabBarHidden ? 1.0f : -1.0f;
        draw->AddLine(ImVec2(center.x - 5.0f, center.y - 2.0f * direction),
                      ImVec2(center.x, center.y + 3.0f * direction),
                      iconColor,
                      1.5f);
        draw->AddLine(ImVec2(center.x, center.y + 3.0f * direction),
                      ImVec2(center.x + 5.0f, center.y - 2.0f * direction),
                      iconColor,
                      1.5f);
        ImGui::SameLine();
    }

    if (ImGui::Button("##Min", ImVec2(buttonWidth, APP_TITLE_BAR_HEIGHT)))
        glfwIconifyWindow(window);
    const ImVec2 minimizeCenter(
        ImGui::GetItemRectMin().x + buttonWidth * 0.5f,
        ImGui::GetItemRectMin().y + APP_TITLE_BAR_HEIGHT * 0.5f);
    draw->AddLine(ImVec2(minimizeCenter.x - 5.0f, minimizeCenter.y),
                  ImVec2(minimizeCenter.x + 5.0f, minimizeCenter.y),
                  iconColor,
                  1.0f);

    ImGui::SameLine();
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered, ThemeVec(state.config.theme.dangerHover));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive, ThemeVec(state.config.theme.dangerActive));
    if (ImGui::Button("##Close", ImVec2(buttonWidth, APP_TITLE_BAR_HEIGHT))) {
        state.navigation.showExitModal = true;
        RequestGuiRedraw();
    }
    const ImVec2 closeCenter(ImGui::GetItemRectMin().x + buttonWidth * 0.5f,
                             ImGui::GetItemRectMin().y + APP_TITLE_BAR_HEIGHT * 0.5f);
    const ImU32 closeColor =
        ImGui::IsItemHovered()
            ? ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.dangerText))
            : iconColor;
    draw->AddLine(ImVec2(closeCenter.x - 4.0f, closeCenter.y - 4.0f),
                  ImVec2(closeCenter.x + 4.0f, closeCenter.y + 4.0f),
                  closeColor,
                  1.0f);
    draw->AddLine(ImVec2(closeCenter.x + 4.0f, closeCenter.y - 4.0f),
                  ImVec2(closeCenter.x - 4.0f, closeCenter.y + 4.0f),
                  closeColor,
                  1.0f);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

}
