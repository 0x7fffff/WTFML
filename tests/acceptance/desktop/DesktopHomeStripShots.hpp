#pragma once
// 主页实例带（#250）样张与实测：默认窗口 1024×620，深色 / 浅色各拍展开、收起，再拍一张放满上限、带长加载器名的。
// 同时把「一排放得下几张卡」量成 home-strip-fit.json（每张卡的宽、右缘，和这一排的宽）。开发工具，不进启动器。
#include "DesktopDesignShots.hpp"
#include "HomeQuickLimit.hpp"
#include "MainWindow.hpp"
#include "MainWindowPageLayout.hpp"
#include "MainWindowUI.hpp"
#include "Animations.hpp"
#include "InlineMessage.hpp"
#include "Theme.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace HomeStripShots {

inline QList<MainWindowUI::HomeQuickItem> realisticItems() {
    const struct { const char *key, *name, *detail; } rows[] = {
        {"spring", "春日生存", "Minecraft 1.21.1 / NeoForge 21.1.77\n上次启动：2 小时前"},
        {"create", "机械动力 Create", "Minecraft 1.20.1 / Fabric 0.16.9\n上次启动：昨天"},
        {"sakura", "樱花小屋", "Minecraft 1.21.4 / Fabric 0.16.9\n上次启动：3 天前"},
        {"dragon", "龙之冒险整合包", "Minecraft 1.20.1 / Forge 47.3.0\n上次启动：上周"},
        {"skyblock", "空岛生存", "Minecraft 1.21.1 / NeoForge 21.1.77\n上次启动：上周"},
        {"shader", "光影测试", "Minecraft 1.21.1 / Quilt 0.26.4\n上次启动：上周"},
    };
    QList<MainWindowUI::HomeQuickItem> items;
    for (const auto &row : rows) {
        MainWindowUI::HomeQuickItem item;
        item.key = QString::fromUtf8(row.key);
        item.name = QString::fromUtf8(row.name);
        item.detail = QString::fromUtf8(row.detail);
        item.recent = true;
        items.append(item);
    }
    items.first().current = true;
    return items;
}

/// 放满上限、加载器名偏长（NeoForge / LegacyFabric 之类）的一排。
inline QList<MainWindowUI::HomeQuickItem> longLoaderItems(int count) {
    const struct { const char *name, *detail; } rows[] = {
        {"春日生存", "Minecraft 1.21.11 / NeoForge 21.11.0"},
        {"机械动力 Create", "Minecraft 1.20.1 / LegacyFabric 0.16.9"},
        {"樱花小屋", "Minecraft 1.21.4 / NeoForge 21.4.62"},
        {"龙之冒险整合包", "Minecraft 1.20.1 / NeoForge 47.1.106"},
        {"空岛生存", "Minecraft 1.21.1 / NeoForge 21.1.77"},
        {"光影测试", "Minecraft 1.21.1 / Quilt 0.26.4"},
    };
    QList<MainWindowUI::HomeQuickItem> items;
    for (int i = 0; i < count && i < 6; ++i) {
        MainWindowUI::HomeQuickItem item;
        item.key = QStringLiteral("long%1").arg(i);
        item.name = QString::fromUtf8(rows[i].name);
        item.detail = QString::fromUtf8(rows[i].detail) + QStringLiteral("\n上次启动：昨天");
        item.recent = true;
        items.append(item);
    }
    items.first().current = true;
    return items;
}

inline QJsonObject measure(MainWindow &window, const QString &label, const QList<MainWindowUI::HomeQuickItem> &items) {
    window.ui()->setHomeQuickRows({items.first()}, {}, items);
    QApplication::processEvents();
    QApplication::processEvents();
    auto *strip = window.findChild<QWidget *>(QStringLiteral("homeQuickRecentSection"));
    const QList<QPushButton *> cards =
        strip->findChildren<QPushButton *>(QRegularExpression(QStringLiteral("^homeQuickCard_")));
    QJsonArray widths, rights;
    int right = 0;
    for (QPushButton *card : cards) {
        widths.append(card->width());
        right = card->mapTo(strip, QPoint(0, 0)).x() + card->width();
        rights.append(right);
    }
    return QJsonObject{{"label", label}, {"stripWidth", strip->width()}, {"windowWidth", window.width()},
                       {"cards", cards.size()}, {"cardWidths", widths}, {"cardRights", rights}};
}

