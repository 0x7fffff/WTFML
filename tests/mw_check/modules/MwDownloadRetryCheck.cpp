#include "Support.hpp"
int runRequestBudgetChecks();
int MwDownloadRetryCheck() {

        QTemporaryDir fixture;
        if (!fixture.isValid()) return 2;
        int failures = runRequestBudgetChecks();
        const auto expect = [&failures](bool ok, const char *text) {
            printf("%s %s\n", ok ? "PASS" : "FAIL", text);
            if (!ok) ++failures;
        };
        expect(DownloadRetryPolicy::displayError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation canceled"))
                   .contains(QStringLiteral("网络请求已中止")), "network abort has Chinese text without claiming user cancellation");
        expect(DownloadRetryPolicy::displayError(QNetworkReply::TimeoutError, QStringLiteral("Timeout"))
                   .contains(QStringLiteral("超时")), "network timeout has Chinese text");
        expect(DownloadRetryPolicy::displayError(QNetworkReply::HostNotFoundError, QStringLiteral("diagnostic")) == QStringLiteral("diagnostic"),
               "unmapped network errors preserve their diagnostic text");
        const auto safeError = DownloadRetryPolicy::displayError(QNetworkReply::ContentAccessDenied,
            QStringLiteral("https://user:password@example.com/a?token=secret#fragment then HTTP://example.net/b?key=other failed"));
        expect(safeError.contains(QStringLiteral("example.com/a")) && safeError.contains(QStringLiteral("example.net/b")) &&
            !safeError.contains(QStringLiteral("secret")) && !safeError.contains(QStringLiteral("password")) &&
            !safeError.contains(QStringLiteral("other")) && !safeError.contains(QStringLiteral("fragment")),
            "network errors strip credentials and signed queries from every URL while retaining endpoints");
        const auto run = [&](int status, int failCount, bool cancel) {
            QTcpServer server;
            if (!server.listen(QHostAddress::LocalHost, 0)) { ++failures; return; }
            int requests = 0, results = 0, batchResults = 0;
            bool succeeded = false;
            const QByteArray body("retry-ok");
            QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
                while (server.hasPendingConnections()) {
                    auto *socket = server.nextPendingConnection();
                    QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                        const auto request = socket->property("request").toByteArray() + socket->readAll();
                        socket->setProperty("request", request);
                        if (!request.contains("\r\n\r\n") || socket->property("responded").toBool()) return;
                        socket->setProperty("responded", true);
                        const int code = ++requests <= failCount ? status : 200;
                        const bool truncated = status == 200 && requests <= failCount;
                        socket->write("HTTP/1.1 " + QByteArray::number(code) + " Response\r\nConnection: close\r\nContent-Length: " +
                                      QByteArray::number(body.size() + (truncated ? 8 : 0)) + "\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
                    QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                }
            });
            DownloadManager downloads;
            downloads.setUseMirror(false);
            QEventLoop loop;
            QTimer deadline;
            deadline.setSingleShot(true);
            QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
            QObject::connect(&downloads, &DownloadManager::downloadFinished, [&](const QString &, bool ok, const QString &) {
                ++results; succeeded = ok;
            });
            QObject::connect(&downloads, &DownloadManager::allDownloadsFinished, &loop, [&](bool) { ++batchResults; loop.quit(); });
            const QString path = fixture.filePath(QStringLiteral("%1-%2-%3.bin").arg(status).arg(failCount).arg(cancel));
            deadline.start(20000); // 并行实装/杀毒扫描时保留调度余量，仍有确定截止时间。
            downloads.beginBatch();
            downloads.downloadFile(QStringLiteral("http://127.0.0.1:%1/file").arg(server.serverPort()), path,
                QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha1).toHex()), body.size());
            downloads.endBatch();
            if (cancel) QTimer::singleShot(150, &downloads, [&] { downloads.cancelBatch(); QTimer::singleShot(700, &loop, &QEventLoop::quit); });
            loop.exec();
            expect(deadline.isActive(), "retry scenario finishes within deadline");
            if (cancel) {
                expect(requests == 1 && !downloads.isBusy() && results == 0 && batchResults == 0,
                       "cancel during backoff sends no retry and emits no failure");
            } else {
                const bool recoverable = status == 503 || status == 200;
                const int expected = recoverable ? qMin(failCount + 1, 4) : 1;
                expect(requests == expected && results == 1 && batchResults == 1,
                       "bounded retries preserve exactly one file and batch result");
                expect(succeeded == (recoverable && failCount < 4), "retry success matches server recovery");
                expect(downloads.stats().total == 1 && downloads.stats().done == 1,
                       "retries never inflate total or completed job counts");
                expect(QFileInfo::exists(path) == succeeded, "failed responses never become completed files");
            }
        };
        run(503, 2, false);
        run(200, 2, false);
        run(503, 10, false);
        run(404, 10, false);
        run(503, 10, true);
        expect(DownloadRetryPolicy::transient(QNetworkReply::TimeoutError, 0) &&
                   DownloadRetryPolicy::transient(QNetworkReply::RemoteHostClosedError, 200) &&
                   DownloadRetryPolicy::transient(QNetworkReply::ProtocolFailure, 200) &&
                   !DownloadRetryPolicy::transient(QNetworkReply::OperationCanceledError, 0) &&
                   !DownloadRetryPolicy::transient(QNetworkReply::SslHandshakeFailedError, 0) &&
                   DownloadRetryPolicy::delayMs(3, {}) == -1 && DownloadRetryPolicy::delayMs(0, "999999999") == 60000,
               "policy retries weak-network failures, caps Retry-After and excludes cancel/TLS errors");
        // 429 专用退避：服务器说"你太快了"时，通用那条 0.5/1/2 秒比限流窗口还快
        // （诊断日志：一条分段 3 秒内连吃 4 个 429）。没给 Retry-After 就
        // 2/4/8 秒；给了就听服务器的。`delayMs()` 那一档一个字都不许跟着变。
        expect(DownloadRetryPolicy::throttleDelayMs(0, {}) == 2000 &&
                   DownloadRetryPolicy::throttleDelayMs(1, {}) == 4000 &&
                   DownloadRetryPolicy::throttleDelayMs(2, {}) == 8000 &&
                   DownloadRetryPolicy::throttleDelayMs(3, {}) == -1 &&
                   DownloadRetryPolicy::throttleDelayMs(0, "7") == 7000 &&
                   DownloadRetryPolicy::delayMs(0, {}) == 500,
               "429 backoff starts at two seconds, doubles, and honours Retry-After");
        // 握手**没做完**（明文回到 TLS 端口 / 连接被掐）换一条连接就可能好；证书判断失败
        // 一万次也一样 —— 这两档必须分开（一个 wrong version number 打断整段安装）。
        expect(DownloadRetryPolicy::sslHandshakeTransient(
                   QByteArrayLiteral("SSL handshake failed: error:0A00010B:SSL routines::wrong version number")) &&
                   DownloadRetryPolicy::sslHandshakeTransient(QByteArrayLiteral("unexpected eof while reading")) &&
                   !DownloadRetryPolicy::sslHandshakeTransient(
                       QByteArrayLiteral("The certificate has expired")) &&
                   !DownloadRetryPolicy::sslHandshakeTransient(
                       QByteArrayLiteral("The host name did not match any of the valid hosts")) &&
                   !DownloadRetryPolicy::transient(QNetworkReply::SslHandshakeFailedError, 0),
               "retry policy retries aborted TLS handshakes but never certificate verdicts");
        expect(InstallerFailure::certificateFailure(QStringLiteral("sun.security.validator.ValidatorException: PKIX path building failed")) &&
                   InstallerFailure::certificateFailure(QStringLiteral("Failed to validate certificates for https://launchermeta.mojang.com/")) &&
                   !InstallerFailure::certificateFailure(QStringLiteral("HTTP 404: not found")),
               "installer certificate diagnosis recognizes Java trust errors without misclassifying HTTP errors");
        {
            const QByteArray payload(32, 'x');
            int requests = 0, results = 0, successes = 0;
            QTemporaryDir root;
            QTcpServer server;
            expect(root.isValid() && server.listen(QHostAddress::LocalHost, 0), "speed debt fixture starts");
            QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
                while (auto *socket = server.nextPendingConnection()) {
                    QObject::connect(socket, &QTcpSocket::readyRead, &server, [&, socket] {
                        const auto bytes = socket->property("request").toByteArray() + socket->readAll();
                        socket->setProperty("request", bytes);
                        if (!bytes.contains("\r\n\r\n") || socket->property("sent").toBool()) return;
                        socket->setProperty("sent", true);
                        ++requests;
                        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 32\r\nConnection: close\r\n\r\n" + payload);
                        socket->disconnectFromHost();
                    });
                }
            });
            DownloadManager manager;
            manager.setSpeedLimitBytesPerSecond(64);
            const QString url = QStringLiteral("http://127.0.0.1:%1/file").arg(server.serverPort());
            QObject::connect(&manager, &DownloadManager::downloadFinished, &manager,
                             [&](const QString &, bool ok, const QString &) {
                ++results;
                successes += ok ? 1 : 0;
                if (results == 1 && ok) {
                    manager.downloadFile(url, root.filePath(QStringLiteral("second.dat")), {}, payload.size());
                    QTimer::singleShot(0, &manager, [&] { manager.setSpeedLimitBytesPerSecond(0); });
                }
            });
            manager.downloadFile(url, root.filePath(QStringLiteral("first.dat")), {}, payload.size());
            QElapsedTimer deadline;
            deadline.start();
            while (results < 2 && deadline.elapsed() < 2500) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            expect(results == 2 && successes == 2 && requests == 2,
                   "disabling speed limit wakes a queue paused on final-buffer token debt");
            manager.cancelBatch();
        }
        return failures == 0 ? 0 : 1;
    
    return 0;
}

