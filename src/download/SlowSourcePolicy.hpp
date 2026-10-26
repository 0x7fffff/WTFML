#pragma once
#include <QtGlobal>

// Probe only sustained low throughput on a large unfinished file. A slow local
// rate limit is not evidence against the remote source. No sample is an error.
namespace SlowSourcePolicy {
inline bool shouldProbe(qint64 elapsedMs, qint64 bytes, qint64 total, bool limited) {
    return !limited && elapsedMs >= 30000 && bytes > 0 && total >= 4 * 1024 * 1024 &&
           total - bytes >= 4 * 1024 * 1024 &&
           double(bytes) * 1000 / elapsedMs < 96 * 1024;
}
inline bool better(qint64 currentBytesPerSecond, qint64 probeBytes, qint64 probeMs) {
    return currentBytesPerSecond > 0 && probeBytes >= 32 * 1024 && probeMs > 0 &&
           probeMs <= 6000 && double(probeBytes) * 1000 / probeMs >=
                                   qMax<qint64>(192 * 1024, currentBytesPerSecond * 3);
}
} // namespace SlowSourcePolicy
