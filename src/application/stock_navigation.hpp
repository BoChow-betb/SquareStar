#pragma once

#include <string>
#include <vector>

#include "application/navigation_state.hpp"
#include "application/search_state.hpp"

namespace squarestar::application {

struct StockNavigationState {
    char ticker[16] = "";
    SearchState headerSearch;


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

}
