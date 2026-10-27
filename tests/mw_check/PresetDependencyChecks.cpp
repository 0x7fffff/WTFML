#include "PresetDependencyChecks.hpp"
#include "download/ResourceStore.hpp"
#include "PresetModReview.hpp"
#include <QDialogButtonBox>
#include <QCoreApplication>
#include <QEvent>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>

int runPresetDependencyChecks() {
    int failed = 0;
    const auto check = [&](bool ok, const char *text) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", text); if (!ok) ++failed; };
    QTcpServer server; check(server.listen(QHostAddress::LocalHost), "exact dependency fixture listens");
    if (!server.isListening()) return failed;
    int mode = 0, downloads = 0; bool validPost = true;
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    const QByteArray payload("review-fixture");
    const auto sha = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha1).toHex());
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                const int split = request.indexOf("\r\n\r\n"); if (split < 0) return;
                int length = 0;
                for (auto line : request.left(split).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (request.size() < split + 4 + length || socket->property("sent").toBool()) return;
                socket->setProperty("sent", true);
                const bool exact = request.startsWith("POST /v1/mods/files ");
                if (exact) validPost = validPost
                    && QJsonDocument::fromJson(request.mid(split + 4)).object().value("fileIds").toArray() == QJsonArray{888};
                const QJsonObject metadata{
                    {"id", mode == 1 ? 889 : 888}, {"modId", 777}, {"fileName", "exact.jar"},
                    {"fileLength", 1}, {"downloadUrl", "https://example.invalid/exact.jar"},
                    {"hashes", QJsonArray{QJsonObject{{"algo", 1}, {"value", QString(40, 'a')}}}},
                    {"gameVersions", QJsonArray{"1.20.1", "Fabric"}}, {"dependencies", QJsonArray{
                        QJsonObject{{"fileId", 999}, {"relationType", 3}}}}};
                auto body = QJsonDocument(QJsonObject{{"data", QJsonArray{metadata}}}).toJson();
                if (!exact) {
                    body = "[]";
                    if (request.contains("/project/alt/version") || request.contains("/project/dep/version")) {
                        const bool alt = request.contains("/project/alt/version");
                        const QJsonObject file{{"filename", alt ? "alt.jar" : "dep.jar"},
                            {"url", base + "/file.jar"}, {"size", payload.size()}, {"primary", true},
                            {"hashes", QJsonObject{{"sha1", sha}}}};
                        const QJsonObject release{{"id", alt ? "alt-v" : "dep-v"},
                            {"project_id", alt ? "alt" : "dep"}, {"game_versions", QJsonArray{"1.20.1"}},
                            {"loaders", QJsonArray{"fabric"}}, {"dependencies", alt ? QJsonArray{QJsonObject{
                                {"project_id", "dep"}, {"dependency_type", "required"}}} : QJsonArray{}},
                            {"files", QJsonArray{file}}};
                        body = QJsonDocument(QJsonArray{release}).toJson();
                    } else if (request.startsWith("GET /file.jar ")) { ++downloads; body = payload; }
                }
                socket->write("HTTP/1.1 " + QByteArray(exact && mode == 2 ? "403 Forbidden" : "200 OK")
                    + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    ResourceStore store;
    store.setCurseForgeApiBaseOverride(QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()));
    for (mode = 0; mode < 3; ++mode) {
        QEventLoop loop; bool answered = false; QString project, problem; ResourceCatalog::VersionEntry version;
        const auto connection = QObject::connect(&store, &ResourceStore::curseForgeVersionFinished, &loop,
            [&](const QString &, const QString &p, const auto &v, const QString &e) { answered = true; project = p; version = v; problem = e; loop.quit(); });
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        store.fetchCurseForgeVersion("exact", "888"); loop.exec(); QObject::disconnect(connection);
        check(answered && validPost && (mode == 0 ? project == "777" && version.id == "888" && problem.isEmpty() : !problem.isEmpty()),
            "file-only CurseForge lookup validates identity and rejects mismatched release or authentication failure");
        if (mode == 0) check(version.dependencies.size() == 1 && version.dependencies.first().projectId.isEmpty()
            && version.dependencies.first().versionId == "999", "CurseForge explicit file-only dependency preserves the exact pin for reverse lookup");
    }
    store.setModrinthApiBaseOverride(base);
    for (int action = 0; action < 4; ++action) {
        QTemporaryDir root; QEventLoop loop; bool completed = false, current = true, injected = false; int reviews = 0;
        Presets::PresetModInstaller::Result result;
        const int before = downloads;
        auto *task = new Presets::PresetModInstaller(nullptr, &store);
        task->setPlanCurrent([&] {
            if (action == 3 && !injected && !QDir(root.path() + "/mods").entryList(
                {".wtfml-preset-*"}, QDir::Files | QDir::Hidden).isEmpty()) {
                QFile external(root.path() + "/mods/dep.jar");
                injected = external.open(QIODevice::WriteOnly) && external.write("external-change") == 15;
            }
            return current;
        });
        task->setReviewHandler([&](const Presets::ModInstallPlan &plan, auto reply) {
            ++reviews;
            Presets::PresetModInstaller::ReviewDecision decision;
            if (action == 1) { reply(decision); return; }
            decision.accepted = true;
            if (reviews == 1) {
                Presets::ModReference alt; alt.platform = "modrinth"; alt.projectId = "alt";
                decision.alternatives.insert("modrinth:missing", alt);
            } else {
                check(plan.ok && plan.rows.size() == 2 && downloads == before,
                    "replacement and necessary child are reviewed together before any file download");
                if (action == 2) current = false;
            }
            reply(decision);
        });
        Presets::ModReference ref; ref.platform = "modrinth"; ref.projectId = "missing";
        QTimer::singleShot(4000, &loop, &QEventLoop::quit);
        task->start({ref}, root.path(), "1.20.1", "fabric", {}, [] { return false; },
            [&](auto value) { result = value; completed = true; loop.quit(); });
        if (!completed) loop.exec();
        check(completed && (action == 0 ? result.ok && result.installed == 2 && reviews == 2
            : action == 3 ? !result.ok && injected && !QFile::exists(root.path() + "/mods/alt.jar")
            : !result.ok && downloads == before && !QFile::exists(root.path() + "/mods/alt.jar")),
            "per-run replacement confirms its closure; rejection or changed selection causes no deployment");
        if (action == 3) {
            QFile external(root.path() + "/mods/dep.jar");
            check(external.open(QIODevice::ReadOnly) && external.readAll() == "external-change",
                "file created after preflight and before publish is preserved byte-for-byte");
        }
        if (!completed) task->cancel();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    Presets::ModInstallPlan plan;
    Presets::ModInstallRow row; row.key = "modrinth:missing"; row.title = "Missing"; row.status = Presets::ModInstallStatus::Missing;
    plan.rows.append(row); plan.ok = false;
    PresetModReview dialog(plan); dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    auto *tree = dialog.findChild<QTreeWidget *>("presetModReviewRows");
    auto *edit = qobject_cast<QLineEdit *>(tree->itemWidget(tree->topLevelItem(0), 5));
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    check(dialog.result() != QDialog::Accepted, "review refuses unresolved required items without an alternative");
    edit->setText("modrinth:alt");
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    check(dialog.result() == QDialog::Accepted && dialog.decision().alternatives.value("modrinth:missing").projectId == "alt",
        "review returns only the explicitly entered per-run alternative");
    return failed;
}
