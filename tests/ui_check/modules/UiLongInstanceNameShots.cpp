// 长实例名字号（步骤 127）的离屏样张：主页「当前实例」与实例设置页眉 × 短 / 12 字 / 30 字 / 60 字符英文
// × 1280x800 与 900x620，主题暮色雪山；另出冰川碧湖（浅色）各一张 30 字的。每张图往标准输出打一行
// 「px <主题> <尺寸> <面> <键> <字号> <是否截断>」。
// 用法：QT_QPA_PLATFORM=offscreen ui_check_long_instance_name_shots <输出目录>
// 不进 ctest：只出图，不做断言。
#include "Support.hpp"

#include "MainWindowUI.hpp"
#include "Theme.hpp"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetrics>

int UiLongInstanceNameShots(QApplication &app) {
    const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
    const int fontId = QFile::exists(fontPath) ? QFontDatabase::addApplicationFont(fontPath) : -1;
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    app.setFont(QFont(families.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : families.first(), 9));
    const QString out = app.arguments().size() > 1 ? app.arguments().at(1)
                                                    : QDir::current().filePath(QStringLiteral("build/long-name-shots"));
    QDir().mkpath(out);

    const QList<std::pair<QString, QString>> names = {
        {QStringLiteral("short"), QStringLiteral("生存")},
        {QStringLiteral("mid"), QStringLiteral("我的第一个生存世界整合包")},
        {QStringLiteral("long"), QStringLiteral("群峦叠翠云海翻腾的远古遗迹探险与机械动力工业整合包第三季重制")},
        {QStringLiteral("latin"), QStringLiteral("All The Mods 9 - To the Sky Expert Edition with Create Addons")},
    };
    const QList<std::pair<QString, QSize>> sizes = {{QStringLiteral("1280"), QSize(1280, 800)},
                                                    {QStringLiteral("900"), QSize(900, 620)}};
    const auto settle = [&app] {
        for (int i = 0; i < 12; ++i) app.processEvents();
    };
    const auto report = [](const QString &preset, const QString &size, const char *surface, const QString &key,
                           const QLabel *label) {
        const bool cut = label != nullptr &&
                         label->fontMetrics().horizontalAdvance(label->text()) > label->contentsRect().width();
        std::printf("px %s %s %s %s %d %s\n", qPrintable(preset), qPrintable(size), surface, qPrintable(key),
                    label != nullptr ? label->font().pixelSize() : -1, cut ? "cut" : "full");
        std::fflush(stdout);
    };

    int saved = 0;
    for (const QString &preset : {QStringLiteral("dark"), QStringLiteral("emerald")}) {
        Theme::setSpec(Theme::presetSpec(preset));
        Theme::apply(app);
        for (const auto &[sizeKey, size] : sizes) {
            for (const auto &[key, name] : names) {
                // 浅色只出 1280 下 30 字那一张：看浅底上的字号与省略号。
                if (preset != QLatin1String("dark") && (key != QLatin1String("long") || sizeKey != QLatin1String("1280")))
                    continue;
                const QString stem = QStringLiteral("r127-%1-%2-%3").arg(preset, sizeKey, key);
                {
                    QMainWindow window;
                    MainWindowUI ui(&window);
                    window.resize(size);
                    window.show();
                    MainWindowUI::HomeQuickItem item;
                    item.key = QStringLiteral("demo");
                    item.name = name;
                    item.detail = QStringLiteral("1.21.4 / Fabric 0.16.9");
                    item.current = true;
                    ui.setHomeQuickRows({item}, {item}, {item});
                    ui.setHomeQuickMode(MainWindowUI::HomeQuickMode::Current);
                    settle();
                    if (window.grab().save(QDir(out).filePath(stem + QStringLiteral("-home.png")))) ++saved;
                    report(preset, sizeKey, "home", key, window.findChild<QLabel *>(QStringLiteral("homeCurrentName")));
                }
                {
                    InstanceSettingsDialog::GlobalValues global;
                    InstanceSettingsDialog dialog(name, name, InstanceSettingsDialog::Override(), global, QStringList(),
                                                  false);
                    dialog.setHeaderMeta(QStringLiteral("1.21.4 · Fabric 0.16.9"));
                    dialog.resize(size);
                    dialog.show();
                    settle();
                    if (dialog.grab().save(QDir(out).filePath(stem + QStringLiteral("-settings.png")))) ++saved;
                    report(preset, sizeKey, "settings", key, dialog.findChild<QLabel *>(QStringLiteral("instanceHeaderTitle")));
                }
            }
        }
    }
    std::printf("saved %d shots to %s\n", saved, qPrintable(QDir::toNativeSeparators(out)));
    return saved > 0 ? 0 : 1;
}
