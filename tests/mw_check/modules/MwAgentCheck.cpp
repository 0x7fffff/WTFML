/**
 * @file MwAgentChecks.cpp
 * @brief 真 MainWindow + 假模型客户端：智能体页存在、发消息→流式→完成→落盘、停止、无明文 key。
 */

#include "Support.hpp"

#include "AgentApprovalCard.hpp"
#include "AgentPage.hpp"
#include "AgentToolCard.hpp"
#include "agent/AgentSessionStore.hpp"
#include "controllers/AgentController.hpp"
#include "agent/AgentModelClient.hpp"

#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QUuid>

namespace {

class FakeAgentModelClient : public AgentModelClient {
public:
    explicit FakeAgentModelClient(QObject *parent = nullptr) : AgentModelClient(parent) {}

    void start(const Agent::ModelRequest &request) override {
        starts += 1;
        lastRequest = request;
        const bool hold = holdNext;
        holdNext = false;
        if (toolCallNext && toolRounds == 0) {
            ++toolRounds;
            QTimer::singleShot(0, this, [this, request] {
                Agent::ModelReply reply;
                reply.message.role = Agent::Role::Assistant;
                reply.message.status = Agent::MessageStatus::Complete;
                Agent::ToolCall call;
                call.id = QStringLiteral("mw-call-1");
                call.name = QStringLiteral("fs.read");
                call.arguments = QJsonObject{{QStringLiteral("path"),
                                              QStringLiteral("config/a.toml")}};
                reply.message.toolCalls.append(call);
                emit finished(request.requestId, reply);
            });
            return;
        }
        QTimer::singleShot(0, this, [this, request, hold] {
            emit textDelta(request.requestId, QStringLiteral("收到："));
            emit textDelta(request.requestId, QStringLiteral("启动日志结论"));
            if (hold) return;
            Agent::ModelReply reply;
            reply.message.role = Agent::Role::Assistant;
            reply.message.content = QStringLiteral("收到：启动日志结论");
            reply.message.status = Agent::MessageStatus::Complete;
            reply.usage = Agent::Usage{12, 5, false};
            emit finished(request.requestId, reply);
        });
    }
    void cancel(const QString &requestId) override {
        cancelled.append(requestId);
    }
    void testConnection(const Agent::ModelRequest &request) override {
        emit connectionTested(request.requestId, true, 200, QStringLiteral("连接成功（假）。"));
    }

    int starts = 0;
    int toolRounds = 0;
    bool holdNext = false;
    bool toolCallNext = false;
    QStringList cancelled;
    Agent::ModelRequest lastRequest;
};

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 3000) {
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return predicate();
}

QStringList filesUnder(const QString &root) {
    QStringList files;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) files.append(it.next());
    return files;
}

}  // namespace

