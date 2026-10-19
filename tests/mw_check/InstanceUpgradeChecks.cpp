#include "InstanceUpgradeChecks.hpp"
#include "operations/InstanceUpgrade.hpp"
#include "manage/BackupFiles.hpp"
#include "InstanceSettingsDialog.hpp"
#include "controllers/InstanceTransferController.hpp"
#include "manage/VersionManager.hpp"
#include "launcher/LauncherManager.hpp"
#include "OverlayHost.hpp"
#include "InlineMessage.hpp"
#include "util/AppPaths.hpp"
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QSet>
#include <QPushButton>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

namespace {
bool write(const QString &path, const QByteArray &bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QByteArray read(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}
int runInstanceUpgradeChecks() {
    int failures = 0;
    const auto check = [&](bool ok, const char *what) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++failures;
    };
    QTemporaryDir scratch;
    if (!scratch.isValid()) return 1;
    {
        InstanceSettingsDialog page("Test", "Instance", InstanceSettingsDialog::Override(),
                                   InstanceSettingsDialog::GlobalValues(), {}, true);
        page.setTransferTargets("upgrade", {{"Target", "target-id"}});
        bool upgrade = false, rollback = false;
        QObject::connect(&page, &InstanceSettingsDialog::upgradeInPlaceRequested, &page,
                         [&](const QString &id) { upgrade = id == "target-id"; });
        QObject::connect(&page, &InstanceSettingsDialog::rollbackUpgradeRequested, &page,
                         [&] { rollback = true; });
        auto *apply = page.findChild<QPushButton *>("instanceUpgradeInPlaceButton");
        auto *restore = page.findChild<QPushButton *>("instanceUpgradeRollbackButton");
        check(apply && apply->isEnabled() && restore, "upgrade controls exist without migration selection");
        if (apply) apply->click();
        if (restore) restore->click();
        check(upgrade && rollback, "in-place upgrade and rollback emit distinct actions");
    }
    {
        InstanceUpgrade::Request r{scratch.path() + "/prepare", "Instance", "prepare-core", "Original"};
        const QString old = r.gameDir + "/versions/Instance";
        check(write(old + "/Instance.json", R"({"id":"Instance"})")
              && write(old + "/Instance.jar", "old-core")
              && write(old + "/wtfml.meta", R"({"instanceId":"stable-id","name":"User name","customSetting":42})")
              && write(old + "/config/custom.cfg", "user-settings")
              && write(old + "/screenshots/picture.png", "picture")
              && write(old + "/saves/World/level.dat", "world")
              && write(old + "/mods/old-mod.jar", "mod")
              && write(r.gameDir + "/versions/Target/Target.jar", "new-core")
              && write(r.gameDir + "/versions/Target/saves/Unwanted/level.dat", "must-not-copy"),
              "prepare full preservation fixture");
        InstanceUpgrade::Core core;
        core.clientJar = r.gameDir + "/versions/Target/Target.jar";
        core.gameVersion = "1.21.1";
        core.loaderName = "Fabric";
        core.loaderVersion = "0.16.0";
        core.mergedJson = QJsonObject{{"id", "Target"}, {"inheritsFrom", "Instance"}, {"jar", "Instance"},
            {"mainClass", "ExampleMain"}, {"downloads", QJsonObject{{"client", QJsonObject{{"sha1",
                QString::fromLatin1(QCryptographicHash::hash("new-core", QCryptographicHash::Sha1).toHex())}}}}}};
        const auto result = InstanceUpgrade::prepareAndCommit(r, core);
        check(result.ok && result.disabledMods == 1, "prepare and commit disables unverified old mods");
        const auto meta = QJsonDocument::fromJson(read(old + "/wtfml.meta")).object();
        const auto json = QJsonDocument::fromJson(read(old + "/Instance.json")).object();
        check(meta.value("instanceId") == "stable-id" && meta.value("name") == "User name"
              && meta.value("customSetting").toInt() == 42 && meta.value("realVersion") == "1.21.1",
              "stable identity and user settings survive core replacement");
        check(json.value("id") == "Instance" && !json.contains("inheritsFrom") && !json.contains("jar"),
              "flattened target profile cannot inherit through replaced original id");
        check(read(old + "/config/custom.cfg") == "user-settings" && read(old + "/screenshots/picture.png") == "picture"
              && read(old + "/saves/World/level.dat") == "world" && read(old + "/mods/old-mod.jar.disabled") == "mod"
              && !QFileInfo::exists(old + "/saves/Unwanted"),
              "all original user content stays and target user content is not imported");
    }
    const auto fixture = [&](const QString &name) {
        InstanceUpgrade::Request r{scratch.path() + '/' + name, "Instance", "tx-test", "Display name"};
        const QString old = r.gameDir + "/versions/Instance";
        check(write(old + "/Instance.json", R"({"id":"Instance"})")
              && write(old + "/Instance.jar", "old-core")
              && write(old + "/assets/asset", "asset-bytes")
              && write(old + "/libraries/library.jar", "library-bytes")
              && write(old + "/saves/World/level.dat", "world-bytes")
              && QDir().mkpath(old + "/empty-directory"), "prepare complete original fixture");
        const QString candidate = InstanceUpgrade::candidateDir(r);
        check(write(candidate + "/Instance.json", R"({"id":"Instance","target":"new"})")
              && write(candidate + "/Instance.jar", "new-core")
              && write(candidate + "/saves/World/level.dat", "world-bytes"), "prepare upgrade candidate");
        return r;
    };
    {
        const QString game = scratch.path() + "/controller";
        const QString source = game + "/versions/Instance";
        check(write(source + "/Instance.json", R"({"id":"Instance","mainClass":"OldMain"})")
              && write(source + "/Instance.jar", "old-core")
              && write(source + "/wtfml.meta", R"({"realVersion":"1.20.1","instanceId":"controller-stable"})")
              && write(source + "/mods/old.jar", "old-mod")
              && write(game + "/versions/Target/Target.json", R"({"id":"Target","mainClass":"NewMain"})")
              && write(game + "/versions/Target/Target.jar", "new-core")
              && write(game + "/versions/Target/wtfml.meta", R"({"realVersion":"1.21.1"})"),
              "prepare controller integration fixture");
        const QString previousRoot = AppPaths::minecraftDir();
        AppPaths::setMinecraftDir(game);
        QWidget anchor;
        QWidget home(&anchor);
        OverlayHost overlay(&anchor, &home);
        VersionManager versions;
        LauncherManager launcher;
        QSet<QString> locks;
        int started = 0, refreshed = 0;
        bool succeeded = false, lockReleasedBeforeFinish = false;
        InstanceTransferController::Host host;
        host.displayName = [](const QString &id) { return id; };
        host.isBeingModified = [&](const QString &id) { return locks.contains(id); };
        host.setFileOperationBusy = [&](const QString &id, bool busy) {
            if (busy) locks.insert(id); else locks.remove(id);
        };
        host.beginTask = [&](Tasks::TaskKind, const QString &, const QString &, const QList<Tasks::TaskStep> &) {
            return Tasks::TaskId(++started);
        };
        host.refreshInstances = [&] { ++refreshed; };
        host.finishTask = [&](Tasks::TaskId, bool ok, const QString &) {
            succeeded = ok;
            lockReleasedBeforeFinish = locks.isEmpty();
        };
        InstanceTransferController controller(&versions, &launcher, &overlay, host);
        QTimer answers;
        bool switchRoot = false;
        QObject::connect(&answers, &QTimer::timeout, &overlay, [&] {
            if (auto *box = overlay.findChild<InlineMessageBox *>()) {
                if (switchRoot) {
                    AppPaths::setMinecraftDir(scratch.path() + "/other-game");
                    switchRoot = false;
                }
                if (auto *button = box->findChild<QPushButton *>(InlineMessageBox::buttonName(0))) button->click();
            }
        });
        answers.start(10);
        QEventLoop wait;
        QTimer deadline;
        deadline.setSingleShot(true);
        bool timedOut = false, done = false, reload = false;
        QObject::connect(&deadline, &QTimer::timeout, &wait, [&] {
            timedOut = true;
            if (auto *box = overlay.findChild<InlineMessageBox *>()) box->reject();
            wait.quit();
        });
        const auto completed = [&](bool changed, const QString &) {
            reload = changed; done = true; wait.quit();
        };
        deadline.start(30000);
        controller.upgradeInPlace("Instance", "Target", completed);
        check(controller.copying() && locks.contains("Instance") && locks.contains("Target"),
              "controller holds source and target locks during background work");
        if (!done) wait.exec();
        deadline.stop();
        check(!timedOut && done && reload && succeeded && started == 1 && refreshed == 1
              && lockReleasedBeforeFinish && !controller.copying(),
              "controller completes task, releases locks, refreshes, and requests stale-page reload");
        check(read(source + "/Instance.jar") == "new-core" && read(source + "/mods/old.jar.disabled") == "old-mod",
              "controller upgrade reaches real disk transaction");
        done = false; reload = false;
        deadline.start(30000);
        controller.rollbackUpgrade("Instance", completed);
        if (!done) wait.exec();
        deadline.stop();
        check(!timedOut && done && reload && succeeded && started == 2 && refreshed == 2 && locks.isEmpty()
              && read(source + "/Instance.jar") == "old-core" && read(source + "/mods/old.jar") == "old-mod",
              "controller rollback restores real original files and lock lifecycle");
        done = false; reload = false; switchRoot = true;
        deadline.start(30000);
        controller.upgradeInPlace("Instance", "Target", completed);
        deadline.stop();
        check(!timedOut && done && !reload && started == 2 && locks.isEmpty()
              && read(source + "/Instance.jar") == "old-core",
              "changing game root inside confirmation aborts without writing either instance");
        AppPaths::setMinecraftDir(game);
        check(write(game + "/versions/Child/Child.json", R"({"id":"Child","inheritsFrom":"Instance"})"),
              "prepare dependent loader profile");
        done = false; reload = false;
        deadline.start(30000);
        controller.upgradeInPlace("Instance", "Target", completed);
        deadline.stop();
        check(done && !reload && started == 2 && read(source + "/Instance.jar") == "old-core",
              "controller refuses replacing a core referenced by another profile");
        check(write(game + "/versions/1.20.1/1.20.1.json", R"({"id":"1.20.1","mainClass":"OldMain"})")
              && write(game + "/versions/1.20.1/1.20.1.jar", "base-core")
              && write(game + "/versions/1.20.1/wtfml.meta", R"({"realVersion":"1.20.1"})"),
              "prepare shared vanilla base fixture");
        done = false; reload = false;
        deadline.start(30000);
        controller.upgradeInPlace("1.20.1", "Target", completed);
        deadline.stop();
        check(done && !reload && started == 2 && read(game + "/versions/1.20.1/1.20.1.jar") == "base-core",
              "canonical vanilla directory cannot be repurposed as a different core");
        answers.stop();
        AppPaths::setMinecraftDir(previousRoot);
    }
    {
        const auto r = fixture("success");
        const auto result = InstanceUpgrade::commitPrepared(r);
        check(result.ok && result.rollbackAvailable && !result.rolledBack, "commit retains rollback material");
        check(read(r.gameDir + "/versions/Instance/Instance.jar") == "new-core", "new core is published");
        check(QFileInfo::exists(InstanceUpgrade::transactionDir(r) + "/original/empty-directory"),
              "retained original preserves empty directories too");
        bool ok = false; QString problem;
        const auto manifest = BackupFiles::readManifest(result.snapshotZip, &ok, &problem);
        bool assets = false, libraries = false;
        for (const auto &file : manifest.files) {
            assets |= file.path == "assets/asset";
            libraries |= file.path == "libraries/library.jar";
        }
        check(ok && manifest.complete && manifest.excluded.isEmpty() && assets && libraries,
              "verified safety snapshot includes assets and libraries");
        check(QFileInfo(result.snapshotZip).fileName().startsWith(BackupFiles::backupFilePrefix(r.instanceId))
              && manifest.sourceInstanceName == r.label,
              "safety snapshot is discoverable by existing restore UI after display name changes");
        check(InstanceUpgrade::recoverableTransactions(r.gameDir, r.instanceId).contains(r.transactionId),
              "committed transaction can be discovered after restart");
        const auto restored = InstanceUpgrade::rollback(r);
        check(restored.ok && restored.rolledBack && !restored.rollbackAvailable, "explicit rollback succeeds");
        check(read(r.gameDir + "/versions/Instance/Instance.jar") == "old-core"
              && read(InstanceUpgrade::transactionDir(r) + "/replaced/Instance.jar") == "new-core",
              "rollback restores original and preserves replaced version");
        check(InstanceUpgrade::recoverableTransactions(r.gameDir, r.instanceId).isEmpty()
              && !InstanceUpgrade::rollback(r).ok, "completed rollback is not offered twice");
    }
    {
        const auto r = fixture("tampered-retained");
        const auto committed = InstanceUpgrade::commitPrepared(r);
        check(committed.ok && write(InstanceUpgrade::transactionDir(r) + "/original/Instance.jar", "bad-core"),
              "alter retained original after successful commit");
        const auto restored = InstanceUpgrade::rollback(r);
        check(!restored.ok && !restored.rolledBack
              && read(r.gameDir + "/versions/Instance/Instance.jar") == "new-core",
              "tampered retained directory cannot replace current version during rollback");
    }
    {
        auto r = fixture("space");
        r.spaceBudgetBytes = 0;
        const auto result = InstanceUpgrade::commitPrepared(r);
        check(!result.ok && result.snapshotZip.isEmpty()
              && read(r.gameDir + "/versions/Instance/Instance.jar") == "old-core",
              "insufficient space stops before snapshot and replacement");
        check(!QFileInfo::exists(InstanceUpgrade::transactionDir(r)),
              "space rejection removes the staged copy instead of leaking it");
    }
    {
        InstanceUpgrade::Request r{scratch.path() + "/prepare-failure", "Instance", "tx-cleanup", "Original"};
        const QString old = r.gameDir + "/versions/Instance";
        const QString target = r.gameDir + "/versions/Target";
        check(write(old + "/Instance.json", R"({"id":"Instance"})")
              && write(old + "/Instance.jar", "old-core")
              && write(old + "/saves/World/level.dat", "world")
              && write(old + "/wtfml.meta", "{not json")
              && write(target + "/Target.jar", "new-core"),
              "prepare failed-upgrade fixture");
        InstanceUpgrade::Core core;
        core.clientJar = target + "/Target.jar";
        core.gameVersion = "1.21.1";
        core.mergedJson = QJsonObject{{"mainClass", "ExampleMain"}};
        const auto result = InstanceUpgrade::prepareAndCommit(r, core);
        check(!result.ok && read(old + "/Instance.jar") == "old-core"
              && read(old + "/saves/World/level.dat") == "world",
              "unreadable original settings abort the upgrade");
        check(!QFileInfo::exists(InstanceUpgrade::transactionDir(r)),
              "failed preparation does not leave an instance-sized staging copy");
    }
    {
        const auto r = fixture("orphan-prepared");
        bool interrupted = false;
        try {
            InstanceUpgrade::commitPrepared(r, [&](InstanceUpgrade::Stage stage) {
                if (stage == InstanceUpgrade::Stage::RetainOriginal) throw std::runtime_error("simulated crash");
            });
        } catch (const std::runtime_error &) { interrupted = true; }
        check(interrupted && QFileInfo::exists(InstanceUpgrade::transactionDir(r) + "/prepared"),
              "crash fixture leaves an unpublished staging copy");
        const auto messages = InstanceUpgrade::recoverInterrupted(r.gameDir);
        check(!messages.isEmpty() && !QFileInfo::exists(InstanceUpgrade::transactionDir(r))
              && read(r.gameDir + "/versions/Instance/Instance.jar") == "old-core",
              "startup recovery removes staging that never retained the original");
    }
    {
        const auto r = fixture("interrupted");
        bool interrupted = false;
        try {
            InstanceUpgrade::commitPrepared(r, [&](InstanceUpgrade::Stage stage) {
                if (stage == InstanceUpgrade::Stage::Publish) throw std::runtime_error("simulated crash");
            });
        } catch (const std::runtime_error &) { interrupted = true; }
        check(interrupted && !QFileInfo::exists(r.gameDir + "/versions/Instance"),
              "crash fixture stops between original retention and publish");
        const auto messages = InstanceUpgrade::recoverInterrupted(r.gameDir);
        check(!messages.isEmpty() && read(r.gameDir + "/versions/Instance/Instance.jar") == "old-core",
              "persisted journal recovers interrupted upgrade without in-memory state");
        check(InstanceUpgrade::recoverInterrupted(r.gameDir).isEmpty(), "startup recovery is idempotent");
    }
    {
        auto r = fixture("invalid");
        r.transactionId = "../escape";
        check(!InstanceUpgrade::commitPrepared(r).ok
              && read(r.gameDir + "/versions/Instance/Instance.jar") == "old-core",
              "path traversal is rejected before touching original");
    }
    {
        const auto r = fixture("wrong-core");
        check(write(InstanceUpgrade::candidateDir(r) + "/Instance.json", R"({"id":"Other"})"), "alter candidate id");
        check(!InstanceUpgrade::commitPrepared(r).ok
              && read(r.gameDir + "/versions/Instance/Instance.jar") == "old-core",
              "mismatched core identity is rejected");
    }
    {
        const auto r = fixture("corrupt-snapshot");
        const auto result = InstanceUpgrade::commitPrepared(r, [&](InstanceUpgrade::Stage stage) {
            if (stage == InstanceUpgrade::Stage::VerifySnapshot) {
                const QDir dir(r.gameDir + "/cache/backups/instances");
                const auto zips = dir.entryList({"*.zip"}, QDir::Files);
                if (!zips.isEmpty()) check(write(dir.filePath(zips.first()), "broken"), "inject corrupt snapshot");
            }
        });
        check(!result.ok && !result.rollbackAvailable
              && read(r.gameDir + "/versions/Instance/Instance.jar") == "old-core",
              "snapshot corruption stops replacement");
    }
    {
        const auto r = fixture("cancel-before-publish");
        bool cancelled = false;
        const auto result = InstanceUpgrade::commitPrepared(r, [&](InstanceUpgrade::Stage stage) {
            if (stage == InstanceUpgrade::Stage::Publish) cancelled = true;
        }, [&] { return cancelled; });
        check(!result.ok && result.rolledBack
              && read(r.gameDir + "/versions/Instance/Instance.jar") == "old-core",
              "interruption after retaining original automatically rolls back");
    }
    {
        const auto r = fixture("corrupt-published");
        const auto result = InstanceUpgrade::commitPrepared(r, [&](InstanceUpgrade::Stage stage) {
            if (stage == InstanceUpgrade::Stage::Verify)
                check(write(r.gameDir + "/versions/Instance/Instance.jar", "bad-core"), "inject same-size corruption");
        });
        check(!result.ok && result.rolledBack
              && read(r.gameDir + "/versions/Instance/Instance.jar") == "old-core"
              && read(InstanceUpgrade::transactionDir(r) + "/replaced/Instance.jar") == "bad-core",
              "hash mismatch triggers rollback without deleting failed version");
    }
    return failures == 0 ? 0 : 1;
}
