/**
 * @file TaskId.hpp
 * @brief 任务的身份（`TaskId`）。
 *
 * 【为什么是一个单调递增的整数，而不是指针/字符串】
 *
 * 这个号要**活着跨过三件事**：跨过发起那一件事的 QObject（任务比它活得久 —— 下载管理器
 * 换批次、会话结束、窗口里那个控件被重建），跨过界面（`ui/` 里拿到的只是
 * `TaskCenterRowData::key`，是个字符串），还要**落盘**（任务历史，见 `TaskHistory`）。
 * 计数器 + 整数是三样都能过的唯一一种：`QObject *` 过不了盘，`QUuid` 能过但"谁比谁新"
 * 要现算，而这个顺序在收起态那条规则里是要用的（同一优先级取**后发起的**那个）。
 *
 * NOTE: `0` 是**无效号**（`invalidTaskId()`），不是"第一个任务"。所以计数器从 1 开始。
 *
 * 读盘之后计数器**要接着盘上那个最大值往后数**（见 `TaskManager::loadHistory()`）：
 * 号是"认哪一个任务"的唯一钥匙，重启之后从 1 重数的话，新任务就会和盘上某条历史撞号
 * —— 两者同在一个账本里，`succeed(1)` 会打到历史那一行上（那一行是终态 ⇒ 什么都不做，
 * 于是**真在跑的那件事再也收不了尾**）。续号之后这条不存在的可能就结构性地没了。
 */

#pragma once

#include <QtGlobal>

namespace Tasks {

using TaskId = quint64;

/// 无效任务号。所有"没找到 / 不适用"一律回它，不要用 -1 那种（它是无符号的）。
inline constexpr TaskId invalidTaskId() { return 0; }

}  // namespace Tasks
