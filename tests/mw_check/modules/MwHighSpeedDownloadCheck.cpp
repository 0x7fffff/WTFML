#include "Support.hpp"
int runDownloadBatchChecks(QApplication &app);
int MwHighSpeedDownloadCheck(QApplication &app) {
        if (app.arguments().contains("--batch-resume-root")) return runDownloadBatchChecks(app);
        const auto childAt = app.arguments().indexOf(QStringLiteral("--resume-root"));
        if (childAt >= 0) {
            const auto root = app.arguments().value(childAt + 1);
            if (!QDir::isAbsolutePath(root) || !QFileInfo(root + "/wtfml.download-recovery").isFile()) return 2;
            AppPaths::setMinecraftDir(root);
            ActivityCenterController childActivity;
            childActivity.tasks()->setHistoryPath(root + "/child-history.json"); childActivity.loadHistory();
            DirectDownloadController childTool(&childActivity, nullptr);
            QCoreApplication::processEvents();
            Tasks::TaskId id = Tasks::invalidTaskId();
            for (const auto &task : childActivity.tasks()->all())
                if (childTool.owns(task.id) && task.capabilities.canRecover) { id = task.id; break; }
            if (id == Tasks::invalidTaskId() || !childTool.resume(id)) return 3;
            QElapsedTimer timer; timer.start();
            while (Tasks::isActiveState(childActivity.tasks()->snapshot(id).state) && timer.elapsed() < 10000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return childActivity.tasks()->snapshot(id).state == Tasks::TaskState::Succeeded ? 0 : 4;
        }

        QTemporaryDir fixture(QDir::current().filePath(QStringLiteral(".download-tool-XXXXXX")));
        if (!fixture.isValid()) return 2;
        int failures = runDownloadBatchChecks(app);
        const auto expect = [&failures](bool ok, const char *message) {
            printf("%s %s\n", ok ? "PASS" : "FAIL", message);
            fflush(stdout);
            if (!ok) ++failures;
        };
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fixture.path());
        app.setOrganizationName(QStringLiteral("WTFML-download-test"));
        app.setApplicationName(QStringLiteral("isolated-download"));
        Theme::apply(app);
        const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
        const int fontId = QFontDatabase::addApplicationFont(fontPath);
        const QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
        if (!fontFamilies.isEmpty()) app.setFont(QFont(fontFamilies.first(), 9));
        DirectDownloadRequest parsed;
        QString error;
        expect(DirectDownloadRequest::parse(QStringLiteral("https://example.com/file.bin"), fixture.path(), {}, &parsed, &error), "HTTPS direct URL and automatic file name accepted");
        for (const auto &url : {QStringLiteral("file:///C:/secret"), QStringLiteral("ftp://example.com/f"), QStringLiteral("https://u:p@example.com/f"), QStringLiteral("https:///missing-host")})
            expect(!DirectDownloadRequest::parse(url, fixture.path(), {}, &parsed, &error), "invalid scheme, host or credentials rejected");
        for (const auto &name : {QStringLiteral("../escape.bin"), QStringLiteral("CON.txt"), QStringLiteral("a/b"), QStringLiteral("x."), QStringLiteral("a:b")})
            expect(!DirectDownloadRequest::parse(QStringLiteral("https://example.com/f"), fixture.path(), name, &parsed, &error), "unsafe or reserved file name rejected");
        expect(!DirectDownloadRequest::parse(QStringLiteral("https://example.com/f"), QStringLiteral("relative"), {}, &parsed, &error), "relative save directory rejected");
        ActivityCenterController activity;
        activity.tasks()->setHistoryPath(fixture.filePath(QStringLiteral("tasks.json")));
        DirectDownloadController tool(&activity, nullptr);
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) return 2;
        QByteArray body(2 * 1024 * 1024, 'x');
        int requests = 0;
        bool slowNow = true;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                    const auto input = socket->property("input").toByteArray() + socket->readAll();
                    socket->setProperty("input", input);
                    if (!input.contains("\r\n\r\n") || socket->property("sent").toBool()) return;
                    socket->setProperty("sent", true); ++requests;
                    const QByteArray path = input.split(' ').value(1).split('?').first();
                    if (path == "/redirect") { socket->write("HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return; }
                    if (path == "/bad") { socket->write("HTTP/1.1 404 Missing\r\nContent-Length: 3\r\nConnection: close\r\n\r\nbad"); socket->disconnectFromHost(); return; }
                    if (path == "/slow" && slowNow) { socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2097152\r\n\r\nx"); return; }
                    const QByteArray code = path == "/partial" ? "206 Partial Content" : "200 OK";
                    socket->write("HTTP/1.1 " + code + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
                    socket->write(body); socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        const auto request = [&](const QString &endpoint, const QString &file) {
            return DirectDownloadRequest{QUrl(QStringLiteral("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(endpoint)), fixture.filePath(file)};
        };
        const auto wait = [&] {
            QElapsedTimer limit; limit.start();
            while (Tasks::isActiveState(activity.tasks()->snapshot(tool.taskId()).state) && limit.elapsed() < 15000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            expect(!Tasks::isActiveState(activity.tasks()->snapshot(tool.taskId()).state), "task reaches terminal state before deadline");
            if (Tasks::isActiveState(activity.tasks()->snapshot(tool.taskId()).state)) tool.cancel(tool.taskId());
            const auto snapshot = activity.tasks()->snapshot(tool.taskId());
            if (snapshot.state == Tasks::TaskState::Failed) printf("DOWNLOAD_FAILURE: %s\n", snapshot.errorSummary.toUtf8().constData());
        };
        const auto read = [](const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return QByteArray(); return f.readAll(); };
        auto ok = request(QStringLiteral("ok"), QStringLiteral("result.bin"));
        tool.open();
        QCoreApplication::processEvents();
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeWindow());
        expect(dialog && dialog->objectName() == QStringLiteral("downloadToolDialog"), "download opens its own reusable tool window");
        if (dialog) expect(dialog->grab().save(fixture.filePath(QStringLiteral("download-tool-preview.png"))), "tool window preview renders successfully in isolated fixture");
        expect(tool.start(ok, false, &error), "direct download starts asynchronously");
        const auto first = tool.taskId();
        if (dialog) dialog->close();
        wait();
        expect(activity.tasks()->snapshot(first).state == Tasks::TaskState::Succeeded && read(ok.path) == body, "closing window does not interrupt task; file and ledger succeed");
        expect(activity.tasks()->snapshot(first).bytesDone == body.size(), "task centre retains final byte progress");
        auto conflict = request(QStringLiteral("ok"), QStringLiteral("conflict.bin"));
        expect(tool.start(conflict, false, &error), "new destination starts without overwrite permission");
        { QFile file(conflict.path); expect(file.open(QIODevice::WriteOnly) && file.write("created-by-another-writer") == 25, "concurrent writer fixture created"); }
        wait();
        expect(activity.tasks()->snapshot(tool.taskId()).state == Tasks::TaskState::Failed && read(conflict.path) == "created-by-another-writer", "destination appearing during transfer is preserved without overwrite consent");
        expect(!tool.start(ok, false, &error), "existing destination requires overwrite confirmation");
        auto bad = request(QStringLiteral("bad"), QStringLiteral("result.bin"));
        bad.url.setQuery(QStringLiteral("token=private-download-proof"));
        expect(tool.start(bad, true, &error), "confirmed overwrite starts"); wait();
        expect(activity.tasks()->snapshot(tool.taskId()).state == Tasks::TaskState::Failed && read(ok.path) == body, "HTTP failure preserves previous destination");
        expect(activity.tasks()->saveHistory() && !read(activity.tasks()->historyPath()).contains("private-download-proof") &&
            activity.tasks()->snapshot(tool.taskId()).errorSummary.contains(QStringLiteral("/bad")),
            "signed URL failure keeps useful endpoint information without persisting URL credentials in task history");
        auto partial = request(QStringLiteral("partial"), QStringLiteral("partial.bin"));
        expect(tool.start(partial, false, &error), "partial-response fixture starts"); wait();
        expect(activity.tasks()->snapshot(tool.taskId()).state == Tasks::TaskState::Failed && !QFileInfo::exists(partial.path), "unsolicited HTTP 206 is never committed as complete file");
        auto redirect = request(QStringLiteral("redirect"), QStringLiteral("redirect.bin"));
        expect(tool.start(redirect, false, &error), "redirect fixture starts"); wait();
        expect(activity.tasks()->snapshot(tool.taskId()).state == Tasks::TaskState::Succeeded && read(redirect.path) == body, "safe HTTP redirect downloads complete body");
        auto slow = request(QStringLiteral("slow"), QStringLiteral("result.bin"));
        expect(tool.start(slow, true, &error), "cancel fixture starts");
        const auto id = tool.taskId();
        expect(tool.cancel(first) && tool.canCancel(id), "stale action for an older direct task cannot cancel the new task");
        expect(!tool.cancel(id + 1000) && tool.canCancel(id), "cancel is scoped to exact task and cannot cancel another operation");
        QTimer::singleShot(150, &tool, [&] { tool.cancel(id); }); wait();
        expect(activity.tasks()->snapshot(id).state == Tasks::TaskState::Cancelled && read(ok.path) == body, "cancel preserves existing file and records cancellation, not failure");
        // 校验失败也不得先替换旧目标；原版/加载器下载共用这条流式提交路径。
        const QString kept = fixture.filePath(QStringLiteral("kept.bin"));
        { QFile file(kept); expect(file.open(QIODevice::WriteOnly) && file.write("old") == 3, "original-file fixture created"); }
        { StreamedFile stream(kept); stream.file.write("bad"); stream.bytes = 3; stream.hash.addData(QByteArray("bad"));
          expect(!stream.finish(QString(40, QLatin1Char('0')), 3, false).isEmpty(), "bad hash rejected before atomic commit"); }
        expect(read(kept) == "old", "hash failure leaves original bytes untouched");
        {
            const auto previousRoot = AppPaths::minecraftDir();
            AppPaths::setMinecraftDir(fixture.filePath("recovery-data"));
            ActivityCenterController firstSession;
            const auto history = fixture.filePath("recovery-data/child-history.json");
            firstSession.tasks()->setHistoryPath(history);
            auto firstTool = std::make_unique<DirectDownloadController>(&firstSession, nullptr);
            const auto recoverRequest = request("slow", "recover.bin");
            expect(firstTool->start(recoverRequest, false, &error), "recoverable absent-target download starts");
            const auto recoverId = firstTool->taskId();
            QElapsedTimer deadline; deadline.start();
            while (firstSession.tasks()->snapshot(recoverId).bytesTotal < 0 && deadline.elapsed() < 2000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            expect(firstTool->pause(recoverId) && firstSession.tasks()->snapshot(recoverId).state == Tasks::TaskState::Paused,
                "pause stops the executor and records Paused instead of Cancelled");
            expect(!firstTool->start(recoverRequest, false, &error), "paused target remains reserved against duplicate submission");
            expect(!read(AppPaths::minecraftDir() + "/wtfml.download-recovery").contains("http://127.0.0.1"),
                "persisted recovery does not expose the source URL as plaintext");
            firstTool.reset();
            ActivityCenterController secondSession;
            secondSession.tasks()->setHistoryPath(history); secondSession.loadHistory();
            DirectDownloadController secondTool(&secondSession, nullptr);
            QCoreApplication::processEvents();
            expect(secondTool.owns(recoverId) && secondSession.tasks()->snapshot(recoverId).capabilities.canRecover,
                "new controller restores the exact task without automatically starting network I/O");
            slowNow = false;
            expect(secondTool.resume(recoverId) && !secondTool.resume(recoverId), "explicit resume queues only once");
            deadline.restart();
            while (Tasks::isActiveState(secondSession.tasks()->snapshot(recoverId).state) && deadline.elapsed() < 5000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            expect(secondSession.tasks()->snapshot(recoverId).state == Tasks::TaskState::Succeeded && read(recoverRequest.path) == body,
                "restored executor re-probes and publishes complete bytes under the same task identity");
            slowNow = true;
            const auto changed = request("slow", "external.bin");
            expect(secondTool.start(changed, false, &error), "external-change recovery fixture starts");
            const auto changedId = secondTool.taskId();
            expect(secondTool.pause(changedId), "external-change fixture pauses");
            { QFile file(changed.path); expect(file.open(QIODevice::WriteOnly) && file.write("external") == 8, "external file fixture is written completely"); }
            expect(!secondTool.resume(changedId) && read(changed.path) == "external", "resume refuses externally-created target without overwriting it");
            secondTool.cancel(changedId);
            const auto childRequest = request("slow", "child-process.bin");
            expect(secondTool.start(childRequest, false, &error), "cross-process recovery fixture starts");
            const auto childId = secondTool.taskId();
            expect(secondTool.pause(childId), "cross-process recovery fixture checkpoints before process replacement");
            secondTool.shutdown(); // Release the old executor and registry ownership before the new process opens it.
            const auto childRoot = AppPaths::minecraftDir();
            expect(firstSession.tasks()->historyPath() == history, "parent and child use an explicit isolated history path");
            expect(QFileInfo::exists(childRoot + "/child-history.json"), "child receives the real persisted task history");
            slowNow = false;
            QProcess child; QEventLoop childLoop;
            QObject::connect(&child, &QProcess::finished, &childLoop, &QEventLoop::quit);
            QTimer::singleShot(15000, &childLoop, [&] { child.kill(); childLoop.quit(); });
            QStringList args;
            if (app.arguments().value(1) == QStringLiteral("mw_check_high_speed_download")) args << app.arguments().value(1);
            args << "--resume-root" << childRoot;
            child.start(app.applicationFilePath(), args); childLoop.exec();
            if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(2000); }
            expect(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0 && read(childRequest.path) == body,
                "a fresh OS process restores the persisted executor and publishes the complete file");
            // The live parent intentionally does not mutate the child's completed ledger.
            // A separate new controller exercises explicit renewal of an expired source.
            ActivityCenterController renewalActivity;
            renewalActivity.tasks()->setHistoryPath(fixture.filePath("renewal-data/child-history.json"));
            DirectDownloadController renewal(&renewalActivity, nullptr);
            const auto expired = request("bad", "expired-source.bin");
            expect(renewal.start(expired, false, &error), "expired-source fixture starts");
            const auto renewId = renewal.taskId(); deadline.restart();
            while (Tasks::isActiveState(renewalActivity.tasks()->snapshot(renewId).state) && deadline.elapsed() < 3000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            auto renewed = expired; renewed.url = request("ok", "unused").url;
            expect(renewal.start(renewed, false, &error) && renewal.lastSubmittedTaskId() == renewId,
                "explicit replacement URL resumes the existing failed task instead of making a duplicate");
            deadline.restart();
            while (Tasks::isActiveState(renewalActivity.tasks()->snapshot(renewId).state) && deadline.elapsed() < 3000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            expect(read(expired.path) == body && renewalActivity.tasks()->snapshot(renewId).state == Tasks::TaskState::Succeeded,
                "renewed source publishes verified complete response bytes");
            slowNow = true;
            expect(renewal.start(request("slow", "paused-cancel.bin"), false, &error), "paused cancellation fixture starts");
            const auto abandoned = renewal.taskId();
            expect(renewal.pause(abandoned), "paused cancellation fixture checkpoints");
            const auto registry = QFileInfo(renewalActivity.tasks()->historyPath()).absolutePath() + "/wtfml.download-recovery";
            const auto staleJournal = read(registry);
            expect(renewal.cancel(abandoned) && !renewal.resume(abandoned) &&
                !renewalActivity.tasks()->snapshot(abandoned).capabilities.canRecover,
                "explicit cancellation of paused download removes both executable record and recovery action");
            renewal.shutdown();
            { QFile journal(registry); expect(journal.open(QIODevice::WriteOnly) && journal.write(staleJournal) == staleJournal.size(), "simulate stale journal retained after a failed final persistence"); }
            ActivityCenterController restoredCancelled;
            restoredCancelled.tasks()->setHistoryPath(renewalActivity.tasks()->historyPath()); restoredCancelled.loadHistory();
            DirectDownloadController restoredTool(&restoredCancelled, nullptr); QCoreApplication::processEvents();
            expect(!restoredTool.owns(abandoned) && restoredCancelled.tasks()->snapshot(abandoned).state == Tasks::TaskState::Cancelled,
                "stale executable record never resurrects a task whose cancellation was durably acknowledged");
            AppPaths::setMinecraftDir(previousRoot);
        }
        const QString live = qEnvironmentVariable("WTFML_LIVE_DIRECT_URL");
        if (!live.isEmpty()) {
            DirectDownloadRequest external;
            expect(DirectDownloadRequest::parse(live, fixture.path(), QStringLiteral("live.bin"), &external, &error), "live HTTPS request parses");
            if (tool.start(external, false, &error)) {
                QElapsedTimer deadline; deadline.start();
                while (tool.canCancel(tool.taskId()) && deadline.elapsed() < 60000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                if (tool.canCancel(tool.taskId())) tool.cancel(tool.taskId());
                expect(activity.tasks()->snapshot(tool.taskId()).state == Tasks::TaskState::Succeeded, "real HTTPS direct download completes within 60 seconds");
            } else expect(false, "live download starts");
        }
        return failures ? 1 : 0;
    
    return 0;
}

