/**
 * @file WindowChrome.hpp
 * @brief 外框：把系统那条白底标题栏去掉，客户区铺满整窗，顶上那条自己画的标题栏接管拖拽。
 * @author 0x7fffff
 * @date 2026-09-25
 *
 * 【这一份是干什么的】
 * 用户原话：「能把整个外边框设计成完全自定义的吗，就是不用默认的那个顶上一条白色的那个，
 * 而是用pcl的那种自己设计顶上」。系统那条标题栏（白底 + 图标 + 名字 + 最小化/最大化/关闭）
 * 得整个拿掉、换成我们自己画的一条（`ui/TitleBar`），而"拿掉"这件事只能从 Win32 那一层做。
 *
 * 【走的是哪条路，为什么不走 Qt 那条】
 * Qt 6.9 起有一条官方的路：`Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint`
 * （见 qwindowswindow.cpp 里 updateCustomTitlebar() 那一整块）。读过源码之后**不采用**：
 *   1. 那条路上 Qt 会把"顶上那一条"当成系统标题栏来**自己往下补发鼠标事件**
 *      （qwindowswindow.cpp:handleNonClientHitTest 里 `SynchronousDelivery` 那一段），
 *      而它和系统随后真发的那一发之间**没有去重** —— 一次点击可能到两次；
 *   2. 客户区仍然被框让出 8 px（实测 GetClientRect 的原点从 (0,0) 变成 (8,0)），
 *      不是真正的"铺满"；
 *   3. 那条路上的绘制全由 Qt 的画法决定，没法让它"一个字都不画"而又保留按钮那些格子。
 *
 * 这里走的是最老、也最直的那条：**WM_NCCALCSIZE 回 0**（客户区 = 整窗）+ 自己回
 * **WM_NCHITTEST**（哪一片是拖拽区、哪一片是按钮）。两条都在
 * `MainWindow::nativeEvent()` 里做 —— 它由 qwindowscontext.cpp:1011 那一句同步调到，
 * 位置在 Qt 自己处理这两条消息**之前**，返回值就是"这一条到此为止"。
 *
 * 【还剩下什么不是我们画的】
 * 圆角 / 阴影 / 贴边吸附 / 标题栏右键那个系统菜单照旧是系统的。缩放边**是我们要自己回的**
 * —— 见下面那条"实测推翻了一句想当然"，命中测试里我们给出 HTTOP/HTLEFT/… 那几档，
 * 系统照着做原生那一套（缩放光标、拖边、双击上边沿……）。所以边框照样能拖大拖小，
 * Win11 下照样是圆角带阴影。
 *
 * 【实测推翻了一句想当然（2026-09-25）】
 * 原本这里写着「最外那一圈交回 DefWindowProc 就有原生缩放边」。**假的**。
 * 客户区一旦铺满整窗，系统的命中测试看哪一点都在客户区里，它**一律给 HTCLIENT** ——
 * 连左上角都是，一个缩放边都没有（探针 build/tools/chromeprobe2 实测）。道理也简单：
 * DefWindowProc 那几个 HTxxx 是拿**非客户区那条带**算出来的，而"铺满"这件事的意义就是
 * 那条带没了。Qt 自己那条 ExpandedClientAreaHint 的路同样是自己算这几个 HTxxx
 * （qwindowswindow.cpp: handleNonClientHitTest 里那段 border/left/right/top/bottom）。
 */

#pragma once

#include <QObject>
#include <QtGlobal>

class QWidget;

/**
 * @brief 主窗口外框：客户区铺满整窗 + 顶上那一条的命中测试。
 *
 * 它**不是窗口部件**，只是挂在主窗口上的一个挂件（跟着主窗口一起死）：
 *   · 最外那一圈缩放边 → 我们回 HTTOP/HTLEFT/HTTOPLEFT/… （**顺序在前**：这一圈压着
 *     条的上沿，不先认它，条会把"从顶上边沿缩放"整条吃掉）；
 *   · 顶上那一条的**空白处** → HTCAPTION（系统拖窗、贴边吸附都白拿）；
 *   · 条上**有子控件的格子**（最小化/关闭那两颗按钮）→ 不认（客户区本来就是 HTCLIENT）；
 *   · 其余一切 → 不认。
 *
 * NOTE: "条上哪一片是按钮"是**问出来的**（`strip->childAt()` 认得到子控件就是），
 *       不是写死哪几颗、更不是写死坐标 —— 以后往条上再加一颗按钮，这里一个字都不用改。
 */
class WindowChrome : public QObject {
    Q_OBJECT

public:
    /**
     * @param window 要接管外框的那个顶层窗口（主窗口）。
     * @param strip  顶上那条自己画的标题栏（`ui/TitleBar`）。可以先不给，之后 setStrip()。
     * @param parent 父子关系（挂到主窗口上）。
     */
    explicit WindowChrome(QWidget *window, QWidget *strip = nullptr, QObject *parent = nullptr);

    /// 顶上那一条（`MainWindowUI::setupUi` 里搭好之后交进来）。
    void setStrip(QWidget *strip);
    QWidget *strip() const { return m_strip; }

    /**
     * @brief 处理一条原生窗口消息。**只认两条**（WM_NCCALCSIZE / WM_NCHITTEST），
     *        别的消息一个字都不碰。
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
     *
     * 条上那几颗按钮摆位要避开它：条的上沿压着这一圈，按钮顶到条的最上面就会变成
     * "按按钮最上面几个像素 = 缩放窗口"。100% 下它是 4+4，高 DPI 下那两个度量跟着变大、
     * 折回逻辑像素仍是 8 左右 —— 所以这里按**系统那一档**实测（不是写死 8），
     * 折回来用的是和那两个度量同一个 DPI（见 .cpp 里 systemDpr() 那两段 NOTE）。
     *
     * NOTE: `TitleBar::buttonInset() >= resizeBorder()` 是条不变式，断言在 ui_check Z11。
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
