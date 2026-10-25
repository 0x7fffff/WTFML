/**
 * @file OperationReport.hpp
 * @brief 一次**文件操作**的共同报告：逐项结果、同名冲突策略、可续的暂存位置和失败原因。
 *
 * 世界导入、迁移 / 升级副本、批量文件改动、单文件复制以前各说各的话（有的只报一句
 * "有 N 个文件没搬成"，有的把失败清单塞进弹窗文本）。这一层把它们收成同一份形状：
 *
 * · `entries` 逐项记“哪一项、到没到、为什么没到”；
 * · `conflictPolicy` 是**用户在冲突那一刻选的那个**（取消 / 跳过已有项 / 覆盖），
 *   续做时沿用同一个选择，不重新问一遍；
 * · `stagingDir` 是还没发布的暂存。它在盘上、路径可核对；只有本模块的
 *   `stagingDirectory()` 造出来的目录才会被自动清理（见 `cleanupOwnedStaging()`），
 *   用户自己的目录一律不碰；
 * · `failureText()` 回答“为什么做不完、接下来能做什么”，由恢复入口写成
 *   `<报告>.failure.txt` 给用户看。
 *
 * 报告本身是普通 JSON，读写都有上限（4 MiB / 两万条）：它可能来自上一次会话，
 * 不能把一份坏文件当命令执行，也不接受越界路径。
 */

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace OperationReport {

/// 报告属于哪一类文件操作。**只影响措辞和“续做时用哪个执行器”**。
enum class Kind {
    WorldImport,
    InstanceTransfer,
    FileCopy,
    Batch,
    /// 升级里的「平台适配模组」下载/替换（2.1.3）；失败时整趟更新回滚。
    ModReplacement,
};

QString kindKey(Kind kind);
Kind kindFromKey(const QString &key);

/// 同名时怎么办。用户确认过的两种是 `Cancel` 与 `SkipExisting`；`Overwrite` 只留给
/// 原本就提供了“盖掉”的单文件入口（世界导入永远不允许覆盖存档）。
enum class ConflictPolicy {
    Cancel,
    SkipExisting,
    Overwrite,
};

QString conflictPolicyKey(ConflictPolicy policy);
ConflictPolicy conflictPolicyFromKey(const QString &key);
/// 界面上那一问给用户看的三个选项（顺序固定：取消 / 跳过 / 覆盖）。
QStringList conflictChoices();
QString conflictPolicyLabel(ConflictPolicy policy);

enum class EntryState {
    Pending,
    Done,
    Skipped,
    Failed,
};

QString entryStateKey(EntryState state);
EntryState entryStateFromKey(const QString &key);

/// 报告里的一项。对世界导入 = 一个文件（失败项必列，成功项可只报总数）；
/// 对迁移 = 迁移计划里的一块（存档 / 模组 / …）。
struct Entry {
    QString path;      ///< 目标相对路径（用 `/` 分隔；不允许绝对路径或 `..`）
    qint64 bytes = 0;
    EntryState state = EntryState::Pending;
    QString reason;    ///< 失败 / 跳过的原因（可空）
};

struct Report {
    static constexpr int kSchema = 1;
    /// 读盘上限：4 MiB 正文、两万条条目。超限一律当坏报告，不静默截断。
    static constexpr qint64 kMaxFileBytes = 4 * 1024 * 1024;
    static constexpr int kMaxEntries = 20000;
    /// `context` 只放续做要用的参数（路径 / 键 / 冲突选择），不放日志正文。
    static constexpr int kMaxContextBytes = 64 * 1024;

    QString id;                 ///< 同时也是暂存目录名与恢复令牌（UUID 字符串）
    Kind kind = Kind::FileCopy;
    QString title;              ///< 「导入世界：World」——任务中心 / 报告抬头
    QString instanceId;
    QString targetRoot;         ///< 目标实例目录或 saves 目录
    QString sourceRoot;
    QString stagingDir;         ///< 未发布的暂存；空 = 没有可续的现场
    ConflictPolicy conflictPolicy = ConflictPolicy::Cancel;
    QList<Entry> entries;
    int totalFiles = 0;
    int doneFiles = 0;
    int skippedFiles = 0;
    int failedFiles = 0;
    qint64 totalBytes = 0;
    qint64 doneBytes = 0;
    bool complete = false;
    bool cancelled = false;
    QString failureReason;
    QJsonObject context;        ///< 续做参数（见上）
    QDateTime startedAt;
    QDateTime updatedAt;

    /// 还有没有“该做但没做”的项（失败的也算 —— 续做要再试一次）。
    bool hasUnfinished() const;
    int unfinishedFiles() const;
    /// 「复制了 3 个（1.2 MB），跳过 1 个，失败 1 个，共 5 个」那一句。
    QString summaryText() const;
    /// 为什么做不完 + 每一个未完成项 + 接下来能做什么（给 `<报告>.failure.txt`）。
    QString failureText(const QString &reportPath = QString()) const;

    QJsonObject toJson() const;
    /// @param ok 非空时写“是不是一份能用的报告”。结构不认识、版本不认识、路径越界、超限
    ///           一律 `false`（调用方当坏报告处理，不能拿它去动盘）。
    static Report fromJson(const QByteArray &bytes, bool *ok);
};

/// 报告目录：`<dataRoot>/cache/operation-reports`。
QString reportDirectory(const QString &dataRoot);
QString reportPath(const QString &dataRoot, const QString &id);
/// 本模块管理的暂存根：`<dataRoot>/cache/operation-staging`。
QString stagingRoot(const QString &dataRoot);
/// 某一次操作的暂存目录。只有这个函数造出来的目录会被 `cleanupOwnedStaging()` 自动清理。
QString stagingDirectory(const QString &dataRoot, const QString &id);

/// 原子写（QSaveFile）。@p problem 写失败原因。条目超过上限时拒绝写，而不是悄悄截断。
bool saveReport(const QString &path, const Report &report, QString *problem = nullptr);
/// 把 `failureText()` 写成 `<path>.failure.txt`（原子写），返回实际路径；失败回空。
QString writeFailureText(const QString &path, const Report &report, QString *problem = nullptr);
/// 读一份报告（有上限）。@p problem 写“为什么读不动”。
Report loadReport(const QString &path, bool *ok = nullptr, QString *problem = nullptr);
/// 读不动 / 已完成的报告也删得掉；删不掉只回 false，不当成业务失败。
bool removeReport(const QString &path);

/// 只清理**本模块的**暂存根下、既没有对应报告、又超过 @p minAgeSeconds 没动过的目录。
/// 用户自己的目录、其它模块的暂存一律不碰。返回清掉的目录名。
QStringList cleanupOwnedStaging(const QString &dataRoot, qint64 minAgeSeconds = 30 * 60);

}  // namespace OperationReport
