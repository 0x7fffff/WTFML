#include "SegmentInstallChecks.hpp"
#include <functional>
#include "download/DownloadManager.hpp"
#include "download/ResumableDownload.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QSet>
#include <cstdio>

// 安装链路（DownloadManager）自动升级成分段下载的离线检查。
//
// 服务器是一个最小的 HTTP/1.1 夹具，故意每个响应都 `Connection: close`，这样四条
// Range 请求就是四条独立连接 —— 和真实 CDN 上的行为一致，也不依赖 Qt 的连接复用。
int runSegmentInstallChecks() {
    QTemporaryDir fixture(QDir::current().filePath(QStringLiteral(".segment-install-check-XXXXXX")));
    if (!fixture.isValid()) return 2;
    int failures = 0;
    auto expect = [&](bool ok, const char *text) {
        printf("%s %s\n", ok ? "PASS" : "FAIL", text);
        fflush(stdout);
        if (!ok) ++failures;
    };

    QByteArray body(8 * 1024 * 1024, 'x');
    for (qsizetype i = 0; i < body.size(); ++i) body[i] = char((i * 7 + 3) % 251);
    QByteArray small(2 * 1024 * 1024, 's');
    for (qsizetype i = 0; i < small.size(); ++i) small[i] = char((i * 13 + 5) % 241);
    const auto digest = [](const QByteArray &data) {
        return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
    };
    const QString bodySha1 = digest(body), smallSha1 = digest(small);

    // 每个场景开始前清零；请求计数按"实际到达数据端点的请求"统计：
    // probes = Range: bytes=0-0；parts = 其它带 Range 的请求；full = 完全不带 Range 的请求。
    // 302 的入口请求不计（一次分段会打入口 + 落点两条），这样计数只反映数据请求本身。
    int probes = 0, parts = 0, full = 0, redirects = 0, signedHits = 0, badSigned = 0;
    // 「持续低速换源」那一节要分清两条腿：主地址上有几条分段、备用地址上取了几次样本。
    int slowProbes = 0, slowParts = 0, officialProbes = 0, officialParts = 0, officialSamples = 0;
    int partDelayMs = 0;
    // 有个别场景要连着看门狗一起等（失速那两跳各 5 秒），截止线单独放宽。
    int deadlineMs = 30000;
    // /flaky：第一条**分段**请求回 404（延迟 300 ms 才回，让同批其它分段先落地），
    // 之后正常。用来钉住"重新探测、只补缺的那一条、不整文件重下"。
    bool flakyFailed = false;
    // /stall：第一条**分段**请求回完头和半段 body 之后**不关连接、不再给字节** ——
    // 用户日志里那条分段停在 3.7/4.2 MiB 的翻版。之后正常。
    bool stallTriggered = false;
    // /silent：第一条**分段**请求只回响应头（Content-Length 照写整段的长度），正文一个字
    // 都不给、连接也不关 —— BMCLAPI 302 到死落点那种形状。看门狗那条"一个字节都没收到"
    // 的 12 秒判据专门抓它。
    bool silentTriggered = false;
    QSet<int> issuedTokens;
    int nextToken = 0;
    const auto resetCounters = [&] {
        probes = parts = full = redirects = signedHits = badSigned = 0;
        slowProbes = slowParts = officialProbes = officialParts = officialSamples = 0;
        partDelayMs = 0;
        flakyFailed = false;
        stallTriggered = false;
        silentTriggered = false;
    };

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

                const QByteArray target = input.split('\n').value(0).trimmed().split(' ').value(1);
                const QByteArray path = target.split('?').value(0);
                const QByteArray query = target.contains('?') ? target.mid(target.indexOf('?') + 1)
                                                              : QByteArray();
                qint64 begin = -1, end = -1;
                for (auto line : input.split('\n')) {
                    line = line.trimmed();
                    if (line.toLower().startsWith("range: bytes=")) {
                        const auto span = line.mid(13).split('-');
                        if (span.size() == 2) {
                            begin = span[0].toLongLong();
                            end = span[1].toLongLong();
                        }
                    }
                }
                const bool probing = begin == 0 && end == 0;
                const bool ranged = begin >= 0 && !probing;
                if (path != "/redirect") {
                    if (begin < 0) ++full;
                    else if (probing) ++probes;
                    else ++parts;
                    if (path == "/slow") {
                        if (probing) ++slowProbes;
                        else if (begin >= 0) ++slowParts;
                    }
                    if (path == "/official") {
                        if (probing) ++officialProbes;
                        else if (begin >= 0) {
                            // 64 KiB 那一发是低速比较的样本，不算分段正文。
                            if (begin == 0 && end == 65535) ++officialSamples;
                            else ++officialParts;
                        }
                    }
                }

                QByteArray response;
                int delay = 0;
                bool trickleOpen = false;
                bool holdOpen = false;
                if (path == "/redirect") {
                    // 模拟 BMCLAPI：入口 302 到一条带令牌的落点，令牌只对本次跳转有效。
                    ++redirects;
                    const int token = ++nextToken;
                    issuedTokens.insert(token);
                    response = "HTTP/1.1 302 Found\r\nLocation: /signed?token="
                               + QByteArray::number(token)
                               + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                } else if (path == "/signed") {
                    const int token = query.mid(QByteArray("token=").size()).toInt();
                    if (!issuedTokens.contains(token)) {
                        ++badSigned;
                        response = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n"
                                   "Connection: close\r\n\r\n";
                    } else {
                        ++signedHits;
                    }
                } else if (path == "/missing" || (path == "/probe404" && begin == 0 && end == 0)) {
                    response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                               "Connection: close\r\n\r\n";
                } else if (path == "/flaky" && ranged && !flakyFailed) {
                    // 延迟到同批其它分段都写完之后再回 404，保证失败发生时 m_complete
                    // 里已经有别的分段（否则协调器会按"一条都没成"退回整文件）。
                    flakyFailed = true;
                    delay = 300;
                    response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                               "Connection: close\r\n\r\n";
                }
                if (response.isEmpty()) {
                    const QByteArray data = path == "/small" ? small : body;
                    const QByteArray etag = path == "/small" ? QByteArray("\"small\"")
                                                             : QByteArray("\"v1\"");
                    if (begin < 0 || path == "/ignore") {
                        // /ignore 故意忽略 Range：探测会看到 200 并退回普通整文件下载。
                        response = "HTTP/1.1 200 OK\r\nContent-Length: "
                                   + QByteArray::number(data.size()) + "\r\nETag: " + etag
                                   + "\r\nConnection: close\r\n\r\n" + data;
                    } else {
                        QByteArray payload = data.mid(begin, end - begin + 1);
                        const QByteArray declared = QByteArray::number(payload.size());
                        if (ranged && path == "/stall" && !stallTriggered) {
                            // 头 + 一小段 body，之后每 2 秒补 512 B（256 B/s）。
                            // 必须低于新分段阈值 4 KiB / 5 s；旧夹具的 2 KiB/s
                            // 已属于本轮刻意保留的慢速可用链路，不能再期待它被掐掉。这是那条分段停在
                            // 3.7/4.2 MiB、以几百字节/秒往终点爬的形状：Qt 的 30 秒传输超时
                            // 因为一直有活动而不响，90 秒"一个字节都没有"的兜底也不响，
                            // 只有"有退路 + 一直太慢"那条判据该动手。
                            stallTriggered = true;
                            trickleOpen = true;
                            payload = payload.left(4096);
                        }
                        if (ranged && path == "/slow") {
                            // 主地址上**每一条**分段都只有几百字节/秒：合计速率也远低于
                            // 持续低速那条线，而且永远下不完。协调器要在看门狗之前拿
                            // 备用地址的样本比一比，然后整条换过去。
                            trickleOpen = true;
                            payload = payload.left(4096);
                        }
                        if (ranged && path == "/silent" && !silentTriggered) {
                            // 头照写（Content-Length = 整段长度），正文一个字节都不给，
                            // 连接也不关：TLS 接得上、数据不来。Qt 的不活动超时要 30 秒
                            // 才响，那条 12 秒判据要在它之前动手。
                            silentTriggered = true;
                            holdOpen = true;
                            payload.clear();
                        }
                        response = "HTTP/1.1 206 Partial Content\r\nContent-Length: "
                                   + declared + "\r\nContent-Range: bytes "
                                   + QByteArray::number(begin) + "-" + QByteArray::number(end) + "/"
                                   + QByteArray::number(data.size()) + "\r\nETag: " + etag
                                   + "\r\nConnection: close\r\n\r\n"
                                   + payload;
                        if (ranged && partDelayMs > 0) delay = partDelayMs;
                    }
                }
                QTimer::singleShot(delay, socket, [socket, response, trickleOpen, holdOpen] {
                    socket->write(response);
                    if (holdOpen) return;  // 只给了头：正文不来、连接也不关
                    if (!trickleOpen) {
                        socket->disconnectFromHost();
                        return;
                    }
                    // 细水长流：每 2 秒 512 B，永远够不到完成。定时器挂在 socket 上，
                    // 客户端 abort、socket 释放时它跟着一起没了。
                    auto *trickle = new QTimer(socket);
                    trickle->setInterval(2000);
                    QObject::connect(trickle, &QTimer::timeout, socket,
                                     [socket] { socket->write(QByteArray(512, 'x')); });
                    trickle->start();
                });
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    const auto url = [&](const QString &path) {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path));
    };
    const auto read = [](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return file.readAll();
    };

    struct Outcome {
        bool deadlineHeld = false;
        bool batchSuccess = false;
        bool exists = false;
        bool busyAfter = false;
        int results = 0;
        int batchResults = 0;
        qint64 wireBytes = 0;
        DownloadStats stats;
        QByteArray bytes;
    };

    // 一次批量下载（单作业），返回所有需要断言的现场。
    const auto runOne = [&](const QString &name, const QString &path, const QString &officialPath,
                            const QString &cacheRoot, bool cancelEarly) {
        Outcome out;
        DownloadManager downloads;
        downloads.setUseMirror(false);
        downloads.setRangeCacheRoot(cacheRoot);
        QObject::connect(&downloads, &DownloadManager::transferBytes, &downloads,
                         [&out](qint64 bytes) { out.wireBytes += bytes; });
        const QString destination = fixture.filePath(name);
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&downloads, &DownloadManager::downloadFinished, &downloads,
                         [&out](const QString &, bool ok, const QString &) {
                             ++out.results;
                             out.batchSuccess = ok;  // 单作业时它就是这一批的结果
                         });
        QObject::connect(&downloads, &DownloadManager::allDownloadsFinished, &loop,
                         [&out, &loop](bool ok) {
                             ++out.batchResults;
                             out.batchSuccess = ok;
                             loop.quit();
                         });
        deadline.start(deadlineMs);
        downloads.beginBatch();
        downloads.downloadFile(url(path).toString(), destination,
                               path == "/small" ? smallSha1 : bodySha1,
                               path == "/small" ? small.size() : body.size(),
                               VerifyMode::SizeAndSha1,
                               officialPath.isEmpty() ? QString() : url(officialPath).toString());
        downloads.endBatch();
        if (cancelEarly) {
            QTimer::singleShot(120, &downloads, [&downloads, &loop] {
                downloads.cancelBatch();
                QTimer::singleShot(500, &loop, &QEventLoop::quit);
            });
        }
        loop.exec();
        out.deadlineHeld = deadline.isActive();
        out.busyAfter = downloads.isBusy();
        out.stats = downloads.stats();
        out.exists = QFileInfo::exists(destination);
        out.bytes = read(destination);
        return out;
    };

    const QString cache = fixture.filePath(QStringLiteral("cache"));

    // 1. 8 MiB + SHA-1 经 DownloadManager：自动探测并四路分段，记账与普通下载一致。
    {
        resetCounters();
        const auto out = runOne(QStringLiteral("large.bin"), QStringLiteral("/file"), {}, cache, false);
        expect(out.deadlineHeld, "large install download terminates within deadline");
        expect(out.bytes == body, "automatically segmented install download produces complete bytes");
        expect(out.wireBytes == body.size(), "segmented telemetry counts each received byte exactly once");
        expect(probes == 1 && parts == 4 && full == 0,
               "eligible install file uses one probe and four bounded range requests");
        expect(out.results == 1 && out.batchResults == 1 && out.batchSuccess,
               "segmented install download emits exactly one file result and one batch result");
        expect(out.stats.total == 1 && out.stats.done == 1 && out.stats.failed == 0,
               "segmented install download keeps batch accounting identical to a plain download");
    }

    // 2. 2 MiB 小文件：低于分段下限，仍是一条普通 GET（小文件正是首字节延迟的大头）。
    {
        resetCounters();
        const auto out = runOne(QStringLiteral("small.bin"), QStringLiteral("/small"), {}, cache, false);
        expect(out.deadlineHeld && out.bytes == small, "small install file still downloads correctly");
        expect(out.wireBytes == small.size(), "plain file telemetry counts actual received bytes");
        expect(probes == 0 && parts == 0 && full == 1,
               "below-threshold install file never probes and sends exactly one ordinary GET");
        expect(out.stats.total == 1 && out.stats.done == 1 && out.stats.failed == 0,
               "small install file does not disturb batch accounting");
    }

    // 3. 服务器忽略 Range：探测后恰好退回一条整文件 GET，不会反复重试。
    {
        resetCounters();
        const auto out = runOne(QStringLiteral("ignore.bin"), QStringLiteral("/ignore"), {}, cache, false);
        expect(out.deadlineHeld && out.bytes == body, "range-ignoring server falls back to a correct full file");
        expect(probes == 1 && parts == 0 && full == 1,
               "range-unsupported fallback sends no additional probes or range requests");
        expect(out.stats.total == 1 && out.stats.done == 1 && out.stats.failed == 0,
               "range-unsupported fallback is billed as one ordinary download");
    }

    {
        resetCounters();
        const auto out = runOne(QStringLiteral("probe404.bin"), QStringLiteral("/probe404"), {}, cache, false);
        expect(out.deadlineHeld && out.bytes == body,
               "404 only on Range probe falls back to verified full GET");
        expect(probes == 1 && parts == 0 && full == 1,
               "404 probe fallback does not loop or retry range requests");
        expect(out.batchSuccess && out.stats.failed == 0,
               "404 probe does not incorrectly fail a downloadable file");
    }

    // 4. 入口 302 到带令牌落点（BMCLAPI 形态）：**探测一次拿到落点，四条分段复用这一条**
    // （docs/features/HIGH_SPEED_DOWNLOAD.md「 现场日志修正」：避免每段重复入口、
    // 反复调度和 429）。令牌真过期的那一批会在补投前重新探测刷新。
    {
        resetCounters();
        const auto out = runOne(QStringLiteral("redirect.bin"), QStringLiteral("/redirect"), {}, cache, false);
        expect(out.deadlineHeld && out.bytes == body, "redirected segmented download assembles correct bytes");
        expect(probes == 1 && parts == 4 && full == 0,
               "redirected segmented download still sends one probe and four range requests");
        // redirects 可能比 1 多：夹具在 302 响应写完就关连接，Qt 偶尔会把一条还没拿到
        // 响应的入口请求再发一次。真正要钉住的是**五个数据请求**（1 次探测 + 4 条分段）
        // 都落在了探测拿到的令牌上，没有一个拿着旧令牌去撞 403。
        expect(redirects >= 1 && signedHits == 5 && badSigned == 0,
               "the probe re-enters the redirect once and all four parts reuse that fresh token");
        expect(out.stats.failed == 0, "redirected segmented download reports no failure");
    }

    // 5. 本地续传目录不可用（父级是普通文件）：这是本地准备失败，不是源站失败。
    // 必须一次性降级成普通下载并成功，不能把文件判死，也不能无限重试。
    {
        resetCounters();
        const QString blocker = fixture.filePath(QStringLiteral("blocker"));
        {
            QFile file(blocker);
            expect(file.open(QIODevice::WriteOnly) && file.write("x") == 1,
                   "create a regular file to make the cache root uncreatable");
        }
        const QString badRoot = QDir(blocker).filePath(QStringLiteral("child"));
        const auto out = runOne(QStringLiteral("bad-cache.bin"), QStringLiteral("/file"), {}, badRoot, false);
        expect(out.deadlineHeld && out.bytes == body,
               "unusable range cache degrades to a successful ordinary download");
        expect(probes == 0 && parts == 0 && full == 1,
               "local cache failure sends no probe: only the plain fallback GET reaches the server");
        expect(out.stats.total == 1 && out.stats.done == 1 && out.stats.failed == 0,
               "local cache failure is not billed as a source failure");
    }

    // 6. 主地址 404、调用方给了官方地址：自动改走官方，失败计数保持 0。
    {
        resetCounters();
        const auto out = runOne(QStringLiteral("official.bin"), QStringLiteral("/missing"),
                                QStringLiteral("/official"), cache, false);
        expect(out.deadlineHeld && out.bytes == body, "official fallback downloads the complete file");
        expect(probes == 2 && parts == 4 && full == 1,
               "real 404 gets one full GET check before probing and segmenting official fallback");
        expect(out.stats.total == 1 && out.stats.done == 1 && out.stats.failed == 0,
               "mirror-to-official retry is not counted as a failed job");
        expect(out.results == 1 && out.batchResults == 1 && out.batchSuccess,
               "official fallback reports one successful file and one batch result");
    }

    {
        resetCounters();
        const auto out = runOne(QStringLiteral("really-missing.bin"), QStringLiteral("/missing"), {}, cache, false);
        expect(out.deadlineHeld && !out.exists && !out.batchSuccess && out.stats.failed == 1,
               "real 404 without fallback remains a failure and publishes no file");
        expect(probes == 1 && parts == 0 && full == 1,
               "real 404 fallback is bounded to one ordinary GET");
    }

    // 7. 分段途中取消：不发布半成品，不误发完成信号，isBusy() 立刻归位。
    {
        resetCounters();
        partDelayMs = 300;
        const auto out = runOne(QStringLiteral("cancelled.bin"), QStringLiteral("/file"), {}, cache, true);
        expect(out.deadlineHeld, "cancelled segmented download leaves the event loop without hanging");
        expect(!out.exists, "cancelling a segmented install download never publishes a partial target");
        expect(out.results == 0 && out.batchResults == 0,
               "cancelling a segmented install download emits no completion signals");
        expect(!out.busyAfter, "cancelled segmented download settles isBusy() back to false");
        partDelayMs = 0;
    }

    // 8. 同一批两个大文件：两个分段作业都能拿到名额，整批只报一次完成。
    {
        resetCounters();
        DownloadManager downloads;
        downloads.setUseMirror(false);
        downloads.setRangeCacheRoot(cache);
        const QString first = fixture.filePath(QStringLiteral("parallel-a.bin"));
        const QString second = fixture.filePath(QStringLiteral("parallel-b.bin"));
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        int results = 0, batchResults = 0;
        bool batchSuccess = false;
        QObject::connect(&downloads, &DownloadManager::downloadFinished, &downloads,
                         [&results](const QString &, bool, const QString &) { ++results; });
        QObject::connect(&downloads, &DownloadManager::allDownloadsFinished, &loop,
                         [&batchResults, &batchSuccess, &loop](bool ok) {
                             ++batchResults;
                             batchSuccess = ok;
                             loop.quit();
                         });
        deadline.start(40000);
        downloads.beginBatch();
        downloads.downloadFile(url(QStringLiteral("/file")).toString(), first, bodySha1, body.size(),
                               VerifyMode::SizeAndSha1);
        downloads.downloadFile(url(QStringLiteral("/file")).toString(), second, bodySha1, body.size(),
                               VerifyMode::SizeAndSha1);
        downloads.endBatch();
        loop.exec();
        const auto stats = downloads.stats();
        expect(deadline.isActive() && read(first) == body && read(second) == body,
               "two concurrent segmented install downloads both complete");
        expect(probes == 2 && parts == 8 && full == 0,
               "two segmented jobs each get their own probe and four ranges");
        expect(results == 2 && batchResults == 1 && batchSuccess,
               "a batch with two segmented jobs emits two file results and one batch result");
        expect(stats.total == 2 && stats.done == 2 && stats.failed == 0,
               "two segmented jobs keep total/done/failed consistent");
    }

    // 9. 某一条分段 404（永久失败、不走瞬态重试）：**先只补缺的那一条**，不整文件重下。
    // 失败那一发延迟 300 ms 才回，保证它到达时另外三条已经完整落盘。
    {
        resetCounters();
        const auto out = runOne(QStringLiteral("flaky.bin"), QStringLiteral("/flaky"), {}, cache, false);
        expect(out.deadlineHeld && out.bytes == body,
               "failed part is retried and the file still assembles");
        // 首轮探测 1 次 + 补投前重新探测刷新签名 1 次 + 第一轮 4 条 + 补发 1 条；
        // 整文件重下会是 full=1。
        expect(probes == 2 && parts == 5 && full == 0,
               "one failed part costs one reprobe and one extra range request, never a whole-file re-download");
        expect(out.stats.total == 1 && out.stats.done == 1 && out.stats.failed == 0,
               "part-level retry keeps the job billed as one success");
    }

    // 10. 某一条分段"头 + 半段 body 之后链子停住、连接还活着"（用户日志的形状）：
    // bytesAvailable 涨过一次 ⇒ 90 秒卡死兜底不响；它身上又没有官方备用地址 ⇒ 旧口径
    // 下"太慢"那条也不管它。结果就是那条分段 664 秒里一直攥着连接，协调器要等整批
    // 收尾才补段 ⇒ 整条作业永远不会结束。
    // Range 作业也算"有退路"（协调器只补缺段）；新口径持续六跳低于阈值才 abort。
    // 断言：看门狗把那条段掐掉 → 协调器补一条 → 整条作业成功；**只多一条 Range**。
    {
        resetCounters();
        deadlineMs = 45000;  // 六跳（约 30–35 s）+ 补段，留出调度余量
        const auto out = runOne(QStringLiteral("stall.bin"), QStringLiteral("/stall"), {}, cache, false);
        deadlineMs = 30000;
        expect(out.deadlineHeld && out.bytes == body,
               "a stalled segmented part is aborted and the file still assembles");
        // 首轮探测 1 次 + 补投前重新探测刷新签名 1 次 + 第一轮 4 条 + 补发卡住那条 1 条；
        // 整文件重下会是 full=1。
        expect(probes == 2 && parts == 5 && full == 0,
               "a stalled part costs one reprobe and one extra range request, never a whole-file re-download");
        expect(out.stats.total == 1 && out.stats.done == 1 && out.stats.failed == 0,
               "aborting a slow part keeps the job billed as one success");
        expect(out.results == 1 && out.batchResults == 1 && out.batchSuccess,
               "a stalled part still produces exactly one file result and one batch result");
    }

    // 11. 某一条分段**只回响应头、正文一个字节都不给**（BMCLAPI 302 到死落点的形状）。
    // 它身上没有官方备用地址，而"12 秒没首字节就换路"那条只管镜像作业 ⇒ 只能等
    // 90 秒兜底（或者 Qt 那条 30 秒不活动超时）。
    // 截止线必须压在 Qt 那条 30 秒**之前**（25 秒）：否则就算被测代码里没有这条判据，
    // 30 秒一到 Qt 自己会把回复掐掉，用例照样绿 —— 那正是这条判据要防的假绿。
    {
        resetCounters();
        deadlineMs = 25000;  // 12 秒判据 + 下一跳（≤5 s）+ 补段；必须 < Qt 的 30 秒
        const auto out = runOne(QStringLiteral("silent.bin"), QStringLiteral("/silent"), {}, cache, false);
        deadlineMs = 30000;
        expect(out.deadlineHeld && out.bytes == body,
               "a part that never sends a body is aborted and the file still assembles");
        // 首轮探测 1 次 + 补投前重新探测刷新签名 1 次 + 第一轮 4 条 + 补发没给正文那条 1 条；
        // 整文件重下会是 full=1。
        expect(probes == 2 && parts == 5 && full == 0,
               "a no-first-byte part costs one reprobe and one extra range request, never a whole-file re-download");
        expect(out.stats.total == 1 && out.stats.done == 1 && out.stats.failed == 0,
               "aborting a silent part keeps the job billed as one success");
        expect(out.results == 1 && out.batchResults == 1 && out.batchSuccess,
               "a silent part still produces exactly one file result and one batch result");
    }

    // 12. 主地址每条分段都只有几百字节/秒、调用方给了官方地址：分段阶段的**合计**速率
    // 持续低于阈值时，协调器从官方取 64 KiB 样本；样本明显更快 ⇒ 整条换过去
    // （重新探测入口与 ETag，旧分段留不留由 prepareCache 决定），不整文件重下。
    // 观察窗口在这里缩到 3 秒（默认 30 秒），阈值本身没有第二处。
    {
        resetCounters();
        ResumableDownload::setLowSpeedWindowForTest(3000);
        const auto out = runOne(QStringLiteral("slow.bin"), QStringLiteral("/slow"),
                                QStringLiteral("/official"), cache, false);
        ResumableDownload::setLowSpeedWindowForTest(0);
        expect(out.deadlineHeld && out.bytes == body,
               "a sustained slow source is compared and the file still completes");
        expect(slowProbes == 1 && slowParts == 4,
               "the slow source only ever gets the first probe and its four ranges");
        expect(officialSamples == 1, "the comparison takes exactly one 64 KiB sample");
        expect(officialProbes == 1 && officialParts == 4,
               "switching re-probes the faster source and runs the missing parts there");
        expect(full == 0 && out.stats.total == 1 && out.stats.done == 1 && out.stats.failed == 0,
               "a source switch is neither a whole-file re-download nor a counted failure");
    }

    return failures ? 1 : 0;
}
