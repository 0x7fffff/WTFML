/**
 * @file WindowChrome.hpp
 * @brief 外框：把系统那条白底标题栏去掉，客户区铺满整窗，顶上那条自己画的标题栏接管拖拽。
 *
 * 【这一份是干什么的】
 * 要求：「能把整个外边框设计成完全自定义的吗，就是不用默认的那个顶上一条白色的那个，
 * 而是用pcl的那种自己设计顶上」。系统那条标题栏（白底 + 图标 + 名字 + 最小化/最大化/关闭）
 * 得整个拿掉、换成我们自己画的一条（`ui/TitleBar`），而"拿掉"这件事只能从 Win32 那一层做。
 *
 * 这里走的是最老、也最直的那条：**WM_NCCALCSIZE 回 0**（客户区 = 整窗）+ 自己回
 * **WM_NCHITTEST**（哪一片是拖拽区、哪一片是按钮）。两条都在
 * `MainWindow::nativeEvent()` 里做 —— 它由 qwindowscontext.cpp:1011 那一句同步调到，
 * 位置在 Qt 自己处理这两条消息**之前**，返回值就是"这一条到此为止"。
 */

#pragma once

#include <QObject>
#include <QtGlobal>

class QWidget;

/**
 * @brief 主窗口外框：客户区铺满整窗 + 顶上那一条的命中测试。
 *
 * 它**不是窗口部件**，只是挂在主窗口上的一个挂件（跟着主窗口一起死）：
 * · 最外那一圈缩放边 → 我们回 HTTOP/HTLEFT/HTTOPLEFT/… （**顺序在前**：这一圈压着
 * 条的上沿，不先认它，条会把"从顶上边沿缩放"整条吃掉）；
 * · 顶上那一条的**空白处** → HTCAPTION（系统拖窗、贴边吸附都白拿）；
 * · 条上**有子控件的格子**（最小化/关闭那两颗按钮）→ 不认（客户区本来就是 HTCLIENT）；
 * · 其余一切 → 不认。
 *
 * NOTE: "条上哪一片是按钮"是**问出来的**（`strip->childAt()` 认得到子控件就是），
 * 不是写死哪几颗、更不是写死坐标 —— 以后往条上再加一颗按钮，这里一个字都不用改。
 */
class WindowChrome : public QObject {
    Q_OBJECT

public:
    /**
     * @param window 要接管外框的那个顶层窗口（主窗口）。
     * @param strip 顶上那条自己画的标题栏（`ui/TitleBar`）。可以先不给，之后 setStrip()。
     * @param parent 父子关系（挂到主窗口上）。
     */
    explicit WindowChrome(QWidget *window, QWidget *strip = nullptr, QObject *parent = nullptr);

    /// 顶上那一条（`MainWindowUI::setupUi` 里搭好之后交进来）。
    void setStrip(QWidget *strip);
    QWidget *strip() const { return m_strip; }

    /**
     * @brief 处理一条原生窗口消息。**只认两条**（WM_NCCALCSIZE / WM_NCHITTEST），
     * 别的消息一个字都不碰。
     * @return true = 这一条我们回了，别往下走（Qt 和 DefWindowProc 都不再看它）。
     */
    bool handleNativeEvent(void *message, qintptr *result);

    /**
     * @brief 让系统重算一次非客户区（SWP_FRAMECHANGED），把"客户区铺满整窗"落到真窗口上。
     *
     * 建窗那一次 WM_NCCALCSIZE 是**收不到**的：CreateWindowEx 的过程中这个 HWND 还没登记
     * 进 Qt 的窗口表，qwindowscontext.cpp:1011 那一句得先认得出是哪个 QWindow 才会送到
     * `nativeEvent()`。所以要在窗口已经出来之后自己催一次 —— 第一次 show、以及每次换
     * HWND（改窗口标志会重建）都会来一趟，见 eventFilter()。
     */
    void applyFrame();

    /**
     * 系统那一圈缩放边有多宽（**逻辑像素**）。
     */
    static int resizeBorder();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// 一条 WM_NCHITTEST 的坐标（lParam 是**物理屏幕**坐标）折成主窗口内的逻辑坐标。
    /// 只有"条在哪一片、哪一片上有按钮"那一半用它（那是 Qt 控件的坐标系）；
    /// 最外那一圈缩放边**不折**，直接用物理量比（见 handleNativeEvent 里那段 NOTE）。
    QPoint hitTestPoint(void *message) const;

    QWidget *m_window = nullptr;
    QWidget *m_strip = nullptr;
};
