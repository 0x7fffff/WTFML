// 主页大标题问候的离屏样张：深色 / 青瓷 × 1280x800 与 900x620 × 四种问候，另拼一张总览 contact.png；
// 再出一张 compare-latin-name.png：名字用文楷自带拉丁字形（旧）与 Dancing Script（新）的对照。
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
        {"evening", QStringLiteral("晚上好，Steve。"), QStringLiteral("Steve")},
        {"late-noname", QStringLiteral("夜深了。"), QString()},
        {"welcome", QStringLiteral("欢迎来到 WTFML。"), QString()},
        {"long", QStringLiteral("晚上好，") + longName + QStringLiteral("。"), longName},
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
                settle();
                const QImage shot = window.grab().toImage();
                const QString file = QDir(out).filePath(QStringLiteral("r89-%1-%2-%3.png")
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

    // ---- 对照：旧（整句文楷，拉丁字母用文楷自带字形）/ 新（拉丁字母走 Dancing Script） ----
    // 旧的那一格：把标题清空后，按旧做法（整句一支文楷、名字用 QFontMetrics 截）画回原位。
    const QStringList names = {QStringLiteral("Steve"), QStringLiteral("José"), longName};
    QVector<QImage> strips;
    for (const QString &preset : {QStringLiteral("dark"), QStringLiteral("emerald")}) {
        Theme::setSpec(Theme::presetSpec(preset));
        Theme::apply(app);
        for (const QString &name : names) {
            for (const bool latinScript : {false, true}) {
                QMainWindow window;
                MainWindowUI ui(&window);
                window.resize(1280, 800);
                window.show();
                const QString text = QStringLiteral("晚上好，") + name + QStringLiteral("。");
                ui.setHomeHeroTitle(latinScript ? text : QString(), latinScript ? name : QString());
                settle();
                QLabel *title = ui.getHomeHeroTitle();
                QImage shot = window.grab().toImage();
                const QRect box(title->mapTo(&window, title->contentsRect().topLeft()), title->contentsRect().size());
                if (!latinScript) {
                    QPainter p(&shot);
                    p.setFont(title->font());
                    p.setPen(title->palette().color(title->foregroundRole()));
                    const QFontMetrics fm(title->font());
                    QString old = text;
                    if (fm.horizontalAdvance(old) > box.width()) {
                        const QString head = QStringLiteral("晚上好，");
                        old = head + fm.elidedText(name, Qt::ElideRight, box.width() - fm.horizontalAdvance(head));
                    }
                    p.drawText(QPointF(box.left(), box.top() + fm.ascent()), old);
                }
                const QImage crop = shot.copy(QRect(box.left() - 24, box.top() - 16, 900, box.height() + 32));
                QImage labeled(crop.width() + 120, crop.height(), QImage::Format_RGB32);
                labeled.fill(QColor(32, 32, 32));
                {
                    QPainter p(&labeled);
                    p.drawImage(120, 0, crop);
                    p.setPen(Qt::white);
                    p.setFont(QFont(app.font().family(), 11));
                    p.drawText(QRect(8, 0, 110, crop.height()), Qt::AlignVCenter | Qt::TextWordWrap,
                               latinScript ? QStringLiteral("新：Dancing Script") : QStringLiteral("旧：文楷拉丁字形"));
                }
                strips.append(labeled);
                window.hide();
            }
        }
    }
    int compareH = 0;
    for (const QImage &strip : strips) compareH += strip.height() + 4;
    QImage compare(strips.value(0).width(), compareH, QImage::Format_RGB32);
    compare.fill(Qt::black);
    {
        QPainter p(&compare);
        int y = 0;
        for (const QImage &strip : strips) {
            p.drawImage(0, y, strip);
            y += strip.height() + 4;
        }
    }
    if (compare.save(QDir(out).filePath(QStringLiteral("compare-latin-name.png")))) ++saved;
    std::printf("home-greeting shots: %d saved to %s\n", saved, qPrintable(out));
    std::fflush(stdout);
    return saved == 18 ? 0 : 1;
}
