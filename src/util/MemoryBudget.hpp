/**
 * @file MemoryBudget.hpp
 * @brief 启动前的低内存判断（只看"这次要的堆"和"现在系统还有多少"）。
 *
 * 为什么放在这里而不是 LaunchController 里：这条判断是纯算术，验证工程要在
 * 不启动进程、不问系统的情况下把边界钉住。取物理内存那一半有平台分支，失败返回 -1
 * （"问不到"→ 不提醒，绝不拿猜出来的数去拦用户）。
 */
#pragma once

#include <QtGlobal>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace MemoryBudget {

/// 当前可用物理内存（MB）；问不到返回 -1。
inline qint64 availablePhysicalMb() {
    // 测试缝：验证工程要在"这台机器刚好内存紧张"时也能稳定跑，不能因为宿主机负载
    // 突然多弹一个确认框就红。产品运行时不设这个变量。
    const int overridden = qEnvironmentVariableIntValue("WTFML_AVAILABLE_MB");
    if (overridden > 0) {
        return overridden;
    }
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<qint64>(status.ullAvailPhys / (1024 * 1024));
    }
#endif
    return -1;
}

/// 这次要的堆内存是否已经超过"当前可用物理内存"的 80%。
///
/// 留 20% 是给 JVM 元空间、显存映射、系统缓存和启动器自己的：Xmx 只是 Java 堆，
/// 进程实际占用会更大，把 100% 都算给堆一定会先卡死系统再失败。
inline bool lowMemory(int xmxMb, qint64 availableMb) {
    return xmxMb > 0 && availableMb > 0 && static_cast<qint64>(xmxMb) * 5 > availableMb * 4;
}

/// 把"已经开着的那些实例各自要的堆"也算进来。
inline bool lowMemoryFor(int currentXmxMb, int alreadyRunningXmxMb, qint64 availableMb) {
    return lowMemory(currentXmxMb + qMax(0, alreadyRunningXmxMb), availableMb);
}

}  // namespace MemoryBudget


