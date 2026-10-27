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
class QLockFile;

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
    struct ReviewDecision { bool accepted = false; QHash<QString, ModReference> alternatives; };
    using Review = std::function<void(const ModInstallPlan &, std::function<void(ReviewDecision)>)>;
    void setReviewHandler(Review review) { m_review = std::move(review); }
    void setPlanCurrent(std::function<bool()> current) { m_planCurrent = std::move(current); }
private:
    void readInventory(bool beforeCommit = false);
    void updateInstalledIdentities();
    bool queryUnidentifiedFiles();
    void resolveNext();
    void download();
    void commit();
    void deployOne(int index);
    void finish(const QString &problem = {});
    bool saveExecutionRecord();
    ResourceStore *m_store = nullptr;
    DownloadManager *m_download = nullptr;
    ModInstallRequest m_request;
    ModInstallPlan m_plan;
    QList<ModInstallRow> m_downloadRows;
    QHash<QString, ModResolveInput> m_answers;
    QSet<QString> m_versionLookups;
    QString m_root, m_waiting;
    std::unique_ptr<QTemporaryDir> m_stage;
    std::unique_ptr<QLockFile> m_writeLock;
    std::function<bool()> m_running;
    std::function<void(Result)> m_completed;
    Result m_result;
    QJsonArray m_selectionSnapshot;
    bool m_done = false;
    bool m_reviewApproved = false, m_reviewPending = false;
    Review m_review;
    std::function<bool()> m_planCurrent;
    QString m_phase = QStringLiteral("plan");
    QHash<QString, QString> m_fileStates;
    QSet<QString> m_skippedDownloads;
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
