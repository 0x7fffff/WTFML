/**
 * @file UpgradeHistory.hpp
 * @brief 升级历史（U6）：每个实例自己那份 `wtfml.upgrade-history.json` 的读写。
 *
 * 【为什么另起一份文件，而不是读 journal】
 *
 * `journal.json` 是**事务进行中**的现场，提交/回滚收尾时会被删掉；它记不下"做完之后
 * 还能回看的历史"。规范要求新旧实例按同一份事务各留一条、能看到来源/目标、前后版本、
 * 类别结果与失败原因 —— 这些要在一份**提交后仍然存在**的文件里。
 *
 * 设计取舍（和 `wtfml.meta` 的合并写一致）：
 * - 只追加、不覆盖：每次升级往数组头里塞一条，最多留 `kMaxEntries` 条；
 * - 未知字段原样保留（以后加字段时老启动器读得懂也写得回）；
 * - 写盘走 `QSaveFile`（临时文件 + 原子替换），读不懂就回空 + 一句话，**不覆盖**。
 */
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace UpgradeHistory {

/// 历史里的一条。`sourceId`/`targetId` 是稳定身份（不是显示名）。
struct Entry {
    QString transactionId;
    QString sourceId;
    QString sourceName;
    QString targetId;
    QString targetName;
    QString fromGameVersion;
    QString toGameVersion;
    QString fromLoader;
    QString toLoader;
    QString startedAt;   ///< ISO-8601（UTC）
    QString finishedAt;  ///< ISO-8601（UTC）
    bool ok = false;
    bool cancelled = false;
    QString problem;     ///< 失败/取消时的一句话；成功留空
    QStringList categories;  ///< 这次实际要迁移的类别键
    int copiedFiles = 0;
    int skippedFiles = 0;
    int failedFiles = 0;
    /// 这次事务的完整快照（文件名；空 = 没有快照，例如副本模式拿源实例当回滚材料）。
    QString snapshotName;
    /// 能不能按这份快照恢复（U7 的"恢复对象"）。
    bool rollbackAvailable = false;
};

/// 每个实例最多留几条（更老的丢掉，不无限长）。
constexpr int kMaxEntries = 20;

/// 实例目录下那份历史文件的路径。
QString historyPath(const QString &instanceDir);

/// 读一份历史（按**新的在前**）。文件不存在 = 空数组、`problem` 空；
/// 读不懂 = 空数组 + `problem` 一句话（调用方据此决定是"没有历史"还是"读不懂"）。
QList<Entry> read(const QString &instanceDir, QString *problem = nullptr);

/// 往数组头里塞一条（最多 `kMaxEntries` 条），原子写。
/// 盘上那份读不懂时**拒绝写**（不拿空数组把它顶掉），`problem` 说明原因。
bool append(const QString &instanceDir, const Entry &entry, QString *problem = nullptr);

}  // namespace UpgradeHistory

