#include "modules/gui_shell.hpp"
#include "modules/gui_shell_internal.hpp"
#include "modules/interface_modals.hpp"
#include "modules/core.hpp"
#include "modules/ui_focus.hpp"
#include "modules/platform.hpp"
#include "modules/market_data.hpp"
#include "modules/views.hpp"
#include "modules/settings_view.hpp"
#include "modules/terminal_stock_windows.hpp"
#include "modules/stock_surface_feedback.hpp"
#include "modules/window_chrome.hpp"

#include "services/config_save_queue.hpp"
#include "application/app_limits.hpp"
#include "application/contextual_keybind_policy.hpp"
#include "application/key_bindings.hpp"
#include "application/main_loop_signal.hpp"
#include "application/navigation_state.hpp"
#include "application/screener_controller.hpp"
#include "application/stock_request_state.hpp"
#include "application/stock_tab_policy.hpp"
#include "domain/market_symbol.hpp"
#include "domain/chart_ranges.hpp"

#include <vector>
namespace squarestar::shell {

using squarestar::application::UiRounding;
using squarestar::application::AppState;


void RenderStockTerminal(AppState& state, GLFWwindow* window) {
    EnforceZeroGraphicsMode(state);
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    BeginObjectFocusFrame();
    ImVec2 workPos = viewport->WorkPos;
    workPos.y += APP_TITLE_BAR_HEIGHT;
    ImVec2 workSize = viewport->WorkSize;
    workSize.y -= APP_TITLE_BAR_HEIGHT;
    if (state.render.appliedThemeModeIndex != state.config.themeModeIndex ||
        state.render.appliedZeroGraphics != state.ZeroGraphicsEnabled()) {
        ApplyTheme(state);
        state.render.appliedThemeModeIndex = state.config.themeModeIndex;
        state.render.appliedZeroGraphics = state.ZeroGraphicsEnabled();
    }
    ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();
    const ImU32 appBg = ImGui::ColorConvertFloat4ToU32(
        ThemeVec(state.navigation.pureMonitorMode ? state.config.theme.monitorBg : state.config.theme.bg));
    bgDrawList->AddRectFilled(
        workPos, ImVec2(workPos.x + workSize.x, workPos.y + workSize.y), appBg);
    PruneClosedTerminalStocks(state);
    const size_t openStockTabs = squarestar::application::CountOpenStockTabs(state);
    SynchronizeTerminalNavigationState(state, openStockTabs);

    const ImVec4 dynamicDimColor = ThemeVec(state.config.theme.dimOverlay);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dynamicDimColor);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiRounding(state, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 24.0f));
    RenderClosingModal(state, window);
    RenderInterfaceSavePrompt(state);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::SetNextWindowPos(workPos);
    ImGui::SetNextWindowSize(workSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground |
                                    ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("SquareStar Background", nullptr, window_flags);
    ImGui::PopStyleVar(3);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const float currentSidebarWidth = RenderTerminalSidebar(state, openStockTabs);
    RenderTerminalMainContent(state, currentSidebarWidth, appBg);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);

    HandleTerminalKeyboardShortcuts(state, window);
    RenderMonitorStockWindows(state, workPos, workSize);
    RenderMonitorExitButton(state, viewport, workPos, workSize);
    RenderMonitorStockPicker(state, viewport, workPos, workSize);
    RenderObjectFocusOverlay(state,
                             viewport->WorkPos,
                             ImVec2(viewport->WorkPos.x + viewport->WorkSize.x,
                                    viewport->WorkPos.y + viewport->WorkSize.y));
    ImGui::End();

    RenderStartupOverlay(state, viewport);
}


} // namespace squarestar::shell
