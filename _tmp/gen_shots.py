NL = chr(92) + 'n'
body = '''// 主页「每日一言」的离屏样张：深色 / 青瓷 × 1280x800 与 900x620 × 有实例 × 中/英/日，
// 另出设置页那一个开关所在的一行。
// 用法：QT_QPA_PLATFORM=offscreen ui_check_daily_quote_shots <输出目录>
// 不进 ctest（和 ui_check_settings_shots 同一个路数）：它只出图，不做断言。
#include "Support.hpp"

#include "MainWindowUI.hpp"
#include "SettingsPageParts.hpp"
#include "Theme.hpp"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QMainWindow>
#include <QSettings>
#include <QTemporaryDir>

int UiDailyQuoteShots(QApplication &app) {
    Anim::setEnabled(false);
    // 临时设置目录：本进程不读盘上那份主题（和走查样张同一个路数）。
    QTemporaryDir settingsDir(QDir::current().filePath(QStringLiteral(".dq-shots-XXXXXX")));
    if (!settingsDir.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    app.setOrganizationName(QStringLiteral("WTFMLDailyQuoteShots"));
    app.setApplicationName(QStringLiteral("DailyQuoteShots"));
    app.setStyle(Theme::createStyle());
    const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
    const int fontId = QFile::exists(fontPath) ? QFontDatabase::addApplicationFont(fontPath) : -1;
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    app.setFont(QFont(families.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : families.first(), 9));

    const QString out = app.arguments().size() > 1
                            ? app.arguments().at(1)
                            : QDir::current().filePath(QStringLiteral("build/daily-quote-shots"));
    QDir().mkpath(out);

    struct Sample {
        const char *key;
        QString text;
        QString tip;
        Theme::QuoteScript script;
    };
    const QVector<Sample> samples = {
        {"zh", QStringLiteral("长河千嶂，大荒孤城，历历在目。"),
         QStringLiteral("— 令《明日方舟》"), Theme::QuoteScript::Han},
        {"en", QStringLiteral("The significance of life is living."),
         QStringLiteral("— Jiddu Krishnamurti"), Theme::QuoteScript::Latin},
        {"ja", QStringLiteral("希望とシェイクハンドして、鏡を見つめ直して、真新しい自分。"),
         QStringLiteral("— 大森元貴《Brand New》"), Theme::QuoteScript::Kana},
    };
    struct Scope {
        const char *key;
        QSize size;
    } NL_    const QVector<Scope> scopes = {{"1280", QSize(1280, 800)}, {"900", QSize(900, 620)}};

    int saved = 0;
    for (const QString &preset : {QStringLiteral("dark"), QStringLiteral("emerald")}) {
        Theme::setSpec(Theme::presetSpec(preset));
        Theme::apply(app);
        for (const Scope &scope : scopes) {
            for (const Sample &sample : samples) {
                QMainWindow window;
                MainWindowUI ui(&window);
                window.resize(scope.size);
                window.show();
                MainWindowUI::HomeQuickItem item;
                item.key = QStringLiteral("demo");
                item.name = QStringLiteral("1.21.4 Fabric");
                item.detail = QStringLiteral("Fabric 0.16.9 · 1.21.4");
                item.current = true;
                ui.setHomeQuickRows({item}, {item}, {item});
                ui.setHomeHeroQuote(sample.text, sample.tip, sample.script);
                app.processEvents();
                const QString file = QDir(out).filePath(
                    QStringLiteral("r71-%1-%2-%3.png")
                        .arg(preset, QString::fromLatin1(scope.key), QString::fromLatin1(sample.key)));
                if (window.grab().save(file)) ++saved;
                window.hide();
            }
        }
        // 设置页那个开关：切到「个性化」、把「每日一言」那一行滚进视野，只抓那一行。
        {
            QMainWindow window;
            MainWindowUI ui(&window);
            window.resize(1280, 800);
            window.show();
            ui.getTabWidget()->setCurrentIndex(MainWindowUI::settingsTabIndex());
            ui.showSettingsCategory(QStringLiteral("look"), QStringLiteral("每日一言"));
            auto *check = window.findChild<QCheckBox *>(MainWindowUI::dailyQuoteCheckName());
            if (check != nullptr) check->setChecked(true);
            app.processEvents();
            app.processEvents();
            QWidget *row = check;
            while (row != nullptr && qobject_cast<SettingsParts::SettingsRow *>(row) == nullptr)
                row = row->parentWidget();
            const QString file = QDir(out).filePath(
                QStringLiteral("r71-%1-settings-switch.png").arg(preset));
            if (row != nullptr && row->grab().save(file)) ++saved;
            else std::printf("FAIL save %s" NL, qPrintable(file));
            window.hide();
        }
    }
    std::printf("daily-quote shots: %d saved to %s" NL, saved, qPrintable(out));
    return saved == 13 ? 0 : 1;
}
'''
body = body.replace('NL_', ';' + NL)
body = body.replace(' NL', ' "' + NL + '"')
open('../tests/ui_check/modules/UiDailyQuoteShots.cpp','w',encoding='utf-8',newline='').write(body)
print('written')
