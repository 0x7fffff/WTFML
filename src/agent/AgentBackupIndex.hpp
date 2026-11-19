/**
 * @file AgentBackupIndex.hpp
 * @brief 文件维护备份的元数据/内容：独立于会话生命周期，保留 owner/source/sourceSessionId。
 *
 * Stage 1B 只做 `fs.write` 的写前备份：内容 DPAPI 加密，元数据明文（不含内容/密钥）。
 * 删除会话**不删除**这些备份；创建备份属于写操作，必须随写审批一起确认。
 */

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

struct AgentBackupEntry {
    QString backupId;
    QString owner = QStringLiteral("agent");
    QString source = QStringLiteral("agent-session");
    QString storage = QStringLiteral("agent-maintenance");
    QString sourceSessionId;
    QString instanceId;
    QString reason;
    QStringList originalPaths;
    QString sha256;
    QString restoreHint;
    qint64 size = 0;
    QDateTime createdAt;

    QJsonObject toJson() const;
    static AgentBackupEntry fromJson(const QJsonObject &object);
};

class AgentBackupIndex {
public:
    explicit AgentBackupIndex(const QString &root = QString());

    void setRoot(const QString &root);
    QString root() const { return m_root; }
    QString dirFor(const QString &backupId) const;

    bool createFromBytes(const QString &sourceSessionId, const QString &instanceId,
                         const QString &relativePath, const QByteArray &original,
                         const QString &reason, QString *backupId, QString *problem = nullptr);
    /// 一份备份收多个文件（Stage 2 的 `backup.create` 一次备 config/mods/options）。
    /// 内容仍是**一个** DPAPI 密文：容器是一段 JSON（路径 + base64），因此单文件备份
    /// 与多文件备份在盘上是同一种形状，`readFiles()` 两种都读得回来。
    bool createFromFiles(const QString &sourceSessionId, const QString &instanceId,
                         const QList<QPair<QString, QByteArray>> &files, const QString &reason,
                         QString *backupId, QString *problem = nullptr);
    /// 一条桶里的原始文件被改回旧版本前，先把它当前内容备份一份（恢复前的快照）。
    bool list(QList<AgentBackupEntry> *entries, QString *problem = nullptr);
    bool readOriginal(const QString &backupId, QByteArray *bytes, QString *problem = nullptr);
    /// 读回一份备份里的全部文件（单文件备份也走这里）。
    bool readFiles(const QString &backupId, QList<QPair<QString, QByteArray>> *files,
                   QString *problem = nullptr);

    /// 只清 Agent 自有备份；launcher/user/shared 不应出现在这里（这里也不会误删它们）。
    bool removeOwnedBackup(const QString &backupId, QString *problem = nullptr);

private:
    bool writeMeta(const AgentBackupEntry &entry, QString *problem);

    QString m_root;
};
