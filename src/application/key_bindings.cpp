#include "key_bindings.hpp"

#include "imgui.h"

namespace squarestar::application {

const char* GetActionName(TerminalAction action) noexcept {
    switch (action) {
    case TerminalAction::CloseTab:
        return "Close Tab";
    case TerminalAction::RefreshData:
        return "Refresh Data";
    case TerminalAction::ToggleSidebar:
        return "Toggle Sidebar";
    case TerminalAction::ToggleFullscreen:
        return "Toggle Fullscreen";
    case TerminalAction::TogglePureMonitorMode:
        return "Toggle Stock Monitor";
    case TerminalAction::ToggleClock:
        return "Toggle Chart Time Axis";
    case TerminalAction::TimeRangePrev:
        return "Previous Time Range";
    case TerminalAction::TimeRangeNext:
        return "Next Time Range";
    case TerminalAction::UpperTabPrev:
        return "Previous Chart / News / Metrics / VS Tab";
    case TerminalAction::UpperTabNext:
        return "Next Chart / News / Metrics / VS Tab";
    case TerminalAction::StockTabPrev:
        return "Previous Stock Tab";
    case TerminalAction::StockTabNext:
        return "Next Stock Tab";
    case TerminalAction::OpenHome:
        return "Open Home";
    case TerminalAction::OpenOverview:
        return "Open Overview";
    case TerminalAction::OpenTerminal:
        return "Open Terminal View";
    case TerminalAction::FocusSearch:
        return "Focus Search Bar";
    case TerminalAction::OpenComparison:
        return "Open Compare Mode";
    case TerminalAction::LiteStockTabUp:
        return "LiteGUI Stock Tab Up";
    case TerminalAction::LiteStockTabDown:
        return "LiteGUI Stock Tab Down";
    case TerminalAction::ToggleGuiMode:
        return "Switch Full GUI / LiteGUI";
    case TerminalAction::ScreenerPrev:
        return "Previous Screener";
    case TerminalAction::ScreenerNext:
        return "Next Screener";
    case TerminalAction::ScreenerPagePrev:
        return "Screener Page Back";
    case TerminalAction::ScreenerPageNext:
        return "Screener Page Forward";
    default:
        return "Unknown";
    }
}

std::string FormatKeyBind(const KeyBind& binding) {
    if (binding.key == ImGuiKey_None)
        return "Unbound";
    std::string result;
    if (binding.ctrl)
        result += "Ctrl + ";
    if (binding.shift)
        result += "Shift + ";
    if (binding.alt)
        result += "Alt + ";
    result += ImGui::GetKeyName(binding.key);
    return result;
}

}
