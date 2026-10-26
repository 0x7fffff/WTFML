#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <functional>

namespace InstanceUpgrade {
// Worker-only disk transaction. The caller locks both instances and prepares
// candidateDir() with core files already named for instanceId.
enum class Stage {
    Checking,
    Snapshot,
    VerifySnapshot,
    RetainOriginal,
    Publish,
    Verify,
    Done,
    /// 副本模式专有：把源实例整棵拷进暂存，或校验那份暂存。
    /// 原地升级**不经过**这两档（它走 Snapshot / VerifySnapshot 那条 ZIP 快照的路）。
    Copy,
    VerifyCopy,
};
struct Request {
    QString gameDir;
    QString instanceId;
    QString transactionId;
    QString label;
    // Optional operation quota, can only reduce the measured free-space budget.
    qint64 spaceBudgetBytes = -1;
    QByteArray originalFingerprint{};
};
struct Result {
    bool ok = false;
    bool rolledBack = false;
    bool rollbackAvailable = false;
    /// 用户按了取消、引擎在安全点停下（原实例未改变，或已自动恢复）。
    /// 和 `problem` 分开：取消不是失败，调用方不该亮红点、也不该弹失败提示。
    bool cancelled = false;
    QString problem;
    QString snapshotZip;
    int disabledMods = 0;
    bool requiresReload = false;
    /// 实际写进候选目录的替换/新增（任务详情与变更日志共用一套说法）。
    QStringList changes;
};
/// 一份“平台适配版”模组文件：已经在暂存里下好并校验过，等事务把它放进候选目录。
///
/// 2.1.3 的替换语义：**只改候选目录**（副本的候选或原地升级的暂存），因此任何一步失败
/// 都能整趟回滚 —— 源实例/原目录一个字节都没动。嵌套 JAR 不单独成项：它们跟着父 JAR
/// 一起替换（`replaces` 是父 JAR 的文件名）。
struct ModReplacement {
    QString stagedPath;      ///< 暂存里的源文件（绝对路径，必须是普通文件）
    QString fileName;        ///< 目标 `mods/<name>`（只是文件名，不含路径）
    QString sha1;            ///< 期望 SHA-1（40 位十六进制）
    qint64 size = -1;        ///< 期望大小；-1 = 不判
    QString replaces;        ///< 要移除的旧文件名（可为空 = 只新增）
    QString projectKey;      ///< `平台:项目号`（写进变更日志）
    QString versionId;       ///< 平台发行版 ID（写进变更日志，也用于确认与预览一致）
    QString title;           ///< 给用户看的名字
    QString fromVersion;     ///< 旧版本号（可空）
    QString toVersion;       ///< 新版本号（可空）
    bool fromDependency = false; ///< 必要前置（不是用户逐项点选的模组）
};

struct Core {
    QJsonObject mergedJson;
    QString clientJar;
    QString gameVersion;
    QString loaderName;
    QString loaderVersion;
    bool disableOldMods = true;
    // Relative paths under mods/ explicitly retained by the reviewed update plan.
    QStringList keepOldMods;
    /// 平台适配版替换（已下载并校验）。空 = 这一趟只换核心。
    QList<ModReplacement> replacements;
};
QString transactionDir(const Request &request);
QString candidateDir(const Request &request);
QString dependentInstanceProblem(const QString &gameDir, const QString &instanceId);
// Preserves all original user files and identity, replaces the merged core and
// client jar only. The target instance's saves/mods/config are never imported.
Result prepareAndCommit(Request request, const Core &core,
                        const std::function<void(Stage)> &progress = {},
                        const std::function<bool()> &cancelled = {});
/// 副本模式（2.1.1 的空间优化）：**源实例保持不动**，只把源目录逐文件复制成
/// `versions/<新名字>` 的候选、逐文件完整校验候选，再换核心并发布。
///
/// 和 `prepareAndCommit()` 的差别只有一处，而且是用户确认过的取舍：**不再为副本另做
/// 一整份 ZIP 快照**（源实例本身就是回滚材料，它一个字节都不会被改），所以空间占用从
/// 「复制 + 快照 + 快照校验解压 + 候选副本」四次降到一次复制。校验一点没少：
/// 复制后按路径/大小/SHA-1 对一遍，发布后再对一遍。
///
/// @param sourceDir 源实例目录（`<gameDir>/versions/<源 id>`）；只读。
/// @param request.instanceId 是**新副本**的名字，它不能已经存在。
Result prepareCopyAndCommit(Request request, const Core &core, const QString &sourceDir,
                            const std::function<void(Stage)> &progress = {},
                            const std::function<bool()> &cancelled = {});
Result commitPrepared(const Request &request,
                      const std::function<void(Stage)> &progress = {},
                      const std::function<bool()> &cancelled = {});
// Restores the retained original by renaming; preserves the replaced version as
// well. Never deletes an existing directory to make a rollback succeed.
// Cancellation is only honoured during the verification phase; the final renames
// are a short, atomic critical section and are deliberately not interruptible.
Result rollback(const Request &request,
                const std::function<bool()> &cancelled = {});
// Only returns journals in this game's private transaction directory for this id.
QStringList recoverableTransactions(const QString &gameDir, const QString &instanceId);
QStringList recoverInterrupted(const QString &gameDir);
/// 在**已知的候选游戏根**里发现并恢复中断事务（2.1.5a）。
///
/// 整个游戏目录被改名/搬走之后，journal 还在根目录里，但里面记的绝对 `gameDir`
/// 已经对不上。每个根自己的 journal 用 `rootFingerprint` 验证；验证不过的不动盘，
/// 只留一句“请使用完整 ZIP 快照恢复”。只扫调用方给的根（启动器文件夹列表），
/// 不全盘搜索，也不删除任何未知目录。
QStringList recoverInterruptedInRoots(const QStringList &gameDirs);
} // namespace InstanceUpgrade
