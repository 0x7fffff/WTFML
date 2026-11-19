/**
 * @file McpManager.hpp
 * @brief MCP 服务器生命周期与工具汇总（docs/features/AGENT.md §10）。
 *
 * 这一层是"能不能启动"的唯一判据：`start()` / `reconnect()` 先过
 * `McpTrustStore::checkStart()`，需要确认就**不启动**并把身份与 configHash 交回调用方；
 * 用户确认后再调 `startConfirmed()`。没有自动重连、没有启动后自动重启。
 *
 * 工具名冲突（两个服务器映射出同一个线上名）在这里拒绝，不把歧义交给模型。
 */

#pragma once

#include "McpClient.hpp"
#include "McpTrustStore.hpp"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class McpManager : public QObject {
    Q_OBJECT

public:
    enum class StartResult { Started, NeedsConfirmation, Blocked };
    Q_ENUM(StartResult)

    explicit McpManager(McpTrustStore *store, QObject *parent = nullptr);
    ~McpManager() override;

    void setStore(McpTrustStore *store) { m_store = store; }
    McpTrustStore *store() const { return m_store; }

    bool reload(QString *problem = nullptr);
    QList<McpServerConfig> servers() const { return m_servers; }
    McpServerConfig server(const QString &serverId) const;

    /// 首次启动 / 重新连接 / 配置变化后都走它：需要确认就回 NeedsConfirmation。
    StartResult start(const QString &serverId, McpTrustStore::GateResult *gate,
                      QString *problem = nullptr);
    /// 重新连接：先停，再按"必须重新确认"启动（D7：重连前也要确认）。
    StartResult reconnect(const QString &serverId, McpTrustStore::GateResult *gate,
                         QString *problem = nullptr);
    /// 用户已确认（确认记录由调用方写进 store），这里只负责起进程。
    StartResult startConfirmed(const QString &serverId, QString *problem = nullptr);
    void stop(const QString &serverId);
    void stopAll();

    McpClient::State state(const QString &serverId) const;
    QString stateDetail(const QString &serverId) const;
    bool anyReady() const;

    QList<McpToolDescriptor> tools() const { return m_tools; }
    /// 线上名 → 描述；找不到回空结构。
    McpToolDescriptor toolForWireName(const QString &wireName) const;
    QString conflictReason(const QString &wireName) const;

    /// 调一次工具；返回请求 id（空 = 现在不能调）。结果通过 `toolFinished` / `toolFailed` 回。
    QString callTool(const QString &serverId, const QString &toolName,
                     const QJsonObject &arguments);
    void cancelCall(const QString &requestId);

    /// 服务器进程/会话状态变化（UI 用）：serverId + 状态名 + 说明。
    static QString startResultName(StartResult result);

signals:
    void serversChanged();
    void toolsChanged();
    void serverStateChanged(const QString &serverId, const QString &stateName,
                            const QString &detail);
    void toolFinished(const QString &requestId, const QJsonObject &result);
    void toolFailed(const QString &requestId, const Agent::Error &error);
    void logLine(const QString &serverId, const QString &text);

private:
    void rebuildTools();
    QString requestOwner(const QString &requestId) const;
    McpClient *ensureClient(const QString &serverId);

    McpTrustStore *m_store = nullptr;
    QList<McpServerConfig> m_servers;
    QHash<QString, McpClient *> m_clients;
    QList<McpToolDescriptor> m_tools;
    QHash<QString, QString> m_toolConflict;   ///< 线上名 → 冲突原因
    QHash<QString, QString> m_requestServer;  ///< 请求 id → serverId
};
