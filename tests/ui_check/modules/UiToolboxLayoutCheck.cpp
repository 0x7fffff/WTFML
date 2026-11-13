// 工具箱版式验收：目录页 → 工具页两层、面包屑与切换浮层、十个工具页在 900×620 / 1280×800 / 窄内容宽
// 下都不出横向滚动条、不被裁；附魔行四个框同高、窄时折成两行。
// 设环境变量 WTFML_TOOLBOX_SHOTS=<目录> 时另外出验收图（深色 / 青瓷 × 两种窗口尺寸）。不弹真窗口。
#include "Support.hpp"
#include "DownloadToolPanel.hpp"
#include "GlassPopover.hpp"

#include <QDir>
#include <QFontDatabase>
#include <QMainWindow>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>

namespace {

/// 平时由 ToolboxController 灌的几张表；这里灌一份样例，版式才接近真实界面。
void fillSamples(ToolboxPage &page) {
    page.setEnchantmentChoices({{QStringLiteral("protection"), QStringLiteral("保护（Protection，上限 4 级）")},
                                {QStringLiteral("unbreaking"), QStringLiteral("耐久（Unbreaking，上限 3 级）")},
                                {QStringLiteral("mending"), QStringLiteral("经验修补（Mending，上限 1 级）")},
                                {QStringLiteral("thorns"), QStringLiteral("荆棘（Thorns，上限 3 级）")}});
    QList<ToolboxPage::PaletteEntry> palette;
    const QStringList colors{QStringLiteral("#000000"), QStringLiteral("#0000aa"), QStringLiteral("#00aa00"),
                             QStringLiteral("#00aaaa"), QStringLiteral("#aa0000"), QStringLiteral("#aa00aa"),
                             QStringLiteral("#ffaa00"), QStringLiteral("#aaaaaa"), QStringLiteral("#555555"),
                             QStringLiteral("#5555ff"), QStringLiteral("#55ff55"), QStringLiteral("#55ffff"),
                             QStringLiteral("#ff5555"), QStringLiteral("#ff55ff"), QStringLiteral("#ffff55"),
                             QStringLiteral("#ffffff")};
    const QString codes = QStringLiteral("0123456789abcdef");
    for (int i = 0; i < 16; ++i) palette.append({codes.mid(i, 1), QStringLiteral("颜色"), colors.at(i), true});
    for (const auto &pair : {qMakePair(QStringLiteral("k"), QStringLiteral("乱码")), qMakePair(QStringLiteral("l"), QStringLiteral("粗体")),
                             qMakePair(QStringLiteral("m"), QStringLiteral("删除线")), qMakePair(QStringLiteral("n"), QStringLiteral("下划线")),
                             qMakePair(QStringLiteral("o"), QStringLiteral("斜体")), qMakePair(QStringLiteral("r"), QStringLiteral("复位"))})
        palette.append({pair.first, pair.second, QString(), false});
    page.setTextPalette(palette);
    page.setTextSource(QStringLiteral("欢迎来到 §b服务器§r！"));
    page.setTextPreview(QStringLiteral("欢迎来到 <span style=\"color:#55ffff\">服务器</span>！"));
    page.setTextOutputs(QStringLiteral("欢迎来到 §b服务器§r！"),
                        QStringLiteral("{\"text\":\"欢迎来到 \",\"extra\":[{\"text\":\"服务器\",\"color\":\"aqua\"},{\"text\":\"！\",\"color\":\"white\"}]}"),
                        QStringLiteral("/tellraw @a {\"text\":\"欢迎来到 \",\"extra\":[{\"text\":\"服务器\",\"color\":\"aqua\"},{\"text\":\"！\",\"color\":\"white\"}]}"));
    page.setDimensionChoices({QStringLiteral("主世界"), QStringLiteral("下界"), QStringLiteral("末地")});
    page.setAlgorithmChoices({QStringLiteral("SHA-256"), QStringLiteral("SHA-1"), QStringLiteral("MD5")});
    page.setBeaconMaterials({QStringLiteral("铁块"), QStringLiteral("金块"), QStringLiteral("钻石块"),
                             QStringLiteral("绿宝石块"), QStringLiteral("下界合金块")});
    page.setBrewingPotions({QStringLiteral("迅捷药水"), QStringLiteral("力量药水"), QStringLiteral("治疗药水")});
    page.setBrewingForms({QStringLiteral("普通"), QStringLiteral("喷溅型"), QStringLiteral("滞留型")});
    page.setBrewingVersions({QString(), QStringLiteral("1.21"), QStringLiteral("1.20")});
    page.setBannerColors({QStringLiteral("白色"), QStringLiteral("橙色"), QStringLiteral("品红色"), QStringLiteral("黑色")},
                         {QColor(0xf9fffe), QColor(0xf9801d), QColor(0xc74ebd), QColor(0x1d1d21)});
    page.setBannerPatterns({QStringLiteral("条纹"), QStringLiteral("十字"), QStringLiteral("边框")});
    page.setBannerVersions({QString(), QStringLiteral("1.21")});
    page.setTrimArmorMaterials({QStringLiteral("下界合金"), QStringLiteral("钻石"), QStringLiteral("铁")});
    page.setTrimParts({QStringLiteral("头盔"), QStringLiteral("胸甲"), QStringLiteral("护腿"), QStringLiteral("靴子")});
    page.setTrimPatterns({QStringLiteral("海岸"), QStringLiteral("哨兵"), QStringLiteral("尖塔")});
    page.setTrimMaterials({QStringLiteral("紫水晶"), QStringLiteral("金"), QStringLiteral("红石")});
    page.setTrimVersions({QString(), QStringLiteral("1.21")});
    page.setBeamColors({QStringLiteral("白色"), QStringLiteral("橙色"), QStringLiteral("蓝色")});
    page.setAnvilRows({{QStringLiteral("protection"), 4, false, 1, false},
                       {QStringLiteral("unbreaking"), 3, true, 1, true},
                       {QStringLiteral("mending"), 1, true, 0, false},
                       {QStringLiteral("thorns"), 3, true, 0, false}});
    ToolboxPage::AnvilView view;
    view.ok = true;
    view.summary = QStringLiteral("合计 14 级（每一步都在 39 级以内），合并后为 保护 IV、耐久 III。");
    view.steps = {QStringLiteral("第 1 步：左槽 经验修补的书 + 右槽 荆棘 III 的书 → 经验修补、荆棘 III 的书（代价 6 级）"),
                  QStringLiteral("第 2 步：左槽 胸甲 + 右槽 第 1 步的合并产物 → 胸甲（代价 8 级）")};
    page.setAnvilView(view);
    ToolboxPage::HashView hash;
    hash.ok = true;
    hash.hex = QStringLiteral("a3f9c1d4e5b6a7980123456789abcdef0123456789abcdef0123456789abcdef");
    hash.sizeText = QStringLiteral("12.4 MB");
    hash.algorithm = QStringLiteral("SHA-256");
    page.setHashView(hash);
    ToolboxPage::ServerView server;
    server.ok = true;
    server.address = QStringLiteral("mc.example.com:25565");
    server.motd = QStringLiteral("欢迎来到服务器");
    server.version = QStringLiteral("Paper 1.21.4");
    server.players = QStringLiteral("128 / 500");
    server.latency = QStringLiteral("42 ms");
    page.setServerAddress(server.address);
    page.setServerView(server);
    page.setCoordinateRows({QStringLiteral("下界：x 10.00 y 64.00 z -9.50"), QStringLiteral("区块 (0, -1) 一带"),
                            QStringLiteral("region 文件 r.0.-1.mca")});
    page.setBeaconResult(QStringLiteral(
        "4 级信标：范围 50 格，底座共 164 块。\n• 第 4 层：9 × 9 = 81 块（最上面那层固定 3 × 3）\n"
        "• 效果：速度、急迫、抗性提升、跳跃提升、力量、再生\n• 信标放在金字塔正中间，上方留出天空（可用玻璃）\n\n"
        "164 块铁块 = 1476 个铁锭（每块 9 个）\n每颗信标另需 3 块黑曜石 + 1 颗下界之星 + 5 块玻璃（合成表：底下 3 块黑曜石、"
        "中间一颗下界之星、四周 5 块玻璃）。"));
    page.setBeamLayers({0, 1, 2, 1, 0});
    page.setBeamResult({QColor(255, 255, 255), QColor(255, 140, 0), QColor(0, 0, 255), QColor(255, 140, 0), QColor(255, 255, 255)},
                       QStringLiteral("第 1 层 / 白色玻璃 → 白\n第 2 层 / 橙色玻璃 → 橙\n第 3 层 / 蓝色玻璃 → 蓝\n"
                                      "第 4 层 / 橙色玻璃 → 橙\n第 5 层 / 白色玻璃 → 白\n最终颜色：#c9c9c9 / RGB(201, 201, 201)\n"
                                      "材料：白色玻璃 × 2、橙色玻璃 × 2、蓝色玻璃 × 1"));
    page.setBrewingResult(QStringLiteral("1 步：水瓶 + 下界疣 → 粗制\n2 步：粗制 + 糖 → 迅捷"));
    page.setBannerDesign(0, {QStringLiteral("0:1"), QStringLiteral("1:2"), QStringLiteral("2:3"), QStringLiteral("0:0"),
                             QStringLiteral("1:1"), QStringLiteral("2:2")}, QString());
    page.setBannerResult(QStringLiteral(
        "底色 白色 / 6 层 / 6\n\n一步一步：\n• 先做一面白色旗帜：6 块白色羊毛 + 1 根木棍\n• 第 1 层：条纹（橙色）\n• 第 2 层：十字（品红色）\n"
        "• 第 3 层：边框（黑色）\n• 第 4 层：条纹（白色）\n• 第 5 层：十字（橙色）\n• 第 6 层：边框（品红色）\n\n"
        "材料清单：\n• 白色羊毛 × 6\n• 木棍 × 1\n• 橙色染料 × 2\n• 品红色染料 × 2\n• 黑色染料 × 1\n\n"
        "指令（1.13–1.20.4 的 NBT 写法；1.20.5 起换成了组件写法，这一版不出）：\n"
        "/give @p white_banner{BlockEntityTag:{Patterns:[{Pattern:ss,Color:2},{Pattern:cr,Color:6},{Pattern:bo,Color:15}]}}"));
    page.setTrimResult(QStringLiteral("要准备：海岸锻造模板 × 2、紫水晶 × 4"));
}

/// 页里所有可见子控件都落在 @p panel 的宽度之内（没有被右侧裁掉）。
bool noChildClipped(QWidget *panel, QString *why) {
    for (QWidget *child : panel->findChildren<QWidget *>()) {
        if (!child->isVisibleTo(panel) || child->width() <= 0) continue;
        if (child->objectName().startsWith(QStringLiteral("qt_"))) continue;
        const QPoint topLeft = child->mapTo(panel, QPoint(0, 0));
        if (topLeft.x() + child->width() > panel->width() + 1 || topLeft.x() < -1) {
            if (why != nullptr)
                *why = QStringLiteral("%1/%2 超出 %3 > %4").arg(child->metaObject()->className(), child->objectName())
                           .arg(topLeft.x() + child->width()).arg(panel->width());
            return false;
        }
    }
    return true;
}

}  // namespace

