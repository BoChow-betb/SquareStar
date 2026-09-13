#pragma once

#include <string>
#include <vector>

#include "application/notification_text.hpp"

struct ImGuiViewport;

namespace squarestar::application {
struct AppState;
}
namespace squarestar::presentation {
class NotificationBlockStack;
}

namespace squarestar::shell {

enum class NotificationCardRenderResult {
    Deferred,
    Visible,
    Dismissed,
};

struct NotificationCardSpec {
    const char* windowId = nullptr;
    const char* dismissId = nullptr;
    const char* title = nullptr;
    const char* body = nullptr;
    float animation = 1.0f;
    const char* actionLabel = nullptr;
    const char* actionUrl = nullptr;
    const char* actionPath = nullptr;
    const char* actionTicker = nullptr;
    std::string* requestedTicker = nullptr;
    const char* accentText = nullptr;
    int accentDirection = 0;
    const std::vector<squarestar::application::StockMoveNotification>* groupedStockRows = nullptr;
    bool pointerInteractionEnabled = true;
};

NotificationCardRenderResult RenderNotificationCard(
    squarestar::application::AppState& state,
    ImGuiViewport* viewport,
    squarestar::presentation::NotificationBlockStack& stack,
    const NotificationCardSpec& spec);

void RenderMarketMoveNotices(squarestar::application::AppState& state,
                             ImGuiViewport* viewport,
                             squarestar::presentation::NotificationBlockStack& stack);
void RenderInteractionNotice(squarestar::application::AppState& state,
                             ImGuiViewport* viewport,
                             squarestar::presentation::NotificationBlockStack& stack);
void RenderFirstFetchWarmupNotice(squarestar::application::AppState& state,
                                  ImGuiViewport* viewport,
                                  squarestar::presentation::NotificationBlockStack& stack);
void RenderMarketOpenNotice(squarestar::application::AppState& state,
                            ImGuiViewport* viewport,
                            squarestar::presentation::NotificationBlockStack& stack);

void ReserveVisibleStockModeNotice(
    const squarestar::application::AppState& state,
    squarestar::presentation::NotificationBlockStack& stack);
void UpdateContextualKeybindHint(squarestar::application::AppState& state);
void RenderContextualKeybindHint(
    squarestar::application::AppState& state,
    ImGuiViewport* viewport,
    squarestar::presentation::NotificationBlockStack& stack);
void RenderMonitorModeHint(squarestar::application::AppState& state,
                           ImGuiViewport* viewport,
                           squarestar::presentation::NotificationBlockStack& stack);

} // namespace squarestar::shell
