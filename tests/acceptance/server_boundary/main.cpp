// Explicit real-server acceptance. Never runs in CI or uses existing user servers.
#include "server/ServerInstallController.hpp"
#include "server/ServerProcess.hpp"
#include "server/ServerRegistry.hpp"
#include "server/ServerRun.hpp"
#include "download/DownloadManager.hpp"
#include "util/AppPaths.hpp"
#include "util/SecretRedactor.hpp"
#include "support/SettingsIsolation.hpp"
#include <QCoreApplication>
#include <QApplication>
#include <QCommandLineParser>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTcpServer>
#include <QTimer>

bool save(const QString &path, const QByteArray &bytes) {
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QJsonObject serverUiEvidence(const QString &root, const ServerInstall::Request &request);
QJsonObject serverWizardInstall(const QString &root, bool checkpoint, bool client, bool resumeOnly, const QString &java);
int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv); app.setOrganizationName("WTFML-server-acceptance"); app.setApplicationName("isolated");
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOptions({{"root", "New isolated root", "root"}, {"java", "Java executable", "java"},
        {"flavor", "vanilla/fabric/forge", "flavor"}, {"accept-eula", "Explicitly authorized test EULA acceptance"},
        {"wrong-java", "Optional real older Java executable", "java"}, {"run-only", "Recheck an owned installed acceptance root"},
        {"ui-only", "Check wizard and console export against owned actual server logs"},
        {"wizard-install", "Run actual MainWindow vanilla installation with checked isolated settings"},
        {"repair-install", "Retry installation only in a previously owned acceptance root"},
        {"install-only", "Stop after installation without starting a server"},
        {"checkpoint-ui", "Pause and reopen actual MainWindow server installation; no server launch"},
        {"client-checkpoint-ui", "Actual Forge client installation and FOV preset after window restart; no game launch"},
        {"resume-client-ui", "Continue an owned client checkpoint without recreating its preset or original request"},
        {"install-seconds", "Installation deadline, at most 650 seconds", "seconds", "300"}});
    parser.process(app);
    const auto root = parser.value("root"), java = parser.value("java"), flavor = parser.value("flavor");
    const bool runOnly = parser.isSet("run-only") || parser.isSet("ui-only");
    const bool repair = parser.isSet("repair-install");
    bool validSeconds = false; const int seconds = parser.value("install-seconds").toInt(&validSeconds);
    if (!validSeconds || seconds < 1 || seconds > 650 || (repair && runOnly)) return 2;
    if (!QDir::isAbsolutePath(root) || !QFileInfo(java).isFile() ||
        !QStringList{"vanilla", "fabric", "forge"}.contains(flavor)) return 2;
    if (runOnly || repair) {
        QFile marker(root + "/owned.json");
        if (!marker.open(QIODevice::ReadOnly) || marker.size() > 4096) return 2;
        const auto owned = QJsonDocument::fromJson(marker.readAll()).object();
        if (owned.value("root").toString() != QDir(root).canonicalPath() || owned.value("flavor").toString() != flavor) return 2;
        QFile previous(root + "/report.json");
        if (previous.exists() && !previous.copy(root + "/report-before-run-only-" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".json")) return 2;
    } else {
        if (QFileInfo::exists(root) || !QDir().mkpath(root) || !save(root + "/owned.json",
            QJsonDocument(QJsonObject{{"root", QDir(root).canonicalPath()}, {"flavor", flavor}}).toJson())) return 2;
    }
    AppPaths::setMinecraftDir(root + "/game");
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + "/settings");
    requireIsolatedSettings(root, "server_boundary");
    ServerInstall::Request request; request.gameVersion = "1.20.1";
    request.flavor = flavor == "forge" ? ServerInstall::Flavor::Forge : flavor == "fabric" ? ServerInstall::Flavor::Fabric : ServerInstall::Flavor::Vanilla;
    if (flavor == "fabric") { request.loaderVersion = "0.16.14"; request.installerVersion = "1.0.1"; }
    if (flavor == "forge") request.loaderVersion = "47.2.0";
    if (parser.isSet("wizard-install") || parser.isSet("checkpoint-ui") || parser.isSet("client-checkpoint-ui") || parser.isSet("resume-client-ui")) {
        if (runOnly || flavor != "vanilla") return 2;
        const bool resume = parser.isSet("resume-client-ui");
        if (resume && !repair) return 2;
        QFile previousUi(root + "/wizard-install.json");
        if (previousUi.exists() && !previousUi.copy(root + "/wizard-install-before-" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".json")) return 2;
        const auto report = serverWizardInstall(root, parser.isSet("checkpoint-ui") || parser.isSet("client-checkpoint-ui") || resume, parser.isSet("client-checkpoint-ui") || resume, resume, java);
        save(root + "/wizard-install.json", QJsonDocument(report).toJson());
        return report.value("ok").toBool() ? 0 : 1;
    }
    if (parser.isSet("ui-only")) {
        const auto report = serverUiEvidence(root, request);
        save(root + "/ui-report.json", QJsonDocument(report).toJson());
        return report.value("ok").toBool() ? 0 : 1;
    }
    ServerModel::Info info; info.name = "Acceptance"; info.gameVersion = request.gameVersion;
    info.flavor = request.flavor; info.flavorKnown = true; info.loaderVersion = request.loaderVersion; info.installerVersion = request.installerVersion;
    QString problem;
    if (!runOnly && !repair && !ServerRegistry::createServer("Acceptance", info, &problem)) return 2;
    DownloadManager downloads; ServerInstallRun::ServerInstallController installer;
    installer.setDownloadManager(&downloads);
    QEventLoop installLoop; bool installed = runOnly && ServerRegistry::describe("Acceptance").installed;
    QObject::connect(&installer, &ServerInstallRun::ServerInstallController::message, &app, [root](const QString &line) {
        const auto clean = SecretRedactor::redact(line);
        QFile log(root + "/installation.log");
        if (log.open(QIODevice::WriteOnly | QIODevice::Append)) log.write(clean.toUtf8() + '\n');
        std::printf("%s\n", clean.left(200).toUtf8().constData()); std::fflush(stdout);
    });
    QObject::connect(&installer, &ServerInstallRun::ServerInstallController::finished, &installLoop,
        [&](auto outcome, const QString &message) { installed = outcome == ServerInstallRun::Outcome::Succeeded; problem = message; installLoop.quit(); });
    QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &installLoop, [&] { installer.cancel(); problem = "install deadline"; installLoop.quit(); });
    if (!runOnly) {
        deadline.start(seconds * 1000); installer.start(request, "Acceptance", java);
        if (installer.isRunning()) installLoop.exec();
    }
    deadline.stop();
    QJsonObject report{{"installed", installed}, {"installProblem", problem}, {"flavor", flavor}, {"installationReused", runOnly}};
    if (!installed) { save(root + "/report.json", QJsonDocument(report).toJson()); return 1; }
    if (parser.isSet("install-only")) { report["ok"] = true; save(root + "/report.json", QJsonDocument(report).toJson()); return 0; }
    const auto plan = ServerInstall::planFor(request);
    const auto dir = AppPaths::serverDir("Acceptance");
    if (runOnly && !save(dir + "/eula.txt", "eula=false\n")) return 2;
    QTcpServer port; if (!port.listen(QHostAddress::LocalHost, 0)) return 2;
    const auto number = port.serverPort(); port.close();
    if (!save(dir + "/server.properties", QByteArray("server-ip=127.0.0.1\nserver-port=") + QByteArray::number(number) +
        "\nonline-mode=false\nlevel-name=acceptance-world\nview-distance=2\nsimulation-distance=2\nmax-players=1\n")) return 2;
    auto run = [&](const QString &label, bool stopOnReady, bool killOnReady, const QString &program = QString()) {
        ServerRun::ServerProcess process; QEventLoop loop; bool ready = false, exited = false; int code = -999;
        QObject::connect(&process, &ServerRun::ServerProcess::statusChanged, &loop, [&](const QString &, auto state) {
            if (state == ServerModel::Status::Running) {
                ready = true;
                if (stopOnReady) QTimer::singleShot(1000, &process, [&] { process.requestStop(); });
                if (killOnReady) QTimer::singleShot(1000, &process, [&] { process.forceKill(); });
            }
        });
        QObject::connect(&process, &ServerRun::ServerProcess::exited, &loop,
            [&](const QString &, int value) { exited = true; code = value; loop.quit(); });
        QTimer timer; timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, [&] { process.forceKill(); loop.quit(); }); timer.start(90000);
        process.start("Acceptance", dir, program.isEmpty() ? java : program, ServerRun::launchArguments(plan, 2048), 10000);
        if (process.isRunning()) loop.exec();
        timer.stop(); if (process.isRunning()) process.forceKill();
        const auto text = SecretRedactor::redact(process.logLines().join('\n'));
        save(root + '/' + label + ".log", text.toUtf8());
        report[label] = QJsonObject{{"ready", ready}, {"exited", exited}, {"exitCode", code}, {"forced", process.wasForceKilled()}};
        if (label == "eula-gate") return exited && !ready && !process.wasForceKilled() && text.contains("eula", Qt::CaseInsensitive);
        if (label == "port-occupied" || label == "wrong-java") return exited && !ready && !process.wasForceKilled() && process.status() == ServerModel::Status::Crashed;
        return ready && exited && (killOnReady ? process.wasForceKilled() : code == 0);
    };
    const bool gate = run("eula-gate", false, false); bool normal = false, killed = false;
    if (gate && parser.isSet("accept-eula") && save(dir + "/eula.txt", "eula=true\n")) {
        normal = run("normal-stop", true, false); if (normal) killed = run("force-kill", false, true);
    }
    bool occupied = false, wrongJava = false;
    if (normal && port.listen(QHostAddress::LocalHost, number)) { occupied = run("port-occupied", false, false); port.close(); }
    if (normal && QFileInfo(parser.value("wrong-java")).isFile()) wrongJava = run("wrong-java", false, false, parser.value("wrong-java"));
    report["ok"] = gate && normal && killed && occupied && wrongJava;
    save(root + "/report.json", QJsonDocument(report).toJson()); return report.value("ok").toBool() ? 0 : 1;
}
