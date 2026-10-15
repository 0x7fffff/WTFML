#pragma once

#include "manage/ModpackExport.hpp"

#include <QObject>
#include <functional>

class LauncherSettings;
class OverlayHost;
class VersionManager;

class ModpackExportController final : public QObject {
    Q_OBJECT
public:
    struct Host {
        std::function<bool(const QString &)> isBeingModified;
        std::function<QString(const QString &)> displayName;
        std::function<void(const QString &)> log;
        std::function<void(const QString &)> setStatus;
    };
    ModpackExportController(VersionManager *versions, LauncherSettings *settings,
                            OverlayHost *overlay, Host host, QObject *parent = nullptr);
    void exportPack(const QString &versionId, const QString &targetPath);
    bool busy() const { return m_busy; }

private:
    void continueExport(const ModpackExport::ExportPlan &plan, const QString &targetPath,
                        ModpackExport::Format format);
    QString displayName(const QString &id) const;
    void log(const QString &text) const;
    void setStatus(const QString &text) const;

    VersionManager *m_versions = nullptr;
    LauncherSettings *m_settings = nullptr;
    OverlayHost *m_overlay = nullptr;
    Host m_host;
    bool m_busy = false;
};
