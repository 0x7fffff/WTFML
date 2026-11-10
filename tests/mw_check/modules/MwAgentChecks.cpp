/**
 * @file MwAgentChecks.cpp
 * @brief 真 MainWindow + 假模型客户端：智能体页存在、发消息→流式→完成→落盘、停止、无明文 key。
 */

#include "Support.hpp"

#include "AgentPage.hpp"
#include "agent/AgentSessionStore.hpp"
#include "controllers/AgentController.hpp"
#include "agent/AgentModelClient.hpp"

#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QTabWidget>
#include <QThread>
#include <QTimer>

namespace {

class FakeAgentModelClient : public AgentModelClient {
public:
    explicit FakeAgentModelClient(QObject *parent = nullptr) : AgentModelClient(parent) {}

    void start(const Agent::ModelRequest &request) override {
        starts += 1;
        lastRequest = request;
        const bool hold = holdNext;
        holdNext = false;
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
    bool holdNext = false;
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

    controller->shutdown();
    check(controller->loop()->state() == AgentLoop::State::Cancelled ||
              !controller->loop()->busy(),
          "真窗口：shutdown 后不再 busy");
    return 0;
}
