/**
 * @file AgentWebTools.hpp
 * @brief Stage 3 联网工具：`web.search`（SearXNG）与 `web.fetch`（pinned-IP 抓取）。
 *
 * 两道闸：每次调用都要审批（搜索词/URL 会发给第三方）；网络层再按 `AgentWebPolicy`
 * 校验全部 A/AAAA、逐跳重定向重检、强制直连。结果是不可信内容：HTML 转文本、遮凭据、
 * 截断后进上下文。
 */

#pragma once

#include "AgentTool.hpp"
#include "AgentWebPolicy.hpp"
#include "AgentWebTransport.hpp"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

class AgentWebTools : public QObject, public AgentToolEngine {
    Q_OBJECT

public:
    using ContextProvider = std::function<AgentToolContext(QString *problem)>;

    explicit AgentWebTools(AgentWebTransport *transport, ContextProvider provider = {},
                           QObject *parent = nullptr);

    void setTransport(AgentWebTransport *transport);
    static QList<AgentToolSpec> specs();
    static bool isWebTool(const QString &internalName);

    QJsonArray schemas() const override;
    AgentToolResult invoke(const Agent::ToolCall &call) override;
    AgentToolResult resolveApproval(const QString &approvalId, bool approved) override;
    void cancelPending() override;
    bool handles(const QString &internalName) const override;
    void setCompletionSink(AgentToolSink sink) override { m_sink = std::move(sink); }

    int running() const { return m_running.size(); }

private:
    struct Approval {
        enum class Kind { Fetch, Search };
        Kind kind = Kind::Fetch;
        Agent::ToolCall call;
        QUrl url;
        QString query;
        AgentToolContext context;
    };

    struct RunningFlight {
        Agent::ToolCall call;
        QUrl currentUrl;   ///< 当前这一跳的 URL（原 host，不是 IP）
        int redirects = 0;
        bool search = false;
        AgentToolContext context;
    };

    void startFetch(const QString &requestId, const QUrl &url, int redirects, bool search,
                    const Agent::ToolCall &call, const AgentToolContext &context);
    void onResolved(const QString &requestId, bool ok, const QList<QHostAddress> &addresses,
                    const QString &error);
    void onResponse(const QString &requestId, const AgentHttpResponse &response);
    void finish(const QString &requestId, const AgentToolResult &result);
    AgentToolResult fetchResult(const RunningFlight &flight, const AgentHttpResponse &response) const;
    AgentToolResult searchResult(const RunningFlight &flight, const AgentHttpResponse &response) const;
    AgentToolResult errorResult(Agent::ErrorKind kind, const QString &message,
                                const QJsonObject &data = {}) const;
    QString newRequestId() const;

    AgentWebTransport *m_transport = nullptr;
    ContextProvider m_provider;
    AgentToolSink m_sink;
    QHash<QString, Approval> m_approvals;
    QHash<QString, RunningFlight> m_running;
    mutable int m_counter = 1;
};
