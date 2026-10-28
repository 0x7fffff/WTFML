#include "MainWindow.hpp"
#include "download/DownloadCheckpoint.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/VersionManager.hpp"
#include "util/AppPaths.hpp"
#include "presets/PresetTargetSnapshot.hpp"
#include "server/ServerInstallController.hpp"
#include "controllers/ServerWorkspaceController.hpp"
#include "download/DownloadManager.hpp"
#include "controllers/PresetController.hpp"
#include <QJsonArray>

QJsonObject MainWindow::snapshotDownloadContinuation() const {
    if (m_serverInstall && m_serverInstall->ownsDownloadBatch()) return m_serverInstall->downloadContinuation();
    auto intent = m_versionManager->downloadContinuation();
    if (intent.isEmpty()) return {};
    QJsonArray presets;
    for (const auto &choice : m_installPendingPresets) presets.append(QJsonObject{{"id", choice.first}, {"force", choice.second}});
    QJsonObject fingerprints;
    for (auto it = m_installPresetFingerprints.cbegin(); it != m_installPresetFingerprints.cend(); ++it) fingerprints.insert(it.key(), it.value());
    intent.insert("presets", presets); intent.insert("presetFingerprints", fingerprints);
    if (!presets.isEmpty()) {
        const auto targets = Presets::snapshotInitializationTargets(AppPaths::versionDir(intent.value("instance").toString()));
        if (targets.isEmpty()) return {};
        intent.insert("presetTargets", targets);
    }
    return intent;
}

bool MainWindow::continueDownloadInstallation(Tasks::TaskId id, const QJsonObject &intent) {
    if (intent.value("kind") == "preset-mods") return m_presetController && m_presetController->continueModInstallation(id, intent);
    if (intent.value("kind") == "server-install") {
        if (!m_serverInstall || m_serverInstall->isRunning() || m_downloadInFlight ||
            m_versionManager->downloads()->isBusy()) return false;
        const auto server = intent.value("server").toString();
        const auto process = m_serverWorkspace->process(server);
        if (process && process->isRunning()) return false;
        const auto previous = m_serverInstallTaskId;
        m_serverInstallTaskId = id;
        if (m_serverInstall->continueDownload(intent)) return true;
        m_serverInstallTaskId = previous;
        return false;
    }
    const auto instance = intent.value("instance").toString(), game = intent.value("game").toString();
    const auto kind = intent.value("kind").toString();
    if (m_downloadInFlight || m_versionManager->isDownloading() || m_versionManager->isLoaderInstalling() ||
        m_versionManager->isModpackImporting() || m_versionManager->isNamedInstanceInstalling() ||
        QDir::cleanPath(intent.value("root").toString()) != QDir::cleanPath(AppPaths::minecraftDir()) ||
        !InstanceFiles::validateDirName(instance).isEmpty()) return false;
    if (kind != "repair" && kind != "named-vanilla" && kind != "named-loader") return false;
    if (kind != "repair" && !InstanceFiles::validateDirName(game).isEmpty()) return false;
    const auto originals = intent.value("originals").toObject();
    if (originals.isEmpty()) return false;
    for (auto it = originals.begin(); it != originals.end(); ++it)
        if (DownloadCheckpoint::fingerprint(it.key()) != it.value().toObject()) return false;
    if (!intent.value("presets").toArray().isEmpty()) {
        const auto targets = intent.value("presetTargets").toObject();
        if (targets.isEmpty() || Presets::snapshotInitializationTargets(AppPaths::versionDir(instance)) != targets) return false;
    }
    const int loader = intent.value("loader").toInt(-1);
    if (kind == "named-loader" && (loader < 0 || loader > int(VersionManager::LoaderKind::Quilt) ||
        !VersionManager::loaderInstallSupported(VersionManager::LoaderKind(loader)))) return false;

    m_installPendingPresets.clear(); m_installPresetFingerprints.clear();
    for (const auto &value : intent.value("presets").toArray()) {
        const auto row = value.toObject();
        m_installPendingPresets.append({row.value("id").toString(), row.value("force").toBool()});
    }
    const auto fingerprints = intent.value("presetFingerprints").toObject();
    for (auto it = fingerprints.begin(); it != fingerprints.end(); ++it) m_installPresetFingerprints.insert(it.key(), it.value().toString());
    m_installPendingPresetInstance = kind == "repair" ? QString() : instance;
    m_installPresetModsReady = false;
    m_installPresetModsOk = true;
    m_installPresetModsReport.clear();
    m_downloadTaskId = id;
    m_downloadBatchOwnerTaskId = id;
    if (kind == "repair") m_repairingInstanceId = instance;
    else m_installingInstanceId = instance;
    setDownloadUiBusy(true);
    m_progressThrottle.invalidate();
    if (kind == "repair") m_versionManager->repairVersionFiles(instance);
    else if (kind == "named-vanilla") m_versionManager->installNamedVanilla(instance, game);
    else m_versionManager->installNamedLoader(instance, game, VersionManager::LoaderKind(loader),
        intent.value("loaderVersion").toString(), intent.value("java").toString());
    return true;
}
