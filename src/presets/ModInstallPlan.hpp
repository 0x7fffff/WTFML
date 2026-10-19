/**
 * @file ModInstallPlan.hpp
 * @brief 模组预设「真正去下」之前的判据层（coding plan §10.1 第 7 项 / P5）。
 * @author 0x7fffff
 * @date 2026-10-18
 *
 * 【这一层解决的四件事】
 *
 *   · **必要 / 可选分开**：预设里每一条引用自己带 `required`；平台回答里每条前置带
 *     `required` / `optional` / `incompatible` / `embedded`。必要项失败 ⇒ 整份计划 `ok = false`
 *     （界面要明确标出来）；可选项失败只计数，不许把整份安装拖垮。
 *   · **依赖**：把「这一版声明要的前置」折进同一张下载清单，按 `平台:项目号` 去重、带环检测；
 *     `optional` 只列出来**不自动下**（那正是"可选失败"的语义），`embedded` 无视。
 *   · **冲突**：平台声明的 `incompatible` 指向的那一项如果也在这一趟里（或已经装在实例里），
 *     这一条就是「不兼容」，**不装** —— 含不兼容文件的计划不算成功。
 *   · **替代项目必须确认**：某一条在平台上确实没有兼容发行版时，调用方可以递一份候选替代；
 *     这一层**只报「等你确认」**，用户确认过（`confirmedAlternatives` 里有那个 key）才换成它。
 *     绝不自己挑一个看起来像的。
 *
 * 【为什么不发网络、不读实例盘】
 *
 * 平台答复（版本列表 / 失败原因）、实例里已经装了什么、用户确认过哪个替代 —— 全是调用方
 * 读好递进来的。这一层只做判断，所以 `verify` 能拿几份手搭的假答复逐格念。
 * 唯一碰盘的是 `verifyDownloadedFile()`（那本来就是"落盘校验"那一步）。
 *
 * 【"不错装不兼容文件"落到哪一条】
 *
 * 挑版本时**先本地再信平台**：版本自己的 `gameVersions` / `loaders` 里对不上目标上下文的
 * 一律不要（接口筛过一遍也不等于这一条一定兼容：CurseForge 的筛法是按文件那一侧，而
 * 版本列表可能是调用方缓存里更早的一次）。这两格为空时**不判**（"不知道"不是"不兼容"）。
 */

#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "PresetTypes.hpp"
#include "download/ResourceCatalog.hpp"

namespace Presets {

// ---------------------------------------------------------------------------
// 输入
// ---------------------------------------------------------------------------

/// 一条引用 + 平台那一侧问回来的东西。
struct ModResolveInput {
    ModReference reference;
    /// 问过平台没有。★ 没问过与问失败是两件事（前者是"还没轮到"，后者是"网络/接口"），
    /// 但在用户眼里下一步都是"再试一次"，所以两者都落 `Unknown`、理由分开写。
    bool queried = false;
    bool queryFailed = false;
    QString queryProblem;
    /// 项目名（取不到就空 —— 界面退成项目号）。
    QString title;
    /// 平台给的版本列表（新的在前，和接口一致）。
    QList<ResourceCatalog::VersionEntry> versions;
};

/// 这一趟要算什么。
struct ModInstallRequest {
    /// 安装上下文（目标实例的游戏版本 / 加载器）。引用自己钉了值的以引用为准。
    QString gameVersion;
    QString loader;

    /// 预设里的引用（顺序 = 计划表的顺序）。
    QList<ModResolveInput> references;

    /// 前置问回来的版本列表：键 = `平台:项目号`（和 `ModReference::key()` 同一套写法）。
    QHash<QString, QList<ResourceCatalog::VersionEntry>> dependencyAnswers;
    /// 前置的名字（键同上；取不到就空）。
    QHash<QString, QString> dependencyTitles;

    /// 目标实例里已经认出来的资源（键同上）。
    QSet<QString> installedKeys;
    /// 平台发行版 ID（不是展示版本号）。仅确认项目存在不能证明精确前置满足。
    QHash<QString, QString> installedVersionIds;

    /// 某一条缺失时**候选的替代项目**（键 = 原引用 `key()`）。空 = 没有候选。
    QHash<QString, QList<ModReference>> alternativesByKey;
    /// 用户已经确认要用的替代（`ModReference::key()` 的集合）。
    /// ★ 替代项自己也得出现在 `references` 里（带平台答复），这一层不替它问网络。
    QSet<QString> confirmedAlternatives;

