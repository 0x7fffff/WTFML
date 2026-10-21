#include "Support.hpp"
int MwWorkspaceCheck(QApplication &app) {

        QTemporaryDir fixture(QDir::current().filePath(QStringLiteral(".workspace-check-XXXXXX")));
        if (!fixture.isValid()) return 2;
        app.setOrganizationName(QStringLiteral("WTFML-workspace-check"));
        app.setApplicationName(QStringLiteral("workspace-check"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fixture.path());
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
        MainWindow window;
        window.resize(1100, 700);
        window.show();
        app.processEvents();
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
        printf("Workspace check: %d/%d passed\n", g_pass, g_pass + g_fail);
        return g_fail == 0 ? 0 : 1;
    
    return 0;
}

