/**
 * @file DesignDefaults.hpp
 * @brief 设计系统**默认生效**的那一层（步骤 120）：新写的列表、栈、文本框不用记得接，
 *        样式 polish 的时候就拿到自家默认；真要原生样子的地方显式退出。
 *
 * 入口只有一个：`WtfmlProxyStyle::polish()`（`ui/Theme.cpp`）每 polish 一个控件就调一次
 * `apply()`。它管四件事：
 * 1. **默认代理**：还挂着 Qt 自带 `QStyledItemDelegate` / `QItemDelegate` 的列表、树、表，
 *    换成 `rowDelegate`：悬停 / 选中是内缩圆角块（和侧栏 navRow 同款，不画竖条），
 *    行高不低于 `minRowHeight()`，文字离块边留白；表格不画网格。自家代理（typeid 不是那两个）一个都不碰，
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

class QAbstractItemDelegate;
class QWidget;

namespace DesignDefaults {

/// 退出口那个属性名。
constexpr const char *optOutProperty() { return "wtfmlNoDesignDefaults"; }
/// 这个控件不要默认（列表留原生代理和视口、栈硬切、文本框留原生框）。调用处写一行理由。
void optOut(QWidget *widget);
bool optedOut(const QWidget *widget);

/// 样式 polish 时调：按控件种类补上默认。可重复调用（焦点环那条会反复 unpolish / polish）。
void apply(QWidget *widget);

/// 是不是这一层换上去的那个默认行代理。
bool isRowDelegate(const QAbstractItemDelegate *delegate);
/// 默认行代理的最小行高（列表 / 树；表格的行高归表头管）。
constexpr int minRowHeight() { return 32; }

}  // namespace DesignDefaults
