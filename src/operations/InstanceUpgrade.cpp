#include "InstanceUpgrade.hpp"

#include "manage/BackupFiles.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/FolderManager.hpp"
#include "util/ZipUtil.hpp"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include "util/DirectorySessionLock.hpp"
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <limits>

namespace InstanceUpgrade {
namespace {
QString originalDir(const Request &r) { return QDir(r.gameDir).filePath("versions/" + r.instanceId); }
QString journalPath(const Request &r) { return transactionDir(r) + "/journal.json"; }
bool redirected(const QString &path) {
    const QFileInfo f(path);
    return f.isSymLink() || f.isJunction();
}
QString validate(const Request &r) {
    if (!QDir::isAbsolutePath(r.gameDir) || !QFileInfo(r.gameDir).isDir()
        || !InstanceFiles::validateDirName(r.instanceId).isEmpty()
        || !QRegularExpression("^[A-Za-z0-9-]{1,80}$").match(r.transactionId).hasMatch())
        return QStringLiteral("升级事务的游戏目录、实例名或事务编号无效。");
    const QStringList paths{r.gameDir, QDir(r.gameDir).filePath("versions"),
        QDir(r.gameDir).filePath("cache"), QDir(r.gameDir).filePath("cache/instance-upgrades"),
        QDir(r.gameDir).filePath("cache/backups"), QDir(r.gameDir).filePath("cache/backups/instances"),
        transactionDir(r), transactionDir(r) + "/prepared", transactionDir(r) + "/prepared/versions",
        originalDir(r), candidateDir(r), transactionDir(r) + "/original",
        transactionDir(r) + "/replaced", journalPath(r)};
    for (const auto &path : paths)
        if (redirected(path)) return QStringLiteral("升级路径包含链接，无法保证完整快照和安全回滚：%1").arg(path);
    return {};
}
// Full snapshots must not silently omit junctions, symlinks or unreadable trees.
QString checkTree(const QString &root) {
    QList<QString> stack{root};
    while (!stack.isEmpty()) {
        const QString path = stack.takeLast();
        const QFileInfo info(path);
        if (redirected(path) || !info.isReadable())
            return QStringLiteral("目录含链接或不可读内容，未执行原地升级：%1").arg(path);
        if (info.isDir()) {
            const auto entries = QDir(path).entryInfoList(QDir::AllEntries | QDir::Hidden
                | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
            for (const auto &entry : entries) stack.append(entry.absoluteFilePath());
        } else if (!info.isFile()) {
            return QStringLiteral("不是普通文件，无法制作完整快照：%1").arg(path);
        }
    }
    return {};
}
QJsonObject readJournal(const Request &r) {
    QFile f(journalPath(r));
    if (!f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024) return {};
    const auto json = QJsonDocument::fromJson(f.readAll()).object();
    if (json.value("schema").toInt() != 1 || json.value("instanceId").toString() != r.instanceId
        || json.value("transactionId").toString() != r.transactionId
        || QDir::cleanPath(json.value("gameDir").toString()) != QDir::cleanPath(r.gameDir)) return {};
    return json;
}
bool writeJournal(const Request &r, const QString &state, const QString &snapshot) {
    const QJsonObject json{{"schema", 1}, {"instanceId", r.instanceId},
        {"transactionId", r.transactionId}, {"gameDir", QDir::cleanPath(r.gameDir)},
        {"state", state}, {"snapshotZip", snapshot},
        {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    QSaveFile f(journalPath(r));
    const auto bytes = QJsonDocument(json).toJson();
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}
bool sameFiles(const QString &root, const QList<BackupFiles::FileRecord> &expected,
               const std::function<bool()> &cancelled = {}) {
    QString problem;
    const auto actual = BackupFiles::collectFiles(root, {}, &problem, cancelled);
    if (!problem.isEmpty() || actual.size() != expected.size()) return false;
    for (qsizetype i = 0; i < actual.size(); ++i)
        if (actual[i].path != expected[i].path || actual[i].size != expected[i].size
            || actual[i].sha1 != expected[i].sha1) return false;
    return true;
}
QByteArray fingerprint(const QList<BackupFiles::FileRecord> &files) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto &file : files) {
        const auto bytes = QJsonDocument(QJsonObject{{"path", file.path},
            {"size", QString::number(file.size)}, {"sha1", file.sha1}}).toJson(QJsonDocument::Compact);
        hash.addData(bytes);
        hash.addData(QByteArray("\n"));
    }
    return hash.result();
}
bool saveObject(const QString &path, const QJsonObject &json) {
    QSaveFile f(path);
    const auto bytes = QJsonDocument(json).toJson();
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}
// A failed attempt can leave `prepared/` holding a full copy of the instance
// (saving everything, mods, screenshots). The copy has no recovery value until
// the original is retained: before that point nothing under `versions/` has
// changed, and a journal without `original/` is ignored by recovery. Keep the
// transaction directory only when it holds rollback material.
void discardUnpublishedStaging(const Request &r) {
    // Never resolve a relative/empty game dir against the process working
    // directory; only a request that passed validation names a real location.
    if (!validate(r).isEmpty()) return;
    const QString root = transactionDir(r);
    if (!QFileInfo(root).isDir() || redirected(root)) return;
    if (QFileInfo(root + "/original").isDir() || QFileInfo(root + "/replaced").isDir()) return;
    removeTreeWithoutFollowingLinks(root);
}
// Recovery first acquires the transaction lock. Copied files preserve source mtimes,
// so include the staging directory's own timestamp in the abandonment grace period.
bool stagingIsStale(const QString &dir) {
    QDateTime newest = QFileInfo(dir).lastModified();
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QDateTime at = it.fileInfo().lastModified();
        if (at.isValid() && (newest.isNull() || at > newest)) newest = at;
    }
    if (newest.isNull()) newest = QFileInfo(dir).lastModified();  // 一个文件都还没拷
    return newest.isValid()
           && newest.secsTo(QDateTime::currentDateTime()) > 30 * 60;
}
} // namespace

QString transactionDir(const Request &r) {
    return QDir(r.gameDir).filePath("cache/instance-upgrades/" + r.transactionId);
}
QString candidateDir(const Request &r) {
    return transactionDir(r) + "/prepared/versions/" + r.instanceId;
}

QString dependentInstanceProblem(const QString &gameDir, const QString &instanceId) {
    if (!QDir::isAbsolutePath(gameDir) || !InstanceFiles::validateDirName(instanceId).isEmpty())
        return QStringLiteral("无法安全检查实例引用关系。");
    const QDir versions(QDir(gameDir).filePath("versions"));
    const QString original = versions.filePath(instanceId);
    for (const auto &entry : versions.entryInfoList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
        if (entry.fileName().compare(instanceId, Qt::CaseInsensitive) == 0) continue;
        if (redirected(entry.absoluteFilePath())
            && InstanceFiles::pathIsSameOrInside(original, entry.absoluteFilePath()))
            return QStringLiteral("实例 %1 通过目录链接使用此核心，请改用升级副本。").arg(entry.fileName());
        const QDir profile(entry.absoluteFilePath());
        for (const auto &name : profile.entryList({"*.json"}, QDir::Files | QDir::Hidden)) {
            QFile f(profile.filePath(name));
            if (!f.open(QIODevice::ReadOnly) || f.size() > 16 * 1024 * 1024)
                return QStringLiteral("无法核实 %1 的核心引用，未执行原地替换。").arg(entry.fileName());
            const auto document = QJsonDocument::fromJson(f.readAll());
            if (!document.isObject()) continue; // Non-profile JSON is not an inheritance edge.
            const auto json = document.object();
            for (const QString &field : {QStringLiteral("inheritsFrom"), QStringLiteral("jar")}) {
                if (json.value(field).toString().compare(instanceId, Qt::CaseInsensitive) == 0)
                    return QStringLiteral("实例 %1 的 %2 引用了此核心。请改用升级副本，避免影响其它实例。")
                        .arg(entry.fileName(), field);
            }
        }
    }
    return {};
}

static Result prepareAndCommitImpl(Request r, const Core &core, const std::function<void(Stage)> &progress,
                                   const std::function<bool()> &cancelled) {
    Result result;
    result.problem = validate(r);
    if (!result.problem.isEmpty()) return result;
    result.problem = dependentInstanceProblem(r.gameDir, r.instanceId);
    if (!result.problem.isEmpty()) return result;
    if (core.mergedJson.value("mainClass").toString().isEmpty() || core.gameVersion.isEmpty()
        || !QDir::isAbsolutePath(core.clientJar) || !QFileInfo(core.clientJar).isFile()
        || redirected(core.clientJar) || QFileInfo::exists(candidateDir(r))) {
        result.problem = QStringLiteral("目标核心、客户端文件或暂存路径无效，未执行原地升级。"); return result;
    }
    if (progress) progress(Stage::Checking);
    const QString original = originalDir(r), candidate = candidateDir(r);
    result.problem = checkTree(original);
    if (!result.problem.isEmpty()) return result;
    QString problem;
    const auto files = BackupFiles::collectFiles(original, {}, &problem, cancelled);
    if (cancelled && cancelled()) {
        result.cancelled = true;
        result.problem = QStringLiteral("已取消准备。");
        return result;
    }
    if (!problem.isEmpty() || files.isEmpty()) {
        result.problem = problem.isEmpty() ? QStringLiteral("原实例是空的。") : problem; return result;
    }
    r.originalFingerprint = fingerprint(files);
    qint64 size = 0;
    for (const auto &file : files) {
        if (file.size < 0 || size > std::numeric_limits<qint64>::max() - file.size) {
            result.problem = QStringLiteral("实例大小溢出，未开始准备。"); return result;
        }
        size += file.size;
    }
    const qint64 available = QStorageInfo(r.gameDir).bytesAvailable();
    if (available < 0 || size > (std::numeric_limits<qint64>::max() - 32 * 1024 * 1024) / 3
        || available < size * 3 + 32 * 1024 * 1024
        || (r.spaceBudgetBytes >= 0 && r.spaceBudgetBytes < size * 3 + 32 * 1024 * 1024)) {
        result.problem = QStringLiteral("空间不足以准备副本和完整快照，原实例未改变。"); return result;
    }
    if (cancelled && cancelled()) { result.cancelled = true; result.problem = QStringLiteral("已取消准备。"); return result; }
    if (!copyTreeWithoutFollowingLinks(original, candidate, cancelled)) {
        if (cancelled && cancelled()) {
            result.cancelled = true;
            result.problem = QStringLiteral("已取消准备。");
        } else {
            result.problem = QStringLiteral("用户文件副本未通过完整校验，原实例未改变。");
        }
        return result;
    }
    if (cancelled && cancelled()) { result.cancelled = true; result.problem = QStringLiteral("已取消准备。"); return result; }
    if (!sameFiles(candidate, files, cancelled)) {
        if (cancelled && cancelled()) {
            result.cancelled = true;
            result.problem = QStringLiteral("已取消准备。");
        } else {
            result.problem = QStringLiteral("用户文件副本未通过完整校验，原实例未改变。");
        }
        return result;
    }
    // Read identity/settings from the copied original, never from the target.
    QJsonObject meta;
    const QString metaPath = candidate + "/wtfml.meta";
    QFile metaFile(metaPath);
    if (metaFile.exists()) {
        if (!metaFile.open(QIODevice::ReadOnly) || metaFile.size() > 16 * 1024 * 1024) {
            result.problem = QStringLiteral("无法读取原实例设置，未继续升级。"); return result;
        }
        const auto document = QJsonDocument::fromJson(metaFile.readAll());
        if (!document.isObject()) { result.problem = QStringLiteral("原实例设置不是有效 JSON，未继续升级。"); return result; }
        meta = document.object();
        metaFile.close();
    }
    auto json = core.mergedJson;
    json.insert("id", r.instanceId);
    json.remove("inheritsFrom"); // Flattening prevents inheritance through the id being replaced.
    json.remove("jar");
    meta.insert("realVersion", core.gameVersion);
    meta.insert("loaderName", core.loaderName);
    meta.insert("loaderVersion", core.loaderVersion);
    meta.insert("upgradeTransactionId", r.transactionId);
    QFile jar(core.clientJar);
    QCryptographicHash hash(QCryptographicHash::Sha1);
    const QString expected = json.value("downloads").toObject().value("client").toObject().value("sha1").toString();
    if (!jar.open(QIODevice::ReadOnly) || !hash.addData(&jar)
        || (!expected.isEmpty() && QString::fromLatin1(hash.result().toHex()).compare(expected, Qt::CaseInsensitive) != 0)) {
        result.problem = QStringLiteral("目标客户端文件未通过校验，原实例未改变。"); return result;
    }
    jar.close();
    // Remove the previous core's jar/json pair before writing the new one.
    //
    // A directory that was renamed outside the launcher (or whose rename failed halfway)
    // still holds a jar/json named after the *old* name. Leaving them beside the new pair
    // is not cosmetic: the launcher adds any top-level jar that contains the main class to
    // the classpath, and the stale client jar of course does — so the old and new cores
    // end up on one classpath.
    //
    // Only these two names count as the previous core (other jars in a version directory,
    // e.g. `lwjgl3ify-forgePatches.jar`, are required at runtime and must not be touched):
    // · `<json id>.jar` (both vanilla and instances the launcher created), and
    // · `<json stem>.jar` (the leftover half of a rename).
    // NOTE: the json's own `jar` field is *not* used — in a version json it names another
    // *version id*, not a file in this directory, so deleting by it could hit an
    // unrelated jar.
    const QString oldJsonName = InstanceFiles::jsonName(r.instanceId);
    if (oldJsonName.contains('/') || oldJsonName.contains('\\') || oldJsonName.contains(':')) {
        result.problem = QStringLiteral("旧版本信息文件名不安全，原实例未改变。"); return result;
    }
    const QString newJarName = r.instanceId + QStringLiteral(".jar");
    const QString newJsonName = r.instanceId + QStringLiteral(".json");
    QJsonObject oldJson;
    {
        QFile oldJsonFile(candidate + '/' + oldJsonName);
        if (oldJsonFile.open(QIODevice::ReadOnly) && oldJsonFile.size() <= 16 * 1024 * 1024) {
            const auto document = QJsonDocument::fromJson(oldJsonFile.readAll());
            if (document.isObject()) oldJson = document.object();
        }
    }
    const QStringList staleJars{oldJson.value(QStringLiteral("id")).toString() + QStringLiteral(".jar"),
                                QFileInfo(oldJsonName).completeBaseName() + QStringLiteral(".jar")};
    for (const QString &name : staleJars) {
        if (name.contains('/') || name.contains('\\') || name.contains(':')
            || !InstanceFiles::validateDirName(name).isEmpty()) {
            result.problem = QStringLiteral("旧版本信息包含不安全的客户端文件名，原实例未改变。");
            return result;
        }
        if (name.isEmpty() || name.compare(newJarName, Qt::CaseInsensitive) == 0) continue;
        const QString stale = candidate + '/' + name;
        if (QFileInfo::exists(stale) && !QFile::remove(stale)) {
            result.problem = QStringLiteral("无法清理暂存副本中的旧客户端文件，原实例未改变。");
            return result;
        }
    }
    if (oldJsonName.compare(newJsonName, Qt::CaseInsensitive) != 0) {
        const QString stale = candidate + '/' + oldJsonName;
        if (QFileInfo::exists(stale) && !QFile::remove(stale)) {
            result.problem = QStringLiteral("无法清理暂存副本中的旧版本信息，原实例未改变。");
            return result;
        }
    }
    const QString client = candidate + '/' + r.instanceId + ".jar";
    if ((QFileInfo::exists(client) && !QFile::remove(client)) || !QFile::copy(core.clientJar, client)
        || !saveObject(candidate + '/' + r.instanceId + ".json", json) || !saveObject(metaPath, meta)) {
        result.problem = QStringLiteral("无法准备新核心或设置，原实例未改变。"); return result;
    }
    QFile copiedJar(client);
    QCryptographicHash copiedHash(QCryptographicHash::Sha1);
    if (!copiedJar.open(QIODevice::ReadOnly) || !copiedHash.addData(&copiedJar)
        || copiedHash.result() != hash.result()) {
        result.problem = QStringLiteral("目标客户端在复制期间发生变化，未发布新版本。"); return result;
    }
    copiedJar.close();
    const QString natives = candidate + "/natives";
    if (QFileInfo::exists(natives) && !removeTreeWithoutFollowingLinks(natives)) {
        result.problem = QStringLiteral("无法清理暂存副本中的旧原生库，原实例未改变。"); return result;
    }
    if (core.disableOldMods) {
        QDirIterator it(candidate + "/mods", QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (!path.endsWith(".jar", Qt::CaseInsensitive)) continue;
              const QString relative = QDir(candidate + "/mods").relativeFilePath(path);
              if (core.keepOldMods.contains(relative)) continue;
            if (QFileInfo::exists(path + ".disabled") || !QFile::rename(path, path + ".disabled")) {
                result.problem = QStringLiteral("无法禁用尚未验证兼容性的旧模组，原实例未改变。"); return result;
            }
            ++result.disabledMods;
        }
    }
    const int disabled = result.disabledMods;
    result = commitPrepared(r, progress, cancelled);
    result.disabledMods = disabled;
    return result;
}

Result prepareAndCommit(Request r, const Core &core, const std::function<void(Stage)> &progress,
                        const std::function<bool()> &cancelled) {
    const QString invalid = validate(r);
    if (!invalid.isEmpty()) { Result result; result.problem = invalid; return result; }
    QString lockProblem;
    if (!DirectorySessionLock::acquire(r.gameDir, &lockProblem)) {
        Result result; result.problem = lockProblem; return result;
    }
    const QString parent = QFileInfo(transactionDir(r)).absolutePath();
    QDir().mkpath(parent);
    QLockFile lock(transactionDir(r) + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) {
        Result result; result.problem = QStringLiteral("升级事务正在使用，未修改实例。"); return result;
    }
    const auto result = prepareAndCommitImpl(r, core, progress, cancelled);
    if (!result.ok) discardUnpublishedStaging(r);
    return result;
}

static Result rollbackImpl(const Request &r, bool verifyRetained,
                           const std::function<bool()> &cancelled = {}) {
    Result result;
    const auto stopped = [&] { return cancelled && cancelled(); };
    const auto stopResult = [&] {
        result.cancelled = true;
        result.problem = QStringLiteral("已取消，当前实例未移动。");
        return result;
    };
    result.problem = validate(r);
    if (!result.problem.isEmpty()) return result;
    const auto journal = readJournal(r);
    const QString retained = transactionDir(r) + "/original";
    const QString displaced = transactionDir(r) + "/replaced";
    const QString original = originalDir(r);
    if (journal.isEmpty() || !QFileInfo(retained).isDir()) {
        result.problem = QStringLiteral("找不到这次升级保留的原目录，没有执行回滚。");
        return result;
    }
    result.snapshotZip = journal.value("snapshotZip").toString();
    result.rollbackAvailable = true;
    if (stopped()) return stopResult();
    if (verifyRetained) {
        result.problem = dependentInstanceProblem(r.gameDir, r.instanceId);
        if (!result.problem.isEmpty()) return result;
        const QString expectedFolder = QDir(r.gameDir).filePath("cache/backups/instances");
        if (redirected(result.snapshotZip) || QDir(QFileInfo(result.snapshotZip).absolutePath()) != QDir(expectedFolder)) {
            result.problem = QStringLiteral("快照位置不符合本次事务，未移动当前实例。"); return result;
        }
        bool ok = false;
        QString problem;
        const auto manifest = BackupFiles::readManifest(result.snapshotZip, &ok, &problem);
        if (!ok || !manifest.complete || !manifest.excluded.isEmpty() || manifest.root != r.instanceId
            || manifest.kind != BackupFiles::Kind::Instance || !checkTree(retained).isEmpty()) {
            result.problem = QStringLiteral("保留的原目录未通过快照清单校验；未移动当前实例，请使用完整 ZIP 快照恢复。%1").arg(problem);
            return result;
        }
        if (stopped()) return stopResult();
        if (!sameFiles(retained, manifest.files, cancelled)) {
            if (stopped()) return stopResult();
            result.problem = QStringLiteral("保留的原目录未通过快照清单校验；未移动当前实例，请使用完整 ZIP 快照恢复。%1").arg(problem);
            return result;
        }
    }
    const bool hadCurrent = QFileInfo::exists(original);
    if (hadCurrent && QFileInfo::exists(displaced)) {
        result.problem = QStringLiteral("回滚保存位置已有文件，未覆盖：%1").arg(displaced);
        return result;
    }
    // 最后这一小段（写阶段记录 + 两三次同卷改名）是**故意不可取消**的临界区：
    // 停在中间会把"当前版本"和"保留的原目录"两边都留在半路。
    if (stopped()) return stopResult();
    if (!writeJournal(r, "rollback_pending", result.snapshotZip)) {
        result.problem = QStringLiteral("无法记录回滚阶段，未移动实例文件。");
        return result;
    }
    if (hadCurrent && !QDir().rename(original, displaced)) {
        result.problem = QStringLiteral("无法暂存当前版本，未覆盖或删除任何文件。");
        return result;
    }
    if (!QDir().rename(retained, original)) {
        const bool putBack = !hadCurrent || QDir().rename(displaced, original);
        result.problem = QStringLiteral("原目录未能恢复，恢复材料保留在 %1。%2")
            .arg(retained, putBack ? QString() : QStringLiteral("当前版本也保留在 replaced 目录中。"));
        return result;
    }
    result.rolledBack = true;
    result.rollbackAvailable = false;
    result.ok = writeJournal(r, "rolled_back", result.snapshotZip);
    if (!result.ok) result.problem = QStringLiteral("原实例已恢复，但回滚记录未能更新；没有删除恢复材料。");
    return result;
}

Result rollback(const Request &r, const std::function<bool()> &cancelled) {
    QString problem;
    if (!DirectorySessionLock::acquire(r.gameDir, &problem)) {
        Result result; result.problem = problem; return result;
    }
    return rollbackImpl(r, true, cancelled);
}

static Result commitPreparedImpl(const Request &r, const std::function<void(Stage)> &progress,
                                 const std::function<bool()> &cancelled) {
    Result result;
    const auto report = [&](Stage stage) { if (progress) progress(stage); };
    const auto stopped = [&] { return cancelled && cancelled(); };
    report(Stage::Checking);
    result.problem = validate(r);
    if (!result.problem.isEmpty()) return result;
    const QString original = originalDir(r), candidate = candidateDir(r);
    if (QFileInfo::exists(journalPath(r)) || !QFileInfo(original).isDir()
        || !QFileInfo(candidate).isDir() || QFileInfo::exists(transactionDir(r) + "/original")) {
        result.problem = QStringLiteral("原实例、暂存版本或事务目录状态不正确，未替换实例。");
        return result;
    }
    for (const auto &root : {original, candidate}) {
        result.problem = checkTree(root);
        if (!result.problem.isEmpty()) return result;
        // 这两趟整树扫描各自可能几十秒（大整合包）；不在这儿看取消位，
        // 用户按了取消要等整棵树扫完才有反应 —— 而这时候还什么都没动。
        if (stopped()) {
            result.cancelled = true;
            result.problem = QStringLiteral("已取消，原实例未改变。");
            return result;
        }
    }
    QFile version(candidate + '/' + r.instanceId + ".json");
    if (!version.open(QIODevice::ReadOnly) || version.size() > 16 * 1024 * 1024
        || QJsonDocument::fromJson(version.readAll()).object().value("id").toString() != r.instanceId) {
        result.problem = QStringLiteral("暂存版本的核心 JSON 缺失或实例名不匹配，未替换。");
        return result;
    }
    version.close();
    QString problem;
    const auto newFiles = BackupFiles::collectFiles(candidate, {}, &problem, cancelled);
    if (stopped()) {
        result.cancelled = true;
        result.problem = QStringLiteral("已取消，原实例未改变。");
        return result;
    }
    if (!problem.isEmpty() || newFiles.isEmpty()) {
        result.problem = problem.isEmpty() ? QStringLiteral("暂存版本没有文件。") : problem;
        return result;
    }
    const auto oldFiles = BackupFiles::collectFiles(original, {}, &problem, cancelled);
    if (stopped()) {
        result.cancelled = true;
        result.problem = QStringLiteral("已取消，原实例未改变。");
        return result;
    }
    if (!r.originalFingerprint.isEmpty() && fingerprint(oldFiles) != r.originalFingerprint) {
        result.problem = QStringLiteral("原实例在准备期间发生了变化，未替换实例。"); return result;
    }
    qint64 oldBytes = 0;
    for (const auto &file : oldFiles) {
        if (file.size < 0 || file.size > (std::numeric_limits<qint64>::max() / 2 - oldBytes)) {
            result.problem = QStringLiteral("实例大小超出可安全计算的范围。"); return result;
        }
        oldBytes += file.size;
    }
    const QStorageInfo storage(r.gameDir);
    const qint64 available = r.spaceBudgetBytes >= 0
        ? qMin(storage.bytesAvailable(), r.spaceBudgetBytes) : storage.bytesAvailable();
    const qint64 margin = 32 * 1024 * 1024;
    if (!problem.isEmpty() || oldFiles.isEmpty() || !storage.isReady()
        || available < 0 || oldBytes > (std::numeric_limits<qint64>::max() - margin) / 2
        || available < oldBytes * 2 + margin) {
        result.problem = problem.isEmpty() ? QStringLiteral("无法确认有足够空间制作并校验完整快照，未替换实例。") : problem;
        return result;
    }
    if (stopped()) { result.cancelled = true; result.problem = QStringLiteral("已取消，原实例未改变。"); return result; }
    report(Stage::Snapshot);
    BackupFiles::Manifest manifest;
    manifest.kind = BackupFiles::Kind::Instance;
    manifest.root = r.instanceId;
    // BackupController discovers instance snapshots by directory-name prefix,
    // not the formatted/custom display name (which changes after an upgrade).
    manifest.label = r.instanceId;
    manifest.sourceInstanceId = r.instanceId;
    manifest.sourceInstanceName = r.label.isEmpty() ? r.instanceId : r.label;
    manifest.complete = true;
    manifest.safety = true;
    manifest.scope = {QStringLiteral("原地升级前完整实例快照（不排除 assets、libraries 或用户文件）")};
    QString zip = QDir(r.gameDir).filePath("cache/backups/instances/")
        + BackupFiles::backupFileName(manifest.label, QDateTime::currentDateTime(), true)
              .chopped(4) + '-' + r.transactionId + ".zip";
    if (QFileInfo::exists(zip) || redirected(zip)) { result.problem = QStringLiteral("快照名称已存在或是链接，未覆盖。"); return result; }
    const auto snapshot = BackupFiles::writeBackup(manifest, original, zip, cancelled);
    if (!snapshot.ok) {
        result.cancelled = stopped(); // Snapshot phase has not moved the original directory.
        result.problem = snapshot.problem;
        return result;
    }
    // `snapshot.zipPath` 才是真正落盘的那一个（撞名时 `writeBackup()` 会换一个名字）。
    // 回滚读的是**记进事务文件的那条路径**，记错名字等于那一份快照永远找不回来。
    zip = snapshot.zipPath;
    result.snapshotZip = zip;
    report(Stage::VerifySnapshot);
    {
        QTemporaryDir verify(transactionDir(r) + "/verify-XXXXXX");
        if (!verify.isValid() || !ZipUtil::extractArchives({zip}, verify.path(), {}, &problem)) {
            result.problem = QStringLiteral("完整快照校验失败，或原实例在备份期间发生变化，未替换。%1").arg(problem);
            return result;
        }
        if (stopped()) {
            result.cancelled = true;
            result.problem = QStringLiteral("已取消，快照保留，原实例未改变。");
            return result;
        }
        if (!sameFiles(verify.path() + '/' + r.instanceId, snapshot.manifest.files, cancelled)
            || !sameFiles(original, snapshot.manifest.files, cancelled)
            || !sameFiles(original, oldFiles, cancelled)) {
            if (stopped()) {
                result.cancelled = true;
                result.problem = QStringLiteral("已取消，快照保留，原实例未改变。");
                return result;
            }
            result.problem = QStringLiteral("完整快照校验失败，或原实例在备份期间发生变化，未替换。%1").arg(problem);
            return result;
        }
    }
    if (stopped()) { result.cancelled = true; result.problem = QStringLiteral("已取消，快照保留，原实例未改变。"); return result; }
    result.problem = checkTree(original);
    if (result.problem.isEmpty()) result.problem = checkTree(candidate);
    if (!result.problem.isEmpty() || !sameFiles(candidate, newFiles, cancelled)) {
        if (stopped()) {
            result.cancelled = true;
            result.problem = QStringLiteral("已取消，原实例未改变。");
        } else if (result.problem.isEmpty()) {
            result.problem = QStringLiteral("暂存版本在准备期间发生变化，未替换。");
        }
        return result;
    }
    if (!writeJournal(r, "prepared", zip)) {
        result.problem = QStringLiteral("无法写入升级事务记录，未替换实例。"); return result;
    }
    const auto failAndRestore = [&](const QString &reason) {
        result.requiresReload = true;
        const auto recovery = rollbackImpl(r, false);
        result.rolledBack = recovery.rolledBack;
        result.rollbackAvailable = recovery.rollbackAvailable;
        // 只有确认恢复了原实例才可显示“取消”。恢复失败必须保持失败终态。
        if (!recovery.ok || !recovery.rolledBack) result.cancelled = false;
        result.problem = reason + (recovery.rolledBack ? QStringLiteral(" 原实例已自动恢复。")
            : QStringLiteral(" 回滚未完成：%1").arg(recovery.problem));
        return result;
    };
    report(Stage::RetainOriginal);
    result.problem = dependentInstanceProblem(r.gameDir, r.instanceId);
    if (!result.problem.isEmpty()) return result;
    if (!QDir().rename(original, transactionDir(r) + "/original")) {
        result.problem = QStringLiteral("无法保留原目录，未替换实例。"); return result;
    }
    if (!writeJournal(r, "original_retained", zip))
        return failAndRestore(QStringLiteral("无法记录原目录位置。"));
    report(Stage::Publish);
    if (stopped()) {
        // 取消落在"已经把原目录收起来、还没换上新版本"这一小段：自动恢复再报取消。
        result.cancelled = true;
        return failAndRestore(QStringLiteral("已取消发布。"));
    }
    if (!QDir().rename(candidate, original))
        return failAndRestore(QStringLiteral("发布新版本中断或失败。"));
    report(Stage::Verify);
    if (stopped()) {
        result.cancelled = true;
        return failAndRestore(QStringLiteral("已取消发布后的校验。"));
    }
    if (!checkTree(original).isEmpty() || !sameFiles(original, newFiles, cancelled)) {
        if (stopped()) result.cancelled = true;
        return failAndRestore(QStringLiteral("发布后的实例校验失败。"));
    }
    if (!writeJournal(r, "committed", zip))
        return failAndRestore(QStringLiteral("无法记录升级完成状态。"));
    result.ok = true;
    result.rollbackAvailable = true;
    report(Stage::Done);
    return result;
}

Result commitPrepared(const Request &r, const std::function<void(Stage)> &progress,
                      const std::function<bool()> &cancelled) {
    QString problem;
    if (!DirectorySessionLock::acquire(r.gameDir, &problem)) {
        Result result; result.problem = problem; return result;
    }
    const auto result = commitPreparedImpl(r, progress, cancelled);
    if (!result.ok) discardUnpublishedStaging(r);
    return result;
}

QStringList recoverableTransactions(const QString &gameDir, const QString &instanceId) {
    QStringList ids;
    const QDir root(QDir(gameDir).filePath("cache/instance-upgrades"));
    for (const auto &id : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
        const Request r{gameDir, instanceId, id, {}};
        if (validate(r).isEmpty() && !readJournal(r).isEmpty()
            && QFileInfo(transactionDir(r) + "/original").isDir()) ids.append(id);
    }
    return ids;
}

QStringList recoverInterrupted(const QString &gameDir) {
    QStringList messages;
    QString problem;
    if (!DirectorySessionLock::acquire(gameDir, &problem)) return {problem};
    const QDir root(QDir(gameDir).filePath("cache/instance-upgrades"));
    if (redirected(root.absolutePath())) return {QStringLiteral("升级事务目录是链接，未自动恢复。")};
    for (const auto &id : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QLockFile lock(root.filePath(id + QStringLiteral(".lock")));
        lock.setStaleLockTime(0);
        if (!lock.tryLock()) continue;
        QFile f(root.filePath(id + "/journal.json"));
        if (redirected(root.filePath(id)) || redirected(f.fileName())) continue;
        if (!f.exists()) {
            // 没有清单的事务目录只有两种来路 —— 「准备」拷到一半就崩了
            // （暂存里有整整一份实例，几个 GB，永远没人清），或者**另一个启动器正在拷**
            // （这份程序没有"只许开一个"的锁）。判据用"最近改过没有"：正在拷的那一份
            // 时时刻刻在写文件；半个小时没人碰它，就只能是一具尸体。
            const QString txn = QDir(gameDir).filePath("cache/instance-upgrades/" + id);
            const bool shaped = QRegularExpression("^[A-Za-z0-9-]{1,80}$").match(id).hasMatch();
            if (shaped && !QFileInfo(txn + "/original").isDir()
                && !QFileInfo(txn + "/replaced").isDir() && stagingIsStale(txn)
                && removeTreeWithoutFollowingLinks(txn))
                messages.append(QStringLiteral("已清理未登记的升级暂存：%1").arg(id));
            continue;
        }
        if (!f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024) continue;
        const auto json = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        const Request r{gameDir, json.value("instanceId").toString(), id, {}};
        if (!validate(r).isEmpty() || readJournal(r).isEmpty()) continue;
        const QString state = json.value("state").toString();
        if (state != "prepared" && state != "original_retained" && state != "rollback_pending") continue;
        if (state == "prepared" && !QFileInfo(transactionDir(r) + "/original").isDir()) {
            // Crash before the original was retained: nothing under versions/ was
            // touched, so the staged copy has no recovery value. Do not leak it.
            // (`prepared` with an original/ directory can only mean the crash hit
            // between the rename and the journal update — that one must roll back.)
            if (removeTreeWithoutFollowingLinks(transactionDir(r)))
                messages.append(QStringLiteral("已清理未发布的升级暂存：%1").arg(r.instanceId));
            continue;
        }
        if (!QFileInfo(transactionDir(r) + "/original").isDir()) continue;
        const auto result = rollbackImpl(r, false);
        messages.append(result.rolledBack ? QStringLiteral("中断的升级已恢复原实例：%1").arg(r.instanceId)
            : QStringLiteral("升级恢复需要处理：%1；%2").arg(r.instanceId, result.problem));
    }
    return messages;
}
} // namespace InstanceUpgrade
