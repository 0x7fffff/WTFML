#include "InstanceDialogController.hpp"

#include "MainWindow.hpp"
#include "ConfigFileEditorPage.hpp"
#include "InlineMessage.hpp"
#include "InstanceSettingsDialog.hpp"
#include "InstanceFilesPage.hpp"
#include "MainWindowUI.hpp"
#include "OverlayHost.hpp"
#include "WorldSettingsPage.hpp"
#include "VersionIcons.hpp"

#include "controllers/BackupController.hpp"
#include "controllers/InstancePagesController.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/FolderManager.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/InstanceGroupStore.hpp"
#include "download/VersionCategory.hpp"
#include "manage/VersionDisplay.hpp"
#include "manage/VersionManager.hpp"
#include "settings/EffectiveJavaSettings.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "settings/GameWindowSettings.hpp"
#include "settings/LauncherSettings.hpp"
#include "util/AppPaths.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTabWidget>
#include <QTimer>

namespace {

InstanceSettingsDialog::Override toDialogOverride(
    const JavaSettingsOverride &from, const GameWindowOverride &window = GameWindowOverride()) {
    InstanceSettingsDialog::Override to;
    to.javaPath = from.javaPath;
    to.xmx = from.xmx;
    to.xms = from.xms;
    to.autoMemory = from.autoMemory;
    to.extraJvmArgs = from.extraJvmArgs;
    if (window.mode.has_value()) {
        to.windowMode = gameWindowModeToken(*window.mode);
    }
    to.windowWidth = window.width;
    to.windowHeight = window.height;
    return to;
}

InstanceSettingsDialog::GlobalValues toDialogGlobal(const EffectiveJavaSettings &from) {
    InstanceSettingsDialog::GlobalValues to;
    to.javaPath = from.javaPath;
    to.xmx = from.xmx;
    to.xms = from.xms;
    to.autoMemory = from.autoMemory;
    to.extraJvmArgs = from.extraJvmArgs;
    return to;
}

QList<InstanceSettingsDialog::WindowModeChoice> windowModeChoices() {
    return {{QObject::tr("小窗（按下面的大小开一个窗口）"),
             gameWindowModeToken(GameWindowMode::Windowed)},
            {QObject::tr("最大化（铺满屏幕，任务栏留出来）"),
             gameWindowModeToken(GameWindowMode::Maximized)},
            {QObject::tr("全屏（原版自己的 --fullscreen）"),
             gameWindowModeToken(GameWindowMode::Fullscreen)}};
}

QString windowModeLabel(GameWindowMode mode) {
    switch (mode) {
    case GameWindowMode::Maximized: return QObject::tr("最大化");
    case GameWindowMode::Fullscreen: return QObject::tr("全屏");
    case GameWindowMode::Windowed: break;
    }
    return QObject::tr("小窗");
}

VersionCategory::Channel versionChannelOf(const VersionManager *versions,
                                           const QString &versionId) {
    QString type = versions == nullptr
                       ? QString()
                       : versions->versionMeta(versionId).value(QStringLiteral("type")).toString();
    if (type.isEmpty() && versions != nullptr && QFile::exists(InstanceFiles::jsonPath(versionId))) {
        type = versions->getVersionJson(versionId).value(QStringLiteral("type")).toString();
    }
    return VersionCategory::channelFor(versionId,
                                       type.isEmpty() ? QStringLiteral("release") : type);
}

QPixmap defaultInstanceMark(const VersionDisplayParts &part, const VersionManager *versions,
                            int size) {
    const QString loader = InstancePagesController::loaderName(part, versions);
    if (!loader.isEmpty()) {
        for (int index = 0; index < MainWindowUI::modLoaderNoneIndex(); ++index) {
            VersionManager::LoaderKind kind;
            if (VersionManager::loaderKindFromIndex(index, &kind)
                && VersionManager::loaderDisplayName(kind) == loader) {
                return VersionIcons::loaderMark(index, size);
            }
        }
        return VersionIcons::grassBlock(size);
    }
    return VersionIcons::versionChannelMark(VersionCategory::channelIndex(
                                                versionChannelOf(versions, part.id)),
                                            size);
}

QSet<QString> knownGroupIdsOf(const InstanceGroupStore *groups) {
    QSet<QString> known;
    for (const InstanceGroupStore::Group &group : groups->groups()) known.insert(group.id);
    return known;
}

QString defaultGroupIdFor(const VersionDisplayParts &part, const VersionManager *versions,
                          const QSet<QString> &known) {
    const auto existing = [&known](InstanceGroupStore::DefaultKind kind) {
        const QString id = InstanceGroupStore::defaultGroupId(kind);
        return known.contains(id) ? id : QString();
    };
    if (!InstancePagesController::loaderName(part, versions).isEmpty())
        return existing(InstanceGroupStore::DefaultKind::Moddable);
    switch (versionChannelOf(versions, part.id)) {
    case VersionCategory::Channel::Release: return existing(InstanceGroupStore::DefaultKind::Vanilla);
    case VersionCategory::Channel::Snapshot: return existing(InstanceGroupStore::DefaultKind::Snapshot);
    case VersionCategory::Channel::Old: return existing(InstanceGroupStore::DefaultKind::Old);
    case VersionCategory::Channel::AprilFools: break;
    }
    return {};
}

QString displayGroupIdOf(const InstanceGroupStore *groups, const QString &key,
                         const VersionDisplayParts &part, const VersionManager *versions,
                         const QSet<QString> &known) {
    QString groupId = groups->groupIdOf(key);
    if (groupId.isEmpty() && !groups->isAssigned(key))
        groupId = defaultGroupIdFor(part, versions, known);
    return groupId;
}

QString homeGroupIdOf(const InstanceGroupStore *groups, const QString &key,
                      const VersionDisplayParts &part, const VersionManager *versions,
                      const QSet<QString> &known) {
    if (!groups->isPinned(key)) return displayGroupIdOf(groups, key, part, versions, known);
    QString origin;
    switch (groups->pinOriginOf(key, &origin)) {
    case InstanceGroupStore::PinOrigin::None: return {};
    case InstanceGroupStore::PinOrigin::Auto: return defaultGroupIdFor(part, versions, known);
    case InstanceGroupStore::PinOrigin::Group: break;
    }
    if (origin == InstanceGroupStore::ungroupedMarker()) return {};
    return known.contains(origin) ? origin : QString();
}

QList<InstanceFilesPage::StructureSource> structureSourcesFor(
    const InstancePagesController::WorldStructures &worlds, bool litematica,
    const QString &schematicsDir) {
    QList<InstanceFilesPage::StructureSource> sources;
    if (!worlds.labels.isEmpty() || litematica) {
        InstanceFilesPage::StructureSource world;
        world.title = InstanceFilesPage::structureWorldTitle();
        world.labels = worlds.labels;
        world.paths = worlds.paths;
        sources.append(world);
    }
    if (litematica) {
        InstanceFilesPage::StructureSource projections;
        projections.title = InstanceFilesPage::structureSchematicTitle();
        projections.labels.append(InstanceFilesPage::structureSchematicText());
        projections.paths.append(schematicsDir);
        sources.append(projections);
    }
    return sources;
}

} // namespace

InstanceDialogController::InstanceDialogController(MainWindow *owner)
    : QObject(owner), m_owner(owner) {}

