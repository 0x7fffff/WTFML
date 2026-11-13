// 账号页「正版 / 离线」两个页签的离屏样张（没配应用 ID 的状态）：深色 / 翡翠 × 1280x800 与 900x620
// × 无账号 / 两个离线账号。整页照真窗口那样经 OverlayHost::showPage 摆在主窗口上，另拼一张总览 contact.png。
// 用法：QT_QPA_PLATFORM=offscreen ui_check_account_kind_shots <输出目录>
// 不进 ctest：只出图，不做断言（断言在 UiAccountSkinChecks）。
#include "Support.hpp"

#include "AccountSkinDialog.hpp"
#include "MainWindowUI.hpp"
#include "OverlayHost.hpp"
#include "Theme.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLineEdit>
#include <QMainWindow>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

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

    constexpr int cellW = 560;
    QVector<QImage> cells;
    int saved = 0;
    for (const QString &preset : {QStringLiteral("dark"), QStringLiteral("emerald")}) {
        Theme::setSpec(Theme::presetSpec(preset));
        Theme::apply(app);
        for (const QSize size : {QSize(1280, 800), QSize(900, 620)}) {
            for (const bool withAccounts : {false, true}) {
                for (const bool offline : {false, true}) {
                    QMainWindow window;
                    MainWindowUI ui(&window);
                    window.resize(size);
                    window.show();
                    auto *dialog = new AccountSkinDialog(&window);
                    // 和 MainWindow::refreshAccountSkin() 喂的同一套：列表、副题、皮肤那一档。
                    if (withAccounts) {
                        dialog->setAccounts({{QStringLiteral("offline-steve"), QStringLiteral("Steve"), {}, true, true},
                                             {QStringLiteral("offline-alex"), QStringLiteral("Alex"), {}, false, true}});
                        dialog->setStatusText(AccountSkinDialog::offlineIdentityText(QStringLiteral("Steve")));
                    } else {
                        dialog->setAccounts({});
                        dialog->setStatusText(AccountSkinDialog::notLoggedInNote());
                    }
                    dialog->setOfflineSkinState(withAccounts, false);
                    dialog->setOnlineLoginUnavailable(unavailable);
                    dialog->showAccountKind(offline);
                    ui.overlay()->showPage(dialog, AccountSkinDialog::windowTitle());
                    settle();
                    const QImage shot = window.grab().toImage();
                    const QString file = QDir(out).filePath(
                        QStringLiteral("r94b-account-%1-%2-%3-%4.png")
                            .arg(preset)
                            .arg(size.width())
                            .arg(withAccounts ? QStringLiteral("two-offline") : QStringLiteral("none"))
                            .arg(offline ? QStringLiteral("offline") : QStringLiteral("online")));
                    if (shot.save(file)) ++saved;
                    // 量几个数，交回里好说清：账号栏多宽、正版说明几行、名字框多宽。
                    const auto *pane = dialog->findChild<QWidget *>(QStringLiteral("accountManagementPane"));
                    const auto *note = dialog->findChild<QLabel *>(AccountSkinDialog::onlineUnavailableLabelName());
                    const auto *edit = dialog->findChild<QLineEdit *>(AccountSkinDialog::offlineNameEditName());
                    const int noteLines = note != nullptr && note->isVisible()
                                              ? note->height() / qMax(1, note->fontMetrics().lineSpacing())
                                              : 0;
                    std::printf("%s pane %d, note %d lines, edit %d, dialog %d\n",
                                qPrintable(QFileInfo(file).fileName()), pane ? pane->width() : -1, noteLines,
                                edit != nullptr && edit->isVisible() ? edit->width() : -1, dialog->width());
                    cells.append(shot.scaledToWidth(cellW, Qt::SmoothTransformation));
                    ui.overlay()->closeOverlay();
                    window.hide();
                }
            }
        }
    }
    // 开页过渡帧：首次打开（这一页刚建出来）/ 再次打开（已建好、收过一次），窗口 901x800
    //（真机 1127x1001 物理像素 / 125%）。照 MainWindow::onAccountSkinButtonClicked 的顺序喂：
    // 先摆页签那几项、showPage，列表和副题那一发刷新排在开页之后。
    {
        Theme::setSpec(Theme::presetSpec(QStringLiteral("dark")));
        Theme::apply(app);
        Anim::setEnabled(true);
        QMainWindow window;
        MainWindowUI ui(&window);
        window.resize(901, 800);
        window.show();
        settle();
        auto *dialog = new AccountSkinDialog(&window);
        const auto keyRects = [dialog] {
            QList<QRect> rects;
            for (const QString &name : {QStringLiteral("accountKindSegment"), AccountSkinDialog::offlineNameEditName(),
                                        AccountSkinDialog::viewerName(), AccountSkinDialog::variantComboName()}) {
                const auto *w = dialog->findChild<QWidget *>(name);
                rects << (w != nullptr ? QRect(w->mapTo(dialog, QPoint()), w->size()) : QRect());
            }
            return rects;
        };
        for (const QString &pass : {QStringLiteral("first"), QStringLiteral("again")}) {
            dialog->setOfflineSkinState(false, false);
            dialog->setOnlineLoginUnavailable(unavailable);
            dialog->showAccountKind(true);
            ui.overlay()->showPage(dialog, AccountSkinDialog::windowTitle());
            const QList<QRect> atOpen = keyRects();
            QImage first = window.grab().toImage();
            // 排在开页之后的那一发刷新（MainWindow::refreshAccountSkin）。
            QMetaObject::invokeMethod(
                dialog,
                [dialog] {
                    dialog->setAccounts({});
                    dialog->setStatusText(AccountSkinDialog::notLoggedInNote());
                },
                Qt::QueuedConnection);
            QVector<QImage> frames{first};
            QElapsedTimer clock;
            clock.start();
            while (clock.elapsed() < 400) {
                QEventLoop loop;
                QTimer::singleShot(40, &loop, &QEventLoop::quit);
                loop.exec();
                frames << window.grab().toImage();
            }
            const QList<QRect> settled = keyRects();
            int worst = 0;
            for (int i = 0; i < atOpen.size(); ++i) {
                worst = qMax(worst, qMax(qAbs(atOpen[i].x() - settled[i].x()), qAbs(atOpen[i].y() - settled[i].y())));
                worst = qMax(worst, qMax(qAbs(atOpen[i].width() - settled[i].width()),
                                         qAbs(atOpen[i].height() - settled[i].height())));
            }
            std::printf("open-%s: worst key-widget drift %d px\n", qPrintable(pass), worst);
            constexpr int frameW = 360;
            QImage strip(frameW * frames.size() + 6 * (frames.size() - 1),
                         frames.first().height() * frameW / frames.first().width(), QImage::Format_RGB32);
            strip.fill(Qt::black);
            QPainter p(&strip);
            for (int i = 0; i < frames.size(); ++i) {
                p.drawImage(i * (frameW + 6), 0, frames[i].scaledToWidth(frameW, Qt::SmoothTransformation));
                if (i < 2) frames[i].save(QDir(out).filePath(QStringLiteral("r94b-open-%1-t%2.png").arg(pass).arg(i + 1)));
            }
            p.end();
            if (strip.save(QDir(out).filePath(QStringLiteral("r94b-open-%1-strip.png").arg(pass)))) ++saved;
            ui.overlay()->closeOverlay();
            settle();
            QEventLoop loop;
            QTimer::singleShot(400, &loop, &QEventLoop::quit);
            loop.exec();
        }
        Anim::setEnabled(false);
        window.hide();
    }
    // 总览：每行一个 主题 × 尺寸 × 账号档，左正版右离线。
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
    return saved == cells.size() + 3 ? 0 : 1;
}
