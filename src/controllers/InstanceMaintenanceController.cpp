/**
 * @file InstanceMaintenanceController.cpp
 * @brief 实现实例配置与世界 level.dat 的轻量备份。
 * @author Agent
 * @date 2026-10
 */
#include "controllers/InstanceMaintenanceController.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <functional>

#include "manage/FolderManager.hpp"
#include "manage/WorldFiles.hpp"
#include "ConfigFileText.hpp"
#include "util/AppPaths.hpp"

QStringList InstanceMaintenanceController::configBackups(const QString &instanceId,
                                                         const QString &configDir,
                                                         const QString &path) {
    QStringList backups;
    if (instanceId.isEmpty() || instanceId == QStringLiteral(".") || instanceId == QStringLiteral("..")
        || instanceId.contains(QLatin1Char('/')) || instanceId.contains(QLatin1Char('\\'))) {
        return backups;
    }
    const QString root = AppPaths::configBackupDir();
    const QStringList names =
        QDir(root + QLatin1Char('/') + instanceId).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QStringList candidates;
    for (const QString &name : names) {
        if (ConfigFileText::isBackupFolderName(name)) {
            candidates.append(name);
        }
    }
    std::sort(candidates.begin(), candidates.end(), std::greater<QString>());
    for (const QString &name : candidates) {
        const QString candidate =
            ConfigFileText::backupPathFor(root, instanceId, configDir, path, name);
        const QFileInfo file(candidate);
        if (!file.isFile() || file.isSymLink()) continue;
        const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
        const QString canonicalFile = file.canonicalFilePath();
        if (canonicalRoot.isEmpty() || canonicalFile.isEmpty()) continue;
        const QString generationRoot = QDir(canonicalRoot).filePath(instanceId + QLatin1Char('/') + name);
        const QString relative = QDir(generationRoot).relativeFilePath(canonicalFile);
        if (QDir::isAbsolutePath(relative) || relative == QStringLiteral("..")
            || relative.startsWith(QStringLiteral("../"))) continue;
        backups.append(candidate);
    }
    return backups;
}

QString InstanceMaintenanceController::latestConfigBackup(const QString &instanceId,
                                                           const QString &configDir,
                                                           const QString &path) {
    return configBackups(instanceId, configDir, path).value(0);
}

void InstanceMaintenanceController::pruneConfigBackups(const QString &instanceId, int keep) {
    if (instanceId.isEmpty()) {
        return;
    }
    const QString dir = AppPaths::configBackupDir() + QLatin1Char('/') + instanceId;
    const QStringList names = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : ConfigFileText::backupFoldersToRemove(names, keep)) {
        removeTreeWithoutFollowingLinks(dir + QLatin1Char('/') + name);
    }
}

bool InstanceMaintenanceController::backupWorldLevelData(const QString &worldDir, int keep,
                                                         QString *problemOut) {
    if (problemOut != nullptr) {
        problemOut->clear();
    }
    const auto fail = [problemOut](const QString &why) {
        if (problemOut != nullptr) {
            *problemOut = why;
        }
        return false;
    };
    const QString path = WorldFiles::levelDataPath(worldDir);
    if (!QFileInfo::exists(path)) {
        return fail(QStringLiteral("这个存档里没有 level.dat。"));
    }

    const QString worldName = QFileInfo(worldDir).fileName();
    const QString root = AppPaths::worldBackupDir();
    const QString base = root + QLatin1Char('/') + worldName;
    const QStringList existing = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const QString folder = ConfigFileText::backupFolderName(QDateTime::currentDateTime(), existing);
    const QString target = ConfigFileText::backupPathFor(root, worldName, worldDir, path, folder);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        return fail(QStringLiteral("建不出备份目录：%1").arg(QDir::toNativeSeparators(target)));
    }
    if (!QFile::copy(path, target)) {
        return fail(QStringLiteral("没能把 level.dat 抄到 %1（磁盘满了？还是那个文件打不开？）")
                        .arg(QDir::toNativeSeparators(target)));
    }

    const QStringList names = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : ConfigFileText::backupFoldersToRemove(names, keep)) {
        removeTreeWithoutFollowingLinks(base + QLatin1Char('/') + name);
    }
    return true;
}

QString InstanceMaintenanceController::latestWorldBackup(const QString &worldDir) {
    const QString dir =
        AppPaths::worldBackupDir() + QLatin1Char('/') + QFileInfo(worldDir).fileName();
    QStringList candidates;
    for (const QString &name : QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (ConfigFileText::isBackupFolderName(name)) {
            candidates.append(name);
        }
    }
    std::sort(candidates.begin(), candidates.end(), std::greater<QString>());
    for (const QString &name : candidates) {
        const QString candidate = dir + QLatin1Char('/') + name + QStringLiteral("/level.dat");
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}
