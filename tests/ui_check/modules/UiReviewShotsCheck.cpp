// 走查验收截图（offscreen，QWidget::grab()）。
//
// 默认这一趟（不带参数）保持原来的行为：新安装默认 + 三个具名预设 ×
// 主页零实例 / 管理页空状态 / 设置·启动 / 设置·下载 / 下载·游戏，窗口 1001×776。
//
// 带 `--extended` 时出一整套「默认窗口尺寸」参考图：指定一个主题和一个窗口尺寸，
// 按固定页面顺序把 30 张图拍下来（文件名为 <页面>-<状态>.png）：
//
//   QT_QPA_PLATFORM=offscreen ui_check_review_shots --extended \
//       --theme dark --size 1280x800 --out D:/shots/dark-1280
//
// `--background <图片绝对路径>` 把它设成自定义页面背景图（铺满整窗），用来看墨色 / 品牌标的取色。
//
// `--popups` 只拍自造顶层浮层打开时的样子（智能体实例下拉 / ⋯ 菜单、下拉弹层、右键菜单、
// tooltip、实例页文件夹浮层与分组齿轮菜单），每张另出一张 `-zoom` 的两倍放大，看四角。
// 浮层是独立的顶层窗，`window.grab()` 拍不到，这里按全局位置叠回主窗口那张图上。
//
// 尺寸只影响主窗口（离屏、DPR=1）；实例设置对话框是产品里那个 820×560 的浮层，
// 这里照真实样子嵌在覆盖层里拍，不跟着窗口拉伸。
#include "Support.hpp"
#include "InputWidthScan.hpp"
#include "LoadingState.hpp"
#include "PresetManagePage.hpp"
#include "InlineMessage.hpp"
#include "PresetInstallPicker.hpp"

#include "AgentPage.hpp"
#include "AgentSettingsPage.hpp"
#include "AgentViewTypes.hpp"
#include "agent/AgentSession.hpp"
#include "OverlayHost.hpp"
#include "Popups.hpp"
#include "ToolboxPage.hpp"
#include "ToolboxSamples.hpp"
#include "VersionIcons.hpp"

#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QFontDatabase>
#include <QFrame>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>

#include <tuple>

