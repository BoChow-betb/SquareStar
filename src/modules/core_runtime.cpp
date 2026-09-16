#include "modules/core.hpp"

#include "application/app_state.hpp"
#include "application/foreground_notification_policy.hpp"
#include "application/main_loop_signal.hpp"
#include "application/notification_channel.hpp"
#include "application/notification_text.hpp"
#include "application/runtime_state.hpp"
#include "application/user_feedback.hpp"
#include "domain/market_runtime.hpp"
#include "modules/currency_display.hpp"
#include "platform/application_paths.hpp"
#include "platform/glfw_runtime.hpp"
#include "platform/win32_app_state.hpp"
#include "platform/windows_path.hpp"
#include "application/alert_service.hpp"
#include "application/app_render_cache.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::application::NotificationChannel;
using squarestar::platform::Win32AppRuntime;
using squarestar::market::CachedMarketOpen;
using squarestar::application::UiModeRequest;
using squarestar::application::SetMainLoopWakeNotifier;
using squarestar::application::WakeMainLoop;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::AppState;
using squarestar::application::UserFeedback;
using squarestar::application::UserFeedbackDestination;
using squarestar::application::UserFeedbackSound;
using squarestar::application::UserFeedbackType;
using squarestar::application::ResolveUserFeedbackRoute;
using squarestar::application::StockContext;
using squarestar::application::FormatStockMovePrices;
using squarestar::application::FormatStockMoveDelta;
using squarestar::application::FormatBackgroundStockMoves;
using squarestar::application::ForegroundNotificationKind;
using squarestar::application::ShouldRenderForegroundNotification;
using squarestar::application::NativeNotificationRequest;
using squarestar::platform::EnsureGlfwPlatformRuntimeInitialized;
using squarestar::platform::ShutdownGlfwPlatformRuntime;

