#pragma once

class QApplication;
class MainWindow;

/// 只用于测量的界面探针。
///
/// **只在 `-DWTFML_PERF_PROBE=ON` 的测量构建里存在**（默认 OFF）。普通构建里
/// `attach()` 是一个内联的空函数，`src/perf/PerfProbe.cpp` 根本不参与编译，
/// 产品代码里也不会多出任何一条分支。
///
/// 为什么不能拿验证工程量这些：`mw_check` / `verify` 按设计是 `-O0`
/// （`tests/mw_check/CMakeLists.txt` 那句 "not product performance baselines"），
/// 它们的耗时不能代表产品。这里量的是真 `Release` 编出来的同一个 exe。
namespace PerfProbe {
#if defined(WTFML_PERF_PROBE)
/// argv 里没有 `--wtfml-perf-probe=<文件>` 就什么都不做（同一个 exe 仍可当普通启动器用）。
void attach(QApplication &app, MainWindow &window);
#else
inline void attach(QApplication &, MainWindow &) {}
#endif
} // namespace PerfProbe
