/**
 * @file DownloadSpeedLimit.hpp
 * @brief 「下载速度上限」那几档（设置 → 下载 → 速度上限；coding plan §9.2）。
 * @author 0x7fffff
 * @date 2026-10-18
 *
 * 【这一项管的是谁】
 *
 * 只管 `DownloadManager` 那条路：游戏本体 / 资源对象 / 模组那几千个自动下载，以及分段
 * 作业拆出来的那些 Range 请求。工具箱里那个「直链高速下载」不走这一个对象（`ResumableDownload`
 * 自己开网络），它是用户手动点、手动看进度的一件工具 —— 这里不去管它，界面上那行说明也
 * 这么说。
 *
 * 【单位与默认值】
 *
 * 存的是**字节/秒**，0 = 不限。默认 0：谁都不动这一项时，行为和加这一项之前一个字都不变。
 * 0 必须是合法值（它是"关掉限速"），所以归一化和 `DownloadConcurrency` 那条"越界回默认"
 * 不完全一样 —— 这里默认就是 0，越界的值也回 0。
 *
 * 【为什么上限给到 1 GiB/s】
 *
 * 这是个"别把整条网线占满"的开关，不是精确计量工具。给 1 GiB/s 是让"手改成一个大数"的人
 * 仍然得到限速，而不是被当成越界悄悄关掉。真正的节拍由 `DownloadManager` 那个 100 ms 的
 * 令牌桶负责（见 `burstBytes()`）。
 *
 * ★ 这个头文件**没有网络、没有 QObject、不看时钟**：qint64 进、qint64 出。verify 直接喂
 *   一串数进来钉归一化（见 tests/main.cpp 的 ZP 节）。
 */

#pragma once

#include <QList>
#include <QtGlobal>

namespace DownloadSpeedLimit {

/// 不限速（出厂值，也是"关掉这一项"的那一档）。
constexpr qint64 kUnlimited = 0;
/// 下限 = 32 KiB/s。再小的限速在 24 路并发下每个回复分不到 1 KiB/s —— 那种值只会让
/// 看门狗和超时判据忙起来，不是用户真想要的"慢一点"。
constexpr qint64 kMin = 32 * 1024;
/// 上限 = 1 GiB/s（理由见文件头）。
constexpr qint64 kMax = qint64(1024) * 1024 * 1024;

/// 下拉框里给的那几档（字节/秒）。**顺序 = 界面上的顺序**；第一档就是"不限"。
inline QList<qint64> presets() {
    return {kUnlimited, qint64(512) * 1024, qint64(1) * 1024 * 1024, qint64(2) * 1024 * 1024,
            qint64(4) * 1024 * 1024, qint64(8) * 1024 * 1024, qint64(16) * 1024 * 1024};
}

/// 落盘那一格怎么归一：0 原样；[kMin, kMax] 内的值原样（**包括不在这几档里、但合法的
/// 手改值**）；其余（负数 / 太小 / 太大）一律回 0（= 不限）。
inline qint64 normalize(qint64 bytesPerSecond) {
    if (bytesPerSecond == kUnlimited) {
        return kUnlimited;
    }
    if (bytesPerSecond < kMin || bytesPerSecond > kMax) {
        return kUnlimited;
    }
    return bytesPerSecond;
}

/// 这个值是不是那几档之一（界面靠它决定要不要补一格"自定义"）。
inline bool isPreset(qint64 bytesPerSecond) {
    return presets().contains(bytesPerSecond);
}

/// 令牌桶的容量（字节）：一次最多攒下多少"欠账"。
///
/// 取限速的 1/4 秒，但至少有 16 KiB —— 每 100 ms 一拍，1/4 秒 = 2.5 拍的余量：足够吸收
/// 定时器抖动（Windows 上 100 ms 的定时器常常迟到 10~20 ms），又不会让用户改完设置之后
/// 看见"先猛冲半秒"。下限那 16 KiB 是给低档位（32 KiB/s → 8 KiB/拍）留的一拍半余量。
inline qint64 burstBytes(qint64 bytesPerSecond) {
    if (bytesPerSecond <= 0) {
        return 0;
    }
    return qMax<qint64>(16 * 1024, bytesPerSecond / 4);
}

}  // namespace DownloadSpeedLimit
