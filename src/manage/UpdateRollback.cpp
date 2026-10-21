/**
 * @file UpdateRollback.cpp
 * @brief 实现更新前的那个轻量回滚点（形状与取舍见 .hpp）。
 */

#include "manage/UpdateRollback.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QObject>
#include <QSaveFile>

#include "manage/FolderManager.hpp"
#include "util/AppPaths.hpp"

namespace UpdateRollback {

namespace {

constexpr int kVersion = 1;

const QString kVersionField = QStringLiteral("version");
const QString kInstanceField = QStringLiteral("instance");
const QString kCreatedField = QStringLiteral("createdAt");
const QString kEntriesField = QStringLiteral("entries");
const QString kPathField = QStringLiteral("path");
const QString kExistedField = QStringLiteral("existed");
const QString kSizeField = QStringLiteral("size");
const QString kSha1Field = QStringLiteral("sha1");

/// 一份文件的 sha1（小写十六进制）。读不动时回空串 —— **调用方必须把空串当失败**。
///
/// NOTE: 和 `BackupFiles.cpp` 里那个同名的小函数是同一段代码（那一边也是"本地一份"）。
/// 两份**同一个算法**（整文件流式）是这里唯一的硬要求：份数不是问题，算法分叉才是。
QString sha1OfFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file)) {
        return QString();
    }
    return QString::fromLatin1(hash.result().toHex());
}

/// 实例 id 能不能当目录名用（Windows 不认 `\/:*?"<>|` 和控制字符）。和
/// `BackupFiles.cpp` 里那个 `safeLabel()` 同一套字。
QString safeSegment(const QString &raw) {
    QString out;
    out.reserve(raw.size());
    for (const QChar ch : raw) {
        const bool bad = ch == QLatin1Char('/') || ch == QLatin1Char('\\') || ch == QLatin1Char(':')
                         || ch == QLatin1Char('*') || ch == QLatin1Char('?') || ch == QLatin1Char('"')
                         || ch == QLatin1Char('<') || ch == QLatin1Char('>') || ch == QLatin1Char('|')
                         || ch < QChar(0x20);
        out.append(bad ? QLatin1Char('-') : ch);
    }
    out = out.trimmed();
    while (!out.isEmpty() && (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))) {
        out.chop(1);
    }
    if (out.isEmpty()) {
        out = QStringLiteral("instance");
    }
    if (out.size() > 60) {
        out = out.left(60);
    }
    // 末尾再挂一段**原来的 id 的哈希**：清洗是**多对一**的（`a/b` 和 `a\b` 都变成 `a-b`），
    // 而两个实例共用一份回滚点的后果是"还原的时候把 A 的旧文件写进 B 里"。哈希那一段让
    // 清洗之后仍然**一一对应**，代价是目录名里多八个字符。
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(raw.toUtf8());
    return out + QLatin1Char('-') + QString::fromLatin1(hash.result().toHex().left(8));
}

QString filesDirOf(const QString &snapshotDirPath) {
    return snapshotDirPath + QStringLiteral("/files");
}

/// 这个相对路径能不能用（**不是**问"它在不在"，是问"它会不会跑到实例目录外面去"）。
/// 空、绝对路径、`..`、反斜杠、盘符一律不行 —— 这些路径是我们自己走目录走出来的，
/// 真出现一个，那是上面那一层算错了。
bool isSafeRelativePath(const QString &path) {
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.endsWith(QLatin1Char('/'))) {
        return false;
    }
    if (path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':'))) {
        return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QStringLiteral("..") || part == QStringLiteral(".")) {
            return false;
        }
    }
    return true;
}

}  // namespace

QString rootDir() { return AppPaths::minecraftDir() + QStringLiteral("/cache/update-rollback"); }

QString snapshotDir(const QString &instanceId) {
    return rootDir() + QLatin1Char('/') + safeSegment(instanceId);
}

QString manifestName() { return QStringLiteral("wtfml-rollback.json"); }

QString formatTag() { return QStringLiteral("wtfml-update-rollback"); }

