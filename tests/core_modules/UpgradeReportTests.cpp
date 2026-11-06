/**
 * @file UpgradeReportTests.cpp
 * @brief 兼容性报告的分组/折叠/搜索/必要项计数（U3 的判断层，纯逻辑）。
 */
#include "Support.hpp"

#include "operations/UpgradeReport.hpp"
#include "operations/FabricVersionDeclaration.hpp"
#include <QJsonValue>

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

    // UR8：评估层分类直接映射，别再靠界面文字猜。
    check(UpgradeReport::bucketForAssessmentClass(QStringLiteral("compatible"))
              == UpgradeReport::Bucket::Compatible,
          "UR8 compatible → 正常");
    check(UpgradeReport::bucketForAssessmentClass(QStringLiteral("incompatible"))
              == UpgradeReport::Bucket::Incompatible,
          "UR8 incompatible → 不兼容");
    check(UpgradeReport::bucketForAssessmentClass(QStringLiteral("unknown"))
              == UpgradeReport::Bucket::Unknown,
          "UR8 unknown → 无法确认");
    check(UpgradeReport::bucketForAssessmentClass(QStringLiteral("missing"))
              == UpgradeReport::Bucket::MissingDependency,
          "UR8 missing → 缺前置");
    check(UpgradeReport::bucketForAssessmentClass(QStringLiteral("failed"))
              == UpgradeReport::Bucket::Failed,
          "UR8 failed → 失败");
    check(UpgradeReport::bucketForAssessmentClass(QStringLiteral("whatever"))
              == UpgradeReport::Bucket::Unknown,
          "UR8 不认识的分类按无法确认");

    // UR9：真实 26.2 整合包里常见的声明写法，目标 26.2 时必须全部判"可以"。
    // 这些写法来自 `海波篮子`（Fabric 26.2）49 个模组的 fabric.mod.json 实际取值。
    for (const QString &rule : {QStringLiteral("26.2"), QStringLiteral("~26.2"),
                                QStringLiteral(">=26.2"), QStringLiteral(">=26.2 <26.3"),
                                QStringLiteral("~26.2-"), QStringLiteral(">=26.2-pre.3"),
                                QStringLiteral(">=26.1 <27"), QStringLiteral("*")}) {
        check(FabricVersionDeclaration::matches(QJsonValue(rule), QStringLiteral("26.2"))
                  == FabricVersionDeclaration::Match::Yes,
              QStringLiteral("UR9 目标 26.2 时「%1」判兼容").arg(rule), rule);
    }
    check(FabricVersionDeclaration::matches(QJsonValue(QStringLiteral("~26.2")),
                                            QStringLiteral("26.3"))
              == FabricVersionDeclaration::Match::No,
          "UR9 目标 26.3 时「~26.2」判不兼容（跨小版本本来就不该放行）");

    // UR10：评估原始行 → 报告行。嵌套 JAR 枚举不完整必须是"无法确认"，不是不兼容。
    {
        const QList<QStringList> raw = {
            {QStringLiteral("Sodium"), QStringLiteral("平台未找到目标发行版\n嵌套 JAR 声明无法确认"),
             QStringLiteral("fabric.mod.json / depends.minecraft: [\"~26.2\"]\n嵌套 JAR 未完整枚举"),
             QStringLiteral("sodium.jar"), QStringLiteral("review"), QString(), QString(),
             QStringLiteral("unknown")},
            {QStringLiteral("Old Mod"), QStringLiteral("平台未找到目标发行版\n本地声明不支持"),
             QStringLiteral("fabric.mod.json / depends.minecraft: [\"1.20.x\"]"),
             QStringLiteral("old.jar"), QStringLiteral("disable"), QString(), QString(),
             QStringLiteral("incompatible")},
            {QStringLiteral("Fine Mod"), QStringLiteral("平台有目标发行版\n本地 Minecraft 声明符合"),
             QStringLiteral("fabric.mod.json / depends.minecraft: [\">=26.2\"]"),
             QStringLiteral("fine.jar"), QStringLiteral("keep"), QStringLiteral("compatible"),
             QStringLiteral("2.0"), QStringLiteral("compatible")},
        };
        const QList<UpgradeReport::Row> rows = UpgradeReport::fromAssessmentRows(raw);
        check(rows.size() == 3, "UR10 三行都转出来了", QString::number(rows.size()));
        check(rows.at(0).bucket == UpgradeReport::Bucket::Unknown,
              "UR10 嵌套不完整 → 无法确认（不是不兼容）");
        check(rows.at(1).bucket == UpgradeReport::Bucket::Incompatible,
              "UR10 明确不支持 → 不兼容");
        check(rows.at(2).bucket == UpgradeReport::Bucket::Compatible,
              "UR10 有目标发行版 → 正常");
        check(rows.at(0).detail.contains(QStringLiteral("嵌套 JAR 未完整枚举")),
              "UR10 依据原样带过去");
    }

    return g_failures == 0 ? 0 : 1;
}






