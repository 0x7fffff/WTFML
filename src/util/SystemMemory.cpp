/**
 * @file SystemMemory.cpp
 * @brief SystemMemory 的实现（Windows 走 GlobalMemoryStatusEx）。
 */

#include "SystemMemory.hpp"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace SystemMemory {

namespace {
constexpr qint64 kBytesPerMb = 1024 * 1024;
}

Usage read() {
    Usage usage;

#ifdef Q_OS_WIN
    // GlobalMemoryStatusEx 是唯一一个不用额外链接库、还能一次拿到"总量 + 当前可用"的调用。
    // ullAvailPhys 的语义是"现在立刻就能拿来用的物理内存"（不用先把页写回磁盘），
    // 和任务管理器里那个「可用」是同一个量级 —— 自动档要的正是这个：机器此刻还剩多少。
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) == 0) {
        // 调用失败：原样返回 {0, 0}，让调用方退回内置默认值。
        // 这里刻意不打日志 —— 每次启动都会读一次，失败也只会退回一个能用的默认值。
        return usage;
    }
    usage.totalMb = static_cast<qint64>(status.ullTotalPhys) / kBytesPerMb;
    usage.availableMb = static_cast<qint64>(status.ullAvailPhys) / kBytesPerMb;
#else
    // 这个项目只出 Windows 版（见 docs/FEATURES.md「产品边界」）。别的平台留 {0, 0}，
    // 调用方按"读不到"处理 —— 不猜，也不为此在这里铺一堆平台分支。
#endif

    return usage;
}

}  // namespace SystemMemory
