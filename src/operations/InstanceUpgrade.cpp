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
bool sameFiles(const QString &root, const QList<BackupFiles::FileRecord> &expected) {
    QString problem;
    const auto actual = BackupFiles::collectFiles(root, {}, &problem);
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

Result prepareAndCommit(Request r, const Core &core, const std::function<void(Stage)> &progress,
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
    const auto files = BackupFiles::collectFiles(original, {}, &problem);
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
    if (cancelled && cancelled()) { result.problem = QStringLiteral("已取消准备。"); return result; }
    if (!copyTreeWithoutFollowingLinks(original, candidate) || !sameFiles(candidate, files)) {
        result.problem = QStringLiteral("用户文件副本未通过完整校验，原实例未改变。"); return result;
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

static Result rollbackImpl(const Request &r, bool verifyRetained) {
    Result result;
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
            || manifest.kind != BackupFiles::Kind::Instance || !checkTree(retained).isEmpty()
            || !sameFiles(retained, manifest.files)) {
            result.problem = QStringLiteral("保留的原目录未通过快照清单校验；未移动当前实例，请使用完整 ZIP 快照恢复。%1").arg(problem);
            return result;
        }
    }
    const bool hadCurrent = QFileInfo::exists(original);
    if (hadCurrent && QFileInfo::exists(displaced)) {
        result.problem = QStringLiteral("回滚保存位置已有文件，未覆盖：%1").arg(displaced);
        return result;
    }
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

Result rollback(const Request &r) { return rollbackImpl(r, true); }

Result commitPrepared(const Request &r, const std::function<void(Stage)> &progress,
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
    }
    QFile version(candidate + '/' + r.instanceId + ".json");
    if (!version.open(QIODevice::ReadOnly) || version.size() > 16 * 1024 * 1024
        || QJsonDocument::fromJson(version.readAll()).object().value("id").toString() != r.instanceId) {
        result.problem = QStringLiteral("暂存版本的核心 JSON 缺失或实例名不匹配，未替换。");
        return result;
    }
    version.close();
    QString problem;
    const auto newFiles = BackupFiles::collectFiles(candidate, {}, &problem);
    if (!problem.isEmpty() || newFiles.isEmpty()) {
        result.problem = problem.isEmpty() ? QStringLiteral("暂存版本没有文件。") : problem;
        return result;
    }
    const auto oldFiles = BackupFiles::collectFiles(original, {}, &problem);
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
    if (stopped()) { result.problem = QStringLiteral("已取消，原实例未改变。"); return result; }
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
    const QString zip = QDir(r.gameDir).filePath("cache/backups/instances/")
        + BackupFiles::backupFileName(manifest.label, QDateTime::currentDateTime(), true)
              .chopped(4) + '-' + r.transactionId + ".zip";
    if (QFileInfo::exists(zip) || redirected(zip)) { result.problem = QStringLiteral("快照名称已存在或是链接，未覆盖。"); return result; }
    const auto snapshot = BackupFiles::writeBackup(manifest, original, zip);
    if (!snapshot.ok) { result.problem = snapshot.problem; return result; }
    result.snapshotZip = zip;
    report(Stage::VerifySnapshot);
    {
        QTemporaryDir verify(transactionDir(r) + "/verify-XXXXXX");
        if (!verify.isValid() || !ZipUtil::extractArchives({zip}, verify.path(), {}, &problem)
            || !sameFiles(verify.path() + '/' + r.instanceId, snapshot.manifest.files)
            || !sameFiles(original, snapshot.manifest.files) || !sameFiles(original, oldFiles)) {
            result.problem = QStringLiteral("完整快照校验失败，或原实例在备份期间发生变化，未替换。%1").arg(problem);
            return result;
        }
    }
    if (stopped()) { result.problem = QStringLiteral("已取消，快照保留，原实例未改变。"); return result; }
    result.problem = checkTree(original);
    if (result.problem.isEmpty()) result.problem = checkTree(candidate);
    if (!result.problem.isEmpty() || !sameFiles(candidate, newFiles)) {
        if (result.problem.isEmpty()) result.problem = QStringLiteral("暂存版本在准备期间发生变化，未替换。");
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
    if (stopped() || !QDir().rename(candidate, original))
        return failAndRestore(QStringLiteral("发布新版本中断或失败。"));
    report(Stage::Verify);
    if (stopped() || !checkTree(original).isEmpty() || !sameFiles(original, newFiles))
        return failAndRestore(QStringLiteral("发布后的实例校验失败。"));
    if (!writeJournal(r, "committed", zip))
        return failAndRestore(QStringLiteral("无法记录升级完成状态。"));
    result.ok = true;
    result.rollbackAvailable = true;
    report(Stage::Done);
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
    const QDir root(QDir(gameDir).filePath("cache/instance-upgrades"));
    if (redirected(root.absolutePath())) return {QStringLiteral("升级事务目录是链接，未自动恢复。")};
    for (const auto &id : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile f(root.filePath(id + "/journal.json"));
        if (redirected(root.filePath(id)) || redirected(f.fileName())
            || !f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024) continue;
        const auto json = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        const Request r{gameDir, json.value("instanceId").toString(), id, {}};
        if (!validate(r).isEmpty() || readJournal(r).isEmpty()) continue;
        const QString state = json.value("state").toString();
        if (state != "prepared" && state != "original_retained" && state != "rollback_pending") continue;
        if (!QFileInfo(transactionDir(r) + "/original").isDir()) continue;
        const auto result = rollbackImpl(r, false);
        messages.append(result.rolledBack ? QStringLiteral("中断的升级已恢复原实例：%1").arg(r.instanceId)
            : QStringLiteral("升级恢复需要处理：%1；%2").arg(r.instanceId, result.problem));
    }
    return messages;
}
} // namespace InstanceUpgrade
