#pragma once
#include "download/DownloadCheckpoint.hpp"
#include "manage/InstanceFiles.hpp"
#include <QDirIterator>

namespace Presets {
// Bind restart-time initialization consent to user settings, not to downloaded cores.
// Mods have a separate inventory/collision gate in PresetModInstaller.
inline QJsonObject snapshotInitializationTargets(const QString &root, const std::atomic_bool *cancel = nullptr) {
    QJsonObject result;
    const QStringList names{"options.txt", "optionsof.txt", "servers.dat", "config", "defaultconfigs",
        "resourcepacks", "shaderpacks", "wtfml.datapacks", "datapacks-pending"};
    const auto remember = [&](const QString &path) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return false;
        if (!DownloadCheckpoint::safePath(path)) return false;
        if (QFileInfo(path).isDir()) result.insert(path, QJsonObject{{"directory", true}});
        else {
            const auto state = DownloadCheckpoint::fingerprint(path, cancel);
            if (state.isEmpty()) return false;
            result.insert(path, state);
        }
        return true;
    };
    const auto rememberTree = [&](const QString &path) -> bool {
        if (!remember(path)) return false;
        if (!QFileInfo(path).isDir()) return true;
        QDirIterator it(path, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
        while (it.hasNext()) if (!remember(it.next())) return false;
        return true;
    };
    for (const auto &name : names) if (!rememberTree(QDir(root).filePath(name))) return {};
    const auto saves = QDir(root).filePath("saves");
    if (!remember(saves)) return {};
    const auto worlds = QDir(saves).entryList(QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const auto &world : worlds) {
        const auto path = QDir(saves).filePath(world);
        if (!remember(path) || !remember(QDir(path).filePath("level.dat")) ||
            !rememberTree(QDir(path).filePath("datapacks"))) return {};
    }
    return result;
}

// Value-only recovery gate: safe to run in a worker, without reading launcher/UI state.
inline bool unchangedInitializationContinuation(const QJsonObject &intent, const std::atomic_bool *cancel = nullptr) {
    const auto kind = intent.value("kind").toString();
    if (kind != "repair" && kind != "named-vanilla" && kind != "named-loader") return true;
    const auto root = intent.value("root").toString();
    const auto instance = intent.value("instance").toString();
    if (!QDir::isAbsolutePath(root) || !InstanceFiles::validateDirName(instance).isEmpty()) return false;
    const auto originals = intent.value("originals").toObject();
    if (originals.isEmpty()) return false;
    for (auto it = originals.begin(); it != originals.end(); ++it) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return false;
        const auto current = DownloadCheckpoint::fingerprint(it.key(), cancel);
        if (current.isEmpty() || current != it.value().toObject()) return false;
    }
    if (!intent.value("presets").toArray().isEmpty()) {
        const auto targets = intent.value("presetTargets").toObject();
        if (targets.isEmpty() || snapshotInitializationTargets(QDir(root).filePath("versions/" + instance), cancel) != targets) return false;
    }
    return !(cancel && cancel->load(std::memory_order_relaxed));
}
}
