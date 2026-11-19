/**
 * @file UpgradeReport.cpp
 * @brief 见头文件：分组、折叠、搜索与"必要项未处理"的计数。
 */
#include "UpgradeReport.hpp"
#include <QCoreApplication>

namespace UpgradeReport {

QList<Bucket> buckets() {
    return {Bucket::Compatible, Bucket::Unknown, Bucket::Failed, Bucket::Incompatible,
            Bucket::MissingDependency};
}

QString bucketTitle(Bucket bucket) {
    switch (bucket) {
    case Bucket::Compatible:
        return QCoreApplication::translate("UpgradeReport", "正常兼容");
    case Bucket::Unknown:
        return QCoreApplication::translate("UpgradeReport", "无法确认");
    case Bucket::Failed:
        return QCoreApplication::translate("UpgradeReport", "检查失败");
    case Bucket::Incompatible:
        return QCoreApplication::translate("UpgradeReport", "不兼容");
    case Bucket::MissingDependency:
        return QCoreApplication::translate("UpgradeReport", "缺前置");
    }
    return QString();
}

bool foldedByDefault(Bucket bucket) {
    return bucket == Bucket::Compatible;
}

QHash<Bucket, int> counts(const QList<Row> &rows) {
    QHash<Bucket, int> out;
    for (const Bucket bucket : buckets()) {
        out.insert(bucket, 0);
    }
    for (const Row &row : rows) {
        out[row.bucket] = out.value(row.bucket) + 1;
    }
    return out;
}

QList<Row> filtered(const QList<Row> &rows, const QString &query, bool hasOnly, Bucket only,
                    bool showCompatible) {
    const QString needle = query.trimmed();
    QList<Row> out;
    for (const Row &row : rows) {
        if (!showCompatible && row.bucket == Bucket::Compatible) {
            continue;
        }
        if (hasOnly && row.bucket != only) {
            continue;
        }
        if (!needle.isEmpty()
            && !row.name.contains(needle, Qt::CaseInsensitive)
            && !row.detail.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        out.append(row);
    }
    return out;
}

Bucket bucketForStateText(const QString &state, const QString &basis) {
    const QString text = state + QLatin1Char('\n') + basis;
    // "说不清"比"说不行"更弱：语法不支持 / 范围无法确定这类先归未知，别被下面的关键词捡走。
    if (text.contains(QCoreApplication::translate("UpgradeReport", "尚不支持")) || text.contains(QCoreApplication::translate("UpgradeReport", "无法解析"))
        || text.contains(QCoreApplication::translate("UpgradeReport", "无法确定")) || text.contains(QCoreApplication::translate("UpgradeReport", "无法确认"))) {
        return Bucket::Unknown;
    }
    // 先判"明确不行"：这类话一出现，别的关键词都只是解释。
    if (text.contains(QCoreApplication::translate("UpgradeReport", "不支持")) || text.contains(QCoreApplication::translate("UpgradeReport", "明确排除"))
        || text.contains(QCoreApplication::translate("UpgradeReport", "不满足")) || text.contains(QCoreApplication::translate("UpgradeReport", "不兼容"))) {
        return Bucket::Incompatible;
    }
    // "缺前置"是另一种明确不行：缺的是别的东西，不是这个模组本身不兼容。
    if (text.contains(QCoreApplication::translate("UpgradeReport", "缺前置")) || text.contains(QCoreApplication::translate("UpgradeReport", "缺少必要"))
        || text.contains(QCoreApplication::translate("UpgradeReport", "缺少目标加载器声明"))) {
        return Bucket::MissingDependency;
    }
    if (text.contains(QCoreApplication::translate("UpgradeReport", "失败")) || text.contains(QCoreApplication::translate("UpgradeReport", "读取失败"))
        || text.contains(QCoreApplication::translate("UpgradeReport", "查询失败"))) {
        return Bucket::Failed;
    }
    if (text.contains(QCoreApplication::translate("UpgradeReport", "已确认兼容")) || text.contains(QCoreApplication::translate("UpgradeReport", "一致"))) {
        return Bucket::Compatible;
    }
    return Bucket::Unknown;
}

Bucket bucketForAssessmentClass(const QString &key) {
    if (key == QStringLiteral("compatible")) return Bucket::Compatible;
    if (key == QStringLiteral("incompatible")) return Bucket::Incompatible;
    if (key == QStringLiteral("missing")) return Bucket::MissingDependency;
    if (key == QStringLiteral("failed")) return Bucket::Failed;
    return Bucket::Unknown;
}

QList<Row> fromAssessmentRows(const QList<QStringList> &rows) {
    QList<Row> out;
    out.reserve(rows.size());
    for (const QStringList &raw : rows) {
        if (raw.size() < 3 || raw.value(0).isEmpty()) {
            continue;
        }
        Row row;
        row.name = raw.value(0);
        row.detail = raw.value(2).isEmpty() ? raw.value(1) : raw.value(2);
        const QString assessed = raw.value(7);
        row.bucket = assessed.isEmpty() ? bucketForStateText(raw.value(1), raw.value(2))
                                        : bucketForAssessmentClass(assessed);
        row.required = true;
        out.append(row);
    }
    return out;
}

int unresolvedRequired(const QList<Row> &rows) {
    int count = 0;
    for (const Row &row : rows) {
        if (row.required && row.bucket != Bucket::Compatible) {
            ++count;
        }
    }
    return count;
}

}  // namespace UpgradeReport




