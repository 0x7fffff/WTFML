/**
 * @file chrome_native.cpp
 * @brief 真 HWND 上的几件量：WM_NCHITTEST、标题栏高度、原生子窗口数（见 chrome_native.hpp）。
 */

#include "chrome_native.hpp"

#include <windows.h>   // 必须在 Qt 头之前

#include <QGuiApplication>
#include <QWidget>
#include <QWindow>

namespace {
bool realPlatform() { return QGuiApplication::platformName() == QLatin1String("windows"); }

HWND hwndOf(QWidget *window) {
    if (window == nullptr || !realPlatform() || window->windowHandle() == nullptr) return nullptr;
    return reinterpret_cast<HWND>(window->winId());
}

POINT screenPoint(QWidget *window, const QPoint &local) {
    const qreal dpr = window->windowHandle()->devicePixelRatio();
    const QPoint global = window->mapToGlobal(local);
    return POINT{qRound(global.x() * dpr), qRound(global.y() * dpr)};
}
}  // namespace

bool chromeNativeAvailable() { return realPlatform(); }

int chromeNativeHitTest(QWidget *window, const QPoint &local) {
    const HWND hwnd = hwndOf(window);
    if (hwnd == nullptr) return -1;
    const POINT p = screenPoint(window, local);
    return int(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y)));
}

int chromeNativeCaptionHeight(QWidget *window) {
    const HWND hwnd = hwndOf(window);
    if (hwnd == nullptr) return -1;
    POINT origin{0, 0};
    RECT wr{};
    ClientToScreen(hwnd, &origin);
    GetWindowRect(hwnd, &wr);
    return int(origin.y - wr.top);
}

int chromeNativeChildCount(QWidget *window) {
    const HWND hwnd = hwndOf(window);
    if (hwnd == nullptr) return -1;
    int count = 0;
    EnumChildWindows(
        hwnd, [](HWND, LPARAM l) -> BOOL { ++*reinterpret_cast<int *>(l); return TRUE; }, reinterpret_cast<LPARAM>(&count));
    return count;
}

int chromeNativeMouseReachesWindow(QWidget *window, const QPoint &local) {
    const HWND hwnd = hwndOf(window);
    if (hwnd == nullptr) return -1;
    // ChildWindowFromPointEx 只看这个窗口的直接子窗口，不受别的程序盖在上面的影响。
    POINT p = screenPoint(window, local);
    ScreenToClient(hwnd, &p);
    return ChildWindowFromPointEx(hwnd, p, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT) == hwnd ? 1 : 0;
}

int chromeHtCaption() { return HTCAPTION; }
int chromeHtClient() { return HTCLIENT; }
