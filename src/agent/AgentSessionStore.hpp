/**
 * @file AgentSessionStore.hpp
 * @brief 会话元数据/正文的原子读写、列表、改名/固定、恢复快照、损坏隔离、删除标记。
 *
 * 盘上布局见 AgentPaths：`sessions/<id>.meta.json` + `<id>.json`（默认明文）或
 * `<id>.enc`（整会话 DPAPI）。正文先写、元数据最后写（带 bodySha256）。
 *
 * NOTE: 活动会话的“先取消再删”是控制器的责任；这一层只负责把 `deletionState`
 * 写成 pending/deleting 并按顺序删文件。等待安全终止不能藏进这里。
 */

#pragma once

#include "AgentSession.hpp"

#include <QString>
#include <QStringList>

class AgentSessionStore {
public:
    explicit AgentSessionStore(const QString &root = QString());

    void setRoot(const QString &root);
    QString root() const { return m_root; }
    QString sessionsDir() const;

    static int maxBodyBytes();
    static int maxMessages();

    bool create(AgentSession *session, QString *problem = nullptr);
    bool load(const QString &sessionId, AgentSession *session, QString *problem = nullptr);
    /// 写正文（先）与元数据（后）。@p snapshot 只在用户消息/完成/审批等稳定点传 true。
    bool save(const AgentSession &session, bool snapshot, QString *problem = nullptr);

    bool list(QList<AgentSessionMeta> *metas, QString *problem = nullptr);

    bool rename(const QString &sessionId, const QString &title, QString *problem = nullptr);
    bool setPinned(const QString &sessionId, bool pinned, QString *problem = nullptr);
    bool setEncrypted(const QString &sessionId, bool encrypted, QString *problem = nullptr);

    /// 把 deletionState 置为 pending（真正删除前要先确认会话已安全终止）。
    bool requestDeletion(const QString &sessionId, QString *problem = nullptr);
    /// 按 deletionState=pending/deleting 执行删除；失败时 @p stageOut 说清卡在哪一步。
    bool completeDeletion(const QString &sessionId, QString *problem = nullptr,
                          QString *stageOut = nullptr);
    /// 启动时收尾：所有 pending/deleting 的会话继续删。
    bool finalizePendingDeletions(QStringList *problems = nullptr);

private:
    QString bodyPath(const AgentSessionMeta &meta) const;
    QString plainBodyPath(const QString &id) const;
    QString encryptedBodyPath(const QString &id) const;
    QString metaPath(const QString &id) const;
    QString recoveryDir(const QString &id) const;
    QString artifactDir(const QString &id) const;
    bool writeAtomic(const QString &path, const QByteArray &bytes, QString *problem) const;
    bool writeMeta(const AgentSessionMeta &meta, QString *problem) const;
    bool readMeta(const QString &path, AgentSessionMeta *meta, QString *problem) const;
    void snapshotAfterSave(const QString &id, QString *problem) const;
    void quarantine(const QString &path, QString *problem) const;

    QString m_root;
};
