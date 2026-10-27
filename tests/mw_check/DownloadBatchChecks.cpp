#include "modules/Support.hpp"
#include "controllers/DownloadBatchController.hpp"
#include "controllers/ActivityCenterController.hpp"
#include "download/DownloadCheckpoint.hpp"
#include "download/DownloadManager.hpp"
#include "controllers/DirectDownloadController.hpp"
#include "manage/VersionManager.hpp"
#include "presets/PresetTargetSnapshot.hpp"

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
                if (continued != 0 || !controller.resume(task.id) || continued != 1) return 6;
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
    const QString sha1 = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha1).toHex());
    int requests = 0;
    bool holdSecond = true;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto input = socket->property("input").toByteArray() + socket->readAll(); socket->setProperty("input", input);
                if (!input.contains("\r\n\r\n") || socket->property("sent").toBool()) return;
                socket->setProperty("sent", true); ++requests;
                if (input.split(' ').value(1).startsWith("/hold") && holdSecond) {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\n\r\nc"); return;
                }
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n");
                socket->write(payload); socket->disconnectFromHost();
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
