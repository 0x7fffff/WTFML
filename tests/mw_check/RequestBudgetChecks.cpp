#include "download/DownloadManager.hpp"
#include "download/SlowSourcePolicy.hpp"
#include "net/HttpClient.hpp"
#include "net/RequestBudget.hpp"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

int runRequestBudgetChecks() {
    int failures = 0;
    auto expect = [&](bool ok, const char *text) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", text);
        if (!ok) ++failures;
    };
    const int oldLimit = RequestBudget::snapshot().limit;
    RequestBudget::setBulkLimit(2);
    {
        auto first = RequestBudget::tryAcquire(RequestBudget::Lane::Bulk);
        auto second = RequestBudget::tryAcquire(RequestBudget::Lane::Bulk);
        expect(first && second && !RequestBudget::tryAcquire(RequestBudget::Lane::Bulk),
               "process bulk budget refuses the third executor");
        auto control = RequestBudget::tryAcquire(RequestBudget::Lane::Control);
        expect(bool(control), "bulk saturation leaves a reserved control lane");
        first->release(); first->release();
        expect(RequestBudget::snapshot().bulk == 1, "lease release is idempotent before destruction");
        RequestBudget::setBulkLimit(1);
        expect(!RequestBudget::tryAcquire(RequestBudget::Lane::Bulk), "lowered budget drains existing work");
    }
    expect(RequestBudget::snapshot().bulk == 0 && RequestBudget::snapshot().control == 0,
           "all lane leases return on scope exit");
    RequestBudget::setBulkLimit(2);
    QTemporaryDir fixture;
    QTcpServer server;
    if (!fixture.isValid() || !server.listen(QHostAddress::LocalHost, 0)) return 2;
    const QString base = QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort());
    int active = 0, peak = 0, count = 0;
    const QByteArray large(8 * 1024 * 1024, 'z');
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            auto input = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, input] {
                *input += socket->readAll();
                if (!input->contains("\r\n\r\n") || socket->property("handled").toBool()) return;
                socket->setProperty("handled", true);
                ++count;
                const bool slow = input->startsWith("GET /slow ");
                const bool sample = input->toLower().contains("range: bytes=0-65535");
                if (slow) {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 8388608\r\nConnection: close\r\n\r\n");
                    socket->write(large.left(256 * 1024));
                    return;
                }
                ++active; peak = qMax(peak, active);
                QTimer::singleShot(60, socket, [&, socket, sample, input] {
                    const bool full = input->startsWith("GET /fast ");
                    QByteArray body = sample ? large.left(65536) : full ? large : QByteArray("ok");
                    socket->write(sample ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n");
                    socket->write("Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
                    socket->write(body);
                    socket->disconnectFromHost();
                    --active;
                });
            });
        }
    });
    auto wait = [](auto done, int timeout = 5000) {
        QElapsedTimer clock; clock.start();
        while (!done() && clock.elapsed() < timeout)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        return done();
    };
    {
        DownloadManager one, two;
        one.setSegmentedDownloadEnabled(false); two.setSegmentedDownloadEnabled(false);
        one.setMaxConcurrentDownloads(8); two.setMaxConcurrentDownloads(8);
        int finished = 0;
        QObject::connect(&one, &DownloadManager::allDownloadsFinished, &one, [&] { ++finished; });
        QObject::connect(&two, &DownloadManager::allDownloadsFinished, &two, [&] { ++finished; });
        for (int n = 0; n < 6; ++n) {
            one.downloadFile(base + QString::number(n), fixture.filePath(QStringLiteral("one%1").arg(n)));
            two.downloadFile(base + QString::number(n), fixture.filePath(QStringLiteral("two%1").arg(n)));
        }
        expect(wait([&] { return finished == 2; }) && peak <= 2 && count == 12,
               "two real executors finish twelve jobs within one shared two-request limit");
    }
    {
        auto one = RequestBudget::tryAcquire(RequestBudget::Lane::Bulk);
        auto two = RequestBudget::tryAcquire(RequestBudget::Lane::Bulk);
        QNetworkAccessManager manager;
        expect(HttpClient::send(&manager, QUrl(base + "control"), HttpClient::Verb::Get).ok(),
               "real control HTTP completes while bulk slots are occupied");
        QList<RequestBudget::Lease> controls;
        for (int n = 0; n < 4; ++n) controls.append(RequestBudget::tryAcquire(RequestBudget::Lane::Control));
        QElapsedTimer deadline; deadline.start();
        const auto timed = HttpClient::send(&manager, QUrl(base), HttpClient::Verb::Get, {}, {}, {}, 60);
        expect(!timed.networkError.isEmpty() && deadline.elapsed() < 500,
               "control queue wait shares the request deadline");
        controls.clear();
        DownloadManager queued;
        queued.downloadFile(base, fixture.filePath("cancelled"));
        queued.cancelBatch();
        one.reset(); two.reset();
        wait([&] { return RequestBudget::snapshot().bulk == 0; }, 100);
        expect(!QFile::exists(fixture.filePath("cancelled")), "cancelling a budget-waiting job never publishes it");
    }
    expect(!SlowSourcePolicy::shouldProbe(29000, 100000, large.size(), false) &&
           !SlowSourcePolicy::shouldProbe(35000, 100000, large.size(), true) &&
           !SlowSourcePolicy::better(30000, 1000, 1) &&
           !SlowSourcePolicy::better(30000, 65536, 1000) &&
           SlowSourcePolicy::better(30000, 65536, 60),
           "source comparison requires sustained slow work and a sufficiently large faster sample");
    {
        DownloadManager download;
        download.setSegmentedDownloadEnabled(false);
        bool finished = false;
        QObject::connect(&download, &DownloadManager::allDownloadsFinished, &download, [&] { finished = true; });
        const QString path = fixture.filePath("compared.bin");
        download.downloadFile(base + "slow", path,
            QString::fromLatin1(QCryptographicHash::hash(large, QCryptographicHash::Sha1).toHex()),
            large.size(), VerifyMode::SizeAndSha1, base + "fast");
        bool triggered = false;
        QTimer::singleShot(200, &download, [&] {
            for (auto *reply : download.networkAccess()->findChildren<QNetworkReply *>()) {
                if (reply->url().path() != "/slow" || reply->isFinished()) continue;
                // Move the age only; socket bytes, watchdog, comparison, fallback
                // and complete-file verification all remain the real production path.
                reply->setProperty("wtfmlStartMs", -35000);
                triggered = QMetaObject::invokeMethod(&download, "onStallWatchdogTick", Qt::DirectConnection);
            }
        });
        expect(wait([&] { return finished; }) && triggered,
               "real watchdog compares a slow socket and completes via the faster sampled fallback");
        QFile file(path);
        expect(file.open(QIODevice::ReadOnly) && file.readAll() == large,
               "comparison discards probe bytes and verifies a complete replacement file");
    }
    expect(wait([&] { return RequestBudget::snapshot().bulk == 0 && RequestBudget::snapshot().control == 0; }),
           "finished and cancelled replies leave no shared request leases");
    RequestBudget::setBulkLimit(oldLimit);
    return failures ? 1 : 0;
}