bool EnsureGlfwRuntimeInitialized() {
    if (!EnsureGlfwPlatformRuntimeInitialized())
        return false;
    SetMainLoopWakeNotifier([] {
        glfwPostEmptyEvent();
    });
    return true;
}
void ShutdownGlfwRuntime() {
    SetMainLoopWakeNotifier({});
    ShutdownGlfwPlatformRuntime();
}
float UiFrameDelta() noexcept {
    return std::min(ImGui::GetIO().DeltaTime, 1.0f / 30.0f);
}
static bool EnsureTrayIconVisible();
static void HandleTaskbarMinimize(HWND hwnd);
static void HandleTaskbarRestore();
HICON LoadSquareStarIcon() {
    static HICON icon = [] {
        constexpr int iconResourceId = 101;
        HICON h = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                                    MAKEINTRESOURCEW(iconResourceId),
                                    IMAGE_ICON,
                                    0,
                                    0,
                                    LR_DEFAULTSIZE);
        const std::string path = squarestar::platform::GetExecutableDirectory() + "\\squarestar.ico";
        if (!h) {
            const std::wstring widePath = squarestar::platform::Utf8PathToWide(path);
            if (!widePath.empty())
                h = (HICON)LoadImageW(nullptr,
                                      widePath.c_str(),
                                      IMAGE_ICON,
                                      0,
                                      0,
                                      LR_LOADFROMFILE | LR_DEFAULTSIZE);
        }
        if (!h)
            h = (HICON)LoadImageW(
                nullptr, L"squarestar.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);


return h ? h : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    }();
    return icon;
}
void RequestLiteGuiMode() {
    if (ApplicationRuntime().CurrentUiMode() !=
        squarestar::application::AppUiMode::LiteGui) {
        ApplicationRuntime().RequestUiMode(UiModeRequest::LiteGui);
        RequestGuiRedraw();
    }
}
#define WM_TRAYICON (WM_USER + 1)
bool IsGuiWindowMaximized(HWND hwnd) {
    return hwnd && (ApplicationRuntime().GuiFullscreenSizeOverride() || IsZoomed(hwnd) ||
                    (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_MAXIMIZE) != 0);
}
bool IsAppWindowForeground() {
    const HWND mainWindow = Win32AppRuntime().MainWindow();
    return mainWindow && GetForegroundWindow() == mainWindow;
}
void ApplyRoundedWindowCorners(HWND hwnd) {
    if (!hwnd)
        return;
    const DWORD roundPreference = 2;
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 33, &roundPreference, sizeof(roundPreference)))) {
        SetWindowRgn(hwnd, NULL, TRUE);
        return;
    }
    if (IsZoomed(hwnd)) {
        SetWindowRgn(hwnd, NULL, TRUE);
        return;
    }
    RECT rect{};
    if (GetWindowRect(hwnd, &rect)) {
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, 18, 18);
        SetWindowRgn(hwnd, region, TRUE);
    }
}
static LRESULT BorderlessWindowHitTest(HWND hwnd, LPARAM lParam) {
    if (!hwnd)
        return HTCLIENT;
    const POINT p{static_cast<short>(LOWORD(lParam)),
                  static_cast<short>(HIWORD(lParam))};
    const bool liteGui = ApplicationRuntime().CurrentUiMode() ==
                         squarestar::application::AppUiMode::LiteGui;


    POINT clientPoint = p;
    RECT clientRect{};
    if (!ScreenToClient(hwnd, &clientPoint) || !GetClientRect(hwnd, &clientRect))
        return HTCLIENT;

    constexpr LONG titleHeight = static_cast<LONG>(APP_TITLE_BAR_HEIGHT);
    const int controlCount = liteGui ? LITE_TITLE_BAR_CONTROL_COUNT : APP_TITLE_BAR_CONTROL_COUNT;
    const LONG controlsWidth = static_cast<LONG>(
        APP_TITLE_BAR_BUTTON_WIDTH * static_cast<float>(controlCount));
    const LONG dragRegionRight =
        std::max(LONG{0}, clientRect.right - controlsWidth);
    if (clientPoint.x >= 0 && clientPoint.y >= 0 &&
        clientPoint.y < titleHeight && clientPoint.x < dragRegionRight)
        return HTCAPTION;

    return HTCLIENT;
}
static constexpr UINT WM_SQUARESTAR_NOTIFICATION = WM_APP + 0x531;
static void ShowPendingNativeNotification();