    /// 依赖展开的最大层数（防环 + 防接口给出病态的长链）。默认 8。
    int maxDependencyDepth = 8;
};

// ---------------------------------------------------------------------------
// 输出
// ---------------------------------------------------------------------------

enum class ModInstallStatus {
    Download,              ///< 要下（版本、文件、大小、sha1 都齐了）
    AlreadyInstalled,      ///< 实例里已经有这个项目
    Missing,               ///< 平台明确说没有兼容发行版
    Unknown,               ///< 没问到 / 问失败 —— **不是缺失**
    Incompatible,          ///< 和这一趟里 / 实例里的另一项冲突
    AlternativeNeedsConfirm,  ///< 缺失，但有候选替代，等用户点头
    Blocked,               ///< 引用本身就认不出来（平台 / 项目号坏了）
};

struct ModInstallRow {
    ModReference reference;
    QString key;
    QString title;
    bool required = true;
    /// 这一行是不是从别处的「必要前置」展开出来的（界面上可以缩进一格）。
    bool fromDependency = false;

    ModInstallStatus status = ModInstallStatus::Missing;

    ResourceCatalog::VersionEntry version;
    ResourceCatalog::FileEntry file;
    QString problem;       ///< 一句人话（**原样能显示**）
    QStringList details;   ///< 逐条补充（可选依赖、替代候选……）

    bool substituted = false;    ///< 这一行最后用的是替代项目
    QString substitutedKey;

    bool isDownload() const { return status == ModInstallStatus::Download; }
    /// 这一行算不算失败（缺 / 说不清 / 不兼容 / 认不出来 / 等确认）。
    bool isFailure() const {
        return status == ModInstallStatus::Missing || status == ModInstallStatus::Unknown ||
               status == ModInstallStatus::Incompatible ||
               status == ModInstallStatus::AlternativeNeedsConfirm ||
               status == ModInstallStatus::Blocked;
    }
};

struct ModInstallPlan {
    /// 可以继续吗：**没有「必要项」失败**。可选失败不影响这个值。
    bool ok = true;

    QList<ModInstallRow> rows;
    /// 平台声明为 optional 的前置（没有自动下）。
    QStringList optionalDependencyKeys;
    /// 冲突的一句话（每一条都点名：谁和谁）。
    QStringList conflicts;

    int download = 0;
    int alreadyInstalled = 0;
    int missing = 0;
    int unknown = 0;
    int incompatible = 0;
    int needsConfirm = 0;
    int blocked = 0;
    int substituted = 0;

    /// 必要项里失败了几条。
    int requiredFailures() const;
    /// 可选（含依赖展开出来的可选行）失败了几条。
    int optionalFailures() const;
    bool hasRequiredFailure() const { return requiredFailures() > 0; }
    /// 这一趟最终要下的文件（只数 `Download`）。
    QList<ModInstallRow> downloads() const;
};

// ---------------------------------------------------------------------------
// 算
// ---------------------------------------------------------------------------

ModInstallPlan planModInstall(const ModInstallRequest &request);

// ---------------------------------------------------------------------------
// 落盘校验
// ---------------------------------------------------------------------------

struct DownloadVerification {
    bool ok = false;
    /// 接口没有给 sha1（只按大小对上了 / 连大小都没有）—— 这不是"校验通过"，
    /// 调用方在报告里要如实说一句。
    bool unverified = false;
    qint64 size = -1;
    QString sha1;
    QString problem;
};

/**
 * @brief 下载完之后对一遍大小与 sha1（**流式读，不把整个 jar 读进内存**）。
 *
 * @param expectedSize -1 = 接口没给大小（不判）。
 * @param expectedSha1 空 = 接口没给哈希（不判，并置 `unverified`）。
 */
DownloadVerification verifyDownloadedFile(const QString &path, qint64 expectedSize,
                                          const QString &expectedSha1);

// ---------------------------------------------------------------------------
// 界面上的字（唯一出处）
// ---------------------------------------------------------------------------

QString modInstallStatusText(ModInstallStatus status);
/// 「下载 3 个，已经在实例里 2 个，缺失 1 个（必要 1）」这种一句总账。
QString modInstallSummaryText(const ModInstallPlan &plan);
/// 「按你确认换成了：X」。
QString modInstallSubstitutedText(const QString &title);
/// 「这几个有替代项目，换不换要你点头：A、B」。
QString modInstallAlternativeText(const QStringList &names);
/// 基础实例装好了、但预设没全成时那一句。
QString modInstallPartialText();

}  // namespace Presets
