/**
 * @file WindowChrome.cpp
 * @brief WindowChrome 的实现（Win32 那一层：WM_NCCALCSIZE / WM_NCHITTEST / 外框样式）。
 */

#include "WindowChrome.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QString>
#include <QWidget>
#include <QWindow>

#include <QtGlobal>

#ifdef Q_OS_WIN
#  include <dwmapi.h>    // DwmSetWindowAttribute（圆角那一句）
#  include <windows.h>
#  include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
// 手上这份 mingw 的 dwmapi.h 比 Win11 旧，这两个名字（圆角偏好）它还没有 —— 自己补上。
// 值取自 dwmapi.h：DWMWA_WINDOW_CORNER_PREFERENCE = 33，DWMWCP_ROUND = 2。
#  ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#    define DWMWA_WINDOW_CORNER_PREFERENCE 33
#  endif
#  ifndef DWMWCP_ROUND
#    define DWMWCP_ROUND 2
#  endif
#endif

namespace {

#ifdef Q_OS_WIN
/// 我们给主窗口**自己加回去**的那一套外框样式。
///
/// 【为什么是"加回去"】`Qt::FramelessWindowHint` 交给 Qt 一个确定的事实：这个窗的外框留白是 0
/// （qwindowswindow.cpp 那句 shouldOmitFrameAdjustment 认的就是这个标志位）—— 于是 Qt 那一侧的
/// 几何账目（`geometry_sys()` = 窗口矩形 - 留白）与我们"客户区铺满整窗"完全对得上，不会出现
/// "Qt 以为客户区比实际小一圈"那种错位。代价是 Qt 为这个标志位给的样式是 `WS_POPUP`
/// （qwindowswindow.cpp 里 `if (flags & Qt::FramelessWindowHint) style = WS_POPUP; // no border`），
/// 而一个无主的 WS_POPUP 窗**没有原生缩放边、也不出现在任务栏里**。所以样式由我们按
/// "一个正常的、可缩放的窗口"重新拼一遍；客户区照旧铺满整窗（WM_NCCALCSIZE 回 0），
/// 于是那圈边框只在最外一两个像素上看得见 —— 圆角、阴影、1 px 边线仍然全是系统画的。
///
/// NOTE: **没有** WS_MAXIMIZEBOX。主窗口本来就不许最大化（`MainWindowUI::setupUi` 里摘了
/// `Qt::WindowMaximizeButtonHint`），这里跟着一起不给，免得双击标题条就最大化。
/// 没有它，系统菜单里的"最大化"也是灰的。
constexpr LONG_PTR kFrameStyle = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX;

/// 那两个度量是按**系统 DPI**给的物理像素（100% 下 4 + 4）。折回逻辑像素才是摆控件、
/// 比坐标用的单位 —— 见 WindowChrome::resizeBorder()。
int resizeBorderPhysical() {
    return GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
}

/// 系统那一档缩放比（折物理 <-> 逻辑用）。
///
/// NOTE: 用的是**系统 DPI**（`GetDeviceCaps(LOGPIXELSY)`），**不是** `QScreen::devicePixelRatio()`。
/// 上面那两个度量是 `GetSystemMetrics` 按系统 DPI 给的，折回来当然要用同一个数 ——
/// 拿 QScreen 那一份会掺进别的两件事：验证工程跑 offscreen 时 `primaryScreen()` 是个
/// 缩放比 1.0 的**假屏**，而 GetSystemMetrics 照旧按这台机器真实的 150% 给 11 ——
/// 两边一折就对不上，`buttonInset() >= resizeBorder()` 那条不变式会**假红**
/// （红在一个跟产品没关系的地方）。`QScreen` 反而只在"真窗口"上才和系统同源。
///
/// NOTE: 多显示器、每屏缩放比不同时，这一份是"主屏那一档"的近似。要按窗口算得用
/// GetDpiForWindow / GetSystemMetricsForDpi（Win10 1607 起）。这里不切过去，是因为
/// **条上那几颗按钮的摆位与这一圈必须用同一个数**（`buttonInset()` 那条不变式就靠它），
/// 而"圈"和"摆位"只要彼此一致就不会出现"按按钮按成缩放"，副屏上最多宽两三个像素。
qreal systemDpr() {
    HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        return 1.0;
    }
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    return dpi > 0 ? dpi / 96.0 : 1.0;
}
#endif

}  // namespace