int MwAgentChecks(QApplication &app) {
    Q_UNUSED(app);
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    QTabWidget *tabs = window.findChild<QTabWidget *>(MainWindowUI::tabWidgetName());
    check(tabs != nullptr && tabs->count() == 5, "真窗口：五个选项卡");
    AgentPage *page = window.findChild<AgentPage *>(QStringLiteral("agentPage"));
    check(page != nullptr && window.ui()->getAgentPage() == page,
          "真窗口：智能体页由 MainWindowUI 持有");
    AgentController *controller = window.agentController();
    check(controller != nullptr, "真窗口：AgentController 已接线");

    FakeAgentModelClient fake;
    controller->setModelClientForTest(&fake);
    AgentProviderProfile profile;
    profile.id = QStringLiteral("mw-test");
    profile.name = QStringLiteral("假服务");
    profile.baseUrl = QStringLiteral("https://api.example.com/v1");
    profile.model = QStringLiteral("fake-model");
    const QString secret = QStringLiteral("sk-mw-secret-1234567890");
    controller->applyProfileForTest(profile, secret);

    check(page != nullptr && page->composer() != nullptr, "真窗口：输入框在");
    controller->sendForTest(QStringLiteral("启动就崩溃，帮我看看"));
    {
        AgentSession immediate;
        QString immediateProblem;
        controller->sessionStore()->load(controller->currentSessionId(), &immediate,
                                         &immediateProblem);
        check(immediate.meta.title.contains(QStringLiteral("启动就崩溃")),
              "真窗口：发送后标题立即写入盘中", immediate.meta.title);
    }
    check(waitFor([&] {
              return controller->loop() != nullptr &&
                     controller->loop()->state() == AgentLoop::State::Completed;
          }),
          "真窗口：假模型流式完成后 Loop 进入 Completed");
    check(fake.starts == 1, "真窗口：只发一次请求");
    check(!fake.lastRequest.apiKey.isEmpty() && fake.lastRequest.apiKey == secret,
          "真窗口：请求构造时带上 key（只在内存里）");
    check(fake.lastRequest.systemPrompt.contains(QStringLiteral("没有")),
          "真窗口：系统提示说明无工具权限");

    AgentSession loaded;
    QString problem;
    check(controller->sessionStore()->load(controller->currentSessionId(), &loaded, &problem),
          "真窗口：会话可从盘上读回", problem);
    check(loaded.messages.size() == 2 && loaded.messages.constFirst().role == Agent::Role::User &&
              loaded.messages.constLast().role == Agent::Role::Assistant &&
              loaded.messages.constLast().content.contains(QStringLiteral("启动日志结论")),
          "真窗口：用户消息 + 完整助手回复都落了盘",
          QString::number(loaded.messages.size()));
    check(loaded.meta.title.contains(QStringLiteral("启动就崩溃")),
          "真窗口：标题取自第一条用户消息", loaded.meta.title);

    // key 不得出现在会话目录/元数据/日志里。
    bool sawSecret = false;
    for (const QString &path : filesUnder(controller->sessionStore()->root())) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly) && file.readAll().contains(secret.toUtf8())) {
            sawSecret = true;
        }
    }
    check(!sawSecret, "真窗口：会话目录 grep 不到明文 API Key");

    // 停止：半截流 + 取消，保留部分内容并标中断。
    fake.holdNext = true;
    controller->sendForTest(QStringLiteral("再来一次"));
    check(waitFor([&] {
              return controller->loop()->state() == AgentLoop::State::Requesting;
          }),
          "真窗口：第二次请求进入 Requesting");
    check(waitFor([&] {
              return !controller->loop()->assistantMessage().content.isEmpty();
          }),
          "真窗口：收到部分流式文本");
    controller->cancelForTest();
    check(controller->loop()->state() == AgentLoop::State::Cancelled,
          "真窗口：停止后进入 Cancelled");
    check(!fake.cancelled.isEmpty(), "真窗口：停止会取消在飞请求");
    check(controller->sessionStore()->load(controller->currentSessionId(), &loaded, &problem),
          "真窗口：停止后的会话仍可读回");
    check(loaded.messages.constLast().status == Agent::MessageStatus::Interrupted,
          "真窗口：停止后最后一条标为中断");

    // Stage 1B：实例锁定 + 文件读审批 + 工具卡 + 活动会话删除。
    {
        const QString root = QDir::tempPath() + QStringLiteral("/wtfml-agent-mw-instance-") +
                             QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(root + QStringLiteral("/config"));
        QFile config(root + QStringLiteral("/config/a.toml"));
        config.open(QIODevice::WriteOnly);
        config.write("maxMemory = 4096\n");
        config.close();
        AgentInstanceInfo info;
        info.id = QStringLiteral("mw-inst-1");
        info.name = QStringLiteral("MW 实例");
        info.gameRootId = AgentInstanceLocks::gameRootId(root);
        controller->setInstanceContextForTest(root, info);
        fake.toolCallNext = true;
        fake.toolRounds = 0;
        controller->sendForTest(QStringLiteral("看看配置"));
        check(waitFor([&] {
                  return controller->loop()->state() == AgentLoop::State::AwaitingApproval;
              }),
              "真窗口：文件读需要审批，Loop 暂停");
        check(page->findChild<AgentApprovalCard *>() != nullptr,
              "真窗口：审批卡显示在智能体页");
        AgentSession lockedSession;
        QString lockProblem;
        check(controller->sessionStore()->load(controller->currentSessionId(), &lockedSession,
                                               &lockProblem) &&
                  lockedSession.meta.instanceLock.id == info.id,
              "真窗口：首次文件工具前锁定实例并落盘", lockProblem);
        check(page->findChild<QPushButton *>(QStringLiteral("agentApprovalSessionAllow")) == nullptr ||
                  !page->findChild<QPushButton *>(QStringLiteral("agentApprovalSessionAllow"))->isVisible(),
              "真窗口：没有会话级目录放行");
        controller->approveToolForTest(true);
        check(waitFor([&] {
                  return controller->loop()->state() == AgentLoop::State::Completed;
              }),
              "真窗口：审批通过后模型继续并完成");
        check(page->findChild<AgentToolCard *>() != nullptr,
              "真窗口：工具结果卡片存在");
        AgentSession readSession;
        controller->sessionStore()->load(controller->currentSessionId(), &readSession, nullptr);
        bool hasToolMessage = false;
        for (const Agent::Message &message : readSession.messages) {
            if (message.role == Agent::Role::Tool &&
                message.content.contains(QStringLiteral("4096"))) {
                hasToolMessage = true;
            }
        }
        check(hasToolMessage, "真窗口：工具结果进了会话正文");

        // 待审批时取消并删除：不执行工具，删除先取消。
        fake.toolCallNext = true;
        fake.toolRounds = 0;
        controller->sendForTest(QStringLiteral("再读一次"));
        waitFor([&] { return controller->loop()->state() == AgentLoop::State::AwaitingApproval; });
        const QString toDelete = controller->currentSessionId();
        controller->cancelForTest();
        check(controller->deleteSessionForTest(toDelete), "真窗口：取消后可删除活动会话");
        AgentSession gone;
        check(!controller->sessionStore()->load(toDelete, &gone, nullptr),
              "真窗口：删除后会话不可读回");
        QDir(root).removeRecursively();
    }

    controller->shutdown();
    check(controller->loop()->state() == AgentLoop::State::Cancelled ||
              !controller->loop()->busy(),
          "真窗口：shutdown 后不再 busy");
    return 0;
}
