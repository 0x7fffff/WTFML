#include "download/DownloadManager.hpp"
#include "download/SlowSourcePolicy.hpp"
#include "net/HttpClient.hpp"
#include "net/RequestBudget.hpp"
#include "net/ControlGet.hpp"
#include "manage/VersionManager.hpp"
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
#include <cstring>

namespace {

/// 到了那一刻就"内部完成"，但**不发 `finished` 信号**：这正是截止定时器先跑时
/// 看到的那一态 —— 正文已经齐了，派送还排在队列里。真 Qt 里 `isFinished()` 为真
/// 就说明数据可读，只是槽还没轮到；这一份夹具把那个瞬间钉住。
class FinishedAwaitingDispatchReply final : public QNetworkReply {
public:
    FinishedAwaitingDispatchReply(const QNetworkRequest &request, QByteArray body,
                                 int finishAfterMs, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body)) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        QTimer::singleShot(finishAfterMs, this, [this] {
            if (isFinished()) return;
            setFinished(true);
            emit readyRead();
        });
    }
    void abort() override {
        if (isFinished()) return;
        setError(OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }
protected:
    qint64 readData(char *data, qint64 maximum) override {
        const qint64 count = qMin<qint64>(maximum, m_body.size() - m_offset);
        if (count <= 0) return -1;
        std::memcpy(data, m_body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }
private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class FinishedAwaitingDispatchNetwork final : public QNetworkAccessManager {
public:
    FinishedAwaitingDispatchNetwork(int finishAfterMs, QObject *owner)
        : QNetworkAccessManager(owner), m_finishAfterMs(finishAfterMs) {}
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        return new FinishedAwaitingDispatchReply(request, QByteArray("late-ok"),
                                                m_finishAfterMs, this);
    }
private:
    int m_finishAfterMs;
};

}  // namespace

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
                if (input->startsWith("GET /trickle ")) {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 1048576\r\nConnection: close\r\n\r\n");
                    auto *heartbeat=new QTimer(socket);
                    heartbeat->setInterval(20);
                    QObject::connect(heartbeat,&QTimer::timeout,socket,[socket] { socket->write("x"); });
                    heartbeat->start();
                    return;
                }
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
    {
        QNetworkAccessManager first, second;
        QObject owner;
        int completed=0;
        bool valid=true;
        const int before=count;
        peak=0;
        for (int n=0;n<8;++n) RequestBudget::ControlGet::start(&owner,n%2 ? &first : &second,
            QNetworkRequest(QUrl(base+"control"+QString::number(n))),1000,
            [&](QNetworkReply *reply,const QString &error) {
                valid = valid && reply && error.isEmpty() && reply->error()==QNetworkReply::NoError
                    && reply->readAll()=="ok" && RequestBudget::snapshot().control<=3;
                ++completed;
            });
        expect(wait([&] { return completed==8; }) && valid && count-before==8 && peak<=4,
               "independent metadata managers share four control slots and release before completion callbacks");
    }
    {
        QList<RequestBudget::Lease> holds;
        for(int n=0;n<4;++n) holds.append(RequestBudget::tryAcquire(RequestBudget::Lane::Control));
        QNetworkAccessManager manager;
        QObject owner;
        const int before=count;
        bool expired=false, cancelledCalled=false;
        RequestBudget::ControlGet::start(&owner,&manager,QNetworkRequest(QUrl(base+"expired")),60,
            [&](QNetworkReply *reply,const QString &error) { expired=!reply && !error.isEmpty(); });
        auto *cancelled = RequestBudget::ControlGet::start(&owner,&manager,QNetworkRequest(QUrl(base+"cancelled")),1000,
            [&](QNetworkReply *,const QString &) { cancelledCalled=true; });
        delete cancelled;
        auto *departedOwner=new QObject;
        RequestBudget::ControlGet::start(departedOwner,&manager,QNetworkRequest(QUrl(base+"departed")),1000,
            [&](QNetworkReply *,const QString &) { cancelledCalled=true; });
        delete departedOwner;
        expect(wait([&] { return expired; },500) && count==before && !cancelledCalled,
               "metadata queue timeout and cancellation never open a socket while the budget is occupied");
        bool resumed=false;
        RequestBudget::ControlGet::start(&owner,&manager,QNetworkRequest(QUrl(base+"resumed")),1000,
            [&](QNetworkReply *reply,const QString &error) { resumed=reply && error.isEmpty() && reply->readAll()=="ok"; });
        holds.clear();
        expect(wait([&] { return resumed; }) && count==before+1,
               "metadata waiting for a shared slot starts after another owner releases its lease");
    }
    {
        QNetworkAccessManager manager;
        QObject owner;
        bool called=false;
        auto *activeRequest=RequestBudget::ControlGet::start(&owner,&manager,QNetworkRequest(QUrl(base+"slow")),1000,
            [&](QNetworkReply *,const QString &) { called=true; });
        expect(wait([&] { return RequestBudget::snapshot().control==1; }),"metadata request reaches its active state");
        delete activeRequest;
        expect(RequestBudget::snapshot().control==0 && !called,
               "destroying active metadata work closes its reply and returns its lease without a stale callback");
        bool timedOut=false;
        RequestBudget::ControlGet::start(&owner,&manager,QNetworkRequest(QUrl(base+"trickle")),100,
            [&](QNetworkReply *reply,const QString &) { timedOut=reply && reply->error()!=QNetworkReply::NoError
                && reply->property("wtfmlControlDeadlineExpired").toBool(); });
        expect(wait([&] { return timedOut; },1000) && RequestBudget::snapshot().control==0,
               "metadata transfer deadline aborts a stalled response and releases its shared slot");
    }
    {
        // 截止到点时回复已经"内部完成"、只是 `finished` 还没派送：这一发必须按成功交出去。
        // 旧写法在这里走 `else` 分支，拿"超时"把一份已经到手的正文丢掉了。
        QObject owner;
        FinishedAwaitingDispatchNetwork network(40, &owner);
        bool called=false, delivered=false;
        QByteArray body;
        bool hadError=true;
        bool markedExpired=true;
        RequestBudget::ControlGet::start(&owner,&network,QNetworkRequest(QUrl(base+"late")),120,
            [&](QNetworkReply *reply,const QString &error) {
                called=true;
                delivered=reply!=nullptr;
                hadError=!error.isEmpty();
                markedExpired=reply && reply->property("wtfmlControlDeadlineExpired").toBool();
                if (reply) body=reply->readAll();
            });
        expect(wait([&] { return called; },1000) && delivered && !hadError &&
                   !markedExpired && body==QByteArray("late-ok") &&
                   RequestBudget::snapshot().control==0,
               "a response already finished when the deadline fires is delivered, not discarded as a timeout");
    }
    {
        QObject owner;
        auto *manager=new QNetworkAccessManager;
        bool stale=false;
        RequestBudget::ControlGet::start(&owner,manager,QNetworkRequest(QUrl(base+"slow")),1000,
            [&](QNetworkReply *,const QString &) { stale=true; });
        expect(wait([&] { return RequestBudget::snapshot().control==1; }),"metadata manager owns an active request before teardown");
        delete manager;
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        expect(RequestBudget::snapshot().control==0 && !stale,
               "network manager teardown releases the control slot without calling a destructing owner");
    }
    {
        QList<RequestBudget::Lease> holds;
        for(int n=0;n<4;++n) holds.append(RequestBudget::tryAcquire(RequestBudget::Lane::Control));
        VersionManager versions;
        int replies=0;
        QObject::connect(&versions,&VersionManager::loaderVersionsFetched,&versions,
            [&](const QString &,VersionManager::LoaderKind,bool,const LoaderMeta::LoaderVersionList &,const QString &) { ++replies; });
        versions.requestLoaderVersions("1.20.1",VersionManager::LoaderKind::Fabric);
        versions.requestLoaderVersions("1.21.1",VersionManager::LoaderKind::Fabric);
        int queued=0;
        for(auto *child:versions.children()) if(dynamic_cast<RequestBudget::ControlGet *>(child)) ++queued;
        expect(queued==1 && replies==0,
               "replacing a budget-waiting loader query leaves exactly one job and no obsolete failure");
        versions.requestLoaderVersions({},VersionManager::LoaderKind::Fabric);
        queued=0;
        for(auto *child:versions.children()) if(dynamic_cast<RequestBudget::ControlGet *>(child)) ++queued;
        expect(queued==0 && replies==1,"clearing a loader selection cancels its queued request instead of dispatching it later");
    }
    {
        QNetworkAccessManager manager;
        QObject owner;
        // `normal` 必须从 false 起：它表示"第二发真的回来了并且正文对"。从 true 起的话，
        // 两发谁先回来都会让 `wait()` 提前满足条件，第二发还在飞时就去读
        // `snapshot().control`，会偶发读成 1（2026-11-01 逮到的那条约 1/6 偶发红就是这么来的）。
        bool oversized=false, normal=false, controlDone=false;
        RequestBudget::ControlGet::start(&owner,&manager,QNetworkRequest(QUrl(base+"fast")),2000,
            [&](QNetworkReply *reply,const QString &) {
                oversized = reply && reply->error()!=QNetworkReply::NoError;
            }, 1024);
        RequestBudget::ControlGet::start(&owner,&manager,QNetworkRequest(QUrl(base+"control")),2000,
            [&](QNetworkReply *reply,const QString &error) {
                normal = reply && error.isEmpty() && reply->readAll()=="ok";
                controlDone = true;
            });
        expect(wait([&] { return oversized && controlDone; }) && normal
                   && RequestBudget::snapshot().control==0,
               "a per-job body limit cuts an oversized metadata answer without affecting the default limit");
    }
    expect(!SlowSourcePolicy::shouldProbe(29000, 100000, large.size(), false) &&
           !SlowSourcePolicy::shouldProbe(35000, 100000, large.size(), true) &&
           !SlowSourcePolicy::better(30000, 1000, 1) &&
           !SlowSourcePolicy::better(30000, 65536, 1000) &&
           SlowSourcePolicy::better(30000, 65536, 60),
           "source comparison requires sustained slow work and a sufficiently large faster sample");
    // 分段协调器那一档看的是**窗口内合计**：还剩多少字节要单独判，不能拿窗口里的字节数
    // 当成整份进度。
    expect(SlowSourcePolicy::shouldCompare(35000, 100000, 4 * 1024 * 1024, false) &&
           !SlowSourcePolicy::shouldCompare(35000, 100000, 4 * 1024 * 1024 - 1, false) &&
           !SlowSourcePolicy::shouldCompare(35000, 100000, 8 * 1024 * 1024, true) &&
           !SlowSourcePolicy::shouldCompare(1000, 100000, 8 * 1024 * 1024, false),
           "the segmented comparison needs a full window, a real remainder and no local limit");
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