WindowChrome::WindowChrome(QWidget *window, QWidget *strip, QObject *parent)
    : QObject(parent), m_window(window), m_strip(strip) {
    if (m_window == nullptr) {
        return;
    }
    // 窗口标志在这一处设（只有这一处）：外框归谁管，标志位就该谁设。必须在窗口**出来之前**
    // 设完 —— 出来之后改标志会重建 HWND。调用点见 MainWindowUI::setupUi（那时还没 show）。
    m_window->setWindowFlags(m_window->windowFlags() | Qt::FramelessWindowHint);
    // 换过 HWND / 第一次 show 都要把外框重新贴上去（见 applyFrame() 那段"建窗那一次收不到"）。
    m_window->installEventFilter(this);
}

void WindowChrome::setStrip(QWidget *strip) {
    m_strip = strip;
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
    if (m_window == nullptr) {
        return;
    }
    // 只在**真 windows 平台**上动手。验证工程那几个 exe 跑的是 offscreen，那边没有 HWND，
    // 而 Qt 的 offscreen 平台给出来的 `winId()` 是个**非空的假句柄**（QPlatformWindow::winId()
    // 默认就返回 this），`if (hwnd == nullptr)` 那道闸拦不住它 —— 拿它去调 SetWindowLongPtrW
    // 这种"系统会先验句柄"的还好，但整件事摆明了是无意义的。这一句把那一整层关掉。
    if (QGuiApplication::platformName() != QLatin1String("windows")) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (hwnd == nullptr) {
        return;
    }

    // 1) 外框样式加回去（见 kFrameStyle 那段）。这里用"或"而不是"等于"：Qt 自己那几位
    // （WS_CLIPSIBLINGS / WS_CLIPCHILDREN）不能弄丢。
    SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) | kFrameStyle);

    // 2) 任务栏条目。无主的 WS_POPUP 窗系统不往任务栏上摆，得自己说一声；这是"建窗时是
    // WS_POPUP、后来才加上 WS_CAPTION"的补救（正常情况下 WS_CAPTION|WS_SYSMENU 就够）。
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_APPWINDOW) == 0) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_APPWINDOW);
    }

    // 3) 圆角。样式是建窗之后才改的，DWM 那一侧可能还记着"这是个 POPUP，不给圆角"，
    // 所以明说一次。Win11 才有这一位；老系统返回 E_INVALIDARG，忽略即可。
    const DWORD cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

    // 4) 让系统按新的非客户区重算一遍（SWP_FRAMECHANGED = "外框样式变了，重算非客户区"）。
    // 尺度和位置一个字不动：客户区本来就该等于整窗，窗口矩形没理由变。
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

