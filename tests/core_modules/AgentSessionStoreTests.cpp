/**
 * @file AgentSessionStoreTests.cpp
 * @brief 元数据/正文分开、往返、bodySha256、快照、损坏隔离、删除标记、大小上限。
 */

#include "Support.hpp"

#include "agent/AgentSessionStore.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>

namespace {
Agent::Message userMessage(const QString &text, const QString &id = QString()) {
    Agent::Message message;
    message.id = id.isEmpty() ? QStringLiteral("m-%1").arg(text) : id;
    message.role = Agent::Role::User;
    message.content = text;
    message.status = Agent::MessageStatus::Complete;
    message.createdAt = QDateTime::currentDateTimeUtc();
    return message;
}
}  // namespace

void runAgentSessionStoreTests() {
    {
        AgentSession session;
        session.meta.title = QStringLiteral("闪退排查");
        session.messages = {userMessage(QStringLiteral("启动就崩")), userMessage(QStringLiteral("  "))};
        Agent::Message reply = userMessage(QStringLiteral("先看日志"), QStringLiteral("a1"));
        reply.role = Agent::Role::Assistant;
        Agent::Message tool = userMessage(QStringLiteral("{\"content\":\"secret\"}"), QStringLiteral("t1"));
        tool.role = Agent::Role::Tool;
        session.messages << reply << tool;
        const QString md = AgentSessionExport::toMarkdown(session);
        check(md == QStringLiteral("# 闪退排查\n\n## 我\n\n启动就崩\n\n## 助手\n\n先看日志\n"),
              "会话导出：标题 + 我 / 助手各一节，空消息与工具结果不写", md);
    }
    QTemporaryDir fixture;
    check(fixture.isValid(), "会话存储：临时目录可用");
    AgentSessionStore store(fixture.path());
    AgentSession session;
    QString problem;
    check(store.create(&session, &problem), "会话存储：新建会话", problem);
    check(!session.meta.id.isEmpty(), "会话存储：生成 uuid");
    check(QFileInfo::exists(fixture.path() + "/sessions/" + session.meta.id + ".meta.json"),
          "会话存储：元数据单独落盘");
    check(QFileInfo::exists(fixture.path() + "/sessions/" + session.meta.id + ".json"),
          "会话存储：默认正文是明文 json");

    session.messages.append(userMessage(QStringLiteral("启动就崩溃")));
    session.meta.title = AgentSessionTitle::fromMessage(QStringLiteral("启动就崩溃"));
    check(store.save(session, true, &problem), "会话存储：保存正文+元数据", problem);
    AgentSession loaded;
    check(store.load(session.meta.id, &loaded, &problem), "会话存储：读回同一会话", problem);
    check(loaded.messages.size() == 1 && loaded.messages.at(0).content == QStringLiteral("启动就崩溃"),
          "会话存储：消息往返");
    check(loaded.meta.bodySha256.size() == 64, "会话存储：bodySha256 是 64 位十六进制");
    check(QDir(fixture.path() + "/sessions/.recovery/" + session.meta.id)
              .entryList(QDir::Dirs | QDir::NoDotAndDotDot)
              .size() == 1,
          "会话存储：稳定点生成恢复快照");

    // bodySha 不一致时以正文为准重算元数据，不覆盖/删除正文。
    {
        QFile meta(fixture.path() + "/sessions/" + session.meta.id + ".meta.json");
        check(meta.open(QIODevice::ReadOnly), "会话存储：读元数据准备制造哈希不一致");
        QJsonObject object = QJsonDocument::fromJson(meta.readAll()).object();
        meta.close();
        object.insert(QStringLiteral("bodySha256"), QStringLiteral("deadbeef"));
        check(meta.open(QIODevice::WriteOnly | QIODevice::Truncate), "会话存储：写坏哈希");
        meta.write(QJsonDocument(object).toJson());
        meta.close();
    }
    loaded = {};
    check(store.load(session.meta.id, &loaded, &problem), "会话存储：哈希不一致仍能载入", problem);
    {
        QFile meta(fixture.path() + "/sessions/" + session.meta.id + ".meta.json");
        meta.open(QIODevice::ReadOnly);
        const QJsonObject object = QJsonDocument::fromJson(meta.readAll()).object();
        check(object.value(QStringLiteral("bodySha256")).toString() != QStringLiteral("deadbeef"),
              "会话存储：按正文重算 bodySha256");
    }

    // 损坏正文：隔离，不删；不当作空会话覆盖。
    QFile body(fixture.path() + "/sessions/" + session.meta.id + ".json");
    check(body.open(QIODevice::WriteOnly | QIODevice::Truncate), "会话存储：制造损坏正文");
    body.write("{not json");
    body.close();
    AgentSession broken;
    check(!store.load(session.meta.id, &broken, &problem), "会话存储：损坏正文拒绝载入");
    check(!QFileInfo::exists(fixture.path() + "/sessions/" + session.meta.id + ".json"),
          "会话存储：损坏正文被移走");
    const QStringList quarantined = QDir(fixture.path() + "/sessions")
                                        .entryList({QStringLiteral("*.corrupt-*")}, QDir::Files);
    check(quarantined.size() == 1, "会话存储：损坏文件改名隔离", quarantined.join(','));

    // 改名/固定/排序。
    AgentSession second;
    check(store.create(&second, &problem), "会话存储：第二个会话");
    second.meta.title = QStringLiteral("B");
    second.meta.updatedAt = QDateTime::currentDateTimeUtc().addSecs(2);
    second.messages.append(userMessage(QStringLiteral("b")));
    store.save(second, false);
    check(store.rename(second.meta.id, QStringLiteral("  新标题  "), &problem), "会话存储：改名");
    check(store.setPinned(second.meta.id, true, &problem), "会话存储：固定");
    QList<AgentSessionMeta> metas;
    check(store.list(&metas, &problem), "会话存储：列表", problem);
    check(!metas.isEmpty() && metas.constFirst().id == second.meta.id && metas.constFirst().pinned,
          "会话存储：固定优先");
    check(metas.constFirst().title == QStringLiteral("新标题"), "会话存储：改名去掉两端空白");

    // 删除标记 + 删除顺序。
    check(store.requestDeletion(second.meta.id, &problem), "会话存储：登记 pending 删除");
    {
        QFile meta(fixture.path() + "/sessions/" + second.meta.id + ".meta.json");
        meta.open(QIODevice::ReadOnly);
        const QJsonObject object = QJsonDocument::fromJson(meta.readAll()).object();
        check(object.value(QStringLiteral("deletionState")).toString() == QStringLiteral("pending"),
              "会话存储：pending 落到元数据");
    }
    check(store.completeDeletion(second.meta.id, &problem, nullptr), "会话存储：完成删除", problem);
    check(!QFileInfo::exists(fixture.path() + "/sessions/" + second.meta.id + ".meta.json"),
          "会话存储：删除后元数据消失");

    // 大小上限：2000 条消息之后停止追加。
    AgentSession big;
    check(store.create(&big, &problem), "会话存储：大会话创建");
    for (int i = 0; i < AgentSessionStore::maxMessages(); ++i) {
        big.messages.append(userMessage(QStringLiteral("x"), QStringLiteral("id-%1").arg(i)));
    }
    check(store.save(big, false, &problem), "会话存储：2000 条消息可以保存", problem);
    big.messages.append(userMessage(QStringLiteral("overflow"), QStringLiteral("overflow")));
    check(!store.save(big, false, &problem), "会话存储：超过 2000 条拒绝保存");
    check(problem.contains(QStringLiteral("2000")), "会话存储：超限提示条数", problem);

    // 快照只保留最近 3 份。
    AgentSession snap;
    store.create(&snap, &problem);
    for (int i = 0; i < 5; ++i) {
        snap.messages.append(userMessage(QStringLiteral("s%1").arg(i), QStringLiteral("s%1").arg(i)));
        store.save(snap, true);
        QThread::msleep(3);
    }
    check(QDir(fixture.path() + "/sessions/.recovery/" + snap.meta.id)
              .entryList(QDir::Dirs | QDir::NoDotAndDotDot)
              .size() == 3,
          "会话存储：恢复快照最多 3 份");
}
