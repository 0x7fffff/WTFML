#include "modules/Support.hpp"
#include "controllers/DownloadBatchController.hpp"
#include "controllers/ActivityCenterController.hpp"
#include "download/DownloadCheckpoint.hpp"
#include "download/DownloadManager.hpp"
#include "controllers/DirectDownloadController.hpp"
#include "manage/VersionManager.hpp"
#include "presets/PresetTargetSnapshot.hpp"
#include "server/ServerInstallController.hpp"
#include "server/ServerRegistry.hpp"

int runDownloadBatchChecks(QApplication &app) {
    const auto childAt = app.arguments().indexOf("--batch-resume-root");
    if (childAt >= 0) {
        const auto root = app.arguments().value(childAt + 1);
        if (!QDir::isAbsolutePath(root) || !QFileInfo(root + "/download-batches").isDir()) return 2;
        ActivityCenterController activity; activity.tasks()->setHistoryPath(root + "/tasks.json"); activity.loadHistory();
        const auto names = QDir(root + "/download-batches").entryList({"*.receipt"}, QDir::Files);
        if (names.size() != 1) return 7;
        DownloadCheckpoint original;
        if (!original.load(root + "/download-batches/" + names.first())) return 7;
        const auto originalId = original.context.value("task").toString().toULongLong(); original.lock.reset();
        DownloadManager downloader;
        DownloadBatchController controller(&activity, &downloader, [] { return Tasks::invalidTaskId(); });
        int continued = 0;
        controller.setContinuation({}, [&](Tasks::TaskId task, const QJsonObject &intent) {
            if (intent.value("kind") != "explicit-test") return false;
            ++continued; activity.finish(task, true, {}, false); return true;
        });
        controller.load();
        for (const auto &task : activity.tasks()->all()) if (controller.owns(task.id)) {
            if (task.id != originalId) return 7;
            if (!controller.resume(task.id)) return 3;
            QElapsedTimer clock; clock.start();
            while (Tasks::isActiveState(activity.tasks()->snapshot(task.id).state) && activity.tasks()->snapshot(task.id).state != Tasks::TaskState::Paused && clock.elapsed() < 10000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (activity.tasks()->snapshot(task.id).state == Tasks::TaskState::Paused) {
                if (continued != 0 || !controller.resume(task.id)) return 6;
                QElapsedTimer verifyClock; verifyClock.start();
                while (controller.isRestoring() && verifyClock.elapsed() < 10000)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                if (continued != 1) return 6;
            }
            return activity.tasks()->snapshot(task.id).state == Tasks::TaskState::Succeeded ? 0 : 4;
        }
        return 5;
    }
    int failures = 0;
    const auto expect = [&](bool ok, const char *message) { printf("%s batch: %s\n", ok ? "PASS" : "FAIL", message); fflush(stdout); if (!ok) ++failures; };
    QTemporaryDir fixture(QDir::current().filePath(QStringLiteral(".download-batch-XXXXXX")));
    if (!fixture.isValid()) return 1;
    const auto write = [](const QString &path, const QByteArray &bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); };
    const auto read = [](const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
    {
        const auto root = fixture.filePath("preset-target"); QDir().mkpath(root);
        const auto missing = Presets::snapshotInitializationTargets(root);
        expect(!missing.isEmpty() && write(root + "/options.txt", "fov:1"), "preset initialization records absent target settings");
        const auto original = Presets::snapshotInitializationTargets(root);
        expect(original != missing && write(root + "/options.txt", "fov:2") && Presets::snapshotInitializationTargets(root) != original,
            "external option edits invalidate saved initialization consent");
        const auto changed = Presets::snapshotInitializationTargets(root);
        expect(write(root + "/core.jar", "downloaded core") && Presets::snapshotInitializationTargets(root) == changed,
            "publishing core files does not invalidate unrelated preset target consent");
        QDir().mkpath(root + "/config");
        expect(write(root + "/config/new.json", "{}") && Presets::snapshotInitializationTargets(root) != changed,
            "new configuration files invalidate restart-time initialization consent");
        const auto beforeWorld = Presets::snapshotInitializationTargets(root);
        QDir().mkpath(root + "/saves/new-world/datapacks");
        expect(write(root + "/saves/new-world/level.dat", "world") && Presets::snapshotInitializationTargets(root) != beforeWorld,
            "new worlds invalidate deferred-data-pack initialization consent without scanning region files");
    }
    QTcpServer server; if (!server.listen(QHostAddress::LocalHost, 0)) return 1;
    const QByteArray payload(128 * 1024, 'c');
    QByteArray namedMetadata, namedManifest;
    const QString sha1 = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha1).toHex());
    int requests = 0;
    std::function<void()> beforeResponse;
    bool holdSecond = true;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto input = socket->property("input").toByteArray() + socket->readAll(); socket->setProperty("input", input);
                if (!input.contains("\r\n\r\n") || socket->property("sent").toBool()) return;
                socket->setProperty("sent", true); ++requests;
                if (beforeResponse) { auto action = std::move(beforeResponse); beforeResponse = {}; action(); }
                if (input.split(' ').value(1).startsWith("/hold") && holdSecond) {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\n\r\nc"); return;
                }
                const auto path = input.split(' ').value(1);
                const auto body = path.startsWith("/named-meta") ? namedMetadata : path.startsWith("/named-manifest") ? namedManifest : payload;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
                socket->write(body); socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const QString url = QStringLiteral("http://127.0.0.1:%1/file?token=secret-proof").arg(server.serverPort());
    const auto wait = [&](const std::function<bool()> &done) {
        QElapsedTimer clock; clock.start();
        while (!done() && clock.elapsed() < 10000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return done();
    };
    // The continuation gate must accept work without hashing on the UI thread,
    // reject duplicates, and never run an installer after cancel/destruction/revoked consent.
    for (const QString mode : {QString("accept"), QString("cancel"), QString("destroy"), QString("revoke"), QString("changed")}) {
        const auto root = fixture.filePath("async-" + mode);
        QDir().mkpath(root + "/download-batches");
        const auto target = root + "/large.bin";
        QFile large(target);
        bool prepared = large.open(QIODevice::WriteOnly);
        const QByteArray chunk(1024 * 1024, 'v');
        for (int part = 0; part < 32 && prepared; ++part) prepared = large.write(chunk) == chunk.size();
        large.close();
        expect(prepared, "async validation uses an owned 32 MiB original");
        if (!prepared) continue;
        ActivityCenterController activity; activity.tasks()->setHistoryPath(root + "/tasks.json");
        const auto id = activity.begin(Tasks::TaskKind::Install, QStringLiteral("后台恢复校验"));
        const auto receiptPath = root + "/download-batches/one.receipt";
        DownloadCheckpoint receipt; receipt.path = receiptPath;
        receipt.context = {{"title", QStringLiteral("后台恢复校验")}, {"task", QString::number(id)},
            {"phase", "continue-install"}, {"continuation", QJsonObject{{"kind", "test"}}}};
        const auto original = DownloadCheckpoint::fingerprint(target);
        expect(receipt.add({url, target, {}, {}, 32 * 1024 * 1024, original}) && receipt.save(),
            "async continuation retains a protected execution receipt");
        receipt.lock.reset();
        activity.finish(id, false, QStringLiteral("模拟上一进程退出"), false);
        if (mode == "changed") expect(write(target, "external-edit"), "external modification precedes validation");
        const auto expectedOriginal = DownloadCheckpoint::fingerprint(target);
        DownloadManager downloads;
        auto batches = std::make_unique<DownloadBatchController>(&activity, &downloads, [] { return Tasks::invalidTaskId(); });
        bool allowed = true; int continued = 0;
        batches->setRestoreAllowed([&] { return allowed; });
        batches->setContinuation({}, [&](Tasks::TaskId task, const QJsonObject &) {
            ++continued; activity.finish(task, true, {}, false); return true;
        });
        batches->load();
        expect(batches->owns(id), "restart fixture restores the original interrupted task identity");
        QElapsedTimer validationTimer; validationTimer.start();
        expect(batches->resume(id) && batches->isRestoring() && continued == 0,
            "resume returns before validation/installer completion and marks the operation busy");
        printf("MEASURE resume-submit mode=%s bytes=33554432 elapsed_us=%lld\n", qPrintable(mode), static_cast<long long>(validationTimer.nsecsElapsed() / 1000));
        fflush(stdout);
        const auto caps = activity.tasks()->snapshot(id).capabilities;
        expect(caps.canCancel && !caps.canPause, "task UI offers cancel rather than pause during validation");
        expect(!batches->resume(id), "duplicate continuation is rejected while validation is pending");
        if (mode == "destroy") {
            batches.reset();
            DownloadCheckpoint preserved;
            expect(preserved.load(receiptPath) && continued == 0,
                "destroying the controller releases the receipt and never runs its continuation");
            continue;
        }
        if (mode == "cancel") expect(batches->cancel(id), "pending validation is cancellable");
        if (mode == "revoke") allowed = false;
        expect(wait([&] { return !batches->isRestoring(); }), "background validation completes or cancels within the test deadline");
        if (mode == "accept") expect(continued == 1 && activity.tasks()->snapshot(id).state == Tasks::TaskState::Succeeded,
            "unchanged files continue the original intent exactly once");
        else expect(continued == 0 && activity.tasks()->snapshot(id).state == (mode == "cancel" ? Tasks::TaskState::Cancelled : Tasks::TaskState::Failed),
            "cancelled or revoked validation never invokes the installer");
        expect(DownloadCheckpoint::fingerprint(target) == expectedOriginal, "validation never modifies the original or external edit");
    }
    for (const QString mode : {QString("accept"), QString("cancel"), QString("destroy"), QString("revoke"),
                               QString("changed"), QString("added"), QString("core-changed")}) {
        const auto root = fixture.filePath("async-initialization-" + mode);
        const auto instance = root + "/versions/example";
        const auto config = instance + "/config/large.bin";
        QDir().mkpath(instance + "/config");
        QFile large(config); bool prepared = large.open(QIODevice::WriteOnly);
        const QByteArray chunk(1024 * 1024, 'i');
        for (int i = 0; i < 32 && prepared; ++i) prepared = large.write(chunk) == chunk.size();
        large.close();
        expect(prepared, "initialization recovery owns a 32 MiB configuration");
        if (!prepared) continue;
        const auto core = instance + "/core.json";
        expect(write(core, "original-core"), "initialization core identity fixture");
        const QJsonObject intent{{"kind", "named-vanilla"}, {"root", root}, {"instance", "example"},
            {"presets", QJsonArray{QJsonObject{{"id", "test"}}}},
            {"originals", QJsonObject{{core, DownloadCheckpoint::fingerprint(core)}}},
            {"presetTargets", Presets::snapshotInitializationTargets(instance)}};
        std::atomic_bool stopped{true};
        expect(Presets::snapshotInitializationTargets(instance, &stopped).isEmpty() &&
                   !Presets::unchangedInitializationContinuation(intent, &stopped),
               "configuration scanning observes cancellation without accepting a partial snapshot");
        auto malformed = intent;
        malformed.insert("originals", QJsonObject{{instance + "/config", QJsonObject()}});
        expect(!Presets::unchangedInitializationContinuation(malformed),
               "an unreadable original cannot match an empty saved fingerprint");
        if (mode == "changed") expect(write(config, "external-configuration"), "external configuration change fixture");
        if (mode == "added") expect(write(instance + "/config/new.json", "{}"), "new configuration file fixture");
        if (mode == "core-changed") expect(write(core, "external-core"), "external core change fixture");
        const auto preserved = DownloadCheckpoint::fingerprint(config);
        ActivityCenterController activity; activity.tasks()->setHistoryPath(root + "/tasks.json");
        const auto id = activity.begin(Tasks::TaskKind::Install, "resume initialization");
        DownloadCheckpoint receipt; receipt.path = root + "/download-batches/one.receipt";
        receipt.context = {{"title", "resume initialization"}, {"task", QString::number(id)},
            {"phase", "continue-install"}, {"continuation", intent}};
        expect(receipt.add({url, root + "/missing.stage", {}, {}, -1, {{"exists", false}}}) && receipt.save(),
               "initialization consent and original targets survive restart");
        receipt.lock.reset(); activity.finish(id, false, "interrupted", false);
        DownloadManager downloads;
        auto batches = std::make_unique<DownloadBatchController>(&activity, &downloads, [] { return Tasks::invalidTaskId(); });
        bool allowed = true; int continued = 0;
        batches->setRestoreAllowed([&] { return allowed; });
        batches->setContinuation({}, [&](Tasks::TaskId task, const QJsonObject &) {
            ++continued; activity.finish(task, true, {}, false); return true;
        }, Presets::unchangedInitializationContinuation);
        batches->load();
        QElapsedTimer submit; submit.start();
        expect(batches->resume(id) && batches->isRestoring() && continued == 0,
               "configuration verification starts in background before installation");
        printf("MEASURE initialization-resume mode=%s bytes=33554432 submit_us=%lld\n", qPrintable(mode), static_cast<long long>(submit.nsecsElapsed()/1000));
        expect(!batches->resume(id), "configuration verification rejects duplicate continuation");
        if (mode == "destroy") {
            batches.reset(); DownloadCheckpoint saved;
            expect(saved.load(receipt.path) && continued == 0, "destroyed configuration scan retains consent and never installs");
            continue;
        }
        if (mode == "cancel") expect(batches->cancel(id), "configuration verification accepts cancellation");
        if (mode == "revoke") allowed = false;
        expect(wait([&] { return !batches->isRestoring(); }), "configuration verification completes within its deadline");
        expect(continued == (mode == "accept" ? 1 : 0) && activity.tasks()->snapshot(id).state ==
                   (mode == "accept" ? Tasks::TaskState::Succeeded : mode == "cancel" ? Tasks::TaskState::Cancelled : Tasks::TaskState::Failed),
               "changed, cancelled or revoked initialization never runs its installer");
        expect(DownloadCheckpoint::fingerprint(config) == preserved, "configuration verification retains external and original contents");
    }
    for (const QString mode : {QString("accept"), QString("cancel"), QString("destroy"),
                               QString("revoke"), QString("changed"), QString("publish-race")}) {
        const auto root = fixture.filePath("async-files-" + mode);
        QDir().mkpath(root);
        const auto target = root + "/original.bin";
        QFile large(target);
        bool prepared = large.open(QIODevice::WriteOnly);
        const QByteArray chunk(1024 * 1024, 'q');
        for (int i = 0; i < 32 && prepared; ++i) prepared = large.write(chunk) == chunk.size();
        large.close();
        expect(prepared, "file resume owns a 32 MiB original");
        if (!prepared) continue;
        ActivityCenterController activity; activity.tasks()->setHistoryPath(root + "/tasks.json");
        const auto id = activity.begin(Tasks::TaskKind::Download, "resume files");
        DownloadCheckpoint receipt; receipt.path = root + "/download-batches/one.receipt";
        receipt.context = {{"title", "resume files"}, {"task", QString::number(id)}};
        expect(receipt.add({url, target, sha1, {}, payload.size(), DownloadCheckpoint::fingerprint(target)}) && receipt.save(),
               "file resume retains its exact original consent");
        receipt.lock.reset(); activity.finish(id, false, "interrupted", false);
        if (mode == "changed") expect(write(target, "external-edit"), "file resume external change fixture");
        const auto original = DownloadCheckpoint::fingerprint(target);
        DownloadManager downloads; downloads.setSegmentedDownloadEnabled(false);
        auto batches = std::make_unique<DownloadBatchController>(&activity, &downloads, [] { return Tasks::invalidTaskId(); });
        bool allowed = true; batches->setRestoreAllowed([&] { return allowed; }); batches->load();
        const auto before = requests;
        QElapsedTimer submit; submit.start();
        expect(batches->resume(id) && batches->isRestoring() && requests == before,
               "file resume admits background validation before network or writes");
        printf("MEASURE file-resume mode=%s bytes=33554432 submit_us=%lld\n", qPrintable(mode), static_cast<long long>(submit.nsecsElapsed()/1000));
        expect(!batches->resume(id) && !activity.tasks()->snapshot(id).capabilities.canPause,
               "file validation rejects duplicate resume and pause");
        if (mode == "destroy") {
            batches.reset();
            DownloadCheckpoint saved;
            expect(saved.load(receipt.path) && requests == before && DownloadCheckpoint::fingerprint(target) == original,
                   "destroyed file validator releases its receipt without publishing");
            continue;
        }
        if (mode == "cancel") expect(batches->cancel(id), "file validation accepts cancellation");
        if (mode == "revoke") allowed = false;
        if (mode == "publish-race") beforeResponse = [&] { expect(write(target, "external-after-validation"), "external change after validation fixture"); };
        expect(wait([&] { return !batches->isRestoring(); }), "file recovery reaches a bounded terminal outcome");
        beforeResponse = {};
        const auto state = activity.tasks()->snapshot(id).state;
        expect(state == (mode == "accept" ? Tasks::TaskState::Succeeded : mode == "cancel" ? Tasks::TaskState::Cancelled : Tasks::TaskState::Failed),
               "file recovery reports success, failure or user cancellation correctly");
        if (mode == "accept") expect(read(target) == payload, "validated file recovery publishes the expected artifact");
        else if (mode == "publish-race") expect(read(target) == "external-after-validation", "publish guard retains a post-validation external edit");
        else expect(requests == before && DownloadCheckpoint::fingerprint(target) == original, "rejected file recovery preserves original without network");
    }
    {
        const auto root = fixture.filePath("cancel-live-validation"); QDir().mkpath(root);
        ActivityCenterController activity; activity.tasks()->setHistoryPath(root + "/tasks.json");
        const auto id = activity.begin(Tasks::TaskKind::Download, "cancel live validation");
        DownloadManager downloads; downloads.setSegmentedDownloadEnabled(false);
        DownloadBatchController batches(&activity, &downloads, [&] { return id; });
        downloads.beginBatch();
        downloads.downloadFile(QString(url).replace("/file?", "/hold?"), root + "/file.bin", sha1, payload.size());
        downloads.endBatch();
        expect(wait([&] { return downloads.canPauseBatch(); }) && batches.pause(id) && batches.resume(id),
               "live paused batch enters background target validation");
        expect(batches.cancel(id) && wait([&] { return !batches.isRestoring() && !downloads.isBusy(); }) &&
                   activity.tasks()->snapshot(id).state == Tasks::TaskState::Cancelled,
               "cancelling live validation also clears the paused downloader busy state");
    }
    {
        DownloadManager downloads; downloads.setSegmentedDownloadEnabled(false);
        downloads.setVersionManifestUrlOverride(QString(url).replace("/file?", "/hold?"));
        const int before = requests;
        downloads.fetchVersionManifest();
        expect(wait([&] { return requests > before; }), "independent startup manifest remains in flight");
        downloads.beginBatch();
        expect(downloads.enableCheckpoint(fixture.filePath("manifest-independent.receipt"), {}), "resource batch beside independent manifest records its intent");
        downloads.downloadFile(url, fixture.filePath("manifest-independent.bin"), sha1, payload.size()); downloads.endBatch();
        expect(downloads.canPauseBatch() && downloads.pauseBatch(), "independent startup manifest never disables file-batch pause");
        downloads.cancelBatch();
    }
    {
        const auto oldRoot = AppPaths::minecraftDir();
        const auto root = fixture.filePath("server-recovery"); QDir().mkpath(root); AppPaths::setMinecraftDir(root);
        ServerInstall::Request request; request.flavor = ServerInstall::Flavor::Vanilla; request.gameVersion = "1.20.1";
        ServerModel::Info info; info.name = "Recovery"; info.flavor = request.flavor; info.flavorKnown = true; info.gameVersion = request.gameVersion;
        QString problem; expect(ServerRegistry::createServer("Recovery", info, &problem), "isolated server created");
        QJsonObject intent;
        {
            DownloadManager downloads; downloads.setSegmentedDownloadEnabled(false);
            ServerInstallRun::ServerInstallController installer; installer.setDownloadManager(&downloads);
            installer.setLocalVersionJson([&](const QString &) { return QJsonObject{{"downloads", QJsonObject{{"server", QJsonObject{{"url", url}, {"sha1", sha1}, {"size", payload.size()}}}}}}; });
            ActivityCenterController activity; activity.tasks()->setHistoryPath(root + "/tasks.json");
            const auto task = activity.begin(Tasks::TaskKind::Install, "server fixture", "Recovery");
            DownloadBatchController batches(&activity, &downloads, [&] { return installer.ownsDownloadBatch() ? task : Tasks::invalidTaskId(); });
            batches.setContinuation([&] { return installer.downloadContinuation(); }, {});
            installer.start(request, "Recovery", {});
            intent = installer.downloadContinuation();
            expect(batches.owns(task) && batches.pause(task) && !intent.isEmpty(), "server owns receipt before batch dispatch and pauses its file stage");
            expect(installer.isRunning() && !ServerRegistry::describe("Recovery").installed, "paused server is not falsely installed");
            expect(batches.resume(task) && wait([&] { return !installer.isRunning(); }) && ServerRegistry::describe("Recovery").installed,
                "server resumes live without losing its completion wiring");
        }
        DownloadManager downloads; ServerInstallRun::ServerInstallController resumed; resumed.setDownloadManager(&downloads);
        const auto properties = AppPaths::serverDir("Recovery") + "/server.properties";
        expect(write(properties, "external=true") && !resumed.continueDownload(intent), "server continuation refuses changed settings");
        QFile::remove(properties);
        expect(resumed.continueDownload(intent) && wait([&] { return !resumed.isRunning(); }) && ServerRegistry::describe("Recovery").installed,
            "new controller explicitly continues exact resolved server files");
        for (const auto &mode : {QString("retry-success"), QString("failure"), QString("noisy")}) {
            const auto id = "Watchdog-" + mode;
            ServerInstall::Request forge; forge.flavor = ServerInstall::Flavor::Forge; forge.gameVersion = "1.20.1"; forge.loaderVersion = "47.2.0";
            info.name = id; info.flavor = forge.flavor; info.loaderVersion = forge.loaderVersion;
            expect(ServerRegistry::createServer(id, info, &problem), "watchdog fixture server created");
            const auto dir = AppPaths::serverDir(id);
            expect(write(dir + "/.watchdog-fixture.json", QJsonDocument(QJsonObject{{"format", "wtfml-installer-watchdog-test/1"}, {"root", dir}, {"mode", mode}}).toJson()), "watchdog private marker saved");
            auto plan = ServerInstall::planFor(forge);
            plan.downloads[0].url = url; plan.downloads[0].sha1 = sha1; plan.downloads[0].size = payload.size();
            ServerInstallRun::ServerInstallController execution; execution.setDownloadManager(&downloads);
            execution.setInstallerWatchdogLimits(350, 1400);
            int terminals = 0; bool success = false; QString error;
            QObject::connect(&execution, &ServerInstallRun::ServerInstallController::finished, &execution,
                [&](auto outcome, const QString &text) { ++terminals; success = outcome == ServerInstallRun::Outcome::Succeeded; error = text; });
            execution.start(forge, id, app.applicationFilePath(), &plan);
            expect(wait([&] { return terminals == 1; }) && success == (mode == "retry-success"), "watchdog real process reaches the expected single terminal outcome");
            expect(read(dir + "/attempts").toInt() == (mode == "noisy" ? 1 : 2), "watchdog retries silence once but total deadline ignores noisy output");
            expect(QFileInfo::exists(dir + '/' + plan.downloads.first().fileName), "watchdog failure keeps downloaded installer");
            if (mode != "retry-success") expect(!error.isEmpty() && !ServerRegistry::describe(id).installed, "failed installer is not published as installed");
            if (mode == "failure") {
                expect(write(dir + "/.watchdog-fixture.json", QJsonDocument(QJsonObject{{"format", "wtfml-installer-watchdog-test/1"}, {"root", dir}, {"mode", "retry-success"}}).toJson()), "retry fixture becomes available again");
                terminals = 0; success = false;
                execution.start(forge, id, app.applicationFilePath(), &plan);
                expect(wait([&] { return terminals == 1; }) && success && ServerRegistry::describe(id).installed,
                    "explicit reinstallation reuses retained installer and succeeds after a watchdog failure");
            }
        }
        AppPaths::setMinecraftDir(oldRoot);
    }
    {
        DownloadManager downloader; downloader.setSegmentedDownloadEnabled(false);
        const QString receipt = fixture.filePath("file.receipt"), target = fixture.filePath("existing.bin");
        expect(write(target, "original"), "existing original created");
        downloader.beginBatch(); expect(downloader.enableCheckpoint(receipt, {{"title", "resource batch"}}), "checkpoint enabled only at batch boundary");
        downloader.downloadFile(url, target, sha1, payload.size(), VerifyMode::AlwaysDownload); downloader.endBatch();
        expect(downloader.canPauseBatch() && downloader.pauseBatch() && downloader.batchPaused(), "whole file batch pauses without a failure terminal signal");
        expect(read(target) == "original" && !read(receipt).contains("secret-proof"), "pause retains original and encrypted URL receipt");
        const int before = requests;
        expect(write(target, "external edit"), "simulate external edit while paused");
        QString problem;
        expect(!downloader.resumeBatch(&problem) && problem.contains(QStringLiteral("外部")) && requests == before && read(target) == "external edit", "changed original blocks resume before any network request");
        expect(write(target, "original") && downloader.resumeBatch(&problem), "unchanged original can resume its consented replacement");
        expect(wait([&] { return !downloader.isBusy(); }) && read(target) == payload, "resumed batch verifies artifact and atomically replaces original");
        downloader.forgetCheckpoint();
        expect(!QFileInfo::exists(receipt), "acknowledged successful batch removes receipt");
    }
    {
        DownloadManager downloader; downloader.setSegmentedDownloadEnabled(false);
        const auto target = fixture.filePath("race.bin");
        downloader.beginBatch(); expect(downloader.enableCheckpoint(fixture.filePath("race.receipt"), {}), "race checkpoint created");
        downloader.downloadFile(url, target, sha1, payload.size(), VerifyMode::AlwaysDownload); downloader.endBatch();
        // Dispatch has captured the absent target, then another writer creates it before commit.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        expect(write(target, "keep-other-writer"), "concurrent destination created");
        expect(wait([&] { return !downloader.isBusy(); }) && read(target) == "keep-other-writer" && downloader.stats().failed == 1,
            "commit guard preserves destination created during an overwrite-capable transfer");
        downloader.cancelBatch();
    }
    {
        DownloadManager downloader; downloader.setMaxConcurrentDownloads(1);
        const auto first = fixture.filePath("unknown-one.bin"), second = fixture.filePath("unknown-two.bin");
        const int before = requests;
        downloader.beginBatch(); expect(downloader.enableCheckpoint(fixture.filePath("unknown.receipt"), {}), "no-hash batch receipt created");
        downloader.downloadFile(url, first, {}, payload.size(), VerifyMode::AlwaysDownload);
        downloader.downloadFile(QString(url).replace("/file?", "/hold?"), second, {}, payload.size(), VerifyMode::AlwaysDownload);
        downloader.endBatch();
        expect(wait([&] { return requests - before == 2; }) && downloader.pauseBatch() && read(first) == payload && !QFileInfo::exists(second),
            "pause retains a committed no-hash file and stops its unfinished sibling");
        holdSecond = false; QString problem;
        expect(downloader.resumeBatch(&problem) && wait([&] { return !downloader.isBusy(); }) && read(second) == payload && requests - before == 3,
            "recorded output identity skips a completed no-hash file and downloads only the missing sibling");
        downloader.forgetCheckpoint();
    }
    {
        ActivityCenterController activity; activity.tasks()->setHistoryPath(fixture.filePath("tasks.json"));
        const auto id = activity.begin(Tasks::TaskKind::Download, QStringLiteral("安装测试文件"));
        auto downloader = std::make_unique<DownloadManager>(); downloader->setSegmentedDownloadEnabled(false);
        auto controller = std::make_unique<DownloadBatchController>(&activity, downloader.get(), [id] { return id; });
        const auto first = fixture.filePath("one.bin"), second = fixture.filePath("two.bin");
        downloader->beginBatch(); downloader->downloadFile(url, first, sha1, payload.size()); downloader->downloadFile(url, second, sha1, payload.size()); downloader->endBatch();
        expect(controller->owns(id) && controller->pause(id), "task centre owns a paused multi-file installation download");
        activity.tasks()->saveHistory();
        controller.reset(); downloader.reset();
        QProcess child; auto args = app.arguments().mid(1);
        args << "--batch-resume-root" << fixture.path();
        child.start(app.applicationFilePath(), args);
        expect(wait([&] { return child.state() == QProcess::NotRunning; }) && child.exitCode() == 0 && read(first) == payload && read(second) == payload,
            "fresh OS process restores both files from protected receipt without executing an installer");
        if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(1000); }
        const auto folders = QDir(fixture.filePath("download-batches")).entryList({"*.receipt"}, QDir::Files);
        expect(folders.isEmpty(), "restored successful batch retires execution receipt");
    }
    {
        const auto bad = fixture.filePath("malformed.receipt"); expect(write(bad, "retain damaged receipt"), "damaged receipt fixture created");
        DownloadManager downloader; QString problem;
        expect(!downloader.restoreBatch(bad, &problem) && read(bad) == "retain damaged receipt", "unreadable receipt is preserved and never executed");
    }
    {
        const auto root = fixture.filePath("continuation"); QDir().mkpath(root);
        ActivityCenterController activity; activity.tasks()->setHistoryPath(root + "/tasks.json");
        const auto id = activity.begin(Tasks::TaskKind::Install, QStringLiteral("需要手动接续的安装"));
        auto downloader = std::make_unique<DownloadManager>(); downloader->setSegmentedDownloadEnabled(false);
        auto controller = std::make_unique<DownloadBatchController>(&activity, downloader.get(), [id] { return id; });
        controller->setContinuation([] { return QJsonObject{{"kind", "explicit-test"}}; }, [](Tasks::TaskId, const QJsonObject &) { return false; });
        downloader->beginBatch(); downloader->downloadFile(url, root + "/artifact.bin", sha1, payload.size()); downloader->endBatch();
        expect(controller->pause(id) && activity.tasks()->saveHistory(), "installation intent checkpoints alongside file receipt");
        controller.reset(); downloader.reset();
        QProcess child; auto args = app.arguments().mid(1); args << "--batch-resume-root" << root;
        child.start(app.applicationFilePath(), args);
        expect(wait([&] { return child.state() == QProcess::NotRunning; }) && child.exitCode() == 0 && read(root + "/artifact.bin") == payload,
            "fresh process must wait for a second explicit action before invoking installation continuation exactly once");
        if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(1000); }
    }
    {
        const auto previousRoot = AppPaths::minecraftDir();
        const auto root = fixture.filePath("vm-root"); QDir().mkpath(root); AppPaths::setMinecraftDir(root);
        ActivityCenterController activity; activity.tasks()->setHistoryPath(root + "/tasks.json");
        auto id = activity.begin(Tasks::TaskKind::Download, QStringLiteral("实例资源文件"));
        VersionManager manager; manager.downloads()->setSegmentedDownloadEnabled(false);
        DownloadBatchController controller(&activity, manager.downloads(), [&] { return id; });
        int terminals = 0; bool succeeded = false;
        QObject::connect(&manager, &VersionManager::resourceDownloadFinished, &manager,
            [&](const QString &, bool success, const QString &) { ++terminals; succeeded = success; activity.finish(id, success, {}, false); });
        expect(manager.downloadResourceFiles(root + "/mods", "test-instance", "mods",
            {{url, "a.bin", sha1, payload.size(), {}}, {url, "b.bin", sha1, payload.size(), {}}}) && controller.pause(id),
            "real VersionManager resource executor pauses through the activity controller");
        expect(terminals == 0 && manager.isDownloading() && manager.busyVersionId() == "test-instance", "paused resource batch retains instance ownership without a spurious failure");
        expect(controller.resume(id) && wait([&] { return terminals == 1; }) && succeeded && read(root + "/mods/a.bin") == payload && read(root + "/mods/b.bin") == payload,
            "resource continuation preserves its original VM completion wiring and completes exactly once");

        const auto instance = root + "/versions/core-test"; QDir().mkpath(instance);
        const QJsonObject json{{"id", "core-test"}, {"mainClass", "test.Main"}, {"libraries", QJsonArray()},
            {"downloads", QJsonObject{{"client", QJsonObject{{"url", url}, {"sha1", sha1}, {"size", payload.size()}}}}}};
        expect(write(instance + "/core-test.json", QJsonDocument(json).toJson()), "real installation-file metadata prepared");
        id = activity.begin(Tasks::TaskKind::Install, QStringLiteral("补齐安装文件")); terminals = 0; succeeded = false;
        QObject::connect(&manager, &VersionManager::versionDownloadFinished, &manager,
            [&](const QString &, bool success, const QString &) { ++terminals; succeeded = success; activity.finish(id, success, {}, false); });
        manager.repairVersionFiles("core-test");
        expect(controller.pause(id) && terminals == 0 && manager.busyVersionId() == "core-test", "real core installation download pauses while keeping the instance locked");
        expect(controller.resume(id) && wait([&] { return terminals == 1; }) && succeeded && read(instance + "/core-test.jar") == payload,
            "real core executor continues after a pause and reports one verified completion");
        id = activity.begin(Tasks::TaskKind::Install, QStringLiteral("发布命名实例")); terminals = 0; succeeded = false;
        namedMetadata = QJsonDocument(json).toJson();
        const auto metaUrl = QString(url).replace("/file?", "/named-meta?");
        namedManifest = QJsonDocument(QJsonObject{{"versions", QJsonArray{QJsonObject{{"id", "core-test"}, {"url", metaUrl}, {"type", "release"}}}}}).toJson();
        manager.downloads()->setUseMirror(false);
        manager.downloads()->setVersionManifestUrlOverride(QString(url).replace("/file?", "/named-manifest?"));
        manager.downloads()->fetchVersionManifest();
        expect(wait([&] { return !manager.downloads()->getAvailableVersions().isEmpty(); }), "named installation metadata is provided by the local fixture only");
        manager.installNamedVanilla("named-complete", "core-test");
        expect(wait([&] { return terminals == 1; }) && succeeded && manager.installedVersions().contains("named-complete") &&
            !InstanceFiles::isUnfinishedImport("named-complete") && QFileInfo::exists(AppPaths::versionMetaPath("named-complete")),
            "completed named aliases publish their instance identity and are not mistaken for interrupted imports");
        id = activity.begin(Tasks::TaskKind::Download, QStringLiteral("取消已暂停资源"));
        expect(manager.downloadResourceFiles(root + "/mods", "test-instance", "cancelled",
            {{url, "cancelled.bin", sha1, payload.size(), {}}}) && controller.pause(id), "resource cancellation fixture pauses");
        expect(manager.canCancelDownload() && manager.cancelActiveDownload() && !manager.isDownloading() && !controller.owns(id) &&
            !activity.tasks()->snapshot(id).capabilities.canRecover && !QFileInfo::exists(root + "/mods/cancelled.bin"),
            "explicit cancellation retires the paused VM receipt without resurrecting a recovery action");
        AppPaths::setMinecraftDir(previousRoot);
    }
    {
        const auto root = fixture.filePath("overwrite"); QDir().mkpath(root);
        ActivityCenterController activity; activity.tasks()->setHistoryPath(root + "/tasks.json");
        DirectDownloadController tool(&activity, nullptr);
        const auto target = root + "/original.bin";
        expect(write(target, "old-original"), "direct replacement original created");
        QString problem;
        expect(tool.start({QUrl(url), target}, true, &problem) && tool.pause(tool.taskId()), "confirmed replacement supports pause");
        const auto id = tool.taskId();
        expect(write(target, "user-edited") && !tool.resume(id) && read(target) == "user-edited", "replacement resume never renews consent for externally changed original");
        expect(write(target, "old-original") && tool.resume(id), "replacement resumes after exact original is restored");
        expect(wait([&] { return activity.tasks()->snapshot(id).state == Tasks::TaskState::Succeeded; }) && read(target) == payload,
            "resumed replacement publishes verified full body");
        tool.shutdown();
    }
    return failures;
}
