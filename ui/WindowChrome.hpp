/**
 * @file WindowChrome.hpp
 * @brief 外框：去掉系统标题栏、客户区铺满整窗，把 Win11 系统背板（亚克力 / 云母）贴上去。
 *
 * 【为什么不用 Qt::FramelessWindowHint】带这个标志、又要半透明的窗口，Qt 会给它加
 * WS_EX_LAYERED，而分层窗口拿不到 DWM 系统背板（本机实测：只剩一块灰）。所以主窗口保持
 * 普通带框窗口（WS_CAPTION | WS_THICKFRAME），由 Qt 的 customMargins 把顶上那条标题栏
 * 收进客户区 —— Qt 自己知道这件事，几何账目（geometry / mapToGlobal）不会错位。
 *
 * 【为什么只从底边往上扩 DWM 边框】`DwmExtendFrameIntoClientArea` 给 -1（整窗）时，DWM 会在
 * 原来标题栏那一角画一组淡淡的系统按钮；只给 bottom = 10000（从底边一路扩到顶）同样整窗
 * 透出背板，但不算"扩进了标题栏"，那组按钮不出来。
 *
 * 【失焦不退色】DWM 只在窗口「非客户区激活」时画亚克力 / 云母，一失焦就换成一块平色（实测 std 0.80 → 0.00），
 * 连客户区外那 1px 可见边也变成一圈灰线。它认的是交给 `DefWindowProc` 的那条 `WM_NCACTIVATE`：这里总按 TRUE 交，
 * DWM 就一直画激活态（和聚焦时逐像素一样）。键盘焦点、Qt 的活动窗口走 `WM_ACTIVATE`，不受影响；非客户区本来
 * 就收掉了、边线也不画（见下），本机量窗外一圈激活 / 失焦没有差别。换档时窗口可能正失焦，`applyFrame()` 补一句激活态。
 * 只在亚克力 / 云母档生效；`WTFML_KEEP_BACKDROP_ACTIVE=0` 关掉（回到系统默认的失焦退色）。
 *
 * 【窗口边不画线】DWM 在客户区外总留 1px 可见边（亚克力 / 云母下是一条没着色的背板，圆角按它裁）。
 * 客户区里再自画一圈线 / 淡出，就是两道各自圆角、错开 1px 的边（浅色主题左边、左下角像两层页面），
 * 所以客户区边上什么都不画（见 WindowEdge.hpp）。最大化时 WM_NCCALCSIZE 自己算，让客户区正好贴屏。
 *
 * 命中测试：最上沿那一圈 = 缩放边（左右下三边是系统那圈看不见的边框，本来就能拖）；
 * 拖拽区（`addDragStrip`）的空白处 = HTCAPTION；拖拽区上有子控件的格子不认。
 *
 * 【窗口里其余的空白处】没有人接的左键按下一路冒泡到窗口，在这里 `startSystemMove()`（系统拖窗，
 * 贴边吸附照常），双击 = 最大化 / 还原。「空白」见 `isDragBlank()`：最深那个控件是纯容器 / 不可选的
 * 标签，而且它到窗口那一路上没有设过手形光标的控件。按钮、输入框、列表、滚动条、自绘的可点卡片都不是空白。
 *
 * 【关掉再打开要重贴】关窗口会销毁平台窗口，再 show 时 Show 事件先到、新的平台窗口后建
 * （`windowHandle()->handle()` 为空）。这一拍贴不上，新 HWND 就带着系统标题栏上屏（高速下载窗第二次打开
 * 变成「系统标题栏 + 窗内标题条」两层）。`applyFrame()` 在平台窗口没建好时排下一拍再贴。
 */

#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QMargins>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QtGlobal>

class QWidget;

class WindowChrome : public QObject {
    Q_OBJECT

public:
    explicit WindowChrome(QWidget *window, QWidget *strip = nullptr, QObject *parent = nullptr);

    /// 第一条拖拽区（右上那条窗口按钮所在的条）。
    void setStrip(QWidget *strip);
    QWidget *strip() const { return m_strip; }
    /// 再加一条拖拽区（导航轨顶上的品牌那一格）。
    void addDragStrip(QWidget *strip);

    /// 处理一条原生窗口消息（WM_NCHITTEST、WM_DPICHANGED、最大化时的 WM_NCCALCSIZE）。
    /// @return true = 这一条我们回了。
    bool handleNativeEvent(void *message, qintptr *result);

    /// 把外框贴到真窗口上：收标题栏、DWM 背板 / 深浅 / 圆角。幂等。
    void applyFrame();

    /// 上沿那一圈缩放边有多宽（逻辑像素）。
    static int resizeBorder();

    /// `under` 是不是「空白」：窗口里按下去可以拖窗的地方（见文件头）。`under == window` 算空白。
    static bool isDragBlank(const QWidget *under, const QWidget *window);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPoint hitTestPoint(void *message) const;
    bool handleBlankPress(QEvent *event);

    QWidget *m_window = nullptr;
    QWidget *m_strip = nullptr;
    QList<QPointer<QWidget>> m_extraStrips;
    QMargins m_appliedMargins;
    bool m_marginsApplied = false;
    bool m_retryPending = false;  // 平台窗口还没建好，已排了下一拍重贴
    QElapsedTimer m_blankClock;  // 上一次空白处按下的时刻，认双击用
    QPoint m_blankPos;
};
