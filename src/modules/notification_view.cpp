#include "modules/notification_view.hpp"
#include "modules/notification_view_internal.hpp"

#include "modules/core.hpp"
#include "modules/platform.hpp"
#include "modules/views.hpp"

#include "application/app_state.hpp"
#include "application/contextual_keybind_policy.hpp"
#include "application/key_bindings.hpp"
#include "application/main_loop_signal.hpp"
#include "application/monitor_stock_policy.hpp"
#include "application/notification_text.hpp"
#include "application/runtime_state.hpp"
#include "application/ui_animation.hpp"
#include "domain/market_calendar.hpp"
#include "platform/windows_path.hpp"
#include "presentation/notification_layout.hpp"
#include "presentation/notification_stack.hpp"
#include "services/http_client.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace squarestar::shell {

using squarestar::application::AppState;
using squarestar::presentation::NotificationBlockStack;

static void RenderLiteGuiForegroundNotifications(AppState& state,
                                                  ImGuiViewport* viewport) {
    NotificationBlockStack stack(16.0f, 12.0f, LITE_GUI_NOTIFICATION_MAX_BLOCKS);
    RenderPriceAlertPopups(state, stack);
    RenderMarketMoveNotices(state, viewport, stack);
}

static void RenderFullGuiForegroundNotifications(AppState& state,
                                                  ImGuiViewport* viewport) {
    UpdateContextualKeybindHint(state);
    NotificationBlockStack stack;
    ReserveVisibleStockModeNotice(state, stack);
    RenderPriceAlertPopups(state, stack);
    RenderMarketMoveNotices(state, viewport, stack);
    RenderInteractionNotice(state, viewport, stack);
    RenderMonitorModeHint(state, viewport, stack);
    RenderFirstFetchWarmupNotice(state, viewport, stack);
    RenderMarketOpenNotice(state, viewport, stack);
    RenderContextualKeybindHint(state, viewport, stack);
}

void RenderForegroundNotifications(AppState& state, ImGuiViewport* viewport) {
    if (!UseForegroundNotificationBlocks()) {
        state.alerts.ClearToasts();
        state.render.notifications.ClearUnavailableForeground();
        return;
    }

    if (state.navigation.liteGuiActive) {
        RenderLiteGuiForegroundNotifications(state, viewport);
        return;
    }

    RenderFullGuiForegroundNotifications(state, viewport);
}


} // namespace squarestar::shell
