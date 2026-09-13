#pragma once

#include <cstddef>
#include <string>

#include "application/app_limits.hpp"
#include "imgui.h"

namespace squarestar::application {

enum class SidebarTab : int {
    Stock = -1,
    Home = 0,
    Overview = 1,
    Settings = 2,
};

constexpr int SidebarTabIndex(SidebarTab tab) noexcept {
    return static_cast<int>(tab);
}

constexpr bool IsSidebarPage(SidebarTab tab) noexcept {
    return tab != SidebarTab::Stock;
}

constexpr SidebarTab SidebarTabFromPersistedValue(int value) noexcept {
    switch (value) {
    case SidebarTabIndex(SidebarTab::Overview):
        return SidebarTab::Overview;
    case SidebarTabIndex(SidebarTab::Settings):
        return SidebarTab::Settings;
    case SidebarTabIndex(SidebarTab::Home):
    default:
        return SidebarTab::Home;
    }
}

enum class TerminalAction {
    CloseTab,
    RefreshData,
    TogglePureMonitorMode,
    ToggleClock,
    TimeRangePrev,
    TimeRangeNext,
    UpperTabPrev,
    UpperTabNext,
    ScreenerPrev,
    ScreenerNext,
    ScreenerPagePrev,
    ScreenerPageNext,
    StockTabPrev,
    StockTabNext,
    OpenHome,
    OpenOverview,
    OpenTerminal,
    FocusSearch,
    ToggleSidebar,
    ToggleFullscreen,
    OpenComparison,
    LiteStockTabUp,
    LiteStockTabDown,
    ToggleGuiMode,
    Count,
};

struct KeyBind {
    ImGuiKey key = ImGuiKey_None;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

struct SavedGuiStockTab {
    std::string ticker;
    int selectedTimeRangeIndex = 0;
    int upperTabIndex = 0;
    bool monitorExcluded = false;
};

} // namespace squarestar::application
