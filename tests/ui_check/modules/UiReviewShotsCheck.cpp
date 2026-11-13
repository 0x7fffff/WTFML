// 走查验收截图：offscreen 下 grab() 出 主页零实例 / 管理页空状态 / 设置·启动 / 下载页 在 深色 + 青瓷 + 海蓝 下的样子。
// 用法：QT_QPA_PLATFORM=offscreen ui_check_review_shots  [输出目录，默认 build/review-shots]
#include "Support.hpp"

#include <QDir>
#include <QFontDatabase>
#include <QMainWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>

int UiReviewShotsCheck(QApplication &app) {
    Anim::setEnabled(false);
    QTemporaryDir settingsDir(QDir::current().filePath(QStringLiteral(".review-shots-XXXXXX")));
    if (!settingsDir.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    app.setOrganizationName(QStringLiteral("WTFMLReviewShots"));
    app.setApplicationName(QStringLiteral("ReviewShots"));
    app.setStyle(Theme::createStyle());
    const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
    const int fontId = QFile::exists(fontPath) ? QFontDatabase::addApplicationFont(fontPath) : -1;
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    app.setFont(QFont(families.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : families.first(), 9));

    const QString out = app.arguments().size() > 1 ? app.arguments().at(1)
                                                   : QDir::current().filePath(QStringLiteral("build/review-shots"));
    QDir().mkpath(out);
    int saved = 0;
    for (const QString &id : {QStringLiteral("dark"), QStringLiteral("emerald"), QStringLiteral("ocean")}) {
        Theme::setSpec(Theme::presetSpec(id));
        Theme::apply(app);
        QMainWindow window;
        MainWindowUI ui(&window);
        window.resize(1127 * 4 / 5 + 100, 776);
        window.show();

        const auto fill = [](QComboBox *combo, const QStringList &items, int current = 0) {
            if (combo == nullptr) return;
            combo->clear();
            combo->addItems(items);
            combo->setCurrentIndex(current);
        };
        fill(ui.getDownloadSourceCombo(), {QStringLiteral("优先官方源"), QStringLiteral("优先国内源")});
        fill(ui.getMemoryModeCombo(), {QStringLiteral("自动分配"), QStringLiteral("手动分配")}, 1);
        fill(ui.getGameWindowModeCombo(), {QStringLiteral("窗口"), QStringLiteral("最大化"), QStringLiteral("全屏")});
        if (ui.getJavaPathLabel() != nullptr) ui.getJavaPathLabel()->setText(QStringLiteral("自动选择　按游戏版本"));
        if (ui.getMemoryHint() != nullptr)
            ui.getMemoryHint()->setText(QStringLiteral(
                "系统总内存 31.5 GB、当前可用 14.5 GB；现在最多给到 4096 MB。左边拖着改，也可以直接填数字。"
                "最小内存由启动器定（1024 MB），不用管它。"));
        if (ui.getMemoryMaxSpin() != nullptr) ui.getMemoryMaxSpin()->setValue(4096);
        if (ui.getMemoryMaxSlider() != nullptr) ui.getMemoryMaxSlider()->setValueMb(4096);

        MainWindowUI::HomeQuickItem none;
        none.key = QStringLiteral("none");
        none.name = QStringLiteral("还没有实例");
        none.empty = true;
        ui.setHomeQuickRows({none}, {}, {});

        const auto shot = [&](const QString &name) {
            app.processEvents();
            app.processEvents();
            if (window.grab().save(QDir(out).filePath(QStringLiteral("%1-%2.png").arg(name, id)))) ++saved;
        };
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        shot(QStringLiteral("home-empty"));
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
        if (auto *list = window.findChild<QTreeWidget *>(MainWindowUI::instanceListName())) list->hide();
        if (auto *empty = window.findChild<QWidget *>(MainWindowUI::instanceEmptyStateName())) empty->show();
        shot(QStringLiteral("manage-empty"));
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::settingsTabIndex());
        ui.getSettingsTabs()->setCurrentIndex(0);
        shot(QStringLiteral("settings-launch"));
        ui.getSettingsTabs()->setCurrentIndex(MainWindowUI::settingsCategoryKeys().indexOf(QStringLiteral("download")));
        shot(QStringLiteral("settings-download"));
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
        shot(QStringLiteral("download-source"));
    }
    std::printf("saved %d shots to %s\n", saved, qPrintable(out));
    return saved > 0 ? 0 : 1;
}
