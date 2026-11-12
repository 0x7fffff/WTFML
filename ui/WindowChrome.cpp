/**
 * @file WindowChrome.cpp
 * @brief WindowChrome 的实现（Win32 / DWM 那一层）。
 */

#include "WindowChrome.hpp"

#include "Glass.hpp"
#include "Theme.hpp"
#include "WindowEdge.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <QtGlobal>

#ifdef Q_OS_WIN
#  include <QtGui/qpa/qplatformwindow_p.h>
#  include <dwmapi.h>
#  include <windows.h>
#  include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
// 手上这份 mingw 的 dwmapi.h 比 Win11 旧，这几个名字自己补（值取自 Windows SDK 的 dwmapi.h）。
#  ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#    define DWMWA_WINDOW_CORNER_PREFERENCE 33
#  endif
#  ifndef DWMWCP_ROUND
#    define DWMWCP_ROUND 2
#  endif
#  define WTFML_DWMWA_USE_IMMERSIVE_DARK_MODE 20
#  define WTFML_DWMWA_BORDER_COLOR 34
#  define WTFML_DWMWA_VISIBLE_FRAME_BORDER_THICKNESS 37
#  define WTFML_DWMWA_CAPTION_COLOR 35
#  define WTFML_DWMWA_SYSTEMBACKDROP_TYPE 38
#  define WTFML_DWMWA_COLOR_NONE 0xFFFFFFFEu
#endif

namespace {

#ifdef Q_OS_WIN
/// 缩放边（看不见的那圈 + 1px 可见边）有多宽，物理像素。按窗口所在那块屏的 DPI 取，
/// 和 AdjustWindowRectExForDpi 用同一个 DPI（混用系统 DPI 会在非 100% 屏上差 1px）。
int resizeBorderPhysical(UINT dpi) {
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

/// DWM 在左右下三边画的那条可见边有多厚（Win11 一般 1px；问不到按 1）。
int visibleBorderPhysical(HWND hwnd) {
    UINT thickness = 0;
    if (FAILED(DwmGetWindowAttribute(hwnd, WTFML_DWMWA_VISIBLE_FRAME_BORDER_THICKNESS, &thickness, sizeof(thickness))))
        return 1;
    return int(thickness);
}

/// 系统那一档缩放比（折物理 <-> 逻辑用）。用系统 DPI 而不是 QScreen：上面那两个度量是
/// 按系统 DPI 给的，验证工程跑 offscreen 时 QScreen 是个 1.0 的假屏。
qreal systemDpr() {
    HDC screen = GetDC(nullptr);
    if (screen == nullptr) return 1.0;
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    return dpi > 0 ? dpi / 96.0 : 1.0;
}

bool realWindowsPlatform() { return QGuiApplication::platformName() == QLatin1String("windows"); }
#endif

}  // namespace

WindowChrome::WindowChrome(QWidget *window, QWidget *strip, QObject *parent)
    : QObject(parent), m_window(window), m_strip(strip) {
    if (m_window == nullptr) return;
#ifdef Q_OS_WIN
    // 半透明必须在 HWND 出来之前说：系统背板要透过客户区露出来。纯色 / 风景底那几档整窗
    // 都画不透明像素，开着它也没有区别。
    if (realWindowsPlatform()) m_window->setAttribute(Qt::WA_TranslucentBackground, true);
#endif
    m_window->installEventFilter(this);
    // 换档、换深浅：重贴背板类型和深色标题区。
    Theme::follow(m_window, [this] { applyFrame(); });
}

void WindowChrome::setStrip(QWidget *strip) { m_strip = strip; }

void WindowChrome::addDragStrip(QWidget *strip) {
    if (strip != nullptr) m_extraStrips.append(strip);
}

int WindowChrome::resizeBorder() {
#ifdef Q_OS_WIN
    return qCeil(resizeBorderPhysical(GetDpiForSystem()) / systemDpr());
#else
    return 0;
#endif
}

void WindowChrome::applyFrame() {
#ifdef Q_OS_WIN
    // offscreen 平台给的 winId() 是个非空假句柄，整层只在真 windows 平台上做。
    if (m_window == nullptr || !realWindowsPlatform() || m_window->windowHandle() == nullptr) return;
    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (hwnd == nullptr) return;

    // 1) 标题栏收进客户区：顶上那一段非客户区（标题 + 上边框）给负的 customMargins。
    // 左右下三边：看不见的那圈缩放边留给系统（拖边缩放、贴靠白拿），但最外那 1px 可见边也收进
    // 客户区 —— 留在非客户区时 DWM 在那儿画一条没着色的背板（关背板时是一道透明缝），
    // 圆角也按它裁，和客户区错开 1px，浅色下左边、左下角看着像两层页面。
    // 最大化时那一档在 handleNativeEvent 的 WM_NCCALCSIZE 里自己算（见那边）。
    RECT frame{0, 0, 0, 0};
    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    const LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    AdjustWindowRectExForDpi(&frame, DWORD(style), FALSE, DWORD(exStyle), GetDpiForWindow(hwnd));
    const int side = WindowEdge::enabled() ? -visibleBorderPhysical(hwnd) : 0;
    const QMargins margins(side, int(frame.top), side, side);
    if (!m_marginsApplied || margins != m_appliedMargins) {
        if (auto *native = m_window->windowHandle()->nativeInterface<QNativeInterface::Private::QWindowsWindow>()) {
            native->setCustomMargins(margins);
            m_appliedMargins = margins;
            m_marginsApplied = true;
        }
    }

    // 2) 圆角、深浅、标题区不着色。
    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    const BOOL dark = Theme::tokens().page.lightness() < 128 ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, WTFML_DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const COLORREF none = WTFML_DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd, WTFML_DWMWA_CAPTION_COLOR, &none, sizeof(none));
    // 窗口边不要线：把系统那条 1px 硬边去掉（Win11 才认这一项，旧系统返回错误、忽略）；
    // WTFML_WINDOW_EDGE=0 时还给系统默认色；Win10 及以下不动系统边。
    if (WindowEdge::supported()) {
        const COLORREF border = WindowEdge::enabled() ? WTFML_DWMWA_COLOR_NONE : 0xFFFFFFFFu;  // DWMWA_COLOR_DEFAULT
        DwmSetWindowAttribute(hwnd, WTFML_DWMWA_BORDER_COLOR, &border, sizeof(border));
    }

