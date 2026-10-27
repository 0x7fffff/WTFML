// Opt-in real-network / physical-volume acceptance. Never runs under CTest.
#include "manage/VersionManager.hpp"
#include "download/DownloadManager.hpp"
#include "net/HttpClient.hpp"
#include "presets/ModInstallPlan.hpp"
#include "util/AppPaths.hpp"
#include "util/ZipUtil.hpp"
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QProcess>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

namespace {
bool save(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool download(const QString &url, const QString &target, const QString &sha1, qint64 size, int cancelAfter = 0) {
    DownloadManager manager;
    manager.setUseMirror(false); manager.setMaxConcurrentDownloads(1);
    QEventLoop loop; bool success = false;
    QObject::connect(&manager, &DownloadManager::allDownloadsFinished, &loop, [&](bool ok) { success = ok; loop.quit(); });
    QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { manager.cancelBatch(); loop.quit(); });
    deadline.start(120000);
    manager.beginBatch(); manager.downloadFile(url, target, sha1, size); manager.endBatch();
    if (cancelAfter > 0) QTimer::singleShot(cancelAfter, &manager, [&] { manager.cancelBatch(); loop.quit(); });
    loop.exec();
    return success && Presets::verifyDownloadedFile(target, size, sha1).ok;
}
int importRestricted(const QString &root, int cancelAfter, bool crash) {
    if (!QFileInfo::exists(root + "/owned.json")) return 2;
    QCoreApplication::instance()->setProperty("wtfml.testDefaultMinecraftDir", root + "/game");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + "/settings");
    AppPaths::setMinecraftDir(root + "/game");
    const QByteArray manifest = R"({"manifestType":"minecraftModpack","manifestVersion":1,"name":"RestrictedBoundary","version":"1","minecraft":{"version":"1.20.1","modLoaders":[{"id":"fabric-0.16.14","primary":true}]},"files":[{"projectID":1121856,"fileID":5813216,"required":true}],"overrides":"overrides"})";
    if (!save(root + "/manifest.json", manifest)) return 2;
    QString problem;
    const QString zip = root + "/restricted-pack.zip";
    if (!QFileInfo::exists(zip) && !ZipUtil::createZip(zip, {{"manifest.json", root + "/manifest.json"}}, &problem)) return 2;
    auto parsed = ModpackImport::parseCurseForgeManifest(manifest);
    if (!parsed.ok) return 2;
    VersionManager manager;
    manager.setUseMirror(false);
    problem = manager.resolveCurseForgeFiles(&parsed.pack);
    if (!problem.isEmpty() || parsed.pack.files.size() != 1) return 2;
    const auto file = parsed.pack.files.first();
    const QString id = QStringLiteral("RestrictedBoundaryVerified");
    const QString instance = root + "/game/versions/" + id;
    QEventLoop loop; bool completed = false, success = false, requestedCancel = false;
    QString reason;
    QObject::connect(&manager, &VersionManager::versionDownloadFinished, &loop,
        [&](const QString &id, bool ok, const QString &text) {
            if (id != QStringLiteral("RestrictedBoundaryVerified")) return;
            completed = true; success = ok; reason = text; loop.quit();
        });
    QObject::connect(&manager, &VersionManager::modpackImportMessage, &loop, [](const QString &text) {
        std::printf("IMPORT %s\n", text.toUtf8().constData()); std::fflush(stdout);
    });
    QElapsedTimer elapsed; elapsed.start();
    QTimer poll; poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (elapsed.elapsed() > 540000) { manager.cancelActiveDownload(); reason = "acceptance deadline"; loop.quit(); return; }
        if (!manager.isDownloading() || elapsed.elapsed() < (cancelAfter > 0 ? cancelAfter : 500)) return;
        if (crash) {
            save(root + "/import-crash.json", QJsonDocument(QJsonObject{{"abruptExit", 86},
                {"importing", QFileInfo::exists(instance + "/wtfml.importing")},
                {"meta", QFileInfo::exists(instance + "/wtfml.meta")}}).toJson());
            std::printf("IMPORT abrupt exit in active production download\n"); std::fflush(stdout);
#ifdef Q_OS_WIN
            TerminateProcess(GetCurrentProcess(), 86);
#endif
            std::_Exit(86);
        }
        if (cancelAfter > 0 && !requestedCancel) requestedCancel = manager.cancelActiveDownload();
    }); poll.start();
    manager.importModpack(zip, id, parsed.pack, {}, QString::fromLatin1(
        QCryptographicHash::hash(manifest, QCryptographicHash::Sha256).toHex()));
    if (!completed) loop.exec();
    poll.stop();
    QFile importReport(instance + "/wtfml.import-report.json");
    QJsonObject persisted;
    if (importReport.open(QIODevice::ReadOnly)) persisted = QJsonDocument::fromJson(importReport.readAll()).object();
    const bool verified = Presets::verifyDownloadedFile(instance + '/' + file.path, file.size, file.sha1).ok;
    const bool marker = QFileInfo::exists(instance + "/wtfml.importing");
    const bool meta = QFileInfo::exists(instance + "/wtfml.meta");
    const bool proof = QFileInfo::exists(instance + "/wtfml.import-ready.json");
    const bool targetExists = QFileInfo::exists(instance);
    const bool ok = requestedCancel ? completed && !success && !meta && (!targetExists || marker)
        : completed && success && verified && meta && proof && !marker && persisted.value("complete").toBool();
    save(root + (cancelAfter > 0 ? "/import-cancel.json" : "/import-complete.json"), QJsonDocument(QJsonObject{
        {"ok", ok}, {"completed", completed}, {"success", success}, {"cancelRequested", requestedCancel},
        {"realFileVerified", verified}, {"stageProof", proof}, {"targetExists", targetExists}, {"marker", marker}, {"meta", meta}, {"reason", reason},
        {"elapsedMs", elapsed.elapsed()}, {"completeReport", persisted.value("complete").toBool()}}).toJson());
    std::printf("IMPORT_RESULT ok=%d cancelled=%d elapsed=%lld reason=%s\n", ok, requestedCancel,
        static_cast<long long>(elapsed.elapsed()), reason.toUtf8().constData());
    return ok ? 0 : 1;
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("WTFML-Acceptance"); app.setApplicationName("ModpackBoundary");
    QCommandLineParser parser;
    parser.addOptions({QCommandLineOption("root", "New isolated acceptance directory", "path"),
        QCommandLineOption("child", "Download worker"), QCommandLineOption("source", "Worker source", "url"),
        QCommandLineOption("import", "Run real restricted CF pack installation"), QCommandLineOption("crash", "Abrupt exit during import I/O"),
        QCommandLineOption("sha1", "Expected SHA-1", "hash"), QCommandLineOption("size", "Expected size", "bytes"),
        QCommandLineOption("cancel", "Cancel worker after milliseconds", "milliseconds", "0")});
    parser.process(app);
    const QString root = QDir::cleanPath(parser.value("root"));
    if (!QDir::isAbsolutePath(root) || QFileInfo(root).isSymLink() || QFileInfo(root).isJunction()
        || root == QDir(root).rootPath()) return 2;
    if (parser.isSet("import")) return importRestricted(root, parser.value("cancel").toInt(), parser.isSet("crash"));
    if (parser.isSet("child")) {
        if (!QFileInfo::exists(root + "/owned.json")) return 2;
        const bool ok = download(parser.value("source"), root + "/recovered.jar", parser.value("sha1"),
            parser.value("size").toLongLong(), parser.value("cancel").toInt());
        return ok ? 0 : 3;
    }
    // Existing non-owned directories are never adopted or cleared.
    if (QFileInfo::exists(root) || !QDir().mkpath(root)) return 2;
    if (!save(root + "/owned.json", "{\"format\":\"wtfml-modpack-boundary/1\"}")) return 2;
    app.setProperty("wtfml.testDefaultMinecraftDir", root + "/game");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + "/settings");
    AppPaths::setMinecraftDir(root + "/game");
    QNetworkAccessManager network;
    const QString api = "https://mod.mcimirror.top/curseforge/v1";
    QJsonObject report{{"format", "wtfml-modpack-boundary/1"}, {"root", root},
        {"projectId", 1121856}, {"fileId", 5813216}};
    const auto project = HttpClient::send(&network, QUrl(api + "/mods/1121856"), HttpClient::Verb::Get);
    save(root + "/project.json", project.raw);
    const auto file = HttpClient::send(&network, QUrl(api + "/mods/1121856/files/5813216"), HttpClient::Verb::Get);
    save(root + "/file.json", file.raw);
    const auto auth = HttpClient::send(&network, QUrl("https://api.curseforge.com/v1/mods/1121856/files/5813216"), HttpClient::Verb::Get);
    report.insert("officialNoKeyStatus", auth.status);
    const auto projectData = project.json.value("data").toObject();
    const auto fileData = file.json.value("data").toObject();
    const bool restricted = project.ok() && projectData.value("allowModDistribution").isBool()
        && !projectData.value("allowModDistribution").toBool()
        && file.ok() && fileData.value("downloadUrl").isNull();
    report.insert("restrictedMetadataConfirmed", restricted);
    ModpackImport::Pack pack;
    pack.format = ModpackImport::Format::CurseForge;
    pack.pendingRefs = {{1121856, 5813216}};
    VersionManager versions;
    versions.setUseMirror(false);
    const QString problem = versions.resolveCurseForgeFiles(&pack);
    report.insert("resolveProblem", problem);
    if (!restricted || pack.files.size() != 1 || pack.files.first().urls.isEmpty()) {
        save(root + "/report.json", QJsonDocument(report).toJson()); return 1;
    }
    const auto item = pack.files.first();
    const QString target = root + "/real-restricted.jar";
    const bool realDownloaded = download(item.urls.first(), target, item.sha1, item.size);
    report.insert("realDownloadVerified", realDownloaded);
    report.insert("sha1", item.sha1); report.insert("size", item.size);
    if (!realDownloaded) { save(root + "/report.json", QJsonDocument(report).toJson()); return 1; }
    QFile actual(target); if (!actual.open(QIODevice::ReadOnly)) return 2;
    const auto bytes = actual.readAll(); actual.close();
    // Replay the real, verified payload slowly to guarantee a cancellation window
    // while writing the requested physical volume. This is not an internet speed test.
    QTcpServer slow;
    if (!slow.listen(QHostAddress::LocalHost)) return 2;
    QObject::connect(&slow, &QTcpServer::newConnection, &slow, [&] {
        while (auto *socket = slow.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, bytes] {
                const auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("sent").toBool()) return;
                socket->setProperty("sent", true);
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\nConnection: close\r\n\r\n");
                auto *timer = new QTimer(socket);
                QObject::connect(timer, &QTimer::timeout, socket, [socket, timer, bytes] {
                    const int at = socket->property("at").toInt();
                    socket->write(bytes.mid(at, 32)); socket->setProperty("at", at + 32);
                    if (at + 32 >= bytes.size()) { timer->stop(); socket->disconnectFromHost(); }
                }); timer->start(8);
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const QString source = QStringLiteral("http://127.0.0.1:%1/file.jar").arg(slow.serverPort());
    const auto runChild = [&](bool cancel, bool kill) {
        QProcess process; QEventLoop loop; QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
        QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { process.kill(); loop.quit(); });
        auto args = QStringList{"--child", "--root", root, "--source", source,
            "--sha1", item.sha1, "--size", QString::number(item.size)};
        if (cancel) args << "--cancel" << "150";
        process.start(app.applicationFilePath(), args);
        if (kill) QTimer::singleShot(400, &process, [&] { process.kill(); });
        deadline.start(30000); loop.exec();
        if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(5000); }
        save(root + (kill ? "/killed.log" : cancel ? "/cancelled.log" : "/restarted.log"),
             process.readAllStandardOutput() + process.readAllStandardError());
        return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    };
    const int cancelled = runChild(true, false);
    report.insert("cancelledSafely", cancelled == 3 && !QFileInfo::exists(root + "/recovered.jar"));
    const int killed = runChild(false, true);
    report.insert("killedWithoutPublishing", killed == -1 && !QFileInfo::exists(root + "/recovered.jar"));
    const int recovered = runChild(false, false);
    report.insert("newProcessRecovered", recovered == 0 &&
        Presets::verifyDownloadedFile(root + "/recovered.jar", item.size, item.sha1).ok);
    report.insert("scope", "Real restricted public CDN payload; real target volume; controlled slow replay; no game launch, no claim of whole-pack finalization.");
    const bool ok = restricted && realDownloaded && (auth.status == 401 || auth.status == 403)
        && report.value("cancelledSafely").toBool() && report.value("killedWithoutPublishing").toBool()
        && report.value("newProcessRecovered").toBool();
    report.insert("ok", ok);
    save(root + "/report.json", QJsonDocument(report).toJson());
    std::printf("Boundary acceptance: %s; %s\n", ok ? "PASS" : "FAIL", root.toUtf8().constData());
    return ok ? 0 : 1;
}
