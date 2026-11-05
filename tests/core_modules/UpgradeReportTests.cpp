/**
 * @file UpgradeReportTests.cpp
 * @brief 兼容性报告的分组/折叠/搜索/必要项计数（U3 的判断层，纯逻辑）。
 */
#include "Support.hpp"

#include "operations/UpgradeReport.hpp"

int runUpgradeReportTests(const QString &) {
    using UpgradeReport::Bucket;
    using UpgradeReport::Row;

    const QList<Bucket> all = UpgradeReport::buckets();
    check(all.size() == 5, "UR1 兼容性报告分五组", QString::number(all.size()));
    check(UpgradeReport::foldedByDefault(Bucket::Compatible)
              && !UpgradeReport::foldedByDefault(Bucket::Unknown)
              && !UpgradeReport::foldedByDefault(Bucket::Failed),
          "UR1 只有「正常兼容」默认折起来");

    QList<Row> rows;
    rows.append({QStringLiteral("Fabric API"), QStringLiteral("本地声明与目标发行版一致"),
                 Bucket::Compatible, true});
    rows.append({QStringLiteral("Sodium"), QStringLiteral("目标版本没有发行版"),
                 Bucket::Incompatible, true});
    rows.append({QStringLiteral("Iris"), QStringLiteral("查询超时"), Bucket::Failed, false});
    rows.append({QStringLiteral("Mystery"), QStringLiteral("认不出是什么"), Bucket::Unknown,
                 false});
    rows.append({QStringLiteral("Cloth Config"), QStringLiteral("缺必要前置"),
                 Bucket::MissingDependency, true});

    const QHash<Bucket, int> counts = UpgradeReport::counts(rows);
    check(counts.value(Bucket::Compatible) == 1 && counts.value(Bucket::Incompatible) == 1
              && counts.value(Bucket::Failed) == 1 && counts.value(Bucket::Unknown) == 1
              && counts.value(Bucket::MissingDependency) == 1,
          "UR2 每组各一条");

    const QList<Row> folded = UpgradeReport::filtered(rows, {}, false, Bucket::Compatible, false);
    check(folded.size() == 4, "UR3 收起正常项之后剩四条", QString::number(folded.size()));
    check(UpgradeReport::filtered(rows, {}, false, Bucket::Compatible, true).size() == 5,
          "UR3 展开之后五条都在");

    const QList<Row> searched = UpgradeReport::filtered(rows, QStringLiteral("sod"), false,
                                                         Bucket::Compatible, true);
    check(searched.size() == 1 && searched.first().name == QStringLiteral("Sodium"),
          "UR4 搜索按名字（大小写不敏感）");
    const QList<Row> byDetail =
        UpgradeReport::filtered(rows, QStringLiteral("超时"), false, Bucket::Compatible, true);
    check(byDetail.size() == 1 && byDetail.first().name == QStringLiteral("Iris"),
          "UR4 搜索也看依据那一列");
    const QList<Row> onlyFailed = UpgradeReport::filtered(rows, {}, true, Bucket::Failed, true);
    check(onlyFailed.size() == 1 && onlyFailed.first().bucket == Bucket::Failed,
          "UR5 只看某一组");

    check(UpgradeReport::unresolvedRequired(rows) == 2,
          "UR6 必要项里未处理的两条（不兼容 + 缺前置），可选的不算",
          QString::number(UpgradeReport::unresolvedRequired(rows)));
    check(!UpgradeReport::bucketTitle(Bucket::Compatible).isEmpty()
              && UpgradeReport::bucketTitle(Bucket::MissingDependency) != UpgradeReport::bucketTitle(Bucket::Incompatible),
          "UR6 五组各有自己的标题");

    // UR7：评估层那句状态 → 五组（关键词归一，顺序是"先判死、再判缺、再判失败、最后才算兼容"）。
    check(UpgradeReport::bucketForStateText(QStringLiteral("本地声明不支持"), QString())
              == Bucket::Incompatible,
          "UR7 「本地声明不支持」归不兼容");
    check(UpgradeReport::bucketForStateText(QStringLiteral("缺少目标加载器声明"), QString())
              == Bucket::MissingDependency,
          "UR7 「缺少目标加载器声明」归缺前置");
    check(UpgradeReport::bucketForStateText(QStringLiteral("声明读取失败"), QString())
              == Bucket::Failed,
          "UR7 「声明读取失败」归检查失败");
    check(UpgradeReport::bucketForStateText(QStringLiteral("已确认兼容"), QString())
              == Bucket::Compatible,
          "UR7 「已确认兼容」归正常兼容（不是靠默认兜底）");
    check(UpgradeReport::bucketForStateText(QStringLiteral("声明范围语法尚不支持"), QString())
              == Bucket::Unknown,
          "UR7 认不出来的说法落在「无法确认」，不冒充兼容");

    return g_failures == 0 ? 0 : 1;
}


