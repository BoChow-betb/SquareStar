#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#endif

namespace squarestar::platform {

#ifdef _WIN32
struct NativeNotificationStyle {
    COLORREF background = RGB(14, 14, 15);
    COLORREF border = RGB(87, 87, 92);
    COLORREF title = RGB(245, 245, 247);
    COLORREF body = RGB(179, 179, 186);
    COLORREF positive = RGB(89, 184, 140);
    COLORREF negative = RGB(219, 110, 115);
};
#endif

class Win32AppRuntimeState final {
  public:
#ifdef _WIN32
    [[nodiscard]] HWND MainWindow() const noexcept;
    void SetMainWindow(HWND window) noexcept;
    [[nodiscard]] WNDPROC OriginalWindowProc() const noexcept;
    void SetOriginalWindowProc(WNDPROC windowProc) noexcept;
    void ClearWindowBinding() noexcept;

    void ConfigureTrayIcon(HWND window, HICON icon, UINT callbackMessage);
    [[nodiscard]] bool EnsureTrayIconVisible();
    void RemoveTrayIcon();
    [[nodiscard]] bool TrayIconVisible() const noexcept;
    [[nodiscard]] bool HasTrayNotificationTarget() const noexcept;
    void SetTrayTooltip(const char* tooltip);
    void PostTrayMessage(UINT message) const noexcept;
    void SetNotificationStyle(const NativeNotificationStyle& style) noexcept;
    [[nodiscard]] bool ShowNotificationToast(const char* title,
                                             const char* message,
                                             const char* accentText = nullptr,
                                             int accentDirection = 0,
                                             std::uint64_t groupId = 0,
                                             const char* actionPath = nullptr,
                                             const char* actionTicker = nullptr);
#endif

    [[nodiscard]] bool MinimizedToTray() const noexcept;
    void SetMinimizedToTray(bool minimized) noexcept;
    [[nodiscard]] bool TaskbarMinimized() const noexcept;
    void SetTaskbarMinimized(bool minimized) noexcept;
    [[nodiscard]] bool MinimizedForNotifications() const noexcept;

  private:
#ifdef _WIN32
    struct NotificationCardState {
        HWND window = nullptr;
        std::wstring title;
        std::wstring message;
        std::wstring accentText;
        int accentDirection = 0;
        std::uint64_t groupId = 0;
        std::string actionPath;
        std::string actionTicker;
        ULONGLONG startedAt = 0;
        UINT dpi = 0;
        int width = 0;
        int height = 0;
        int targetX = 0;
        int targetY = 0;
        int travel = 0;
    };

    static constexpr std::size_t kNotificationCardCapacity = 5;

    NOTIFYICONDATAA notificationIcon_{};
    HWND mainWindow_ = nullptr;
    WNDPROC originalWindowProc_ = nullptr;
    NativeNotificationStyle notificationStyle_{};
    std::array<NotificationCardState, kNotificationCardCapacity> notificationCards_{};
    std::array<std::size_t, kNotificationCardCapacity> notificationOrder_{};
    std::size_t notificationOrderCount_ = 0;
    HFONT notificationTitleFont_ = nullptr;
    HFONT notificationBodyFont_ = nullptr;
    UINT notificationFontDpi_ = 0;
    HANDLE notificationRegularFontResource_ = nullptr;
    HANDLE notificationMediumFontResource_ = nullptr;

    [[nodiscard]] bool EnsureNotificationWindow(NotificationCardState& card);
    [[nodiscard]] NotificationCardState* FindNotificationCard(HWND window) noexcept;
    [[nodiscard]] const NotificationCardState* FindNotificationCard(HWND window) const noexcept;
    void UpdateNotificationWindow(HWND window);
    void PaintNotificationWindow(HWND window);
    void ReleaseNotificationFonts() noexcept;
    void EnsureNotificationFonts(UINT dpi);
    [[nodiscard]] bool RefreshNotificationLayout(NotificationCardState& card, UINT dpi);
    void ReflowNotificationStack();
    void RemoveNotificationCardFromOrder(HWND window) noexcept;
    [[nodiscard]] int MeasureNotificationHeight(const NotificationCardState& card,
                                                UINT dpi) const;
    static LRESULT CALLBACK NotificationWindowProc(HWND window,
                                                   UINT message,
                                                   WPARAM wParam,
                                                   LPARAM lParam);
    LRESULT HandleNotificationWindowMessage(HWND window,
                                            UINT message,
                                            WPARAM wParam,
                                            LPARAM lParam);
#endif
    bool minimizedToTray_ = false;
    bool trayIconVisible_ = false;
    bool taskbarMinimized_ = false;
};

Win32AppRuntimeState& Win32AppRuntime() noexcept;

int ReportConfigPersistenceFailure();

}
