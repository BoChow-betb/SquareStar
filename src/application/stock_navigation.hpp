#pragma once

#include <string>
#include <vector>

#include "application/navigation_state.hpp"
#include "application/search_state.hpp"

namespace squarestar::application {

struct StockNavigationState {
    char ticker[16] = "";
    SearchState headerSearch;
    // A newly opened stock can fail before it has ever published usable data.
    // Preserve the exact surface that launched it so the failure path can put
    // the user back where they were instead of guessing a replacement tab.
    std::string failureReturnTicker;
    SidebarTab failureReturnSidebarTab = SidebarTab::Stock;
    bool hasSearched = false;
    bool open = true;
    bool justOpened = false;
    bool monitorExcluded = false;
    bool refreshSurfaceVisible = false;
    int selectedTimeRangeIndex = 0;
    int displayedTimeRangeIndex = 0;
    int upperTabIndex = 0;
    int nextUpperTab = -1;
    int preMonitorUpperTabIndex = 0;
    std::vector<std::string> comparisonSymbols;
    bool comparisonPickerRequested = false;
};

} // namespace squarestar::application
