/**
 * @file AgentLoop.hpp
 * @brief Stage 1A 的**无工具**对话循环：校验 → 构建上下文 → 流式请求 → 完成/失败/取消。
 *
 * 状态机按 Agent.md §8.1 收紧到本阶段实际会走的那几档：
 *   Idle → Preparing → Requesting → Completed / Failed / Cancelled
 * 不执行任何工具；`m_history` 里只会出现 user / assistant。
 *
 * 重试只发生在**首字节前**（网络/429/5xx），最多两次；流已经开始后失败保留部分文本，
 * 不自动重发。取消后旧请求的回调一律按 requestId 丢弃。
 */

#pragma once

#include "AgentModelClient.hpp"
#include "AgentSettings.hpp"
#include "AgentTool.hpp"
#include "AgentTypes.hpp"

#include <QList>
#include <QObject>
#include <QString>

class QTimer;

class AgentLoop : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Preparing, Requesting, AwaitingApproval, ExecutingTool, Completed, Failed, Cancelled };
    Q_ENUM(State)

    struct Config {
        AgentProviderProfile profile;
        QString apiKey;
        QString systemPrompt;
        int maxRetry = 2;
        int maxToolSteps = 8;
        QList<int> retryDelaysMs{1000, 4000};
    };

    explicit AgentLoop(AgentModelClient *client, QObject *parent = nullptr);

    void setClient(AgentModelClient *client);
    void setToolEngine(AgentToolEngine *engine);
    AgentToolEngine *toolEngine() const { return m_toolEngine; }
    AgentModelClient *client() const { return m_client; }

    void setConfig(const Config &config);
    Config config() const { return m_config; }

    /// 用完整历史起一轮。历史最后一条必须是 user；不会重复追加。
    void setHistory(const QList<Agent::Message> &history);
    QList<Agent::Message> history() const { return m_history; }

    void start();
    /// 用户点“重试”：丢掉最后一条失败/中断的 assistant，再发一次。
    void retry();
    void cancel();
    /// 用户对当前待审批请求作出决定；只有 AwaitingApproval 状态才生效。
    void resolveApproval(bool approved);

    bool busy() const;
    State state() const { return m_state; }
    Agent::Message assistantMessage() const { return m_assistant; }

    /// 测试专用：把退避压成 0，避免单测等 1s/4s。
    void setRetryDelaysForTest(const QList<int> &delays);

    static QString stateName(State state);

signals:
    void stateChanged(AgentLoop::State state);
    void assistantUpdated(const Agent::Message &message);
    void turnFinished(const Agent::Message &message);
    void turnFailed(const Agent::Error &error);
    void usageUpdated(const Agent::Usage &usage);
    void messageAdded(const Agent::Message &message);
    void toolCallStarted(const Agent::ToolCall &call);
    void toolCallFinished(const Agent::ToolCall &call, const AgentToolResult &result);
    void approvalRequested(const QString &approvalId, const Agent::ToolCall &call,
                           const QJsonObject &request);

private:
    void setState(State state);
    void beginRequest(bool isRetry);
    void handleFinished(const QString &requestId, const Agent::ModelReply &reply);
    void handleFailed(const QString &requestId, const Agent::Error &error);
    void deliverFailure(const Agent::Error &error);
    void appendAssistantToHistory();
    void processToolCalls();
    void handleToolOutcome(const Agent::ToolCall &call, const AgentToolResult &result);
    void invalidateRequest();
    Agent::Error configurationError() const;

    AgentModelClient *m_client = nullptr;
    AgentToolEngine *m_toolEngine = nullptr;
    Config m_config;
    QList<Agent::Message> m_history;
    Agent::Message m_assistant;
    Agent::Usage m_usage;
    Agent::Error m_lastError;
    QString m_requestId;
    State m_state = State::Idle;
    int m_retryAttempt = 0;
    int m_generation = 0;
    int m_toolSteps = 0;
    int m_nextToolCall = 0;
    QList<Agent::ToolCall> m_toolQueue;
    QString m_approvalId;
    Agent::ToolCall m_pendingCall;
    bool m_cancelled = false;
    bool m_assistantAppended = false;
};
