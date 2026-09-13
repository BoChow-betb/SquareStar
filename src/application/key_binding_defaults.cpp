#include "application/key_bindings.hpp"

#include "application/lite_tab_policy.hpp"

#include "imgui.h"

namespace squarestar::application {

void InitializeDefaultKeybinds(AppConfig& config) {
    config.KeyBinds.clear();
    config.KeyBinds[TerminalAction::CloseTab] = {ImGuiKey_W, true, false, false};
    config.KeyBinds[TerminalAction::RefreshData] = {ImGuiKey_R, true, false, false};
    config.KeyBinds[TerminalAction::ToggleSidebar] = {ImGuiKey_B, true, false, false};
    config.KeyBinds[TerminalAction::ToggleFullscreen] = {ImGuiKey_F11, false, false, false};
    config.KeyBinds[TerminalAction::TogglePureMonitorMode] = {
        ImGuiKey_M, true, true, false};
    config.KeyBinds[TerminalAction::ToggleClock] = {ImGuiKey_T, false, true, false};
    config.KeyBinds[TerminalAction::TimeRangePrev] = {
        ImGuiKey_LeftBracket, false, false, false};
    config.KeyBinds[TerminalAction::TimeRangeNext] = {
        ImGuiKey_RightBracket, false, false, false};
    config.KeyBinds[TerminalAction::UpperTabPrev] = {
        ImGuiKey_PageUp, true, false, false};
    config.KeyBinds[TerminalAction::UpperTabNext] = {
        ImGuiKey_PageDown, true, false, false};
    config.KeyBinds[TerminalAction::StockTabPrev] = {
        ImGuiKey_Tab, true, true, false};
    config.KeyBinds[TerminalAction::StockTabNext] = {
        ImGuiKey_Tab, true, false, false};
    config.KeyBinds[TerminalAction::OpenHome] = {ImGuiKey_1, true, false, false};
    config.KeyBinds[TerminalAction::OpenOverview] = {ImGuiKey_2, true, false, false};
    config.KeyBinds[TerminalAction::OpenTerminal] = {ImGuiKey_3, true, false, false};
    config.KeyBinds[TerminalAction::FocusSearch] = {ImGuiKey_K, true, false, false};
    config.KeyBinds[TerminalAction::OpenComparison] = {
        ImGuiKey_C, true, true, false};
    config.KeyBinds[TerminalAction::ScreenerPrev] = {
        ImGuiKey_UpArrow, true, false, false};
    config.KeyBinds[TerminalAction::ScreenerNext] = {
        ImGuiKey_DownArrow, true, false, false};
    config.KeyBinds[TerminalAction::ScreenerPagePrev] = {
        ImGuiKey_PageUp, false, false, false};
    config.KeyBinds[TerminalAction::ScreenerPageNext] = {
        ImGuiKey_PageDown, false, false, false};
    config.KeyBinds[TerminalAction::LiteStockTabUp] = kDefaultLiteStockTabUpBinding;
    config.KeyBinds[TerminalAction::LiteStockTabDown] = kDefaultLiteStockTabDownBinding;
    config.KeyBinds[TerminalAction::ToggleGuiMode] = {ImGuiKey_G, true, true, false};
}

} // namespace squarestar::application
