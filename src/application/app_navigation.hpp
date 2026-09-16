#pragma once

#include <string>
#include <vector>

#include "application/app_limits.hpp"
#include "application/navigation_state.hpp"
#include "application/search_state.hpp"

namespace squarestar::application {

struct GuiWorkspaceSnapshot {
    std::vector<SavedGuiStockTab> stockTabs;
    std::string lastActiveTab;
    bool pureMonitorMode = false;
    SidebarTab activeSidebarTab = SidebarTab::Home;
    SidebarTab previousActiveSidebarTab = SidebarTab::Stock;
    int restoreX = 100;
    int restoreY = 100;
    int restoreWidth = kDefaultGuiWindowWidth;
    int restoreHeight = kDefaultGuiWindowHeight;
    bool restoreMaximized = false;
    bool wasFullscreen = false;
};

struct AppNavigation {
    SidebarTab activeSidebarTab = SidebarTab::Home;
    SidebarTab previousActiveSidebarTab = SidebarTab::Stock;
    bool isSidebarHovered = false;
    bool isFullscreen = false;
    bool pureMonitorMode = false;
    bool monitorModeButtonRequested = false;
    SearchState mainSearch;
    SearchState liteSearch;
    char watchlistFilterBuffer[16] = "";
    bool notificationCenterOpen = false;
    GuiWorkspaceSnapshot guiWorkspace;
    std::string lastActiveTab;
    bool liteGuiActive = false;
    bool liteMonitorMode = false;
    std::vector<int> liteWorldClocks;
    std::vector<int> liteHiddenWorldClocks;
    int liteGuiAppliedHeight = 0;
    int activeScreenerIndex = 0;
    int currentScreenerPage = 0;
    bool watchlistOpen = false;
    bool showInterfaceSavePrompt = false;
    int pendingInterfaceSwitchTarget = -1;
    int deferredInterfaceSwitchTarget = -1;
    bool interfaceSwitchDecisionReady = false;
    bool interfaceSwitchSaveCurrentData = true;
    bool showExitModal = false;
    bool IsRebinding = false;
    TerminalAction ActionBeingRebound = TerminalAction::CloseTab;
};

}