QByteArray manifestToJson(const Snapshot &snapshot) {
    QJsonObject root;
    root.insert(QStringLiteral("format"), formatTag());
    root.insert(kVersionField, kVersion);
    root.insert(kInstanceField, snapshot.instanceId);
    root.insert(kCreatedField, snapshot.createdAt.toString(Qt::ISODate));
    QJsonArray entries;
    for (const Entry &entry : snapshot.entries) {
        QJsonObject one;
        one.insert(kPathField, entry.relativePath);
        one.insert(kExistedField, entry.existed);
        if (entry.existed) {
            one.insert(kSizeField, double(entry.size));
            one.insert(kSha1Field, entry.sha1);
        }
        entries.append(one);
    }
    root.insert(kEntriesField, entries);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

Snapshot manifestFromJson(const QByteArray &bytes, bool *ok) {
    if (ok != nullptr) {
        *ok = false;
    }
    Snapshot snapshot;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return snapshot;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != formatTag()
        || root.value(kVersionField).toInt(-1) != kVersion) {
        return snapshot;  // 不是我们写的那种（不认识就一句话都不信）
    }
    snapshot.instanceId = root.value(kInstanceField).toString();
    snapshot.createdAt = QDateTime::fromString(root.value(kCreatedField).toString(), Qt::ISODate);
    const QJsonArray entries = root.value(kEntriesField).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject one = value.toObject();
        Entry entry;
        entry.relativePath = one.value(kPathField).toString();
        entry.existed = one.value(kExistedField).toBool();
        entry.size = qint64(one.value(kSizeField).toDouble(0));
        entry.sha1 = one.value(kSha1Field).toString();
        if (!isSafeRelativePath(entry.relativePath)) {
            return Snapshot{};  // 有一条不能用 = 整份不能用（见 .hpp 那段）
        }
        snapshot.entries.append(entry);
    }
    if (snapshot.instanceId.isEmpty() || snapshot.entries.isEmpty()) {
        return Snapshot{};
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return snapshot;
}

