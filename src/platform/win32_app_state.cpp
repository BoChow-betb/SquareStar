#include "platform/win32_app_state.hpp"

#include "application/native_notification_timing.hpp"
#include "application/notification_text.hpp"
#include "application/runtime_state.hpp"
#include "platform/embedded_resource.hpp"
#include "platform/windows_path.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace squarestar::platform {

#ifdef _WIN32
namespace {

constexpr UINT_PTR kNotificationTimerId = 1;
constexpr UINT kNotificationAnimationTimerMs = 16;
constexpr ULONGLONG kNotificationEnterMs = 220;
constexpr ULONGLONG kNotificationHoldMs = 5000;
constexpr ULONGLONG kNotificationExitMs = 240;

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8,
                                            MB_ERR_INVALID_CHARS,
                                            text.data(),
                                            static_cast<int>(text.size()),
                                            nullptr,
                                            0);
    if (length <= 0)
        return std::wstring(text.begin(), text.end());
    std::wstring output(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8,
                        MB_ERR_INVALID_CHARS,
                        text.data(),
                        static_cast<int>(text.size()),
                        output.data(),
                        length);
    return output;
}

HFONT CreateNotificationFont(UINT dpi,
                             int logicalPixelSize,
                             int weight,
                             const wchar_t* family) {
    return CreateFontW(-MulDiv(logicalPixelSize, static_cast<int>(dpi), 96),
                       0,
                       0,
                       0,
                       weight,
                       FALSE,
                       FALSE,
                       FALSE,
                       DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE,
                       family && *family ? family : L"Segoe UI");
}

HANDLE RegisterEmbeddedNotificationFont(std::string_view resourceName) {
    const auto resource = squarestar::platform::FindEmbeddedResource(resourceName);
    if (!resource)
        return nullptr;
    DWORD fontCount = 0;
    return AddFontMemResourceEx(
        const_cast<unsigned char*>(resource.data),
        static_cast<DWORD>(resource.size),
        nullptr,
        &fontCount);
}

} // namespace

HWND Win32AppRuntimeState::MainWindow() const noexcept {
    return mainWindow_;
}

void Win32AppRuntimeState::SetMainWindow(HWND window) noexcept {
    mainWindow_ = window;
}

WNDPROC Win32AppRuntimeState::OriginalWindowProc() const noexcept {
    return originalWindowProc_;
}

void Win32AppRuntimeState::SetOriginalWindowProc(WNDPROC windowProc) noexcept {
    originalWindowProc_ = windowProc;
}

void Win32AppRuntimeState::ClearWindowBinding() noexcept {
    notificationOrderCount_ = 0;
    for (auto& card : notificationCards_) {
        if (card.window) {
            KillTimer(card.window, kNotificationTimerId);
            DestroyWindow(card.window);
        }
        card = {};
    }
    ReleaseNotificationFonts();
    mainWindow_ = nullptr;
    originalWindowProc_ = nullptr;
}

void Win32AppRuntimeState::ConfigureTrayIcon(HWND window,
                                            HICON icon,
                                            UINT callbackMessage) {
    notificationIcon_ = {};
    notificationIcon_.cbSize = sizeof(notificationIcon_);
    notificationIcon_.hWnd = window;
    notificationIcon_.uID = 1001;
    notificationIcon_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    notificationIcon_.uCallbackMessage = callbackMessage;
    notificationIcon_.hIcon = icon;
    strcpy_s(notificationIcon_.szTip, "SquareStar alerts");
    trayIconVisible_ = false;
    taskbarMinimized_ = false;
}

bool Win32AppRuntimeState::EnsureTrayIconVisible() {
    if (trayIconVisible_)
        return true;
    if (!notificationIcon_.hWnd || !Shell_NotifyIconA(NIM_ADD, &notificationIcon_))
        return false;
    notificationIcon_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconA(NIM_SETVERSION, &notificationIcon_);
    trayIconVisible_ = true;
    return true;
}

