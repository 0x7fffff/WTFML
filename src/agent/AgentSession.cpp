/**
 * @file AgentSession.cpp
 * @brief 会话元数据/标题的纯函数部分。
 */

#include "AgentSession.hpp"

#include <QStringList>

QJsonObject AgentSessionMeta::toJson() const {
    QJsonObject object{
        {QStringLiteral("schema"), schema},
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), title},
        {QStringLiteral("createdAt"),
         createdAt.isValid() ? createdAt.toUTC().toString(Qt::ISODateWithMs) : QString()},
        {QStringLiteral("updatedAt"),
         updatedAt.isValid() ? updatedAt.toUTC().toString(Qt::ISODateWithMs) : QString()},
        {QStringLiteral("pinned"), pinned},
        {QStringLiteral("archived"), archived},
        {QStringLiteral("encrypted"), encrypted},
        {QStringLiteral("skill"), skill},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("messageCount"), messageCount},
        {QStringLiteral("bodySha256"), bodySha256},
        {QStringLiteral("policyVersion"), policyVersion},
        {QStringLiteral("lastTurnState"), lastTurnState},
        {QStringLiteral("deletionState"), deletionState},
    };
    if (instanceLock.isValid()) {
        object.insert(QStringLiteral("instanceLock"), instanceLock.toJson());
    } else {
        object.insert(QStringLiteral("instanceLock"), QJsonValue::Null);
    }
    return object;
}

AgentSessionMeta AgentSessionMeta::fromJson(const QJsonObject &object) {
    AgentSessionMeta meta;
    meta.schema = object.value(QStringLiteral("schema")).toInt(1);
    meta.id = object.value(QStringLiteral("id")).toString();
    meta.title = object.value(QStringLiteral("title")).toString();
    meta.createdAt =
        QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(),
                              Qt::ISODateWithMs);
    meta.updatedAt =
        QDateTime::fromString(object.value(QStringLiteral("updatedAt")).toString(),
                              Qt::ISODateWithMs);
    meta.pinned = object.value(QStringLiteral("pinned")).toBool(false);
    meta.archived = object.value(QStringLiteral("archived")).toBool(false);
    meta.encrypted = object.value(QStringLiteral("encrypted")).toBool(false);
    meta.skill = object.value(QStringLiteral("skill")).toString(QStringLiteral("general"));
    meta.provider = object.value(QStringLiteral("provider")).toObject();
    meta.instanceLock =
        AgentInstanceLock::fromJson(object.value(QStringLiteral("instanceLock")).toObject());
    meta.messageCount = object.value(QStringLiteral("messageCount")).toInt(0);
    meta.bodySha256 = object.value(QStringLiteral("bodySha256")).toString();
    meta.policyVersion = object.value(QStringLiteral("policyVersion")).toInt(1);
    meta.lastTurnState =
        object.value(QStringLiteral("lastTurnState")).toString(QStringLiteral("complete"));
    meta.deletionState =
        object.value(QStringLiteral("deletionState")).toString(QStringLiteral("none"));
    return meta;
}

namespace AgentSessionTitle {
QString fromMessage(const QString &message) {
    const QString simplified = message.simplified();
    if (simplified.isEmpty()) {
        return QStringLiteral("新对话");
    }
    if (simplified.size() <= 24) {
        return simplified;
    }
    return simplified.left(24);
}
}  // namespace AgentSessionTitle

namespace AgentSessionExport {
QString toMarkdown(const AgentSession &session) {
    QString out = QStringLiteral("# %1\n").arg(session.meta.title.isEmpty() ? QStringLiteral("新对话")
                                                                             : session.meta.title);
    for (const Agent::Message &message : session.messages) {
        if (message.role != Agent::Role::User && message.role != Agent::Role::Assistant) continue;
        if (message.content.trimmed().isEmpty()) continue;
        out += message.role == Agent::Role::User ? QStringLiteral("\n## 我\n\n") : QStringLiteral("\n## 助手\n\n");
        out += message.content.trimmed() + QLatin1Char('\n');
    }
    return out;
}
}  // namespace AgentSessionExport
