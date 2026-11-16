/**
 * @file WindowChrome.cpp
 * @brief WindowChrome 的实现（Win32 / DWM 那一层）。
 */

#include "WindowChrome.hpp"

#include "Glass.hpp"
#include "Theme.hpp"
#include "WindowEdge.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSet>
#include <QString>
#include <QTabBar>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <QtGlobal>

#include <algorithm>

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
/// 缩放边（看不见的那圈 + 1px 可见边）有多宽，物理像素。按窗口所在那块屏的 DPI 取，
/// 和 AdjustWindowRectExForDpi 用同一个 DPI（混用系统 DPI 会在非 100% 屏上差 1px）。
int resizeBorderPhysical(UINT dpi) {
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
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

/// 失焦也让 DWM 按激活态画亚克力 / 云母（文件头【失焦不退色】）。WTFML_KEEP_BACKDROP_ACTIVE=0 关掉，回到系统默认的失焦退色。
bool keepBackdropActive() {
    return Glass::systemBackdrop() && qEnvironmentVariable("WTFML_KEEP_BACKDROP_ACTIVE") != QLatin1String("0");
}
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
    // 关掉再打开：Qt 销毁了平台窗口，Show 事件到这儿时新的还没建出来（handle() 为空，winId() 也给不出 HWND）。
    // 这一拍什么都不做的话，新 HWND 带着系统标题栏就这么上屏了（customMargins 没人再贴）。下一拍平台窗口在了再贴。
    if (m_window->windowHandle()->handle() == nullptr) {
        if (m_window->isVisible() && !m_retryPending) {
            m_retryPending = true;
            QTimer::singleShot(0, this, [this] {
                m_retryPending = false;
                applyFrame();
            });
        }
        return;
    }
    // 句柄从 QWindow 取：QWidget::winId() 缓存着上一个（已销毁的）HWND，新平台窗口建出来之后它还没更新
    // （实测：旧 HWND 的 GetWindowLong 全是 0，AdjustWindowRect 算出 customMargins = 0，重贴等于没贴）。
    const HWND hwnd = reinterpret_cast<HWND>(m_window->windowHandle()->winId());
    if (hwnd == nullptr) return;

    // 1) 标题栏收进客户区：顶上那一段非客户区（标题 + 上边框）给负的 customMargins。
    // 左右下三边留着 —— 那是系统那圈看不见的缩放边（外面那 1px 可见边 DWM 总画在客户区外，
    // 跟着客户区走：实测把客户区往外扩 1px，它也外移 1px，所以不去动它）。
    // 最大化时那一档在 handleNativeEvent 的 WM_NCCALCSIZE 里自己算（见那边）。
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

    // 1b) 同一个平台窗口对象换了新 HWND：Qt 记着 customMargins 没变，不会再触发 SWP_FRAMECHANGED，
    // 新 HWND 的系统标题栏就留着。客户区顶比窗口顶低一截 = 标题栏还在：窗口矮一截、重算非客户区
    // （Qt 的 WM_NCCALCSIZE 按记着的 customMargins 把客户区顶上去，客户区尺寸不变）。
    if (margins.top() < 0 && !IsZoomed(hwnd) && !IsIconic(hwnd)) {
        POINT origin{0, 0};
        RECT wr{};
        ClientToScreen(hwnd, &origin);
        GetWindowRect(hwnd, &wr);
        const int caption = origin.y - wr.top;
        if (caption > 0) {
            SetWindowPos(hwnd, nullptr, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top - caption,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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
    // 换档时窗口可能正失焦：换进亚克力 / 云母补一句激活态，换走还回真实状态（-1 = 不重画非客户区）。
    DefWindowProcW(hwnd, WM_NCACTIVATE, keepBackdropActive() || GetForegroundWindow() == hwnd, -1);
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
    case WM_NCACTIVATE:
        // 失焦也按激活交给系统：DWM 只在激活态画亚克力 / 云母（文件头【失焦不退色】）。焦点、键盘走 WM_ACTIVATE，不受影响。
        if (!keepBackdropActive()) return false;
        *result = DefWindowProcW(msg->hwnd, WM_NCACTIVATE, TRUE, msg->lParam);
        return true;
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
    case WM_ENTERSIZEMOVE:
        m_inSizeMove = true;
        return false;
    case WM_EXITSIZEMOVE:
        m_inSizeMove = false;
        return false;
    case WM_WINDOWPOSCHANGING: {
        // 自己拖窗的窗口不要 Win+←/→ 的键盘贴靠（拖动的贴靠在上面已经不会发生）。键盘贴靠不进系统的移动循环、
        // 也不是跨线程 SetWindowPos（实测 InSendMessageEx 为 0），带的标志位含 0x80000（Qt 自己的 resize、DPI 变化、
        // 显示隐藏都不带），而且一定改大小。所以：循环外、没最小化、改大小、带这一位 = 贴靠，把大小和位置都按住。
        // shortcut: 0x80000 是未公开的标志位，在 Win11 实测；换了系统版本若贴靠又出现，来这里重新量标志位。
        if (!m_ownMove || m_inSizeMove) return false;
        auto *pos = reinterpret_cast<WINDOWPOS *>(msg->lParam);
        if ((pos->flags & SWP_NOSIZE) != 0 || (pos->flags & 0x80000) == 0 || IsIconic(msg->hwnd)) return false;
        pos->flags |= SWP_NOSIZE | SWP_NOMOVE;
        return false;
    }
    case WM_NCLBUTTONDBLCLK:
        // 双击上 / 下 / 左 / 右边框：系统把窗口纵向（横向）贴满工作区，这也是一种贴靠，吃掉。
        if (m_ownMove && msg->wParam >= HTLEFT && msg->wParam <= HTBOTTOMRIGHT) {
            *result = 0;
            return true;
        }
        return false;
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
        // 自己拖窗的窗口不回 HTCAPTION（那会进系统移动循环、带出贴靠），交还客户区，由 handleBlankPress 搬。
        if (m_ownMove) return false;
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

bool WindowChrome::isDragBlank(const QWidget *under, const QWidget *window) {
    if (under == nullptr || window == nullptr) return false;
    for (const QWidget *w = under; w != nullptr && w != window; w = w->parentWidget()) {
        if (w->testAttribute(Qt::WA_SetCursor) && w->cursor().shape() != Qt::ArrowCursor) return false;
        // 控件自己的内部子件（表头视口、编辑框的内部控件…）也算控件，不管它们的类是什么。
        if (qobject_cast<const QAbstractButton *>(w) || qobject_cast<const QLineEdit *>(w) ||
            qobject_cast<const QAbstractSpinBox *>(w) || qobject_cast<const QComboBox *>(w) ||
            qobject_cast<const QAbstractItemView *>(w) || qobject_cast<const QAbstractSlider *>(w) ||
            qobject_cast<const QTextEdit *>(w) || qobject_cast<const QPlainTextEdit *>(w) ||
            qobject_cast<const QTabBar *>(w)) {
            return false;
        }
    }
    if (under == window) return true;
    if (const auto *label = qobject_cast<const QLabel *>(under)) {
        return !(label->textInteractionFlags() & (Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse));
    }
    // shortcut: 只认这几种纯容器；自绘的可点卡片是自己的类，一律不算空白，新增纯容器类要加进这里。
    static const QSet<QByteArray> plain{"QWidget",    "QFrame",         "QStackedWidget", "QTabWidget",
                                        "QGroupBox",  "ThemeBackground", "Glass::Backdrop"};
    return plain.contains(under->metaObject()->className()) || qobject_cast<const QDialog *>(under) != nullptr;
}

bool WindowChrome::handleBlankPress(QEvent *event) {
    const auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::LeftButton || m_window->isFullScreen() || m_window->windowHandle() == nullptr) return false;
    const QPoint global = me->globalPosition().toPoint();
    const QWidget *under = QApplication::widgetAt(global);
    if (under == nullptr || under->window() != m_window) return false;
    // 自己拖窗时，拖拽区本身（窗口按钮条、导航轨的空白）也算空白：它们不再靠 HTCAPTION 拖。
    const bool onStrip = m_ownMove && (under == m_strip || std::any_of(m_extraStrips.cbegin(), m_extraStrips.cend(),
                                                  [under](const QPointer<QWidget> &strip) { return strip.data() == under; }));
    if (!onStrip && !isDragBlank(under, m_window)) return false;
    // 双击只在窗口能最大化时有意义；不能最大化的窗口里，第二下照样是一次拖窗的开始。
    const bool canMaximize = m_window->windowFlags() & Qt::WindowMaximizeButtonHint;
    const bool second = canMaximize &&
                        (event->type() == QEvent::MouseButtonDblClick ||
                         (m_blankClock.isValid() && !m_blankClock.hasExpired(QApplication::doubleClickInterval()) &&
                          (global - m_blankPos).manhattanLength() <= QApplication::startDragDistance()));
    if (second) {
        m_blankClock.invalidate();
        if (m_window->windowFlags() & Qt::WindowMaximizeButtonHint) {
            m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
        }
        return true;
    }
    m_blankClock.restart();
    m_blankPos = global;
    if (m_ownMove) {
        beginOwnMove();
    } else {
        m_window->windowHandle()->startSystemMove();
    }
    return true;
}

void WindowChrome::beginOwnMove() {
#ifdef Q_OS_WIN
    if (!realWindowsPlatform()) {
        m_window->windowHandle()->startSystemMove();  // offscreen 没有真 HWND 可搬
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(m_window->windowHandle()->winId());
    POINT cursor{};
    RECT wr{};
    if (hwnd == nullptr || !GetCursorPos(&cursor) || !GetWindowRect(hwnd, &wr)) return;
    m_moveOffset = QPoint(wr.left - cursor.x, wr.top - cursor.y);
    m_moveDpi = GetDpiForWindow(hwnd);
    if (m_moveTimer == nullptr) {
        m_moveTimer = new QTimer(this);
        m_moveTimer->setInterval(8);
        connect(m_moveTimer, &QTimer::timeout, this, &WindowChrome::stepOwnMove);
    }
    m_moveTimer->start();
#endif
}

void WindowChrome::stepOwnMove() {
#ifdef Q_OS_WIN
    const HWND hwnd = m_window != nullptr && m_window->windowHandle() != nullptr
                          ? reinterpret_cast<HWND>(m_window->windowHandle()->winId())
                          : nullptr;
    // 左键松开（含系统按键互换）、窗口没了、最小化 = 收手。窗口留在松手的地方。
    const int button = GetSystemMetrics(SM_SWAPBUTTON) != 0 ? VK_RBUTTON : VK_LBUTTON;
    POINT cursor{};
    if (hwnd == nullptr || (GetAsyncKeyState(button) & 0x8000) == 0 || IsIconic(hwnd) || !GetCursorPos(&cursor)) {
        m_moveTimer->stop();
        return;
    }
    // 拖到 DPI 不同的屏上，系统会换窗口大小：偏移按新旧 DPI 之比换算，鼠标还指着窗口里同一个相对位置。
    const unsigned dpi = GetDpiForWindow(hwnd);
    if (dpi != m_moveDpi && dpi != 0 && m_moveDpi != 0) {
        m_moveOffset = m_moveOffset * int(dpi) / int(m_moveDpi);
        m_moveDpi = dpi;
    }
    RECT wr{};
    if (GetWindowRect(hwnd, &wr) && wr.left == cursor.x + m_moveOffset.x() && wr.top == cursor.y + m_moveOffset.y()) return;
    SetWindowPos(hwnd, nullptr, cursor.x + m_moveOffset.x(), cursor.y + m_moveOffset.y(), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

bool WindowChrome::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_window) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            // 走到窗口这一层的按下 = 路上没有控件接（接了的不会冒泡上来）。
            if (handleBlankPress(event)) return true;
            break;
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