inline void run(QApplication &app, MainWindow &window, const QString &root) {
    struct Shot { QString theme, state; };
    static QList<Shot> shots;
    for (const QString &theme : {QStringLiteral("dark"), QStringLiteral("emerald")})
        for (const char *state : {"expanded", "collapsed", "full", "picker"}) shots.append({theme, QLatin1String(state)});
    static int index = 0;
    static QJsonArray report;
    static std::function<void()> step;
    step = [&app, &window, root]() {
        if (index >= shots.size()) {
            // 开合动画逐帧采样：每 16 ms 记一次「页签 y / 启动按钮底 / 立绘底」，收起、展开各一遍；
            // 同时在 100 ms 处抓一张中间帧。动画是真的（Anim 打开）。
            Anim::setEnabled(true);
            auto *samples = new QJsonArray;
            auto *phase = new int(0);
            auto *tick = new QTimer(&window);
            auto *frame = new int(0);
            auto *tabs = window.findChild<QWidget *>(QStringLiteral("homeQuickRecentTab"));
            auto *launch = window.findChild<QWidget *>(QStringLiteral("homeCurrentLaunch"));
            auto *viewer = window.findChild<QWidget *>(MainWindowUI::skinViewerName());
            const auto sample = [=, &window]() {
                const auto y = [&window](QWidget *w, int h) { return w->mapTo(&window, QPoint(0, h)).y(); };
                samples->append(QJsonObject{{"phase", *phase}, {"t", *frame * 16}, {"tabsY", y(tabs, 0)},
                                            {"launchBottom", y(launch, launch->height())},
                                            {"viewerBottom", y(viewer, viewer->height())}});
            };
            QObject::connect(tick, &QTimer::timeout, &window, [=, &app, &window, &root]() {
                sample();
                if (*frame == 6 && *phase == 0) window.grab().save(root + QStringLiteral("/mid-collapse-dark-1024x620.png"));
                if (++*frame * 16 > 300) {
                    if (*phase == 0) {
                        *phase = 1;
                        *frame = 0;
                        window.ui()->setHomeQuickCollapsed(false);
                    } else {
                        tick->stop();
                        QFile out(root + QStringLiteral("/home-strip-motion.json"));
                        if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(*samples).toJson());
                        app.exit(0);
                    }
                }
            });
            Theme::setSpec(Theme::presetSpec(QStringLiteral("dark")));
            Theme::apply(app);
            window.ui()->setHomeQuickCollapsed(false, false);
            window.ui()->setHomeQuickMode(MainWindowUI::HomeQuickMode::Recent);
            QTimer::singleShot(300, &window, [&window, tick]() {
                window.ui()->setHomeQuickCollapsed(true);
                tick->start(16);
            });
            QFile out(root + QStringLiteral("/home-strip-fit.json"));
            if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(report).toJson());
            return;
        }
        const Shot shot = shots.at(index++);
        Theme::setSpec(Theme::presetSpec(shot.theme));
        Theme::apply(app);
        window.showNormal();
        window.resize(MainWindowPageLayout::kMainWindowDefaultWidth, MainWindowPageLayout::kMainWindowDefaultHeight);
        window.ui()->getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        window.ui()->setHomeQuickCollapsed(false, false);
        const auto seed = [&window, shot]() {
            const QList<MainWindowUI::HomeQuickItem> items =
                shot.state == QStringLiteral("full") ? longLoaderItems(kHomeQuickMaxCount + 1) : realisticItems();
            window.ui()->setHomeQuickRows({items.first()}, {}, items);
            window.ui()->setHomeQuickMode(MainWindowUI::HomeQuickMode::Recent);
            window.ui()->setHomeQuickCollapsed(shot.state == QStringLiteral("collapsed"), false);
        };
        seed();
        QTimer::singleShot(300, &window, [&window]() { window.resize(MainWindowPageLayout::kMainWindowDefaultWidth, MainWindowPageLayout::kMainWindowDefaultHeight); });
        QTimer::singleShot(700, &window, seed);
        QTimer::singleShot(1000, &window, [&window, root, shot]() {
            const QString file = QStringLiteral("%1-%2-%3x%4.png").arg(shot.state, shot.theme).arg(window.width()).arg(window.height());
            if (shot.state == QStringLiteral("picker")) {
                QList<MainWindowUI::HomePickerInstance> rows;
                for (const auto &item : realisticItems()) {
                    MainWindowUI::HomePickerInstance row;
                    row.id = item.key;
                    row.title = item.name;
                    row.subtitle = item.detail.section(QLatin1Char('\n'), 0, 0);
                    row.current = item.current;
                    row.recent = item.recent;
                    rows.append(row);
                }
                rows[1].subtitle += QStringLiteral(" · 版本不符");
                rows[3].subtitle += QStringLiteral(" · 加载器不符");
                QTimer::singleShot(300, &window, [&window, root, file] {
                    auto *box = window.findChild<InlineMessageBox *>(MainWindowUI::resourceDownloadInstancePickerName());
                    const bool saved = box && window.grab().save(root + QLatin1Char('/') + file);
                    report.append(QJsonObject{{"file", file}, {"saved", saved}});
                    if (box) box->buttonAt(2)->click();
                });
                bool otherFolder = false;
                window.ui()->chooseResourceDownloadInstance(rows, &otherFolder);
                QTimer::singleShot(200, &window, [] { step(); });
                return;
            }
            report.append(QJsonObject{{"file", file}, {"saved", window.grab().save(root + QLatin1Char('/') + file)},
                                      {"width", window.width()}, {"height", window.height()}});
            if (shot.theme == QStringLiteral("dark") && shot.state == QStringLiteral("expanded")) {
                for (int n = 1; n <= 6; ++n) {
                    report.append(measure(window, QStringLiteral("realistic-%1").arg(n), realisticItems().mid(0, n)));
                    report.append(measure(window, QStringLiteral("long-loader-%1").arg(n), longLoaderItems(n)));
                }
                window.ui()->setHomeQuickMode(MainWindowUI::HomeQuickMode::Recent);
            }
            QTimer::singleShot(200, &window, [] { step(); });
        });
    };
    QTimer::singleShot(1500, &window, [] { step(); });
}

}  // namespace HomeStripShots
