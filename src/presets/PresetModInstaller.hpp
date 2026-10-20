#pragma once

#include "ModInstallPlan.hpp"
#include "InstalledModInventory.hpp"
#include <QObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <functional>
#include <memory>

class DownloadManager;
class ResourceStore;

namespace Presets {
// One installation operation owns its requests and staging files. No nested event loop.
class PresetModInstaller : public QObject {
public:
    struct Result {
        bool ok = false;
        bool cancelled = false;
        int installed = 0;
        int optionalFailures = 0;
        QStringList lines;
    };
    explicit PresetModInstaller(QObject *parent = nullptr, ResourceStore *store = nullptr);
    ~PresetModInstaller() override;
    void start(const QList<ModReference> &references, const QString &root,
               const QString &gameVersion, const QString &loader, const QString &apiKey,
               std::function<bool()> running, std::function<void(Result)> completed);
    void cancel();
    void setSelectionSnapshot(const QJsonArray &snapshot) { m_selectionSnapshot = snapshot; }
    void setInstanceIdentityKey(const QString &key) { m_identityKey = key; }
private:
    void readInventory(bool beforeCommit = false);
    void updateInstalledIdentities();
    bool queryUnidentifiedFiles();
    void resolveNext();
    void download();
    void commit();
    void finish(const QString &problem = {});
    ResourceStore *m_store = nullptr;
    DownloadManager *m_download = nullptr;
    ModInstallRequest m_request;
    ModInstallPlan m_plan;
    QList<ModInstallRow> m_downloadRows;
    QHash<QString, ModResolveInput> m_answers;
    QSet<QString> m_versionLookups;
    QString m_root, m_waiting;
    std::unique_ptr<QTemporaryDir> m_stage;
    std::function<bool()> m_running;
    std::function<void(Result)> m_completed;
    Result m_result;
    QJsonArray m_selectionSnapshot;
    bool m_done = false;
    QString m_identityKey;
    InstalledModInventory m_inventory;
    QList<InstalledModFile> m_identifiedFiles;
    QSet<QString> m_duplicateInstalled;
    QSet<QString> m_ambiguousInstalledProjects;
    QSet<QString> m_hashLookups;
    QSet<QString> m_hashIdentityConflicts;
    std::shared_ptr<std::atomic_bool> m_scanCancel = std::make_shared<std::atomic_bool>(false);
};
}
