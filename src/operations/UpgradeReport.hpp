/**
 * @file UpgradeReport.hpp
 * @brief 兼容性报告的**分组与筛选**（U3 的判断层）：不碰控件，只回答"这一条属于哪一组、
 *        默认折起来没有、搜索/筛选之后还剩谁"。
 *
 * 报告本身的数据仍由 `UpgradeAssessment` 产出（本地声明 + 平台查询）；这一层只做**读侧**：
 * 规范要求"正常兼容项折叠、未知/失败/不兼容/缺前置分开"，以及弹窗里可搜索、可筛选。
 * 把这些判断放这里，弹窗只负责摆控件，`verify_instances` 直接钉。
 *
 * 分组的边界（和规范 3.1 的措辞对齐）：
 * - `Compatible`：**已确认**兼容（本地声明与平台给的目标发行版对得上）；"没发现问题"
 *   不等于这一档，它落在 `Unknown`；
 * - `Unknown`：信息不足、认不出来、查询失败但还没到"不可用"；
 * - `Failed`：查询/下载等动作本身失败；
 * - `Incompatible`：明确不兼容（加载器/游戏版本对不上）；
 * - `MissingDependency`：缺必要前置。
 */
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace UpgradeReport {

enum class Bucket {
    Compatible,
    Unknown,
    Failed,
    Incompatible,
    MissingDependency,
};

/// 报告里的一条（名字 + 一句话依据 + 分组）。
struct Row {
    QString name;
    QString detail;
    Bucket bucket = Bucket::Unknown;
    /// 必要项（缺了不能继续）；只影响排序与"必要项未处理"的汇总，不改分组。
    bool required = false;
};

/// 五组的展示顺序（正常项在最前，因为默认折起来之后它就是那一行标题）。
QList<Bucket> buckets();

/// 组标题（界面直接用）。
QString bucketTitle(Bucket bucket);

/// 这一组默认是不是折起来的（只有正常兼容项折）。
bool foldedByDefault(Bucket bucket);

/// 每组的条数（空的组也给 0，方便界面画"标题 (0)"还是干脆不画）。
QHash<Bucket, int> counts(const QList<Row> &rows);

/// 按搜索词（名字或依据里包含，大小写不敏感）+ 只显示某组（`Bucket` 传 `-1` 不好，
/// 用 `hasOnly`）+ 是否展开正常项，筛一遍；顺序保持输入顺序。
QList<Row> filtered(const QList<Row> &rows, const QString &query, bool hasOnly,
                    Bucket only, bool showCompatible);

/// 必要项里尚未处理（`Unknown` / `Failed` / `Incompatible` / `MissingDependency`）的条数。
int unresolvedRequired(const QList<Row> &rows);

/// 把 `UpgradeAssessment` 那句状态（以及依据）归到五组里。
///
/// 评估层给的是给人看的一句话（"本地声明不支持" / "缺少目标加载器声明" / "已确认兼容"…），
/// 没有枚举；这里按关键词归一，顺序是"先判死、再判缺、再判失败、最后才算兼容"——
/// 同一句话里同时出现两种关键词时，"不支持"比"声明"更该赢。
Bucket bucketForStateText(const QString &state, const QString &basis);

/// 评估层给出的分类（`compatible` / `incompatible` / `unknown` / `missing` / `failed`）。
/// 报告优先用它，**不要**再从界面文字里猜：同一句"符合"配不同平台文字会被猜错。
Bucket bucketForAssessmentClass(const QString &key);

/// 把评估层原始行（`[名称, 状态, 依据, 文件, 默认处理, 分组标记, 替换版本, 分类]`）
/// 转成报告行。报告一律走这里，不再从界面文字反推。
QList<Row> fromAssessmentRows(const QList<QStringList> &rows);

}  // namespace UpgradeReport