Snapshot create(const QString &instanceDir, const QString &instanceId,
                const QStringList &relativePaths, QString *problemOut, bool keepPrevious) {
    if (problemOut != nullptr) {
        problemOut->clear();
    }
    Snapshot snapshot;
    if (instanceId.isEmpty()) {
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("不知道该给哪个实例做回滚点。");
        }
        return snapshot;
    }
    if (!QFileInfo(instanceDir).isDir()) {
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("找不到实例目录：%1")
                              .arg(QDir::toNativeSeparators(instanceDir));
        }
        return snapshot;
    }
    // 去重（同一条路径来两次 = 抄两遍、清单里两条），并挡掉不合法的。
    QStringList wanted;
    for (const QString &path : relativePaths) {
        if (!isSafeRelativePath(path)) {
            if (problemOut != nullptr) {
                *problemOut = QObject::tr("有一个文件不在实例目录里（%1），没有动手。").arg(path);
            }
            return Snapshot{};
        }
        if (!wanted.contains(path)) {
            wanted.append(path);
        }
    }
    if (wanted.isEmpty()) {
        // 没有要碰的文件 = 这一趟没有回滚点这回事（调用方不该走到这儿，但别把空目录留下）。
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("这一趟没有要动的文件。");
        }
        return snapshot;
    }

    const QString dir = snapshotDir(instanceId);
    // 从前这里是一句"先清掉上一趟那一份"：清完再抄新的。抄新的失败（盘满、文件被占着），
    // 旧的回滚点已经没了；抄成了但调用方那一批被拒（下载器正忙 / 另一套安装状态机占着 /
    // 接口给的文件名不能用），调用方 discard() 一撤，旧的同样没了 —— 两次都是"一个字节
    // 都没动，用户却失去了『撤销上一次更新』"。
    // 现在：旧的改名成 `<dir>.previous` 先留着；摆新的失败 → 改名回来；这一批被拒
    // （discard）→ 改名回来；只有这一批真开起来了，调用方才 commit() 把它删掉。
    const QString previous = dir + QStringLiteral(".previous");
    // An interrupted preparation has not accepted a new batch. Restore the retained
    // snapshot first, including when a partial new directory was already created.
    if (keepPrevious && QFileInfo::exists(previous)) {
        QFile retained(previous + QLatin1Char('/') + manifestName());
        bool valid = false;
        Snapshot retainedSnapshot;
        if (retained.open(QIODevice::ReadOnly) && retained.size() <= 16 * 1024 * 1024)
            retainedSnapshot = manifestFromJson(retained.readAll(), &valid);
        retained.close();
        if (!valid || retainedSnapshot.instanceId != instanceId) {
            if (problemOut) *problemOut = QObject::tr("中断前的回滚清单损坏，已保留两份目录供检查。");
            return Snapshot{};
        }
        if ((QFileInfo::exists(dir) && !removeTreeWithoutFollowingLinks(dir))
            || !QDir().rename(previous, dir)) {
            if (problemOut) *problemOut = QObject::tr("无法恢复中断前的回滚点，未创建新快照。");
            return Snapshot{};
        }
    }
    const auto restorePrevious = [&] {
        removeTreeWithoutFollowingLinks(dir);  // 这一趟抄的半份不要
        if (keepPrevious && QFileInfo::exists(previous)) {
            QDir().rename(previous, dir);
        }
    };
    if (keepPrevious) {
        if (QFileInfo::exists(previous) && !removeTreeWithoutFollowingLinks(previous)) {
            if (problemOut != nullptr) {
                *problemOut = QObject::tr("清不掉上一次留下的旧回滚点（%1），它可能正被别的程序占着。")
                                  .arg(QDir::toNativeSeparators(previous));
            }
            return Snapshot{};
        }
        if (QFileInfo::exists(dir) && !QDir().rename(dir, previous)) {
            if (problemOut != nullptr) {
                *problemOut = QObject::tr("没能把上一次的回滚点先收起来（%1）。")
                                  .arg(QDir::toNativeSeparators(dir));
            }
            return Snapshot{};
        }
    }
    // **先清掉上一趟那一份**（见 .hpp 里"为什么只留最新那一份"那段）。清不动就当场
    // 停：留着上一趟的东西混进这一趟，还原的时候会把上一次的文件写回来。
    if (QFileInfo::exists(dir) && !removeTreeWithoutFollowingLinks(dir)) {
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("清不掉上一次的回滚点（%1），它可能正被别的程序占着。")
                              .arg(QDir::toNativeSeparators(dir));
        }
        if (keepPrevious && QFileInfo::exists(previous)) {
            QDir().rename(previous, dir);
        }
        return Snapshot{};
    }
    if (!QDir().mkpath(filesDirOf(dir))) {
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("建不了回滚点目录（%1）。").arg(QDir::toNativeSeparators(dir));
        }
        restorePrevious();
        return Snapshot{};
    }

    snapshot.instanceId = instanceId;
    snapshot.createdAt = QDateTime::currentDateTime();
    snapshot.dir = dir;

    for (const QString &relative : wanted) {
        const QString source = instanceDir + QLatin1Char('/') + relative;
        Entry entry;
        entry.relativePath = relative;
        const QFileInfo info(source);
        if (info.isFile()) {
            const QString target = filesDirOf(dir) + QLatin1Char('/') + relative;
            if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
                if (problemOut != nullptr) {
                    *problemOut = QObject::tr("建不了回滚点里的目录（%1）。")
                                      .arg(QDir::toNativeSeparators(QFileInfo(target).absolutePath()));
                }
                removeTreeWithoutFollowingLinks(dir);  // 半份也不要留
                if (keepPrevious && QFileInfo::exists(previous)) QDir().rename(previous, dir);
                return Snapshot{};
            }
            if (!QFile::copy(source, target)) {
                if (problemOut != nullptr) {
                    *problemOut = QObject::tr("抄不动「%1」（多半被别的程序占着，或者盘满了）。")
                                      .arg(QDir::toNativeSeparators(source));
                }
                restorePrevious();
                return Snapshot{};
            }
            entry.existed = true;
            entry.size = info.size();
            entry.sha1 = sha1OfFile(source);
            // 抄下来这一份自己读不动 = 这一份回滚点靠不住（"能退回来"这句话要能兑现）。
            if (entry.sha1.isEmpty()) {
                if (problemOut != nullptr) {
                    *problemOut = QObject::tr("抄下来的「%1」读不回来，这一份回滚点不敢用。")
                                      .arg(QDir::toNativeSeparators(source));
                }
                restorePrevious();
                return Snapshot{};
            }
        } else {
            // 现在盘上没有它 —— 更新多半会**添**上它，退回来就是把它删掉（见 .hpp 那段）。
            entry.existed = false;
        }
        snapshot.entries.append(entry);
    }

    // 清单最后写（**它在了才算这一份真的成了**：读的时候没有清单就当没有这一份）。
    QFile manifest(dir + QLatin1Char('/') + manifestName());
    if (!manifest.open(QIODevice::WriteOnly)) {
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("回滚点的清单写不出去（%1）。")
                              .arg(QDir::toNativeSeparators(manifest.fileName()));
        }
        restorePrevious();
        return Snapshot{};
    }
    const QByteArray bytes = manifestToJson(snapshot);
    const bool written = manifest.write(bytes) == bytes.size();
    manifest.close();
    if (!written) {
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("回滚点的清单没写完（%1）。")
                              .arg(QDir::toNativeSeparators(manifest.fileName()));
        }
        restorePrevious();
        return Snapshot{};
    }
    return snapshot;
}

