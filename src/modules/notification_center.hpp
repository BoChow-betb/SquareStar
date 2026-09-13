#pragma once

#include <functional>
#include <string>

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

void RenderNotificationCenterMenu(
    squarestar::application::AppState& state,
    const char* idSuffix,
    float buttonSize,
    float buttonSpacing,
    int visibleCardLimit,
    const std::function<void(const std::string&)>& onExecute);

} // namespace squarestar::shell
