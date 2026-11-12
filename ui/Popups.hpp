/**
 * @file Popups.hpp
 * @brief 浮层那一层：自绘 tooltip、下拉弹层、菜单共用的磨砂外壳与动效。
 *
 * 「浮层」在这套界面里只有一种面：磨砂玻璃（`tokens().glass`）+ 1px 亮边（`glassEdge`），
 * 没有硬描边。窗口内的一切浮层都走这里，不再各画各的一块不透明方块：
 * - **tooltip**：自绘（系统那个黄底方角气泡不再出现），停 500ms 出、120ms 淡入上移 2px；
 *   只给图标按钮和条目用，文字按钮不出提示（按 `text()` 判，见 `TooltipDriver::isTextButton`）。
 * - **下拉弹层**（`QComboBox` 那 67 处全仓一个样）：圆角 14、内边距 6、行高 34、圆角 8，
 *   最多露 8 项，超出在弹层里滚（滚轮 / 拖动 / 键盘），右缘一条 3px 细条**只在滚动时**露
 *   出来、停 600ms 淡出；项数超过 15 的（旗帜图案、附魔）顶上多一条搜索框。
 * - **菜单**（右键 / 「…」）以及其他 `Qt::Popup` 自建浮层：同一个 `paintSurface()` 画的底。
 *
 * 真·透桌面：Win11 且磨砂生效时给浮层挂 DWM 瞬态亚克力（`DWMSBT_TRANSIENTWINDOW`），
 * 与主窗口同一档材质；系统背板用不了（Win10 / 关透明 / 高对比度）就只剩这层着色玻璃。
 *
 * 限制：截图（`QWidget::grab()`）拍不到 DWM 合成的那一层，验收图里看到的是自绘着色玻璃。
 */

#pragma once

#include <QColor>
#include <QRectF>
#include <QString>

class QApplication;
class QPainter;
class QWidget;

namespace Popups {

/// 产品与验证工程各调一次（`Theme::apply()` 里调，重复调用无副作用）。
void install(QApplication &app);

/// 浮层面：磨砂底 + 1px 亮边，无硬描边。半径按规格给（tooltip 8 / 下拉与菜单 14）。
void paintSurface(QPainter &painter, const QRectF &rect, qreal radius);

/// 把一个**顶层浮层窗口**接进磨砂那一套：透明底 + Win11 瞬态亚克力（能挂才挂）+ 圆角。
/// 幂等；系统背板不可用时只做前两件，底仍由 `paintSurface()` 画。
void glassify(QWidget *popup, qreal radius);

/// 自己弹一条提示（列表代理那种 `QToolTip::showText` 的去处用它）。`anchor` 用来判"贴着谁"。
void showTooltip(const QString &text, const QPoint &globalPos, const QWidget *anchor = nullptr);
void hideTooltip();
/// 正在显示的那条（测试用）：文案与它落的全局位置。
QString tooltipText();
bool tooltipVisible();

/// 文字按钮不出提示：`QAbstractButton` 且 `text()` 非空。图标钮（没字）照旧出。
bool isTextButton(const QWidget *widget);

}  // namespace Popups
