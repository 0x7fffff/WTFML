/**
 * @file PresetApply.hpp
 * @brief 把一份预设**落到**一个实例上：读目标快照 → 算计划 → 备份 → 执行（执行链）。
 *
 * 【为什么这一层要存在】
 *
 * `PresetPlanner` 只算"会做什么"（**只读**），四个适配器只会"把这一段落下去"。把它们串成
 * 一条能用的流程还有三件事：**目标快照怎么读**、**计划在哪儿再算一遍**、**动手之前先备份
 * 哪些文件**。三件事都需要同时认识四个适配器，所以它们只能待在这一层 —— 让界面自己去按
 * 段种类分派，同一个"先 options 后 servers"的次序就会在管理页和安装流程里各写一遍。
 *
 * 【两趟协议是照着 `PresetPlanner` 那一份实现的】
 *
 * 第一趟 `replaceConfirmed = false`：计划里任何一段是「覆盖」都会让结果 `needsConfirm`，
 * **一个字节都不写**。用户确认之后，调用方再以 `replaceConfirmed = true` 叫一次。这不是
 * 本层另立的规矩：`planPresetApplication()` 文件头那段"两趟"说的就是它，`ApplyPlan::canApply()`
 * 也是那么判的。
 *
 * 【备份什么、备份到哪儿】
 *
 * 只备份**这一趟真的会被改写或删除**的那些**已存在**的文件（目标上本来没有的不备份 ——
 * 没东西可丢）。落点：
 *
 * ```
 * <presets 根>/backups/<实例 id>/<yyyyMMdd-HHmmss>/
 * ```
 *
 * 里面按照**相对实例根的原路径**摆（`options.txt`、`servers.dat`、
 * `config/xxx.json`、`resourcepacks/yyy.zip`）。用户要手工还原时，把那一棵照原样拷回去
 * 就是还原，不需要我们再写一个还原器 —— 而"再写一个还原器"正是"先备份再原子
 * 替换"最容易被做坏的附件。
 *
 * 备份失败 ⇒ **整趟不开始**（不是"备份不上就算了"）。只在有东西要被覆盖时才建备份目录；
 * 一趟全是"缺了才拷"的时候不建空目录。
 *
 * 【写失败之后不回滚】
 *
 * 和适配器那几条同一条纪律：一份写失败就收手、前面已经写下去的留在盘上，整趟回滚是备份那
 * 一层的事。区别只在于这一层会把"哪一段做完了、哪一段没做成"逐行报出来 —— 用户
 * 看得到该拿备份还原哪几样。
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "MalilibKeys.hpp"
#include "PresetPlanner.hpp"
#include "PresetTypes.hpp"

namespace Presets {

/**
 * @brief 目标实例的**快照**（`PresetPlanner::TargetSnapshot` 的读取器）。
 *
 * 读 `options.txt` 和 `servers.dat` 两样；通用文件组和 MaLiLib 那几段要的东西由
 * `PresetPlanner` / 适配器自己按盘上的路径去看（那两口本来就收 `instanceRoot`）。
 *
 * @param problemOut `options.txt` **存在但读不动**时的一句人话。这是唯一让它非空的来路：
 * 没有 `options.txt` 是正常开局（新实例就是没有），而读不动的那一份绝不能当成
 * 空的来铺预设 —— 那会把用户整份设置换成预设里那几个键（"合并不是覆盖"在那一刻
 * 已经不成立了）。
 */
TargetSnapshot readTargetSnapshot(const QString &instanceRoot, QString *problemOut = nullptr);

// ---------------------------------------------------------------------------
// 执行
// ---------------------------------------------------------------------------

