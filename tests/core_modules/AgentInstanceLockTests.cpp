/**
 * @file AgentInstanceLockTests.cpp
 * @brief 首次锁定、锁定后不跟随主页、换游戏根/同名实例、显式切换。
 */

#include "Support.hpp"

#include "agent/AgentInstanceLock.hpp"

#include <QTemporaryDir>

void runAgentInstanceLockTests() {
    QTemporaryDir rootA;
    QTemporaryDir rootB;
    const QString rootIdA = AgentInstanceLocks::gameRootId(rootA.path());
    const QString rootIdB = AgentInstanceLocks::gameRootId(rootB.path());
    check(!rootIdA.isEmpty() && rootIdA != rootIdB, "实例锁：游戏根身份不同");

    AgentInstanceInfo a;
    a.id = QStringLiteral("1.20.1-forge");
    a.name = QStringLiteral("1.20.1 Forge");
    a.gameRootId = rootIdA;
    AgentInstanceInfo b = a;
    b.name = QStringLiteral("另一个文件夹里的同名实例");
    b.gameRootId = rootIdB;

    AgentSession session;
    session.meta.id = QStringLiteral("s1");
    check(!session.meta.instanceLock.isValid(), "实例锁：新会话未锁定");
    check(AgentInstanceLocks::chipText(session.meta.instanceLock, a) == a.name,
          "实例锁：未锁定时胶囊显示主页当前实例");
    check(AgentInstanceLocks::chipText(session.meta.instanceLock, AgentInstanceInfo{}) == QStringLiteral("无"),
          "实例锁：未锁定且主页没选实例时显示「无」");
    check(AgentInstanceLocks::lock(&session, a), "实例锁：首次锁定成功");
    check(session.meta.instanceLock.isValid() && session.meta.instanceLock.id == a.id,
          "实例锁：锁定 id 写入会话");
    check(AgentInstanceLocks::isUsable(session.meta.instanceLock, a),
          "实例锁：同一游戏根 + 同一实例可用");
    check(!AgentInstanceLocks::isUsable(session.meta.instanceLock, b),
          "实例锁：换游戏文件夹后锁定实例不可用");
    AgentInstanceInfo sameNameOtherId = a;
    sameNameOtherId.id = QStringLiteral("1.20.1-forge-copy");
    check(!AgentInstanceLocks::isUsable(session.meta.instanceLock, sameNameOtherId),
          "实例锁：同名不同 id 不当作同一个");
    check(AgentInstanceLocks::chipText(session.meta.instanceLock, a) == session.meta.instanceLock.name,
          "实例锁：锁定后胶囊显示锁定的实例名");
    check(AgentInstanceLocks::chipText(session.meta.instanceLock, b).contains(QStringLiteral("不可用")),
          "实例锁：锁定的实例不在当前游戏目录时胶囊标「不可用」");

    check(AgentInstanceLocks::switchTo(&session, b), "实例锁：显式切换覆盖旧锁");
    check(session.meta.instanceLock.gameRootId == rootIdB &&
              session.meta.instanceLock.name == b.name,
          "实例锁：切换后锁指向新实例");
    AgentInstanceInfo invalid;
    check(!AgentInstanceLocks::lock(&session, invalid), "实例锁：空实例拒绝锁定");
}
