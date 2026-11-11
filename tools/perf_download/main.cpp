/**
 * @file main.cpp
 * @brief 并发下载基准：在本机起一个 HTTP 服务器，用**真的** `DownloadManager` 下一批文件，
 *        量完成时间、吞吐、失败数与主线程最长空档。
 *
 * 【为什么要有这个工具】
 *
 * 产品 exe 里没有假网络注入点，验证工程又是按设计 `-O0` 编的 —— 拿它们报"并发下载变快了"
 * 都不算证据（见 docs/dev/TESTING.md 的性能规则）。这个工具只做一件事：在 Release 下把
 * `DownloadManager` 摆到一条已知的、本机的链路上，把同一批作业跑 N 遍，输出可比的数字。
 *
 * 【它能说明什么、不能说明什么】
 *
 * 能说明：调度（并发档位、分段升级、限速令牌桶）、每文件写盘、信号量对主线程的影响。
 * 不能说明：广域网时延/丢包/镜像质量。loopback 没有 RTT，这里的"吞吐"上限是内存带宽
 * 与磁盘写速，不是用户网线。
 */

#include "HttpFileServer.hpp"

#include "download/DownloadManager.hpp"
#include "util/AppPaths.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <limits>

namespace {

struct Config {
    int files = 200;
    qint64 fileSize = 256 * 1024;
    int concurrency = DownloadConcurrency::kDefault;
    bool segmented = false;
    qint64 speedLimit = 0;  // 字节/秒；0 = 不限
    int rounds = 3;
    int warmup = 1;
    int timeoutMs = 180000;
    QString label;
    QString jsonPath;
    QString workDir;
    bool keepWorkDir = false;
};

struct RoundStats {
    double wallMs = 0;
    qint64 bytes = 0;         // 期望的总字节数
    qint64 transferred = 0;   // transferBytes 实际报上来的合计
    int failed = 0;
    int finished = 0;
    bool timedOut = false;
    bool verified = false;
    qint64 maxGapMs = 0;
    qint64 maxGapAtMs = 0;
    int over50ms = 0;
    int ticks = 0;
    int progressSignals = 0;
    int transferSignals = 0;
    QStringList errors;
};

struct Watchdog {
    QElapsedTimer clock;
    QTimer timer;
    qint64 last = 0;
    qint64 maxGap = 0;
    qint64 maxGapAt = 0;
    int over50 = 0;
    int ticks = 0;

    void start() {
        clock.start();
        last = 0;
        timer.setInterval(20);
        timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer, &QTimer::timeout, [this] {
            const qint64 now = clock.elapsed();
            const qint64 gap = now - last;
            last = now;
            if (gap > maxGap) {
                maxGap = gap;
                maxGapAt = now;
            }
            if (gap - timer.interval() > 50) {
                ++over50;
            }
            ++ticks;
        });
        timer.start();
    }
    void stop() { timer.stop(); }
};

qint64 parseBytes(const QString &text, bool *ok) {
    QString value = text.trimmed();
    qint64 multiplier = 1;
    const QString upper = value.toUpper();
    for (const auto &suffix : {QStringLiteral("KIB"), QStringLiteral("MIB"), QStringLiteral("GIB"),
                               QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB"),
                               QStringLiteral("K"), QStringLiteral("M"), QStringLiteral("G")}) {
        if (upper.endsWith(suffix)) {
            const qint64 base = suffix.startsWith(QLatin1Char('K')) ? 1024
                : suffix.startsWith(QLatin1Char('M'))            ? 1024 * 1024
                                                                 : 1024LL * 1024 * 1024;
            multiplier = base;
            value.chop(suffix.size());
            break;
        }
    }
    bool parsed = false;
    const qint64 number = value.trimmed().toLongLong(&parsed);
    *ok = parsed && number > 0 && number <= std::numeric_limits<qint64>::max() / multiplier;
    return *ok ? number * multiplier : 0;
}

