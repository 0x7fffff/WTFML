/**
 * @file AgentCommandTools.hpp
 * @brief Stage 3 固定诊断命令（`command.diagnose`，docs/features/AGENT.md §9.5.1）。
 *
 * 只开放编译进程序的 `CommandSpec`：可执行文件名、固定参数模板、可选的一个 host 参数、
 * 超时与输出上限。默认关闭（设置里打开才可用），每次执行都要审批，参数不拼 shell 字符串，
 * 进程树用 Job Object 清理。
 *
 * `command.run`（任意可执行文件）本阶段**不开放**。
 */

#pragma once

#include "AgentTool.hpp"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

struct AgentCommandSpec {
    QString id;
    QString displayName;
    QString program;          ///< 解析后的可执行文件名（用 QStandardPaths 查绝对路径）
    QStringList fixedArgs;    ///< 固定参数；`%1` 是 host 参数的位置
    bool needsHost = false;
    int timeoutMs = 60000;
    QString description;
};

namespace AgentCommandSpecs {
QList<AgentCommandSpec> all();
AgentCommandSpec find(const QString &id);
/// host 参数只允许域名或 IP 字面量（不做 shell 转义，也不接受空白/引号/管道）。
bool validHostArgument(const QString &host);
}  // namespace AgentCommandSpecs

class AgentCommandTools : public QObject, public AgentToolEngine {
    Q_OBJECT

public:
    using ContextProvider = std::function<AgentToolContext(QString *problem)>;

    explicit AgentCommandTools(ContextProvider provider = {}, QObject *parent = nullptr);

    static QList<AgentToolSpec> specs();
    static bool isCommandTool(const QString &internalName);

    QJsonArray schemas() const override;
    AgentToolResult invoke(const Agent::ToolCall &call) override;
    AgentToolResult resolveApproval(const QString &approvalId, bool approved) override;
    void cancelPending() override;
    bool handles(const QString &internalName) const override;
    void setCompletionSink(AgentToolSink sink) override { m_sink = std::move(sink); }

private:
    struct Approval {
        AgentCommandSpec spec;
        QString host;
        QString programPath;
        QStringList arguments;
        Agent::ToolCall call;
        AgentToolContext context;
    };

    void run(const QString &requestId);
    void finish(const QString &requestId, const AgentToolResult &result);
    void killTree();
    AgentToolResult errorResult(Agent::ErrorKind kind, const QString &message) const;

    ContextProvider m_provider;
    AgentToolSink m_sink;
    QHash<QString, Approval> m_approvals;
    QHash<QString, Agent::ToolCall> m_running;
    QProcess *m_process = nullptr;
    QTimer *m_timer = nullptr;
    QString m_currentId;
    QByteArray m_output;
    bool m_truncated = false;
    qint64 m_jobHandle = 0;
    int m_counter = 1;
};
