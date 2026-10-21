#include "Support.hpp"

void runDownloadSpeedLimitTests() {
    section("ZP. 「下载速度上限」那几档（§9.2）");

    // ---------- ZP0. 几档 + 出厂值 ----------
    {
        const QList<qint64> presets = DownloadSpeedLimit::presets();
        check(!presets.isEmpty() && presets.first() == DownloadSpeedLimit::kUnlimited,
              "ZP0 第一档就是「不限」（0）—— 用户找「关掉限速」时第一眼就能看到");
        check(presets.contains(DownloadSpeedLimit::kUnlimited),
              "★ ZP0 出厂值（不限）**必须**是那几档之一（否则下拉框一上来就指不到盘上那个值）");
        bool ascending = true;
        bool inRange = true;
        for (int i = 1; i < presets.size(); ++i) {
            if (presets.at(i) <= presets.at(i - 1)) ascending = false;
            if (presets.at(i) < DownloadSpeedLimit::kMin
                || presets.at(i) > DownloadSpeedLimit::kMax) {
                inRange = false;
            }
        }
        check(ascending, "ZP0 那几档从「不限」往上排，越往下越宽（次序 = 界面上的次序）");
        check(inRange, "ZP0 每一档都落在 [kMin, kMax] 里（越界的那一档自己就选不中）");
        check(DownloadSpeedLimit::kMin > 0 && DownloadSpeedLimit::kMax >= presets.last(),
              "ZP0 下限为正、上限不低于最宽那一档",
              QStringLiteral("%1 / %2").arg(DownloadSpeedLimit::kMin).arg(DownloadSpeedLimit::kMax));
    }

    // ---------- ZP1. 归一化：0 原样留，越界回 0，表外的合法值原样留 ----------
    {
        check(DownloadSpeedLimit::normalize(0) == DownloadSpeedLimit::kUnlimited,
              "★ ZP1 0 ⇒ 0（不限是**合法值**，不是「没设」）");
        check(DownloadSpeedLimit::normalize(-1) == DownloadSpeedLimit::kUnlimited
                  && DownloadSpeedLimit::normalize(-1024 * 1024)
                         == DownloadSpeedLimit::kUnlimited,
              "ZP1 负数 ⇒ 0（不限）");
        check(DownloadSpeedLimit::normalize(1) == DownloadSpeedLimit::kUnlimited
                  && DownloadSpeedLimit::normalize(DownloadSpeedLimit::kMin - 1)
                         == DownloadSpeedLimit::kUnlimited,
              "★ ZP1 比下限还小 ⇒ 0，**不夹到 kMin** —— 夹上去等于悄悄给用户开了个最慢档");
        check(DownloadSpeedLimit::normalize(DownloadSpeedLimit::kMax + 1)
                      == DownloadSpeedLimit::kUnlimited
                  && DownloadSpeedLimit::normalize(qint64(1) << 62)
                         == DownloadSpeedLimit::kUnlimited,
              "ZP1 越过大上限 / 荒唐的大数 ⇒ 0（不限）");
        check(DownloadSpeedLimit::normalize(DownloadSpeedLimit::kMin) == DownloadSpeedLimit::kMin
                  && DownloadSpeedLimit::normalize(DownloadSpeedLimit::kMax)
                         == DownloadSpeedLimit::kMax,
              "ZP1 两个边界值本身都留得住");
        const qint64 custom = 1536 * 1024;  // 1.5 MB/s：手改过配置的人写的
        check(DownloadSpeedLimit::normalize(custom) == custom
                  && !DownloadSpeedLimit::isPreset(custom),
              "★★ ZP1 表外的合法值（1.5 MB/s）**原样留**，并认成「不是那几档」（界面补一格自定义）",
              QString::number(DownloadSpeedLimit::normalize(custom)));
        bool presetsIntact = true;
        for (qint64 v : DownloadSpeedLimit::presets()) {
            if (DownloadSpeedLimit::normalize(v) != v || !DownloadSpeedLimit::isPreset(v)) {
                presetsIntact = false;
            }
        }
        check(presetsIntact, "ZP1 每一档自己过一遍归一化都原样回来，而且都被认成「是那一档」");
    }

    // ---------- ZP2. 令牌桶容量 ----------
    {
        check(DownloadSpeedLimit::burstBytes(0) == 0,
              "ZP2 不限速时容量是 0（一个令牌都不该攒）");
        check(DownloadSpeedLimit::burstBytes(DownloadSpeedLimit::kMin) >= 16 * 1024,
              "★ ZP2 最低那档也留至少 16 KiB 的余量（100 ms 一拍时够吸收定时器迟到）",
              QString::number(DownloadSpeedLimit::burstBytes(DownloadSpeedLimit::kMin)));
        const qint64 eight = qint64(8) * 1024 * 1024;
        check(DownloadSpeedLimit::burstBytes(eight) == eight / 4,
              "ZP2 高档位的容量 = 限速的 1/4 秒（不多攒，改完设置不会先猛冲一下）",
              QString::number(DownloadSpeedLimit::burstBytes(eight)));
        check(DownloadSpeedLimit::burstBytes(eight) < eight,
              "ZP2 容量永远小于一秒的量（否则那一瞬间就等于没限速）");
    }

    // ---------- ZP3. DownloadManager 上真在用哪一档 ----------
    {
        DownloadManager mgr;
        check(mgr.speedLimitBytesPerSecond() == DownloadSpeedLimit::kUnlimited,
              "ZP3 刚建的下载器是**不限速**（谁都不设的时候行为和加这一项之前一个字不变）",
              QString::number(mgr.speedLimitBytesPerSecond()));
        const qint64 twoMegabytes = qint64(2) * 1024 * 1024;
        mgr.setSpeedLimitBytesPerSecond(twoMegabytes);
        check(mgr.speedLimitBytesPerSecond() == twoMegabytes,
              "★ ZP3 设成 2 MB/s ⇒ 就是 2 MB/s",
              QString::number(mgr.speedLimitBytesPerSecond()));
        mgr.setSpeedLimitBytesPerSecond(-5);
        check(mgr.speedLimitBytesPerSecond() == DownloadSpeedLimit::kUnlimited,
              "★ ZP3 设一个越界的值 ⇒ 回到不限（归一化在 setter 里过一遍）",
              QString::number(mgr.speedLimitBytesPerSecond()));
        mgr.setSpeedLimitBytesPerSecond(8);
        check(mgr.speedLimitBytesPerSecond() == DownloadSpeedLimit::kUnlimited,
              "ZP3 设一个比下限还小的值 ⇒ 不限（不是夹到 32 KB/s）",
              QString::number(mgr.speedLimitBytesPerSecond()));
        mgr.setSpeedLimitBytesPerSecond(1024 * 1024);
        check(mgr.speedLimitBytesPerSecond() == 1024 * 1024,
              "ZP3 再设成 1 MB/s ⇒ 1 MB/s",
              QString::number(mgr.speedLimitBytesPerSecond()));
    }
}

