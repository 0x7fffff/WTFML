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
#include "controllers/ActivityCenterController.hpp"
#include "controllers/DownloadBatchController.hpp"
#include <memory>
#include <QElapsedTimer>
#include "controllers/PresetController.hpp"
#include "presets/PresetStore.hpp"
#include "manage/VersionManager.hpp"
#include "PresetInstallPicker.hpp"
#include "InlineMessage.hpp"
#include "TaskCenterPanel.hpp"
#include <QComboBox>
#include <QListWidget>
#include <QTreeWidget>
#include <QTabWidget>
#include <QUuid>
#include <QMouseEvent>

namespace {
bool prepareClientInstall(MainWindow &window, const QString &root, const QString &presetId) {
    window.ui()->getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
    if (!QMetaObject::invokeMethod(&window, "openInstallPage", Qt::DirectConnection, Q_ARG(QString, QString("1.20.1")))) return false;
    auto *tree = window.ui()->getLoaderTree();
    QTreeWidgetItem *pinned = nullptr;
    QElapsedTimer clock; clock.start();
    while (!pinned && clock.elapsed() < 90000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto *row = tree->topLevelItem(i);
            if (row->data(0, Qt::UserRole).toInt() != VersionManager::loaderIndexForKind(VersionManager::LoaderKind::Forge)) continue;
            for (int j = 0; j < row->childCount(); ++j)
                if (row->child(j)->data(0, Qt::UserRole).toString() == "47.2.0") pinned = row->child(j);
        }
    }
    if (!pinned || !QMetaObject::invokeMethod(tree, "itemClicked", Qt::DirectConnection, Q_ARG(QTreeWidgetItem *, pinned), Q_ARG(int, 0))) return false;
    window.ui()->getInstallNameEdit()->setText("ClientRecovery");
    bool selected = false;
    QTimer promptGuard; promptGuard.setSingleShot(true);
    QObject::connect(&promptGuard, &QTimer::timeout, &window, [&] { window.ui()->overlay()->dismiss(); });
    promptGuard.start(10000);
    QTimer::singleShot(100, &window, [&] {
        for (auto *picker : window.findChildren<PresetInstallPicker *>()) {
            auto *list = picker->findChild<QListWidget *>(PresetInstallPicker::listName(0));
            if (!list) continue;
            int index = -1;
            for (int n = 0; n < list->count(); ++n) if (list->item(n)->data(Qt::UserRole).toString() == presetId) { index = n; break; }
            if (index < 0) continue;
            list->setCurrentRow(index); selected = picker->selectedCount() == 1;
            if (auto *box = qobject_cast<InlineMessageBox *>(picker->parentWidget())) {
                if (auto *button = box->findChild<QPushButton *>(InlineMessageBox::buttonName(0))) button->click();
            } else for (auto *box : window.findChildren<InlineMessageBox *>())
                if (auto *button = box->findChild<QPushButton *>(InlineMessageBox::buttonName(0))) button->click();
        }
    });
    QTreeWidgetItem *presetRow = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->data(0, Qt::UserRole).toInt() == MainWindowUI::installPresetRowRole()) presetRow = tree->topLevelItem(i);
    if (!presetRow || !QMetaObject::invokeMethod(tree, "itemClicked", Qt::DirectConnection, Q_ARG(QTreeWidgetItem *, presetRow), Q_ARG(int, 0))) return false;
    promptGuard.stop(); if (!selected) return false;
    window.grab().save(root + "/client-install-selection.png");
    QTimer confirmation;
    QObject::connect(&confirmation, &QTimer::timeout, &window, [&] {
        for (auto *box : window.findChildren<InlineMessageBox *>()) {
            auto *title = box->findChild<QLabel *>(InlineMessageBox::titleLabelName());
            if (box->isVisible() && title && title->text() == QStringLiteral("实例已存在")) {
                if (auto *button = box->findChild<QPushButton *>(InlineMessageBox::buttonName(0))) button->click();
                return;
            }
        }
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->isVisible() && (button->text() == QStringLiteral("安装并应用") || button->text() == QStringLiteral("校验并补全"))) { button->click(); return; }
    }); confirmation.start(50); promptGuard.start(10000);
    auto *install = window.ui()->getInstallButton();
    const bool enabled = install && install->isEnabled();
    if (enabled) install->click();
    confirmation.stop(); promptGuard.stop();
    return enabled;
}
}

