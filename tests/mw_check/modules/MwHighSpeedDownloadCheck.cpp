#include "Support.hpp"
int MwHighSpeedDownloadCheck(QApplication &app) {

        QTemporaryDir fixture(QDir::current().filePath(QStringLiteral(".download-tool-XXXXXX")));
        if (!fixture.isValid()) return 2;
        int failures = 0;
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
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                    const auto input = socket->property("input").toByteArray() + socket->readAll();
                    socket->setProperty("input", input);
                    if (!input.contains("\r\n\r\n") || socket->property("sent").toBool()) return;
                    socket->setProperty("sent", true); ++requests;
                    const QByteArray path = input.split(' ').value(1);
                    if (path == "/redirect") { socket->write("HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return; }
                    if (path == "/bad") { socket->write("HTTP/1.1 404 Missing\r\nContent-Length: 3\r\nConnection: close\r\n\r\nbad"); socket->disconnectFromHost(); return; }
                    if (path == "/slow") { socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2097152\r\n\r\nx"); return; }
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
            while (tool.canCancel(tool.taskId()) && limit.elapsed() < 15000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            expect(!tool.canCancel(tool.taskId()), "task reaches terminal state before deadline");
            if (tool.canCancel(tool.taskId())) tool.cancel(tool.taskId());
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
        expect(tool.start(bad, true, &error), "confirmed overwrite starts"); wait();
        expect(activity.tasks()->snapshot(tool.taskId()).state == Tasks::TaskState::Failed && read(ok.path) == body, "HTTP failure preserves previous destination");
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

