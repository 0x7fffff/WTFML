#include "Support.hpp"
int UiToolboxPolishCheck(QApplication &app) {

        const int fontId = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc")));
        const auto families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) app.setFont(QFont(families.first(), 10));
        app.setStyle(Theme::createStyle());
        Theme::setSpec(Theme::defaultSpec());
        Theme::apply(app);
        int failures = 0;
        const auto expect = [&](bool ok, const char *text) {
            printf("%s %s\n", ok ? "PASS" : "FAIL", text);
            if (!ok) ++failures;
        };
        ToolboxPage page;
        const QStringList names{QStringLiteral("coordResult"), ToolboxPage::beaconResultName(),
                                ToolboxPage::brewingResultName(), ToolboxPage::trimResultName()};
        for (const auto &name : names) {
            auto *copy = page.findChild<QPushButton *>(name + QStringLiteral("Copy"));
            expect(copy != nullptr && !copy->isEnabled() && !copy->autoDefault(), "empty report cannot copy and does not steal Enter");
        }
        const QString report = QStringLiteral("材料清单\n铁锭 × 9\n保留原样 <测试> & 引号");
        page.setCoordinateRows({QStringLiteral("主世界 <位置>"), QStringLiteral("区块 -1, -2")});
        page.setBeaconResult(report); page.setBrewingResult(report); page.setTrimResult(report);
        for (const auto &name : names) {
            auto *copy = page.findChild<QPushButton *>(name + QStringLiteral("Copy"));
            if (copy == nullptr) { expect(false, "copy button exists"); continue; }
            expect(copy->isEnabled(), "nonempty report enables copy");
            copy->click();
            const auto expected = name == QStringLiteral("coordResult")
                ? QStringLiteral("主世界 <位置>\n区块 -1, -2") : report;
            expect(QGuiApplication::clipboard()->text() == expected, "copy preserves Unicode/newlines and never exposes HTML");
        }
        page.setCoordinateRows({}); page.setBeaconResult({}); page.setBrewingResult({}); page.setTrimResult({});
        for (const auto &name : names) {
            auto *copy = page.findChild<QPushButton *>(name + QStringLiteral("Copy"));
            expect(copy != nullptr && !copy->isEnabled(), "clearing report prevents copying stale results");
        }
        int requests = 0;
        QObject::connect(&page, &ToolboxPage::serverCheckRequested, &page, [&](const QString &) { ++requests; });
        auto *address = page.findChild<QLineEdit *>(QStringLiteral("serverAddress"));
        expect(address != nullptr, "server address input exists");
        if (address != nullptr) {
            page.setServerBusy(true);
            QMetaObject::invokeMethod(address, "returnPressed", Qt::DirectConnection);
            expect(requests == 0, "Enter cannot bypass busy server-check button");
            page.setServerBusy(false);
            QMetaObject::invokeMethod(address, "returnPressed", Qt::DirectConnection);
            expect(requests == 1, "Enter starts exactly one check when idle");
        }
        // 「在线」那一格：-1 是「服务器没说」，不是「0 个人」—— 不许把 -1 画上屏。
        expect(ToolboxPage::serverPlayerCountText(7, 20) == QStringLiteral("7 / 20"),
               "server player count renders shown/max");
        expect(ToolboxPage::serverPlayerCountText(7, -1) == QStringLiteral("7"),
               "server player count omits the missing maximum");
        expect(ToolboxPage::serverPlayerCountText(0, 0) == QStringLiteral("0"),
               "zero players reported stays 0 (not treated as unknown)");
        expect(ToolboxPage::serverPlayerCountText(-1, -1).isEmpty(),
               "server that reported no online count yields empty text, never -1");
        ToolboxPage::ServerView noCountsView;
        noCountsView.ok = true;
        noCountsView.address = QStringLiteral("example.test");
        noCountsView.motd = QStringLiteral("欢迎");
        noCountsView.version = QStringLiteral("Paper 1.21.4");
        noCountsView.players = ToolboxPage::serverPlayerCountText(-1, -1);
        noCountsView.latency = QStringLiteral("26 ms");
        page.setServerView(noCountsView);
        const auto *serverResult = page.findChild<QLabel *>(QStringLiteral("serverResult"));
        expect(serverResult != nullptr && serverResult->text().contains(QStringLiteral("Paper 1.21.4")) &&
                   !serverResult->text().contains(QStringLiteral("在线")),
               "server card omits the 在线 line when the count is unknown");
        noCountsView.players = ToolboxPage::serverPlayerCountText(7, 20);
        page.setServerView(noCountsView);
        expect(serverResult != nullptr && serverResult->text().contains(QStringLiteral("在线")),
               "server card shows the 在线 line when the count is known");
        QImage preview(900, 750, QImage::Format_ARGB32_Premultiplied);
        preview.fill(Qt::white);
        QPainter painter(&preview); painter.setFont(app.font());
        const QList<QColor> inks{QColor("#075b50"), QColor("#185a9d"), QColor("#e4e9ed")};
        for (int column = 0; column < inks.size(); ++column) {
            const int x = column * 300;
            painter.fillRect(QRect(x, 0, 300, 750), column == 2 ? QColor("#20262c") : QColor("#f5f8f7"));
            painter.setPen(inks[column]);
            painter.drawText(QRect(x + 68, 20, 220, 28), Qt::AlignVCenter, QStringLiteral("工具箱 · 线稿预览"));
            const auto tools = ToolboxPage::tools();
            for (int index = 0; index < tools.size(); ++index) {
                const auto tool = tools[index]; const auto icon = ToolboxPage::toolIcon(tool, inks[column]);
                const auto image = icon.pixmap(24, 24).toImage();
                expect(!image.isNull() && image.hasAlphaChannel(), "tool icon renders with transparency");
                icon.paint(&painter, QRect(x + 26, 80 + index * 64, 24, 24));
                icon.paint(&painter, QRect(x + 70, 72 + index * 64, 40, 40));
                painter.drawText(QRect(x + 130, 78 + index * 64, 160, 28), Qt::AlignVCenter, ToolboxPage::toolName(tool));
            }
        }
        painter.end();
        const QString path = qEnvironmentVariable("WTFML_TOOLBOX_ICON_PREVIEW");
        if (!path.isEmpty()) expect(preview.save(path), "save icon preview sheet");
        return failures ? 1 : 0;
    
 return 0;
}

