// 走查验收截图（offscreen，QWidget::grab()）。
//
// 默认这一趟（不带参数）保持原来的行为：新安装默认 + 三个具名预设 ×
// 主页零实例 / 管理页空状态 / 设置·启动 / 设置·下载 / 下载·游戏，窗口 1001×776。
//
// 带 `--extended` 时出一整套「默认窗口尺寸」参考图：指定一个主题和一个窗口尺寸，
// 按固定页面顺序把 29 张图拍下来（文件名为 <页面>-<状态>.png）：
//
//   QT_QPA_PLATFORM=offscreen ui_check_review_shots --extended \
//       --theme dark --size 1280x800 --out D:/shots/dark-1280
//
// 尺寸只影响主窗口（离屏、DPR=1）；实例设置对话框是产品里那个 820×560 的浮层，
// 这里照真实样子嵌在覆盖层里拍，不跟着窗口拉伸。
#include "Support.hpp"

#include "AgentPage.hpp"
#include "AgentViewTypes.hpp"
#include "OverlayHost.hpp"
#include "ToolboxPage.hpp"
#include "VersionIcons.hpp"

#include <QDir>
#include <QFontDatabase>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTreeWidget>

namespace {

struct ShotOptions {
    QString out;
    QString theme;
    QSize size{0, 0};
    bool extended = false;
};

/// 颜色块 + 一个字母的小图标（资源行 / 账号行用；产品里那些是真图，这里只要不像空白）。
QPixmap letterMark(const QColor &color, const QString &letter, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(0, 0, size, size), size * 0.24, size * 0.24);
    QFont font = QApplication::font();
    font.setPixelSize(qMax(9, size / 2));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 235));
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, letter.left(1).toUpper());
    return pixmap;
}

/// 账号弹层里那张"脸"：圆底 + 首字母。
QPixmap faceMark(const QColor &color, const QString &letter, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRectF(0, 0, size, size));
    QFont font = QApplication::font();
    font.setPixelSize(qMax(9, size / 2));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 235));
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, letter.left(1).toUpper());
    return pixmap;
}

/// 设置页里那些下拉框 / 勾选框 / 数值框，平时由 MainWindow 灌；截图前灌一份样例。
void fillSettingsSamples(MainWindowUI &ui) {
    const auto fill = [](QComboBox *combo, const QStringList &items, int current = 0) {
        if (combo == nullptr) return;
        combo->clear();
        combo->addItems(items);
        combo->setCurrentIndex(current);
    };
    fill(ui.getGameWindowModeCombo(), {QStringLiteral("窗口化"), QStringLiteral("最大化"), QStringLiteral("全屏")});
    fill(ui.getLauncherBehaviourCombo(), {QStringLiteral("收起启动器"), QStringLiteral("保持打开"), QStringLiteral("退出启动器")});
    fill(ui.getProcessPriorityCombo(), {QStringLiteral("正常"), QStringLiteral("较高"), QStringLiteral("较低")});
    fill(ui.getDownloadSourceCombo(), {QStringLiteral("优先官方源"), QStringLiteral("优先镜像"), QStringLiteral("仅官方")});
    fill(ui.getDownloadConcurrencyCombo(), {QStringLiteral("8 个"), QStringLiteral("16 个"), QStringLiteral("32 个")});
    fill(ui.getDownloadSpeedLimitCombo(), {QStringLiteral("不限速"), QStringLiteral("10 MB/s"), QStringLiteral("5 MB/s")});
    fill(ui.getDownloadProxyCombo(), {QStringLiteral("跟随系统"), QStringLiteral("直连"), QStringLiteral("手动")});
    fill(ui.getMemoryModeCombo(), {QStringLiteral("自动分配"), QStringLiteral("手动分配")}, 1);
    if (ui.getJavaPathLabel() != nullptr) ui.getJavaPathLabel()->setText(QStringLiteral("自动选择　按游戏版本"));
    if (ui.getMemoryHint() != nullptr)
        ui.getMemoryHint()->setText(QStringLiteral("系统总内存 31.5 GB、当前可用 14.5 GB；现在最多给到 4096 MB。"));
    if (ui.getMemoryMaxSpin() != nullptr) ui.getMemoryMaxSpin()->setValue(4096);
    if (ui.getMemoryMaxSlider() != nullptr) ui.getMemoryMaxSlider()->setValueMb(4096);
    if (ui.getGameWindowWidthSpin() != nullptr) ui.getGameWindowWidthSpin()->setValue(1280);
    if (ui.getGameWindowHeightSpin() != nullptr) ui.getGameWindowHeightSpin()->setValue(720);
    if (ui.getShowGameLogCheck() != nullptr) ui.getShowGameLogCheck()->setChecked(true);
    if (ui.getWarnDuplicateLaunchCheck() != nullptr) ui.getWarnDuplicateLaunchCheck()->setChecked(true);
    if (ui.getWarnWorldLossCheck() != nullptr) ui.getWarnWorldLossCheck()->setChecked(true);
    if (ui.getJavaWarnMismatchCheck() != nullptr) ui.getJavaWarnMismatchCheck()->setChecked(true);
    if (ui.getWarnMissingDependenciesCheck() != nullptr) ui.getWarnMissingDependenciesCheck()->setChecked(true);
    if (ui.getDownloadAutoIntoInstanceCheck() != nullptr) ui.getDownloadAutoIntoInstanceCheck()->setChecked(true);
    if (ui.getAnimationsCheck() != nullptr) ui.getAnimationsCheck()->setChecked(true);
    if (ui.getHomeFavoriteCountSpin() != nullptr) ui.getHomeFavoriteCountSpin()->setValue(3);
    if (ui.getHomeRecentCountSpin() != nullptr) ui.getHomeRecentCountSpin()->setValue(3);
}

