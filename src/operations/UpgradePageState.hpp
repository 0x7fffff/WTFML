/**
 * @file UpgradePageState.hpp
 * @brief 迁移式升级页的**状态模型**：来源/目标、报告是否还有效、名字校验、能不能确认。
 *
 * 【为什么先做状态、不先做控件】
 *
 * 页面上真正容易错的是这几条判断，而不是布局：
 * - 目标版本 / 加载器一改，**上一次兼容性检查就作废**（不能拿旧结论去确认）；
 * - 新实例名不能为空、不能和现有实例重名（重名要当场拦住，不能等写到一半才发现）；
 * - "至少选了一类" + "报告有效" + "名字合法" 三个条件合成一个 `canConfirm()`。
 *
 * 把它们放在这里（纯逻辑、不碰控件）就能先用 `verify_instances` 钉住，页面只负责把
 * 状态画出来、把用户的动作转发进来。控件层（U1 的 UI 部分）随后接。
 */
#pragma once

#include <QString>
#include <QStringList>

#include "UpgradeSelection.hpp"

namespace UpgradePageState {

/// 目标那一栏（新实例）。`loaderKey` 为空 = 原版。
struct Target {
    QString gameVersion;
    QString loaderKey;
    QString loaderVersion;
    QString instanceName;
};

/// 用户此刻看到的整页状态。
struct State {
    // 来源（进入页面时锁定，页面内不允许改）
    QString sourceId;
    QString sourceGameVersion;
    QString sourceLoaderKey;
    QString sourceLoaderVersion;
    /// 来源实例现有的名字集合之外的重名判断由调用方给（见 `nameTaken`）
    Target target;
    UpgradeSelection::Plan selection;
    /// 来源实例此刻是不是"正被占用"（在跑 / 有别的作业压着）。占用时不允许开始迁移。
    bool instanceBusy = false;
    QString instanceBusyReason;
    /// 报告是否还有效；`checkedFingerprint` 是生成报告时的计划指纹。
    bool reportValid = false;
    QString checkedFingerprint;
};

/// 挡住"确认迁移"的第一条原因（`None` = 可以确认）。
enum class Blocker {
    None,
    TargetIncomplete,  ///< 目标版本 / 加载器组合没填全
    InstanceBusy,      ///< 来源实例正在运行或被别的作业占用
    NameEmpty,
    NameTaken,
    NoSelection,       ///< 一类都没选
    ConfigWithoutMods, ///< 勾了 Mod 配置却没勾 Mod 文件（归属不能按文件名猜）
    ReportMissing,     ///< 还没做过兼容性检查
    ReportStale,       ///< 检查过，但目标或选择之后又改过
};

/// 目标版本 / 加载器 / 选择参与指纹；来源与目标名字不参与
/// （改名字不该让检查作废，改目标才该）。
QString planFingerprint(const State &state);

/// 改目标版本；值真的变了才让报告作废（改成同一个值不算）。
/// @return 是否发生了变化。
bool setTargetVersion(State &state, const QString &gameVersion);
bool setTargetLoader(State &state, const QString &loaderKey, const QString &loaderVersion);

/// 改新实例名；空名照收（由 `firstBlocker()` 拦住并说明），重名交给 @p nameTaken 判。
/// @return 名字是否合法（非空且不重名）。
bool setInstanceName(State &state, const QString &name, const QString &sourceId,
                     const QStringList &existingNames);

/// 这一发"检查完成"：把当前指纹记下来（之后目标/选择再变，报告自动失配）。
void markReportChecked(State &state);

/// 挡住确认的第一条原因（按"先目标、再名字、再选择、再报告"的顺序）。
Blocker firstBlocker(const State &state);

/// 给用户看的一句话（`None` 回空串）。
QString blockerText(Blocker blocker);

}  // namespace UpgradePageState


