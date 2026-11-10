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
    bool list(QList<AgentBackupEntry> *entries, QString *problem = nullptr);
    bool readOriginal(const QString &backupId, QByteArray *bytes, QString *problem = nullptr);

    /// 只清 Agent 自有备份；launcher/user/shared 不应出现在这里（这里也不会误删它们）。
    bool removeOwnedBackup(const QString &backupId, QString *problem = nullptr);

private:
    bool writeMeta(const AgentBackupEntry &entry, QString *problem);

    QString m_root;
};
