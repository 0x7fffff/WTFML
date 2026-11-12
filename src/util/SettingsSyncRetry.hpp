#pragma once
/**
 * @file SettingsSyncRetry.hpp
 * @brief QSettings 写盘的有界重试：账号凭据、账号列表这类"写不进去就等于丢登录"的写入用它。
 *
 * QSettings::sync() 内部用 QSaveFile：先写临时文件，再 rename 覆盖目标。装了杀毒/管家
 * 的机器上，文件刚写完 1~30 ms 内会被过滤驱动短暂占住，rename 返回 ERROR_ACCESS_DENIED，
 * sync() 报 AccessError。这是瞬时占用，几毫秒后重试基本都能成。
 *
 * 每次尝试都由调用方的 lambda 重新构造 QSettings、重新 setValue 再 sync()：失败后旧对象
 * 的内部状态可能残留，新对象从盘上重读，最终文件内容只取决于最后一次成功的那次写。
 */

#include <QSettings>
#include <QString>
#include <QThread>

#include <functional>

namespace SettingsSyncRetry {

constexpr int kMaxAttempts = 4;
// 第 1 次失败后等 5 ms，之后 20 ms、50 ms；总等待 ≤ 75 ms。
constexpr int kBackoffMs[kMaxAttempts - 1] = {5, 20, 50};

/// 重试 `attempt` 直到它返回 true 或 4 次用完。`attemptsOut` 写入实际尝试次数。
/// `sleepMs` 可注入（测试里不真睡）。
inline bool run(const std::function<bool()> &attempt, int *attemptsOut = nullptr,
                const std::function<void(int)> &sleepMs = [](int ms) { QThread::msleep(ms); }) {
    int attempts = 0;
    bool ok = false;
    while (!ok && attempts < kMaxAttempts) {
        if (attempts > 0) sleepMs(kBackoffMs[attempts - 1]);
        ++attempts;
        ok = attempt();
    }
    if (attemptsOut != nullptr) *attemptsOut = attempts;
    return ok;
}

/// 对默认 QSettings 做一次"改 + sync + 查 status"，带重试。`apply` 里只管 setValue / remove。
/// `fileNameOut` 给错误串用（失败时是最后一次尝试的 ini 路径）。
inline bool write(const std::function<void(QSettings &)> &apply, QString *fileNameOut = nullptr,
                  int *attemptsOut = nullptr) {
    return run(
        [&] {
            QSettings settings;
            apply(settings);
            settings.sync();
            if (fileNameOut != nullptr) *fileNameOut = settings.fileName();
            return settings.status() == QSettings::NoError;
        },
        attemptsOut);
}

}  // namespace SettingsSyncRetry
