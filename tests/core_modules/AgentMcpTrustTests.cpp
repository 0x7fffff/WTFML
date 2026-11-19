/**
 * @file AgentMcpTrustTests.cpp
 * @brief MCP 确认闸：首次启动要确认、配置/身份变化失效、重连要重新确认、拒绝不启动、
 *        工具逐次审批、启动确认与工具审批互相独立。
 */

#include "Support.hpp"

#include "agent/AgentMcpToolEngine.hpp"
#include "agent/McpManager.hpp"
#include "agent/McpTrustStore.hpp"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

namespace {

/// 造一个"可执行文件"：内容不同 → SHA-256 不同。
QString writeFakeExecutable(const QString &dir, const QString &name, const QByteArray &content) {
    const QString path = dir + QLatin1Char('/') + name;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return QString();
    file.write(content);
    file.close();
    return path;
}

McpServerConfig makeServer(const QString &path) {
    McpServerConfig server;
    server.id = QStringLiteral("server-1");
    server.name = QStringLiteral("测试服务器");
    server.command = path;
    server.enabled = true;
    return server;
}

}  // namespace

void runAgentMcpTrustTests() {
    QTemporaryDir fixture;
    check(fixture.isValid(), "MCP 确认：临时目录可用");
    const QString configDir = fixture.path() + QStringLiteral("/mcp");
    const QString exe = writeFakeExecutable(fixture.path(), QStringLiteral("server.exe"),
                                            QByteArray("v1"));
    check(!exe.isEmpty(), "MCP 确认：造一个假可执行文件");

    // ---------- 身份与配置校验 ----------
    QString problem;
    const McpExecutableIdentity identity = McpTrustStore::computeIdentity(exe, &problem);
    check(identity.isValid(), "MCP 确认：算得出可执行文件身份", problem);
    check(identity.sha256.size() == 64, "MCP 确认：身份带 SHA-256");
    check(identity.size == 2, "MCP 确认：身份带大小");
    const McpExecutableIdentity sameAgain = McpTrustStore::computeIdentity(exe, nullptr);
    check(identity.sameAs(sameAgain), "MCP 确认：同一文件身份一致");
    check(!McpTrustStore::computeIdentity(fixture.path() + QStringLiteral("/nope.exe"), nullptr)
               .isValid(),
          "MCP 确认：文件不存在时身份无效");

    McpServerConfig server = makeServer(exe);
    check(McpTrustStore::validateConfig(server).isEmpty(), "MCP 确认：合法配置通过校验");
    McpServerConfig relative = server;
    relative.command = QStringLiteral("server.exe");
    check(!McpTrustStore::validateConfig(relative).isEmpty(),
          "MCP 确认：相对路径被拒（不解析 PATH）");
    McpServerConfig tooManyArgs = server;
    for (int i = 0; i < 40; ++i) tooManyArgs.arguments.append(QStringLiteral("--x"));
    check(!McpTrustStore::validateConfig(tooManyArgs).isEmpty(), "MCP 确认：参数过多被拒");
    McpServerConfig badCwd = server;
    badCwd.workingDirectory = QStringLiteral("relative/dir");
    check(!McpTrustStore::validateConfig(badCwd).isEmpty(), "MCP 确认：相对工作目录被拒");

    // ---------- 首次启动 / 配置变化 / 身份变化 ----------
    McpTrustStore store(configDir);
    check(store.upsert(server, &problem), "MCP 确认：保存服务器配置", problem);
    McpTrustStore::GateResult gate = store.checkStart(server.id);
    check(gate.gate == McpTrustStore::GateResult::Gate::NeedsConfirmation,
          "MCP 确认：首次启动需要确认");
    check(!gate.identity.sha256.isEmpty(), "MCP 确认：确认卡能拿到身份");

    // 没确认就启动：Manager 必须拒绝。
    McpManager manager(&store);
    manager.reload(&problem);
    McpManager::StartResult started = manager.start(server.id, &gate, &problem);
    check(started == McpManager::StartResult::NeedsConfirmation,
          "MCP 确认：未确认时 Manager 不起进程");
    check(manager.state(server.id) == McpClient::State::Idle,
          "MCP 确认：拒绝后服务器仍未启动");

    check(store.confirm(server.id, QStringLiteral("start"), &gate, &problem),
          "MCP 确认：用户确认后写记录", problem);
    check(store.checkStart(server.id).gate == McpTrustStore::GateResult::Gate::Allowed,
          "MCP 确认：确认后闸门放行");

    // 改配置 → 旧确认失效。
    McpServerConfig changed = server;
    changed.arguments = {QStringLiteral("--changed")};
    check(store.upsert(changed, &problem), "MCP 确认：保存改过的配置", problem);
    gate = store.checkStart(server.id);
    check(gate.gate == McpTrustStore::GateResult::Gate::NeedsConfirmation,
          "MCP 确认：配置改过后要重新确认");
    check(gate.reason.contains(QStringLiteral("配置")), "MCP 确认：原因说配置改过");
    store.confirm(server.id, QStringLiteral("start"), &gate, &problem);

    // 换可执行文件内容（路径不变）→ 身份变化，旧确认失效。
    check(!writeFakeExecutable(fixture.path(), QStringLiteral("server.exe"),
                               QByteArray("v2-tampered"))
               .isEmpty(),
          "MCP 确认：替换可执行文件内容");
    gate = store.checkStart(server.id);
    check(gate.gate == McpTrustStore::GateResult::Gate::NeedsConfirmation,
          "MCP 确认：可执行文件变了要重新确认");
    check(gate.reason.contains(QStringLiteral("可执行文件")), "MCP 确认：原因说文件变了");

    // 删掉可执行文件 → 直接 Blocked。
    QFile::remove(exe);
    gate = store.checkStart(server.id);
    check(gate.gate == McpTrustStore::GateResult::Gate::Blocked, "MCP 确认：文件没了直接阻断");
    check(!store.confirm(server.id, QStringLiteral("start"), &gate, &problem),
          "MCP 确认：算不出身份时确认也拒绝");

    // 没启用的服务器直接 Blocked。
    check(!writeFakeExecutable(fixture.path(), QStringLiteral("server.exe"),
                               QByteArray("v3"))
               .isEmpty(),
          "MCP 确认：重新造一份可执行文件");
    McpServerConfig disabled = server;
    disabled.enabled = false;
    store.upsert(disabled, &problem);
    check(store.checkStart(server.id).gate == McpTrustStore::GateResult::Gate::Blocked,
          "MCP 确认：未启用的服务器阻断");

    // 重新启用并确认；重连必须再次确认（即使配置没变）。
    store.upsert(server, &problem);
    store.confirm(server.id, QStringLiteral("start"), &gate, &problem);
    check(store.checkStart(server.id).gate == McpTrustStore::GateResult::Gate::Allowed,
          "MCP 确认：重新确认后放行");
    const McpManager::StartResult reconnect = manager.reconnect(server.id, &gate, &problem);
    check(reconnect == McpManager::StartResult::NeedsConfirmation,
          "MCP 确认：手动重连也要再确认");
    check(gate.reason.contains(QStringLiteral("重新连接")), "MCP 确认：重连原因说得清");

    // ---------- 工具逐次审批（不依赖真进程：手工塞工具描述） ----------
    AgentMcpToolEngine engine(&manager);
    McpToolDescriptor echo;
    echo.serverId = server.id;
    echo.serverName = server.name;
    echo.name = QStringLiteral("echo");
    echo.description = QStringLiteral("回显");
    engine.setManager(nullptr);
    engine.setManager(&manager);
    const QString wire = echo.wireName();

    Agent::ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = wire;
    call.arguments = QJsonObject{{QStringLiteral("text"), QStringLiteral("hi")}};
    AgentToolResult result = engine.invoke(call);
    check(result.status == AgentToolResult::Status::Error,
          "MCP 确认：服务器没有列过工具时调用被拒");

    // 服务器配置文件的读写：确认记录能落盘、重新读回来还在。
    McpTrustStore reloaded(configDir);
    McpConfirmation confirmation;
    check(reloaded.confirmation(server.id, &confirmation), "MCP 确认：确认记录可重新读出");
    check(confirmation.configHash == server.configHash(), "MCP 确认：记录里的 configHash 匹配");
    check(!confirmation.confirmedAt.isEmpty(), "MCP 确认：记录带确认时间");

    // 删除服务器时确认记录一起清掉。
    check(reloaded.remove(server.id, &problem), "MCP 确认：删除服务器", problem);
    McpTrustStore::GateResult afterRemove = reloaded.checkStart(server.id);
    check(afterRemove.gate == McpTrustStore::GateResult::Gate::Blocked,
          "MCP 确认：删掉后不再有配置");
}
