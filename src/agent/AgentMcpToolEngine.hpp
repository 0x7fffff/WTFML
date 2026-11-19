/**
 * @file AgentMcpToolEngine.hpp
 * @brief 把 MCP 工具接成智能体工具（docs/features/AGENT.md §10.3）。
 *
 * 每个 MCP 工具**逐次审批**（即便服务器声明只读注解也不放行）；批准后才发起调用，
 * 调用本身是异步的：`invoke()` 回 `Status::Pending`，结果通过完成回调和
 * `McpManager` 的信号回来。
 *
 * 结果进入模型上下文前只做两件事：遮凭据（`AgentRedaction`）与 16 KB 截断。外发本身
 * 已由这一次的审批覆盖，不再二次弹确认。
 */

#pragma once

#include "McpManager.hpp"
#include "McpTypes.hpp"
#include "AgentTool.hpp"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

class AgentMcpToolEngine : public QObject, public AgentToolEngine {
    Q_OBJECT

public:
    using ContextProvider = std::function<AgentToolContext(QString *problem)>;

    explicit AgentMcpToolEngine(McpManager *manager, ContextProvider provider = {},
                                QObject *parent = nullptr);

    void setManager(McpManager *manager);
    void setContextProvider(ContextProvider provider) { m_provider = std::move(provider); }

    QJsonArray schemas() const override;
    AgentToolResult invoke(const Agent::ToolCall &call) override;
    AgentToolResult resolveApproval(const QString &approvalId, bool approved) override;
    void cancelPending() override;
    bool handles(const QString &internalName) const override;
    void setCompletionSink(AgentToolSink sink) override { m_sink = std::move(sink); }

    /// 正在等结果的那些请求（会话关闭/删会话时统一取消）。
    int pendingCalls() const { return m_calls.size(); }

private:
    struct ApprovalPending {
        McpToolDescriptor tool;
        QJsonObject arguments;
        Agent::ToolCall call;
    };

    void onToolFinished(const QString &requestId, const QJsonObject &result);
    void onToolFailed(const QString &requestId, const Agent::Error &error);
    AgentToolResult errorResult(Agent::ErrorKind kind, const QString &message) const;
    AgentToolResult resultFromContent(const Agent::ToolCall &call, const QJsonObject &result);

    McpManager *m_manager = nullptr;
    ContextProvider m_provider;
    AgentToolSink m_sink;
    QHash<QString, ApprovalPending> m_approvals;
    QHash<QString, Agent::ToolCall> m_calls;   ///< 请求 id → 原始工具调用
};