Snapshot read(const QString &instanceId, bool *ok, QString *problemOut) {
    if (ok != nullptr) {
        *ok = false;
    }
    if (problemOut != nullptr) {
        problemOut->clear();
    }
    QString dir = snapshotDir(instanceId);
    // Crash between retaining the previous snapshot and publishing the new manifest:
    // keep rollback available immediately, not only after the next update attempt.
    if (!QFileInfo::exists(dir + QLatin1Char('/') + manifestName())
        && QFileInfo::exists(dir + QStringLiteral(".previous/") + manifestName()))
        dir += QStringLiteral(".previous");
    QFile manifest(dir + QLatin1Char('/') + manifestName());
    if (!manifest.exists()) {
        return Snapshot{};  // 没有那一份 —— **不是错误**，从来没更新过就是这样
    }
    if (!manifest.open(QIODevice::ReadOnly)) {
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("读不动回滚点的清单：%1").arg(manifest.errorString());
        }
        return Snapshot{};
    }
    const QByteArray bytes = manifest.readAll();
    manifest.close();
    bool parsed = false;
    Snapshot snapshot = manifestFromJson(bytes, &parsed);
    if (!parsed) {
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("回滚点的清单读不懂（%1），这一份不敢拿去还原。")
                              .arg(QDir::toNativeSeparators(manifest.fileName()));
        }
        return Snapshot{};
    }
    snapshot.dir = dir;
    if (ok != nullptr) {
        *ok = true;
    }
    return snapshot;
}

bool discard(const QString &instanceId) {
    const QString dir = snapshotDir(instanceId);
    const QString previous = dir + QStringLiteral(".previous");
    if (QFileInfo::exists(previous)) {
        if (QFileInfo::exists(dir) && !removeTreeWithoutFollowingLinks(dir)) {
            return false;
        }
        return QDir().rename(previous, dir);
    }
    if (!QFileInfo::exists(dir)) {
        return true;
    }
    return removeTreeWithoutFollowingLinks(dir);
}

bool commit(const QString &instanceId) {
    const QString previous = snapshotDir(instanceId) + QStringLiteral(".previous");
    if (!QFileInfo::exists(previous)) {
        return true;
    }
    return removeTreeWithoutFollowingLinks(previous);
}

RestoreResult restore(const Snapshot &snapshot, const QString &instanceDir) {
    RestoreResult result;
    if (!snapshot.isValid() || !QFileInfo(instanceDir).isDir()) {
        result.failed = 1;
        result.problems.append(QObject::tr("这一份回滚点用不了（清单不全或者实例目录没了）。"));
        return result;
    }
    const auto note = [&result](const QString &text) {
        if (!result.problems.contains(text)) {
            result.problems.append(text);
        }
    };

    for (const Entry &entry : snapshot.entries) {
        const QString target = instanceDir + QLatin1Char('/') + entry.relativePath;
        if (!entry.existed) {
            // 更新**添**上去的那一份：退回去就是让它消失。
            if (QFileInfo::exists(target)) {
                if (QFile::remove(target)) {
                    ++result.removed;
                } else {
                    ++result.failed;
                    note(QObject::tr("删不掉「%1」（多半被占着）。").arg(entry.relativePath));
                }
            }
            continue;
        }
        const QString source = filesDirOf(snapshot.dir) + QLatin1Char('/') + entry.relativePath;
        if (!QFileInfo(source).isFile()) {
            ++result.failed;
            note(QObject::tr("回滚点里少了「%1」。").arg(entry.relativePath));
            continue;
        }
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            ++result.failed;
            note(QObject::tr("建不了「%1」的目录。").arg(entry.relativePath));
            continue;
        }
        // QSaveFile 在目标旁写临时文件并原子替换。不能先删 target 再 rename：后一动作失败
        // 时，用户当前文件和回滚文件会同时丢失。
        QFile in(source);
        QSaveFile out(target);
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
            ++result.failed;
            note(QObject::tr("抄不回来「%1」。").arg(entry.relativePath));
            continue;
        }
        bool copied = true;
        QByteArray buffer(256 * 1024, Qt::Uninitialized);
        while (!in.atEnd()) {
            const qint64 got = in.read(buffer.data(), buffer.size());
            if (got <= 0 || out.write(buffer.constData(), got) != got) {
                copied = false;
                break;
            }
        }
        if (!copied || !out.commit()) {
            ++result.failed;
            note(QObject::tr("换不上「%1」。").arg(entry.relativePath));
            continue;
        }
        // 放回去的**对不对**：照清单里记的那个 sha1 对一遍（文件在那儿 ≠ 文件是对的）。
        if (!entry.sha1.isEmpty() && sha1OfFile(target) != entry.sha1) {
            ++result.failed;
            note(QObject::tr("放回去的「%1」和记下来的对不上。").arg(entry.relativePath));
            continue;
        }
        ++result.restored;
    }
    return result;
}

}  // namespace UpdateRollback
