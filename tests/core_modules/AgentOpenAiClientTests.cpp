/**
 * @file AgentOpenAiClientTests.cpp
 * @brief 本地假服务器：流式请求遇到 400 时保留供应商 JSON 错误体；工具线上名不含点号。
 */

#include "Support.hpp"

#include "agent/AgentArtifactStore.hpp"
#include "agent/AgentFileToolEngine.hpp"
#include "agent/AgentFileTools.hpp"
#include "agent/OpenAiCompatibleClient.hpp"

#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

void runAgentOpenAiClientTests() {
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "客户端：本地假服务器启动");
    QByteArray requestBody;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &requestBody] {
        QTcpSocket *socket = server.nextPendingConnection();
        if (socket == nullptr) return;
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &requestBody] {
            QByteArray data = socket->readAll();
            requestBody.append(data);
            const int headerEnd = requestBody.indexOf("\r\n\r\n");
            if (headerEnd < 0) return;
            const QByteArray headers = requestBody.left(headerEnd);
            int contentLength = 0;
            for (const QByteArray &line : headers.split('\n')) {
                if (line.toLower().startsWith("content-length:")) {
                    contentLength = line.mid(15).trimmed().toInt();
                }
            }
            if (requestBody.size() < headerEnd + 4 + contentLength) return;
            const QByteArray error =
                R"({"error":{"message":"Invalid 'tools[0].function.name': string does not match pattern. Expected a string that matches the pattern '^[a-zA-Z0-9_-]+$'.","type":"invalid_request_error"}})";
            QByteArray response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n";
            response += "Content-Length: " + QByteArray::number(error.size()) + "\r\n\r\n" + error;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    });

    QTemporaryDir fixture;
    AgentFileTools files;
    AgentArtifactStore artifacts(fixture.path());
    AgentFileToolEngine engine(&files, &artifacts, [](QString *) { return AgentToolContext{}; });

    OpenAiCompatibleClient client;
    Agent::ModelRequest request;
    request.requestId = QStringLiteral("req-1");
    request.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    request.model = QStringLiteral("deepseek-flash");
    request.apiKey = QStringLiteral("test-key-123456");
    request.localHttpAllowed = true;
    request.stream = true;
    request.tools = engine.schemas();
    request.systemPrompt = QStringLiteral("system");
    Agent::Message user;
    user.role = Agent::Role::User;
    user.content = QStringLiteral("hi");
    request.messages = {user};

    Agent::Error captured;
    QEventLoop loop;
    QObject::connect(&client, &AgentModelClient::failed, &client,
                     [&](const QString &id, const Agent::Error &error) {
                         if (id != request.requestId) return;
                         captured = error;
                         loop.quit();
                     });
    QTimer::singleShot(4000, &loop, &QEventLoop::quit);
    client.start(request);
    if (captured.isEmpty()) loop.exec();
    check(captured.kind == Agent::ErrorKind::Protocol,
          "客户端：HTTP 400 分类为 Protocol", Agent::errorKey(captured.kind));
    check(captured.message.contains(QStringLiteral("tools[0].function.name")),
          "客户端：400 的非流式错误体不会被 SSE 解析丢掉", captured.message);

    const int bodyStart = requestBody.indexOf("\r\n\r\n");
    const QJsonDocument document =
        QJsonDocument::fromJson(requestBody.mid(bodyStart + 4));
    const QJsonArray tools = document.object().value(QStringLiteral("tools")).toArray();
    bool allWireNamesSafe = !tools.isEmpty();
    for (const QJsonValue &value : tools) {
        const QString name = value.toObject()
                                 .value(QStringLiteral("function"))
                                 .toObject()
                                 .value(QStringLiteral("name"))
                                 .toString();
        allWireNamesSafe = allWireNamesSafe && !name.contains(QLatin1Char('.'));
    }
    check(allWireNamesSafe, "客户端：请求里的 function name 不含点号，符合 DeepSeek/OpenAI 规则");
}
