#pragma once
#include "download/DownloadCheckpoint.hpp"
#include <QDirIterator>

namespace Presets {
// Stage files and the execution report are ours; only user mod files invalidate consent.
inline QJsonObject snapshotModTargets(const QString &root, const std::atomic_bool *cancel = nullptr) {
    if (cancel && cancel->load(std::memory_order_relaxed)) return {};
    const auto mods = QDir(root).filePath("mods");
    if (!DownloadCheckpoint::safePath(mods)) return {};
    QJsonObject result{{"exists", QFileInfo::exists(mods)}};
    if (!QFileInfo::exists(mods)) return result;
    if (!QFileInfo(mods).isDir()) return {};
    QDirIterator it(mods, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    int count = 0;
    while (it.hasNext()) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return {};
        const auto path = it.next();
        if (++count > 50000 || !DownloadCheckpoint::safePath(path)) return {};
        const auto info = it.fileInfo();
        const auto state = info.isDir() ? QJsonObject{{"directory", true}} : DownloadCheckpoint::fingerprint(path, cancel);
        if (state.isEmpty()) return {};
        result.insert(QDir(mods).relativeFilePath(path), state);
    }
    return result;
}
}