QByteArray makePayload(qint64 size) {
    QByteArray payload;
    payload.resize(int(size));
    quint32 state = 0x9e3779b9u;
    for (int i = 0; i < payload.size(); ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        payload[i] = static_cast<char>(state & 0xff);
    }
    return payload;
}

QString sha1Of(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}

bool verifyFile(const QString &path, qint64 expectedSize, const QString &expectedSha1) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() != expectedSize) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file)) {
        return false;
    }
    return QString::fromLatin1(hash.result().toHex()) == expectedSha1;
}

qint64 effectiveBytes(const RoundStats &stats) {
    return stats.transferred > 0 ? stats.transferred : stats.bytes;
}

double megabytesPerSecond(qint64 bytes, double wallMs) {
    if (wallMs <= 0) {
        return 0;
    }
    return double(bytes) / (1024.0 * 1024.0) / (wallMs / 1000.0);
}

RoundStats runRound(const Config &config, quint16 port, const QString &roundDir,
                    const QByteArray &payload, const QString &sha1) {
    RoundStats stats;
    QDir().mkpath(roundDir);
    AppPaths::setMinecraftDir(roundDir);

    DownloadManager manager;
    manager.setMaxConcurrentDownloads(config.concurrency);
    manager.setSegmentedDownloadEnabled(config.segmented);
    manager.setRangeCacheRoot(roundDir + QStringLiteral("/range-cache"));
    manager.setSpeedLimitBytesPerSecond(config.speedLimit);
    manager.setAllowOverwrite(true);

    Watchdog watchdog;
    QEventLoop loop;
    bool batchFinished = false;

    QObject::connect(&manager, &DownloadManager::downloadFinished, &loop,
                     [&stats](const QString &fileName, bool success, const QString &error) {
                         ++stats.finished;
                         if (!success) {
                             ++stats.failed;
                             if (stats.errors.size() < 5) {
                                 stats.errors << QStringLiteral("%1: %2").arg(fileName, error);
                             }
                         }
                     });
    QObject::connect(&manager, &DownloadManager::transferBytes, &loop,
                     [&stats](qint64 bytes) {
                         stats.transferred += bytes;
                         ++stats.transferSignals;
                     });
    QObject::connect(&manager, &DownloadManager::downloadProgress, &loop,
                     [&stats](const QString &, qint64, qint64) { ++stats.progressSignals; });
    QObject::connect(&manager, &DownloadManager::allDownloadsFinished, &loop,
                     [&loop, &batchFinished](bool) {
                         batchFinished = true;
                         loop.quit();
                     });

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&loop] { loop.quit(); });
    timeout.start(config.timeoutMs);

    QElapsedTimer wall;
    watchdog.start();
    wall.start();
    manager.beginBatch();
    for (int i = 0; i < config.files; ++i) {
        const QString destination =
            roundDir + QStringLiteral("/files/file-%1.bin").arg(i, 4, 10, QLatin1Char('0'));
        const QString url = QStringLiteral("http://127.0.0.1:%1/file/%2.bin").arg(port).arg(i);
        manager.downloadFile(url, destination, sha1, payload.size());
    }
    manager.endBatch();
    if (!batchFinished) {
        loop.exec();
    }
    stats.wallMs = double(wall.elapsed());
    stats.timedOut = !batchFinished;
    if (stats.timedOut) {
        manager.cancelBatch();
    }
    timeout.stop();
    watchdog.stop();
    stats.maxGapMs = watchdog.maxGap;
    stats.maxGapAtMs = watchdog.maxGapAt;
    stats.over50ms = watchdog.over50;
    stats.ticks = watchdog.ticks;

    stats.bytes = qint64(config.files) * payload.size();
    stats.verified = true;
    for (int i = 0; i < config.files; ++i) {
        const QString destination =
            roundDir + QStringLiteral("/files/file-%1.bin").arg(i, 4, 10, QLatin1Char('0'));
        if (!verifyFile(destination, payload.size(), sha1)) {
            stats.verified = false;
            break;
        }
    }
    return stats;
}