namespace {

struct ShotOptions {
    QString out;
    QString theme;
    QSize size{0, 0};
    QString background; ///< 自定义页面背景图（绝对路径），空 = 不设
    bool extended = false;
    bool popups = false;
    bool loading = false;  ///< `--loading`：只拍加载态（步骤 122）那几张
    bool agentPolish = false;
    bool files = false;  ///< `--files`：只拍实例设置·模组页 60 个模组（步骤 139），文件名 r139-<主题>-*
    /// `--files --first-disable`：40 个模组全启用 → 禁用第一个，前后各一张（看列表跳不跳），
    /// 文件名 first-disable-<before|after>-<宽>.png
    bool firstDisable = false;
    /// `--r151`：步骤 151 的四处（资源包页查完更新前后 / 预设弹窗标题图标 / 服务器表头 /
    /// 主页窗口按钮），文件名 r151-<主题>-<宽>-*；配 `--background` 拍亮背景图那一档。
    bool r151 = false;
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
        ui.getMemoryHint()->setText(QStringLiteral("系统总内存 31.5 GB，当前可用 14.5 GB；最小内存由启动器设为 1024 MB。"));
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

/// 主页那几排快捷方式：4 个已安装实例 + 2 个收藏 + 2 个最近，含一个带加载器的。
///
/// NOTE: `current` 这一位**全表只有一个真**（就是下拉框里选中的那一份，见
/// `HomeQuickController::build()`）；这里 4 个都标上就会在「最近」条上摆出两个「当前」——
/// r81 那批截图正是这么错的。
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
    // 第一个是当前实例（唯一一个 `current = true`）。
    const QList<MainWindowUI::HomeQuickItem> installed{
        item(QStringLiteral("1.20.1-Forge"), QStringLiteral("1.20.1-Forge 47.2.0"),
             QStringLiteral("Minecraft 1.20.1 / Forge 47.2.0"), true, false, true),
        item(QStringLiteral("1.21.4-Fabric"), QStringLiteral("1.21.4-Fabric 0.16.9"),
             QStringLiteral("Minecraft 1.21.4 / Fabric 0.16.9"), false, true, true),
        item(QStringLiteral("1.20.4"), QStringLiteral("1.20.4 原版"),
             QStringLiteral("Minecraft 1.20.4 / 原版"), false, false, false),
        item(QStringLiteral("1.19.2-NeoForge"), QStringLiteral("1.19.2-NeoForge 20.4.237"),
             QStringLiteral("Minecraft 1.19.2 / NeoForge 20.4.237"), false, true, false)};
    const QList<MainWindowUI::HomeQuickItem> favorites{
        installed.at(1), installed.at(3)};
    const QList<MainWindowUI::HomeQuickItem> recent{installed.at(0), installed.at(1)};
    ui.setHomeQuickRows({installed.at(0)}, favorites, recent);
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
    if (qEnvironmentVariableIsSet("WTFML_SCAN_INPUTS")) {
        for (const InputProbe &p : scanInputs(&window))
            std::printf("  INPUT [%s] %s %s '%s' w=%d max=%d kind=%s\n", qPrintable(name), qPrintable(p.cls),
                        qPrintable(p.name), qPrintable(p.hint.left(24)), p.width, p.maxWidth, qPrintable(p.kind));
    }
    const QString file = QDir(outDir).filePath(name + QStringLiteral(".png"));
    if (window.grab().save(file)) {
        ++(*saved);
        std::printf("  %s  %dx%d\n", qPrintable(name), window.width(), window.height());
        return true;
    }
    std::printf("  FAIL %s -> %s\n", qPrintable(name), qPrintable(file));
    return false;
}

/// `--loading`：加载态（ui/LoadingState）在真界面上的样子。动效开着、等淡入走完再拍。
/// 文件名 after-<场景>-<主题>.png。
/// `--files`：实例设置·模组页，60 个模组（每 5 个禁一个），平铺 / 悬停 + 选中 / 悬停在禁用行上。
/// 悬停用合成事件：行控件版发 Enter 给那一行，代理版发 MouseMove 给视口，两版都吃。
int shootFiles(QApplication &app, QMainWindow &window, MainWindowUI &ui, const ShotOptions &options) {
    int saved = 0;
    QTemporaryDir instanceRoot(QDir::current().filePath(QStringLiteral(".review-files-XXXXXX")));
    if (!instanceRoot.isValid()) return 2;
    const QString root = instanceRoot.path();
    makeInstanceFixture(root);
    QDir(root + QStringLiteral("/mods")).removeRecursively();
    QDir().mkpath(root + QStringLiteral("/mods"));
    for (int i = 0; i < (options.firstDisable ? 40 : 60); ++i) {
        QFile file(root + QStringLiteral("/mods/mod-%1-1.%2.%3.jar%4")
                              .arg(QChar(QLatin1Char('a' + i % 26)))
                              .arg(i)
                              .arg(i % 7)
                              .arg(i % 5 == 1 && !options.firstDisable ? QStringLiteral(".disabled") : QString()));
        if (file.open(QIODevice::WriteOnly)) file.write(QByteArray(4096 + i * 1531, 'x'));
    }
    using Dlg = InstanceSettingsDialog;
    Dlg::GlobalValues global;
    global.javaPath = QStringLiteral("C:/Program Files/Java/jdk-21/bin/java.exe");
    Dlg dialog(QStringLiteral("1.20.1 整合包"), QStringLiteral("1.20.1-Forge"), Dlg::Override(), global,
               QStringList(), false);
    QHash<QString, QString> dirs;
    dirs.insert(QStringLiteral("mod"), root + QStringLiteral("/mods"));
    dirs.insert(QStringLiteral("resourcepack"), root + QStringLiteral("/resourcepacks"));
    dirs.insert(QStringLiteral("save"), root + QStringLiteral("/saves"));
    dialog.setInstanceDir(root);
    dialog.setFileDirs(dirs);
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
    app.processEvents();
    if (!ui.overlay()->showPage(&dialog, QStringLiteral("实例设置"), MainWindowUI::instancesTabIndex())) {
        std::printf("  SKIP files：覆盖层没开出来\n");
        return 1;
    }
    app.processEvents();
    app.processEvents();
    dialog.showPage(QStringLiteral("mod"));
    for (int i = 0; i < 4; ++i) app.processEvents();
    InstanceFilesPage *page = dialog.filesPage(QStringLiteral("mod"));
    QListWidget *list = page != nullptr ? page->list() : nullptr;
    if (list == nullptr) return 1;
    if (options.firstDisable) {
        const QString width = QString::number(window.width());
        const auto report = [&](const char *when) {
            const auto *tools = page->findChild<QWidget *>(QStringLiteral("modSelectionTools"));
            std::printf("  %s: list viewport y=%d, selection row y=%d\n", when,
                        list->viewport()->mapTo(&window, QPoint()).y(),
                        tools != nullptr ? tools->mapTo(&window, QPoint()).y() : -1);
        };
        report("before");
        saveShot(app, window, options.out, QStringLiteral("first-disable-before-") + width, &saved);
        const QString first = list->item(0)->data(InstanceFilesPage::pathRole()).toString();
        QFile::rename(first, first + QStringLiteral(".disabled"));
        page->refresh();
        for (int i = 0; i < 4; ++i) app.processEvents();
        report("after");
        saveShot(app, window, options.out, QStringLiteral("first-disable-after-") + width, &saved);
        ui.overlay()->closeOverlay();
        app.processEvents();
        return saved == 2 ? 0 : 1;
    }
    const QString tag = QStringLiteral("r139-") + options.theme + QStringLiteral("-");
    saveShot(app, window, options.out, tag + QStringLiteral("mods"), &saved);
    const auto hoverRow = [&](int row) {
        QWidget *viewport = list->viewport();
        // 先把上一行的悬停撤掉（行控件版：每一行各收一发 Leave；代理版：视口收一发 Leave）。
        QEvent leave(QEvent::Leave);
        for (QObject *child : viewport->children())
            if (child->isWidgetType()) QApplication::sendEvent(child, &leave);
        QApplication::sendEvent(viewport, &leave);
        const QPoint pos = list->visualItemRect(list->item(row)).center() + QPoint(-40, 0);
        QWidget *w = viewport->childAt(pos);
        while (w != nullptr && w->parentWidget() != viewport) w = w->parentWidget();
        const QPoint global = viewport->mapToGlobal(pos);
        if (w != nullptr) {
            QEnterEvent enter(w->mapFrom(viewport, pos), w->mapFrom(viewport, pos), global);
            QApplication::sendEvent(w, &enter);
        }
        QMouseEvent move(QEvent::MouseMove, pos, pos, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        viewport->update();
    };
    list->item(2)->setSelected(true);
    hoverRow(0);
    saveShot(app, window, options.out, tag + QStringLiteral("mods-hover-selected"), &saved);
    list->item(2)->setSelected(false);
    hoverRow(3);  // mod-b-1.1.1.jar.disabled
    saveShot(app, window, options.out, tag + QStringLiteral("mods-disabled-hover"), &saved);
    ui.overlay()->closeOverlay();
    app.processEvents();
    std::printf("\nFiles shots: %d saved -> %s\n", saved, qPrintable(options.out));
    return saved == 3 ? 0 : 1;
}

/// `--r151`：步骤 151 走查。资源包页（紧凑页头 + 批量）查更新前 / 冒出「可更新」后各一张，
/// 服务器列表（自家代理的第一列 + 右对齐的状态列）一张，预设弹窗提问 ⓘ / 拼图各一张，主页一张。
int shootR151(QApplication &app, QMainWindow &window, MainWindowUI &ui, const ShotOptions &options) {
    int saved = 0;
    const QString tag = QStringLiteral("r151-%1-%2-").arg(options.theme).arg(window.width());
    const auto pump = [&app] { for (int i = 0; i < 4; ++i) app.processEvents(); };
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
    pump();
    saveShot(app, window, options.out, tag + QStringLiteral("home"), &saved);
    if (!options.background.isEmpty()) return saved == 1 ? 0 : 1;  // 亮背景图那一档只看主页

    QTemporaryDir instanceRoot(QDir::current().filePath(QStringLiteral(".review-r151-XXXXXX")));
    if (!instanceRoot.isValid()) return 2;
    const QString root = instanceRoot.path();
    makeInstanceFixture(root);
    QDir().mkpath(root + QStringLiteral("/resourcepacks"));
    for (int i = 0; i < 12; ++i) {
        QFile file(root + QStringLiteral("/resourcepacks/pack-%1.zip").arg(QChar(QLatin1Char('a' + i))));
        if (file.open(QIODevice::WriteOnly)) file.write(QByteArray(2048 + i * 977, 'x'));
    }
    using Dlg = InstanceSettingsDialog;
    Dlg dialog(QStringLiteral("1.20.1 整合包"), QStringLiteral("1.20.1-Forge"), Dlg::Override(), Dlg::GlobalValues(),
               QStringList(), false);
    QHash<QString, QString> dirs;
    dirs.insert(QStringLiteral("mod"), root + QStringLiteral("/mods"));
    dirs.insert(QStringLiteral("resourcepack"), root + QStringLiteral("/resourcepacks"));
    dirs.insert(QStringLiteral("save"), root + QStringLiteral("/saves"));
    dialog.setInstanceDir(root);
    dialog.setFileDirs(dirs);
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
    pump();
    if (!ui.overlay()->showPage(&dialog, QStringLiteral("实例设置"), MainWindowUI::instancesTabIndex())) return 1;
    pump();
    dialog.showPage(QStringLiteral("resourcepack"));
    pump();
    if (InstanceFilesPage *page = dialog.filesPage(QStringLiteral("resourcepack")); page != nullptr && page->list()) {
        QListWidget *list = page->list();
        std::printf("  resourcepack before check: list viewport y=%d\n", list->viewport()->mapTo(&window, QPoint()).y());
        saveShot(app, window, options.out, tag + QStringLiteral("rp-before-check"), &saved);
        page->setUpdateResult({list->item(0)->data(InstanceFilesPage::pathRole()).toString()}, true);
        pump();
        std::printf("  resourcepack update found: list viewport y=%d\n", list->viewport()->mapTo(&window, QPoint()).y());
        saveShot(app, window, options.out, tag + QStringLiteral("rp-update-found"), &saved);
    }
    dialog.showPage(QStringLiteral("server"));
    dialog.setServerList({{QStringLiteral("甲乙服"), QStringLiteral("mc.example.org")},
                          {QStringLiteral("周末建筑"), QStringLiteral("build.example.net:25570")},
                          {QStringLiteral("老服"), QStringLiteral("old.example.com")}},
                         QString());
    Dlg::ServerRowStatus up;
    up.tested = up.online = true;
    up.latencyMs = 42;
    up.playersOnline = 7;
    up.playersMax = 20;
    up.motd = QStringLiteral("欢迎回来");
    Dlg::ServerRowStatus slow = up;
    slow.latencyMs = 318;
    slow.playersOnline = 0;
    Dlg::ServerRowStatus down;
    down.tested = true;
    down.timedOut = true;
    down.error = QStringLiteral("超时（网络不通）");
    dialog.setServerStatuses({up, slow, down});
    pump();
    saveShot(app, window, options.out, tag + QStringLiteral("server-list"), &saved);
    ui.overlay()->closeOverlay();
    pump();

    // 预设弹窗：照 InstallPageController 那样开（提问底色 + 800 宽正文），左边是改前（不换图标）。
    for (const bool puzzle : {false, true}) {
        InlineMessageBox box(MessageKind::Question, MainWindowUI::installPresetPickerTitle(), QString(),
                             {MainWindowUI::installPresetConfirmText(), MainWindowUI::installPresetCancelText()}, 0, -1,
                             &window);
        auto *picker = new PresetInstallPicker;
        picker->setContext(QStringLiteral("1.21.4"), QStringLiteral("Fabric"));
        PresetInstallPicker::Category game;
        game.key = QStringLiteral("gameSettings");
        game.title = QStringLiteral("游戏设置");
        game.options.append({QStringLiteral("daily-game"), QStringLiteral("日常配置"), true, true, 12});
        PresetInstallPicker::Category mods;
        mods.key = QStringLiteral("mods");
        mods.title = QStringLiteral("模组");
        mods.options.append({QStringLiteral("daily"), QStringLiteral("日常配置"), true, true, 42});
        mods.options.append({QStringLiteral("build"), QStringLiteral("建筑辅助"), true, false, 18});
        picker->setCategories({game, mods});
        picker->setComposites({});
        box.setContent(picker, 800);
        if (puzzle) box.setHeaderIcon(QStringLiteral("puzzle"));
        const QString name = tag + (puzzle ? QStringLiteral("preset-puzzle") : QStringLiteral("preset-question-i"));
        QTimer::singleShot(150, &box, [&] {
            saveShot(app, window, options.out, name, &saved);
            box.done(1);
        });
        ui.overlay()->execPrompt(&box);
        pump();
    }
    std::printf("\nr151 shots: %d saved -> %s\n", saved, qPrintable(options.out));
    return saved == 6 ? 0 : 1;
}

int shootLoading(QApplication &app, QMainWindow &window, MainWindowUI &ui, const ShotOptions &options) {
    Anim::setEnabled(true);
    int saved = 0;
    const auto settle = [&app](int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
        app.processEvents();
    };
    // 拍在三颗点有明有暗的那一刻（第一颗刚过峰、第二颗快到峰），别碰巧拍到三颗都歇着。
    const auto toPhase = [&app] {
        while (LoadingState::clockMs() % Anim::loadingDotPeriodMs() < 430 ||
               LoadingState::clockMs() % Anim::loadingDotPeriodMs() > 470) {
            QEventLoop loop;
            QTimer::singleShot(4, &loop, &QEventLoop::quit);
            loop.exec();
        }
        app.processEvents();
    };
    const QString tag = options.theme;
    const QString modKey = QStringLiteral("mod");

    // ---- 下载·模组：整块加载（树清空，盖一层「正在获取最热门的模组…」）----
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
    ui.getDownloadCategoryList()->setCurrentRow(MainWindowUI::downloadCategoryModIndex());
    ui.setDownloadHeaderRow(MainWindowUI::downloadCategoryModIndex());
    ui.getDownloadStack()->setCurrentIndex(MainWindowUI::downloadCategoryModIndex());
    QTreeWidget *modTree = ui.getDownloadResourceTree(modKey);
    if (modTree != nullptr) {
        modTree->clear();
        LoadingState::begin(modTree, QStringLiteral("正在获取最热门的模组…"));
        settle(Anim::loadingDelayMs() + Anim::appearMs() + 120);
        toPhase();
        saveShot(app, window, options.out, QStringLiteral("after-block-%1").arg(tag), &saved);
        // ---- 整块出错 ----
        LoadingState::fail(modTree, QStringLiteral("没取到模组列表：连接 Modrinth 超时"), [] {});
        settle(Anim::overlayMs() + 120);
        saveShot(app, window, options.out, QStringLiteral("after-error-%1").arg(tag), &saved);
        LoadingState::finish(modTree);
        settle(Anim::exitMs() + 80);
    }

    // ---- 下载·模组：第三层行内加载（Iris Shaders → 1.21.4 展开在等）----
    fillResourceTree(ui, modKey);
    if (modTree != nullptr && modTree->topLevelItemCount() > 1 && modTree->topLevelItem(1)->childCount() > 0) {
        QTreeWidgetItem *game = modTree->topLevelItem(1)->child(0);
        LoadingState::beginRow(game, QStringLiteral("正在获取…"));
        game->setExpanded(true);
        settle(Anim::loadingDelayMs() + Anim::appearMs() + 120);
        toPhase();
        saveShot(app, window, options.out, QStringLiteral("after-inline-%1").arg(tag), &saved);
    }

    // ---- 安装页加载器树：一条在等、一条没取到（带「重试」）----
    if (QTreeWidget *loaders = ui.getLoaderTree(); loaders != nullptr && ui.getInstallNameEdit() != nullptr) {
        ui.getDownloadCategoryList()->setCurrentRow(MainWindowUI::downloadCategoryGameIndex());
        ui.getDownloadStack()->setCurrentIndex(MainWindowUI::downloadCategoryGameIndex());
        for (QWidget *child = ui.getInstallNameEdit(); child != nullptr && child->parentWidget() != nullptr;
             child = child->parentWidget()) {
            if (auto *stack = qobject_cast<QStackedWidget *>(child->parentWidget())) stack->setCurrentWidget(child);
        }
        if (loaders->topLevelItemCount() > 2) {
            QTreeWidgetItem *waiting = loaders->topLevelItem(1);
            QTreeWidgetItem *failed = loaders->topLevelItem(2);
            qDeleteAll(waiting->takeChildren());
            qDeleteAll(failed->takeChildren());
            LoadingState::beginRow(waiting, MainWindowUI::loaderVersionFetchingText());
            for (const QString &text :
                 {MainWindowUI::loaderVersionLatestText(), MainWindowUI::loaderVersionStableText()})
                new QTreeWidgetItem(failed, {text});
            LoadingState::failRow(LoadingState::beginRow(failed), MainWindowUI::loaderVersionFailedText(), [] {});
            waiting->setExpanded(true);
            failed->setExpanded(true);
        }
        // 页面滚到底：出错那一行在最下面。
        for (QWidget *w = loaders->parentWidget(); w != nullptr; w = w->parentWidget()) {
            if (auto *area = qobject_cast<QScrollArea *>(w)) {
                area->widget()->adjustSize();
                app.processEvents();
                area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
                break;
            }
        }
        settle(Anim::loadingDelayMs() + Anim::appearMs() + 120);
        toPhase();
        saveShot(app, window, options.out, QStringLiteral("after-loader-%1").arg(tag), &saved);
    }

    // ---- 实例页「预设」那一档：点进一份，「预设内容」那一格在读 ----
    // 照产品的嵌法（PresetController::buildPage）：塞进 getInstancePresetPage() 那个宿主。
    if (QWidget *host = ui.getInstancePresetPage(); host != nullptr && host->layout() != nullptr) {
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
        ui.setInstanceMode(MainWindowUI::InstanceMode::Preset);
        auto *page = new PresetManagePage(host);
        page->setWindowFlags(Qt::Widget);
        page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        host->layout()->addWidget(page);
        page->setCategories({{QString(), QStringLiteral("全部"), true},
                             {QStringLiteral("settings"), QStringLiteral("游戏设置"), false},
                             {QStringLiteral("mods"), QStringLiteral("模组"), false}});
        QList<PresetManagePage::PresetItem> presets;
        for (const auto &[id, name, category] :
             {std::tuple{QStringLiteral("p1"), QStringLiteral("生存常用"), QStringLiteral("settings")},
              std::tuple{QStringLiteral("p2"), QStringLiteral("光影与性能"), QStringLiteral("mods")},
              std::tuple{QStringLiteral("p3"), QStringLiteral("建筑按键"), QStringLiteral("settings")}}) {
            PresetManagePage::PresetItem item;
            item.id = id;
            item.name = name;
            item.categoryKey = category;
            item.categoryText = category == QStringLiteral("mods") ? QStringLiteral("模组") : QStringLiteral("游戏设置");
            item.sectionCount = 2;
            presets.append(item);
        }
        page->setPresets(presets, {}, QStringLiteral("p2"));
        page->show();
        // 点中那一份 = 进细节那一层（产品里那条 itemClicked 连接）。
        if (auto *list = page->findChild<QTreeWidget *>(PresetManagePage::presetListName());
            list != nullptr && list->currentItem() != nullptr)
            emit list->itemClicked(list->currentItem(), 0);
        app.processEvents();
        page->showPresetLoading(QStringLiteral("p2"));
        settle(Anim::loadingDelayMs() + Anim::appearMs() + 120);
        toPhase();
        saveShot(app, window, options.out, QStringLiteral("after-preset-%1").arg(tag), &saved);
        page->showPresetReadError(QStringLiteral("preset.json 不是合法的 JSON"));
        settle(Anim::overlayMs() + 120);
        saveShot(app, window, options.out, QStringLiteral("after-preset-error-%1").arg(tag), &saved);
        delete page;
    }
    Anim::setEnabled(false);
    std::printf("\nLoading shots: %d saved -> %s\n", saved, qPrintable(options.out));
    return saved > 0 ? 0 : 1;
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

    Theme::Spec spec = options.theme == QLatin1String("default") ? Theme::load() : Theme::presetSpec(options.theme);
    if (!options.background.isEmpty()) {
        Theme::PartStyle &page = spec.parts[int(Theme::Part::PageBackground)];
        page.hasImage = true;
        page.imagePath = options.background;
    }
    Theme::setSpec(spec);
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
    if (options.loading) return shootLoading(app, window, ui, options);
    if (options.files) return shootFiles(app, window, ui, options);
    if (options.r151) return shootR151(app, window, ui, options);

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
    ui.titleBar()->setCollapsed(true);
    saveShot(app, window, options.out, QStringLiteral("home-rail-collapsed"), &saved);
    ui.titleBar()->setCollapsed(false);

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

    // ---- 设置：启动 / 提示 / 个性化（顶、背景图）/ 下载 ----
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
                if (label->text() == QStringLiteral("背景图")) {
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
        saveShot(app, window, options.out, QStringLiteral("settings-look-background"), &saved);
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
        saveShot(app, window, options.out, QStringLiteral("agent-configured-empty"), &saved);
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

        // 就地改名：顶行标题点一下 / 列表双击，各拍一张编辑态，再按 Esc 收掉。
        const auto escape = [agent](const char *name) {
            if (QWidget *edit = agent->findChild<QWidget *>(QString::fromLatin1(name))) {
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(edit, &esc);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
        };
        if (auto *title = agent->findChild<QLabel *>(QStringLiteral("agentSessionTitle"))) {
            const QPointF at(4, title->height() / 2.0);
            QMouseEvent release(QEvent::MouseButtonRelease, at, title->mapToGlobal(at), Qt::LeftButton, Qt::NoButton,
                                Qt::NoModifier);
            QApplication::sendEvent(title, &release);
            saveShot(app, window, options.out, QStringLiteral("agent-title-rename"), &saved);
            escape("agentTitleRenameEdit");
        }
        QListWidget *list = agent->sessionList();
        for (int i = 0; list != nullptr && i < list->count(); ++i) {
            if (list->item(i)->text() == QStringLiteral("帮我写一条启动参数")) {
                emit list->itemDoubleClicked(list->item(i));
                saveShot(app, window, options.out, QStringLiteral("agent-list-rename"), &saved);
                escape("agentSessionRenameEdit");
                break;
            }
        }
    } else {
        std::printf("  SKIP agent：拿不到 AgentPage\n");
    }

    // ---- 智能体：API 与模型设置（覆盖层整页；顶部 / 滚到底） ----
    {
        AgentSettingsPage settingsPage;
        AgentView::ProviderDraft draft;
        draft.id = QStringLiteral("default");
        draft.name = QStringLiteral("DeepSeek");
        draft.baseUrl = QStringLiteral("https://api.deepseek.com/v1");
        draft.model = QStringLiteral("deepseek-chat");
        settingsPage.setProfile(draft, true, QStringLiteral("credential-manager"));
        if (ui.overlay()->showPage(&settingsPage, QStringLiteral("API 与模型设置"), MainWindowUI::agentTabIndex())) {
            pump();
            saveShot(app, window, options.out, QStringLiteral("agent-settings-top"), &saved);
            if (auto *scroll = settingsPage.findChild<QScrollArea *>()) {
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
                pump();
            }
            saveShot(app, window, options.out, QStringLiteral("agent-settings-bottom"), &saved);
            if (auto *advanced = settingsPage.findChild<SettingsParts::ExpandSection *>()) {
                advanced->setExpanded(true, false);
                pump();
                if (auto *scroll = settingsPage.findChild<QScrollArea *>())
                    scroll->ensureWidgetVisible(advanced->body(), 0, 0);
                pump();
                saveShot(app, window, options.out, QStringLiteral("agent-settings-advanced"), &saved);
            }
            ui.overlay()->closeOverlay();
            pump();
        } else {
            std::printf("  SKIP agent-settings：覆盖层没开出来\n");
        }
    }

    // ---- 工具箱：目录页 + 十个工具页 ----
    ui.openToolboxTab();
    QWidget *host = ui.toolboxTab();
    ToolboxPage *toolbox = nullptr;
    if (host != nullptr && host->layout() != nullptr) {
        toolbox = new ToolboxPage(host);
        host->layout()->addWidget(toolbox);
        wireToolboxSamples(*toolbox);
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

    // ---- 下载 → 游戏 → 安装页（版本名称 + 模组加载器卡片 + 底下的主按钮）----
    if (QLineEdit *installName = ui.getInstallNameEdit()) {
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
        ui.getDownloadCategoryList()->setCurrentRow(MainWindowUI::downloadCategoryGameIndex());
        ui.getDownloadStack()->setCurrentIndex(MainWindowUI::downloadCategoryGameIndex());
        for (QWidget *child = installName; child != nullptr && child->parentWidget() != nullptr; child = child->parentWidget()) {
            if (auto *stack = qobject_cast<QStackedWidget *>(child->parentWidget())) stack->setCurrentWidget(child);
        }
        installName->show();
        pump();
        if (QPushButton *button = ui.getInstallButton())
            std::printf("  install button center x=%d (window %d)\n", button->mapTo(&window, button->rect().center()).x(), window.width());
        saveShot(app, window, options.out, QStringLiteral("download-install"), &saved);
    } else {
        std::printf("  SKIP download-install：拿不到安装页\n");
    }

    // ---- 下载 → 服务端 → 新建服务端：照 MainWindow::openNewServerPage() 嵌进表单宿主，八档照 ServerInstall::allFlavors() ----
    if (QWidget *formHost = ui.getServerFormHost(); formHost != nullptr && formHost->layout() != nullptr) {
        auto *page = new NewServerPage(formHost);
        page->setWindowFlags(Qt::Widget);
        page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        formHost->layout()->addWidget(page);
        QList<NewServerPage::FlavorOption> flavors;
        const auto flavor = [&](const char *token, const QString &name, bool loader, bool installer, bool build) {
            NewServerPage::FlavorOption option;
            option.token = QString::fromLatin1(token);
            option.name = name;
            option.needsLoader = loader;
            option.needsInstaller = installer;
            option.needsBuild = build;
            option.needsLoaderGame = true;
            flavors.append(option);
        };
        flavor("vanilla", QStringLiteral("原版"), false, false, false);
        flavor("forge", QStringLiteral("Forge"), true, false, false);
        flavor("neoforge", QStringLiteral("NeoForge"), true, false, false);
        flavor("fabric", QStringLiteral("Fabric"), true, true, false);
        flavor("quilt", QStringLiteral("Quilt"), false, true, false);
        flavor("paper", QStringLiteral("Paper"), false, false, true);
        flavor("purpur", QStringLiteral("Purpur"), false, false, true);
        flavor("spigot", QStringLiteral("Spigot"), false, false, false);
        page->setFlavors(flavors, QStringLiteral("vanilla"));
        page->setDefaults(QStringLiteral("Server1.21.4"), QStringLiteral("1.21.4"));
        ui.setServerFormIntro(QStringLiteral("将安装 1.21.4 服务端"));
        page->show();
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
        ui.getDownloadCategoryList()->setCurrentRow(MainWindowUI::downloadCategoryServerIndex());
        ui.getDownloadStack()->setCurrentIndex(MainWindowUI::downloadCategoryServerIndex());
        ui.getServerStack()->setCurrentIndex(MainWindowUI::downloadServerFormIndex());
        pump();
        {
            const auto yOf = [&](QWidget *w) { return w != nullptr ? w->mapTo(&window, QPoint(0, 0)).y() : -1; };
            QWidget *nameEdit = page->findChild<QWidget *>(MainWindowUI::newServerNameEditName());
            QWidget *card = page->findChild<QWidget *>(QStringLiteral("newServerIdentity"));
            QWidget *create = page->findChild<QWidget *>(MainWindowUI::newServerCreateButtonName());
            std::printf("  new-server: name edit y=%d h=%d, identity y=%d h=%d, create center x=%d (window %d)\n",
                        yOf(nameEdit), nameEdit != nullptr ? nameEdit->height() : -1, yOf(card),
                        card != nullptr ? card->height() : -1,
                        create != nullptr ? create->mapTo(&window, create->rect().center()).x() : -1, window.width());
            for (QLabel *label : page->findChildren<QLabel *>())
                if (label->text() == QStringLiteral("服务端"))
                    std::printf("  new-server: section title y=%d\n", yOf(label));
        }
        saveShot(app, window, options.out, QStringLiteral("new-server"), &saved);
    } else {
        std::printf("  SKIP new-server：表单宿主挂不上\n");
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
/// 主窗口 + 此刻开着的顶层浮层叠成一张；`-zoom` 那张是浮层四周留 24px、放大两倍。
bool savePopupShot(QApplication &app, QMainWindow &window, const QString &outDir, const QString &name, int *saved) {
    app.processEvents();
    app.processEvents();
    QPixmap shot = window.grab();
    QRect popupArea;
    {
        QPainter painter(&shot);
        for (QWidget *top : QApplication::topLevelWidgets()) {
            const Qt::WindowType type = top->windowType();
            if ((type != Qt::Popup && type != Qt::ToolTip) || !top->isVisible()) continue;
            const QRect at(window.mapFromGlobal(top->mapToGlobal(QPoint(0, 0))), top->size());
            painter.drawPixmap(at.topLeft(), top->grab());
            popupArea = popupArea.united(at);
        }
    }
    if (popupArea.isEmpty()) {
        std::printf("  FAIL %s：没有开着的浮层\n", qPrintable(name));
        return false;
    }
    const QDir dir(outDir);
    const QPixmap zoom = shot.copy(popupArea.adjusted(-24, -24, 24, 24).intersected(shot.rect()));
    const QPixmap zoomed = zoom.scaled(zoom.size() * 2, Qt::KeepAspectRatio, Qt::FastTransformation);
    if (!shot.save(dir.filePath(name + QStringLiteral(".png"))) ||
        !zoomed.save(dir.filePath(name + QStringLiteral("-zoom.png")))) {
        std::printf("  FAIL %s -> %s\n", qPrintable(name), qPrintable(outDir));
        return false;
    }
    ++(*saved);
    std::printf("  %s  %dx%d\n", qPrintable(name), window.width(), window.height());
    return true;
}

int runPopups(QApplication &app, const ShotOptions &options) {
    Theme::Spec spec = options.theme == QLatin1String("default") ? Theme::load() : Theme::presetSpec(options.theme);
    if (!options.background.isEmpty()) {
        Theme::PartStyle &page = spec.parts[int(Theme::Part::PageBackground)];
        page.hasImage = true;
        page.imagePath = options.background;
    }
    Theme::setSpec(spec);
    Theme::apply(app);
    QDir().mkpath(options.out);
    int saved = 0;
    const auto pump = [&app] {
        app.processEvents();
        app.processEvents();
    };
    const auto closePopups = [&pump] {
        for (QWidget *top : QApplication::topLevelWidgets()) {
            if (top->windowType() == Qt::Popup && top->isVisible()) top->close();
        }
        Popups::hideTooltip();
        pump();
    };

    QMainWindow window;
    MainWindowUI ui(&window);
    window.resize(options.size);
    window.show();
    pump();

    // ---- 主页风景底上：右键菜单 + tooltip ----
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
    fillHomeSamples(ui);
    pump();
    {
        QMenu menu(&window);
        menu.addAction(QStringLiteral("打开实例文件夹"));
        menu.addAction(QStringLiteral("复制路径"));
        menu.addSeparator();
        menu.addAction(QStringLiteral("删除实例"));
        menu.popup(window.mapToGlobal(QPoint(window.width() / 2, window.height() / 3)));
        savePopupShot(app, window, options.out, QStringLiteral("home-context-menu"), &saved);
        menu.close();
        pump();
    }
    Popups::showTooltip(QStringLiteral("打开实例文件夹"), window.mapToGlobal(QPoint(window.width() / 3, window.height() / 2)));
    savePopupShot(app, window, options.out, QStringLiteral("home-tooltip"), &saved);
    closePopups();

    // ---- 智能体：实例下拉（用户报黑框的那一块）/ ⋯ 菜单 ----
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::agentTabIndex());
    if (AgentPage *agent = ui.getAgentPage()) {
        agent->setConfigured(true, QStringLiteral("已连接 · OpenAI 兼容"));
        agent->setInstanceChip(QStringLiteral("跟随主页"), false);
        AgentView::SessionMeta session;
        session.id = QStringLiteral("s1");
        session.title = QStringLiteral("查一下崩溃原因");
        agent->setSessions({session});
        agent->setCurrentSession(QStringLiteral("s1"));
        agent->setTitle(session.title);
        agent->showEmptyState(false);
        pump();
        agent->showInstanceMenu({{QStringLiteral("a"), QStringLiteral("1.20.1-Forge 47.2.0")},
                                 {QStringLiteral("b"), QStringLiteral("1.21.4-Fabric 0.16.9")},
                                 {QStringLiteral("c"), QStringLiteral("1.20.4 原版")}},
                                QString());
        savePopupShot(app, window, options.out, QStringLiteral("agent-instance-menu"), &saved);
        closePopups();
        agent->openMoreMenu();
        savePopupShot(app, window, options.out, QStringLiteral("agent-more-menu"), &saved);
        closePopups();
    }

    // ---- 设置页：下拉弹层 ----
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::settingsTabIndex());
    fillSettingsSamples(ui);
    pump();
    for (QComboBox *combo : window.findChildren<QComboBox *>()) {
        if (!combo->isVisible() || combo->count() < 3) continue;
        combo->showPopup();
        savePopupShot(app, window, options.out, QStringLiteral("settings-combo"), &saved);
        combo->hidePopup();
        pump();
        break;
    }
    {
        // 补全列表（实例设置「目标版本」、更新实例那两处的可编辑下拉框）：借设置页当底。
        QComboBox editable(ui.getTabWidget()->currentWidget());
        editable.setEditable(true);
        editable.addItems({QStringLiteral("1.20.1"), QStringLiteral("1.20.4"), QStringLiteral("1.21"),
                           QStringLiteral("1.21.4"), QStringLiteral("24w14a")});
        editable.completer()->setFilterMode(Qt::MatchContains);
        editable.completer()->setCompletionMode(QCompleter::PopupCompletion);
        // 摆在「运行时」小标题那一行的空档里（标题和「Java 管理…」之间），不压任何字。
        editable.setGeometry(120, 82, 320, 32);
        editable.show();
        editable.lineEdit()->setText(QStringLiteral("1.2"));
        editable.completer()->setCompletionPrefix(QStringLiteral("1.2"));
        editable.completer()->complete();
        savePopupShot(app, window, options.out, QStringLiteral("settings-completer"), &saved);
        editable.completer()->popup()->hide();
        pump();
    }

    // ---- 实例页：文件夹浮层 / 分组齿轮菜单 ----
    ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
    fillInstanceTree(ui);
    ui.setFolderRows({{QStringLiteral("D:/Games/.minecraft"), QStringLiteral("官方启动器"), QStringLiteral("D:/Games/.minecraft"), false, true},
                      {QStringLiteral("E:/MC/modpacks"), QStringLiteral("整合包"), QStringLiteral("E:/MC/modpacks"), false, false}});
    pump();
    if (auto *pill = window.findChild<QPushButton *>(MainWindowUI::instanceFolderPillName())) {
        pill->click();
        savePopupShot(app, window, options.out, QStringLiteral("instance-folder-popup"), &saved);
        closePopups();
    }
    if (QTreeWidget *tree = ui.getInstanceList(); tree != nullptr && tree->topLevelItemCount() > 0) {
        MainWindowUI::GroupRow row;
        row.groupId = QStringLiteral("g-modded");
        row.name = QStringLiteral("1.20.1 整合包");
        row.memberCount = 2;
        row.hasSelection = true;
        const QRect head = tree->visualItemRect(tree->topLevelItem(0));
        // 锚点放在行中间：只为看面，贴行尾时 offscreen 那块屏比窗口宽，菜单会伸到窗外拍不全。
        const QRect gear(tree->viewport()->mapToGlobal(QPoint(head.center().x(), head.top())), QSize(28, head.height()));
        ui.openGroupMenu(row, gear);
        savePopupShot(app, window, options.out, QStringLiteral("instance-group-menu"), &saved);
        closePopups();
    }
    // ---- 实例设置·概览：「更改图标」浮层（勾着第 4 颗「幸运方块」）----
    {
        using Dlg = InstanceSettingsDialog;
        Dlg dialog(QStringLiteral("1.20.1 整合包"), QStringLiteral("1.20.1-Forge"), Dlg::Override(),
                   Dlg::GlobalValues(), QStringList(), false);
        dialog.setIcon(VersionIcons::iconValueForKey(VersionIcons::iconCandidateKeys().value(3)), QPixmap(), QPixmap());
        if (ui.overlay()->showPage(&dialog, QStringLiteral("实例设置"), MainWindowUI::instancesTabIndex())) {
            dialog.showPage(Dlg::overviewCategoryKey());
            pump();
            if (auto *change = dialog.findChild<QPushButton *>(Dlg::changeIconButtonName())) {
                change->click();
                // 浮层左缘对齐按钮，按钮贴着页右缘 ⇒ 离屏那张图里会伸出窗口；拍图时往左挪回窗内。
                if (auto *popup = dialog.findChild<QWidget *>(Dlg::iconMenuName())) {
                    const int overflow = popup->frameGeometry().right() - window.mapToGlobal(QPoint(window.width() - 16, 0)).x();
                    if (overflow > 0) popup->move(popup->pos() - QPoint(overflow, 0));
                }
                savePopupShot(app, window, options.out, QStringLiteral("instance-icon-popup"), &saved);
                closePopups();
            }
            ui.overlay()->closeOverlay();
            pump();
        }
    }
    std::printf("popups: %d 张\n", saved);
    return saved > 0 ? 0 : 1;
}

/// `--agent-polish`（步骤 108）：发送钮禁用 / 可用（各带一张放大）、长标题 + 失败行、
/// API 设置页「不认证」态（顶部 + 高级展开）。
/// 离屏没有真鼠标：挂悬停位还不够，按钮自己记着「鼠标在不在形状里」，得喂一次悬停进入 + 移动才认
/// （别 repolish，会把 36 的定高冲掉）。on=false 撤掉。
void fakeHover(QWidget *w, bool on) {
    w->setAttribute(Qt::WA_UnderMouse, on);
    const QPointF at(w->rect().center());
    QHoverEvent hover(on ? QEvent::HoverEnter : QEvent::HoverLeave, at, w->mapToGlobal(at), QPointF(-1, -1));
    QApplication::sendEvent(w, &hover);
    if (!on) return;
    QMouseEvent move(QEvent::MouseMove, at, w->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &move);
}

int runAgentPolish(QApplication &app, const ShotOptions &options) {
    Theme::setSpec(Theme::presetSpec(options.theme));
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
    const QString prefix = options.theme + QLatin1Char('-');
    const auto zoomOf = [&](QWidget *target, const QString &name, const std::function<void()> &prep = {}) {
        pump();
        if (prep) prep();  // 抓图前最后一刻挂状态：pump 里的合成进出事件会清掉悬停位
        const QPixmap shot = window.grab();
        const QRect at(target->mapTo(&window, QPoint(0, 0)), target->size());
        const QPixmap piece = shot.copy(at.adjusted(-40, -28, 16, 16).intersected(shot.rect()));
        if (piece.scaled(piece.size() * 4, Qt::KeepAspectRatio, Qt::FastTransformation)
                .save(QDir(options.out).filePath(name + QStringLiteral(".png"))))
            ++saved;
    };

    ui.getTabWidget()->setCurrentIndex(MainWindowUI::agentTabIndex());
    if (AgentPage *agent = ui.getAgentPage()) {
        agent->setConfigured(true, QStringLiteral("已连接 · OpenAI 兼容"));
        agent->setInstanceChip(QStringLiteral("跟随主页"), false);
        const QString longAsk = QStringLiteral("Forge 1.20.1 启动就崩，日志里说 mixin 冲突");
        AgentView::SessionMeta session;
        session.id = QStringLiteral("s1");
        session.title = AgentSessionTitle::fromMessage(longAsk);
        session.updatedAt = QDateTime::currentDateTime();
        AgentView::SessionMeta blank;
        blank.id = QStringLiteral("s0");
        blank.title = QStringLiteral("新对话");
        blank.updatedAt = QDateTime::currentDateTime().addSecs(-60);
        agent->setSessions({session, blank});

        // 空会话：发送钮禁用 / 打字后可用。
        agent->setCurrentSession(QStringLiteral("s0"));
        agent->showEmptyState(true);
        pump();
        QWidget *send = agent->findChild<QWidget *>(QStringLiteral("agentSendButton"));
        saveShot(app, window, options.out, prefix + QStringLiteral("send-disabled"), &saved);
        if (send != nullptr) zoomOf(send, prefix + QStringLiteral("send-disabled-zoom"));
        auto *composer = agent->findChild<QPlainTextEdit *>();
        if (composer != nullptr) composer->setPlainText(QStringLiteral("为什么启动就崩？"));
        pump();
        saveShot(app, window, options.out, prefix + QStringLiteral("send-enabled"), &saved);
        if (send != nullptr) {
            zoomOf(send, prefix + QStringLiteral("send-enabled-zoom"));
            zoomOf(send, prefix + QStringLiteral("send-hover-zoom"), [send] { fakeHover(send, true); });
            if (auto *button = qobject_cast<QPushButton *>(send)) {
                button->setDown(true);
                zoomOf(send, prefix + QStringLiteral("send-pressed-zoom"));
                button->setDown(false);
            }
            fakeHover(send, false);
        }
        if (composer != nullptr) composer->clear();

        // 长标题 + 发送失败那一行（只在消息行上出一次，带「重试」）。
        agent->setCurrentSession(QStringLiteral("s1"));
        agent->setTitle(session.title);
        AgentView::Message ask;
        ask.id = QStringLiteral("m1");
        ask.user = true;
        ask.content = longAsk;
        AgentView::Message failed;
        failed.id = QStringLiteral("m2");
        failed.status = AgentView::MessageStatus::Error;
        failed.errorText = QStringLiteral("模型服务拒绝连接。");
        agent->setMessages({ask, failed});
        agent->showEmptyState(false);
        saveShot(app, window, options.out, prefix + QStringLiteral("long-title-failed"), &saved);

        // 生成中（步骤 110）：停止钮常态 / 悬停 / 按下。
        AgentView::Message streaming;
        streaming.id = QStringLiteral("m2");
        streaming.status = AgentView::MessageStatus::Streaming;
        streaming.content = QStringLiteral("先看日志里 mixin 冲突那几行：");
        agent->setMessages({ask, streaming});
        agent->setStreaming(true);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);  // 旧行 deleteLater，离屏手动收
        pump();
        saveShot(app, window, options.out, prefix + QStringLiteral("streaming"), &saved);
        if (auto *stop = agent->findChild<QPushButton *>(QStringLiteral("agentStopButton"))) {
            zoomOf(stop, prefix + QStringLiteral("stop-zoom"));
            zoomOf(stop, prefix + QStringLiteral("stop-hover-zoom"), [stop] { fakeHover(stop, true); });
            stop->setDown(true);
            zoomOf(stop, prefix + QStringLiteral("stop-pressed-zoom"));
            stop->setDown(false);
            fakeHover(stop, false);
        }
        agent->setStreaming(false);
    }

    // API 与模型设置：认证方式「不认证」、没存密钥。
    {
        AgentSettingsPage settingsPage;
        AgentView::ProviderDraft draft;
        draft.id = QStringLiteral("default");
        draft.name = QStringLiteral("Local test");
        draft.baseUrl = QStringLiteral("http://127.0.0.1:9/v1");
        draft.model = QStringLiteral("test-model");
        draft.authMode = QStringLiteral("none");
        draft.deployment = QStringLiteral("local");
        settingsPage.setProfile(draft, false, QString());
        if (ui.overlay()->showPage(&settingsPage, QStringLiteral("API 与模型设置"), MainWindowUI::agentTabIndex())) {
            pump();
            saveShot(app, window, options.out, prefix + QStringLiteral("api-noauth-top"), &saved);
            if (auto *advanced = settingsPage.findChild<SettingsParts::ExpandSection *>()) {
                advanced->setExpanded(true, false);
                pump();
                if (auto *scroll = settingsPage.findChild<QScrollArea *>())
                    scroll->ensureWidgetVisible(advanced->body(), 0, 0);
                saveShot(app, window, options.out, prefix + QStringLiteral("api-noauth-advanced"), &saved);
            }
            ui.overlay()->closeOverlay();
            pump();
        }
    }
    std::printf("agent-polish: %d 张\n", saved);
    return saved > 0 ? 0 : 1;
}

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
            ui.getMemoryHint()->setText(QStringLiteral("系统总内存 31.5 GB，当前可用 14.5 GB；最小内存由启动器设为 1024 MB。"));
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
        } else if (arg == QLatin1String("--popups")) {
            options.popups = true;
        } else if (arg == QLatin1String("--loading")) {
            options.loading = true;
            options.extended = true;
        } else if (arg == QLatin1String("--files")) {
            options.files = true;
            options.extended = true;
        } else if (arg == QLatin1String("--r151")) {
            options.r151 = true;
            options.extended = true;
        } else if (arg == QLatin1String("--first-disable")) {
            options.firstDisable = true;
        } else if (arg == QLatin1String("--agent-polish")) {
            options.agentPolish = true;
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
        } else if (arg.startsWith(QLatin1String("--background="))) {
            options.background = valueOf(QStringLiteral("--background="));
        } else if (arg == QLatin1String("--background") && i + 1 < args.size()) {
            options.background = args.at(++i);
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
    if (options.agentPolish) {
        if (options.theme.isEmpty()) options.theme = QStringLiteral("dark");
        if (!options.size.isValid() || options.size.width() <= 0) options.size = QSize(1280, 800);
        return runAgentPolish(app, options);
    }
    if (options.popups) {
        if (options.theme.isEmpty()) options.theme = QStringLiteral("dark");
        if (!options.size.isValid() || options.size.width() <= 0) options.size = QSize(1280, 800);
        return runPopups(app, options);
    }
    if (options.extended) {
        if (options.theme.isEmpty()) options.theme = QStringLiteral("dark");
        if (!options.size.isValid() || options.size.width() <= 0 || options.size.height() <= 0)
            options.size = QSize(1280, 800);
        return runExtended(app, options);
    }
    return runLegacy(app, options.out);
}