void Win32AppRuntimeState::RemoveTrayIcon() {
    if (trayIconVisible_)
        Shell_NotifyIconA(NIM_DELETE, &notificationIcon_);
    trayIconVisible_ = false;
    notificationOrderCount_ = 0;
    for (auto& card : notificationCards_) {
        if (!card.window)
            continue;
        KillTimer(card.window, kNotificationTimerId);
        ShowWindow(card.window, SW_HIDE);
        card.title.clear();
        card.message.clear();
        card.accentText.clear();
        card.accentDirection = 0;
        card.groupId = 0;
        card.actionPath.clear();
        card.actionTicker.clear();
    }
}

bool Win32AppRuntimeState::TrayIconVisible() const noexcept {
    return trayIconVisible_;
}

bool Win32AppRuntimeState::HasTrayNotificationTarget() const noexcept {
    return notificationIcon_.hWnd != nullptr;
}

void Win32AppRuntimeState::SetTrayTooltip(const char* tooltip) {
    strcpy_s(notificationIcon_.szTip, tooltip ? tooltip : "");
}

void Win32AppRuntimeState::PostTrayMessage(UINT message) const noexcept {
    if (notificationIcon_.hWnd)
        PostMessageA(notificationIcon_.hWnd, message, 0, 0);
}

void Win32AppRuntimeState::SetNotificationStyle(
    const NativeNotificationStyle& style) noexcept {
    notificationStyle_ = style;
    for (const auto& card : notificationCards_) {
        if (card.window)
            InvalidateRect(card.window, nullptr, FALSE);
    }
}

bool Win32AppRuntimeState::EnsureNotificationWindow(NotificationCardState& card) {
    if (card.window)
        return true;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    constexpr wchar_t windowClassName[] = L"SquareStar.NotificationCard";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    if (!GetClassInfoExW(instance, windowClassName, &windowClass)) {
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &Win32AppRuntimeState::NotificationWindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
        windowClass.lpszClassName = windowClassName;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }
    card.window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        windowClassName,
        L"SquareStar notification",
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance,
        this);
    if (!card.window)
        return false;
    const DWORD roundPreference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(card.window, 33, &roundPreference, sizeof(roundPreference));
    SetLayeredWindowAttributes(card.window, 0, 0, LWA_ALPHA);
    return true;
}

Win32AppRuntimeState::NotificationCardState*
Win32AppRuntimeState::FindNotificationCard(HWND window) noexcept {
    for (auto& card : notificationCards_) {
        if (card.window == window)
            return &card;
    }
    return nullptr;
}

const Win32AppRuntimeState::NotificationCardState*
Win32AppRuntimeState::FindNotificationCard(HWND window) const noexcept {
    for (const auto& card : notificationCards_) {
        if (card.window == window)
            return &card;
    }
    return nullptr;
}

void Win32AppRuntimeState::ReleaseNotificationFonts() noexcept {
    if (notificationTitleFont_)
        DeleteObject(notificationTitleFont_);
    if (notificationBodyFont_)
        DeleteObject(notificationBodyFont_);
    notificationTitleFont_ = nullptr;
    notificationBodyFont_ = nullptr;
    notificationFontDpi_ = 0;
    if (notificationRegularFontResource_)
        RemoveFontMemResourceEx(notificationRegularFontResource_);
    if (notificationMediumFontResource_)
        RemoveFontMemResourceEx(notificationMediumFontResource_);
    notificationRegularFontResource_ = nullptr;
    notificationMediumFontResource_ = nullptr;
}

void Win32AppRuntimeState::EnsureNotificationFonts(UINT dpi) {
    if (notificationFontDpi_ == dpi && notificationTitleFont_ && notificationBodyFont_)
        return;

    if (notificationTitleFont_)
        DeleteObject(notificationTitleFont_);
    if (notificationBodyFont_)
        DeleteObject(notificationBodyFont_);
    notificationTitleFont_ = nullptr;
    notificationBodyFont_ = nullptr;

    if (!notificationRegularFontResource_)
        notificationRegularFontResource_ = RegisterEmbeddedNotificationFont("Outfit-Regular.ttf");
    if (!notificationMediumFontResource_)
        notificationMediumFontResource_ = RegisterEmbeddedNotificationFont("Outfit-Medium.ttf");
    const wchar_t* family =
        notificationRegularFontResource_ || notificationMediumFontResource_ ? L"Outfit"
                                                                            : L"Segoe UI";
    // Match the in-app card: fontData (26 px, medium) for the title and
    // fontNormal (21 px, regular) for the body at 100% interface scale.
    notificationTitleFont_ = CreateNotificationFont(dpi, 26, FW_MEDIUM, family);
    notificationBodyFont_ = CreateNotificationFont(dpi, 21, FW_NORMAL, family);
    notificationFontDpi_ = dpi;
}

