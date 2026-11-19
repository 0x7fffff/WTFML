/**
 * @file AgentPerformanceTests.cpp
 * @brief Stage 4 的性能基线（只测本机可重复的那几项）：2000 条会话读写、上下文裁剪、
 *        加密会话往返。数字打印到 stdout（`[perf]` 前缀），记录见
 *        docs/dev/perf-notes/AGENT_STAGE4.md。
 */

#include "Support.hpp"

#include "agent/AgentContextBuilder.hpp"
#include "agent/AgentSessionStore.hpp"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTemporaryDir>

namespace {

QList<Agent::Message> longHistory(int count) {
    QList<Agent::Message> messages;
    for (int i = 0; i < count; ++i) {
        Agent::Message message;
        message.id = QStringLiteral("m-%1").arg(i);
        message.role = (i % 2 == 0) ? Agent::Role::User : Agent::Role::Assistant;
        message.content = QStringLiteral("第 %1 条消息：启动器在 1.20.1 上启动失败，"
                                         "日志里说缺少前置模组。")
                              .arg(i);
        message.status = Agent::MessageStatus::Complete;
        message.createdAt = QDateTime::currentDateTimeUtc();
        messages.append(message);
    }
    return messages;
}

void report(const QString &what, qint64 ms) {
    out() << "  [perf] " << what << "=" << ms << "ms\n";
    out().flush();
}

}  // namespace

void runAgentPerformanceTests() {
    QTemporaryDir fixture;
    check(fixture.isValid(), "性能：临时目录可用");

    // ---------- 会话读写：2000 条 ----------
    AgentSessionStore store(fixture.path() + QStringLiteral("/sessions"));
    AgentSession session;
    QString problem;
    check(store.create(&session, &problem), "性能：建会话", problem);
    session.messages = longHistory(2000);
    session.meta.title = QStringLiteral("性能基线");
    QElapsedTimer timer;
    timer.start();
    check(store.save(session, true, &problem), "性能：保存 2000 条消息", problem);
    const qint64 saveMs = timer.elapsed();
    report(QStringLiteral("session-save-2000"), saveMs);
    check(saveMs < 5000, "性能：保存 2000 条消息在 5 秒内", QString::number(saveMs));

    timer.restart();
    AgentSession loaded;
    check(store.load(session.meta.id, &loaded, &problem), "性能：读回 2000 条消息", problem);
    const qint64 loadMs = timer.elapsed();
    report(QStringLiteral("session-load-2000"), loadMs);
    check(loaded.messages.size() == 2000, "性能：读回条数一致",
          QString::number(loaded.messages.size()));
    check(loadMs < 5000, "性能：读回 2000 条消息在 5 秒内", QString::number(loadMs));

    // ---------- 加密会话往返 ----------
    timer.restart();
    check(store.setEncrypted(session.meta.id, true, &problem), "性能：开启整会话加密", problem);
    const qint64 encryptMs = timer.elapsed();
    report(QStringLiteral("session-encrypt-2000"), encryptMs);
    AgentSession encrypted;
    timer.restart();
    check(store.load(session.meta.id, &encrypted, &problem), "性能：读回加密会话", problem);
    const qint64 decryptMs = timer.elapsed();
    report(QStringLiteral("session-decrypt-2000"), decryptMs);
    check(encrypted.messages.size() == 2000, "性能：加密会话读回条数一致");
    check(encryptMs < 10000, "性能：加密 2000 条在 10 秒内", QString::number(encryptMs));
    check(decryptMs < 10000, "性能：解密 2000 条在 10 秒内", QString::number(decryptMs));

    // ---------- 上下文裁剪 ----------
    const QList<Agent::Message> history = longHistory(2000);
    AgentContextReport reportData;
    timer.restart();
    const QList<Agent::Message> built = AgentContextBuilder::build(
        history, QStringLiteral("系统提示"), 32768, 4096, &reportData);
    const qint64 contextMs = timer.elapsed();
    report(QStringLiteral("context-build-2000"), contextMs);
    check(!built.isEmpty(), "性能：上下文裁剪有输出");
    check(reportData.omittedMessages > 0, "性能：2000 条历史一定被裁掉一部分");
    check(contextMs < 1000, "性能：裁剪 2000 条在 1 秒内", QString::number(contextMs));
    check(built.constLast().content.contains(QStringLiteral("第 1999 条")),
          "性能：最后一条用户消息保留");

    // ---------- 单条工具结果上限 ----------
    QString big;
    big.fill(QChar('x'), 16 * 1024);
    timer.restart();
    int total = 0;
    for (int i = 0; i < 200; ++i) total += AgentContextBuilder::estimateTokens(big);
    const qint64 tokensMs = timer.elapsed();
    report(QStringLiteral("estimate-tokens-200x16k"), tokensMs);
    check(total > 0, "性能：token 估算有结果");
    check(tokensMs < 1000, "性能：估算 200 段 16 KB 文本在 1 秒内", QString::number(tokensMs));
}
