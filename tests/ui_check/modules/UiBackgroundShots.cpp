// 自定义背景图的离屏样张：深色 / 青瓷 × 1280x800 × 三张 ——
// 设置 →「个性化」里「背景图」那一组、设了图之后的主页、设了图之后的设置页。
// 只出图、不做断言，不进 ctest（和 ui_check_settings_shots 同一个路数）。
// 用法：QT_QPA_PLATFORM=offscreen ui_check_background_shots <输出目录>
#include "Support.hpp"

#include "Glass.hpp"
#include "MainWindowUI.hpp"
#include "Theme.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QLinearGradient>
#include <QMainWindow>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>

int UiBackgroundShots(QApplication &app) {
    Anim::setEnabled(false);
    // 设置与素材都放 %TEMP%，进程退出时 QTemporaryDir 自己删干净。
    QTemporaryDir settingsDir(QDir::temp().filePath(QStringLiteral("wtfml-r82-settings-XXXXXX")));
    QTemporaryDir artDir(QDir::temp().filePath(QStringLiteral("wtfml-r82-art-XXXXXX")));
    if (!settingsDir.isValid() || !artDir.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    app.setOrganizationName(QStringLiteral("WTFMLBackgroundShots"));
    app.setApplicationName(QStringLiteral("BackgroundShots"));
    app.setStyle(Theme::createStyle());
    const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
    const int fontId = QFile::exists(fontPath) ? QFontDatabase::addApplicationFont(fontPath) : -1;
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    app.setFont(QFont(families.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : families.first(), 9));

    const QString out = app.arguments().size() > 1
                            ? app.arguments().at(1)
                            : QDir::current().filePath(QStringLiteral("build/background-shots"));
    QDir().mkpath(out);

    // 一张一眼认得出的测试底图：从深青到暖金的斜向渐变（默认风景图里没有这个配色的）。
    const QString artPath = QDir(artDir.path()).filePath(QStringLiteral("wtfml-r82-background.png"));
    {
        QImage art(1600, 1000, QImage::Format_RGB32);
        QPainter painter(&art);
        QLinearGradient gradient(0, 0, art.width(), art.height());
        gradient.setColorAt(0.0, QColor(0x0e, 0x3b, 0x4a));
        gradient.setColorAt(0.55, QColor(0x1f, 0x93, 0x86));
        gradient.setColorAt(1.0, QColor(0xe8, 0xb0, 0x4a));
        painter.fillRect(art.rect(), gradient);
        painter.end();
        if (!art.save(artPath, "PNG")) return 3;
    }

    const auto pump = [&app] {
        app.processEvents();
        app.processEvents();
        QThread::msleep(160);  // 场景底那条 120 ms 防抖（Glass::Backdrop）走完再截
        app.processEvents();
    };

    int saved = 0;
    for (const QString &preset : {QStringLiteral("dark"), QStringLiteral("emerald")}) {
        Theme::setSpec(Theme::presetSpec(preset));
        Theme::apply(app);

        QMainWindow window;
        MainWindowUI ui(&window);
        window.resize(1280, 800);
        window.show();

        // 主页按真样子灌一份：一个当前实例，最近 / 收藏都有记录（否则底部那一条是空的）。
        MainWindowUI::HomeQuickItem item;
        item.key = QStringLiteral("demo");
        item.name = QStringLiteral("1.21.4 Fabric");
        item.detail = QStringLiteral("Fabric 0.16.9 · 1.21.4");
        item.current = true;
        ui.setHomeQuickRows({item}, {item}, {item});
        ui.setHomeQuickMode(MainWindowUI::HomeQuickMode::Current);
        pump();

        // ① 设置 →「个性化」：「背景图」那一组滚进视野。
        const int lookIndex = MainWindowUI::settingsCategoryKeys().indexOf(QStringLiteral("look"));
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::settingsTabIndex());
        ui.getSettingsTabs()->setCurrentIndex(lookIndex);
        QScrollArea *scroll = qobject_cast<QScrollArea *>(ui.getSettingsCategoryStack()->widget(lookIndex));
        const auto scrollToBackgroundGroup = [scroll] {
            if (scroll == nullptr || scroll->widget() == nullptr) return;
            QLabel *anchor = nullptr;
            for (QLabel *label : scroll->widget()->findChildren<QLabel *>()) {
                if (label->text() == QStringLiteral("背景图")) {
                    anchor = label;
                    break;
                }
            }
            scroll->verticalScrollBar()->setValue(
                anchor == nullptr ? 0
                                  : qMax(0, anchor->mapTo(scroll->widget(), QPoint(0, 0)).y() - 16));
        };
        // 滚两次：第一次之后页面还会重排（行里那几颗控件的宽度/高度是量完才定的），
        // 只滚一次会滚过头，把分组标题和「图片」那一行推到视野外。
        scrollToBackgroundGroup();
        pump();
        scrollToBackgroundGroup();
        pump();
        const QString groupShot = QDir(out).filePath(
            QStringLiteral("r82-%1-settings-background-group.png").arg(preset));
        if (window.grab().save(groupShot)) ++saved;

        // ② 设上图：规格里那一条就是产品落盘的那一条（路径 / 摆法 / 不透明度）。
        Theme::Spec spec = Theme::presetSpec(preset);
        Theme::PartStyle style;
        style.hasImage = true;
        style.imagePath = artPath;
        style.imageName = QFileInfo(artPath).fileName();
        style.fit = Theme::ImageFit::Cover;
        style.imageOpacity = 100;
        spec.parts.insert(int(Theme::Part::PageBackground), style);
        Theme::setSpec(spec);
        Theme::apply(app);
        // 设置页那几格照产品刷一遍（产品里是 ThemeSettingsController::refresh()：
        // 配色下拉认 presetId、背景图那一行写「现在铺的是：…」并亮起三样控件）。
        if (QComboBox *combo = ui.getThemePresetCombo()) {
            const QSignalBlocker block(combo);
            const int index = combo->findData(spec.presetId);
            if (index >= 0) combo->setCurrentIndex(index);
        }
        {
            const MainWindowUI::ThemeImageRow &row = ui.getThemeImageRow(0);
            if (row.state != nullptr) {
                row.state->setText(QStringLiteral("当前主题：%1").arg(style.imageName));
                const int fitIndex = row.fit->findData(Theme::imageFitKey(style.fit));
                if (fitIndex >= 0) row.fit->setCurrentIndex(fitIndex);
                row.opacity->setValue(style.imageOpacity);
                row.percent->setText(MainWindowUI::themeImagePercentText(style.imageOpacity));
                row.fit->setEnabled(true);
                row.opacity->setEnabled(true);
                row.percent->setEnabled(true);
                row.clear->setEnabled(true);
            }
        }
        if (auto *backdrop = window.findChild<Glass::Backdrop *>()) backdrop->rebuildNow();
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        pump();
        const QString homeShot =
            QDir(out).filePath(QStringLiteral("r82-%1-home-background.png").arg(preset));
        if (window.grab().save(homeShot)) ++saved;

        // ③ 设了图之后的设置页。
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::settingsTabIndex());
        ui.getSettingsTabs()->setCurrentIndex(lookIndex);
        if (scroll != nullptr) scroll->verticalScrollBar()->setValue(0);
        pump();
        const QString settingsShot =
            QDir(out).filePath(QStringLiteral("r82-%1-settings-background.png").arg(preset));
        if (window.grab().save(settingsShot)) ++saved;
        window.hide();
    }
    std::printf("background shots: %d saved to %s\n", saved, qPrintable(out));
    return saved == 6 ? 0 : 1;
}