bool Win32AppRuntimeState::RefreshNotificationLayout(NotificationCardState& card,
                                                      UINT dpi) {
    if (!card.window)
        return false;
    EnsureNotificationFonts(dpi);
    card.dpi = dpi;
    card.width = MulDiv(410, static_cast<int>(dpi), 96);
    card.height = MeasureNotificationHeight(card, dpi);

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = MonitorFromWindow(
        mainWindow_ ? mainWindow_ : card.window, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &monitorInfo))
        return false;

    const int margin = MulDiv(16, static_cast<int>(dpi), 96);
    card.travel = MulDiv(56, static_cast<int>(dpi), 96);
    card.targetX = monitorInfo.rcWork.right - card.width - margin;

    HRGN region = CreateRoundRectRgn(0,
                                     0,
                                     card.width + 1,
                                     card.height + 1,
                                     MulDiv(20, static_cast<int>(dpi), 96),
                                     MulDiv(20, static_cast<int>(dpi), 96));
    if (region && !SetWindowRgn(card.window, region, FALSE))
        DeleteObject(region);
    InvalidateRect(card.window, nullptr, FALSE);
    return true;
}

void Win32AppRuntimeState::ReflowNotificationStack() {
    if (notificationOrderCount_ == 0)
        return;

    HWND anchor = mainWindow_;
    if (!anchor) {
        for (std::size_t position = 0; position < notificationOrderCount_; ++position) {
            const auto index = notificationOrder_[position];
            if (index < notificationCards_.size() && notificationCards_[index].window) {
                anchor = notificationCards_[index].window;
                break;
            }
        }
    }
    if (!anchor)
        return;

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = MonitorFromWindow(anchor, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &monitorInfo))
        return;
    const UINT dpi = std::max<UINT>(96, GetDpiForWindow(anchor));
    const int margin = MulDiv(16, static_cast<int>(dpi), 96);
    const int spacing = MulDiv(12, static_cast<int>(dpi), 96);
    int bottomOffset = margin;

    for (std::size_t position = 0; position < notificationOrderCount_; ++position) {
        const auto index = notificationOrder_[position];
        if (index >= notificationCards_.size())
            continue;
        auto& card = notificationCards_[index];
        if (!card.window)
            continue;
        if (card.dpi != dpi || card.width <= 0 || card.height <= 0) {
            if (!RefreshNotificationLayout(card, dpi))
                continue;
        }
        card.targetX = monitorInfo.rcWork.right - card.width - margin;
        card.targetY = monitorInfo.rcWork.bottom - card.height - bottomOffset;
        bottomOffset += card.height + spacing;

        RECT current{};
        int currentX = card.targetX + card.travel;
        if (IsWindowVisible(card.window) && GetWindowRect(card.window, &current))
            currentX = current.left;
        SetWindowPos(card.window,
                     HWND_TOPMOST,
                     currentX,
                     card.targetY,
                     card.width,
                     card.height,
                     SWP_NOACTIVATE);
    }
}

void Win32AppRuntimeState::RemoveNotificationCardFromOrder(HWND window) noexcept {
    for (std::size_t position = 0; position < notificationOrderCount_; ++position) {
        const auto index = notificationOrder_[position];
        if (index >= notificationCards_.size() || notificationCards_[index].window != window)
            continue;
        for (std::size_t next = position + 1; next < notificationOrderCount_; ++next)
            notificationOrder_[next - 1] = notificationOrder_[next];
        --notificationOrderCount_;
        return;
    }
}

