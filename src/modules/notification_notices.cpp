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

using squarestar::application::ApplicationRuntime;
using squarestar::application::AppUiMode;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::RequestGuiWakeAt;
using squarestar::application::UiRounding;
using squarestar::application::AppState;
using squarestar::application::CountMonitorStockTiles;
using squarestar::application::ShouldHoldMonitorModeHint;
using squarestar::application::ResolveContextualKeybindSurface;
using squarestar::application::HasContextualKeybindHint;
using squarestar::application::ContextualKeybindSurface;
using squarestar::application::FormatKeyBind;
using squarestar::application::StockContext;
using squarestar::application::TerminalAction;
using squarestar::application::IsLightGuiTheme;
using squarestar::presentation::NotificationBlockStack;
using squarestar::presentation::NotificationCardWidth;
using squarestar::presentation::LinkedNotificationCardHeight;
using squarestar::presentation::ShouldStackNotificationRowValues;

void RenderMarketMoveNotices(AppState& state,
                                    ImGuiViewport* viewport,
                                    NotificationBlockStack& stack) {
    if (!viewport || IsCleanGuiCaptureFrame())
        return;

    const auto now = std::chrono::steady_clock::now();
    auto& notices = state.render.notifications.marketMoves.notices;
    for (auto it = notices.begin(); it != notices.end();) {
        if (state.navigation.liteGuiActive && !stack.HasBlockCapacity()) {


for (auto queued = it; queued != notices.end(); ++queued)
                queued->RefreshVisibleHold(now);
            break;
        }
        const bool holding = it->Holding(now);
        const float target = holding ? 1.0f : 0.0f;
        if (it->presentationStarted &&
            it->until != std::chrono::steady_clock::time_point{} && holding)
            RequestGuiWakeAt(it->until);
        const auto animation = squarestar::application::AdvanceTransientNoticeAnimation(
            it->animation, holding, UiFrameDelta(), state.UiAnimationsEnabled(), 20.0f);
        it->animation = animation.value;
        if (animation.clearExpired) {
            it = notices.erase(it);
            continue;
        }

        if (it->animation > 0.001f) {
            char windowId[80]{};
            char dismissId[80]{};
            std::snprintf(windowId,
                          sizeof(windowId),
                          "##MarketMoveNotice_%llu",
                          static_cast<unsigned long long>(it->serial));
            std::snprintf(dismissId,
                          sizeof(dismissId),
                          "##DismissMarketMoveNotice_%llu",
                          static_cast<unsigned long long>(it->serial));
            std::string requestedTicker;
            const bool singleTicker = it->rows.size() == 1 && !it->rows.front().ticker.empty();
            const std::string openLabel =
                singleTicker ? "Open in Terminal  " + it->rows.front().ticker : std::string{};
            const NotificationCardRenderResult cardResult = RenderNotificationCard(
                state,
                viewport,
                stack,
                {.windowId = windowId,
                 .dismissId = dismissId,
                 .title = it->title.c_str(),
                 .body = it->priceText.c_str(),
                 .animation = it->animation,
                 .actionLabel = singleTicker ? openLabel.c_str() : nullptr,
                 .actionTicker = singleTicker ? it->rows.front().ticker.c_str() : nullptr,
                 .requestedTicker = &requestedTicker,
                 .accentText = it->deltaText.c_str(),
                 .accentDirection = it->direction,
                 .groupedStockRows = it->rows.size() > 1 ? &it->rows : nullptr,


.pointerInteractionEnabled =
                     !state.navigation.liteGuiActive || it->pointerDismissArmed});
            if (!requestedTicker.empty())
                (void)OpenNotificationStock(state, requestedTicker);
            if (cardResult == NotificationCardRenderResult::Visible) {
                const bool firstVisibleFrame = !it->presentationStarted;
                it->MarkPresented(now);
                it->pointerDismissArmed = true;
                if (firstVisibleFrame)
                    RequestGuiWakeAt(it->until);
            } else if (cardResult == NotificationCardRenderResult::Deferred) {


                it->RefreshVisibleHold(now);
            } else {
                it->Dismiss();
            }
        }
        if (state.UiAnimationsEnabled() && std::abs(it->animation - target) > 0.001f)
            RequestGuiRedraw();
        ++it;
    }
}

