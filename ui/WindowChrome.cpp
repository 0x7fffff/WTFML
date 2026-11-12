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
#include <cstring>
#include <QColor>
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

// ---- accent 亚克力（实验路径） ----
// 系统背板（DWMWA_SYSTEMBACKDROP_TYPE）在窗口失活时被 DWM 换成中性纯色，这是系统策略（见
// `.tongxi/任务书/失焦磨砂.md`）。`SetWindowCompositionAttribute` 的 accent 亚克力是另一条绘制路径，
// 不进那条策略。它没进 SDK 头，参数是社区逆向出来的；取不到导出或调用失败时照旧用系统背板。
struct AccentPolicy {
    int state;         // 4 = ACCENT_ENABLE_ACRYLICBLURBEHIND
    int flags;
    DWORD gradient;    // 0xAABBGGRR（alpha 在前）
    int animationId;
};

struct CompositionAttributeData {
    int attribute;     // 19 = WCA_ACCENT_POLICY
    void *data;
    ULONG size;
};

constexpr int kAccentEnableAcrylicBlurBehind = 4;
/// qframelesswindow 用的那一档（画边框 + 阴影那几位）。
constexpr int kAccentFlagsAllBorders = 0x20 | 0x40 | 0x80 | 0x100;
constexpr int kCompositionAttributeAccentPolicy = 19;

using SetWindowCompositionAttributeFn = BOOL(WINAPI *)(HWND, CompositionAttributeData *);

SetWindowCompositionAttributeFn setWindowCompositionAttribute() {
    // FARPROC 转函数指针：直接 reinterpret_cast 会踩 -Wcast-function-type，用 memcpy 走一遍。
    static const auto fn = [] {
        SetWindowCompositionAttributeFn result = nullptr;
        const FARPROC proc = GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
        static_assert(sizeof(result) == sizeof(proc));
        std::memcpy(&result, &proc, sizeof(result));
        return result;
    }();
    return fn;
}

/// 着色值取整窗那一层现算的令牌色（和 Glass 铺在背板上的同一支），取不到时退回主题页色 75%。
QColor accentGradient() {
    QColor tint = Theme::tokens().page;
    if (const Glass::Backdrop *backdrop = Glass::Backdrop::active()) tint = backdrop->tint();
    if (tint.alpha() == 255) tint.setAlphaF(0.75);
    return tint;
}

bool applyAccentAcrylic(HWND hwnd) {
    const auto fn = setWindowCompositionAttribute();
    if (fn == nullptr) return false;
    const QColor tint = accentGradient();
    AccentPolicy policy{};
    policy.state = kAccentEnableAcrylicBlurBehind;
    policy.flags = kAccentFlagsAllBorders;
    policy.gradient = (DWORD(tint.alpha()) << 24) | (DWORD(tint.blue()) << 16) | (DWORD(tint.green()) << 8) |
                      DWORD(tint.red());
    policy.animationId = 0;
    CompositionAttributeData data{kCompositionAttributeAccentPolicy, &policy, sizeof(policy)};
    return fn(hwnd, &data) != FALSE;
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
    // 亚克力档的实验路径：先贴 accent，成功就把系统背板关掉（别叠两层材质）。失败 / 没开开关 /
    // 云母档，都还是原来那一条。
    const bool accent = mode == Glass::Mode::Acrylic && Glass::accentAcrylicEnabled() && applyAccentAcrylic(hwnd);
    const int systemBackdrop = accent ? 1 /* DWMSBT_NONE */ : backdrop;
    DwmSetWindowAttribute(hwnd, WTFML_DWMWA_SYSTEMBACKDROP_TYPE, &systemBackdrop, sizeof(systemBackdrop));
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
    if (under == nullptr || under->window() != m_window || !isDragBlank(under, m_window)) return false;
    const bool second = event->type() == QEvent::MouseButtonDblClick ||
                        (m_blankClock.isValid() && !m_blankClock.hasExpired(QApplication::doubleClickInterval()) &&
                         (global - m_blankPos).manhattanLength() <= QApplication::startDragDistance());
    if (second) {
        m_blankClock.invalidate();
        if (m_window->windowFlags() & Qt::WindowMaximizeButtonHint) {
            m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
        }
        return true;
    }
    m_blankClock.restart();
    m_blankPos = global;
    m_window->windowHandle()->startSystemMove();
    return true;
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