/// 主页那几排快捷方式：4 个"当前"实例 + 2 个收藏 + 2 个最近，含一个带加载器的。
void fillHomeSamples(MainWindowUI &ui) {
    const auto item = [](const QString &key, const QString &name, const QString &detail, bool current,
                         bool favorite, bool recent) {
        MainWindowUI::HomeQuickItem row;
        row.key = key;
        row.name = name;
        row.detail = detail;
        row.icon = QIcon(VersionIcons::isoBlock(VersionIcons::isoBlockKindFor(name),
                                                MainWindowUI::instanceTreeIconSize() * 2));
        row.current = current;
        row.favorite = favorite;
        row.recent = recent;
        return row;
    };
    const QList<MainWindowUI::HomeQuickItem> current{
        item(QStringLiteral("1.20.1-Forge"), QStringLiteral("1.20.1-Forge 47.2.0"),
             QStringLiteral("Minecraft 1.20.1 / Forge 47.2.0"), true, false, true),
        item(QStringLiteral("1.21.4-Fabric"), QStringLiteral("1.21.4-Fabric 0.16.9"),
             QStringLiteral("Minecraft 1.21.4 / Fabric 0.16.9"), true, true, true),
        item(QStringLiteral("1.20.4"), QStringLiteral("1.20.4 原版"),
             QStringLiteral("Minecraft 1.20.4 / 原版"), true, false, false),
        item(QStringLiteral("1.19.2-NeoForge"), QStringLiteral("1.19.2-NeoForge 20.4.237"),
             QStringLiteral("Minecraft 1.19.2 / NeoForge 20.4.237"), true, true, false)};
    const QList<MainWindowUI::HomeQuickItem> favorites{
        current.at(1), current.at(3)};
    const QList<MainWindowUI::HomeQuickItem> recent{current.at(0), current.at(1)};
    ui.setHomeQuickRows(current, favorites, recent);
    ui.setHomeQuickMode(MainWindowUI::HomeQuickMode::Current);
}

/// 实例页那张树：两个组、5 个实例（4 个带加载器）。行形状照 MainWindowViewData::fillInstanceTree()。
void fillInstanceTree(MainWindowUI &ui) {
    QTreeWidget *tree = ui.getInstanceList();
    if (tree == nullptr) return;
    tree->clear();
    const auto addRow = [&](QTreeWidgetItem *parent, const QString &text, const QString &id, int loader) {
        auto *row = new QTreeWidgetItem(parent);
        row->setText(0, text);
        row->setData(0, MainWindowUI::instanceIdRole(), id);
        row->setToolTip(0, QStringLiteral("D:/Games/.minecraft/versions/%1").arg(id));
        row->setIcon(0, QIcon(VersionIcons::loaderMark(loader, MainWindowUI::instanceTreeIconSize())));
        row->setData(0, MainWindowUI::rowPinRole(), true);
        return row;
    };
    const auto addGroup = [&](const QString &name, const QString &id) {
        auto *group = new QTreeWidgetItem(tree);
        group->setText(0, MainWindowUI::groupItemText(name, 0));
        group->setData(0, MainWindowUI::groupIdRole(), id);
        group->setData(0, MainWindowUI::rowGroupGearRole(), true);
        group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
        group->setExpanded(true);
        return group;
    };
    QTreeWidgetItem *modded = addGroup(QStringLiteral("1.20.1 整合包"), QStringLiteral("g-modded"));
    addRow(modded, QStringLiteral("1.20.1-Forge 47.2.0"), QStringLiteral("1.20.1-Forge"), 1);
    addRow(modded, QStringLiteral("1.20.1-Fabric 0.15.11"), QStringLiteral("1.20.1-Fabric"), 0);
    modded->setText(0, MainWindowUI::groupItemText(QStringLiteral("1.20.1 整合包"), modded->childCount()));
    QTreeWidgetItem *other = addGroup(QStringLiteral("其他"), QStringLiteral("g-other"));
    addRow(other, QStringLiteral("1.21.4-Fabric 0.16.9"), QStringLiteral("1.21.4-Fabric"), 0);
    addRow(other, QStringLiteral("1.19.2-NeoForge 20.4.237"), QStringLiteral("1.19.2-NeoForge"), 2);
    addRow(other, QStringLiteral("1.20.4 原版"), QStringLiteral("1.20.4"), 4);
    other->setText(0, MainWindowUI::groupItemText(QStringLiteral("其他"), other->childCount()));
    if (other->childCount() > 0) tree->setCurrentItem(other->child(0));
}

/// 下载·游戏：最新版本 + 四栏，正式版展开（照 MainWindow::refreshAvailableVersions() 的行形状）。
void fillVersionTree(MainWindowUI &ui) {
    QTreeWidget *tree = ui.getVersionTree();
    if (tree == nullptr) return;
    tree->clear();
    const int column = MainWindowUI::versionTreeKeyColumn();
    const auto addLeaf = [&](QTreeWidgetItem *parent, const QString &id, const QString &date, int channel) {
        auto *leaf = new QTreeWidgetItem(parent);
        leaf->setData(column, Qt::UserRole, id);
        leaf->setText(column, id);
        leaf->setData(column, MainWindowUI::rowSubTextRole(), date);
        leaf->setData(column, MainWindowUI::rowIconRole(),
                      VersionIcons::versionChannelMark(channel, MainWindowUI::versionTreeIconSize()));
        return leaf;
    };
    auto *latest = new QTreeWidgetItem(tree);
    latest->setData(column, Qt::UserRole, QStringLiteral("latest"));
    latest->setText(column, QStringLiteral("最新版本"));
    addLeaf(latest, QStringLiteral("1.21.4"), QStringLiteral("2025-12-09"), 0);
    addLeaf(latest, QStringLiteral("25w50a"), QStringLiteral("2025-12-11"), 1);
    latest->setExpanded(true);

    const QList<QPair<QString, QString>> channels{
        {QStringLiteral("release"), QStringLiteral("正式版")},
        {QStringLiteral("snapshot"), QStringLiteral("快照版")},
        {QStringLiteral("old"), QStringLiteral("远古版")},
        {QStringLiteral("april"), QStringLiteral("愚人节版")}};
    const QList<QStringList> ids{
        {QStringLiteral("1.21.4"), QStringLiteral("1.21.3"), QStringLiteral("1.21.2"),
         QStringLiteral("1.21.1"), QStringLiteral("1.21"), QStringLiteral("1.20.6"),
         QStringLiteral("1.20.4"), QStringLiteral("1.20.1"), QStringLiteral("1.19.2"),
         QStringLiteral("1.18.2"), QStringLiteral("1.16.5"), QStringLiteral("1.12.2")},
        {QStringLiteral("25w50a"), QStringLiteral("25w49a"), QStringLiteral("25w45a")},
        {QStringLiteral("b1.7.3"), QStringLiteral("a1.2.6")},
        {QStringLiteral("15w14a"), QStringLiteral("3D Shareware v1.34")}};
    for (int c = 0; c < channels.size(); ++c) {
        auto *top = new QTreeWidgetItem(tree);
        top->setData(column, Qt::UserRole, channels.at(c).first);
        top->setText(column, channels.at(c).second);
        top->setData(column, MainWindowUI::rowIconRole(),
                     VersionIcons::versionChannelMark(c, MainWindowUI::versionTreeIconSize()));
        for (const QString &id : ids.at(c)) addLeaf(top, id, QStringLiteral("2025-12-09"), c);
        top->setExpanded(c == 0);  // 只要「正式版」那一组展开
    }
}

