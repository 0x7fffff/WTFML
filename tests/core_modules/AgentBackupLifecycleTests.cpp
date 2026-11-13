/**
 * @file AgentBackupLifecycleTests.cpp
 * @brief 维护备份元数据、owner/source/sourceSessionId、来源 Session 删除后仍可读。
 */

#include "Support.hpp"

#include "agent/AgentBackupIndex.hpp"
#include "agent/AgentSessionStore.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

void runAgentBackupLifecycleTests() {
    QTemporaryDir fixture;
    AgentBackupIndex backups(fixture.path() + QStringLiteral("/backups"));
    QString backupId;
    QString problem;
    const QByteArray original("maxMemory = 4096\n");
    check(backups.createFromBytes(QStringLiteral("session-1"), QStringLiteral("inst-1"),
                                  QStringLiteral("config/a.toml"), original,
                                  QStringLiteral("fs.write 前备份"), &backupId, &problem),
          "备份：创建", problem);
    check(!backupId.isEmpty(), "备份：生成 id");
    QList<AgentBackupEntry> entries;
    check(backups.list(&entries, &problem) && entries.size() == 1, "备份：列表");
    const AgentBackupEntry entry = entries.constFirst();
    check(entry.owner == QStringLiteral("agent") &&
              entry.source == QStringLiteral("agent-session") &&
              entry.storage == QStringLiteral("agent-maintenance"),
          "备份：owner/source/storage 正确");
    check(entry.sourceSessionId == QStringLiteral("session-1"),
          "备份：保留来源会话 id");
    check(entry.instanceId == QStringLiteral("inst-1"), "备份：保留实例 id");
    check(entry.sha256.size() == 64 && entry.size == original.size(),
          "备份：记录 sha256/大小");
    check(entry.originalPaths.contains(QStringLiteral("config/a.toml")),
          "备份：记录来源相对路径");
    QByteArray restored;
    check(backups.readOriginal(backupId, &restored, &problem) && restored == original,
          "备份：内容可解出", problem);

    // 元数据明文不含内容。
    QFile meta(backups.dirFor(backupId) + QStringLiteral("/meta.json"));
    check(meta.open(QIODevice::ReadOnly), "备份：读元数据");
    const QByteArray metaBytes = meta.readAll();
    check(!metaBytes.contains("maxMemory = 4096"), "备份：元数据不含文件内容");
    const QJsonObject metaObject = QJsonDocument::fromJson(metaBytes).object();
    meta.close();
    check(!metaObject.value(QStringLiteral("approved")).toBool(false),
          "备份：元数据不存审批结果");

    // 删除来源会话不删备份。
    AgentSessionStore sessions(fixture.path());
    AgentSession session;
    sessions.create(&session, &problem);
    check(sessions.requestDeletion(session.meta.id, &problem) &&
              sessions.completeDeletion(session.meta.id, &problem, nullptr),
          "备份：来源会话可删除");
    check(QFileInfo::exists(backups.dirFor(backupId) + QStringLiteral("/meta.json")),
          "备份：删除会话后备份仍在");
    entries.clear();
    backups.list(&entries, nullptr);
    check(!entries.isEmpty() && entries.constFirst().sourceSessionId == QStringLiteral("session-1"),
          "备份：来源会话删除后元数据仍可读");

    // 只允许清理 owner=agent。
    check(backups.removeOwnedBackup(backupId, &problem), "备份：清理 owner=agent 成功", problem);
    check(!QFileInfo::exists(backups.dirFor(backupId)), "备份：清理后目录消失");

    // 伪造 owner=user 的元数据，清理必须拒绝。
    const QString foreignId = QStringLiteral("foreign-1");
    QDir().mkpath(backups.dirFor(foreignId));
    QJsonObject foreign{{QStringLiteral("schema"), 1},
                        {QStringLiteral("backupId"), foreignId},
                        {QStringLiteral("owner"), QStringLiteral("user")},
                        {QStringLiteral("source"), QStringLiteral("user-export")}};
    QFile foreignMeta(backups.dirFor(foreignId) + QStringLiteral("/meta.json"));
    check(foreignMeta.open(QIODevice::WriteOnly), "备份：写一份外部来源的元数据", foreignMeta.fileName());
    foreignMeta.write(QJsonDocument(foreign).toJson());
    foreignMeta.close();
    check(!backups.removeOwnedBackup(foreignId, &problem), "备份：owner=user 不允许 Agent 清理");
    check(QFileInfo::exists(backups.dirFor(foreignId) + QStringLiteral("/meta.json")),
          "备份：共享/外部备份未被误删");
}
