#pragma once

#include <functional>
#include <string>

namespace squarestar::application {
struct AppState;
struct SearchState;
}

namespace squarestar::shell {

struct SearchBarOptions {
    bool fixedDropdownHeight = false;
    bool containDropdownInParent = false;
    bool detachedDropdown = false;
    bool showNotificationCenter = true;
    int notificationCenterVisibleCardLimit = 2;
};

void RenderIntegratedSearchBar(
    squarestar::application::AppState& state,
    squarestar::application::SearchState& searchState,
    const char* idSuffix,
    float customWidth,
    const std::function<void(const std::string&)>& onExecute,
    SearchBarOptions options = {});

}
