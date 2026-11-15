/**
 * @file DesignDefaults.hpp
 * @brief 设计系统**默认生效**的那一层（步骤 120）：新写的列表、栈、文本框不用记得接，
 *        样式 polish 的时候就拿到自家默认；真要原生样子的地方显式退出。
 *
 * 入口只有一个：`WtfmlProxyStyle::polish()`（`ui/Theme.cpp`）每 polish 一个控件就调一次
 * `apply()`。它管四件事：
 * 1. **默认代理**：还挂着 Qt 自带 `QStyledItemDelegate` / `QItemDelegate` 的列表、树、表，
 *    换成 `rowDelegate`：悬停 / 选中是内缩圆角块（和侧栏 navRow 同款，不画竖条），
 *    行高不低于 `minRowHeight()`，文字离块边留白；表格不画网格、关左上角角按钮、表头字靠左。自家代理（typeid 不是那两个）一个都不碰，
 *    下拉 / 补全弹层各有自己的代理，所以也不会被带歪。
 * 2. **视口透明**：列表 / 树 / 表与文本框一律 `NoFrame`、视口不铺底，直接排在窗口那层底上。
 * 3. **栈换页**：`QStackedWidget`（含 `QTabWidget` 里那个）切页时对新那一页 `Anim::pageAppear()`。
 *    手动接过的地方已经删掉了重复的那条 `connect`；万一还有人照旧手动接，`pageAppear()`
 *    在同一拍里对同一页第二次调用直接返回，只播一发。
 * 4. **文本框**：`QPlainTextEdit` / `QTextEdit` 去原生边框，换输入面（QSS 里那条
 *    `QPlainTextEdit, QTextEdit`，和输入框同一族）。
 *
 * 退出：控件上 `setProperty(optOutProperty(), true)`（`optOut()`），每处写一行理由。
 * 在控件第一次露面之前挂上即可（polish 发生在显示时；栈那一条在每次换页时现查）。
 */

#pragma once

#include <QStyledItemDelegate>

class QAbstractItemDelegate;
class QPainter;
class QRect;
class QStyleOptionHeader;
class QWidget;

namespace DesignDefaults {

/// 退出口那个属性名。
constexpr const char *optOutProperty() { return "wtfmlNoDesignDefaults"; }
/// 这个控件不要默认（列表留原生代理和视口、栈硬切、文本框留原生框）。调用处写一行理由。
void optOut(QWidget *widget);
bool optedOut(const QWidget *widget);

/// 样式 polish 时调：按控件种类补上默认。可重复调用（焦点环那条会反复 unpolish / polish）。
void apply(QWidget *widget);

/**
 * 默认行代理：悬停 / 选中画成内缩圆角块（和 `VersionTreeDelegate` 的 navRow 同一档：
 * 内缩 3/2、半径 `controlRadius()`、悬停 = 正文色 α14、选中 = 焦点色 α34/30，不画竖条），
 * 其余（图标、文字、勾选框、省略）交回样式照常画，只是不再让样式铺选中底。
 * 多列（树 / 表）时只有一行的首尾两格是圆的，中间几格方着接上，整行看着是一块。
 * 有表头的视图要自己的编辑器时从它派生（只改 createEditor / initStyleOption 那几样），
 * 格里的字才和表头从同一条竖线起。
 */
class RowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

/// 表头每一节跟着那一列的内容走（样式画 `CE_HeaderLabel` 前调，改的是那一份拷贝）：
/// 没单独给对齐的一节照这一列第一行的对齐（数字列右对齐，表头也右对齐）；
/// 树的第一节往里让一档缩进（`rootIsDecorated` 时顶层行的字从缩进后起）。
void alignHeaderLabel(QStyleOptionHeader *option, const QWidget *header);

/// 是不是这一层换上去的那个默认行代理。
bool isRowDelegate(const QAbstractItemDelegate *delegate);
/// 默认行代理的最小行高（列表 / 树；表格的行高归表头管）。
constexpr int minRowHeight() { return 32; }
/// 格内文字离格边的距离（左右同值）。**行代理每一格和表头每一节共用这一个数**
/// （表头那条 QSS 的左右 padding 念的就是它），所以表头字和下面那一列的字从同一条竖线起。
constexpr int cellTextInset() { return 11; }

/// 行的悬停 / 选中底：内缩圆角淡块（默认行代理和自家代理共用，别各画一份）。
/// @p roundLeft / @p roundRight：多列时只有一行首尾两格是圆的，中间几格方着接上。
void paintRowBlock(QPainter *painter, const QRect &cell, bool selected, bool hovered, bool roundLeft = true,
                   bool roundRight = true);

}  // namespace DesignDefaults
