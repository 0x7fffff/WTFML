#include "ServerStatusCache.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "util/AppPaths.hpp"

namespace {

/// 盘上那几个键（读和写各只出现一次，见下面两个函数）。
const char *kMotdKey = "motd";
const char *kIconKey = "icon";
const char *kLatencyKey = "latencyMs";
const char *kOnlineKey = "online";
const char *kPlayersOnlineKey = "playersOnline";
const char *kMaxKey = "playersMax";

}  // namespace

namespace ServerStatusCache {

QString cachePath(const QString &address) {
    const QString trimmed = address.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    // 文件名用**地址的 sha1**（`:` `/` `[` 那些字符在文件名里能不能用要按平台问，
    // 不值得；而 sha1 是定长十六进制，谁也不认识谁 —— 这一点无所谓，缓存是按内容
    // 反查的，不是给人看的）。
    const QByteArray digest =
        QCryptographicHash::hash(trimmed.toUtf8(), QCryptographicHash::Sha1).toHex();
    return AppPaths::serverCacheDir() + QLatin1Char('/') + QString::fromLatin1(digest) +
           QStringLiteral(".json");
}

Entry load(const QString &address) {
    Entry entry;
    const QString path = cachePath(address);
    if (path.isEmpty()) {
        return entry;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return entry;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError error {};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        // 写坏了的缓存（断电、手改）当"没有" —— **不删它**：删文件的动作留给验证工程和
        // 用户，读这一路上一个字节都不写。
        return entry;
    }
    const QJsonObject root = document.object();
    entry.motdRaw = root.value(QLatin1String(kMotdKey)).toString();
    entry.iconPng =
        QByteArray::fromBase64(root.value(QLatin1String(kIconKey)).toString().toLatin1());
    entry.online = root.value(QLatin1String(kOnlineKey)).toBool(false);
    entry.latencyMs = root.value(QLatin1String(kLatencyKey)).toInt(-1);
    entry.playersOnline = root.value(QLatin1String(kPlayersOnlineKey)).toInt(-1);
    entry.playersMax = root.value(QLatin1String(kMaxKey)).toInt(-1);
    entry.valid = true;
    return entry;
}

void save(const QString &address, const Entry &entry) {
    const QString path = cachePath(address);
    if (path.isEmpty()) {
        return;
    }
    const QString dir = AppPaths::serverCacheDir();
    if (!QDir().mkpath(dir)) {
        return;  // 目录建不出来：当没这回事（见声明处那条 NOTE）。
    }

    QJsonObject root;
    root.insert(QLatin1String(kMotdKey), entry.motdRaw);
    // 图标是**原始 PNG 字节**，JSON 里放不了二进制 ⇒ base64。它是几 KB 的一张小图，
    // 塞进同一个文件比另开一个 `.png` 省一次"两个文件对不上"的机会。
    root.insert(QLatin1String(kIconKey), QString::fromLatin1(entry.iconPng.toBase64()));
    root.insert(QLatin1String(kOnlineKey), entry.online);
    root.insert(QLatin1String(kLatencyKey), entry.latencyMs);
    root.insert(QLatin1String(kPlayersOnlineKey), entry.playersOnline);
    root.insert(QLatin1String(kMaxKey), entry.playersMax);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
}

}  // namespace ServerStatusCache