/// 下载·模组：一页资源结果（5 条），第二条底下展开两个游戏版本。
void fillResourceTree(MainWindowUI &ui, const QString &categoryKey) {
    QTreeWidget *tree = ui.getDownloadResourceTree(categoryKey);
    if (tree == nullptr) return;
    tree->clear();
    const int column = MainWindowUI::versionTreeKeyColumn();
    const QList<QPair<QString, QString>> hits{
        {QStringLiteral("Sodium"), QStringLiteral("jellysquid3 / 42.1M")},
        {QStringLiteral("Iris Shaders"), QStringLiteral("IrisShaders / 18.7M")},
        {QStringLiteral("Fabric API"), QStringLiteral("modmuss50 / 96.3M")},
        {QStringLiteral("Lithium"), QStringLiteral("jellysquid3 / 21.5M")},
        {QStringLiteral("JEI"), QStringLiteral("mezz / 88.0M")}};
    const QList<QColor> colors{QColor(0x4f, 0x8a, 0xd8), QColor(0x7a, 0x5c, 0xc9), QColor(0x3c, 0x9e, 0x7a),
                               QColor(0xc9, 0x7a, 0x3c), QColor(0x9e, 0x4b, 0x6a)};
    for (int i = 0; i < hits.size(); ++i) {
        auto *hit = new QTreeWidgetItem(tree);
        hit->setData(column, Qt::UserRole, QStringLiteral("project-%1").arg(i + 1));
        hit->setData(column, MainWindowUI::resourceKindRole(), categoryKey);
        hit->setData(column, MainWindowUI::resourceSourceRole(), QStringLiteral("modrinth"));
        hit->setText(column, hits.at(i).first);
        hit->setData(column, MainWindowUI::rowSubTextRole(), hits.at(i).second);
        hit->setData(column, MainWindowUI::rowIconRole(),
                     letterMark(colors.at(i), hits.at(i).first, MainWindowUI::resourceTreeIconSize()));
        if (i == 1) {
            for (const QString &version : {QStringLiteral("1.21.4"), QStringLiteral("1.20.1")}) {
                auto *game = new QTreeWidgetItem(hit);
                game->setData(column, Qt::UserRole, version);
                game->setText(column, version);
            }
            hit->setExpanded(true);
        }
    }
}

