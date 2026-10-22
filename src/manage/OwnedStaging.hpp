#pragma once

#include "manage/RestoreJournal.hpp"
#include "util/DirectorySessionLock.hpp"
#include <QDirIterator>
#include <QElapsedTimer>
#include <QLockFile>
#include <QFileInfo>
#include <algorithm>
#include <memory>

namespace OwnedStaging {
inline QString directory(const QString &root) {
    return QDir(root).filePath(QStringLiteral("cache/staging-records"));
}
inline QString marker(const QString &path) {
    return QDir(path).filePath(QStringLiteral(".wtfml-staging-owner.json"));
}
inline bool valid(const QString &root, const QJsonObject &record) {
    const QString path = record.value(QStringLiteral("path")).toString();
    const QString kind = record.value(QStringLiteral("kind")).toString();
    const QString prefix = kind == QLatin1String("backup") ? QStringLiteral(".wtfml-backup-")
                         : kind == QLatin1String("restore") ? QStringLiteral(".wtfml-restore-") : QString();
    const QString name = QFileInfo(path).fileName();
    const QString suffix = name.mid(prefix.size());
    return record.value(QStringLiteral("schema")).toInt() == 1
        && !QUuid(record.value(QStringLiteral("token")).toString()).isNull()
        && !prefix.isEmpty() && name.startsWith(prefix) && suffix.size() == 6
        && std::all_of(suffix.begin(), suffix.end(), [](QChar c) { return c.isLetterOrNumber() && c.unicode() < 128; })
        && path == QDir::cleanPath(path) && path != root
        && !InstanceFiles::pathIsSameOrInside(path, directory(root))
        && RestoreJournal::ordinaryPath(root, path);
}
inline QJsonObject read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// Declare BEFORE QTemporaryDir: the directory normally disappears first, then
// this lease removes its registry entry. A crash leaves both ownership records.
class Lease {
public:
    bool attach(const QString &root, const QString &path, const QString &kind) {
        if (!InstanceFiles::pathIsSameOrInside(root, path)) return true; // External: no automatic cleanup.
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject record{{QStringLiteral("schema"), 1}, {QStringLiteral("token"), token},
            {QStringLiteral("path"), path}, {QStringLiteral("kind"), kind}};
        const QString dir = directory(root);
        if (!valid(root, record) || !RestoreJournal::ordinaryPath(root, dir) || !QDir().mkpath(dir)) return false;
        m_path = path;
        m_record = QDir(dir).filePath(token + QStringLiteral(".json"));
        if (!RestoreJournal::ordinaryPath(root, m_record)
            || !RestoreJournal::ordinaryPath(root, m_record + QStringLiteral(".lock"))) return false;
        m_lock = std::make_unique<QLockFile>(m_record + QStringLiteral(".lock"));
        m_lock->setStaleLockTime(0);
        m_registered = m_lock->tryLock(0) && RestoreJournal::write(marker(path), record)
            && RestoreJournal::write(m_record, record);
        return m_registered;
    }
    ~Lease() {
        if (m_registered && !QFileInfo::exists(m_path)) QFile::remove(m_record);
    }
private:
    QString m_path;
    QString m_record;
    std::unique_ptr<QLockFile> m_lock;
    bool m_registered = false;
};

// Bounded, incremental cleanup: no recursive remove command and no link following.
// Marker is removed last, so interrupted/partial cleanup remains attributable.
inline QStringList cleanup(const QString &gameDir) {
    QStringList messages;
    const QString root = InstanceFiles::canonicalPath(gameDir);
    QString problem;
    if (root.isEmpty() || !QFileInfo(root).isDir()) return messages;
    if (!DirectorySessionLock::acquire(root, &problem)) return {problem};
    const QString dirPath = directory(root);
    if (!RestoreJournal::ordinaryPath(root, dirPath)) return messages;
    QElapsedTimer elapsed; elapsed.start();
    const QDir dir(dirPath);
    int operations = 0;
    const auto records = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const auto &name : records.mid(0, 128)) {
        if (elapsed.elapsed() >= 1000 || operations >= 512) break;
        const QString path = dir.filePath(name);
        if (!RestoreJournal::ordinaryPath(root, path)
            || !RestoreJournal::ordinaryPath(root, path + QStringLiteral(".lock"))) continue;
        const auto record = read(path);
        if (!valid(root, record) || name != record.value(QStringLiteral("token")).toString() + QStringLiteral(".json")) continue;
        QLockFile lock(path + QStringLiteral(".lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) continue;
        const QString stage = record.value(QStringLiteral("path")).toString();
        if (!QFileInfo::exists(stage)) { QFile::remove(path); continue; }
        if (!QFileInfo(stage).isDir() || !RestoreJournal::ordinaryPath(root, marker(stage))
            || read(marker(stage)) != record) continue;
        QStringList directories;
        QDirIterator iterator(stage, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories); // No FollowSymlinks flag.
        while (iterator.hasNext() && elapsed.elapsed() < 1000 && operations < 512) {
            const QString entry = iterator.next();
            const QFileInfo info = iterator.fileInfo();
            ++operations;
            if (entry == marker(stage) || info.isSymLink() || info.isJunction()) continue;
            if (info.isDir()) directories.append(entry);
            else if (info.isFile() && RestoreJournal::ordinaryPath(root, entry)) QFile::remove(entry);
        }
        std::sort(directories.begin(), directories.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
        for (const auto &entry : directories) {
            if (elapsed.elapsed() >= 1000) break;
            if (RestoreJournal::ordinaryPath(root, entry)) QDir().rmdir(entry);
        }
        const auto remaining = QDir(stage).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        if (remaining == QStringList{QFileInfo(marker(stage)).fileName()} && QFile::remove(marker(stage))) {
            if (QDir().rmdir(stage)) {
                QFile::remove(path);
                messages.append(QStringLiteral("已清理确认归属的崩溃暂存：%1").arg(stage));
            } else RestoreJournal::write(marker(stage), record);
        }
    }
    return messages;
}
} // namespace OwnedStaging