QJsonObject roundJson(const RoundStats &stats) {
    QJsonObject object;
    object.insert("wall_ms", stats.wallMs);
    object.insert("megabytes_per_second", megabytesPerSecond(effectiveBytes(stats), stats.wallMs));
    object.insert("bytes", double(stats.bytes));
    object.insert("transferred_bytes", double(stats.transferred));
    object.insert("finished", stats.finished);
    object.insert("failed", stats.failed);
    object.insert("timed_out", stats.timedOut);
    object.insert("verified", stats.verified);
    object.insert("max_gap_ms", double(stats.maxGapMs));
    // 20 ms 节拍本身占一格：扣掉它才是"这一下真卡了多久"，和界面探针同一口径。
    object.insert("max_stall_ms", double(stats.maxGapMs > 20 ? stats.maxGapMs - 20 : 0));
    object.insert("max_gap_at_ms", double(stats.maxGapAtMs));
    object.insert("gaps_over_50ms", stats.over50ms);
    object.insert("watchdog_ticks", stats.ticks);
    object.insert("progress_signals", stats.progressSignals);
    object.insert("transfer_signals", stats.transferSignals);
    if (!stats.errors.isEmpty()) {
        object.insert("errors", QJsonArray::fromStringList(stats.errors));
    }
    return object;
}