/// 工具箱十页的样例数据（照 ToolboxController 灌进来的那几张表；版式才接近真实界面）。
void fillToolboxSamples(ToolboxPage &page) {
    page.setEnchantmentChoices(
        {{QStringLiteral("protection"), QStringLiteral("保护（Protection，上限 4 级）")},
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
    for (const auto &pair : {qMakePair(QStringLiteral("k"), QStringLiteral("乱码")),
                             qMakePair(QStringLiteral("l"), QStringLiteral("粗体")),
                             qMakePair(QStringLiteral("m"), QStringLiteral("删除线")),
                             qMakePair(QStringLiteral("n"), QStringLiteral("下划线")),
                             qMakePair(QStringLiteral("o"), QStringLiteral("斜体")),
                             qMakePair(QStringLiteral("r"), QStringLiteral("复位"))})
        palette.append({pair.first, pair.second, QString(), false});
    page.setTextPalette(palette);
    page.setTextSource(QStringLiteral("欢迎来到 §b服务器§r！"));
    page.setTextPreview(QStringLiteral("欢迎来到 <span style=\"color:#55ffff\">服务器</span>！"));
    page.setTextOutputs(
        QStringLiteral("欢迎来到 §b服务器§r！"),
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
    page.setBannerPreview(QImage(), QStringLiteral("加载本地游戏贴图后可预览图案。"));
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
    page.setCoordinateRows({QStringLiteral("下界：x 10.00 y 64.00 z -9.50"),
                            QStringLiteral("区块 (0, -1) 一带"),
                            QStringLiteral("region 文件 r.0.-1.mca")});
    page.setBeaconResult(QStringLiteral(
        "4 级信标：范围 50 格，底座共 164 块。\n• 第 4 层：9 × 9 = 81 块（最上面那层固定 3 × 3）\n"
        "• 效果：速度、急迫、抗性提升、跳跃提升、力量、再生\n• 信标放在金字塔正中间，上方留出天空（可用玻璃）\n\n"
        "164 块铁块 = 1476 个铁锭（每块 9 个）\n每颗信标另需 3 块黑曜石 + 1 颗下界之星 + 5 块玻璃（合成表：底下 3 块黑曜石、"
        "中间一颗下界之星、四周 5 块玻璃）。"));
    page.setBeamLayers({0, 1, 2, 1, 0});
    page.setBeamResult({QColor(255, 255, 255), QColor(255, 140, 0), QColor(0, 0, 255), QColor(255, 140, 0),
                        QColor(255, 255, 255)},
                       QStringLiteral("第 1 层 / 白色玻璃 → 白\n第 2 层 / 橙色玻璃 → 橙\n第 3 层 / 蓝色玻璃 → 蓝\n"
                                      "第 4 层 / 橙色玻璃 → 橙\n第 5 层 / 白色玻璃 → 白\n最终颜色：#c9c9c9 / RGB(201, 201, 201)\n"
                                      "材料：白色玻璃 × 2、橙色玻璃 × 2、蓝色玻璃 × 1"));
    page.setBrewingResult(QStringLiteral("1 步：水瓶 + 下界疣 → 粗制\n2 步：粗制 + 糖 → 迅捷"));
    page.setBannerDesign(0,
                         {QStringLiteral("0:1"), QStringLiteral("1:2"), QStringLiteral("2:3"), QStringLiteral("0:0"),
                          QStringLiteral("1:1"), QStringLiteral("2:2")},
                         QString());
    page.setBannerResult(QStringLiteral(
        "底色 白色 / 6 层 / 6\n\n一步一步：\n• 先做一面白色旗帜：6 块白色羊毛 + 1 根木棍\n• 第 1 层：条纹（橙色）\n"
        "• 第 2 层：十字（品红色）\n• 第 3 层：边框（黑色）\n• 第 4 层：条纹（白色）\n• 第 5 层：十字（橙色）\n"
        "• 第 6 层：边框（品红色）\n\n材料清单：\n• 白色羊毛 × 6\n• 木棍 × 1\n• 橙色染料 × 2\n• 品红色染料 × 2\n"
        "• 黑色染料 × 1\n\n指令（1.13–1.20.4 的 NBT 写法；1.20.5 起换成了组件写法，这一版不出）：\n"
        "/give @p white_banner{BlockEntityTag:{Patterns:[{Pattern:ss,Color:2},{Pattern:cr,Color:6},{Pattern:bo,Color:15}]}}"));
    page.setTrimResult(QStringLiteral("要准备：海岸锻造模板 × 2、紫水晶 × 4"));
}

/// 实例设置对话框的夹具：一个装了点东西的实例目录（模组页要有内容）。
void makeInstanceFixture(const QString &root) {
    const auto mkfile = [](const QString &path) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) file.write(QByteArray(4096, 'x'));
    };
    mkfile(root + QStringLiteral("/mods/sodium-0.6.13.jar"));
    mkfile(root + QStringLiteral("/mods/iris-1.8.1.jar"));
    mkfile(root + QStringLiteral("/mods/lithium-0.14.3.jar"));
    mkfile(root + QStringLiteral("/mods/old-fabric-api.jar.disabled"));
    mkfile(root + QStringLiteral("/resourcepacks/材质.zip"));
    mkfile(root + QStringLiteral("/datapacks/数据.zip"));
    mkfile(root + QStringLiteral("/shaderpacks/光影.zip"));
    mkfile(root + QStringLiteral("/saves/我的世界/level.dat"));
    mkfile(root + QStringLiteral("/logs/latest.log"));
    mkfile(root + QStringLiteral("/screenshots/2026-11-12_20.31.07.png"));
    QDir().mkpath(root + QStringLiteral("/game"));
}

/// 账号弹层的两种状态。
QList<MainWindowUI::PanelRow> accountRows(bool signedIn) {
    QList<MainWindowUI::PanelRow> rows;
    if (!signedIn) {
        MainWindowUI::PanelRow empty;
        empty.placeholder = true;
        empty.title = QStringLiteral("未登录");
        rows.append(empty);
        MainWindowUI::PanelRow login;
        login.menuItem = true;
        login.title = QStringLiteral("登录");
        login.key = QStringLiteral("login");
        rows.append(login);
        return rows;
    }
    MainWindowUI::PanelRow steve;
    steve.key = QStringLiteral("steve");
    steve.title = QStringLiteral("Steve");
    steve.subtitle = QStringLiteral("正版");
    steve.icon = faceMark(QColor(0x3c, 0x9e, 0x7a), QStringLiteral("S"), 32);
    steve.checked = true;
    rows.append(steve);
    MainWindowUI::PanelRow alex;
    alex.key = QStringLiteral("alex");
    alex.title = QStringLiteral("Alex");
    alex.subtitle = QStringLiteral("离线");
    alex.icon = faceMark(QColor(0xc9, 0x7a, 0x3c), QStringLiteral("A"), 32);
    rows.append(alex);
    MainWindowUI::PanelRow add;
    add.key = QStringLiteral("add");
    add.title = QStringLiteral("添加账号");
    add.menuItem = true;
    rows.append(add);
    MainWindowUI::PanelRow manage;
    manage.key = QStringLiteral("manage");
    manage.title = QStringLiteral("管理账号与皮肤");
    manage.menuItem = true;
    rows.append(manage);
    return rows;
}

/// 活动中心那两三条任务（一条在跑、一条已完成、一条失败）。
QList<TaskCenter::Item> activityItems() {
    QList<TaskCenter::Item> items;
    TaskCenter::Item download;
    download.key = QStringLiteral("1");
    download.title = QStringLiteral("下载 Sodium 0.6.13");
    download.detail = QStringLiteral("已下载 12.4 MB / 30.1 MB");
    download.percent = 41;
    download.state = TaskCenter::State::Running;
    download.steps = {{QStringLiteral("解析依赖"), TaskCenter::StepState::Done, QString()},
                      {QStringLiteral("下载文件"), TaskCenter::StepState::Running, QStringLiteral("12.4 MB / 30.1 MB")},
                      {QStringLiteral("写入实例"), TaskCenter::StepState::Pending, QString()}};
    download.capabilities.cancel = true;
    download.capabilities.pause = true;
    items.append(download);
    TaskCenter::Item install;
    install.key = QStringLiteral("2");
    install.title = QStringLiteral("安装 1.21.4-Fabric");
    install.detail = QStringLiteral("26 个文件");
    install.percent = 100;
    install.state = TaskCenter::State::Succeeded;
    install.finishedAt = QDateTime::currentDateTime().addSecs(-120);
    install.steps = {{QStringLiteral("下载游戏文件"), TaskCenter::StepState::Done, QString()},
                     {QStringLiteral("安装加载器"), TaskCenter::StepState::Done, QString()}};
    install.capabilities.openTarget = true;
    items.append(install);
    TaskCenter::Item failed;
    failed.key = QStringLiteral("3");
    failed.title = QStringLiteral("更新 Iris Shaders");
    failed.detail = QStringLiteral("连接超时");
    failed.state = TaskCenter::State::Failed;
    failed.errorSummary = QStringLiteral("连接超时（已重试 2 次）");
    failed.finishedAt = QDateTime::currentDateTime().addSecs(-300);
    failed.capabilities.retry = true;
    failed.capabilities.openLog = true;
    items.append(failed);
    return items;
}

/// 把控件"现在长什么样"抓下来（逻辑像素，DPR=1）。
bool saveShot(QApplication &app, QMainWindow &window, const QString &outDir, const QString &name,
              int *saved) {
    app.processEvents();
    app.processEvents();
    const QString file = QDir(outDir).filePath(name + QStringLiteral(".png"));
    if (window.grab().save(file)) {
        ++(*saved);
        std::printf("  %s  %dx%d\n", qPrintable(name), window.width(), window.height());
        return true;
    }
    std::printf("  FAIL %s -> %s\n", qPrintable(name), qPrintable(file));
    return false;
}

int runExtended(QApplication &app, const ShotOptions &options) {
    Anim::setEnabled(false);
    QTemporaryDir settingsDir(QDir::current().filePath(QStringLiteral(".review-shots-XXXXXX")));
    if (!settingsDir.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    app.setOrganizationName(QStringLiteral("WTFMLReviewShots"));
    app.setApplicationName(QStringLiteral("ReviewShots"));
    app.setStyle(Theme::createStyle());
    const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
    const int fontId = QFile::exists(fontPath) ? QFontDatabase::addApplicationFont(fontPath) : -1;
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    app.setFont(QFont(families.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : families.first(), 9));

    Theme::setSpec(options.theme == QLatin1String("default") ? Theme::load() : Theme::presetSpec(options.theme));
    Theme::apply(app);

    QDir().mkpath(options.out);
    int saved = 0;
    const auto pump = [&app] {
        app.processEvents();
        app.processEvents();
    };

    QMainWindow window;
    MainWindowUI ui(&window);
    window.resize(options.size);
    window.show();
    pump();

    // ---- 主页：零实例 / 有实例 ----
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
    MainWindowUI::HomeQuickItem none;
    none.key = QStringLiteral("none");
    none.name = QStringLiteral("还没有实例");
    none.empty = true;
    ui.setHomeQuickRows({none}, {}, {});
    saveShot(app, window, options.out, QStringLiteral("home-empty"), &saved);

    fillHomeSamples(ui);
    saveShot(app, window, options.out, QStringLiteral("home-instances"), &saved);

    // ---- 实例管理：空状态 / 列表 ----
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
    if (auto *list = window.findChild<QTreeWidget *>(MainWindowUI::instanceListName())) list->hide();
    if (auto *empty = window.findChild<QWidget *>(MainWindowUI::instanceEmptyStateName())) empty->show();
    saveShot(app, window, options.out, QStringLiteral("manage-empty"), &saved);

    fillInstanceTree(ui);
    if (auto *list = window.findChild<QTreeWidget *>(MainWindowUI::instanceListName())) list->show();
    if (auto *empty = window.findChild<QWidget *>(MainWindowUI::instanceEmptyStateName())) empty->hide();
    saveShot(app, window, options.out, QStringLiteral("manage-list"), &saved);

    // ---- 实例二级设置：总览 / 模组（浮层里，对话框本身 820×560） ----
    {
        QTemporaryDir instanceRoot(QDir::current().filePath(QStringLiteral(".review-instance-XXXXXX")));
        if (!instanceRoot.isValid()) return 2;
        const QString root = instanceRoot.path();
        makeInstanceFixture(root);
        using Dlg = InstanceSettingsDialog;
        Dlg::GlobalValues global;
        global.javaPath = QStringLiteral("C:/Program Files/Java/jdk-21/bin/java.exe");
        global.xmx = 4096;
        global.xms = 1024;
        global.autoMemory = false;
        global.extraJvmArgs = QStringLiteral("-XX:+UseG1GC");
        Dlg dialog(QStringLiteral("1.20.1 整合包"), QStringLiteral("1.20.1-Forge"), Dlg::Override(), global,
                   QStringList(), false);
        QHash<QString, QString> dirs;
        dirs.insert(QStringLiteral("mod"), root + QStringLiteral("/mods"));
        dirs.insert(QStringLiteral("resourcepack"), root + QStringLiteral("/resourcepacks"));
        dirs.insert(QStringLiteral("datapack"), root + QStringLiteral("/datapacks"));
        dirs.insert(QStringLiteral("shader"), root + QStringLiteral("/shaderpacks"));
        dirs.insert(QStringLiteral("save"), root + QStringLiteral("/saves"));
        dirs.insert(QStringLiteral("logs"), root + QStringLiteral("/logs"));
        dirs.insert(QStringLiteral("screenshots"), root + QStringLiteral("/screenshots"));
        dirs.insert(QStringLiteral("game"), root + QStringLiteral("/game"));
        dialog.setInstanceDir(root);
        dialog.setFileDirs(dirs);
        dialog.setLastLaunch(QDateTime::currentDateTime().addSecs(-3600 * 26));
        dialog.setIcon(VersionIcons::iconCandidateKeys().value(0), VersionIcons::grassBlock(48));
        if (ui.overlay()->showPage(&dialog, QStringLiteral("实例设置"), MainWindowUI::instancesTabIndex())) {
            dialog.showPage(Dlg::overviewCategoryKey());
            pump();
            std::printf("  instance dialog %dx%d at (%d,%d)\n", dialog.width(), dialog.height(),
                        dialog.mapTo(&window, QPoint(0, 0)).x(), dialog.mapTo(&window, QPoint(0, 0)).y());
            saveShot(app, window, options.out, QStringLiteral("instance-settings-overview"), &saved);
            dialog.showPage(QStringLiteral("mod"));
            saveShot(app, window, options.out, QStringLiteral("instance-settings-mods"), &saved);
            ui.overlay()->closeOverlay();
            pump();
        } else {
            std::printf("  SKIP instance-settings：覆盖层没开出来\n");
        }
    }

    // ---- 下载：游戏（正式版展开）/ 模组 ----
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
    ui.getDownloadCategoryList()->setCurrentRow(MainWindowUI::downloadCategoryGameIndex());
    ui.setDownloadHeaderRow(MainWindowUI::downloadCategoryGameIndex());
    ui.getDownloadStack()->setCurrentIndex(MainWindowUI::downloadCategoryGameIndex());
    ui.getGameStack()->setCurrentIndex(MainWindowUI::downloadGameTreeIndex());
    fillVersionTree(ui);
    pump();
    saveShot(app, window, options.out, QStringLiteral("download-game-release"), &saved);

    ui.getDownloadCategoryList()->setCurrentRow(MainWindowUI::downloadCategoryModIndex());
    ui.setDownloadHeaderRow(MainWindowUI::downloadCategoryModIndex());
    ui.getDownloadStack()->setCurrentIndex(MainWindowUI::downloadCategoryModIndex());
    const auto fillFilter = [](QComboBox *combo, const QStringList &items) {
        if (combo == nullptr) return;
        QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItems(items);
        combo->setCurrentIndex(0);
    };
    const QString modKey = QStringLiteral("mod");
    fillFilter(ui.getDownloadFilterVersionCombo(modKey),
               {QStringLiteral("全部版本"), QStringLiteral("1.21.4"), QStringLiteral("1.20.1")});
    fillFilter(ui.getDownloadFilterLoaderCombo(modKey),
               {QStringLiteral("全部加载器"), QStringLiteral("Fabric"), QStringLiteral("Forge")});
    fillFilter(ui.getDownloadFilterSourceCombo(modKey),
               {QStringLiteral("Modrinth"), QStringLiteral("CurseForge")});
    fillFilter(ui.getDownloadFilterTypeCombo(modKey),
               {QStringLiteral("全部类型"), QStringLiteral("模组"), QStringLiteral("整合包")});
    if (auto *search = ui.getDownloadSearchEdit(modKey)) search->setText(QStringLiteral("sodium"));
    fillResourceTree(ui, modKey);
    pump();
    saveShot(app, window, options.out, QStringLiteral("download-mod"), &saved);

    // ---- 设置：启动 / 提示 / 个性化（顶、主页背景图）/ 下载 ----
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::settingsTabIndex());
    fillSettingsSamples(ui);
    const QStringList settingsKeys = MainWindowUI::settingsCategoryKeys();
    QWidget *settingsPage = ui.getTabWidget()->widget(MainWindowUI::settingsTabIndex());
    const auto settingsScroll = [&](int index) -> QScrollArea * {
        return qobject_cast<QScrollArea *>(ui.getSettingsCategoryStack()->widget(index));
    };
    const auto shotSettings = [&](int index, const QString &name) {
        ui.getSettingsTabs()->setCurrentIndex(index);
        if (QScrollArea *scroll = settingsScroll(index)) scroll->verticalScrollBar()->setValue(0);
        saveShot(app, window, options.out, name, &saved);
    };
    shotSettings(settingsKeys.indexOf(QStringLiteral("launch")), QStringLiteral("settings-launch"));
    shotSettings(settingsKeys.indexOf(QStringLiteral("warn")), QStringLiteral("settings-warn"));
    const int lookIndex = settingsKeys.indexOf(QStringLiteral("look"));
    shotSettings(lookIndex, QStringLiteral("settings-look-top"));
    if (QScrollArea *scroll = settingsScroll(lookIndex)) {
        QLabel *anchor = nullptr;
        if (scroll->widget() != nullptr) {
            for (QLabel *label : scroll->widget()->findChildren<QLabel *>()) {
                if (label->text() == QStringLiteral("主页背景图")) {
                    anchor = label;
                    break;
                }
            }
        }
        if (anchor != nullptr) {
            const int y = anchor->mapTo(scroll->widget(), QPoint(0, 0)).y();
            scroll->verticalScrollBar()->setValue(qMax(0, y - 16));
            std::printf("  settings-look anchor y=%d scroll=%d/%d\n", y,
                        scroll->verticalScrollBar()->value(), scroll->verticalScrollBar()->maximum());
        } else {
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum() / 3);
        }
        saveShot(app, window, options.out, QStringLiteral("settings-look-home-background"), &saved);
    }
    shotSettings(settingsKeys.indexOf(QStringLiteral("download")), QStringLiteral("settings-download"));
    if (settingsPage != nullptr) settingsPage->update();

    // ---- 智能体：未配置 / 有会话 ----
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::agentTabIndex());
    AgentPage *agent = ui.getAgentPage();
    if (agent != nullptr) {
        agent->setConfigured(false, QString());
        agent->showEmptyState(true);
        saveShot(app, window, options.out, QStringLiteral("agent-unconfigured"), &saved);

        agent->setConfigured(true, QStringLiteral("已连接 · OpenAI 兼容"));
        agent->setModelLabel(QStringLiteral("gpt-4o"));
        agent->setEncryptionChecked(true);
        agent->setInstanceChip(QStringLiteral("1.20.1-Forge"), false);
        QList<AgentView::SessionMeta> sessions;
        AgentView::SessionMeta first;
        first.id = QStringLiteral("s1");
        first.title = QStringLiteral("查一下崩溃原因");
        first.updatedAt = QDateTime::currentDateTime().addSecs(-600);
        first.encrypted = true;
        sessions.append(first);
        AgentView::SessionMeta second;
        second.id = QStringLiteral("s2");
        second.title = QStringLiteral("帮我写一条启动参数");
        second.updatedAt = QDateTime::currentDateTime().addSecs(-86400);
        sessions.append(second);
        AgentView::SessionMeta third;
        third.id = QStringLiteral("s3");
        third.title = QStringLiteral("模组冲突排查");
        third.updatedAt = QDateTime::currentDateTime().addSecs(-86400 * 3);
        third.pinned = true;
        sessions.append(third);
        agent->setSessions(sessions);
        agent->setCurrentSession(QStringLiteral("s1"));
        agent->setTitle(QStringLiteral("查一下崩溃原因"));
        QList<AgentView::Message> messages;
        AgentView::Message ask;
        ask.id = QStringLiteral("m1");
        ask.user = true;
        ask.content = QStringLiteral("游戏一进世界就崩，日志里最后几行是 java.lang.NullPointerException。帮我看看。");
        messages.append(ask);
        AgentView::Message answer;
        answer.id = QStringLiteral("m2");
        answer.content = QStringLiteral(
            "日志里 NPE 出现在 `net.minecraft.client.renderer` 那一层，最常见的原因是某个渲染模组和当前版本不匹配。\n\n"
            "先按这个顺序试：\n1. 把 `sodium` 和 `iris` 一起停用，只留一个；\n"
            "2. 确认 Fabric API 的版本跟 1.20.1 对得上；\n3. 再启动一次，看 `latest.log` 里第一个 ERROR 出现在哪。");
        messages.append(answer);
        AgentView::Message toolMsg;
        toolMsg.id = QStringLiteral("m3");
        toolMsg.content = QStringLiteral("已读取实例日志：`logs/latest.log`（2.1 MB）。");
        messages.append(toolMsg);
        agent->setMessages(messages);
        AgentView::ToolCard card;
        card.id = QStringLiteral("t1");
        card.title = QStringLiteral("读取 logs/latest.log");
        card.summary = QStringLiteral("找到 1 处 ERROR、3 处 WARN");
        card.detail = QStringLiteral("ERROR 23:41:07 java.lang.NullPointerException");
        card.status = AgentView::ToolCardStatus::Done;
        agent->setToolCards({card});
        agent->showEmptyState(false);
        saveShot(app, window, options.out, QStringLiteral("agent-session"), &saved);
    } else {
        std::printf("  SKIP agent：拿不到 AgentPage\n");
    }

    // ---- 工具箱：目录页 + 十个工具页 ----
    ui.openToolboxTab();
    QWidget *host = ui.toolboxTab();
    ToolboxPage *toolbox = nullptr;
    if (host != nullptr && host->layout() != nullptr) {
        toolbox = new ToolboxPage(host);
        host->layout()->addWidget(toolbox);
        fillToolboxSamples(*toolbox);
        toolbox->showCatalog();
        pump();
        saveShot(app, window, options.out, QStringLiteral("toolbox-catalog"), &saved);
        for (ToolboxPage::Tool tool : ToolboxPage::tools()) {
            toolbox->showTool(tool);
            pump();
            saveShot(app, window, options.out,
                     QStringLiteral("toolbox-%1").arg(ToolboxPage::toolKey(tool)), &saved);
        }
    } else {
        std::printf("  SKIP toolbox：工具箱页挂不上\n");
    }

    // ---- 活动中心：两三条任务 ----
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
    ui.setTaskCenterContent([] { return activityItems(); }, [] {});
    MainWindowUI::BottomBarTaskSummary summary;
    summary.hasTask = true;
    summary.state = TaskCenter::State::Running;
    summary.percent = 41;
    summary.extraCount = 2;
    summary.title = QStringLiteral("下载 Sodium 0.6.13");
    ui.setBottomBarTaskSummary(summary);
    // 「活动中心」的公开入口就是底栏中间那一格（点一下开、再点一下收）。
    if (QPushButton *summaryButton = ui.getTaskSummaryButton()) {
        summaryButton->click();
        pump();
        if (QFrame *pop = window.findChild<QFrame *>(QStringLiteral("activityPopover"))) {
            std::printf("  activity popover %dx%d at (%d,%d) visible=%d\n", pop->width(), pop->height(),
                        pop->mapTo(&window, QPoint(0, 0)).x(), pop->mapTo(&window, QPoint(0, 0)).y(),
                        int(pop->isVisible()));
        }
        saveShot(app, window, options.out, QStringLiteral("activity-center"), &saved);
        summaryButton->click();
        pump();
    } else {
        std::printf("  SKIP activity-center：拿不到底栏任务按钮\n");
    }

    // ---- 账号弹层：未登录 / 已登录 ----
    ui.setBottomBarPanelContent(
        [](bool account) {
            return account ? accountRows(false) : QList<MainWindowUI::PanelRow>();
        },
        [](bool, const QString &, int) {});
    ui.toggleAccountsPanel();
    pump();
    if (QWidget *pop = window.findChild<QWidget *>(MainWindowUI::accountPopoverName())) {
        std::printf("  account popover %dx%d at (%d,%d) visible=%d\n", pop->width(), pop->height(),
                    pop->mapTo(&window, QPoint(0, 0)).x(), pop->mapTo(&window, QPoint(0, 0)).y(),
                    int(pop->isVisible()));
    }
    saveShot(app, window, options.out, QStringLiteral("account-signed-out"), &saved);
    ui.toggleAccountsPanel();
    pump();

    if (ui.getHomeAccountLabel() != nullptr) ui.getHomeAccountLabel()->setText(QStringLiteral("Steve"));
    if (ui.getHomeAccountTypeLabel() != nullptr) {
        ui.getHomeAccountTypeLabel()->setText(MainWindowUI::accountTypeOnlineText());
        ui.getHomeAccountTypeLabel()->show();
    }
    if (ui.getHomeAvatarLabel() != nullptr)
        ui.getHomeAvatarLabel()->setPixmap(faceMark(QColor(0x3c, 0x9e, 0x7a), QStringLiteral("S"), 32));
    ui.setBottomBarPanelContent(
        [](bool account) {
            return account ? accountRows(true) : QList<MainWindowUI::PanelRow>();
        },
        [](bool, const QString &, int) {});
    ui.toggleAccountsPanel();
    pump();
    saveShot(app, window, options.out, QStringLiteral("account-signed-in"), &saved);
    ui.toggleAccountsPanel();
    pump();

    std::printf("saved %d shots to %s (%dx%d, theme=%s)\n", saved, qPrintable(options.out),
                options.size.width(), options.size.height(), qPrintable(options.theme));
    return saved > 0 ? 0 : 1;
}

