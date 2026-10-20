#pragma once
#include <QList>
#include <QString>
#include <atomic>
#include "download/ResourceCatalog.hpp"

namespace Presets {
struct InstalledModFile {
    QString path;
    QString sha1;
    QString projectKey;
    QString versionId;
    ResourceCatalog::VersionEntry version;
};
struct InstalledModInventory {
    QList<InstalledModFile> files;
    QString problem;
};
// Enabled, regular JARs only. No network, recursive scan, filename guessing or writes.
// Previous operation records supply identity only after matching actual file contents.
InstalledModInventory scanInstalledMods(const QString &root, const std::atomic_bool *cancel);
bool sameInstalledMods(const InstalledModInventory &left, const InstalledModInventory &right);
}
