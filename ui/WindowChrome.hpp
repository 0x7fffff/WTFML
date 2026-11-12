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
 * 【窗口边不画线】DWM 在客户区外总留 1px 可见边（亚克力 / 云母下是一条没着色的背板，圆角按它裁）。
 * 客户区里再自画一圈线 / 淡出，就是两道各自圆角、错开 1px 的边（浅色主题左边、左下角像两层页面），
 * 所以客户区边上什么都不画（见 WindowEdge.hpp）。最大化时 WM_NCCALCSIZE 自己算，让客户区正好贴屏。
 *
 * 命中测试：最上沿那一圈 = 缩放边（左右下三边是系统那圈看不见的边框，本来就能拖）；
 * 拖拽区（`addDragStrip`）的空白处 = HTCAPTION；拖拽区上有子控件的格子不认。
 */

#pragma once

#include <QList>
#include <QMargins>
#include <QObject>
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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPoint hitTestPoint(void *message) const;

    QWidget *m_window = nullptr;
    QWidget *m_strip = nullptr;
    QList<QPointer<QWidget>> m_extraStrips;
    QMargins m_appliedMargins;
    bool m_marginsApplied = false;
};
