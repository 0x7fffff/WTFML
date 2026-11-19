/**
 * @file AgentMcpProtocolTests.cpp
 * @brief MCP 帧编解码：合法/非法行、大小上限、反应答、工具线上名映射与冲突。
 */

#include "Support.hpp"

#include "agent/McpProtocol.hpp"
#include "agent/McpTypes.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

void runAgentMcpProtocolTests() {
    // ---------- 解析 ----------
    const QString request = QStringLiteral(
        R"({"jsonrpc":"2.0","id":"1","method":"initialize","params":{"protocolVersion":"2024-11-05"}})");
    McpProtocol::Frame frame = McpProtocol::parseLine(request.toUtf8());
    check(frame.valid, "MCP 协议：合法请求可解析", frame.problem);
    check(frame.hasId && frame.id == QStringLiteral("1"), "MCP 协议：字符串 id 保留");
    check(frame.method == QStringLiteral("initialize"), "MCP 协议：方法名解析");
    check(frame.params.value(QStringLiteral("protocolVersion")).toString() ==
              QStringLiteral("2024-11-05"),
          "MCP 协议：params 解析");

    frame = McpProtocol::parseLine(
        QByteArray(R"({"jsonrpc":"2.0","id":7,"result":{"tools":[]}})"));
    check(frame.valid && frame.id == QStringLiteral("7") && frame.idWasNumber,
          "MCP 协议：数字 id 按数字回写");

    frame = McpProtocol::parseLine(
        QByteArray(R"({"jsonrpc":"2.0","id":8,"error":{"code":-32601,"message":"x"}})"));
    check(frame.valid && frame.hasError, "MCP 协议：错误回包可识别");
    check(frame.error.value(QStringLiteral("code")).toInt() == -32601, "MCP 协议：错误码保留");

    frame = McpProtocol::parseLine(QByteArray(""));
    check(!frame.valid, "MCP 协议：空行无效");
    frame = McpProtocol::parseLine(QByteArray("not json"));
    check(!frame.valid, "MCP 协议：非 JSON 无效");
    frame = McpProtocol::parseLine(QByteArray(R"({"id":1,"result":{}})"));
    check(!frame.valid, "MCP 协议：缺 jsonrpc 字段无效");
    frame = McpProtocol::parseLine(QByteArray(R"([1,2,3])"));
    check(!frame.valid, "MCP 协议：数组不是合法帧");

    QByteArray huge(McpLimits::kMaxLineBytes + 1, 'x');
    frame = McpProtocol::parseLine(huge);
    check(!frame.valid && frame.problem.contains(QStringLiteral("上限")),
          "MCP 协议：超过 8 MB 一行被拒");

    // CRLF 也要能吃掉。
    frame = McpProtocol::parseLine(
        QByteArray(R"({"jsonrpc":"2.0","id":"9","result":{}})" "\r\n"));
    check(frame.valid && frame.id == QStringLiteral("9"), "MCP 协议：CRLF 结尾可解析");

    // ---------- 编码 ----------
    const QByteArray encoded =
        McpProtocol::encodeRequest(QStringLiteral("abc"), QStringLiteral("tools/list"), {});
    check(encoded.endsWith('\n'), "MCP 协议：请求以换行结尾");
    check(encoded.contains("\"jsonrpc\":\"2.0\""), "MCP 协议：请求带 jsonrpc=2.0");
    const McpProtocol::Frame roundTrip =
        McpProtocol::parseLine(encoded.trimmed());
    check(roundTrip.valid && roundTrip.method == QStringLiteral("tools/list"),
          "MCP 协议：编码后可解析回来");

    const QByteArray notification = McpProtocol::encodeNotification(
        QStringLiteral("notifications/initialized"), QJsonObject{});
    check(!notification.contains("\"id\""), "MCP 协议：通知不带 id");

    // ---------- 服务器反向请求 ----------
    McpProtocol::Frame ping;
    ping.valid = true;
    ping.hasId = true;
    ping.id = QStringLiteral("srv-1");
    ping.method = QStringLiteral("ping");
    const QByteArray pingReply = McpProtocol::replyToServerRequest(ping);
    check(pingReply.contains("\"result\""), "MCP 协议：ping 直接回结果");
    McpProtocol::Frame unknown = ping;
    unknown.method = QStringLiteral("sampling/createMessage");
    const QByteArray unknownReply = McpProtocol::replyToServerRequest(unknown);
    check(unknownReply.contains("-32601"), "MCP 协议：不支持的方法回 -32601");

    // ---------- 工具线上名 ----------
    McpToolDescriptor tool;
    tool.serverId = QStringLiteral("0f0e1d2c-aaaa-bbbb-cccc-000000000000");
    tool.serverName = QStringLiteral("假服务器");
    tool.name = QStringLiteral("second.page");
    const QString wire = tool.wireName();
    check(wire.startsWith(QStringLiteral("mcp__")), "MCP 协议：工具线上名带 mcp__ 前缀");
    check(!wire.contains('.'), "MCP 协议：线上名里没有点号");
    check(wire.size() <= McpLimits::kToolNameMaxLength, "MCP 协议：线上名不超过长度上限");
    check(wire.endsWith(QStringLiteral("second_page")), "MCP 协议：非法字符换成下划线");

    tool.name = QString(80, QChar('x'));
    check(tool.wireName().size() <= McpLimits::kToolNameMaxLength,
          "MCP 协议：过长工具名被截断");

    // ---------- 配置哈希 ----------
    McpServerConfig server;
    server.id = QStringLiteral("server-1");
    server.name = QStringLiteral("测试");
    server.command = QStringLiteral("C:/Servers/mcp/server.exe");
    server.enabled = true;
    const QString baseHash = server.configHash();
    check(baseHash.size() == 64, "MCP 协议：configHash 是 sha256");
    server.arguments = {QStringLiteral("--stdio")};
    check(server.configHash() != baseHash, "MCP 协议：改参数改变 configHash");
    const QString withArgs = server.configHash();
    server.workingDirectory = QStringLiteral("C:/Servers/mcp");
    check(server.configHash() != withArgs, "MCP 协议：改工作目录改变 configHash");
    server.environmentNames = {QStringLiteral("PATH")};
    const QString withEnv = server.configHash();
    server.enabled = false;
    check(server.configHash() != withEnv, "MCP 协议：启用开关也进 configHash");
}
