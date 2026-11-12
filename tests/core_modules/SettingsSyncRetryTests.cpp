#include "Support.hpp"
#include "util/SettingsSyncRetry.hpp"

void runSettingsSyncRetryTests() {
    section("ZR. QSettings 写盘有界重试（前 N 次失败、第 N+1 次成功）");

    // N = 0..3：第 N+1 次成功，尝试次数恰好是 N+1，退避序列是 5/20/50 的前 N 项。
    for (int failFirst = 0; failFirst <= 3; ++failFirst) {
        int calls = 0, attempts = 0;
        QList<int> sleeps;
        const bool ok = SettingsSyncRetry::run(
            [&] { return ++calls > failFirst; }, &attempts, [&](int ms) { sleeps << ms; });
        const QList<int> expected = QList<int>{5, 20, 50}.mid(0, failFirst);
        check(ok && calls == failFirst + 1 && attempts == failFirst + 1 && sleeps == expected,
              QStringLiteral("ZR1 前 %1 次失败：第 %2 次成功，退避 %3").arg(failFirst).arg(failFirst + 1)
                  .arg(QStringLiteral("5/20/50").left(failFirst == 0 ? 0 : failFirst == 1 ? 1 : failFirst == 2 ? 4 : 7)),
              QStringLiteral("calls=%1 attempts=%2 sleeps=%3").arg(calls).arg(attempts).arg(sleeps.size()));
    }

    // N >= 4：只试 4 次就放弃，总等待 75 ms，不会无限重试。
    for (int failFirst : {4, 9}) {
        int calls = 0, attempts = 0, slept = 0;
        const bool ok = SettingsSyncRetry::run(
            [&] { return ++calls > failFirst; }, &attempts, [&](int ms) { slept += ms; });
        check(!ok && calls == 4 && attempts == 4 && slept == 75,
              QStringLiteral("ZR2 前 %1 次失败：共试 4 次后失败，总等待 75 ms").arg(failFirst),
              QStringLiteral("calls=%1 attempts=%2 slept=%3").arg(calls).arg(attempts).arg(slept));
    }

    // 真 QSettings 写一次：成功路径只试 1 次，值落盘；remove 同理。
    {
        int attempts = 0;
        QString file;
        const bool wrote = SettingsSyncRetry::write(
            [](QSettings &s) { s.setValue(QStringLiteral("ZRProbe"), QStringLiteral("v")); }, &file, &attempts);
        check(wrote && attempts == 1 && !file.isEmpty() &&
                  QSettings().value(QStringLiteral("ZRProbe")).toString() == QStringLiteral("v"),
              "ZR3 真 QSettings 写入：1 次成功，值读得回来");
        const bool removed = SettingsSyncRetry::write(
            [](QSettings &s) { s.remove(QStringLiteral("ZRProbe")); }, nullptr, &attempts);
        check(removed && attempts == 1 && !QSettings().contains(QStringLiteral("ZRProbe")),
              "ZR3 真 QSettings 删除：1 次成功，键已不在");
    }
}