/// 原有的那五张 × 四个主题（窗口 1001×776）—— 不带参数时的默认行为，保持不动。
int runLegacy(QApplication &app, const QString &out) {
    Theme::setSpec(Theme::load());
    QDir().mkpath(out);
    int saved = 0;
    // 第一趟是**新安装**：这个进程用的是临时设置目录（见调用方），盘上一项主题都没有，
    // 所以 `Theme::load()` 拿到的就是出厂那一份 —— 顺带把预设 / 主题色 / 窗口底色打出来。
    for (const QString &id : {QStringLiteral("default"), QStringLiteral("dark"),
                              QStringLiteral("emerald"), QStringLiteral("ocean")}) {
        if (id == QLatin1String("default")) {
            Theme::setSpec(Theme::load());
        } else {
            Theme::setSpec(Theme::presetSpec(id));
        }
        Theme::apply(app);
        if (id == QLatin1String("default")) {
            std::printf("new install (no settings): preset=%s accent=%s window-bg=%s\n",
                        qPrintable(Theme::current().presetId),
                        qPrintable(Theme::current().accent.name()),
                        qPrintable(app.palette().color(QPalette::Window).name()));
        }
        QMainWindow window;
        MainWindowUI ui(&window);
        window.resize(1127 * 4 / 5 + 100, 776);
        window.show();

        const auto fill = [](QComboBox *combo, const QStringList &items, int current = 0) {
            if (combo == nullptr) return;
            combo->clear();
            combo->addItems(items);
            combo->setCurrentIndex(current);
        };
        fill(ui.getDownloadSourceCombo(), {QStringLiteral("优先官方源"), QStringLiteral("优先国内源")});
        fill(ui.getMemoryModeCombo(), {QStringLiteral("自动分配"), QStringLiteral("手动分配")}, 1);
        fill(ui.getGameWindowModeCombo(), {QStringLiteral("窗口"), QStringLiteral("最大化"), QStringLiteral("全屏")});
        if (ui.getJavaPathLabel() != nullptr) ui.getJavaPathLabel()->setText(QStringLiteral("自动选择　按游戏版本"));
        if (ui.getMemoryHint() != nullptr)
            ui.getMemoryHint()->setText(QStringLiteral(
                "系统总内存 31.5 GB、当前可用 14.5 GB；现在最多给到 4096 MB。左边拖着改，也可以直接填数字。"
                "最小内存由启动器定（1024 MB），不用管它。"));
        if (ui.getMemoryMaxSpin() != nullptr) ui.getMemoryMaxSpin()->setValue(4096);
        if (ui.getMemoryMaxSlider() != nullptr) ui.getMemoryMaxSlider()->setValueMb(4096);

        MainWindowUI::HomeQuickItem none;
        none.key = QStringLiteral("none");
        none.name = QStringLiteral("还没有实例");
        none.empty = true;
        ui.setHomeQuickRows({none}, {}, {});

        const auto shot = [&](const QString &name) {
            app.processEvents();
            app.processEvents();
            if (window.grab().save(QDir(out).filePath(QStringLiteral("%1-%2.png").arg(name, id)))) ++saved;
        };
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        shot(QStringLiteral("home-empty"));
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
        if (auto *list = window.findChild<QTreeWidget *>(MainWindowUI::instanceListName())) list->hide();
        if (auto *empty = window.findChild<QWidget *>(MainWindowUI::instanceEmptyStateName())) empty->show();
        shot(QStringLiteral("manage-empty"));
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::settingsTabIndex());
        ui.getSettingsTabs()->setCurrentIndex(0);
        shot(QStringLiteral("settings-launch"));
        ui.getSettingsTabs()->setCurrentIndex(
            MainWindowUI::settingsCategoryKeys().indexOf(QStringLiteral("download")));
        shot(QStringLiteral("settings-download"));
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
        shot(QStringLiteral("download-source"));
    }
    std::printf("saved %d shots to %s\n", saved, qPrintable(out));
    return saved > 0 ? 0 : 1;
}

}  // namespace