QJsonObject serverWizardInstall(const QString &root, bool checkpoint, bool client, bool resumeOnly, const QString &java) {
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
    if (client) { isolated.setValue("Java/Path", java); isolated.sync(); }
    if (isolated.status() != QSettings::NoError) return {{"ok", false}, {"problem", "isolated settings write failed"}};
    QDir().mkpath(root + "/game");
    qApp->setProperty("wtfml.testDefaultMinecraftDir", root + "/game");
    AppPaths::setMinecraftDir(root + "/game");
    QString presetId;
    if (client && !resumeOnly) {
        Presets::PresetManifest preset; preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        preset.name = "Recovery FOV"; preset.category = Presets::PresetCategory::GameSettings;
        Presets::PresetSection options; options.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        options.kind = Presets::SectionKind::VanillaOptions;
        options.items = QJsonArray{QJsonObject{{"key", "fov"}, {"value", "0.5"}}};
        preset.sections.append(options);
        Presets::PresetIndex index; index.order = {preset.id};
        if (!Presets::saveManifest(PresetController::presetsRoot(), preset) || !Presets::saveIndex(PresetController::presetsRoot(), index))
            return {{"ok", false}, {"problem", "isolated preset could not be saved"}};
        presetId = preset.id;
    }
    auto owner = std::make_unique<MainWindow>();
    auto &window = *owner; window.resize(1100, 760); window.show();
    int previousIntents = 0;
    if (auto *activity = window.findChild<ActivityCenterController *>())
        for (const auto &task : activity->tasks()->all())
            if (task.targetId == (client ? "ClientRecovery" : "Acceptance") && task.kind == Tasks::TaskKind::Install) ++previousIntents;
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
    if (resumeOnly) { accepted = true; screenshot = true; }
    else if (client) { accepted = prepareClientInstall(window, root, presetId); screenshot = accepted; }
    else create->click();
    pageDeadline.stop();
    QEventLoop loop; QTimer poll, deadline;
    bool paused = false;
    Tasks::TaskId taskId = Tasks::invalidTaskId();
    const auto batchesFor = [](MainWindow &host) -> DownloadBatchController * {
        for (auto *child : host.children()) if (auto *batches = dynamic_cast<DownloadBatchController *>(child)) return batches;
        return nullptr;
    };
    if (resumeOnly) {
        auto *activity = window.findChild<ActivityCenterController *>();
        auto *batches = batchesFor(window);
        if (activity && batches) for (const auto &task : activity->tasks()->all()) {
            if (task.targetId == "ClientRecovery" && batches->owns(task.id)) { taskId = task.id; paused = true; }
        }
    }
    const auto taskScreenshot = [&](MainWindow &host, const QString &name) {
        auto *panel = host.findChild<QWidget *>(MainWindowUI::taskCenterPanelName());
        if (!panel || !panel->isVisible())
            if (auto *button = host.findChild<QPushButton *>(MainWindowUI::taskSummaryButtonName())) button->click();
        QElapsedTimer animation; animation.start();
        while (animation.elapsed() < 400) QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (auto *row = host.findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(0))) {
            const QPointF local(row->rect().center()), global(row->mapToGlobal(row->rect().center()));
            QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(row, &press); QApplication::sendEvent(row, &release);
        }
        QApplication::processEvents();
        panel = host.findChild<QWidget *>(MainWindowUI::taskCenterPanelName());
        if (!panel || !panel->isVisible()) return false;
        return host.grab().save(root + '/' + name);
    };
    poll.setInterval(checkpoint ? 1 : 100); deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (checkpoint) {
            auto *activity = window.findChild<ActivityCenterController *>();
            auto *batches = batchesFor(window);
            if (activity && batches) for (const auto &task : activity->tasks()->all()) {
                if (task.targetId == (client ? "ClientRecovery" : "Acceptance") && batches->owns(task.id) && task.capabilities.canPause && batches->pause(task.id)) {
                    taskId = task.id; paused = true; loop.quit(); return;
                }
            }
            return;
        }
        if (ServerRegistry::describe("Acceptance").installed) { installed = true; loop.quit(); }
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (accepted && !resumeOnly) { poll.start(); deadline.start(client ? 120000 : 300000); loop.exec(); }
    poll.stop(); deadline.stop();
    if (checkpoint) {
        auto *activity = window.findChild<ActivityCenterController *>();
        const bool pauseShot = paused && taskScreenshot(window, "paused-task.png");
        const bool saved = activity && activity->tasks()->saveHistory();
        if (!paused || !saved) return {{"ok", false}, {"accepted", accepted}, {"paused", paused}, {"problem", "could not checkpoint actual installation download"}};
        window.hide(); owner.reset();
        MainWindow reopened; reopened.resize(1100, 760); reopened.show();
        QTimer completion;
        QObject::connect(&completion, &QTimer::timeout, &reopened, [&] {
            for (auto *box : reopened.findChildren<InlineMessageBox *>()) {
                const auto *title = box->findChild<QLabel *>(InlineMessageBox::titleLabelName());
                if (!box->isVisible() || !title || title->text() != QStringLiteral("安装完成")) continue;
                reopened.grab().save(root + "/installation-complete-prompt.png");
                if (auto *button = box->findChild<QPushButton *>(InlineMessageBox::buttonName(0))) button->click();
            }
        }); completion.start(100);
        auto *restored = reopened.findChild<ActivityCenterController *>();
        auto *batches = batchesFor(reopened);
        if (!restored || !batches || !batches->owns(taskId))
            return {{"ok", false}, {"problem", "main window did not restore original task"}};
        const bool alreadyReady = restored->tasks()->snapshot(taskId).detail.contains(QStringLiteral("文件已补齐"));
        if (!batches->resume(taskId)) return {{"ok", false}, {"problem", "original task refused explicit resume"}, {"detail", restored->tasks()->snapshot(taskId).detail}};
        QElapsedTimer timer; timer.start();
        // A receipt may already be at the explicit-continuation gate after an OS
        // restart. One click then starts the installer rather than file replay.
        if (alreadyReady && client) {
            // The first resume above was the user's explicit continuation.
        } else while (restored->tasks()->snapshot(taskId).state != Tasks::TaskState::Paused && timer.elapsed() < (client ? 600000 : 300000))
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        const bool ready = restored->tasks()->snapshot(taskId).state == Tasks::TaskState::Paused;
        const bool readyShot = (ready || alreadyReady) && taskScreenshot(reopened, "files-ready-task.png");
        const bool explicitContinue = alreadyReady || (ready && batches->resume(taskId));
        if (!explicitContinue) return {{"ok", false}, {"problem", "files did not reach the continuation gate"}, {"detail", restored->tasks()->snapshot(taskId).detail}};
        timer.restart();
        while (!Tasks::isTerminalState(restored->tasks()->snapshot(taskId).state) && timer.elapsed() < (client ? 480000 : 30000))
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        bool presetApplied = !client;
        if (client) {
            QFile options(AppPaths::versionDir("ClientRecovery") + "/options.txt");
            presetApplied = options.open(QIODevice::ReadOnly) && options.readAll().contains("fov:0.5");
        }
        auto *versions = reopened.findChild<VersionManager *>();
        const bool listed = !client || (versions && versions->installedVersions().contains("ClientRecovery"));
        installed = restored->tasks()->snapshot(taskId).state == Tasks::TaskState::Succeeded && presetApplied && listed &&
            (client ? QFileInfo::exists(AppPaths::versionJsonPath("ClientRecovery")) : ServerRegistry::describe("Acceptance").installed);
        int intents = 0;
        for (const auto &task : restored->tasks()->all()) if (task.targetId == (client ? "ClientRecovery" : "Acceptance") && task.kind == Tasks::TaskKind::Install) ++intents;
        const bool oneIntent = intents == previousIntents + (resumeOnly ? 0 : 1);
        const bool finalShot = taskScreenshot(reopened, "completed-task.png");
        return {{"ok", accepted && paused && (ready || alreadyReady) && explicitContinue && installed && oneIntent && pauseShot && readyShot && finalShot},
            {"paused", paused}, {"filesReadyWaited", ready || alreadyReady}, {"explicitContinue", explicitContinue}, {"installed", installed},
            {"singleIntent", oneIntent}, {"screenshots", pauseShot && readyShot && finalShot},
            {"presetApplied", presetApplied},
            {"listed", listed},
            {"resumedExistingProcessReceipt", resumeOnly},
            {"task", QString::number(taskId)}, {"settingsFile", resolved},
            {"scope", client ? "Actual MainWindow Forge 47.2.0/1.20.1 + FOV preset, public download, pause, reopen, files-only restore, explicit installer continuation; no game launch"
                : "Actual MainWindow server wizard, public download, pause, destroy/reopen window, files-only restore, explicit installation; no server launch"}};
    }
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
