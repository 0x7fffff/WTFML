/**
 * @file McpClient.hpp
 * @brief 一个 stdio MCP 服务器的进程与 JSON-RPC 会话（docs/features/AGENT.md §10.2）。
 *
 * 生命周期：`start()` → `initialize` 握手 → `notifications/initialized` → `tools/list`
 * （分页最多 10 页）→ Ready。之后只做 `tools/call` 与 `tools/list_changed` 重列。
 *
 * 硬约束：
 * - 启动/重连的确认由 `McpTrustStore` + `McpManager` 负责，这一层只管进程；
 * - 服务器崩溃/超时/身份不可用都**不自动重启**；pending 请求一律以错误结束，不永远等；
 * - stdout 只走协议，stderr 进脱敏日志；垃圾行计数有上限；
 * - 进程用 Job Object 管，`stop()`/析构时清理整棵进程树。
 */

#pragma once

#include "AgentTypes.hpp"
#include "McpTypes.hpp"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class QProcess;
class QTimer;

class McpClient : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Starting, Handshaking, Listing, Ready, Failed };
    Q_ENUM(State)

    explicit McpClient(QObject *parent = nullptr);
    ~McpClient() override;

    void start(const McpServerConfig &server, const McpExecutableIdentity &identity);
    /// 主动停：先关 Job Object 再 kill，pending 请求以 Cancelled 结束。
    void stop();

    State state() const { return m_state; }
    bool ready() const { return m_state == State::Ready; }
    QString serverId() const { return m_server.id; }
    QString protocolVersion() const { return m_protocolVersion; }
    QList<McpToolDescriptor> tools() const { return m_tools; }
    QString stateDetail() const { return m_detail; }

    /// 发起一次工具调用。返回空串表示当前不可调用（未 Ready / pending 已满）。
    QString callTool(const QString &toolName, const QJsonObject &arguments);
    /// 用户取消：发 `notifications/cancelled`，该请求以 `Cancelled` 结束。
    void cancelCall(const QString &requestId);
    /// 重新列工具（`notifications/tools/list_changed` 之后）。
    void requestToolList();

    static QString stateName(State state);

signals:
    void stateChanged(McpClient::State state, const QString &detail);
    void toolsChanged();
    void toolFinished(const QString &requestId, const QJsonObject &result);
    void toolFailed(const QString &requestId, const Agent::Error &error);
    /// stderr 与协议异常（已脱敏、已截断）。
    void logLine(const QString &text);

private:
    struct Pending {
        enum class Kind { Initialize, List, Call };
        Kind kind = Kind::Call;
        QString toolName;
        QTimer *timer = nullptr;
    };

    void setState(State state, const QString &detail);
    void failSession(const QString &reason);
    void onStarted();
    void onReadyRead();
    void handleLine(const QByteArray &line);
    void handleResponse(const QString &id, const bool hasError, const QJsonObject &error,
                        const QJsonObject &result);
    void startRequest(const QString &id, Pending::Kind kind, const QByteArray &frame,
                      int timeoutMs, const QString &toolName = QString());
    void finishPending(const QString &id);
    void failPending(const QString &id, Agent::ErrorKind kind, const QString &message);
    void failAllPending(Agent::ErrorKind kind, const QString &message);
    void parseToolsResult(const QJsonObject &result);
    void sendToolsList();
    void emitLog(const QString &text);
    void setupJobObject();
    void killProcessTree();

    McpServerConfig m_server;
    McpExecutableIdentity m_identity;
    QProcess *m_process = nullptr;
    State m_state = State::Idle;
    QString m_detail;
    QString m_protocolVersion;
    QByteArray m_buffer;
    QHash<QString, Pending> m_pending;
    QList<McpToolDescriptor> m_tools;
    QString m_listCursor;
    int m_listPages = 0;
    int m_nextRequestId = 1;
    int m_junkLines = 0;
    qint64 m_jobHandle = 0;   ///< HANDLE，按整数存，避免在头文件里拖进 windows.h
    bool m_stopping = false;
};
