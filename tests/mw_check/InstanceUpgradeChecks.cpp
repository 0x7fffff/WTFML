#include "InstanceUpgradeChecks.hpp"
#include "operations/InstanceUpgrade.hpp"
#include "operations/FabricVersionDeclaration.hpp"
#include "InstanceUpdateWizard.hpp"
#include <QAbstractButton>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QElapsedTimer>
#include <QRegularExpression>
#include "manage/BackupFiles.hpp"
#include "InstanceSettingsDialog.hpp"
#include "controllers/InstanceTransferController.hpp"
#include "controllers/FileOperationRecoveryController.hpp"
#include "operations/OperationReport.hpp"
#include "manage/VersionManager.hpp"
#include "launcher/LauncherManager.hpp"
#include "OverlayHost.hpp"
#include "InlineMessage.hpp"
#include "util/AppPaths.hpp"
#include "util/ZipUtil.hpp"
#include "util/DirectorySessionLock.hpp"
#include "controllers/UpgradeAssessment.hpp"
#include "manage/InstanceIconLookup.hpp"
#include "download/ResourceStore.hpp"
#include <QTcpServer>
#include <QTcpSocket>
#include <QComboBox>
#include <QTreeWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
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
#include <QLockFile>
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
    {
        QTemporaryDir root;
        check(root.isValid(), "Directory lock fixture created");
        if (root.isValid()) {
            QLockFile contender(QDir::temp().filePath(QStringLiteral("wtfml-directory-%1.lock")
                .arg(DirectorySessionLock::keyFor(root.path()))));
            contender.setStaleLockTime(0);
            const bool ownsLock = contender.tryLock(0);
            check(ownsLock, "Other launcher directory lock acquired");
            const bool wasEnabled = DirectorySessionLock::enabled;
            DirectorySessionLock::enabled = true;
            const InstanceUpgrade::Request request{root.path(), QStringLiteral("instance"),
                                                   QStringLiteral("transaction"), {}};
            const auto prepare = InstanceUpgrade::prepareAndCommit(request, {});
            const auto commit = InstanceUpgrade::commitPrepared(request);
            const auto rollback = InstanceUpgrade::rollback(request);
            const auto recover = InstanceUpgrade::recoverInterrupted(root.path());
            DirectorySessionLock::enabled = wasEnabled;
            check(ownsLock && !prepare.ok && !commit.ok && !rollback.ok
                      && prepare.problem.contains(QStringLiteral("占用"))
                      && commit.problem.contains(QStringLiteral("占用"))
                      && rollback.problem.contains(QStringLiteral("占用"))
                      && recover.size() == 1 && recover.first().contains(QStringLiteral("占用")),
                  "Upgrade prepare/commit/rollback/recovery reject another launcher's directory");
            check(QDir(root.path()).entryList(QDir::AllEntries | QDir::Hidden
                      | QDir::NoDotAndDotDot).isEmpty(),
                  "Directory lock rejection leaves no transaction or backup files");
        }
    }
    {
        using namespace FabricVersionDeclaration;
        check(predicate(QStringLiteral("~26.2-"), QStringLiteral("26.2")) == Match::Yes,
              "Fabric empty prerelease lower bound accepts the stable release");
        check(predicate(QStringLiteral("~26.2-"), QStringLiteral("26.3-alpha.1")) == Match::No,
              "Fabric tilde upper bound excludes the next minor prereleases");
        check(predicate(QStringLiteral(">=26.2 <26.3-alpha.3"), QStringLiteral("26.3-alpha.2")) == Match::Yes,
              "Fabric prerelease ordering and compound constraints");
        check(predicate(QStringLiteral("^0.2"), QStringLiteral("0.8")) == Match::Yes,
              "Fabric caret does not use the npm zero-major exception");
        check(predicate(QStringLiteral("26.2.x.x"), QStringLiteral("26.2.4+local")) == Match::Yes,
              "Fabric repeated trailing wildcard and build metadata");
        check(predicate(QStringLiteral(">=99999999999999999"), QStringLiteral("26.2")) == Match::Unknown,
              "overflow declarations remain unconfirmed");
        check(matches(QJsonArray{QStringLiteral("*"), 1}, QStringLiteral("26.2")) == Match::Unknown,
              "malformed OR arrays are not silently accepted");
        auto *target = new QWidget; auto *targetLayout = new QVBoxLayout(target);
        auto *combo = new QComboBox(target); combo->addItems({QStringLiteral("one"), QStringLiteral("two")});
        targetLayout->addWidget(combo);
        InstanceUpdateWizard wizard(target, new QWidget, new QWidget, new QWidget);
        wizard.show(); QApplication::processEvents();
        check(!wizard.button(QWizard::NextButton)->isEnabled(), "wizard blocks an incomplete target step");
        wizard.setStepReady(0, true); wizard.next();
        check(wizard.currentId() == 1 && !wizard.button(QWizard::NextButton)->isEnabled(),
              "wizard waits for mod review before confirmation");
        wizard.back();
        QWheelEvent wheel(QPointF(3, 3), QPointF(combo->mapToGlobal(QPoint(3, 3))), QPoint(), QPoint(0, -120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(combo, &wheel);
        check(combo->currentIndex() == 0, "closed wizard dropdown ignores wheel input");
        wizard.setStepReady(1, true); wizard.next(); wizard.next();
        check(wizard.currentId() == 2, "wizard preserves review readiness when going back");
        wizard.setStepReady(2, true); wizard.next();
        check(wizard.currentId() == 3 && !wizard.button(QWizard::FinishButton)->isEnabled(),
              "wizard cannot finish before the transaction has a result");
        wizard.setStepReady(3, true);
        check(wizard.button(QWizard::FinishButton)->isEnabled(), "wizard enables finish after a terminal result");
        QWidget background;
        OverlayHost outer(&background, &background);
        QDialog prompt;
        bool routed = false;
        QTimer::singleShot(0, &prompt, [&] {
            routed = wizard.promptHost()->promptOpen() && !outer.promptOpen(); prompt.accept();
        });
        check(outer.execPrompt(&prompt) == QDialog::Accepted && routed,
              "transaction questions route inside the modal wizard rather than the blocked main window");
        QDialog closePrompt;
        QTimer::singleShot(0, &wizard, [&] { wizard.close(); });
        check(outer.execPrompt(&closePrompt) == QDialog::Rejected && wizard.isVisible(),
              "closing during a question dismisses the question without hiding the wizard or hanging");
    }
    const QString inspect = qEnvironmentVariable("WTFML_UPGRADE_INSPECT_MODS");
    if (!inspect.isEmpty()) {
        QStringList jars;
        for (const auto &file : QDir(inspect).entryInfoList({QStringLiteral("*.jar")}, QDir::Files | QDir::NoSymLinks))
            jars.append(file.absoluteFilePath());
        QElapsedTimer clock; clock.start(); QString problem;
        const auto metadata = ZipUtil::readMetadataBatch(jars, {QStringLiteral("fabric.mod.json"), QStringLiteral("META-INF/mods.toml"), QStringLiteral("META-INF/neoforge.mods.toml")}, &problem);
        int fabric = 0, declared = 0, supported = 0, unknown = 0;
        for (const auto &entry : metadata) {
            if (!entry.entries.contains(QStringLiteral("fabric.mod.json"))) continue;
            ++fabric;
            const auto rule = QJsonDocument::fromJson(entry.entries.value(QStringLiteral("fabric.mod.json"))).object()
                .value(QStringLiteral("depends")).toObject().value(QStringLiteral("minecraft"));
            if (rule.isUndefined()) continue;
            ++declared;
            const auto match = FabricVersionDeclaration::matches(rule, QStringLiteral("26.2"));
            supported += match == FabricVersionDeclaration::Match::Yes;
            unknown += match == FabricVersionDeclaration::Match::Unknown;
        }
        std::printf("READONLY METADATA jars=%lld returned=%lld fabric=%d declared=%d supports26.2=%d unknown=%d elapsedMs=%lld problem=%s\n",
            static_cast<long long>(jars.size()), static_cast<long long>(metadata.size()), fabric, declared, supported, unknown,
            static_cast<long long>(clock.elapsed()), problem.toUtf8().constData());
        check(problem.isEmpty() && metadata.size() == jars.size(), "readonly real-instance batch returns every requested archive");
    }
    QTemporaryDir scratch;
    if (!scratch.isValid()) return 1;
    {
        const QString game = scratch.filePath(QStringLiteral("assessment-game"));
        const QString source = game + QStringLiteral("/versions/Source");
        const QString jar = source + QStringLiteral("/mods/example.jar");
        const QString declaration = scratch.filePath(QStringLiteral("fabric-declaration.json"));
        const QString forgeDeclaration = scratch.filePath(QStringLiteral("mods.toml"));
        const QString neoDeclaration = scratch.filePath(QStringLiteral("neoforge.mods.toml"));
        check(write(forgeDeclaration, "[[dependencies.example]]\nmodId=\"minecraft\"\nmandatory=true\nversionRange=\"[1.21,1.22)\"\n") &&
              write(neoDeclaration, "[[dependencies.example]]\nmodId=\"minecraft\"\ntype=\"required\"\nversionRange=\"[1.20,1.21)\"\n"),
              "prepare Forge and NeoForge dependency declarations");
        check(write(declaration, R"({"schemaVersion":1,"id":"example","name":"Local Example","version":"1.0","depends":{"minecraft":">=1.20 <1.23"},"breaks":{"minecraft":"1.22.2"}})")
            && QDir().mkpath(source + QStringLiteral("/mods")) && ZipUtil::createZip(jar, {{QStringLiteral("fabric.mod.json"), declaration},
                {QStringLiteral("META-INF/mods.toml"), forgeDeclaration}, {QStringLiteral("META-INF/neoforge.mods.toml"), neoDeclaration}}),
            "create real JAR containing a local compatibility declaration");
        QByteArray bounded; QString boundedProblem;
        check(ZipUtil::readEntry(jar, QStringLiteral("fabric.mod.json"), &bounded, &boundedProblem, QStringLiteral("metadata fixture"), 8) == ZipUtil::EntryStatus::Failed
              && bounded.isEmpty() && !boundedProblem.isEmpty(), "oversized declarations are rejected before extraction");
        check(ZipUtil::readEntry(jar, QStringLiteral("fabric.mod.json"), &bounded, &boundedProblem, QStringLiteral("metadata fixture"), 4096) == ZipUtil::EntryStatus::Ok
              && bounded == read(declaration), "bounded metadata reader preserves the original bytes");
        const QString broken = scratch.filePath(QStringLiteral("broken.jar"));
        check(write(broken, "not a zip"), "prepare independent corrupt archive");
        QString batchProblem;
        const auto batch = ZipUtil::readMetadataBatch({broken, jar}, {QStringLiteral("fabric.mod.json"), QStringLiteral("META-INF/mods.toml")}, &batchProblem);
        check(!batch.value(broken).problem.isEmpty() && batch.value(jar).entries.value(QStringLiteral("fabric.mod.json")) == read(declaration)
              && batch.value(jar).entries.value(QStringLiteral("META-INF/mods.toml")) == read(forgeDeclaration),
              "batch metadata reads all loaders and isolates corrupt archives");
        check(write(source + QStringLiteral("/wtfml.meta"), R"({"realVersion":"1.20.1","loaderName":"Fabric"})") &&
              write(game + QStringLiteral("/versions/Target/wtfml.meta"), R"({"realVersion":"1.21.1","loaderName":"Fabric","loaderVersion":"0.16"})") &&
              write(game + QStringLiteral("/versions/Other/wtfml.meta"), R"({"realVersion":"1.22.1","loaderName":"Fabric","loaderVersion":"0.16"})") &&
              write(game + QStringLiteral("/versions/Excluded/wtfml.meta"), R"({"realVersion":"1.22.2","loaderName":"Fabric","loaderVersion":"0.16"})"),
              "prepare target metadata without modifying the source instance");
        check(write(game + QStringLiteral("/versions/ForgeTarget/wtfml.meta"), R"({"realVersion":"1.21.1","loaderName":"Forge"})") &&
              write(game + QStringLiteral("/versions/NeoTarget/wtfml.meta"), R"({"realVersion":"1.21.1","loaderName":"NeoForge"})"),
              "prepare targets for loader-specific declaration checks");
        InstanceIconLookup::Options options; options.cacheDir = scratch.filePath(QStringLiteral("identity-cache")); options.cacheKey = QStringLiteral("Source");
        ResourceCatalog::Hit hit; hit.projectId = QStringLiteral("known-project"); hit.title = QStringLiteral("Known Example");
        QJsonObject record{{"size", double(QFileInfo(jar).size())}, {"mtime", double(QFileInfo(jar).lastModified().toMSecsSinceEpoch())},
            {"sha1", QString::fromLatin1(QCryptographicHash::hash(read(jar), QCryptographicHash::Sha1).toHex())}, {"hit", ResourceCatalog::hitToJson(hit)}};
        check(write(InstanceIconLookup::cachePath(options), QJsonDocument(QJsonObject{{"files", QJsonObject{{jar, record}}}}).toJson()),
              "cache a platform identity verified against the actual JAR hash");
        QTcpServer server; bool failPlatform = false, fallbackMode = false, delayed = false;
        int inFlight = 0, maxInFlight = 0, fallbackLookups = 0;
        check(server.listen(QHostAddress::LocalHost, 0), "assessment platform fixture listens on loopback");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (auto *socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                    if (socket->property("replied").toBool()) return;
                    const QByteArray request = socket->property("request").toByteArray() + socket->readAll(); socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n")) return;
                    if (request.startsWith("POST ")) {
                        const auto header = request.left(request.indexOf("\r\n\r\n"));
                        const auto length = QRegularExpression(QStringLiteral("(?i)Content-Length: (\\d+)")).match(QString::fromLatin1(header));
                        if (length.hasMatch() && request.size() - header.size() - 4 < length.captured(1).toInt()) return;
                    }
                    socket->setProperty("replied", true);
                    QByteArray body = "[]"; int status = failPlatform ? 403 : 200;
                    if (fallbackMode && request.startsWith("GET /mods/")) status = 503;
                    if (fallbackMode && request.startsWith("POST /version_files")) {
                        ++fallbackLookups;
                        const QString sha = record.value("sha1").toString();
                        const QJsonObject file{{"filename", "example.jar"}, {"url", "https://example.invalid/file.jar"},
                            {"hashes", QJsonObject{{"sha1", sha}}}};
                        const QJsonObject version{{"id", "file-version"}, {"project_id", "fallback-project"},
                            {"version_number", "1.0"}, {"files", QJsonArray{file}}};
                        body = QJsonDocument(QJsonObject{{sha, version}}).toJson(QJsonDocument::Compact);
                    }
                    ++inFlight; maxInFlight = qMax(maxInFlight, inFlight);
                    const QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Type: application/json\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                    QTimer::singleShot(delayed ? 120 : 0, socket, [&, socket, response] {
                        --inFlight; socket->write(response); socket->disconnectFromHost();
                    });
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        const QString oldRoot = AppPaths::minecraftDir(); AppPaths::setMinecraftDir(game);
        {
            VersionManager versions;
            InstanceSettingsDialog page(QStringLiteral("Source"), QStringLiteral("Source"), {}, {}, {}, true);
            ResourceStore store;
            store.setModrinthApiBaseOverride(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
            page.setTransferTargets(QStringLiteral("upgrade"), {{QStringLiteral("Target"), QStringLiteral("Target")}, {QStringLiteral("Other"), QStringLiteral("Other")},
                {QStringLiteral("Excluded"), QStringLiteral("Excluded")}, {QStringLiteral("Forge"), QStringLiteral("ForgeTarget")}, {QStringLiteral("NeoForge"), QStringLiteral("NeoTarget")},
                {QStringLiteral("Unprepared"), QStringLiteral("@download:1.21.2")}});
            UpgradeAssessment::attach(&page, &versions, &store, QStringLiteral("Source"), source, options.cacheDir);
            auto *combo = page.findChild<QComboBox *>(InstanceSettingsDialog::transferTargetComboName(QStringLiteral("upgrade")));
            auto *tree = page.findChild<QTreeWidget *>(QStringLiteral("instanceUpgradeModChecks"));
            const auto waitFor = [&](const QString &text) {
                QEventLoop loop; QTimer poll, timeout;
                QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (tree->topLevelItemCount() && tree->topLevelItem(0)->text(1).contains(text)) loop.quit(); });
                timeout.setSingleShot(true); QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
                poll.start(10); timeout.start(30000); loop.exec();
                return tree->topLevelItemCount() && tree->topLevelItem(0)->text(1).contains(text);
            };
            combo->setCurrentIndex(1);
            check(!page.findChild<QPushButton *>(QStringLiteral("instanceUpgradeInPlaceButton"))->isEnabled(),
                  "in-place update waits for local archive reads to release their file handles");
            check(waitFor(QStringLiteral("平台未找到目标发行版")) && tree->topLevelItem(0)->text(1).contains(QStringLiteral("本地 Minecraft 声明符合")),
                  "known mod with no platform release still displays its compatible local declaration");
            failPlatform = true; combo->setCurrentIndex(2);
            check(waitFor(QStringLiteral("平台查询失败")) && tree->topLevelItem(0)->text(1).contains(QStringLiteral("本地 Minecraft 声明符合")),
                  "network failure is not reported as absent release and does not erase local support");
            combo->setCurrentIndex(3);
            check(waitFor(QStringLiteral("本地明确排除此版本")), "Fabric breaks.minecraft overrides a matching depends range");
            combo->setCurrentIndex(4);
            check(waitFor(QStringLiteral("本地 Minecraft 声明符合")), "Forge TOML dependency range supports the target version");
            combo->setCurrentIndex(5);
            check(waitFor(QStringLiteral("本地声明不支持")), "NeoForge TOML exclusive upper bound rejects the target version");
            failPlatform = false; combo->setCurrentIndex(6);
            check(waitFor(QStringLiteral("平台未找到目标发行版")) && tree->topLevelItem(0)->text(1).contains(QStringLiteral("本地 Minecraft 声明符合")),
                  "unprepared target is assessed using the explicitly stated current-loader assumption");
            check(write(InstanceIconLookup::cachePath(options), "{\"files\":{}}"), "remove only the fixture's platform identity");
            page.findChild<QPushButton *>(QStringLiteral("instanceUpgradeRecheck"))->click();
            check(waitFor(QStringLiteral("未识别平台项目")) && tree->topLevelItem(0)->text(1).contains(QStringLiteral("本地 Minecraft 声明符合")),
                  "unknown platform identity still retains the local compatibility declaration after recheck");
            const QJsonObject installed{{"filename", QStringLiteral("example.jar")}, {"sha1", record.value(QStringLiteral("sha1"))}, {"project", QStringLiteral("modrinth:known-project")}};
            check(write(source + QStringLiteral("/wtfml.preset-mod-install.json"), QJsonDocument(QJsonObject{
                {"format", QStringLiteral("wtfml-preset-mod-result/1")}, {"plannedFiles", QJsonArray{installed}}}).toJson()), "prepare a hash-verifiable preset install identity");
            page.findChild<QPushButton *>(QStringLiteral("instanceUpgradeRecheck"))->click();
            check(waitFor(QStringLiteral("平台未找到目标发行版")) && tree->topLevelItem(0)->text(1).contains(QStringLiteral("本地 Minecraft 声明符合")),
                  "identified preset-installed mods are assessed even without an icon lookup cache");
            hit.source = ResourceCatalog::Source::CurseForge;
            record.insert("hit", ResourceCatalog::hitToJson(hit));
            check(write(InstanceIconLookup::cachePath(options), QJsonDocument(QJsonObject{{"files", QJsonObject{{jar, record}}}}).toJson()),
                "prepare a CurseForge identity for outage fallback");
            fallbackMode = true;
            store.setCurseForgeApiBaseOverride(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
            page.findChild<QPushButton *>(QStringLiteral("instanceUpgradeRecheck"))->click();
            check(waitFor(QStringLiteral("平台未找到目标发行版")) && fallbackLookups == 1,
                "CurseForge 503 uses exact file hash identity before querying Modrinth fallback");
            fallbackMode = false; delayed = true; maxInFlight = 0;
            hit.source = ResourceCatalog::Source::Modrinth; record.insert("hit", ResourceCatalog::hitToJson(hit));
            QJsonObject records{{jar, record}};
            for (int i = 0; i < 5; ++i) {
                const QString copy = source + QStringLiteral("/mods/copy-%1.jar").arg(i);
                check(QFile::copy(jar, copy), "prepare a parallel-query fixture JAR");
                auto copyRecord = record; copyRecord.insert("mtime", double(QFileInfo(copy).lastModified().toMSecsSinceEpoch()));
                records.insert(copy, copyRecord);
            }
            check(write(InstanceIconLookup::cachePath(options), QJsonDocument(QJsonObject{{"files", records}}).toJson()), "cache six valid fixture identities");
            page.findChild<QPushButton *>(QStringLiteral("instanceUpgradeRecheck"))->click();
            QElapsedTimer parallelClock; parallelClock.start();
            while (parallelClock.elapsed() < 10000 && (page.property("upgradeAssessmentReading").toBool()
                || page.property("upgradeAssessmentQuerying").toBool() || tree->topLevelItemCount() != 6)) {
                QApplication::processEvents(QEventLoop::AllEvents, 10);
            }
            check(maxInFlight == 4 && !page.property("upgradeAssessmentQuerying").toBool(),
                "platform queries overlap with a hard four-request concurrency limit");
        }
        AppPaths::setMinecraftDir(oldRoot);
    }
    {
        const QString source = scratch.filePath("world-source");
        const QString saves = scratch.filePath("saves");
        check(write(source + "/level.dat", "world") && QDir().mkpath(saves), "prepare background world import");
        bool busy = false, completed = false, success = false, returned = false;
        bool callbackWasAsync = false;
        InstanceTransferController::Host host;
        host.isBeingModified = [&](const QString &) { return busy; };
        host.instanceDirForKey = [&](const QString &, const QString &) { return saves; };
        host.setFileOperationBusy = [&](const QString &, bool value) { busy = value; };
        InstanceTransferController controller(nullptr, nullptr, nullptr, host);
        QEventLoop loop;
        controller.importWorldInBackground("id", source, "Imported", [&](bool ok, const QString &problem) {
            callbackWasAsync = returned;
            completed = true;
            success = ok;
            loop.quit();
        });
        returned = true;
        check(busy && !completed, "background world import returns immediately and reserves instance");
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
        check(completed && success && callbackWasAsync && !busy,
              "background world import finishes asynchronously and releases reservation");
        check(read(saves + "/Imported/level.dat") == "world" && read(source + "/level.dat") == "world",
              "background world import publishes complete copy without altering source");
    }
    {
        // 后台导入世界的「取消」：任务中心那颗按钮的判据 + 协作式停机。
        // 源世界故意做一个 32 MiB 的 region —— 拷贝器每 1 MiB 看一次取消位，
        // 主线程发完请求立刻按下取消，不会出现"还没按就已经拷完"的竞态。
        const QString source = scratch.filePath("world-cancel-source");
        const QString saves = scratch.filePath("saves-cancel");
        check(write(source + "/level.dat", "world") && QDir().mkpath(saves)
                  && write(source + "/region/r.0.0.mca", QByteArray(32 * 1024 * 1024, 'x')),
              "prepare cancellable world import");
        bool busy = false, completed = false, success = true, markedCancelled = false;
        bool finishCalled = false;
        Tasks::TaskId importTask = Tasks::invalidTaskId();
        InstanceTransferController::Host host;
        host.isBeingModified = [&](const QString &) { return busy; };
        host.instanceDirForKey = [&](const QString &, const QString &) { return saves; };
        host.setFileOperationBusy = [&](const QString &, bool value) { busy = value; };
        host.beginTask = [&](Tasks::TaskKind, const QString &, const QString &,
                             const QList<Tasks::TaskStep> &) {
            importTask = 4242;
            return importTask;
        };
        host.finishTask = [&](Tasks::TaskId, bool, const QString &) { finishCalled = true; };
        host.markTaskCancelled = [&](Tasks::TaskId id) { markedCancelled = id == importTask; };
        QString recoveryToken;
        host.registerRecovery = [](const OperationReport::Report &report) {
            return !report.id.isEmpty() && !report.context.isEmpty();
        };
        // 恢复入口必须挂在**终态之后**：这里顺便钉住那个顺序。
        host.setTaskRecovery = [&](Tasks::TaskId id, const QString &token) {
            if (id == importTask) recoveryToken = token;
        };
        InstanceTransferController controller(nullptr, nullptr, nullptr, host);
        QEventLoop loop;
        controller.importWorldInBackground("id", source, "Cancelled", [&](bool ok, const QString &) {
            completed = true;
            success = ok;
            loop.quit();
        });
        check(controller.canCancelTask(importTask),
              "running background world import advertises a working cancel");
        check(!controller.canCancelTask(Tasks::invalidTaskId()),
              "invalid task id is never cancellable");
        check(controller.cancelTask(importTask), "background world import accepts cancellation");
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
        check(completed && !success && markedCancelled && !finishCalled && !busy,
              "cancelled world import ends as cancelled, not failed, and releases instance");
        check(!recoveryToken.isEmpty(),
              "★★ cancelled world import offers 「继续未完成操作」 after the task is terminal");
        check(!QFileInfo::exists(saves + "/Cancelled"),
              "cancelled world import publishes nothing");
        check(read(source + "/level.dat") == "world", "cancelled world import leaves source intact");
    }
    {
        // 后台删除世界：确认之后整棵树在工作线程里删，删完才回调。
        // 控制器只肯删「当前实例的 saves 直接子目录」，所以数据目录也摆成真实形状。
        const QString deleteRoot = scratch.filePath("game-delete");
        const QString previousDeleteRoot = AppPaths::minecraftDir();
        AppPaths::setMinecraftDir(deleteRoot);
        const QString world = AppPaths::versionDir(QStringLiteral("id")) + "/saves/world-delete";
        check(write(world + "/level.dat", "world")
                  && write(world + "/region/r.0.0.mca", QByteArray(2 * 1024 * 1024, 'y')),
              "prepare background world delete");
        bool busy = false, completed = false, success = false, returned = false;
        bool callbackWasAsync = false, taskFinished = false, finishSuccess = false;
        Tasks::TaskId deleteTask = Tasks::invalidTaskId();
        bool deleteCapabilitiesSeen = false, deleteCancelExposed = true;
        QString deleteBlockedReason;
        InstanceTransferController::Host host;
        host.isBeingModified = [&](const QString &) { return busy; };
        host.beginTask = [&](Tasks::TaskKind, const QString &, const QString &,
                             const QList<Tasks::TaskStep> &) {
            deleteTask = 4343;
            return deleteTask;
        };
        host.setTaskCapabilities = [&](Tasks::TaskId, const Tasks::TaskCapabilities &caps) {
            deleteCapabilitiesSeen = true;
            deleteCancelExposed = caps.canCancel;
            deleteBlockedReason = caps.cancelBlockedReason;
        };
        host.setFileOperationBusy = [&](const QString &, bool value) { busy = value; };
        host.finishTask = [&](Tasks::TaskId id, bool ok, const QString &) {
            taskFinished = id == deleteTask;
            finishSuccess = ok;
        };
        InstanceTransferController controller(nullptr, nullptr, nullptr, host);
        QEventLoop loop;
        controller.deleteWorldInBackground("id", world, [&](bool ok, const QString &) {
            callbackWasAsync = returned;
            completed = true;
            success = ok;
            loop.quit();
        });
        returned = true;
        check(busy && !completed,
              "background world delete returns immediately and reserves instance");
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
        check(completed && success && callbackWasAsync && !busy,
              "background world delete finishes asynchronously and releases reservation");
        check(!QFileInfo::exists(world), "background world delete removes the whole tree");
        check(taskFinished && finishSuccess,
              "background world delete closes its task as succeeded");
        check(deleteCapabilitiesSeen && !deleteCancelExposed && !deleteBlockedReason.isEmpty(),
              "world delete says why it has no cancel instead of only hiding the button");
        AppPaths::setMinecraftDir(previousDeleteRoot);
    }
    {
        // 删除的闸：实例正被别的操作占着时一个字都不许动盘，当场拒绝。
        const QString world = scratch.filePath("world-delete-busy");
        check(write(world + "/level.dat", "world"), "prepare rejected world delete");
        InstanceTransferController::Host host;
        host.isBeingModified = [&](const QString &) { return true; };
        host.displayName = [&](const QString &) { return QStringLiteral("Busy"); };
        bool began = false;
        host.beginTask = [&](Tasks::TaskKind, const QString &, const QString &,
                             const QList<Tasks::TaskStep> &) {
            began = true;
            return 4344;
        };
        InstanceTransferController controller(nullptr, nullptr, nullptr, host);
        bool called = false, ok = true;
        controller.deleteWorldInBackground("id", world, [&](bool value, const QString &) {
            called = true;
            ok = value;
        });
        check(called && !ok && !began,
              "busy instance refuses background world delete before touching disk");
        check(read(world + "/level.dat") == "world",
              "refused world delete leaves the world intact");
    }
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
        check(apply && !apply->isEnabled(), "upgrade requires an explicit target selection instead of silently choosing the first core");
        page.findChild<QComboBox *>(InstanceSettingsDialog::transferTargetComboName(QStringLiteral("upgrade")))->setCurrentIndex(1);
        check(apply && apply->isEnabled() && restore, "upgrade controls exist without migration selection");
        if (apply) apply->click();
        if (restore) restore->click();
        check(upgrade && rollback, "in-place upgrade and rollback emit distinct actions");
        page.setProperty("modernUpgradeFlow", true);
        auto *tree = page.findChild<QTreeWidget *>(QStringLiteral("instanceUpgradeModChecks"));
        tree->setColumnCount(4);
        page.setUpgradeModChecks({{"Compatible", "local compatible", "range", "keep.jar", "keep"},
                                  {"Unknown", "unknown", "", "unknown.jar", "review"}}, "review");
        check(page.upgradeKeptMods() == QStringList{"keep.jar"} && page.upgradePendingModCount() == 1,
              "review retains compatible files but requires an explicit decision for unknown files");
        auto *choice = qobject_cast<QComboBox *>(tree->itemWidget(tree->topLevelItem(1), 3));
        choice->setCurrentIndex(2);
        check(page.upgradePendingModCount() == 0, "explicit per-file choice clears the unresolved count");
        page.setUpgradeModChecks({{"Compatible", "query updated", "range", "keep.jar", "keep"},
                                  {"Unknown", "query failed", "", "unknown.jar", "disable"}}, "updated");
        check(page.upgradeKeptMods().contains("unknown.jar"), "platform updates do not discard explicit per-file decisions");
        page.setProperty("modernUpgradeWizard", true);
        const QList<QStringList> grouped{{"Compatible", "compatible", "range", "keep.jar", "keep", "compatible"},
            {"Needs review", "query failed", "", "review.jar", "review", ""}};
        page.setUpgradeModChecks(grouped, "grouped");
        auto *group = tree->topLevelItem(1);
        check(tree->topLevelItemCount() == 2 && tree->topLevelItem(0)->text(0) == "Needs review"
            && group->childCount() == 1 && !group->isExpanded(), "compatible mods move to a collapsed group after problem rows");
        check(page.upgradeKeptMods().contains("keep.jar") && page.upgradePendingModCount() == 1,
            "collapsed compatible rows retain real choice controls and correct counts");
        group->setExpanded(true); page.setUpgradeModChecks(grouped, "refreshed");
        check(tree->topLevelItem(1)->isExpanded(), "query refresh preserves the user's expanded compatible group");
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
              && write(old + "/mods/compatible.jar", "compatible-mod")
              && write(r.gameDir + "/versions/Target/Target.jar", "new-core")
              && write(r.gameDir + "/versions/Target/saves/Unwanted/level.dat", "must-not-copy"),
              "prepare full preservation fixture");
        InstanceUpgrade::Core core;
        core.clientJar = r.gameDir + "/versions/Target/Target.jar";
        core.gameVersion = "1.21.1";
        core.loaderName = "Fabric";
        core.loaderVersion = "0.16.0";
        core.keepOldMods = {QStringLiteral("compatible.jar")};
        core.mergedJson = QJsonObject{{"id", "Target"}, {"inheritsFrom", "Instance"}, {"jar", "Instance"},
            {"mainClass", "ExampleMain"}, {"downloads", QJsonObject{{"client", QJsonObject{{"sha1",
                QString::fromLatin1(QCryptographicHash::hash("new-core", QCryptographicHash::Sha1).toHex())}}}}}};
        const auto result = InstanceUpgrade::prepareAndCommit(r, core);
        check(result.ok && result.disabledMods == 1, "prepare and commit disables unverified old mods");
        check(read(old + "/mods/compatible.jar") == "compatible-mod"
              && !QFileInfo::exists(old + "/mods/compatible.jar.disabled"),
              "reviewed compatible mod remains enabled inside the snapshot-protected transaction");
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
    {
        // 2.1.1 副本模式：源实例保持不动，只复制一份候选并完整校验；
        // **不再为副本另做整份 ZIP 快照**（空间从四次写盘降到一次复制）。
        InstanceUpgrade::Request r{scratch.path() + "/copy-mode", "CopyName", "copy-ok", "Source"};
        const QString source = r.gameDir + "/versions/Source";
        check(write(source + "/Source.json", R"({"id":"Source"})")
              && write(source + "/Source.jar", "old-core")
              && write(source + "/wtfml.meta", R"({"instanceId":"copy-stable","name":"User name"})")
              && write(source + "/config/custom.cfg", "user-settings")
              && write(source + "/saves/World/level.dat", "world")
              && write(source + "/mods/old-mod.jar", "mod")
              && write(r.gameDir + "/versions/Target/Target.jar", "new-core")
              && write(r.gameDir + "/versions/Target/Target.json", R"({"id":"Target","mainClass":"ExampleMain"})"),
              "prepare copy-mode fixture");
        InstanceUpgrade::Core core;
        core.clientJar = r.gameDir + "/versions/Target/Target.jar";
        core.gameVersion = "1.21.1";
        core.loaderName = "Fabric";
        core.mergedJson = QJsonObject{{"id", "Target"},
            {"mainClass", "ExampleMain"},
            {"downloads", QJsonObject{{"client", QJsonObject{{"sha1",
                QString::fromLatin1(QCryptographicHash::hash("new-core", QCryptographicHash::Sha1).toHex())}}}}}};
        const auto result = InstanceUpgrade::prepareCopyAndCommit(r, core, source);
        const QString copyDir = r.gameDir + "/versions/CopyName";
        check(result.ok && result.requiresReload && result.snapshotZip.isEmpty(),
              "★ copy mode publishes a verified copy without a ZIP snapshot");
        check(read(source + "/Source.jar") == "old-core" && read(source + "/Source.json") == R"({"id":"Source"})"
              && read(source + "/mods/old-mod.jar") == "mod",
              "★★ copy mode leaves the source instance byte-for-byte intact");
        check(read(copyDir + "/CopyName.jar") == "new-core" && read(copyDir + "/mods/old-mod.jar.disabled") == "mod"
              && read(copyDir + "/saves/World/level.dat") == "world"
              && read(copyDir + "/config/custom.cfg") == "user-settings",
              "copy mode replaces only the core and archives unverified mods");
        const auto copyMeta = QJsonDocument::fromJson(read(copyDir + "/wtfml.meta")).object();
        check(copyMeta.value("instanceId") == "copy-stable" && copyMeta.value("realVersion") == "1.21.1",
              "copy mode keeps stable identity and records the new game version");
        check(QDir(r.gameDir + "/cache/backups/instances").entryList({"*.zip"}, QDir::Files).isEmpty(),
              "★★ no full ZIP snapshot is made for a copy (source instance is the rollback material)");
        check(InstanceUpgrade::recoverableTransactions(r.gameDir, "CopyName").isEmpty(),
              "copy mode offers no in-place rollback entry");
        const auto recoverMessages = InstanceUpgrade::recoverInterrupted(r.gameDir);
        check(!recoverMessages.isEmpty() && !QFileInfo::exists(InstanceUpgrade::transactionDir(r))
                  && read(copyDir + "/CopyName.jar") == "new-core",
              "startup recovery clears a committed copy transaction and keeps the published copy");
    }
    {
        // 副本模式失败：源实例不动、目标名不出、暂存自己收掉。
        InstanceUpgrade::Request r{scratch.path() + "/copy-fail", "CopyFail", "copy-fail", "Source"};
        const QString source = r.gameDir + "/versions/Source";
        check(write(source + "/Source.json", R"({"id":"Source"})")
              && write(source + "/Source.jar", "old-core")
              && write(source + "/wtfml.meta", "{not json")
              && write(r.gameDir + "/versions/Target/Target.jar", "new-core"),
              "prepare failing copy-mode fixture");
        InstanceUpgrade::Core core;
        core.clientJar = r.gameDir + "/versions/Target/Target.jar";
        core.gameVersion = "1.21.1";
        core.mergedJson = QJsonObject{{"mainClass", "ExampleMain"}};
        const auto result = InstanceUpgrade::prepareCopyAndCommit(r, core, source);
        check(!result.ok && !QFileInfo::exists(r.gameDir + "/versions/CopyFail")
                  && read(source + "/Source.jar") == "old-core",
              "failed copy mode publishes nothing and leaves the source intact");
        check(!QFileInfo::exists(InstanceUpgrade::transactionDir(r)),
              "failed copy mode removes its staging copy");
    }
    {
        // 副本模式取消：停在复制阶段，源实例不动、暂存不会漏。
        InstanceUpgrade::Request r{scratch.path() + "/copy-cancel", "CopyCancel", "copy-cancel", "Source"};
        const QString source = r.gameDir + "/versions/Source";
        check(write(source + "/Source.json", R"({"id":"Source"})")
              && write(source + "/Source.jar", "old-core")
              && write(source + "/saves/Big/level.dat", QByteArray(8 * 1024 * 1024, 'c'))
              && write(r.gameDir + "/versions/Target/Target.jar", "new-core"),
              "prepare cancellable copy-mode fixture");
        InstanceUpgrade::Core core;
        core.clientJar = r.gameDir + "/versions/Target/Target.jar";
        core.gameVersion = "1.21.1";
        core.mergedJson = QJsonObject{{"mainClass", "ExampleMain"}};
        bool cancel = false;
        const auto result = InstanceUpgrade::prepareCopyAndCommit(r, core, source,
            [&](InstanceUpgrade::Stage stage) {
                if (stage == InstanceUpgrade::Stage::Copy) cancel = true;
            },
            [&] { return cancel; });
        check(!result.ok && result.cancelled
                  && !QFileInfo::exists(r.gameDir + "/versions/CopyCancel")
                  && read(source + "/Source.jar") == "old-core",
              "cancelled copy mode publishes nothing and keeps the source intact");
        check(!QFileInfo::exists(InstanceUpgrade::transactionDir(r)),
              "cancelled copy mode removes its staging copy");
    }
    {
        // 副本模式在发布阶段被外部打断（这里用异常模拟"进程没走完"）：
        // 重启恢复只收自己那份未发布暂存，不碰版本目录。
        InstanceUpgrade::Request r{scratch.path() + "/copy-crash", "CopyCrash", "copy-crash", "Source"};
        const QString source = r.gameDir + "/versions/Source";
        check(write(source + "/Source.json", R"({"id":"Source"})")
              && write(source + "/Source.jar", "old-core")
              && write(r.gameDir + "/versions/Target/Target.jar", "new-core"),
              "prepare copy-mode crash fixture");
        InstanceUpgrade::Core core;
        core.clientJar = r.gameDir + "/versions/Target/Target.jar";
        core.gameVersion = "1.21.1";
        core.mergedJson = QJsonObject{{"mainClass", "ExampleMain"}};
        bool interrupted = false;
        try {
            InstanceUpgrade::prepareCopyAndCommit(r, core, source,
                [](InstanceUpgrade::Stage stage) {
                    if (stage == InstanceUpgrade::Stage::Publish) throw std::runtime_error("simulated crash");
                });
        } catch (const std::runtime_error &) { interrupted = true; }
        check(interrupted && QFileInfo::exists(InstanceUpgrade::transactionDir(r) + "/prepared")
                  && !QFileInfo::exists(r.gameDir + "/versions/CopyCrash"),
              "crash before publish leaves a copy staging directory");
        const auto messages = InstanceUpgrade::recoverInterrupted(r.gameDir);
        check(!messages.isEmpty() && !QFileInfo::exists(InstanceUpgrade::transactionDir(r))
                  && read(source + "/Source.jar") == "old-core"
                  && !QFileInfo::exists(r.gameDir + "/versions/CopyCrash"),
              "startup recovery removes unpublished copy staging without touching the source");
    }
    {
        InstanceUpgrade::Request r{scratch.path() + "/unsafe", "Instance", "unsafe-id", "Original"};
        const QString old = r.gameDir + "/versions/Instance";
        write(old + "/Instance.json", R"({"id":"../../../../victim"})");
        write(old + "/Instance.jar", "old-core");
        write(old + "/wtfml.meta", R"({"name":"Original"})");
        const QString victim = QDir::cleanPath(InstanceUpgrade::candidateDir(r) + "/../../../../victim.jar");
        write(victim, "keep");
        const QString client = r.gameDir + "/target.jar";
        write(client, "new-core");
        InstanceUpgrade::Core core;
        core.clientJar = client; core.gameVersion = "1.21.1";
        core.mergedJson = QJsonObject{{"mainClass", "ExampleMain"}};
        const auto result = InstanceUpgrade::prepareAndCommit(r, core);
        check(!result.ok && read(victim) == "keep" && read(old + "/Instance.jar") == "old-core",
              "traversal in old version id aborts upgrade without changing original");
    }
    {
        InstanceUpgrade::Request r{scratch.path() + "/active", "Instance", "active-stage", "Original"};
        const QString txn = InstanceUpgrade::transactionDir(r);
        write(txn + "/prepared/old.jar", "old");
        write(txn + "/journal.json", R"({"instanceId":"Instance"})");
        QLockFile lock(txn + ".lock");
        lock.setStaleLockTime(0);
        check(lock.tryLock(), "active transaction owns recovery exclusion lock");
        const auto competing = InstanceUpgrade::prepareAndCommit(r, InstanceUpgrade::Core{});
        check(!competing.ok && QFileInfo::exists(txn + "/prepared/old.jar"),
              "competing failed preparation cannot discard an active transaction");
        InstanceUpgrade::recoverInterrupted(r.gameDir);
        check(QFileInfo::exists(txn + "/prepared/old.jar"),
              "recovery leaves locked active preparation untouched");
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
        bool upgradeCancelExposed = false, cancelledMarked = false;
        QSet<Tasks::TaskId> cancelExposedTasks;
        InstanceTransferController::Host host;
        host.displayName = [](const QString &id) { return id; };
        host.isBeingModified = [&](const QString &id) { return locks.contains(id); };
        host.setFileOperationBusy = [&](const QString &id, bool busy) {
            if (busy) locks.insert(id); else locks.remove(id);
        };
        host.beginTask = [&](Tasks::TaskKind, const QString &, const QString &, const QList<Tasks::TaskStep> &) {
            return Tasks::TaskId(++started);
        };
        // Host 是按值交给控制器的：这两个观察点必须在构造之前挂上，之后再改就只剩本地副本。
        host.setTaskCapabilities = [&](Tasks::TaskId id, const Tasks::TaskCapabilities &caps) {
            if (caps.canCancel) cancelExposedTasks.insert(id); else cancelExposedTasks.remove(id);
        };
        host.markTaskCancelled = [&](Tasks::TaskId) { cancelledMarked = true; };
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
        // 后台原地升级**支持取消**：引擎在准备 / 快照 / 校验这些安全点看取消位。
        // 源实例故意做一个 64 MiB 的存档，按下取消那一刻它必然还在准备阶段。
        const QString cancelDir = game + "/versions/CancelMe";
        check(write(cancelDir + "/CancelMe.json", R"({"id":"CancelMe","mainClass":"OldMain"})")
              && write(cancelDir + "/CancelMe.jar", "old-core")
              && write(cancelDir + "/wtfml.meta", R"({"realVersion":"1.20.1","instanceId":"cancel-stable"})")
              && write(cancelDir + "/saves/Big/level.dat", QByteArray(64 * 1024 * 1024, 'c'))
              && write(game + "/versions/Target3/Target3.json", R"({"id":"Target3","mainClass":"NewMain"})")
              && write(game + "/versions/Target3/Target3.jar", "new-core")
              && write(game + "/versions/Target3/wtfml.meta", R"({"realVersion":"1.21.1"})"),
              "prepare cancellable controller upgrade fixture");
        done = false; reload = false;
        deadline.start(60000);
        controller.upgradeInPlace("CancelMe", "Target3", completed);
        const Tasks::TaskId cancelId(started);
        check(controller.canCancelTask(cancelId),
              "running in-place upgrade advertises a working cancel");
        upgradeCancelExposed = cancelExposedTasks.contains(cancelId);
        check(controller.cancelTask(cancelId), "in-place upgrade accepts cancellation");
        if (!done) wait.exec();
        deadline.stop();
        check(!timedOut && done && !reload && cancelledMarked && upgradeCancelExposed
                  && locks.isEmpty() && !controller.copying(),
              "★★ cancelled upgrade ends as Cancelled (not Failed), releases locks, keeps the page");
        check(read(cancelDir + "/CancelMe.jar") == "old-core"
                  && read(cancelDir + "/saves/Big/level.dat").size() == 64 * 1024 * 1024,
              "cancelled upgrade leaves the original instance byte-for-byte intact");
        answers.stop();
        AppPaths::setMinecraftDir(previousRoot);
    }
    {
        // 2.1.1 副本模式 + 父任务：任务中心只出现一个父任务（复制/校验/发布是它的步骤），
        // 源实例一个字节都不动，而且**不再为副本另做一份 ZIP 快照**。
        const QString game = scratch.path() + "/copy-controller";
        const QString source = game + "/versions/Source";
        check(write(source + "/Source.json", R"({"id":"Source","mainClass":"OldMain"})")
              && write(source + "/Source.jar", "old-core")
              && write(source + "/wtfml.meta", R"({"realVersion":"1.20.1","instanceId":"copy-controller"})")
              && write(source + "/saves/World/level.dat", "world")
              && write(source + "/mods/old.jar", "old-mod")
              && write(game + "/versions/Target/Target.json", R"({"id":"Target","mainClass":"NewMain"})")
              && write(game + "/versions/Target/Target.jar", "new-core")
              && write(game + "/versions/Target/wtfml.meta", R"({"realVersion":"1.21.1"})"),
              "prepare copy-mode controller fixture");
        const QString previousRoot = AppPaths::minecraftDir();
        AppPaths::setMinecraftDir(game);
        QWidget anchor;
        QWidget home(&anchor);
        OverlayHost overlay(&anchor, &home);
        VersionManager versions;
        LauncherManager launcher;
        bool childStarted = false, standaloneStarted = false, finished = false, success = false;
        bool parentStepDone = false, settledOk = false, settledCancelled = false;
        Tasks::TaskId seenParent = 0;
        int seenParentStep = -1;
        InstanceTransferController::Host host;
        host.displayName = [](const QString &id) { return id; };
        host.isBeingModified = [](const QString &) { return false; };
        host.setFileOperationBusy = [](const QString &, bool) {};
        host.beginTask = [&](Tasks::TaskKind, const QString &, const QString &,
                             const QList<Tasks::TaskStep> &) {
            standaloneStarted = true;
            return Tasks::TaskId(900);
        };
        host.beginChildTask = [&](Tasks::TaskId parent, Tasks::TaskKind, const QString &,
                                  const QString &, const QList<Tasks::TaskStep> &) {
            childStarted = true;
            seenParent = parent;
            return Tasks::TaskId(901);
        };
        host.setTaskStepState = [&](Tasks::TaskId id, int index, Tasks::TaskStepState state) {
            if (id == Tasks::TaskId(77) && index == 2 && state == Tasks::TaskStepState::Done) {
                parentStepDone = true;
            }
        };
        host.setTaskCapabilities = [](Tasks::TaskId, const Tasks::TaskCapabilities &) {};
        host.setTaskDetail = [](Tasks::TaskId, const QString &) {};
        host.setTaskProgress = [](Tasks::TaskId, qint64, qint64) {};
        host.finishTask = [&](Tasks::TaskId, bool ok, const QString &) {
            finished = true;
            success = ok;
        };
        host.markTaskCancelled = [](Tasks::TaskId) {};
        host.refreshInstances = [] {};
        InstanceTransferController controller(&versions, &launcher, &overlay, host);
        // 收尾那一发会弹一句结果（成功/失败都弹）：这里只负责按掉它，免得挂住事件循环。
        QTimer answers;
        QObject::connect(&answers, &QTimer::timeout, &overlay, [&] {
            if (auto *box = overlay.findChild<InlineMessageBox *>()) {
                if (auto *button = box->findChild<QPushButton *>(InlineMessageBox::buttonName(0))) {
                    button->click();
                } else {
                    box->reject();
                }
            }
        });
        answers.start(10);
        QEventLoop wait;
        QTimer deadline;
        deadline.setSingleShot(true);
        bool timedOut = false;
        QObject::connect(&deadline, &QTimer::timeout, &wait, [&] { timedOut = true; wait.quit(); });
        const QHash<int, int> parentSteps{
            {int(InstanceUpgrade::Stage::Checking), 1},
            {int(InstanceUpgrade::Stage::Copy), 1},
            {int(InstanceUpgrade::Stage::VerifyCopy), 1},
            {int(InstanceUpgrade::Stage::Publish), 2},
            {int(InstanceUpgrade::Stage::Verify), 2}};
        controller.upgradeToCopy("Source", "CopyOut", "Target",
            [&](bool ok, const QString &) { finished = true; success = ok; wait.quit(); },
            {}, true, Tasks::TaskId(77), parentSteps,
            [&](bool ok, bool cancelled) { settledOk = ok; settledCancelled = cancelled; });
        deadline.start(30000);
        if (!finished) wait.exec();
        deadline.stop();
        check(!timedOut && finished && success && childStarted && !standaloneStarted
                  && seenParent == Tasks::TaskId(77) && parentStepDone,
              "★★ copy-mode upgrade is one child of the update parent task (no extra task row)");
        check(settledOk && !settledCancelled, "copy-mode reports the exact settled outcome");
        check(read(source + "/Source.jar") == "old-core"
                  && read(source + "/mods/old.jar") == "old-mod",
              "★★ copy-mode leaves the source instance untouched");
        check(read(game + "/versions/CopyOut/CopyOut.jar") == "new-core"
                  && read(game + "/versions/CopyOut/saves/World/level.dat") == "world"
                  && read(game + "/versions/CopyOut/mods/old.jar.disabled") == "old-mod",
              "copy-mode publishes the new core with the copied user files");
        check(QDir(game + "/cache/backups/instances").entryList({"*.zip"}, QDir::Files).isEmpty(),
              "★★ copy-mode makes no extra ZIP snapshot (space optimization)");
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
        check(!result.ok && result.rolledBack && result.cancelled && result.requiresReload
              && read(r.gameDir + "/versions/Instance/Instance.jar") == "old-core",
              "interruption after retaining original automatically rolls back");
    }
    {
        const auto r = fixture("cancel-rollback-blocked");
        bool cancelled = false;
        const auto result = InstanceUpgrade::commitPrepared(r, [&](InstanceUpgrade::Stage stage) {
            if (stage == InstanceUpgrade::Stage::Publish) {
                // Simulate external files occupying both rename destinations.
                check(write(r.gameDir + "/versions/Instance/block.txt", "CURRENT")
                      && write(InstanceUpgrade::transactionDir(r) + "/replaced/block.txt", "KEEP"),
                      "prepare blocked rollback during cancellation");
                cancelled = true;
            }
        }, [&] { return cancelled; });
        check(!result.ok && !result.cancelled && !result.rolledBack && result.requiresReload
                  && read(InstanceUpgrade::transactionDir(r) + "/original/Instance.jar") == "old-core"
                  && read(r.gameDir + "/versions/Instance/block.txt") == "CURRENT",
              "cancel request cannot hide rollback failure or delete retained user data");
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
    {
        // 2.1.2「继续未完成操作」：报告登记 → 任务中心那一颗 → 补完并收尾。
        const QString game = scratch.path() + "/recovery-game";
        const QString world = game + "/source-world";
        const QString saves = game + "/versions/Recovery/saves";
        check(write(world + "/level.dat", "level-bytes")
                  && write(world + "/region/r.0.0.mca", QByteArray(2048, 'r'))
                  && QDir().mkpath(saves),
              "prepare recovery fixture");
        const QString previousRoot = AppPaths::minecraftDir();
        AppPaths::setMinecraftDir(game);
        QWidget anchor;
        QWidget home(&anchor);
        OverlayHost overlay(&anchor, &home);
        int started = 0;
        bool finished = false, success = false;
        QString finishedProblem, recoveryToken;
        FileOperationRecoveryController::Host host;
        host.displayName = [](const QString &id) { return id; };
        host.isInstanceBusy = [](const QString &) { return false; };
        host.log = [](const QString &) {};
        host.beginTask = [&](Tasks::TaskKind, const QString &, const QString &,
                             const QList<Tasks::TaskStep> &) {
            return Tasks::TaskId(++started);
        };
        host.setTaskDetail = [](Tasks::TaskId, const QString &) {};
        host.setTaskStepState = [](Tasks::TaskId, int, Tasks::TaskStepState) {};
        host.finishTask = [&](Tasks::TaskId, bool ok, const QString &problem) {
            finished = true;
            success = ok;
            finishedProblem = problem;
        };
        host.setTaskRecovery = [&](Tasks::TaskId, const QString &token) { recoveryToken = token; };
        FileOperationRecoveryController controller(&overlay, host);

        const QString reportId = QStringLiteral("aaaaaaaa-1111-2222-3333-444444444444");
        OperationReport::Report report;
        report.id = reportId;
        report.kind = OperationReport::Kind::WorldImport;
        report.title = QStringLiteral("导入世界：Recovery");
        report.instanceId = QStringLiteral("Recovery");
        report.sourceRoot = world;
        report.targetRoot = saves;
        report.stagingDir = OperationReport::stagingDirectory(game, reportId);
        report.conflictPolicy = OperationReport::ConflictPolicy::SkipExisting;
        report.totalFiles = 2;
        report.complete = false;
        report.startedAt = QDateTime::currentDateTime();
        report.updatedAt = report.startedAt;
        report.context = QJsonObject{{QStringLiteral("dataRoot"), game},
                                     {QStringLiteral("worldDir"), world},
                                     {QStringLiteral("savesDir"), saves},
                                     {QStringLiteral("name"), QStringLiteral("Recovered")}};
        report.entries.append(OperationReport::Entry{
            QStringLiteral("Recovered"), 0, OperationReport::EntryState::Pending, {}});
        check(controller.registerReport(report) && controller.has(reportId)
                  && QFileInfo::exists(OperationReport::reportPath(game, reportId)),
              "★ recovery report registers with its on-disk report");
        check(controller.resume(reportId) && started == 1,
              "★ task center's resume starts one operation task");
        QEventLoop wait;
        QTimer deadline;
        deadline.setSingleShot(true);
        bool timedOut = false;
        QObject::connect(&deadline, &QTimer::timeout, &wait, [&] { timedOut = true; wait.quit(); });
        const auto poll = new QTimer(&wait);
        QObject::connect(poll, &QTimer::timeout, &wait, [&] {
            if (finished) wait.quit();
        });
        poll->start(20);
        deadline.start(30000);
        if (!finished) wait.exec();
        poll->stop();
        deadline.stop();
        check(!timedOut && finished && success && recoveryToken.isEmpty(),
              "★★ resume completes the world import and leaves no recovery entry");
        check(read(saves + "/Recovered/level.dat") == "level-bytes"
                  && read(saves + "/Recovered/region/r.0.0.mca") == QByteArray(2048, 'r'),
              "★★ resumed import publishes the complete world");
        check(!QFileInfo::exists(OperationReport::reportPath(game, reportId))
                  && !controller.has(reportId) && !QFileInfo::exists(report.stagingDir),
              "resume clears the report and staging once everything is done");

        // 做不完时：收失败、重新挂上恢复入口、并写出“为什么做不完”的报告。
        const QString blockedId = QStringLiteral("bbbbbbbb-1111-2222-3333-444444444444");
        OperationReport::Report blocked = report;
        blocked.id = blockedId;
        blocked.stagingDir = OperationReport::stagingDirectory(game, blockedId);
        blocked.context = QJsonObject{{QStringLiteral("dataRoot"), game},
                                      {QStringLiteral("worldDir"), game + "/missing-world"},
                                      {QStringLiteral("savesDir"), saves},
                                      {QStringLiteral("name"), QStringLiteral("Blocked")}};
        blocked.entries = {OperationReport::Entry{
            QStringLiteral("Blocked"), 0, OperationReport::EntryState::Pending, {}}};
        check(controller.registerReport(blocked) && controller.resume(blockedId),
              "register and resume a world import whose source is gone");
        finished = false;
        success = true;
        poll->start(20);
        deadline.start(30000);
        if (!finished) wait.exec();
        poll->stop();
        deadline.stop();
        const QString failurePath =
            OperationReport::reportPath(game, blockedId) + QStringLiteral(".failure.txt");
        check(!timedOut && finished && !success && recoveryToken == blockedId
                  && QFileInfo::exists(failurePath),
              "★★ an impossible resume fails honestly, keeps the entry and writes a failure report");
        check(QString::fromUtf8(read(failurePath)).contains(QStringLiteral("继续未完成操作"))
                  && QString::fromUtf8(read(failurePath)).contains(QStringLiteral("未完成")),
              "failure report explains what is unfinished and what the user can do next");
        AppPaths::setMinecraftDir(previousRoot);
    }
    return failures == 0 ? 0 : 1;
}
