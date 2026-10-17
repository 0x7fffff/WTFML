#pragma once

#include <QCryptographicHash>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QNetworkReply>

// 一个回复一个临时文件。校验之前不替换原文件；最多读取 64 KiB，不缓存整份下载。
class StreamedFile {
public:
    explicit StreamedFile(const QString &path, bool overwrite = true) : file(path), hash(QCryptographicHash::Sha1), allowOverwrite(overwrite) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) error = file.errorString();
    }
    void drain(QNetworkReply *reply) {
        if (!error.isEmpty()) return;
        // ★ 2026-10-17（用户桌面版的 log）：`abort()` 之后迟到的 `readyRead` / `finished`
        //   还会走到这儿，那时回复已经关掉了 —— `read()` 只会打一句
        //   `QIODevice::read (QNetworkReplyHttpImpl): device not open`，一个字节都读不到。
        //   `isReadable()` 判的就是"它还开着没有"，关着就当场返回。
        if (!reply->isReadable()) return;
        while (reply->bytesAvailable() > 0) {
            const QByteArray chunk = reply->read(64 * 1024);
            if (chunk.isEmpty()) break;
            if (prefix.size() < 4) prefix += chunk.left(4 - prefix.size());
            if (file.write(chunk) != chunk.size()) { error = file.errorString(); return; }
            hash.addData(chunk);
            bytes += chunk.size();
        }
    }
    QString finish(const QString &sha1, qint64 size, bool zip) {
        if (!error.isEmpty()) return error;
        if (bytes == 0) return QStringLiteral("Server returned an empty body");
        if (size > 0 && bytes != size) return QStringLiteral("Downloaded size does not match the expected size");
        if (!sha1.isEmpty() && QString::fromLatin1(hash.result().toHex()).compare(sha1, Qt::CaseInsensitive) != 0)
            return QStringLiteral("SHA1 verification failed");
        if (zip && sha1.isEmpty() && prefix != QByteArray("PK\x03\x04", 4) &&
            prefix != QByteArray("PK\x05\x06", 4) && prefix != QByteArray("PK\x07\x08", 4))
            return QStringLiteral("Not a zip archive (expected a jar/zip body)");
        if (!allowOverwrite && QFileInfo::exists(file.fileName())) return QStringLiteral("目标文件已存在，未覆盖。请更换文件名或确认覆盖。");
        if (!file.commit()) return file.errorString();
        return {};
    }
    QSaveFile file;
    QCryptographicHash hash;
    qint64 bytes = 0;
    QByteArray prefix;
    QString error;
    bool allowOverwrite;
};
