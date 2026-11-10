/**
 * @file AgentArtifactStore.hpp
 * @brief 模型可读 artifact 与本地原始存档分离（不共用一个模型可见 id 空间）。
 *
 * - 模型可读 artifact：内容已按 D11 脱敏；DPAPI 加密落盘；`readModel()` 每次重新脱敏。
 * - 本地原始存档：完整原文；DPAPI 加密；**不返回模型可见 id**；模型请求任何非模型可读
 *   id 都按“不可读”处理，不泄露存在性。
 */

#pragma once

#include <QString>

class AgentArtifactStore {
public:
    explicit AgentArtifactStore(const QString &root = QString());

    void setRoot(const QString &root);
    QString root() const { return m_root; }
    QString sessionDir(const QString &sessionId) const;

    bool putModelReadable(const QString &sessionId, const QString &content, QString *artifactId,
                          QString *problem = nullptr);
    bool putLocalRaw(const QString &sessionId, const QString &content, QString *artifactId,
                     QString *problem = nullptr);

    /// 只读模型可读 artifact；每次调用都重新执行 AgentRedaction。
    bool readModel(const QString &sessionId, const QString &artifactId, int offset, int length,
                   QString *content, QString *problem = nullptr);

    /// 本地用户显式导出/查看原始存档；**不经过工具层**。
    bool readLocalRaw(const QString &sessionId, const QString &artifactId, QString *content,
                      QString *problem = nullptr);

    bool removeSession(const QString &sessionId);

private:
    bool put(const QString &sessionId, const QString &content, bool modelReadable,
             QString *artifactId, QString *problem);
    bool readEncrypted(const QString &sessionId, const QString &artifactId, bool requireModelReadable,
                       QString *content, QString *problem);

    QString m_root;
};
