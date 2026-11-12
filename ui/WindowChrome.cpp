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
#  define WTFML_DWMWA_CAPTION_COLOR 35
#  define WTFML_DWMWA_SYSTEMBACKDROP_TYPE 38
#  define WTFML_DWMWA_COLOR_NONE 0xFFFFFFFEu
#endif

namespace {

#ifdef Q_OS_WIN
int resizeBorderPhysical() {
    return GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
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
    // 最外沿那一圈（样张）：主窗口、游戏日志窗、ChromeDialog 都从这里装上。
    WindowEdge::install(m_window);
    // 换档、换深浅：重贴背板类型和深色标题区。
    Theme::follow(m_window, [this] { applyFrame(); });
}

void WindowChrome::setStrip(QWidget *strip) { m_strip = strip; }

void WindowChrome::addDragStrip(QWidget *strip) {
    if (strip != nullptr) m_extraStrips.append(strip);
}

int WindowChrome::resizeBorder() {
#ifdef Q_OS_WIN
    return qCeil(resizeBorderPhysical() / systemDpr());
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
    // 左右下三边留着 —— 那是系统那圈看不见的缩放边。
    RECT frame{0, 0, 0, 0};
    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    const LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    AdjustWindowRectExForDpi(&frame, DWORD(style), FALSE, DWORD(exStyle), GetDpiForWindow(hwnd));
    const QMargins margins(0, int(frame.top), 0, 0);
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
    // 自己画了最外沿那一圈，就把系统那条 1px 硬边去掉（Win11 才认这一项，旧系统返回错误、忽略）；
    // 关掉渐变时还给系统默认色。
    const COLORREF border = WindowEdge::enabled() ? WTFML_DWMWA_COLOR_NONE : 0xFFFFFFFFu;  // DWMWA_COLOR_DEFAULT
    DwmSetWindowAttribute(hwnd, WTFML_DWMWA_BORDER_COLOR, &border, sizeof(border));

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
    case WM_NCHITTEST: {
        RECT wr{};
        if (GetWindowRect(msg->hwnd, &wr) == FALSE) return false;
        // 1) 上沿那一圈 = 缩放边（标题栏收进客户区之后，上边没有系统边框可拖了）。
        if (!IsZoomed(msg->hwnd)) {
            const int px = GET_X_LPARAM(msg->lParam) - wr.left;
            const int py = GET_Y_LPARAM(msg->lParam) - wr.top;
            const int band = resizeBorderPhysical();
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
