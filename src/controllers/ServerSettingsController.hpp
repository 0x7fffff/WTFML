#pragma once

#include "server/ServerCheck.hpp"
#include "server/ServerModel.hpp"
#include "server/ServerPortCheck.hpp"

#include <QString>

class ServerWorkspaceController;

class ServerSettingsController final {
public:
    using Values = QList<QPair<QString, QString>>;

    struct SaveResult {
        bool ok = false;
        int changed = 0;
        QStringList rejected;
        QString problem;
        QString serverName;
    };

    explicit ServerSettingsController(const ServerWorkspaceController *workspace = nullptr)
        : m_workspace(workspace) {}

    QString readRaw(const QString &serverId) const;
    Values valuesFromRaw(const QString &raw) const;
    QString mergeIntoRaw(const QString &raw, const Values &edits) const;
    SaveResult save(const QString &serverId, const QString &raw, const Values &edits) const;
    Values fallbackEdits(const ServerModel::Properties &properties) const;

    QString worldName(const QString &serverId) const;
    QString worldDir(const QString &serverId) const;
    QString backupLabel(const QString &serverId) const;
    int worldBackupCount(const QString &serverId) const;
    ServerCheck::Facts checkFacts(const QString &serverId,
                                  ServerPortCheck::State portState) const;

private:
    static qint64 directoryBytes(const QString &dirPath);

    const ServerWorkspaceController *m_workspace = nullptr;
};
