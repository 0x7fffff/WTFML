/**
 * @file TextMarks.hpp
 * @brief 界面正文里那几种「一个字说明状态」的记号：**只用默认界面字体里真有的那几格**。
 *
 * 【为什么单列一份】
 *
 * 默认界面字体（本机是 Microsoft YaHei UI 9pt）里**没有**的字，Qt 第一次排版它时要先把
 * 整条回退链建起来 —— 本机实测 **277 ms**（`docs/dev/PERFORMANCE.md`「启动第一屏别用默认字体里
 * 没有的字形」那一节）。这笔钱一个进程只付一次，但付的时机是**那一页第一次画出来的那一帧**：
 * 任务中心、预设页、迁移计划表、日志里那些「✓ 日志已导出」都会因此当场顿一下。
 * 所以界面上要往正文里塞记号，统一从这里取。
 *
 * 【表】（右边那组数是本机在同一台机器上逐个字符开新进程量的首次排版耗时）
 *
 * - 成功 `√`（U+221A，4 ms）：原来是 `✓`（U+2713，279 ms）。
 * - 失败 `×`（U+00D7，4 ms）：原来是 `✗`（U+2717，272 ms）。
 * - 提醒 `※`（U+203B，4 ms）：原来是 `⚠`（U+26A0，277 ms）。
 * - 勾上 `■`（U+25A0）/ 没勾 `□`（U+25A1），两个都是 4 ms：原来是 `☑` / `☐`
 *   （U+2611 / U+2610，约 280 ms）。
 * - 进行中 `→`（U+2192）、待办 `•`（U+2022）、待定 `○`（U+25CB）、正在跑 `●`（U+25CF）：
 *   本来就在主字体里（4~5 ms），照旧用。
 *
 * 【别做的事】
 *
 * 别往界面正文里塞 emoji 或装饰符号 —— `✔ ✘ ⚡ ▶ ◀ ↻ ⟳ ⓘ ℹ ⌛ ⏳ ✎ ✏ ⌄ ⌃ ↔`
 * 这些在本机上都是回退字形（272~290 ms）。要图标就走 `VersionIcons` 画的那一套
 * （箭头、刷新圈、信息圈、草方块、加载器标志都是画出来的，不吃字体）。
 *
 * 这条规矩钉在 `tests/ui_check/modules/UiTaskCenterChecks.cpp` 的 I2 那一组断言上：
 * 有人把记号改回 `✓` 那一档，它会红。
 */
#pragma once

#include <QString>

namespace TextMarks {

// 记号本身。要拿它另拼句子（比如后面紧跟一小段正文）时用这一组。

inline QString done() { return QStringLiteral("√"); }
inline QString failed() { return QStringLiteral("×"); }
inline QString warning() { return QStringLiteral("※"); }
inline QString checked() { return QStringLiteral("■"); }
inline QString unchecked() { return QStringLiteral("□"); }
inline QString running() { return QStringLiteral("→"); }
inline QString pending() { return QStringLiteral("•"); }

// 「记号 + 一个空格 + 正文」。**记号不许写进 tr() 里**：它不是文案，换语言也不该翻，
// 而正文那边要保持原样的占位符编号（`%1` 还是 `%1`）。

inline QString done(const QString &text) { return done() + QLatin1Char(' ') + text; }
inline QString failed(const QString &text) { return failed() + QLatin1Char(' ') + text; }
inline QString warning(const QString &text) { return warning() + QLatin1Char(' ') + text; }

}  // namespace TextMarks
