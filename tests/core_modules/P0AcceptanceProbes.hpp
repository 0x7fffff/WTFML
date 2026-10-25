#pragma once
// 手工进程验收入口：只由传入全新隔离目录的脚本调用，不进入默认核心回归。
#include "manage/BackupFiles.hpp"
#include "manage/ModpackCompletion.hpp"
#include "manage/ModpackInstallReport.hpp"
#include "manage/WorldFiles.hpp"
#include "presets/GenericFiles.hpp"
#include "operations/TransferApply.hpp"
#include <QSaveFile>
#include <QJsonDocument>
#include <QThread>
#include <cstdio>
#include <cstdlib>

inline bool p0Put(const QString &path, const QByteArray &bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

inline void p0Hold(const char *stage) {
    std::printf("P0_HOLD %s\n", stage);
    std::fflush(stdout);
    QThread::sleep(240); // 父进程应在看到 marker 后立即终止；失联也不会永远挂住。
    std::fprintf(stderr, "P0_HOLD_TIMEOUT %s\n", stage);
    std::_Exit(87); // 父进程失联时不继续提交测试事务，保留恢复现场。
}

inline int runP0AcceptanceProbe(const QStringList &args) {
    if (args.size() < 3 || !args.at(1).startsWith("--p0-")) return -1;
    const QString root = QDir::cleanPath(args.at(2));
    if (!QDir::isAbsolutePath(root) || root == QDir(root).rootPath()) return 2;
    const QString command = args.at(1);
    const QString sentinel = root + "/p0-fixture.json";
    if (command == "--p0-prepare") {
        if (QFileInfo::exists(root) && !QDir(root).entryList(
                QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty()) return 2;
        if (!p0Put(sentinel, "{\"fixture\":1}")) return 2;
    } else {
        QFile guard(sentinel);
        if (!guard.open(QIODevice::ReadOnly) || guard.readAll() != "{\"fixture\":1}") return 2;
    }
    QCoreApplication::setOrganizationName("WTFML-P0-Acceptance");
    QCoreApplication::setApplicationName("WTFML-P0-Acceptance");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + "/settings");
    AppPaths::setMinecraftDir(root);
    DirectorySessionLock::enabled = true;
    const QString target = root + "/versions/RestoreTest";
    const QString instance = root + "/versions/CompletionTest";
    QString error;
    if (command == "--p0-prepare") {
        if (!p0Put(target + "/level.dat", "BACKUP") || !p0Put(target + "/config/user.txt", "USER")) return 2;
        BackupFiles::Manifest manifest;
        manifest.kind = BackupFiles::Kind::Instance;
        manifest.root = "RestoreTest"; manifest.label = "RestoreTest";
        manifest.sourceInstanceId = "RestoreTest"; manifest.sourceInstanceName = "RestoreTest";
        manifest.complete = true; manifest.createdAt = QDateTime::currentDateTime();
        const auto backup = BackupFiles::writeBackup(manifest, target, root + "/restore.zip");
        if (!backup.ok || !p0Put(target + "/level.dat", "CURRENT")) {
            std::fprintf(stderr, "PREPARE %s\n", qPrintable(backup.problem)); return 3;
        }
        const QString fingerprint(64, QLatin1Char('a'));
        if (!p0Put(instance + "/CompletionTest.jar", "CLIENT") ||
            !p0Put(instance + "/CompletionTest.json", "{\"id\":\"CompletionTest\"}") ||
            !p0Put(instance + "/config/user.txt", "USER") ||
            !p0Put(instance + "/wtfml.meta", "{\"jvmArgs\":[\"-Dkeep=user\"]}") ||
            !p0Put(instance + "/wtfml.importing", QJsonDocument(QJsonObject{
                {"manifestSha256", fingerprint}}).toJson())) return 3;
        ModpackImport::Pack pack;
        pack.name = "P0"; pack.gameVersion = "1.99.77";
        if (!ModpackCompletion::capture(instance, root, pack, fingerprint, {}, &error, {})) {
            std::fprintf(stderr, "CAPTURE %s\n", qPrintable(error)); return 3;
        }
        return 0;
    }
    if (command == "--p0-restore" || command == "--p0-restore-hold" ||
        command == "--p0-restore-collision") {
        const QString stage = args.value(3);
        const auto result = BackupFiles::restoreBackup(root + "/restore.zip", root + "/versions", {},
            [&](BackupFiles::RestorePhase phase) {
                if (command == "--p0-restore-hold") {
                    if (stage == "retained" && phase == BackupFiles::RestorePhase::OriginalRetained) p0Hold("restore-retained");
                    if (stage == "published" && phase == BackupFiles::RestorePhase::Published) p0Hold("restore-published");
                }
                if (command == "--p0-restore-collision" && phase == BackupFiles::RestorePhase::OriginalRetained)
                    p0Put(target + "/external.txt", "EXTERNAL");
            });
        const QJsonObject record{{"ok", result.ok}, {"recoveryFailed", result.recoveryFailed},
            {"problem", result.problem}, {"retainedPath", result.retainedPath}};
        p0Put(root + "/restore-result.json", QJsonDocument(record).toJson());
        return result.ok ? 0 : 4;
    }
    if (command == "--p0-recover") {
        const auto messages = BackupFiles::recoverInterruptedRestores(root);
        p0Put(root + "/recovery-result.json", QJsonDocument(QJsonObject{
            {"messages", QJsonArray::fromStringList(messages)}}).toJson());
        return 0;
    }
    if (command == "--p0-prepare-writes") {
        // 共享准备入口之后才允许建立这一组权限夹具；每一路的目标互不重叠。
        NbtTree::Document world;
        world.root.type = NbtTree::Type::Compound;
        const auto bytes = NbtTree::serialize(world);
        const bool ok = p0Put(root + "/world/level.dat", bytes) &&
            p0Put(root + "/world-source/level.dat", bytes) &&
            p0Put(root + "/preset-source/config/test.txt", "PRESET") &&
            p0Put(root + "/preset-target/config/test.txt", "ORIGINAL") &&
            p0Put(root + "/migration-source/mods/test.jar", "MOD") &&
            p0Put(root + "/delete-target/keep.txt", "KEEP") &&
            QDir().mkpath(root + "/migration-target/mods") &&
            QDir().mkpath(root + "/snapshot-target") &&
            QDir().mkpath(root + "/copy-target/versions") &&
            QDir().mkpath(root + "/versions/ImportTarget/saves");
        return ok ? 0 : 3;
    }
    if (command == "--p0-write-attempt") {
        const QString operation = args.value(3);
        bool ok = false;
        if (operation == "world") {
            WorldFiles::LevelData data;
            if (WorldFiles::readLevelData(root + "/world", &data, &error))
                ok = WorldFiles::writeLevelData(root + "/world", data, &error);
        } else if (operation == "import") {
            ok = WorldFiles::copyWorldInto(root + "/world-source", root + "/versions/ImportTarget/saves", "Imported", &error);
        } else if (operation == "copy") {
            VersionManager manager;
            ok = manager.copyInstanceFrom(instance, "CompletionTest", root + "/copy-target", "Copied", &error);
        } else if (operation == "snapshot") {
            BackupFiles::Manifest manifest;
            manifest.kind = BackupFiles::Kind::Instance; manifest.complete = true;
            manifest.root = "RestoreTest"; manifest.label = "RestoreTest";
            manifest.sourceInstanceId = "RestoreTest"; manifest.createdAt = QDateTime::currentDateTime();
            const auto result = BackupFiles::writeBackup(manifest, target, root + "/snapshot-target/snapshot.zip");
            ok = result.ok; error = result.problem;
        } else if (operation == "preset") {
            const auto result = Presets::applyFileGroup(root + "/preset-source", root + "/preset-target",
                Presets::FileGroup::Config, Presets::ApplyMode::Replace);
            ok = result.ok; error = result.problem;
        } else if (operation == "migration") {
            const auto result = TransferApply::transferMods(root + "/migration-source", root + "/migration-target",
                {root + "/migration-source/mods/test.jar"}, true);
            ok = result.failed == 0 && result.moved == 1; error = result.problems.join('\n');
        } else if (operation == "delete") {
            ok = removeTreeWithoutFollowingLinks(root + "/delete-target");
            if (!ok) error = "Deletion denied or incomplete";
        } else return 2;
        p0Put(root + "/write-result.json", QJsonDocument(QJsonObject{
            {"operation", operation}, {"ok", ok}, {"problem", error}}).toJson());
        return ok ? 0 : 4;
    }
    if (command == "--p0-completion" || command == "--p0-completion-hold") {
        bool held = false;
        const auto stop = [&] {
            if (command != "--p0-completion-hold" || held) return false;
            QFile report(instance + "/wtfml.import-report.json");
            if (report.open(QIODevice::ReadOnly) &&
                QJsonDocument::fromJson(report.readAll()).object().value("complete").toBool()) {
                held = true;
                p0Hold("completion-report");
            }
            return false;
        };
        const bool ok = ModpackCompletion::finish(instance, &error, stop);
        p0Put(root + "/completion-result.json", QJsonDocument(QJsonObject{{"ok", ok}, {"problem", error},
            {"unfinished", InstanceFiles::isUnfinishedImport("CompletionTest")}}).toJson());
        return ok ? 0 : 4;
    }
    return 2;
}