void InstanceDialogController::open(const QString &versionId, const QString &pageKey) {

    // 已经写了的那部分覆盖从 meta 里读回来当起点，「当前全局是哪个值」由 global 提供。
    // 读归读，**这里一个字节都不写** —— 写盘只发生在下面那两条接线里（2026-09-26 起这一页
    // 没有「确定」，改一格就写一格）。
    // NOTE: 窗口那一组覆盖读的是**同一份 meta**（一次读出来、两处用），不然这一页打开一次
    //       就要把同一个文件解析两遍。
    const QJsonObject instanceMeta = m_owner->m_versionManager->versionMeta(versionId);
    const JavaSettingsOverride current = javaOverrideFromJson(instanceMeta);
    // 第二个参数是**目录名**（改名要改的就是它），第一个是显示名（页眉上那行字）——
    // 两个值可以不一样（自定义名、拼进去的加载器名），页面上是分开的两行。
    InstanceSettingsDialog::GlobalValues global = toDialogGlobal(m_owner->m_javaSettings->effective());
    // 全局那一档窗口（模式 + 分辨率）：窗口那两行的「跟随全局（当前 …）」写的就是这一份。
    const GameWindowSettings globalWindow = m_owner->m_settings->gameWindowSettings();
    global.windowMode = gameWindowModeToken(globalWindow.mode);
    global.windowModeLabel = windowModeLabel(globalWindow.mode);
    global.windowWidth = globalWindow.width;
    global.windowHeight = globalWindow.height;
    // 「最大内存」那一格能拖到哪儿 = **设置页那一格能拖到哪儿**，取的是设置页那个数字框现在的
    // 上限（开窗时 applyMemoryLimits 已经按这台机器的物理内存收过口，见 refreshMemoryUi）。
    // 不在这儿另读一次系统内存，是为了两页**逐字同一个数**：用户刚在设置页看见"最多 16256"，
    // 切到实例页又是另一个上限的话，两页各说各的。
    global.memoryCapMb = m_owner->m_ui->getMemoryMaxSpin()->maximum();
    InstanceSettingsDialog dialog(m_owner->instanceDisplayName(versionId), versionId,
                                  toDialogOverride(current, gameWindowOverrideFromJson(instanceMeta)),
                                  global, m_owner->m_versionManager->versionJvmArgs(versionId),
                                  !m_owner->m_versionManager->skipJavaVersionWarning(versionId),
                                  windowModeChoices(), m_owner);
    connect(&dialog, &InstanceSettingsDialog::launchInstanceRequested, m_owner,
            [this, versionId]() {
                QComboBox *combo = m_owner->m_ui->getInstalledVersionCombo();
                const int row = combo == nullptr ? -1 : combo->findData(versionId);
                if (row >= 0) {
                    combo->setCurrentIndex(row);
                    m_owner->onStartButtonClicked();
                }
            });
    // ---------- 概览栏那四行的初值（2026-09-26）----------
    //
    // 三张图标**由这里解好递进去**：ui/ 不做"这个实例默认该长什么样"那个判断（它要读版本
    // json 的 type 和加载器名，是业务层的事），也够不着实例目录下那份 icon.png 的名字
    // （AppPaths 在 src/ 里）。这一层解好、那一层只管画。
    const VersionDisplayParts parts = m_owner->m_versionManager->displayParts(versionId);
    const int previewSize = InstanceSettingsDialog::iconPreviewSize();
    QPixmap localIcon;
    if (parts.icon == InstanceSettingsDialog::localIconValue()) {
        localIcon = VersionIcons::fileMark(AppPaths::versionIconPath(versionId), previewSize);
    }
    dialog.setIcon(parts.icon, defaultInstanceMark(parts, m_owner->m_versionManager, previewSize), localIcon);

    // 「分组…」那一格：候选项是**现在所有的组**（含空组 —— 「往空组里放第一个实例」正是用户
    // 会干的事，那是它唯一的入口之一），当前值是那个实例的**老家**。
    //
    // NOTE: 走 homeGroupIdOf() 而**不是** displayGroupIdOf()。对没置顶的实例两者逐字相同；
    //       对置顶着的那一个，displayGroupIdOf() 答的是置顶组（盘上 members 指着它），而
    //       用户在这一格里说的永远是"它本来属于哪儿" —— 摆上置顶组的话，用户点开看见
    //       「置顶」被灰着、还以为是这一格出了问题，同时"想给它换个老家"这件事**做不了**
    //       （选了别的组又会被 applyInstanceSettings 当成"老家改了"，而它本来就是"老家改了"，
    //       只是起点显示错了）。
    const QSet<QString> knownGroupIds = knownGroupIdsOf(m_owner->m_groupStore);
    QList<QPair<QString, QString>> groupChoices;
    for (const InstanceGroupStore::Group &group : m_owner->m_groupStore->groups()) {
        groupChoices.append(qMakePair(group.name, group.id));
    }
    const QString key = m_owner->instanceKey(versionId);
    const QString groupAtOpen = homeGroupIdOf(m_owner->m_groupStore, key, parts, m_owner->m_versionManager,
                                              knownGroupIds);
    dialog.setGroupChoices(groupChoices, groupAtOpen);

    // 置顶那一行：判据是"它此刻在不在置顶组里"（isPinned()，不是"老家等于置顶组"——
    // 两边都显示之后那两句话不再等值：老家的的确确可以和置顶组是同一个组，而那时它
    // 也照样是置顶着的）。没有置顶组（用户把它删了）时恒假，而且那一格会灰掉
    // （见 setPinned()）。
    const QString pinnedGroupId = m_owner->m_groupStore->pinnedGroupId();
    dialog.setPinned(m_owner->m_groupStore->isPinned(key), pinnedGroupId);

    // ---------- 「文件管理」那八页的目录与可用性（2026-09-26）----------
    //
    // **所有路径都由这一层算好递进去**：那几个目录名是 MC 的约定，出处是
    // ResourceCatalog::folderForKind()（见 m_owner->instanceFileDirs()），ui/ 不抄第二份。
    //
    // ★ 收成一个 lambda 而不是直接写四句：**改完名要整份重算**（下面那条改名线里调它）。
    //   改名会把整个实例换到另一个目录，那八条路径、那两页的可用性、"这九颗按钮开的是
    //   哪儿"全都跟着换 —— 不重算的话，用户改完名点「模组」，看的是那个**已经不在了**
    //   的老目录；再点一下「打开文件夹」，`mkpath()` 会把老目录重新建出来（一个只有几个
    //   空文件夹的空壳实例）。这是改名和"实时写"凑到一起才会露出来的一类坑。
    //
    // NOTE: 它按**引用**收 targetId（而不是收一个 QString）：改名之后那个值变了，重算
    //       必须看新值。下面那些 lambda 也是同一个理由全都按引用捕获它。
    QString targetId = versionId;
    const auto refreshInstanceDirs = [this, &dialog](const QString &id) {
        // ★ 2026-10-15：这一段"算"的活全在 `InstancePagesController` 里了（哪几页用得上、
        //   那几页指向哪个目录、有结构的存档是哪几个 —— 原来一百来行判断就夹在这儿）。
        //   这一层剩下的**只有摆**：把那一份事实挨个 `set` 到对话框上。
        //
        // ★ 2026-10-11：`fileDirs()` 每跑一趟都要新建一个九项的 QHash（含九次 QString
        //   拼接）—— 这一趟要用它两次（`setFileDirs` 和下面 structure 那一格），算一次存下来。
        const QHash<QString, QString> fileDirs = InstancePagesController::fileDirs(id);
        dialog.setInstanceDir(AppPaths::versionDir(id));
        // ★ 2026-10-13（coding plan §5.3「实例工具入口补齐」）：概览那排「常用目录」多要
        //   三路 —— 游戏目录 / 录像 / 崩溃报告。**并进同一份一起灌**（见 InstanceSettingsDialog
        //   里 setFileDirs() 那条 NOTE：这一层只是"按某个键取一个路径"，多出来的键没人当页读），
        //   不去动 fileDirs() 的形状（它那个九条有断言在数）。
        QHash<QString, QString> shortcutDirs = fileDirs;
        shortcutDirs.insert(QStringLiteral("game"), AppPaths::minecraftDir());
        shortcutDirs.insert(QStringLiteral("replay"), AppPaths::replayRecordingsDir());
        shortcutDirs.insert(QStringLiteral("crash"), AppPaths::crashReportsDir());
        dialog.setFileDirs(shortcutDirs);
        // 那排按钮分两段的判据（哪几路"现在真有东西"）—— 理由与逐条判据见
        // InstancePagesController::commonFolderKeys()。**必须紧跟着 setFileDirs()**：它按
        // 这一份路径算。
        dialog.setFolderPresentKeys(InstancePagesController::commonFolderKeys(
            id, shortcutDirs, InstanceSettingsDialog::shortcutPathKeys()));
        // 左栏那一列是**按实例现算**的（2026-09-27）：这几页要"用得上"才列出来。四个判据
        // 全在业务层 —— loadername / mods 目录 / 装没装那个 mod / 游戏版本够不够新，ui/
        // 一样都够不着。
        //
        // ★ 四个 setter 的**默认都是"可用"**（直接 new 出来的对话框七条全列）：忘了调哪一个，
        //   那个实例上就会多列出一页来 —— 而多一页是"用户点进去看见一句用不上"，不是静默
        //   的错。所以宁可这样默认，也不要反过来（默认不可用 = 忘了调就把一整页藏了）。
        //
        // NOTE: 「模组」的判据是 `modsUsable()`（有模组加载器 **或** mods/ 里有 jar）
        //       ⇒ **OptiFine 也算**，装 OptiFine 的实例会多一条「模组」。这是有意的：
        //       OptiFine 从 1.13 起就是塞进 mods/ 的，那一页在那样的实例上真的用得上。
        //       ★ 2026-09-27：原来这里写的是 `displayParts().loaderName` 非空，而那一格
        //       只有我们自己的安装器会写 ⇒ 用户盘上 70 个实例里 67 个（全是 PCL 装的）
        //       被误判成"没有模组加载器"。判据与理由见那个函数那段说明。
        const InstancePagesController::Usability usable =
            InstancePagesController::usability(m_owner->m_versionManager, id);
        dialog.setModUsable(usable.mods);
        dialog.setShaderUsable(usable.shader);
        // ★ 2026-10-10：「结构/原理图」那一页顶上那两排「来源」（用户原话：「两个下拉框
        //   并排…上面一排是存档的，下面一排是投影的」）。**判定全在业务层**（哪些存档真有
        //   结构要读盘、装没装投影要查 mods/），摆控件的是那一页自己。
        //   NOTE: 和上面那四个 setter 一样收在这个 lambda 里 —— 改名会把整个实例换到另一个
        //         目录，哪些存档有结构也就跟着换一份。
        dialog.setSchematicUsable(usable.schematic);
        if (InstanceFilesPage *schematics = dialog.filesPage(QStringLiteral("schematic"))) {
            // 原理图与原版结构已拆为独立页面；原理图只列实例级 schematics 目录。
            schematics->setStructureSources({});
        }
        dialog.setDatapackUsable(usable.datapack);
        // ★ 2026-10-10：那两颗「加入…」（**临时占位**，见 QuickPlay）。
        //   **露不露** = 这个版本认不认那两条参数 —— 判据只有 `m_owner->instanceSupportsQuickPlay()`
        //   一处（和 `m_owner->startQuickPlay()` 的第一道闸是同一句话）。空串 = 那一颗整颗收起来。
        //   NOTE: 收在这里（而不是摆在各页自己的构造里）的理由和上面那四个 setter 逐字
        //         相同：这一层才知道"这个实例是哪个版本"，而改名会让 id 换一个。
        //   NOTE: **服务器那一页**的那颗要等 `m_owner->loadServerList()` 之后的选中状态，所以它自己
        //         那两句摆在最下面（见 `updateServerSelectionActions()`）。
        dialog.setServerJoinText(
            m_owner->instanceSupportsQuickPlay(id, QuickPlay::Kind::Multiplayer)
                ? InstanceSettingsDialog::serverJoinText()
                : QString(),
            InstanceSettingsDialog::serverJoinTip());
        if (InstanceFilesPage *saves = dialog.filesPage(QStringLiteral("save"))) {
            saves->setJoinText(m_owner->instanceSupportsQuickPlay(id, QuickPlay::Kind::Singleplayer)
                                   ? InstanceFilesPage::joinText()
                                   : QString(),
                               InstanceFilesPage::joinTip());
        }
        m_owner->loadServerList(&dialog, id);
        // 那几页里那些资源文件（模组 / 资源包 / 数据包 / 光影包）的图标（2026-09-27）：
        // 按文件内容反查，**在后台线程上跑**，回来了一个一个换上去（见那一支）。
        // 摆在这儿而不是摆在开窗那一刻：改名会让整个实例换一个目录，那批路径跟着全换，
        // 图标也得按新路径重认一遍（旧的那一页那时还画着老目录那些条的图）。
        m_owner->requestInstanceFileIcons(&dialog, id);
        // ★ 2026-09-28：上一趟「检查更新」查出来的结果也在这一格上摆回来（见
        //   pushInstanceFileUpdates / m_owner->m_instanceOutdatedPaths）。不摆的话，用户查一次、
        //   关掉这一页、再进来，「可更新」那一格就变回"还没查过"—— 而那趟检测刚跑完。
        //   NOTE: 和上面那几行一样写在 `refreshInstanceDirs` 这一支里（换实例时重喊）。
        m_owner->pushInstanceFileUpdates(&dialog, id);
    };
    refreshInstanceDirs(targetId);

    // 用户自己勾掉的那几页（2026-09-27）——**必须排在 refreshInstanceDirs() 之后**：
    // 灌进来的那份表是"藏起来的页"，而"这一条到底露不露"要拿"可不可用"一起算（见
    // InstanceSettingsDialog::shownEntryKeys()）。反过来的话，第一遍重建会先把不可用
    // 那几条全列出来、再被这一发推回去 —— 中间那一下会顺手把右栏换到某页上
    // （rebuildEntryList() 里那个"当前页没了就回第一页"）。
    dialog.setPageChoices(
        m_owner->m_versionManager->versionMeta(targetId).value(VersionManager::settingsPagesField())
            .toObject());

    // 两条**动作**信号（2026-09-26）：这一页里那两颗按钮（导出启动脚本 / 补全文件）不自己
    // 干活 —— ui/ 那边不 include src/、也不碰磁盘，"这一次到底执行什么"只有这一层知道
    // （authManager / versionManager / launcherManager 都在这儿）。
    //
    // 接在 execPage **之前**：那一下会阻塞在一个嵌套事件循环里（用户在页面上点按钮时，
    // 这一句还没返回），接晚了就一辈子收不到。
    //
    // NOTE: dialog 是**栈上的局部对象**，所以这几条 connect 用 lambda 捕获它和 targetId ——
    //       这里没有 m_owner->m_instanceSettingsDialog 那种成员（也刻意不加：加了就得管生命周期，
    //       而这一页每次都是新开一个）。
    connect(&dialog, &InstanceSettingsDialog::exportLaunchScriptRequested, m_owner,
            [this, &targetId](const QString &path) { m_owner->exportLaunchScript(targetId, path); });
    connect(&dialog, &InstanceSettingsDialog::repairFilesRequested, m_owner,
            [this, &targetId]() { m_owner->repairInstanceFiles(targetId); });
    // 第三条同类：概览那一栏的「导出…」（2026-09-26 从灰占位换成真的）。**和上面第一条
    // 不是一件事** —— 那个出的是 .bat，这个出的是整合包。
    connect(&dialog, &InstanceSettingsDialog::exportModpackRequested, m_owner,
            [this, &targetId](const QString &path) { m_owner->exportModpack(targetId, path); });

    // 第四条同类：概览那一栏的「检查更新」（2026-09-28，用户原话「实例设置那里做一个更新
    // 检测」）。**结果是一个弹框**（Prompt 那一档），所以这一发是**异步**的：反查要读几十兆
    // jar（后台线程），每个项目还要各问一发版本列表 —— 那几发回来的时候这一页可能还开着
    // （正常），也可能已经被用户收掉了（那时候 `m_owner->m_ui->overlay()` 上摆着的是别的页面）。
    //
    // NOTE: 收页了也**照样弹**（不判断这一页还在不在）：用户按下这一颗按钮之后，那个框
    //       就是他等的那个结果 —— 把它吞掉等于"按了没反应"。这一条和文件管理那几处
    //       "动完盘得刷新那一页"不一样：那些是**改这一页的样子**，这一件是把结果**告诉人**。
    connect(&dialog, &InstanceSettingsDialog::checkUpdatesRequested, m_owner,
            [this, &targetId, &dialog]() { m_owner->checkInstanceUpdates(targetId, &dialog); });

    // 「更新中心」（coding plan §7.1，2026-10-13）：紧挨着「检查更新」的那一颗。
    //
    // ★ **和上面那一条相反，这一颗不收页**：它开的那一页（更新中心）自己就是盖在内容区上
    //   的一整页，而这一颗要先把当前这一页**收掉**才开得进去（`m_owner->runWhenOverlayFree()` 会
    //   静默收掉占着的那一层 —— 用户 2026-09-29 定的方向）。
    // ★ 这一颗**一个网络都不发**（它读的是上一次检测留下来的那份结论）：所以断网的时候
    //   「撤销上一次更新」也按得动 —— 那正是它存在的意义（见 InstanceSettingsDialog 里
    //   那个信号的说明）。
    connect(&dialog, &InstanceSettingsDialog::updateCenterRequested, m_owner,
            [this, &targetId]() {
                const QString id = targetId;
                m_owner->runWhenOverlayFree([this, id] { m_owner->openUpdateCenter(id); });
            });

    // 「检查并修复」（coding plan §4.1，2026-10-12）：概览栏那颗按钮。**和「检查更新」不是
    // 一回事** —— 那个查的是模组有没有新版本（联网），这个查的是游戏文件齐不齐（只读盘）。
    //
    // 收页再起：点下这一颗就说明用户要看的是"体检结果"，而结果会开成**另一整页**（报告页）。
    // 留着这一页的话，扫完那一下要么叠一层、要么得走"让位"（见 onIntegrityScanFinished 那段）；
    // 先把它收掉最干净。**排到下一轮再起**：体检起手那几道守卫失败时会往**同一条覆盖层**上
    // 弹提示，现在不排的话会撞上还没收干净的这一页。
    connect(&dialog, &InstanceSettingsDialog::integrityCheckRequested, m_owner,
            [this, &targetId, &dialog]() {
                dialog.reject();  // 和顶上那颗「← 返回」是同一条路
                const QString id = targetId;
                QTimer::singleShot(0, m_owner, [this, id] { m_owner->submitIntegrityCheck(id); });
            });

    // ---------- §4.4：概览那一栏那两颗「快照」----------
    //
    // ★ 这两颗**不收页**（和上面「检查并修复」故意不一样）：体检的结果是**另一整页**（报告页），
    //   不收这一页就叠了两层；而快照 / 还原的结果是一个弹框 + 账本里那一行 —— 留着这一页
    //   正好让用户接着做别的（跟「导出…」那颗一样）。
    //
    // NOTE: 「创建快照…」那一问（默认排除还是完整）在 `m_owner->snapshotInstance()` 里 —— 弹话要
    //       OverlayHost，那玩意儿在 MainWindow 手里，这一层连读都没有读。
    connect(&dialog, &InstanceSettingsDialog::createSnapshotRequested, m_owner,
            [this, &targetId]() { m_owner->snapshotInstance(targetId); });
    connect(&dialog, &InstanceSettingsDialog::restoreSnapshotRequested, m_owner,
            [this, &targetId]() { m_owner->restoreInstanceSnapshot(targetId); });

    // 「文件管理」那三发（2026-09-26）：开文件夹 / 启用·禁用一条 / 删一条。
    //
    // NOTE: 这两条**要多捕获一个 &dialog** —— 动完盘得让那几页重列一遍（不然删掉的那一行
    //       还在屏幕上，用户会以为没删掉）。dialog 是栈上的局部对象、这一句又写在
    //       execPage 之前，所以引用一直活着；那几发也只在 execPage 期间可能发生。
    connect(&dialog, &InstanceSettingsDialog::backupSelectedWorldRequested, m_owner,
            [this](const QString &path) { m_owner->backupWorldNow(path); });
    connect(&dialog, &InstanceSettingsDialog::restoreSelectedWorldRequested, m_owner,
            [this, &dialog](const QString &path) { m_owner->m_backupController->restoreWorld(path, &dialog); });
    connect(&dialog, &InstanceSettingsDialog::openInstanceFileRequested, m_owner,
            [this](const QString &path) {
                if (!QFileInfo(path).isFile() || !QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
                    InlineMessage::warning(m_owner->m_ui->overlay(), tr("打开文件"),
                                           tr("无法打开文件：%1").arg(QDir::toNativeSeparators(path)));
                }
            });
    connect(&dialog, &InstanceSettingsDialog::openInstanceFolderRequested, m_owner,
            [this](const QString &path) {
                if (path.isEmpty()) {
                    return;
                }
                // 先建出来再开：新实例的 saves/ screenshots/ 多半还不存在，而这九颗按钮
                // 就是给"我要往里面放东西"用的 —— 在这儿弹一句"这个文件夹不在盘上"
                // 正好挡住用户要做的事。建一个空目录也正是游戏第一次跑起来会做的事。
                QDir().mkpath(path);
                m_owner->onFolderOpenRequested(path);
            });
    connect(&dialog, &InstanceSettingsDialog::toggleInstanceFileRequested, m_owner,
            [this, &dialog](const QString &path) {
                if (m_owner->toggleInstanceFile(path)) {
                    dialog.refreshFilePages();
                }
            });
    connect(&dialog, &InstanceSettingsDialog::deleteInstanceFileRequested, m_owner,
            [this, &dialog](const QString &path) {
                if (m_owner->deleteInstanceFile(path)) {
                    dialog.refreshFilePages();
                }
            });

    // ★ 2026-10-09（需求 2）：那四页上「选中之后悬浮出来的那一条」里的三颗 —— 更新 /
    //   禁用·启用一把 / 删除一把。和上面那两条**不是一回事**：那两条是**行尾**那一颗开关
    //   和那一颗垃圾桶发出来的**单条**动作，这三条是**悬浮条**发出来的**一批**。
    //
    //   ui/ 那边只管"用户要动这一批"（它不 include src/、也不碰磁盘），到底下什么、
    //   每条落哪儿、名字怎么改，全在这一层（和上面那两条同一个分工）。
    connect(&dialog, &InstanceSettingsDialog::fileBatchUpdateRequested, m_owner,
            [this, &targetId](const QString &pageKey, const QStringList &paths) {
                // `targetId` **按引用**（和上面那批 lambda 同一个理由，见 refreshInstanceDirs
                // 的 NOTE）：改名之后它变了，而这一发要更新的是**改名之后**那个实例里的
                // 文件。**不刷新那一页** —— 理由见 m_owner->updateInstanceFiles() 末尾那段。
                m_owner->updateInstanceFiles(targetId, pageKey, paths);
            });
    connect(&dialog, &InstanceSettingsDialog::fileBatchToggleRequested, m_owner,
            [this, &dialog](const QString &pageKey, const QStringList &paths, bool disable) {
                Q_UNUSED(pageKey);  // 改名字跟哪一页无关：给的就是**这一批的绝对路径**
                m_owner->toggleInstanceFiles(paths, disable);
                // 一律重列：改过名的那几条路径变了（选中集里它们会掉出去，这是对的），
                // 而"现在到底是启用还是禁用"也要重新画。
                dialog.refreshFilePages();
            });
    connect(&dialog, &InstanceSettingsDialog::fileBatchDeleteRequested, m_owner,
            [this, &dialog](const QString &pageKey, const QStringList &paths) {
                Q_UNUSED(pageKey);  // 同上：删的是一批绝对路径
                m_owner->deleteInstanceFiles(paths);
                // 删掉的那几行还在屏幕上就说明没删掉 —— 重列一次。
                dialog.refreshFilePages();
            });

    // ---------- 「复制实例」（2026-09-28）----------
    //
    // 用户原话：「实例设置的概览里面加一条复制实例，点击以后让用户选择复制到的文件夹，然后
    // 给定复制后的实例名称」。三件事按顺序：
    //   ① 选**游戏文件夹**（`QFileDialog::getExistingDirectory`，和「下载到哪个文件夹」那一处
    //      同一路）；② 问**新名字**（`InlineMessage::getText`，预填"<原名> 副本"）；
    //   ③ 真正动手的那一半走 m_owner->copyInstanceTo()。
    //
    // ★ ②问的是**新名字**、①问的是**游戏文件夹**，不是"拷到哪个目录底下"：副本落在
    //   `<挑中的游戏文件夹>/versions/<新名字>/`。理由见 VersionManager::copyInstance 那段
    //   ——"拷进一个普通目录"的话，那一份在启动器里**根本不会出现**（它只认 versions/ 下面
    //   的东西），用户会觉得按了没反应。这一条在按钮的 tooltip 里也写明了。
    //
    // NOTE: 两条原生对话框都在这一层（用户 2026-09-26 那四条 QFileDialog 的同一个理由：
    //       ui/ 不碰 AppPaths，"该默认到哪儿"只有这儿知道）。它们**驱动不了**，所以真正
    //       动手的那一半被切了出去，验证工程直接喂路径。
    connect(&dialog, &InstanceSettingsDialog::copyInstanceRequested, m_owner,
            [this, &targetId]() {
                const QString sourceId = targetId;  // 按值收：下面那两条对话框会让出事件循环
                const QString defaultDir = AppPaths::minecraftDir();
                const QString folder = QFileDialog::getExistingDirectory(
                    m_owner, tr("复制到哪个游戏文件夹"), defaultDir,
                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
                if (folder.isEmpty()) {
                    return;  // 用户取消了
                }
                const QString suggestion = tr("%1 副本").arg(m_owner->instanceDisplayName(sourceId));
                bool ok = false;
                const QString name = InlineMessage::getText(
                    m_owner->m_ui->overlay(), tr("复制实例"), tr("复制出来的这一份叫什么？"), suggestion, &ok);
                if (!ok) {
                    return;
                }
                m_owner->copyInstanceTo(sourceId, folder, name);
            });

    // ---------- 「删除实例」（2026-09-28）----------
    //
    // 用户原话：「顺便把删除实例功能做了，按钮放在概览里」。**没有新写删除**：那一颗按钮
    // 走的就是实例行行尾那颗垃圾桶走的那条路（`m_owner->confirmDeleteInstance()`，两道闸 + 问一句
    // + 真删目录）。这一层只多一件事 —— **删完把这一页收掉**。
    //
    // ★ 为什么要收：这一页说的是"某个实例的设置"，而那个实例刚被删掉。不收的话用户会停在
    //   一页对着已经不存在的目录的设置上：上面那几格还写着它的名字和分组，随便动一格都会
    //   把 wtfml.meta 写进一个**已经被删掉的目录**里（`QDir().mkpath()` 会把它重新建出来
    //   —— 屏幕上就会出现一个只有 meta、没有 json 的空壳实例）。
    //
    // ★ 收页走 `dismiss()`（不是 `closeOverlay()`）：这一页正卡在 `execPage()` 的嵌套
    //   事件循环里，`dismiss()` 发的就是"返回箭头那一发"，`execPage()` 会正常返回；
    //   `closeOverlay()` 走的是另一条路（见 OverlayHost::dismiss() 那段 ★）。
    //
    // NOTE: 没删成（用户点了取消、或者被两道闸拦下）**不收页** —— 那时候实例还在，
    //       用户正该停在这一页上看着那句解释。
    connect(&dialog, &InstanceSettingsDialog::deleteInstanceRequested, m_owner,
            [this, &targetId]() {
                // 按值收："问一句"那一步会让出事件循环，删完这一页也要收掉了。
                const QString id = targetId;
                if (m_owner->confirmDeleteInstance(id)) {
                    m_owner->m_ui->overlay()->dismiss();
                }
            });

    // ---------- 「存档」那一页（2026-09-28）----------
    //
    // 这一页上有三样东西：行尾那支笔（「编辑」）、顶上「导入世界」、顶上「批量编辑」
    // （**占位**）。三发信号都是**页面自己**发的 —— 没有走 InstanceSettingsDialog 再转发
    // 一遍：那一层转发只是把同样的三行重写一次，而这几位只有这一页会发。
    //
    // NOTE: `filesPage()` 在那一页**没被列出来**时（用户在左栏把它勾掉了）返回 nullptr，
    //       所以这里必须判空 —— 不判就是空指针解引用，症状是"一开实例设置就崩"，而
    //       取消勾选这一页是用户随时会做的事。
    //
    // NOTE: 三条都接在 execPage **之前**，理由和上面那一整批逐字相同（那一下会阻塞在
    //       嵌套事件循环里，接晚了就一辈子收不到）。
    if (InstanceFilesPage *saves = dialog.filesPage(QStringLiteral("save"))) {
        // ★ 那支笔 = **直接进「世界设置」那一页**（2026-09-29 改）。
        //
        //   改之前是一个四项小菜单（世界设置… / 重命名… / 打开文件夹 / 取消），现在那个
        //   菜单**没有了**：用户原话「世界设置换成点开是一个额外的页面，改文件的设置上方是
        //   重命名 打开文件夹 删除世界（弹窗确认）」—— 三件事都搬进页里当按钮了，所以这
        //   一支笔点下去只剩一件事可做。
        //
        // NOTE: 点下去**一句话都不问，直接进页**（2026-09-29 用户点名的第二件事：「点击笔的
        //       时候不要弹备份提示了」）。原来这里先弹一问「打开 / 先备份一份 / 取消」，现在
        //       那一问没有了 —— 备份是**页里**那颗「备份一份」，以及展开「修改存档文件」那个
        //       折叠框时那一句警告（用户原话要的就是那一句，见 `fileEditingRequested` 那一段）。
        connect(saves, &InstanceFilesPage::editRequested, m_owner,
                [this, &dialog](const QString &path) { m_owner->showWorldSettingsPage(&dialog, path); });

        // ★ 2026-10-10：顶上那颗「加入选中的世界」（**临时占位**）。
        //
        //   发过来的是一行存档的**绝对路径**，而 `--quickPlaySingleplayer` 要的是**存档目录
        //   名**（游戏就是按 `saves/` 底下那个目录名找世界的）⇒ 这儿摘一个 fileName() 出来。
        //   NOTE: 摘名字这一步**只能在这一层**办：ui/ 那一层只知道"哪一条被选中了"，而
        //         "游戏要的是名字不是路径"是那条启动参数的规矩，写在 launcher/QuickPlay.hpp 里。
        //
        //   NOTE: 路径空 = 没选中（那颗按钮此刻是灰的，见 InstanceFilesPage::updateJoinEnabled()
        //         —— 这是第二道闸）。
        connect(saves, &InstanceFilesPage::joinRequested, m_owner,
                [this, &targetId](const QString &path) {
                    if (path.isEmpty()) {
                        return;
                    }
                    const QString worldName = QFileInfo(path).fileName();
                    if (worldName.isEmpty()) {
                        return;
                    }
                    m_owner->appendLog(tr("试着直接进入世界：%1").arg(worldName));
                    m_owner->startQuickPlay(targetId, QuickPlay::Kind::Singleplayer, worldName);
                });

        // 顶上那颗「导入世界」：选一个世界目录 → 问它进来之后叫什么 → 拷进 saves/。
        // 两条对话框的顺序和「复制实例」一致（先选东西、再起名字，名字预填现成的那个）。
        connect(saves, &InstanceFilesPage::importRequested, m_owner,
                [this, &dialog, &targetId]() {
                    // 默认开在**当前游戏文件夹自己的 saves** 上：用户多半是从别的整合包 /
                    // 别的游戏文件夹里拿过来的那一份，从这儿起步最省事。
                    // NOTE: 这个目录可能不存在（一个没跑过游戏的游戏文件夹），
                    //       QFileDialog 自己会退回"我的电脑"那一层，不用我们判。
                    const QString startDir =
                        AppPaths::minecraftDir() + QStringLiteral("/saves");
                    const QString dir = QFileDialog::getExistingDirectory(
                        m_owner, tr("选择要导入的世界"), startDir,
                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
                    if (dir.isEmpty()) {
                        return;  // 用户取消了
                    }
                    // 预填用户挑的那个目录名：世界叫什么，目录名就是什么（见 WorldFiles.hpp
                    // 那段"两边都不读 level.dat 里的世界名"）。
                    bool ok = false;
                    const QString name = InlineMessage::getText(
                        m_owner->m_ui->overlay(), tr("导入世界"), tr("导入进来之后叫什么？"),
                        QFileInfo(dir).fileName(), &ok);
                    if (!ok) {
                        return;
                    }
                    if (m_owner->importWorldInto(targetId, dir, name)) {
                        dialog.refreshFilePages();
                    }
                });

        // ★ 顶上那颗「批量编辑」：**今天是个占位**（用户原话「这个先做占位」）。
        //   那颗按钮的 tooltip 里已经写了"还没做"，这里再补一句**看得见**的话 —— 只靠
        //   tooltip 的话，用户点下去屏幕上什么都不发生，那和"按钮坏了"分不出来。
        //   NOTE: 用 information 而不是 warning：这不是出错，是"这一颗还没轮到"。
        connect(saves, &InstanceFilesPage::batchEditRequested, m_owner,
                [this]() {
                    InlineMessage::information(m_owner->m_ui->overlay(), tr("批量编辑"),
                                               tr("批量编辑还没做，这一颗先摆在这儿占位。"));
                });
    }

    // ---------- 「世界设置」那一页（2026-09-28）----------
    //
    // ★ 和「配置文件」那一页一样：这一页是**构造时就建好、一直留着**的，所以这四条
    //   connect 只连这一次（不是"每开一个世界连一遍"）。
    // NOTE: 依然接在 `execPage()` **之前** —— 那一下会阻塞在嵌套事件循环里，页面上那颗
    //       按钮按下去的时候这一句还没返回，接晚了就一辈子收不到（和上面那一整批逐字相同）。
    if (WorldSettingsPage *world = dialog.worldSettingsPage()) {
        connect(world, &WorldSettingsPage::saveRequested, m_owner,
                [this, &dialog]() { m_owner->saveWorldSettings(&dialog); });
        // 「备份一份」：这一颗**不动页面上任何东西**（用户可能只是想先抄一份再接着改），
        // 所以只做、只报，不切页、不刷新。
        connect(world, &WorldSettingsPage::backupRequested, m_owner,
                [this]() {
                    QString why;
                    if (m_owner->backupWorldLevelData(m_owner->m_worldDir, &why)) {
                        InlineMessage::information(
                            m_owner->m_ui->overlay(), tr("备份一份"),
                            tr("这个世界的 level.dat 已经原样抄了一份到启动器的缓存里。\n"
                               "要还原的话，把那一份拷回存档目录、改名叫 level.dat 就行。\n\n"
                               "放这儿了：%1")
                                .arg(QDir::toNativeSeparators(m_owner->latestWorldBackup(m_owner->m_worldDir))));
                    } else {
                        InlineMessage::warning(m_owner->m_ui->overlay(), tr("备份一份"),
                                               tr("没能留成备份：%1").arg(why));
                    }
                });
        // 存档目录一定存在（这一格能开着就是刚从这个目录底下读出来的），所以**不 mkpath**
        // —— 和上面那九颗快捷方式按钮那一条故意不同。
        connect(world, &WorldSettingsPage::openFolderRequested, m_owner,
                [this]() {
                    if (!m_owner->m_worldDir.isEmpty()) {
                        m_owner->onFolderOpenRequested(m_owner->m_worldDir);
                    }
                });
        connect(world, &WorldSettingsPage::backRequested, m_owner,
                [this, &dialog]() { m_owner->leaveWorldSettings(&dialog); });

        // ---------- §4.4：整份存档的备份 / 还原 / 打开备份目录 ----------
        //
        // 三颗都**只做、只报**（和上面那颗「备份一份」一个形状）：它们不动页面上任何一格，
        // 也不切页。还原那一颗例外 —— 它会把这一页上摆着的值全换掉（那些值就是 level.dat
        // 里读出来的），所以跑完得把这一页重读一遍，见 onBackupRestoreFinished() 那段。
        connect(world, &WorldSettingsPage::fullBackupRequested, m_owner,
                [this]() { m_owner->backupWorldNow(m_owner->m_worldDir); });
        connect(world, &WorldSettingsPage::openBackupFolderRequested, m_owner,
                [this]() { m_owner->openBackupFolder(BackupFiles::Kind::World); });
        connect(world, &WorldSettingsPage::restoreBackupRequested, m_owner,
                [this, &dialog]() { m_owner->m_backupController->restoreWorld(m_owner->m_worldDir, &dialog); });

        // ★ 「重命名…」（2026-09-29：原来在那一支笔的四项小菜单里，现在搬到页里当按钮）。
        //   整件事和原来那一条**逐字相同**，只多了一步：改完之后**页面上那个名字也得换**
        //   （`setDirName()`），而且这一层记着的那条路径（`m_owner->m_worldDir`）跟着变 —— 不换的
        //   话，用户改完名接着按「保存」，`m_owner->saveWorldSettings()` 会对着一个已经不在了的
        //   目录报"这个存档不见了"，而屏幕上刚显示完"改好了"。
        connect(world, &WorldSettingsPage::renameRequested, m_owner,
                [this, &dialog, world]() {
                    if (m_owner->m_worldDir.isEmpty()) {
                        return;  // 没开着哪个世界（这一页没 setWorld 过）—— 不该发生
                    }
                    const QString worldDir = m_owner->m_worldDir;
                    const QString worldName = QFileInfo(worldDir).fileName();
                    bool ok = false;
                    const QString wanted = InlineMessage::getText(
                        m_owner->m_ui->overlay(), tr("重命名世界"), tr("这个存档以后叫什么？"), worldName,
                        &ok);
                    if (!ok) {
                        return;
                    }
                    // 实例 id 从**路径**反推（这一页记的是存档目录，不是实例 —— 见 .hpp 里
                    // `m_owner->m_worldDir` 那条 NOTE）。反推不出来（存档挂在某个实例的 saves 外面）
                    // 就是空串 —— `isVersionRunning("")` 是 false，于是"正在跑就不许改"那道
                    // 闸不管它，交给 `renameWorld()` 自己判盘上那份还在不在。
                    if (!m_owner->renameInstanceWorld(m_owner->instanceIdOfPath(worldDir), worldDir, wanted)) {
                        return;
                    }
                    // ★ 名字一变，这一层记的那条路径**同时**跟着变。`WorldFiles::renameWorld()`
                    //   只动目录名（见它的头文件），所以 `level.dat` 的大小 / 修改时间两样都
                    //   没变 —— 打开时量的那个戳对新的那条路径照样作数，"盘上被人改过"那道
                    //   闸不会因为改了个名就误报。
                    const QString renamed =
                        QFileInfo(worldDir).absolutePath() + QLatin1Char('/') + wanted.trimmed();
                    m_owner->m_worldDir = renamed;
                    world->setDirName(wanted.trimmed());
                    // 重列一遍存档页：目录名变了，行上那行小字和那支笔挂的路径**全都得跟着
                    // 换**（和原来那个菜单里同一条理由）。
                    dialog.refreshFilePages();
                });

        // ★ 「删除世界…」（2026-09-29 新加的那一颗）。做法照着 `m_owner->deleteInstanceFile()` 那
        //   一条：**先问一句**（红的那一档）、再用 junction 安全的 `removeTreeWithoutFollowingLinks()`
        //   删、删不掉时把"多半是被游戏占着"说出来。
        //
        //   NOTE: 这里不比 `m_owner->deleteInstanceFile()` 多判一条"游戏在跑"就删 —— 多判的那一条
        //         是有用的：游戏手里那份存档是它自己读进去的，它退出（或者在游戏里存一次）
        //         的时候会**把整个世界重新写一遍**，而那时候目录已经没了。
        connect(world, &WorldSettingsPage::deleteRequested, m_owner,
                [this, &dialog]() {
                    if (m_owner->m_worldDir.isEmpty()) {
                        return;
                    }
                    const QString worldDir = m_owner->m_worldDir;
                    const QString worldName = QFileInfo(worldDir).fileName();
                    const QString instanceId = m_owner->instanceIdOfPath(worldDir);
                    if (!instanceId.isEmpty() &&
                        m_owner->m_launcherManager->isVersionRunning(instanceId)) {
                        InlineMessage::warning(m_owner->m_ui->overlay(), tr("删除世界"),
                                               tr("游戏正在运行，先关掉游戏再删这个世界。"));
                        return;
                    }
                    // 默认**不**落在「删除」上（`defaultYes = false`）：误按一下回车不该把
                    // 用户几百 MB 的游玩记录删掉（和 `m_owner->deleteInstanceFile()` 一个方向）。
                    if (!InlineMessage::question(
                            m_owner->m_ui->overlay(), tr("删除世界"),
                            tr("把「%1」连同里面的东西从盘上删掉？删了就没了。").arg(worldName),
                            tr("删除"), tr("取消"), /*defaultYes=*/false,
                            /*destructive=*/true)) {
                        return;
                    }
                    // **不能**用 `QDir::removeRecursively()`：实测它会跟着 junction / 符号
                    // 链接走进目标目录（见 FolderManager.cpp 里那段）—— 把 saves 挂到网盘是
                    // 常见做法，而这一颗点的是"删掉这个世界"。
                    if (!removeTreeWithoutFollowingLinks(worldDir)) {
                        InlineMessage::warning(
                            m_owner->m_ui->overlay(), tr("删除失败"),
                            tr("没能删掉「%1」：多半是它正被游戏（或者别的程序）占着。"
                               "先把游戏关掉再试。")
                                .arg(QDir::toNativeSeparators(worldDir)));
                        return;
                    }
                    m_owner->appendLog(tr("删除世界：%1").arg(QDir::toNativeSeparators(worldDir)));
                    // 世界没了 —— 这一页留着也没有对象了，收掉它，再把存档页重列一遍
                    //（那一行现在指着一条不存在的目录）。
                    dialog.closeWorldSettings();
                    m_owner->m_worldDir.clear();
                    m_owner->m_worldStampSize = 0;
                    m_owner->m_worldStampMtimeMs = 0;
                    dialog.refreshFilePages();
                });

        // ★ 「修改存档文件」那个折叠框（2026-09-29 新加）。用户原话：「修改文件的设置放在一个
        //   类似版本下载那里的收起来的框里，默认收起，**展开是弹出备份警告**」。
        //
        //   所以这一发信号说的**不是"框开了"**，是"用户想改文件了"（见 .hpp 里那个信号）：
        //   那几格改的是 `level.dat` 里游戏自己都改不了的东西（世界名 / 极限模式 / 难度），
        //   改坏了这个世界就变味了 —— 所以开框之前先把"备份"这件事摆到他面前。
        //
        //   NOTE: 页面上那颗钮**已经**把框摆开了（它先摆样子再发信号 —— 不先摆的话，用户按
        //         下去屏幕上什么都不动，看着像按了个坏按钮）。所以这里三条出口要各自负责
        //         把样子摆回去：只有「取消」那一档要收。
        connect(world, &WorldSettingsPage::fileEditingRequested, m_owner,
                [this, world](bool wanted) {
                    if (!wanted) {
                        return;  // 用户自己把框收起来了 —— 这一趟不需要问任何事
                    }
                    const QString worldName = QFileInfo(m_owner->m_worldDir).fileName();
                    const int choice = InlineMessage::ask(
                        m_owner->m_ui->overlay(), MessageKind::Question, tr("改动存档文件"),
                        tr("下面这几格改的是「%1」的存档文件本身 —— 世界名、允许作弊、极限"
                           "模式、难度。游戏里没有改一个已有世界的入口，改坏了这个世界就"
                           "变味了。\n\n要接着改，最好先留一份现在能玩的。")
                            .arg(worldName),
                        {tr("继续改"), tr("先备份一份"), tr("取消")},
                        /*defaultIndex=*/0, /*destructiveIndex=*/-1,
                        /*dismissOnOutsideClick=*/false,
                        tr("「先备份一份」= 把这个世界的 level.dat 原样抄一份到启动器的"
                           "缓存里，不动存档本身。"));
                    if (choice == 2 || choice == -1) {
                        // 「取消」和"点外面关掉"都算"不改了" ⇒ 把框**收回去**（不收回来的
                        // 话页面上那个框开着、而用户什么也没答应，看着像那句警告白弹了）。
                        world->setFileEditingOpen(false);
                        return;
                    }
                    if (choice == 1) {
                        QString problem;
                        if (!m_owner->backupWorldLevelData(m_owner->m_worldDir, &problem)) {
                            InlineMessage::warning(m_owner->m_ui->overlay(), tr("没能备份"), problem);
                            // ★ 备份没留成 ⇒ **不许接着改**（这一整套的前提正是"改坏了能
                            //   回去"，而这四格没有撤销）—— 和 `m_owner->saveWorldSettings()` 里
                            //   "留不下就不写"同一条规矩。
                            world->setFileEditingOpen(false);
                            return;
                        }
                        InlineMessage::information(
                            m_owner->m_ui->overlay(), tr("备份好了"),
                            tr("「%1」的 level.dat 已经留了一份：\n%2")
                                .arg(worldName,
                                     QDir::toNativeSeparators(m_owner->latestWorldBackup(m_owner->m_worldDir))));
                    }
                    // 「继续改」/「先备份一份」（备成了）：框**保持开着**。
                    //
                    // NOTE: 这里显式摆一次而不是"什么都不做" —— 页面上那颗钮是在**信号发
                    //       出去之前**摆的样子，中间跑了两个嵌套事件循环（那一问、以及备份
                    //       好了那一句），把这件事实打实地钉一遍比"相信中间没人动过它"稳。
                    world->setFileEditingOpen(true);
                });
    }

    // ---------- 「配置文件」那一页（2026-09-27）----------
    //
    // ★ 这一页和别的几页不是一回事：点一行**不是"重列一遍"**，而是"读那个文件、把它摆进
    //   编辑器"。编辑器和那张列表是**同一页里的两格**（开不出第二个整页覆盖层 —— 见
    //   InstanceSettingsDialog::configFilePage() 上面那一段），所以这一趟没有"开页 / 关页"，
    //   只有"读进来灌进去"和"切回第一格"。
    //
    // NOTE: `configFilePage()` 是**构造时就建好、一直留着**的那一个（那把 NOTE 说了不要
    //       把它当一次性的）—— 所以这三条 connect 只连这一次，之后每开一个文件都复用。
    connect(&dialog, &InstanceSettingsDialog::openConfigFileRequested, m_owner,
            [this, &dialog](const QString &path) { m_owner->openConfigFile(&dialog, path); });
    if (ConfigFileEditorPage *editor = dialog.configFilePage()) {
        connect(editor, &ConfigFileEditorPage::saveRequested, m_owner,
                [this, &dialog](const QString &path, const QByteArray &bytes) {
                    m_owner->saveConfigFile(&dialog, path, bytes);
                });
        // 编辑器给的是**文件**路径，开文件夹要的是它所在的那个目录。
        //
        // NOTE: 这儿**不 mkpath**（不像上面那九颗快捷方式按钮）：那个目录一定是存在的
        //       ——这一格能开着，说明刚从这个目录底下读出来过一个文件。
        connect(editor, &ConfigFileEditorPage::openFolderRequested, m_owner,
                [this](const QString &path) {
                    const QString dir = QFileInfo(path).absolutePath();
                    if (!dir.isEmpty()) {
                        m_owner->onFolderOpenRequested(dir);
                    }
                });
        // ★ 「还原上一次」：读备份是读盘（见 ConfigFileEditorPage.hpp 那段"这一层不碰盘"），
        //   所以那一格只发"用户要这个文件的上一次"，取和摆在这儿做。
        connect(editor, &ConfigFileEditorPage::restoreRequested, m_owner,
                [this, &dialog](const QString &path) { m_owner->restoreConfigFile(&dialog, path); });
        connect(editor, &ConfigFileEditorPage::backRequested, m_owner,
                [this, &dialog]() { m_owner->leaveConfigEditor(&dialog); });
    }
    // ★ 顶上那颗返回箭头（覆盖层页眉上那一颗）走的是 `dialog->reject()`，点一下关掉的是
    //   **整个「实例设置」** —— 而编辑器那一格里可能正压着改了一半的东西。对话框把那一关
    //   挡下来了（见它那条 reject()）：这一发就是"挡下来了、你说怎么办"。
    //
    // NOTE: 用户说"丢掉"之后要用 `done(QDialog::Rejected)` 而不是 `reject()` —— 后者会
    //       又走回对话框那条 override 上，于是又发一次这一发（问不完的问）。
    connect(&dialog, &InstanceSettingsDialog::closeBlockedByUnsavedConfigFile, m_owner,
            [this, &dialog]() {
                ConfigFileEditorPage *editor = dialog.configFilePage();
                if (editor == nullptr || !m_owner->confirmDiscardConfigEdits()) {
                    return;  // 留在这儿：页照旧开着，编辑器那一格原样。
                }
                editor->discardChanges();
                dialog.done(QDialog::Rejected);
            });
    // 「世界设置」那一页上的同一件事（上面那条的第二个实例，逐字适用）。
    //
    // NOTE: 这一条**不用**先把那一页倒回去（上面那条要 `discardChanges()`）：整页马上就关
    //       了，下一次从那支笔进来又是从头 setWorld() 灌一遍（见 m_owner->showWorldSettingsPage()），
    //       留着一个"倒回原样"的动作只是给下一次开页添乱。
    connect(&dialog, &InstanceSettingsDialog::closeBlockedByUnsavedWorldSettings, m_owner,
            [this, &dialog]() {
                if (!m_owner->confirmDiscardWorldEdits()) {
                    return;  // 留在这儿：页照旧开着，那一格原样。
                }
                m_owner->m_worldDir.clear();
                m_owner->m_worldStampSize = 0;
                m_owner->m_worldStampMtimeMs = 0;
                dialog.done(QDialog::Rejected);
            });
    // 服务器列表那一页的「刷新」：重读一遍 servers.dat 再递进去，**顺手测一遍**
    // （2026-09-27）—— 「刷新」的意思是"把这一页刷新成现在的样子"，而这一页现在不只是
    // 那份文件的镜像（图标 / MOTD / 延迟都是刚问来的）。
    // ★ 2026-10-10：「测速」并进来了（用户原话「服务器那一栏测速和刷新作用重复了」），
    //   所以那一条 connect 没有第二份了 —— 这一发就是这一页"重新看一眼"的全部。
    connect(&dialog, &InstanceSettingsDialog::refreshServerListRequested, m_owner,
            [this, &dialog, &targetId]() {
                m_owner->loadServerList(&dialog, targetId);
                m_owner->pingServers(&dialog, targetId);
            });
    // ★ 「点进去就自动测速」（用户 2026-09-27 原话）。这一发是**切到那一页**发出来的
    //   （见 InstanceSettingsDialog::applyPageKey），和上面那两条"用户按了某颗按钮"不是
    //   一回事。它每一页都会发一次 —— 用户切走再切回来就又测一遍，而"已经在跑"那一趟
    //   不会被重开（见 pingServers 里那道闸）。
    connect(&dialog, &InstanceSettingsDialog::serverPageEntered, m_owner,
            [this, &dialog, &targetId]() { m_owner->pingServers(&dialog, targetId); });

    // 「下载新模组 / 资源包 / 数据包 / 光影」那四颗（2026-09-27）：**切到下载页对应的那一栏**。
    // 那一页和下载页是两块地方，路只有这一条 —— 收页 + 摆选项卡 + 挑那一行全在
    // m_owner->openDownloadCategory() 一处（内含"为什么要排到下一轮事件循环"那句）。
    //
    // NOTE: 接在 execPage **之前**，理由同上：那一下会阻塞在一个嵌套事件循环里，
    //       用户在页面上点那颗按钮时，这一句还没返回，接晚了就一辈子收不到。
    // ★ 2026-09-28：把**是哪个实例**也带过去（`targetId` 就是这一页说的那个实例）。那一栏
    //   靠它做两件事：左上角露出「← 返回实例设置」（回得来）、以及把「已安装版本」选到它
    //   身上（下来的东西落进"当前实例"）。不带的话用户点完那颗键就回不去了。
    connect(&dialog, &InstanceSettingsDialog::browseDownloadsRequested, m_owner,
            [this, &targetId](const QString &categoryKey) {
                m_owner->openDownloadCategory(categoryKey, targetId);
            });

    // ★ 行上的名字点得动（2026-09-27，用户原话「除了模组以外的几个都是点击模组名称自动
    //   进入下载界面（前提是匹配上了，模组的待会再说）」）：**进的是那个资源自己的那一页**
    //   （和下载页里点一行开的是同一页）—— 「前提是匹配上了」那句话只有这样才说得通，
    //   换成"切到下载页"就跟匹配不匹配没关系了。
    //   路径 → 它是哪个资源，是上面那趟反查认出来的（见 m_owner->m_instanceFileIdentities）；页面
    //   那边只会为**记过名字**的那几条发这一发（没认出来的名字压根点不动）。
    // ★★ `targetId` 按**引用**收（和上面那批 lambda 同一个理由，见 refreshInstanceDirs 的
    //   NOTE）：它必须取到**改名之后**的值 —— 用户改完名再点名字，回来时那一页要开在
    //   新目录名的实例上，收一份旧名字就是"实例设置打不开"。
    // NOTE: 同样接在 execPage **之前**，理由和上面那条逐字相同。
    connect(&dialog, &InstanceSettingsDialog::fileResourceRequested, m_owner,
            [this, &dialog, &targetId](const QString &pageKey, const QString &path) {
                // ★★ 2026-10-11：**在收掉这一页之前**把"现在是哪一行在最上面"量下来
                //   （用户原话：「点进模组详情以后退出来保留刚才的滚动位置」）。
                //
                //   非量不可的时机：下面那一发会把这一页收掉，而返回时开出来的是**另一个
                //   对话框、另一个 InstanceFilesPage**（`openInstanceSettingsNow()` 里那个
                //   栈上的对象）—— 位置只有这一刻还活在盘面上。摆回去那一段见
                //   `openInstanceSettingsNow()` 里 `showPage()` 之前那一段。
                //
                //   NOTE: 按**引用**收 `dialog`，理由和上面那批 lambda 逐字相同（它也接在
                //         `execPage()` 之前，那一发阻塞在嵌套事件循环里，这一层的栈一直
                //         站着 —— 见 targetId 那一段）。右键点一行走的也是这一发
                //         （见 InstanceSettingsDialog 那边这段注释），所以两条路都顾到了。
                if (InstanceFilesPage *files = dialog.filesPage(pageKey)) {
                    const InstanceFilesPage::ScrollPosition position = files->scrollPosition();
                    m_owner->m_fileScrollReturn.instanceId = targetId;
                    m_owner->m_fileScrollReturn.pageKey = pageKey;
                    m_owner->m_fileScrollReturn.identity = position.identity;
                    m_owner->m_fileScrollReturn.delta = position.delta;
                }
                m_owner->openInstanceFileResource(targetId, pageKey, path);
            });

    // 地址那一格右键 ⇒ 把**完整的**地址放进剪贴板（2026-09-27，用户原话「ip地址右键可以
    // 复制」）。复制完**不弹话**：这一下是用户自己按的，粘一下就看得见 —— 和资源详情页
    // 那条「复制名称」逐字同一个路数（那边进日志留个痕就够）。
    connect(&dialog, &InstanceSettingsDialog::copyServerAddressRequested, m_owner,
            [this](const QString &address) {
                if (address.isEmpty()) {
                    return;
                }
                if (QClipboard *clipboard = QApplication::clipboard()) {
                    clipboard->setText(address);
                    m_owner->appendLog(tr("已复制服务器地址：%1").arg(address));
                }
            });

    // ---------- 服务器列表那三件会写盘的事（2026-10-15 搬进 `ServerListController`）----------
    //
    // 改一台 / 加一台 / 删一台的**判断**都搬到那一层了（"一个字节都不写"那道闸、地址必填、
    // 「（无名）」那条显示规矩、以及"动完统一重读重测"）。这一层剩下的是**只有它办得了的
    // 两件事**：问话（`OverlayHost` 在这儿）和重读重测（`pingServers` 要那个对话框）。
    //
    // NOTE: 它是**这一趟的局部对象**（不是成员，和 `m_owner->m_instanceSettings` 那种不一样）：它那
    //       两条回调要用 `&dialog` 和 `&targetId`，而这两个都只活在这一趟里 —— 和上面那一批
    //       "dialog 是栈上的局部对象"的 lambda 同一个道理。它和 `dialog` 一样活到 `execPage()`
    //       结束，而那三发信号只可能在 `execPage()` 期间发出来。
    ServerListController serverEdits(ServerListController::Host{
        [this](ServerListController::Ask which, const QString &initial, QString *out) {
            // **四句问话的文案全在这儿** —— 只有主窗口有 OverlayHost，而且"哪一档说哪句话"
            // 正是这一层该管的事（那一层只说"要问哪一格"）。
            QString title;
            QString label;
            switch (which) {
                case ServerListController::Ask::EditName:
                    title = tr("编辑服务器");
                    label = tr("这一台叫什么？");
                    break;
                case ServerListController::Ask::EditAddress:
                    title = tr("编辑服务器");
                    label = tr("地址是什么？（`主机名:端口`，端口不写就是默认那个）");
                    break;
                case ServerListController::Ask::AddName:
                    title = tr("添加服务器");
                    // 名字可以留空：`servers.dat` 里的 `name` 真能是空串（游戏那边显示成
                    // 未命名），所以这一句得明说"留空也行"，不然用户会以为填不满就不让过。
                    label = tr("这一台叫什么？（可以留空）");
                    break;
                case ServerListController::Ask::AddAddress:
                    title = tr("添加服务器");
                    label = tr("地址是什么？（`主机名:端口`，端口不写就是默认那个）");
                    break;
            }
            bool ok = false;
            const QString answer =
                InlineMessage::getText(m_owner->m_ui->overlay(), title, label, initial, &ok);
            if (ok) {
                *out = answer;
            }
            return ok;
        },
        [this](const QString &who, const QString &address) {
            // 那一句问话里的名字已经过了 `displayName()`（空名字那一档是「（无名）」）——
            // 用户点的是哪一台，问的就是哪一台。
            return InlineMessage::question(
                m_owner->m_ui->overlay(), tr("删除服务器"),
                tr("要把 %1（%2）从服务器列表里删掉吗？\n\n游戏里那份 servers.dat 里"
                   "就少这一台了。")
                    .arg(who, address),
                tr("是"), tr("否"), /*defaultYes=*/false, /*destructive=*/true);
        },
        [this](const ServerListController::Notice &notice) { m_owner->showServerListNotice(notice); },
        [this](const QString &text) { m_owner->appendLog(text); },
        [this, &dialog](const QString &id) {
            m_owner->loadServerList(&dialog, id);
            m_owner->pingServers(&dialog, id);
        }});

    // ★ 2026-10-10：「编辑」那颗（用户原话「在服务器列表那里做一个编辑地址和名称的功能」）。
    //   两样分开问**两句**（`InstanceFilesPage::setLineEdit()` 那样的两格表单这一层没有 ——
    //   InlineMessage 一次只问一样），顺序和「导入世界」那一套一致：**先说名字、再报地址**。
    //
    //   落盘走 `src/manage/ServerListFile`（`NbtUtil` 那一层**故意只读**，见它的文件头）。
    //   认出"要改的是哪一条"靠**按值匹配**（名字 + 地址，退一步只按地址）—— 不能按下标：
    //   `NbtUtil::readServerList` 会跳过没有可用 `ip` 的条目，于是界面上第 N 行**不等于**
    //   NBT 里第 N 条（见 ServerListFile.hpp 那段）。
    //   ★ 2026-10-15：那两句话本身（标题 / 那两格问什么 / 要不要预填）在**上面那个 Host 里**
    //     —— 这一层只剩"把这一发转给它"。
    connect(&dialog, &InstanceSettingsDialog::editServerRequested, m_owner,
            [&serverEdits, &targetId](const QString &oldName, const QString &oldAddress) {
                serverEdits.edit(targetId, oldName, oldAddress);
            });

    // ★ 2026-10-10：「添加」（用户原话「服务器那个页面里放一个添加服务器的按钮，允许添加」）。
    //   和「编辑」**逐字同一套**：两发 `getText()`（先说名字、再报地址）、落盘走
    //   `ServerListFile`、失败用 `InlineMessage::warning` 挡住。差别只有两处：
    //   ① 预填的是**空**（要加的那一台还没有名字 / 地址）；② 走的是 `addEntry()` —— 那个
    //      入口在**文件根本不存在**时也不当失败（那正是"这个实例第一次加服务器"）。
    connect(&dialog, &InstanceSettingsDialog::addServerRequested, m_owner,
            [&serverEdits, &targetId]() { serverEdits.add(targetId); });

    // ★ 2026-10-10：「删除」（用户原话「编辑要能改名字改地址以及删除」）。
    //   和「编辑」同一套，多出来的只有**中间那一道问话** —— 这是这一页上唯一一次真会从
    //   用户自己的文件里拿掉东西的操作。
    //
    //   ★ 那句问话**必须点名字**（用户点的是哪一台，问的就是哪一台）：`servers.dat` 里
    //     名字是空串是合法的，所以那一档写「（无名）」+ 地址 —— 光说"这一台"的话，两台
    //     没名字的服务器在屏幕上是同一个问题。
    connect(&dialog, &InstanceSettingsDialog::deleteServerRequested, m_owner,
            [&serverEdits, &targetId](const QString &name, const QString &address) {
                serverEdits.remove(targetId, name, address);
            });

    // ★ 2026-10-10：「加入服务器」（**临时占位**）。和存档那一页那颗走的是同一条路
    //   （`m_owner->startQuickPlay()`）—— 差别只有 Kind 和那串值（这里给的就是 `servers.dat` 里写的
    //   `host:port`，游戏自己认得这个写法）。
    connect(&dialog, &InstanceSettingsDialog::joinServerRequested, m_owner,
            [this, &targetId](const QString &name, const QString &address) {
                m_owner->appendLog(tr("试着直接加入服务器：%1（%2）")
                              .arg(name.isEmpty() ? tr("（无名）") : name, address));
                m_owner->startQuickPlay(targetId, QuickPlay::Kind::Multiplayer, address);
            });

    // ---------- 「改了就写」那两条（2026-09-26）----------
    //
    // ★ 用户原话：「实例设置里面的那个取消和确定删了，修改了的设置实时写入」。从前收尾那
    //   一段（`execPage` 返回之后读对话框、改名、写设置）**整个搬到这两条 lambda 里** ——
    //   写什么、按什么顺序写，一个字没变，只是时机从"点确定"变成了"值真的变了"
    //   （对话框那边保证：值没变不发，见 notifyEdited()）。
    //
    // NOTE: `targetId` 是**活到 execPage 结束**的那个局部变量（上面算目录时就摆好了；
    //       下面 execPage 会阻塞在嵌套事件循环里，这一层的栈一直站着），改名成功之后它
    //       跟着换 —— 不换的话，改完名之后动任何一个开关都会写到**老目录名**上，而
    //       updateVersionMeta() 会照着老名字把目录重新建出来，用户手上就多一个只有一份
    //       wtfml.meta 的空壳实例。
    connect(&dialog, &InstanceSettingsDialog::settingsEdited, m_owner,
            [this, &dialog, &targetId]() { m_owner->applyInstanceSettings(targetId, dialog); });

    // 改名：**排在写设置之外自己一条**（用户 2026-09-25 原话「重命名进实例设置」）。
    // 两件事不能合成一条 —— 改名的副作用是**整个实例换了个目录**，写设置必须发生在它
    // 之后（顺序反了就是上面那个空壳实例）。对话框那边也把这两件事分成了两条信号：
    // 那一格走 `editingFinished`（回车 / 失焦），正好是"这次改名想清楚了"的那一刻。
    connect(&dialog, &InstanceSettingsDialog::renameRequested, m_owner,
            [this, &dialog, &targetId, &refreshInstanceDirs](const QString &name) {
                const QString wanted = name.trimmed();
                // 空 = 用户把那一格清空了（不是"想改成没有名字"），等于没改。
                if (wanted.isEmpty() || wanted == targetId) {
                    dialog.setInstanceName(targetId);
                    return;
                }
                if (!m_owner->applyRename(targetId, wanted)) {
                    // ★ 失败要把那一格退回原样。不退回的话，用户眼前那一格写着新名字、
                    //   而磁盘上还是老名字 —— 他接着动任何一个开关，那一次写入就落到一个
                    //   **不存在的目录**上（updateVersionMeta() 会照着它把目录建出来）。
                    //   m_owner->applyRename() 已经把原因弹出来了，这里只负责让界面别撒谎。
                    dialog.setInstanceName(targetId);
                    return;
                }
                // 改完名之后写的是**新目录**（m_owner->applyRename() 里那句 m_owner->refreshInstalledVersions()
                // 已经把列表和下拉框都换到新名字上了）。
                targetId = wanted;
                dialog.setInstanceName(targetId);
                // 「文件管理」那八页说的也是**这个实例的目录**，整份跟着重算 ——
                // 重算之前它们还指着那个已经不在了的老目录（见上面那个 lambda 的 NOTE）。
                refreshInstanceDirs(targetId);
                // 页眉上那行字也是"这个实例叫什么"，跟着换 —— 不换的话页面上最高处还写着
                // 老名字，而它底下那一格已经是新的了。
                //
                // NOTE: 先让对话框把**它自己那行标题**重拼一遍（`titleText()` 读的就是它），
                //       再拿它去问覆盖层要那行字。`titleText()` 在改名前是**构造时按当时那个
                //       名字拼好的死字符串** —— 不重拼就直接递给 setPageTitle，页眉上写的
                //       还是老名字（而且看不出来哪里错了：两边调的是同一个函数）。
                dialog.setInstanceDisplayName(m_owner->instanceDisplayName(targetId));
                m_owner->m_ui->overlay()->setPageTitle(dialog.titleText());
            });

    // ---------- 停在哪一页（2026-09-28，用户原话「从模组点进去，返回也在模组而非概览」）----------
    //
    // 摆在这一段**最后、execPage 之前**有两条理由：
    //   ① `showPage()` 换不了"左栏里没有的那几页"（被用户勾掉的、以及「配置文件」那种没
    //      入口的），而"哪几页在左栏里"是上面 `setPageChoices()` 定的 ⇒ 必须在它**之后**；
    //   ② `execPage()` 是**阻塞**的（下面那一句会卡在嵌套事件循环里转到用户点返回），
    //      再往后的代码这一趟根本执行不到。
    // 认不出来的键 `showPage()` 自己判 -1 什么都不做（停「概览」）—— 不静默出错。
    // ★★ 2026-10-11：**从资源详情页返回**时把那一页的滚动位置塞回去（用户原话：
    //   「点进模组详情以后退出来保留刚才的滚动位置」）。位置是离开那一刻量的
    //   （见 fileResourceRequested 那一条接线）。
    //
    // ★ 必须排在下面那句 `showPage()` **之前**：`showPage()` 会把那一页列出来，而
    //   "列出来"这一趟的收尾就是 `restoreScrollAnchor()` —— 塞晚了这一趟已经落回顶端了，
    //   而且那份锚点是**一次性的**（还完就丢，见那一支）。
    //
    //   对不上就整份作废（换了实例 / 开的是另一页 ⇒ 落回顶端才是对的）；不管用没用上，
    //   到这儿都清掉 —— 这一趟之后它的使命就结束了，留着的话下一次开实例设置会拿一份
    //   过期的位置去摆（而那时用户多半已经翻到别处去了）。
    if (!m_owner->m_fileScrollReturn.identity.isEmpty() && m_owner->m_fileScrollReturn.instanceId == targetId &&
        !pageKey.isEmpty() && m_owner->m_fileScrollReturn.pageKey == pageKey) {
        if (InstanceFilesPage *files = dialog.filesPage(pageKey)) {
            InstanceFilesPage::ScrollPosition position;
            position.identity = m_owner->m_fileScrollReturn.identity;
            position.delta = m_owner->m_fileScrollReturn.delta;
            files->setScrollPosition(position);
        }
    }
    m_owner->m_fileScrollReturn = MainWindow::FileScrollReturn();

    if (!pageKey.isEmpty()) {
        dialog.showPage(pageKey);
    }

    // ---------- 这一页归「实例」那一颗选项卡（2026-09-28）----------
    //
    // ★ 见 .hpp 那段：不钉的话，从下载页那颗「← 返回实例设置」进来时这一页会被记成
    //   **下载页的**页面，而用户点「实例」看见的是实例列表（那一页收着看不见）、又因为它
    //   占着覆盖层那一位而从此开不了新的实例设置，每次还被拽回下载页。
    //   钉住 + 先把选项卡切过去（**切完再开**：`ownerTab` 不是当前这一颗的话，这一页一开
    //   出来就是"收着"的，用户按了那颗返回键屏幕上什么都不出现）。
    //
    // NOTE: "让位"那一段（收掉占着的那一层、静默继续）在 `m_owner->openInstanceSettings()` 里、
    //       这一半之前 —— 走到这儿时这一层**一定是空的**，所以切选项卡不会顺手把用户翻到
    //       别的选项卡上去（从前那颗闸会弹话，弹话时就不该翻页）。
    const int instancesTab = MainWindowUI::instancesTabIndex();
    if (QTabWidget *tabs = m_owner->m_ui->getTabWidget()) {
        if (instancesTab >= 0 && instancesTab < tabs->count()) {
            tabs->setCurrentIndex(instancesTab);
        }
    }

    // ★ 拖进来的文件（2026-09-29 用户点名要的那一条）：这一页**认不出**一个 jar 是什么、
    //   也够不着盘 —— `ui/` 不 include `src/`（那条分层约定见 InstanceSettingsDialog.hpp
    //   里 dragEnterEvent 那段）。所以它只发一句"拖进来这几个路径"，认类型、问那一句
    //   "是否继续"、把文件复制进目录，**全在 MainWindow 这一层**（见 handleInstanceSettingsDrop）。
    //
    // NOTE: `targetId` 按**引用**收 —— 改完名这一页还开着，那时拖进来的东西必须落进新目录
    //       （同上面那些 lambda 的理由，见 targetId 那一段）。
    connect(&dialog, &InstanceSettingsDialog::filesDropped, m_owner,
            [this, &dialog, &targetId](const QStringList &paths) {
                m_owner->handleInstanceSettingsDrop(&dialog, targetId, paths);
            });

    // 这一页盖在内容区上（标签栏和底栏都留着），左上角那颗返回箭头 = 关掉这一页（见
    // OverlayHost::execPage）。标题写在页眉上，所以这一页自己那行标题要收起来 ——
    // 两行一模一样的字挨着是重复，不是强调。
    //
    // NOTE: 这一页**不再 accept**（没有「确定」了），所以这里**不看返回值** —— 关页
    //       不等于放弃（该写的早就在上面那些 lambda 里写完了），"关掉"就是"关掉"。
    dialog.setTitleVisible(false);
    m_owner->m_ui->overlay()->execPage(&dialog, dialog.titleText(), instancesTab);
}

