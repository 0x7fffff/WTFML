/**
 * @file Animations.hpp
 * @brief 界面动效的**唯一出处**：按下按钮、切换页面、展开/折叠。
 * @author 0x7fffff
 * @date 2026-09-29
 * @note 用户 2026-09-29 原话：「接下来是制作动画，按下按钮，切换页面，展开/折叠的时候的动画，
 *       可以不那么严格的检查，只要不要有元素超出，我人工会校对哪里需要改，可以多个agent
 *       负责不同的页面和部分」。
 *
 * 【这一层只做三件事，一件都不多】
 *
 *   1. **按下按钮**（`installPressAnimation()`）：往整个应用挂一个事件过滤器，谁被按下去就
 *      给谁压暗一档、松开再弹回来。**没有第二处** —— 全仓 92 个 `new QPushButton` 一个字
 *      都不用改（这也是为什么走事件过滤器而不是做一个 `WtfmlButton` 子类：那个得改 92 处，
 *      而且漏一处就是"有些按钮按下去没反应"）。
 *   2. **切换页面**（`pageAppear()` / `overlayAppear()`）：新那一页淡进来，顺带从下面抬
 *      一小段。
 *   3. **展开/折叠**（`animateExpansion()`）：`QTreeView::setAnimated(true)`，交给 Qt 自己
 *      放那一段动画。
 *
 * 【★ 硬要求：不许有元素超出】
 *
 * 这是用户点名的唯一一条验收线（「只要不要有元素超出」），所以这一层给自己上了一条死规矩：
 *
 *   * **除了上拉面板那一个例外，一个像素的几何都不动**。淡入淡出是把控件先渲染到一张
 *     pixmap 上、再按不透明度贴回来（`QGraphicsEffect` 那条路）—— 控件自己的 `geometry()`
 *     从头到尾没变过，布局也没被重新算过。
 *   * 那个例外是底栏那两块上拉面板（`slideUp()`）：它们是**顶层窗口**（`Qt::Popup`），
 *     "从下面滑上来"就是它们的 `pos()` 在动，而它们本来就是一块独立的窗口 —— 动它不会把
 *     任何东西挤出容器，动完也一定停在算好的那个点上（`finished` 里再 `move()` 一次兜底）。
 *   * 自绘的那一份（`AnimEffect::draw()`）**自己钉了一条裁剪**：画之前先按源矩形
 *     `setClipRect(..., IntersectClip)`，就算哪天平移量算错了，也画不到自己那一块之外。
 *   * 页面抬起来那一段（`pageRiseDistance()`）是**把内容整块往下挪着画**，所以是**上面**
 *     多出一条、下面少一条，而不是内容溢出到页面外 —— 源矩形那一剪裁在这里同样管着。
 *
 * 【为什么不走调色板 / 不走样式表】
 *
 * 样式表里已经有 `:pressed` 那一档颜色（`Theme::widgetStyleSheet()`），它是**瞬间**生效的；
 * 样式表的属性动画（`qproperty-…`）要求控件自己有那个 `Q_PROPERTY`，等于还是得改 92 处。
 * 这一层动的是"渲染完的那张图"，所以和样式表、和调色板都不打架 —— 底下的按下色照旧生效，
 * 这一层只是**让这个变化看起来是"按下去"而不是"跳一下"**。
 */

#pragma once

#include <QPoint>

class QApplication;
class QTreeView;
class QWidget;

namespace Anim {

// —— 时长与档位：**唯一出处**（要改节奏就改这儿，别在调用点写数）——

/// 按下去那一下多快（毫秒）。
constexpr int pressInMs() { return 90; }
/// 松开之后弹回来多快。
constexpr int pressOutMs() { return 130; }
/// 换页那一下（淡入 + 上抬）。
constexpr int appearMs() { return 160; }
/// 整块覆盖那一层（`OverlayHost`）淡进来那一下。
constexpr int overlayMs() { return 140; }
/// 底栏上拉面板滑上来那一下。
constexpr int panelMs() { return 130; }
/// 上拉面板的起手位比落点低多少像素。
constexpr int panelSlideDistance() { return 16; }
/// 换页时内容从下面抬多少像素。
constexpr int pageRiseDistance() { return 10; }
/// 按下时把按钮压暗**百分之几**（和悬停那一档 `Theme::rowHoverAccentPercent()` 同一个数）。
constexpr int pressDepthPercent() { return 12; }

/// 动效总开关。**默认开**；关掉之后下面这些函数全部原地返回（一个效果都不装、一个像素都不动）。
/// 三个验证工程在自己的 `main()` 头一行关掉它：那些工程要在 offscreen 下量几何和像素，
/// 半路飞进来一段动画会让"刚切过去就截图"那种判据时灵时不灵。
bool enabled();
void setEnabled(bool on);

/// 给整个应用装上"按下按钮"那个事件过滤器。**幂等**（`Theme::apply()` 会被调好几次，
/// 这里也一样可能被调好几次）。`src/main.cpp` 调它一次，验证工程不调（它们不要动效）。
bool installPressAnimation(QApplication &app);

/// 换页：新那一页淡进来，并从下面抬 `pageRiseDistance()` 那么一小段。
/// 顶栏四颗选项卡、下载页左栏、设置页左栏那几处内层换页都走这一个。
void pageAppear(QWidget *widget);

/// 整块覆盖的那一层（`OverlayHost`）淡进来。**只淡、不抬** —— 它是"整块盖住内容区"的，
/// 抬起来会在边上露出一条没盖住的缝。
void overlayAppear(QWidget *widget);

/// 展开/折叠动画。**唯一出处**：所有会展开折叠的树/表都在自己搭好之后调它一次。
/// 关掉动效时它把这一位关回去（等于回到 Qt 的出厂行为：瞬间展开）。
void animateExpansion(QTreeView *view);

/// 底栏那两块上拉面板：从落点下面 `panelSlideDistance()` 滑上来。
/// 调用方先把面板 `show()` 出来（顺序见 `BottomBarPanel::popUp()`）。
void slideUp(QWidget *window, const QPoint &target);

}  // namespace Anim
