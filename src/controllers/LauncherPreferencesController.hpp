#pragma once

#include "settings/LauncherSettings.hpp"

#include <QObject>
#include <functional>

class GlobalJavaSettings;
class MainWindowUI;
class ResourceStore;
class VersionManager;

class LauncherPreferencesController final : public QObject {
    Q_OBJECT

public:
    struct Host {
        std::function<void(const QString &)> log;
        std::function<void()> refreshInstalled;
        std::function<void()> refreshAvailable;
    };

    LauncherPreferencesController(MainWindowUI *ui, LauncherSettings *settings,
                                  GlobalJavaSettings *javaSettings,
                                  VersionManager *versions, ResourceStore *resources,
                                  Host host, QObject *parent = nullptr);

    void launcherBehaviourChanged();
    void downloadSourceChanged();
    void downloadConcurrencyChanged();
    void downloadSpeedLimitChanged();
    void downloadProxyModeChanged();
    void downloadProxyManualChanged();
    void downloadAutoIntoInstanceChanged();
    void warnMissingDependenciesChanged();
    void showGameLogChanged();
    void warnDuplicateLaunchChanged();
    void warnWorldLossChanged();
    void processPriorityChanged();
    void gameWindowSizeChanged();
    void gameWindowModeChanged();
    void updateGameWindowSizeEnabled();
    void syncDownloadSource();
    void syncDownloadConcurrency();
    void syncDownloadSpeedLimit();
    void syncDownloadProxy();
    void syncCatalogCredentials();
    void curseForgeApiKeyEdited();
    void applyExtraJvmArgs();

private:
    void log(const QString &message) const;

    MainWindowUI *m_ui = nullptr;
    LauncherSettings *m_settings = nullptr;
    GlobalJavaSettings *m_javaSettings = nullptr;
    VersionManager *m_versions = nullptr;
    ResourceStore *m_resources = nullptr;
    Host m_host;
};
