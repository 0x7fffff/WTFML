#pragma once

#include "manage/InstanceFiles.hpp"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

// Rename-only crash recovery. Recovery never recursively deletes retained user data.
namespace RestoreJournal {
inline QString directory(const QString &gameDir) {
    return QDir(gameDir).filePath(QStringLiteral("cache/restore-transactions"));
}
inline bool ordinaryPath(const QString &root, const QString &path) {
    if (!QDir::isAbsolutePath(path) || !InstanceFiles::pathIsSameOrInside(root, path)) return false;
    QString current = QDir::cleanPath(path);
    const QString boundary = QDir::cleanPath(root);
    for (;;) {
        const QFileInfo info(current);
        if (info.isSymLink() || info.isJunction()) return false;
        if (current == boundary) return true;
        const QString parent = info.absolutePath();
        if (parent == current) return false;
        current = parent;
    }
}
inline bool write(const QString &path, const QJsonObject &record) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact);
    return bytes.size() <= 64 * 1024 && file.open(QIODevice::WriteOnly)
        && file.write(bytes) == bytes.size() && file.commit();
}
inline QJsonObject record(const QString &target, const QString &retained, bool hadOriginal) {
    return {{QStringLiteral("schema"), 1}, {QStringLiteral("target"), target},
        {QStringLiteral("retained"), retained}, {QStringLiteral("hadOriginal"), hadOriginal},
        {QStringLiteral("state"), QStringLiteral("prepared")}};
}
inline bool valid(const QString &root, const QJsonObject &json) {
    const QString target = json.value(QStringLiteral("target")).toString();
    const QString retained = json.value(QStringLiteral("retained")).toString();
    const QString state = json.value(QStringLiteral("state")).toString();
    const QString prefix = QStringLiteral(".wtfml-restore-previous-");
    const QString name = QFileInfo(retained).fileName();
    return json.value(QStringLiteral("schema")).toInt() == 1
        && json.value(QStringLiteral("hadOriginal")).isBool()
        && (state == QLatin1String("prepared") || state == QLatin1String("published"))
        && target == QDir::cleanPath(target) && retained == QDir::cleanPath(retained)
        && target != root && QFileInfo(target).absolutePath() == QFileInfo(retained).absolutePath()
        && !InstanceFiles::pathIsSameOrInside(target, directory(root))
        && !QFileInfo(target).fileName().startsWith(QStringLiteral(".wtfml-"))
        && name.startsWith(prefix) && !QUuid(name.mid(prefix.size())).isNull()
        && ordinaryPath(root, target) && ordinaryPath(root, retained);
}
} // namespace RestoreJournal
