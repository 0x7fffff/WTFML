// 账号页「正版 / 离线」两个页签的离屏样张（没配应用 ID 的状态）：深色 / 翡翠 × 1280x800 与 900x620。
// 整页照真窗口那样经 OverlayHost::showPage 摆在主窗口上，另拼一张总览 contact.png。
// 用法：QT_QPA_PLATFORM=offscreen ui_check_account_kind_shots <输出目录>
// 不进 ctest：只出图，不做断言（断言在 UiAccountSkinChecks）。
#include "Support.hpp"

#include "AccountSkinDialog.hpp"
#include "MainWindowUI.hpp"
#include "OverlayHost.hpp"
#include "Theme.hpp"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QMainWindow>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>

int UiAccountKindShots(QApplication &app) {
    Anim::setEnabled(false);
    QTemporaryDir settingsDir(QDir::current().filePath(QStringLiteral(".ak-shots-XXXXXX")));
    if (!settingsDir.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    app.setOrganizationName(QStringLiteral("WTFMLAccountKindShots"));
    app.setApplicationName(QStringLiteral("AccountKindShots"));
    app.setStyle(Theme::createStyle());
    const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
    const int fontId = QFile::exists(fontPath) ? QFontDatabase::addApplicationFont(fontPath) : -1;
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    app.setFont(QFont(families.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : families.first(), 9));

    const QString out = app.arguments().size() > 1
                            ? app.arguments().at(1)
                            : QDir::current().filePath(QStringLiteral("build/account-kind-shots"));
    QDir().mkpath(out);
    // 与 AuthManager::loginConfigurationError() 没配应用 ID 那一句相同（UI 测试库不链认证后端）。
    const QString unavailable =
        QStringLiteral("这个版本暂时不能登录正版账号，可以先用离线账号进游戏。已保存的账号不会丢失。");
    const auto settle = [&app] {
        for (int i = 0; i < 12; ++i) app.processEvents();
    };

    constexpr int cellW = 640;
    QVector<QImage> cells;
    int saved = 0;
    for (const QString &preset : {QStringLiteral("dark"), QStringLiteral("emerald")}) {
        Theme::setSpec(Theme::presetSpec(preset));
        Theme::apply(app);
        for (const QSize size : {QSize(1280, 800), QSize(900, 620)}) {
            for (const bool offline : {false, true}) {
                QMainWindow window;
                MainWindowUI ui(&window);
                window.resize(size);
                window.show();
                auto *dialog = new AccountSkinDialog(&window);
                dialog->setStatusText(QStringLiteral("还没有登录过，或者登录信息已经不完整了。"));
                dialog->setOfflineSkinState(false, false);
                dialog->setOnlineLoginUnavailable(unavailable);
                dialog->showAccountKind(offline);
                ui.overlay()->showPage(dialog, AccountSkinDialog::windowTitle());
                settle();
                const QImage shot = window.grab().toImage();
                const QString file = QDir(out).filePath(QStringLiteral("r94-account-%1-%2-%3.png")
                                                            .arg(preset)
                                                            .arg(size.width())
                                                            .arg(offline ? QStringLiteral("offline")
                                                                         : QStringLiteral("online")));
                if (shot.save(file)) ++saved;
                std::printf("%s %dx%d %s → %s\n", qPrintable(preset), size.width(), size.height(),
                            offline ? "offline" : "online", qPrintable(file));
                cells.append(shot.scaledToWidth(cellW, Qt::SmoothTransformation));
                ui.overlay()->closeOverlay();
                window.hide();
            }
        }
    }
    // 总览：每行一个 主题 × 尺寸，左正版右离线。
    int sheetH = 0;
    for (int i = 0; i < cells.size(); i += 2) sheetH += cells.at(i).height() + 8;
    QImage sheet(cellW * 2 + 8, sheetH - 8, QImage::Format_RGB32);
    sheet.fill(Qt::black);
    {
        QPainter p(&sheet);
        int y = 0;
        for (int i = 0; i < cells.size(); i += 2) {
            p.drawImage(0, y, cells.at(i));
            p.drawImage(cellW + 8, y, cells.at(i + 1));
            y += cells.at(i).height() + 8;
        }
    }
    if (sheet.save(QDir(out).filePath(QStringLiteral("contact.png")))) ++saved;
    std::printf("account-kind shots: %d saved to %s\n", saved, qPrintable(out));
    std::fflush(stdout);
    return saved == cells.size() + 1 ? 0 : 1;
}
