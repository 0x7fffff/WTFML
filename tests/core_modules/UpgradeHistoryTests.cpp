/**
 * @file UpgradeHistoryTests.cpp
 * @brief 每个实例那份升级历史（U6 的存储层）：追加、限量、读不懂不覆盖、未知键保留。
 */
#include "Support.hpp"

#include "operations/UpgradeHistory.hpp"

int runUpgradeHistoryTests(const QString &sandbox) {
    using UpgradeHistory::Entry;

    const QString instance = sandbox + QStringLiteral("/history-instance");
    QDir().mkpath(instance);
    check(!QFile::exists(UpgradeHistory::historyPath(instance)),
          "UH1 还没有历史时文件不存在");

    Entry first;
    first.transactionId = QStringLiteral("t-1");
    first.sourceId = QStringLiteral("src");
    first.sourceName = QStringLiteral("原实例");
    first.targetId = QStringLiteral("dst");
    first.targetName = QStringLiteral("新实例");
    first.fromGameVersion = QStringLiteral("1.20.1");
    first.toGameVersion = QStringLiteral("1.21.4");
    first.fromLoader = QStringLiteral("fabric 0.15.0");
    first.toLoader = QStringLiteral("fabric 0.16.0");
    first.startedAt = QStringLiteral("2026-11-05T08:00:00Z");
    first.finishedAt = QStringLiteral("2026-11-05T08:01:00Z");
    first.ok = true;
    first.categories = {QStringLiteral("saves"), QStringLiteral("config")};
    first.copiedFiles = 12;
    QString problem;
    check(UpgradeHistory::append(instance, first, &problem), "UH2 第一条历史写得进去", problem);
    check(problem.isEmpty(), "UH2 写成功时没有多余的一句话");

    Entry second = first;
    second.transactionId = QStringLiteral("t-2");
    second.ok = false;
    second.problem = QStringLiteral("空间不足");
    check(UpgradeHistory::append(instance, second, &problem), "UH3 第二条也写得进去", problem);

    QString readProblem;
    const QList<Entry> entries = UpgradeHistory::read(instance, &readProblem);
    check(readProblem.isEmpty() && entries.size() == 2, "UH4 读回两条",
          QString::number(entries.size()));
    check(entries.first().transactionId == QStringLiteral("t-2"),
          "UH4 **新的在前**（后写的在数组头）", entries.first().transactionId);
    check(entries.first().problem == QStringLiteral("空间不足") && !entries.first().ok,
          "UH4 失败那条带着原因");
    check(entries.at(1).categories == QStringList{QStringLiteral("saves"), QStringLiteral("config")},
          "UH4 类别清单原样存回来", entries.at(1).categories.join(QLatin1Char(',')));

    // 限量：超过 kMaxEntries 之后最老的被丢掉。
    for (int i = 0; i < UpgradeHistory::kMaxEntries + 3; ++i) {
        Entry extra = first;
        extra.transactionId = QStringLiteral("t-extra-%1").arg(i);
        check(UpgradeHistory::append(instance, extra, &problem), "UH5 追加一条");
    }
    const QList<Entry> capped = UpgradeHistory::read(instance);
    check(capped.size() == UpgradeHistory::kMaxEntries,
          "UH5 最多留 kMaxEntries 条", QString::number(capped.size()));
    check(capped.first().transactionId == QStringLiteral("t-extra-22"),
          "UH5 最新的那条仍在最前面", capped.first().transactionId);

    // 读不懂的那份：读回空 + 一句话；写时拒绝（不拿空数组把它顶掉）。
    const QString broken = sandbox + QStringLiteral("/history-broken");
    QDir().mkpath(broken);
    {
        QFile file(UpgradeHistory::historyPath(broken));
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "UH6 摆一份坏历史");
        file.write("{不是 json");
    }
    QString brokenProblem;
    check(UpgradeHistory::read(broken, &brokenProblem).isEmpty() && !brokenProblem.isEmpty(),
          "UH6 读不懂时回空并说得出为什么", brokenProblem);
    QString writeProblem;
    check(!UpgradeHistory::append(broken, first, &writeProblem),
          "UH6 读不懂的那份不许被覆盖", writeProblem);

    // 未知顶层键：追写之后还在。
    const QString withExtra = sandbox + QStringLiteral("/history-extra");
    QDir().mkpath(withExtra);
    {
        QJsonObject root{{QStringLiteral("schema"), 1},
                         {QStringLiteral("futureField"), QStringLiteral("keep-me")},
                         {QStringLiteral("entries"), QJsonArray{}}};
        QFile file(UpgradeHistory::historyPath(withExtra));
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "UH7 摆一份带未知键的历史");
        file.write(QJsonDocument(root).toJson());
    }
    check(UpgradeHistory::append(withExtra, first, &problem), "UH7 在它上面追加一条", problem);
    {
        QFile file(UpgradeHistory::historyPath(withExtra));
        check(file.open(QIODevice::ReadOnly), "UH7 读回原始字节");
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        check(root.value(QStringLiteral("futureField")).toString() == QStringLiteral("keep-me"),
              "UH7 未知顶层键活过一次追写");
    }

    // UH8：快照与"可恢复"标记也存得住（U7 的恢复对象）。
    Entry withSnapshot = first;
    withSnapshot.transactionId = QStringLiteral("t-snap");
    withSnapshot.snapshotName = QStringLiteral("instance-20261105.zip");
    withSnapshot.rollbackAvailable = true;
    check(UpgradeHistory::append(instance, withSnapshot, &problem), "UH8 带快照那条写得进去",
          problem);
    const QList<Entry> snapEntries = UpgradeHistory::read(instance);
    check(!snapEntries.isEmpty() && snapEntries.first().snapshotName
              == QStringLiteral("instance-20261105.zip")
              && snapEntries.first().rollbackAvailable,
          "UH8 快照名与可恢复标记都读得回来");
    check(snapEntries.size() > 1 && snapEntries.at(1).snapshotName.isEmpty(),
          "UH8 没有快照的那条读回来是空的");

    return g_failures == 0 ? 0 : 1;
}

