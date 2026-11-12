#pragma once
// Explicit marked-profile acceptance only. Normal startup does not enter this path.
#include "AppPaths.hpp"
#include "SecretRedactor.hpp"
#include <QApplication>
#include <QDebug>
#include <QSharedPointer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractButton>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace StartupAcceptance {
struct Context {
    QString root, sentinel;
    int closeAfterMs = 30000, stressRows = 0;
    int windowWidth = 0, windowHeight = 0;
    bool enabled() const { return !root.isEmpty(); }
};
inline bool safe(const QString &path) {
    for (QString p = path;;) {
        const QFileInfo info(p);
        if (info.isSymLink() || info.isJunction() || (info.exists() && !info.isDir())) return false;
        const auto parent = info.absolutePath();
        if (p == parent) return true;
        p = parent;
    }
}
inline bool configure(Context *out, QString *error) {
    const auto args = QCoreApplication::arguments();
    QString root;
    int supplied = 0;
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--acceptance-profile") {
            if (++i == args.size() || args[i].startsWith("--")) { *error = "Missing acceptance profile path"; return false; }
            root = args[i]; ++supplied;
        } else if (args[i].startsWith("--acceptance-profile=")) {
            root = args[i].mid(QString("--acceptance-profile=").size()); ++supplied;
        }
    }
    if (!supplied) return true;
    if (supplied != 1 || !QDir::isAbsolutePath(root) || !safe(root)) {
        *error = "Acceptance requires one absolute non-redirected marked profile"; return false;
    }
    root = QDir::cleanPath(root);
    QFile marker(root + "/acceptance-profile.json");
    if (QFileInfo(marker).isSymLink() || !marker.open(QIODevice::ReadOnly) || marker.size() > 4096) {
        *error = "Acceptance marker missing or invalid"; return false;
    }
    const auto object = QJsonDocument::fromJson(marker.readAll()).object();
    const int closeMs = object.value("closeAfterMs").toInt(-1);
    const int rows = object.value("stressRows").toInt(0);
    const int windowWidth = object.value("windowWidth").toInt(0);
    const int windowHeight = object.value("windowHeight").toInt(0);
    const auto sentinel = object.value("sentinel").toString();
    if (object.value("format") != "wtfml-startup-acceptance/1" || closeMs < 2000 || closeMs > 120000 ||
        rows < 0 || rows > 200 || (rows && (sentinel.size() < 16 || sentinel.size() > 128)) ||
        (windowWidth != 0 &&
         (windowWidth < 640 || windowWidth > 3840 || windowHeight < 480 || windowHeight > 2160))) {
        *error = "Invalid acceptance limits"; return false;
    }
    if (!safe(root + "/settings") || !safe(root + "/game") ||
        !QDir().mkpath(root + "/settings") || !QDir().mkpath(root + "/game")) {
        *error = "Cannot create isolated acceptance settings/data"; return false;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + "/settings");
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, root + "/settings-system");
    qApp->setProperty("wtfml.testDefaultMinecraftDir", root + "/game");
    AppPaths::setMinecraftDir(root + "/game");
    *out = {root, sentinel, closeMs, rows, windowWidth, windowHeight};
    SecretRedactor::remember(sentinel);
    return true;
}
inline void record(const Context &context, QJsonObject fields) {
    const auto path = context.root + "/startup-" + QString::number(QCoreApplication::applicationPid()) + ".json";
    QFile existing(path);
    QJsonObject object;
    if (existing.open(QIODevice::ReadOnly)) object = QJsonDocument::fromJson(existing.readAll()).object();
    existing.close();
    for (auto i = fields.begin(); i != fields.end(); ++i) object.insert(i.key(), i.value());
    object.insert("pid", QString::number(QCoreApplication::applicationPid()));
    object.insert("platform", QGuiApplication::platformName());
    object.insert("dataRoot", AppPaths::minecraftDir());
    object.insert("settingsFile", QSettings().fileName());
    QSaveFile output(path); output.setDirectWriteFallback(false);
    if (output.open(QIODevice::WriteOnly)) {
        const auto bytes = QJsonDocument(object).toJson();
        if (output.write(bytes) == bytes.size()) output.commit();
    }
}
inline void dismissRefusal(const Context &context) {
    if (!context.enabled()) return;
    QTimer::singleShot(500, qApp, [context] {
        // 提示是一张提示卡，挂在一扇以这句话为标题的小窗上（见 main.cpp 的 showStartupRefusal）。
        QWidget *dialog = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets())
            if (top->isVisible() && top->windowTitle() == QStringLiteral("启动器已在运行或目录不可用")) dialog = top;
        const bool shown = dialog != nullptr;
        const bool shot = shown && dialog->grab().save(context.root + "/refusal-" + QString::number(QCoreApplication::applicationPid()) + ".png");
        record(context, {{"refused", true}, {"dialogShown", shown}, {"screenshot", shot}});
        if (auto *ok = dialog ? dialog->findChild<QAbstractButton *>(QStringLiteral("inlineMessageButton0")) : nullptr) ok->click();
    });
}
inline void attach(const Context &context, QWidget &window) {
    if (!context.enabled()) return;
    QTimer::singleShot(500, &window, [context, &window] {
        record(context, {{"mainWindowShown", window.isVisible()},
            {"windowWidth", window.width()}, {"windowHeight", window.height()},
            {"screenshot", window.grab().save(context.root + "/window-" + QString::number(QCoreApplication::applicationPid()) + ".png")}});
    });
    QTimer::singleShot(context.closeAfterMs, &window, &QWidget::close);
    if (!context.stressRows) return;
    auto finished = QSharedPointer<int>::create(0);
    for (int worker = 0; worker < 4; ++worker) {
        auto *watcher = new QFutureWatcher<void>(&window);
        QObject::connect(watcher, &QFutureWatcher<void>::finished, &window, [watcher, finished, context] {
            watcher->deleteLater();
            if (++*finished == 4) record(context, {{"stressDone", true}, {"stressRows", context.stressRows * 4}});
        });
        watcher->setFuture(QtConcurrent::run([context, worker] {
            for (int i = 0; i < context.stressRows; ++i)
                qInfo().noquote() << QString("[acceptance-stress] worker=%1 row=%2 value=%3").arg(worker).arg(i).arg(context.sentinel);
        }));
    }
}
} // namespace StartupAcceptance