int UiReviewShotsCheck(QApplication &app) {
    Anim::setEnabled(false);
    QTemporaryDir settingsDir(QDir::current().filePath(QStringLiteral(".review-shots-XXXXXX")));
    if (!settingsDir.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    app.setOrganizationName(QStringLiteral("WTFMLReviewShots"));
    app.setApplicationName(QStringLiteral("ReviewShots"));
    app.setStyle(Theme::createStyle());
    const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
    const int fontId = QFile::exists(fontPath) ? QFontDatabase::addApplicationFont(fontPath) : -1;
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    app.setFont(QFont(families.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : families.first(), 9));

    ShotOptions options;
    options.out = QDir::current().filePath(QStringLiteral("build/review-shots"));
    QStringList positional;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        const auto valueOf = [&](const QString &prefix) {
            return arg.startsWith(prefix) ? arg.mid(prefix.size()) : QString();
        };
        if (arg == QLatin1String("--extended")) {
            options.extended = true;
        } else if (arg.startsWith(QLatin1String("--theme="))) {
            options.theme = valueOf(QStringLiteral("--theme="));
        } else if (arg == QLatin1String("--theme") && i + 1 < args.size()) {
            options.theme = args.at(++i);
        } else if (arg.startsWith(QLatin1String("--size="))) {
            const QStringList parts = valueOf(QStringLiteral("--size=")).split(QLatin1Char('x'));
            if (parts.size() == 2) options.size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
        } else if (arg == QLatin1String("--size") && i + 1 < args.size()) {
            const QStringList parts = args.at(++i).split(QLatin1Char('x'));
            if (parts.size() == 2) options.size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
        } else if (arg.startsWith(QLatin1String("--out="))) {
            options.out = valueOf(QStringLiteral("--out="));
        } else if (arg == QLatin1String("--out") && i + 1 < args.size()) {
            options.out = args.at(++i);
        } else if (!arg.startsWith(QLatin1Char('-'))) {
            positional.append(arg);
        }
    }
    // 不带参数时仍是老行为：第一个位置参数是输出目录。
    if (!positional.isEmpty() && options.out.endsWith(QStringLiteral("build/review-shots"))) {
        options.out = positional.first();
    }
    if (options.extended) {
        if (options.theme.isEmpty()) options.theme = QStringLiteral("dark");
        if (!options.size.isValid() || options.size.width() <= 0 || options.size.height() <= 0)
            options.size = QSize(1280, 800);
        return runExtended(app, options);
    }
    return runLegacy(app, options.out);
}
