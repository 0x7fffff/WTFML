/**
 * @file McpTrustStore.hpp
 * @brief MCP 服务器配置、可执行文件身份与确认记录（docs/features/AGENT.md §10.1 / D7）。
 *
 * 落盘：`<agent 数据目录>/mcp/servers.json`（配置，**不含秘密值**）与
 * `mcp/confirmations.json`（`serverId + configHash + 可执行文件身份 + confirmedAt`）。
 *
 * 确认闸的判定全在这里：首次启动、配置修改、重新连接都必须先过 `checkStart()`；
 * 可执行文件缺失/读不动/身份算不出来一律 Blocked，绝不放行。
 */

#pragma once

#include "McpTypes.hpp"

#include <QHash>
#include <QString>

class McpTrustStore {
public:
    struct GateResult {
        enum class Gate { Allowed, NeedsConfirmation, Blocked };
        Gate gate = Gate::NeedsConfirmation;
        QString reason;
        McpServerConfig server;
        McpExecutableIdentity identity;
        QString configHash;

        bool allowed() const { return gate == Gate::Allowed; }
        QString gateName() const;
    };

    explicit McpTrustStore(const QString &root = QString());

    void setRoot(const QString &root);
    QString root() const { return m_root; }

    bool load(QList<McpServerConfig> *servers, QString *problem = nullptr) const;
    bool save(const QList<McpServerConfig> &servers, QString *problem = nullptr);
    bool upsert(const McpServerConfig &server, QString *problem = nullptr);
    bool remove(const QString &serverId, QString *problem = nullptr);
    McpServerConfig find(const QString &serverId) const;

    /// 计算可执行文件身份（绝对路径、大小、mtime、卷序列 + 文件 ID、SHA-256）。
    static McpExecutableIdentity computeIdentity(const QString &path, QString *problem = nullptr);

    /// 启动/重连前的闸。`NeedsConfirmation` 时把 identity/configHash 一并回给调用方。
    GateResult checkStart(const QString &serverId) const;

    /// 用户点了确认之后调用：写一条确认记录。命令/身份变了要重新确认。
    bool confirm(const QString &serverId, const QString &purpose, GateResult *result,
                 QString *problem = nullptr);

    bool confirmation(const QString &serverId, McpConfirmation *out) const;
    void revoke(const QString &serverId);
    bool loadConfirmations(QString *problem = nullptr) const;
    bool saveConfirmations(QString *problem = nullptr) const;

    /// 配置校验：路径必须是绝对路径、参数/工作目录/环境变量长度与数量在上限内。
    static QString validateConfig(const McpServerConfig &server);

    QString serversPath() const;
    QString confirmationsPath() const;

private:
    QString m_root;
    mutable QHash<QString, McpConfirmation> m_confirmations;
    mutable bool m_confirmationsLoaded = false;
};
