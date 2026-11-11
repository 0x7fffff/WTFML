/**
 * @file AgentInstanceLock.cpp
 * @brief 实例锁定的纯判据（见 .hpp）。
 */

#include "AgentInstanceLock.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>

namespace AgentInstanceLocks {

QString gameRootId(const QString &gameRootPath) {
    const QString canonical =
        QDir::cleanPath(QDir(gameRootPath).absolutePath()).toLower();
    return QStringLiteral("sha256:") +
           QString::fromLatin1(QCryptographicHash::hash(canonical.toUtf8(),
                                                        QCryptographicHash::Sha256)
                                   .toHex());
}

bool lock(AgentSession *session, const AgentInstanceInfo &info, QString *problem) {
    if (session == nullptr || !info.isValid()) {
        if (problem != nullptr) *problem = QStringLiteral("没有可锁定的实例。");
        return false;
    }
    AgentInstanceLock value;
    value.id = info.id;
    value.name = info.name;
    value.gameRootId = info.gameRootId;
    value.lockedAt = QDateTime::currentDateTimeUtc();
    session->meta.instanceLock = value;
    return true;
}

bool switchTo(AgentSession *session, const AgentInstanceInfo &info, QString *problem) {
    return lock(session, info, problem);  // 写入同一格；调用方负责“显式确认”。
}

bool isUsable(const AgentInstanceLock &lock, const AgentInstanceInfo &current) {
    if (!lock.isValid() || !current.isValid() || !current.exists) return false;
    return lock.id == current.id && lock.gameRootId == current.gameRootId;
}

QString chipText(const AgentInstanceLock &lock, const AgentInstanceInfo &current) {
    if (!lock.isValid()) {
        return current.isValid() ? QStringLiteral("跟随主页：%1").arg(current.name)
                                 : QStringLiteral("跟随主页：未选择实例");
    }
    if (isUsable(lock, current)) {
        return QStringLiteral("已锁定：%1").arg(lock.name);
    }
    if (!current.isValid()) {
        return QStringLiteral("已锁定：%1（实例不可用）").arg(lock.name);
    }
    return QStringLiteral("已锁定：%1（主页当前：%2）").arg(lock.name, current.name);
}

}  // namespace AgentInstanceLocks
