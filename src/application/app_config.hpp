#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "application/app_limits.hpp"
#include "application/frame_rate.hpp"
#include "application/navigation_state.hpp"
#include "application/theme.hpp"

namespace squarestar::application {

struct AppConfig {
    bool showStartupAnim = true;
    bool soundEnabled = true;
    bool animEnabled = true;
    bool zeroGraphics = false;
    int graphTimeZone = 1;
    std::string displayCurrency = "USD";
    std::vector<int> activeWorldClocks;
    char exportDirectory[260] = "default";
    bool objectFocus = false;
    bool sidebarHidden = false;
    int windowedX = 100;
    int windowedY = 100;
    int windowedWidth = kDefaultGuiWindowWidth;
    int windowedHeight = kDefaultGuiWindowHeight;
    bool stockTabBarHidden = false;
    int themeModeIndex = 0;
    int lastOpenMode = 0;
    int fpsMode = static_cast<int>(GuiFrameRateMode::VSync);
    int chartXAxisMode = 0;
    std::vector<std::string> searchHistory;
    std::vector<std::string> watchlist;
    std::map<std::string, std::string> searchHistoryNames;
    Theme theme;
    bool liteGuiHintShown = false;
    bool monitorModeHintDisabled = false;
    bool keybindReminders = true;
    bool saveLastUsedUiData = true;
    bool saveSearchHistory = true;
    bool askBeforeInterfaceSwitch = true;
    bool marketMoveNotifications = true;
    double marketMoveThresholdPct = 3.0;
    int marketMoveCooldownMinutes = 15;
    bool marketMove52WeekEvents = true;
    bool marketMoveStateChanges = true;
    bool marketMoveBatching = true;
    int plotLineType = 1;
    int crosshairMode = 1;
    std::map<TerminalAction, KeyBind> KeyBinds;
};

} // namespace squarestar::application
