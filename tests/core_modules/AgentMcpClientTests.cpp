/**
 * @file AgentMcpClientTests.cpp
 * @brief 真进程端到端：用 verify_agent 自己的 `--mcp-fake-server` 模式起服务器，
 *        验握手/分页、工具审批、结果脱敏、stdout 垃圾行、崩溃不自动重启与进程清理。
 */

#include "Support.hpp"

#include "agent/AgentMcpToolEngine.hpp"
#include "agent/McpClient.hpp"
#include "agent/McpManager.hpp"
#include "agent/McpTrustStore.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>

namespace {

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 12000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    return true;
}

McpServerConfig fakeServerConfig() {
    McpServerConfig server;
    server.id = QStringLiteral("fake-server");
    server.name = QStringLiteral("假 MCP 服务器");
    server.command = QCoreApplication::applicationFilePath();
    server.arguments = {QStringLiteral("--mcp-fake-server")};
    server.enabled = true;
    server.handshakeTimeoutMs = 15000;
    server.listTimeoutMs = 15000;
    server.callTimeoutMs = 15000;
    return server;
}

}  // namespace

void runAgentMcpClientTests() {
    QTemporaryDir fixture;
    check(fixture.isValid(), "MCP 端到端：临时目录可用");
    const QString configDir = fixture.path() + QStringLiteral("/mcp");
    McpTrustStore store(configDir);
    McpServerConfig server = fakeServerConfig();
    QString problem;
    check(store.upsert(server, &problem), "MCP 端到端：保存服务器配置", problem);

    McpTrustStore::GateResult gate;
    check(store.confirm(server.id, QStringLiteral("start"), &gate, &problem),
          "MCP 端到端：确认后写记录", problem);

    McpManager manager(&store);
    check(manager.reload(&problem), "MCP 端到端：加载配置", problem);
    QStringList logs;
    QObject::connect(&manager, &McpManager::logLine, [&logs](const QString &, const QString &text) {
        logs.append(text);
    });

    // ---------- 握手 + 分页 ----------
    const McpManager::StartResult started = manager.start(server.id, &gate, &problem);
    check(started == McpManager::StartResult::Started, "MCP 端到端：确认后启动进程", problem);
    const bool ready = waitFor([&manager] {
        return manager.state(QStringLiteral("fake-server")) == McpClient::State::Ready;
    });
    check(ready, "MCP 端到端：握手并对接就绪",
          McpClient::stateName(manager.state(QStringLiteral("fake-server"))));
    const QList<McpToolDescriptor> tools = manager.tools();
    check(tools.size() == 3, "MCP 端到端：分页后合并出 3 个工具",
          QString::number(tools.size()));
    bool pageTwo = false;
    QString echoWire;
    for (const McpToolDescriptor &tool : tools) {
        if (tool.name == QStringLiteral("second.page")) pageTwo = true;
        if (tool.name == QStringLiteral("echo")) echoWire = tool.wireName();
    }
    check(pageTwo, "MCP 端到端：第二页的工具也列进来了");
    check(!echoWire.isEmpty(), "MCP 端到端：工具线上名可查");

    // stderr 里的口令要被遮住。
    bool stderrSeen = false;
    bool leaked = false;
    for (const QString &line : logs) {
        if (line.contains(QStringLiteral("fake-mcp"))) stderrSeen = true;
        if (line.contains(QStringLiteral("stderr-secret"))) leaked = true;
    }
    check(stderrSeen, "MCP 端到端：stderr 进日志");
    check(!leaked, "MCP 端到端：stderr 里的口令被遮挡");

    // ---------- 工具审批 + 结果脱敏 ----------
    AgentMcpToolEngine engine(&manager);
    AgentToolResult sinkResult;
    bool sinkCalled = false;
    engine.setCompletionSink([&sinkResult, &sinkCalled](const Agent::ToolCall &,
                                                        const AgentToolResult &result) {
        sinkResult = result;
        sinkCalled = true;
    });
    Agent::ToolCall call;
    call.id = QStringLiteral("call-echo");
    call.name = echoWire;
    call.arguments = QJsonObject{{QStringLiteral("text"), QStringLiteral("hi")}};

    AgentToolResult result = engine.invoke(call);
    check(result.status == AgentToolResult::Status::ApprovalRequired,
          "MCP 端到端：每次调用都要审批");
    const QString approvalId = result.approval.value(QStringLiteral("approvalId")).toString();
    const AgentToolResult denied = engine.resolveApproval(approvalId, false);
    check(denied.error.kind == Agent::ErrorKind::ApprovalDenied,
          "MCP 端到端：拒绝审批不发起调用");
    check(!sinkCalled, "MCP 端到端：拒绝后没有结果回调");

    result = engine.invoke(call);
    const QString secondApproval = result.approval.value(QStringLiteral("approvalId")).toString();
    const AgentToolResult pending = engine.resolveApproval(secondApproval, true);
    check(pending.status == AgentToolResult::Status::Pending,
          "MCP 端到端：批准后进入异步等待");
    check(waitFor([&sinkCalled] { return sinkCalled; }), "MCP 端到端：工具结果回得来");
    check(sinkResult.status == AgentToolResult::Status::Ok, "MCP 端到端：调用成功",
          sinkResult.summary);
    const QString content = sinkResult.data.value(QStringLiteral("content")).toString();
    check(content.contains(QStringLiteral("echo: hi")), "MCP 端到端：结果内容正确", content);
    check(!content.contains(QStringLiteral("sk-fake-123456")), "MCP 端到端：结果里的口令被遮挡");
    check(sinkResult.data.value(QStringLiteral("untrusted")).toBool(),
          "MCP 端到端：结果标成不可信");

    // isError 的工具回错误。
    QString failWire;
    for (const McpToolDescriptor &tool : tools) {
        if (tool.name == QStringLiteral("fail")) failWire = tool.wireName();
    }
    sinkCalled = false;
    Agent::ToolCall failCall;
    failCall.id = QStringLiteral("call-fail");
    failCall.name = failWire;
    failCall.arguments = QJsonObject{};
    result = engine.invoke(failCall);
    engine.resolveApproval(result.approval.value(QStringLiteral("approvalId")).toString(), true);
    check(waitFor([&sinkCalled] { return sinkCalled; }), "MCP 端到端：isError 的结果也回得来");
    check(sinkResult.status == AgentToolResult::Status::Error, "MCP 端到端：isError 映射成错误");

    // ---------- 未连接时的拒绝 ----------
    manager.stop(server.id);
    check(manager.state(server.id) == McpClient::State::Idle, "MCP 端到端：断开后回到 idle");
    check(manager.tools().isEmpty(), "MCP 端到端：断开后不再有工具");
    result = engine.invoke(call);
    check(result.status == AgentToolResult::Status::Error, "MCP 端到端：断开后调用被拒");

    // ---------- stdout 垃圾行 ----------
    qputenv("WTFML_FAKE_MCP_GARBAGE", "1");
    check(store.checkStart(server.id).allowed(), "MCP 端到端：垃圾行用例沿用同一份确认");
    check(manager.startConfirmed(server.id, &problem) == McpManager::StartResult::Started,
          "MCP 端到端：带垃圾行的服务器能启动", problem);
    check(waitFor([&manager] {
              return manager.state(QStringLiteral("fake-server")) == McpClient::State::Ready;
          }),
          "MCP 端到端：垃圾行不断连");
    qunsetenv("WTFML_FAKE_MCP_GARBAGE");
    manager.stop(server.id);

    // ---------- 崩溃不自动重启 ----------
    qputenv("WTFML_FAKE_MCP_CRASH", "1");
    check(manager.startConfirmed(server.id, &problem) == McpManager::StartResult::Started,
          "MCP 端到端：崩溃用例可以启动", problem);
    check(waitFor([&manager] {
              return manager.state(QStringLiteral("fake-server")) == McpClient::State::Failed;
          }),
          "MCP 端到端：服务器退出后状态是 failed");
    QThread::msleep(300);
    check(manager.state(server.id) == McpClient::State::Failed,
          "MCP 端到端：不会自动重启");
    check(manager.start(server.id, &gate, &problem) == McpManager::StartResult::Started,
          "MCP 端到端：确认有效时可以手动再启动", problem);
    check(waitFor([&manager] {
              return manager.state(QStringLiteral("fake-server")) == McpClient::State::Failed;
          }),
          "MCP 端到端：再次失败也一样");
    qunsetenv("WTFML_FAKE_MCP_CRASH");

    // ---------- 重连要重新确认 ----------
    manager.stop(server.id);
    check(manager.reconnect(server.id, &gate, &problem) ==
              McpManager::StartResult::NeedsConfirmation,
          "MCP 端到端：重连前必须再确认");
    manager.stopAll();
}
