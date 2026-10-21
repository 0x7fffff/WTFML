/**
 * @file DownloadConcurrency.hpp
 * @brief 「同时最多下几个文件」那几档（设置 → 下载 → 并发数；coding plan §9.2）。
 *
 * 【那四个数是从哪儿来的】
 *
 * 它们在 `DownloadManager.hpp` 里那份 的实测记录上：1.20.1 的全部 3598 个资源
 * 对象 / 620.43 MB、官方源，6 路 138.76 s、12 路 90.52 s、24 路 72.63 s、48 路 67.05 s
 * （全 0 失败 0 超时）。6 那一档是**延迟** bound（每个文件一个 ~150 ms 的往返 ÷ 6 条），
 * 24 已经贴着这条网线的带宽上限（620 MB ÷ 8.54 MB/s ≈ 73 s），48 只再多 8%。
 * ⇒ 四档就照那四条线给，**默认 24**（谁都不动这一项时，行为一个字都不变）。
 *
 * 【为什么越界的值回默认，而不是夹到边上】
 *
 * 夹到边上（999 → 64）会把"被限流 / 下不动"变成**看起来像用户自己选的**一个值；回默认
 * 至少回到那个量过的、四档都验过的数上。参数的下限是 1 而不是 0：0 会让队列永远不泵，
 * 表现是"下载卡在 0%"这种最难查的形状（见 `mayIssueAnotherDownload()` 那条注释里那种
 * 「必须有人把这批作业留在我们自己手里」的不变量）。上限 64 = 默认的 2.7 倍，比实测最激进的
 * 一档（48）还高，留出"网线换了"的余地。
 *
 * 这个头文件**没有网络、没有 QObject、不看时钟**：int 进、int 出。所以 verify 能直接
 * 喂一串数进来断言归一化的结果（见 tests/main.cpp 的 ZO 节）。
 */

#pragma once

#include <QList>

namespace DownloadConcurrency {

constexpr int kDefault = 24;
/// 下限。**不能是 0**：队列一泵都不泵，表现是"下载永远停在 0%"。
constexpr int kMin = 1;
constexpr int kMax = 64;

inline QList<int> presets() {
    return {6, 12, 24, 48};
}

/// 落盘那一格怎么归一：认得出来的一律原样（**包括不在这四档里、但落在 [kMin, kMax] 之内的
/// 值** —— 手改过配置的人看到的就是他自己写的那个数），认不出来（越界/非数）回默认。
inline int normalize(int value) {
    if (value < kMin || value > kMax) {
        return kDefault;
    }
    return value;
}

/// 这个值是不是那四档之一（界面靠它决定要不要给一格"自定义"的位置）。
inline bool isPreset(int value) {
    return presets().contains(value);
}

}  // namespace DownloadConcurrency
