#pragma once
#include "presets/PresetModInstaller.hpp"
#include "download/ResourceStore.hpp"
#include "PresetModEditor.hpp"
#include "PresetOptionsEditor.hpp"
#include "presets/PresetStore.hpp"
#include "presets/PresetApply.hpp"
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QEventLoop>
#include <QFile>
#include <QDir>
#include <QTableWidget>
#include <QFontDatabase>
#include <QApplication>
#include <cstdio>

inline int runPresetInstallChecks() {
    int failures = 0;
    auto check = [&](bool ok, const char *name) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", name); if (!ok) ++failures; };
    QTemporaryDir scratch;
    QTcpServer server;
    check(scratch.isValid() && server.listen(QHostAddress::LocalHost, 0), "local preset fixture starts");
    if (!server.isListening()) return 1;
    const QByteArray payload("preset-fixture-jar");
    int scenario = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("served").toBool()) return;
                socket->setProperty("served", true);
                if (scenario == 5) return;
                QByteArray body = payload;
                if (request.contains("/project/")) {
                    const bool missing = request.contains("/missing/");
                    const bool dependency = request.contains("/dep/");
                    QJsonArray versions;
                    if (!missing) {
                        const QString hash = (scenario == 3 || ((scenario == 7 || scenario == 10) && dependency)) ? QString(40, QLatin1Char('0')) :
                            QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha1).toHex());
                        QJsonArray dependencies;
                        if (!dependency) dependencies.append(QJsonObject{{"project_id", "dep"}, {"dependency_type", "required"}});
                        if (!dependency && scenario == 8) {
                            auto pin = dependencies[0].toObject(); pin.insert("version_id", "dep-old"); dependencies[0] = pin;
                        }
                        versions.append(QJsonObject{{"id", dependency ? "dep-version" : "main-version"},
                            {"version_number", "1"}, {"game_versions", QJsonArray{"1.20.1"}}, {"loaders", QJsonArray{"fabric"}},
                            {"dependencies", dependencies}, {"files", QJsonArray{QJsonObject{
                                {"url", QStringLiteral("http://127.0.0.1:%1/file.jar").arg(server.serverPort())},
                                {"filename", dependency ? "dep.jar" : "main.jar"}, {"size", payload.size()}, {"primary", true},
                                {"hashes", QJsonObject{{"sha1", hash}}}}}}});
                        if (dependency && scenario == 8) {
                            auto old = versions[0].toObject(); old.insert("id", "dep-old");
                            auto files = old.value("files").toArray(); auto file = files[0].toObject();
                            file.insert("filename", "dep-old.jar"); files[0] = file; old.insert("files", files);
                            versions.append(old);
                        }
                    }
                    body = QJsonDocument(versions).toJson(QJsonDocument::Compact);
                }
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                              "\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    for (scenario = 0; scenario < 11; ++scenario) {
        const QString root = scratch.filePath(QString::number(scenario)); QDir().mkpath(root);
        ResourceStore store;
        store.setModrinthApiBaseOverride(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        auto *task = new Presets::PresetModInstaller(&store, &store);
        QList<Presets::ModReference> refs;
        Presets::ModReference main; main.platform = QStringLiteral("modrinth");
        main.projectId = scenario == 1 ? QStringLiteral("missing") : QStringLiteral("main");
        if (scenario == 7 || scenario == 10) main.required = false;
        if (scenario == 9) main.gameVersion = QStringLiteral("1.19.4");
        refs.append(main);
        if (scenario == 10) { auto dependency = main; dependency.projectId = QStringLiteral("dep"); dependency.required = true; refs.append(dependency); }
        if (scenario == 2) { auto optional = main; optional.projectId = QStringLiteral("missing"); optional.required = false; refs.append(optional); }
        if (scenario == 4) { QDir().mkpath(root + "/mods"); QFile f(root + "/mods/main.jar"); if (f.open(QIODevice::WriteOnly)) f.write("existing"); }
        QEventLoop loop; QTimer deadline; deadline.setSingleShot(true);
        Presets::PresetModInstaller::Result result;
        bool completed = false;
        QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { task->cancel(); }); deadline.start(6000);
        task->start(refs, root, QStringLiteral("1.20.1"), QStringLiteral("fabric"), {},
            [&] { return scenario == 6; }, [&](const auto &value) { result = value; completed = true; loop.quit(); });
        if (scenario == 5) QTimer::singleShot(50, task, [task] { task->cancel(); });
        if (!completed) loop.exec();
        check(completed && result.ok == (scenario == 0 || scenario == 8), "preset resolves/downloads or reports an explicit partial/failure result");
        if (scenario == 0 || scenario == 2)
            check(QFile::exists(root + "/mods/main.jar") && QFile::exists(root + "/mods/dep.jar"), "main mod and required dependency reach the instance");
        if (scenario == 1 || scenario == 3 || scenario == 5 || scenario == 6)
            check(!QFile::exists(root + "/mods/main.jar"), "missing/checksum/cancel/running guard never writes a main mod");
        if (scenario == 4) { QFile f(root + "/mods/main.jar"); check(f.open(QIODevice::ReadOnly) && f.readAll() == "existing", "existing conflicting jar is preserved"); }
        if (scenario == 2) check(result.optionalFailures > 0, "optional missing is reported independently from installed required mods");
        if (scenario == 5) check(result.cancelled, "cancellation is explicit");
        if (scenario == 7) check(!QFile::exists(root + "/mods/main.jar") && result.optionalFailures > 0,
                                 "optional parent is not installed when its required dependency download fails");
        if (scenario == 8) check(QFile::exists(root + "/mods/dep-old.jar") && !QFile::exists(root + "/mods/dep.jar"),
                                 "exact dependency version is selected rather than the newest incompatible release");
        if (scenario == 9) check(!QFile::exists(root + "/mods/main.jar"), "preset version restriction never installs into the wrong game version");
        if (scenario == 10) check(!QFile::exists(root + "/mods/main.jar") && !QFile::exists(root + "/mods/dep.jar"),
                                  "dependency encountered through optional parent stays required for an explicit required root");
        QFile audit(root + "/wtfml.preset-mod-install.json");
        const bool audited = audit.open(QIODevice::ReadOnly);
        const auto record = audited ? QJsonDocument::fromJson(audit.readAll()).object() : QJsonObject();
        check(audited && record.value("gameVersion").toString() == QStringLiteral("1.20.1") &&
              record.value("success").toBool() == result.ok, "preset operation records context and truthful result");
    }
    PresetModEditor editor({{QStringLiteral("modrinth"), QStringLiteral("abc123"), QStringLiteral("Example"), {}, {}, false}});
    const auto values = editor.entries();
    check(values.size() == 1 && values[0].projectId == QStringLiteral("abc123") && !values[0].required,
          "mod preset editor round-trips stable identity and optional status");
    PresetOptionsEditor options({{QStringLiteral("fov"), QStringLiteral("0.5")},
                                {QStringLiteral("key_key.jump"), QStringLiteral("key.keyboard.space")}});
    check(options.entries().size() == 2 && options.entries()[0].value == QStringLiteral("0.5"),
          "game settings editor round-trips selected options and keys");
    const auto presetRoot = scratch.filePath(QStringLiteral("presets"));
    auto manifest = Presets::create(presetRoot, QStringLiteral("settings"), {}, Presets::PresetCategory::GameSettings);
    Presets::PresetSection section;
    section.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    section.kind = Presets::SectionKind::VanillaOptions;
    section.items = QJsonArray{QJsonObject{{"key", "fov"}, {"value", "0.5"}}};
    manifest.sections.append(section);
    check(Presets::saveManifest(presetRoot, manifest), "edited settings manifest saves atomically");
    const auto gameRoot = scratch.filePath(QStringLiteral("settings-instance")); QDir().mkpath(gameRoot);
    QFile config(gameRoot + "/options.txt");
    check(config.open(QIODevice::WriteOnly) && config.write("fov:0.0\nunknownPreserved:yes\n") > 0,
          "settings target fixture is isolated"); config.close();
    Presets::PresetApplyRequest apply;
    apply.instanceId = QStringLiteral("test"); apply.instanceRoot = gameRoot;
    const auto applied = Presets::applyPresetToInstance(presetRoot, manifest, apply);
    const bool opened = config.open(QIODevice::ReadOnly);
    const auto contents = config.readAll(); config.close();
    check(applied.ok && opened && contents.contains("fov:0.5") && contents.contains("unknownPreserved:yes"),
          "settings application changes selected keys and preserves unknown fields");
    const QString preview = qEnvironmentVariable("WTFML_PRESET_PREVIEW_DIR");
    if (!preview.isEmpty()) {
        QDir().mkpath(preview);
        const int font = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        const auto families = QFontDatabase::applicationFontFamilies(font);
        if (!families.isEmpty()) QApplication::setFont(QFont(families.first(), 10));
        qApp->setStyleSheet(Theme::widgetStyleSheet());
        editor.show(); QApplication::processEvents();
        check(editor.grab().save(QDir(preview).filePath(QStringLiteral("preset-mod-editor.png"))), "mod editor preview renders");
        editor.hide(); options.show(); QApplication::processEvents();
        check(options.grab().save(QDir(preview).filePath(QStringLiteral("preset-options-editor.png"))), "settings editor preview renders");
        options.hide();
    }
    return failures ? 1 : 0;
}
