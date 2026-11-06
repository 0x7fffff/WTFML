#pragma once

/// 真网络/真进程的一次性探针（java 运行时下载、版本安装、启动、多实例压测、实例文件）。
///
/// 从 `main.cpp` 搬出来：那个文件有 4000 行上限（`check-test-modules` 会拦），
/// 而这几条探针本身互不相干，只是都"要真跑一次"。
///
/// @return 命中了某条探针就返回它的退出码；一个都没命中返回 -1。
int runLiveProbeMode(int argc, char *argv[]);
