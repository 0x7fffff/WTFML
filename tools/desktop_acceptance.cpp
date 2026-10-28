// Native layout evidence only. No real accounts, game data or system settings are modified.
#include "MainWindow.hpp"
#include "MainWindowUI.hpp"
#include "Theme.hpp"
#include "Animations.hpp"
#include "util/AppPaths.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QScreen>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <QWindow>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("WTFML-desktop-acceptance");
    app.setApplicationName("isolated");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"root", "New isolated evidence directory", "path"});
    parser.process(app);
    const QString root = QDir::cleanPath(parser.value("root"));
    if (!QDir::isAbsolutePath(root) || QFileInfo::exists(root)) return 2;
    // Qt environment overrides would simulate scaling rather than test Windows DPI.
    for (const char *key : {"QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "QT_FONT_DPI"})
        if (!qEnvironmentVariableIsEmpty(key)) return 2;
    if (QGuiApplication::platformName() != "windows") return 2;
    if (!QDir().mkpath(root + "/settings") || !QDir().mkpath(root + "/game")) return 2;
    app.setProperty("wtfml.testDefaultMinecraftDir", root + "/game");
    AppPaths::setMinecraftDir(root + "/game");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + "/settings");
    QSettings settings;
    settings.setValue("Launcher/Folders", QStringList{root + "/game"});
    settings.setValue("Launcher/CurrentFolder", root + "/game");
    settings.sync();
    if (settings.status() != QSettings::NoError) return 2;
    QApplication::setStyle(Theme::createStyle());
    Theme::setSpec(Theme::defaultSpec());
    Theme::apply(app);
    Anim::load();
    Anim::refreshPlatformReducedMotion();
    Anim::installPressAnimation(app);
    Anim::installScrollBarAnimation(app);
    MainWindow window;
    window.setWindowTitle("WTFML Desktop Acceptance");
    window.resize(1040, 700);
    window.show();
    QJsonArray samples;
    // Actual available preset IDs are the source of truth.
    QStringList themes = Theme::presetIds();
    themes.removeAll("system");
    int sample = 0;
    QTimer next;
    next.setInterval(700);
    QObject::connect(&next, &QTimer::timeout, &app, [&] {
        const int total = themes.size() * 4 * 2;
        if (sample >= total) {
            next.stop();
            const auto screen = window.windowHandle()->screen();
            const QJsonObject report{{"platform", QGuiApplication::platformName()},
                {"devicePixelRatio", window.devicePixelRatioF()},
                {"logicalDpi", screen->logicalDotsPerInch()},
                {"availableWidth", screen->availableGeometry().width()},
                {"availableHeight", screen->availableGeometry().height()},
                {"settingsFile", settings.fileName()}, {"samples", samples},
                {"scope", "native screenshots; keyboard, focus contrast and overlays require separate review"}};
            QFile output(root + "/report.json");
            const QByteArray bytes = QJsonDocument(report).toJson();
            const bool saved = output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size();
            bool allSaved = samples.size() == total;
            for (const auto &value : samples) {
                const auto entry = value.toObject();
                allSaved &= entry.value("saved").toBool() && entry.value("secondaryPaletteMismatches").toInt() == 0;
            }
            app.exit(saved && allSaved ? 0 : 1);
            return;
        }
        const int page = sample % 4;
        const int size = (sample / 4) % 2;
        const auto theme = themes.at(sample / 8);
        if (page == 0) {
            Theme::setSpec(Theme::presetSpec(theme));
            Theme::apply(app);
            Anim::setEnabled(size == 0);
            window.resize(size == 0 ? QSize(1040, 700) : QSize(900, 600));
        }
        window.ui()->getTabWidget()->setCurrentIndex(page);
        // Capture on the next event-loop turn, after layout and paint have settled.
        const int current = sample++;
        QTimer::singleShot(450, &window, [&, page, size, theme, current] {
            const QString name = QString("%1-%2-%3.png").arg(theme).arg(size).arg(page);
            const bool saved = window.grab().save(root + '/' + name);
            int secondaryLabels = 0, secondaryMismatches = 0;
            for (auto *label : window.findChildren<QLabel *>()) {
                if (label->property(Theme::textRolePropertyName().toUtf8().constData()).toString() != Theme::secondaryTextRoleValue()) continue;
                ++secondaryLabels;
                if (label->palette().color(QPalette::WindowText) != app.palette().color(QPalette::PlaceholderText)) ++secondaryMismatches;
            }
            samples.append(QJsonObject{{"index", current}, {"theme", theme}, {"page", page},
                {"secondaryLabels", secondaryLabels}, {"secondaryPaletteMismatches", secondaryMismatches},
                {"animations", size == 0}, {"requestedWidth", size == 0 ? 1040 : 900},
                {"requestedHeight", size == 0 ? 700 : 600}, {"width", window.width()},
                {"height", window.height()}, {"saved", saved}, {"file", name}});
        });
    });
    QTimer::singleShot(1000, &next, [&] { next.start(); });
    QTimer::singleShot(120000, &app, [&] { app.exit(124); });
    return app.exec();
}
