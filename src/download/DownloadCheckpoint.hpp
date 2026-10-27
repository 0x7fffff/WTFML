#pragma once

#include "auth/TokenStore.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QHash>
#include <QRegularExpression>
#include <QUrl>
#include <memory>

// A file-batch receipt, not a serialized Java installer. URLs can contain credentials.
// The original receipt survives malformed input, a busy lock, and failed atomic writes.
class DownloadCheckpoint {
public:
    struct File {
        QString url, path, sha1, official;
        qint64 size = -1;
        QJsonObject original;
    };
    QString path;
    QJsonObject context;
    QList<File> files;
    QHash<QString, int> indexes;
    bool dirty = false;
    std::unique_ptr<QLockFile> lock;
    static constexpr qint64 maximumBytes = 32 * 1024 * 1024;
    static constexpr int maximumFiles = 50000;

    static bool safePath(const QString &path) {
        if (!QDir::isAbsolutePath(path)) return false;
        for (QString p = path;;) {
            QFileInfo info(p);
            if (info.isSymLink() || info.isJunction()) return false;
            if (p != path && info.exists() && !info.isDir()) return false;
            const auto next = info.absolutePath();
            if (next == p) break;
            p = next;
        }
        return true;
    }
    static QJsonObject fingerprint(const QString &path) {
        QFileInfo info(path);
        if (!safePath(path)) return {};
        if (!info.exists()) return {{"exists", false}};
        QFile file(path);
        if (!info.isFile() || !file.open(QIODevice::ReadOnly)) return {};
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file)) return {};
        return {{"exists", true}, {"size", QString::number(info.size())},
                {"sha256", QString::fromLatin1(hash.result().toHex())}};
    }
    static bool unchanged(const File &entry) {
        const auto current = fingerprint(entry.path);
        if (current.isEmpty()) return false;
        if (current == entry.original) return true;
        // A previous run may have committed this exact declared artifact before dying.
        if (entry.sha1.isEmpty() || !current.value("exists").toBool()) return false;
        QFile file(entry.path);
        if (entry.size >= 0 && QFileInfo(file).size() != entry.size) return false;
        if (!file.open(QIODevice::ReadOnly)) return false;
        QCryptographicHash hash(QCryptographicHash::Sha1);
        return hash.addData(&file) && QString::fromLatin1(hash.result().toHex()).compare(entry.sha1, Qt::CaseInsensitive) == 0;
    }
    bool add(const File &entry) {
        const auto key = entry.path.toCaseFolded();
        if (indexes.contains(key)) {
            const auto &old = files.at(indexes.value(key));
            return old.url == entry.url && old.sha1 == entry.sha1 && old.size == entry.size;
        }
        if (files.size() >= maximumFiles || entry.original.isEmpty()) return false;
        indexes.insert(key, files.size());
        files.append(entry); dirty = true; return true;
    }
    const File *find(const QString &path) const {
        const auto it = indexes.constFind(QDir::cleanPath(path).toCaseFolded());
        return it == indexes.cend() ? nullptr : &files.at(it.value());
    }
    bool save() {
        if (!safePath(path)) return false;
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        if (!lock) {
            auto candidate = std::make_unique<QLockFile>(path + ".lock");
            candidate->setStaleLockTime(0);
            if (!candidate->tryLock(0)) return false;
            lock = std::move(candidate);
        }
        QJsonArray rows;
        for (const auto &f : files) rows.append(QJsonObject{{"url", f.url}, {"path", f.path},
            {"sha1", f.sha1}, {"size", QString::number(f.size)}, {"official", f.official}, {"original", f.original}});
        const auto plain = QJsonDocument(QJsonObject{{"format", 1}, {"context", context}, {"files", rows}}).toJson(QJsonDocument::Compact);
        if (plain.size() > maximumBytes) return false;
        const auto bytes = TokenStore::protect(plain);
#ifdef Q_OS_WIN
        if (!bytes.startsWith("WTFML-PROTECTED-1\n")) return false;
#endif
        QSaveFile output(path); output.setDirectWriteFallback(false);
        if (bytes.isEmpty() || bytes.size() > maximumBytes || !output.open(QIODevice::WriteOnly) ||
            output.write(bytes) != bytes.size() || !output.commit()) return false;
        dirty = false; return true;
    }
    bool load(const QString &source) {
        if (!safePath(source) || lock) return false;
        auto candidate = std::make_unique<QLockFile>(source + ".lock"); candidate->setStaleLockTime(0);
        if (!candidate->tryLock(0)) return false;
        QFile input(source);
        if (!input.open(QIODevice::ReadOnly) || input.size() > maximumBytes) return false;
        const auto bytes = input.readAll();
#ifdef Q_OS_WIN
        if (!bytes.startsWith("WTFML-PROTECTED-1\n")) return false;
#endif
        const auto document = QJsonDocument::fromJson(TokenStore::unprotect(bytes));
        const auto object = document.object();
        if (!document.isObject() || object.value("format") != 1 || !object.value("context").isObject() ||
            !object.value("files").isArray() || object.value("files").toArray().size() > maximumFiles) return false;
        QList<File> parsed;
        QSet<QString> paths;
        const auto validUrl = [](const QString &text) {
            QUrl url(text, QUrl::StrictMode);
            return url.isValid() && (url.scheme() == "http" || url.scheme() == "https") && !url.host().isEmpty() && url.userInfo().isEmpty();
        };
        for (const auto &value : object.value("files").toArray()) {
            if (!value.isObject()) return false;
            const auto row = value.toObject(); bool sizeOk = false;
            File f{row.value("url").toString(), row.value("path").toString(), row.value("sha1").toString(),
                row.value("official").toString(), row.value("size").toString().toLongLong(&sizeOk), row.value("original").toObject()};
            const auto key = QDir::cleanPath(f.path).toCaseFolded();
            if (!sizeOk || f.size < -1 || !validUrl(f.url) || (!f.official.isEmpty() && !validUrl(f.official)) ||
                !QDir::isAbsolutePath(f.path) || QDir::cleanPath(f.path) != f.path || paths.contains(key) ||
                !f.original.value("exists").isBool()) return false;
            if (f.original.value("exists").toBool() && (f.original.value("sha256").toString().size() != 64 ||
                !f.original.value("size").isString())) return false;
            if (!f.sha1.isEmpty() && f.sha1.size() != 40) return false;
            paths.insert(key); parsed.append(f);
        }
        path = source; context = object.value("context").toObject(); files = parsed; lock = std::move(candidate);
        indexes.clear();
        for (int i = 0; i < files.size(); ++i) indexes.insert(files.at(i).path.toCaseFolded(), i);
        return true;
    }
    void discard() { if (safePath(path)) QFile::remove(path); lock.reset(); }
};
