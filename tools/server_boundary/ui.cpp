#include "MainWindowUI.hpp"
#include "MainWindow.hpp"
#include "NewServerPage.hpp"
#include "OverlayHost.hpp"
#include "ServerConsolePage.hpp"
#include "Theme.hpp"
#include "controllers/ServerConsoleController.hpp"
#include "controllers/ServerWorkspaceController.hpp"
#include "download/ServerInstall.hpp"
#include "util/AppPaths.hpp"
#include "util/SecretRedactor.hpp"
#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QJsonObject>
#include <QEventLoop>
#include <QSettings>
#include "server/ServerRegistry.hpp"
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>

QJsonObject serverWizardInstall(const QString &root) {
    const auto configRoot = QDir(root + "/settings").absolutePath() + '/';
    // main() sets a dedicated organization and redirects IniFormat/UserScope to
    // root/settings. Verify that exact default resolution before any write.
    const auto resolved = QDir::cleanPath(QSettings().fileName());
    if (QSettings::defaultFormat() != QSettings::IniFormat ||
        !resolved.startsWith(configRoot, Qt::CaseInsensitive) ||
        qApp->organizationName() != "WTFML-server-acceptance")
        return {{"ok", false}, {"problem", "settings are not isolated; no write attempted"}};
    QDir().mkpath(QFileInfo(resolved).absolutePath());
    QSettings isolated(resolved, QSettings::IniFormat);
    isolated.setValue("Launcher/Folders", QStringList{root + "/game"});
    isolated.setValue("Launcher/CurrentFolder", root + "/game"); isolated.sync();
    if (isolated.status() != QSettings::NoError) return {{"ok", false}, {"problem", "isolated settings write failed"}};
    QDir().mkpath(root + "/game");
    qApp->setProperty("wtfml.testDefaultMinecraftDir", root + "/game");
    AppPaths::setMinecraftDir(root + "/game");
    MainWindow window; window.resize(1100, 760); window.show();
    bool accepted = false, screenshot = false, installed = false;
    auto *create = window.findChild<QPushButton *>(MainWindowUI::newServerButtonName());
    if (!create) return {{"ok", false}, {"problem", "missing production creation entry"}};
    QTimer::singleShot(100, &window, [&] {
        auto *page = window.findChild<NewServerPage *>();
        if (!page) return;
        page->findChild<QLineEdit *>(MainWindowUI::newServerNameEditName())->setText("Acceptance");
        page->findChild<QLineEdit *>(MainWindowUI::newServerGameVersionEditName())->setText("1.20.1");
        screenshot = page->grab().save(root + "/main-window-wizard.png");
        auto *button = page->findChild<QPushButton *>(MainWindowUI::newServerCreateButtonName());
        accepted = button && button->isEnabled();
        if (accepted) button->click();
    });
    QTimer pageDeadline; pageDeadline.setSingleShot(true);
    QObject::connect(&pageDeadline, &QTimer::timeout, &window, [&] {
        for (auto *overlay : window.findChildren<OverlayHost *>()) overlay->evict();
    }); pageDeadline.start(15000);
    create->click(); pageDeadline.stop();
    QEventLoop loop; QTimer poll, deadline;
    poll.setInterval(100); deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (ServerRegistry::describe("Acceptance").installed) { installed = true; loop.quit(); }
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (accepted) { poll.start(); deadline.start(300000); loop.exec(); }
    window.hide();
    return {{"ok", accepted && installed && screenshot}, {"accepted", accepted}, {"installed", installed},
        {"screenshot", screenshot}, {"settingsFile", resolved},
        {"scope", "Actual MainWindow create entry -> wizard -> production vanilla installation, isolated settings"}};
}

QJsonObject serverUiEvidence(const QString &root, const ServerInstall::Request &request) {
    QMainWindow window;
    MainWindowUI ui(&window); window.resize(1100, 760); window.show();
    NewServerPage wizard(&window);
    wizard.setFlavors({{ServerInstall::flavorToken(request.flavor), ServerInstall::flavorName(request.flavor), {},
        request.flavor != ServerInstall::Flavor::Vanilla, request.flavor == ServerInstall::Flavor::Fabric, false, true}},
        ServerInstall::flavorToken(request.flavor));
    wizard.setDefaults("Acceptance", request.gameVersion);
    wizard.setTargetDir(AppPaths::serverDir("Acceptance"));
    wizard.findChild<QLineEdit *>(MainWindowUI::newServerLoaderEditName())->setText(request.loaderVersion);
    wizard.findChild<QLineEdit *>(MainWindowUI::newServerInstallerEditName())->setText(request.installerVersion);
    wizard.setSummary("真实服务端验收参数", {});
    bool screenshot = false;
    QTimer::singleShot(100, &wizard, [&] {
        screenshot = wizard.grab().save(root + "/wizard.png");
        wizard.findChild<QPushButton *>(MainWindowUI::newServerCreateButtonName())->click();
    });
    const bool accepted = wizard.exec() == QDialog::Accepted && wizard.gameVersion() == request.gameVersion &&
        wizard.flavorToken() == ServerInstall::flavorToken(request.flavor) && wizard.loaderVersion() == request.loaderVersion;
    ServerWorkspaceController workspace;
    ServerConsoleController console(&workspace, &ui, &window, {});
    QFile actual(root + "/normal-stop.log");
    if (!actual.open(QIODevice::ReadOnly) || actual.size() > 4 * 1024 * 1024) return {{"ok", false}, {"problem", "missing bounded actual log"}};
    const auto text = QString::fromUtf8(actual.readAll());
    const auto secret = QStringLiteral("server-ui-export-test-secret-20261027");
    SecretRedactor::remember(secret);
    const auto destination = root + "/ui-export-" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".log";
    bool consoleShot = false, dialogAccepted = false;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &window, [&] {
        if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) dialog->reject();
        ui.overlay()->evict();
    }); guard.start(15000);
    QTimer::singleShot(100, &window, [&] {
        console.appendLine("Acceptance", text + "\naccess_token=" + secret);
        auto *page = window.findChild<ServerConsolePage *>();
        if (!page) { ui.overlay()->evict(); return; }
        consoleShot = page->grab().save(root + "/console.png");
        QTimer::singleShot(100, &window, [&] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                dialog->setDirectory(root);
                if (auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit")))
                    name->setText(QFileInfo(destination).fileName());
                else dialog->selectFile(QFileInfo(destination).fileName());
                QTimer::singleShot(100, dialog, [&, dialog] {
                    dialogAccepted = dialog->selectedFiles().contains(destination) &&
                        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                });
            }
        });
        page->findChild<QPushButton *>(ServerConsolePage::exportLogButtonName())->click();
        ui.overlay()->evict();
    });
    console.open("Acceptance"); guard.stop();
    QFile exported(destination);
    const bool readable = exported.open(QIODevice::ReadOnly);
    const auto bytes = readable ? exported.readAll() : QByteArray();
    const bool ok = accepted && screenshot && consoleShot && dialogAccepted && readable &&
        bytes.contains("Stopping") && !bytes.contains(secret.toUtf8()) && bytes.contains("<redacted>");
    return {{"ok", ok}, {"wizardAccepted", accepted}, {"wizardScreenshot", screenshot},
        {"consoleScreenshot", consoleShot}, {"dialogAccepted", dialogAccepted}, {"exported", destination},
        {"scope", "Production wizard widgets and console export with actual server log; Qt file dialog, not native OS dialog"}};
}
