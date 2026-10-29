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
#include <atomic>

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
    // 盘上那一份的两个形状（见 encodeReceipt / load）：
    //   `WTFML-PROTECTED-1\n` + DPAPI(明文 JSON)  —— 老的那一档，只读不写；
    //   `WTFML-PROTECTED-2\n` + DPAPI(zlib 压缩过的 JSON) —— 现在写的这一档。
    // 前一档留着是因为**用户盘上那些暂停中的批次就是那个形状**，换了格式也得读得回来。
    static constexpr char plainMagic[] = "WTFML-PROTECTED-1\n";
    static constexpr char compressedMagic[] = "WTFML-PROTECTED-2\n";

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
    /// 把一份清单 JSON 编成盘上那一坨字节（加密 + 顺手压一遍）。
    ///
    /// 为什么压：这一层是**同步跑在 GUI 线程上**的，而 DPAPI 是按时长里最大的一块
    /// （实测 50,000 条 / 22 MB 那份：加密 1.05 秒，占整趟的八成；压缩本身只要 35 毫秒，
    /// 压完 1.3 MB 之后同一趟加密只剩 8～64 毫秒）。压缩的代价远小于它省下的加密时间 ——
    /// 见 `tests/core_modules/DownloadReceiptScaleTests.cpp` 的 `receipt-breakdown` 那几格。
    ///
    /// 压不小（很小的清单）就照老样子写明文那一档，绝不为了"格式统一"去写一份更大的。
    static QByteArray encodeReceipt(const QByteArray &plain) {
#ifdef Q_OS_WIN
        const auto squeezed = qCompress(plain, 1);
        if (squeezed.size() + 1024 < plain.size()) {
            const auto blob = TokenStore::protect(squeezed);
            const QByteArray protectPrefix(plainMagic, int(sizeof(plainMagic)) - 1);
            if (!blob.startsWith(protectPrefix)) return {};
            const QByteArray receiptPrefix(compressedMagic, int(sizeof(compressedMagic)) - 1);
            return receiptPrefix + blob.mid(protectPrefix.size());
        }
#endif
        return TokenStore::protect(plain);
    }
    static QJsonObject fingerprint(const QString &path, const std::atomic_bool *cancel = nullptr) {
        QFileInfo info(path);
        if (!safePath(path)) return {};
        if (!info.exists()) return {{"exists", false}};
        QFile file(path);
        if (!info.isFile() || !file.open(QIODevice::ReadOnly)) return {};
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!file.atEnd()) {
            if (cancel && cancel->load(std::memory_order_relaxed)) return {};
            const auto bytes = file.read(1024 * 1024);
            if (bytes.isEmpty() && file.error() != QFileDevice::NoError) return {};
            hash.addData(bytes);
        }
        if (cancel && cancel->load(std::memory_order_relaxed)) return {};
        return {{"exists", true}, {"size", QString::number(info.size())},
                {"sha256", QString::fromLatin1(hash.result().toHex())}};
    }
    static bool unchanged(const File &entry, const std::atomic_bool *cancel = nullptr, QJsonObject *verified = nullptr) {
        const auto current = fingerprint(entry.path, cancel);
        if (current.isEmpty()) return false;
        if (current == entry.original) { if (verified) *verified = current; return true; }
        // A previous run may have committed this exact declared artifact before dying.
        if (entry.sha1.isEmpty() || !current.value("exists").toBool()) return false;
        QFile file(entry.path);
        if (entry.size >= 0 && QFileInfo(file).size() != entry.size) return false;
        if (!file.open(QIODevice::ReadOnly)) return false;
        QCryptographicHash hash(QCryptographicHash::Sha1);
        while (!file.atEnd()) {
            if (cancel && cancel->load(std::memory_order_relaxed)) return false;
            const auto bytes = file.read(1024 * 1024);
            if (bytes.isEmpty() && file.error() != QFileDevice::NoError) return false;
            hash.addData(bytes);
        }
        const bool valid = !(cancel && cancel->load(std::memory_order_relaxed)) &&
            QString::fromLatin1(hash.result().toHex()).compare(entry.sha1, Qt::CaseInsensitive) == 0;
        if (valid && verified) *verified = current;
        return valid;
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
        const auto bytes = encodeReceipt(plain);
#ifdef Q_OS_WIN
        if (!bytes.startsWith(plainMagic) && !bytes.startsWith(compressedMagic)) return false;
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
        QByteArray decoded;
#ifdef Q_OS_WIN
        if (bytes.startsWith(compressedMagic)) {
            // 解不开（截断 / 投毒 / 不是这一台加密的）就是空的那一份，下面按"格式不对"拒掉 ——
            // **原件一个字节都不动**，用户那一份还是出事之后唯一能拿来对的东西。
            //
            // NOTE: 剥掉 `-2` 之后**得把 `-1` 那个魔术接回去**再交给 `unprotect` —— 它按那个
            // 魔术分"DPAPI 解"和"老混淆解"，不带魔术后缀它会走老那一条，解出来是一坨乱码。
            const QByteArray dpapi(plainMagic, int(sizeof(plainMagic)) - 1);
            const auto payload = TokenStore::unprotect(
                dpapi + bytes.mid(int(sizeof(compressedMagic)) - 1));
            // 压缩头那 4 个字节就是解压后的长度：**先拿它比一下 `maximumBytes` 再解** ——
            // 免得一份投毒/截断的记录让 `qUncompress` 去要一块天文数字的内存。
            const bool plausible = payload.size() >= 4
                && ((quint32(quint8(payload.at(0))) << 24) | (quint32(quint8(payload.at(1))) << 16)
                    | (quint32(quint8(payload.at(2))) << 8) | quint32(quint8(payload.at(3))))
                       <= quint32(maximumBytes);
            if (!plausible) return false;
            decoded = qUncompress(payload);
        } else if (bytes.startsWith(plainMagic)) {
            decoded = TokenStore::unprotect(bytes);  // 老那一档：盘上还留着的暂停批次
        } else {
            return false;
        }
#else
        decoded = TokenStore::unprotect(bytes);  // 非 Windows 走旧混淆，没有魔术
#endif
        const auto document = QJsonDocument::fromJson(decoded);
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
