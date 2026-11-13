/**
 * @file AgentSessionCryptoTests.cpp
 * @brief 整会话加密/解密、明文正文删除、失败不回退、元数据仍可列。
 */

#include "Support.hpp"

#include "agent/AgentSessionCrypto.hpp"
#include "agent/AgentSessionStore.hpp"

#include <QFile>
#include <QTemporaryDir>

void runAgentSessionCryptoTests() {
    QTemporaryDir fixture;
    AgentSessionStore store(fixture.path());
    AgentSession session;
    QString problem;
    check(store.create(&session, &problem), "会话加密：新建", problem);
    Agent::Message message;
    message.id = QStringLiteral("m1");
    message.role = Agent::Role::User;
    message.content = QStringLiteral("password=super-secret-value");
    message.createdAt = QDateTime::currentDateTimeUtc();
    session.messages.append(message);
    check(store.save(session, false, &problem), "会话加密：先写明文", problem);

    check(store.setEncrypted(session.meta.id, true, &problem), "会话加密：开启整会话加密", problem);
    const QString plain = fixture.path() + "/sessions/" + session.meta.id + ".json";
    const QString encrypted = fixture.path() + "/sessions/" + session.meta.id + ".enc";
    check(!QFileInfo::exists(plain) && QFileInfo::exists(encrypted),
          "会话加密：明文正文被删除，只剩 .enc");
    {
        QFile body(encrypted);
        check(body.open(QIODevice::ReadOnly), "会话加密：读回密文", body.fileName());
        const QByteArray bytes = body.readAll();
        check(bytes.startsWith(AgentSessionCrypto::magic()),
              "会话加密：.enc 以 WTFML-AGENT-SESSION-1 开头");
        check(!bytes.contains("super-secret-value"),
              "会话加密：密文里不含明文正文");
    }
    AgentSession loaded;
    check(store.load(session.meta.id, &loaded, &problem), "会话加密：当前用户可解密", problem);
    check(loaded.messages.size() == 1 && loaded.messages.at(0).content.contains("super-secret-value"),
          "会话加密：解密后正文一致");
    QList<AgentSessionMeta> metas;
    store.list(&metas, nullptr);
    check(!metas.isEmpty() && metas.constFirst().encrypted,
          "会话加密：元数据仍可列且标记 encrypted");

    // 改坏密文：只能报错，不能回退明文。
    {
        QFile body(encrypted);
        check(body.open(QIODevice::WriteOnly | QIODevice::Truncate), "会话加密：改坏密文",
              body.fileName());
        body.write(AgentSessionCrypto::magic() + QByteArray("broken"));
    }
    AgentSession broken;
    check(!store.load(session.meta.id, &broken, &problem), "会话加密：坏密文拒绝载入");
    check(problem.contains(QStringLiteral("无法解密")), "会话加密：错误说明无法解密", problem);

    // 改回明文：重新加密前先把会话恢复成一个可载入的临时副本（测试直接造一份新的）。
    AgentSession second;
    store.create(&second, &problem);
    second.messages.append(message);
    store.save(second, false);
    check(store.setEncrypted(second.meta.id, true, &problem), "会话加密：第二份开启加密");
    check(store.setEncrypted(second.meta.id, false, &problem), "会话加密：关闭加密");
    check(QFileInfo::exists(fixture.path() + "/sessions/" + second.meta.id + ".json") &&
              !QFileInfo::exists(fixture.path() + "/sessions/" + second.meta.id + ".enc"),
          "会话加密：关闭后恢复明文并删掉 .enc");
}