int UiToolboxLayoutCheck(QApplication &app) {
    Anim::setEnabled(false);
    QTemporaryDir settingsDir(QDir::current().filePath(QStringLiteral(".toolbox-layout-XXXXXX")));
    if (!settingsDir.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    app.setOrganizationName(QStringLiteral("WTFMLToolboxLayout"));
    app.setApplicationName(QStringLiteral("ToolboxLayout"));
    app.setStyle(Theme::createStyle());
    const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
    const int fontId = QFile::exists(fontPath) ? QFontDatabase::addApplicationFont(fontPath) : -1;
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    app.setFont(QFont(families.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : families.first(), 9));

    const QString shotDir = qEnvironmentVariable("WTFML_TOOLBOX_SHOTS");
    if (!shotDir.isEmpty()) QDir().mkpath(shotDir);
    int saved = 0;
    const auto pump = [&app] {
        app.processEvents();
        app.processEvents();
    };

    for (const QString &id : {QStringLiteral("dark"), QStringLiteral("emerald")}) {
        Theme::setSpec(Theme::presetSpec(id));
        Theme::apply(app);
        QMainWindow window;
        MainWindowUI ui(&window);
        window.show();
        QTabWidget *tabs = ui.getTabWidget();
        check(tabs->count() == 6 && MainWindowUI::toolboxTabIndex() == 5 &&
                  tabs->tabText(MainWindowUI::toolboxTabIndex()) == QStringLiteral("工具箱") &&
                  tabs->widget(MainWindowUI::toolboxTabIndex())->objectName() == MainWindowUI::toolboxTabName(),
              "工具箱是第六个选项卡");
        check(ui.titleBar()->navCount() == 5, "顶上导航仍是五颗，工具箱不上导航", QString::number(ui.titleBar()->navCount()));
        QWidget *host = ui.toolboxTab();
        auto *page = new ToolboxPage(host);
        host->layout()->addWidget(page);
        fillSamples(*page);
        check(ui.getToolboxButton() != nullptr && !ui.getToolboxButton()->isChecked(), "不在工具箱页时导航轨底部那颗不高亮");
        ui.openToolboxTab();  // 没装 handler：只切选项卡
        check(tabs->currentIndex() == MainWindowUI::toolboxTabIndex() && ui.getToolboxButton()->isChecked(),
              "点「工具箱」直接进工具箱页并高亮，没有弹出框");
        check(window.findChild<QFrame *>(QStringLiteral("toolboxPanel")) == nullptr &&
                  window.findChild<QPushButton *>(QStringLiteral("toolboxExpandButton")) == nullptr,
              "没有工具箱弹出框，也没有「全部展开为页面」");

        for (const QSize size : {QSize(900, 620), QSize(1280, 800)}) {
            window.resize(size);
            pump();
            const QString sizeText = QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
            const auto shot = [&](const QString &name) {
                pump();
                if (shotDir.isEmpty()) return;
                const QString file = QDir(shotDir).filePath(QStringLiteral("toolbox-%1-%2-%3.png").arg(name, id, sizeText));
                if (window.grab().save(file)) ++saved;
            };

            // ---- 目录页 ----
            page->showCatalog();
            pump();
            check(page->inCatalog(), "目录页：停在目录");
            int cells = 0;
            QList<int> xs;
            for (ToolboxPage::Tool tool : ToolboxPage::tools()) {
                auto *cell = page->findChild<QPushButton *>(ToolboxPage::toolCellName(tool));
                if (cell != nullptr && cell->isVisible()) {
                    ++cells;
                    if (!xs.contains(cell->mapTo(page, QPoint(0, 0)).x())) xs.append(cell->mapTo(page, QPoint(0, 0)).x());
                    check(cell->width() >= 240 - 1 || xs.size() == 1, "目录格不窄于 240", QString::number(cell->width()));
                }
            }
            check(cells == 10, "目录页：十个工具一格不少", QString::number(cells));
            check(xs.size() >= 2 && xs.size() <= 4, "目录页：自适应成多列", QString::number(xs.size()));
            check(page->findChild<QCheckBox *>(QStringLiteral("toolboxRememberInputs")) != nullptr &&
                      page->findChild<QCheckBox *>(QStringLiteral("toolboxRememberInputs"))->isVisible(),
                  "目录页右上有「记住输入」");
            check(!page->findChild<QPushButton *>(ToolboxPage::crumbRootName())->isVisible(), "目录页没有面包屑");
            shot(QStringLiteral("cat"));

            // ---- 十个工具页 ----
            for (ToolboxPage::Tool tool : ToolboxPage::tools()) {
                auto *cell = page->findChild<QPushButton *>(ToolboxPage::toolCellName(tool));
                if (cell != nullptr) cell->click();
                else page->showTool(tool);
                pump();
                const QString key = ToolboxPage::toolKey(tool);
                check(!page->inCatalog() && page->currentTool() == tool, "点目录格进工具页", key);
                auto *crumbTool = page->findChild<QPushButton *>(ToolboxPage::crumbToolName());
                auto *crumbRoot = page->findChild<QPushButton *>(ToolboxPage::crumbRootName());
                check(crumbTool != nullptr && crumbTool->text() == ToolboxPage::toolName(tool) && crumbRoot != nullptr &&
                          crumbRoot->isVisible(),
                      "面包屑「工具箱 / 工具名」", key);
                // 面包屑文字（按钮左内边距 8）与工具内容左缘对齐。
                if (crumbRoot != nullptr && crumbRoot->isVisible()) {
                    auto *scrollArea = page->findChild<QScrollArea *>(ToolboxPage::toolPageName(tool));
                    QWidget *content = page->findChild<QWidget *>(ToolboxPage::toolPanelName(tool));
                    if (scrollArea != nullptr && content != nullptr) {
                        const int textLeft = crumbRoot->mapTo(page, QPoint(0, 0)).x() + 8;
                        const int contentLeft = content->mapTo(page, QPoint(0, 0)).x();
                        check(qAbs(textLeft - contentLeft) <= 1, "面包屑文字与内容左缘对齐", QStringLiteral("%1 vs %2 %3 | crumbRoot.x=%4 head.x=%5 inner.x=%6").arg(textLeft).arg(contentLeft).arg(key).arg(crumbRoot->mapTo(page, QPoint(0,0)).x()).arg(crumbRoot->parentWidget()->mapTo(page, QPoint(0,0)).x()).arg(crumbRoot->parentWidget()->parentWidget()->mapTo(page, QPoint(0,0)).x()));
                    }
                }
                auto *scroll = page->findChild<QScrollArea *>(ToolboxPage::toolPageName(tool));
                check(scroll != nullptr && scroll->isVisible(), "工具页可见", key);
                if (scroll != nullptr) {
                    QWidget *panel = page->findChild<QWidget *>(ToolboxPage::toolPanelName(tool));
                    QString why;
                    check(scroll->horizontalScrollBar()->maximum() == 0, QStringLiteral("%1 %2：没有横向滚动范围").arg(key, sizeText),
                          QString::number(scroll->horizontalScrollBar()->maximum()));
                    check(panel != nullptr && panel->minimumSizeHint().width() <= scroll->viewport()->width(),
                          QStringLiteral("%1 %2：内容最小宽 ≤ 视口宽").arg(key, sizeText),
                          QStringLiteral("%1 > %2").arg(panel ? panel->minimumSizeHint().width() : -1).arg(scroll->viewport()->width()));
                    check(panel != nullptr && noChildClipped(panel, &why),
                          QStringLiteral("%1 %2：没有控件被右侧裁掉").arg(key, sizeText), why);
                    // 信标 / 彩色文本 / 旗帜：最小窗口下整页放得下，不出纵向滚动条。
                    if (size == QSize(900, 620) && (tool == ToolboxPage::Tool::Beacon || tool == ToolboxPage::Tool::Text ||
                                                    tool == ToolboxPage::Tool::Banner)) {
                        check(scroll->verticalScrollBar()->maximum() == 0,
                              QStringLiteral("%1 %2：没有纵向滚动范围").arg(key, sizeText),
                              QStringLiteral("max=%1 viewport=%2 content=%3").arg(scroll->verticalScrollBar()->maximum())
                                  .arg(scroll->viewport()->height()).arg(scroll->widget()->height()));
                    }
                }
                shot(key);
            }

            // ---- 附魔页：四个框统一高、宽窗口一行 ----
            page->showTool(ToolboxPage::Tool::Anvil);
            pump();
            QList<QComboBox *> combos = page->findChild<QWidget *>(QStringLiteral("anvilRows"))->findChildren<QComboBox *>();
            QList<QSpinBox *> spins = page->findChild<QWidget *>(QStringLiteral("anvilRows"))->findChildren<QSpinBox *>();
            combos.removeIf([](QComboBox *c) { return !c->isVisible(); });  // 刚被替换掉、等着删的旧行
            spins.removeIf([](QSpinBox *c) { return !c->isVisible(); });
            bool sameHeight = !combos.isEmpty() && !spins.isEmpty();
            for (QWidget *w : QList<QWidget *>(combos.cbegin(), combos.cend()) + QList<QWidget *>(spins.cbegin(), spins.cend()))
                sameHeight = sameHeight && w->height() == Theme::inputHeight();
            check(sameHeight, "附魔行：附魔 / 等级 / 物品 / 已用次数四个框统一高 36", sizeText);
            const bool oneLine = !combos.isEmpty() && combos.first()->mapTo(page, QPoint(0, 0)).y() == spins.first()->mapTo(page, QPoint(0, 0)).y();
            check(oneLine == (size.width() >= 900), "附魔行：宽窗口一行排下", sizeText);

            // ---- 切换浮层 ----
            auto *crumbTool = page->findChild<QPushButton *>(ToolboxPage::crumbToolName());
            crumbTool->click();
            pump();
            QWidget *popover = window.findChild<QWidget *>(ToolboxPage::switchPopoverName());
            check(popover != nullptr && popover->isVisible() && popover->width() == 236, "点工具名弹出切换浮层（宽 236）", sizeText);
            check(popover != nullptr && popover->findChild<QListWidget *>() != nullptr &&
                      popover->findChild<QListWidget *>()->count() == 10,
                  "切换浮层列出十个工具");
            shot(QStringLiteral("switch"));
            if (auto *list = popover != nullptr ? popover->findChild<QListWidget *>() : nullptr) {
                list->itemClicked(list->item(1));
                pump();
                check(page->currentTool() == ToolboxPage::tools().at(1) && !page->inCatalog() &&
                          window.findChild<QWidget *>(ToolboxPage::switchPopoverName()) == nullptr,
                      "点浮层里的工具直接切过去并收起浮层");
            }
            page->findChild<QPushButton *>(ToolboxPage::crumbRootName())->click();
            pump();
            check(page->inCatalog(), "点面包屑「工具箱」回到目录");
        }
    }

    // ---- 窄内容宽（独立页，内容宽 504 < 640）：重排而不是出滚动条 ----
    {
        Theme::setSpec(Theme::presetSpec(QStringLiteral("dark")));
        Theme::apply(app);
        for (const int width : {560, 700, 836, 1216}) {
            ToolboxPage narrow;
            fillSamples(narrow);
            narrow.resize(width, 620);
            narrow.show();
            pump();
            for (ToolboxPage::Tool tool : ToolboxPage::tools()) {
                narrow.showTool(tool);
                pump();
                auto *scroll = narrow.findChild<QScrollArea *>(ToolboxPage::toolPageName(tool));
                QWidget *panel = narrow.findChild<QWidget *>(ToolboxPage::toolPanelName(tool));
                QString why;
                const QString label = QStringLiteral("%1 @%2").arg(ToolboxPage::toolKey(tool)).arg(width);
                check(scroll != nullptr && scroll->horizontalScrollBar()->maximum() == 0,
                      QStringLiteral("窄宽 %1：没有横向滚动范围").arg(label),
                      QStringLiteral("max=%1 panelMin=%2 viewport=%3").arg(scroll != nullptr ? scroll->horizontalScrollBar()->maximum() : -1)
                          .arg(panel != nullptr ? panel->minimumSizeHint().width() : -1)
                          .arg(scroll != nullptr ? scroll->viewport()->width() : -1));
                check(panel != nullptr && noChildClipped(panel, &why),
                      QStringLiteral("窄宽 %1：没有控件被裁").arg(label), why);
                if (!shotDir.isEmpty() && (width == 560 || width == 700) && narrow.grab().save(QDir(shotDir).filePath(
                        QStringLiteral("toolbox-narrow-%1-dark-%2x620.png").arg(ToolboxPage::toolKey(tool)).arg(width)))) ++saved;
            }
            narrow.showTool(ToolboxPage::Tool::Anvil);
            pump();
            auto *rows = narrow.findChild<QWidget *>(QStringLiteral("anvilRows"));
            auto combos = rows->findChildren<QComboBox *>();
            auto spins = rows->findChildren<QSpinBox *>();
            combos.removeIf([](QComboBox *c) { return !c->isVisible(); });
            spins.removeIf([](QSpinBox *c) { return !c->isVisible(); });
            const bool oneLine = combos.first()->mapTo(&narrow, QPoint(0, 0)).y() == spins.first()->mapTo(&narrow, QPoint(0, 0)).y();
            check(oneLine == (rows->width() >= 640), QStringLiteral("附魔行折行阈值 640：内容宽 %1").arg(rows->width()));
            if (rows->width() < 640) {
                check(combos.first()->width() >= 200, "窄时附魔框独占一行并 ≥ 200");
            }
        }
    }
    if (!shotDir.isEmpty()) std::printf("saved %d shots to %s\n", saved, qPrintable(shotDir));
    return 0;
}
