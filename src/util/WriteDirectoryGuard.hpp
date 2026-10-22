#pragma once

#include "util/DirectorySessionLock.hpp"
#include "util/AppPaths.hpp"
#include <QFileInfo>

// Entry-point guard for instance writes; use the same root key as product main,
// not a second lock on versions/<id> that another launcher would never see.
namespace WriteDirectoryGuard {
inline QString rootFor(const QString &instance) {
    if (instance.trimmed().isEmpty()) return {};
    const QString path = InstanceFiles::canonicalPath(instance);
    const QString current = InstanceFiles::canonicalPath(AppPaths::minecraftDir());
    // The selected root is authoritative, even if an instance/world happens to
    // be named "Servers" or "versions". Names alone must not override it.
    if (InstanceFiles::pathIsSameOrInside(current, path)) return current;
    const QString launcherRoot = InstanceFiles::canonicalPath(AppPaths::defaultMinecraftDir());
    if (InstanceFiles::pathIsSameOrInside(launcherRoot, path)) return launcherRoot;
    QString cursor = path;
    for (;;) {
        const QString parent = QFileInfo(cursor).absolutePath();
        const QString group = QFileInfo(parent).fileName();
        if (group.compare(QStringLiteral("versions"), Qt::CaseInsensitive) == 0
            || group.compare(QStringLiteral("Servers"), Qt::CaseInsensitive) == 0)
            return QFileInfo(parent).absolutePath();
        if (parent == cursor) break;
        cursor = parent;
    }
    return path;
}
inline bool acquire(const QString &instance, QString *problem = nullptr) {
    return DirectorySessionLock::acquire(rootFor(instance), problem);
}
} // namespace WriteDirectoryGuard