bool WindowChrome::handleNativeEvent(void *message, qintptr *result) {
#ifdef Q_OS_WIN
    if (m_window == nullptr || message == nullptr || result == nullptr) {
        return false;
    }
    MSG *msg = static_cast<MSG *>(message);
    switch (msg->message) {
    case WM_NCCALCSIZE: {
        // WM_NCCALCSIZE 有**两问**：wParam = FALSE 那一问是"客户区多大"，交给 DefWindowProc；
        // TRUE 那一问给的是窗口矩形，要我们回"非客户区吃掉多少"。回 0 = 一点不吃
        // ⇒ 客户区 = 整窗 ⇒ 系统那条标题栏虽然还在样式里，却整条被客户区盖住了。
        if (msg->wParam == FALSE) {
            return false;
        }
        // 最大化那一态不能这么干：那会儿窗口矩形比工作区大一圈（系统把边缘推到屏幕外），
        // 客户区跟着铺满就会把那圈内容顶出屏幕。主窗口本来就没有最大化按钮，这一句是保险。
        if (IsZoomed(msg->hwnd)) {
            return false;
        }
        *result = 0;
        return true;
    }
    case WM_NCHITTEST: {
        // 1) 最外那一圈 = 缩放边。**这一手得我们自己回**。
        RECT wr{};
        if (GetWindowRect(msg->hwnd, &wr) == FALSE) {
            return false;
        }
        const int px = GET_X_LPARAM(msg->lParam) - wr.left;
        const int py = GET_Y_LPARAM(msg->lParam) - wr.top;
        if (!IsZoomed(msg->hwnd)) {
            const int band = resizeBorderPhysical();
            const int w = wr.right - wr.left;
            const int h = wr.bottom - wr.top;
            const bool left = px < band;
            const bool right = px >= w - band;
            const bool top = py < band;
            const bool bottom = py >= h - band;
            if (left || right || top || bottom) {
                if (left) {
                    *result = top ? HTTOPLEFT : (bottom ? HTBOTTOMLEFT : HTLEFT);
                } else if (right) {
                    *result = top ? HTTOPRIGHT : (bottom ? HTBOTTOMRIGHT : HTRIGHT);
                } else {
                    *result = top ? HTTOP : HTBOTTOM;
                }
                return true;
            }
        }

        // 2) 顶上那一条的空白处 = 拖窗区（HTCAPTION：系统拖窗、贴边吸附都白拿）。
        if (m_strip == nullptr || !m_strip->isVisible()) {
            return false;
        }
        // 条在**主窗口**坐标系里的位置（条挂在 centralWidget 下，两个坐标系原点重合，
        // 但这里照样走 mapTo，别去假设）。
        const QPoint local = hitTestPoint(message);
        const QRect bar(m_strip->mapTo(m_window, QPoint(0, 0)), m_strip->size());
        if (!bar.contains(local)) {
            return false;
        }
        // 条上那几颗按钮自己认领自己那一片：childAt() 找得到子控件 ⇒ 那一下是给按钮的，
        // 不是拖窗。**问出来**而不是写死坐标 —— 往后往条上再加一颗按钮，这里一个字不改。
        if (m_strip->childAt(local - bar.topLeft()) != nullptr) {
            return false;
        }
        *result = HTCAPTION;
        return true;
    }
    default:
        break;
    }
#endif
    return false;
}

bool WindowChrome::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_window) {
        switch (event->type()) {
        // Show：建窗那一次 WM_NCCALCSIZE **收不到**（CreateWindowEx 的过程中这个 HWND 还没登记
        // 进 Qt 的窗口表，qwindowscontext.cpp 那句 filterNativeEvent 得先认得出是哪个
        // QWindow 才轮得到 nativeEvent），所以在这里自己催一次。
        // WinIdChange：改窗口标志会重建 HWND，新 HWND 上得重贴一遍。
        // WindowStateChange：最小化/还原之后系统会按标志重摆样式，顺手贴回来。
        // 三样都是幂等的，多来一次没有代价。
        case QEvent::Show:
        case QEvent::WinIdChange:
        case QEvent::WindowStateChange:
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
    // WM_NCHITTEST 的 lParam 是**物理屏幕**坐标（per-monitor v2），Qt 这边全是逻辑坐标。
    const QPoint logicalGlobal(qRound(GET_X_LPARAM(msg->lParam) / dpr),
                              qRound(GET_Y_LPARAM(msg->lParam) / dpr));
    return m_window->mapFromGlobal(logicalGlobal);
#else
    Q_UNUSED(message);
    return QPoint();
#endif
}
