/**
 * @file AgentLoopTests.cpp
 * @brief 无工具 loop：文本→完成、重试、取消、部分流不自动重发、无 key 报错。
 */

#include "Support.hpp"

#include "agent/AgentLoop.hpp"
#include "agent/AgentModelClient.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

namespace {

class FakeModelClient : public AgentModelClient {
public:
    explicit FakeModelClient(QObject *parent = nullptr) : AgentModelClient(parent) {}

    void start(const Agent::ModelRequest &request) override {
        starts += 1;
        requests.append(request);
        const int mode = behaviours.isEmpty() ? 0 : behaviours.takeFirst();
        QTimer::singleShot(0, this, [this, request, mode] {
            if (mode == 1) {
                emit failed(request.requestId,
                            Agent::Error{Agent::ErrorKind::Network, QStringLiteral("断网"),
                                         0, -1, true});
                return;
            }
            if (mode == 2) {
                emit textDelta(request.requestId, QStringLiteral("半截"));
                emit failed(request.requestId,
                            Agent::Error{Agent::ErrorKind::Network, QStringLiteral("流中断"),
                                         0, -1, true});
                return;
            }
            emit textDelta(request.requestId, QStringLiteral("你"));
            emit textDelta(request.requestId, QStringLiteral("好"));
            Agent::ModelReply reply;
            reply.message.role = Agent::Role::Assistant;
            reply.message.content = QStringLiteral("你好");
            reply.message.status = Agent::MessageStatus::Complete;
            reply.usage = Agent::Usage{10, 2, false};
            emit finished(request.requestId, reply);
        });
    }
    void cancel(const QString &requestId) override { cancelled.append(requestId); }
    void testConnection(const Agent::ModelRequest &) override {}

    int starts = 0;
    QList<int> behaviours;
    QList<Agent::ModelRequest> requests;
    QStringList cancelled;
};

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 2000) {
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return predicate();
}

Agent::Message user(const QString &text) {
    Agent::Message message;
    message.id = QStringLiteral("u1");
    message.role = Agent::Role::User;
    message.content = text;
    message.status = Agent::MessageStatus::Complete;
    message.createdAt = QDateTime::currentDateTimeUtc();
    return message;
}

}  // namespace

void runAgentLoopTests() {
    {
        FakeModelClient client;
        AgentLoop loop(&client);
        AgentLoop::Config config;
        config.profile.baseUrl = QStringLiteral("https://api.example.com/v1");
        config.profile.model = QStringLiteral("m");
        config.apiKey = QStringLiteral("k");
        config.systemPrompt = QStringLiteral("system");
        loop.setConfig(config);
        Agent::Message finished;
        QObject::connect(&loop, &AgentLoop::turnFinished, &loop,
                         [&finished](const Agent::Message &message) { finished = message; });
        loop.setHistory({user(QStringLiteral("hi"))});
        loop.start();
        check(waitFor([&] { return loop.state() == AgentLoop::State::Completed; }),
              "Loop：文本流完成后进入 Completed");
        check(finished.content == QStringLiteral("你好") &&
                  finished.status == Agent::MessageStatus::Complete,
              "Loop：合并 delta 成完整回复", finished.content);
        check(loop.history().size() == 2 && loop.history().constLast().role == Agent::Role::Assistant,
              "Loop：完成后历史追加 assistant");
        check(client.starts == 1, "Loop：只发一次请求");
        check(client.requests.constFirst().systemPrompt == QStringLiteral("system"),
              "Loop：系统提示进入请求");
    }
    {
        FakeModelClient client;
        client.behaviours = {1, 0};
        AgentLoop loop(&client);
        AgentLoop::Config config;
        config.profile.baseUrl = QStringLiteral("https://api.example.com/v1");
        config.profile.model = QStringLiteral("m");
        config.apiKey = QStringLiteral("k");
        loop.setConfig(config);
        loop.setRetryDelaysForTest({0, 0});
        loop.setHistory({user(QStringLiteral("hi"))});
        loop.start();
        check(waitFor([&] { return loop.state() == AgentLoop::State::Completed; }),
              "Loop：首字节前网络失败自动重试后完成");
        check(client.starts == 2, "Loop：重试一次", QString::number(client.starts));
    }
    {
        FakeModelClient client;
        client.behaviours = {2};
        AgentLoop loop(&client);
        AgentLoop::Config config;
        config.profile.baseUrl = QStringLiteral("https://api.example.com/v1");
        config.profile.model = QStringLiteral("m");
        config.apiKey = QStringLiteral("k");
        loop.setConfig(config);
        loop.setRetryDelaysForTest({0, 0});
        loop.setHistory({user(QStringLiteral("hi"))});
        loop.start();
        check(waitFor([&] { return loop.state() == AgentLoop::State::Failed; }),
              "Loop：流已开始后失败进入 Failed");
        check(client.starts == 1, "Loop：已有文本时不自动重发");
        check(loop.history().constLast().status == Agent::MessageStatus::Interrupted,
              "Loop：保留部分文本并标为中断");
        check(loop.history().constLast().content == QStringLiteral("半截"),
              "Loop：中断时保留已收到文本");
    }
    {
        FakeModelClient client;
        AgentLoop loop(&client);
        AgentLoop::Config config;
        config.profile.baseUrl = QStringLiteral("https://api.example.com/v1");
        config.profile.model = QStringLiteral("m");
        config.apiKey = QStringLiteral("k");
        loop.setConfig(config);
        loop.setHistory({user(QStringLiteral("hi"))});
        loop.start();
        check(loop.busy(), "Loop：发出后处于 busy");
        loop.cancel();
        check(!loop.busy() && loop.state() == AgentLoop::State::Cancelled,
              "Loop：取消后立即回到非 busy / Cancelled");
        check(!client.cancelled.isEmpty(), "Loop：取消会通知客户端");
        check(loop.history().constLast().status == Agent::MessageStatus::Interrupted,
              "Loop：取消保留中断消息");
    }
    {
        FakeModelClient client;
        AgentLoop loop(&client);
        AgentLoop::Config config;
        config.profile.baseUrl = QStringLiteral("https://api.example.com/v1");
        config.profile.model = QStringLiteral("m");
        config.apiKey.clear();
        loop.setConfig(config);
        loop.setHistory({user(QStringLiteral("hi"))});
        Agent::Error error;
        QObject::connect(&loop, &AgentLoop::turnFailed, &loop,
                         [&error](const Agent::Error &value) { error = value; });
        loop.start();
        check(error.kind == Agent::ErrorKind::NoConfiguration,
              "Loop：没有 API Key 时拒绝发送");
        check(client.starts == 0, "Loop：配置不完整时不发网络请求");
    }
    {
        FakeModelClient client;
        client.behaviours = {1, 0};
        AgentLoop loop(&client);
        AgentLoop::Config config;
        config.profile.baseUrl = QStringLiteral("https://api.example.com/v1");
        config.profile.model = QStringLiteral("m");
        config.apiKey = QStringLiteral("k");
        loop.setConfig(config);
        loop.setRetryDelaysForTest({0, 0});
        loop.setHistory({user(QStringLiteral("hi"))});
        loop.start();
        waitFor([&] { return loop.state() == AgentLoop::State::Completed; });
        check(client.requests.size() == 2 &&
                  client.requests.at(1).messages.constLast().role == Agent::Role::User,
              "Loop：重试请求仍以同一条 user 结尾（不重复追加）");
    }
}
