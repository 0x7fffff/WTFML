/**
 * @file ServerWorkspaceController.cpp
 * @brief 实现服务端进程集合与信号汇聚。
 */
#include "controllers/ServerWorkspaceController.hpp"
#include <QCoreApplication>

#include <algorithm>

#include <QDir>
#include <QFileInfo>

#include "server/ServerRegistry.hpp"

ServerWorkspaceController::ServerWorkspaceController(QObject *parent) : QObject(parent) {}

ServerRun::ServerProcess *ServerWorkspaceController::ensureProcess(const QString &serverId) {
    if (serverId.isEmpty()) {
        return nullptr;
    }
    if (ServerRun::ServerProcess *existing = process(serverId)) {
        return existing;
    }
    auto *created = new ServerRun::ServerProcess(this);
    m_processes.insert(serverId, created);
    connect(created, &ServerRun::ServerProcess::message, this,
            &ServerWorkspaceController::message);
    connect(created, &ServerRun::ServerProcess::statusChanged, this,
            &ServerWorkspaceController::statusChanged);
    connect(created, &ServerRun::ServerProcess::lineAppended, this,
            &ServerWorkspaceController::lineAppended);
    connect(created, &ServerRun::ServerProcess::forceKillAvailable, this,
            &ServerWorkspaceController::forceKillAvailable);
    connect(created, &ServerRun::ServerProcess::exited, this,
            &ServerWorkspaceController::exited);
    return created;
}

ServerRun::ServerProcess *ServerWorkspaceController::process(const QString &serverId) const {
    return m_processes.value(serverId, nullptr);
}

QStringList ServerWorkspaceController::runningServerIds() const {
    QStringList ids;
    for (auto it = m_processes.constBegin(); it != m_processes.constEnd(); ++it) {
        if (it.value() != nullptr && it.value()->isRunning()) {
            ids.append(it.key());
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void ServerWorkspaceController::removeProcess(const QString &serverId) {
    if (ServerRun::ServerProcess *dead = m_processes.take(serverId)) {
        dead->deleteLater();
    }
}

bool ServerWorkspaceController::stopAndWait(const QString &serverId) {
    ServerRun::ServerProcess *running = process(serverId);
    return running != nullptr && running->stopAndWait();
}

bool ServerWorkspaceController::detach(const QString &serverId) {
    ServerRun::ServerProcess *running = process(serverId);
    if (running == nullptr) {
        return false;
    }
    running->detach();
    return true;
}

ServerInstall::Plan ServerWorkspaceController::launchPlan(const QString &serverId,
                                                          QString *problem) {
    if (problem != nullptr) {
        problem->clear();
    }
    const ServerModel::Info info = ServerRegistry::readMeta(serverId);
    if (info.isEmpty()) {
        if (problem != nullptr) {
            *problem = QCoreApplication::translate("ServerWorkspaceController", "服务端安装记录缺失，无法确定启动参数。请检查启动文件，或通过启动器重新创建服务端。");
        }
        return {};
    }
    ServerInstall::Request request;
    request.flavor = info.flavor;
    request.gameVersion = info.gameVersion;
    request.loaderVersion = info.loaderVersion;
    request.installerVersion = info.installerVersion;
    request.buildId = info.buildId;
    const ServerInstall::Plan plan = ServerInstall::planFor(request);
    if (!plan.ok && problem != nullptr) {
        *problem = plan.problem;
    }
    return plan;
}

int ServerWorkspaceController::countEnabledMods(const QString &serverDir) {
    const QDir modsDir(QDir(serverDir).filePath(QStringLiteral("mods")));
    int count = 0;
    for (const QFileInfo &info : modsDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        if (info.fileName().endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive)) {
            ++count;
        }
    }
    return count;
}

qint64 ServerWorkspaceController::defaultWorldBytes(const QString &serverDir) {
    qint64 bytes = 0;
    QList<QFileInfo> pending{QFileInfo(QDir(serverDir).filePath(QStringLiteral("world")))};
    while (!pending.isEmpty()) {
        const QFileInfo current = pending.takeLast();
        if (current.isSymLink() || current.isJunction()) {
            continue;
        }
        if (current.isFile()) {
            bytes += current.size();
            continue;
        }
        if (current.isDir()) {
            pending.append(QDir(current.absoluteFilePath())
                               .entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System
                                              | QDir::NoDotAndDotDot));
        }
    }
    return bytes;
}