static void ReleaseGuiMouseButtons() {
    if (!ImGui::GetCurrentContext())
        return;
    ImGuiIO& io = ImGui::GetIO();
    for (int button = 0; button < ImGuiMouseButton_COUNT; ++button)
        io.AddMouseButtonEvent(button, false);
}
LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_SQUARESTAR_NOTIFICATION) {
        ShowPendingNativeNotification();
        return 0;
    }
    if (msg == WM_NCCALCSIZE && wParam)
        return 0;
    if (msg == WM_DPICHANGED && lParam) {


const LRESULT result = CallWindowProc(
            Win32AppRuntime().OriginalWindowProc(), hwnd, msg, wParam, lParam);
        ApplyRoundedWindowCorners(hwnd);
        return result;
    }
    if (msg == WM_NCHITTEST) {
        const LRESULT hit = BorderlessWindowHitTest(hwnd, lParam);
        if (hit != HTCLIENT)
            return hit;
    }
    if (msg == WM_ACTIVATE) {
        if (LOWORD(wParam) == WA_INACTIVE) {
            ReleaseGuiMouseButtons();
        } else {
            ApplicationRuntime().RequestGuiFrameDeltaReset();
            RequestGuiRedraw();
        }
    }
    if (msg == WM_SIZE) {
        ApplyRoundedWindowCorners(hwnd);
        if (wParam == SIZE_MINIMIZED) {
            HandleTaskbarMinimize(hwnd);
        } else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) {
            HandleTaskbarRestore();
            ApplicationRuntime().RequestGuiFrameDeltaReset();


RedrawWindow(hwnd,
                         nullptr,
                         nullptr,
                         RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
            WakeMainLoop();
        }
    }
    if (msg == WM_TRAYICON) {
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuA(hMenu, MF_STRING, 1, "Show SquareStar");
            AppendMenuA(hMenu, MF_STRING, 2, "Exit");
            SetForegroundWindow(hwnd);
            int cmd =
                TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == 1) {
                RestoreFromTray(hwnd);
            } else if (cmd == 2) {
                ApplicationRuntime().RequestQuit();
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
        } else if (LOWORD(lParam) == WM_LBUTTONUP ||
                   LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            RestoreFromTray(hwnd);
        }
        return 0;
    }
    return CallWindowProc(Win32AppRuntime().OriginalWindowProc(), hwnd, msg, wParam, lParam);
}
void SetupTrayIcon(HWND hwnd) {
    Win32AppRuntime().ConfigureTrayIcon(hwnd, LoadSquareStarIcon(), WM_TRAYICON);
}
static bool EnsureTrayIconVisible() {
    return Win32AppRuntime().EnsureTrayIconVisible();
}
void RemoveTrayIcon() {
    Win32AppRuntime().RemoveTrayIcon();
    NotificationChannel().Clear();
}
bool IsAppMinimizedForNotifications() {
    return Win32AppRuntime().MinimizedForNotifications();
}
bool UseBackgroundNotificationBlock() {


return IsAppMinimizedForNotifications();
}
bool UseForegroundNotificationBlocks() {
    if (IsAppMinimizedForNotifications())
        return false;
    const squarestar::application::AppUiMode mode =
        ApplicationRuntime().CurrentUiMode();
    return mode == squarestar::application::AppUiMode::Gui ||
           mode == squarestar::application::AppUiMode::LiteGui;
}
namespace {

bool FeedbackUsesBackgroundDestination(UserFeedbackDestination destination) {
    return destination == UserFeedbackDestination::Background ||
           (destination == UserFeedbackDestination::Automatic &&
            UseBackgroundNotificationBlock());
}

void ShowForegroundInteractionNotice(AppState& state,
                                     std::string title,
                                     std::string body,
                                     std::chrono::seconds duration,
                                     std::string actionLabel,
                                     std::string actionUrl,
                                     std::string actionPath) {
    auto& notice = state.render.notifications.interaction;
    notice.title = std::move(title);
    notice.body = std::move(body);
    notice.actionLabel = std::move(actionLabel);
    notice.actionUrl = std::move(actionUrl);
    notice.actionPath = std::move(actionPath);
    notice.presentation.until = std::chrono::steady_clock::now() + duration;
    notice.pointerDismissArmed = false;
    if (!state.UiAnimationsEnabled())
        notice.presentation.animation = 1.0f;
    RequestGuiRedraw();
}

}

