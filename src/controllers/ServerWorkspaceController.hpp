/**
 * @file ServerWorkspaceController.hpp
 * @brief 持有全部服务端进程及其生命周期，向主窗口暴露稳定的领域信号。
 */
#pragma once

#include <QObject>
#include <QHash>
#include <QStringList>

#include "server/ServerProcess.hpp"
#include "download/ServerInstall.hpp"

class ServerWorkspaceController final : public QObject {
    Q_OBJECT

public:
    explicit ServerWorkspaceController(QObject *parent = nullptr);

    ServerRun::ServerProcess *ensureProcess(const QString &serverId);
    ServerRun::ServerProcess *process(const QString &serverId) const;
    QStringList runningServerIds() const;
    void removeProcess(const QString &serverId);
    bool stopAndWait(const QString &serverId);
    bool detach(const QString &serverId);

    static ServerInstall::Plan launchPlan(const QString &serverId, QString *problem = nullptr);
    static int countEnabledMods(const QString &serverDir);
    static qint64 defaultWorldBytes(const QString &serverDir);

signals:
    void message(const QString &text);
    void statusChanged(const QString &serverId, ServerModel::Status status);
    void lineAppended(const QString &serverId, const QString &line);
    void forceKillAvailable(const QString &serverId);
    void exited(const QString &serverId, int exitCode);

private:
    QHash<QString, ServerRun::ServerProcess *> m_processes;
};
