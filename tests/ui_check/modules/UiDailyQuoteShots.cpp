// 主页「每日一言」的离屏样张：深色 / 青瓷 × 1280x800 与 900x620 × 有实例 × 中/英/日 × 句库里最短 / 最长那句，
// 另出设置页那一个开关所在的一行。每张图同时往标准输出打一行「px <主题> <宽> <键> <字号> <句子>」，拼对照图用。
// 用法：QT_QPA_PLATFORM=offscreen ui_check_daily_quote_shots <输出目录>
// 不进 ctest（和 ui_check_settings_shots 同一个路数）：它只出图，不做断言。
#include "Support.hpp"

#include "MainWindowUI.hpp"
#include "SettingsPageParts.hpp"
#include "Theme.hpp"
#include "util/DailyQuote.hpp"

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
        QByteArray key;
        QString text;
        QString tip;
        Theme::QuoteScript script;
    };
    // 句库（fallbackQuotes）里每种文字最短、最长各一句：字号区间两头都看得到。
    QVector<Sample> samples;
    const struct {
        const char *key;
        DailyQuote::Script from;
        Theme::QuoteScript to;
    } scripts[] = {{"zh", DailyQuote::Script::Han, Theme::QuoteScript::Han},
                   {"en", DailyQuote::Script::Latin, Theme::QuoteScript::Latin},
                   {"ja", DailyQuote::Script::Kana, Theme::QuoteScript::Kana}};
    for (const auto &script : scripts) {
        const DailyQuote::Quote *shortest = nullptr;
        const DailyQuote::Quote *longest = nullptr;
        for (const DailyQuote::Quote &q : DailyQuote::fallbackQuotes()) {
            if (q.script != script.from) continue;
            if (shortest == nullptr || q.text.size() < shortest->text.size()) shortest = &q;
            if (longest == nullptr || q.text.size() > longest->text.size()) longest = &q;
        }
        if (shortest == nullptr) continue;
        samples.append({QByteArray(script.key) + "-short", shortest->text, DailyQuote::attribution(*shortest), script.to});
        samples.append({QByteArray(script.key) + "-long", longest->text, DailyQuote::attribution(*longest), script.to});
    }
    struct Scope {
        const char *key;
        QSize size;
    };
    const QVector<Scope> scopes = {{"1280", QSize(1280, 800)}, {"900", QSize(900, 620)}};

    // 版面要走几趟事件循环才稳：实例带是 show() 之后才灌进去的，它那一段高度一变，
    // 上面英雄区（stretch 1）就跟着重排。截早了截到的就是那半截没排完的样子。
    const auto settle = [&app] {
        for (int i = 0; i < 12; ++i) app.processEvents();
    };

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
                // 停在「当前」：否则实例带那两排（最近 / 收藏）浮在别的那一档上，
                // 截出来是一条空带（r71 那批就是这样）。灌完再走一遍事件循环，
                // 卡片是 show() 之后才塞进去的，一次 processEvents 排不完版。
                ui.setHomeQuickMode(MainWindowUI::HomeQuickMode::Current);
                ui.setHomeHeroQuote(sample.text, sample.tip, sample.script);
                settle();
                const QString where = QStringLiteral("%1-%2").arg(preset, QString::fromLatin1(scope.key));
                const QString file = QDir(out).filePath(
                    QStringLiteral("r116-%1-%2-%3.png")
                        .arg(preset, QString::fromLatin1(scope.key), QString::fromLatin1(sample.key)));
                if (window.grab().save(file)) ++saved;
                std::printf("px %s %s %s %d %s\n", qPrintable(preset), scope.key, sample.key.constData(),
                            ui.getHomeHeroQuote()->font().pixelSize(), qPrintable(sample.text));
                std::fflush(stdout);
                // 同档的对照（没有引文那一张）+ 两边的「当前实例」y：证明这一行只吃标题下面那块空白。
                if (QString::fromLatin1(sample.key) == QLatin1String("zh-long")) {
                    auto *eyebrow = window.findChild<QWidget *>(QStringLiteral("homeCurrentEyebrow"));
                    const auto yOf = [&](QWidget *w) { return w->mapTo(&window, QPoint(0, 0)).y(); };
                    const int on = eyebrow == nullptr ? -1 : yOf(eyebrow);
                    ui.setHomeHeroQuote(QString(), QString(), sample.script);
                    settle();
                    const int off = eyebrow == nullptr ? -1 : yOf(eyebrow);
                    const QString plain = QDir(out).filePath(
                        QStringLiteral("r116-%1-%2-noquote.png")
                            .arg(preset, QString::fromLatin1(scope.key)));
                    if (window.grab().save(plain)) ++saved;
                    std::printf("eyebrow %s: on=%d off=%d\n", qPrintable(where), on, off);
                    std::fflush(stdout);
                }
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
                QStringLiteral("r116-%1-settings-switch.png").arg(preset));
            if (row != nullptr && row->grab().save(file)) ++saved;
            else std::printf("FAIL save %s" "\n", qPrintable(file));
            window.hide();
        }
    }
    std::printf("daily-quote shots: %d saved to %s" "\n", saved, qPrintable(out));
    return saved == 2 * (scopes.size() * (samples.size() + 1) + 1) ? 0 : 1;
}
