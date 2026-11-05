/**
 * @file UpgradeSelection.hpp
 * @brief 迁移式升级里"搬哪几类内容"的**选择状态**：九类稳定键、默认勾选、指纹与依赖提示。
 *
 * 【为什么单开一层】
 *
 * `TransferPlanner` 已经会扫盘、算体积、给默认值；但新升级页（规范 U1/U2）还要把用户
 * **改过之后**的那份选择记下来，并且：
 * - 按**稳定的类别键**记（落盘、历史、报告都认它，不认展示名）；
 * - 校验"目标一改，旧的检查与选择就作废"（指纹变了就是另一份计划）；
 * - 说出类别之间的依赖（没搬模组却搬了配置、没搬资源包却搬了启用列表）。
 *
 * 这些判断不碰控件、不碰盘，所以单独放这里、由 `verify_instances` 直接钉。
 *
 * 【九类与默认值】
 *
 * 顺序 = 规范里三列网格的阅读顺序；`screenshots` / `schematics` 默认不勾（和
 * `TransferPlanner::neverDefaultSelected()` 同一套理由：记录/素材，不是运行必需品）。
 * 存档里的 `datapacks/` 随存档走，**不再单列一类**（规范 4.3 的去重要求）。
 */
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "TransferPlanner.hpp"

namespace UpgradeSelection {

/// 一类可迁移内容。`key` 是稳定键（落盘/历史/报告只认它）。
struct Category {
    QString key;
    QString label;
    bool defaultSelected = true;
};

/// 九类，按页面阅读顺序；键唯一。
QList<Category> categories();

/// 只需要键的时候（`categories()` 的顺序）。
QStringList keys();

/// @p key 是不是九类之一。
bool known(const QString &key);

/// 当前选择。`selected` 永远按 `keys()` 的顺序、无重复。
struct State {
    QStringList selected;

    bool contains(const QString &key) const;
    /// 勾上 / 取消一类；未知键回 false 且不改动。`true` 表示这一发被接受。
    bool set(const QString &key, bool on);
    void selectAll();
    void clear();
    /// 按 `keys()` 顺序给出真正选中的那些（`selected` 自身已保证顺序）。
    QStringList ordered() const;
};

/// 一次选择所属的计划身份。来源/目标一变，指纹就变（旧检查结果与旧选择都不再算数）。
struct Plan {
    bool ok = true;
    QString problem;
    QString sourceId;
    QString targetId;
    State state;
};

/// 按 @p items（`TransferPlanner` 扫出来的那些）建一份初始计划：
/// 只收九类里认得的键；默认值取 `categories()`；盘上不存在的项不进入已选集合
/// （`item.exists == false` 的项列得出来，但不算"选了"）。
/// @p items 为空时回一份 `ok = false`（没有可迁移内容）。
Plan makePlan(const QList<TransferPlanner::Item> &items, const QString &sourceId,
              const QString &targetId);

/// 稳定指纹：来源 + 目标 + 按顺序的已选键。用于"目标改了 ⇒ 旧检查作废"。
QString fingerprint(const Plan &plan);

/// 类别之间的依赖提示（空 = 没有要提醒的）。每条都是一句给人看的话。
QStringList warnings(const Plan &plan);

/// `TransferPlanner::Item::key` → 九类里的哪一类（不认识的回空串）。
///
/// 现有的 `TransferPlanner` 是按"目录行"列的，跟规范的九类不是一一对应：
/// `structure` 是原理图那一类的第一段路径；`defaultconfigs` 归入「Mod 配置」；
/// `logs` / `crashreports` 不属于九类（记录类不随升级搬）。
QString categoryKeyForPlannerKey(const QString &plannerKey);

/// 这一类在 @p items 里对应的所有项（`config` 会同时命中 `config` 与 `defaultconfigs`）。
QList<TransferPlanner::Item> itemsFor(const QList<TransferPlanner::Item> &items,
                                      const QString &categoryKey);

/// 某一组类别合计多少文件 / 多少字节（不存在或没选中的项不算）。
struct Amounts {
    int files = 0;
    qint64 bytes = 0;
};

Amounts amounts(const QList<TransferPlanner::Item> &items, const QStringList &categoryKeys);

/// 当前选择合计多少（底部摘要那一行）。
Amounts selectedAmounts(const Plan &plan, const QList<TransferPlanner::Item> &items);

/// 某一类对应的顶层路径（和 `selectedRoots()` 同一张表；只查一类时用）。
QStringList rootsFor(const QString &categoryKey);

/// 扫这些顶层路径里有多少"链接"（符号链接 / junction）。升级的复制链路**不跟随链接**，
/// 遇到会直接失败；这里在选内容时就把数量报出来，别等点到确认才报错。
struct Links {
    int count = 0;
    QStringList samples;  ///< 前几条相对路径（给人看，最多 3 条）
};
Links scanLinks(const QString &sourceDir, const QStringList &roots);

/// 选中类别对应的**相对实例目录的顶层路径**（给"只复制选中内容"用）。
///
/// 空列表 = 什么都没选；`config` 给两条（`config/` 与 `defaultconfigs/`），
/// `schematics` 给 `structures/`（`schematics/` 目录的接入在 U4 里补）。
/// 返回的每条都是单层相对路径，调用方要按现有的路径规矩再校验一遍。
QStringList selectedRoots(const Plan &plan);

}  // namespace UpgradeSelection



