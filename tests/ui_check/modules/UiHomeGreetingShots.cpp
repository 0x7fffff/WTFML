// 主页大标题问候的离屏样张：深色 / 青瓷 × 1280x800 与 900x620 × 六种问候，另拼一张总览 contact.png。
// 带名字的那几张，右上账号胶囊也写同一个名字（和真窗口一样同一来源）；不带名字的胶囊写「登录」。
// 用法：QT_QPA_PLATFORM=offscreen ui_check_home_greeting_shots <输出目录>
// 不进 ctest：只出图，不做断言（断言在 UiDailyQuoteCheck 的 HG-UI 段）。
#include "Support.hpp"

#include "MainWindowUI.hpp"
#include "Theme.hpp"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QMainWindow>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>

int UiHomeGreetingShots(QApplication &app) {
    Anim::setEnabled(false);
    QTemporaryDir settingsDir(QDir::current().filePath(QStringLiteral(".hg-shots-XXXXXX")));
    if (!settingsDir.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    app.setOrganizationName(QStringLiteral("WTFMLHomeGreetingShots"));
    app.setApplicationName(QStringLiteral("HomeGreetingShots"));
    app.setStyle(Theme::createStyle());
    const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
    const int fontId = QFile::exists(fontPath) ? QFontDatabase::addApplicationFont(fontPath) : -1;
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    app.setFont(QFont(families.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : families.first(), 9));

    const QString out = app.arguments().size() > 1
                            ? app.arguments().at(1)
                            : QDir::current().filePath(QStringLiteral("build/home-greeting-shots"));
    QDir().mkpath(out);

    struct Sample {
        const char *key;
        QString text;
        QString name;
    };
    const QString longName = QStringLiteral("ThisIsAVeryLongName_16");
    const QVector<Sample> samples = {
        {"steve", QStringLiteral("晚上好 Steve"), QStringLiteral("Steve")},
        {"xiaoming", QStringLiteral("晚上好 小明"), QStringLiteral("小明")},
        {"mixed", QStringLiteral("晚上好 Steve小明"), QStringLiteral("Steve小明")},
        {"late-noname", QStringLiteral("夜深了"), QString()},
        {"welcome", QStringLiteral("欢迎来到 WTFML"), QString()},
        {"long", QStringLiteral("晚上好 ") + longName, longName},
    };
    const QVector<QSize> sizes = {QSize(1280, 800), QSize(900, 620)};
    const auto settle = [&app] {
        for (int i = 0; i < 12; ++i) app.processEvents();
    };

    // 总览：每行一个 主题 × 尺寸，每列一种问候，统一缩到 480 宽。
    constexpr int cellW = 480;
    QVector<QVector<QImage>> rows;
    int saved = 0;
    for (const QString &preset : {QStringLiteral("dark"), QStringLiteral("emerald")}) {
        Theme::setSpec(Theme::presetSpec(preset));
        Theme::apply(app);
        for (const QSize size : sizes) {
            QVector<QImage> row;
            for (const Sample &sample : samples) {
                QMainWindow window;
                MainWindowUI ui(&window);
                window.resize(size);
                window.show();
                MainWindowUI::HomeQuickItem item;
                item.key = QStringLiteral("demo");
                item.name = QStringLiteral("1.21.4 Fabric");
                item.detail = QStringLiteral("Fabric 0.16.9 · 1.21.4");
                item.current = true;
                ui.setHomeQuickRows({item}, {item}, {item});
                ui.setHomeQuickMode(MainWindowUI::HomeQuickMode::Current);
                ui.setHomeHeroQuote(QStringLiteral("长河千嶂，大荒孤城，历历在目。"),
                                    QStringLiteral("— 令《明日方舟》"), Theme::QuoteScript::Han);
                ui.setHomeHeroTitle(sample.text, sample.name);
                const bool named = !sample.name.isEmpty();
                if (QLabel *pill = ui.getHomeAccountLabel())
                    pill->setText(named ? sample.name : MainWindowUI::homeAccountSignInText());
                if (QLabel *type = ui.getHomeAccountTypeLabel()) {
                    type->setText(MainWindowUI::homeAccountMicrosoftText());
                    type->setVisible(named);
                }
                if (QLabel *face = ui.getHomeAvatarLabel()) {
                    QPixmap mark;
                    if (named) {
                        mark = QPixmap(face->size());
                        mark.fill(QColor(0x3c, 0x9e, 0x7a));
                    }
                    face->setPixmap(mark);
                }
                settle();
                const QImage shot = window.grab().toImage();
                const QString file = QDir(out).filePath(QStringLiteral("r89b-%1-%2-%3.png")
                                                            .arg(preset)
                                                            .arg(size.width())
                                                            .arg(QString::fromLatin1(sample.key)));
                if (shot.save(file)) ++saved;
                std::printf("%s %dx%d %s → %s\n", qPrintable(preset), size.width(), size.height(), sample.key,
                            qPrintable(ui.homeHeroTitleShown()));
                row.append(shot.scaledToWidth(cellW, Qt::SmoothTransformation));
                window.hide();
            }
            rows.append(row);
        }
    }
    int sheetH = 0;
    for (const auto &row : rows) sheetH += row.value(0).height() + 8;
    QImage sheet(cellW * samples.size() + 8 * (samples.size() - 1), sheetH - 8, QImage::Format_RGB32);
    sheet.fill(Qt::black);
    {
        QPainter p(&sheet);
        int y = 0;
        for (const auto &row : rows) {
            for (int i = 0; i < row.size(); ++i) p.drawImage(i * (cellW + 8), y, row.at(i));
            y += row.value(0).height() + 8;
        }
    }
    if (sheet.save(QDir(out).filePath(QStringLiteral("contact.png")))) ++saved;

    std::printf("home-greeting shots: %d saved to %s\n", saved, qPrintable(out));
    std::fflush(stdout);
    return saved == samples.size() * sizes.size() * 2 + 1 ? 0 : 1;
}