    // 3) 系统背板（文件头"为什么只从底边往上扩"）。用不了的系统上这两句返回错误，忽略。
    const Glass::Mode mode = Glass::effectiveMode();
    const bool system = mode == Glass::Mode::Acrylic || mode == Glass::Mode::Mica;
    const MARGINS extend{0, 0, 0, system ? 10000 : 0};
    DwmExtendFrameIntoClientArea(hwnd, &extend);
    const int backdrop = mode == Glass::Mode::Acrylic ? 3 : (mode == Glass::Mode::Mica ? 2 : 1);
    DwmSetWindowAttribute(hwnd, WTFML_DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    m_window->update();
#endif
}

bool WindowChrome::handleNativeEvent(void *message, qintptr *result) {
#ifdef Q_OS_WIN
    if (m_window == nullptr || message == nullptr || result == nullptr) return false;
    MSG *msg = static_cast<MSG *>(message);
    switch (msg->message) {
    case WM_DPICHANGED:
        // 标题栏高度跟着 DPI 变，等 Qt 把这一条处理完再按新 DPI 重收一次。
        QTimer::singleShot(0, this, [this] { applyFrame(); });
        return false;
    case WM_NCCALCSIZE: {
        // 最大化：窗口矩形四边各伸出屏外一圈缩放边。按 customMargins 那套算，顶边会跟着标题栏一起收、
        // 落到屏幕上沿往上一圈缩放边的地方（内容最上面那几像素在屏外）；左右下给可见边的那 1px 也会出屏。
        // 这一档自己算：系统默认结果的左右下正好贴屏，顶边改成窗口上沿往下一圈缩放边。
        if (msg->wParam == FALSE || !IsZoomed(msg->hwnd)) return false;
        auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
        const LONG windowTop = params->rgrc[0].top;
        DefWindowProcW(msg->hwnd, msg->message, msg->wParam, msg->lParam);
        params->rgrc[0].top = windowTop + resizeBorderPhysical(GetDpiForWindow(msg->hwnd));
        *result = 0;
        return true;
    }
    case WM_NCHITTEST: {
        RECT wr{};
        if (GetWindowRect(msg->hwnd, &wr) == FALSE) return false;
        // 1) 上沿那一圈 = 缩放边（标题栏收进客户区之后，上边没有系统边框可拖了）。
        if (!IsZoomed(msg->hwnd)) {
            const int px = GET_X_LPARAM(msg->lParam) - wr.left;
            const int py = GET_Y_LPARAM(msg->lParam) - wr.top;
            const int band = resizeBorderPhysical(GetDpiForWindow(msg->hwnd));
            const int w = wr.right - wr.left;
            if (py >= 0 && py < band) {
                *result = px < band * 2 ? HTTOPLEFT : (px >= w - band * 2 ? HTTOPRIGHT : HTTOP);
                return true;
            }
        }
        // 2) 拖拽区的空白处 = HTCAPTION（系统拖窗、贴边吸附都白拿）；子控件那一格不认。
        const QPoint local = hitTestPoint(message);
        QList<QWidget *> strips;
        if (m_strip != nullptr) strips.append(m_strip);
        for (const QPointer<QWidget> &s : std::as_const(m_extraStrips)) {
            if (s != nullptr) strips.append(s.data());
        }
        for (QWidget *strip : std::as_const(strips)) {
            if (!strip->isVisible()) continue;
            const QRect bar(strip->mapTo(m_window, QPoint(0, 0)), strip->size());
            if (!bar.contains(local)) continue;
            if (strip->childAt(local - bar.topLeft()) != nullptr) return false;
            *result = HTCAPTION;
            return true;
        }
        return false;
    }
    default:
        break;
    }
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}

bool WindowChrome::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_window) {
        switch (event->type()) {
        // 第一次 show 之后 HWND 才在；改窗口标志会重建 HWND。都幂等。
        case QEvent::Show:
        case QEvent::WinIdChange:
            m_marginsApplied = false;
            applyFrame();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

QPoint WindowChrome::hitTestPoint(void *message) const {
#ifdef Q_OS_WIN
    const MSG *msg = static_cast<const MSG *>(message);
    QWindow *win = m_window->windowHandle();
    const qreal dpr = win != nullptr ? win->devicePixelRatio() : 1.0;
    // WM_NCHITTEST 的 lParam 是物理屏幕坐标（per-monitor v2），Qt 这边全是逻辑坐标。
    const QPoint logicalGlobal(qRound(GET_X_LPARAM(msg->lParam) / dpr), qRound(GET_Y_LPARAM(msg->lParam) / dpr));
    return m_window->mapFromGlobal(logicalGlobal);
#else
    Q_UNUSED(message);
    return QPoint();
#endif
}
