#pragma once

#include <string>

#include "imgui.h"

namespace squarestar::application {
struct AppState;
}
namespace squarestar::presentation {
class NotificationBlockStack;
}

namespace squarestar::shell {

void RenderHomePage(squarestar::application::AppState& state);
void RenderOverviewFirstPage(squarestar::application::AppState& state);
void RenderOverviewFirstPage(squarestar::application::AppState& state,
                             bool allowDataRequests);
bool OpenNotificationStock(squarestar::application::AppState& state,
                           const std::string& ticker);
void RenderPriceAlertPopups(squarestar::application::AppState& state,
                           squarestar::presentation::NotificationBlockStack& stack);

} // namespace squarestar::shell