int Win32AppRuntimeState::MeasureNotificationHeight(const NotificationCardState& card,
                                                    UINT dpi) const {
    const int width = MulDiv(410, static_cast<int>(dpi), 96);
    const int horizontalPadding = MulDiv(18, static_cast<int>(dpi), 96);
    const int contentWidth = std::max(1, width - horizontalPadding * 2);
    HDC screen = GetDC(card.window);
    if (!screen)
        return MulDiv(96, static_cast<int>(dpi), 96);

    HGDIOBJ previous = SelectObject(screen, notificationTitleFont_);
    TEXTMETRICW titleMetrics{};
    GetTextMetricsW(screen, &titleMetrics);
    SelectObject(screen, notificationBodyFont_);
    TEXTMETRICW bodyMetrics{};
    GetTextMetricsW(screen, &bodyMetrics);

    int bodyHeight = bodyMetrics.tmHeight;
    const bool hasAccent = !card.accentText.empty() && card.accentDirection != 0;
    if (hasAccent) {
        SIZE bodySize{};
        SIZE accentSize{};
        GetTextExtentPoint32W(screen,
                              card.message.c_str(),
                              static_cast<int>(card.message.size()),
                              &bodySize);
        GetTextExtentPoint32W(screen,
                              card.accentText.c_str(),
                              static_cast<int>(card.accentText.size()),
                              &accentSize);
        const int gap = MulDiv(10, static_cast<int>(dpi), 96);
        const bool stackAccent = bodySize.cx + gap + accentSize.cx > contentWidth;
        bodyHeight = stackAccent
                         ? bodySize.cy + MulDiv(3, static_cast<int>(dpi), 96) + accentSize.cy
                         : std::max(bodySize.cy, accentSize.cy);
    } else {
        RECT bodyBounds{0, 0, contentWidth, 0};
        DrawTextW(screen,
                  card.message.c_str(),
                  static_cast<int>(card.message.size()),
                  &bodyBounds,
                  DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        bodyHeight = std::max(bodyHeight, static_cast<int>(bodyBounds.bottom));
    }

    SelectObject(screen, previous);
    ReleaseDC(card.window, screen);
    const int verticalPadding = MulDiv(14, static_cast<int>(dpi), 96);
    const int titleGap = MulDiv(8, static_cast<int>(dpi), 96);
    const int naturalHeight = verticalPadding * 2 + titleMetrics.tmHeight + titleGap + bodyHeight;
    return std::clamp(naturalHeight,
                      MulDiv(84, static_cast<int>(dpi), 96),
                      MulDiv(260, static_cast<int>(dpi), 96));
}

bool Win32AppRuntimeState::ShowNotificationToast(const char* title,
                                                 const char* message,
                                                 const char* accentText,
                                                 int accentDirection,
                                                 std::uint64_t groupId,
                                                 const char* actionPath,
                                                 const char* actionTicker) {
    static_assert(kNotificationCardCapacity ==
                  squarestar::application::kNotificationMaxVisibleBlocks);

    const auto applyContent = [&](NotificationCardState& card) {
        card.title = Utf8ToWide(title && *title ? title : "SquareStar");
        card.message = Utf8ToWide(message ? message : "");
        card.accentText = Utf8ToWide(accentText ? accentText : "");
        card.accentDirection = accentDirection > 0 ? 1 : accentDirection < 0 ? -1 : 0;
        card.actionPath = actionPath ? actionPath : "";
        card.actionTicker = actionTicker ? actionTicker : "";
    };

    // Price-move batches carry a stable id. If another stock lands inside the
    // 1.5 s grouping window, update the already-visible card immediately rather
    // than creating a second card or delaying the first notification.
    if (groupId != 0) {
        for (std::size_t position = 0; position < notificationOrderCount_; ++position) {
            const auto existingIndex = notificationOrder_[position];
            if (existingIndex >= notificationCards_.size())
                continue;
            auto& existing = notificationCards_[existingIndex];
            if (!existing.window || existing.groupId != groupId)
                continue;
            applyContent(existing);
            const ULONGLONG now = GetTickCount64();
            const ULONGLONG elapsed = now - existing.startedAt;
            if (elapsed >= kNotificationEnterMs)
                existing.startedAt = now - kNotificationEnterMs;
            existing.dpi = 0;
            const HWND dpiWindow = mainWindow_ ? mainWindow_ : existing.window;
            const UINT dpi = std::max<UINT>(96, GetDpiForWindow(dpiWindow));
            if (!RefreshNotificationLayout(existing, dpi))
                return false;
            ReflowNotificationStack();
            InvalidateRect(existing.window, nullptr, FALSE);
            UpdateNotificationWindow(existing.window);
            return true;
        }
    }

    bool active[kNotificationCardCapacity]{};
    for (std::size_t position = 0; position < notificationOrderCount_; ++position) {
        const auto index = notificationOrder_[position];
        if (index < kNotificationCardCapacity)
            active[index] = true;
    }

    std::size_t index = kNotificationCardCapacity;
    if (notificationOrderCount_ >= kNotificationCardCapacity) {
        index = notificationOrder_[0];
        auto& oldest = notificationCards_[index];
        if (oldest.window) {
            KillTimer(oldest.window, kNotificationTimerId);
            ShowWindow(oldest.window, SW_HIDE);
        }
        for (std::size_t next = 1; next < notificationOrderCount_; ++next)
            notificationOrder_[next - 1] = notificationOrder_[next];
        --notificationOrderCount_;
    } else {
        for (std::size_t candidate = 0; candidate < kNotificationCardCapacity; ++candidate) {
            if (!active[candidate]) {
                index = candidate;
                break;
            }
        }
    }
    if (index >= kNotificationCardCapacity)
        return false;

    auto& card = notificationCards_[index];
    if (!EnsureNotificationWindow(card))
        return false;
    applyContent(card);
    card.groupId = groupId;
    card.startedAt = GetTickCount64();
    card.dpi = 0;

    const HWND dpiWindow = mainWindow_ ? mainWindow_ : card.window;
    const UINT dpi = std::max<UINT>(96, GetDpiForWindow(dpiWindow));
    if (!RefreshNotificationLayout(card, dpi))
        return false;

    notificationOrder_[notificationOrderCount_++] = index;
    ReflowNotificationStack();
    InvalidateRect(card.window, nullptr, FALSE);
    UpdateNotificationWindow(card.window);
    ShowWindow(card.window, SW_SHOWNOACTIVATE);
    return true;
}

void Win32AppRuntimeState::UpdateNotificationWindow(HWND window) {
    NotificationCardState* card = FindNotificationCard(window);
    if (!card || !card->window)
        return;
    const ULONGLONG elapsed = GetTickCount64() - card->startedAt;
    const squarestar::application::NativeNotificationTimeline timeline =
        squarestar::application::EvaluateNativeNotificationTimeline(
            elapsed,
            kNotificationEnterMs,
            kNotificationHoldMs,
            kNotificationExitMs,
            kNotificationAnimationTimerMs);
    if (timeline.phase == squarestar::application::NativeNotificationPhase::Complete) {
        KillTimer(card->window, kNotificationTimerId);
        ShowWindow(card->window, SW_HIDE);
        RemoveNotificationCardFromOrder(card->window);
        card->title.clear();
        card->message.clear();
        card->accentText.clear();
        card->accentDirection = 0;
        card->groupId = 0;
        card->actionPath.clear();
        card->actionTicker.clear();
        ReflowNotificationStack();
        return;
    }

    // Enter/exit receive 16 ms animation ticks. The 5 second static hold uses
    // one deadline wake, so up to five visible cards still add no hold-phase
    // polling/GDI loop.
    SetTimer(card->window, kNotificationTimerId, timeline.nextWakeMs, nullptr);

    const HWND dpiWindow = mainWindow_ ? mainWindow_ : card->window;
    const UINT dpi = std::max<UINT>(96, GetDpiForWindow(dpiWindow));
    if (dpi != card->dpi || card->width <= 0 || card->height <= 0) {
        if (!RefreshNotificationLayout(*card, dpi))
            return;
        ReflowNotificationStack();
    }
    const int x = card->targetX +
                  static_cast<int>(std::lround((1.0 - timeline.visibility) * card->travel));

    SetLayeredWindowAttributes(card->window,
                               0,
                               static_cast<BYTE>(std::lround(255.0 * timeline.visibility)),
                               LWA_ALPHA);
    SetWindowPos(card->window,
                 HWND_TOPMOST,
                 x,
                 card->targetY,
                 card->width,
                 card->height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Win32AppRuntimeState::PaintNotificationWindow(HWND window) {
    NotificationCardState* card = FindNotificationCard(window);
    if (!card || !card->window)
        return;
    PAINTSTRUCT paint{};
    HDC target = BeginPaint(card->window, &paint);
    RECT client{};
    GetClientRect(card->window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        EndPaint(card->window, &paint);
        return;
    }
    HDC buffer = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ previousBitmap = SelectObject(buffer, bitmap);
    const UINT dpi = std::max<UINT>(96, GetDpiForWindow(card->window));
    if (dpi != card->dpi) {
        (void)RefreshNotificationLayout(*card, dpi);
        ReflowNotificationStack();
    } else {
        EnsureNotificationFonts(dpi);
    }
    const int radius = MulDiv(10, static_cast<int>(dpi), 96);
    HBRUSH background = CreateSolidBrush(notificationStyle_.background);
    HPEN border = CreatePen(PS_SOLID,
                            std::max(1, MulDiv(1, static_cast<int>(dpi), 96)),
                            notificationStyle_.border);
    HGDIOBJ previousBrush = SelectObject(buffer, background);
    HGDIOBJ previousPen = SelectObject(buffer, border);
    RoundRect(buffer, 0, 0, width, height, radius * 2, radius * 2);
    SetBkMode(buffer, TRANSPARENT);

    const int horizontalPadding = MulDiv(18, static_cast<int>(dpi), 96);
    const int verticalPadding = MulDiv(14, static_cast<int>(dpi), 96);
    const int contentWidth = std::max(1, width - horizontalPadding * 2);
    HGDIOBJ previousFont = SelectObject(buffer, notificationTitleFont_);
    SetTextColor(buffer, notificationStyle_.title);
    RECT titleRect{horizontalPadding,
                   verticalPadding,
                   width - horizontalPadding,
                   height - verticalPadding};
    DrawTextW(buffer,
              card->title.c_str(),
              static_cast<int>(card->title.size()),
              &titleRect,
              DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    TEXTMETRICW titleMetrics{};
    GetTextMetricsW(buffer, &titleMetrics);

    SelectObject(buffer, notificationBodyFont_);
    SetTextColor(buffer, notificationStyle_.body);
    const int bodyTop = verticalPadding + titleMetrics.tmHeight +
                        MulDiv(8, static_cast<int>(dpi), 96);
    const bool hasAccent = !card->accentText.empty() && card->accentDirection != 0;
    if (hasAccent) {
        SIZE bodySize{};
        SIZE accentSize{};
        GetTextExtentPoint32W(buffer,
                              card->message.c_str(),
                              static_cast<int>(card->message.size()),
                              &bodySize);
        GetTextExtentPoint32W(buffer,
                              card->accentText.c_str(),
                              static_cast<int>(card->accentText.size()),
                              &accentSize);
        const int gap = MulDiv(10, static_cast<int>(dpi), 96);
        const bool stackAccent = bodySize.cx + gap + accentSize.cx > contentWidth;
        RECT bodyRect{horizontalPadding,
                      bodyTop,
                      width - horizontalPadding,
                      height - verticalPadding};
        DrawTextW(buffer,
                  card->message.c_str(),
                  static_cast<int>(card->message.size()),
                  &bodyRect,
                  DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(buffer,
                     card->accentDirection > 0 ? notificationStyle_.positive
                                               : notificationStyle_.negative);
        const int accentX = stackAccent ? horizontalPadding
                                        : horizontalPadding + bodySize.cx + gap;
        const int accentY = stackAccent
                                ? bodyTop + bodySize.cy +
                                      MulDiv(3, static_cast<int>(dpi), 96)
                                : bodyTop;
        RECT accentRect{accentX,
                        accentY,
                        width - horizontalPadding,
                        height - verticalPadding};
        DrawTextW(buffer,
                  card->accentText.c_str(),
                  static_cast<int>(card->accentText.size()),
                  &accentRect,
                  DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        RECT bodyRect{horizontalPadding,
                      bodyTop,
                      width - horizontalPadding,
                      height - verticalPadding};
        DrawTextW(buffer,
                  card->message.c_str(),
                  static_cast<int>(card->message.size()),
                  &bodyRect,
                  DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    BitBlt(target, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previousFont);
    SelectObject(buffer, previousPen);
    SelectObject(buffer, previousBrush);
    SelectObject(buffer, previousBitmap);
    DeleteObject(border);
    DeleteObject(background);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    EndPaint(card->window, &paint);
}

LRESULT CALLBACK Win32AppRuntimeState::NotificationWindowProc(HWND window,
                                                              UINT message,
                                                              WPARAM wParam,
                                                              LPARAM lParam) {
    Win32AppRuntimeState* state = reinterpret_cast<Win32AppRuntimeState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<Win32AppRuntimeState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    return state ? state->HandleNotificationWindowMessage(window, message, wParam, lParam)
                 : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Win32AppRuntimeState::HandleNotificationWindowMessage(HWND window,
                                                              UINT message,
                                                              WPARAM wParam,
                                                              LPARAM lParam) {
    (void)lParam;
    switch (message) {
    case WM_TIMER:
        if (wParam == kNotificationTimerId) {
            UpdateNotificationWindow(window);
            return 0;
        }
        break;
    case WM_PAINT:
        PaintNotificationWindow(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        if (auto* card = FindNotificationCard(window))
            card->dpi = 0;
        UpdateNotificationWindow(window);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        if (auto* card = FindNotificationCard(window);
            card && (!card->actionPath.empty() || !card->actionTicker.empty()) &&
                LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_LBUTTONUP:
        if (auto* card = FindNotificationCard(window)) {
            if (!card->actionPath.empty()) {
                (void)squarestar::platform::RevealFileInFolder(card->actionPath);
            } else if (!card->actionTicker.empty()) {
                squarestar::application::ApplicationRuntime().RequestNotificationStockOpen(
                    card->actionTicker);
                if (mainWindow_)
                    PostMessageW(mainWindow_, WM_NULL, 0, 0);
            }
            card->startedAt = GetTickCount64() - (kNotificationEnterMs + kNotificationHoldMs);
            UpdateNotificationWindow(window);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(window, kNotificationTimerId);
        RemoveNotificationCardFromOrder(window);
        if (auto* card = FindNotificationCard(window))
            card->window = nullptr;
        ReflowNotificationStack();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
#endif

bool Win32AppRuntimeState::MinimizedToTray() const noexcept {
    return minimizedToTray_;
}

void Win32AppRuntimeState::SetMinimizedToTray(bool minimized) noexcept {
    minimizedToTray_ = minimized;
}

bool Win32AppRuntimeState::TaskbarMinimized() const noexcept {
    return taskbarMinimized_;
}

void Win32AppRuntimeState::SetTaskbarMinimized(bool minimized) noexcept {
    taskbarMinimized_ = minimized;
}

bool Win32AppRuntimeState::MinimizedForNotifications() const noexcept {
    return minimizedToTray_ || taskbarMinimized_;
}

Win32AppRuntimeState& Win32AppRuntime() noexcept {
    static Win32AppRuntimeState runtime;
    return runtime;
}

int ReportConfigPersistenceFailure() {
#ifdef _WIN32
    MessageBoxA(nullptr,
                "SquareStar could not persist one or more settings. Check that the "
                "portable data folder beside SquareStar.exe is writable and try again.",
                "SquareStar settings were not fully saved",
                MB_OK | MB_ICONERROR | MB_TOPMOST);
#endif
    return 1;
}

} // namespace squarestar::platform
