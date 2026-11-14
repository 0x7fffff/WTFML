/**
 * @file LoadingState.hpp
 * @brief 加载态的**唯一出处**：整块（盖在一块区域上）和行内（树里某一层展开后在等）。
 *
 * 规矩（步骤 122，样张在 `.tongxi/设计稿/loading.md`）：
 * - 开始后 `Anim::loadingDelayMs()`（150）还没结果才出现；之前到了什么都不出。
 * - 出现时淡入 `Anim::appearMs()`，收掉时淡出 `Anim::exitMs()`，都是 OutCubic；
 *   淡入中途就到了，从当时的透明度往回淡，不补最短显示时间。
 * - 三颗主题色的点，一轮 `Anim::loadingDotPeriodMs()`，错开 `Anim::loadingDotStaggerMs()`，
 *   InOutSine。动效关着时门槛照旧，但不淡入淡出、点不呼吸（三颗一样 .55）。
 * - 出错：断开的云 + 一句原因 + 「重试」（整块是次按钮，行内是文字按钮）。
 *
 * 下拉框不归这里管（它们照旧写占位文字「正在获取…」）。
 */

#pragma once

#include <QRect>
#include <QString>

#include <functional>

class QModelIndex;
class QPainter;
class QStyleOptionViewItem;
class QTreeWidgetItem;
class QWidget;

namespace LoadingState {

// —— 整块：盖在 host 上（列表/树就传它自己，不传 viewport：viewport 的子控件会跟着滚）——

/// 往 host 上挂一层透明的加载态（不拦鼠标），跟着 host 的大小走，150ms 后才淡入。
/// 重复 begin 只换文案、不叠第二层；已经露着（比如出错态点了重试）就直接换成点，不闪。
void begin(QWidget *host, const QString &text = QString());
/// 结果到了：加载态没露过就直接撤；露过就淡出，同时 host 的内容淡入。
void finish(QWidget *host);
/// 失败：换成出错态（立刻出现，接鼠标）。点「重试」= 调 retry；retry 里照常再 begin()。
void fail(QWidget *host, const QString &reason, std::function<void()> retry);

// —— 行内：树里某一层展开后在等。由 VersionTreeDelegate 认 `rowLoadingRole()` 来画 ——

/// 在 parent 底下建一条禁用的占位子行（文字照旧写在第 0 列），并登记到共享时钟上。
QTreeWidgetItem *beginRow(QTreeWidgetItem *parent, const QString &text = QString());
/// 同一行改成出错样子；行尾「重试」那一格点中 = 调 retry（retry 可以删掉这一行）。
void failRow(QTreeWidgetItem *row, const QString &reason, std::function<void()> retry);
// 结果到了：调用方照旧 takeChildren() 删占位行、摆新行。不另加 API。

/// 行内那条占位行挂的角色：值 ≥ 0 = 加载中（开始时刻，`clockMs()` 刻度）；-1 = 出错。
constexpr int rowLoadingRole() { return Qt::UserRole + 17; }  // 号码登记见 MainWindowUI.hpp
/// 委托的钩子：这一格是加载/出错行就画掉并返回 true（调用方别再画别的）。
/// textLeft 是这一行正文起点（和别的行同缩进）。
bool paintRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index,
              int textLeft);

// —— 给验证工程看的 ——

/// host 上那一层（没有就是 nullptr）。`accessibleName()` 是它现在写的那句话，
/// 属性 `state` 是 "loading" / "error"。
QWidget *overlay(QWidget *host);
/// 整块出错态那颗「重试」的 objectName。
QString retryButtonName();
/// 共享时钟这会儿在不在转。
bool clockRunning();
/// 时钟上登记着几条行内加载行。
int tickingRowCount();
/// 出错行「重试」那一格（视图 viewport 坐标；上一次画出来的位置，没画过是空矩形）。
QRect rowRetryRect(const QModelIndex &index);
/// 共享时钟的刻度（毫秒）。
qint64 clockMs();

}  // namespace LoadingState
