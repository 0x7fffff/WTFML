#include "Support.hpp"

void runDownloadConcurrencyTests() {
    section("ZO. 「同时最多下几个」那几档");

    // ---------- ZO0. 四档 + 出厂值 ----------
    {
        const QList<int> presets = DownloadConcurrency::presets();
        check(presets == QList<int>({6, 12, 24, 48}),
              "ZO0 四档就是那四条线（6/12/24/48），次序 = 界面上的次序",
              QStringList([&presets] {
                  QStringList out;
                  for (int v : presets) { out << QString::number(v); }
                  return out;
              }()).join(QStringLiteral(",")));
        check(presets.contains(DownloadConcurrency::kDefault),
              "★ ZO0 出厂那一档**必须**是四档之一（否则下拉框一上来就指不到盘上那个值）",
              QString::number(DownloadConcurrency::kDefault));
        check(DownloadConcurrency::kMin >= 1,
              "★ ZO0 下限不小于 1（0 会让队列一泵都不泵，表现是「下载停在 0%」）",
              QString::number(DownloadConcurrency::kMin));
        check(DownloadConcurrency::kMax >= presets.last(),
              "ZO0 上限不低于最激进那一档（不然那一档自己就落在界外了）",
              QString::number(DownloadConcurrency::kMax));
        check(DownloadManager::kMaxConcurrentDownloads == DownloadConcurrency::kDefault,
              "★ ZO0 DownloadManager 那个常量 == 判据层的出厂值（同一个数，不是各写一遍）",
              QString::number(DownloadManager::kMaxConcurrentDownloads));
    }

    // ---------- ZO1. 归一化：越界回默认、表外的合法值原样留 ----------
    {
        check(DownloadConcurrency::normalize(0) == DownloadConcurrency::kDefault,
              "★★ ZO1 0 ⇒ 默认（不夹到下限 1：那等于「用户自己选了一个 1 路」）",
              QString::number(DownloadConcurrency::normalize(0)));
        check(DownloadConcurrency::normalize(-5) == DownloadConcurrency::kDefault,
              "ZO1 负数 ⇒ 默认",
              QString::number(DownloadConcurrency::normalize(-5)));
        check(DownloadConcurrency::normalize(999) == DownloadConcurrency::kDefault,
              "★★ ZO1 越过大上限 ⇒ **默认**（不夹到 64：夹到边上会让被限流的形状看起来像用户选的）",
              QString::number(DownloadConcurrency::normalize(999)));
        check(DownloadConcurrency::normalize(DownloadConcurrency::kMin) == DownloadConcurrency::kMin &&
                  DownloadConcurrency::normalize(DownloadConcurrency::kMax) == DownloadConcurrency::kMax,
              "ZO1 两个边界值本身都留得住");
        check(DownloadConcurrency::normalize(32) == 32,
              "★★ ZO1 表外的合法值（手改过配置的 32）**原样留** —— 回默认等于把用户手写的数抹掉",
              QString::number(DownloadConcurrency::normalize(32)));
        bool presetsIntact = true;
        for (int v : DownloadConcurrency::presets()) {
            if (DownloadConcurrency::normalize(v) != v || !DownloadConcurrency::isPreset(v)) {
                presetsIntact = false;
            }
        }
        check(presetsIntact, "ZO1 四档自己过一遍归一化都原样回来，而且都被认成「是那一档」");
        check(!DownloadConcurrency::isPreset(32) && !DownloadConcurrency::isPreset(0),
              "ZO1 32 和 0 都不是那四档之一（下拉框要给它补一格「自定义」）");
    }

    // ---------- ZO2. DownloadManager 上真在用哪一档 ----------
    {
        DownloadManager mgr;
        check(mgr.maxConcurrentDownloads() == DownloadManager::kMaxConcurrentDownloads,
              "ZO2 刚建的下载器用的是**出厂那一档**（谁都不设的时候行为一个字不变）",
              QString::number(mgr.maxConcurrentDownloads()));
        mgr.setMaxConcurrentDownloads(6);
        check(mgr.maxConcurrentDownloads() == 6, "★ ZO2 设成 6 ⇒ 就是 6",
              QString::number(mgr.maxConcurrentDownloads()));
        mgr.setMaxConcurrentDownloads(999);
        check(mgr.maxConcurrentDownloads() == DownloadConcurrency::kDefault,
              "★ ZO2 设一个越界的值 ⇒ 回到默认（归一化也在 setter 里过一遍）",
              QString::number(mgr.maxConcurrentDownloads()));
        mgr.setMaxConcurrentDownloads(12);
        check(mgr.maxConcurrentDownloads() == 12, "ZO2 再设成 12 ⇒ 12",
              QString::number(mgr.maxConcurrentDownloads()));
    }

    // ---------- ZO3. 「还能不能再发一条」那一问按**给的那个上限**算 ----------
    {
        // 上限 6 那一档：到 6 条就一条都不发（不管后面还排着多少）。
        check(DownloadManager::mayIssueAnotherDownload(5, 1, 6),
              "ZO3 上限 6、飞着 5 条、还排着 1 条 ⇒ 发");
        check(!DownloadManager::mayIssueAnotherDownload(6, 1, 6),
              "★★ ZO3 上限 6、飞着 6 条 ⇒ **不发**（哪怕后面还排着一百条）");
        check(!DownloadManager::mayIssueAnotherDownload(6, 0, 6),
              "ZO3 上限 6、飞着 6 条、队列空 ⇒ 也不发（队列空那一半照旧）");
        check(DownloadManager::mayIssueAnotherDownload(0, 1, 6),
              "ZO3 一条都没飞、有排队的 ⇒ 发");
        check(!DownloadManager::mayIssueAnotherDownload(0, 0, 6),
              "ZO3 队列空 ⇒ 不发（没东西可发）");
        // 两参数那个版本 = 拿出厂那一档问一遍（验证工程的老断言按它钉边界）。
        check(DownloadManager::mayIssueAnotherDownload(
                  DownloadManager::kMaxConcurrentDownloads - 1, 1) &&
                  !DownloadManager::mayIssueAnotherDownload(
                      DownloadManager::kMaxConcurrentDownloads, 1),
              "★ ZO3 两参数那个版本就是拿出厂那一档问的（老断言不跟着改动漂）");
    }
}

