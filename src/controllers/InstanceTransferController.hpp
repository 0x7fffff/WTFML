#pragma once

#include <QObject>
#include <functional>

class LauncherManager;
class OverlayHost;
class VersionManager;

class InstanceTransferController final : public QObject {
    Q_OBJECT
public:
    struct Host {
        std::function<bool(const QString &)> isBeingModified;
        std::function<QString(const QString &)> displayName;
        std::function<QString(const QString &, const QString &)> instanceDirForKey;
        std::function<void()> refreshInstances;
        std::function<void(const QString &)> log;
    };
    InstanceTransferController(VersionManager *versions, LauncherManager *launcher,
                               OverlayHost *overlay, Host host, QObject *parent = nullptr);
    bool copyInstance(const QString &sourceId, const QString &destGameFolder,
                      const QString &newName);
    bool importWorld(const QString &instanceId, const QString &worldDir, const QString &name);
    bool renameWorld(const QString &instanceId, const QString &worldDir, const QString &newName);

private:
    QString displayName(const QString &id) const;
    void log(const QString &text) const;
    VersionManager *m_versions = nullptr;
    LauncherManager *m_launcher = nullptr;
    OverlayHost *m_overlay = nullptr;
    Host m_host;
};
