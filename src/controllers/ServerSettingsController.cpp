#include "controllers/ServerSettingsController.hpp"

#include "controllers/ServerWorkspaceController.hpp"
#include "manage/BackupFiles.hpp"
#include "server/ServerPropertiesForm.hpp"
#include "server/ServerRegistry.hpp"
#include "server/ServerWriteGuard.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

QString ServerSettingsController::readRaw(const QString &serverId) const {
    QFile file(ServerRegistry::propertiesPath(serverId));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

ServerSettingsController::Values ServerSettingsController::valuesFromRaw(
    const QString &raw) const {
    return ServerPropertiesForm::valuesOf(ServerModel::parseProperties(raw),
                                          ServerPropertiesForm::commonFields());
}

QString ServerSettingsController::mergeIntoRaw(const QString &raw,
                                                const Values &edits) const {
    ServerModel::Properties properties = ServerModel::parseProperties(raw);
    ServerPropertiesForm::applyEdits(properties, edits);
    return properties.toString();
}

ServerSettingsController::SaveResult ServerSettingsController::save(
    const QString &serverId, const QString &raw, const Values &edits) const {
    SaveResult result;
    ServerModel::Properties properties = ServerModel::parseProperties(raw);
    Values accepted;
    for (const QPair<QString, QString> &edit : edits) {
        const ServerPropertiesForm::Field *field = ServerPropertiesForm::fieldFor(edit.first);
        if (field == nullptr) {
            continue;
        }
        QString normalized;
        QString problem;
        if (!ServerPropertiesForm::accepts(*field, edit.second, &normalized, &problem)) {
            result.rejected.append(QStringLiteral("·「%1」：%2").arg(field->label, problem));
            continue;
        }
        accepted.append(qMakePair(edit.first, normalized));
    }
    if (!result.rejected.isEmpty()) {
        return result;
    }
    result.changed = ServerPropertiesForm::applyEdits(properties, accepted);
    if (!ServerRegistry::writeProperties(serverId, properties, &result.problem)) {
        return result;
    }
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    result.serverName = entry.name.isEmpty() ? serverId : entry.name;
    result.ok = true;
    return result;
}

ServerSettingsController::Values ServerSettingsController::fallbackEdits(
    const ServerModel::Properties &properties) const {
    Values edits;
    for (const ServerPropertiesForm::ValueProblem &problem
         : ServerPropertiesForm::problemsIn(properties)) {
        if (const ServerPropertiesForm::Field *field =
                ServerPropertiesForm::fieldFor(problem.key)) {
            edits.append(qMakePair(problem.key, field->fallback));
        }
    }
    return edits;
}

QString ServerSettingsController::worldName(const QString &serverId) const {
    const ServerModel::Properties properties = ServerRegistry::readProperties(serverId);
    const QString name = ServerPropertiesForm::worldDirName(properties);
    if (!name.isEmpty()) {
        return name;
    }
    const ServerPropertiesForm::Field *field =
        ServerPropertiesForm::fieldFor(QStringLiteral("level-name"));
    return field != nullptr && !properties.contains(field->key) ? field->fallback : QString();
}

QString ServerSettingsController::worldDir(const QString &serverId) const {
    const QString name = worldName(serverId);
    return name.isEmpty() ? QString()
                          : QDir(ServerRegistry::describe(serverId).dirPath).filePath(name);
}

QString ServerSettingsController::backupLabel(const QString &serverId) const {
    const QString name = worldName(serverId);
    return name.isEmpty() ? QString() : serverId + QLatin1Char('-') + name;
}

int ServerSettingsController::worldBackupCount(const QString &serverId) const {
    const QString label = backupLabel(serverId);
    if (label.isEmpty()) {
        return 0;
    }
    const QString prefix = BackupFiles::backupFilePrefix(label);
    int count = 0;
    for (const QFileInfo &info : QDir(BackupFiles::backupDir(BackupFiles::Kind::World))
                                     .entryInfoList({QStringLiteral("*.zip")}, QDir::Files)) {
        count += info.fileName().startsWith(prefix, Qt::CaseInsensitive) ? 1 : 0;
    }
    return count;
}

qint64 ServerSettingsController::directoryBytes(const QString &dirPath) {
    if (!QFileInfo(dirPath).isDir()) {
        return 0;
    }
    qint64 total = 0;
    QDirIterator it(dirPath, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

ServerCheck::Facts ServerSettingsController::checkFacts(
    const QString &serverId, ServerPortCheck::State portState) const {
    ServerCheck::Facts facts;
    if (serverId.isEmpty()) {
        return facts;
    }
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    facts.installed = entry.installed;
    facts.installedNote = entry.installed ? QString() : entry.note;
    const ServerModel::Info info = ServerRegistry::readMeta(serverId);
    facts.metaKnown = !info.isEmpty();
    QString launchProblem;
    const ServerInstall::Plan plan = ServerWorkspaceController::launchPlan(serverId,
                                                                           &launchProblem);
    facts.launchable = plan.ok;
    facts.launchProblem = launchProblem;
    facts.eulaAccepted = ServerRegistry::readEula(serverId);
    const ServerRun::ServerProcess *process =
        m_workspace == nullptr ? nullptr : m_workspace->process(serverId);
    facts.running = process != nullptr && process->isRunning();
    const ServerModel::Properties properties = ServerRegistry::readProperties(serverId);
    facts.port = ServerModel::serverPortOf(properties);
    facts.portState = facts.port > 0 && !facts.running ? portState
                                                       : ServerPortCheck::State::Unknown;
    facts.worldName = worldName(serverId);
    const QString dir = worldDir(serverId);
    facts.worldExists = !dir.isEmpty() && QFileInfo(dir).isDir();
    facts.worldBytes = facts.worldExists ? directoryBytes(dir) : 0;
    facts.modCount = ServerWorkspaceController::countEnabledMods(entry.dirPath);
    facts.worldBackupCount = worldBackupCount(serverId);
    facts.valueProblems = ServerPropertiesForm::problemsIn(properties);
    facts.duplicateKeys = ServerPropertiesForm::duplicateKeys(properties);
    facts.unknownKeys = ServerPropertiesForm::unknownKeys(properties);
    return facts;
}

QString ServerSettingsController::playersPath(const QString &serverId,
                                              ServerPlayers::List list) const {
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    if (entry.dirPath.isEmpty()) return QString();
    return entry.dirPath + QLatin1Char('/') + ServerPlayers::fileName(list);
}

ServerPlayers::Content ServerSettingsController::readPlayers(const QString &serverId,
                                                             ServerPlayers::List list) const {
    const QString path = playersPath(serverId, list);
    if (path.isEmpty()) {
        ServerPlayers::Content content;
        content.ok = false;
        content.problem = QStringLiteral("找不到这个服务端的目录。");
        return content;
    }
    QFile file(path);
    if (!file.exists()) {
        // 文件不在 = 空名单（服务端还没跑过、或者从来没加过）。不是错误。
        return ServerPlayers::Content();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        ServerPlayers::Content content;
        content.ok = false;
        content.problem = QStringLiteral("%1 打不开：%2")
                              .arg(ServerPlayers::fileName(list), file.errorString());
        return content;
    }
    return ServerPlayers::parse(list, file.readAll());
}

bool ServerSettingsController::writePlayers(const QString &serverId, ServerPlayers::List list,
                                            const QList<ServerPlayers::Entry> &entries,
                                            QString *problemOut) const {
    if (!ServerWriteGuard::acquireFile(serverId, ServerPlayers::fileName(list), problemOut)) return false;
    const QString path = playersPath(serverId, list);
    if (path.isEmpty()) {
        if (problemOut != nullptr) *problemOut = QStringLiteral("找不到这个服务端的目录。");
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (problemOut != nullptr) {
            *problemOut = QStringLiteral("%1 写不进去：%2")
                              .arg(ServerPlayers::fileName(list), file.errorString());
        }
        return false;
    }
    const auto bytes = ServerPlayers::serialize(list, entries);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        if (problemOut) *problemOut = QStringLiteral("玩家名单写入不完整，原文件未替换。");
        return false;
    }
    if (!file.commit()) {
        if (problemOut != nullptr) {
            *problemOut = QStringLiteral("%1 写不进去：%2")
                              .arg(ServerPlayers::fileName(list), file.errorString());
        }
        return false;
    }
    return true;
}