void PublishUserFeedback(AppState& state,
                         UserFeedback feedback) {
    const auto route = ResolveUserFeedbackRoute(feedback);
    if (FeedbackUsesBackgroundDestination(route.destination)) {
        TriggerTrayNotification(feedback.title.c_str(),
                                feedback.body.c_str(),
                                std::move(feedback.actionPath));
        return;
    }
    if (ShouldRenderForegroundNotification(
            ApplicationRuntime().CurrentUiMode(),
            ForegroundNotificationKind::InteractionFeedback)) {
        ShowForegroundInteractionNotice(state,
                                        std::move(feedback.title),
                                        std::move(feedback.body),
                                        feedback.duration,
                                        std::move(feedback.actionLabel),
                                        std::move(feedback.actionUrl),
                                        std::move(feedback.actionPath));
    }
    if (route.soundAsset)
        PlayUISound(route.soundAsset, state);
}
void PublishUserFeedback(AppState& state,
                                UserFeedbackType type,
                                std::string title,
                                std::string body,
                                std::chrono::seconds duration) {
    PublishUserFeedback(
        state, UserFeedback{type, std::move(title), std::move(body), duration});
}
void PublishSilentFeedback(
    AppState& state,
    UserFeedbackType type,
    std::string title,
    std::string body,
    UserFeedbackDestination destination) {
    PublishUserFeedback(
        state,
        UserFeedback{type,
                     std::move(title),
                     std::move(body),
                     std::chrono::seconds(4),
                     destination,
                     UserFeedbackSound::None});
}
void PublishBackgroundError(AppState& state,
                                   std::string title,
                                   std::string body) {
    PublishSilentFeedback(state,
                          UserFeedbackType::Error,
                          std::move(title),
                          std::move(body),
                          UserFeedbackDestination::Background);
}
bool IsStockInteractionSurfaceAudible(const StockContext& ctx) {
    const squarestar::application::AppUiMode mode =
        ApplicationRuntime().CurrentUiMode();
    return ctx.navigation.refreshSurfaceVisible && !IsAppMinimizedForNotifications() &&
           (mode == squarestar::application::AppUiMode::Gui ||
            mode == squarestar::application::AppUiMode::LiteGui);
}
static void HandleTaskbarMinimize(HWND hwnd) {
    auto& runtime = Win32AppRuntime();
    if (!hwnd || runtime.MinimizedForNotifications())
        return;
    runtime.SetTaskbarMinimized(true);
    runtime.SetTrayTooltip(
        "SquareStar is minimized; alerts remain active");
    if (EnsureTrayIconVisible())
        TriggerTrayNotification(
            "SquareStar minimized",
            "Price alerts and market-move notices remain active.");
}
static void HandleTaskbarRestore() {
    auto& runtime = Win32AppRuntime();
    if (!runtime.TaskbarMinimized())
        return;
    runtime.SetTaskbarMinimized(false);
    if (!runtime.MinimizedToTray())
        RemoveTrayIcon();
}
void RestoreFromTray(HWND hwnd) {
    if (!hwnd)
        return;
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetWindowPos(hwnd,
                 HWND_TOP,
                 0,
                 0,
                 0,
                 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    RemoveTrayIcon();
    Win32AppRuntime().SetMinimizedToTray(false);
    Win32AppRuntime().SetTaskbarMinimized(false);
    ApplicationRuntime().RequestGuiFrameDeltaReset();
    WakeMainLoop();
}
void MinimizeToTray(HWND hwnd) {
    if (!hwnd)
        return;
    if (!Win32AppRuntime().MinimizedToTray()) {
        Win32AppRuntime().SetTrayTooltip("SquareStar is running in background");
        if (!EnsureTrayIconVisible())
            return;
        Win32AppRuntime().SetMinimizedToTray(true);
        Win32AppRuntime().SetTaskbarMinimized(false);
    }
    ShowWindow(hwnd, SW_HIDE);
    TriggerTrayNotification(
        "SquareStar",
        "Price alerts and market-move notices are active in the background.");
}


static void ShowPendingNativeNotification() {
    std::optional<NativeNotificationRequest> pending = NotificationChannel().Take();
    if (!pending)
        return;
    NativeNotificationRequest request = std::move(*pending);
    if (!Win32AppRuntime().TrayIconVisible())
        return;

    (void)Win32AppRuntime().ShowNotificationToast(
        request.title.empty() ? "SquareStar" : request.title.c_str(),
        request.message.c_str(),
        request.accentText.c_str(),
        request.accentDirection,
        request.groupId,
        request.actionPath.c_str(),
        request.actionTicker.c_str());
}

static void EnqueueNativeNotification(NativeNotificationRequest request) {
    if (!Win32AppRuntime().HasTrayNotificationTarget() ||
        (!Win32AppRuntime().TrayIconVisible() && !EnsureTrayIconVisible()))
        return;
    NotificationChannel().Enqueue(std::move(request));
    Win32AppRuntime().PostTrayMessage(WM_SQUARESTAR_NOTIFICATION);
}

void TriggerTrayNotification(const char* title,
                             const char* message,
                             std::string actionPath) {
    EnqueueNativeNotification(
        {title && *title ? title : "SquareStar",
         message ? message : "",
         {},
         0,
         0,
         std::move(actionPath),
         {}});
}
bool ShouldPublishMarketMoveNotification(AppState& state,
                                                const StockContext& ctx,
                                                double before,
                                                double after) {
    if (!state.config.marketMoveNotifications || !CachedMarketOpen())
        return false;
    const auto trigger = squarestar::alerts::EvaluateMarketMoveTriggers(
        before,
        after,
        ctx.RawData().previousClose,
        ctx.RawData().openPrice,
        ctx.RawData().fiftyTwoWeekHigh,
        ctx.RawData().fiftyTwoWeekLow,
        state.config.marketMoveThresholdPct,
        state.config.marketMove52WeekEvents,
        state.config.marketMoveStateChanges);
    if (!trigger.Any())
        return false;

    const auto now = std::chrono::steady_clock::now();
    if (state.alerts.IsMarketMoveCoolingDown(ctx.navigation.ticker, now))
        return false;

    const int cooldownMinutes = std::clamp(state.config.marketMoveCooldownMinutes, 0, 60);
    if (cooldownMinutes > 0) {
        state.alerts.SetMarketMoveCooldown(
            ctx.navigation.ticker, now + std::chrono::minutes(cooldownMinutes));
        state.alerts.PruneMarketMoveCooldowns(now, 128);
    } else {
        state.alerts.ClearMarketMoveCooldown(ctx.navigation.ticker);
    }

    squarestar::application::StockMoveNotification historyRow;
    historyRow.ticker = ctx.navigation.ticker;
    historyRow.before = before;
    historyRow.after = after;
    historyRow.currency = "USD";
    state.render.notifications.notificationCenter.Push(std::move(historyRow));
    return true;
}

static void RefreshForegroundMarketMoveNoticeText(
    squarestar::application::MarketMoveNotice& notice) {
    if (notice.rows.empty()) {
        notice.title.clear();
        notice.priceText.clear();
        notice.deltaText.clear();
        notice.direction = 0;
        return;
    }
    if (notice.rows.size() == 1) {
        const auto& row = notice.rows.front();
        notice.title = "[Market move] " + row.ticker;
        notice.priceText = FormatStockMovePrices(row.before, row.after, row.currency);
        notice.deltaText = FormatStockMoveDelta(row.before, row.after);
        notice.direction = row.after > row.before ? 1 : -1;
        return;
    }

    auto text = FormatBackgroundStockMoves(notice.rows, 2048);
    notice.title = "[Market moves] " + std::to_string(notice.rows.size()) + " stocks";
    notice.priceText = std::move(text.message);
    notice.deltaText.clear();
    notice.direction = 0;
}

void PublishForegroundStockMove(AppState& state,
                                const char* ticker,
                                double before,
                                double after,
                                std::string_view currency) {
    if (!UseForegroundNotificationBlocks() ||
        !ShouldRenderForegroundNotification(
            ApplicationRuntime().CurrentUiMode(),
            ForegroundNotificationKind::MarketMove))
        return;
    if (!ticker || !*ticker || !std::isfinite(before) || before <= 0.0 ||
        !std::isfinite(after) || after <= 0.0 || std::abs(after - before) <= 1e-12)
        return;

    const auto now = std::chrono::steady_clock::now();
    auto& moves = state.render.notifications.marketMoves;
    squarestar::application::StockMoveNotification row;
    row.ticker = ticker;
    row.before = before;
    row.after = after;
    row.currency = std::string(currency);

    const bool withinGroupingWindow =
        state.config.marketMoveBatching && !moves.notices.empty() &&
        moves.lastQueuedAt != std::chrono::steady_clock::time_point{} &&
        now - moves.lastQueuedAt <= squarestar::application::kNotificationGroupingWindow;
    bool mergedIntoLatestBlock = false;
    if (withinGroupingWindow) {
        auto& latest = moves.notices.back();
        auto existing = std::find_if(latest.rows.begin(), latest.rows.end(), [&](const auto& item) {
            return item.ticker == row.ticker;
        });
        if (existing != latest.rows.end()) {


            if (!std::isfinite(existing->before) || existing->before <= 0.0)
                existing->before = row.before;
            existing->after = row.after;
            if (!row.currency.empty())
                existing->currency = std::move(row.currency);
            mergedIntoLatestBlock = true;
        } else if (latest.rows.size() < squarestar::application::kNotificationMaxGroupedRows) {
            latest.rows.push_back(std::move(row));
            mergedIntoLatestBlock = true;
        }
        if (mergedIntoLatestBlock) {


            latest.RefreshVisibleHold(now);
            RefreshForegroundMarketMoveNoticeText(latest);
        }
    }

    if (!mergedIntoLatestBlock) {
        while (moves.notices.size() >= squarestar::application::kNotificationMaxVisibleBlocks)
            moves.notices.pop_front();

        squarestar::application::MarketMoveNotice notice;
        notice.rows.push_back(std::move(row));


        notice.animation = state.UiAnimationsEnabled() ? 0.0f : 1.0f;
        notice.serial = ++moves.nextSerial;
        RefreshForegroundMarketMoveNoticeText(notice);
        moves.notices.push_back(std::move(notice));
    }

    moves.lastQueuedAt = now;
    RequestGuiRedraw();
}
void QueueTrayPriceMove(const AppState& state,
                        const char* ticker,
                        double before,
                        double after,
                        bool priceAlert,
                        double alertThreshold,
                        std::string_view currency) {
    const bool hasPriceMove = std::isfinite(before) && before > 0.0 &&
                              std::isfinite(after) && after > 0.0 &&
                              std::abs(after - before) > 1e-12;
    const bool hasAlertMatch = priceAlert && std::isfinite(after) && after > 0.0 &&
                               std::isfinite(alertThreshold) && alertThreshold > 0.0;
    if (!ticker || !*ticker || (!hasPriceMove && !hasAlertMatch))
        return;

    double notificationBefore = before;
    double notificationAfter = after;
    double notificationThreshold = hasAlertMatch ? alertThreshold : 0.0;
    std::string notificationCurrency(currency);
    if (currency == "USD") {
        double convertedAfter = 0.0;
        double convertedBefore = 0.0;
        double convertedThreshold = 0.0;
        const bool afterReady =
            TryConvertUsdForDisplay(state, after, convertedAfter);
        const bool beforeReady =
            !std::isfinite(before) || before <= 0.0 ||
            TryConvertUsdForDisplay(state, before, convertedBefore);
        const bool thresholdReady =
            !hasAlertMatch ||
            TryConvertUsdForDisplay(state, alertThreshold, convertedThreshold);
        if (afterReady && beforeReady && thresholdReady) {
            notificationAfter = convertedAfter;
            if (std::isfinite(before) && before > 0.0)
                notificationBefore = convertedBefore;
            if (hasAlertMatch)
                notificationThreshold = convertedThreshold;
            notificationCurrency = std::string(DisplayCurrencyCode(state));
        }
    }
    NotificationChannel().QueuePriceMove(
        {ticker,
         notificationBefore,
         notificationAfter,
         priceAlert,
         notificationThreshold,
         std::move(notificationCurrency)},
        state.config.marketMoveBatching);
}
void FlushTrayPriceMoves() {
    for (NativeNotificationRequest& request :
         NotificationChannel().TakeReadyPriceMoveNotifications(
             UseBackgroundNotificationBlock())) {
        EnqueueNativeNotification(std::move(request));
    }
}
double SecondsUntilPendingTrayPriceMoveFlush() {
    return NotificationChannel().SecondsUntilPriceMoveFlush();
}

}
