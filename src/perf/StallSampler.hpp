#pragma once

#include <QJsonObject>
#include <QList>

/// 「主线程卡住的那一瞬间，它卡在哪一行」。
///
/// **只在 `-DWTFML_PERF_PROBE=ON` 的测量构建里存在**（`src/perf/PerfProbe.cpp` 的搭档）。
///
/// 为什么要另开一个线程：主线程被占住的时候它自己没法记账 —— 那正是要量的事。
/// 探针那个 20 ms 的定时器每跳一次打一下心跳（`beat()`）；这里每 10 ms 看一眼，
/// 心跳停了超过阈值就 `SuspendThread` + `StackWalk64` 把主线程的栈走一遍再放行。
///
/// 栈里属于本程序 exe 的帧只记 `exe+0xRVA`：MinGW 出来的 DWARF 只有 addr2line 认得，
/// dbghelp 认不了（它认 PDB）。名字由 `tools/symbolize-stalls.ps1` 事后补，
/// 所以那个测量构建必须带 `-g`（CMakeLists 里 `WTFML_PERF_PROBE` 那一段）。
namespace StallSampler {

/// 起采样线程。`thresholdMs` 是「心跳停多久才算卡住」，探针传 50（两拍半）。
void start(int thresholdMs);

/// 主线程打心跳。`stepIndex` 是当前测量步骤的序号（0 起），卡住时一起记下来。
void beat(int stepIndex);

/// 停采样线程并等它收干净（会 join，所以别再打了）。
void stop();

/// 取走这次测量采到的栈（`gapMs` / `step` / `frames`）。只能在 `stop()` 之后调。
QList<QJsonObject> samples();

} // namespace StallSampler
