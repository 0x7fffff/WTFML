/**
 * @file AgentInstanceLock.hpp
 * @brief 会话实例锁定：第一次实例操作时锁定；锁定后不跟随主页切换。
 *
 * 工具不接受模型传入的实例 id。切换实例必须显式确认；换游戏文件夹后锁定实例不可用，
 * 不把新文件夹里的同名实例当成同一个。
 */

#pragma once

#include "AgentSession.hpp"

#include <QString>

struct AgentInstanceInfo {
    QString id;
    QString name;
    QString gameRootId;
    bool exists = true;

    bool isValid() const { return !id.isEmpty() && !gameRootId.isEmpty(); }
};

namespace AgentInstanceLocks {

QString gameRootId(const QString &gameRootPath);

/// 首次实例操作：写入 session.meta.instanceLock；失败不“先用后记”。
bool lock(AgentSession *session, const AgentInstanceInfo &info, QString *problem = nullptr);

/// 显式切换（调用方必须先得到用户确认）。旧 lock 直接被替换。
bool switchTo(AgentSession *session, const AgentInstanceInfo &info, QString *problem = nullptr);

/// 锁定实例是否仍在当前游戏文件夹、且 id 仍存在。
bool isUsable(const AgentInstanceLock &lock, const AgentInstanceInfo &current);

QString chipText(const AgentInstanceLock &lock, const AgentInstanceInfo &current);

}  // namespace AgentInstanceLocks
