#include "Support.hpp"
#include "controllers/TaskDetailController.hpp"
#include "manage/ModpackInstallReport.hpp"
#include "manage/ModpackCompletion.hpp"
#include "controllers/ImportController.hpp"
#include "controllers/ResourceInstallController.hpp"
int MwWorkspaceCheck(QApplication &app) {
        runPromptProbeChecks();
        QPointer<QObject> resourceLifetime;
        {
            MainWindow lifetimeWindow;
            int resourceControllers = 0;
            for (auto *child : lifetimeWindow.children()) {
                if (dynamic_cast<ResourceInstallController *>(child)) {
                    resourceLifetime = child;
                    ++resourceControllers;
                }
            }
            check(resourceControllers == 1, "资源安装控制器由真实主窗口持有且只有一份");
        }
        check(resourceLifetime.isNull(), "关闭主窗口同步释放资源安装控制器，断开异步回信接线");


        QTemporaryDir fixture(QDir::current().filePath(QStringLiteral(".workspace-check-XXXXXX")));
        if (!fixture.isValid()) return 2;
        app.setOrganizationName(QStringLiteral("WTFML-workspace-check"));
        app.setApplicationName(QStringLiteral("workspace-check"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fixture.path());
        {
            QSettings parent(QStringLiteral("WTFML-check"), QStringLiteral("mw-module"));
            parent.setValue(QStringLiteral("Sandbox/ParentSentinel"), QStringLiteral("keep-parent"));
            parent.sync();
            QProcess child;
            child.setProgram(QCoreApplication::applicationFilePath());
            child.setArguments({QStringLiteral("sandbox-probe")});
            child.start();
            const bool started = child.waitForStarted(5000);
            const bool finished = started && child.waitForFinished(15000);
            if (child.state() != QProcess::NotRunning) {
                child.kill();
                child.waitForFinished(3000);
            }
            check(finished && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                  "独立沙盒探针子进程在期限内正常退出");
            QJsonObject report;
            for (const auto &line : child.readAllStandardOutput().split('\n')) {
                if (line.startsWith("SANDBOX_PROBE:"))
                    report = QJsonDocument::fromJson(line.mid(14)).object();
            }
            const QString childRoot = report.value(QStringLiteral("root")).toString();
            check(!childRoot.isEmpty() && childRoot != QFileInfo(AppPaths::defaultMinecraftDir()).absolutePath() &&
                      !report.value(QStringLiteral("inheritedParent")).toBool(true) &&
                      report.value(QStringLiteral("saved")).toBool(),
                  "真实子进程使用另一份默认根且读不到父进程设置");
            check(!childRoot.isEmpty() &&
                      report.value(QStringLiteral("settingsFile")).toString().startsWith(childRoot + QLatin1Char('/')) &&
                      report.value(QStringLiteral("dataRoot")).toString().startsWith(childRoot + QLatin1Char('/')) &&
                      !QFileInfo::exists(childRoot),
                  "子进程设置和账本归本进程目录所有且退出后回收");
            parent.sync();
            check(parent.value(QStringLiteral("Sandbox/ParentSentinel")).toString() == QStringLiteral("keep-parent") &&
                      !parent.contains(QStringLiteral("Sandbox/ChildSentinel")),
                  "子进程写入不污染父设置且不删除父设置");
        }
        const QString gameRoot = fixture.filePath(QStringLiteral("minecraft"));
        const QString version = QStringLiteral("workspace-test");
        QDir().mkpath(gameRoot + QStringLiteral("/versions"));
        makeInstalledVersion(QDir(gameRoot + QStringLiteral("/versions")), version);
        {
            QFile meta(gameRoot + QStringLiteral("/versions/") + version + QStringLiteral("/wtfml.meta"));
            if (!meta.open(QIODevice::WriteOnly)) return 2;
            meta.write(R"({"name":"workspace-test","instanceId":"workspace-stable-identity"})");
        }
        {
            QSettings settings;
            settings.setValue(QStringLiteral("Launcher/Folders"), QStringList{gameRoot});
            settings.setValue(QStringLiteral("Launcher/CurrentFolder"), gameRoot);
        }
        AppPaths::setMinecraftDir(gameRoot);
        seedOfflineRandomSkin(QStringLiteral("WorkspaceCheck"), kSeededSkinUrl, QColor("#4080c0"));
        QString accountError;
        AccountStore::addOffline(QStringLiteral("WorkspaceCheck"), &accountError);
        // 这一条也会构造 MainWindow ⇒ 一构造就发版本清单。不装夹具的话这一跑实际打的是
        // 真 Mojang：断网机器上卡满 10 秒超时、连得上时把当天官方清单塞进树里，
        // 而断言照样全绿 —— 于是它被当成"全量非 visual"的一部分，却从来不是离线的。
        // 夹具起不来就停：接着跑等于把"离线"偷偷换成真网络。
        FakeNetwork fakeNet;
        if (!fakeNet.start()) {
            check(false, "workspace 的假外网起来了（起不来就不往下跑：会打到真地址上）");
            return 2;
        }
        installFakeNetworkSettings(fakeNet);
        MainWindow window;
        window.resize(1100, 700);
        window.show();
        app.processEvents();
        pumpUntil([&fakeNet]() {
            return fakeNet.count(QStringLiteral("/version_manifest_v2.json")) >= 1;
        }, 5000);
        check(fakeNet.count(QStringLiteral("/version_manifest_v2.json")) == 1,
              "workspace 冷启动那一发版本清单打到了夹具上（正好一发）",
              QStringLiteral("次数=%1 最先几条=%2")
                  .arg(fakeNet.count(QStringLiteral("/version_manifest_v2.json")))
                  .arg(fakeNet.paths.mid(0, 4).join(QStringLiteral(", "))));
        auto *tabs = window.findChild<QTabWidget *>(MainWindowUI::tabWidgetName());
        auto *host = window.findChild<OverlayHost *>();
        check(tabs && host, "workspace navigation widgets exist");
        if (!tabs || !host) return 2;
        auto *groups = window.findChild<InstanceGroupStore *>();
        auto *versions = window.findChild<VersionManager *>();
        check(groups && versions && versions->instanceId(version) == QStringLiteral("workspace-stable-identity"),
              "favorite regression uses stable identity distinct from directory name");
        if (groups && versions) {
            const QString key = versions->instanceId(version);
            const auto homeHasFavorite = [&]() {
                auto *section = window.findChild<QWidget *>(QStringLiteral("homeFavoriteSection"));
                return section && section->findChild<QPushButton *>(QStringLiteral("homeQuickCard_") + version);
            };
            QString error;
            check(groups->pinInstance(key, &error), "management favorite write succeeds", error);
            app.processEvents();
            check(homeHasFavorite(), "management favorite immediately appears on home without restarting");
            check(groups->unpinInstance(key, &error), "management favorite removal succeeds", error);
            app.processEvents();
            check(!homeHasFavorite(), "removing favorite immediately updates home");
        }
        for (const int source : {MainWindowUI::homeTabIndex(), MainWindowUI::instancesTabIndex()}) {
            tabs->setCurrentIndex(source);
            if (source == MainWindowUI::homeTabIndex()) {
                if (auto *picker = window.findChild<QPushButton *>(MainWindowUI::homeInstanceSelectButtonName()))
                    picker->click();
                if (auto *search = window.findChild<QLineEdit *>(QStringLiteral("homePickerSearch")))
                    search->setText(QStringLiteral("remember-filter"));
            }
            bool sawSettings = false;
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(&timeout, &QTimer::timeout, &window, [&]() {
                check(false, "instance settings return completed within five seconds");
                for (auto *dialog : window.findChildren<InstanceSettingsDialog *>()) dialog->reject();
            });
            timeout.start(5000);
            QTimer::singleShot(0, &window, [&]() {
                sawSettings = host->pageOpen();
                check(tabs->currentIndex() == source, "instance settings keeps source tab");
                if (auto *back = host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) back->click();
            });
            QMetaObject::invokeMethod(&window, "openInstanceSettings", Qt::DirectConnection, Q_ARG(QString, version));
            timeout.stop();
            check(sawSettings && !host->pageOpen() && tabs->currentIndex() == source,
                  "back returns to source root page");
            if (source == MainWindowUI::homeTabIndex()) {
                auto *picker = window.findChild<QWidget *>(MainWindowUI::homeInstancePickerName());
                auto *search = window.findChild<QLineEdit *>(QStringLiteral("homePickerSearch"));
                check(picker && picker->isVisible() && search && search->text() == QStringLiteral("remember-filter"),
                      "home instance list and search survive opening settings");
            }
        }
        {
            auto *activity = window.findChild<ActivityCenterController *>();
            auto *details = window.findChild<TaskDetailController *>();
            const QString repairId = QStringLiteral("repair-report-probe");
            writeFileAt(InstanceFiles::importMarkerPath(repairId), "{}");
            writeFileAt(AppPaths::versionDir(repairId) + QStringLiteral("/wtfml.import-report.json"),
                R"({"schemaVersion":1,"packName":"Repair probe","complete":false,"issues":[]})");
            check(activity && details, "repair report uses real window controllers");
            if (activity && details) {
                const auto task = activity->begin(Tasks::TaskKind::Import, QStringLiteral("Repair probe"), repairId);
                activity->finish(task, false, QStringLiteral("missing files"), false);
                host->openPage(QStringLiteral("活动中心"));
                bool sawReport = false;
                QTimer timeout;
                timeout.setSingleShot(true);
                QObject::connect(&timeout, &QTimer::timeout, &window, [&] {
                    check(false, "repair report action completed within five seconds");
                    for (auto *box : window.findChildren<InlineMessageBox *>()) box->reject();
                });
                timeout.start(5000);
                QTimer::singleShot(0, &timeout, [&] {
                    for (auto *box : window.findChildren<InlineMessageBox *>()) {
                        auto *title = box->findChild<QLabel *>(InlineMessageBox::titleLabelName());
                        if (title && title->text().contains(QStringLiteral("Repair probe"))) {
                            sawReport = true; box->reject();
                        }
                    }
                });
                details->action(QString::number(task), TaskDetailPage::Action::RepairImport);
                timeout.stop();
                check(sawReport && host->pageOpen(), "task repair opens report over activity page and preserves page on close");
                const QString repairRoot = AppPaths::versionDir(repairId);
                const QByteArray probe("recheck-probe");
                writeFileAt(repairRoot + QStringLiteral("/mods/probe.jar"), probe);
                ModpackImport::Pack pack; pack.name = QStringLiteral("Repair probe");
                const ModpackImport::FileIssue issue{{123,456}, QStringLiteral("probe.jar"),
                    QStringLiteral("mods/probe.jar"), QString::fromLatin1(QCryptographicHash::hash(
                    probe, QCryptographicHash::Sha1).toHex()), probe.size(), QStringLiteral("missing")};
                QString reportError;
                check(ModpackInstallReport::write(repairRoot, pack, {issue}, false, &reportError),
                    "prepare report entry for real background recheck", reportError);
                bool requestedRecheck = false, sawRechecked = false;
                QTimer pulse;
                QObject::connect(&pulse, &QTimer::timeout, &pulse, [&] {
                    for (auto *box : window.findChildren<InlineMessageBox *>()) {
                        if (!box->isVisible()) continue;
                        auto *title = box->findChild<QLabel *>(InlineMessageBox::titleLabelName());
                        if (!title || !title->text().contains(QStringLiteral("Repair probe"))) continue;
                        if (!requestedRecheck) {
                            requestedRecheck = true;
                            if (auto *button = box->findChild<QPushButton *>(InlineMessageBox::buttonName(3))) button->click();
                        } else {
                            auto *note = box->findChild<QLabel *>(InlineMessageBox::noteLabelName());
                            sawRechecked = note && note->text().contains(QStringLiteral("本次校验通过 1 项"));
                            pulse.stop(); box->reject();
                        }
                        break;
                    }
                });
                timeout.start(5000); pulse.start(10);
                details->action(QString::number(task), TaskDetailPage::Action::RepairImport);
                pulse.stop(); timeout.stop();
                check(requestedRecheck && sawRechecked && host->pageOpen(),
                    "report recheck runs in background without choosing archive or source file");
                QFile persisted(repairRoot + QStringLiteral("/wtfml.import-report.json"));
                check(persisted.open(QIODevice::ReadOnly) &&
                    !QJsonDocument::fromJson(persisted.readAll()).object().value(QStringLiteral("complete")).toBool()
                    && QFileInfo::exists(InstanceFiles::importMarkerPath(repairId))
                    && !QFileInfo::exists(AppPaths::versionMetaPath(repairId)),
                    "read-only recheck never publishes installation success or clears marker");
                persisted.close();
                makeInstalledVersion(QDir(gameRoot + QStringLiteral("/versions")), repairId);
                const QString fingerprint(64, QLatin1Char('b'));
                writeFileAt(InstanceFiles::importMarkerPath(repairId), QJsonDocument(QJsonObject{
                    {QStringLiteral("manifestSha256"), fingerprint}, {QStringLiteral("attempt"), QStringLiteral("ui-finish")}}).toJson());
                pack.fileIssues = {issue}; pack.gameVersion = QStringLiteral("1.20.1");
                check(ModpackCompletion::capture(repairRoot, gameRoot, pack, fingerprint, {}, &reportError, {}),
                      "prepare stage evidence for real report completion action", reportError);
                bool requestedFinish = false, sawCompleted = false;
                QObject::disconnect(&pulse, nullptr, nullptr, nullptr);
                QObject::connect(&pulse, &QTimer::timeout, &pulse, [&] {
                    for (auto *box : window.findChildren<InlineMessageBox *>()) {
                        if (!box->isVisible()) continue;
                        auto *title = box->findChild<QLabel *>(InlineMessageBox::titleLabelName());
                        if (!title) continue;
                        if (title->text() == QStringLiteral("安装完成")) {
                            sawCompleted = true; pulse.stop(); box->reject(); break;
                        }
                        if (!requestedFinish && title->text().contains(QStringLiteral("Repair probe"))) {
                            requestedFinish = true;
                            if (auto *button = box->findChild<QPushButton *>(InlineMessageBox::buttonName(4))) button->click();
                            break;
                        }
                    }
                });
                timeout.start(5000); pulse.start(10);
                details->action(QString::number(task), TaskDetailPage::Action::RepairImport);
                pulse.stop(); timeout.stop();
                check(requestedFinish && sawCompleted && !QFileInfo::exists(InstanceFiles::importMarkerPath(repairId)) &&
                      QFileInfo::exists(AppPaths::versionMetaPath(repairId)),
                      "real report completion button verifies evidence and publishes without original archive");
                host->closeOverlay();
            }
        }
        TaskCenterPanel panel(nullptr, {}, {}, {}, {}, {}, nullptr, true);
        panel.resize(1000, 500);
        panel.showEmbedded();
        app.processEvents();
        auto *toggle = panel.findChild<QPushButton *>(QStringLiteral("taskCenterRunningCollapse"));
        auto *pane = panel.findChild<QWidget *>(QStringLiteral("taskCenterRunningPane"));
        check(toggle && pane && !toggle->isChecked() && pane->width() == 36,
              "running pane starts collapsed when nothing is running");
        if (toggle && pane) {
            for (int i = 0; i < 4; ++i) {
                toggle->click();
                app.processEvents();
                // 开局是收着的 ⇒ 第一下是**展开**（i == 0），第三下又展开。
                const bool expanded = i % 2 == 0;
                check(toggle->isChecked() == expanded && pane->width() == (expanded ? 220 : 36),
                      "toggle selection follows running pane width");
                check(pane->rect().contains(QRect(toggle->mapTo(pane, QPoint()), toggle->size())),
                      "running pane toggle stays inside collapsed header");
            }
        }
        check(fakeNet.notFound == 0, "workspace 的假外网没有收到没路由的请求",
              QStringLiteral("404 了 %1 发：%2")
                  .arg(fakeNet.notFound)
                  .arg(fakeNet.unrouted.mid(0, 8).join(QStringLiteral(", "))));
        // 从真实执行入口建任务，不能手动 begin(..., target) 掩盖缺失的目标 ID 接线。
        if (auto *imports = window.findChild<ImportController *>()) {
            const QByteArray manifest = R"({"formatVersion":1,"game":"minecraft","name":"Target wiring","versionId":"1","dependencies":{"minecraft":"1.20.1"},"files":[{"path":"mods/probe.jar","hashes":{"sha1":"0000000000000000000000000000000000000000"},"downloads":["https://cdn.modrinth.com/data/test/versions/test/probe.jar"],"fileSize":1}]})";
            const QString target = QStringLiteral("real-import-target");
            const bool started = imports->applyModpackImport(fixture.filePath("target.zip"), target,
                manifest, ModpackImport::Format::Modrinth);
            auto *activity = window.findChild<ActivityCenterController *>();
            bool bound = false;
            if (activity) for (const auto &task : activity->tasks()->all())
                if (task.kind == Tasks::TaskKind::Import && task.targetId == target) bound = true;
            check(started && bound, "real import entry binds task target instance for repair action");
        } else check(false, "real import controller exists for target wiring test");
        printf("Workspace check: %d/%d passed\n", g_pass, g_pass + g_fail);
        return g_fail == 0 ? 0 : 1;
    
    return 0;
}

