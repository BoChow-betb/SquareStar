#pragma once

#include "application/runtime_state.hpp"

namespace squarestar::application {

enum class ForegroundNotificationKind {
    PriceAlert,
    MarketMove,
    InteractionFeedback,
    FirstFetchWarmup,
    MarketOpen,
    MonitorModeHint,
    ContextualKeybindHint,
};

[[nodiscard]] constexpr bool ShouldRenderForegroundNotification(
    AppUiMode mode,
    ForegroundNotificationKind kind) noexcept {
    if (mode == AppUiMode::Gui)
        return true;

    // Keep LiteGUI quiet: only price alerts and market moves get cards.
    // Other feedback stays audio-only.
    return kind == ForegroundNotificationKind::PriceAlert ||
           kind == ForegroundNotificationKind::MarketMove;
}

} // namespace squarestar::application
