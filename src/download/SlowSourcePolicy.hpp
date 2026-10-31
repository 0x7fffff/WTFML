#pragma once
#include <QtGlobal>

// Probe only sustained low throughput on a large unfinished file. A slow local
// rate limit is not evidence against the remote source. No sample is an error.
// 阈值只在这里写一次：单文件那条路（DownloadManager）和分段协调器
// （ResumableDownload）都从这里取。
namespace SlowSourcePolicy {
// 「持续」有多久：一个观察窗口。单文件那条路看的是这一条回复已经跑了多久，
// 分段协调器看的是一个窗口里的合计流量。
inline constexpr qint64 kSustainedWindowMs = 30000;
// 小于这么多字节的文件不值得比较来源。
inline constexpr qint64 kMinimumRemainingBytes = 4 * 1024 * 1024;
// 低于这个速率才算「持续低速」。
inline constexpr qint64 kSlowBytesPerSecond = 96 * 1024;

/// 窗口内（或一条回复至今）的平均速率够不够慢。`elapsedMs` 为 0 时不算慢 ——
/// 那是「还没有观察」，不是「观察到很慢」。
inline bool slowRate(qint64 elapsedMs, qint64 bytes) {
    return elapsedMs > 0 && bytes > 0 &&
           double(bytes) * 1000 / elapsedMs < double(kSlowBytesPerSecond);
}

/// 单文件那条路：这一条回复已经跑了 `elapsedMs`、收到 `bytes`，整份 `total` 还剩多少
/// 就是 `total - bytes`。
inline bool shouldProbe(qint64 elapsedMs, qint64 bytes, qint64 total, bool limited,
                        qint64 windowMs = kSustainedWindowMs) {
    return !limited && elapsedMs >= windowMs && total >= kMinimumRemainingBytes &&
           total - bytes >= kMinimumRemainingBytes && slowRate(elapsedMs, bytes);
}

/// 分段协调器：`bytes` / `elapsedMs` 是**窗口内**的合计（四条分段的流量加在一起），
/// 还剩多少字节要单独给 —— 窗口里的字节数不是整份的进度。
inline bool shouldCompare(qint64 elapsedMs, qint64 bytes, qint64 remaining, bool limited,
                          qint64 windowMs = kSustainedWindowMs) {
    return !limited && elapsedMs >= windowMs && remaining >= kMinimumRemainingBytes &&
           slowRate(elapsedMs, bytes);
}

inline bool better(qint64 currentBytesPerSecond, qint64 probeBytes, qint64 probeMs) {
    return currentBytesPerSecond > 0 && probeBytes >= 32 * 1024 && probeMs > 0 &&
           probeMs <= 6000 && double(probeBytes) * 1000 / probeMs >=
                                   qMax<qint64>(192 * 1024, currentBytesPerSecond * 3);
}
} // namespace SlowSourcePolicy
