#include "RangeChecks.hpp"
#include <functional>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonObject>
#include "download/ResumableDownload.hpp"
#include "download/DownloadRate.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QThread>
#include <cstdio>
#include <cstdlib>

int runRangeChild(const QStringList &args) {
    if (args.size() < 5) return 2;
    ResumableDownload download;
    QObject::connect(&download, &ResumableDownload::checkpointSaved, &download, [](qint64) { std::_Exit(0); });
    QObject::connect(&download, &ResumableDownload::finished, &download, [](bool, const QString &) { QCoreApplication::exit(3); });
    QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, [] { QCoreApplication::exit(124); });
    deadline.start(15000);
    download.start(QUrl(args.at(2)), args.at(3), {}, -1, true, args.at(4));
    return QCoreApplication::exec();
}

int runRangeChecks() {
    QTemporaryDir fixture(QDir::current().filePath(QStringLiteral(".range-check-XXXXXX")));
    if (!fixture.isValid()) return 2;
    int failures = 0;
    auto expect = [&](bool ok, const char *text) { printf("%s %s\n", ok ? "PASS" : "FAIL", text); fflush(stdout); if (!ok) ++failures; };
    {
        DownloadRate rate;
        rate.reset();
        expect(rate.bytesPerSecond(0) < 0 && rate.remainingSeconds(0, 100000) < 0,
               "telemetry never invents speed or ETA before receiving bytes");
        rate.sample(1000, 1000);
        expect(rate.remainingSeconds(1000, 99000) < 0, "ETA waits for a two-second warmup");
        rate.sample(2000, 2000);
        expect(rate.bytesPerSecond(2000) == 1000 && rate.remainingSeconds(2000, 98000) == 98,
               "constant transfer rate produces a truthful ETA");
        rate.sample(3000, 12000);
        expect(rate.bytesPerSecond(3000) > 1000 && rate.bytesPerSecond(3000) < 4000,
               "a one-second burst cannot multiply the displayed speed tenfold");
        const auto eta = rate.remainingSeconds(3000, 88000);
        expect(eta > 80 && eta < 98, "ETA blends bursts instead of jumping to their instantaneous estimate");
        expect(rate.remainingSeconds(3000, -1) < 0, "unknown total never produces an ETA");
        rate.sample(9000, 12000);
        expect(rate.bytesPerSecond(9000) == 0 && rate.remainingSeconds(9000, 88000) < 0,
               "a real stall clears the countdown rather than pretending it is still downloading");
        rate.reset(10000, 500000);
        rate.sample(11000, 501000);
        expect(rate.bytesPerSecond(11000) == 1000, "resumed baseline bytes are not counted as network speed");
        rate.sample(12000, 100);
        expect(rate.bytesPerSecond(12000) < 0, "a reset counter starts a fresh telemetry window");
    }
    QByteArray body(8 * 1024 * 1024, 'x');
    for (qsizetype i = 0; i < body.size(); ++i) body[i] = char(i % 251);
    QByteArray second(body.size(), 'z');
    bool version2 = false, holdOthers = false;
    int active = 0, peak = 0, full = 0;
    QList<QPair<qint64, qint64>> ranges;
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return 2;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto input = socket->property("input").toByteArray() + socket->readAll();
                socket->setProperty("input", input);
                if (!input.contains("\r\n\r\n") || socket->property("sent").toBool()) return;
                socket->setProperty("sent", true);
                const QByteArray endpoint = input.split(' ').value(1);
                qint64 begin = -1, end = -1;
                for (auto line : input.split('\n')) {
                    line = line.trimmed();
                    if (line.toLower().startsWith("range: bytes=")) {
                        const auto span = line.mid(13).split('-');
                        if (span.size() == 2) { begin = span[0].toLongLong(); end = span[1].toLongLong(); }
                    }
                }
                const bool probing = begin == 0 && end == 0;
                if (begin >= 0 && !probing) ranges.append({begin, end});
                if (begin < 0) ++full;
                QByteArray data = version2 || (!probing && endpoint == "/changing") ? second : body;
                QByteArray etag = version2 || (!probing && endpoint == "/changing") ? QByteArray("\"v2\"") : QByteArray("\"v1\"");
                // 「来源规则」那几行（见 docs/features/HIGH_SPEED_DOWNLOAD.md 的「验证」表）：
                // · /small     ：正文小于分段下限（4 MiB）⇒ 不该拆段。
                // · /no-etag   ：一个校验凭据都不给 ⇒ 不该跨请求拼接。
                // · /weak-etag ：只有弱 ETag（W/"…"）⇒ 等于没有强校验。
                // · /huge      ：探测回来的总长（40 GiB）超过分段上限（kMaximumBytes = 32 GiB）⇒ 不该拆段。
                // · /expired   ：分段请求撞上"预签名地址过期"那种 403 ⇒ 报错，不悄悄整包重下。
                if (endpoint == "/small") data = body.left(2 * 1024 * 1024);
                if (endpoint == "/no-etag") etag.clear();
                if (endpoint == "/weak-etag") etag = QByteArray("W/\"v1\"");
                const QByteArray etagHeader = etag.isEmpty() ? QByteArray()
                                                             : "ETag: " + etag + "\r\n";
                QByteArray response;
                if (probing && endpoint == "/huge") {
                    response = "HTTP/1.1 206 Partial Content\r\nContent-Length: 1\r\nContent-Range: bytes 0-0/42949672960\r\nETag: \"huge\"\r\nConnection: close\r\n\r\n" + data.left(1);
                } else if (begin < 0 || endpoint == "/ignore") {
                    response = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(data.size()) + "\r\n" + etagHeader + "Connection: close\r\n\r\n" + data;
                } else if (!probing && endpoint == "/expired") {
                    response = "HTTP/1.1 403 Forbidden\r\nContent-Length: 9\r\nConnection: close\r\n\r\nexpired!!";
                } else if (!probing && endpoint == "/416") {
                    response = "HTTP/1.1 416 Invalid\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                } else {
                    const auto contentStart = endpoint == "/bad-range" && !probing ? begin + 1 : begin;
                    response = "HTTP/1.1 206 Partial Content\r\nContent-Length: " + QByteArray::number(end - begin + 1) +
                        "\r\nContent-Range: bytes " + QByteArray::number(contentStart) + "-" + QByteArray::number(end) + "/" + QByteArray::number(data.size()) +
                        "\r\n" + etagHeader + "Connection: close\r\n\r\n" + data.mid(begin, end - begin + 1);
                }
                if (!probing && begin >= 0) {
                    ++active; peak = qMax(peak, active); socket->setProperty("activePart", true);
                }
                const int delay = probing || begin < 0 ? 0 : (holdOthers && begin > 0 ? 800 : 30);
                QTimer::singleShot(delay, socket, [socket, response] { socket->write(response); socket->disconnectFromHost(); });
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, [&, socket] {
                if (socket->property("activePart").toBool()) --active;
                socket->deleteLater();
            });
        }
    });
    const QString cache = fixture.filePath(QStringLiteral("cache"));
    const auto url = [&](const QString &path) { return QUrl(QStringLiteral("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(path)); };
    const auto read = [](const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return QByteArray(); return file.readAll(); };
    const auto pump = [&](const std::function<bool()> &done, int limit = 20000) {
        QElapsedTimer time; time.start();
        while (!done() && time.elapsed() < limit) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
        return done();
    };
    const auto run = [&](const QString &endpoint, const QString &target, const QByteArray &expected) {
        ResumableDownload download;
        bool done = false, success = false; QString problem;
        QObject::connect(&download, &ResumableDownload::finished, &download, [&](bool ok, const QString &error) { done = true; success = ok; problem = error; });
        download.start(url(endpoint), target, {}, -1, true, cache);
        expect(pump([&] { return done; }), "transfer terminates within deadline");
        if (!success) fprintf(stderr, "RANGE ERROR %s\n", qPrintable(problem));
        expect(success && read(target) == expected, "assembled or fallback output matches complete source bytes");
    };
    // 分段请求本身出问题时（Content-Range 对不上 / 416 / ETag 变了）的现行口径
    // （docs/features/HIGH_SPEED_DOWNLOAD.md「分段下载」）：先只补缺的段，
    // 补投仍失败就保留缓存并报错，**不自动整包重下**；目标文件绝不落半截错内容。
    const auto runRejected = [&](const QString &endpoint, const QString &target) {
        ResumableDownload download;
        bool done = false, success = true; QString problem;
        QObject::connect(&download, &ResumableDownload::finished, &download,
                         [&](bool ok, const QString &error) { done = true; success = ok; problem = error; });
        download.start(url(endpoint), target, {}, -1, true, cache);
        expect(pump([&] { return done; }), "rejected transfer terminates within deadline");
        expect(!success && !problem.isEmpty(), "rejected ranges fail with a reason");
        expect(!QFileInfo::exists(target), "rejected ranges never publish a partial target");
        if (!success) fprintf(stderr, "RANGE ERROR %s\n", qPrintable(problem));
    };
    run(QStringLiteral("range"), fixture.filePath(QStringLiteral("parallel.bin")), body);
    expect(ranges.size() == 4 && peak >= 2 && peak <= 4 && full == 0, "large file downloads four bounded parallel ranges without whole-file request");
    ranges.clear(); full = 0;
    run(QStringLiteral("ignore"), fixture.filePath(QStringLiteral("ignore.bin")), body);
    expect(full == 1 && ranges.isEmpty(), "server ignoring probe Range falls back to exactly one ordinary GET");
    full = 0; ranges.clear();
    runRejected(QStringLiteral("bad-range"), fixture.filePath(QStringLiteral("bad-range.bin")));
    expect(full == 0 && ranges.size() == 8,
           "invalid Content-Range never gets stitched; missing parts are retried once, not a whole-file re-download");
    full = 0; ranges.clear();
    runRejected(QStringLiteral("416"), fixture.filePath(QStringLiteral("416.bin")));
    expect(full == 0 && ranges.size() == 8,
           "HTTP 416 on a part is retried part-wise, never a whole-file re-download");
    full = 0; ranges.clear();
    runRejected(QStringLiteral("changing"), fixture.filePath(QStringLiteral("changed.bin")));
    expect(full == 0 && ranges.size() == 8,
           "ETag change mid-download never mixes representations and never silently re-downloads the whole file");

    full = 0; ranges.clear();
    run(QStringLiteral("no-etag"), fixture.filePath(QStringLiteral("no-etag.bin")), body);
    expect(full == 1 && ranges.isEmpty(),
           "no strong validator means no cross-request stitching: one ordinary GET");

    full = 0; ranges.clear();
    run(QStringLiteral("weak-etag"), fixture.filePath(QStringLiteral("weak-etag.bin")), body);
    expect(full == 1 && ranges.isEmpty(), "a weak ETag is not a strong validator: one ordinary GET");

    full = 0; ranges.clear();
    run(QStringLiteral("small"), fixture.filePath(QStringLiteral("small.bin")),
        body.left(2 * 1024 * 1024));
    expect(full == 1 && ranges.isEmpty(), "a file below the segment floor is downloaded whole");

    full = 0; ranges.clear();
    run(QStringLiteral("huge"), fixture.filePath(QStringLiteral("huge.bin")), body);
    expect(full == 1 && ranges.isEmpty(),
           "a declared total above the segment ceiling is downloaded whole");

    full = 0; ranges.clear();
    runRejected(QStringLiteral("expired"), fixture.filePath(QStringLiteral("expired.bin")));
    expect(full == 0 && !ranges.isEmpty(),
           "an expired signed URL on a part fails loudly without silently re-downloading the whole file");

    const auto crashCheckpoint = [&](const QString &target) {
        holdOthers = true; version2 = false;
        QProcess child;
        child.setProgram(QCoreApplication::applicationFilePath());
        child.setWorkingDirectory(fixture.path());
        child.setArguments({QStringLiteral("range-resume-child"), url(QStringLiteral("range")).toString(), target, cache});
        child.start();
        expect(pump([&] { return child.state() == QProcess::NotRunning; }, 20000), "checkpoint child exits before deadline");
        if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(3000); }
        expect(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, "child hard-exits after an atomic completed-part checkpoint");
        holdOthers = false;
    };
    auto target = fixture.filePath(QStringLiteral("resume.bin"));
    crashCheckpoint(target);
    ranges.clear(); full = 0;
    run(QStringLiteral("range"), target, body);
    expect(!ranges.contains({0, 2 * 1024 * 1024 - 1}) && ranges.size() == 3, "new process reuses checkpointed first segment without downloading it again");
    target = fixture.filePath(QStringLiteral("corrupt.bin"));
    crashCheckpoint(target);
    { QFile file(QDir(ResumableDownload::cacheDirectory(url(QStringLiteral("range")), target, cache)).filePath(QStringLiteral("part-0.bin")));
      expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(QByteArray(2 * 1024 * 1024, 'z')) == 2 * 1024 * 1024, "corrupted-cache fixture preserves length but changes bytes"); }
    ranges.clear();
    run(QStringLiteral("range"), target, body);
    expect(ranges.contains({0, 2 * 1024 * 1024 - 1}), "same-length corrupted segment is detected and downloaded again");
    target = fixture.filePath(QStringLiteral("new-source.bin"));
    crashCheckpoint(target);
    version2 = true; ranges.clear();
    run(QStringLiteral("range"), target, second);
    expect(ranges.size() == 4, "changed ETag across restart invalidates all previous segments");
    version2 = false;
    target = fixture.filePath(QStringLiteral("cancel-resume.bin"));
    {
        ResumableDownload download;
        bool cancelled = false;
        holdOthers = true;
        QObject::connect(&download, &ResumableDownload::checkpointSaved, &download, [&](qint64) {
            cancelled = download.cancel();
        });
        download.start(url(QStringLiteral("range")), target, {}, -1, true, cache);
        expect(pump([&] { return cancelled; }) && !download.isRunning() && !QFileInfo::exists(target),
               "cancellation after checkpoint never publishes a partial target");
        holdOthers = false;
    }
    ranges.clear();
    run(QStringLiteral("range"), target, body);
    expect(!ranges.contains({0, 2 * 1024 * 1024 - 1}), "cancelled download reuses its complete checkpoint on retry");
    target = fixture.filePath(QStringLiteral("bad-hash.bin"));
    {
        QFile original(target);
        expect(original.open(QIODevice::WriteOnly) && original.write("original") == 8, "create original target for checksum failure");
        original.close();
        ResumableDownload download;
        bool done = false, success = true;
        QObject::connect(&download, &ResumableDownload::finished, &download, [&](bool ok, const QString &) { done = true; success = ok; });
        download.start(url(QStringLiteral("range")), target, QString(40, QLatin1Char('0')), body.size(), true, cache);
        expect(pump([&] { return done; }) && !success && read(target) == QByteArray("original"),
               "whole-file SHA1 mismatch preserves the existing target after parallel assembly");
    }
    return failures ? 1 : 0;
}