struct PresetApplyRequest {
    QString instanceId;
    /// 实例目录（`<数据目录>/versions/<id>`）。
    QString instanceRoot;
    /// 这个实例现在正在跑。真了整趟不写（理由见 `PresetPlanner::PlanRequest::gameRunning`）。
    bool gameRunning = false;
    /// 用户已经明确点过"覆盖"了。第一趟**必须**是 false。
    bool replaceConfirmed = false;
    /// 这个实例的游戏版本有没有数据包（默认 true，见 `TargetSnapshot::supportsDataPacks`）。
    bool supportsDataPacks = true;
};

/// 每一段这一趟怎么样（= 计划表右边那一列的"实际版"）。
struct PresetApplyRow {
    QString sectionId;
    SectionKind kind = SectionKind::Custom;
    ApplyMode mode = ApplyMode::Merge;
    /// 段自己的标题（可能空 —— 界面按 `kind` 现翻）。
    QString title;

    bool ok = false;       ///< 这一段办完了（`Skip` 也算办完）
    bool blocked = false;  ///< 计划阶段就做不了 / 执行时读不动
    bool skipped = false;  ///< 这一段这一趟就是「跳过」

    int written = 0;       ///< 真写进去的项数
    int added = 0;         ///< 其中目标上原来没有的
    int removed = 0;       ///< 被拿掉的（覆盖档）
    int skippedItems = 0;  ///< 预设里有、这一趟没动的项数
    /// 目标上还没有对应世界、记成「待应用」的项数（数据包那一段）。★ 它**不是** `written`：
    /// 东西没进任何存档，等那个世界建出来之后才挪得进去（见 `DataPackPreset.hpp`）。
    int deferred = 0;

    /// 一句话（`blocked` 时是给人看的原文，**原样能显示**）。
    QString problem;
    /// 逐条的人话（读不动 / 类型不符……原样能显示）。
    QStringList details;
};

struct PresetApplyResult {
    bool ok = false;
    /// 计划里有「覆盖」而用户还没确认。★ 这时**盘上一个字节都没动**。
    bool needsConfirm = false;
    /// 整趟没办成的一句人话（计划算不出来 / 快照读不动 / 备份失败……）。
    QString problem;
    /// 整趟不许开始（游戏在跑）。非空 ⇒ 同样一个字节都没动。
    QString blockedReason;

    /// 备份落在哪儿（空 = 这一趟没有需要备份的东西）。
    QString backupDir;

    QList<PresetApplyRow> rows;
    QList<MalilibConflict> keyConflicts;

    int written = 0;
    int removed = 0;
    int deferred = 0;
    QStringList details;

    /// 这一趟是不是**什么都没写**（跳过 / 要选择 / 全做不了）。
    bool wroteAnything() const { return written > 0 || removed > 0; }
};

/**
 * @brief 把一份预设落到实例上。
 *
 * @param presetsRoot 预设根（`PresetStore::presetsRoot()` 或验证工程的临时目录）。
 * 这一层用它推两件事：`PresetStore::filesDir()`（通用文件组那几段的源）和备份
 * 落点。`manifest.id` 不合法 ⇒ 直接失败。
 *
 * 做的事（次序固定）：
 * 1. 读目标快照（`readTargetSnapshot()`）；
 * 2. `planPresetApplication()` 算一遍 —— 算不出来 / 游戏在跑 / 需要确认 ⇒ 到此为止；
 * 3. 算"哪些已存在的文件会被改写"，**先把它们备份走**；
 * 4. 按段次序执行：`options.txt` 两段在内存里叠加、最后**一次**原子写出；服务器列表
 * 逐段落；通用文件组逐段落；MaLiLib 逐段落。
 *
 * 第 4 步的次序就是计划表的次序（= manifest 里 `sections` 的次序）。把 `options.txt`
 * 两段合成一次写，是因为它们落在同一个文件上：分两次写会让第二段读到第一段刚写下去的
 * 那一份（那样也对，但会多一次原子替换、多一个中途状态）。
 */
PresetApplyResult applyPresetToInstance(const QString &presetsRoot, const PresetManifest &manifest,
                                        const PresetApplyRequest &request);

}  // namespace Presets
