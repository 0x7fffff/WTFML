#pragma once

#include "manage/ServerStatusCache.hpp"
#include "util/ServerPing.hpp"
#include "InstanceSettingsDialog.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>

#include <functional>

class ServerPingController final : public QObject {
    Q_OBJECT

public:
    using ServerList = QList<QPair<QString, QString>>;
    struct Host {
        std::function<ServerList(const QString &)> serverList;
    };

    explicit ServerPingController(Host host, QObject *parent = nullptr);
    void ping(InstanceSettingsDialog *dialog, const QString &versionId);

private:
    struct Run {
        QPointer<InstanceSettingsDialog> dialog;
        QStringList addresses;
        QString versionId;
        QStringList pendingAddresses;
        QPointer<InstanceSettingsDialog> pendingDialog;
        QString pendingVersionId;
        QHash<int, ServerStatusCache::Entry> cached;
        QHash<int, ServerPing::Status> fresh;
        bool active = false;
    };

    static InstanceSettingsDialog::ServerRowStatus rowFromCache(
        const ServerStatusCache::Entry &entry);
    static QHash<int, ServerStatusCache::Entry> cachedEntries(const QStringList &addresses);
    static QList<InstanceSettingsDialog::ServerRowStatus> cachedRows(const QStringList &addresses);
    void begin(InstanceSettingsDialog *dialog, const QString &versionId,
               const QStringList &addresses);
    void pushRows();
    void oneFinished(int index, const ServerPing::Status &status);
    void allFinished();

    Host m_host;
    Run m_run;
    ServerPing::Pinger *m_pinger = nullptr;
};