double median(QList<double> values) {
    if (values.isEmpty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    return values.at((values.size() - 1) / 2);  // 偶数取偏低那一个，和 perf-startup 一致
}

void printRow(const QString &label, const RoundStats &stats) {
    // "stall" 是扣掉 20 ms 节拍之后的那一格，和界面探针同一口径（见 docs/dev/PERFORMANCE.md）。
    std::fprintf(stdout, "%-28s %8.0f ms  %7.1f MiB/s  stall %4lld ms (>50ms x%2d)  fail %d%s%s\n",
                 qUtf8Printable(label), stats.wallMs, megabytesPerSecond(effectiveBytes(stats), stats.wallMs),
                 static_cast<long long>(stats.maxGapMs > 20 ? stats.maxGapMs - 20 : 0), stats.over50ms,
                 stats.failed, stats.verified ? "" : "  VERIFY-FAILED",
                 stats.timedOut ? "  TIMEOUT" : "");
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("WTFMLPerfDownload"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("WTFML 并发下载基准（本机 HTTP，Release）"));
    parser.addHelpOption();
    const QCommandLineOption filesOption(QStringLiteral("files"), QStringLiteral("文件数（默认 200）"),
                                         QStringLiteral("n"));
    const QCommandLineOption sizeOption(QStringLiteral("size"),
                                        QStringLiteral("每个文件字节数，可带 K/M（默认 256K）"),
                                        QStringLiteral("bytes"));
    const QCommandLineOption concurrencyOption(QStringLiteral("concurrency"),
                                               QStringLiteral("并发档位（默认 24）"),
                                               QStringLiteral("n"));
    const QCommandLineOption segmentedOption(QStringLiteral("segmented"),
                                             QStringLiteral("允许安全分段（≥4 MiB 的文件会走分段）"));
    const QCommandLineOption speedOption(QStringLiteral("speed-limit"),
                                         QStringLiteral("限速字节/秒，0 = 不限（默认 0）"),
                                         QStringLiteral("bytes"));
    const QCommandLineOption roundsOption(QStringLiteral("rounds"), QStringLiteral("重复轮数（默认 3）"),
                                          QStringLiteral("n"));
    const QCommandLineOption warmupOption(
        QStringLiteral("warmup"),
        QStringLiteral("先跑几轮热身、只留覆盖结果（默认 1；磁盘缓存与连接池都冷的时候第一轮偏慢）"),
        QStringLiteral("n"));
    const QCommandLineOption timeoutOption(QStringLiteral("timeout-ms"),
                                           QStringLiteral("单轮超时（默认 180000）"),
                                           QStringLiteral("ms"));
    const QCommandLineOption labelOption(QStringLiteral("label"), QStringLiteral("报表里的用例名"),
                                         QStringLiteral("name"));
    const QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("结果 JSON 的落点"),
                                        QStringLiteral("path"));
    const QCommandLineOption workOption(QStringLiteral("work-dir"),
                                        QStringLiteral("下载落点根目录（默认系统临时目录）"),
                                        QStringLiteral("path"));
    const QCommandLineOption keepOption(QStringLiteral("keep"),
                                        QStringLiteral("跑完不删临时目录"));
    parser.addOptions({filesOption, sizeOption, concurrencyOption, segmentedOption, speedOption,
                       roundsOption, warmupOption, timeoutOption, labelOption, jsonOption, workOption,
                       keepOption});
    parser.process(app);

    Config config;
    bool ok = true;
    const auto parseInt = [&](const QCommandLineOption &option, int &target) {
        bool parsed = false;
        target = parser.value(option).toInt(&parsed);
        ok = ok && parsed;
    };
    if (parser.isSet(filesOption)) {
        parseInt(filesOption, config.files);
    }
    if (parser.isSet(sizeOption)) {
        bool parsed = false;
        config.fileSize = parseBytes(parser.value(sizeOption), &parsed);
        ok = ok && parsed;
    }
    if (parser.isSet(concurrencyOption)) {
        parseInt(concurrencyOption, config.concurrency);
    }
    if (parser.isSet(speedOption)) {
        const QString text = parser.value(speedOption).trimmed();
        bool parsed = true;
        config.speedLimit = text == QStringLiteral("0") ? 0 : parseBytes(text, &parsed);
        ok = ok && parsed;
    }
    if (parser.isSet(roundsOption)) {
        parseInt(roundsOption, config.rounds);
    }
    if (parser.isSet(warmupOption)) {
        parseInt(warmupOption, config.warmup);
    }
    if (parser.isSet(timeoutOption)) {
        parseInt(timeoutOption, config.timeoutMs);
    }
    config.segmented = parser.isSet(segmentedOption);
    config.label = parser.value(labelOption);
    config.jsonPath = parser.value(jsonOption);
    config.workDir = parser.value(workOption);
    config.keepWorkDir = parser.isSet(keepOption);
    if (!ok || config.files < 1 || config.fileSize < 1 || config.fileSize > 512LL * 1024 * 1024 || config.rounds < 1
        || config.warmup < 0 || config.warmup > 10 || config.timeoutMs < 1 || config.timeoutMs > 900000
        || config.concurrency < DownloadConcurrency::kMin
        || config.concurrency > DownloadConcurrency::kMax) {
        std::fprintf(stderr, "parameter error; see --help\n");
        return 2;
    }

    QTemporaryDir temporary;
    const QString root = config.workDir.isEmpty() ? temporary.path() : config.workDir;
    if (!QDir().mkpath(root)) {
        std::fprintf(stderr, "cannot create work dir\n");
        return 2;
    }
    if (config.workDir.isEmpty()) {
        temporary.setAutoRemove(!config.keepWorkDir);
    }

    const QByteArray payload = makePayload(config.fileSize);
    const QString sha1 = sha1Of(payload);

    // 服务器在**自己的线程**里：它的 socket 写入不能和被测的主线程抢事件循环。
    QThread *serverThread = new QThread;
    serverThread->start();
    auto *server = new HttpFileServer(payload);
    server->moveToThread(serverThread);
    quint16 port = 0;
    QMetaObject::invokeMethod(
        server, [&port, server, &config] { port = server->start(config.files); },
        Qt::BlockingQueuedConnection);
    if (port == 0) {
        std::fprintf(stderr, "cannot listen on loopback\n");
        return 3;
    }

    const QString label = config.label.isEmpty()
        ? QStringLiteral("files=%1 size=%2 conc=%3 seg=%4")
              .arg(config.files)
              .arg(config.fileSize)
              .arg(config.concurrency)
              .arg(config.segmented ? 1 : 0)
        : config.label;
    std::fprintf(stdout, "== %s ==\n", qUtf8Printable(label));
    std::fflush(stdout);

    for (int i = 0; i < config.warmup; ++i) {
        const QString warmupDir = QDir(root).filePath(QStringLiteral("warmup-%1").arg(i));
        QDir(warmupDir).removeRecursively();
        const RoundStats stats = runRound(config, port, warmupDir, payload, sha1);
        printRow(QStringLiteral("warmup %1").arg(i), stats);
    }

    QList<RoundStats> rounds;
    for (int i = 0; i < config.rounds; ++i) {
        const QString roundDir = QDir(root).filePath(QStringLiteral("%1/round-%2")
                                                         .arg(QString::fromLatin1(
                                                             QCryptographicHash::hash(
                                                                 label.toUtf8(),
                                                                 QCryptographicHash::Sha1)
                                                                 .toHex()
                                                                 .left(8)))
                                                         .arg(i));
        QDir(roundDir).removeRecursively();  // 自己这份临时目录，不碰用户数据
        const RoundStats stats = runRound(config, port, roundDir, payload, sha1);
        rounds.append(stats);
        printRow(QStringLiteral("round %1").arg(i), stats);
    }

    QList<double> walls;
    QList<double> gaps;
    QList<double> rates;
    for (const RoundStats &round : rounds) {
        walls.append(round.wallMs);
        gaps.append(double(round.maxGapMs));
        rates.append(megabytesPerSecond(effectiveBytes(round), round.wallMs));
    }
    const double wallMedian = median(walls);
    const double gapMedian = median(gaps);
    std::fprintf(stdout, "%-28s %8.0f ms  %7.1f MiB/s  stall %4.0f ms  (n=%lld)\n", "median",
                 wallMedian, median(rates), gapMedian > 20 ? gapMedian - 20 : 0,
                 static_cast<long long>(rounds.size()));
    std::fflush(stdout);

    if (!config.jsonPath.isEmpty()) {
        QJsonObject report;
        report.insert("tool", QStringLiteral("wtfml-perf-download"));
        report.insert("label", label);
        report.insert("timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));
        QJsonObject configuration;
        configuration.insert("files", config.files);
        configuration.insert("bytes_per_file", double(config.fileSize));
        configuration.insert("concurrency", config.concurrency);
        configuration.insert("segmented", config.segmented);
        configuration.insert("speed_limit", double(config.speedLimit));
        configuration.insert("warmup_rounds", config.warmup);
        report.insert("config", configuration);
        QJsonArray roundArray;
        for (const RoundStats &round : rounds) {
            roundArray.append(roundJson(round));
        }
        report.insert("rounds", roundArray);
        QJsonObject medians;
        medians.insert("wall_ms", wallMedian);
        medians.insert("megabytes_per_second", median(rates));
        medians.insert("max_gap_ms", gapMedian);
        medians.insert("max_stall_ms", gapMedian > 20 ? gapMedian - 20 : 0);
        report.insert("median", medians);
        report.insert("note", QStringLiteral(
                                  "Loopback HTTP/1.1 on 127.0.0.1: measures scheduling, write path and "
                                  "main-thread impact, not WAN latency. Shared payload: every file has "
                                  "identical content."));
        QSaveFile file(config.jsonPath);
        const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
        const bool written = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()
            && file.commit();
        if (!written) {
            std::fprintf(stderr, "cannot write json\n");
        }
    }

    QMetaObject::invokeMethod(server, [server] { server->stop(); }, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(server, [server] { delete server; }, Qt::BlockingQueuedConnection);
    serverThread->quit();
    serverThread->wait();
    delete serverThread;

    if (config.workDir.isEmpty() && config.keepWorkDir) {
        std::fprintf(stdout, "work dir kept: %s\n", qUtf8Printable(root));
    }
    const bool allGood = std::all_of(rounds.cbegin(), rounds.cend(), [](const RoundStats &round) {
        return round.verified && !round.timedOut && round.failed == 0;
    });
    return allGood ? 0 : 1;
}
