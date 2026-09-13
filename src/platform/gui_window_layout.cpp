#include "platform/gui_window_layout.hpp"
#include "platform/window_centering.hpp"

#ifdef _WIN32
#ifndef GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <windows.h>
#include <dwmapi.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <algorithm>
#endif

namespace squarestar::platform {

#ifdef _WIN32
namespace {

HMONITOR ResolveCenteringMonitor(HWND hwnd) noexcept {
    if (hwnd && IsWindowVisible(hwnd))
        return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    POINT cursor{};
    if (GetCursorPos(&cursor)) {
        if (HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL))
            return monitor;
    }

    if (HWND foreground = GetForegroundWindow()) {
        if (HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL))
            return monitor;
    }
    return hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
}

} // namespace
#endif

void CenterGlfwWindowInWorkArea(GLFWwindow* window, int width, int height) {
#ifdef _WIN32
    if (!window)
        return;

    const HWND hwnd = glfwGetWin32Window(window);
    const HMONITOR monitor = ResolveCenteringMonitor(hwnd);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!hwnd || !monitor || !GetMonitorInfoW(monitor, &monitorInfo))
        return;

    RECT placementRect{0, 0, std::max(1, width), std::max(1, height)};
    (void)GetWindowRect(hwnd, &placementRect);
    RECT visualRect = placementRect;
    // DWM can add invisible resize borders outside the visible frame. Center
    // the visible frame while still moving the Win32 placement rectangle.
    (void)DwmGetWindowAttribute(hwnd,
                                DWMWA_EXTENDED_FRAME_BOUNDS,
                                &visualRect,
                                sizeof(visualRect));
    const RECT& work = monitorInfo.rcWork;
    const NativeWindowPosition centered = CenterVisualWindowBounds(
        NativeWindowBounds{placementRect.left,
                           placementRect.top,
                           placementRect.right,
                           placementRect.bottom},
        NativeWindowBounds{visualRect.left,
                           visualRect.top,
                           visualRect.right,
                           visualRect.bottom},
        NativeWindowBounds{work.left, work.top, work.right, work.bottom});

    SetWindowPos(hwnd,
                 nullptr,
                 centered.x,
                 centered.y,
                 0,
                 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#else
    (void)window;
    (void)width;
    (void)height;
#endif
}

void ApplyFixedGlfwWindowLayout(GLFWwindow* window, int width, int height) {
#ifdef _WIN32
    if (!window)
        return;
    if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED))
        glfwRestoreWindow(window);
    glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_FALSE);
    glfwSetWindowSizeLimits(window, width, height, width, height);
    glfwSetWindowSize(window, width, height);
    CenterGlfwWindowInWorkArea(window, width, height);
#else
    (void)window;
    (void)width;
    (void)height;
#endif
}

void ApplyResizableGlfwWindowLayout(GLFWwindow* window,
                                    int width,
                                    int height,
                                    int minimumWidth,
                                    int minimumHeight) {
#ifdef _WIN32
    if (!window)
        return;
    if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED))
        glfwRestoreWindow(window);
    glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_TRUE);
    glfwSetWindowSizeLimits(window,
                            minimumWidth,
                            minimumHeight,
                            GLFW_DONT_CARE,
                            GLFW_DONT_CARE);
    glfwSetWindowSize(window, width, height);
    CenterGlfwWindowInWorkArea(window, width, height);
#else
    (void)window;
    (void)width;
    (void)height;
    (void)minimumWidth;
    (void)minimumHeight;
#endif
}

} // namespace squarestar::platform
