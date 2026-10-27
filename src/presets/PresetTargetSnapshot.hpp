#pragma once
#include "download/DownloadCheckpoint.hpp"
#include <QDirIterator>

namespace Presets {
// Bind restart-time initialization consent to user settings, not to downloaded cores.
// Mods have a separate inventory/collision gate in PresetModInstaller.
inline QJsonObject snapshotInitializationTargets(const QString &root) {
    QJsonObject result;
    const QStringList names{"options.txt", "optionsof.txt", "servers.dat", "config", "defaultconfigs",
        "resourcepacks", "shaderpacks", "wtfml.datapacks", "datapacks-pending"};
    const auto remember = [&](const QString &path) {
        if (!DownloadCheckpoint::safePath(path)) return false;
        if (QFileInfo(path).isDir()) result.insert(path, QJsonObject{{"directory", true}});
        else {
            const auto state = DownloadCheckpoint::fingerprint(path);
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
}
