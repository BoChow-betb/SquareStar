#pragma once

#include <future>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

namespace squarestar::application {

struct SearchState {
    char inputBuffer[128] = "";
    std::string lastQuery;
    std::string inFlightQuery;
    std::string pendingQuery;
    std::vector<std::pair<std::string, std::string>> results;
    bool isDropdownOpen = false;
    bool isHoveringDropdown = false;
    int selectedIndex = -1;
    std::future<std::vector<std::pair<std::string, std::string>>> searchFuture;
    float debounceTimer = 0.0f;
    bool isSearching = false;
    float dropdownAnim = 0.0f;
    bool dropdownShowsRecommendations = false;
    bool isFocused = false;
    bool focusRequested = false;
    ImVec2 dropdownMin{};
    ImVec2 dropdownMax{};
    bool dropdownBoundsValid = false;

    void ResetUi() noexcept {
        inputBuffer[0] = '\0';
        lastQuery.clear();
        pendingQuery.clear();
        if (!isSearching)
            inFlightQuery.clear();
        results.clear();
        selectedIndex = -1;
        isDropdownOpen = false;
        isHoveringDropdown = false;
        dropdownBoundsValid = false;
        dropdownShowsRecommendations = false;
        dropdownAnim = 0.0f;
        debounceTimer = 0.0f;
        isFocused = false;
        focusRequested = false;
    }
};

} // namespace squarestar::application
