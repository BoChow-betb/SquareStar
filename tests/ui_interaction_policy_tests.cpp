#include "application/app_render_cache.hpp"
#include "application/foreground_notification_policy.hpp"
#include "application/user_feedback.hpp"
#include "presentation/gui_shell_runtime_state.hpp"
#include "presentation/notification_layout.hpp"
#include "presentation/notification_stack.hpp"
#include "presentation/ui_window_policy.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

bool Near(float actual, float expected, float tolerance = 0.000001f) {
    return std::abs(actual - expected) <= tolerance;
}

} // namespace

int main() {
    using namespace squarestar::presentation;

    const UiRect owner{120.0f, 80.0f, 420.0f, 260.0f};
    const UiPoint rightBottom = ClampWindowToBounds(
        UiPoint{900.0f, 900.0f}, UiSize{180.0f, 90.0f}, owner, 6.0f);
    Check(Near(rightBottom.x, 354.0f) && Near(rightBottom.y, 244.0f),
          "tooltip is clamped inside the owning window at the lower-right edge");

    const UiPoint oversized = ClampWindowToBounds(
        UiPoint{-1000.0f, -1000.0f}, UiSize{900.0f, 700.0f}, owner, 6.0f);
    Check(Near(oversized.x, 126.0f) && Near(oversized.y, 86.0f),
          "oversized tooltip remains anchored to the owner's padded origin");

    const UiRect negativeMonitor{-1920.0f, 40.0f, 1920.0f, 1040.0f};
    const UiPoint secondaryPopup = ClampWindowToBounds(
        UiPoint{-80.0f, 1000.0f}, UiSize{360.0f, 360.0f}, negativeMonitor, 14.0f);
    Check(Near(secondaryPopup.x, -374.0f) && Near(secondaryPopup.y, 706.0f),
          "popup bounds support negative-coordinate secondary monitors");

    const UiSize dpiPopup = FitWindowToBounds(
        UiSize{360.0f * 2.5f, 360.0f * 2.5f}, UiRect{0, 0, 800, 600}, 14.0f);
    Check(Near(dpiPopup.width, 772.0f) && Near(dpiPopup.height, 572.0f),
          "high-DPI popup size is capped to the monitor work area");

    GuiShellRuntimeState menuRuntime;
    constexpr ImGuiID menuId = 0x51A7u;
    const auto firstOpen = menuRuntime.UpdateAnimatedFloatingMenu(
        menuId, true, false, 1, false, 1.0f / 60.0f);
    Check(firstOpen.visible && firstOpen.open && Near(firstOpen.animation, 1.0f),
          "non-animated floating menu opens immediately");
    Check(menuRuntime.DismissAnimatedFloatingMenu(menuId, true),
          "floating menu can be dismissed instantly");
    menuRuntime.EndAnimatedFloatingMenu();
    const auto reopened = menuRuntime.UpdateAnimatedFloatingMenu(
        menuId, true, false, 2, false, 1.0f / 60.0f);
    Check(reopened.visible && reopened.open && Near(reopened.animation, 1.0f),
          "floating menu responds when activated again after an instant close");
    menuRuntime.EndAnimatedFloatingMenu();

    const UiRect workAreas[] = {
        UiRect{0, 0, 1920, 1040},
        UiRect{-2560, -120, 2560, 1400},
        UiRect{1920, 0, 1280, 720}};
    Check(SelectOwningWorkArea(workAreas, UiRect{-1800, 50, 900, 700}) == 1,
          "owner overlap selects the correct monitor in a mixed monitor grid");
    Check(SelectOwningWorkArea(workAreas, UiRect{3400, 100, 200, 200}) == 2,
          "off-screen owner falls back to the nearest work-area center");

    Check(Near(LinkedNotificationCardHeight(96.0f),
               kLinkedNotificationCardMinimumHeight) &&
              Near(LinkedNotificationCardHeight(184.0f), 184.0f),
          "linked notices share a minimum height but grow to avoid clipping");

    const float liteNotificationCapacity = NotificationVerticalCapacity(108.0f, 36.0f);
    NotificationBlockStack liteNotificationStack;
    Check(Near(liteNotificationCapacity, 68.0f) &&
              !liteNotificationStack.CanReserve(kLinkedNotificationCardMinimumHeight,
                                                liteNotificationCapacity) &&
              Near(liteNotificationStack.RemainingHeight(liteNotificationCapacity), 52.0f),
          "compact LiteGUI exposes a bounded notification area below the title bar");
    NotificationBlockStack expandedLiteNotificationStack;
    const float expandedLiteNotificationCapacity = NotificationVerticalCapacity(222.0f, 36.0f);
    Check(expandedLiteNotificationStack
              .TryReserve(kLinkedNotificationCardMinimumHeight, expandedLiteNotificationCapacity)
              .has_value(),
          "expanded LiteGUI keeps the regular linked notification card");

    NotificationBlockStack cappedLiteStack(16.0f, 12.0f, 2);
    const float twoCardCapacity = NotificationVerticalCapacity(420.0f, 36.0f);
    Check(cappedLiteStack.TryReserve(112.0f, twoCardCapacity).has_value() &&
              cappedLiteStack.TryReserve(150.0f, twoCardCapacity).has_value() &&
              !cappedLiteStack.TryReserve(48.0f, twoCardCapacity).has_value() &&
              cappedLiteStack.ReservedBlocks() == 2,
          "LiteGUI notification tower is capped at two foreground cards");
    NotificationBlockStack spacedLiteStack(16.0f, 12.0f, 2);
    static_cast<void>(spacedLiteStack.ReserveSpace(132.0f));
    Check(spacedLiteStack.ReservedBlocks() == 0 && spacedLiteStack.HasBlockCapacity(),
          "reserved stock-notice space does not consume a Lite notification slot");

    using squarestar::application::AppUiMode;
    using squarestar::application::ForegroundNotificationKind;
    using squarestar::application::ShouldRenderForegroundNotification;
    Check(ShouldRenderForegroundNotification(
              AppUiMode::LiteGui, ForegroundNotificationKind::PriceAlert) &&
              ShouldRenderForegroundNotification(
                  AppUiMode::LiteGui, ForegroundNotificationKind::MarketMove) &&
              !ShouldRenderForegroundNotification(
                  AppUiMode::LiteGui, ForegroundNotificationKind::InteractionFeedback) &&
              !ShouldRenderForegroundNotification(
                  AppUiMode::LiteGui, ForegroundNotificationKind::MarketOpen) &&
              ShouldRenderForegroundNotification(
                  AppUiMode::Gui, ForegroundNotificationKind::InteractionFeedback),
          "LiteGUI visual-notification policy is limited to alerts and market moves");

    squarestar::application::NotificationRenderState notificationState;
    notificationState.interaction.title = "full gui only";
    notificationState.interaction.presentation.until =
        std::chrono::steady_clock::time_point{std::chrono::seconds(10)};
    notificationState.marketOpen.until =
        std::chrono::steady_clock::time_point{std::chrono::seconds(10)};
    notificationState.firstFetchWarmup.pending = true;
    notificationState.monitorModeHint.dismissed = true;
    notificationState.keybindHint.surface =
        squarestar::application::ContextualKeybindSurface::Stock;
    notificationState.marketMoves.notices.emplace_back();
    notificationState.marketMoves.nextSerial = 42;
    squarestar::application::StockMoveNotification historyRow;
    historyRow.ticker = "AAPL";
    historyRow.before = 100.0;
    historyRow.after = 101.0;
    notificationState.notificationCenter.Push(historyRow);
    notificationState.ClearLiteGuiSuppressed();
    Check(notificationState.interaction.title.empty() &&
              notificationState.marketOpen.until == std::chrono::steady_clock::time_point{} &&
              !notificationState.firstFetchWarmup.pending &&
              notificationState.monitorModeHint.dismissed &&
              notificationState.keybindHint.surface ==
                  squarestar::application::ContextualKeybindSurface::None &&
              notificationState.marketMoves.notices.size() == 1 &&
              notificationState.marketMoves.nextSerial == 42 &&
              notificationState.notificationCenter.entries.size() == 1,
          "LiteGUI cleanup drops FullGUI-only state without discarding notification history");

    notificationState.firstFetchWarmup.pending = true;
    notificationState.firstFetchWarmup.presentation.until =
        std::chrono::steady_clock::time_point{std::chrono::seconds(20)};
    notificationState.ClearUnavailableForeground();
    Check(notificationState.marketMoves.notices.empty() &&
              notificationState.firstFetchWarmup.pending &&
              notificationState.firstFetchWarmup.presentation.until !=
                  std::chrono::steady_clock::time_point{} &&
              notificationState.notificationCenter.entries.size() == 1,
          "hidden foreground cleanup preserves deferred history and first-fetch state");

    squarestar::application::NotificationCenterRenderState notificationCenter;
    for (int index = 0; index < 25; ++index) {
        squarestar::application::StockMoveNotification row;
        row.ticker = "T" + std::to_string(index);
        row.before = 100.0 + index;
        row.after = 101.0 + index;
        notificationCenter.Push(std::move(row));
    }
    Check(notificationCenter.entries.size() ==
              squarestar::application::kNotificationCenterMaxEntries &&
              notificationCenter.entries.front().row.ticker == "T5" &&
              notificationCenter.entries.back().row.ticker == "T24",
          "notification center retains only the newest twenty blocks");
    Check(notificationCenter.focusedSerial == 0,
          "new notification history does not create sticky pointer focus");
    const std::uint64_t newestSerial = notificationCenter.entries.back().serial;
    notificationCenter.focusedSerial = newestSerial;
    notificationCenter.entries.back().focusAnimation = 1.0f;
    Check(notificationCenter.Remove(newestSerial) &&
              notificationCenter.entries.size() ==
                  squarestar::application::kNotificationCenterMaxEntries - 1 &&
              notificationCenter.focusedSerial == 0,
          "notification dismissal clears pointer focus instead of moving it to another block");
    notificationCenter.Clear();
    Check(notificationCenter.entries.empty() && notificationCenter.focusedSerial == 0,
          "notification center internal clear removes every stored block");
    squarestar::application::StockMoveNotification alertHistoryRow;
    alertHistoryRow.ticker = "AAPL";
    alertHistoryRow.after = 99.0;
    alertHistoryRow.priceAlert = true;
    alertHistoryRow.alertThreshold = 100.0;
    notificationCenter.Push(alertHistoryRow);
    squarestar::application::StockMoveNotification laterHistoryRow;
    laterHistoryRow.ticker = "MSFT";
    laterHistoryRow.before = 200.0;
    laterHistoryRow.after = 201.0;
    notificationCenter.Push(laterHistoryRow);
    notificationCenter.focusedSerial = notificationCenter.entries.front().serial;
    notificationCenter.entries.front().focusAnimation = 1.0f;
    alertHistoryRow.after = 98.5;
    notificationCenter.Push(alertHistoryRow);
    Check(notificationCenter.entries.size() == 2 &&
              notificationCenter.entries.back().row.ticker == "AAPL" &&
              notificationCenter.entries.back().row.after == 98.5 &&
              notificationCenter.focusedSerial == 0 &&
              notificationCenter.entries.back().focusAnimation == 0.0f,
          "refreshed price alerts coalesce without retaining stale pointer focus");
    Check(notificationCenter.HasDismissibleEntries(),
          "mixed notification history reports manually dismissible entries");
    notificationCenter.ClearDismissible();
    Check(notificationCenter.entries.size() == 1 &&
              notificationCenter.entries.front().row.priceAlert &&
              notificationCenter.entries.front().row.ticker == "AAPL" &&
              !notificationCenter.HasDismissibleEntries(),
          "manual clear-all preserves active price-alert history blocks");
    Check(notificationCenter.RemovePriceAlertForTicker("AAPL") &&
              notificationCenter.entries.empty(),
          "price-alert history can be removed automatically when the alert is no longer active");

    notificationCenter.Push(laterHistoryRow);
    notificationCenter.focusedSerial = notificationCenter.entries.back().serial;
    notificationCenter.entries.back().focusAnimation = 0.75f;
    notificationCenter.ClearPointerFocus();
    Check(notificationCenter.focusedSerial == 0 &&
              notificationCenter.entries.back().focusAnimation == 0.0f,
          "notification center pointer focus resets when the popup closes");
    notificationCenter.Clear();

    using squarestar::application::MarketMoveNotice;
    using squarestar::application::kNotificationHoldDuration;
    const auto marketMoveStart = std::chrono::steady_clock::time_point{std::chrono::seconds(100)};
    MarketMoveNotice marketMoveNotice;
    Check(marketMoveNotice.Holding(marketMoveStart) &&
              !marketMoveNotice.presentationStarted &&
              !marketMoveNotice.pointerDismissArmed &&
              marketMoveNotice.until == std::chrono::steady_clock::time_point{},
          "queued market-move notice does not spend its lifetime before first presentation");
    marketMoveNotice.MarkPresented(marketMoveStart);
    Check(marketMoveNotice.presentationStarted &&
              marketMoveNotice.until == marketMoveStart + kNotificationHoldDuration &&
              marketMoveNotice.Holding(marketMoveStart + std::chrono::seconds(4)) &&
              !marketMoveNotice.Holding(marketMoveStart + std::chrono::seconds(6)),
          "market-move hold begins only when the notification is actually presented");
    marketMoveNotice.RefreshVisibleHold(marketMoveStart + std::chrono::seconds(3));
    Check(marketMoveNotice.until ==
              marketMoveStart + std::chrono::seconds(3) + kNotificationHoldDuration,
          "temporarily displaced market-move notice receives a fresh visible hold window");
    marketMoveNotice.Dismiss();
    Check(!marketMoveNotice.Holding(marketMoveStart + std::chrono::seconds(3)),
          "dismissed market-move notice no longer reports itself as holding");

    squarestar::application::UserFeedback normalFeedback;
    normalFeedback.type = squarestar::application::UserFeedbackType::Information;
    Check(std::string_view(
              squarestar::application::ResolveUserFeedbackRoute(normalFeedback).soundAsset) ==
              "key.wav",
          "normal notification blocks use the key cue");

    squarestar::application::UserFeedback warningFeedback;
    warningFeedback.type = squarestar::application::UserFeedbackType::Warning;
    Check(std::string_view(
              squarestar::application::ResolveUserFeedbackRoute(warningFeedback).soundAsset) ==
              "decline.wav",
          "warning notification blocks use the decline cue");

    squarestar::application::UserFeedback errorFeedback;
    errorFeedback.type = squarestar::application::UserFeedbackType::Error;
    Check(std::string_view(
              squarestar::application::ResolveUserFeedbackRoute(errorFeedback).soundAsset) ==
              "decline.wav",
          "error notification blocks use the decline cue");

    squarestar::application::UserFeedback declinedFeedback;
    declinedFeedback.sound = squarestar::application::UserFeedbackSound::Decline;
    Check(std::string_view(
              squarestar::application::ResolveUserFeedbackRoute(declinedFeedback).soundAsset) ==
              "decline.wav",
          "explicit decline feedback keeps the decline cue");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All UI interaction policy tests passed\n";
    return EXIT_SUCCESS;
}