void RenderInteractionNotice(AppState& state,
                                    ImGuiViewport* viewport,
                                    NotificationBlockStack& stack) {
    if (!viewport || IsCleanGuiCaptureFrame())
        return;

    auto& notice = state.render.notifications.interaction;
    const auto now = std::chrono::steady_clock::now();
    const bool holding = notice.Holding(now);
    const float target = holding ? 1.0f : 0.0f;
    if (holding)
        RequestGuiWakeAt(notice.presentation.until);

    const auto animation = squarestar::application::AdvanceTransientNoticeAnimation(
        notice.presentation.animation,
        holding,
        UiFrameDelta(),
        state.UiAnimationsEnabled(),
        20.0f);
    notice.presentation.animation = animation.value;
    if (animation.clearExpired) {
        notice.Clear();
        return;
    }
    if (notice.presentation.animation <= 0.001f)
        return;

    const NotificationCardRenderResult cardResult = RenderNotificationCard(
        state,
        viewport,
        stack,
        {.windowId = "##InteractionNotice",
         .dismissId = "##DismissInteractionNotice",
         .title = notice.title.c_str(),
         .body = notice.body.c_str(),
         .animation = notice.presentation.animation,
         .actionLabel = notice.actionLabel.c_str(),
         .actionUrl = notice.actionUrl.c_str(),
         .actionPath = notice.actionPath.c_str(),
         .pointerInteractionEnabled = notice.pointerDismissArmed});


    notice.pointerDismissArmed = cardResult == NotificationCardRenderResult::Visible;
    if (cardResult == NotificationCardRenderResult::Dismissed)
        notice.Dismiss();

    if (state.UiAnimationsEnabled() &&
        std::abs(notice.presentation.animation - target) > 0.001f)
        RequestGuiRedraw();
}

void RenderFirstFetchWarmupNotice(AppState& state,
                                         ImGuiViewport* viewport,
                                         NotificationBlockStack& stack) {
    auto& warmup = state.render.notifications.firstFetchWarmup;
    const bool normalGui = ApplicationRuntime().CurrentUiMode() == AppUiMode::Gui;
    if (!normalGui || !viewport) {
        warmup.Clear();
        return;
    }
    if (IsCleanGuiCaptureFrame())
        return;

    const auto now = std::chrono::steady_clock::now();
    if (warmup.pending) {
        warmup.pending = false;
        warmup.presentation.until = now + std::chrono::seconds(5);
        RequestGuiRedraw();
    }
    const bool holding = warmup.presentation.Holding(now);
    const float target = holding ? 1.0f : 0.0f;
    if (holding)
        RequestGuiWakeAt(warmup.presentation.until);

    const auto animation = squarestar::application::AdvanceTransientNoticeAnimation(
        warmup.presentation.animation,
        holding,
        UiFrameDelta(),
        state.UiAnimationsEnabled());
    warmup.presentation.animation = animation.value;
    if (animation.clearExpired) {
        warmup.presentation.until = {};
        return;
    }
    if (warmup.presentation.animation <= 0.001f)
        return;

    constexpr const char* title = "First fetch may be slower";
    constexpr const char* body =
        "The first quote can take a moment while the connection starts.";
    if (RenderNotificationCard(
            state,
            viewport,
            stack,
            {.windowId = "##FirstFetchWarmupNotice",
             .dismissId = "##DismissFirstFetchWarmupNotice",
             .title = title,
             .body = body,
             .animation = warmup.presentation.animation}) ==
        NotificationCardRenderResult::Dismissed)
        warmup.presentation.until = {};

    if (state.UiAnimationsEnabled() &&
        std::abs(warmup.presentation.animation - target) > 0.001f)
        RequestGuiRedraw();
}

void RenderMarketOpenNotice(AppState& state,
                                   ImGuiViewport* viewport,
                                   NotificationBlockStack& stack) {
    auto& notice = state.render.notifications.marketOpen;
    const auto now = std::chrono::steady_clock::now();
    const bool holding = notice.Holding(now);
    const float target = holding ? 1.0f : 0.0f;
    if (holding)
        RequestGuiWakeAt(notice.until);

    const auto animation = squarestar::application::AdvanceTransientNoticeAnimation(
        notice.animation,
        holding,
        UiFrameDelta(),
        state.UiAnimationsEnabled());
    notice.animation = animation.value;
    if (animation.clearExpired) {
        notice.until = {};
        return;
    }
    if (notice.animation <= 0.001f)
        return;

    constexpr const char* title = "US market is open";
    const std::tm local = squarestar::market::CurrentNewYorkTime(std::time(nullptr));
    const int closeMinutes = squarestar::market::MarketCloseMinutesForDate(
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    const char* closeLabel = closeMinutes == 13 * 60 ? "1:00 PM ET" : "4:00 PM ET";
    const std::string body =
        std::string("Price refreshes, movement notices, and alerts are active until ") +
        closeLabel + ".";
    if (RenderNotificationCard(
            state,
            viewport,
            stack,
            {.windowId = "##MarketOpenNotice",
             .dismissId = "##DismissMarketOpenNotice",
             .title = title,
             .body = body.c_str(),
             .animation = notice.animation}) ==
        NotificationCardRenderResult::Dismissed)
        notice.until = {};

    if (state.UiAnimationsEnabled() && std::abs(notice.animation - target) > 0.001f)
        RequestGuiRedraw();
}


}
