/**
 * @file MainWindow.cpp
 * @brief 实现主应用程序窗口及用户交互逻辑。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 本文件实现了 UI 事件处理函数并委托给各个管理器的方法。
 *
 * 本文件将 UI 控件连接到启动器，处理用户命令，
 * 并协调身份验证、下载和启动操作。
 */

#include "MainWindow.hpp"

#include "..\ui\Animations.hpp"  // 换游戏文件夹时那一栏淡一下（2026-09-29）
#include "..\ui\ConfigFileEditorPage.hpp"
#include "..\ui\ConfigFileText.hpp"
#include "..\ui\GroupDialog.hpp"
#include "..\ui\InstanceGroupDelegate.hpp"
#include "..\ui\InstanceFilesPage.hpp"
#include "..\ui\InstanceSettingsDialog.hpp"
#include "..\ui\IntegrityReportPage.hpp"  // 完整性检查报告页（2026-10-12，coding plan §4.1）
#include "..\ui\JavaMismatchDialog.hpp"
#include "..\ui\JavaManageDialog.hpp"
#include "..\ui\AccountSkinDialog.hpp"
#include "..\ui\ResourceDetailPage.hpp"
// 更新前那个轻量回滚点（coding plan §7.1，2026-10-13）：`updateInstanceFiles()` 动手之前
// 摆一份、`onUpdateCenterUndoRequested()` 照着它退回去。
#include "manage/UpdateRollback.hpp"
#include "..\ui\GameLogWindow.hpp"
#include "..\ui\MainWindowUI.hpp"
// 任务中心面板（2026-10-12，coding plan §3.2）：底栏中间那格点开之后向上弹出来的那块。
// 这一边给它两样东西 —— 一行长什么样（`TaskCenter::Item`，那层是**纯显示**的 DTO）和
// 「清除已完成」被点了怎么办（见 setupConnections() 里 setTaskCenterContent() 那一段）。
#include "..\ui\TaskCenterPanel.hpp"
#include "..\ui\TaskDetailPage.hpp"
// 那一页本身（存档那支笔点开的「世界设置」）。**它摆什么、怎么摆只有它自己知道**，
// 这一边只负责把盘上读出来的东西翻译成 `WorldSettingsPage::WorldView`（见
// worldViewOf() 那段），以及把页面上改过的翻译回去。
#include "..\ui\WorldSettingsPage.hpp"
// 内容区上面那一层（2026-09-25）：设置界面和各种提示都开在它里面，不再另开独立窗口。
// 见它的文件头 —— ui/ 那边只管"开在哪儿、长什么样"，不认识业务。
#include "..\ui\OverlayHost.hpp"
// 安装页「预设」那一行点开的紧凑弹窗（coding plan §7.1）：四类各挑一份、看一眼计划。
// 它只画外面喂进去的值（`ui/` 不读盘），所以这一边负责把 controller 算好的东西递进去。
#include "..\ui\PresetInstallPicker.hpp"
// QMessageBox 的替身（2026-09-25 换过来的）：`InlineMessage::warning(overlay, 标题, 正文)`
// 顶掉原来的 `QMessageBox::warning(this, 标题, 正文)`。原来那些调用点**一行一行**对得上，
// 语义一个字没变 —— 变的是"这一句说在哪"。
#include "..\ui\InlineMessage.hpp"
// 主页右栏那个小人怎么画：几何、投影、默认外观、头像裁切全在它里面（纯函数 + 值）。
#include "..\ui\SkinModel.hpp"
#include "..\ui\Theme.hpp"
// 顶上那条自己画的标题栏。这个头是 2026-09-25 加的：`MainWindowUI::titleBar()` 只前置
// 声明了 `class TitleBar`，而"设置页改名字 → 条上立刻跟着变"那两处要调它的 setBrandText()。
#include "..\ui\TitleBar.hpp"
// 加载器那棵树底下每一条版本号前面挂的**它自己那个加载器的标志**（见
// fillLoaderVersionItems()）：那几条是"Fabric 能装哪些版本"，前面顶个草方块会让人
// 以为是原版号。树的顶层那五条由 MainWindowUI 那边给图标，这里只管子条目。
#include "..\ui\VersionIcons.hpp"
#include "..\ui\VersionTreeDelegate.hpp"
#include "..\ui\WindowChrome.hpp"
// 树上"这一行被激活了"的唯一接线口（鼠标单击 + 键盘回车，一次手势只进来一次）。
// 三棵树（版本树 / 加载器树 / 下载页那几张资源表）都走它，见那个文件头。
#include "..\ui\RowActivation.hpp"

// 「背景图」那一节挑回来的那张图：**拷进数据目录**再记路径（见那个文件头那段）。
// `ui/` 那边不许 include `src/`，所以收图这一步只能在 MainWindow 这一层做
// （`MainWindowUI::ThemeImageRow` 只管摆控件）。
#include "util/ThemeImage.hpp"

// 添加以下头文件（防止hpp变动导致文件失效）
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
// 「配置文件」那一页记盘上的"那一份"要一秒不差地带上修改时间（见 ConfigFileText::Stamp），
// 备份文件夹名也是拿它拼的。
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
// 玩家外观那一套（皮肤/披风贴图）和存档图标都按 QImage 处理。
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
// 拖进来的文件是从 QMimeData 上那串 url 读出来的（见 MainWindow::dropEvent）。
#include <QMimeData>
#include <QMenu>
#include <QMouseEvent>
// 底栏面板里那几行的图标要按"底栏那几行字的墨色"上色（图标是画的，颜色由调用方给）。
#include <QPalette>
// 下载页那几张资源表前面那个缩略图（onResourceIconReady 里还要造一个全透明的占位）。
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
// 那几处"先写临时文件、再原子替换"的落盘（启动脚本 / 导出 / 备份那几处）。
#include <QSaveFile>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
// 整合包那一页下完 .mrpack 之后要"排到下一轮"再走导入（不能在那个信号栈里直接开新批次，
// 见 onResourceDownloadFinished 里那段 NOTE）。
#include <QThread>
// 「文件管理」那几页的图标反查跑在它上面（见 requestInstanceFileIcons 里那段 NOTE）。
#include <QTimer>
#include <QUrl>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QtConcurrent/QtConcurrentRun>
// 日志窗那两颗导出键的保存框默认落在桌面（见 exportDefaultDirectory()）。
#include <QStandardPaths>

// 底栏右面板那三行要按 saved_at 排序（std::stable_sort：同一秒里存的两个账号要保持
// 列表里原来的先后，不然每次点开顺序都可能不一样）。
#include <algorithm>
// 「配置文件」那一页挑最近那一份备份（`std::greater<QString>`：备份文件夹名倒序 == 时间
// 倒序，见 ConfigFileText::backupFolderName() 那段 NOTE）。
#include <functional>
#include <memory>

#include "auth/AccountStore.hpp"
#include "auth/AuthManager.hpp"
#include "auth/SkinApi.hpp"
#include "controllers/JavaRuntimeController.hpp"
#include "controllers/JavaSettingsController.hpp"
#include "controllers/PresetController.hpp"
#include "controllers/LauncherPreferencesController.hpp"
#include "controllers/CrashReportController.hpp"
#include "controllers/IntegrityController.hpp"
#include "controllers/ToolboxController.hpp"
#include "controllers/BackupController.hpp"
#include "controllers/ServerPingController.hpp"
#include "controllers/FolderController.hpp"
#include "controllers/ModpackExportController.hpp"
#include "controllers/InstanceFileController.hpp"
#include "controllers/InstanceTransferController.hpp"
#include "controllers/InstanceDialogController.hpp"
#include "controllers/InstanceEditorController.hpp"
#include "controllers/ImportController.hpp"
#include "controllers/GameSessionController.hpp"
#include "controllers/LaunchController.hpp"
#include "controllers/ConnectionsController.hpp"
#include "controllers/InstanceIconController.hpp"
#include "controllers/ActivityCenterController.hpp"
#include "controllers/TaskDetailController.hpp"
#include "controllers/HomeQuickController.hpp"
#include "controllers/InstanceMaintenanceController.hpp"
#include "controllers/ServerWorkspaceController.hpp"
#include "controllers/ServerConsoleController.hpp"
#include "controllers/ServerSettingsController.hpp"
#include "controllers/ThemeSettingsController.hpp"
#include "controllers/InstancePagesController.hpp"
#include "controllers/PlayerSkinController.hpp"
// 下载页那几类资源的取数（搜索 / 版本列表 / 图标）。头文件那边只前置声明了这个类
// （那里用不到它的定义），这里要用真的。
#include "download/ResourceStore.hpp"
#include "launcher/AsciiPathAlias.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/DefaultSkin.hpp"
// 拖进来的那一份文件是哪一类（纯判断，见那个头文件）—— 2026-09-29 加。
#include "manage/DroppedResource.hpp"
#include "manage/FolderManager.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/InstanceGroupStore.hpp"
#include "manage/ModpackExport.hpp"
#include "manage/ModpackImport.hpp"
// 离线账号自己挑的那张皮肤（「账号与外观」→「换一张…」）—— 2026-10-18 加。
#include "manage/OfflineSkin.hpp"
#include "manage/PlayerSkinStore.hpp"
#include "manage/RandomSkinStore.hpp"
#include "manage/VersionDisplay.hpp"
#include "manage/VersionManager.hpp"
#include "manage/WorldFiles.hpp"
// 游戏异常退出那几句怎么得出来（coding plan §4.3）：纯函数，规则全在它自己那个文件里。
#include "diag/GameExitDiagnosis.hpp"
#include "settings/EffectiveJavaSettings.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "settings/LauncherSettings.hpp"
// 任务账本（2026-10-12，coding plan §2 / §3）—— 底栏中间那格和任务中心面板都读它。
// 账本现在由 ActivityCenterController 持有；MainWindow 仍在迁移期通过 controller 更新执行进度。
#include "tasks/TaskManager.hpp"
#include "util/AppPaths.hpp"
// 日志窗底下那颗「导出错误报告…」那一份文本怎么拼（头部那些事实 + 两段日志）。
#include "util/ErrorReport.hpp"
// 「服务器列表」那一页要读 servers.dat（NBT，老版本还是 gzip 过的）。
#include "util/NbtUtil.hpp"
// 同一页的「编辑」「添加」「删除」（2026-10-10）：改名字和地址、加一台、拿掉一台都要
// **写** servers.dat —— NbtUtil 那一层故意只读，写侧单独一层（格式、按值匹配、gzip
// 保留的理由都在它的文件头）。`ServerListFile::pathFor()` 也是这一份路径的唯一出处。
#include "manage/ServerListFile.hpp"
// 服务端工作区那一层（coding plan §6 / 里程碑 M4）：判据层（状态 / EULA / properties）和
// 扫盘那一层（`<数据目录>/Servers` 底下谁在住）。界面那一页只摆控件，判据全在这儿。
#include "download/ServerInstall.hpp"
#include "server/ServerModel.hpp"
#include "server/ServerRegistry.hpp"
#include "server/ServerRestart.hpp"  // §9.7 崩溃自动重启那几条判据（纯头）
#include "server/ServerResourcePack.hpp"  // §9.7 本地资源包那一份 zip 的判据 + SHA-1
#include "server/ServerContent.hpp"  // §9.7 服务端内容（模组 / 数据包）的名字与分类判据
// 服务端安装那四段的**判据层**（"还差哪一份元数据、取回来怎么读、分成几步"，coding plan
// §6.2 / M4-2）：纯函数，`verify` 的 ZY 一节直接钉它。这里只用到那几个翻译。
#include "server/ServerInstallRun.hpp"
#include "server/ServerRun.hpp"  // 服务端"跑"的那几条判据（纯头，见它的文件头）
// 同一页的「刷新」：真的连上去问一遍每台现在什么样（图标 / MOTD / 人数 / 延迟）。
#include "util/ServerPing.hpp"
#include "util/SystemMemory.hpp"
// 「导入整合包…」要读压缩包根目录里的 modrinth.index.json（ZipUtil::readEntry）。
#include "util/ZipUtil.hpp"

namespace {

/**
 * @brief 读下拉框里选中的版本号（**不是**显示文本）。
 * @param combo 版本下拉框。
 * @return 选中的版本号；没选中任何项时返回空串。
 *
 * NOTE: 取的是 Qt::UserRole 里存的裸 id，不是 currentText()。这两个值从"版本显示名"
 *       那次改动起就分家了：条目文本可能是 `我的整合包-1.20.1-Forge`，而真正进
 *       `--version` 和文件路径的永远是 id。拿文本当版本号用会一路静默地拼出错误路径。
 *
 * currentIndex() 为 -1 表示"当前什么都没选"（下拉框空着，或者刚好被 clear() 过），
 * 这时 currentData() 也要当没选。显式写 currentIndex() 是为了说清为什么空串可以当
 * 判据：任何版本号都不会是空串。
 */
QString selectedVersionId(const QComboBox *combo) {
    return combo->currentIndex() < 0 ? QString() : combo->currentData().toString();
}

// 任务账本到界面 DTO 的枚举翻译由 ActivityCenterController 统一负责。

/**
 * @brief 一串步骤名 ⇒ 账本要的那张步骤表（`Tasks::TaskStep`），第 @p runningIndex 格是
 *        正在跑的那一格。
 *
 * 服务端安装那四段报步骤的方式是"一条直线、一格一格往前走"（`ServerInstallRun::stepNames()`
 * 给名字，`stepsChanged` / `stepChanged` 给走到哪儿了），而账本要的是**整张表**。这一处就是
 * 那个翻译：下标小于正在跑的都算跑完了、等于的算在跑、大于的还是待办。
 *
 * @param runningIndex 负数 = 还没开始（全都算待办）。
 */
QList<Tasks::TaskStep> taskStepsFromNames(const QStringList &names, int runningIndex) {
    QList<Tasks::TaskStep> steps;
    steps.reserve(names.size());
    for (int i = 0; i < names.size(); ++i) {
        Tasks::TaskStep step;
        step.name = names.at(i);
        if (i < runningIndex) {
            step.state = Tasks::TaskStepState::Done;
        } else if (i == runningIndex) {
            step.state = Tasks::TaskStepState::Running;
        } else {
            step.state = Tasks::TaskStepState::Pending;
        }
        steps.append(step);
    }
    return steps;
}

// ---------------------------------------------------------------------------
// 「世界设置」那一页的两趟翻译（2026-09-28）
//
// 【为什么这个翻译非有不可】
//
// `ui/` **不许 include `src/`**（这是这个仓库唯一一条不许越的界线，见 InstanceFilesPage
// 文件头）。所以那一页摆的东西全在它自己那两个只用 Qt 类型的结构里（`WorldView` /
// `Rule`），够不着 `WorldFiles::WorldInfo` 也够不着 `NbtTree::GameRule`。这两趟翻译就是
// 那条界线上的两个口岸。
//
// 【为什么是"两个方向各写一遍"而不是一个可逆的视图】
//
// 因为两边**本来就不一样**：去的那一趟要把盘上那一份翻译成"屏幕上摆什么"（并补上盘上还
// 没有的那几十条规则），回的那一趟只认"用户真动过哪几格"。硬做成一个双向结构就得把两种
// "没有"混成一种。抄错一处的症状是"改了一项、存下去发现没变"——验证工程量的是**页面上那个
// 控件里的值**，会当场红（见 mw_check 里那一节）。
//
// ★ 2026-09-29：回来的那一趟**不再看 `hasXxx`**（那四格现在一律可动 —— 盘上没有的项，
//   屏幕上摆的是默认值）。改成看 `WorldSettingsPage::Delta`（"和打开这一页时逐字比"），
//   "用户没碰的那几格一个字节都不写"这条纪律因此一点没松，只是判据从"界面灰不灰"挪到了
//   "值变没变"。
// ---------------------------------------------------------------------------

/// 盘上那一份 `level.dat` → 页面上摆的那一份。
[[maybe_unused]] WorldSettingsPage::WorldView worldViewOf(const QString &worldDir,
                                         const WorldFiles::LevelData &data) {
    WorldSettingsPage::WorldView view;
    view.dirName = QFileInfo(worldDir).fileName();
    view.gzipped = data.info.gzipped;

    view.hasLevelName = data.info.hasLevelName;
    view.levelName = data.info.levelName;
    view.hasVersionName = data.info.hasVersionName;
    view.versionName = data.info.versionName;
    view.hasFormat = data.info.hasDataVersion;
    view.formatText = data.info.hasDataVersion ? QString::number(data.info.dataVersion)
                                               : QString();
    view.hasLastPlayed = data.info.hasLastPlayed;
    view.lastPlayedMs = data.info.lastPlayedMs;

    view.hasAllowCommands = data.info.hasAllowCommands;
    view.allowCommands = data.info.allowCommands;
    view.hasHardcore = data.info.hasHardcore;
    view.hardcore = data.info.hardcore;
    view.hasDifficulty = data.info.hasDifficulty;
    view.difficulty = data.info.difficulty;

    view.hasRules = data.info.hasRules;
    // ★ 2026-09-29：这一列是**并过全集的**（用户原话「下面的世界设置不要要求进了世界才
    //   显示而是把所有的都列出来」）。盘上那份 `GameRules` 常常只有几条 —— 工具做出来的
    //   档、从没进过一次的档**一条都没有**，原来那一页遇到这种档就只写一句"进游戏玩一次
    //   就会写进去"，等于让用户先绕一圈。缺的那些现在由 `WorldFiles::knownGameRules()`
    //   （启动器认识的规则全集，值取游戏默认）顶着。
    //
    //   `Rule::inFile` 记着哪几条是"盘上真有的" —— 界面上据此挂那枚「默认」小标记，而
    //   **写盘时不用它**：那件事由 `deltaFromOpened()`（值变没变）说了算，理由见 .hpp。
    QHash<QString, QString> onDisk;
    for (const NbtTree::GameRule &rule : data.info.rules) {
        onDisk.insert(rule.name, rule.value);
    }
    for (const NbtTree::GameRule &rule : WorldFiles::allGameRules(data.info.rules)) {
        WorldSettingsPage::Rule row;
        row.name = rule.name;
        row.value = rule.value;
        row.inFile = onDisk.contains(rule.name);
        // 三档一一对应（`ValueKind` 和 `Kind` 的成员顺序也一样，但**故意用 switch 写死**
        // 而不是 `static_cast`：两个枚举以后各自加一档的时候，直接转换会静默地对错位）。
        switch (NbtTree::ruleKind(rule.value)) {
        case NbtTree::ValueKind::Bool:
            row.kind = WorldSettingsPage::Kind::Bool;
            break;
        case NbtTree::ValueKind::Int:
            row.kind = WorldSettingsPage::Kind::Int;
            break;
        case NbtTree::ValueKind::Text:
            row.kind = WorldSettingsPage::Kind::Text;
            break;
        }
        view.rules.append(row);
    }
    return view;
}

/// `Delta` 里那几条规则 → 数据层那一条（`applyChangedGameRules()` 吃它）。
[[maybe_unused]] QList<NbtTree::GameRule> changedRules(const WorldSettingsPage::Delta &delta) {
    QList<NbtTree::GameRule> out;
    out.reserve(delta.rules.size());
    for (const QPair<QString, QString> &pair : delta.rules) {
        NbtTree::GameRule rule;
        rule.name = pair.first;
        rule.value = pair.second;
        out.append(rule);
    }
    return out;
}

/// 用户**真改过**的那几格 → 盘上那一份（**以读出来的那一份为底**，只覆盖那几格）。
///
/// ★ 判据是 @p delta（"和打开这一页时那一份逐字比"），不是"页面上摆过没有"：那四格现在
///   一律可动，盘上没有的项在屏幕上摆的是默认值 —— 用户没碰的那几格**一个字节都不许写
///   进去**（"不发明"那条纪律的落点）。
///
/// ★ 底子用的是 @p data（刚重新读盘那一份），不是"打开这一页时那一份"：中间别人动过的
///   那几项因此**原样留着**，我们只动用户在页面上真改过的那几格。这和"配置文件"那条路
///   的做法一致（写之前重读一遍，改的是现在盘上这一份）。
///
/// NOTE: 规则**不在这里写**（先把 `info.rules` 清空）：新规则那一路要走
///       `MissingRule::Create`，而 `applyWorldInfo()` 里的规则那一截是"只改已有的"。
///       改过的那几条由 `changedRules()` 交出去、由 `saveWorldSettings()` 单独喂给
///       `applyChangedGameRules()`。清空是**必须**的 —— 不清的话 `applyWorldInfo()` 会把
///       盘上那几条原样再写一遍（值一样，改不动什么，但那是白做）。
[[maybe_unused]] void applyWorldViewTo(WorldFiles::LevelData *data, const WorldSettingsPage::Delta &delta) {
    WorldFiles::WorldInfo &info = data->info;
    info.rules.clear();
    if (delta.nameChanged) {
        info.hasLevelName = true;
        info.levelName = delta.nameValue;
    }
    if (delta.allowCommandsChanged) {
        info.hasAllowCommands = true;
        info.allowCommands = delta.allowCommandsValue;
    }
    if (delta.hardcoreChanged) {
        info.hasHardcore = true;
        info.hardcore = delta.hardcoreValue;
    }
    if (delta.difficultyChanged) {
        info.hasDifficulty = true;
        info.difficulty = delta.difficultyValue;
    }
}

/**
 * @brief 把一批版本灌进下拉框，并尽量保住用户原来选中的那一项。
 * @param combo 目标下拉框。
 * @param parts 每个版本的显示名组成部分。
 * @param options 全局「版本名格式」开关。
 *
 * 条目文本是 formatVersionName() 拼出来的显示名，版本号存在 Qt::UserRole 里，
 * 两者由 selectedVersionId() / currentText() 分开取。同一个版本在两个下拉框里
 * 可能显示得不一样（比如一个 `我的整合包-1.20.1`、一个 `1.20.1`），这是两个数据源
 * 各自独立设计的必然结果：已安装那边能从磁盘读到自定义名，在线清单 800+ 条只填 id。
 * 别为了"看着一致"给在线清单加逐条文件 I/O。
 *
 * 以前只有一个 QListWidget，clear() 会把用户选中的版本一起丢掉，所以下载完成时
 * 干脆不刷新列表。现在刷新必须自己把选中项捡回来。
 *
 * 语义是"只保住、不新增"：原来选的还在清单里就选回去；已经不在了（目录被删、
 * manifest 变了）就退回 currentIndex() == -1，让占位文案显出来，逼用户重新明确
 * 挑一个 —— 启动和下载都是重操作，不替用户猜。
 */
void fillVersionCombo(QComboBox *combo, const QList<VersionDisplayParts> &parts,
                      const VersionNameOptions &options) {
    const QString previous = selectedVersionId(combo);

    // 重灌过程会发 currentIndexChanged。现在接的这些槽都是幂等的，但先挡上，
    // 免得以后有人接槽时被中间态（先变成 -1 再选中）刷两遍。
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const VersionDisplayParts &part : parts) {
        // NOTE: 必须逐条 addItem(显示名, id)。addItems(QStringList) 是**不设**
        // Qt::UserRole 的，用它就等于所有条目的 currentData() 都是无效 QVariant，
        // selectedVersionId() 会一路返回空串 —— 启动按钮就再也启不动任何版本了。
        combo->addItem(formatVersionName(part, options), part.id);
    }

    // NOTE: 别指望 QComboBox 自己选中第 0 项。只要下拉框设了非空 placeholderText，
    // 往空模型里插第一批数据时它不会自动选中任何一项。选中项只能自己恢复。
    // 也必须按 id（findData）找回来，不能按显示文本（findText）——显示名会随
    // 自定义名/开关变，拿它当身份标识一定找错。
    if (previous.isEmpty()) {
        return;
    }
    if (const int restored = combo->findData(previous); restored >= 0) {
        combo->setCurrentIndex(restored);
    }
}

// ======================================================================
// 实例页那张树（分组）
// ======================================================================
//
// 这一组自由函数和上面那些直筒子列表的写法（fillVersionCombo / syncListToCombo 那一类）
// **不能合并**：实例列表要两层（组 + 实例）。硬合并的写法是"到处判 item 是组还是实例"，
// 那样每多一个调用点就多一次判错的机会。
//
// NOTE: 2026-09-22 下载页那份"可安装版本"从下拉框 + 列表换成了版本树，原来和这一组
//       配对的那几个（fillVersionList / syncListToCombo / syncComboToList /
//       fillVersionViews / plainVersionParts）随之删掉了 —— 它们只服务于那一对。

/**
 * @brief 实例树里一个组节点，以及挂在它下面的那些实例。
 *
 * 「未分组」用 groupId 为空表示 —— 它是**虚拟**的，不进 wtfml.folders，而且只有存在
 * 真实分组时才会被画出来（一个组都没有时列表是平铺的，见 fillInstanceTree）。
 */
struct InstanceGroupBucket {
    QString groupId;
    QString name;
    /// 这一组在树里**画成展开的还是收着的**（用户 2026-09-25 原话里的「默认展开/关闭」）。
    ///
    /// NOTE: 它的出处是 wtfml.folders 里那一格（InstanceGroupStore::Group::expanded），
    ///       用户点一下组行（或者齿轮菜单里那一项）就翻过来并落盘 —— 于是"下次重灌还
    ///       照这样画"。虚拟的「未分组」没有记录，恒为 true（它收起来这件事不落盘）。
    bool expanded = true;
    QList<VersionDisplayParts> parts;
};

/**
 * @brief 一个树条目代表的实例 id；**组头返回空串**。
 * @param item 树条目；nullptr 也返回空串。
 *
 * 所有读这棵树的地方都必须过这一道，不许直接 `currentItem()->data(0, Qt::UserRole)`。
 * 组头天然能被选中（QTreeWidget::setCurrentItem() 不看 ItemIsSelectable），而组头上
 * 那个 role 是空的 —— 界面于是悄悄变成"没选中任何实例"，用户按启动得到"未选择版本"，
 * 而屏幕上明明有一行亮着。
 *
 * NOTE: 判据是**它有没有组 id 那个 role**，而不是"实例 id 是不是空的"。后者在
 *       "某个实例的 id 恰好是空串"时会误判，而这个判据不该依赖那个前提。
 */
QString instanceIdOf(const QTreeWidgetItem *item) {
    if (item == nullptr) {
        return QString();
    }
    if (item->data(0, MainWindowUI::groupIdRole()).isValid()) {
        return QString();
    }
    return item->data(0, MainWindowUI::instanceIdRole()).toString();
}

/**
 * @brief 按分组把实例灌进实例页那张树。
 * @param tree 目标树。
 * @param buckets 组（含虚拟的「未分组」，见 InstanceGroupBucket）。
 * @param options 全局「版本名格式」开关。
 * @param runningIds 此刻在跑的那些实例 id（显示名后面那句「（运行中）」）。
 * @param marks 每个实例行前面那个小图标（`id -> QIcon`，由 instanceRowMark() 算好）。
 *        缺一个不画图标，不影响别的（验证工程直接摆一张只有几个键的表也能灌）。
 * @param pinnedGroupId **置顶组**是哪一个（InstanceGroupStore::pinnedGroupId()）。空串 =
 *        这份 .minecraft 里没有置顶组 ⇒ 哪一行都不摆那格箭头（点下去没有去处）。
 *        非空时按 @p pinnedInstanceIds 给两个互斥的角色：不在里面 ⇒ `rowPinRole()`
 *        （悬停画 ↑ 置顶）、在里面 ⇒ `rowUnpinRole()`（悬停画 ↓ 取消置顶）。两个角色都由
 *        fillInstanceTree 写，代理/eventFilter 两边读。
 * @param pinnedInstanceIds 此刻**正在置顶**的那些实例 id（`InstanceGroupStore::isPinned()`）。
 *        它和 @p pinnedGroupId 是**两件事**：前者是"哪些实例置着顶"、后者只是"这一份
 *        .minecraft 里有没有置顶组"。有置顶组但一个实例都没置顶时前者是空的、后者非空。
 *
 * NOTE: 【那两个角色为什么按**实例**给，不再按"这一行画在哪一组"给】
 *       2026-09-26 之前置顶 = 把它挪进置顶组，所以"这一行的组 id 就是置顶组"和"这个实例
 *       置着顶"是同一句话，判据写成前者（`bucket.groupId == / != pinnedGroupId`）。
 *       两边都显示之后这两句**不再等值**：置着顶的那个实例在**两个组里各有一行**，两行
 *       说的是同一个实例的同一个状态。按"画在哪一组"给的话老家那一行会画成 ↑（= 置顶），
 *       而它其实已经置着顶了 —— 点下去是"再置一次"（空操作），在用户眼里就是"这颗按了
 *       没用"。所以判据换成**实例自己**置没置顶，两行拿到同一个答案（都画 ↓）。
 *
 * 两种形态，由调用方给进来的 buckets 决定（它才知道有没有真实分组）：
 *   - **只有一格、groupId 为空** → 平铺，不插组头。这是"还没分过组"的样子，
 *     和换树之前那张列表逐项一致。
 *   - 否则每格画一个组头，实例挂在它下面。**一个实例都没有的组不画**（用户 2026-09-25
 *     原话：「空白的分组隐藏」）—— 藏起来的只是那行标题，组本身还在 wtfml.folders 里、
 *     还在「分组设置…」里，也照样能被接收实例（那颗齿轮的第一项列的是**全部**组，
 *     含这些藏着的，见 MainWindowUI::openGroupPicker()）。
 *
 * NOTE: 灌完**不动选中项**（和 fillVersionList 一致），收尾由调用方 syncTreeToCombo()。
 * NOTE: 每个实例条目的 tooltip 是**目录的完整路径**。显示名和目录名可能不一样
 *       （自定义名、拼进去的加载器名、或者用户在资源管理器里改过目录名），而
 *       "这个实例到底在哪个目录"是排查问题时唯一要问的东西。
 */
/// 一个实例在树里那一行该写什么：显示名（后面缀上"（运行中）"当它真的在跑时）。
///
/// **唯一一处**拼这一行 —— 灌树（fillInstanceTree）和游戏起落时那次局部刷新
/// （MainWindow::updateInstanceTreeRunState）都走它。分开写的话，游戏起来那一刻
/// 后缀会跟灌树时那版慢慢漂开。
QString instanceTreeItemText(const VersionDisplayParts &part, const VersionNameOptions &options,
                             bool running) {
    const QString base = formatVersionName(part, options);
    return running ? base + MainWindowUI::instanceRunningSuffixText() : base;
}

void fillInstanceTree(QTreeWidget *tree, const QList<InstanceGroupBucket> &buckets,
                      const VersionNameOptions &options, const QSet<QString> &runningIds,
                      const QHash<QString, QIcon> &marks, const QString &pinnedGroupId,
                      const QSet<QString> &pinnedInstanceIds) {
    const QSignalBlocker blocker(tree);
    tree->clear();

    // 平铺还是插组头，按**调用方给的** buckets 判（它才知道那一份 .minecraft 里有没有
    // 真实分组），不看下面滤掉空组之后剩下什么：分组全空的那一档确实一行都不画，
    // 但那仍然是"分过组的"，形态不该因此翻过来。
    const bool flat = buckets.size() == 1 && buckets.first().groupId.isEmpty();
    for (const InstanceGroupBucket &bucket : buckets) {
        // 一个实例都没有的组**不画**（见函数头）。平铺那一格不参与：那不是一个组。
        if (!flat && bucket.parts.isEmpty()) {
            continue;
        }
        QTreeWidgetItem *parent = nullptr;
        if (!flat) {
            parent = new QTreeWidgetItem(tree);
            parent->setText(0, MainWindowUI::groupItemText(bucket.name, bucket.parts.size()));
            parent->setData(0, MainWindowUI::groupIdRole(), bucket.groupId);
            // 行尾那颗**齿轮**：几何的唯一出处是 InstanceGroupDelegate::gearRect()，
            // 认下那一下的是 MainWindow::eventFilter()。虚拟的「未分组」那一格也挂
            // （它是"把实例放进某个组"的最后一条路，见 MainWindowUI::rowGroupGearRole()）。
            parent->setData(0, MainWindowUI::rowGroupGearRole(), true);
            // 组头**不给选中**：它是"一堆东西的标题"，不是"一个能启动的实例"。
            // 清掉 ItemIsSelectable 之后，选择模型会拒绝把它放进"被选中"的集合，
            // 所以点它**不会亮**（ui_check 的 M 段真的发了一次鼠标事件钉住这条）。
            //
            // NOTE: 但**它仍然会变成 currentItem()** —— QAbstractItemView 在
            //       mousePressEvent 里只查 ItemIsEnabled 就把当前项挪过去了，选择命令
            //       是后一步的事。所以这一行是"别让它高亮"，**不是**"别让它被选中"；
            //       真正兜住"当前选中的是哪个实例"的是 instanceIdOf() 那道守卫，
            //       每一个读 currentItem() 的地方都绕不过去。
            parent->setFlags(parent->flags() & ~Qt::ItemIsSelectable);
            // 展开还是收着**由那一组自己记着**（bucket.expanded ← wtfml.folders 里那一格）：
            // 用户点一下组行就翻过来并落盘，于是"下次打开还照这样画"。默认那五个组都是
            // 展开的（新组的默认值也一样）—— 折起来的组里有什么，用户得点一下才知道。
            parent->setExpanded(bucket.expanded);
        }
        // 平铺时挂在**不可见的根**下面，那和"顶层条目"是一回事，而且省掉了
        // "parent 是空的就换一个构造函数"那条分叉（两个构造函数收的类型不一样）。
        QTreeWidgetItem *holder = parent != nullptr ? parent : tree->invisibleRootItem();
        for (const VersionDisplayParts &part : bucket.parts) {
            auto *item = new QTreeWidgetItem(holder);
            item->setText(0, instanceTreeItemText(part, options, runningIds.contains(part.id)));
            item->setData(0, MainWindowUI::instanceIdRole(), part.id);
            item->setToolTip(0, QDir::toNativeSeparators(AppPaths::versionDir(part.id)));
            // 行前面那个小图标（原版草方块 / Forge 铁砧…）。谁画的、怎么认出来的
            // 见 instanceRowMark() 那段 —— 这一层只负责把它挂上去。
            const auto mark = marks.constFind(part.id);
            if (mark != marks.constEnd()) {
                item->setIcon(0, mark.value());
            }
            // 中间那一格（**一对反义词共用的一格**，见 MainWindowUI::rowUnpinRole() 那段）
            // 的判据：画它的是 InstanceGroupDelegate::paint()、认下那一下的是
            // MainWindow::eventFilter()，三处读的都是**这两个角色**。
            //   - `rowPinRole()` = 还没置顶 ⇒ 悬停画 ↑「置顶」；
            //   - `rowUnpinRole()` = 已经置顶了 ⇒ 悬停画 ↓「取消置顶」（用户 2026-09-25
            //     原话：「置顶组里的实例悬停时右侧显示下箭头，即取消置顶」）；
            //   - 两个都假 = **这一份 .minecraft 里没有置顶组**（pinnedGroupId 是空串：
            //     那一格点下去没有去处，见 InstanceGroupStore::pinnedGroupId() 那三条出口）
            //     ⇒ 这一格空着、也不弹话。
            // 两个判据互斥（同一个 pinned，一正一反），所以这一格永远只有一个方向。
            //
            // NOTE: 判据是**这个实例**置没置顶（见函数头那条 NOTE）—— 所以置着顶的那个实例
            //       在老家和置顶组里的**那两行画的是同一颗 ↓**，不是一个 ↑ 一个 ↓。
            // NOTE: "已经在置顶组里 ⇒ 不画上箭头"这条老规矩换个说法活着 —— 它仍然是"不画 ↑"，
            //       只是改成画 ↓ 了（原来那档什么都不画：挪不动的不画，这条先例在
            //       ui/GroupListDelegate 里）。
            const bool pinned = pinnedInstanceIds.contains(part.id);
            item->setData(0, MainWindowUI::rowPinRole(),
                          !pinnedGroupId.isEmpty() && !pinned);
            item->setData(0, MainWindowUI::rowUnpinRole(),
                          !pinnedGroupId.isEmpty() && pinned);
        }
    }

    // NOTE: 这里原来还有一段收尾：给**画出来的**组行写一格位掩码，说明"行尾那两颗上/下
    //       箭头该画哪一颗"。2026-09-25 用户把两颗箭头整个搬进了「分组设置…」那张表
    //       （原话：「这个箭头不应该在这里，而应该在 分组设置 里面，这里的删了，去分组设置
    //       里面加上，设置那个框下方的上移下移的按钮就删了」），于是这一段连同
    //       `paintedGroupRows` 整个没了 —— 组行现在**只有两个动作**：点这一行=展开/收起、
    //       点齿轮=那一组的更多操作。那张表里"能不能挪"是算出来的（行序就是组序），
    //       不需要角色位，见 ui/GroupListDelegate。
}

/**
 * @brief 让实例树里的选中项跟上下拉框（两边都按 id 找，不按文本）。
 * @param combo 版本下拉框。
 * @param tree 实例页那张树。
 *
 * 找不到那个 id（清单刚刷新、实例被删了）时**清空选中**，和列表版一样。
 */
void syncTreeToCombo(const QComboBox *combo, QTreeWidget *tree) {
    const QString id = selectedVersionId(combo);
    // 挡信号：这里是"跟着别人变"，不该反过来再把下拉框动一遍。真正的用户操作才发信号。
    const QSignalBlocker blocker(tree);
    if (!id.isEmpty()) {
        QTreeWidgetItemIterator it(tree);
        while (*it != nullptr) {
            if (instanceIdOf(*it) == id) {
                tree->setCurrentItem(*it);
                return;
            }
            ++it;
        }
    }
    // 没找到（或者下拉框本来就没选）：留空，别硬选一个别的。
    tree->setCurrentItem(nullptr);
}

/**
 * @brief 让下拉框跟上实例树里选中的那一项。
 * @param combo 版本下拉框。
 * @param tree 实例页那张树。
 *
 * NOTE: 选中的是**组头**时什么都不做 —— 点一个组的标题不是"挑一个版本"。守卫走
 *       instanceIdOf()，理由见它的说明。
 */
[[maybe_unused]] void syncComboToTree(QComboBox *combo, const QTreeWidget *tree) {
    const QString id = instanceIdOf(tree->currentItem());
    if (id.isEmpty()) {
        return;
    }
    const int index = combo->findData(id);
    if (index >= 0 && combo->currentIndex() != index) {
        combo->setCurrentIndex(index);
    }
}

// 「一个组」的两份表示之间的转换。看着啰嗦，但它是 ui/ 和业务层解耦的**全部代价**：
// ui/ 一直不 include src/（ui_check 那个工程只编 ui/MainWindowUI.cpp 就能跑），所以
// 分组对话框有自己的 Group、存储层有自己的 Group，两边逐字段相同。把它们合成一个
// 类型就得让 ui/ 依赖业务层，或者反过来 —— 相比之下这两段循环是最便宜的。

QList<InstanceGroupStore::Group> toStoreGroups(const QList<GroupDialog::Group> &from) {
    QList<InstanceGroupStore::Group> to;
    to.reserve(from.size());
    for (const GroupDialog::Group &group : from) {
        InstanceGroupStore::Group item;
        item.id = group.id;
        item.name = group.name;
        // 「默认展开」也搬过来：它在两边是同一个字段（不是"界面上的显示状态"），
        // 丢了的话在对话框里摆好的展开状态按一次确定就被重置回默认。
        item.expanded = group.expanded;
        // 「设为置顶组」同样要搬（2026-09-25）。这一格漏掉的表现很具体：用户在对话框里
        // 勾了置顶组、按确定，实例行行尾那颗上箭头**还是往老的那一组送** —— 勾选框像是
        // 没生效，而两份清单比出来"没变"的话连盘都不写（见 Group::operator==）。
        item.pinned = group.pinned;
        to.append(item);
    }
    return to;
}

QList<GroupDialog::Group> toDialogGroups(const QList<InstanceGroupStore::Group> &from) {
    QList<GroupDialog::Group> to;
    to.reserve(from.size());
    for (const InstanceGroupStore::Group &group : from) {
        GroupDialog::Group item;
        item.id = group.id;
        item.name = group.name;
        item.expanded = group.expanded;  // 见 toStoreGroups() 那段
        item.pinned = group.pinned;      // 见 toStoreGroups() 那段
        to.append(item);
    }
    return to;
}

// 「一个实例的启动五项覆盖」的两份表示之间的转换。理由和上面那两段一样：ui/ 不 include
// src/，所以 InstanceSettingsDialog 有自己的 Override、settings/ 有自己的
// JavaSettingsOverride，逐字段相同、由这两段循环在两边搬。

/**
 * @brief 组齿轮菜单（以及它里面"移入分组…"弹出的挑目标框）那一刻的那份**快照**。
 * @param ui 主界面（组行都长在它的实例树里）。
 * @param store 分组存储（组的清单、顺序、展开状态从这儿来）。
 * @param selectedKey 此刻选中的那个实例的归属键（instanceKey()）；空串 = 没选中。
 * @param selectedHomeGroupId 那个实例的**老家**在哪一组（`homeGroupIdOf()`）；空串 = 未分组
 *        （也含"没选中"那一档，见下面的 NOTE）。挑目标那个框里被灰掉、写着「已经在这一组
 *        里了」的就是它。
 *
 * NOTE: 实例数是在**当前那张树**上数的（它就是用户此刻看到的东西）：藏起来的组不在树上
 *       ⇒ 它是 0 —— 那正好也是"这一组里有几个实例"在那一刻的实话。置顶着的那一条在两个
 *       组里各画一行，所以那两个数里各算它一次 —— 这正是"那一组里有几行"的实话。
 *
 * NOTE: 【为什么老家由调用方给，不在这张树上现看】2026-09-26 之前看的是"选中的那一条画在
 *       哪个组下面"，因为那时候一行只画在一个组里、那个组就是它的归属。两边都显示之后
 *       同一个实例有**两行**（老家一行 + 置顶组里一行），"画在哪一组"取决于用户点的是
 *       哪一行 —— 同一个实例会给出两个不同的答案：点老家那行时「置顶」那格亮着，点置顶
 *       组那行时又变成老家那格亮着。而这一格灰掉的意思是"已经在这一组里了、点它没用"，
 *       跟"你刚好点的是哪一行"没关系。所以判据换成老家 —— 它是实例自己的属性，两行拿到
 *       同一个答案。老家那个值 `homeGroupIdOf()` 只有 MainWindow 算得出来（要 VersionManager
 *       和版本类型），这个自由函数手上只有 ui/ 和 store，所以由调用方递进来。
 *
 * NOTE: 它是个自由函数（不是 MainWindow 的成员），因为 MainWindowUI::GroupRow 是 ui/ 里的
 *       嵌套类型，而 MainWindow.hpp **不 include ui/ 的头**（见那个文件头那几条前置声明）。
 *       摆在这儿的那几个私有成员函数只收窄（组 id / 一整行）就不必把它写进头文件。
 */
QList<MainWindowUI::GroupRow> groupRowsForMenu(const MainWindowUI *ui,
                                               const InstanceGroupStore *store,
                                               const QString &selectedKey,
                                               const QString &selectedHomeGroupId) {
    QHash<QString, int> counts;
    const QTreeWidget *tree = ui->getInstanceList();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *top = tree->topLevelItem(i);
        const QVariant groupId = top->data(0, MainWindowUI::groupIdRole());
        if (!groupId.isValid()) {
            // 平铺那一档（一个真实分组都没有时）：顶层条目**就是**实例本身，没有组头
            // 可数 —— 那一档连「未分组」那格都不摆（见下面 `!groups.isEmpty()` 那道闸），
            // 所以这儿一个数都不记，谁也不会来问。
            continue;
        }
        counts.insert(groupId.toString(), top->childCount());
    }

    const bool hasSelection = !selectedKey.isEmpty();
    QList<MainWindowUI::GroupRow> rows;
    const QList<InstanceGroupStore::Group> groups = store->groups();
    rows.reserve(groups.size() + 1);
    for (const InstanceGroupStore::Group &group : groups) {
        MainWindowUI::GroupRow row;
        row.groupId = group.id;
        row.name = group.name;
        row.memberCount = counts.value(group.id, 0);
        row.expanded = group.expanded;
        row.hasSelection = hasSelection;
        row.containsSelected = hasSelection && selectedHomeGroupId == group.id;
        rows.append(row);
    }
    // 虚拟的「未分组」排在最后（和树上同一个位置）。**只有真的分过组才摆它** ——
    // 一个真实分组都没有时列表是平铺的，那颗齿轮压根没地方挂，这张表也就没人看得见。
    if (!groups.isEmpty()) {
        MainWindowUI::GroupRow rest;
        rest.name = MainWindowUI::ungroupedGroupText();
        rest.memberCount = counts.value(QString(), 0);
        // 它没有记录（那一格不落盘），树上每次都是展开的 —— 快照照实说。
        rest.expanded = true;
        rest.hasSelection = hasSelection;
        rest.containsSelected = hasSelection && selectedHomeGroupId.isEmpty();
        rest.virtualGroup = true;
        rows.append(rest);
    }
    return rows;
}

/**
 * @brief 某一组此刻的那份快照（groupRowsForMenu() 里对应的那一条）。
 * @param row 拿到的那一行。@param selectedKey / @param selectedHomeGroupId 见
 *        groupRowsForMenu()（两个都缺省 = 没选中实例）。
 * @return 这一组不在（菜单开着的时候被别处删了）时返回 false，@p row 原样不动。
 *
 * NOTE: 虚拟的「未分组」的组 id 就是空串，所以传空串进来拿到的是**它**，不是"没找到"。
 *       两个调用点要的都是"这一刻的样子"，所以它每次都重新翻一遍快照，不复用菜单里
 *       那一份 —— 菜单摆出来之后用户可能看两眼才点，这中间别处一行代码就能把它删了。
 */
bool groupRowFor(const MainWindowUI *ui, const InstanceGroupStore *store, const QString &groupId,
                 MainWindowUI::GroupRow *row, const QString &selectedKey = QString(),
                 const QString &selectedHomeGroupId = QString()) {
    const QList<MainWindowUI::GroupRow> rows =
        groupRowsForMenu(ui, store, selectedKey, selectedHomeGroupId);
    for (const MainWindowUI::GroupRow &item : rows) {
        if (item.groupId == groupId) {
            if (row != nullptr) {
                *row = item;
            }
            return true;
        }
    }
    return false;
}

/**
 * @brief 某一组那一行行尾**齿轮**占的那一格，全局坐标（弹挑目标框时的锚点）。
 * @return 那一行不在树上（一个实例都没有的组是藏着的）时返回空矩形。
 *
 * 几何的唯一出处是 InstanceGroupDelegate::gearRect() —— 和代理画它、eventFilter 认下
 * 那一下用的是同一个式子（写歪了的症状就是"看着点中了齿轮、其实点的是那一行"）。
 */
QRect gearAnchorFor(const MainWindowUI *ui, const QString &groupId) {
    QTreeWidget *tree = ui->getInstanceList();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->data(0, MainWindowUI::groupIdRole()).toString() != groupId
            || !item->data(0, MainWindowUI::rowGroupGearRole()).toBool()) {
            continue;
        }
        const QRect gear = InstanceGroupDelegate::gearRect(tree->visualItemRect(item));
        return QRect(tree->viewport()->mapToGlobal(gear.topLeft()), gear.size());
    }
    return QRect();
}

JavaSettingsOverride toSettingsOverride(const InstanceSettingsDialog::Override &from) {
    JavaSettingsOverride to;
    to.javaPath = from.javaPath;
    to.xmx = from.xmx;
    to.xms = from.xms;
    to.autoMemory = from.autoMemory;
    to.extraJvmArgs = from.extraJvmArgs;
    return to;
}

/// 把窗口那一组覆盖翻成对话框认的那一份（**代号字符串**，不是枚举 —— 见
/// InstanceSettingsDialog::WindowModeChoice 上面那段：ui/ 不许 include src/）。
[[maybe_unused]] InstanceSettingsDialog::Override toDialogOverride(const JavaSettingsOverride &from,
                                                  const GameWindowOverride &window
                                                  = GameWindowOverride()) {
    InstanceSettingsDialog::Override to;
    to.javaPath = from.javaPath;
    to.xmx = from.xmx;
    to.xms = from.xms;
    to.autoMemory = from.autoMemory;
    to.extraJvmArgs = from.extraJvmArgs;
    // 窗口那三项。模式取**代号**（有覆盖时才给值），宽高各自原样 —— "有没有值"这一位
    // 就是界面上「跟随全局 / 自定义」那一对的状态（见对话框那边灌值那一段）。
    if (window.mode.has_value()) {
        to.windowMode = gameWindowModeToken(*window.mode);
    }
    to.windowWidth = window.width;
    to.windowHeight = window.height;
    return to;
}

/// 上面那个的反方向：对话框摆好的覆盖 → 要写进 meta 的那一组。
///
/// NOTE: 代号认不出来时**当没覆盖**（= 删键）而不是"退回小窗那一档" —— 认不出来只可能
///       来自手改过的 meta 或改过名字的老版本，那时的意思都是"不认识"，把它写成一个具体的
///       档位等于替用户做了个他从没做过的选择。读侧（gameWindowOverrideFromJson）同一个态度。
GameWindowOverride toWindowOverride(const InstanceSettingsDialog::Override &from) {
    GameWindowOverride to;
    if (from.windowMode.has_value()) {
        GameWindowMode mode = GameWindowMode::Windowed;
        if (gameWindowModeFromToken(*from.windowMode, &mode)) {
            to.mode = mode;
        }
    }
    // 宽和高一起（对话框那边要么两个都有值、要么两个都没有，见 .hpp 里 override() 那段）。
    to.width = from.windowWidth;
    to.height = from.windowHeight;
    return to;
}

[[maybe_unused]] InstanceSettingsDialog::GlobalValues toDialogGlobal(const EffectiveJavaSettings &from) {
    InstanceSettingsDialog::GlobalValues to;
    to.javaPath = from.javaPath;
    to.xmx = from.xmx;
    to.xms = from.xms;
    to.autoMemory = from.autoMemory;
    to.extraJvmArgs = from.extraJvmArgs;
    return to;
}

/// 「窗口模式」那个下拉框里的档位表（显示的字 + 存进 meta 的代号）。
///
/// **标签在这里拼、代号从 src/ 那边取**（gameWindowModeToken()）—— 这一份表就是"ui/ 不
/// include src/"那条分层规矩留下的那一道缝的填法：界面那一侧只认"标签 + 代号"这一对，
/// 代号是什么、有几档，全由这里说了算。顺序 = 界面上的顺序 = "窗口从花到满"。
[[maybe_unused]] QList<InstanceSettingsDialog::WindowModeChoice> windowModeChoices() {
    return {InstanceSettingsDialog::WindowModeChoice{
                QObject::tr("小窗（按下面的大小开一个窗口）"),
                gameWindowModeToken(GameWindowMode::Windowed)},
            InstanceSettingsDialog::WindowModeChoice{
                QObject::tr("最大化（铺满屏幕，任务栏留出来）"),
                gameWindowModeToken(GameWindowMode::Maximized)},
            InstanceSettingsDialog::WindowModeChoice{
                QObject::tr("全屏（原版自己的 --fullscreen）"),
                gameWindowModeToken(GameWindowMode::Fullscreen)}};
}

/// 这一档在那一句「跟随全局（当前 …）」里怎么写。
[[maybe_unused]] QString windowModeLabel(GameWindowMode mode) {
    switch (mode) {
        case GameWindowMode::Maximized:
            return QObject::tr("最大化");
        case GameWindowMode::Fullscreen:
            return QObject::tr("全屏");
        case GameWindowMode::Windowed:
            break;
    }
    return QObject::tr("小窗");
}

/**
 * @brief 按 itemData 里的枚举值选中一项。
 * @return 找到并选中了返回 true；没有这一项返回 false（调用方决定要不要兜底）。
 *
 * NOTE: 枚举以 int 存进 itemData。界面这边只认"存进去的那个值"，不去 include
 *       src/settings 里的枚举 —— 界面层不该为了显示一个下拉框去依赖设置模块。
 */
template <typename Enum> bool selectByEnumData(QComboBox *combo, Enum value) {
    const int index = combo->findData(static_cast<int>(value));
    if (index < 0) {
        return false;
    }
    combo->setCurrentIndex(index);
    return true;
}

// 2026-09-28：这里原来有一对「往 Java 版本下拉框里塞条目 / 把它摆正」的帮手
// （addJavaRuntimeItem / selectConfiguredJava）。那个下拉框换成了「Java 管理」那颗
// 按钮，配对的两条也就没主了 —— 现在"这台机器上有哪些 Java"这件事整份住在
// m_localJavaVersions 里，界面那一页按主版本号自己去里面取。

// ---------------------------------------------------------------------------
// 底栏那两块上拉面板（左：正在跑的实例 · 右：最近用过的账号）
// ---------------------------------------------------------------------------

/// 左面板行尾那两颗按钮的**编号**（onBottomBarPanelAction() 认的就是这两个数）。
///
/// NOTE: 编号是"这一行第几颗"，不是"这颗按钮上画着什么" —— 拿图形反认的话，哪天把电源
///       换成终止、或者两颗换个位置，那一下点下去会静悄悄地做错事（关实例变成开设置）。
constexpr int kRunningPanelStopAction = 0;
constexpr int kRunningPanelSettingsAction = 1;
/// 「看这一场游戏的输出」（2026-10-14 加，coding plan §3.3「运行中实例弹层优先展示实例名、
/// 运行时长和『打开日志/停止』等可用动作」）。**摆在最后**：前两颗是"对**这一份会话**
/// 做什么"（关了它 / 改它的设置），这一颗是"看它说了什么"，最不打紧的一下摆边上。
constexpr int kRunningPanelLogAction = 2;

/// 左面板里一行的那三颗按钮（电源 = 关掉这份会话，齿轮 = 进这个实例的设置，
/// 「日志」= 打开这一场的日志窗）。
/// @param ink 图标那一档颜色（和底栏那几行字同一个墨色 —— 画的图标不带自己的颜色）。
QList<MainWindowUI::PanelAction> runningPanelActions(const QColor &ink) {
    const int iconSize = MainWindowUI::bottomBarPanelActionIconSize();
    MainWindowUI::PanelAction stop;
    stop.icon = VersionIcons::power(iconSize, ink);
    // 图标按钮**必须**有 tooltip：一个电源符号说明不了"点了会把游戏直接杀掉、不会存档"。
    stop.tooltip = QObject::tr("关闭这个实例（直接结束游戏进程，游戏不会存档）");
    MainWindowUI::PanelAction settings;
    settings.icon = VersionIcons::gear(iconSize, ink);
    settings.tooltip = QObject::tr("打开这个实例的设置");
    // 这一颗是**字的**（理由见 bottomBarPanelLogText() 那段：14 px 上画不出一个一眼认得出
    // 的"日志"符号）。NOTE: 图标那两颗是 `setFixedSize(actionSize)` 的正方形，这一颗按字
    //        撑宽 —— 行尾那几颗是"齐着右边摆"的，混着摆不会错位（见行控件那一段）。
    MainWindowUI::PanelAction log;
    log.text = MainWindowUI::bottomBarPanelLogText();
    log.tooltip = QObject::tr("打开这一场游戏的日志窗");
    return {stop, settings, log};
}

/// 这个实例属于哪个分类（正式版 / 快照版 / 远古版 / 愚人节版）。
///
/// 单一出处是 VersionCategory::channelFor()（和下载页那棵树**同一个出处**，别在这儿另写
/// 一个 switch：写歪了的症状是"快照那一栏前面白草、远古那一栏还是绿草"，不报错、只是难看）。
/// 那条要一个 `type` 字段，两个地方能给：
///   - wtfml.meta 里手写的 `type`（**优先**：用户明确写下的东西盖过一切，他也可能是把
///     一个实例"装作"另一个分类）；
///   - 没有就回实例自己的 `versions/<id>/<id>.json` 里读 —— 那才是这个字段的出处。
/// 两个都读不到（meta 丢了、json 也被删了）就当**正式版**：大多数实例是正式版，而
/// "一个方块都不画"在界面上是个说不清的空白。
///
/// 【2026-09-25：第二段是补上的，在那之前这里是个真 bug】
/// `type` 原来**只从 meta 读，而全项目没有任何一处往 meta 里写过它**（写侧那半从来没做
/// 出来）。于是这个函数对每一个已装实例都退化成"正式版"，看得见的后果有三处：快照版实例
/// 自动归类跑进「原版」组、默认组「快照版 / 远古版」永远是空的、实例页那一行的图标
/// （见 instanceRowMark()）全是草方块。用户这一轮要的正是"给每个版本一个对的图标"，
/// 而底下的因是这一条。
///
/// 修法选的是**回 json 读**，不是"装的时候往 meta 里抄一份 type"：
///   - json 本来就是这个字段的出处。抄一份出去就有两个可能不一致的地方（用户在资源管理器
///     里换掉一份 json、或者把同一个版本号重装一遍，meta 里那份就过期了，而且看不出过期）；
///   - `getVersionJson()` 给的是**合并过**的那一份（顺 inheritsFrom 链），所以整合包实例
///     的 type 是从它继承的原版那一层来的 —— 那正是它该显示的分类。而装的时候手里只有
///     `pack.gameVersion` 这个版本号，想写 type 反而要再回清单里查一次。
/// 代价是每个"meta 里没写 type 的"实例第一次问它时要读+解析一次那份 json；结果在
/// VersionManager 里带缓存（mergedVersionJsons），所以一次会话里每个实例只读一次。
VersionCategory::Channel versionChannelOf(const VersionManager *versions, const QString &versionId) {
    QString type = versions == nullptr
                       ? QString()
                       : versions->versionMeta(versionId).value(QStringLiteral("type")).toString();
    // 先看一眼那份 json 在不在：不在就别问 getVersionJson()（它会 qWarning，而这个函数
    // 每次刷新实例列表都要对每个实例跑一遍，一句"版本 JSON 找不到"会把日志刷满）。
    //
    // NOTE: 这道闸只管"文件不在"那一种 —— **解析不了的**（用户手里是一份坏 json）照样会
    //       抱怨一句"Failed to parse ..."，而且每次刷新都会再说一遍（解析失败那条路不进
    //       缓存，见 VersionManager::getVersionJson）。这是**故意留着**的：那份 json 坏掉
    //       的实例本来就启动不了，日志里说一句比闷着强，而且它跟"缺 jar""缺 java"是同一档
    //       频率（一次刷新一句，不是每个像素一句）。见 mw_check 的 J 节 —— 那一节的样本
    //       文件因此必须是**能解析**的 json。
    if (type.isEmpty() && versions != nullptr
        && QFile::exists(InstanceFiles::jsonPath(versionId))) {
        type = versions->getVersionJson(versionId).value(QStringLiteral("type")).toString();
    }
    if (type.isEmpty()) {
        type = QStringLiteral("release");
    }
    return VersionCategory::channelFor(versionId, type);
}

/// 这个实例该画哪个分类的方块（草方块 / 覆雪草块 / 土块 / 幸运方块）。
int versionChannelIndexOf(const VersionManager *versions, const QString &versionId) {
    return VersionCategory::channelIndex(versionChannelOf(versions, versionId));
}

/// 这个实例**实际**基于哪个加载器 —— 给界面看的名字（空串 = 没认出来）。
/// @param versionOut 认出来时顺手带上加载器版本（只有"加载器对不上"那道闸要用它）。
///
/// ★ 判断与那两条规矩（"先看已经读过的那一格、为空再回扣一次版本 json"、"为什么必须回扣"）
///   在 `InstancePagesController::loaderName()` 里。这个自由函数留着是因为 mw_check 的
///   判据按名字驱动它，而且它在文件里还有十来处调用点。
QString instanceLoaderName(const VersionDisplayParts &parts, const VersionManager *versions,
                           QString *versionOut = nullptr) {
    return InstancePagesController::loaderName(parts, versions, versionOut);
}

/// 加载器名字 → **分组用的那个小写键**（`VersionGroup::loader` 的写法）。
///
/// ★ 两侧的词形**不是一套**，这是本仓库里最容易静默失灵的一处：`instanceLoaderName()`
///   回的是显示名（「Fabric」/「Forge」/「NeoForge」/「Quilt」/「OptiFine」），而
///   `ResourceCatalog::groupVersions()` 的 `preferredLoader` 与 `VersionGroup::loader`
///   收的都是小写那一套（「fabric」/「neoforge」…）。直接把显示名递进去 ⇒ 一个字都比不上
///   ⇒「把对应加载器排在前面」这件事**永久不生效，而屏幕上一点异常都没有**。
///
/// 认不出来（空串 / 别的加载器）回空串 = "没人在乎"，`groupVersions()` 就照旧排。
QString loaderGroupKey(const QString &loaderName) {
    return loaderName.trimmed().toLower();
}

/// 这个实例**没挑过图标**时该画哪一个方块（草方块 / 覆雪草块 / 土块 / 幸运方块，或者
/// 某个加载器的标志）—— 也叫它的「默认图标」。
///
/// 两条路，用的都是**另外两棵树上挂的同一份图标**（别在这儿另画一套）：
///   - **装了加载器的**（见 instanceLoaderName()：meta 里记着的，或者版本 json 自己说的）：
///     给那个加载器的标志（Fabric / Forge / NeoForge / OptiFine），走
///     `VersionIcons::loaderMark()` —— 安装页那棵加载器树上挂的就是它；
///   - **没装加载器的**（原版 / 快照版 / 远古版 / 愚人节版）：按版本分类给，走
///     `VersionIcons::versionChannelMark()` —— 下载页那棵版本树上挂的就是它。
///
/// 于是"下载时就带"这件事**不需要任何落盘**：图标是从实例自己那份 json 的 `type` 现算的
/// （见 versionChannelOf()），而那份 json 是下载的时候写下去的 —— 装完第一次刷新就有图标，
/// 老实例（一个 type 都没写过）也一样算得出来，不用回填任何东西。
///
/// NOTE: 加载器名 → 下标那一步用 `loaderKindFromIndex()` 反查，**不自己写一张"Forge 就是 1"
///       的表**：那张表的唯一出处是 MainWindowUI::modLoaderOptionNames()（图标那边的下标
///       就是照它排的），抄一遍就会在某次加加载器时慢慢漂开。
/// NOTE: 认不出来的加载器名（用户手写的别的名字、或者以后新加的加载器）也是草方块 ——
///       和 loaderMark() 越界那条一个规矩：认不出来就画原版。
///       **Quilt 就是这一档**：`instanceLoaderName()` 认得出来（版本 json 里
///       `org.quiltmc:quilt-loader` 那条），而 MainWindowUI 那四颗加载器钮里没有它 ⇒ 画草
///       方块。这是老行为（我们自己的安装器不装 Quilt，这一档只从 json 那一路进得来），
///       要给它一个标志就加一颗加载器钮 —— 别在这儿另画一张图。
QPixmap defaultInstanceMark(const VersionDisplayParts &part, const VersionManager *versions,
                            int size) {
    const QString loader = instanceLoaderName(part, versions);
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
    return VersionIcons::versionChannelMark(versionChannelIndexOf(versions, part.id), size);
}

/// 实例页某一行前面那个小图标（用户 2026-09-25 原话：「为每个版本预设一个图标（默认下载时
/// 就带，原版草块，forge铁砧等，现有的没有就补上）」；2026-09-26 加上了"用户可以自己换一张"，
/// 那一条在「实例设置 → 概览 → 图标」里）。
///
/// **挑过的压过没挑过的**：wtfml.meta 里有 icon 就按它走，没有才落到上面那个默认图标。
/// 三种形态的判据与写法见 VersionManager::iconField()（那是唯一出处，这里不再抄一遍）。
///
/// NOTE: 收 @p size 而不是自己去问两个 size 常量里的哪一个 —— 同一个实例的图标在实例树上和
///       底栏「正在运行」那一行里**是同一张图**，只是边长不同（那两处各有各的常量）。
///       让调用方递边长，这里就只需要一处判断。
/// NOTE: `builtin:` 那个前缀认不出来（用户手写了个 `builtin:草方块`）时**落到默认**而不是
///       画一张空图 —— 和那几处"认不出来就当原版"同一个态度：屏幕上宁可显示一个不对的
///       方块，也不要一个空洞。
/// NOTE: 本地那张走 VersionIcons::fileMark()（按路径 + 修改时间缓存）。**不在这儿直接
///       QPixmap(path)**：这个函数实例树每一行都要问一次，而每刷新一次整棵树重建一遍，
///       用户挑的那张 1024×1024 的图会被解上成百遍。
QPixmap instanceRowMark(const VersionDisplayParts &part, const VersionManager *versions, int size) {
    const QString iconKey = VersionIcons::iconKeyFromValue(part.icon);
    if (!iconKey.isEmpty()) {
        const QPixmap mark = VersionIcons::iconMarkForKey(iconKey, size);
        if (!mark.isNull()) {
            return mark;
        }
    } else if (part.icon == InstanceSettingsDialog::localIconValue()) {
        // 「本地那张」= 实例目录下那份 icon.png（名字的唯一出处是 AppPaths::versionIconPath）。
        const QPixmap mark = VersionIcons::fileMark(AppPaths::versionIconPath(part.id), size);
        if (!mark.isNull()) {
            return mark;
        }
    }
    return defaultInstanceMark(part, versions, size);
}

/// 这一份 .minecraft 里**现在真的还在**的那些组 id。默认组被用户删掉之后它就不在里面了，
/// 于是一整组实例自动落到「未分组」—— 这正是用户要的「组被删掉后版本放到未分组里」。
QSet<QString> knownGroupIdsOf(const InstanceGroupStore *groups) {
    QSet<QString> known;
    for (const InstanceGroupStore::Group &group : groups->groups()) {
        known.insert(group.id);
    }
    return known;
}

/// 一个实例**默认**该归到哪个默认组 —— 只给"用户还从来没给它表过态"的实例用（见调用点）。
///
/// 判据就是用户 2026-09-24 定的那五个组名：
///   - 装了模组加载器 → 可安装mod（判据见 instanceLoaderName()：**不只** wtfml.meta 那一格，
///     版本 json 自己说的也算 —— PCL 那类合并实例 meta 里是空的，只按 meta 判会把 67 个
///     装了加载器的实例全扔进「原版」，而「可安装mod」永远是空的）；
///   - 否则按版本分类：正式版 → 原版、快照版 → 快照版、远古版 → 远古版；
///   - **愚人节版没有对应的默认组**，返回空串 = 未分组。用户给的那五个里没有它，而把
///     它塞进快照版是替用户做主（那九个版本的 type 全是 snapshot，正是这个"顺理成章"
///     的错法，见 VersionCategory.hpp 文件头）。
///
/// @param known 这一份 .minecraft 里**现在真的还在**的那些组 id。默认组被用户删掉之后
///        它就不在 known 里了，于是一整组实例自动落到「未分组」—— 这正是用户要的
///        「组被删掉后版本放到未分组里」，为它写任何清理代码都是多余的。
QString defaultGroupIdFor(const VersionDisplayParts &part, const VersionManager *versions,
                          const QSet<QString> &known) {
    const auto existing = [&known](InstanceGroupStore::DefaultKind kind) {
        const QString id = InstanceGroupStore::defaultGroupId(kind);
        return known.contains(id) ? id : QString();
    };
    // 加载器那一条先判：装了加载器的快照版/远古版也算"可安装mod"（那才是它现在的样子）。
    if (!instanceLoaderName(part, versions).isEmpty()) {
        return existing(InstanceGroupStore::DefaultKind::Moddable);
    }
    switch (versionChannelOf(versions, part.id)) {
    case VersionCategory::Channel::Release:
        return existing(InstanceGroupStore::DefaultKind::Vanilla);
    case VersionCategory::Channel::Snapshot:
        return existing(InstanceGroupStore::DefaultKind::Snapshot);
    case VersionCategory::Channel::Old:
        return existing(InstanceGroupStore::DefaultKind::Old);
    case VersionCategory::Channel::AprilFools:
        break;
    }
    return QString();
}

/// 一个实例**显示在**哪一组里 —— 也叫它的「归属」，树上摆在哪一组就是它。
///
/// 和 `InstanceGroupStore::groupIdOf()`（**存的是**哪一组）不是一回事：从来没表过态的实例
/// 由显示层按版本分类自动归类（defaultGroupIdFor）。凡是"要说清这个实例现在归在哪"的地方
/// 都得走这一个函数 —— 设置对话框那一格和实例树各说各的话，用户就会看到"对话框里写着未
/// 分组、而树上明明把它摆在「原版」里"。
///
/// NOTE: 判据是"文件里有没有这一条"（isAssigned）、**不是"groupId 是不是空"**：用户在对话框
///       里明确选了「未分组」的实例也算表过态（存的是那个保留值，读出来是空串），不该被自动
///       归类又拉回默认组去 —— 否则就是"点了确定没生效"。
QString displayGroupIdOf(const InstanceGroupStore *groups, const QString &key,
                         const VersionDisplayParts &part, const VersionManager *versions,
                         const QSet<QString> &knownGroupIds) {
    QString groupId = groups->groupIdOf(key);
    if (groupId.isEmpty() && !groups->isAssigned(key)) {
        groupId = defaultGroupIdFor(part, versions, knownGroupIds);
    }
    return groupId;
}

/// 一个实例的「老家」—— 它**本来**属于哪一组（置顶组里那一条同名的只是多出来的）。
///
/// 2026-09-26 用户把置顶改成**两边都显示**之前，这件事根本不需要回答：那时候"归属"只有
/// 一格（`groupIdOf()`），置顶就是把那一格改成置顶组。现在置顶着的那一条在盘上是
/// "members 指着置顶组 + pinOrigin 记着老家"，于是"它本来在哪一组"成了**显示层要问的问题**
/// （树上那一条留在老家、实例设置里「分组…」那一格显示的也是它）。
///
/// 判据就是 `pinOriginOf()` 那三种形态，**逐条**对应 `unpinInstance()` 的落点 —— 两处必须
/// 看到同一个答案，否则"取消置顶之后它跳去了一个和对话框里写着的不一样的组"。所以：
///   - 没在置顶 ⇒ 就是 displayGroupIdOf()（和以前一样，一个字没变）；
///   - 在置顶 + 记着一个组 ⇒ 那个组（**组已经没了就按"没有记录"处理**，见下）；
///   - 在置顶 + 记的是 null ⇒ 它置顶前没表过态 ⇒ 按版本类型现算一个（defaultGroupIdFor）；
///   - 在置顶 + 压根没有这一条 ⇒ 未分组（"用户自己塞进置顶组的"那一档）。
///
/// NOTE: 那个组被删掉之后**不写盘**（删组只清 members、pinOrigin 那一格要等下一次写盘的
///       自清理）⇒ 这里必须自己判一次"那一组还在不在"，不然会返回一个**树上没有的组 id**，
///       于是那一条同名的哪儿都不出现、看着像凭空少了一行。`unpinInstance()` 之所以不用
///       判，是因为它紧接着就要写盘、那一次写盘本身就会把悬空的记录清掉。
QString homeGroupIdOf(const InstanceGroupStore *groups, const QString &key,
                      const VersionDisplayParts &part, const VersionManager *versions,
                      const QSet<QString> &knownGroupIds) {
    if (!groups->isPinned(key)) {
        return displayGroupIdOf(groups, key, part, versions, knownGroupIds);
    }
    QString origin;
    switch (groups->pinOriginOf(key, &origin)) {
    case InstanceGroupStore::PinOrigin::None:
        return QString();
    case InstanceGroupStore::PinOrigin::Auto:
        return defaultGroupIdFor(part, versions, knownGroupIds);
    case InstanceGroupStore::PinOrigin::Group:
        break;
    }
    if (origin == InstanceGroupStore::ungroupedMarker()) {
        return QString();
    }
    return knownGroupIds.contains(origin) ? origin : QString();
}

/// 「本机现在跑着的这几份会话」翻成面板里的几行。
///
/// **一行 = 一份会话**（不是"一个实例"）：同一个实例多开时那两行的名字一样，靠第二行那个
/// "已运行 …"分开（用户 2026-09-24 定的："一行一个会话，有时长是好主意"）。行尾那颗电源
/// 关的也就是**这一份**会话。那一行是谁由 `PanelRow::key` 带着（= 会话号，见下面那一段）。
QList<MainWindowUI::PanelRow> runningGamePanelRows(const QList<LauncherManager::GameSession> &games,
                                                   const VersionManager *versions, const QColor &ink,
                                                   qint64 nowMs) {
    QList<MainWindowUI::PanelRow> rows;
    rows.reserve(games.size());
    // 一份都没在跑：摆一句说明（**不是**空白面板 —— 用户点开是想知道"有没有在跑"，
    // 空白答不了这个问题）。
    if (games.isEmpty()) {
        MainWindowUI::PanelRow empty;
        empty.title = MainWindowUI::bottomBarPanelNoRunningTitle();
        empty.subtitle = MainWindowUI::bottomBarPanelNoRunningNote();
        empty.placeholder = true;
        rows.append(empty);
        return rows;
    }
    for (const LauncherManager::GameSession &game : games) {
        MainWindowUI::PanelRow row;
        // 图标走**和实例树同一个函数**（instanceRowMark）：用户挑过就画他挑的那张，没挑过
        // 才按版本分类 / 加载器给。这里原来写的是「图标先占位，等实例自定义头像有了换的是
        // 这一处」—— 2026-09-26 头像有了，换的就是这一处。
        row.icon = instanceRowMark(versions->displayParts(game.versionId), versions,
                                   MainWindowUI::bottomBarPanelIconSize());
        row.title = game.displayName;
        // 那句"跑了多久"在产品侧唯一的一处算它（MainWindowUI::runningDurationText()）——
        // 放那边是为了让四档话术都能在验证工程里直接量（"已经跑了 3 天"那一档等不出来）。
        row.subtitle = MainWindowUI::runningDurationText(game.startedAtMs, nowMs);
        row.actions = runningPanelActions(ink);
        // 这一行是谁 = 那一份会话的编号（回来时按它认人，**不按"第几行"**：面板开着的时候
        // 哪个实例先结束是它自己的事，按行号认会关错游戏）。
        row.key = QString::number(game.sessionId);
        rows.append(row);
    }
    return rows;
}

/// 右面板里那一行右边的那颗按钮的下标（onBottomBarPanelAction() 认的就是它）。
constexpr int kAccountPanelUseAction = 0;
const QString kAccountPanelManageKey = QStringLiteral("__wtfml_manage_accounts__");

/// 一份**会话**的身份键：这个人是谁。
///
/// 和 `MainWindow::launchIdentityKey()` 是同一个规矩，**两处必须一致**（那边有整段说明）：
/// 离线会话的 `accountUuid` 是空的，拿 uuid 当键会把所有离线账号撞成同一个。
[[maybe_unused]] QString sessionIdentityKey(const LauncherManager::GameSession &session) {
    return session.accountUuid.isEmpty() ? session.accountName : session.accountUuid;
}

/// 「最后登录的三个账户」翻成面板里的几行。
///
/// 口径是用户 2026-09-24 定的：「最后登录的三个账户就拿缓存放（包含离线和正版，总共
/// 三个）」—— 读的就是**账号列表**那一条缓存（AccountStore::accounts()），不另外记账、
/// 不联网、不碰令牌。
///
/// 顺序按 `saved_at` **倒序**：那是这个列表里唯一一个"最近"（它写的是"这份登录凭据最后
/// 一次取到的时刻"，见 AccountStore::captureActive()）。NOTE: 切账号**不会**刷新它，所以
/// 准确的说法是"最近登录/添加过的三个"，而不是"最近用过的三个"—— 不新加一个字段是这一轮
/// 的取舍（新加键就得连带处理老数据、还有"登录取自"那句 tooltip 的语义）。
/// NOTE: `saved_at` 全是 `QDateTime::currentDateTimeUtc().toString(Qt::ISODate)` 写出来的
///       （唯一写方），形状一致，所以按文本比就是按时间比。
QList<MainWindowUI::PanelRow> recentAccountPanelRows(const QList<SavedAccount> &accounts, int limit,
                                                     const QHash<QString, QPixmap> &faces) {
    QList<MainWindowUI::PanelRow> rows;
    if (accounts.isEmpty()) {
        MainWindowUI::PanelRow empty;
        empty.title = MainWindowUI::bottomBarPanelNoAccountTitle();
        empty.subtitle = MainWindowUI::bottomBarPanelNoAccountNote();
        empty.placeholder = true;
        rows.append(empty);
        return rows;
    }

    QList<SavedAccount> sorted = accounts;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const SavedAccount &a, const SavedAccount &b) {
                         return a.savedAt > b.savedAt;
                     });
    if (limit > 0 && sorted.size() > limit) {
        sorted = sorted.mid(0, limit);
    }

    rows.reserve(sorted.size());
    for (const SavedAccount &account : sorted) {
        MainWindowUI::PanelRow row;
        // 行首那个头像**由调用方给**（`faces`，按 uuid 认人）：这里是纯函数，不能自己去
        // 读缓存、更不该去联网（见 MainWindow::accountFacePixmap() 那段 —— 它只碰本地
        // 已经躺着的图，一张都不下）。认人用 uuid 而不是名字：名字是可以重复的。
        // 取不到就是一张空图，那一行的行首**什么都不画**；这一档不该出现（最差也有一个
        // 首字母方块），只是不留"这里必须有一张图"的隐含前提。
        row.icon = faces.value(account.uuid);
        row.title = account.name;
        row.subtitle = account.offline ? MainWindowUI::bottomBarPanelOfflineAccountText()
                                       : MainWindowUI::bottomBarPanelOnlineAccountText();
        row.key = account.uuid;

        MainWindowUI::PanelAction use;
        if (account.isCurrent) {
            // 当前账号：按钮写「登出」、**可点**（用户 2026-09-24 原话：「把那个账户管理里的
            // 当前改成登出」）。原先这儿是「当前」+ 灰掉 —— 灰掉那颗点不动，于是这一行除了
            // 告诉用户"你就是你"之外没有别的用处。
            use.text = MainWindowUI::bottomBarPanelLogoutText();
            use.tooltip = MainWindowUI::bottomBarPanelLogoutTip();
        } else {
            use.text = account.offline ? MainWindowUI::bottomBarPanelSelectText()
                                       : MainWindowUI::bottomBarPanelLoginText();
            use.tooltip = account.offline
                              ? QObject::tr("切到这个离线账号（用这个名字离线进游戏）")
                              : QObject::tr("切到这个正版账号；令牌快到期了会顺手换一张");
        }
        row.actions.append(use);
        rows.append(row);
    }
    return rows;
}

/**
 * @brief 「这件事对账本上那件任务的收尾责任」——从建出来到 `disarm()` 之间任何一个早退
 *        （`return`）都由析构里那一发兜住。
 *
 * 【为什么非要它】
 *
 * 一件漏收尾的任务，症状是"底栏中间那格从此一直显示它、任务中心里它永远在'当前工作'那一栏"
 * （账本里再没有人会去碰它了）。而那种洞**只在走到那一条早退时才露出来**：`onStartButtonClicked()`
 * 从"点了启动"到"进程起来"之间有七八个 `return`（版本信息缺失 / 继承链不全 / natives 起不来 /
 * ASCII 别名建不出 / 启动前检查没过 / 用户在那个 Java 不匹配的框里点了取消），靠人记着每个
 * 前面都补一句 `finishTask(...)`，迟早漏一个 —— 漏掉的那一条平时一次都测不到。
 * 把责任挂在栈上就没有"漏一处"这回事：作用域一退出，没 `disarm()` 过的就一律按失败收尾。
 *
 * 【什么时候要 `disarm()`】
 *
 * 不是"没出错"就完事，而是"这件事**已经按别的说法收过尾了**"——三种：
 *   · 成功（进程真的起来了 ⇒ 由 `onProcessOutput()` 那边收）；
 *   · 用户自己取消（该记 `cancel()` 而不是失败，免得账本上多一次"失败的启动"）；
 *   · 转交给别人（比如后面那条异步的路自己会收）。
 */
class TaskAbortGuard {
public:
    explicit TaskAbortGuard(std::function<void()> onAbort) : m_onAbort(std::move(onAbort)) {}
    ~TaskAbortGuard() {
        if (m_armed && m_onAbort) {
            m_onAbort();
        }
    }
    /// 这一趟的收尾**已经由别处负责了**（见类上那段），撤销兜底。
    void disarm() { m_armed = false; }

private:
    std::function<void()> m_onAbort;
    bool m_armed = true;
};

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_ui(new MainWindowUI(this)),
      m_settings(new LauncherSettings(this)),
      m_launcherManager(new LauncherManager(this)),
      m_authManager(new AuthManager(this)),
      m_folderManager(new FolderManager(m_settings, this)),
      m_versionManager(new VersionManager(this)),
      m_resourceStore(new ResourceStore(this)),
      m_serverWorkspace(new ServerWorkspaceController(this)),
      m_javaSettings(new GlobalJavaSettings(this)),
      m_javaRuntimeController(new JavaRuntimeController(this)),
      m_groupStore(new InstanceGroupStore(this)),
      m_skinStore(new PlayerSkinStore(this)),
      m_randomSkinStore(new RandomSkinStore(this)),
      m_skinApi(new SkinApi(this)),
      // ★ 2026-10-15：玩家外观那一整套。它只**接**上面那三个 Store 的信号（不接管所有权：
      //   那三个本来就是这一层的子对象，活得比"这一趟外观"长得多），自己持有那九个状态。
      //   排在 m_skinApi 后面就够了 —— 它构造时只用那三个指针，不读盘。
      m_playerSkin(
          new PlayerSkinController(m_authManager, m_skinStore, m_randomSkinStore, this)) {
    // 安装页那棵树默认停在「不装（纯原版）」那一条（打开安装页时也是它，见
    // openInstallPage()）—— 这一位构造完就得是个真值，免得任何早于安装页读到它的路
    // （selectedLoaderIndex()）拿到一个没初始化的下标。
    //
    // NOTE: 这里原来写着「updateLoaderRows() 在下面就会被调到」，不成立 —— 构造函数里
    //       没有任何一处调它，第一次是 openInstallPage()（:1742）。初值不写在头文件里
    //       是因为那边只前置声明了 MainWindowUI。
    m_selectedLoaderIndex = MainWindowUI::modLoaderNoneIndex();
    // ★ 2026-09-29：整个窗口接拖放（用户原话见 .hpp 里 dragEnterEvent() 那段）。
    //   "没有哪一页开着"的那一档就是它接的；实例设置开着时是**那一页**先接住
    //   （拖放事件从光标底下那个控件沿父链往上找第一个接了拖放的控件）。
    setAcceptDrops(true);
    // ---------- 任务账本（2026-10-12，coding plan §2 / §3） ----------
    // **唯一**记录"现在有哪些事在跑、各自跑到哪儿了"的地方。它自己一个网络都不开、一个
    // 进程都不起（执行者照旧是 DownloadManager / VersionManager / LauncherManager）——
    // 这一层负责把它们的信号翻成 begin() / setProgress() / succeed()（§2.1「不把任务中心
    // 做成第二套业务逻辑」）。
    m_activityCenter = new ActivityCenterController(this);
    m_serverSettingsController = new ServerSettingsController(m_serverWorkspace);
    m_themeSettingsController = new ThemeSettingsController(m_ui, m_settings, this, this);
    m_javaSettingsController = new JavaSettingsController(
        m_ui, m_javaSettings, m_javaRuntimeController, this,
        JavaSettingsController::Host{[this](const QString &text) { appendLog(text); }}, this);
    // 「实例预设」那一页（coding plan §6 P1 / §7）：页面、增删改、提取、计划、备份、落盘
    // 全在 PresetController 里；这儿只把"实例叫什么 / 在不在跑 / 日志往哪儿写"三样递过去，
    // 以及实例页第三档那个宿主（页面由 controller 自己塞进去）。
    m_presetController = new PresetController(
        m_versionManager, m_ui->getInstancePresetPage(),
        PresetController::Host{
            [this](const QString &id) { return instanceDisplayName(id); },
            [this](const QString &id) { return m_launcherManager->isVersionRunning(id); },
            [this](const QString &text) { appendLog(text); },
            // 模组预设那一趟在后台往 `mods/` 里写文件：期间这一份不能启动、不能删
            // （和「复制实例」「还原」同一个集合，见 installingVersionIds()）。
            [this](const QString &id, bool busy) {
                if (busy) {
                    markFileOperationBusy(id);
                } else {
                    releaseFileOperationBusy(id);
                }
            },
            // ★ 2026-10-19：应用预设**也要先问一句**"这一份是不是正在别的作业手里"。
            //   ★ 这里问的是**搬文件那一份**（`m_fileOperationInstanceIds`），不是
            //   `installingVersionIds()` 的合集 —— 后者在「装完实例、接着跑安装时选的
            //   预设」那一趟里**故意**还压着 `m_installingInstanceId`（见
            //   `onVersionDownloadFinished()` 开头那段），问合集的话，安装向导最后
            //   那一步会被自己挡掉。
            [this](const QString &id) {
                // 导入整合包那一路也会把 overrides 整棵解压进实例根目录（`m_importingInstanceId`），
                // 和预设写的是同一片地方。它不在搬文件那个计数集合里，单独并进来。
                return m_fileOperationInstanceIds.contains(id) || id == m_importingInstanceId;
            },
            // ★ 2026-10-19：`applyPlan()` 专用、宽的那一份 —— 正被安装 / 补全文件的
            //   实例也不能这时候落预设。它只从预设页那一颗「应用」进来，不存在
            //   "安装向导收尾自己挡自己"的问题（那一步走的是下面窄的那道闸）。
            [this](const QString &id) {
                return installingVersionIds().contains(id);
            },
            // 收尾之后重灌实例清单：这一趟期间别的刷新（下载收尾、改设置）会把这一份
            // 按 `installingVersionIds()` 滤掉，不重刷它就一直不在列表里。
            [this]() { refreshInstalledVersions(); }},
        this);
    // 模组兼容检查里 CurseForge 那一侧要用同一个 key（设置里填过才算可用）。
    m_presetController->setCurseForgeApiKey(m_settings->curseForgeApiKey());
    m_launcherPreferences = new LauncherPreferencesController(
        m_ui, m_settings, m_javaSettings, m_versionManager, m_resourceStore,
        LauncherPreferencesController::Host{
            [this](const QString &text) { appendLog(text); },
            [this] { refreshInstalledVersions(); }, [this] { refreshAvailableVersions(); }},
        this);
    m_crashReports = new CrashReportController(
        m_activityCenter, m_ui->overlay(), this,
        CrashReportController::Host{[this](const QString &text) { appendLog(text); }}, this);
    // 工具箱那五件小工具（2026-10-16）：页面、算术、线程全在那个 controller 里，这儿只有
    // "建它"和"把底栏面板上那五颗按钮接到它身上"两句话。
    m_toolboxController = new ToolboxController(
        m_ui->overlay(), this,
        ToolboxController::Host{
            [this](const QString &text) { appendLog(text); },
            [this](std::function<void()> open) { runWhenOverlayFree(open); }, m_activityCenter},
        this);
    m_ui->setToolboxToolHandler(
        [this](const QString &key) { m_toolboxController->open(key); });
    m_integrityController = new IntegrityController(
        m_versionManager, m_activityCenter, m_ui->overlay(), this,
        IntegrityController::Host{
            [this](const QString &id) { return instanceDisplayName(id); },
            [this](const QString &text) { appendLog(text); },
            [this](std::function<void()> open) { runWhenOverlayFree(open); },
            [this](const QString &id) { repairInstanceFiles(id); }},
        this);
    m_backupController = new BackupController(
        m_versionManager, m_activityCenter, m_ui->overlay(),
        BackupController::Host{
            [this](const QString &id) { return instanceDisplayName(id); },
            [this](const QString &path) { return instanceIdOfPath(path); },
            [this](const QString &id) { return m_launcherManager->isVersionRunning(id); },
            [this](const QString &id) { return installingVersionIds().contains(id); },
            [this](const QString &text) { appendLog(text); },
            [this](InstanceSettingsDialog *dialog, const QString &worldDir) {
                showWorldSettingsPage(dialog, worldDir);
            },
            [this](const QString &serverId) {
                if (m_serverSettingsPage != nullptr && m_serverSettingsId == serverId) {
                    refreshServerSettingsPage();
                }
            },
            // 还原一份实例期间它算「正在搬文件」：不能启动、不能删（见 restoreInstance()）。
            [this](const QString &id, bool busy) {
                if (busy) {
                    markFileOperationBusy(id);
                } else {
                    releaseFileOperationBusy(id);
                }
            },
            [this] { refreshInstalledVersions(); }},
        this);
    m_serverPingController = new ServerPingController(
        ServerPingController::Host{
            [this](const QString &id) { return serverListOf(id); }}, this);
    // 控制器迁移总览（2026-10-15）：服务端控制台、实例文件图标反查、启动历史、游戏会话、
    // 任务详情和实例设置写侧已经各自持有页面/状态/业务判断。MainWindow 只保留跨域协调所需的
    // Host 回调和兼容入口；具体所有权与生命周期约束写在各 Controller 的文件头，不在这里重复。
    m_serverConsole = new ServerConsoleController(
        m_serverWorkspace, m_ui, this,
        ServerConsoleController::Host{
            [this](const QString &text) { appendLog(text); },
            [this](const QString &id) { stopServer(id); },
            [this](const QString &id) { forceKillServer(id); }},
        this);
    m_folderController = new FolderController(
        m_folderManager, m_launcherManager, m_ui, this,
        FolderController::Host{
            [this](const QString &path) { return folderTitle(path); },
            [this] { refreshFolderList(); }, [this] { refreshInstalledVersions(); },
            [this](const QString &text) { appendLog(text); }},
        this);
    m_modpackExportController = new ModpackExportController(
        m_versionManager, m_settings, m_ui->overlay(),
        ModpackExportController::Host{
            [this](const QString &id) { return installingVersionIds().contains(id); },
            [this](const QString &id) { return instanceDisplayName(id); },
            [this](const QString &text) { appendLog(text); },
            [this](const QString &status) {
                m_syncWaitStatus = status;
                updateBottomBar();
            },
            // 任务中心那一格（coding plan §9.9）。★ 收尾**不走 `MainWindow::finishTask()`**：
            // 那一位会顺手把"这一批下载还能不能重试"塞进失败的那一项，而导出根本没有那个
            // 失败清单 —— 挂上去会露一颗按下去重做别人下载的「重试」。
            [this](Tasks::TaskKind kind, const QString &title, const QString &targetId,
                   const QList<Tasks::TaskStep> &steps) {
                return m_activityCenter->begin(kind, title, targetId, steps);
            },
            [this](Tasks::TaskId id, const QString &detail) {
                m_activityCenter->tasks()->setDetail(id, detail);
            },
            [this](Tasks::TaskId id, qint64 done, qint64 total) {
                m_activityCenter->tasks()->setProgress(id, done, total);
            },
            [this](Tasks::TaskId id, int index, Tasks::TaskStepState state) {
                m_activityCenter->tasks()->setStepState(id, index, state);
            },
            [this](Tasks::TaskId id, bool success, const QString &errorSummary) {
                m_activityCenter->finish(id, success, errorSummary, false);
            },
            [this](Tasks::TaskId id) { m_activityCenter->tasks()->cancel(id); }},
        this);
    m_instanceFileController = new InstanceFileController(
        m_ui->overlay(), InstanceFileController::Host{
            [this](const QString &text) { appendLog(text); }}, this);
    m_instanceTransferController = new InstanceTransferController(
        m_versionManager, m_launcherManager, m_ui->overlay(),
        InstanceTransferController::Host{
            [this](const QString &id) { return installingVersionIds().contains(id); },
            [this](const QString &id) { return instanceDisplayName(id); },
            [this](const QString &id, const QString &key) {
                return instanceFileDirs(id).value(key);
            },
            [this] { refreshInstalledVersions(); },
            [this](const QString &text) { appendLog(text); },
            [this](const QString &id, bool busy) {
                if (busy) {
                    markFileOperationBusy(id);
                } else {
                    releaseFileOperationBusy(id);
                }
            },
            // 任务中心那一格：「复制实例」后台那一趟（coding plan §9.9）。收尾同样**不走**
            // `MainWindow::finishTask()` —— 复制没有"可重试的下载批次"。
            [this](Tasks::TaskKind kind, const QString &title, const QString &targetId,
                   const QList<Tasks::TaskStep> &steps) {
                return m_activityCenter->begin(kind, title, targetId, steps);
            },
            [this](Tasks::TaskId id, const QString &detail) {
                m_activityCenter->tasks()->setDetail(id, detail);
            },
            [this](Tasks::TaskId id, qint64 done, qint64 total) {
                m_activityCenter->tasks()->setProgress(id, done, total);
            },
            [this](Tasks::TaskId id, int index, Tasks::TaskStepState state) {
                m_activityCenter->tasks()->setStepState(id, index, state);
            },
            [this](Tasks::TaskId id, bool success, const QString &errorSummary) {
                m_activityCenter->finish(id, success, errorSummary, false);
            }},
        this);
    m_instanceDialogController = new InstanceDialogController(this);
    m_instanceEditorController = new InstanceEditorController(this);
    m_importController = new ImportController(this);
    // GameSessionController 要写启动历史，所以必须等 m_launchHistory 就位后再创建。
    m_launchController = new LaunchController(this);
    m_connectionsController = new ConnectionsController(this);
    m_instanceIconController = new InstanceIconController(
        m_settings, m_resourceStore,
        InstanceIconController::Host{
            [this](const QString &versionId) { return instanceFileDirs(versionId); },
            [this](const QString &versionId) { return instanceDisplayName(versionId); },
            [this](const QString &text) { appendLog(text); },
            [this](const QString &iconKey, const ResourceCatalog::Hit &hit) {
                m_resourceHits.insert(iconKey, hit);
            }},
        this);
    // ---------- 「上一次启动」那本账（coding plan §5.1） ----------
    // ★ 它在这儿建（不是初始化列表里），和上面那份任务账本同一个理由：它要读盘，得等前面
    //   几样都就位了；而且必须**赶在第一次 `updateHomeRecent()` 之前** —— 那之后凡是
    //   "实例列表变了"的路（重扫实例树那一条在构造里就会走）都会去问它要账本。
    m_launchHistory = new LaunchHistoryController(this);
    // 账本一变就重画主页那两排快速入口。发的位置那一侧管着（读回来 / 记了一条 / 真改到了
    // 一条的结局那三处；结局没对上任何一条的空转不发），这边只管接。
    connect(m_launchHistory, &LaunchHistoryController::changed, this,
            &MainWindow::updateHomeRecent);
    connect(m_launchHistory, &LaunchHistoryController::logMessage, this,
            [this](const QString &text) { appendLog(text); });
    m_gameSessionController = new GameSessionController(
        m_launcherManager, m_settings, m_versionManager, m_launchHistory, m_ui,
        GameSessionController::Host{
            [this](const QString &text) { appendLog(text); },
            [this](Tasks::TaskId id) {
                const auto steps = m_activityCenter->tasks()->snapshot(id).steps;
                for (int index = 0; index < steps.size(); ++index)
                    if (steps.at(index).state == Tasks::TaskStepState::Running || index == steps.size() - 1)
                        m_activityCenter->tasks()->setStepState(id, index, Tasks::TaskStepState::Done);
            },
            // §9.8 第三条：进程起来的这一刻把账本上那件「启动 X」和会话号对上。
            [this](Tasks::TaskId id, int sessionId) {
                m_activityCenter->bindSession(id, sessionId);
            },
            [this](Tasks::TaskId id, bool ok, const QString &note) { finishTask(id, ok, note); },
            [this] { updateBottomBar(); },
            [this] { updateInstanceTreeRunState(); },
            [this](const QString &versionId) { return instanceDisplayName(versionId); },
            [this](int sessionId, int exitCode, bool crashed) {
                reportGameCrash(sessionId, exitCode, crashed);
            },
            [this](int sessionId) { exportGameLog(sessionId); },
            [this](int sessionId) { exportGameErrorReport(sessionId); },
            [this] { return frameGeometry(); },
            [this] {
                showNormal();
                raise();
                activateWindow();
            }},
        this);
    m_taskDetail = new TaskDetailController(
        m_activityCenter, m_crashReports, m_versionManager, m_gameSessionController,
        m_ui->overlay(), this,
        TaskDetailController::Host{
            [this](const QString &text) { appendLog(text); },
            [this](const Tasks::TaskSnapshot &task) { return cancelWouldWork(task); },
            [this](const QString &key) { cancelTaskFromDetail(key); },
            [this](const QString &key) { retryTaskFromDetail(key); }},
        this);
    connect(m_serverWorkspace, &ServerWorkspaceController::message, this,
            [this](const QString &text) { appendLog(text); });
    connect(m_serverWorkspace, &ServerWorkspaceController::statusChanged, this,
            &MainWindow::onServerStatusChanged);
    connect(m_serverWorkspace, &ServerWorkspaceController::lineAppended, this,
            &MainWindow::onServerLineAppended);
    connect(m_serverWorkspace, &ServerWorkspaceController::forceKillAvailable, this,
            &MainWindow::onServerForceKillAvailable);
    connect(m_serverWorkspace, &ServerWorkspaceController::exited, this,
            &MainWindow::onServerExited);
    // 收起态那一格的重画时机里有一档是"刚完成那一项露到头了"（§3.1「完成任务短暂显示后
    // 收进历史」）—— 那个到期**没有任何信号会来**，只能自己起一个单发定时器。
    m_taskSummaryTimer = new QTimer(this);
    m_taskSummaryTimer->setSingleShot(true);
    connect(m_taskSummaryTimer, &QTimer::timeout, this, &MainWindow::updateBottomBarTaskSummary);
    // 账本一变就重画底栏那**两**格（中间那格 + 右下那格）。**不去分辨"变的是哪一项"**：
    // 收起态显示谁是把整张账本放在一起算出来的（`Tasks::collapsedSummary()`），只重画
    // "变了的那一行"在这里没有对应的东西可画（那一格只有一个整体）。
    //
    // ★ 2026-10-15：右边那格也得跟着来一发 —— 红那一档的判据就是"账上还有没有没处理的
    //   异常退出"（`CrashReportController::pendingRecordIds()`），而这句话两头都要能变：
    //   出事的**那一刻**要变红（那一发是 `taskFinished` ⇒ 走到这儿），用户把任务中心里
    //   那一项**清掉**之后要变回灰/绿（那一发是 `historyChanged`，也走到这儿）。
    //   漏掉这一句的症状是：红要么不出现，要么清完了还挂在那儿不走。
    //
    // NOTE: 整条底栏（`updateBottomBar()`）**不许**接在这上面：账本每来一帧进度都会发这个
    //       信号（下载那份是 200 ms 一帧），接上去等于每帧连头像和账号名一起重刷。
    connect(m_activityCenter, &ActivityCenterController::activityChanged, this, [this]() {
        updateBottomBarTaskSummary();
        updateBottomBarRunState();
    });
    // 「已完成」那一栏变了（收尾 / 清除 / 读盘）：面板要是开着，跟着重画一遍。
    connect(m_activityCenter, &ActivityCenterController::historyChanged, this, [this]() {
        m_ui->refreshTaskCenterPanel();
    });
    // ---------- 「实例设置」写侧那一套（coding plan §4，2026-10-15） ----------
    // 它自己一块界面都不碰（**一个字节的中文都没有**），要问、要说的那几件事全走下面这六个
    // 回调：这几件恰恰是"只有主窗口做得成"的事（谁在跑要问 LauncherManager、那句话怎么说要
    // 挑中文、重扫实例树是主窗口的活）。
    //
    // ★ 为什么这几个回调是**同步**的：`notice` 是在 controller 那一句代码的位置上当场执行的，
    //   顺序变了用户看到的东西就变（提示卡片是一张盖一张的）。所以这里一行都不许改成排队/异步。
    m_instanceSettings = new InstanceSettingsController(
        m_versionManager, m_groupStore, m_settings,
        InstanceSettingsController::Host{
            // 谁在跑。**必须问 LauncherManager**，不能自己看那个"正在启动"的标志位 ——
            // 一个会话起来之后标志位就落了，而游戏还在跑。
            [this](const QString &versionId) {
                return m_launcherManager->isVersionRunning(versionId);
            },
            // 谁在体检。那一位是主窗口手上的（`m_integrityScanningVersion`），controller
            // 那一层够不着也不该够着。
            [this](const QString &versionId) { return integrityScanningVersion(versionId); },
            [this](const InstanceSettingsController::Notice &notice) {
                showInstanceSettingsNotice(notice);
            },
            [this](const QString &text) { appendLog(text); },
            // 名字/图标都变了 ⇒ 实例树得重扫（那棵树上那一格画的就是这两个）。
            [this]() { refreshInstalledVersions(); },
            // 改完名要显式选中新那一个：刷新时下拉框那边是"保住原来选中的 id"，而那个 id
            // 已经不在了，于是它会停在空选。
            [this](const QString &newId) {
                QComboBox *combo = m_ui->getInstalledVersionCombo();
                const int index = combo->findData(newId);
                if (index >= 0) {
                    combo->setCurrentIndex(index);  // 这条连接是活的，按钮文字会跟着刷新
                }
                updateStartButtonText();
            },
            // 分组/置顶那三条规则走的是**现有那两个入口**（不是在这儿另写一遍
            // `wtfml.folders` 的读写）：它们各自会发 groupsChanged 把树重灌，而且
            // "没有置顶组时什么都不做""认不出来就落未分组"这些判断都已经在那边了。
            [this](const QString &versionId, const QString &groupId) {
                setInstanceHomeGroup(versionId, groupId);
            },
            [this](const QString &versionId) { pinInstance(versionId); },
            [this](const QString &versionId) { unpinInstance(versionId); }},
        this);
    // 任务中心那块面板要画什么、以及「清除已完成」被点了做什么。**每次点开/重画现问一遍**
    // （和底栏那两块面板一个路数：面板自己不会跟着账本刷新，缓存上一次那几行迟早说假话）。
    //
    // NOTE: provider 那一头有一处**副作用**（"列出来"就是"看过了"，见 taskCenterItems()），
    //       所以它不能是 const 的、更不能被别人当"读一下"随便调。
    m_ui->setTaskCenterContent([this]() { return taskCenterItems(); },
                               [this]() { m_activityCenter->clearFinished(); });
    // 点任务中心里的**一行** = 开任务详情页（coding plan §3.2「点击任务进入内嵌 TaskDetailPage」）。
    m_ui->setTaskCenterRowActivatedHandler([this](const QString &key) { openTaskDetailPage(key); });
    // 行尾那颗「取消」：走的是和详情页里那一颗**完全同一条路**（`onTaskDetailAction`）——
    // 两处各写一遍的话，"取消"的语义迟早会分家（一处删文件、一处不删）。
    m_ui->setTaskCenterRowActionHandler(
        [this](const QString &key, TaskCenter::RowAction action) {
            switch (action) {
            case TaskCenter::RowAction::MoveUp:
            case TaskCenter::RowAction::MoveDown:
                m_activityCenter->moveQueued(key.toULongLong(), action == TaskCenter::RowAction::MoveUp ? -1 : 1);
                break;
            case TaskCenter::RowAction::Viewed:
                m_activityCenter->markViewed(key);
                break;
            case TaskCenter::RowAction::Cancel:
                onTaskDetailAction(key, TaskDetailPage::Action::Cancel);
                break;
            case TaskCenter::RowAction::Retry:
                onTaskDetailAction(key, TaskDetailPage::Action::Retry);
                break;
            case TaskCenter::RowAction::OpenTarget:
                onTaskDetailAction(key, TaskDetailPage::Action::OpenTarget);
                break;
            case TaskCenter::RowAction::OpenLog:
                onTaskDetailAction(key, TaskDetailPage::Action::OpenLog);
                break;
            case TaskCenter::RowAction::ExportReport:
                onTaskDetailAction(key, TaskDetailPage::Action::ExportReport);
                break;
            case TaskCenter::RowAction::OpenCrashDir:
                onTaskDetailAction(key, TaskDetailPage::Action::OpenCrashDir);
                break;
            }
        });
    m_ui->setHomeQuickActionHandler(
        [this](const QString &key, MainWindowUI::HomeQuickAction action) {
            if (action == MainWindowUI::HomeQuickAction::ChooseInstance) {
                openHomeInstancePicker();
                return;
            }
            if (key.isEmpty() || m_versionManager == nullptr
                || !m_versionManager->installedVersions().contains(key)) {
                updateHomeRecent();
                return;
            }
            if (action == MainWindowUI::HomeQuickAction::Edit) {
                openInstanceSettings(key);
                return;
            }
            if (action == MainWindowUI::HomeQuickAction::OpenFolder) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::versionDir(key)));
                return;
            }
            QComboBox *combo = m_ui->getInstalledVersionCombo();
            const int row = combo == nullptr ? -1 : combo->findData(key);
            if (row >= 0) {
                combo->setCurrentIndex(row);
                onStartButtonClicked();
            }
        });
    connect(m_ui->getHomeInstanceSelectButton(), &QPushButton::clicked, this,
            &MainWindow::openHomeInstancePicker);
    setupConnections();
    // 下载页那五页的筛子先灌上（来源 / 类型 / 加载器那三项这一刻就定得下来；「版本」那一栏
    // 等在线清单一到才有的灌，见 onVersionsFetched）。界面那边只搭空框 —— ui/ 不 include
    // src/，"每一项是什么"只能由这一层灌进去。
    fillDownloadFilters();
    // 设置页先按盘上的值摆好（不扫 Java，那一段等第一次切到设置页，见
    // refreshJavaList()）。必须在下面 fetchAvailableVersions() **之前**：
    // 清单请求要不要走镜像就是在这一步交给 VersionManager 的。
    applySettingsToUi();
    // 上次用的是个**离线账号**：把名字接回 AuthManager。**必须在下面 updateBottomBar()
    // 之前** —— AuthManager 构造函数只读活动槽，而离线账号在盘上**没有凭据**，它读出来的
    // 是占位的 "Player"：不接回来的话底栏和 --username 里是个假名字。
    //
    // NOTE: 2026-09-24 之前这里还要顺手勾一下主页那个「离线模式」勾选框（不勾的话启动那条
    //       路会去要凭据，而盘上一份都没有 —— 用户拿到的是"请先登录"，一个死胡同）。
    //       那个勾选框去掉了，现在"这一趟走离线"这件事**只由"当前账号是离线账号"决定**
    //       （见 offlineIdentity()），于是这一句就够了。
    {
        const QString active = AccountStore::activeUuid();
        if (AccountStore::isOffline(active)) {
            m_authManager->setOfflinePlayer(AccountStore::nameOf(active));
        }
    }
    // 「游戏文件夹」：先把当前那一份交给 AppPaths —— 从这一句起，扫版本、下资源、
    // 找 Java 用的都是它。**必须在 refreshInstalledVersions() 之前**，那个函数扫的
    // 就是这个根目录。左边那张列表此刻还没有条目可选，所以要挡着信号灌。
    applyCurrentFolderToPaths();
    refreshFolderList();
    // 上次那本任务账读回来（§3.2：只存摘要与错误；**没跑完的标成"已中断"，不假装继续**）。
    //
    // 账本是启动器级的固定文件，不随当前游戏文件夹切换。这样正在下载/启动时切换目录，
    // 内存里的任务不会在下一次落盘时被写进另一份 .minecraft。
    m_activityCenter->loadHistory();
    // 「忽略这一版 / 固定这一版」那一本账（§7.1）：**启动时读一次**。
    //
    // ★ 读不回来（文件被改坏了 / 权限不对）**不拦任何事**：那是"以后别再提示我"这一档的
    //   记忆，读丢了的最坏结果是那几条又提示了一次 —— 比"启动时弹一个框说这本账读不动、
    //   然后用户什么也做不了"轻得多。所以只写日志。
    {
        QString policyProblem;
        m_updatePolicy = UpdatePolicy::load(UpdatePolicy::storePath(), &policyProblem);
        if (!policyProblem.isEmpty()) {
            appendLog(tr("「更新中心」那份记录读不回来：%1").arg(policyProblem));
        }
    }
    // 本地已安装的先填上（同步扫盘）；在线清单是异步拉的，回来之后由
    // onVersionsFetched() 填"可安装"下拉框 —— 在那之前它显示占位文案。
    refreshInstalledVersions();
    // 顺序要紧：必须在 refreshInstalledVersions() **之后**（下拉框里得先有条目可选），
    // 并且在 fetchAvailableVersions() 之前（免得用户在等待网络期间就手选了一个，
    // 那样启动时反而会把用户的选择抢掉）。也**只在启动时**跑这一次 ——
    // 不能塞进 refreshInstalledVersions()，那个函数每次清单到达、每次下载完成都会跑，
    // 塞进去就破坏了它"只保住、不新增"的语义。
    restoreLastLaunchedVersion();
    // 启动历史账本（§5.1）：读回来并刷新主页“最近游玩”排（读完发一声 `changed()`，接在
    // `updateHomeRecent()` 上；那一发是**同步**的，所以下面几行看到的已经是刷新过的界面）。
    //
    // ★ 位置有一处讲究：必须在 `refreshInstalledVersions()` **之后** —— 那张卡上「继续游玩」
    //   还是「启动实例」要看"那一份实例还在不在已安装列表里"，列表空着的时候问它只会得到
    //   "不在"，卡片就会显示成降级的那一档（明明能玩）。
    m_launchHistory->load();
    // 底栏那三格的初始值：登录名可能已经从存下来的令牌里恢复了，进度两格先藏起来，
    // 运行状态也要等第一次真的启动才有内容。
    updateBottomBar();
    // 主页右栏那个小人 + 底栏头像。放在 updateBottomBar() 之后是因为它要问
    // AuthManager"登过没有"，而那份状态是在它自己的构造函数里从盘上读回来的。
    // **必须在 fetchAvailableVersions() 之前**：那一下要发网络请求，外观这条线不该
    // 排在它后面（离线那条路更是完全不让它走）。
    startPlayerSkinLoad();
    // 底栏那两块上拉面板里画什么、行尾的按钮点了做什么（面板本身在 MainWindowUI 里，
    // 它不查数据）。**每次点开现算一遍**：面板弹出来那一刻的一张快照，它自己不会跟着
    // 刷新（实例随时会结束、账号随时会多一个），所以这里不许缓存上一次那几行。
    //
    // NOTE: 行里带着的 key 是"**这一行是谁**"（左面板 = 会话号、右面板 = 账号 uuid），不是
    //       行号 —— 面板弹出来到某个按钮被点之间隔着用户看的那两眼，那中间列表是会变的
    //       （见 MainWindowUI::PanelRow::key 那段）。
    m_ui->setBottomBarPanelContent(
        [this](bool account) -> QList<MainWindowUI::PanelRow> {
            if (account) {
                // 「最后登录的三个账户」（用户 2026-09-24 原话）—— 3 这个数只在这一处。
                const QList<SavedAccount> saved = AccountStore::accounts();
                // 每个人的脸**一次算完**再交给 recentAccountPanelRows()：那个函数是纯函数
                // （它不认识缓存目录、也不该认识）。这里给**全部**账号各算一张、不是只算
                // 要显示的那三张 —— 挑哪三张是那个函数的事（它按 saved_at 倒序取前三个），
                // 在调它之前再排一遍序就是两处各算一遍。
                QHash<QString, QPixmap> faces;
                faces.reserve(saved.size());
                for (const SavedAccount &one : saved) {
                    faces.insert(one.uuid, accountFacePixmap(one));
                }
                QList<MainWindowUI::PanelRow> rows = recentAccountPanelRows(saved, 3, faces);
                MainWindowUI::PanelRow manage;
                manage.key = kAccountPanelManageKey;
                manage.title = MainWindowUI::accountSkinButtonText();
                manage.subtitle = tr("管理账号、皮肤与披风");
                MainWindowUI::PanelAction open;
                open.text = tr("打开");
                open.tooltip = tr("打开账号与外观");
                manage.actions.append(open);
                rows.append(manage);
                return rows;
            }
            return runningGamePanelRows(
                m_launcherManager->runningGames(), m_versionManager,
                // 图标那一档颜色 = 底栏那几行字的墨色（画的图标不带自己的颜色）。
                m_ui->getBottomBar()->palette().color(QPalette::WindowText),
                QDateTime::currentMSecsSinceEpoch());
        },
        [this](bool account, const QString &key, int action) {
            onBottomBarPanelAction(account, key, action);
        });

    // 主题一换，底栏要按当前状态**重画一遍**：那颗三态方块和头像都是**位图**（颜色已经画进
    // 像素里了），底栏自己那几位（模式文字、次要文字的颜色）也是建的时候从调色板取的。
    // `updateBottomBar()` 正是"底栏全部内容按当前状态重画"那一个函数，它每次状态变化都会
    // 被调到、本身是幂等的 —— 挂它就够了。
    //
    // NOTE: 挂的是**底栏**那个控件（常驻、和窗口同寿）⇒ 这一条不会比窗口活得长。
    Theme::follow(m_ui->getBottomBar(), [this]() { updateBottomBar(); });

    m_versionManager->fetchAvailableVersions();
}

MainWindow::~MainWindow() {
    // 日志窗**没有父窗口**（理由见 GameSessionController::ensureGameLogWindow() 那段），
    // 所以 Qt 不会顺手把它们带走 —— 这里自己收，否则关掉启动器之后屏幕上会留着几扇无主的
    // 日志窗。那一份表连同收法都在那一位控制器里（它建的那些窗，它自己收）。
    m_gameSessionController->closeAllGameLogWindows();
    // 体检那一趟可能还在跑（几千次 stat，慢盘上要好几秒）。**先把收手旗子举起来再等它**
    // —— worker 每比几条看一眼那面旗，看到就退（见 submitIntegrityCheck 里那段）。
    //
    // NOTE: 不等的话是**崩**，不是"悄悄漏一个线程"：m_integrityThread 的父对象是 this（下面
    //       这个对象要散了），而 Qt 对"QThread 还在跑、父对象却析构了"是直接 abort
    //       （`QThread: Destroyed while thread is still running`）。
    m_integrityController->shutdown();
    // 工具箱里"算哈希"那一条也是 `QThread::create()` 出来的（父对象是 controller 自己，
    // 同一个坑，同一个收法）。
    m_toolboxController->shutdown();
    // 备份 / 还原那一趟同理（§4.4）。**这一趟没法中途收手**（见 .hpp 里 m_backupAbort 那段
    // —— 停下来只会留一个半截的包或者删了一半的目录），所以这里只能**等它跑完**：
    // 一个几百 MB 的实例快照要几十秒，关窗会晚这么多才真的关上。
    //
    // NOTE: 这一位真正的用处是"跑完之后别把结果排回一个正在拆的窗口" —— 引擎不读它。
    // NOTE: 不等的话同上：`m_backupThread` 的父对象是 this，Qt 直接 abort。
    m_backupController->shutdown();
    // 剩下两趟后台活（coding plan §3.1②）。它们和上面那两趟是**同一个坑**，只是一直没人接：
    // `QThread::create()` 出来的这几个对象，父对象都是 this（下面这个对象要散了），而 Qt
    // 对"线程还在跑、父对象却析构了"是直接 abort。这两趟又都是**网络活** ——
    //   · 图标反查：打开实例设置（或文件管理那几页）就起，几个 sha1 + 一发反查；
    //   · 检查更新：点「检查更新」就起。
    // 关窗那一刻正卡在等响应上是最平常的一件事（`HttpClient` 那一条要等 15 秒），于是在
    // 那十几秒里关启动器 = 崩一下。从前析构里只等了体检和备份那两趟 ⇒ 这两趟一直在裸奔。
    //
    // ★ 这一段的规矩**照着这张单子来**：**这一层（或者这一层的子控制器）**起了的线程，
    //   析构里必须**一个一个等过来**。眼下是四条（体检 / 备份 / 图标反查 / 检查更新）——
    //   再加一趟就得多一条，加的时候连同这句一起改。四条里三条已经把"等"收进了各自的
    //   控制器（`shutdown()` / `waitForLookup()`），检查更新那一趟还在这段里直接等。
    //   （`ServerPing::Pinger` 那八条不在这张单子上：它们的父对象是 Pinger，
    //   而 `~Pinger()` 自己就 abort+wait，见那边。）
    //
    // NOTE: 这两趟**没有收手旗子**（`InstanceIconLookup::lookup()` 和检查更新那一趟里头没有
    //       "看着旗子退出来"的口子），所以这里只能**等它跑完** —— 卡在超时上的话，关窗就会
    //       晚那十几秒才真的关上。那比崩一下强（同一个取舍，见上面备份那一段）。
    // NOTE: 图标那一趟 2026-10-15 起它的线程父对象是**那个控制器**（不是这一层）——
    //       和"父对象是 this"是同一个坑（控制器也是 this 的子对象，一样要散），所以这一
    //       条照样得留：等的活儿在控制器里，等这一下得在这一层发。
    m_instanceIconController->waitForLookup();
    if (m_updateCheck.thread != nullptr) {
        m_updateCheck.thread->wait();
    }
    delete m_ui;
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    // 外框那两条消息（WM_NCCALCSIZE / WM_NCHITTEST）转给 ui/WindowChrome —— 那一份的
    // 文件头把"为什么走这条路、为什么不走 Qt 那条 ExpandedClientAreaHint"写全了。
    //
    // 时机上多说一句：Qt 是在**处理这两条消息之前**把消息交到这里来的
    // （qwindowscontext.cpp 那句 `filterNativeEvent(platformWindow->window(), &msg, result)`
    // 排在它自己那个大 switch 前面），所以这里回的"这一条到此为止"是算数的 —— Qt 那套
    // 非客户区处理和我们自己的外框不会打架。
    WindowChrome *chrome = m_ui != nullptr ? m_ui->chrome() : nullptr;
    if (chrome != nullptr && chrome->handleNativeEvent(message, result)) {
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    const QList<LauncherManager::GameSession> games = m_launcherManager->runningGames();
    // 还在跑的那些服务端（键 = serverId，**按名字排序** —— 提示里报出来的名字每次都得是
    // 同一个顺序，不然同一件事两句话看着像两件事）。
    const QStringList serverIds = m_serverWorkspace->runningServerIds();

    if (games.isEmpty() && serverIds.isEmpty()) {
        QMainWindow::closeEvent(event);
        return;
    }

    if (!games.isEmpty()) {
        // 游戏还在跑。以前这里什么都不做 —— 而"什么都不做"的实际后果是**把游戏强杀**：
        // MainWindow 是 main() 里的栈对象，窗口关掉 → exec() 返回 → MainWindow 析构 →
        // LauncherManager（它的子对象）析构 → m_gameProcess（它的子对象）析构，而
        // QProcess 的析构是这么写的（Qt 6.10，qprocess.cpp）：
        //
        //     if (d->processState != NotRunning) { qWarning() << ...; kill(); waitForFinished(); }
        //
        // 也就是说，用户只是想关掉启动器，游戏却收到一个 TerminateProcess（Windows 上
        // kill() 就是它）—— 游戏来不及存档、没有"保存并退出"。PCL 那种启动器关掉之后游戏
        // 照跑，java.exe 本来也活得过启动器，这一步只是不让上面那条析构链碰到它。
        //
        // 所以这里问一句，而且**默认那一边是"别动游戏"**（回车 = 取消，不关窗口）：用户要
        // 是真想停游戏，在游戏里按"退出游戏"那条路才是干净的。
        // 同时开着好几份时，真名报出来（"还有 3 个游戏在跑：A、B、C"）比"游戏还在运行"
        // 有用得多 —— 用户要判断的正是"我还开着哪些"。
        QStringList names;
        names.reserve(games.size());
        for (const LauncherManager::GameSession &game : games) {
            names.append(instanceDisplayName(game.versionId));
        }
        const QString text =
            games.size() == 1
                ? tr("游戏「%1」还在运行。\n\n关掉启动器**不会**关掉游戏 —— 它会继续跑，"
                     "存档、联机都照常。\n要现在关掉启动器吗？")
                      .arg(names.first())
                : tr("还有 %1 个游戏在运行：%2。\n\n关掉启动器**不会**关掉游戏 —— "
                     "它们会继续跑，存档、联机都照常。\n要现在关掉启动器吗？")
                      .arg(games.size())
                      .arg(names.join(QStringLiteral("、")));
        // 按钮文字自己写：启动器没装 Qt 的中文翻译，标准按钮在界面上是英文的 Yes/No。
        // NOTE: 关掉窗口这一发**本身**在跑 closeEvent，不能开独立窗（用户给的方向是最终
        //       只有内容区那一块覆盖层）—— 内嵌提示在 closeEvent 里同样作数：窗口这时候
        //       还没关掉，`execPrompt` 那个嵌套事件循环照转，"事件循环里再开事件循环"
        //       这件事和原来 `box.exec()` 一模一样。
        //       默认那一边照旧是**取消**（下标 1），回车 = 别动游戏。
        //       第三格是 destructiveIndex —— 两个都不是危险动作（关的是启动器，游戏照跑），
        //       所以给 -1。
        const int choice = InlineMessage::ask(
            m_ui->overlay(), MessageKind::Question, tr("游戏正在运行"), text,
            QStringList{tr("关掉启动器"), tr("取消")}, 1, -1);
        if (choice != 0) {
            event->ignore();
            return;
        }
    }

    if (!serverIds.isEmpty()) {
        // 服务端和游戏**不是一回事**，所以单独问一次（用户 2026-10-13 定的：问一句再决定
        // ——「一起停掉」/「留着它们继续跑」）。
        //
        // 为什么值得单独问：服务端不是"关掉启动器它也一样活着"的那种进程 —— 它是我们的子
        // 进程，启动器一退，那条析构链就会把它 TerminateProcess 掉，**正在写的世界文件
        // 就烂在那儿**。而"停干净"和"留着它继续跑"都是合理的诉求：前者是"我这台机器上
        // 不想再开服了"，后者是"我还想让它继续开着，只是把启动器关了"。
        QStringList names;
        names.reserve(serverIds.size());
        for (const QString &id : serverIds) {
            const ServerRegistry::Entry entry = ServerRegistry::describe(id);
            names.append(entry.name.isEmpty() ? id : entry.name);
        }
        const QString text =
            serverIds.size() == 1
                ? tr("服务端「%1」还在运行。\n\n关掉启动器时要把它怎么办？\n"
                     "「一起停掉」会让它**存完盘**再退（可能要等一会儿）；"
                     "「留着它继续跑」的话，启动器关了它照样开着。")
                      .arg(names.first())
                : tr("还有 %1 个服务端在运行：%2。\n\n关掉启动器时要把它们怎么办？\n"
                     "「一起停掉」会让它们**存完盘**再退（可能要等一会儿）；"
                     "「留着它们继续跑」的话，启动器关了它们照样开着。")
                      .arg(serverIds.size())
                      .arg(names.join(QStringLiteral("、")));
        // 默认那一边是**留着它们继续跑**（下标 1）：和游戏那一条同理 —— 不主动动别人的
        // 进程更安全（突然停下来那一发万一赶在写存档的中间，就是丢存档的姿势）。
        const int choice = InlineMessage::ask(
            m_ui->overlay(), MessageKind::Question, tr("服务端正在运行"), text,
            QStringList{tr("一起停掉"), tr("留着它们继续跑")}, 1, -1);
        if (choice == 0) {
            // ★ 停要**等它走完**，而且等的是宽限那两个数算出来的时间（`ServerRun::
            //   gracefulStopMs()` —— 模组越多、存档越大，给的越宽）。这一句是阻塞的：
            //   界面在这几秒里不动。选择让它静态地等，是因为另一种做法（发完 stop 就让
            //   析构去杀）等于是**把这一问白问了** —— 那正是丢存档的那条路。
            for (const QString &id : serverIds) {
                m_serverWorkspace->stopAndWait(id);
            }
            appendLog(tr("已关掉启动器，服务端已经停下来。"));
        } else {
            // 交出去：脱离所有权（`ServerProcess::detach()` 里那段说明），启动器退了它们
            // 照样在系统里跑。
            for (const QString &id : serverIds) {
                m_serverWorkspace->detach(id);
            }
            appendLog(tr("已关掉启动器，服务端继续在跑。"));
        }
    }

    if (!games.isEmpty()) {
        // 顺序要紧：**先**把进程交出去（脱离所有权 + 断信号），再让窗口关掉 —— 关窗口之后
        // 紧接着就是析构链，交出去那一步必须在那之前做完（见 LauncherManager::detachRunningGame）。
        m_launcherManager->detachRunningGame();
        appendLog(tr("已关掉启动器，游戏继续在跑。"));
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::setupConnections() {
    m_connectionsController->setup();
}

void MainWindow::enterSyncWait(const QString &statusText) {
    // 这一段里 UI 线程在等网络或等进程（见 .hpp 里那三种情况），Qt 不重绘、不响应输入，
    // Windows 过几秒还会给标题挂上"(无响应)"——用户看到的就是"点了启动就卡死"。
    // 这里能做的只有"进去之前先把话说出来"：底栏那格写上在等什么，再套一个忙指针。
    m_syncWaitStatus = statusText;
    updateBottomBar();
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    // 不刷这一次，上面那些改动要等窗口重新绘制时才看得见 —— 而那要等到这段路走完。
    // 排掉用户输入是**必须的**：不排的话这次 processEvents() 会把用户在这期间的点击
    // （比如"再点一次启动"）送回来，于是同一段路走两遍，比卡着更糟。
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::leaveSyncWait() {
    m_syncWaitStatus.clear();
    QGuiApplication::restoreOverrideCursor();
    updateBottomBar();
}

void MainWindow::setDownloadUiBusy(bool busy) {
    m_downloadInFlight = busy;
    // ---------- 这几项**故意不灰**（2026-09-23 多账户多实例那批改的） ----------
    // 启动按钮、「已安装版本」下拉框、实例页那张树，以前是跟着 busy 一起灰的。放开它们的
    // 理由是同一条：**"在下载"和"能启动什么"是两件事**，而且现在按版本分开了 ——
    // 正在装的那个从 launchable 里滤掉（见 installingVersionIds()），列表里剩下的
    // 都是文件齐的。所以"界面显示选的 A、实际在下的却是 B"这种误判在做完过滤之后
    // 不可能发生，而"装 1.20.1 的时候开一个早就装好的 1.18.2"是用户明确要的。
    //
    // 真正危险的那一种由 onStartButtonClicked() 里那道**按版本**的闸口兜住。
    //
    // 安装页那一整层（名称框、四个加载器、安装按钮、返回）**照样灰**：在装什么是点击
    // 那一刻定下的，这时候改名字/换加载器并不会把手上这次安装换掉（真正干活的
    // VersionManager 已经拿着那个枚举在跑了），留着能改只会让人以为改生效了。
    m_ui->getInstallButton()->setEnabled(!busy);
    m_ui->getInstallNameEdit()->setEnabled(!busy);
    m_ui->getVersionTree()->setEnabled(!busy);
    // 从安装页上退回去也不给（退回去那棵树也是灰的）：那会让人以为"这次安装取消了"。
    m_ui->getInstallBackButton()->setEnabled(!busy);
    // 「导入整合包…」也一起灰：导入期间再点一次，选文件和问名字那两个框走完，
    // 数据层才会用一句"已有任务正在进行"把它顶回来 —— 白走一趟对话框。
    m_ui->getImportModpackButton()->setEnabled(!busy);
    // 下载页「整合包」那一页上那颗是**同一件事的另一个入口**（同一个槽，见
    // setupConnections 里那条），所以它得跟着一样灰 —— 一个灰着、另一个亮着，用户点的
    // 正好是亮着那颗，走的是同一条被顶回来的路。
    if (QPushButton *downloadImport = m_ui->getDownloadFilterImportModpackButton()) {
        downloadImport->setEnabled(!busy);
    }
    // 实例页左边那列也一起灰：换游戏文件夹 = 换一整套 .minecraft，下载中途换过去
    // 会让这一批文件一半写进 A、一半写进 B。两个按钮的可用性由 updateFolderButtons()
    // 一处说了算（「移除文件夹」还要看选中的是不是自带那份）。
    m_ui->getFolderList()->setEnabled(!busy);
    // 「模组加载器」那棵树一起灰，理由同上。忙完解禁走 updateLoaderRows() 一处重算
    // （那棵树的样子全由它一处决定）；「安装」那个按钮还要看名称合法不合法，走
    // updateInstallNameHint() 一处。
    m_ui->getLoaderTree()->setEnabled(!busy);
    updateLoaderRows();
    updateInstallNameHint();
    // 重灌一遍"能启动的实例"：上面那条过滤（在装的那个先不出现）要立刻生效 ——
    // 置忙那一刻它该从列表里消失，置闲那一刻它该回来（成功的话还在 onVersionDownloadFinished
    // 里被选中，那句也在这个函数之后跑）。
    refreshInstalledVersions();
    updateFolderButtons();

    // ---------- 底栏那一格那个数字：从这里起/到这里止只由账本说了算 ----------
    // 以前这一支直接写进度条和那句"已下载 d / t"（还要顺手把三个数清掉，免得两批之间
    // 串台）。现在那三样都在**任务自己身上**（`Tasks::TaskSnapshot`），这一格只是账本的
    // 一块投影 —— 要清的不是三个成员，而是"把这一批的那件任务收掉"。
    if (busy) {
        beginDownloadTask();
    } else {
        // 这一批到此为止。正常情况下那条终态信号（onVersionDownloadFinished）已经按成败
        // 收过尾了，这一句是**兜底**：数据层要是哪一次没发出终态（被拒绝的批次、中途退出的
        // 那几路），账本上就会留一件"永远在跑"的任务，底栏那一格从此一直显示它。
        //
        // NOTE: 对已经收过尾的那一件，`finishTask()` 什么都不做（终态只落一次，见
        //       TaskManager::finish）—— 所以这一句不会把成功改写成失败、也不会多一颗红点。
        finishTask(m_downloadTaskId, false, tr("这一批下载没有正常结束。"));
        m_downloadTaskId = Tasks::invalidTaskId();
    }
    updateBottomBar();
}

void MainWindow::updateBottomBar() {
    // ---------- 左：头像 + 用户名 ----------
    // 登录过就是档案里的真名（登录那一刻拿到的，或者开窗时从盘上读回来的那份）；
    // 没登录是占位的 "Player" —— 这里不自己造假名字，也不去猜。
    const QString name = m_authManager->playerName();
    m_ui->getAccountLabel()->setText(name);
    // 名字下面那行：这次启动会用哪种身份。**判据只看一件事**（当前账号是不是离线账号），
    // 不自己推"应该用哪个" —— 真正的判据在 onStartButtonClicked() 里，两处一旦各写一套
    // 就会出现"底栏说正版、启动时却说请先登录"。现在两处读的是**同一个函数**
    // （offlineIdentity()），连"各写一套"的可能都没有了。
    // NOTE: loggedIn() 是"盘上有账户"的意思（AuthManager::loggedIn() = uuid 非空），
    //       令牌过期了它照样是 true —— 那没关系：这一行说的是**身份**，令牌过没过期是
    //       启动那一刻刷新才知道的事（那时失败会有弹窗说清原因）。
    const QString accountType =
        offlineIdentity()
            ? MainWindowUI::accountTypeOfflineText()
            : (m_authManager->loggedIn() ? MainWindowUI::accountTypeOnlineText()
                                         : MainWindowUI::accountTypeNotLoggedInText());
    m_ui->getAccountTypeLabel()->setText(accountType);
    // 主页是账号的唯一可见入口；底栏那份标签暂时仍为兼容旧面板保留数据，但不再承担身份展示。
    if (QLabel *homeName = m_ui->getHomeAccountLabel()) {
        homeName->setText(name);
    }
    if (QLabel *homeType = m_ui->getHomeAccountTypeLabel()) {
        homeType->setText(accountType);
    }
    // 头像优先用**真皮肤**抠出来的那一块（m_avatarPixmap，在 applyPlayerSkin() 里算好
    // 存着 —— 这里每次进度刷新都会被调到，不能现算）。没有它的时候（未登录、还没取到、
    // 取不到、贴图载不进来）退回首字母方块：退路必须留着，一块空白的头像比一个首字母
    // 更像"界面坏了"。
    // NOTE: setPixmap()/setText() 都会把另一份内容清掉（QLabel 只显示最后设的那一个），
    //       所以这里不用自己清。
    if (m_avatarPixmap.isNull()) {
        m_ui->getAvatarLabel()->setText(name.isEmpty() ? QStringLiteral("?")
                                                       : name.left(1).toUpper());
    } else {
        m_ui->getAvatarLabel()->setPixmap(m_avatarPixmap);
    }
    if (QLabel *avatar = m_ui->getHomeAvatarLabel()) {
        if (m_avatarPixmap.isNull()) {
            avatar->setPixmap(QPixmap::fromImage(
                SkinModel::headAvatar(SkinModel::defaultSteveSkin(), avatar->width())));
        } else {
            avatar->setPixmap(m_avatarPixmap.scaled(avatar->size(), Qt::KeepAspectRatio,
                                                    Qt::FastTransformation));
        }
    }

    // ---------- 中：任务摘要（coding plan §3.1） ----------
    // 这一格的内容**不在这里算**：显示谁是 `Tasks::collapsedSummary()` 那条纯规则的事，
    // 画成什么样是 `MainWindowUI::setBottomBarTaskSummary()` 的事。这里只负责"状态变了，
    // 重画一遍"。
    //
    // NOTE: 这里原来是三支抢这一格的优先级（启动 / 下载 / 同步等待，判据叫
    //       `bottomBarProgressOwner`）——**整个撤掉了**。§3.1 要的是"启动不再抢走并覆盖
    //       下载"：那两件事现在是账本里并列的两行，收起态挑一行展示而已，没有"借走"
    //       这回事，也就没有"借完要照原样还回去"。同步等待是唯一还压在上面的一层，
    //       理由写在 updateBottomBarTaskSummary() 里。
    updateBottomBarTaskSummary();

    // ---------- 右：当前实例运行状态 ----------
    // ★ 2026-10-15 从这一支里**整块搬进它自己的函数**（和中间那格 `updateBottomBarTaskSummary()`
    //   对称）。理由不是"函数太长"：红那一档现在看的是**账本**（账上还有没有没处理的异常退出），
    //   而账本每变一次都会来一发信号 —— 一发信号就重画整条底栏（连头像、账号名一起）是浪费，
    //   也把"这事儿归谁刷新"弄糊了。拆开之后：账本变 ⇒ 只叫这一格和中间那一格。
    updateBottomBarRunState();
}

/// 底栏右下那一格（"当前实例运行状态"）整个重画一遍 —— 方块的颜色、第一行、第二行。
///
/// **唯一出处**在下面那段注释里（四档的顺序）；这里只管画。
void MainWindow::updateBottomBarRunState() {
    // 与底栏使用同一份会话事实；队列刷新避免删除正在发 clicked 的行内按钮。
    m_ui->refreshTaskCenterPanel();
    QLabel *runStateLabel = m_ui->getRunStateLabel();
    QLabel *runStateDetailLabel = m_ui->getRunStateDetailLabel();
    QLabel *runStateMarkLabel = m_ui->getRunStateMarkLabel();
    const QList<LauncherManager::GameSession> games = m_launcherManager->runningGames();

    // 四档的顺序是**唯一的判据**（放这里当唯一出处，下面几支只负责画）：
    //   · 正在启动优先（黄）—— 那一段里进程已经起来了（所以 runningGames() 里已经有它），
    //     但用户等的是"游戏窗口出来"，这时候该说的是"启动中"，不是"运行中"；
    //   · 然后是**账上还有没处理的异常退出**（红，用户 2026-10-15 改的就这一档）——
    //     "出过事"比"此刻还在跑"更需要被看见；
    //   · 然后是"一个都没有"（灰）；
    //   · 剩下的就是真的在跑（绿）。
    //
    // NOTE: 判据里**没有**"选中的是哪个实例"——用户 2026-09-24 要的是"这台机器上有没有
    //       实例在跑"，那件事跟下拉框选着谁没关系（第二行的显示规则才看那个，见
    //       runStateShowsDetail()）。
    //
    // NOTE: 这个判据（`launching`）是**这一格自己的**，和中间那格的"任务摘要"没有关系：
    //       中间那格现在看的是账本（哪件任务该被显示），左边这一格看的是"会话起没起来"。
    //       一件 Launch 任务已经收过尾（游戏出话了）而这一份还在"启动中"是可能的
    //       —— 两者本来就是先后关系，谁也不该去问对方要判据。
    //
    // ★ 红那一档问的是 `CrashReportController::pendingRecordIds()`（"账上还有没有那一项"），
    //   **不是** `runningGames()`：红说的是"发生过的事"，跟此刻有没有进程在跑是两个维度
    //   （两份东西可以同时为真）。灰那一档才是原来那个问题（"一个都没有且没欠着事"）。
    //   用户 2026-10-15 原话：「黄和绿不变，红色改有实例报错，没有实例用灰色」。
    const bool launching = m_gameSessionController->hasLaunchingSessions();
    const bool pendingCrash = !m_crashReports->pendingRecordIds().isEmpty();
    const MainWindowUI::RunStateMark mark =
        launching ? MainWindowUI::RunStateMark::Launching
                  : (pendingCrash
                         ? MainWindowUI::RunStateMark::Error
                         : (games.isEmpty() ? MainWindowUI::RunStateMark::Idle
                                            : MainWindowUI::RunStateMark::Running));
    // 那颗方块的图**每次重画都重新要一张**（VersionIcons 里按"边长+颜色"缓存着，
    // 所以这一步不贵）：状态变了颜色就得跟着变，而这里正是"状态唯一定下来"的地方。
    //
    // 调色板取**底栏**的，不取那颗按钮的：按钮上挂着一份 QSS（applyBottomBarButtonStyle），
    // 而带样式表的控件解析出来的 QPalette 里 Window 那个角色是**黑**的（实测亮度 0），
    // 拿它判深浅就会在浅色主题下也退回深色那一档的取色。方块画在底栏那块底上，
    // 底栏的调色板才是"它背后那块布"的颜色 —— 深色主题下它自己会跟着变。
    runStateMarkLabel->setPixmap(
        MainWindowUI::runStateMarkPixmap(mark, m_ui->getBottomBar()->palette()));
    runStateMarkLabel->setVisible(true);

    // 左下那一格**现在点不点得开**。判据就是"左面板里会不会有行"—— 那几行是拿
    // runningGames() 翻出来的（见 setBottomBarPanelContent() 里那个 provider），所以
    // 空不空和这里**同源**。用户 2026-09-25 原话：「没有运行中的实例时让那个实例栏点不开，
    // 箭头隐藏」。
    // NOTE: 黄灯那一档（"正在启动"）在这里也是**点得开**的：那一段里会话已经在
    //       runningGames() 里了（见上面三态那段注释），面板里列得出它。
    m_ui->setRunPanelAvailable(!games.isEmpty());

    if (mark == MainWindowUI::RunStateMark::Launching) {
        // 黄：正在启动。名字的写法**不看「版本名格式」那四个开关**（见 launchingStatusName()）。
        runStateLabel->setText(MainWindowUI::runStateLaunchingText(launchingStatusName()));
        runStateLabel->setVisible(true);
        // 第二行在这一档**一律不显示**：第一行那句话里已经带着实例名了（判据在
        // runStateShowsDetail() 里，和别的那几种情况同一处）。
        runStateDetailLabel->setText(QString());
        runStateDetailLabel->setVisible(false);
        return;
    }

    if (mark == MainWindowUI::RunStateMark::Error) {
        // 红：有实例异常退出、账上那一项还没处理掉（用户 2026-10-15 定的这一句）。
        runStateLabel->setText(MainWindowUI::runStateErrorText());
        runStateLabel->setVisible(true);
        // 第二行写**出事的那个实例**（和绿那一支写实例名是同一个意思：这一格说状态的时候
        // 总是连着说是谁）。名字空了就整行收起来 —— 宁可少一行，也不要一行空白。
        const QString crashedName = m_crashReports->latestPendingInstanceName();
        runStateDetailLabel->setText(crashedName);
        runStateDetailLabel->setVisible(!crashedName.isEmpty());
        return;
    }

    if (games.isEmpty()) {
        // 没有实例在跑：写「当前没有运行中的实例」。**不是藏起来** —— 用户 2026-09-23 要的
        // 就是这句话，而且那一格现在还兼着"点开看实例列表"那颗按钮，藏起来连按钮一起没了。
        // NOTE: 连带的好处是那一格的文字**任何时候都非空**，布局上不再有"从无到有"这一步
        //       （那一步正是"中间那根条突然缩短一截"的来源）。
        // NOTE: 这一档的方块 2026-10-15 从红改成灰了（红挪去说"出过事"）—— 那句话本身
        //       一个字没动，见 runStateNotRunningText() 那段。
        runStateLabel->setText(MainWindowUI::runStateNotRunningText());
        runStateLabel->setVisible(true);
        runStateDetailLabel->setText(QString());
        runStateDetailLabel->setVisible(false);
        return;
    }

    // 只有一份在跑时用老那句话（"实例运行中"），多份时报个数。同一句话在两个地方
    // 各拼一半，就会出现"跑了三个却说实例运行中"这种没人看得出错的说法。
    runStateLabel->setText(games.size() == 1
                               ? MainWindowUI::runStateRunningText()
                               : MainWindowUI::runStateRunningCountText(games.size()));
    runStateLabel->setVisible(true);

    if (games.size() == 1) {
        // 第二行要不要出，判据在 runStateShowsDetail() 里（那里也是它唯一的出处）。
        const bool showDetail = MainWindowUI::runStateShowsDetail(
            m_ui->isHomeTabCurrent(), selectedVersionId(m_ui->getInstalledVersionCombo()),
            games.first().versionId, launching);
        runStateDetailLabel->setVisible(showDetail);
        runStateDetailLabel->setText(showDetail ? instanceDisplayName(games.first().versionId)
                                                : QString());
        return;
    }

    // 两份以上：这一行**一定**要出，而且**必须带上身份**（"哪个实例是哪个账号"正是
    // 多开这件事本身要回答的问题 —— 三个同版本实例跑在三个账号上时，光列名字分不出谁是谁）。
    // NOTE: 只在这一支里带账号，是想让"只开了一个"那种绝大多数情况下的界面一个字都不变。
    QStringList lines;
    lines.reserve(games.size());
    for (const LauncherManager::GameSession &game : games) {
        const QString name = instanceDisplayName(game.versionId);
        lines.append(game.accountName.isEmpty()
                         ? name
                         : MainWindowUI::runStateDetailWithAccount(name, game.accountName));
    }
    runStateDetailLabel->setVisible(true);
    runStateDetailLabel->setText(lines.join(QStringLiteral("、")));
}

// ============================ 任务中心（coding plan §2 / §3） ============================
//
// 这一小片是**产品侧通往账本的唯一一道门**：上面那些执行者（下载 / 版本 / 启动）的信号
// 在这里被翻成一次 `begin()` / `setProgress()` / `succeed()`，反过来底栏和任务中心面板
// 只读账本、不认识任何一个执行者。
//
// NOTE: 这几个函数里一个汉字都不该进日志 —— 用户点名的规矩是「进度不进日志」（日志里那
//       一份是给人事后排查的，几千个资源对象各写一行会把它淹掉）。

void MainWindow::updateBottomBarTaskSummary() {
    const QDateTime now = QDateTime::currentDateTime();
    // 收起态显示谁 —— **唯一出处**在账本层那条纯函数里（§3.1 那一串优先级 + "刚完成那一
    // 项还露着"的时间窗）。这里一个字都不加规则：加了就等于有两套优先级。
    const Tasks::TaskSummary picked = m_activityCenter->collapsedSummary(
        now, MainWindowUI::taskSummaryRecentWindowMs());

    MainWindowUI::BottomBarTaskSummary summary;
    summary.hasTask = picked.id != Tasks::invalidTaskId();
    summary.state = ActivityCenterController::presentationState(picked.state);
    summary.percent = picked.percent;
    summary.extraCount = picked.extraCount;
    summary.title = picked.title;
    // 红点看的是"**有没有没看过的失败**"，和正显示的是哪一项无关（§3.1：正在下载而上一件
    // 事失败了，两个信号都该看得见）—— 这条也是账本层算的，原样搬过来。
    summary.unreadFailure = picked.hasFailure;

    // ---------- 同步等待压在账本上面 ----------
    // 界面这一刻**是真的不响应**的（见 enterSyncWait()：UI 线程在等网络或等进程），
    // "在等什么"这句话比"哪件任务排第一"更该被看见。
    //
    // ★ 不压的话还有一个具体的坏处：一份正在跑的下载每 200 ms 来一帧（见
    //   onVersionDownloadStats），那一帧会把这句话顶掉 —— 2026-09-24 修过的正是这一类，
    //   当时那几句话（「正在校验登录状态…」「正在准备 Java 运行时…」）是专为"点了启动
    //   就像卡死"做的。现在那几帧只改**任务自己的** detail，改不到这一格的标题。
    //
    // NOTE: 它**不假装是一件任务**（不往账本里塞一行）：要的只是"别画成占位话、条子画成
    //       不确定态"。点开的那块面板画的是**账本**，所以那句等待话在面板里看不到 ——
    //       它属于哪件任务，那件任务的 detail 里会写（启动那一路就是启动任务）。
    if (!m_syncWaitStatus.isEmpty()) {
        summary.hasTask = true;
        summary.state = TaskCenter::State::Running;
        summary.percent = -1;
        summary.title = m_syncWaitStatus;
    }

    m_ui->setBottomBarTaskSummary(summary);

    // ---------- 「刚完成那一项还露着」的倒计时 ----------
    // 这一档**没有任何信号**会来告诉界面"到期了"：不自己起一个单发定时器的话，那句话会
    // 一直挂在底栏上，直到下一次有东西变 —— 而那时候可能已经过了十分钟（一次下载下完、
    // 用户就去干别的了）。
    m_taskSummaryTimer->stop();
    if (summary.hasTask && Tasks::isTerminalState(picked.state)
        && picked.id != Tasks::invalidTaskId()) {
        const Tasks::TaskSnapshot task = m_activityCenter->tasks()->snapshot(picked.id);
        if (task.finishedAt.isValid()) {
            const qint64 left = MainWindowUI::taskSummaryRecentWindowMs()
                                - task.finishedAt.msecsTo(now);
            if (left > 0) {
                // +50 ms：保证这一发落在**过了期限之后**。掐着期限那一毫秒醒来会算出
                // 同一个 left（还是 > 0），于是立刻再起一发 —— 那就是个死循环。
                m_taskSummaryTimer->start(int(left) + 50);
            }
        }
    }
}

/// 「这一项**此刻**按『取消』真的会做点什么吗」。
///
/// ★ 判据必须和 `onTaskDetailAction()` 里那条取消分派**逐字对齐** —— 那儿能做的只有两件
///   事（服务端安装那一趟、和 `VersionManager` 那一批下载）。所以这里也只问这两处，
///   **不在这一层按 kind 或状态另推一遍**（推错的下场：装作某一阶段还能取消）。
///
/// ★ 为什么非要现问一遍：账本里那位 `canCancel` 是 `begin()` 按**种类**记的（"这一类活
///   **允许**取消"），可这一类活里有几段**就是取消不掉**的 —— 加载器的安装器进程正在跑
///   的时候（那是个 `QProcess`，不在下载批次里），还有整合包解压那一段。只读账本的话，
///   那两段时间里详情页/面板上会留一颗**点了没反应**的「取消」。
///   `VersionManager::canCancelDownload()` 上头那句"别再加阶段判据，`isBusy()` 那时候
///   本来就是 false，那一格自然就没有取消可点" —— **说的就是这里**，从前没人接它。
bool MainWindow::cancelWouldWork(const Tasks::TaskSnapshot &task) const {
    if (m_modTransferCancels.contains(task.id)) return true;
    if (task.id == m_downloadTaskId && m_presetController && m_presetController->isInstallingMods()) return true;
    if (m_toolboxController && m_toolboxController->ownsDownloadTask(task.id))
        return m_toolboxController->canCancelDownloadTask(task.id);
    // ① 服务端安装那一趟：自己跑 `QProcess` + 自己的元数据请求，不走下载批次。
    if (m_serverInstall != nullptr && m_serverInstall->isRunning()
        && m_serverInstallTaskId != Tasks::invalidTaskId() && task.id == m_serverInstallTaskId) {
        return true;
    }
    // ② 其余的（下载 / 安装 / 导入）底下都是 `VersionManager` 那一批：有批次在飞才真能掐。
    // ★ 2026-10-19：判据里**必须**带"这一项自己就是那一批"。原来只看"下载器忙不忙"，
    //   于是任何被记成 Install 的**别种**任务（典型：升级副本那种后台搬文件，见
    //   InstanceDialogController 的 begin）都会长出一颗「取消」，按下去掐掉的却是当时
    //   正好在飞的**另一批**版本下载。`cancelTaskFromDetail()` 里那一句分派必须同口径。
    // ★ 2026-10-19 再补：`VersionManager` 上可以同时挂两批，账本里也就有两个号 ——
    //   ① `m_downloadTaskId`（`beginDownloadTask()` 记的版本 / 整合包 / 资源下载）；
    //   ② `m_updateBatch.task`（「更新某个实例的 N 个资源」，见
    //   `updateInstanceResources()`）。两批都是 `canCancelDownload()` 的真主人，少认一个
    //   就是"取消按钮整个消失"。别的号（升级副本、导入搬文件…）仍然一律不认。
    const bool ownsVersionBatch =
        task.id == m_downloadTaskId
        || (m_updateBatch.task != Tasks::invalidTaskId() && task.id == m_updateBatch.task);
    return m_versionManager != nullptr && ownsVersionBatch
           && m_versionManager->canCancelDownload();
}

bool MainWindow::retryWouldWork(const Tasks::TaskSnapshot &task) const {
    // 判据本体在账本层的纯函数 `Tasks::retryOffered()`（verify 的 AL 节钉着它那张真值表）——
    // 这一层只负责把执行器的“现在还能不能重试”喂给活动中心；失败清单属于哪一项由
    // ActivityCenterController 自己持有。
    const bool held = m_versionManager != nullptr && m_versionManager->canRetryDownload();
    return m_activityCenter->retryWouldWork(task, held);
}

/// 账本里的一项 ⇒ 界面要的一份。**`taskCenterItems()`（面板）和任务详情页共用的唯一
/// 翻译点** —— 两边各写一遍的话，同一项在面板上和详情页上会说出不同的话。
TaskCenter::Item MainWindow::taskCenterItem(const Tasks::TaskSnapshot &task) const {
    const bool retryPayloadAvailable =
        m_versionManager != nullptr && m_versionManager->canRetryDownload();
    return m_activityCenter->item(task, cancelWouldWork(task), retryPayloadAvailable,
                                  m_crashReports->hasRecord(task.id));
}

QList<TaskCenter::Item> MainWindow::taskCenterItems() {
    const QSet<Tasks::TaskId> crashRecordIds = m_crashReports->recordIds();
    bool viewStateChanged = false;
    const bool retryPayloadAvailable =
        m_versionManager != nullptr && m_versionManager->canRetryDownload();
    QList<TaskCenter::Item> items = m_activityCenter->items(
        [this](const Tasks::TaskSnapshot &task) { return cancelWouldWork(task); },
        retryPayloadAvailable, crashRecordIds, &viewStateChanged);
    if (viewStateChanged) {
        updateBottomBarTaskSummary();
    }
    return items;
}

void MainWindow::openTaskDetailPage(const QString &key) {
    // 保留这个兼容入口：面板的 rowActivated 和验证工程按这个名字调用。
    m_taskDetail->open(key);
}

void MainWindow::onTaskDetailAction(const QString &key, TaskDetailPage::Action action) {
    // 面板那一行行尾那几颗（取消 / 重试 / 打开目标 / 打开日志 / 导出报告 / 打开崩溃目录）
    // 走的就是这一条 —— 和详情页里那几颗**完全同一条路**（页里也发 `actionRequested`）。
    m_taskDetail->action(key, action);
}

void MainWindow::cancelTaskFromDetail(const QString &key) {
    bool transferParsed = false;
    const auto transferId = key.toULongLong(&transferParsed);
    if (transferParsed && m_modTransferCancels.contains(transferId)) {
        m_modTransferCancels.value(transferId)();
        return;
    }
    bool directKey = false;
    const auto directId = key.toULongLong(&directKey);
    if (directKey && m_toolboxController && m_toolboxController->cancelDownloadTask(directId)) return;
    if (m_presetController && key == QString::number(m_downloadTaskId) && m_presetController->cancelModInstall()) return;
    // ★ 取消要**按这一项是谁**分派：服务端安装那一趟和版本/资源下载那一批不是同一份
    //   状态（一个自己跑 QProcess + 自己的元数据请求，一个走 `VersionManager` 的批次）。
    //   两边都无脑叫一遍的话，取消一次服务端安装会顺手把别人正在跑的下载掐掉 ——
    //   而下载那一批可能是用户在下载页上排的、跟这一项毫无关系。
    //
    // NOTE: 这一条**留在 MainWindow**（`TaskDetailController` 只是按名字转手过来）：判据
    //       必须和 `cancelWouldWork()` 那条**逐字对齐** —— "那颗按钮露不露"和"按下去做的
    //       是哪一件事"是同一件事的两面，摆在一起才看得住。
    if (m_serverInstall != nullptr && m_serverInstall->isRunning()
        && m_serverInstallTaskId != Tasks::invalidTaskId()
        && key == QString::number(m_serverInstallTaskId)) {
        m_serverInstall->cancel();
        return;
    }
    // ★ 和 `cancelWouldWork()` 的 ② 逐字同口径：版本下载 / 整合包导入那一批认
    //   `m_downloadTaskId`，「更新某个实例资源」那一批认 `m_updateBatch.task`。
    //   两者底下是同一个下载器，所以都是同一条 `cancelActiveDownload()`。
    if (m_versionManager != nullptr
        && (key == QString::number(m_downloadTaskId)
            || (m_updateBatch.task != Tasks::invalidTaskId()
                && key == QString::number(m_updateBatch.task)))
        && m_versionManager->cancelActiveDownload()) {
        appendLog(tr("下载已取消。已经下好的文件留在原处。"));
    }
}

void MainWindow::retryTaskFromDetail(const QString &key) {
    // ★ 只认**这一项自己**那一批（`retryWouldWork()` 就是这条判据的唯一出处）：界面那边
    //   已经把不合条件的那颗按钮收起来了，这里是第二道闸 —— 页对象是**建一次留着**的，
    //   用户完全可能在页开着的时候又失败了一批，而按钮画的还是上一帧那份。
    bool parsed = false;
    const Tasks::TaskId retryId = key.toInt(&parsed);
    if (parsed && m_versionManager != nullptr
        && retryWouldWork(m_activityCenter->tasks()->snapshot(retryId))
        && m_versionManager->retryDownload()) {
        // 那一格失败清单**刚被这一按吃掉了**（`retryDownload()` 自己 reset）⇒ 号也跟着
        // 松开，不然同一颗按钮留在页上还能再点一次（再点一次什么都不会发生）。
        m_activityCenter->markRetryConsumed();
        appendLog(tr("正在重试上一次没下下来的那几个文件…"));
    }
}

Tasks::TaskId MainWindow::beginTask(Tasks::TaskKind kind, const QString &title,
                                    const QString &targetId,
                                    const QList<Tasks::TaskStep> &steps) {
    return m_activityCenter->begin(kind, title, targetId, steps);
}

void MainWindow::finishTask(Tasks::TaskId id, bool success, const QString &errorSummary) {
    const bool retryPayloadAvailable =
        m_versionManager != nullptr && m_versionManager->canRetryDownload();
    m_activityCenter->finish(id, success, errorSummary, retryPayloadAvailable);
}

void MainWindow::beginDownloadTask() {
    // 同一批里 `setDownloadUiBusy(true)` 只该来一次；但要是上一批的号还留着（理论上不该），
    // 也不能拿它当新的使 —— 那一行的进度会从上一批的位置开始爬。
    if (m_downloadTaskId != Tasks::invalidTaskId() && m_activityCenter->tasks()->has(m_downloadTaskId)) {
        return;
    }
    // 这一批到底是哪一类活：判据就是界面上自己记的那三个 id（它们在**置忙之前**就写好了，
    // 见那三处的 NOTE），而收尾那几条路（onVersionDownloadFinished）用的也是它们 ——
    // 所以起手和收尾的分档天然一致，不会出现"起手记成安装、收尾按下载报"。
    Tasks::TaskKind kind = Tasks::TaskKind::Download;
    QString title;
    if (!m_importingInstanceId.isEmpty()) {
        kind = Tasks::TaskKind::Import;  // 导入 = "装一整套东西"，和安装同一档优先级
        title = tr("导入整合包 %1").arg(instanceDisplayName(m_importingInstanceId));
    } else if (!m_installingInstanceId.isEmpty()) {
        kind = Tasks::TaskKind::Install;
        title = tr("安装 %1").arg(instanceDisplayName(m_installingInstanceId));
    } else if (!m_repairingInstanceId.isEmpty()) {
        kind = Tasks::TaskKind::Install;
        title = tr("校验并补全 %1").arg(instanceDisplayName(m_repairingInstanceId));
    } else {
        // 兜底那一支：今天从界面上**走不到**（能置忙的三条路都会先写上那三个 id 之一 ——
        // 版本下载没有单独的入口，它是那三条路里的一段）。留着是因为它是个安全的默认：
        // 以后多一条"不特定属于某个实例的下载"，它会得到一个说不岔的标题，而不是一条
        // 空标题的任务。种类给 `Download`（"把文件拿下来"）而不是 `Install`。
        title = tr("下载游戏文件");
    }
    m_downloadTaskId = beginTask(kind, title);
}

void MainWindow::startPlayerSkinLoad() {
    // ★ 2026-10-15：这一趟的**全部判断**都搬进 PlayerSkinController 了（挑哪一张 / 回来的
    //   是不是刚才那一位 / 什么时候才记账 / 取不到怎么降级 —— 四个见那边的文件头）。这一位
    //   现在只剩一件事：把它看不见的那一格交给它 —— **现在选中的是哪个实例**（下拉框在这一
    //   层）。那一格不是可有可无的：离线账号那张官方默认皮肤是**跟着实例走**的，换版本这张
    //   脸真的会变（两套规矩算出来的不是同一张，见 DefaultSkin.hpp 文件头）。
    //
    // NOTE: 六处调用点（开窗末尾、登录成功、切账号、换版本、换完外观、退出登录）读起来还是
    //       "把外观拉一遍"，所以名字没改。
    m_playerSkin->reload(selectedVersionId(m_ui->getInstalledVersionCombo()));
}

// ---------- 下面四个是**薄壳**：验证工程按名字打量它们 ----------
//
// mw_check 的 X 节（喂一份外观 / 演"档案到了""贴图到了"）和 AF5/AF6/AF7（演"挑好的那张回来
// 了"）都是 `QMetaObject::invokeMethod` 直接打这几个名字 —— 那是**全程不联网**验完整条外观
// 路径的唯一办法，所以名字必须留在这里、逻辑一行都不能留在这里。

void MainWindow::onSkinProfileReady(const QString &uuid, const PlayerSkinProfile &profile) {
    // 整份判断在 `PlayerSkinController::onProfileReady()`：认人（回来的是不是发请求时那位）、
    // 落盘、和手上那份一样就什么都不做，以及"披风地址换了才作废"那一条。
    m_playerSkin->onProfileReady(uuid, profile);
}

void MainWindow::onSkinTextureReady(const QString &url, const QString &path) {
    // 整份判断在 `PlayerSkinController::onTextureReady()`：两张贴图**分开**回来，按 URL 认领
    // 到该去的那一格，再用手上这两格重画一遍（谁先到都能画）。
    m_playerSkin->onTextureReady(url, path);
}

void MainWindow::onRandomSkinReady(const QString &skinUrl) {
    // 整份判断在 `PlayerSkinController::onRandomSkinReady()`："发请求时那位 == 现在这位"那道
    // 闸、以及"过期就按当下这位再问一次"都在那边（见那段长注释）。
    m_playerSkin->onRandomSkinReady(skinUrl);
}

void MainWindow::applyPlayerSkin(const QString &skinPath, const QString &capePath, bool slim) {
    // 整份判断在 `PlayerSkinController::applyPlayerSkin()`：那里管"载不进来就退回默认外观"
    // （那张图没有地址、离线账号也没有披风），载成功才把那两格路径记下来。
    // 这一层收到的是那边发回来的 `skinChanged`（见 onPlayerSkinChanged）。
    m_playerSkin->applyPlayerSkin(skinPath, capePath, slim);
}

void MainWindow::onPlayerSkinChanged(const QImage &skin, const QImage &cape, bool slim) {
    // ★ 2026-10-15：这一段就是原来 applyPlayerSkin() 的尾巴，一步都没改顺序 —— 只是触发它的
    //   从"这一层自己判断完了"换成了"PlayerSkinController 说现在该画这一套"。
    //
    // 收到的两张图一定是**画得出来的**（载不进来时那一边已经退回默认外观了），所以这里只管
    // 摆，不再有一句"这张图打不开怎么办"。
    m_ui->getSkinViewer()->setSkin(skin, cape, slim);
    // 头像在这里**算一次存起来**：updateBottomBar() 每次进度刷新（几百毫秒一次）都会被调，
    // 在它里面现算等于一直在重渲染同一张 8×8 的皮肤。裁切和放大都在
    // SkinModel::headAvatar() 里（最近邻放大，保持像素画的那个方块感）。
    m_avatarPixmap = QPixmap::fromImage(SkinModel::headAvatar(skin, MainWindowUI::avatarSize()));
    // 那个窗里的 3D 预览跟着**同一份贴图**走：它和这里是同一个东西的两个窗口，各下各的、
    // 各画各的就会出现"主页换了、那个窗还是旧外观"。挂在这一路上是因为全仓**唯一**一处决定
    // "现在显示的是哪一套外观"的就是 `PlayerSkinController::skinChanged` 这一条（贴图从哪来、
    // 档案怎么换都不影响这条）。
    if (m_accountSkinDialog != nullptr) {
        m_accountSkinDialog->setLooks(skin, cape, slim);
    }
    // 画了新东西 → 那句话按"手上有没有真皮肤"重挑一档（那一边把事实都准备好了）。
    updateSkinHint();
    updateBottomBar();
}

QPixmap MainWindow::accountFacePixmap(const SavedAccount &account) const {
    // ★ 这条路**只读本地已经躺着的东西**：不联网、不换令牌、不读客户端 JAR。
    //   面板是"点一下就弹出来"的，用户原话的前提也是「（因为选择/登录过，所以肯定会有缓存，
    //   应该不会遇到丢失的情况）」—— 所以这里按"缓存里有就用、没有就自己画一个"来收尾，
    //   而不是去把缺的那张拉下来（拉皮肤要网络、要令牌，还要为每一行各等一次）。
    const int size = MainWindowUI::bottomBarPanelIconSize();
    // 缓存图 → 头像。图上取不到（文件不在 / 不是一张皮肤）就是一张空图，让调用方接着退。
    const auto faceFromTexture = [size](const QString &path) -> QPixmap {
        if (path.isEmpty() || !QFile::exists(path)) {
            return {};
        }
        const QImage texture(path);
        if (texture.isNull()) {
            return {};
        }
        // headAvatar() 自己认 64×32 的老格式（那一档只取头和帽子，是同一块区域）。
        const QImage head = SkinModel::headAvatar(texture, size);
        return head.isNull() ? QPixmap() : QPixmap::fromImage(head);
    };

    // ① 当前账号：底栏那颗头像是**现成的**（m_avatarPixmap，applyPlayerSkin() 里算好的），
    //    缩一下就是这一行的图。
    //
    //    这一条必须排在最前面。它是"面板上这一行"和"底栏那颗"是同一张脸的**唯一保证** ——
    //    走下面那几条缓存路的话，当前账号正在"随机皮肤还没下回来"那一档时，面板会给出一张
    //    缓存里的旧图（或者干脆一个首字母方块），同一个窗口里同一个人两种脸。
    //    NOTE: 缩放的插值方式跟着 headAvatar() 那套走（最近邻）—— 那张图本来就是 8×8
    //          放大出来的像素画，平滑插值只会把脸糊掉。
    if (account.isCurrent && !m_avatarPixmap.isNull()) {
        return m_avatarPixmap.scaled(size, size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    if (!account.offline) {
        // ② 正版账号：缓存里那份档案写着他的皮肤地址，图按地址的 sha1 躺在同一个缓存目录里
        //    （PlayerSkinStore 的两级缓存，见那边的文件头）。地址是内容哈希，所以"本地有这张
        //    图"就等于"这是他当前的皮肤"。
        PlayerSkinProfile profile;
        if (PlayerSkinStore::loadCachedProfile(account.uuid, &profile) &&
            !profile.skinUrl.isEmpty()) {
            const QPixmap face = faceFromTexture(PlayerSkinStore::cachePathForUrl(profile.skinUrl));
            if (!face.isNull()) {
                return face;
            }
        }
    } else {
        // ③ 离线账号优先用**他自己挑的那张**（「账号与外观」→「换一张…」，见 OfflineSkin）。
        //    它排在这里是为了和 PlayerSkinController::reload() 的次序一致：那个函数里手动
        //    挑的也排在随机 / 算出来的前面。次序不一致的症状就是"主页是这张、面板是那张"。
        const QPixmap manualFace = faceFromTexture(OfflineSkin::skinPathFor(account.uuid));
        if (!manualFace.isNull()) {
            return manualFace;
        }
        // ④ 其次是**随机挑的那张**（RandomSkinStore 记在盘上的地址）。挑过就有地址，图和
        //    正版走同一个缓存 —— 同一个账号在底栏、面板、主页看到的是同一张脸。
        //    NOTE: 认人用 account.uuid（离线账号的 uuid 是 md5("OfflinePlayer:"+名字)，
        //          和 startPlayerSkinLoad() 里读的是同一个值），不是 m_authManager 那个
        //          —— 后者对离线账号是空串。
        const QString chosen = RandomSkinStore::loadChosenSkinUrl(account.uuid);
        if (!chosen.isEmpty()) {
            const QPixmap face = faceFromTexture(PlayerSkinStore::cachePathForUrl(chosen));
            if (!face.isNull()) {
                return face;
            }
        }
        // ⑤ 再退一步：**算出来的**那张官方默认皮肤的缓存图（见 DefaultSkin）。
        //
        //    它的缓存名带三段（账号 / 版本 / 条目），所以这里得先猜"当初是按哪个实例算的"。
        //    猜错只是 QFile::exists 落空，**绝不会触发读 JAR** —— 那一条路要起一次
        //    PowerShell（几百毫秒），挂在"点开面板"这一下上是不行的。所以这里**直接拼
        //    cachePathFor()**，一步都不走 resolve()。
        // 猜"当初按哪个实例算的"那三档在 PlayerSkinController 里（它才知道上一轮真的按谁
        // 算过），这里把另两格喂给它：当下选中的实例、上次启动的版本。
        const QStringList versions = m_playerSkin->offlineDefaultSkinCacheVersions(
            selectedVersionId(m_ui->getInstalledVersionCombo()),
            m_settings->lastLaunchedVersion());
        for (const QString &versionId : versions) {
            for (const DefaultSkin::Rule rule :
                 {DefaultSkin::Rule::Eighteen, DefaultSkin::Rule::Two}) {
                const DefaultSkin::Prediction prediction = DefaultSkin::predict(account.uuid, rule);
                if (!prediction.valid) {
                    continue;
                }
                const QPixmap face = faceFromTexture(
                    DefaultSkin::cachePathFor(account.uuid, versionId, prediction.entryPath));
                if (!face.isNull()) {
                    return face;
                }
            }
        }
    }

    // ⑤ 一张图都没有 —— 自己画一个首字母方块。**这是常态，不是出错**：从没登录过的正版
    //    账号（列表里那条只是个凭据副本）、刚加进来还没挑过皮肤的离线账号，本来就没有图。
    //    面板不能因为某一项没图就空着那一格（三行的行首一高一低比一个方块更难认）。
    return QPixmap::fromImage(SkinModel::letterAvatar(account.name, size));
}

void MainWindow::updateSkinHint() {
    // ★ 2026-10-15：这里不再去读外观那九个成员（它们跟着决策搬进 PlayerSkinController 了），
    //   改成问它要**几格事实**再挑一句。分工是：那一边说"现在是什么状况"，这里说"这种状况
    //   该讲哪一句中文" —— 文案仍然只在这一层出现（`MainWindowUI` 那五句），那一层一个中文字
    //   都不认识（也不许认识 `ui/`）。
    const QString name = m_authManager->playerName();
    QLabel *hint = m_ui->getSkinHintLabel();
    const PlayerSkinController::HintFacts facts = m_playerSkin->hintFacts();
    // 离线账号有一套自己的话（"正版外观"那句对它是错的，"未登录"那句也不对 ——
    // 它是**故意**不登录的，读起来像出错了）。判据只有一个：当前账号本身是不是离线账号。
    if (facts.hasSkin) {
        if (!facts.offline) {
            hint->setText(MainWindowUI::skinHintLoadedText(name, facts.hasCape));
        } else if (facts.offlineCustomSkin) {
            // 用户自己挑的那张（「账号与外观」→「换一张…」）：这是明确的选择，既不是
            // "随机挑的"也不是"某个实例算出来的"，所以有它自己那一句。
            hint->setText(MainWindowUI::skinHintOfflineCustomSkinText(name));
        } else if (!facts.offlineDefaultSkinVersionId.isEmpty()) {
            // 算出来的那张官方默认皮肤（见 DefaultSkin）：说得比"随机挑的"准一点 —— 这是
            // 那个版本的客户端本来就会给他分配的那一张。版本号**不是废话**：换版本这张脸
            // 真的会变（两套规矩算出来的不是同一张），那句话要能解释这件事。
            // NOTE: 这一格存的是目录名，显示名是这一层翻的（`instanceDisplayName()`）——
            //       那一层拿不到下拉框，也不该拿到。
            hint->setText(MainWindowUI::skinHintOfflineDefaultSkinText(
                name, instanceDisplayName(facts.offlineDefaultSkinVersionId)));
        } else {
            hint->setText(MainWindowUI::skinHintOfflineLoadedText(name));
        }
    } else if (facts.offline) {
        // 那张随机外观还没画上：不是"还在要"就是"没要到"。这两句都不弹窗（理由同
        // PlayerSkinStore.hpp 文件头：外观是装饰，取不到也照样能启动游戏）。
        hint->setText(facts.fetching ? MainWindowUI::skinHintLoadingText(name)
                                     : MainWindowUI::skinHintFailedText(name));
    } else if (!facts.loggedIn) {
        hint->setText(MainWindowUI::skinHintNotLoggedInText());
    } else if (facts.fetching) {
        hint->setText(MainWindowUI::skinHintLoadingText(name));
    } else {
        hint->setText(MainWindowUI::skinHintFailedText(name));
    }
}

QString MainWindow::instanceDisplayName(const QString &versionId) const {
    // 用下拉框里那条文本，也就是用户看惯了的显示名（可能是"我的整合包-1.20.1-Forge"），
    // 而不是裸目录名。找不到（版本刚被删、或者压根不在本地）时退回版本号本身 ——
    // 底栏宁可显示一个用户没见过的目录名，也不能什么都不显示。
    const QComboBox *combo = m_ui->getInstalledVersionCombo();
    const int index = combo->findData(versionId);
    return index >= 0 ? combo->itemText(index) : versionId;
}

void MainWindow::updateDownloadProgressUi(int done, int total, int failed,
                                          const QString &currentFile) {
    // 这一批对上的那件任务（没有这一批 = 无效号）。**进度只往它身上写** —— 底栏那一格和
    // 任务中心面板都是从账本投影出来的，没有第二处要同步（§2.1：账本是唯一真相）。
    const Tasks::TaskId task = m_downloadTaskId;

    // 标题下面那行小字。**没报过总数时只说"已下载多少"，不许编一个百分比出来**（§3.4 明写
    // 的那条验收）。`total <= 0` 在这里是"这一批还没报过总数"（版本 json 还在路上、
    // 或者加载器安装器那类还算不出要下几个文件）——不是"这一批下了 0 个"。
    QString detail;
    if (total <= 0) {
        detail = done > 0 ? tr("已下载 %1 个文件").arg(done) : tr("正在准备下载...");
    } else {
        detail = tr("已下载 %1 / %2").arg(done).arg(total);
        if (failed > 0) {
            detail += tr("，失败 %1").arg(failed);
        }
        if (!currentFile.isEmpty()) {
            // 路径可能很长（asset 目录层级深），只显示文件名
            detail += QStringLiteral("  ") + QFileInfo(currentFile).fileName();
        }
    }

    if (task != Tasks::invalidTaskId() && m_activityCenter->tasks()->has(task)) {
        // 两样都记：数量那半边决定百分比（条子画多长），文字那半边是"现在在干什么"。
        // NOTE: `total <= 0` 原样传给账本（它是"不可测"的判据，见 Tasks::percentOf()）——
        //       上面那句文案里的 done/total 是**说给人听**的，不要把 0 也传进去。
        m_activityCenter->tasks()->setProgress(task, done > 0 ? done : 0, total > 0 ? total : 0);
        m_activityCenter->tasks()->setDetail(task, detail);
        // ⚠ 这一段里**不要**再自己写进度条和状态栏。它们由 `taskChanged` → 底栏重画那一发
        //   统一摆（`Anim::smoothProgress()` 也在那条路上）——两处各写一次，用户看到的是
        //   同一格在 240 ms 的追值动画里被第二只手按住，实际表现是"条子偶尔一顿"。
    }

    // ★ 这里原来有一道"这一格现在不归下载就一个像素都不许动"的闸（判据是
    //   `bottomBarProgressOwner`）——**撤掉了**，因为那个问题在结构上没有了：这一句只改
    //   **任务自己的** detail，改不到底栏那一格的标题（那一格的标题由
    //   `Tasks::collapsedSummary()` 现算，而"在同步等待"那一层压在它上面，见
    //   updateBottomBarTaskSummary()）。2026-09-24 修的那一类"启动那几句话被 200 ms 一帧
    //   的下载顶掉"因此不会再回来。
}

void MainWindow::onVersionDownloadStats(int done, int total, int failed,
                                        const QString &currentFile) {
    // 前缘节流：200 ms 内的后续更新直接丢掉。不这么做的话，5000 多个 asset 会疯狂重排
    // 状态栏，界面反而卡住。
    // NOTE: 丢掉的那些**没有**"结束时补刷"这回事 —— 底栏那一格停在上一次越过阈值的
    //       那一帧，最多比真实进度旧 200 ms。整批结束由 setDownloadUiBusy(false) 把那一件
    //       任务收掉（收尾之后那一格换成"（已完成）"，停在哪一帧都看不出来了）。
    if (m_progressThrottle.isValid() && m_progressThrottle.elapsed() < 200) {
        return;
    }
    m_progressThrottle.start();
    updateDownloadProgressUi(done, total, failed, currentFile);
}

void MainWindow::appendLog(const QString &text) {
    // 界面里**不再有**日志面板（2026-09 分选项卡时去掉的）：命令行里本来就有
    // LauncherManager 的全套输出，多加一块面板只是把同一批东西抄第二遍。
    //
    // NOTE: 所以这条通道是"给人事后看"的，不是"必须让人看见"的。**必须让用户看到**
    //       的消息（缺 Java、下载失败、未选版本、找不到 version.json）一律走
    //       `InlineMessage`（`ui/InlineMessage.hpp`，全仓已无一处 `QMessageBox`）挡住
    //       操作，绝不能只写这里。★ 2026-09-27 更新：这里原来写的是"走 QMessageBox"，
    //       而那句话早就和代码相反了（内嵌化那一批把 90 来处全换掉了）—— 正是
    //       "注释逐字声称的那一侧才是洞"那个形状，所以顺手改掉。
    qDebug().noquote() << QStringLiteral("[WTFML]") << text;
}

void MainWindow::markFileOperationBusy(const QString &versionId) {
    if (versionId.isEmpty()) {
        return;
    }
    // ★ 2026-10-19：计数而不是集合 —— 同一份实例上可以叠两趟作业。收尾顺序不受控
    //   （复制跑完的可能比模组预设早），先到先摘的话另一趟就裸奔了。
    ++m_fileOperationBusyCounts[versionId];
    m_fileOperationInstanceIds.insert(versionId);
}

void MainWindow::releaseFileOperationBusy(const QString &versionId) {
    if (versionId.isEmpty()) {
        return;
    }
    auto it = m_fileOperationBusyCounts.find(versionId);
    if (it == m_fileOperationBusyCounts.end()) {
        // 没记过就摘：说明调用方配错了一对，别把它变成负数、也别动集合（可能还有
        // 别的一趟正压着这个 id）。
        return;
    }
    if (--it.value() > 0) {
        return;
    }
    m_fileOperationBusyCounts.erase(it);
    m_fileOperationInstanceIds.remove(versionId);
}

QSet<QString> MainWindow::installingVersionIds() const {
    // 三个来源合起来才是"现在**不能启动**的那几个实例"。分开记是因为它们覆盖的时段
    // 不一样：VersionManager 那个只在一批文件真的在写的时候非空，而"点了导入、还在拉
    // 版本表"那几段是界面自己记的（数据层那会儿连批都还没开）。
    // 后台搬文件（模组迁移 / 「迁移」「升级副本」）两个入口共用这一份名单。
    QSet<QString> ids = m_fileOperationInstanceIds;
    const QString busy = m_versionManager->busyVersionId();
    if (!busy.isEmpty()) {
        ids.insert(busy);
    }
    // 这两个只在"用户点的是它"那一刻置上、终态时清掉（见那两个成员的说明），覆盖面比
    // VersionManager 那个宽 —— 正好补上前奏那几段。
    if (!m_importingInstanceId.isEmpty()) {
        ids.insert(m_importingInstanceId);
    }
    if (!m_installingInstanceId.isEmpty()) {
        ids.insert(m_installingInstanceId);
    }
    // 「补全文件」那颗按钮（2026-09-26）：它**是**一批下载在跑，所以上面那个 busy 那一份
    // 通常已经盖住它了；记这一笔是给"点下去、批次还没起来"那一小段兜底（见头文件）。
    if (!m_repairingInstanceId.isEmpty()) {
        ids.insert(m_repairingInstanceId);
    }
    return ids;
}

void MainWindow::refreshInstalledVersions() {
    QElapsedTimer refreshTimer;
    refreshTimer.start();
    // 只扫本地磁盘（.minecraft/versions/<id>/<id>.jar），同步返回，随叫随到。
    //
    // 识别不出来的版本只在第一次上报：本函数一次会话会被调很多次（构造、版本清单到达、
    // 下载完成、每勾一下「版本名格式」都会走一遍），每回都报就是刷屏。第一次报过之后
    // 连扫都不扫了 —— 传 nullptr 时
    // installedVersions() 走的是和加这个参数之前完全一样的那条路径。
    QStringList warnings;
    const QStringList installed = m_versionManager->installedVersions(
        m_reportedUnrecognizedVersions ? nullptr : &warnings);
    if (!warnings.isEmpty()) {
        m_reportedUnrecognizedVersions = true;
        for (const QString &warning : warnings) {
            appendLog(warning);
        }
    }

    QComboBox *combo = m_ui->getInstalledVersionCombo();
    // 还在下载/安装的那几个**先不出现在启动选项里**：一份文件没下全的实例列在下拉框里，
    // 用户点它就是启动一个文件不全的版本（游戏崩在缺库上，而且看不出原因）。
    //
    // NOTE: 这一步同时把"启动"和"下载"两件事**按版本**分开了：装 1.20.1 的这十来分钟里，
    //       早就装好的 1.18.2 照样在列表里、照样能启动（见 onStartButtonClicked 里的闸口）。
    //       以前是"有任何一批下载在跑，启动按钮就整片灰掉+下拉框禁用"，那既拦不住真正
    //       危险的那一种，又把完全无关的另一种一起堵死了。
    const QSet<QString> installing = installingVersionIds();
    QStringList launchable;
    launchable.reserve(installed.size());
    for (const QString &id : installed) {
        if (!installing.contains(id)) {
            launchable.append(id);
        }
    }

    QList<VersionDisplayParts> parts;
    parts.reserve(launchable.size());
    for (const QString &id : launchable) {
        // 每个版本读一次 <id>/wtfml.meta（几百字节的可选文件，绝大多数版本没有）。
        // 刻意不缓存：这个文件会被"重命名"功能改写，缓存了就会变成"改了名不生效"。
        // 开销上不必担心 —— 真机上实例数可以到 61 个，而这个函数一次会话没有调用上界。
        parts.append(m_versionManager->displayParts(id));
    }
    // 第一次用这一份 .minecraft：把五个默认组（置顶 / 可安装mod / 原版 / 快照版 /
    // 远古版）种进去。
    //
    // 时机就选在这里：**有实例可摆**的时候才种（一个实例都没有时种下去，树里就是五个
    // 空组头加一个「未分组」，纯噪声）。这个函数每次刷实例都走一遍，但种只在文件还不
    // 存在的那一次真的发生（见 seedDefaultGroups()），代价是一次 QFile::exists。
    //
    // NOTE: 挡住信号。这一趟本来就要把树重画一遍（下面那一段），而 groupsChanged 接的
    //       正是本函数 —— 不挡的话会把自己再叫一遍，白扫一次盘。
    // NOTE: 只在文件不存在时种，所以这是**一次**的事：用户把那五个删光之后不会再长回来
    //       （"默认可以删"那条要求的实现就在 seedDefaultGroups() 里）。
    // NOTE: 种不下去（盘写不了）只说一句日志 —— 默认组是锦上添花，原来那种"没分过组就
    //       平铺"的样子照样能用，为它弹一个框反而吓人。
    if (!parts.isEmpty()) {
        const QSignalBlocker blocker(m_groupStore);
        QString seedError;
        if (!m_groupStore->seedDefaultGroups(&seedError)) {
            appendLog(seedError);
        }
    }

    // 主页上"已安装版本"只有下拉框这一个控件了（右栏那张列表 2026-09 去掉，见
    // MainWindowUI 里主页那一段），所以这里只灌下拉框，同步选中项也只剩实例页那张列表。
    fillVersionCombo(combo, parts, m_settings->versionNameOptions());

    // 实例页那张树：**扫盘这一段先把原料留好**（见 m_instancePageParts 那段 —— 搜索框每
    // 敲一个字都要重灌树，那一条路不许再扫盘），然后重灌一遍。筛选条件此刻是什么样，
    // 那一趟自己会读（`instanceFilterQuery()`）。
    m_instancePageParts = parts;
    m_instancePageMarks.clear();
    m_instancePageMarks.reserve(parts.size());
    for (const VersionDisplayParts &part : parts) {
        m_instancePageMarks.insert(part.id, QIcon(instanceRowMark(
                                                  part, m_versionManager,
                                                  MainWindowUI::instanceTreeIconSize())));
    }
    rebuildInstanceTree();

    // 占位文案随上下文变。**两个串都必须非空**：空占位文案会让 QComboBox 在往空
    // 模型里插第一批数据时静默选中第 0 项（qcombobox.cpp:1753-1759），而这里必须
    // 停在"没选"，否则用户会在毫无察觉的情况下启动一个自己没挑过的版本。
    //
    // 这一段同时治掉一个真实的用户报告：「打开启动器时显示没有已安装版本」——
    // 真因就是这句以前写死成"暂无已安装版本"，灌完数据后没人改它，于是明明装了
    // 好几个版本，画面上却说着一个都没有。
    // 判据同样用 launchable："一个能启动的都没有"才是"暂无已安装版本"（唯一那个正在
    // 装的时候，下拉框里确实一个都挑不了，那时候说"请选择一个版本"是句空话）。
    combo->setPlaceholderText(launchable.isEmpty() ? MainWindowUI::noInstalledVersionPlaceholder()
                                                   : MainWindowUI::chooseVersionPlaceholder());

    // 刷完必须手动同步按钮文字和 tooltip：上面 fillVersionCombo() 里套了
    // QSignalBlocker，重灌过程不会发信号，光靠 connect 那条接不到。
    updateStartButtonText();
    const qint64 elapsedMs = refreshTimer.elapsed();
    if (elapsedMs >= 250) {
        qInfo() << "refreshInstalledVersions:" << elapsedMs << "ms for" << installed.size()
                << "installed versions";
    }

    // 服务端那一页也顺手重扫一遍：它扫的是 `<数据目录>/Servers`，和上面那条实例目录同源，
    // 而"换数据目录"正是这个函数被叫的场合之一（另一处是启动那一刻）。两页各自重灌，
    // 谁也不去猜对方缓存了什么。
    refreshServerList();
    // 「预设」那一页的两个实例下拉框跟着这份清单走（页面还没建出来时它自己什么都不做，
    // 建过之后按 id 保留当前选择）。**不能只在切到「预设」那一档时灌**：用户切到别的
    // 顶栏页之后后台装好了一份实例，再切回来时模式行还停在「预设」上，`open()` 不会被
    // 叫到 —— 下拉框里就少那一份，用户以为没装上。
    if (m_presetController != nullptr) {
        m_presetController->refreshInstances();
    }
    updateHomeRecent();
}

InstanceFilter::Query MainWindow::instanceFilterQuery() const {
    InstanceFilter::Query query;
    if (const QLineEdit *search = m_ui->getInstanceSearchEdit()) {
        query.text = search->text();
    }
    // 原版 / 模组是**一对互斥**的档：两颗都亮着时按"后点的那颗说了算"判不出来，所以
    // 干脆按一颗读（两颗同时亮着只可能是代码里摆错了，那时候按「原版」算 —— 界面上
    // 两颗都亮着的样子本身就是错的，不必为它编一套语义）。
    const auto checked = [this](MainWindowUI::InstanceFilterButton which) {
        const QPushButton *button = m_ui->getInstanceFilterButton(which);
        return button != nullptr && button->isChecked();
    };
    const bool vanilla = checked(MainWindowUI::InstanceFilterButton::Vanilla);
    const bool modded = checked(MainWindowUI::InstanceFilterButton::Modded);
    if (vanilla && !modded) {
        query.kind = InstanceFilter::Kind::Vanilla;
    } else if (modded && !vanilla) {
        query.kind = InstanceFilter::Kind::Modded;
    }
    query.updatableOnly = checked(MainWindowUI::InstanceFilterButton::Updatable);
    query.recentOnly = checked(MainWindowUI::InstanceFilterButton::Recent);
    return query;
}

InstanceFilter::Facts MainWindow::instanceFilterFacts(const VersionDisplayParts &part) const {
    InstanceFilter::Facts facts;
    facts.texts = InstanceFilter::textsFor(part, m_settings->versionNameOptions());
    facts.hasLoader = !part.loaderName.isEmpty();
    // 「可更新」认的是**实例设置里那趟检查**的结果（键在不在 = 查过没有，见
    // m_instanceOutdatedPaths 那段）。没查过的实例在这一档里筛不出来 —— 那是对的，
    // 谁也没说过它有更新。
    facts.updatable = !m_instanceOutdatedPaths.value(part.id).isEmpty();
    // 「最近运行」认的是启动账本（`wtfml.history`，跟着当前游戏文件夹走）。账本只留最近
    // 二十来条 ⇒ 很久没玩过的实例会从这一档里掉出去，口径写在那一颗的 tooltip 里。
    for (const LaunchHistory::Record &record : m_launchHistory->records()) {
        if (record.instanceId == part.id) {
            facts.recent = true;
            break;
        }
    }
    return facts;
}

void MainWindow::rebuildInstanceTree() {
    QTreeWidget *tree = m_ui->getInstanceList();
    const QList<VersionDisplayParts> parts = m_instancePageParts;

    // ---- ① 筛：过不了的实例**根本不进桶**（不是"画好了再藏起来"）----
    //
    // 「手工隐藏行」那条老毛病（§7.2 原话：「避免继续手工隐藏行」）在这一页上本来就不
    // 存在：这棵树**每次都是整个重建的**（fillInstanceTree 里第一句就是 tree->clear()），
    // 要少画几行只有一个正当办法 —— 不建它。
    const InstanceFilter::Query query = instanceFilterQuery();
    const bool filtering = query.isActive();
    QList<VersionDisplayParts> shown;
    shown.reserve(parts.size());
    for (const VersionDisplayParts &part : parts) {
        if (filtering && !InstanceFilter::matches(query, instanceFilterFacts(part))) {
            continue;
        }
        shown.append(part);
    }

    // ---- ② 按分组摆（归属照旧由 homeGroupIdOf() 答，筛子一个字都不改它）----
    //
    // 这一段和 `refreshInstalledVersions()` 里原来那段逐句一样，只是喂进去的是 shown。
    // 归属键走 instanceKey()（稳定 id，没有就退回目录名）。
    //
    // NOTE: 每个实例这里要读两次 wtfml.meta（上面 displayParts() 一次、这里 instanceId()
    //       一次）。看着浪费，但这个文件是几百字节的可选文件、真机上最多 61 个实例，
    //       而"把两者合并成一次读"要把 instanceId 塞进 VersionDisplayParts ——那是个
    //       纯显示用的结构，塞一个身份字段进去会让"显示名怎么拼"和"归属怎么认"纠缠在
    //       一起。分开读，代价是一次小文件打开。
    const QList<InstanceGroupStore::Group> groups = m_groupStore->groups();
    QList<InstanceGroupBucket> buckets;
    QHash<QString, int> bucketOfGroup;
    const QSet<QString> knownGroupIds = knownGroupIdsOf(m_groupStore);
    for (const InstanceGroupStore::Group &group : groups) {
        InstanceGroupBucket bucket;
        bucket.groupId = group.id;
        bucket.name = group.name;
        // ★ 筛着的时候**每一组都摆成展开的**（§7.2「清空搜索恢复原展开状态」前半句）。
        //   不这么做的话，命中条目躲在收起来的组里 = 用户看着像"搜不到"。那个展开状态
        //   **不落盘**（下面灌树只管画，写盘那条路在 onInstanceTreeActivated 里、被
        //   `filtering` 挡着），所以清空之后各组回到 wtfml.folders 里记着的样子。
        bucket.expanded = filtering ? true : group.expanded;
        bucketOfGroup.insert(group.id, buckets.size());
        buckets.append(bucket);
    }
    const QString pinnedGroupId = m_groupStore->pinnedGroupId();
    QSet<QString> pinnedInstanceIds;
    QList<VersionDisplayParts> ungrouped;
    for (const VersionDisplayParts &part : shown) {
        const QString key = instanceKey(part.id);
        const QString homeId = homeGroupIdOf(m_groupStore, key, part, m_versionManager,
                                             knownGroupIds);
        const auto found = bucketOfGroup.constFind(homeId);
        if (found != bucketOfGroup.constEnd()) {
            buckets[found.value()].parts.append(part);
        } else {
            ungrouped.append(part);
        }

        // ★ 两边都显示（用户 2026-09-26 定的）：置顶着的那一个在置顶组里**再摆一条同名
        // 的**，老家那一条从头到尾不动。取消置顶就是把这一条去掉。
        if (pinnedGroupId.isEmpty() || !m_groupStore->isPinned(key)) {
            continue;
        }
        pinnedInstanceIds.insert(part.id);
        if (homeId == pinnedGroupId) {
            continue;
        }
        const auto pinnedBucket = bucketOfGroup.constFind(pinnedGroupId);
        if (pinnedBucket != bucketOfGroup.constEnd()) {
            buckets[pinnedBucket.value()].parts.append(part);
        }
    }
    if (groups.isEmpty()) {
        // 一个真实分组都没有：平铺，一个组头都不插。
        buckets.clear();
        InstanceGroupBucket all;
        all.parts = shown;
        buckets.append(all);
    } else {
        InstanceGroupBucket rest;
        rest.name = MainWindowUI::ungroupedGroupText();
        rest.parts = ungrouped;
        buckets.append(rest);
    }
    fillInstanceTree(tree, buckets, m_settings->versionNameOptions(), runningVersionIds(),
                     m_instancePageMarks, pinnedGroupId, pinnedInstanceIds);
    syncTreeToCombo(m_ui->getInstalledVersionCombo(), tree);

    // ---- ③ 顶上那行说明：这一份文件夹里有几个实例（筛着的时候再说一句筛出来几个）----
    //
    // 那个数字数的是**能启动的**（= 已经从 installed 里滤掉在装的那几个）：这一行是
    // 实例页顶上那句"这里有几个实例"，和树里数得出来的行数必须是同一个数 —— 报
    // installed.size() 的话，正在装的那一个会让这句话比树上多一个，看着像漏画了。
    // 筛着的时候两个数**都要**：只报筛出来的那个数，用户会以为自己的实例没了。
    const QString root = AppPaths::minecraftDir();
    QString folderText = tr("当前文件夹：%1").arg(QDir::toNativeSeparators(root));
    if (!QDir(root).exists()) {
        folderText += tr("（这个目录不在盘上了）");
    } else {
        folderText += filtering
                          ? tr("（%1 个实例，筛出 %2 个）").arg(parts.size()).arg(shown.size())
                          : tr("（%1 个实例）").arg(parts.size());
    }
    m_ui->getInstanceFolderLabel()->setText(folderText);

    // ---- ④ 一条都没剩下时那一句话 ----
    //
    // 只在**真筛过**的时候露：不筛时树是空的（一个实例都没有）是另一件事，那句话由上面
    // 那行说明（「（0 个实例）」）去说 —— 混成一句的话，一个装着 60 个实例、只是还没扫完
    // 或者盘不在的用户会看见"没有符合筛选条件的实例"。
    if (QLabel *noMatch = m_ui->getInstanceNoMatchLabel()) {
        noMatch->setVisible(filtering && shown.isEmpty());
    }
}

QSet<QString> MainWindow::runningVersionIds() const {
    QSet<QString> ids;
    const QList<LauncherManager::GameSession> games = m_launcherManager->runningGames();
    for (const LauncherManager::GameSession &game : games) {
        // 同一个实例多开：这里天然去重（QSet），而"它在跑"这件事本来就只要一个是。
        ids.insert(game.versionId);
    }
    return ids;
}

void MainWindow::updateInstanceTreeRunState() {
    QTreeWidget *tree = m_ui->getInstanceList();
    // 挡信号：改的是文字，不该被当成"用户在树里点了什么"（树上的点击会同步到下拉框）。
    const QSignalBlocker blocker(tree);
    const VersionNameOptions options = m_settings->versionNameOptions();
    QTreeWidgetItemIterator it(tree);
    while (*it != nullptr) {
        const QString id = instanceIdOf(*it);
        if (!id.isEmpty()) {
            // 重新拼一遍而不是"在旧文本上加减后缀"：加减法要认得旧文本长什么样，
            // 而显示名本身会随"版本名格式"那一项变（那时 refreshInstalledVersions()
            // 会整棵重灌，不会走到这里）—— 每回从头拼，两条路的结果就一定一样。
            (*it)->setText(0, instanceTreeItemText(m_versionManager->displayParts(id), options,
                                                   m_launcherManager->isVersionRunning(id)));
        }
        ++it;
    }
}

void MainWindow::restoreLastLaunchedVersion() {
    const QString remembered = m_settings->lastLaunchedVersion();
    if (remembered.isEmpty()) {
        // 从没成功启动过任何版本：留空，让用户自己挑。这里不是错误路径，不打日志。
        return;
    }

    QComboBox *combo = m_ui->getInstalledVersionCombo();
    if (combo->currentIndex() >= 0) {
        // 已经有选中项了（理论上不会发生：构造函数里刚刷完、placeholderText 非空所以
        // 灌数据时不会自动选中第 0 项）。真发生了也不抢 —— 当前显示出来的那个才是
        // 用户看得见的选择，偷偷改掉比不恢复更糟。
        return;
    }

    const int index = combo->findData(remembered);
    if (index < 0) {
        // 记着的那个版本已经不在磁盘上了（被删了、目录改过名）。此时**刻意留空**：
        // 退回"再选一个"，而不是替用户改选一个别的版本 —— 启动是重操作，不替他猜。
        // 占位文案是"请选择一个版本"，开头那句"暂无已安装版本"只在真一个都没装时出现。
        qDebug() << "Last launched version is no longer installed:" << remembered;
        return;
    }
    // 显式收尾，**不依赖** currentIndexChanged 那条连接。那条连接是活的（这里没有
    // QSignalBlocker），但依赖它就等于依赖"setupConnections() 已经跑过了"这个隐藏
    // 前提 —— 而这里少刷一次的后果是按钮第二行停在"未选择版本"、和上面选中的版本
    // 对不上，正是 mw_check 那一整摊断言在防的事。这一句是幂等的，重复跑不花钱。
    combo->setCurrentIndex(index);
    updateStartButtonText();
}

void MainWindow::updateStartButtonText() {
    // 按钮文字分两行，第二行显示当前选中的版本，"点下去会启动哪个"不用再回头
    // 看上面的下拉框。名字可能很长（26.2-snapshot-1，或"我的整合包-1.20.1-Forge"），
    // 交给 QPushButton 自己按宽度决定换不换行，这里只保证第一行永远是"启动游戏"。
    QComboBox *combo = m_ui->getInstalledVersionCombo();
    const QString shown =
        combo->currentIndex() < 0 ? tr("未选择版本") : combo->currentText();
    m_ui->getStartButton()->setText(tr("启动游戏\n%1").arg(shown));

    // 下拉框的 tooltip 挂**裸 id**，不是显示名。两个理由：
    //   1. 下拉框设了 minimumContentsLength(12)，长显示名会被省略号截断；
    //   2. 更要紧的是，id 在界面上**任何开关组合下都不会出现** —— 显示名那边参与
    //      拼装的"实际版本"是 parts.realVersion（为空才退化成 id），所以只要
    //      wtfml.meta 里写了 realVersion，目录名就彻底从界面上消失了。而它才是真正
    //      进 --version 和文件路径的那个值。用户要排查"怎么启动错了版本"时，
    //      悬停是唯一能看到它的地方。
    // tooltip 必须走这条函数（而不是在 refreshInstalledVersions 里直接设），因为
    // fillVersionCombo() 套了 QSignalBlocker，刷新路径只能靠这一处收尾。
    const QString id = selectedVersionId(combo);
    combo->setToolTip(id.isEmpty() ? QString() : tr("版本目录名：%1").arg(id));
}

void MainWindow::refreshAvailableVersions() {
    // 只填树里那几条（2026-09-25 起是「最新版本」那一组 + 那四条分类）：它们唯一的数据源
    // 就是在线的版本清单（和已安装那一摊完全不相干 —— 以前清单为空时这里会退回显示本地
    // 已安装版本，两个数据源混在一张列表里，用户分不清点下去到底是启动还是下载）。
    // 四堆怎么分是 VersionManager 的事（它那边只有 VersionCategory 一处判据，含那张写死的
    // 愚人节版名单），"最新的两条是哪两条"同样是它的事（latestVersions()）。
    //
    // 条目文本就是**版本号本身**：在线清单 900+ 条逐条去读 wtfml.meta 是不可接受的
    // I/O，而且那些版本压根还没下载、磁盘上没有元数据可读。
    const QList<VersionManager::VersionChannel> channels =
        m_versionManager->availableVersionChannels();

    // 重灌之前记住"哪些分类是展开的"、"当前选中的是哪一条" —— 清单刷新（每次启动
    // 一次，拉回来还会再刷一次）不该把用户正在看的那一摊收起来。
    QTreeWidget *tree = m_ui->getVersionTree();
    QSet<QString> expandedKeys;
    QString selectedVersion;
    // 展开状态**逐个顶层条目读**，不区分"有没有选中项"。
    // NOTE: 以前这里是个 if/else：有选中项就只记"选中项所在的那个分类"、其它分类
    //       一律不管，只有**没有**选中项时才逐个扫一遍。于是同时展开着"正式版"和
    //       "快照"、并且在正式版里点过一个版本号之后，下次重灌会把快照那条收起来
    //       —— 而清单刷新（启动时那一次、以及拉回来之后那一次）本来就不该动用户
    //       看的那一摊。展开状态和选中项是两件互不相干的事，得分别记。
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = tree->topLevelItem(i);
        if (top->isExpanded()) {
            expandedKeys.insert(
                top->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole).toString());
        }
    }
    if (QTreeWidgetItem *current = tree->currentItem()) {
        if (QTreeWidgetItem *parent = current->parent()) {
            // 选中的是某个分类下的版本号。这个分类必须保住展开 —— 否则重灌之后选中
            // 的那条藏在收起来的分类里，用户看到的像是"选中的版本丢了"。
            expandedKeys.insert(
                parent->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole).toString());
            selectedVersion =
                current->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole).toString();
        }
    }

    // 「最新版本」那一组是**默认展开**的（用户原话：「下载游戏那个界面最上方放一个 最新版本 类，
    // 默认展开，里面放最新的正式版和快照」）。这一格只**摆一次**：用户把它收起来之后，
    // 清单刷新（每次启动那一次、以及拉回来那一次）不该又把它撑开 —— 而"记住哪些分类是展开的"
    // 这笔账上面刚记完，靠的就是这份集合，所以这里直接往里面塞一格，走的是**和用户自己展开
    // 完全一样**的那条路（重灌之后照旧按 isExpanded() 记回来），不是"每一发都强行展开"。
    //
    // NOTE: 为什么需要一个成员来记"摆过一次"：头一发时树是空的 ⇒ expandedKeys 也是空的
    //       ⇒ 不加这一格它从一开始就是收着的。而"树是不是空的"这个判据不能当替身 ——
    //       refreshAvailableVersions() 之外还有别人能清这棵树（那样用户收起来的东西会被
    //       又撑开一次），用成员就与"谁清了树"无关。
    if (!mLatestGroupExpandSeeded) {
        expandedKeys.insert(VersionManager::latestGroupKey());
        mLatestGroupExpandSeeded = true;
    }

    const QSignalBlocker blocker(tree);
    tree->clear();

    // ★ 2026-09-25 新加的第一条：**「最新版本」**（用户原话见上面那段）。
    //
    // 摆在**最上面**：它讲的是"现在最值得装的这两条"，放在四个分类后面等于没摆（那四栏各自
    // 几百条，谁也不会滚到底去看）。它不是第五个分类，所以有几件事刻意和那四条不一样：
    //   · 那四条是清单的**全集划分**（一条版本不重不漏地落进某一栏），这一组是**从里面挑
    //     两条出来** —— 同一个版本号在这两处各有一份（它自己那一栏里那条还在）；
    //   · 身份不来自 VersionCategory::channelKey()，走 VersionManager::latestGroupKey()
    //     （那个键保证和四个分类键两两不同，展开状态那笔账才串不了）；
    //   · 里面那两条**只认版本号**（和别处的版本号一样：点一下就是"基于它装"），
    //     所以它前面那个小图标按它**自己属于哪一类**给（正式版草方块 / 快照版覆雪草块）。
    //
    // NOTE: 清单没拉回来时（离线、或者第一发就失败）这一组照旧摆着，只是里面空的 ——
    //       和那四条空分类是同一个规矩（"这一条压根不出现"和"点开是空的"在界面上是两件
    //       很不一样的事，见 availableVersionChannels() 那段）。
    auto *latestTop = new QTreeWidgetItem(tree);
    latestTop->setData(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole,
                       VersionManager::latestGroupKey());
    latestTop->setText(MainWindowUI::versionTreeKeyColumn(), VersionManager::latestGroupText());
    for (const VersionManager::LatestVersion &entry : m_versionManager->availableLatestVersions()) {
        auto *child = new QTreeWidgetItem(latestTop);
        child->setData(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole, entry.version.id);
        child->setText(MainWindowUI::versionTreeKeyColumn(), entry.version.id);
        child->setData(MainWindowUI::versionTreeKeyColumn(), MainWindowUI::rowSubTextRole(),
                       releaseDateText(entry.version.releaseTime));
        const QPixmap mark =
            VersionIcons::versionChannelMark(VersionCategory::channelIndex(entry.channel),
                                             MainWindowUI::versionTreeIconSize());
        if (!mark.isNull()) {
            child->setData(MainWindowUI::versionTreeKeyColumn(), MainWindowUI::rowIconRole(), mark);
        }
    }
    if (expandedKeys.contains(VersionManager::latestGroupKey())) {
        latestTop->setExpanded(true);
    }

    for (const VersionManager::VersionChannel &channel : channels) {
        auto *top = new QTreeWidgetItem(tree);
        // 身份在 userData（= 分类键），文字只是给人看的 —— 和左栏那列分类同一个约定。
        top->setData(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole, channel.key);
        top->setText(MainWindowUI::versionTreeKeyColumn(),
                     channel.text.isEmpty() ? channel.key : channel.text);
        // NOTE: 这里**不要**去动 setChildIndicatorPolicy —— 想关掉 Qt 自己那个"左边
        //       一个小三角"的话，那是关错了地方：QTreeModel::hasChildren() 是按这个
        //       policy 直接 return false 的（QTreeWidget 源码里那条 switch），于是
        //       **整个分类的版本号一条都排不进布局**，树上只剩四条光秃秃的分类、
        //       点开也是空的。真要把左边那个三角去掉，走 rootIsDecorated(false)
        //       （见 MainWindowUI 里建树那一段）：它只影响装饰格的绘制，不动
        //       "有没有孩子"这件事。
        //       右边那个箭头仍然由 VersionTreeDelegate 画在行最右边（用户要的位置），
        //       一个版本都没有的分类它连箭头都不画。
        // 版本号前面那个小图标**按分类**给（用户 2026-09-23：「快照版的草用白色（覆雪草块），
        // 远古版就土块，愚人节版用幸运方块，原版不变」）。分类 → 图标那张表在
        // VersionIcons::versionChannelMark()，这里只把分类的**下标**递过去 —— ui/ 那边
        // 不认 VersionCategory（见 VersionIcons.hpp 那段"纯函数"的说明），所以分类 → 下标
        // 这一步必须走 VersionCategory::channelIndex()，别在这儿另写一个 switch：
        // 写歪了的症状是"快照那一栏前面白草、远古那一栏还是绿草"，不报错、只是难看。
        const QPixmap channelMark =
            VersionIcons::versionChannelMark(VersionCategory::channelIndex(channel.channel),
                                             MainWindowUI::versionTreeIconSize());
        for (const MinecraftVersion &version : channel.versions) {
            auto *child = new QTreeWidgetItem(top);
            child->setData(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole, version.id);
            child->setText(MainWindowUI::versionTreeKeyColumn(), version.id);
            // 版本号下面那行小字：发布时间（精确到日）。清单里没有 / 认不出来的，那行就不画。
            child->setData(MainWindowUI::versionTreeKeyColumn(), MainWindowUI::rowSubTextRole(),
                           releaseDateText(version.releaseTime));
            if (!channelMark.isNull()) {
                child->setData(MainWindowUI::versionTreeKeyColumn(), MainWindowUI::rowIconRole(),
                               channelMark);
            }
        }
        if (expandedKeys.contains(channel.key)) {
            top->setExpanded(true);
        }
        // 把选中那条找回来：**只在四条分类里找，不找「最新版本」那一组**。
        // Why: 同一个版本号在树上有两份（它自己那一栏里一条、最新那一组里可能还有一条），
        //      两个都是真的、点哪个都是"基于它装"。这一搜要是从头开始（「最新版本」现在在
        //      第一条），用户本来在"正式版"里选着的那条**每次刷新都会悄悄跳到最上面那一组**
        //      去 —— 他没动过手，高亮自己跑了。只在分类里找 = 和这次改动之前**一模一样**的
        //      行为（以前树上只有那四条）。代价：要是用户当初是从「最新版本」那一组点进去的，
        //      刷新之后高亮会落到它自己那一栏里 —— 那也仍在屏幕上、仍是同一条版本。
        if (!selectedVersion.isEmpty()) {
            for (int i = 0; i < top->childCount(); ++i) {
                if (top->child(i)->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole)
                        .toString() == selectedVersion) {
                    tree->setCurrentItem(top->child(i));
                    break;
                }
            }
        }
    }

    // ★ 2026-10-18：重灌完**再按筛选框里现在的字筛一遍**。上面那一趟只认得清单，
    //   不认识"用户此刻在找哪个版本号"—— 框里的字不会被 clear() 清掉，而条目全是
    //   新摆的（全是露着的）。少了这一句，屏幕上是"框里写着 1.20、树里 742 条全在"，
    //   不报错、只是一个字都不筛。
    m_ui->applyVersionFilter();
}

void MainWindow::openInstallPage(const QString &gameVersion) {
    if (gameVersion.isEmpty()) {
        return;
    }
    m_installGameVersion = gameVersion;
    m_installNameTouched = false;

    // 这一层只管界面：要装的是哪个版本、名称框里默认写什么、那五条加载器现在该是什么
    // 状态。真正的安装请求在 onInstallButtonClicked() 里发出去。
    m_ui->getInstallTargetLabel()->setText(MainWindowUI::installTargetText(gameVersion));
    // 默认名跟着"选中的加载器"走（没选就是原版号本身）—— updateLoaderRows() 会顺手
    // 把它算出来，所以这里先摆状态、再让它收尾。
    // 注意每次进安装页都**回到「不装（纯原版）」**：上一个实例选的是哪条加载器跟这一个
    // 没关系，留着的话下一个版本会顶着一个"上次顺手选的 Forge"进来。
    m_selectedLoaderIndex = MainWindowUI::modLoaderNoneIndex();
    // 钉住的版本号同理：换了一个游戏版本，上一份能装的版本就完全不是这一份了
    // （Forge 每个游戏版本能装的版本完全不同）。
    m_chosenLoaderVersion.clear();
    // 收起来 + 清掉上次灌进去的子条目（**不能**只 clear 整棵树：那样顶层那五条也一起
    // 没了）。takeChildren() 交出来的那些条目已经不带爹了，得自己删 —— 不删就是每进一次
    // 安装页漏一批（几十上百个 QTreeWidgetItem）。requestAllLoaderVersions() 里对每个
    // 加载器再来一遍，顺手把"正在获取…"占位摆上。
    for (int i = 0; i < MainWindowUI::modLoaderOptionTexts().size(); ++i) {
        QTreeWidgetItem *item = loaderItem(i);
        if (item == nullptr) {
            continue;
        }
        item->setExpanded(false);
        qDeleteAll(item->takeChildren());
    }
    updateLoaderRows();
    // 四个加载器能装哪些版本：**进这一页就全问出去**（用户原话："获取列表不能点开了再
    // 获取，得全获取完"）。以前是"点开哪条现拉哪条"，那次实现里还藏着一个"点不开了"
    // 的 bug（见 fillLoaderVersionItems() 里那段 NOTE）。
    requestAllLoaderVersions();
    // 名称框里的默认值：勾的是「不装」时就是原版号本身（那正是"装原版"这条老路）。
    updateInstallName();

    // 实例预设那一行（§7.1）：摘要按 `m_installPresetChoices` 刷一遍。
    // ★ 用户的选择**不跟着翻页重置**：换一个版本再看一眼，不该让他已经挑好的
    //   哪几类预设悄悄没了（取消弹窗也不写它 —— 见 openInstallPresetPicker()）。
    updateInstallPresetSummary();

    // 树那一层留着不动（用户按返回时回的就是它，连展开状态都还在）。
    m_ui->getGameStack()->setCurrentIndex(MainWindowUI::downloadGameInstallIndex());
    m_ui->getInstallNameEdit()->setFocus();
    appendLog(tr("准备安装，基于原版 %1。").arg(gameVersion));
}

void MainWindow::backToVersionTree() {
    // 只翻页，什么都不清：树里那份展开状态、名称框里敲过的字都留着 —— 用户按返回
    // 是想"再看一眼别的版本"，不是想"放弃刚才那些输入"。真要放弃，重进一次就重算了。
    m_ui->getGameStack()->setCurrentIndex(MainWindowUI::downloadGameTreeIndex());
}

// ---------------------------------------------------------------------------
// 安装页：名称框 + 四个加载器（各自选版本）
// ---------------------------------------------------------------------------

QString MainWindow::defaultInstanceName() const {
    // 目录名的形状照抄真机上那些实例：`1.18.2-Forge_40.3.12`、`1.21.1-NeoForge_21.1.210`。
    // 没选加载器时就是原版号本身 —— 那一份**不写薄 json**（id == 父版本），装出来的就是
    // 原版那个实例，和改动前「下载版本」的行为一个字不差。
    //
    // NOTE: 刻意**不**走 VersionDisplay::formatVersionName()：那是**显示名**的拼法，跟着
    //       用户在设置页选的"版本名格式"开关走（关掉"实际版本"那一档就会拼出一个不含
    //       版本号的目录名），而目录名要的是确定、可预期、跟显示开关无关。
    if (m_installGameVersion.isEmpty()) {
        return QString();
    }
    VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
    const int index = selectedLoaderIndex();
    if (!VersionManager::loaderKindFromIndex(index, &kind)) {
        return m_installGameVersion;
    }
    const QString loaderName = VersionManager::loaderDisplayName(kind);
    const QString pinned = pinnedLoaderVersion(index);
    // 钉的是一个**具体版本号**时原样写进目录名（`1.18.2-Forge_40.3.12`）。
    // 钉的是**那两档**（`@latest` / `@stable`）时**不带后缀**（`1.21.4-Forge`）—— 和改动前
    // 那条「自动（推荐）」写出来的名字一个字不差。两个理由：
    //   · 那两档最终装到哪个版本号，要等安装那条路联网挑完才知道（见
    //     VersionManager::installLoaderVersion），这一刻算不出来 —— 写 `_stable` 是拿
    //     "哪一档"当版本号用，而真实版本号在装完那份 wtfml.meta 的 loaderVersion 里；
    //   · 名字一样才走得上「已经有叫这个的实例了 → 再装一次是校验并补齐」那条路。带后缀
    //     的话，用户"同一个游戏同一个加载器"再装一次会多出一个并行实例，而他手上的老实例
    //     还叫 `1.21.4-Forge`。同一档换个版本装进同一个实例，本来就是这条路支持的用法。
    LoaderMeta::LoaderChannel channel;
    const bool byChannel = LoaderMeta::loaderChannelFromKey(pinned, &channel);
    const QString suffix = byChannel ? QString() : pinned;
    return suffix.isEmpty() ? QStringLiteral("%1-%2").arg(m_installGameVersion, loaderName)
                            : QStringLiteral("%1-%2_%3").arg(m_installGameVersion, loaderName,
                                                             suffix);
}

int MainWindow::selectedLoaderIndex() const {
    return m_selectedLoaderIndex;
}

QTreeWidgetItem *MainWindow::loaderItem(int loaderIndex) const {
    // 负数是「预设」那一行的哨兵（`installPresetRowRole()`），不是加载器下标 ——
    // 放进去会正好把它找出来（那个 UserRole 就是 -1）。
    if (loaderIndex < 0) {
        return nullptr;
    }
    QTreeWidget *tree = m_ui->getLoaderTree();
    if (tree == nullptr) {
        return nullptr;
    }
    // 顺序是界面自己按 modLoaderOptionNames() 摆的（第 i 条就是第 i 个加载器），
    // 但**身份**仍然从 userData 读 —— 摆的顺序哪天变了，这里不会跟着错。
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->data(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole).toInt() == loaderIndex) {
            return item;
        }
    }
    return nullptr;
}

QString MainWindow::pinnedLoaderVersion(int loaderIndex) const {
    // 这一格里有三种东西（见 MainWindowUI::loaderVersionStableText 上面那段）：
    //   · 一个**具体版本号** —— 用户在那两档下面点的那一条；
    //   · `@latest` / `@stable` —— 用户点的是上面那两档（LoaderMeta::loaderChannelKey）；
    //   · **空串** —— 没挑过（没进过安装页、或者版本列表没取到）。空串在数据层那边按
    //     稳定档算（LoaderMeta::loaderRequestFromChoice），也就是改动前"让安装器按老规矩挑"。
    // 没进过 map 的下标也是空串。
    return m_chosenLoaderVersion.value(loaderIndex);
}

void MainWindow::updateLoaderRows() {
    // 那棵树的样子**全部**在这一处决定（别处一个 setText/setData 都不许写）：
    //   · 选中的那一条：第二行小字写它选的那一格（版本号 / 「最新版」/「稳定版」）；
    //   · 其余几条加载器：第二行小字写「与 X 不兼容」（用户要的那句话）；
    //   · 「不装（纯原版）」那条：没有第二行（它不是加载器，不跟谁冲突）。
    // NOTE: 选中某一条之后，**其余几条加载器变灰、也点不开**（用户原话："选择以后让其他
    //       选项变成不可用（不能点开）的模式"）。改选别的加载器要先点「不装（纯原版）」
    //       —— 那条**故意不灰**，它既是"回纯原版"，也是唯一那条还点得动的退路；
    //       换版本则点当前这条底下的版本号。本启动器一次只装一个加载器，所以"不兼容"
    //       说的是"两个不能同时装"，而灰掉说的是"这一下点了也不会变成装它"。
    const int selected = selectedLoaderIndex();
    VersionManager::LoaderKind selectedKind = VersionManager::LoaderKind::Fabric;
    const bool hasLoader = VersionManager::loaderKindFromIndex(selected, &selectedKind);
    const QString selectedName =
        hasLoader ? VersionManager::loaderDisplayName(selectedKind) : QString();

    const int count = MainWindowUI::modLoaderOptionTexts().size();
    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem *item = loaderItem(i);
        if (item == nullptr) {
            continue;
        }
        VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
        const bool isLoader = VersionManager::loaderKindFromIndex(i, &kind);
        QString subText;
        if (isLoader && hasLoader) {
            if (i == selected) {
                const QString pinned = pinnedLoaderVersion(i);
                LoaderMeta::LoaderChannel channel;
                if (LoaderMeta::loaderChannelFromKey(pinned, &channel)) {
                    // 挑的是那一档：小字写档名（版本号要等装的时候才定下来）。
                    subText = MainWindowUI::loaderVersionChannelShortText(
                        channel == LoaderMeta::LoaderChannel::Newest);
                } else if (pinned.isEmpty()) {
                    // 没挑过（版本列表没取到）：按默认那一档说 —— 稳定档。
                    subText = MainWindowUI::loaderVersionChannelShortText(false);
                } else {
                    subText = pinned;
                }
            } else {
                subText = MainWindowUI::loaderConflictText(selectedName);
            }
        }
        // 角色是 rowSubTextRole()：空串 = 那一行不画第二行小字（代理那边统一处理）。
        item->setData(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowSubTextRole(), subText);
        // 选中的那条加载器：行尾（箭头左边）画一个「取消」小叉 —— 点它 = 不装这个加载器了、
        // 退回「不装（纯原版）」（用户原话："选中以后在那个 > 左侧加一个 x 来取消安装
        // 对应版本"）。**只有这一条**有这个叉：其余几条（包括「不装」）没有"要取消的东西"。
        // 点它的那一下由 MainWindow::eventFilter 接（几何是 VersionTreeDelegate::clearRect()）。
        item->setData(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowClearRole(),
                      isLoader && hasLoader && i == selected);
        // 灰那几条：选中了某个加载器之后**其余几条加载器**不可用（见上面那段 NOTE）。
        // 顺手把灰掉的那条**收起来** —— 收着的时候它底下的版本号看不见，而"展开着一份
        // 版本列表、整条却是灰的"看着像"点得动、只是不让点"（点一下才知道不是）。
        // 「不装（纯原版）」不参与：它不是加载器（isLoader 为假），而且是那条退路。
        if (isLoader) {
            const bool locked = hasLoader && i != selected;
            if (locked && item->isExpanded()) {
                item->setExpanded(false);
            }
            item->setDisabled(locked);
        }
    }
}

void MainWindow::selectLoader(int loaderIndex) {
    // 「不装（纯原版）」和四个加载器都是合法值；表外的值（界面表和枚举哪天对不上）
    // 一律当作"没选加载器"—— 宁可装成原版，也不要拿着一个不认识的下标去猜加载器。
    VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
    m_selectedLoaderIndex = VersionManager::loaderKindFromIndex(loaderIndex, &kind)
                                ? loaderIndex
                                : MainWindowUI::modLoaderNoneIndex();
    updateLoaderRows();
    // 默认名里带着加载器名（和它钉的版本号），换一条就得重算一次。
    updateInstallName();
    // NOTE: **这里不发任何请求**。"有哪些版本"是进安装页时一次性全问好的
    //       （见 requestAllLoaderVersions()，用户原话："获取列表不能点开了再获取，
    //       得全获取完"）。走这一句的只有两条路：点一条**版本号**（选中它爹那条加载器）、
    //       点「不装（纯原版）」那一条。点一条加载器**不再**选中它（那一下只展开/收起，
    //       见 onLoaderTreeClicked() 上面那段）—— 点一下就去碰那棵树是 2026-09-23 那个
    //       "点不开了"的成因之一，别再走回去。
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    // 安装页那棵树上**唯一一个"行里的按钮"**：选中那条加载器右边的「取消」小叉
    // （谁画的、画在哪一格，见 VersionTreeDelegate::clearRect() 那段 NOTE）。
    //
    // 为什么不走 itemClicked：它只给"点中了哪个条目"、不给"点在条目里的哪儿"，而这一下
    // 点的是行里的一个按钮、**不是这一行** —— 点中叉的时候那一行不该跟着展开/收起。
    // 过滤器能把事件吃掉（返回 true 之后 Qt 那套点击处理整条不走了），两件事就都干净。
    QTreeWidget *tree = m_ui->getLoaderTree();
    if (tree != nullptr && watched == tree->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QTreeWidgetItem *item = nullptr;
        if (mouse->button() == Qt::LeftButton) {
            item = tree->itemAt(mouse->position().toPoint());
        }
        if (item != nullptr &&
            item->data(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowClearRole())
                .toBool() &&
            VersionTreeDelegate::clearRect(tree->visualItemRect(item))
                .contains(mouse->position().toPoint())) {
            if (event->type() == QEvent::MouseButtonPress) {
                // 按下就动手（松开那一下只负责被吃掉）：跟普通按钮一样，按下去就该有反应。
                // 这一下取消的是"装这个加载器"这件事 —— 退回「不装（纯原版）」，也就是
                // 装纯原版（用户原话："来取消安装对应版本"）。
                // NOTE: **钉住的版本号留着**（不清 m_chosenLoaderVersion）：那是用户自己挑的
                //       一版，按一下叉就把它忘掉，下次再选中这条加载器还得从头找一遍。
                //       它只在"这一条被选中"的时候才显示，留着不会让人看到不该看的东西。
                selectLoader(MainWindowUI::modLoaderNoneIndex());
            }
            return true;
        }
    }
    // 实例页那张树上**行里的按钮**，头一个：组行行尾那颗齿轮（谁画的、画在哪一格，
    // 见 InstanceGroupDelegate::gearRect()）。判据和上面那个叉一模一样 —— 包括"吃掉
    // 那一下"：不吃的话点齿轮会顺手把这一组展开/收起（那正是"点组行"要干的事）。
    // 另外两颗（上移 / 下移）在下面那一段，判据同族，只有"按得快要不要补那一发"不同。
    //
    // NOTE: 这里**也接双击**。点得快的时候第二发及以后到的是 MouseButtonDblClick
    //       （见 ui/RowActivation.hpp 那段），只接 Press/Release 的话"连点两下齿轮"
    //       的第二下会漏给下面那条"点中了这一行"的路。
    QTreeWidget *instanceTree = m_ui->getInstanceList();
    if (instanceTree != nullptr && watched == instanceTree->viewport()
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QTreeWidgetItem *item =
            mouse->button() == Qt::LeftButton ? instanceTree->itemAt(mouse->position().toPoint())
                                              : nullptr;
        if (item != nullptr && item->data(0, MainWindowUI::rowGroupGearRole()).toBool()
            && InstanceGroupDelegate::gearRect(instanceTree->visualItemRect(item))
                   .contains(mouse->position().toPoint())) {
            // 按下就动手（松开、以及那几发双击只负责被吃掉）：跟普通按钮一样，按下去就该
            // 有反应。双击那几发**不能**再弹一遍 —— 弹第二遍时上面那块框已经开着，
            // openGroupMenu() 认到"还是同一组"就把它收回去了，用户看到的是"连点两下齿轮
            // 反而没了"（那一处正是"再点同一颗齿轮 = 收回"，别在门口把它挡掉）。
            if (event->type() == QEvent::MouseButtonPress) {
                openGroupMenuFor(item);
            }
            return true;
        }
    }
    // 同一个实例树上**实例行**行尾那一排（用户 2026-09-25 原话：「实例设置也是放在每一条的
    // 右侧，鼠标悬停时右侧显示上箭头和垃圾桶，分别对应 置顶和删除实例，删除前需要弹窗提示」）。
    // 行内三颗：**常显的齿轮**（= 实例设置）+ 悬停才出现的**上箭头**（= 置顶）和**垃圾桶**
    // （= 删除这个实例）。谁画的、画在哪一格，见 InstanceGroupDelegate::instanceGearRect()
    // 那几个函数 —— 这里读的是**同一个函数**，差一个像素就是"看着点中了垃圾桶、其实点的是
    // 那一行"（那一行现在也有一件事要做：进实例设置，见 onInstanceTreeActivated）。
    //
    // 判据"这是不是实例行"用**没有** groupIdRole()：和代理那边 paint()/sizeHint() 是同一条
    // （组行有那个角色、实例行没有）。
    //
    // NOTE: 三格的命中判定顺序无所谓（探针量过：三格之间零重叠、零空隙），但中间那一格
    //       要先问**这一行到底摆没摆它**（rowPinRole() / rowUnpinRole()）—— 这份
    //       .minecraft 里没有置顶组时那一格是空的：指着一块空白弹出「置顶」、点下去还
    //       真的挪了一次，是最难解释的一种。
    // NOTE: 中间那一格是**两个动作共用的一格**（不在置顶组 ⇒ ↑ 置顶 / 已经在了 ⇒ ↓
    //       取消置顶，见 MainWindowUI::rowUnpinRole()）—— 所以这里比的是"在里面不在里面"，
    //       不是"角色号是哪一个"。
    // NOTE: 也接双击，理由和上面组行齿轮那一段逐字相同（点得快时第二发及以后到的是
    //       双击事件，不接的话会漏给"点中了这一行"那条路，于是连点垃圾桶的第二下变成
    //       进实例设置）。
    if (instanceTree != nullptr && watched == instanceTree->viewport()
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QTreeWidgetItem *item =
            mouse->button() == Qt::LeftButton ? instanceTree->itemAt(mouse->position().toPoint())
                                              : nullptr;
        // 三格的顺序照代理**画它们的顺序**（齿轮 / 上箭头 / 垃圾桶），一粒一粒判。
        if (item != nullptr && !item->data(0, MainWindowUI::groupIdRole()).isValid()) {
            const QRect row = instanceTree->visualItemRect(item);
            const QPoint pos = mouse->position().toPoint();
            const QString instanceId = item->data(0, MainWindowUI::instanceIdRole()).toString();
            const bool pressed = event->type() == QEvent::MouseButtonPress;
            if (InstanceGroupDelegate::instanceGearRect(row).contains(pos)) {
                // 按下就动手（松开、以及那几发双击只负责被吃掉）：跟普通按钮一样，按下去
                // 就该有反应。齿轮 = 进这个实例的设置（用户原话：「直接点空白区域（和左边
                // 图标版本名）进入实例设置」—— 那一行别的地方也是同一件事）。
                if (pressed) {
                    openInstanceSettings(instanceId);
                }
                return true;
            }
            // 中间那一格：↑「置顶」和 ↓「取消置顶」**共用的一格**（见 MainWindowUI::
            // rowUnpinRole()）。判据是"这一行摆没摆它"那两个角色里**有一个为真**，
            // 按下去调谁看的是**在里面还是不在**（canUnpin 优先，两个都真时不可能，
            // 代理那边同一个约定）。
            const bool canPin = item->data(0, MainWindowUI::rowPinRole()).toBool();
            const bool canUnpin = item->data(0, MainWindowUI::rowUnpinRole()).toBool();
            if ((canPin || canUnpin) && InstanceGroupDelegate::instancePinRect(row).contains(pos)) {
                if (pressed) {
                    if (canUnpin) {
                        unpinInstance(instanceId);
                    } else {
                        pinInstance(instanceId);
                    }
                }
                return true;
            }
            if (InstanceGroupDelegate::instanceTrashRect(row).contains(pos)) {
                if (pressed) {
                    confirmDeleteInstance(instanceId);
                }
                return true;
            }
        }
    }
    // NOTE: 这里原来还有一段：同一个实例树上组行行尾那两颗「上移 / 下移」（齿轮左边那两颗
    //       实心三角）的接线。2026-09-25 用户把它们整个搬进了「分组设置…」那张表（原话：
    //       「这个点击一下就同时展开和调整了，其次这个箭头不应该在这里，而应该在 分组设置
    //       里面，这里的删了，去分组设置里面加上」），于是这一段和它调的
    //       moveGroupInOrder() 一起删了 —— 那张表里认下那一下的是
    //       `ui/GroupDialog::eventFilter()`（挂在那张表的视口上），"每一发都算"和
    //       "Shift = 置顶/置底"那两条手势原样跟着搬了过去。
    //       ★ 2026-09-25 第二轮：实例行行尾那一排的接线加在**上面**（就在组行齿轮那段
    //         后面）—— 它和这里删掉的那套是同一个位置、同一族判据，别再往下找。
    // 下载页左栏那份分类列表：每一行最右边那个刷新按钮（谁画的、画在哪一格，见
    // VersionTreeDelegate::refreshRect()）。判据和上面那个叉一模一样，只是宿主是个
    // QListWidget（它的 itemAt/visualItemRect 和 QTreeWidget 那对同名同义）。
    QListWidget *categories = m_ui->getDownloadCategoryList();
    if (categories != nullptr && watched == categories->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item =
            mouse->button() == Qt::LeftButton ? categories->itemAt(mouse->position().toPoint())
                                              : nullptr;
        if (item != nullptr &&
            item->data(MainWindowUI::rowRefreshRole()).toBool() &&
            VersionTreeDelegate::refreshRect(categories->visualItemRect(item))
                .contains(mouse->position().toPoint())) {
            if (event->type() == QEvent::MouseButtonPress) {
                // 按下就动手（松开那一下只负责被吃掉）—— 和那个叉一个规矩。
                refreshDownloadCategory(categories->row(item));
            }
            return true;
        }
    }
    // 下载页那四张资源表：**点行身 = 打开这一条资源的详情页**（用户原话：「下载页选择了
    // 模组以后进入一个独立界面…和 pcl 类似」）；点行尾那个箭头仍然是老规矩（展开 / 收起，
    // 判据是同一个 arrowRect()）—— 于是"展开这一条"这件事从此只挂在箭头上。
    //
    // NOTE: 这一块**只认第一层**（一个资源）。第二层（游戏版本）点开是"问这个资源在这个
    //       游戏版本上有哪些版本可下"、第三层（版本号）点它是"下它"，两条老路原样留着
    //       （用户要的详情页是**另一条**看整份清单的路，不是把树上那条拆掉）。
    // NOTE: 三发都吃掉（按下 / 松开 / 双击）：`connectRowActivation` 那边给"开关行"补的
    //       那一发双击也会落到"展开"上 —— 而这一下已经算"进这一页"了，不该再翻一次树。
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
        || event->type() == QEvent::MouseButtonDblClick) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        for (ResourceCatalog::Kind treeKind : ResourceCatalog::allKinds()) {
            const QString treeKey = ResourceCatalog::kindKey(treeKind);
            QTreeWidget *tree = m_ui->getDownloadResourceTree(treeKey);
            if (tree == nullptr || watched != tree->viewport()) {
                continue;
            }
            const QPoint pos = mouse->position().toPoint();
            QTreeWidgetItem *item =
                mouse->button() == Qt::LeftButton ? tree->itemAt(pos) : nullptr;
            if (item == nullptr || resourceRowDepth(item) != 0) {
                continue;
            }
            // "这一行是不是我们建的"：占位行（「正在搜索…」那几行）没有这一位。
            const int column = MainWindowUI::versionTreeKeyColumn();
            const QString kindKey =
                item->data(column, MainWindowUI::resourceKindRole()).toString();
            if (kindKey.isEmpty()) {
                continue;
            }
            // 箭头那一格**交回去**（展开 / 收起还是它）：判据和代理画它用的是同一个函数，
            // 两边各算一份的话迟早错开，而错开的形状是"看着点的是箭头、其实进了一页"。
            if (VersionTreeDelegate::arrowRect(tree->visualItemRect(item)).contains(pos)) {
                continue;
            }
            if (event->type() == QEvent::MouseButtonPress) {
                // 按下就动手（松开和那几发双击只负责被吃掉）—— 和实例行上那三颗一个规矩。
                const QString iconKey =
                    item->data(column, MainWindowUI::resourceIconIdRole()).toString();
                // **排到下一轮事件循环**再开页：`execPage` 里面要转起一个嵌套事件循环，
                // 而这一发还在视图的事件处理里（列表那一侧的状态机悬在半路）。这里传的是
                // 两个字符串**值**，不是条目指针 —— 下一轮里树可能已经被重搜清空了。
                //
                // ★ 来路钉死是 `Download`（2026-09-27）：这条路上这一页**永远**摆「显示
                //   快照版」，哪怕点的是一个模组（用户原话「不要不小心把下载页改了」）。
                //
                // ★ 2026-09-28：把"当前实例"一起带过去（空 = 不是从某个实例进的下载页）——
                //   从实例设置点「下载新模组」进来的那一次，用户是在给**那个实例**找模组，
                //   开页时按它的版本预选、把它的加载器排前面（见 openResourceDetailPage）。
                //   抓的是**此刻**这一位的值（下一轮里它可能已经变了）。
                const QString instanceId = m_downloadFromInstanceId;
                QTimer::singleShot(0, this, [this, iconKey, kindKey, instanceId] {
                    // ★ 2026-10-18（coding plan §1.4 那两处之二）：从前覆盖层被别的页占着
                    //   时（典型场面：实例设置那一页**收着**、用户切到下载页逛），下面
                    //   `openResourceDetailPage()` 开头那条 `isOpen()` 会把这一下当场吃掉
                    //   —— 用户点了行身，屏幕上一个反应都没有。现在先让位：静默收掉占着的
                    //   那一层，再开（让位那一段的唯一出处是 `runWhenOverlayFree()`）。
                    runWhenOverlayFree([this, iconKey, kindKey, instanceId] {
                        openResourceDetailPage(iconKey, kindKey, DetailSource::Download, instanceId);
                    });
                });
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::requestAllLoaderVersions() {
    // 进安装页时把**四个加载器**的版本列表一次性全问出去（用户原话："获取列表不能点开了
    // 再获取，得全获取完"）。四条各占一格请求（见 VersionManager 里 m_loaderVersionRequests
    // 那段），所以四条真能同时在飞。
    //
    // 代价是进一次安装页约 500 KB（Forge 那份 maven 清单自己就 211 KB）—— 换来的是
    // "点开哪一条都立刻有东西看"，不再有"点开 → 等一下 → 才出列表"那一下。
    if (m_installGameVersion.isEmpty()) {
        return;  // 没定下来装哪个游戏版本，问不出"这个版本上有哪些加载器版本"
    }
    for (int i = 0; i < MainWindowUI::modLoaderOptionTexts().size(); ++i) {
        VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
        if (!VersionManager::loaderKindFromIndex(i, &kind)) {
            continue;  // 「不装（纯原版）」那一条：原版没有加载器版本可问
        }
        QTreeWidgetItem *item = loaderItem(i);
        if (item == nullptr) {
            continue;
        }
        // 先摆一条灰的"正在获取…"占住那一条底下的位置：空着一条看着像"这个加载器一个
        // 版本都没有"。回复在 onLoaderVersionsFetched() 里收，按"加载器 + 游戏版本"认领。
        item->setExpanded(false);
        qDeleteAll(item->takeChildren());
        auto *waiting = new QTreeWidgetItem(item);
        waiting->setText(MainWindowUI::loaderTreeKeyColumn(),
                         MainWindowUI::loaderVersionFetchingText());
        waiting->setDisabled(true);  // 灰的、点不动：它不是能选的东西（选了也不该有反应）
        m_versionManager->requestLoaderVersions(m_installGameVersion, kind);
    }
}

void MainWindow::fillLoaderVersionItems(int loaderIndex,
                                        const LoaderMeta::LoaderVersionList &versions,
                                        bool includeChannels) {
    QTreeWidgetItem *item = loaderItem(loaderIndex);
    if (item == nullptr) {
        return;
    }
    // 别动用户已经选中的那一条：列表可能刚好是"同一条"（缓存命中的那次会走到这里），
    // 重灌一次把它顶回第 0 条就是"我选的 47.4.10 自己变回最新版了"。选中的版本号在
    // m_chosenLoaderVersion 里，所以重灌只是把条目重建一遍、再把光标放回去。
    const QString chosen = pinnedLoaderVersion(loaderIndex);
    const bool wasExpanded = item->isExpanded();
    // 连"正在获取…"那条一起清掉：它就是等这一份列表用的。
    qDeleteAll(item->takeChildren());
    // 这一版上有没有正式版 —— 稳定那一行灰不灰就看它。**列表为空时一律当作"不知道"**：
    // 那时候这份列表根本没取到（下面 includeChannels 为假的那一支是"问到了、就是没有"，
    // 连这两行都不摆），而"让安装器自己挑"那条路不依赖这份列表，所以不该把用户堵死。
    bool hasStable = false;
    for (const LoaderMeta::LoaderVersionEntry &entry : versions) {
        hasStable = hasStable || entry.stable;
    }
    // 头两条 = 那两档（见 MainWindowUI::loaderVersionLatestText 上面那段）。它们的 userData
    // 是 LoaderMeta::loaderChannelKey() 那两个键，再往下才是具体版本号。
    //
    // includeChannels 为假 = 这一版**一个能装的都没有**（versions 也是空的）：连这两档
    // 都不留 —— 理由写在 onLoaderVersionsFetched() 那段（留着等于让用户挑一条必然装不
    // 出来的路）。调用方保证"includeChannels 假"只在这种情况出现。
    QTreeWidgetItem *latestItem = nullptr;
    QTreeWidgetItem *stableItem = nullptr;
    if (includeChannels) {
        const auto mark = [loaderIndex](QTreeWidgetItem *row) {
            row->setIcon(MainWindowUI::loaderTreeKeyColumn(),
                         QIcon(VersionIcons::loaderMark(
                             loaderIndex, MainWindowUI::versionTreeIconSize())));
        };
        latestItem = new QTreeWidgetItem(item);
        latestItem->setText(MainWindowUI::loaderTreeKeyColumn(),
                            MainWindowUI::loaderVersionLatestText());
        latestItem->setData(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole,
                            LoaderMeta::loaderChannelKey(LoaderMeta::LoaderChannel::Newest));
        mark(latestItem);

        stableItem = new QTreeWidgetItem(item);
        stableItem->setText(MainWindowUI::loaderTreeKeyColumn(),
                            MainWindowUI::loaderVersionStableText());
        stableItem->setData(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole,
                            LoaderMeta::loaderChannelKey(LoaderMeta::LoaderChannel::Stable));
        mark(stableItem);
        // **问到了、可一条正式版都没有**（NeoForge + 26.3 就是：11 个构建全是 `-beta`）：
        // 这一行灰掉、第二行小字把原因写出来。留着能点的它等于让用户挑一条必然装不出来的
        // 路 —— 装的时候会以"还没有支持 X 的版本"收场。
        if (!versions.isEmpty() && !hasStable) {
            stableItem->setDisabled(true);
            stableItem->setData(MainWindowUI::loaderTreeKeyColumn(),
                                MainWindowUI::rowSubTextRole(),
                                MainWindowUI::loaderVersionStableMissingText());
        }
    }
    QTreeWidgetItem *chosenItem = nullptr;
    for (const LoaderMeta::LoaderVersionEntry &entry : versions) {
        const QString &version = entry.version;
        auto *child = new QTreeWidgetItem(item);
        child->setText(MainWindowUI::loaderTreeKeyColumn(), version);
        child->setData(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole, version);
        // 子条目前面挂**它自己那个加载器**的标志（那一列列出来的是"Fabric 能装哪些版本"，
        // 前面顶个草方块会让人以为是原版号）。
        child->setIcon(MainWindowUI::loaderTreeKeyColumn(),
                       QIcon(VersionIcons::loaderMark(loaderIndex,
                                                      MainWindowUI::versionTreeIconSize())));
        // 不是正式版的那几条：第二行小字标出「测试版」。**照列、也能装**（用户原话：
        // "beta 版虽然不稳定，但是也能使用"）—— 这一行小字是"它是什么"，不是"它不行"。
        if (!entry.stable) {
            child->setData(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowSubTextRole(),
                           MainWindowUI::loaderVersionBetaSubText());
        }
        if (version == chosen) {
            chosenItem = child;
        }
    }
    // 光标放回"现在钉着的那一条"—— 展开之后一眼看到的就是它。没挑过（空串）时摆在
    // **默认会走的那一档**上：稳定档（它灰了就是最新档），和安装那条路的默认值一致。
    // **这不改选中状态**：选中是 m_selectedLoaderIndex 的事，这里只是画光标。
    if (chosenItem == nullptr) {
        LoaderMeta::LoaderChannel channel;
        if (LoaderMeta::loaderChannelFromKey(chosen, &channel)) {
            chosenItem = channel == LoaderMeta::LoaderChannel::Newest ? latestItem : stableItem;
        } else {
            // 没挑过、或者钉的那条这次没列出来（换了游戏版本那种）：退回默认那一档。
            chosenItem = (stableItem != nullptr && !stableItem->isDisabled()) ? stableItem
                                                                             : latestItem;
        }
    }
    //
    // ★ 这一下**只能在那一行开着的时候做**（2026-09-23 用户报的 bug 就在这儿）。
    //   链路：`QTreeWidget::setCurrentItem()` → currentChanged → QAbstractItemView 的
    //   `if (d->autoScroll) scrollTo(newCurrent)` → `QTreeView::scrollTo()` 头一句就是
    //   "Expand all parents if the parent(s) of the node are not expanded"（qtreeview.cpp
    //   里那几行，实测过：把 setCurrentItem 摘掉，那个"点不开了"当场就好了）。
    //   于是"把光标放到一条收着的加载器的子条目上" = **把那一条撑开**。用户看到的是：
    //   点开 Fabric → 选一条版本号 → 再点 Fabric，它闪一下就不动了（撑开它的那一下
    //   正好把点击处理里那句"取反"吃掉了）。
    //   收着的时候不放也没损失 —— 这几个版本号就写在那一行自己的第二行小字上
    //   （"选择版本以后收缩，显示版本"），不靠光标指。
    QTreeWidget *tree = m_ui->getLoaderTree();
    // chosenItem 可能是空指针（"这一版一个能装的都没有"那次：连那两档都没有，底下只剩一条
    // 灰的）。那种情况下这一列本来就没有"现在钉着的那一条"可指 —— 什么都不做。
    if (tree != nullptr && wasExpanded && chosenItem != nullptr) {
        tree->setCurrentItem(chosenItem);
    }
    // 列表是灌在**已经展开**的那一条底下的：条目换了一批，那棵树的高度得跟着重算
    // （MainWindowUI 那边挂在 expanded/collapsed 上，可这一批是原地换的、信号不会响）。
    // 收起来的那条不用管：它在展开的那一刻会自己算一次。
    if (wasExpanded) {
        item->setExpanded(false);
        item->setExpanded(true);
    }
}

void MainWindow::onLoaderVersionsFetched(const QString &gameVersion, int loaderIndex, bool success,
                                         const LoaderMeta::LoaderVersionList &versions,
                                         const QString &errorMessage) {
    // 这一发回复可能已经过时了（用户换了游戏版本）。按"这条回复讲的是不是现在这一页"来判，
    // 过时的直接丢 —— 填错地方的下场是"点了 Fabric 却看到 Forge 的版本号"，那不报错、
    // 只会装错东西。
    if (gameVersion != m_installGameVersion) {
        return;
    }
    // 那棵树是按**下标**摆的，日志里那句"是哪个加载器"要的是名字，所以这里翻译回枚举；
    // 反函数在这边失手（表没改全）就当没收到 —— 宁可那一条底下空着，也不要填错。
    VersionManager::LoaderKind kind;
    if (!VersionManager::loaderKindFromIndex(loaderIndex, &kind)) {
        return;
    }
    QTreeWidgetItem *item = loaderItem(loaderIndex);
    if (item == nullptr) {
        return;
    }
    if (!success) {
        // 拉不到：那一条底下**只留那两档**，外加一条灰的「列表没取到」把话说到 ——
        // 不钉版本这条路是不需要这份列表的（安装器自己会挑，问的是另一份东西：
        // promotions / maven 清单，见 installLoaderVersion 那条路），所以"列不出来"不该把
        // 安装整件事堵死，但也不能一声不吭。
        // 两档都**不灰**：这一刻我们根本不知道有没有正式版（那份列表没到手），而"让安装器
        // 自己挑"这条路由数据层去挑、挑不出来会在安装那条路上说清楚。
        // 具体原因（超时？404？）写进日志 —— 那是给排查用的，不是给用户读的。
        fillLoaderVersionItems(loaderIndex, LoaderMeta::LoaderVersionList(),
                               /*includeChannels=*/true);
        auto *failed = new QTreeWidgetItem(item);
        failed->setText(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::loaderVersionFailedText());
        failed->setDisabled(true);
        appendLog(tr("%1 在 %2 上有哪些版本没取到：%3")
                      .arg(VersionManager::loaderDisplayName(kind), gameVersion, errorMessage));
        return;
    }
    if (versions.isEmpty()) {
        // **问到了、就是没有**（四种加载器上都可能：Forge 还没支持某个刚发的快照）。
        // 这时候底下**不留那两档**（用户原话："如果对应版本不支持，就不要保留自动了"）：
        // 留着它等于让用户挑一条必然装不出来的路 —— 安装那条路会以"X 还没有支持 Y 的版本"
        // 收场，那一下本来在这一眼就该说完（用户原话："这个情况下的 列表没取到 换成无可用
        // 版本"）。
        //
        // NOTE: 2026-09-23 起"**有预览版、没有正式版**"的那种版本**不算**这一支 ——
        //       用户原话："beta 版虽然不稳定，但是也能使用"，所以那一份列表非空、照列，
        //       只是稳定那一行是灰的。
        // 于是这条加载器**一个能点的子条目都没有**，也就选不中它 —— 这正是"这一条我装不了"
        // 该有的样子；底下那条灰的「无可用版本」只是把原因写出来。
        fillLoaderVersionItems(loaderIndex, LoaderMeta::LoaderVersionList(),
                               /*includeChannels=*/false);
        auto *none = new QTreeWidgetItem(item);
        none->setText(MainWindowUI::loaderTreeKeyColumn(),
                      MainWindowUI::loaderVersionUnavailableText());
        none->setDisabled(true);
        appendLog(tr("%1 还没有支持 %2 的版本。")
                      .arg(VersionManager::loaderDisplayName(kind), gameVersion));
        return;
    }
    // 这一版上一条正式版都没有（真实例子：NeoForge + 26.3，11 个构建全是 `-beta`）：
    // 用户还什么都没挑的时候，把默认那一档**挪到「最新版」**上 —— 不然什么都没点、点安装，
    // 走的是稳定档那条必然装不出来的路（那一行同时也是灰的，见 fillLoaderVersionItems）。
    //
    // NOTE: 只在他**没挑过**的时候改（空串）。他已经挑过的那一格不许动 —— 那是他自己的
    //       选择，哪怕挑的正是一条现在没列出来的版本号。
    if (pinnedLoaderVersion(loaderIndex).isEmpty()) {
        bool hasStable = false;
        for (const LoaderMeta::LoaderVersionEntry &entry : versions) {
            hasStable = hasStable || entry.stable;
        }
        if (!hasStable) {
            m_chosenLoaderVersion.insert(
                loaderIndex, LoaderMeta::loaderChannelKey(LoaderMeta::LoaderChannel::Newest));
        }
    }
    // 灌进去就完了 —— 不存缓存：这一批是进安装页时一次性问全的（requestAllLoaderVersions），
    // 每一条在这次安装页里只会被灌这一次。
    fillLoaderVersionItems(loaderIndex, versions, /*includeChannels=*/true);
}

// ---------------------------------------------------------------------------
// 下载页后面那几页：搜索 → 挑游戏版本 → 挑版本号 → 装进实例
// ---------------------------------------------------------------------------
//
// 这几页（整合包 / 模组 / 数据包 / 资源包 / 光影）走的是**同一条路**，区别只有"搜哪一类"
// 和"下到哪儿去"。
// 三层树里每一层都是用户点开才动：
//   第一层 资源   —— 进页面时空搜索词问一次（= 这一类里最热门的），回车重搜
//   第二层 游戏版本 —— 搜索响应里本来就带着这个资源支持哪些游戏版本，**不用再问接口**
//   第三层 版本号  —— 点开第二层那一下才问（一个资源 × 一个游戏版本 = 一条请求）
// 点第三层那一下 = 下它（下到实例的 mods/ 、datapacks/ 之类；整合包那条走缓存 + 导入）。
//
// 三层都是**平条目**（不是自绘控件），用的是版本树/加载器树那同一个代理，所以长得一样
// （见 VersionTreeDelegate 第三个用户那段）。

/// 「这一条在第几层」。0 = 资源 / 1 = 游戏版本 / 2 = 版本号；不在树上回 -1。
///
/// NOTE: 代理那边认层用的是"有没有爹"（isChannel），这边认层用的是"往上数几层" ——
///       两处必须说的是同一件事，而"往上数几层"这句话在这里写一次（见头文件那段）。
int MainWindow::resourceRowDepth(const QTreeWidgetItem *item) {
    if (item == nullptr) {
        return -1;
    }
    int depth = 0;
    const QTreeWidgetItem *walk = item;
    while (walk->parent() != nullptr) {
        walk = walk->parent();
        ++depth;
    }
    return depth;
}

QTreeWidgetItem *MainWindow::addResourcePlaceholderRow(QTreeWidget *tree, const QString &text) {
    if (tree == nullptr) {
        return nullptr;
    }
    auto *item = new QTreeWidgetItem(tree);
    item->setText(MainWindowUI::versionTreeKeyColumn(), text);
    // 灰的（禁用）—— 和加载器树里「正在获取…」「列表没取到」那两行是同一种样子、
    // 也是同一种意思："这一行不是能点的东西"。判据见 onResourceItemActivated 开头。
    // NOTE: 不用 setFlags(Qt::ItemIsEnabled) 那一套"能看不能选"：代理是看 enabled 位
    //       整行淡 50% 的（见 VersionTreeDelegate 里 rowOpacity），禁用位才是"灰"的唯一出处。
    item->setDisabled(true);
    return item;
}

QTreeWidgetItem *MainWindow::addResourcePlaceholderChild(QTreeWidgetItem *parent,
                                                        const QString &text) {
    if (parent == nullptr) {
        return nullptr;
    }
    auto *item = new QTreeWidgetItem(parent);
    item->setText(MainWindowUI::versionTreeKeyColumn(), text);
    item->setDisabled(true);
    return item;
}

bool MainWindow::kindForSearchRequestKey(const QString &key, ResourceCatalog::Kind *out,
                                         ResourceCatalog::Source *outSource) const {
    // 拼钥匙和认钥匙读的是同一个函数（searchRequestKey）—— 所以这里不解析字符串，
    // 一条一条比。有几类，比几次。
    //
    // NOTE: **来源也要比一圈**（2026-09-25）。钥匙里带来源（见 searchRequestKey 那段），
    //       只比"默认那个来源"的话，CurseForge 那一发回来时一把都对不上 —— 症状是
    //       "切到 CurseForge 按了搜索，那一页什么都没有、也不报错"（结果被当成"不是我们
    //       发的"整发丢掉）。
    for (ResourceCatalog::Kind kind : ResourceCatalog::allKinds()) {
        for (ResourceCatalog::Source source : ResourceCatalog::allSources()) {
            if (ResourceCatalog::searchRequestKey(kind, source) == key) {
                if (out != nullptr) {
                    *out = kind;
                }
                if (outSource != nullptr) {
                    *outSource = source;
                }
                return true;
            }
        }
    }
    return false;
}

void MainWindow::ensureResourcePageLoaded(ResourceCatalog::Kind kind) {
    QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind));
    if (tree == nullptr) {
        return;
    }
    if (tree->topLevelItemCount() > 0) {
        // 这一页已经有东西了（用户搜过、或者刚才进来时搜了一次）。**不重搜**：来回换分类
        // 每次都问一遍，既慢、又是在拿限流额度换同一份东西。
        return;
    }
    startResourceSearch(kind);
}

void MainWindow::refreshDownloadCategory(int row) {
    const QStringList keys = MainWindowUI::downloadCategoryKeys();
    if (row < 0 || row >= keys.size()) {
        // 过滤器交上来的行号理论上一定在表里（那一行是 MainWindowUI 按这张表建出来的），
        // 越界只可能是"表和列表对不上了"那种时候 —— 什么都不做比按错一页去做强。
        return;
    }
    if (row == MainWindowUI::downloadCategoryServerIndex()) {
        // 「服务端」那一栏**没有"重新问一遍"这回事**（2026-10-18）：它不向任何资源站
        // 问东西，那一页只有一句说明 + 一颗「新建服务端…」。界面上那颗行尾刷新和页头
        // 刷新在这一栏都已经不露了（见 MainWindowUI），这里再挡一道是因为
        // `refreshDownloadCategory()` 还有别的调用点（键盘/测试），不能靠"用户点不到"
        // 当保证。
        return;
    }
    // 点的那一类要是现在没选中的，先切过去 —— 不切的话请求飞出去了、结果落在看不见的
    // 那一页上，用户看到的就是"点了没反应"。这一条对**五条一视同仁**（「游戏」也是：
    // 它刷新的是版本树，那棵树就长在它自己那一页上）。
    //
    // NOTE: 切过去那一下会触发 currentRowChanged → ensureResourcePageLoaded()，而那一页
    //       要是还空着，那儿**自己就会搜一次**。所以下面分两半：切过去的路上那一发已经
    //       发出去了就直接完事（再发一发是同一份东西问两遍，白烧一次限流额度）；页面上
    //       已经有东西了才轮到我们按现在的搜索词重搜一次 —— 那才是用户按这个按钮要的
    //       那件事：**重新问一遍**，而不是"第一次问"。
    QListWidget *categories = m_ui->getDownloadCategoryList();
    const bool wasCurrent = categories == nullptr || categories->currentRow() == row;
    if (!wasCurrent) {
        categories->setCurrentRow(row);
    }
    if (row == MainWindowUI::downloadCategoryGameIndex()) {
        // 「游戏」那一页是**在线版本清单**，不是资源搜索（那张表上根本没有"游戏"这个键，
        // kindFromCategoryKey 也认不出来，所以它得自己一条分支）。
        //
        // 摆不摆"正在…"那一行：**不摆**。版本树那边和资源表不是一回事 ——
        // refreshAvailableVersions() 每一发都重建**恰好那几条顶层**（「最新版本」那一组 +
        // 四条分类，那是它的活），中途插一行灰的会被它整个冲掉；而且版本树本来就不会因为
        // "正在等"而空着：老那棵一直挂在
        // 那儿（fetchAvailableVersions 失败时也保留上一份好清单，见 onVersionsFetched）。
        // 用户看到的是"点了一下、树没消失、过一会儿多的版本冒出来"，正是要的样子。
        appendLog(tr("正在重新获取游戏版本列表…"));
        m_versionManager->fetchAvailableVersions();
        return;
    }
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    if (!ResourceCatalog::kindFromCategoryKey(keys.at(row), &kind)) {
        return;  // 认不出来 = 这一行不是我们建的那几类之一，没有"重问一遍"这回事
    }
    if (!wasCurrent) {
        QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind));
        if (tree != nullptr && tree->topLevelItemCount() > 0) {
            startResourceSearch(kind);
        }
        return;
    }
    // 已经在这一页上：按搜索框里现在那个词重搜（空词 = 这一类里最热门的，和第一次进来同一个规矩）。
    // NOTE: **图标缓存一个字节都不动**。这一发重新走的是 search()，回来的那批 hit 里
    //       project_id 和上次多半是同一批，requestIcon() 在内存/磁盘缓存里就命中了
    //       （见 ResourceStore 里 requestIcon 那段"两级缓存"）—— 用户要的"图标留缓存"正是
    //       这个：重新问一遍**不会**把已经存下来的图标丢掉、也不会重新下一遍。
    //       这一下**也不重新核对**（核对的闸是"启动器这一趟运行"一道，重搜不重置它，
    //       见 m_iconRevalidated）—— 核对要跟着"每次打开启动器"，不能跟着"每点一下"。
    //       唯一的例外是"这一批刚才放弃了"：那阵子没核成的会在这里得到第二次机会
    //       （search() 会把那道闸放回去，见它开头那两句）。
    startResourceSearch(kind);
}

// ---------- 每一页顶上那一行筛子（2026-09-25）----------
//
// 这一段的整段理由在头文件里那段：界面只搭空框，**内容全在这儿灌**，读也只从下面这几个
// 访问器读。谁要是绕开它们自己去 `combo->currentText()`，症状是"界面上筛了、请求里没筛"
// —— 不报错、也看不出哪儿不对。

ResourceCatalog::Source MainWindow::downloadFilterSource(ResourceCatalog::Kind kind) const {
    ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
    QComboBox *combo = m_ui->getDownloadFilterSourceCombo(ResourceCatalog::kindKey(kind));
    if (combo != nullptr) {
        // 认不出来（框是空的 / 里面不是我们灌的那个键）就停在 Modrinth —— 和"这一页
        // 没有来源框"是同一个结果，也正是加这一栏之前的行为。
        ResourceCatalog::sourceFromKey(combo->currentData().toString(), &source);
    }
    return source;
}

QString MainWindow::downloadFilterGameVersion(ResourceCatalog::Kind kind) const {
    // **读框里的字**，不是 currentData()。这一格是可输入的（用户原话「版本可自行输入」），
    // 手打一个版本号时列表里没有那一项、currentData() 是空的 —— 而框里明明写着那个版本号。
    QComboBox *combo = m_ui->getDownloadFilterVersionCombo(ResourceCatalog::kindKey(kind));
    return combo == nullptr ? QString() : combo->currentText().trimmed();
}

QString MainWindow::downloadFilterLoader(ResourceCatalog::Kind kind) const {
    // 别的页上这个控件**不存在**（MainWindowUI 里是"只有模组页才建"，不是建了再藏），
    // 所以空指针就是"这一页没有这一栏"。回空串 = 不筛，正是这一栏没有时该有的行为。
    QComboBox *combo = m_ui->getDownloadFilterLoaderCombo(ResourceCatalog::kindKey(kind));
    return combo == nullptr ? QString() : combo->currentData().toString();
}

QString MainWindow::downloadFilterCategory(ResourceCatalog::Kind kind) const {
    QComboBox *combo = m_ui->getDownloadFilterTypeCombo(ResourceCatalog::kindKey(kind));
    return combo == nullptr ? QString() : combo->currentData().toString();
}

void MainWindow::fillDownloadFilterVersionOptions() {
    // 版本那一格：**一整份在线清单**，没有「全部」那一项 —— 这一格是可输入的，
    // "不筛"就是框里空着（那时框里显示灰字「全部版本」，见 downloadFilterVersionPlaceholder）。
    const QStringList versions = m_versionManager->availableVersions();
    for (ResourceCatalog::Kind kind : ResourceCatalog::allKinds()) {
        QComboBox *combo = m_ui->getDownloadFilterVersionCombo(ResourceCatalog::kindKey(kind));
        if (combo == nullptr) {
            continue;
        }
        // 灌之前把用户手里那串字记下来、灌完放回去。清单是**异步**回来的（开机一次、
        // 用户按左栏那个刷新按钮又一次），而 clear() 会把框里编辑着的东西一起清掉：
        // 他刚打完版本号、清单正好回来，那串字就没了，可筛子还在生效 —— 界面和请求对不上。
        const QString current = combo->currentText();
        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItems(versions);
        if (!current.isEmpty()) {
            combo->setCurrentText(current);
        }
    }
}

void MainWindow::refreshDownloadFilterOptions(ResourceCatalog::Kind kind) {
    const QString key = ResourceCatalog::kindKey(kind);
    const ResourceCatalog::Source source = downloadFilterSource(kind);

    // 「模组加载器」那一栏（**只有模组页有**，别的页 getDownloadFilterLoaderCombo 回空指针）。
    if (QComboBox *loaderCombo = m_ui->getDownloadFilterLoaderCombo(key)) {
        const QSignalBlocker blocker(loaderCombo);
        loaderCombo->clear();
        for (const ResourceCatalog::FilterOption &option : ResourceCatalog::loaderOptions(source)) {
            loaderCombo->addItem(option.label, option.key);
        }
        // 换来源之后退回「全部」（= 第一项）。两边的键根本不是一套（Modrinth 的 `fabric`
        // 对 CurseForge 的 `4`），留着上一个来源选中那一项，就是拿 A 家的号去问 B 家 ——
        // 而且它不报错，只会搜出来一片空。
        loaderCombo->setCurrentIndex(0);
    }

    // 「类型」那一栏。**回空表就把整个小组藏起来**（连标签一起，见 MainWindowUI 里那段）：
    // 摆一个只有「全部」的框等于骗人。现在只有 Modrinth 的数据包会空。
    QComboBox *typeCombo = m_ui->getDownloadFilterTypeCombo(key);
    QWidget *typeBox = m_ui->getDownloadFilterTypeBox(key);
    if (typeCombo != nullptr) {
        const QSignalBlocker blocker(typeCombo);
        typeCombo->clear();
        // 「全部」那一项是 categoryOptions() 自己带的第一项，这里不补也不动顺序。
        for (const ResourceCatalog::FilterOption &option :
             ResourceCatalog::categoryOptions(source, kind)) {
            typeCombo->addItem(option.label, option.key);
        }
        typeCombo->setCurrentIndex(typeCombo->count() > 0 ? 0 : -1);
    }
    if (typeBox != nullptr) {
        typeBox->setVisible(typeCombo != nullptr && typeCombo->count() > 0);
    }
}

void MainWindow::fillDownloadFilters() {
    for (ResourceCatalog::Kind kind : ResourceCatalog::allKinds()) {
        const QString key = ResourceCatalog::kindKey(kind);
        // 来源那一格：两项，顺序照 allSources()（Modrinth 在前：它是不要 key 的那个、
        // 也是"原来就能用"的那个）。**没有「全部」这一项** —— 一个页面上同时只搜一个
        // 来源，那是这一格的语义。
        if (QComboBox *sourceCombo = m_ui->getDownloadFilterSourceCombo(key)) {
            const QSignalBlocker blocker(sourceCombo);
            sourceCombo->clear();
            for (ResourceCatalog::Source source : ResourceCatalog::allSources()) {
                sourceCombo->addItem(ResourceCatalog::sourceLabel(source),
                                     ResourceCatalog::sourceKey(source));
            }
            sourceCombo->setCurrentIndex(0);
        }
        // 加载器 / 类型那两项跟着来源走，所以灌法是"按当前来源算一遍"。
        refreshDownloadFilterOptions(kind);
    }
    fillDownloadFilterVersionOptions();
}

void MainWindow::resetDownloadFilters(ResourceCatalog::Kind kind) {
    const QString key = ResourceCatalog::kindKey(kind);
    // 名称那一格：就是原来那个搜索框。
    if (QLineEdit *edit = m_ui->getDownloadSearchEdit(key)) {
        edit->clear();
    }
    // 版本那一格是可输入的，`setCurrentIndex(-1)` 不一定把框里的字清掉（值是从
    // currentText() 读的），所以**显式**再清一遍那串字。
    if (QComboBox *versionCombo = m_ui->getDownloadFilterVersionCombo(key)) {
        const QSignalBlocker blocker(versionCombo);
        versionCombo->setCurrentIndex(-1);
        versionCombo->clearEditText();
    }
    // 加载器和类型那两格：第一项就是「全部」（键空串），回到它 = 不筛。
    if (QComboBox *loaderCombo = m_ui->getDownloadFilterLoaderCombo(key)) {
        const QSignalBlocker blocker(loaderCombo);
        loaderCombo->setCurrentIndex(loaderCombo->count() > 0 ? 0 : -1);
    }
    if (QComboBox *typeCombo = m_ui->getDownloadFilterTypeCombo(key)) {
        const QSignalBlocker blocker(typeCombo);
        typeCombo->setCurrentIndex(typeCombo->count() > 0 ? 0 : -1);
    }
    // 「来源」**不清**：它不是"条件"，是"去哪儿问"。清成"没有来源"这件事不存在
    // （这一格只有两项、总有一项是选中的），而且用户刚在设置里配好的 CurseForge key
    // 就是给这一格用的 —— 清一下把他踢回 Modrinth，那是替他做了个他没说的决定。
    //
    // NOTE: 这一层**只清**，搜不搜是调用方的事 —— 那颗键上接着"清完重搜一发"（2026-09-26
    //       用户原话「修改任何内容…都刷新一下」，接线见构造函数里那段）。
    appendLog(tr("已清空%1那一页的搜索条件。").arg(ResourceCatalog::kindLabel(kind)));
}

void MainWindow::startResourceSearch(ResourceCatalog::Kind kind) {
    const QString key = ResourceCatalog::kindKey(kind);
    QTreeWidget *tree = m_ui->getDownloadResourceTree(key);
    if (tree == nullptr) {
        return;
    }
    const QLineEdit *edit = m_ui->getDownloadSearchEdit(key);
    const QString query = edit == nullptr ? QString() : edit->text().trimmed();

    // 上一批结果连同展开状态一起清掉，换上一行灰的「正在…」。不摆这一行的话，用户按回车
    // 之后界面**完全没有反应**（请求一秒上下才回来），而"点了没反应"是用户唯一会当成
    // 启动器坏了的情况。
    tree->clear();
    addResourcePlaceholderRow(tree,
                             query.isEmpty()
                                 ? tr("正在获取最热门的%1…").arg(ResourceCatalog::kindLabel(kind))
                                 : tr("正在搜索「%1」…").arg(query));
    // 记下**这一发真的问了什么**：回来时那句「没有搜到「xx」」里的 xx 要是那一次的词。
    // 读编辑框里现在的字是错的 —— 用户完全可能在等结果的时候又敲了几个字（那几下还没发出去，
    // 回车才算一发）。
    m_resourceQueries.insert(key, query);

    // 另外四个筛子（版本 / 加载器 / 类型 / 来源）在这儿一次读完。读法全在 downloadFilterXxx()
    // 那四个访问器里（**唯一出处**），这里只负责把它们递给 store —— 空串一律 = 不筛，
    // 所以"没选"和"没有这一栏"在这里是同一件事，不用分岔。
    const ResourceCatalog::Source source = downloadFilterSource(kind);
    const QString gameVersion = downloadFilterGameVersion(kind);
    const QString loader = downloadFilterLoader(kind);
    const QString category = downloadFilterCategory(kind);

    appendLog(query.isEmpty() ? tr("正在获取最热门的%1…").arg(ResourceCatalog::kindLabel(kind))
                              : tr("正在搜索%1「%2」…")
                                    .arg(ResourceCatalog::kindLabel(kind), query));
    // 日志里把这一发**带了哪些条件**说全（那两格的值写的是框里显示的字，不是键 ——
    // CurseForge 的类型键是 `6814` 这种号，写进日志没人看得懂）。搜出来"怎么是这么几条"，
    // 十有八九就是某个筛子在起作用；不写下来的话只能对着结果猜。
    QStringList conditions;
    conditions.append(tr("来源 %1").arg(ResourceCatalog::sourceLabel(source)));
    if (!gameVersion.isEmpty()) {
        conditions.append(tr("版本 %1").arg(gameVersion));
    }
    const auto shownText = [this, &key](QComboBox *combo) {
        return combo == nullptr ? QString() : combo->currentText();
    };
    if (!loader.isEmpty()) {
        conditions.append(
            tr("模组加载器 %1").arg(shownText(m_ui->getDownloadFilterLoaderCombo(key))));
    }
    if (!category.isEmpty()) {
        conditions.append(tr("类型 %1").arg(shownText(m_ui->getDownloadFilterTypeCombo(key))));
    }
    appendLog(tr("搜索条件：%1").arg(conditions.join(QStringLiteral(" · "))));

    // 钥匙里要带来源（见 ResourceCatalog::searchRequestKey 那段）：用户刚点完 Modrinth 又
    // 切成 CurseForge 时，前一趟回来的结果不该被当成"我现在等的那一发"填进树里。
    m_resourceStore->search(ResourceCatalog::searchRequestKey(kind, source), kind, source, query,
                            gameVersion, loader, category);
}

void MainWindow::fillResourceHitRows(QTreeWidget *tree, ResourceCatalog::Kind kind,
                                     const ResourceCatalog::SearchResult &result) {
    if (tree == nullptr) {
        return;
    }
    const int column = MainWindowUI::versionTreeKeyColumn();
    for (const ResourceCatalog::Hit &hit : result.hits) {
        auto *item = new QTreeWidgetItem(tree);
        // 身份 = project_id：图标回来时按它在树里找回这一行、点开游戏版本时按它拼版本请求。
        item->setData(column, Qt::UserRole, hit.projectId);
        // 这一条是哪一类。点最底下那一层要按它算"下到哪儿去"（见 resourceKindRole()）——
        // 存的是**分类键**（不是枚举的整数值），回来时走 kindFromCategoryKey 认，
        // 认不出来就是"这一行不是我们建的"。
        item->setData(column, MainWindowUI::resourceKindRole(), ResourceCatalog::kindKey(kind));
        // 这一条是从哪个来源搜出来的（点第二层要问对那一侧，见 resourceSourceRole()）。
        item->setData(column, MainWindowUI::resourceSourceRole(),
                      ResourceCatalog::sourceKey(hit.source));
        // 图标那一格的**身份**（带来源前缀，见 resourceIconIdRole()）：图标回来时按它认行。
        item->setData(column, MainWindowUI::resourceIconIdRole(), hit.iconCacheId());
        item->setText(column, hit.title);

        // 第二行小字：「作者 · 下载量」。下载量那串字只有一处出处（formatDownloads），
        // 界面上别处显示下载量也要读它。作者缺了就少一段。
        QStringList bits;
        if (!hit.author.isEmpty()) {
            bits.append(hit.author);
        }
        bits.append(ResourceCatalog::formatDownloads(hit.downloads));
        item->setData(column, MainWindowUI::rowSubTextRole(), bits.join(QStringLiteral(" · ")));

        // 简介一行放不下（代理只画一行、超了截断），鼠标停上去看全文。
        // 空简介不挂 tooltip —— 挂一个空的等于鼠标一停就冒一个空框。
        if (!hit.description.isEmpty()) {
            item->setToolTip(column, hit.description);
        }

        // 第二层：游戏版本。**不用再问接口** —— 搜索响应里本来就带着"这个资源支持哪些
        // 游戏版本"（Hit::gameVersions）。顺序由 gameVersionChoices() 统一成"新的在前"。
        // 第二层**一律折**（`false`）：那枚「显示快照版」勾选框在详情页顶上，这一棵树
        // 没有它 —— 用户要的是"快照版归到对应的大版本里"，这一层就是那句话的落点。
        const QStringList choices = ResourceCatalog::gameVersionChoices(hit.gameVersions, false);
        if (choices.isEmpty()) {
            // 一个游戏版本都没列（数据包那类）。摆一行灰的说清楚 —— 不然这一行看着能展开、
            // 点开却是空的。
            addResourcePlaceholderChild(item, tr("这个%1没有列出支持的游戏版本。")
                                                  .arg(ResourceCatalog::kindLabel(kind)));
        }
        for (const QString &gameVersion : choices) {
            auto *gameItem = new QTreeWidgetItem(item);
            // 身份 = 游戏版本号。第三层还不建（点开这一条才去问，见 onResourceItemActivated）。
            gameItem->setData(column, Qt::UserRole, gameVersion);
            gameItem->setText(column, gameVersion);
        }

        // 整条 hit 存一份（键就是上面那个图标身份，也是"这一行是哪个资源"的钥匙）。
        // 条目上那四个角色装不下简介 / slug / 更新时间，而点开详情页三样都要
        // （见 m_resourceHits 那段 NOTE）。
        m_resourceHits.insert(hit.iconCacheId(), hit);
        // 图标**最后**要：ResourceStore 命中内存缓存时是同步发 iconReady 的，那一行得先在
        // 那儿（见 requestIcon 的 NOTE）—— 所以这一句必须排在最后。
        // 收的是整条 hit 不是 id：要挑地址（能用 webp 就走那条小 7 倍的，见 requestIcon）。
        m_resourceStore->requestIcon(hit);
    }
}

void MainWindow::onResourceSearchFinished(const QString &requestKey,
                                         const ResourceCatalog::SearchResult &result) {
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    ResourceCatalog::Source fromSource = ResourceCatalog::Source::Modrinth;
    if (!kindForSearchRequestKey(requestKey, &kind, &fromSource)) {
        // 不是我们认得的那几页发的（别的东西也用这个 store 时才会走到）。
        return;
    }
    // 这一发回来的时候，用户可能已经把来源换成另一个了 —— 两个来源的请求是**各自飞的**
    // （钥匙不同，store 不会互相掐掉）。不是这一页**现在**选的那个来源就丢掉：留着会把
    // 上一家那批结果摆在一个写着另一家的页面上，而页面上那句"来源 CurseForge"就成了假话。
    if (fromSource != downloadFilterSource(kind)) {
        return;
    }
    const QString key = ResourceCatalog::kindKey(kind);
    QTreeWidget *tree = m_ui->getDownloadResourceTree(key);
    if (tree == nullptr) {
        return;
    }
    // 「正在搜索…」那一行先撤掉（下面每一条分支都会摆上自己那一行/那一批）。
    tree->clear();

    if (!result.ok) {
        // 「没搜成」和「没搜到」是**两句不同的话**（加载器树那边「无可用版本」和
        // 「列表没取到」踩过同一个坑，见记忆里那条）。这里说的是前一句。
        //
        // NOTE: 这里**不弹窗**。产品那条"错误要弹窗"管的是"必须让用户看见的东西不能只写
        //       日志"（日志面板早就没有了，写进去等于没写）；这一句**不是**那种：那一行就
        //       摆在用户刚点的那一页上、就是他正在看的地方，而且话是接口/网络自己说的
        //       （超时 / 连不上 / 还是"你问得太勤了，过一分钟再试" —— 最后那种正是最该
        //       看清、又最不该用一个模态框打断他的）。回车再按一次就是重试。
        //       这一条和加载器树那条「列表没取到」是同一种形状，所以做法也一样。
        //       真要弹窗的是那些**没有别的地方能显示**的错（见 onResourceDownloadFinished）。
        appendLog(tr("✗ 搜索%1失败：%2").arg(ResourceCatalog::kindLabel(kind), result.problem));
        addResourcePlaceholderRow(tree, result.problem);
        return;
    }
    if (result.hits.isEmpty()) {
        // **问到了、就是没有** —— 不算错，也**不弹窗**（一个"什么都没发生"的弹窗比没有
        // 更烦人）。用记下来的那个搜索词，别读编辑框里现在的字（理由见 startResourceSearch）。
        const QString query = m_resourceQueries.value(key);
        appendLog(tr("%1 一条也没有搜到（%2）。")
                      .arg(ResourceCatalog::kindLabel(kind), query.isEmpty() ? tr("空搜索词") : query));
        addResourcePlaceholderRow(tree, query.isEmpty()
                                            ? tr("这一类里一条都没有。")
                                            : tr("没有搜到「%1」。").arg(query));
        return;
    }
    fillResourceHitRows(tree, kind, result);
}

void MainWindow::onResourceIconReady(const QString &iconId, const QPixmap &icon) {
    // 没有图标（作者没传过 / 这次没取到 / 被限流）也**要**给这一行挂上东西：一个全透明的
    // 方块。为什么：代理是"条目给了图标，文字才让出前面那一格"的（见 VersionTreeDelegate
    // 里 iconRect 那段），什么都不挂的话这一行的字会顶到最左边，和上下那些有图标的行
    // 参差不齐 —— 而"这一列没对齐"在用户眼里是"这软件做得很糙"，不是"这个模组没图标"。
    // 全透明是为了不再多画一个"没有图标"的形状（那个形状本身也是要设计的东西）。
    // NOTE: **不是错误**、不弹窗：项目没上传过图标是完全正常的一件事。
    QPixmap mark = icon;
    if (mark.isNull()) {
        mark = QPixmap(MainWindowUI::resourceTreeIconSize(), MainWindowUI::resourceTreeIconSize());
        mark.fill(Qt::transparent);
    }
    const int column = MainWindowUI::versionTreeKeyColumn();
    // 按**图标身份**（`Hit::iconCacheId()`，见 resourceIconIdRole）找回**现在**树上那一行 ——
    // 不按裸 project_id：两个来源的 id 空间会撞，撞上就是把另一个来源同号那个资源的图标
    // 画到这一行上（而且不报错）。也不存条目指针：重搜会把整棵树清掉，存下来的指针就是
    // 悬空的 —— 按身份找，找得到才画，找不到就什么都不做。
    for (ResourceCatalog::Kind kind : ResourceCatalog::allKinds()) {
        QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind));
        if (tree == nullptr) {
            continue;
        }
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = tree->topLevelItem(i);
            if (item->data(column, MainWindowUI::resourceIconIdRole()).toString() != iconId) {
                continue;
            }
            item->setData(column, MainWindowUI::rowIconRole(), mark);
        }
    }
    // 详情页正开着、回来的又正好是它那一张 → 换上头卡那一格。
    //
    // NOTE: 判据是**图标身份**而不是"页开着"就换：图标是一条一条回来的，页上那一格只该
    //       认自己这一条（换个说法：这是同一套"按身份认行"的规矩，只不过这儿认的是那一页）。
    //       头卡那一格比列表大（64 vs 32），缩放由那一页自己做（见 setIcon）。
    if (m_resourcePage.open && iconId == m_resourcePage.iconKey
        && m_resourceDetailPage != nullptr) {
        m_resourceDetailPage->setIcon(icon);
    }

    // ---------- 「文件管理」那几页（2026-09-27）----------
    //
    // 同一个身份也可能是**实例里某个文件**换来的那一个（见 requestInstanceFileIcons）——
    // 这一格和上面那棵树是两回事（那边是"搜出来的资源"，这边是"盘上躺着的文件"），
    // 所以两条路都要走一遍，谁都不抢谁。
    //
    // ★ 这一句**必须排在 m_resourceStore->requestIcon() 之后**（也就是"图标回来时"）：
    //   盘上已经有缓存的话 requestIcon() 是**同步**发 iconReady 的（见它那条 NOTE），
    //   所以这里是"记了账、立刻就用上"，而记账那一步在 requestIcon 之前就写完了。
    m_instanceIconController->rememberResourceIcon(iconId, icon);
}

void MainWindow::onResourceItemActivated(QTreeWidgetItem *item) {
    if (item == nullptr) {
        return;
    }
    // 灰的那些（「正在搜索…」「没有搜到…」「正在获取…」「这个游戏版本上没有…」）不是能点的
    // 东西：点它什么都不该发生。放在最前面 —— "点不动"这件事对三层是同一个道理。
    if (item->isDisabled()) {
        return;
    }
    const int depth = resourceRowDepth(item);
    if (depth == 0 || depth == 1) {
        // 第一层（一个资源）和第二层（一个游戏版本）都是**只展开 / 收起** ——
        // 用户原话：「点开模组后是选择游戏版本」。"点开"是"看看有什么"，看一眼不等于选了它。
        const bool expand = !item->isExpanded();
        item->setExpanded(expand);
        if (depth != 1 || !expand || item->childCount() > 0) {
            return;
        }
        // 第二层**第一次**被点开：这时才去问"这个资源在这个游戏版本上，哪些版本可下"。
        // 判据是"底下一条都没有" —— 这一层点开之前一定是空的，问过之后无论有没有结果都会
        // 留下至少一行（版本号、或者一句灰的"没有"），所以"空"就等于"还没问过"。
        // 这也顺带成了"再点一次重试"那条路：没取到时底下留着一行灰的，把那一条删掉
        // （重搜/重新点开）就会再问一次。
        const QTreeWidgetItem *resourceItem = item->parent();
        const QString projectId = resourceItem == nullptr
                                      ? QString()
                                      : resourceItem->data(MainWindowUI::versionTreeKeyColumn(),
                                                           Qt::UserRole).toString();
        // 这一条是从哪个来源搜出来的（见 resourceSourceRole()）：**必须问对那一侧** ——
        // 两个来源的 id 空间会撞，问错了不报错，只会显示另一个资源的版本列表。
        // 认不出来（那一行不是我们建的）就当 Modrinth，和"没选来源"的默认一致。
        ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
        if (resourceItem != nullptr) {
            ResourceCatalog::sourceFromKey(
                resourceItem->data(MainWindowUI::versionTreeKeyColumn(),
                                   MainWindowUI::resourceSourceRole()).toString(),
                &source);
        }
        const QString gameVersion =
            item->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole).toString();
        if (projectId.isEmpty() || gameVersion.isEmpty()) {
            return;
        }
        addResourcePlaceholderChild(item, tr("正在获取…"));
        // NOTE: **不按加载器筛**（loader 传空）。为什么：这一层的游戏版本是从**搜索响应**
        //       来的，而那份响应里没有"这个资源的哪个游戏版本属于哪个加载器"这份对应关系
        //       （categories 是整个资源的标签，不是某个版本的）。要按加载器筛就得先知道
        //       用户在给哪个实例装 —— 那件事在点版本号那一下才定（见
        //       downloadResourceVersion 里那道加载器对不上的闸口）。
        m_resourceStore->fetchVersions(
            ResourceCatalog::versionsRequestKey(projectId, gameVersion, QString(), source),
            projectId, source, gameVersion);
        return;
    }
    if (depth == 2) {
        // 第三层（一个版本号）：**这一下就是"下它"**。
        downloadResourceVersion(item);
    }
}

void MainWindow::fillResourceVersionRows(QTreeWidgetItem *gameItem,
                                        const QList<ResourceCatalog::VersionEntry> &versions,
                                        const QString &problem) {
    if (gameItem == nullptr) {
        return;
    }
    // 先撤掉「正在获取…」。这一层点开之前一定是空的，所以这里清掉的只有那一行占位。
    while (gameItem->childCount() > 0) {
        delete gameItem->takeChild(0);
    }
    const int column = MainWindowUI::versionTreeKeyColumn();

    if (!problem.isEmpty()) {
        // 「没取到」（超时 / 限流 / 那条资源被作者删了）。**不弹窗**：用户要的是"展开看看"，
        // 进来已经有话说了；而且这一层再点一次（收起再展开）就是重新问一次。
        addResourcePlaceholderChild(gameItem, problem);
        return;
    }
    if (versions.isEmpty()) {
        addResourcePlaceholderChild(gameItem, tr("这个游戏版本上没有可下载的版本。"));
        return;
    }

    // 三层行前面那个图标：跟着它所属的那个资源（同一列里三个草方块会让人以为是三个
    // Minecraft 版本）。资源那条的图标是异步回来的，这里读到的可能还是空的 —— 那就
    // 按老规矩留给代理（空的等于没给）。
    const QVariant resourceIcon =
        gameItem->parent() == nullptr
            ? QVariant()
            : gameItem->parent()->data(column, MainWindowUI::rowIconRole());

    for (const ResourceCatalog::VersionEntry &version : versions) {
        auto *item = new QTreeWidgetItem(gameItem);
        // 身份 = 版本 **id**（不是版本号）：点它那一下要拿它回 m_resourceVersions 里换
        // "这一版的文件是哪几个"，而文件清单是按 id 找的。
        item->setData(column, Qt::UserRole, version.id);
        // 正文写**文件名**：接下来要落到 mods/ 里的就是这个名字（作者给这一版起的名字
        // 常常已经在文件名里写全了，两个都写一行放不下）。
        const ResourceCatalog::FileEntry *file = version.primaryFile();
        item->setText(column, file == nullptr ? version.versionNumber : file->filename);
        // 第二行小字：release/beta/alpha · 加载器 · 发布日期（各缺各的就不补）。
        QStringList bits;
        if (!version.versionType.isEmpty()) {
            bits.append(version.versionType);
        }
        if (!version.loaders.isEmpty()) {
            bits.append(version.loaders.join(QLatin1Char('/')));
        }
        if (!version.datePublished.isEmpty()) {
            // ISO 8601（`2026-06-13T…`）只取日期那一段。
            bits.append(version.datePublished.left(10));
        }
        item->setData(column, MainWindowUI::rowSubTextRole(), bits.join(QStringLiteral(" · ")));
        if (resourceIcon.isValid()) {
            item->setData(column, MainWindowUI::rowIconRole(), resourceIcon);
        }
    }
}

void MainWindow::onResourceVersionsFinished(const QString &requestKey,
                                           const QList<ResourceCatalog::VersionEntry> &versions,
                                           const QString &problem) {
    const int column = MainWindowUI::versionTreeKeyColumn();
    // 先看是不是**详情页**那一发（钥匙里游戏版本、加载器都是空的，和树上那些一定带一个
    // 游戏版本的钥匙不会撞，见 openResourceDetailPage）。
    if (m_resourcePage.open && requestKey == m_resourcePage.requestKey) {
        if (!problem.isEmpty()) {
            // 「没取到」（超时 / 限流 / 那条资源被作者删了）。**不弹窗**：用户是点进这一页
            // 来看东西的，页上已经有话说了；再点一次这个资源就是重新问一次。
            m_resourcePage.versions.clear();
            m_resourcePage.loaded = true;   // 回来过了（回的是"没取到"）
            m_resourceDetailPage->setPlaceholder(problem);
            return;
        }
        m_resourcePage.versions = versions;
        m_resourcePage.loaded = true;
        // 摆哪一份由 chip 决定（这一步同时也处理"筛完一条都不剩"和"一条都没发过"两种空）。
        fillResourcePageGroups();
        return;
    }
    // 不解析那把钥匙（钥匙的格式只有 ResourceCatalog 知道）。改成**按内容重算**：把树上
    // 每一个"资源 × 游戏版本"重算一次钥匙，和回来的这一把比。几页加起来也就几十对，
    // 一次线性扫描。这样钥匙的拼法只有一处（versionsRequestKey），而且不必在条目上存
    // "这一发是谁发的"（那个指针在重搜时会被整个清掉，留着就是悬空的）。
    for (ResourceCatalog::Kind kind : ResourceCatalog::allKinds()) {
        QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind));
        if (tree == nullptr) {
            continue;
        }
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *resourceItem = tree->topLevelItem(i);
            const QString projectId = resourceItem->data(column, Qt::UserRole).toString();
            if (projectId.isEmpty()) {
                continue;
            }
            // 这一条是从哪个来源搜出来的：**必须跟着它算钥匙**。钥匙里带来源（见
            // versionsRequestKey 那段），拿默认那一档去比的话，CurseForge 那条路上一把都
            // 对不上 —— 症状是"展开游戏版本，一直停在「正在获取…」"，而请求其实已经回来了。
            ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
            ResourceCatalog::sourceFromKey(
                resourceItem->data(column, MainWindowUI::resourceSourceRole()).toString(), &source);
            for (int j = 0; j < resourceItem->childCount(); ++j) {
                QTreeWidgetItem *gameItem = resourceItem->child(j);
                const QString gameVersion = gameItem->data(column, Qt::UserRole).toString();
                if (gameVersion.isEmpty()
                    || ResourceCatalog::versionsRequestKey(projectId, gameVersion, QString(), source)
                           != requestKey) {
                    continue;
                }
                // 就是它。清单存下来（点第三层那一下要用 URL / sha1 / 大小），再摆第三层。
                // NOTE: 这张表只往里放、不清理（键是"资源 × 游戏版本"，值是一份纯数据的
                //       小清单）。不清理不会读到旧数据：第三层那些行只在问过之后才存在，
                //       而问过就一定覆盖过同一个键。
                m_resourceVersions.insert(requestKey, versions);
                fillResourceVersionRows(gameItem, versions, problem);
                return;
            }
        }
    }
    // 树里找不到对应的行 = 用户重搜过了，这一发的结果已经不是现在这棵树上的东西了。
    // 丢掉是对的 —— 把旧结果填进新列表比丢掉糟得多。
}

void MainWindow::downloadResourceVersion(QTreeWidgetItem *item) {
    if (item == nullptr) {
        return;
    }
    const int column = MainWindowUI::versionTreeKeyColumn();
    QTreeWidgetItem *gameItem = item->parent();
    QTreeWidgetItem *resourceItem = gameItem == nullptr ? nullptr : gameItem->parent();
    if (gameItem == nullptr || resourceItem == nullptr) {
        return;
    }
    const QString versionId = item->data(column, Qt::UserRole).toString();
    const QString gameVersion = gameItem->data(column, Qt::UserRole).toString();
    const QString projectId = resourceItem->data(column, Qt::UserRole).toString();
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    if (!ResourceCatalog::kindFromCategoryKey(
            resourceItem->data(column, MainWindowUI::resourceKindRole()).toString(), &kind)) {
        return;  // 这一行不是我们建的（正常走不到）
    }
    // 这一版的文件清单：随"点开游戏版本"那一发回来的。没有它就下不了（正常走不到 ——
    // 有第三层这些行就说明那一发回来过）。说清楚，别静静地什么都不做。
    //
    // 钥匙里的来源要**从这一行自己读**（和点开时那把一模一样，见 onResourceItemActivated）：
    // 少带来源的话，CurseForge 那一页上每一版都查不到自己的清单 —— 用户按下去拿到的是
    // 一句「这一版的下载信息还没取到」，可它明明就摆在眼前。
    ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
    ResourceCatalog::sourceFromKey(
        resourceItem->data(column, MainWindowUI::resourceSourceRole()).toString(), &source);
    const QString requestKey =
        ResourceCatalog::versionsRequestKey(projectId, gameVersion, QString(), source);
    const auto cached = m_resourceVersions.constFind(requestKey);
    if (cached == m_resourceVersions.constEnd()) {
        InlineMessage::warning(m_ui->overlay(), tr("下载失败"),
            tr("这一版的下载信息还没取到。把上面那个游戏版本收起再点开一次，然后重试。"));
        return;
    }
    const ResourceCatalog::VersionEntry *chosen = nullptr;
    for (const ResourceCatalog::VersionEntry &entry : *cached) {
        if (entry.id == versionId) {
            chosen = &entry;
            break;
        }
    }
    if (chosen == nullptr) {
        // 行在、清单里没有它 = 这一行是上一次搜索留下的（重搜过）。用户没做错什么，不弹窗。
        return;
    }
    // 剩下的（选哪个实例、落到哪个目录、文件清单、加载器那道闸）详情页那条路也要走一遍，
    // 全在 installResourceVersion 里 —— **这一条路不自己再算一份**。
    installResourceVersion(kind, resourceItem->text(column), *chosen, source);
}

void MainWindow::installResourceVersion(ResourceCatalog::Kind kind, const QString &displayTitle,
                                        const ResourceCatalog::VersionEntry &version,
                                        ResourceCatalog::Source source) {
    // ★ 2026-09-28：模组这一路**先看一眼前置**（用户原话：「在模组下载页，选择模组后显示
    //   其依赖，点击下载后弹出窗口，让用户可以点击一键下载所有缺少的依赖」）。
    //
    // 判据：这一版声明的必装前置里，**目标实例还缺哪几个**（`ModDependencies` 那边算，
    // 纯函数）。一条不缺就走下面那一句 —— 那时候**一个请求都不多发**，和从前逐字一样。
    //
    // ★ 2026-10-09 扩了两处（用户：「点击下载模组时检测当前实例里面是否存在所有的前置…
    //   此模组缺失必要的前置」，并确认**光影包也查**）：
    //   ① 光影包一路上这句判据（那一版声明的必装就是 `iris`，和模组的前置是同一件事）；
    //   ② 目标实例改问 `dependencyInstanceId()` —— 它多认一条来路（从实例设置点进详情页
    //      再下载的那一趟），见那一处的说明。
    //
    // NOTE: 资源包 / 数据包**不看**：它们那一版列的是 `minecraft` / `datapack`（游戏本体），
    //       那和"前置"不是一回事；整合包更不用 —— 它的依赖在清单里，本来就是整个包一起装。
    if ((kind == ResourceCatalog::Kind::Mod || kind == ResourceCatalog::Kind::Shader) &&
        m_settings->warnMissingDependencies() && !m_dependencyCheck.active) {
        const QString instanceId = dependencyInstanceId();
        // ★ 光影的**豁免**（2026-10-09）：这个实例已经能跑光影了（加载器是 OptiFine，或者
        //   mods/ 里已经有 iris / oculus / optifine）⇒ 整段跳过。
        //   理由：Modrinth 上光影项目声明的前置就是 `iris`（OptiFine 那一支还有的写
        //   `optifine`），而**装了 OptiFine 的人不需要 iris** —— 不豁免的话，他每下一次光影
        //   都被劝着装一个用不上的前置。判据直接借现成的 `instanceShaderUsable()`
        //   （整段词匹配那一条也在它里面，不在这儿再写一遍）。
        const bool shaderAlreadyUsable =
            kind == ResourceCatalog::Kind::Shader && instanceShaderUsable(instanceId);
        if (!instanceId.isEmpty() && !shaderAlreadyUsable) {
            const ModDependencies::Target target = dependencyTargetFor(instanceId, source);
            bool installedKnown = false;
            const QSet<QString> installed = installedModKeys(instanceId, &installedKnown);
            const QList<ModDependencies::Lookup> missing =
                ModDependencies::missingRequiredDependencies(version, installed, target);
            if (!missing.isEmpty()) {
                startDependencyCheck(kind, displayTitle, version, target, missing, installedKnown);
                return;
            }
        }
        // 设置里关着 / 不知道装进哪个实例 / 光影已经能用 / 没有缺的：交给下面那一句，
        // 它的判断和话术一个字都不变（也就是用户定的那一条：关掉 = 只下他点的那一版）。
    }
    installResourceVersionNow(kind, displayTitle, version, {});
}

QString MainWindow::dependencyInstanceId() const {
    // ① 资源详情页**正开着**、而且它记着来路实例 ⇒ 用它。
    //    那一格是"这一版是给哪个实例的"最具体的一句：用户就是站在那个实例的页面上点的
    //    这一版（`InstanceFiles` 那条路，见 `openFileResourcePage()`；`Download` 那条路
    //    递进来的本来就是下拉框里那一个，两份值一样）。
    //    NOTE: 判据带上 `open`，是为了不让**关掉那一页之后**残留的旧值去顶替下拉框 ——
    //          用户关掉详情页、回到下载页直接下另一个模组时，目标该是下拉框上那一个。
    //          （异步那一趟不受影响：`finishDependencyCheck()` 把实例**显式传下去**了。）
    if (m_resourcePage.open && !m_resourcePage.instanceId.isEmpty()) {
        return m_resourcePage.instanceId;
    }
    // ② 下载页顶上那个「已安装版本」下拉框。
    return selectedVersionId(m_ui->getInstalledVersionCombo());
}

bool MainWindow::downloadCameFromInstance() const {
    // ① 从实例设置点「下载新模组」切到下载页那一条（`m_downloadFromInstanceId` —— 就是
    //    那颗「← 返回实例设置」用的同一个值）。
    if (!m_downloadFromInstanceId.isEmpty()) {
        return true;
    }
    // ② 资源详情页是从某个实例点进来的（实例设置 → 模组 → 点名字，见
    //    `openInstanceFileResource()`）。那一条路不经过下载页，所以上面那一位是空的 ——
    //    而用户站在**那个实例**的模组列表里点进来的这一版，意思就是"装进它"。
    return m_resourcePage.open && !m_resourcePage.instanceId.isEmpty();
}

ModDependencies::Target MainWindow::dependencyTargetFor(const QString &instanceId,
                                                       ResourceCatalog::Source source) {
    ModDependencies::Target target;
    target.instanceId = instanceId;
    if (instanceId.isEmpty()) {
        return target;
    }
    // 游戏版本和加载器都从**这个实例自己**读（不是从页面上那颗 chip）：前置要装进的是
    // 这个实例，而 chip 是用户在挑"看哪个版本"，两者可以不一样。
    const VersionDisplayParts parts = m_versionManager->displayParts(instanceId);
    // ★★ 2026-10-11：这一格走 `instanceGameVersion()`，**不是 `parts.realVersion`**。
    //
    //   缘由（用户 2026-10-11 原话：「看上去依然获取不到有效更新，比如说那个
    //   hyperlands-new 实例里面有不少模组需要更新，但是实际上显示为一个都没有」）：
    //   `parts.realVersion` 那一格**只有 `wtfml.meta` 里写着 `realVersion` 才有值**，
    //   而今天**没有任何代码会写它**（见 VersionDisplayParts 那段自己的 NOTE）——
    //   照他的盘量出来的是**72 个实例里 69 个递出去的是空串**（mw_check 的
    //   `update-target` 探针，那一节就是干这个用的）。
    //
    //   而这两个筛参数**空着 = 不筛**（那是加载器那一格特意定的规矩，见下面那段），
    //   于是检查更新问回来的成了"这个项目**最新**的那一版"——和这个实例的游戏版本
    //   没有任何关系。真机实测（`update-live` 探针，同一个实例、同一批文件）：
    //   递空串 ⇒ 报 40 条"可更新"，全是 26.3 / 26.4 的（那是另一代游戏版本上的 jar）；
    //   递 1.21.11 ⇒ 报 12 条，逐条都是这个实例真能装的。前置那一趟同病：空串会让它
    //   挑出别的游戏版本的依赖，装进去就崩（见上面那段注释里说的是同一件事）。
    //
    //   `instanceGameVersion()` 读的是**同一份 meta 的同一格**（① realVersion），读不出来
    //   再顺继承链上 `clientVersion`（PCL / HMCL 那类把整份 json 合进实例目录的实例唯一
    //   说得出口的地方）和链末端那个目录名 —— **导出整合包那一趟用的就是它**，两处要的
    //   本来就是同一件事（"这个实例是哪个原版"），不该各有一份判据。（它比 ① 严一点：
    //   那个值还得**像**一个真能装出来的版本号，认不出来就回空串 = 退回"不筛"，
    //   也就是这一行今天的样子。）
    target.gameVersion = m_versionManager->instanceGameVersion(instanceId);
    // ★ 加载器名走 instanceLoaderName()（理由同下面那道加载器闸）：PCL 那类合并实例
    //   （用户盘上 70 个里 67 个）从 parts.loaderName 读出来是空的，而那两个筛参数空着
    //   等于"不筛" —— 问回来的可能是任意加载器的前置。
    // ★ 名字要**归一成小写那一套**（`loaderGroupKey()`）：`instanceLoaderName()` 回的是
    //   显示名（「Fabric」），而下游（`versionsUrl()` / `versionsRequestKey()`）按小写的词写。
    //   2026-10-10 之前这一格直接递显示名，两边都一个字都比不上 —— 而接口不会报错，只是
    //   **筛不出东西**：给 Fabric 实例问回来的前置可能是任意加载器的（装进去就崩）。
    target.loader = loaderGroupKey(instanceLoaderName(parts, m_versionManager, nullptr));
    // 来源跟着**这一条资源是从哪一家搜出来的**走（调用方从条目 / 页面状态里读出来递进来）：
    // 前置也去同一家问 —— 两家的项目号不是一套，拿 Modrinth 的号去 CurseForge 问，
    // 问出来的是**另一个项目**（或者什么都没有），而且不报错。
    target.source = source;
    if (target.source == ResourceCatalog::Source::CurseForge) {
        // 根地址那一处判走 ModpackImport::curseForgeApiBase（**唯一出处**，和
        // ResourceStore::fetchProjects 里面用的是同一条 —— 两边不一致的话，问出去的是
        // 一条地址、解析回来的是另一条的服务，症状是"名字永远取不到"）。
        target.apiBase = ModpackImport::curseForgeApiBase(m_settings->curseForgeApiKey(),
                                                         m_settings->curseForgeApiBaseOverride());
    }
    return target;
}

QSet<QString> MainWindow::installedModKeys(const QString &instanceId, bool *known) const {
    QSet<QString> keys;
    const QString modsDir = instanceFileDirs(instanceId).value(QStringLiteral("mod"));
    if (modsDir.isEmpty()) {
        if (known != nullptr) {
            *known = false;
        }
        return keys;
    }
    // 认出来的那些在那一层（"这一条是哪个资源"那张表跟着反查那一趟住在
    // `InstanceIconController` 里，2026-10-15 搬过去的）。
    keys = m_instanceIconController->recognizedResourceKeysUnder(modsDir);
    // ★ `known` 说的是"这个实例的 mods 到底认过没有"，而不是"认出来几个" ——
    //   一个模组都没认出（新实例、全是自己改过的 jar）和"没进过文件管理页"都表现为空表，
    //   而界面上那两句话不一样（见 startDependencyCheck）。
    if (known != nullptr) {
        *known = m_instanceIconController->recognizedInstanceIs(instanceId);
    }
    return keys;
}

void MainWindow::startDependencyCheck(ResourceCatalog::Kind kind, const QString &displayTitle,
                                      const ResourceCatalog::VersionEntry &version,
                                      const ModDependencies::Target &target,
                                      const QList<ModDependencies::Lookup> &missing,
                                      bool installedKnown) {
    m_dependencyCheck = DependencyCheck{};
    m_dependencyCheck.active = true;
    m_dependencyCheck.kind = kind;
    m_dependencyCheck.title = displayTitle;
    m_dependencyCheck.version = version;
    m_dependencyCheck.instanceId = target.instanceId;
    m_dependencyCheck.target = target;
    m_dependencyCheck.missing = missing;
    // "这个实例的 mods 到底认过没有"（见 installedModKeys 的 known 出参）—— 它只决定
    // 弹框那句话怎么写（"这几个还没装" vs "无法确认你装了没有"），所以跟着这一趟存着。
    m_dependencyCheck.installedKnown = installedKnown;

    QStringList ids;
    ids.reserve(missing.size());
    for (const ModDependencies::Lookup &lookup : missing) {
        if (!ids.contains(lookup.dependency.projectId)) {
            ids.append(lookup.dependency.projectId);
        }
    }
    // ★ 消费方名字（`dependency`）**必须跟详情页那一趟（`detail`）分开**：两处问的完全可能
    //   是同一批号，而 `fetchProjects()` 是按钥匙去重的（同一把钥匙再来一发把前一发掐掉）。
    //   撞上的症状是"先发的那一趟永远等不到回信"，屏幕上一点异常都没有。
    m_dependencyCheck.titleRequestKey = ModDependencies::projectsKeyFor(
        QStringLiteral("dependency"), ids, m_dependencyCheck.target.source);
    appendLog(ModDependencies::dependencyLookupLogText(missing.size()));
    m_resourceStore->fetchProjects(m_dependencyCheck.titleRequestKey, ids,
                                   m_dependencyCheck.target.source);
}

void MainWindow::onDependencyProjectsFinished(const QString &requestKey,
                                             const QList<ResourceCatalog::Hit> &hits,
                                             const QString &problem) {
    if (!m_dependencyCheck.active || requestKey != m_dependencyCheck.titleRequestKey) {
        return; // 不是这一趟等的（用户中途又点了别的下载）
    }
    // 名字取不到**不算错**：那一条退成项目号显示（见 ModDependencies::buildPlan 那段），
    // 而"没有名字"挡不住"要不要一起下"这个决定。所以这里只记一句日志。
    if (!problem.isEmpty()) {
        appendLog(tr("前置模组的名字没取到：%1").arg(problem));
    }
    // 名字进**会话级**那个缓存（详情页那一行读的是同一份）。取不到名字的那几条**不写**：
    // 写一个空串进去等于把"还没问过"说成"问过了、它没名字"。
    rememberProjectTitles(hits, m_dependencyCheck.target.source);

    // 逐条去问"它有哪些版本"。**同一批发出去**（一把钥匙一个项目，互相不撞）——
    // 串起来问的话，N 个前置就是 N 个往返，而它们本来就互不相干。
    m_dependencyCheck.answers.clear();
    m_dependencyCheck.pending = 0;
    for (const ModDependencies::Lookup &lookup : m_dependencyCheck.missing) {
        ++m_dependencyCheck.pending;
        m_resourceStore->fetchVersions(lookup.requestKey, lookup.dependency.projectId,
                                       m_dependencyCheck.target.source,
                                       m_dependencyCheck.target.gameVersion,
                                       m_dependencyCheck.target.loader);
    }
    if (m_dependencyCheck.pending == 0) {
        finishDependencyCheck(); // 一条都不缺（走不到：missing 非空才进这一趟）
    }
}

void MainWindow::onDependencyVersionsFinished(const QString &requestKey,
                                             const QList<ResourceCatalog::VersionEntry> &versions,
                                             const QString &problem) {
    if (!m_dependencyCheck.active) {
        return;
    }
    bool ours = false;
    for (const ModDependencies::Lookup &lookup : m_dependencyCheck.missing) {
        if (lookup.requestKey == requestKey) {
            ours = true;
            break;
        }
    }
    if (!ours) {
        return; // 详情页那一发（或者别的页面）的回复，不关这一趟的事
    }
    // ★ 失败时**不往 answers 里放空表**：`buildPlan` 那边"钥匙缺席"和"回来了但是空的"
    //   是两句不同的话（"没问到"和"它就是没有版本"）—— 放空表等于把前者说成后者。
    if (problem.isEmpty()) {
        m_dependencyCheck.answers.insert(requestKey, versions);
    }
    if (m_dependencyCheck.pending > 0) {
        --m_dependencyCheck.pending;
    }
    if (m_dependencyCheck.pending == 0) {
        finishDependencyCheck();
    }
}

void MainWindow::finishDependencyCheck() {
    DependencyCheck check = m_dependencyCheck;
    m_dependencyCheck.active = false;

    const bool installedKnown = check.installedKnown;

    // 名字从会话缓存里查（详情页那一行读的是同一份）。缺的那几条**退成项目号**——
    // 列出来总比"有 2 个前置没找到"强（见 ModDependencies::buildPlan 那段），
    // 而"为什么没名字"已经写进日志了（见 onDependencyProjectsFinished）。
    QHash<QString, QString> titles;
    for (const ModDependencies::Lookup &lookup : check.missing) {
        const QString name =
            m_projectTitles.value(projectTitleKey(check.target.source, lookup.dependency.projectId));
        if (!name.isEmpty()) {
            titles.insert(lookup.dependency.projectId, name);
        }
    }

    const ModDependencies::Plan plan =
        ModDependencies::buildPlan(check.missing, check.answers, titles);

    QStringList names;
    names.reserve(check.missing.size());
    for (const ModDependencies::Lookup &lookup : check.missing) {
        const QString name = titles.value(lookup.dependency.projectId);
        names.append(name.isEmpty() ? lookup.dependency.projectId : name);
    }

    const QString note = ModDependencies::unresolvedNoteText(plan.unresolved);
    QList<ModDependencies::Resolved> extras;
    if (plan.downloads.isEmpty()) {
        // 一条都没解开：**照样把这个模组装上**，但得如实说一句"前置一条都没找到"。
        // 不说的话，用户点完下载看到的就是"下好了"，而进游戏那一下就崩。
        //
        // NOTE: 这一档走 `ask()` 而不是 `warning()`：只有 `ask()` 带得了那行**灰字**
        //       （用户 2026-10-09 要的"可以在设置里关掉"）。一颗「知道了」按下去就走。
        QString text = ModDependencies::dependencyPromptText(check.kind, names);
        if (!note.isEmpty()) {
            text += QStringLiteral("\n\n") + note;
        }
        InlineMessage::ask(m_ui->overlay(), MessageKind::Warning,
                           ModDependencies::dependencyPromptTitle(), text, {tr("知道了")}, 0, -1,
                           false, ModDependencies::dependencyPromptNoteText());
    } else {
        QString text = installedKnown
                           ? ModDependencies::dependencyPromptText(check.kind, names)
                           : ModDependencies::dependencyPromptUnknownText(check.kind, names);
        if (!note.isEmpty()) {
            text += QStringLiteral("\n\n") + note;
        }
        // 两颗按钮：左「只下这个」（**默认那颗**，和从前 `question(..., defaultYes=false)`
        // 同向 —— 误按回车不该替用户多做一件事），右「一起下载」。
        // NOTE: 从前这儿调的是 `question()`，它**带不了那行灰字**（没有 note 这一格）。
        const int choice = InlineMessage::ask(
            m_ui->overlay(), MessageKind::Question, ModDependencies::dependencyPromptTitle(), text,
            {ModDependencies::dependencyOnlyThisButtonText(),
             ModDependencies::dependencyAllButtonText()},
            0, -1, false, ModDependencies::dependencyPromptNoteText());
        if (choice == 1) {
            extras = plan.downloads;
        }
    }

    // ★ `check.instanceId` 显式传下去：这一趟是从一轮网络往返**之后**回到这里的，那期间
    //   用户完全可能把详情页收掉（于是 `m_resourcePage.open` 落了、`dependencyInstanceId()`
    //   只能退回下拉框）。检查时算的是哪个实例，就得装进哪个实例。
    installResourceVersionNow(check.kind, check.title, check.version, extras, check.instanceId);
}

// ============================ 实例更新检测 ============================
//
// 用户 2026-09-28：「实例设置那里做一个更新检测」。
//
// 【这一趟在做什么（顺序不能换）】
//
//   ① 把这个实例那四个能下载的目录**枚举**一遍（模组 / 资源包 / 数据包 / 光影 ——
//      和文件管理页那几页同一份目录表）；
//   ② 后台线程里**反查一次**（`InstanceIconLookup`：哈希 → 项目号 → 项目对象）。
//      顺手带出来的是每个文件的 **sha1** —— 判"盘上这一份是第几版"就靠它
//      （见 `Found::sha1` 那段：这一位本来就是为它加的）；
//   ③ 每个项目**问一发版本列表**（按这个实例的游戏版本 + 加载器筛！不筛的话，"最新版"
//      会是另一个游戏版本的版本，于是报告说"有新版本"、点进去装不上）；
//   ④ `judgeUpdate()` 逐条判，攒成一份报告。
//
// 【为什么别的事都不做】
//
// 不自动下载、不自动改盘、不问"要不要更新" —— 用户要的是"检测"。报告里只说事实
//（哪几个有新版本、最新的那一版叫什么），后面那一步是他自己的事。

void MainWindow::checkInstanceUpdates(const QString &instanceId, InstanceSettingsDialog *dialog,
                                     const QStringList &selectedPaths) {
    if (instanceId.isEmpty()) {
        return;
    }
    // 已经在跑：**到此为止**（不排队、不重开）。重开那一趟的代价是把几十兆 jar 再读一遍、
    // 把刚才那几发再问一遍，而屏幕上那一份报告正在弹着 —— 用户看不到任何差别。
    if (m_updateCheck.thread != nullptr) {
        appendLog(tr("更新检测已经在跑了，等它出结果。"));
        return;
    }

    // ① 候选文件（四个"能下载的资源"目录，不含存档/原理图 —— 那两样没有"更新"这回事）。
    QStringList paths;
    const QSet<QString> selectedScope(selectedPaths.begin(), selectedPaths.end());
    // ★★ 2026-10-11：这一趟每个路径的**类别**（问版本列表时带不带加载器靠它，见
    //   `ModDependencies::kindUsesLoader()` 那段）。收集的时候就记下来 —— 目录本来就是
    //   按类走出来的，而反查那一趟只回一个路径，那时再猜就晚了。
    QHash<QString, ResourceCatalog::Kind> pathKinds;
    const QHash<QString, QString> dirs = instanceFileDirs(instanceId);
    const QStringList wantedDirs = {QStringLiteral("mod"), QStringLiteral("resourcepack"),
                                    QStringLiteral("datapack"), QStringLiteral("shader")};
    for (const QString &kindKey : wantedDirs) {
        ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
        if (!ResourceCatalog::kindFromCategoryKey(kindKey, &kind)) {
            continue;  // 页键认不回类别 = 这张表改了名，那一类这一趟不查（宁可少一类）
        }
        const QString dir = dirs.value(kindKey);
        if (dir.isEmpty()) {
            continue;
        }
        QDir folder(dir);
        const QFileInfoList entries =
            folder.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);
        for (const QFileInfo &info : entries) {
            if (!selectedScope.isEmpty() && !selectedScope.contains(info.absoluteFilePath())) continue;
            // ★★ 2026-10-11：**禁用着的那几条（`X.jar.disabled`）这一趟整个跳过**
            //   （用户原话：「检查更新时跳过它」）。
            //
            //   从前这里是"不禁用/启用"（那四个字的后缀也是同一个文件、sha1 照样能判）——
            //   改口径的理由：那一行在屏幕上是被**划掉**的（名字一道删除线、开关关着，
            //   见 InstanceFilesPage::makeRow），游戏根本不加载它 ⇒ 替它问一趟"有没有新版本"
            //   是替一个**用不到的东西**问：真答了也只是让那一行多出一个 ↑，而用户按不下去
            //   （更新那一发是要**装进这个实例**的，装一个被禁用的文件没有意义）。
            //   顺带这一趟也小一截（少读几个 jar、少发几发反查）。
            //
            //   NOTE: 判据就是盘上那个名字的后缀 —— 和文件页"这一条是不是禁用着"**逐字同一
            //         个**（`disabledSuffix()`，大小写不敏感，和 InstanceFilesPage 里那处一样）。
            //   NOTE: **不是**用 `identityPath()` 去重 —— 被跳过的那一条压根不进这份名单。
            if (info.fileName().endsWith(InstanceFilesPage::disabledSuffix(),
                                         Qt::CaseInsensitive)) {
                continue;
            }
            paths.append(info.absoluteFilePath());
            pathKinds.insert(info.absoluteFilePath(), kind);
        }
    }
    if (paths.isEmpty()) {
        InlineMessage::information(m_ui->overlay(), tr("更新检测"),
                                 tr("这个实例里没有可以查更新的文件。"));
        return;
    }

    m_updateCheck = UpdateCheckRun{};
    m_updateCheck.selectedScope = selectedScope;
    m_updateCheck.instanceId = instanceId;
    m_updateCheck.pathKinds = pathKinds;
    // 结果回来时要递给**同一页**（见 UpdateCheckRun::dialog 那段）。弱引用：收页了就不递。
    m_updateCheck.dialog = dialog;
    appendLog(tr("正在检查「%1」里的资源有没有新版本（%2 个文件）...")
                  .arg(instanceDisplayName(instanceId))
                  .arg(paths.size()));

    InstanceIconLookup::Options options;
    // 三个口子和 ResourceStore / VersionManager / 文件管理页那三处**同一份设置**。
    options.curseForgeApiKey = m_settings->curseForgeApiKey();
    options.modrinthApiBaseOverride = m_settings->modrinthApiBaseOverride();
    options.curseForgeApiBaseOverride = m_settings->curseForgeApiBaseOverride();
    // ★★ 2026-10-10：和文件管理页那一趟**共用同一份盘上缓存**（同一个实例 ⇒ 同一个文件）
    //    —— 两边要的本来就是同一件事（"这个文件是哪个资源" + 它的 sha1），谁先跑谁把它
    //    写下来，另一个就直接读。见 InstanceIconLookup 的 .hpp 里那段。
    options.cacheDir = AppPaths::instanceIconCacheDir();
    options.cacheKey = instanceId;
    options.requireVersionMetadata = true;

    // NOTE: 这里以前还有一段"上一趟那个线程对象还挂着就先 deleteLater"——它**永远进不去**：
    //       上面那道 `m_updateCheck.thread != nullptr` 已经把"上一趟没跑完"挡回去了，而跑完
    //       的那一趟由 `finishUpdateCheck()` 自己收（见那一段 ★）。线程对象的回收一并放在那里。
    m_updateCheck.thread = QThread::create([this, paths, options, instanceId]() {
        const InstanceIconLookup::Result result = InstanceIconLookup::lookup(paths, options);
        QMetaObject::invokeMethod(
            this,
            [this, result, instanceId]() { onUpdateCheckLookupFinished(result, instanceId); },
            Qt::QueuedConnection);
    });
    m_updateCheck.thread->setParent(this);
    m_updateCheck.thread->start();
}

void MainWindow::onUpdateCheckLookupFinished(const InstanceIconLookup::Result &result,
                                            const QString &instanceId) {
    // 回来看的这一趟是不是**现在**要的那一趟（用户完全可能又点了一次检查、或者换了实例）。
    if (m_updateCheck.instanceId != instanceId) {
        return;
    }
    // 反查那一趟自己的毛病（读不动几个文件 / 整趟没跑完）——报告末尾照实说一句。
    // **不算失败**：认出来的那些照样判（那是真的）。
    if (!result.problem.isEmpty()) {
        m_updateCheck.lookupNote = result.problem;
    } else if (result.unreadable > 0) {
        m_updateCheck.lookupNote =
            tr("有 %1 个文件读不动（多半被别的程序占着），这一批里没有它们。")
                .arg(result.unreadable);
    }

    const ModDependencies::Target target =
        dependencyTargetFor(instanceId, ResourceCatalog::Source::Modrinth);

    // ★★ 2026-10-11：**一个文件只查一家**（缘由见 `UpdateCheckRun::fallbackItems` 那段）。
    //   先量一遍"哪几个文件 Modrinth 认出来了" —— 同一个文件在两家都认出来时，
    //   下面那两条记录（路径逐字同一个）只有**第一条**该往下走。
    //   必须**单独走一趟**：`result.found` 的顺序不定（两份回复都是哈希表），
    //   边遍历边攒的话，"CurseForge 那条"完全可能排在"Modrinth 那条"前面。
    QSet<QString> modrinthPaths;
    for (const InstanceIconLookup::Found &found : result.found) {
        if (found.hit.projectId.isEmpty()) {
            continue;
        }
        if (found.hit.source == ResourceCatalog::Source::Modrinth) {
            modrinthPaths.insert(found.path);
        }
    }

    for (const InstanceIconLookup::Found &found : result.found) {
        if (found.hit.projectId.isEmpty()) {
            continue;
        }
        ModDependencies::InstalledItem item;
        item.path = found.path;
        item.iconKey = found.hit.iconCacheId();
        item.title = found.hit.title;
        item.sha1 = found.sha1;
        item.currentVersion = found.currentVersion;
        item.projectId = found.hit.projectId;
        item.source = found.hit.source;
        // ★★ 2026-10-11：这一条问版本列表时**带不带加载器**（见
        //   `ModDependencies::kindUsesLoader()` 那段 —— 资源包 / 光影包 / 数据包带上
        //   实例的加载器就一条都问不到）。类别是收集候选文件那一步记下的。
        item.loaderApplies = ModDependencies::kindUsesLoader(
            m_updateCheck.pathKinds.value(found.path, ResourceCatalog::Kind::Mod));
        // ★★ 2026-10-11：**Modrinth 认出来的那一拨不走单发**。
        //
        //   缘由见 `UpdateCheckRun::batchItems` 那段（用户报的「无法正常获取更新」）：一页
        //   几百个模组各问一发版本列表，实测整批 429。这一拨改成"一次带一串 sha1"的批量
        //   那一发（`POST /version_files/update`），一个项目都不单独问。
        //
        //   NOTE: 只有 Modrinth 有这个接口 —— CurseForge 那些照旧一项目一发（它们通常
        //         只占一小撮，几发不成气候；而"两家的号不是一套"这件事在下面那把钥匙上
        //         已经带着来源了）。
        // ★★ 2026-10-11：这一条该带的加载器（`item.loaderApplies` 那一位的用法，
        //   见 `ModDependencies::kindUsesLoader()` 那段）。**每一处拼钥匙 / 发请求都得
        //   走它** —— 直接递 `target.loader` 的话，资源包那几类又带上「fabric」了。
        const QString itemLoader = item.loaderApplies ? target.loader : QString();
        if (found.hit.source == ResourceCatalog::Source::Modrinth && !found.sha1.isEmpty()) {
            // 批量那一发**按钥匙分组**（加载器不同的两拨不能合成一发 —— 见
            // `UpdateCheckRun::batchItems` 那段）。
            const QString batchKey =
                ModDependencies::latestVersionsKeyFor(QStringLiteral("update"), target.gameVersion,
                                                      itemLoader);
            m_updateCheck.batchItems[batchKey][found.sha1.toLower()].append(item);
            m_updateCheck.batchLoader.insert(batchKey, itemLoader);
            continue;
        }
        // ★ 钥匙跟着**这一条自己的来源**走（两家的 id 空间不是一套，问错边问出来的是
        //   另一个项目）。来源在 hit 上，不在 target 上。
        const QString key = ModDependencies::versionsKeyFor(
            QStringLiteral("update"), found.hit.projectId, target.gameVersion, itemLoader,
            found.hit.source);
        // ★ 同一个文件 Modrinth 那边也认出来了 ⇒ **押着**（它已经在 `batchItems` 里了，
        //   而两家的答案在这里同义）。等批量那一发回来看它有没有这个 sha1 的答案：有就
        //   到此为止（这一条已经被判过），没有才退回问 CurseForge。
        if (found.hit.source != ResourceCatalog::Source::Modrinth &&
            modrinthPaths.contains(found.path)) {
            m_updateCheck.fallbackItems[key].append(item);
            continue;
        }
        m_updateCheck.itemsByKey[key].append(item);
    }

    m_updateCheck.waitingKeys.clear();
    for (auto it = m_updateCheck.itemsByKey.constBegin();
         it != m_updateCheck.itemsByKey.constEnd(); ++it) {
        m_updateCheck.waitingKeys.insert(it.key());
    }
    // 批量那几发（Modrinth 那一拨）：**一把钥匙一发**（模组那拨带加载器、资源包那拨不带
    // —— 见 `UpdateCheckRun::batchItems` 那段）。空表就整个不发（"一个都不用问"也是走完了）。
    for (auto it = m_updateCheck.batchItems.constBegin();
         it != m_updateCheck.batchItems.constEnd(); ++it) {
        m_updateCheck.pendingBatchKeys.insert(it.key());
        m_resourceStore->fetchLatestVersions(it.key(), it.value().keys(), target.gameVersion,
                                             m_updateCheck.batchLoader.value(it.key()));
    }
    if (m_updateCheck.waitingKeys.isEmpty() && m_updateCheck.pendingBatchKeys.isEmpty()) {
        // 一个都没认出来（新实例、或者全是自己改过的 jar）——**照样出一份报告**：
        // "一个都认不出来"正是用户需要知道的事，闷着不说是最坏的。
        finishUpdateCheck(instanceId);
        return;
    }

    // 逐个项目问版本列表。**一把钥匙一发**，互相不撞。
    for (auto it = m_updateCheck.itemsByKey.constBegin();
         it != m_updateCheck.itemsByKey.constEnd(); ++it) {
        requestUpdateCheckVersions(it.key());
    }
}

// 更新检测：**按钥匙发那一发"问这个项目的版本列表"**（一把钥匙一发）。
//
// 单独一个函数的缘由是它有两个调用点 —— 反查那一趟回来时对 `itemsByKey` 里每把钥匙发一遍，
// 以及批量那一发回来时给"退回问 CurseForge"的那些补发（见 `onUpdateCheckLatestFinished()`）。
// 两处要发的东西是**逐字同一句**，抄第二份的话迟早有一边忘了带上来源。
void MainWindow::requestUpdateCheckVersions(const QString &requestKey) {
    const QList<ModDependencies::InstalledItem> items = m_updateCheck.itemsByKey.value(requestKey);
    if (items.isEmpty()) {
        return; // 这一把底下没人 = 不是这一趟的钥匙，不发
    }
    // 目标（游戏版本 / 加载器）每次现算：它读的是实例自己那几份文件，而这一趟的实例
    // 就在 `m_updateCheck.instanceId` 上。
    const ModDependencies::Target target =
        dependencyTargetFor(m_updateCheck.instanceId, ResourceCatalog::Source::Modrinth);
    // 项目号从钥匙里读不出来（钥匙是拼出来的一串），所以从这一组里那一条自己的 `iconKey`
    // （`<来源>-<项目号>`）上取。
    const ModDependencies::InstalledItem &any = items.first();
    ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
    const int dash = any.iconKey.indexOf(QLatin1Char('-'));
    if (dash > 0) {
        ResourceCatalog::sourceFromKey(any.iconKey.left(dash), &source);
    }
    const QString projectId = dash > 0 ? any.iconKey.mid(dash + 1) : any.iconKey;
    // ★★ 2026-10-11：加载器只对**模组**说得上话（见 `kindUsesLoader()` 那段）。这一把钥匙
    //   底下那几条是同一类（钥匙里就带着这一格），随便读一条都一样。
    const QString itemLoader = any.loaderApplies ? target.loader : QString();
    // 先记账再发：答案回来时按 `waitingKeys` 认"是不是这一趟等的"，漏记就是"判了不算"。
    m_updateCheck.waitingKeys.insert(requestKey);
    m_resourceStore->fetchVersions(requestKey, projectId, source, target.gameVersion, itemLoader);
}

void MainWindow::onUpdateCheckLatestFinished(
    const QString &requestKey, const QHash<QString, ResourceCatalog::VersionEntry> &latest,
    const QString &problem) {
    // 不是这一趟等的那一发就丢掉（这一把钥匙只有更新检测在用，但"是不是我这一趟"这件事
    // 还是得问一句 —— 用户完全可能刚又点了一次检查）。
    if (!m_updateCheck.pendingBatchKeys.contains(requestKey)) {
        return;
    }
    m_updateCheck.pendingBatchKeys.remove(requestKey);
    // ★★ 2026-10-11：这一趟里**问回来过的 sha1 攒一份**（跨那几把批量钥匙）。下面"退回问
    //   CurseForge"那一拨判的是"Modrinth 到底答没答过这一个哈希"，而它现在只看得见
    //   **这一发**的答案 —— 不看这一份的话，另一把钥匙答过的那些会被当成"没答"再问一遍
    //   （同一个文件于是进报告两条）。
    for (auto it = latest.constBegin(); it != latest.constEnd(); ++it) {
        m_updateCheck.batchAnswered.insert(it.key());
    }

    // 押着的那些文件**在结果里只该有一条**：它们的答案由下面那一拨（退回问 CurseForge）
    // 说了算，所以这一发**没给出它们 sha1 的答案**时不在这一圈里判 —— 不然同一个文件会
    // 带着一条"说不清"进报告，下面那一发再给一条（"检查了 N 个资源"那个数于是比盘上的
    // 文件还多）。见 `UpdateCheckRun::fallbackItems` 那段。
    QSet<QString> heldPaths;
    for (auto it = m_updateCheck.fallbackItems.constBegin();
         it != m_updateCheck.fallbackItems.constEnd(); ++it) {
        for (const ModDependencies::InstalledItem &item : it.value()) {
            heldPaths.insert(item.path);
        }
    }

    // 只判**这一发**问回来的那一拨（`batchItems` 现在按钥匙分层，别的钥匙底下那些不归它）。
    const QHash<QString, QList<ModDependencies::InstalledItem>> answeredItems =
        m_updateCheck.batchItems.value(requestKey);
    for (auto it = answeredItems.constBegin(); it != answeredItems.constEnd(); ++it) {
        const ResourceCatalog::VersionEntry found = latest.value(it.key());
        for (const ModDependencies::InstalledItem &item : it.value()) {
            if (found.id.isEmpty() && heldPaths.contains(item.path)) {
                continue; // 交给退回的那一发（上面那段）
            }
            const ModDependencies::UpdateVerdict verdict =
                ModDependencies::judgeUpdateAgainstLatest(item, found, problem);
            m_updateCheck.verdicts.append(verdict);
            // "这条可更新"的那一瞬间把它该换成的那一版留一份（和单发那条路同一个规矩，
            // 见 UpdateCheckRun::latestByPath）。
            if (verdict.state == ModDependencies::UpdateState::Outdated && !found.id.isEmpty()) {
                m_updateCheck.latestByPath.insert(item.path, found);
            }
        }
    }

    // ★★ 2026-10-11：**别的批量钥匙还押着就先别退**。退回那一拨（下面那一段）判的是
    //   "Modrinth 到底答没答过这一个哈希"，而答案分在**几发**里 —— 早退一步的后果是
    //   另一把钥匙底下那些**已经答过**的文件被当成"没答"退回问 CurseForge：同一个文件
    //   进报告两条（一条 Modrinth 判的、一条 CurseForge 判的，还可能一条"有新版本"、
    //   一条"已是最新"），正是 `UpdateCheckRun::fallbackItems` 那一段要防的那个症状。
    //   收尾判据和 `checkInstanceUpdates()` / `onUpdateCheckVersionsFinished()` 那两句
    //   **逐字同一条**。
    if (!m_updateCheck.pendingBatchKeys.isEmpty()) {
        return;
    }

    // ★★ 2026-10-11：**退回问 CurseForge 的那一拨**（见 `UpdateCheckRun::fallbackItems`
    //   那段）。押在这儿的都是"两家都认出来了、先让 Modrinth 开口"的文件，现在按答案分流：
    //   · **答过它的 sha1** ⇒ Modrinth 答了，上面那个循环已经把它判过了，丢掉；
    //   · **没答过** ⇒ 这一条到现在为止**一发都没问过**（Modrinth 那边没有这个哈希 / 它
    //     在这个游戏版本+加载器下没有版本），退回问 CurseForge —— "一个文件至少被问过
    //     一次"是这一套的底线，不然它就成了报告里凭空少掉的一条。
    //
    //   NOTE: 判据是**这一趟攒下来的那一份**（`batchAnswered`，跨所有批量钥匙），
    //         不是这一发的回复 —— 批量现在分几发，只看这一发的话另一把钥匙答过的那些
    //         会被当成"没答"再问一遍（见上面那段）。
    QStringList refire;
    for (auto it = m_updateCheck.fallbackItems.constBegin();
         it != m_updateCheck.fallbackItems.constEnd(); ++it) {
        bool answered = false;
        for (const ModDependencies::InstalledItem &item : it.value()) {
            if (!item.sha1.isEmpty() && m_updateCheck.batchAnswered.contains(item.sha1)) {
                answered = true;
                break;
            }
        }
        if (answered) {
            continue;
        }
        // 挪进 `itemsByKey` —— **这一挪同时把它算进收尾判据**（那把钥匙发出去之后
        // `onUpdateCheckVersionsFinished()` 判的就是这儿这一组）。
        m_updateCheck.itemsByKey[it.key()].append(it.value());
        // 同一个项目的**另一条**（比如它底下还有一个只有 CurseForge 认出来的文件）如果
        // 已经发出去过了，就不再发第二发 —— 答案本来就是同一份（版本列表是按项目问的），
        // 而 `onUpdateCheckVersionsFinished()` 是按**答案回来时**那一组来判的，照样判得到。
        if (!m_updateCheck.waitingKeys.contains(it.key())) {
            refire.append(it.key());
        }
    }
    // 先清空再发：`requestUpdateCheckVersions()` 会往 `waitingKeys` 里加，而这两件事的顺序
    // 反过来就是"刚记上的账又被这一句抹掉"（也免得一边遍历 `fallbackItems` 一边动它）。
    m_updateCheck.fallbackItems.clear();
    for (const QString &key : refire) {
        requestUpdateCheckVersions(key);
    }

    if (m_updateCheck.waitingKeys.isEmpty()) {
        finishUpdateCheck(m_updateCheck.instanceId);
    }
}

void MainWindow::onUpdateCheckVersionsFinished(const QString &requestKey,
                                              const QList<ResourceCatalog::VersionEntry> &versions,
                                              const QString &problem) {
    if (!m_updateCheck.waitingKeys.contains(requestKey)) {
        return; // 不是这一趟等的（详情页那一发 / 前置那一趟 / 已经判过的那把）
    }
    m_updateCheck.waitingKeys.remove(requestKey);

    // 那一发没成 → **空表**进 judgeUpdate：那几条判"说不清"（**不是**"已是最新"）。
    // ★ 2026-10-11：`problem` **跟着一起递进去** —— 那一发自己报的那句话（"问得太勤了"
    //   这种）就是这几条判"说不清"的理由。从前它在这一句上被丢掉，于是 429 在报告里长得
    //   和"网断了"一模一样（见 ModDependencies::judgeUpdate 那段）。
    const QList<ResourceCatalog::VersionEntry> &answer =
        problem.isEmpty() ? versions : QList<ResourceCatalog::VersionEntry>();
    for (const ModDependencies::InstalledItem &item : m_updateCheck.itemsByKey.value(requestKey)) {
        const ModDependencies::UpdateVerdict verdict =
            ModDependencies::judgeUpdate(item, answer, problem);
        m_updateCheck.verdicts.append(verdict);
        // ★ 2026-10-09：判"这条可更新"的那一瞬间，把**它该换成的那一版**也留一份 ——
        //   文件管理页悬浮条上那颗「更新」要的就是它（见 UpdateCheckRun::latestByPath）。
        //   `answer.first()` 和 `judgeUpdate()` 认的"最新"是同一个（它里面也是这么取的），
        //   所以这两份永远说得上同一件事。
        if (verdict.state == ModDependencies::UpdateState::Outdated && !answer.isEmpty()) {
            m_updateCheck.latestByPath.insert(item.path, answer.first());
        }
    }
    // ★★ 2026-10-11：**批量那几发还押着就不算跑完**。
    //
    //   这一节手上这两样是两条**互相独立**的线：`waitingKeys` 是"一项目一发"那一拨
    //   （CurseForge 那些），`pendingBatchKeys` 是 Modrinth 那一拨的批量那几发。只看
    //   `waitingKeys` 的话，一个**两家都有**的实例上（真机：271 条走批量 + 80 条走单发）
    //   只要 CurseForge 那 80 发里**任意一发**先回来、而它正好是最后一把没还的钥匙，
    //   这一趟就当场收尾了 —— 批量那一发回来时它自己那一句闸（见上面）直接 return：
    //   **那 271 条一条都不会进报告**，退回问 CurseForge 的那一拨（`fallbackItems`）
    //   也一辈子不会发出去。屏幕上是一份"检查了 80 个资源"的正常报告，日志也干干净净
    //   —— 用户看到的就是「无法正常获取更新」。
    //   收尾判据必须和 `checkInstanceUpdates()` / `onUpdateCheckLatestFinished()` 那两句
    //   （"空表 + 没有批量"）**逐字同一条**。
    if (m_updateCheck.waitingKeys.isEmpty() && m_updateCheck.pendingBatchKeys.isEmpty()) {
        finishUpdateCheck(m_updateCheck.instanceId);
    }
}

// ★ 参数**按值收**（2026-09-28，别改回 `const QString &`）：下面那句 `m_updateCheck =
//   UpdateCheckRun{}` 会把这个成员清空，而 `onUpdateCheckVersionsFinished` 那边递进来的
//   正是 `m_updateCheck.instanceId` **本身** —— 按引用收的话，函数体第一句就把自己这个参数
//   指的那块 QString 抹成空的，于是整份名单记在**空键**底下（`m_instanceOutdatedPaths`）。
//   症状非常有欺骗性：**当着一页的面筛得出来**（那一趟是拿 `run.instanceId` 直接递过去的，
//   而 `run` 是赋值前拷出来的），关掉这一页再进来就永远筛不出东西、还重新挂着「还没有查过
//   更新」—— 而那一趟明明刚跑完。屏幕上看不出任何异常，日志也是干净的。
//   QString 是隐式共享的，按值收不多拷一个字节的正文。
void MainWindow::finishUpdateCheck(QString instanceId) {
    const UpdateCheckRun run = m_updateCheck;
    // ★ 2026-10-19：`run.thread`（那一趟反查的 QThread）**不属于这一份账**，可它原来是
    //   跟着 `m_updateCheck = UpdateCheckRun{}` 一起被丢掉的指针 —— 结果是：
    //     · 每查一次都在 MainWindow 底下攒一个已经跑完的僵尸 QThread（父对象是 this，
    //       要等整扇窗散掉才一起删）；
    //     · 析构里那句 `m_updateCheck.thread->wait()` 对**刚收完尾**的那一趟永远看不到
    //       它（指针已经没了）。而这一趟的回报正是它自己排过来的 —— 主线程处理这条
    //       排队消息时，worker 可能还在 `run()` 的最后几条指令里。那个窗口里关窗，
    //       Qt 对"线程在跑、父对象先散"是直接 abort。
    //   所以先在这里把它收干净：已经停了就 deleteLater；还在收尾就等它一下（微秒级，
    //   它那句 `invokeMethod` 已经是函数体最后一行）。
    if (run.thread != nullptr) {
        if (!run.thread->isFinished()) {
            run.thread->wait();
        }
        run.thread->deleteLater();
    }
    m_updateCheck = UpdateCheckRun{};

    // 报告：**有新版本的排最前面**（那才是用户点这一下的目的），然后是"说不清"，
    // 最后才是"已是最新"。同一个档里按名字排 —— 不排的话每条的位置都随哈希表顺序变，
    // 同一个实例连查两次的名单顺序都不一样。
    QList<ModDependencies::UpdateVerdict> outdated;
    QList<ModDependencies::UpdateVerdict> unknown;
    QList<ModDependencies::UpdateVerdict> current;
    for (const ModDependencies::UpdateVerdict &verdict : run.verdicts) {
        switch (verdict.state) {
        case ModDependencies::UpdateState::Outdated:
            outdated.append(verdict);
            break;
        case ModDependencies::UpdateState::UpToDate:
            current.append(verdict);
            break;
        case ModDependencies::UpdateState::Unknown:
            unknown.append(verdict);
            break;
        }
    }

    // ★ 2026-09-28：这一趟查出来的"哪几条有新版本"**留下来**（从前这里是丢掉、只写一份
    //   报告）。文件管理那几页状态栏里那一格「可更新」要的就是它 —— 不留的话，那趟检测
    //   跑完只在弹框里活了几秒钟，而"筛出可更新的那几条"这件事一次都做不到。
    //
    //   存的是**`item.path`**（= 盘上那一条的绝对路径，和 `InstanceFilesPage::pathRole()`
    //   逐字同一个口径 —— 禁用着的那条带着 `.disabled`）。**不是**"说不清"那些：那几条
    //   按定义就是"判不出来"，把它们当成可更新是在说一句没有依据的话（报告里照旧按
    //   「说不清」单列一档）。
    // ★ 2026-10-13（§7.1）：**原始结论也留一份**（见 m_instanceUpdateResults 那段）。
    //   下面那份名单是"按忽略 / 固定筛过"的，而用户点了「不再忽略」之后不可能再跑一趟检测
    //   （那要读几十兆 jar、发几十发网络）—— 得拿这一份当场重算。
    UpdateCheckResult raw;
    raw.verdicts = ModDependencies::mergeScopedUpdates(
        m_instanceUpdateResults.value(instanceId).verdicts, run.verdicts, run.selectedScope);
    raw.lookupNote = run.lookupNote;
    m_instanceUpdateResults.insert(instanceId, raw);
    // ★ 2026-10-09（需求 2）：上面那份原始结论旁边再存一份"路径 → 换成哪一版"（见
    //   m_instanceUpdateTargets 那段）。**和上面那一句同一处**：两份东西说的本来就是同
    //   一件事的两面，"哪几条要更新"和"换成哪一版"分开写迟早会对不上。
    auto targets = run.selectedScope.isEmpty()
        ? QHash<QString, ResourceCatalog::VersionEntry>() : m_instanceUpdateTargets.value(instanceId);
    for (const auto &path : run.selectedScope) targets.remove(path);
    for (auto it = run.latestByPath.cbegin(); it != run.latestByPath.cend(); ++it)
        targets.insert(it.key(), it.value());
    m_instanceUpdateTargets.insert(instanceId, targets);
    // ★ 2026-10-13（§7.1）：「固定这一版」那几条的判据是**盘上那一份的内容 sha1**，所以
    //   用户自己把文件换过之后那条记录就该失效。这一趟检测正好把每个文件的 sha1 都算出来
    //   了 —— 顺手清一遍（不清的话那几条会一直挂着"已固定"，而他明明已经换成新的了）。
    {
        QHash<QString, QString> sha1ByKey;
        const QString instanceDirForPolicy = AppPaths::versionDir(instanceId);
        for (const ModDependencies::UpdateVerdict &verdict : run.verdicts) {
            const QString key = UpdatePolicy::storeKey(
                instanceDirForPolicy, InstanceFilesPage::identityPath(verdict.item.path));
            if (!key.isEmpty()) {
                sha1ByKey.insert(key, verdict.item.sha1);
            }
        }
        // Missing keys in a partial scan are not evidence of removed files.
        if (run.selectedScope.isEmpty() && UpdatePolicy::dropStalePins(m_updatePolicy, instanceId, sha1ByKey) > 0) {
            saveUpdatePolicy();
        }
    }
    // 名单由**唯一那一处**按现在这本账算（见 rebuildInstanceOutdatedPaths）。
    rebuildInstanceOutdatedPaths(instanceId);
    // **先记账再递**：递出去的那一份是上面这一行写的（不是从这里临时拼的），于是"这一页
    // 现在筛得出来的"和"下一次开这一页筛得出来的"永远是同一份。
    pushInstanceFileUpdates(run.dialog, instanceId);
    const auto byName = [](const ModDependencies::UpdateVerdict &a,
                           const ModDependencies::UpdateVerdict &b) {
        return a.item.title < b.item.title;
    };
    std::sort(outdated.begin(), outdated.end(), byName);
    std::sort(unknown.begin(), unknown.end(), byName);
    std::sort(current.begin(), current.end(), byName);

    const auto displayName = [](const ModDependencies::UpdateVerdict &verdict) {
        if (!verdict.item.title.isEmpty()) {
            return verdict.item.title;
        }
        // 名字取不到就用文件名 —— 那是**盘上那一条**的名字，用户能照它找到文件。
        return QFileInfo(verdict.item.path).fileName();
    };

    QStringList lines;
    for (const ModDependencies::UpdateVerdict &verdict : outdated) {
        lines.append(QStringLiteral("· %1 —— %2 %3")
                         .arg(displayName(verdict), ModDependencies::updateOutdatedText(),
                              verdict.latestVersionNumber));
    }
    for (const ModDependencies::UpdateVerdict &verdict : unknown) {
        lines.append(QStringLiteral("· %1 —— %2：%3")
                         .arg(displayName(verdict), ModDependencies::updateUnknownText(),
                              verdict.reason));
    }
    for (const ModDependencies::UpdateVerdict &verdict : current) {
        lines.append(QStringLiteral("· %1 —— %2")
                         .arg(displayName(verdict), ModDependencies::updateUpToDateText()));
    }

    const int checked = run.verdicts.size();
    const QString summary = ModDependencies::updateSummaryText(checked, outdated.size());

    appendLog(tr("更新检测（%1）：%2").arg(instanceDisplayName(instanceId), summary));
    for (const QString &line : lines) {
        appendLog(QStringLiteral("  ") + line);
    }
    // ★ 2026-10-13（§7.1）：**这段文本不再弹框**，改成开「更新中心」那一页 —— 每一行都
    //   带着自己的动作（逐条更新 / 忽略这一版 / 固定这一版），底下那颗能把上一次更新退回去。
    //   从前那段文本关掉就没了，而那正是用户要按下去的入口（`summary` / `lines` 照旧进日志，
    //   所以"这一趟查了什么"在日志里仍然查得到）。
    //
    // NOTE: 这一趟是异步回来的（反查 + 几发版本列表），用户完全可能已经把实例设置那一页
    //       收掉、去干别的了。`runWhenOverlayFree()` 会等覆盖层空下来再开（占着的那一层
    //       静默收掉，用户 2026-09-29 定的方向），和体检报告页同一个规矩。
    runWhenOverlayFree([this, instanceId] { openUpdateCenter(instanceId); });
}

// ===========================================================================
// 更新中心（coding plan §7.1，2026-10-13）
// ===========================================================================
//
// 这一段的形状：**判据在 `src/manage/UpdatePolicy`**（"这一条现在要不要提示"）、
// **回滚在 `src/manage/UpdateRollback`**（"动手之前先留一份旧的"）、**页面在
// `ui/UpdateCenterPage`**（只画不判）。这一层夹在中间，干的只有三件事：
//   ① 把上面那一趟检测的原始结论翻成页面认的那一份（`updateCenterReportForPage()`）；
//   ② 用户按下去之后把它翻成一个动作（更新 / 记一笔忽略固定 / 还原）；
//   ③ 每一批更新起一个任务账本条目，并**逐个资源**回报结果。
//
// 【为什么忽略 / 固定一动就要重算一遍名单】
//
// 「可更新」那一档在文件管理页上是个**筛选**（`m_instanceOutdatedPaths`）。不重算的话，
// 用户刚忽略掉的那一条仍然算在"可更新 3 条"里 —— 而他明明看着那一行写着「已忽略这一版」。

void MainWindow::rebuildInstanceOutdatedPaths(const QString &instanceId) {
    if (instanceId.isEmpty()) {
        return;
    }
    // 没查过 = 什么都不动。★ **不能**在那儿写一个空名单："键在不在 = 查过没有"（见
    // m_instanceOutdatedPaths 那段），塞一个空表进去等于替用户说了一句"查过了，都是最新"。
    const auto result = m_instanceUpdateResults.constFind(instanceId);
    if (result == m_instanceUpdateResults.constEnd()) {
        return;
    }
    const QString instanceDir = AppPaths::versionDir(instanceId);

    QStringList outdatedPaths;
    for (const ModDependencies::UpdateVerdict &verdict : result.value().verdicts) {
        if (verdict.state != ModDependencies::UpdateState::Outdated) {
            continue;
        }
        // 判据**只此一处**（见 UpdatePolicy 文件头：忽略记的是"那一版"、固定记的是"那一份"）。
        // 钥匙先摘掉 `.disabled` —— 和文件管理页那把钥匙逐字同一个（`identityPath()`）。
        const UpdatePolicy::Entry entry = UpdatePolicy::entryOf(
            m_updatePolicy, instanceId,
            UpdatePolicy::storeKey(instanceDir, InstanceFilesPage::identityPath(verdict.item.path)));
        if (UpdatePolicy::suppressed(entry, verdict.item.sha1, verdict.latestVersionId)
            != UpdatePolicy::Suppress::None) {
            continue;
        }
        outdatedPaths.append(verdict.item.path);
    }
    m_instanceOutdatedPaths.insert(instanceId, outdatedPaths);

    // 这一页要是正开着就**当场重摆**（用户刚按下的就是它上面那一行）。
    if (m_updateCenterPage != nullptr && m_updateCenterInstanceId == instanceId) {
        m_updateCenterPage->setReport(updateCenterReportForPage(instanceId));
    }
}

bool MainWindow::saveUpdatePolicy() {
    QString problem;
    if (UpdatePolicy::save(m_updatePolicy, UpdatePolicy::storePath(), &problem)) {
        return true;
    }
    // 写不成**只写日志**：用户按的是一颗"以后别提示我"，弹一个"存不进去"的框除了让他分心
    // 以外没有下一步可做 —— 而那一份记录在这一次会话里仍然是生效的（内存里那份还在）。
    appendLog(tr("「更新中心」那份记录没能写进盘：%1").arg(problem));
    return false;
}

void MainWindow::setInstanceMode(MainWindowUI::InstanceMode mode) {
    m_instanceMode = mode;
    if (m_ui != nullptr) {
        m_ui->setInstanceMode(mode);  // 停在哪一页 + 谁勾着（唯一出处）
    }
    if (mode == MainWindowUI::InstanceMode::Server) {
        // 切到服务端那一页时顺手重扫一遍：盘上那个 Servers/ 可能刚被别的东西动过
        // （用户在资源管理器里删了一个、或者拖进来一个装好的）。
        refreshServerList();
    } else if (mode == MainWindowUI::InstanceMode::Preset && m_presetController != nullptr) {
        // 预设那一页是常驻的，切过来时刷新一遍（列表 / 实例下拉框都可能刚变过）。
        m_presetController->open();
    }
}

namespace {

/// 服务端那一档在 `VersionIcons::loaderMark()` 那张表里的下标。
///
/// 那张表的顺序是 0 Fabric / 1 Forge / 2 NeoForge / 3 OptiFine / 4 草方块（「不装（纯原版）」）。
/// 服务端八档里只有三个加载器有自己那张图，其余（原版 / Paper / Purpur / Quilt / Spigot）
/// 一律拿草方块 —— 和"认不出下标就给草方块"是同一条规矩，别去新画一张。
int loaderMarkIndexForServerFlavor(ServerInstall::Flavor flavor) {
    switch (flavor) {
    case ServerInstall::Flavor::Fabric:
        return 0;
    case ServerInstall::Flavor::Forge:
        return 1;
    case ServerInstall::Flavor::NeoForge:
        return 2;
    default:
        return 4;
    }
}

/// 启动器日志里一行最多留这么多个字符。服务端那些行可以很长（mod 列表、堆栈），
/// 原样抄进去的话底栏那个日志窗会被一条长句撑得没法看。**只截写进日志的这一份**，
/// 控制台那一页里照旧是原话。
constexpr int kMaxLoggedServerLineChars = 400;

}  // namespace

void MainWindow::refreshServerList() {
    if (m_ui == nullptr) {
        return;
    }
    QTreeWidget *tree = m_ui->getServerList();
    if (tree == nullptr) {
        return;
    }
    const QString keepId = [&tree] {
        const QTreeWidgetItem *current = tree->currentItem();
        return current == nullptr ? QString() : current->data(0, Qt::UserRole).toString();
    }();

    tree->clear();
    const QList<ServerRegistry::Entry> entries = ServerRegistry::scanAll();
    const QSize rowSize(0, MainWindowUI::serverWorkspaceRowHeight());
    for (const ServerRegistry::Entry &entry : entries) {
        auto *item = new QTreeWidgetItem(tree);
        item->setData(0, Qt::UserRole, entry.serverId);
        // 行首那张图：档名对应的加载器标志（认不出来的档一律草方块，见那一处）。
        item->setIcon(0, QIcon(VersionIcons::loaderMark(
                            loaderMarkIndexForServerFlavor(entry.flavor),
                            MainWindowUI::instanceTreeIconSize())));
        item->setSizeHint(0, rowSize);
        item->setSizeHint(1, rowSize);
        // 第 1 列那颗按钮：一个服务端一颗（行尾）。**整行做成控件是不行的** —— 名字那一截
        // 的文字渲染已经在了，为了塞一颗按钮把整行换成自绘控件，等于把这一页推回重画。
        // 摆两列、第二列固定宽，是把"行尾有一颗按钮"这件事塞进现成列表里最省的做法。
        auto *button = new QPushButton(tree);
        button->setObjectName(MainWindowUI::serverRowActionName());
        button->setFixedWidth(MainWindowUI::serverRowActionWidth());
        connect(button, &QPushButton::clicked, this,
                [this, id = entry.serverId] { activateServerRow(id); });
        tree->setItemWidget(item, 1, button);
        // 名字 / 状态字 / 那颗按钮的字与颜色 —— **全在 updateServerRow() 一处**
        // （刚建出来的那一行也要走一遍，别在这儿再抄一份摆法）。
        updateServerRow(entry.serverId);
    }
    if (QTreeWidgetItem *first = tree->topLevelItem(0)) {
        tree->setCurrentItem(first);
    }
    // 把选中项还回去（重扫前后停在同一行上）。用户上一次选的那一个还在就选它。
    if (!keepId.isEmpty()) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = tree->topLevelItem(i);
            if (item != nullptr && item->data(0, Qt::UserRole).toString() == keepId) {
                tree->setCurrentItem(item);
                break;
            }
        }
    }
    if (QLabel *empty = m_ui->getServerEmptyLabel()) {
        empty->setVisible(entries.isEmpty());
    }
}

void MainWindow::openNewServerPage() {
    OverlayHost *overlay = m_ui == nullptr ? nullptr : m_ui->overlay();
    if (overlay == nullptr) {
        return;
    }
    if (m_newServerPage == nullptr) {
        m_newServerPage = new NewServerPage(this);
        connect(m_newServerPage, &NewServerPage::fieldsChanged, this,
                [this] { updateNewServerSummary(m_newServerPage); });
    }
    // 摆那八档（**每次开页都重喂**：页面是复用的）。哪几档要填哪几格由这一层翻好
    // （`ServerInstall` 那一层知道，页面不知道）。
    QList<NewServerPage::FlavorOption> options;
    for (const ServerInstall::Flavor flavor : ServerInstall::allFlavors()) {
        NewServerPage::FlavorOption option;
        option.token = ServerInstall::flavorToken(flavor);
        option.name = ServerInstall::flavorName(flavor);
        option.note = ServerInstall::flavorNote(flavor);
        // 哪几档要哪几格 —— 和 `ServerInstall::planFor()` 里"缺它会 ok=false"的那几条一字对应。
        switch (flavor) {
        case ServerInstall::Flavor::Forge:
        case ServerInstall::Flavor::NeoForge:
            option.needsLoader = true;
            break;
        case ServerInstall::Flavor::Fabric:
            option.needsLoader = true;
            option.needsInstaller = true;
            break;
        case ServerInstall::Flavor::Quilt:
            option.needsInstaller = true;
            break;
        case ServerInstall::Flavor::Paper:
        case ServerInstall::Flavor::Purpur:
            option.needsBuild = true;
            break;
        default:
            break;
        }
        option.needsLoaderGame = true;
        options.append(option);
    }
    m_newServerPage->setFlavors(options, ServerInstall::flavorToken(ServerInstall::Flavor::Vanilla));
    // 默认名照用户真机上那份叫法（`Server<版本>`）。用当前主页选中的那一个版本当缺省
    // （`selectedVersionId()` 是"下拉框里那个 id 是什么"的唯一出处，见本文件上面那个
    // 自由函数 —— 它已经处理了"空着 = 没选"）
    const QString currentVersion = selectedVersionId(m_ui->getInstalledVersionCombo());
    m_newServerPage->setDefaults(
        ServerRegistry::defaultServerName(ServerInstall::Flavor::Vanilla, currentVersion),
        currentVersion);
    updateNewServerSummary(m_newServerPage);
    // ★ 同控制台那一页：说清 `ownerTab` 并先切过去（这一页也是从服务端列表上开出来的）。
    const int instancesTab = MainWindowUI::instancesTabIndex();
    if (QTabWidget *tabs = m_ui->getTabWidget()) {
        if (instancesTab >= 0 && instancesTab < tabs->count()) {
            tabs->setCurrentIndex(instancesTab);
        }
    }
    if (overlay->execPage(m_newServerPage, NewServerPage::windowTitle(), instancesTab)
        != QDialog::Accepted) {
        return;
    }
    // 收页了 —— 建这个服务端：名字洗成目录名、重名加后缀、写 `.wtfml-server`。
    const QString desired = ServerRegistry::sanitizeId(m_newServerPage->serverName());
    if (desired.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), QStringLiteral("这个名字不能当目录名"),
                               QStringLiteral("换一个名字再试（名字里不能只有标点或空白）。"));
        return;
    }
    const QString serverId = ServerRegistry::uniqueServerId(desired);
    ServerModel::Info info;
    info.name = m_newServerPage->serverName();
    ServerInstall::Flavor flavor = ServerInstall::Flavor::Vanilla;
    if (ServerInstall::flavorFromToken(m_newServerPage->flavorToken(), &flavor)) {
        info.flavor = flavor;
        info.flavorKnown = true;
    }
    info.gameVersion = m_newServerPage->gameVersion();
    info.loaderVersion = m_newServerPage->loaderVersion();
    info.installerVersion = m_newServerPage->installerVersion();
    info.buildId = m_newServerPage->buildId();
    info.memoryMb = m_newServerPage->memoryMb();
    QString problem;
    if (!ServerRegistry::createServer(serverId, info, &problem)) {
        InlineMessage::critical(m_ui->overlay(), QStringLiteral("建不了这个服务端"), problem);
        return;
    }
    // 切到服务端那一页（用户刚建的就在那儿），并重扫一遍让它出现在列表里。
    setInstanceMode(MainWindowUI::InstanceMode::Server);
    // ★ 用户 2026-10-13 定的：「创建」= 建目录 + 记一份账 + **当趟就把东西下下来 / 跑安装器**
    //   （不是"建完让用户自己再点一下安装"）。排在切页之后：那一行先以「未安装」露出来，
    //   紧接着任务中心就动起来 —— 用户看到的是"这一行正在变成能用的"。
    ServerInstall::Request request;
    request.flavor = info.flavor;
    request.gameVersion = info.gameVersion;
    request.loaderVersion = info.loaderVersion;
    request.installerVersion = info.installerVersion;
    request.buildId = info.buildId;
    installServer(serverId, request);
}

void MainWindow::installServer(const QString &serverId, const ServerInstall::Request &request) {
    if (m_serverInstall == nullptr || serverId.isEmpty()) {
        return;
    }
    // §2.3：先单任务聚合。下载那一层只有一份批次状态，同时开两批会让两边的收尾互相算错
    // —— 所以忙的时候说清楚，而不是让它排着队（队列是 M5 的事）。
    if (m_serverInstall->isRunning()) {
        InlineMessage::warning(m_ui->overlay(), tr("先等这一个装完"),
                               tr("同一时间只装一个服务端 —— 下载是共用的一条路，两批一起跑"
                                  "会把彼此的进度算乱。"));
        return;
    }
    // 这一档这一轮接没接。**判据的唯一出处是 `ServerInstallRun::flavorSupported()`** ——
    // 页面上那颗「创建」的禁用读的也是它（Spigot：列出但明说暂不支持）。
    if (!ServerInstallRun::flavorSupported(request.flavor)) {
        InlineMessage::warning(m_ui->overlay(), tr("这一档还没接"),
                               ServerInstallRun::unsupportedReason(request.flavor));
        return;
    }
    const ServerInstall::Plan plan = ServerInstall::planFor(request);
    if (!plan.ok) {
        // `planFor()` 给的 `problem` 就是给用户看的那句话（那一层承诺过），别再另编一句。
        InlineMessage::critical(m_ui->overlay(), tr("装不了这个服务端"), plan.problem);
        return;
    }
    // 跑安装器的那三档（Forge / NeoForge / Quilt）才要 java。**在记账之前**问：
    // 问不到就什么都没排（不往任务中心里塞一项注定会失败的活）。
    QString java;
    if (!pickServerInstallerJava(request, &java)) {
        return;  // 已经弹过窗说清楚为什么了
    }

    const QStringList names = ServerInstallRun::stepNames(request, plan);
    const Tasks::TaskId id = beginTask(Tasks::TaskKind::Install,
                                       tr("安装服务端 %1").arg(serverId), serverId,
                                       taskStepsFromNames(names, 0));
    m_serverInstallTaskId = id;
    m_serverInstallStepCount = names.size();
    // NOTE: 百分比按"走到第几格"算（`stepsChanged` / `stepChanged` 那两条连线里往前推）。
    //       服务端安装的总量（一共要下多少字节）要等元数据取回来才知道，在那之前报任何
    //       百分数都是编的 —— 同 `Tasks::percentOf()` 那条"测不出就不显示"的规矩。
    m_activityCenter->tasks()->setProgress(id, 0, qMax(1, names.size()));

    // ★ 起手先切到服务端那一页：用户刚点的就是那儿，而"装到哪个实例里"这件事在这一页上
    //   （任务中心那一条也带 targetId，点「打开目标位置」就是它的目录）。
    setInstanceMode(MainWindowUI::InstanceMode::Server);
    m_serverInstall->start(request, serverId, java);
}

bool MainWindow::pickServerInstallerJava(const ServerInstall::Request &request, QString *out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    // 这一档跑不跑安装器 —— 判据是计划里有没有给工具参数（`installerJarName()` 的唯一出处
    // 就在那一层，这里不按"是哪一档"另判一遍）。
    const ServerInstall::Plan plan = ServerInstall::planFor(request);
    if (!plan.ok || ServerInstallRun::installerJarName(plan).isEmpty()) {
        return true;  // 不跑安装器 ⇒ 用不上 java（下下来就是服务端）
    }
    // 判据和装加载器那条路（`pickInstallerJava()`）**同一套**：版本 json 里的 `javaVersion`
    // 说了算；盘上没有这个版本的 json（客户端还没装）就不知道要求，此时原样用配的那一份
    // —— 不在这一步拦人（猜一个主版本比不猜更危险，同那边的理由）。
    const QString configuredJava = m_javaSettings->javaPath();
    QString java = configuredJava;
    int requiredJavaMajor = 0;
    const QJsonObject gameJson = m_versionManager->getVersionJson(request.gameVersion);
    if (!gameJson.isEmpty()) {
        requiredJavaMajor = gameJson[QStringLiteral("javaVersion")]
                                .toObject()[QStringLiteral("majorVersion")]
                                .toInt();
    }
    QString reason;
    if (!m_javaSettings->installerJavaFor(requiredJavaMajor, configuredJava, &java, &reason)) {
        appendLog(tr("装这个服务端要的 Java 拿不到：%1").arg(reason));
        InlineMessage::warning(m_ui->overlay(), tr("Java 版本不够"), reason);
        return false;
    }
    if (java != configuredJava) {
        appendLog(tr("这个版本要 Java %1，装的时候改用 %2").arg(requiredJavaMajor).arg(java));
    }
    *out = java;
    return true;
}

// ======================================================================
// 服务端的「跑起来 / 停下来 / 控制台」（coding plan §6.2 的操作 + §6.3 的停止时序 / M4-3）
// ======================================================================
//
// 这一片里**没有一条判据**：那条命令怎么拼、起来没有、退了算哪一档状态、宽限多久才允许
// 强杀，全在 `ServerRun.hpp`（纯头，verify 的 ZZ 一节钉死）；进程本身在 `ServerProcess`
// （mw_check 的 T41 拿本工程自己的 exe 当"服务端"跑整趟）。这一片是**接线** ——
// "谁在什么时候按了什么 → 调哪一个"，以及"界面上那颗按钮现在该写什么字"。

void MainWindow::activateServerRow(const QString &serverId) {
    if (serverId.isEmpty()) {
        return;
    }
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    if (!entry.installed) {
        repairServer(serverId);  // 未装好 ⇒ 那颗按钮就是「修复」
        return;
    }
    const ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    if (process != nullptr && process->isRunning()) {
        stopServer(serverId);
        return;
    }
    startServer(serverId);
}

void MainWindow::startServer(const QString &serverId, bool automatic) {
    if (serverId.isEmpty()) {
        return;
    }
    // 用户亲手起的 = 新的一轮：上一轮的"连着崩几次"从这里清零。自动重启那一路**不清**
    // （理由见 `ServerRestart::decide()`：清了就等于不限次数）。
    if (!automatic) {
        m_serverRestartAttempts.remove(serverId);
    }
    m_serverStopRequested.remove(serverId);
    // 闸①：装好了没有。判据是 `ServerRegistry::describe()`（= `ServerModel::isInstalled()`），
    // 和那一行上写着「未安装」的是同一个出处。
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    if (!entry.installed) {
        InlineMessage::warning(m_ui->overlay(), tr("这个服务端还不能启动"),
                               ServerRun::notInstalledMessage());
        return;
    }
    ServerRun::ServerProcess *process = m_serverWorkspace->ensureProcess(serverId);
    if (process == nullptr) {
        return;
    }
    // 闸②：已经在跑了（同一行连点两下、或者右键菜单里又点了一次）。
    if (process->isRunning()) {
        appendLog(tr("%1 已经在跑了。").arg(serverId));
        return;
    }
    // 闸③：EULA。**只有用户自己勾了才写**（见 ensureServerEula）。
    if (!ensureServerEula(serverId)) {
        return;
    }
    // 闸④：那条命令拼得出来吗（这一档要的信息在不在我们记的那一份里）。
    QString problem;
    const ServerInstall::Plan plan = serverLaunchPlan(serverId, &problem);
    if (!plan.ok) {
        InlineMessage::critical(m_ui->overlay(), tr("起不了这个服务端"), problem);
        return;
    }
    // 闸⑤：java。判据和装加载器/装服务端那两条路**同一套**（版本 json 里的 `javaVersion`
    // 说了算），拿不到就说清楚为什么。
    const ServerModel::Info info = ServerRegistry::readMeta(serverId);
    // 这一个自己指定过 Java 就用它（`Info::javaPath` 的原话），否则跟全局那一套。
    const QString configuredJava =
        info.javaPath.isEmpty() ? m_javaSettings->javaPath() : info.javaPath;
    QString java = configuredJava;
    int requiredJavaMajor = 0;
    const QJsonObject gameJson = m_versionManager->getVersionJson(info.gameVersion);
    if (!gameJson.isEmpty()) {
        requiredJavaMajor = gameJson[QStringLiteral("javaVersion")]
                                .toObject()[QStringLiteral("majorVersion")]
                                .toInt();
    }
    QString javaReason;
    if (!m_javaSettings->installerJavaFor(requiredJavaMajor, configuredJava, &java, &javaReason)) {
        appendLog(tr("起这个服务端要的 Java 拿不到：%1").arg(javaReason));
        InlineMessage::warning(m_ui->overlay(), tr("Java 版本不够"), javaReason);
        return;
    }
    if (java != configuredJava) {
        appendLog(tr("这个服务端要 Java %1，起的时候用 %2").arg(requiredJavaMajor).arg(java));
    }

    // 那条命令 + 宽限那两个入参（模组数量 / 存档大小，见 ServerRun.hpp 那张表）。
    // 内存：这一个自己配过就用它，没配过（`memoryMb <= 0`，见 `ServerModel::Info` 那句）
    // 就跟全局那一档 —— 和上面 java 的规矩一样。
    const int memoryMb = info.memoryMb > 0 ? info.memoryMb : m_javaSettings->xmx();
    const QStringList arguments = ServerRun::launchArguments(plan, memoryMb);
    const int mods = countServerMods(entry.dirPath);
    const qint64 worldBytes = serverWorldBytes(entry.dirPath);
    const int gracefulMs = ServerRun::gracefulStopMs(mods, worldBytes);
    appendLog(tr("起服务端 %1：%2 个模组、存档 %3 MB，停下来最多等 %4 秒。")
                  .arg(entry.name.isEmpty() ? serverId : entry.name)
                  .arg(mods)
                  .arg(worldBytes / (1024 * 1024))
                  .arg(gracefulMs / 1000));
    if (!plan.note.isEmpty()) {
        // 那一档自己的补充说明（"第一趟要联网""入口是两张参数文件"…）——**唯一出处**，
        // 照抄，别在这儿另编一句。
        appendLog(plan.note);
    }
    m_serverStartedAt.insert(serverId, QDateTime::currentDateTime());
    process->start(serverId, entry.dirPath, java, arguments, gracefulMs);
}

void MainWindow::stopServer(const QString &serverId) {
    ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    if (process == nullptr || !process->isRunning()) {
        return;
    }
    // ★ "这一次是我们让它停的"：退出回调要拿这个把 `Crash` 那一档排掉（见 `onServerExited`）。
    m_serverStopRequested.insert(serverId);
    if (process->requestStop()) {
        return;
    }
    // 那一句写不进去（它的 stdin 已经关了 / 它根本不在读）⇒ 只剩强杀一条路。这时候还
    // 摆着"再等 X 秒"就是骗人，直接说实话。
    appendLog(tr("%1：让它停的那一句没能写进去，只能强制结束。").arg(serverId));
    process->forceKill();
}

void MainWindow::forceKillServer(const QString &serverId) {
    ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    if (process == nullptr || !process->isRunning()) {
        return;
    }
    // 同上：这是用户让它死的，退出码再难看也不是"崩了"。
    m_serverStopRequested.insert(serverId);
    process->forceKill();
}

void MainWindow::openServerConsole(const QString &serverId) {
    // 保留这个兼容入口：服务端菜单和 ConnectionsController 按这个名字调用。
    m_serverConsole->open(serverId);
}

void MainWindow::updateServerConsole() {
    // 状态那几行的唯一出处现在在控制器里（见 ServerConsoleController::refresh()）。
    m_serverConsole->refresh();
}

void MainWindow::openServerFolder(const QString &serverId) {
    if (serverId.isEmpty()) {
        return;
    }
    const QString dir = ServerRegistry::describe(serverId).dirPath;
    appendLog(tr("正在打开 %1。").arg(QDir::toNativeSeparators(dir)));
    // 走 QDesktopServices 而不是自己 QProcess 起 explorer.exe（和"打开实例文件夹"同一条路）。
    // 开不了要说一句：静静地什么都不发生，用户只会一遍遍点它。
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        InlineMessage::warning(m_ui->overlay(), tr("打不开这个文件夹"),
                               tr("没能打开：\n%1").arg(QDir::toNativeSeparators(dir)));
    }
}

void MainWindow::repairServer(const QString &serverId) {
    if (serverId.isEmpty()) {
        return;
    }
    const ServerModel::Info info = ServerRegistry::readMeta(serverId);
    if (info.isEmpty()) {
        // 从外面拖进来的服务端就是这样：盘上有，但那一份"我们记的"不在 ⇒ 不知道它该按
        // 哪一档重装。这句话要给出下一步，不能只说"修不了"。
        InlineMessage::warning(
            m_ui->overlay(), tr("没法重装这一个"),
            tr("这一个不是启动器装的（目录里没有我们记的那一份），所以我们不知道该按哪一档"
               "把它补齐。\n让它自己下好入口文件，或者删了重新建一个。"));
        return;
    }
    ServerInstall::Request request;
    request.flavor = info.flavor;
    request.gameVersion = info.gameVersion;
    request.loaderVersion = info.loaderVersion;
    request.installerVersion = info.installerVersion;
    request.buildId = info.buildId;
    installServer(serverId, request);
}

void MainWindow::removeServer(const QString &serverId) {
    if (serverId.isEmpty()) {
        return;
    }
    const ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    if (process != nullptr && process->isRunning()) {
        // 跑着的进程还开着这个目录里的文件（世界那几份是它自己在写），先让它停下。
        InlineMessage::warning(m_ui->overlay(), tr("它还在跑"),
                               tr("先把这一个服务端停下来再删 —— 它现在正开着目录里的文件。"));
        return;
    }
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    const QString shown = entry.name.isEmpty() ? serverId : entry.name;
    // ★ 这是**唯一一处会删用户东西的地方**，而且删的是别人几个月的存档 —— 所以：默认那
    //   一边是「取消」（`defaultYes = false`，回车不会把它删掉）、那颗按钮按危险档染红。
    const bool go = InlineMessage::question(
        m_ui->overlay(), tr("删掉这个服务端"),
        tr("「%1」这一整个目录会被删掉 —— 里面的世界（world/）、模组、配置**一起没**，"
           "删了找不回来。\n\n确定要删吗？")
            .arg(shown),
        tr("删掉它"), tr("取消"), false, true);
    if (!go) {
        return;
    }
    QString problem;
    if (!ServerRegistry::removeServer(serverId, &problem)) {
        InlineMessage::critical(m_ui->overlay(), tr("删不掉"), problem);
        return;
    }
    // 进程对象一起收掉：它现在没在跑，但表里留着的话，下次 `describe()` 同名的那个会
    // 拿到上一辈子的状态（而且行都没了，它再发信号就是对着空气说）。
    m_serverWorkspace->removeProcess(serverId);
    m_serverConsole->forget(serverId);
    appendLog(tr("已删除服务端 %1。").arg(serverId));
    refreshServerList();
}

void MainWindow::showServerContextMenu(const QPoint &pos) {
    QTreeWidget *tree = m_ui == nullptr ? nullptr : m_ui->getServerList();
    if (tree == nullptr) {
        return;
    }
    QTreeWidgetItem *item = tree->itemAt(pos);
    if (item == nullptr) {
        return;
    }
    const QString serverId = item->data(0, Qt::UserRole).toString();
    if (serverId.isEmpty()) {
        return;  // 空白处右键：什么都不弹（这一页没有"对整个列表做的事"）
    }
    tree->setCurrentItem(item);
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    const ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    const bool running = process != nullptr && process->isRunning();

    QMenu menu(this);
    for (int i = 0; i < MainWindowUI::serverMenuActionCount(); ++i) {
        const auto action = static_cast<MainWindowUI::ServerMenuAction>(i);
        // ★ 用不上的那几项**不摆**（而不是摆一颗灰的）：
        //   装好了就没有「修复」；没在跑就没有「强制结束」—— 摆一颗永远点不动的项，
        //   比不摆它更容易让人以为界面坏了。
        if (action == MainWindowUI::ServerMenuAction::Repair && entry.installed) {
            continue;
        }
        if (action == MainWindowUI::ServerMenuAction::ForceKill && !running) {
            continue;
        }
        QAction *entryAction = menu.addAction(MainWindowUI::serverMenuText(action));
        if (action == MainWindowUI::ServerMenuAction::RestartOnCrash) {
            // 可勾选：勾没勾读的是盘上那一份（`.wtfml-server` 的 `restartOnCrash`）——
            // 这一项是**长期设置**，不是"这一次会话"的开关。
            entryAction->setCheckable(true);
            entryAction->setChecked(ServerRegistry::readMeta(serverId).restartOnCrash);
            entryAction->setToolTip(MainWindowUI::serverRestartMenuNote());
        }
        if (action == MainWindowUI::ServerMenuAction::ForceKill) {
            // 宽限时间那一条：没到点之前是不给按的（用户 2026-10-13 定的 —— 存盘中途被
            // 打死正是丢存档的姿势）。
            const bool allowed = process != nullptr && process->forceKillAllowed();
            entryAction->setEnabled(allowed);
            entryAction->setToolTip(allowed
                                        ? ServerRun::forceKillNote(0)
                                        : ServerRun::stoppingNote(
                                              (process != nullptr ? process->gracefulMs() : 0)
                                              / 1000));
        }
        connect(entryAction, &QAction::triggered, this,
                [this, serverId, i] { runServerMenuAction(serverId, i); });
    }
    if (menu.isEmpty()) {
        return;
    }
    menu.exec(tree->viewport()->mapToGlobal(pos));
}

void MainWindow::runServerMenuAction(const QString &serverId, int action) {
    switch (static_cast<MainWindowUI::ServerMenuAction>(action)) {
    case MainWindowUI::ServerMenuAction::Console:
        openServerConsole(serverId);
        break;
    case MainWindowUI::ServerMenuAction::Folder:
        openServerFolder(serverId);
        break;
    case MainWindowUI::ServerMenuAction::Settings:
        openServerSettingsPage(serverId);
        break;
    case MainWindowUI::ServerMenuAction::Repair:
        repairServer(serverId);
        break;
    case MainWindowUI::ServerMenuAction::ForceKill:
        forceKillServer(serverId);
        break;
    case MainWindowUI::ServerMenuAction::RestartOnCrash: {
        // 勾 / 取消勾：写进 `.wtfml-server`（下一次开菜单读的就是它）。写不进去要说一声 ——
        // 一项"看起来勾上了、其实没记住"的设置比没有这一项更糟。
        ServerModel::Info info = ServerRegistry::readMeta(serverId);
        info.restartOnCrash = !info.restartOnCrash;
        QString problem;
        if (!ServerRegistry::writeMeta(serverId, info, &problem)) {
            InlineMessage::warning(m_ui->overlay(), tr("没记住"), problem);
            return;
        }
        if (!info.restartOnCrash) {
            // 关掉 = 这一轮到此为止（再打开时从第 1 次算起）。
            m_serverRestartAttempts.remove(serverId);
        }
        appendLog(tr("服务端 %1：异常退出后自动重启%2。")
                      .arg(serverId, info.restartOnCrash ? tr("已打开") : tr("已关闭")));
        break;
    }
    case MainWindowUI::ServerMenuAction::Remove:
        removeServer(serverId);
        break;
    }
}

// ===========================================================================
// 「服务端设置」那一页（coding plan §6.3 / 里程碑 M4-4）
// ===========================================================================
//
// 这一页上摆着四样东西，每一样背后都有一层判据（见 MainWindow.hpp 里那段）：
//
//   ① 常用设置   `ServerPropertiesForm`（那十几个旋钮的表与值域）
//   ② 原始文本   这一层手里那份**原文**（`ServerModel::parseProperties` 解它）
//   ③ 世界备份   `BackupFiles`（M2 那一套引擎 / 任务账本，和世界页**共用同一条路**）
//   ④ 体检       `ServerCheck`（纯值进、结论出）+ `ServerPortCheck`（真占一次端口）
//
// ★ 这一节里会写盘的只有两处：`saveServerSettings()` 和那三颗备份按钮 —— 其余全是读。
// ★ 两种编辑方式怎么不打起来（表单 / 原始文本），见 `saveServerSettings()` 那段。

// 「12.4 MB」那一句的唯一出处（定义在世界页 / 实例页那一批下面）—— 这一节在它前面，
// 所以要自己把它引出来（**不是另写一份**：这个格式两边必须一模一样）。
static QString backupSizeText(qint64 bytes);

namespace {

/// 一个服务端世界的备份在盘上认哪一串前缀。
///
/// ★ **不能只用世界目录名**：`ServerRegistry` 底下两个服务端都叫 `world` 是常态，而挑备份
///   那一套（`pickBackupZip()`）是按**文件名前缀**筛的 —— 只写世界名的话，两边会互相挑到
///   对方的备份。加上 serverId（目录名，唯一）就不会撞。
///
/// NOTE: 中间那个 `-` 不会被 `BackupFiles::safeLabel()` 换成别的东西（它换的是
///       `\/:*?"<>|` 和一切控制字符），所以前缀里的分隔符一路原样。
/// 这一份的文件名是不是"还原之前自动做的那一份"（安全快照）。
///
/// ★ 从**文件名**认，不去读清单：读一份清单要起一次 PowerShell，而这一列里可能摆着几十份
///   （见 `BackupFiles::backupFilePrefix()` 那段）。那个记号本身是 `backupFileName()` 定的
///   ——「恢复前-」夹在前缀和时间戳中间。
bool backupNameIsSafety(const QString &fileName, const QString &prefix) {
    return fileName.mid(prefix.size()).startsWith(QStringLiteral("恢复前-"));
}

/// 盘上那一份 `server.properties` 的原文（文件不在 / 读不动 = 空串）。
/// 编码跟着 `ServerRegistry` 那一对（`readProperties` / `writeProperties` 都是 UTF-8）。
}  // namespace

void MainWindow::openServerSettingsPage(const QString &serverId) {
    OverlayHost *overlay = m_ui == nullptr ? nullptr : m_ui->overlay();
    if (overlay == nullptr || serverId.isEmpty()) {
        return;
    }
    if (m_serverSettingsPage == nullptr) {
        m_serverSettingsPage = new ServerSettingsPage(this);
        // NOTE: 这一批只接一次（页面复用）。**认人认 `m_serverSettingsId`，不认"列表上现在
        //       选中谁"** —— 同控制台那一页那条理由。
        connect(m_serverSettingsPage, &ServerSettingsPage::sectionChanged, this,
                [this](int section) { mergeServerSettingsEdits(-1, section); });
        connect(m_serverSettingsPage, &ServerSettingsPage::edited, this,
                [this] { updateServerSettingsSaveState(); });
        connect(m_serverSettingsPage, &ServerSettingsPage::saveRequested, this,
                [this] { saveServerSettings(); });
        connect(m_serverSettingsPage, &ServerSettingsPage::revertRequested, this,
                [this] { revertServerSettings(); });
        connect(m_serverSettingsPage, &ServerSettingsPage::backupNowRequested, this,
                [this] { backupServerWorld(m_serverSettingsId); });
        connect(m_serverSettingsPage, &ServerSettingsPage::restoreBackupRequested, this,
                [this](const QString &zipPath) {
                    restoreServerWorldBackup(m_serverSettingsId, zipPath);
                });
        connect(m_serverSettingsPage, &ServerSettingsPage::deleteBackupRequested, this,
                [this](const QString &zipPath) { deleteServerBackup(zipPath); });
        connect(m_serverSettingsPage, &ServerSettingsPage::openBackupFolderRequested, this,
                [this] { openBackupFolder(BackupFiles::Kind::World); });
        connect(m_serverSettingsPage, &ServerSettingsPage::recheckRequested, this,
                [this] { runServerCheck(); });
        connect(m_serverSettingsPage, &ServerSettingsPage::actionRequested, this,
                [this](const QString &token) { runServerCheckAction(token); });
        // 玩家名单（§9.7）：换一份名单就地重摆；加 / 移除由这一层问话、写盘。
        connect(m_serverSettingsPage, &ServerSettingsPage::playerListChanged, this,
                [this](int) { refreshServerPlayers(); });
        connect(m_serverSettingsPage, &ServerSettingsPage::playerAddRequested, this,
                [this] { addServerPlayer(); });
        connect(m_serverSettingsPage, &ServerSettingsPage::playerRemoveRequested, this,
                [this](const QString &identity) { removeServerPlayer(identity); });
        // 资源包那一颗（§9.7）：选本地 zip、算 SHA-1。
        connect(m_serverSettingsPage, &ServerSettingsPage::resourcePackPickRequested, this,
                [this] { pickServerResourcePack(); });
        // 内容那一页（§9.7）：换类就地重摆；加 / 启用禁用 / 打开文件夹 / 刷新由这一层做。
        connect(m_serverSettingsPage, &ServerSettingsPage::contentKindChanged, this,
                [this](int) { refreshServerContent(); });
        connect(m_serverSettingsPage, &ServerSettingsPage::contentAddRequested, this,
                [this] { addServerContent(); });
        connect(m_serverSettingsPage, &ServerSettingsPage::contentToggleRequested, this,
                [this](const QString &name, bool enable) { toggleServerContent(name, enable); });
        connect(m_serverSettingsPage, &ServerSettingsPage::contentOpenFolderRequested, this,
                [this] { openServerContentFolder(); });
        connect(m_serverSettingsPage, &ServerSettingsPage::contentRefreshRequested, this,
                [this] { refreshServerContent(); });
        // 「踢出…」：送一条 kick 命令进正在跑的服务端（§9.7：必须走控制台、回显结果）。
        connect(m_serverSettingsPage, &ServerSettingsPage::playerKickRequested, this,
                [this](const QString &name) { kickServerPlayer(name); });
    }
    m_serverSettingsId = serverId;
    m_serverSettingsPage->setSection(ServerSettingsPage::Section::Common);
    refreshServerSettingsPage();

    // ★ 开页要说清 `ownerTab`、并且**先切过去**（见 `OverlayHost::execPage` 那段）：这一页
    //   是从服务端列表上开出来的，那一位就是"实例"这一颗。
    const int instancesTab = MainWindowUI::instancesTabIndex();
    if (QTabWidget *tabs = m_ui->getTabWidget()) {
        if (instancesTab >= 0 && instancesTab < tabs->count()) {
            tabs->setCurrentIndex(instancesTab);
        }
    }
    overlay->execPage(m_serverSettingsPage, ServerSettingsPage::windowTitle(), instancesTab);
    // 收页之后"这一页说的是谁"就没意义了（同控制台那一页末尾那一段）。
    // NOTE: 手里那份原文也一起扔掉 —— 它只在开页期间有用，留着的话下一次开页前要是有人
    //       读了它，读到的会是上一个服务端的内容。
    m_serverSettingsId.clear();
    m_serverSettingsRaw.clear();
    m_serverSettingsOriginal.clear();
}

void MainWindow::refreshServerSettingsPage() {
    ServerSettingsPage *page = m_serverSettingsPage;
    const QString serverId = m_serverSettingsId;
    if (page == nullptr || serverId.isEmpty()) {
        return;
    }
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    page->setServerName(entry.name.isEmpty() ? serverId : entry.name);

    // 在跑的时候整页只读（见 .hpp 里那段：它退出时会把自己内存里那份写回盘上）。
    const ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    const bool running = process != nullptr && process->isRunning();
    m_serverSettingsEditable = !running;
    QString statusText =
        ServerModel::statusName(process != nullptr ? process->status() : ServerModel::Status::Stopped);
    if (running) {
        statusText += MainWindowUI::serverSettingsRunningStatusSuffix();
    }
    page->setStatusText(statusText);
    page->setEditable(m_serverSettingsEditable,
                      running ? MainWindowUI::serverSettingsRunningNote() : QString());

    // ---------- ① 常用设置 + ② 原始文本 ----------
    //
    // ★ 两样**一起**摆：表单里那几格是叠在原文上的补丁，所以"原文"和"基线"必须是同一份
    //   盘上读出来的东西（`setValues()` 自己就把基线重设到刚摆进去的那一份）。
    const QString raw = m_serverSettingsController->readRaw(serverId);
    m_serverSettingsRaw = raw;
    m_serverSettingsOriginal = raw;
    const ServerModel::Properties properties = ServerModel::parseProperties(raw);

    QList<ServerSettingsPage::FieldView> views;
    const QList<ServerPropertiesForm::Field> &fields = ServerPropertiesForm::commonFields();
    for (const ServerPropertiesForm::Field &field : fields) {
        ServerSettingsPage::FieldView view;
        view.key = field.key;
        view.label = field.label;
        view.hint = field.hint;
        switch (field.kind) {
        case ServerPropertiesForm::Kind::Text:
            view.kind = ServerSettingsPage::Kind::Text;
            break;
        case ServerPropertiesForm::Kind::Number:
            view.kind = ServerSettingsPage::Kind::Number;
            break;
        case ServerPropertiesForm::Kind::Switch:
            view.kind = ServerSettingsPage::Kind::Switch;
            break;
        case ServerPropertiesForm::Kind::Choice:
            view.kind = ServerSettingsPage::Kind::Choice;
            break;
        }
        view.min = field.min;
        view.max = field.max;
        // 哪一行要挂"游戏里长什么样"的预览、哪一行旁边摆「选择本地 zip…」，都由字段表
        // 说了算（目前分别是 `motd` 与 `resource-pack-sha1`）。
        view.preview = field.preview;
        view.zipPick = field.zipPick;
        for (const ServerPropertiesForm::Choice &choice : field.choices) {
            view.choiceTokens.append(choice.token);
            view.choiceNames.append(choice.name);
        }
        views.append(view);
    }
    page->setFields(views);
    page->setValues(ServerPropertiesForm::valuesOf(properties, fields));
    page->setRawText(raw);

    // ---------- ③ 世界备份 ----------
    const QString worldName = serverWorldName(serverId);
    const QString worldDir = serverWorldDir(serverId);
    const QString backupDir = BackupFiles::backupDir(BackupFiles::Kind::World);
    QList<ServerSettingsPage::BackupRow> rows;
    if (!worldName.isEmpty()) {
        const QString prefix = BackupFiles::backupFilePrefix(
            m_serverSettingsController->backupLabel(serverId));
        const QFileInfoList entries = QDir(backupDir).entryInfoList(
            QStringList{QStringLiteral("*.zip")}, QDir::Files, QDir::Name | QDir::Reversed);
        for (const QFileInfo &info : entries) {
            if (!info.fileName().startsWith(prefix, Qt::CaseInsensitive)) {
                continue;
            }
            ServerSettingsPage::BackupRow row;
            row.path = info.absoluteFilePath();
            row.when = info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
            row.size = backupSizeText(info.size());
            row.safety = backupNameIsSafety(info.fileName(), prefix);
            row.scope = row.safety ? tr("还原之前自动做的那一份")
                                   : tr("这个世界的整个文件夹");
            rows.append(row);
        }
    }
    page->setBackupRows(rows);
    page->setBackupNote(worldDir.isEmpty()
                            ? tr("这个服务端的 level-name 现在这个值我们认不出来，不知道世界在"
                                 "哪儿 —— 先去「常用设置」把它改成一个正常的目录名。")
                            : tr("世界在：%1\n备份放在：%2（「立即备份」就打一份新的到这里）")
                                  .arg(QDir::toNativeSeparators(worldDir),
                                       QDir::toNativeSeparators(backupDir)));

    // ---------- ④ 体检 ----------
    runServerCheck();

    // 保存带：刚摆完 = 什么都没改。
    updateServerSettingsSaveState();
}

void MainWindow::updateServerSettingsSaveState() {
    ServerSettingsPage *page = m_serverSettingsPage;
    if (page == nullptr) {
        return;
    }
    if (!m_serverSettingsEditable) {
        // 在跑的时候那句话由 `setEditable()` 那一句管（`refreshSaveBar()` 会拿它盖住）。
        page->setSaveState(false, MainWindowUI::serverSettingsRunningNote());
        return;
    }
    const int changed = page->changedOnly().size();
    const bool raw = page->rawEdited();
    if (changed > 0 && raw) {
        page->setSaveState(true, MainWindowUI::serverSettingsDirtyBothNote(changed));
    } else if (changed > 0) {
        page->setSaveState(true, MainWindowUI::serverSettingsDirtyFieldsNote(changed));
    } else if (raw) {
        page->setSaveState(true, MainWindowUI::serverSettingsDirtyRawNote());
    } else {
        page->setSaveState(false, MainWindowUI::serverSettingsCleanNote());
    }
}

void MainWindow::mergeServerSettingsEdits(int fromSection, int toSection) {
    ServerSettingsPage *page = m_serverSettingsPage;
    if (page == nullptr || m_serverSettingsId.isEmpty()) {
        return;
    }
    Q_UNUSED(fromSection);
    if (toSection == static_cast<int>(ServerSettingsPage::Section::RawText)) {
        // **离开表单、去看原文**：把表单里改过的那几项先合进文本框 —— 用户马上要看的正是
        // "我改完之后这份文件长什么样"，这时候还摆着旧的那一份就是骗人（而且他很可能顺手
        // 就在这一页上接着改，两边不一致会丢改动）。
        //
        // ★ 合的方式是"解析 → 叠补丁 → 吐回文本"——**和保存走的是同一条路**
        //   （`saveServerSettings()` 前一半），所以"他在文本框里看到的那一份"和"按保存会
        //   写下去的那一份"是同一份，一个字节都不差。
        const ServerSettingsPage::Values edits = page->changedOnly();
        if (!edits.isEmpty()) {
            page->setRawText(m_serverSettingsController->mergeIntoRaw(page->rawText(), edits));
            // ★ 合完就算"用户动过文本框"了（内容真的变了、而且这一份是未保存的状态）——
            //   `setRawText()` 把那个记号清掉了，所以在这儿补回来。补丁那边**不动基线**：
            //   它还摆在表单上，切回去的时候两边一比还是"改过那几项"。
            page->markRawEdited();
        }
        return;
    }
    if (toSection == static_cast<int>(ServerSettingsPage::Section::Common)) {
        // **回到表单**：按文本框里**现在**那一份重摆表单，并把基线重设到它 —— 用户在文本框
        // 里删了一行、改了一个值，切回来时就该看得见。
        //
        // NOTE: 这一步**只动表单和基线，不写盘**（保存还是那唯一一条写盘的路）。
        page->setValues(m_serverSettingsController->valuesFromRaw(page->rawText()));
    }
    // 「世界备份」「体检」两页和这两样都不搭界，切过去什么都不用做。
}

void MainWindow::saveServerSettings() {
    ServerSettingsPage *page = m_serverSettingsPage;
    const QString serverId = m_serverSettingsId;
    if (page == nullptr || serverId.isEmpty() || !m_serverSettingsEditable) {
        return;
    }
    const ServerSettingsController::SaveResult result = m_serverSettingsController->save(
        serverId, page->rawText(), page->changedOnly());
    if (!result.rejected.isEmpty()) {
        // ★ **整份都不写**（而不是"能写的那几项先写下去"）：写盘是一发原子的 `QSaveFile`，
        //   写一半再报错，用户看着的是"保存了"而盘上是半份 —— 宁可让他把那一格改对再来。
        InlineMessage::warning(
            m_ui->overlay(), tr("有几项改不了"),
            tr("这几项的值不对，所以**这一趟什么都没写**：\n%1\n\n"
               "把这几项改对（表单里的格子、或者「原始文本」里那一行），再按一次保存。")
                .arg(result.rejected.join(QStringLiteral("\n"))));
        return;
    }
    if (!result.ok) {
        InlineMessage::critical(m_ui->overlay(), tr("保存不了"), result.problem);
        return;
    }
    appendLog(tr("服务端 %1 的 server.properties 已保存（改了 %2 项）。")
                  .arg(result.serverName)
                  .arg(result.changed));
    // 存完之后把这一页整个按**盘上那份新文件**重摆一遍（原文、表单基线、备份那一列、
    // 体检单都跟着换）—— 然后在那行小字上说明"刚刚存过"。
    refreshServerSettingsPage();
    page->setSaveState(false, MainWindowUI::serverSettingsSavedNote());
}

void MainWindow::revertServerSettings() {
    ServerSettingsPage *page = m_serverSettingsPage;
    if (page == nullptr || m_serverSettingsId.isEmpty() || !m_serverSettingsEditable) {
        return;
    }
    // 放弃改动 = 把这一页按盘上那一份重新摆一遍（原文、表单基线、保存带全回到开页那一刻）。
    // NOTE: 这一路**不碰盘**，所以什么都不用问。
    refreshServerSettingsPage();
    page->setSaveState(false, MainWindowUI::serverSettingsCleanNote());
}

// ---------------------------------------------------------------------------
// 体检
// ---------------------------------------------------------------------------

ServerCheck::Facts MainWindow::serverCheckFacts() {
    return m_serverSettingsController->checkFacts(m_serverSettingsId,
                                                   m_serverSettingsPortState);
}

void MainWindow::runServerCheck() {
    ServerSettingsPage *page = m_serverSettingsPage;
    if (page == nullptr || m_serverSettingsId.isEmpty()) {
        return;
    }
    // 端口那一次真监听**先做**（它是个同步的、几十微秒的动作）——体检单里那一条要的正是
    // 它的结论。
    const ServerModel::Properties properties = ServerRegistry::readProperties(m_serverSettingsId);
    const int port = ServerModel::serverPortOf(properties);
    m_serverSettingsPortState = port > 0 ? ServerPortCheck::stateOf(port, nullptr)
                                         : ServerPortCheck::State::Unknown;

    const ServerCheck::Facts facts = serverCheckFacts();
    const QList<ServerCheck::Finding> findings = ServerCheck::inspect(facts);
    QList<ServerSettingsPage::CheckRow> rows;
    rows.reserve(findings.size());
    for (const ServerCheck::Finding &finding : findings) {
        ServerSettingsPage::CheckRow row;
        // 两边的档位是按下标对齐的（0 要处理 / 1 提醒 / 2 说明）。
        row.severity = static_cast<int>(finding.severity);
        row.title = finding.title;
        row.detail = finding.detail;
        row.actionToken = ServerCheck::actionToken(finding.action);
        rows.append(row);
    }
    page->setCheckRows(rows);
    page->setCheckSummary(ServerCheck::summaryText(findings));

    // ---------- ④ 玩家名单（§9.7，2026-10-18）----------
    // **摆在最后**：它要读 `m_serverSettingsEditable`（上面 `setEditable()` 那一发算出来的），
    // 而那一句又回头会重摆那两颗按钮的灰亮。
    refreshServerPlayers();
    // ---------- ⑤ 内容（§9.7，2026-10-18）----------
    refreshServerContent();
}

// ---------------------------------------------------------------------------
// 玩家名单（§9.7「玩家管理」）
// ---------------------------------------------------------------------------

void MainWindow::refreshServerPlayers() {
    ServerSettingsPage *page = m_serverSettingsPage;
    const QString serverId = m_serverSettingsId;
    if (page == nullptr || serverId.isEmpty()) {
        return;
    }
    const int index = qBound(0, page->playerList(), ServerPlayers::listCount() - 1);
    const auto list = static_cast<ServerPlayers::List>(index);
    // 「踢出…」只对正在跑的服务端有意义（和写名单那两颗正好相反 —— 见页面那段）。
    page->setPlayerKickAvailable(!m_serverSettingsEditable);
    const ServerPlayers::Content content =
        m_serverSettingsController->readPlayers(serverId, list);
    QString note = MainWindowUI::serverSettingsPlayerNoteText(index, !m_serverSettingsEditable);
    if (!content.ok) {
        // ★ 读不懂就**什么都不摆、也不许写**：那份文件里可能还有用户自己的记录，
        //   拿空名单覆盖它比"这一页现在看不了"糟得多。
        page->setPlayerRows({});
        page->setPlayerNote(content.problem);
        return;
    }
    if (content.entries.isEmpty()) {
        note += tr("\n现在一条都没有。");
    }
    const QDateTime now = QDateTime::currentDateTime();
    QList<ServerSettingsPage::PlayerRow> rows;
    for (const ServerPlayers::Entry &entry : content.entries) {
        ServerSettingsPage::PlayerRow row;
        row.identity = ServerPlayers::identityOf(list, entry);
        if (list == ServerPlayers::List::BannedIps) {
            row.name = entry.ip;
            row.detail = QString();
            // 封禁 IP 那一档没有"玩家名"可踢 —— `kickName` 留空，那颗按钮就是灰的。
        } else {
            row.name = entry.name.isEmpty() ? entry.uuid : entry.name;
            row.detail = entry.uuid;
            row.kickName = entry.name;
        }
        QStringList notes;
        if (list == ServerPlayers::List::Ops) {
            notes << tr("等级 %1").arg(entry.level);
            if (entry.bypassesPlayerLimit) notes << tr("可以绕过人数上限");
        } else if (list == ServerPlayers::List::BannedPlayers
                   || list == ServerPlayers::List::BannedIps) {
            notes << ServerPlayers::expiryText(entry, now);
            if (!entry.reason.isEmpty()) notes << entry.reason;
            row.expired = ServerPlayers::isExpired(entry, now);
        }
        row.note = notes.join(QStringLiteral(" · "));
        rows.append(row);
    }
    page->setPlayerRows(rows);
    page->setPlayerNote(note);
}

void MainWindow::addServerPlayer() {
    ServerSettingsPage *page = m_serverSettingsPage;
    const QString serverId = m_serverSettingsId;
    if (page == nullptr || serverId.isEmpty()) {
        return;
    }
    if (!m_serverSettingsEditable) {
        InlineMessage::warning(
            m_ui->overlay(), tr("现在改不了"),
            MainWindowUI::serverSettingsPlayerNoteText(page->playerList(), true));
        return;
    }
    const int index = qBound(0, page->playerList(), ServerPlayers::listCount() - 1);
    const auto list = static_cast<ServerPlayers::List>(index);
    const QString listTitle = MainWindowUI::serverSettingsPlayerListTitle(index);
    const bool ipList = list == ServerPlayers::List::BannedIps;
    bool ok = false;
    const QString typed =
        InlineMessage::getText(
            m_ui->overlay(), tr("加进%1").arg(listTitle),
            ipList ? tr("要封哪个地址？\n\n只写一个 IPv4 / IPv6 地址（不带端口）。")
                   : tr("玩家名是什么？\n\n"
                        "离线模式（online-mode=false）的服务端按名字算 UUID；"
                        "正版服务器的名单要在控制台里用 whitelist add / op 加，"
                        "那一边才会解析成 Mojang 的 UUID。"),
            QString(), &ok)
            .trimmed();
    if (!ok || typed.isEmpty()) {
        return;
    }
    // 大致的形状闸（真正的判据在服务端那边 —— 这一层只挡明显的手滑）。
    if (ipList && (typed.contains(QLatin1Char('/')) || typed.contains(QLatin1Char(' ')))) {
        InlineMessage::warning(m_ui->overlay(), tr("加进%1").arg(listTitle),
                               tr("地址里不该有空格或者斜杠。"));
        return;
    }
    const ServerPlayers::Content content =
        m_serverSettingsController->readPlayers(serverId, list);
    if (!content.ok) {
        InlineMessage::critical(m_ui->overlay(), tr("加不进%1").arg(listTitle),
                                content.problem);
        return;
    }
    QList<ServerPlayers::Entry> entries = content.entries;
    for (const ServerPlayers::Entry &entry : entries) {
        const bool same = ipList
                              ? entry.ip.compare(typed, Qt::CaseInsensitive) == 0
                              : entry.name.compare(typed, Qt::CaseInsensitive) == 0;
        if (same) {
            InlineMessage::information(m_ui->overlay(), tr("加进%1").arg(listTitle),
                                       tr("「%1」已经在这一份名单上了。").arg(typed));
            return;
        }
    }
    entries.append(ServerPlayers::makeEntry(list, typed, QStringLiteral("WTFML"),
                                            QDateTime::currentDateTime()));
    QString problem;
    if (!m_serverSettingsController->writePlayers(serverId, list, entries, &problem)) {
        InlineMessage::critical(m_ui->overlay(), tr("加不进%1").arg(listTitle), problem);
        return;
    }
    appendLog(tr("服务端 %1 的%2里加上了「%3」。").arg(serverId, listTitle, typed));
    refreshServerPlayers();
}

void MainWindow::removeServerPlayer(const QString &identity) {
    ServerSettingsPage *page = m_serverSettingsPage;
    const QString serverId = m_serverSettingsId;
    if (page == nullptr || serverId.isEmpty() || identity.isEmpty()) {
        return;
    }
    if (!m_serverSettingsEditable) {
        InlineMessage::warning(
            m_ui->overlay(), tr("现在改不了"),
            MainWindowUI::serverSettingsPlayerNoteText(page->playerList(), true));
        return;
    }
    const int index = qBound(0, page->playerList(), ServerPlayers::listCount() - 1);
    const auto list = static_cast<ServerPlayers::List>(index);
    const QString listTitle = MainWindowUI::serverSettingsPlayerListTitle(index);
    const ServerPlayers::Content content =
        m_serverSettingsController->readPlayers(serverId, list);
    if (!content.ok) {
        InlineMessage::critical(m_ui->overlay(), tr("从%1里拿掉").arg(listTitle),
                                content.problem);
        return;
    }
    QList<ServerPlayers::Entry> entries = content.entries;
    const int at = ServerPlayers::indexOf(entries, list, identity);
    if (at < 0) {
        // 页面停着的时候文件被别处改了：不硬来，重摆一遍让他看到现在的样子。
        refreshServerPlayers();
        return;
    }
    const ServerPlayers::Entry entry = entries.at(at);
    const QString who = list == ServerPlayers::List::BannedIps
                            ? entry.ip
                            : (entry.name.isEmpty() ? entry.uuid : entry.name);
    if (!InlineMessage::question(
            m_ui->overlay(), tr("从%1里拿掉").arg(listTitle),
            list == ServerPlayers::List::BannedIps
                ? tr("「%1」会从封禁名单上拿掉（等于解封这个地址）。").arg(who)
                : list == ServerPlayers::List::BannedPlayers
                      ? tr("「%1」会从封禁名单上拿掉（等于解封）。").arg(who)
                      : tr("「%1」会从这份名单上拿掉。").arg(who),
            tr("拿掉"), tr("取消"), /*defaultYes=*/false, /*destructive=*/false)) {
        return;
    }
    entries.removeAt(at);
    QString problem;
    if (!m_serverSettingsController->writePlayers(serverId, list, entries, &problem)) {
        InlineMessage::critical(m_ui->overlay(), tr("从%1里拿掉").arg(listTitle), problem);
        return;
    }
    appendLog(tr("服务端 %1 的%2里拿掉了「%3」。").arg(serverId, listTitle, who));
    refreshServerPlayers();
}

void MainWindow::pickServerResourcePack() {
    ServerSettingsPage *page = m_serverSettingsPage;
    if (page == nullptr || m_serverSettingsId.isEmpty()) {
        return;
    }
    if (!m_serverSettingsEditable) {
        InlineMessage::warning(m_ui->overlay(), tr("现在改不了"),
                               MainWindowUI::serverSettingsRunningNote());
        return;
    }
    // 上一次选过的那一份就在它旁边接着选（`QFileDialog` 的"上次目录"由系统记，这里只
    // 给一个起点）。筛子允许 `.zip` 和 `.jar`：资源包有时候是别人从模组里剥出来的，
    // 扩展名不一定对 —— 真正的判据在 `ServerResourcePack::inspect()` 里（看条目表）。
    const QString chosen = QFileDialog::getOpenFileName(
        this, tr("选一份资源包 zip"), QString(),
        tr("资源包 (*.zip *.jar);;所有文件 (*.*)"));
    if (chosen.isEmpty()) {
        return;
    }
    const ServerResourcePack::Inspect inspect = ServerResourcePack::inspect(chosen);
    if (!inspect.ok) {
        // 选错了 / 包坏了 / pack.mcmeta 套在文件夹里 —— 都要当场说清楚，不能只记日志。
        InlineMessage::warning(m_ui->overlay(), tr("这个压缩包不能当资源包用"),
                               inspect.problem);
        return;
    }
    // 填的是**那一格**（算"用户改过"，保存时会写回 `server.properties`）。
    page->setFieldValue(QStringLiteral("resource-pack-sha1"), inspect.sha1);
    appendLog(tr("从本地资源包算出了 SHA-1：%1（%2，%3 字节）")
                  .arg(inspect.sha1, ServerResourcePack::fileCountText(inspect),
                       QString::number(inspect.bytes)));
    // ★ 说清两件事：算的是哪一份、以及**地址还得你自己托管** —— 启动器不会把本机路径
    //   填进 `resource-pack`（别人连不到你的硬盘，那会让每个人进来都卡一下）。
    InlineMessage::information(
        m_ui->overlay(), tr("SHA-1 算好了"),
        tr("本地这一份「%1」的 SHA-1 已经填进「资源包 SHA-1」那一格：\n\n%2\n\n"
           "「资源包地址」那一格还得你自己填 —— 要一个别的玩家**连得到**的 http(s) "
           "链接（自己的网站 / 网盘直链 / 对象存储都行）。本机文件路径别人打不开，"
           "启动器不会把它填进去。\n\n"
           "换了 zip 就要重新算一遍 SHA-1；服务端下一次启动时才会用上新值。")
            .arg(QFileInfo(chosen).fileName(), inspect.sha1));
}

// ---------------------------------------------------------------------------
// 服务端内容（§9.7「内容管理」）
// ---------------------------------------------------------------------------

QString MainWindow::serverContentFolder() const {
    ServerSettingsPage *page = m_serverSettingsPage;
    const QString serverId = m_serverSettingsId;
    if (page == nullptr || serverId.isEmpty()) {
        return QString();
    }
    const int kindIndex = qBound(0, page->contentKind(), ServerContent::kindCount() - 1);
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    if (static_cast<ServerContent::Kind>(kindIndex) == ServerContent::Kind::Mods) {
        return ServerContent::modsDir(entry.dirPath);
    }
    // 数据包跟着世界走：`level-name` 认不出来 / 还没建过世界 ⇒ 这一页什么都不列。
    return ServerContent::datapacksDir(serverWorldDir(serverId));
}

void MainWindow::refreshServerContent() {
    ServerSettingsPage *page = m_serverSettingsPage;
    const QString serverId = m_serverSettingsId;
    if (page == nullptr || serverId.isEmpty()) {
        return;
    }
    const int kindIndex = qBound(0, page->contentKind(), ServerContent::kindCount() - 1);
    const auto kind = static_cast<ServerContent::Kind>(kindIndex);
    const QString folder = serverContentFolder();
    const bool running = !m_serverSettingsEditable;
    if (folder.isEmpty()) {
        // 数据包那一类：世界在哪儿都还不知道 —— 别摆一个假路径，说清楚该去哪儿改。
        page->setContentRows({});
        page->setContentNote(
            tr("这个服务端的 level-name 现在这个值我们认不出来，不知道世界在哪儿 —— "
               "先去「常用设置」把它改成一个正常的目录名，数据包才知道往哪个文件夹里放。"));
        return;
    }
    page->setContentNote(MainWindowUI::serverSettingsContentNoteText(
        kindIndex, QDir::toNativeSeparators(folder), running));

    QDir dir(folder);
    QStringList names = dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    // ★ 排序在后、认名在前：`entriesFromNames()` 会把别的文件（配置、备份、`.jar.bak`）
    //   筛掉，禁用着的（`.jar.disabled`）留下来。
    const QList<ServerContent::Entry> entries = ServerContent::entriesFromNames(names, kind);
    QList<ServerSettingsPage::ContentRow> rows;
    for (const ServerContent::Entry &entry : entries) {
        ServerSettingsPage::ContentRow row;
        row.name = entry.name;
        row.size = backupSizeText(QFileInfo(dir.filePath(entry.name)).size());
        row.disabled = entry.disabled;
        rows.append(row);
    }
    page->setContentRows(rows);
}

void MainWindow::addServerContent() {
    ServerSettingsPage *page = m_serverSettingsPage;
    const QString serverId = m_serverSettingsId;
    if (page == nullptr || serverId.isEmpty()) {
        return;
    }
    if (!m_serverSettingsEditable) {
        InlineMessage::warning(m_ui->overlay(), tr("现在改不了"),
                               MainWindowUI::serverSettingsRunningNote());
        return;
    }
    const QString folder = serverContentFolder();
    if (folder.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("加不了"),
                               tr("这个服务端的 level-name 认不出来，不知道世界在哪儿。"));
        return;
    }
    const int kindIndex = qBound(0, page->contentKind(), ServerContent::kindCount() - 1);
    const auto kind = static_cast<ServerContent::Kind>(kindIndex);
    const QString suffix = ServerContent::fileSuffix(kind);
    const QStringList chosen = QFileDialog::getOpenFileNames(
        this, kind == ServerContent::Kind::Datapacks ? tr("选数据包 zip") : tr("选模组 jar"),
        QString(),
        kind == ServerContent::Kind::Datapacks ? tr("数据包 (*.zip);;所有文件 (*.*)")
                                               : tr("模组 (*.jar);;所有文件 (*.*)"));
    if (chosen.isEmpty()) {
        return;
    }
    // 文件夹可能还不存在（服务端还没跑过）—— 这一次"要往里放东西"了，建出来。
    if (!QDir().mkpath(folder)) {
        InlineMessage::critical(m_ui->overlay(), tr("加不了"),
                                tr("建不出这个文件夹：%1").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    QStringList added;
    QStringList skipped;
    for (const QString &source : chosen) {
        const QFileInfo info(source);
        // 后缀不对的不硬拦（别人给的包偶尔被改成 `.zip` / `.jar`），但要说一句。
        if (!info.fileName().endsWith(suffix, Qt::CaseInsensitive)) {
            skipped.append(tr("%1（后缀不是 %2）").arg(info.fileName(), suffix));
            continue;
        }
        const QString target = QDir(folder).filePath(info.fileName());
        if (QFileInfo::exists(target)) {
            // ★ **重名不覆盖**：用户手上那一份可能正是他调过的。
            skipped.append(tr("%1（文件夹里已经有一份了）").arg(info.fileName()));
            continue;
        }
        if (!QFile::copy(source, target)) {
            skipped.append(tr("%1（拷不进去）").arg(info.fileName()));
            continue;
        }
        added.append(info.fileName());
    }
    if (!added.isEmpty()) {
        appendLog(tr("服务端 %1 的%2里加进了 %3 份：%4")
                      .arg(serverId, MainWindowUI::serverSettingsContentKindTitle(kindIndex),
                           QString::number(added.size()), added.join(QStringLiteral("、"))));
        refreshServerContent();
    }
    if (!skipped.isEmpty()) {
        InlineMessage::warning(
            m_ui->overlay(), tr("有几份没有加进去"),
            skipped.join(QStringLiteral("\n"))
                + tr("\n\n重名的那几份没有覆盖 —— 要换版本，先在「打开文件夹」里把旧的挪走。"));
    } else if (!added.isEmpty()) {
        InlineMessage::information(
            m_ui->overlay(), tr("加好了"),
            tr("放进去了 %1 份。服务端下一次启动时才会加载它们。").arg(added.size()));
    }
}

void MainWindow::toggleServerContent(const QString &name, bool enable) {
    ServerSettingsPage *page = m_serverSettingsPage;
    const QString serverId = m_serverSettingsId;
    if (page == nullptr || serverId.isEmpty() || name.isEmpty()) {
        return;
    }
    if (!m_serverSettingsEditable) {
        InlineMessage::warning(m_ui->overlay(), tr("现在改不了"),
                               MainWindowUI::serverSettingsRunningNote());
        return;
    }
    const int kindIndex = qBound(0, page->contentKind(), ServerContent::kindCount() - 1);
    const auto kind = static_cast<ServerContent::Kind>(kindIndex);
    const QString folder = serverContentFolder();
    const QString target = ServerContent::toggledName(kind, name, enable);
    if (folder.isEmpty() || target.isEmpty() || target == name) {
        // 认不出来的名字（不是这一类的文件）—— 不猜，重摆一遍让用户看到现在是什么。
        refreshServerContent();
        return;
    }
    const QString sourcePath = QDir(folder).filePath(name);
    const QString targetPath = QDir(folder).filePath(target);
    if (!QFileInfo(sourcePath).isFile()) {
        // 页面停着的时候文件夹被别处动了：重摆一遍，别对着空气改名。
        refreshServerContent();
        return;
    }
    if (QFileInfo::exists(targetPath)) {
        InlineMessage::warning(
            m_ui->overlay(), tr("改不了"),
            tr("文件夹里已经有一份「%1」了 —— 先把那一份挪走，再改这一份的名字。").arg(target));
        return;
    }
    QFile file(sourcePath);
    // ★ 改名而不是删除 / 移动：文件内容一个字节都不动，用户想恢复就再点一次。
    if (!file.rename(targetPath)) {
        InlineMessage::critical(m_ui->overlay(), tr("改不了"),
                                tr("「%1」改不成「%2」：%3")
                                    .arg(name, target, file.errorString()));
        return;
    }
    appendLog(tr("服务端 %1 的「%2」%3了（现在是「%4」）。")
                  .arg(serverId, name, enable ? tr("启用") : tr("禁用"), target));
    refreshServerContent();
}

void MainWindow::openServerContentFolder() {
    const QString folder = serverContentFolder();
    if (folder.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("找不到这个文件夹"),
                               tr("这个服务端的 level-name 认不出来，不知道世界在哪儿。"));
        return;
    }
    if (!QFileInfo(folder).isDir()) {
        // 还没往里放过东西（服务端还没跑过 / 还没装过任何内容）——说清楚它该在哪儿，
        // 别弹一个"打开失败"（`QDesktopServices` 对不存在的路径只会静默不受理）。
        InlineMessage::information(
            m_ui->overlay(), tr("这个文件夹还没建出来"),
            tr("先往里放一份文件（「添加…」），或者自己建一个：\n%1")
                .arg(QDir::toNativeSeparators(folder)));
        return;
    }
    // 走 `QDesktopServices`（和"打开实例文件夹"同一条路：验证工程能换掉 url handler 拦下来）。
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) {
        InlineMessage::warning(m_ui->overlay(), tr("打开不了"),
                               tr("系统没有受理这个文件夹：%1")
                                   .arg(QDir::toNativeSeparators(folder)));
    }
}

void MainWindow::kickServerPlayer(const QString &name) {
    const QString serverId = m_serverSettingsId;
    if (serverId.isEmpty() || name.isEmpty()) {
        return;
    }
    ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    if (process == nullptr || !process->isRunning()) {
        InlineMessage::warning(m_ui->overlay(), tr("没有在跑的服务端"),
                               tr("「踢出」是把一条 kick 命令送进正在跑的服务端 —— "
                                  "它现在没在跑，所以踢不了人。"));
        return;
    }
    // §9.7：在线动作**必须走运行控制台**（不是偷偷改名单 —— 那要等它重启才生效，
    // 而且用户会以为人已经踢下去了）。命令和用户自己敲的那一条完全相同。
    const QString command = QStringLiteral("kick ") + name;
    if (!process->sendCommand(command)) {
        InlineMessage::critical(m_ui->overlay(), tr("命令没有送出去"),
                                tr("服务端的输入通道已经不在了（它可能刚好在退出）。"));
        return;
    }
    // 回显：服务端答的那一行会走日志通道进控制台；这一条 `[wtfml]` 只是把"我们发了什么"
    // 摆在同一页上（和"现在可以强杀了"那一句同一个前缀）。
    m_serverConsole->appendLine(serverId, QStringLiteral("[wtfml] ") + tr("已发送：%1").arg(command));
    appendLog(tr("服务端 %1：%2").arg(serverId, command));
    InlineMessage::information(
        m_ui->overlay(), tr("命令送出去了"),
        tr("「%1」已经送进服务端控制台。它答的那一行会出现在控制台里 —— "
           "去「控制台」看一眼结果（那才是真正说了算的）。").arg(command));
}

void MainWindow::runServerCheckAction(const QString &token) {
    ServerSettingsPage *page = m_serverSettingsPage;
    const QString serverId = m_serverSettingsId;
    if (page == nullptr || serverId.isEmpty()) {
        return;
    }
    if (token == QStringLiteral("reinstall")) {
        // 重装是那一行行尾那颗「修复」做的同一件事（`repairServer()`）——**先收掉这一页**：
        // 它正卡在 `execPage()` 的嵌套事件循环里，而重装要走一遍下载 + 装，收掉之后用户
        // 才看得见任务中心里那一项在跑（同"开新页之前先收页面"那条老规矩）。
        //
        // ★ 收页走 `dismiss()`（不是 `closeOverlay()`）：这一页正卡在 `execPage()` 的嵌套
        //   事件循环里，`dismiss()` 发的就是"返回箭头那一发"，`execPage()` 会正常返回；
        //   `closeOverlay()` 走的是另一条路（见 `OverlayHost::dismiss()` 那段 ★）。
        m_ui->overlay()->dismiss();
        repairServer(serverId);
        return;
    }
    if (token == QStringLiteral("accept-eula")) {
        // EULA 那道闸就是 `ensureServerEula()`（那一发问的就是"同意并启动"）——这一页上
        // 并没有"启动"这回事，所以这里借它把那段话问出来、把 `eula.txt` 写下去。
        ensureServerEula(serverId);
        refreshServerSettingsPage();
        return;
    }
    if (token == QStringLiteral("fix-values")) {
        // 把文件里不合法的那几项改回表里的缺省（**只动那几项**，别的行一个字节都不碰）。
        const ServerModel::Properties properties = ServerRegistry::readProperties(serverId);
        const QList<ServerPropertiesForm::ValueProblem> problems =
            ServerPropertiesForm::problemsIn(properties);
        if (problems.isEmpty()) {
            return;  // 已经自己好了（另一处改的）：什么都不做
        }
        QStringList lines;
        for (const ServerPropertiesForm::ValueProblem &problem : problems) {
            const ServerPropertiesForm::Field *field =
                ServerPropertiesForm::fieldFor(problem.key);
            if (field != nullptr) {
                lines.append(tr("·「%1」：%2 → %3")
                                 .arg(field->label, problem.value.isEmpty() ? tr("（空的）")
                                                                            : problem.value,
                                      field->fallback));
            }
        }
        if (!InlineMessage::question(
                m_ui->overlay(), tr("改回缺省值"),
                tr("这几项会被改回这张表上的缺省值（别的行一个字节都不动）：\n%1\n\n要改吗？")
                    .arg(lines.join(QStringLiteral("\n"))),
                tr("改回缺省值"), tr("取消"), /*defaultYes=*/true, /*destructive=*/false)) {
            return;
        }
        const ServerSettingsController::SaveResult result = m_serverSettingsController->save(
            serverId, m_serverSettingsController->readRaw(serverId),
            m_serverSettingsController->fallbackEdits(properties));
        if (!result.ok) {
            InlineMessage::critical(m_ui->overlay(), tr("改不了"), result.problem);
            return;
        }
        appendLog(tr("服务端 %1 的 server.properties 里有 %2 项改回了缺省值。")
                      .arg(serverId)
                      .arg(result.changed));
        refreshServerSettingsPage();
        return;
    }
    if (token == QStringLiteral("open-settings")) {
        page->setSection(ServerSettingsPage::Section::Common);
        return;
    }
    if (token == QStringLiteral("open-backups")) {
        page->setSection(ServerSettingsPage::Section::Backups);
        return;
    }
}

// ---------------------------------------------------------------------------
// 世界备份（走 M2 那一套引擎，和「世界设置」那一页共用同一条路）
// ---------------------------------------------------------------------------

QString MainWindow::serverWorldName(const QString &serverId) const {
    return m_serverSettingsController->worldName(serverId);
}

QString MainWindow::serverWorldDir(const QString &serverId) const {
    return m_serverSettingsController->worldDir(serverId);
}

int MainWindow::serverWorldBackupCount(const QString &serverId) const {
    return m_serverSettingsController->worldBackupCount(serverId);
}

void MainWindow::backupServerWorld(const QString &serverId) {
    if (serverId.isEmpty()) {
        return;
    }
    if (m_backupController->busy()) {
        InlineMessage::information(m_ui->overlay(), tr("现在做不了"),
                                   tr("已经有一份备份或者还原在跑了，等它完了再来。"));
        return;
    }
    const ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    if (process != nullptr && process->isRunning()) {
        // 和"游戏在跑不给备份世界"同一条理由：它每隔几秒就在往存档里写，这时候抄出来的
        // 那一份可能正好截在半截写入上。
        InlineMessage::warning(m_ui->overlay(), tr("备份世界"),
                               tr("这个服务端正在跑。先让它停下来再备份 —— 它每隔几秒就在往"
                                  "存档里写，这时候抄出来的那一份可能是不完整的。"));
        return;
    }
    const QString worldName = serverWorldName(serverId);
    const QString worldDir = serverWorldDir(serverId);
    if (worldName.isEmpty()) {
        InlineMessage::warning(
            m_ui->overlay(), tr("备份不了"),
            tr("这个服务端的 level-name 现在这个值我们认不出来，不知道世界在哪儿。\n"
               "先去「常用设置」把它改成一个正常的目录名。"));
        return;
    }
    if (!QFileInfo(worldDir).isDir()) {
        InlineMessage::information(
            m_ui->overlay(), tr("还没有世界"),
            tr("「%1」这个目录还不存在 —— 服务端第一次启动之后才会生成它，"
               "现在没有能备份的东西。")
                .arg(worldName));
        return;
    }

    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    const QDateTime now = QDateTime::currentDateTime();
    BackupFiles::Manifest manifest;
    manifest.kind = BackupFiles::Kind::World;
    // `root` 是 zip 里那一层前缀、也是还原时落回去的目录名 ⇒ 就是这个世界目录名。
    manifest.root = worldName;
    // `label` 只用来拼文件名 —— 带上 serverId（理由见 `serverBackupLabel()` 那段）。
    manifest.label = m_serverSettingsController->backupLabel(serverId);
    manifest.worldName = worldName;
    // NOTE: 这两格说的是"这个世界是从哪个**实例**里备出来的"，而服务端不是实例 ——
    //       留空。它在界面上只有"打开目标位置"那一处读者，而那一处走的是 `targetId`
    //       那一位（下面传的是服务端目录）。
    manifest.sourceInstanceId = QString();
    manifest.sourceInstanceName = entry.name.isEmpty() ? serverId : entry.name;
    manifest.gameVersion = entry.gameVersion;
    manifest.createdAt = now;
    manifest.scope = QStringList{tr("这个服务端的整个世界文件夹")};

    const QString dir = BackupFiles::backupDir(BackupFiles::Kind::World);
    const QString zip = QDir(dir).filePath(
        BackupFiles::backupFileName(manifest.label, now));
    // ★ 跑完之后要把这一页的备份那一列**重摆一遍**（多了一份）—— 记下是哪个服务端。
    m_backupController->startWrite(manifest, worldDir, zip,
                                   tr("备份世界「%1」").arg(worldName), entry.dirPath,
                                   tr("备份完成"), serverId);
}

void MainWindow::restoreServerWorldBackup(const QString &serverId, const QString &zipPath) {
    if (serverId.isEmpty() || zipPath.isEmpty()) {
        return;
    }
    if (m_backupController->busy()) {
        InlineMessage::information(m_ui->overlay(), tr("现在做不了"),
                                   tr("已经有一份备份或者还原在跑了，等它完了再来。"));
        return;
    }
    const ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    if (process != nullptr && process->isRunning()) {
        InlineMessage::warning(m_ui->overlay(), tr("恢复备份"),
                               tr("这个服务端正在跑。先让它停下来再还原 —— 不然它退出的时候"
                                  "会把内存里那份旧存档重新写一遍，把刚还原出来的盖掉。"));
        return;
    }
    const QString worldName = serverWorldName(serverId);
    const QString serverDir = ServerRegistry::describe(serverId).dirPath;
    // 默认**不**落在「还原」上：这一步会把这个世界现在的文件整个换掉（和删世界同一档）。
    if (!InlineMessage::question(
            m_ui->overlay(), tr("恢复备份"),
            tr("「%1」现在的文件会被备份里那一份**整个换掉**。\n\n"
               "还原之前会自动给现在这一份留一份安全快照，所以这一步是能退回来的。\n\n"
               "要还原的是这一份：\n%2")
                .arg(worldName.isEmpty() ? serverId : worldName,
                     QDir::toNativeSeparators(zipPath)),
            tr("还原"), tr("取消"), /*defaultYes=*/false, /*destructive=*/true)) {
        return;
    }
    // `root`（zip 里那一层前缀）就是世界目录名 ⇒ 解到**服务端目录**底下正好落回原地。
    m_backupController->startRestore(zipPath, serverDir,
                                     tr("还原世界「%1」").arg(worldName), serverDir,
                                     tr("还原完成"), serverId);
}

void MainWindow::deleteServerBackup(const QString &zipPath) {
    if (zipPath.isEmpty()) {
        return;
    }
    const QFileInfo info(zipPath);
    if (!info.exists()) {
        InlineMessage::warning(m_ui->overlay(), tr("删不掉"),
                               tr("这一份已经不在了：\n%1")
                                   .arg(QDir::toNativeSeparators(zipPath)));
        refreshServerSettingsPage();
        return;
    }
    // 删了就拿不回来 ⇒ 默认那一边是「取消」（和删服务端 / 删世界同一档）。
    if (!InlineMessage::question(
            m_ui->overlay(), tr("删掉这一份备份"),
            tr("这一份会被删掉，删了找不回来：\n%1\n\n要删吗？")
                .arg(QDir::toNativeSeparators(zipPath)),
            tr("删掉它"), tr("取消"), /*defaultYes=*/false, /*destructive=*/true)) {
        return;
    }
    if (!QFile::remove(zipPath)) {
        InlineMessage::critical(m_ui->overlay(), tr("删不掉"),
                                tr("这一份删不掉（可能被别的程序占着）：\n%1")
                                    .arg(QDir::toNativeSeparators(zipPath)));
        return;
    }
    appendLog(tr("已删除一份备份：%1").arg(QDir::toNativeSeparators(zipPath)));
    refreshServerSettingsPage();
}

bool MainWindow::ensureServerEula(const QString &serverId) {
    // 已经写着接受 = 直接放行（`ServerRegistry::readEula()` 是判据的唯一出处，它自己也
    // 就是 `ServerModel::eulaAccepted()` 的落点）。
    if (ServerRegistry::readEula(serverId)) {
        return true;
    }
    OverlayHost *overlay = m_ui == nullptr ? nullptr : m_ui->overlay();
    if (overlay == nullptr) {
        return false;
    }
    // ★ 默认那一颗给 **-1**：这一格上一颗都不许被回车打中 —— "把同意两个字打在屏幕上"
    //   这件事不能被一次误按回车办了。
    InlineMessageBox box(MessageKind::Question, ServerRun::eulaPromptTitle(),
                         ServerRun::eulaPromptText(),
                         QStringList{tr("同意并启动"), tr("先不启动")}, -1, -1);
    // 勾选框和「同意并启动」绑在一起：没勾之前那一颗是灰的（见 setCheckBox() 那段）。
    box.setCheckBox(ServerRun::eulaCheckBoxText(), 0);
    box.setLink(ServerRun::eulaLinkText(), ServerRun::eulaUrl());
    // 那一行链接被点了 ⇒ **由这一层开**（和"打开实例文件夹"同一条路：要写日志、开不了
    // 要说一句）。那一页自己不开浏览器（见 setLink() 那段）。
    connect(&box, &InlineMessageBox::linkActivated, this, [this](const QString &url) {
        if (url.isEmpty()) {
            return;
        }
        appendLog(tr("正在打开 %1。").arg(url));
        if (!QDesktopServices::openUrl(QUrl(url))) {
            InlineMessage::warning(m_ui->overlay(), tr("打不开浏览器"),
                                   tr("没能打开这一条网址：\n%1").arg(url));
        }
    });
    overlay->execPrompt(&box, false);
    // 两道都要过：点了「同意并启动」**并且**那个框是勾上的。只按前者的话，那个勾就成了
    // 摆设（而那正是"只有你自己摁出来"这句话唯一的技术落点）。
    if (box.clickedIndex() != 0 || !box.checkBoxChecked()) {
        appendLog(tr("没有同意 EULA，%1 这次不启动。").arg(serverId));
        return false;
    }
    QString problem;
    if (!ServerRegistry::acceptEula(serverId, &problem)) {
        InlineMessage::critical(m_ui->overlay(), tr("写不了 eula.txt"), problem);
        return false;
    }
    appendLog(tr("%1：你已经同意 EULA，eula.txt 里写上了 eula=true。").arg(serverId));
    return true;
}

ServerInstall::Plan MainWindow::serverLaunchPlan(const QString &serverId, QString *problem) const {
    return ServerWorkspaceController::launchPlan(serverId, problem);
}

void MainWindow::updateServerRow(const QString &serverId) {
    QTreeWidget *tree = m_ui == nullptr ? nullptr : m_ui->getServerList();
    if (tree == nullptr || serverId.isEmpty()) {
        return;
    }
    QTreeWidgetItem *item = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *candidate = tree->topLevelItem(i);
        if (candidate != nullptr
            && candidate->data(0, Qt::UserRole).toString() == serverId) {
            item = candidate;
            break;
        }
    }
    if (item == nullptr) {
        return;  // 这一行已经不在列表里了（删掉了 / 还没重扫出来）
    }
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    const ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    const ServerModel::Status status =
        process != nullptr
            ? process->status()
            : (entry.installed ? ServerModel::Status::Ready : ServerModel::Status::NotInstalled);

    // 第 0 列：名字 — 档 · 状态。**服务端没有"图标"那一栏能替它说话**（实例行行首有
    // 图标，这一页是名字 + 状态两个字）。
    item->setText(0, QStringLiteral("%1  —  %2 · %3")
                         .arg(entry.name, ServerInstall::flavorName(entry.flavor),
                              ServerModel::statusName(status)));
    QString tip = entry.note;
    if (!tip.isEmpty()) {
        tip += QStringLiteral("\n");
    }
    tip += QDir::toNativeSeparators(entry.dirPath);
    item->setToolTip(0, tip);
    // 没装好的那一行给个"灰一点"的观感（这一页还没有自绘代理）。
    item->setDisabled(!entry.installed);

    // 第 1 列：那颗按钮。**一行只有一颗**，按状态换身份（用户 2026-10-13 定的：
    // 行尾按钮 + 右键菜单；其余几项全在右键里）。
    auto *button = qobject_cast<QPushButton *>(tree->itemWidget(item, 1));
    if (button == nullptr) {
        return;
    }
    if (!entry.installed) {
        button->setText(MainWindowUI::serverRepairText());
        button->setEnabled(true);
        button->setToolTip(tr("还没装好 —— 点它把该下的下下来。"));
        Theme::markButton(button, Theme::ButtonKind::Accent);
    } else if (process != nullptr && process->isRunning()) {
        const bool stopping = status == ServerModel::Status::Stopping;
        button->setText(stopping ? MainWindowUI::serverStoppingText()
                                 : MainWindowUI::serverStopText());
        button->setEnabled(!stopping);
        button->setToolTip(stopping
                               ? tr("已经让它停了，在等它存盘收尾。")
                               : tr("让它自己存盘、收尾，然后退出（推荐）。"));
        Theme::markButton(button, Theme::ButtonKind::Danger);
    } else {
        button->setText(MainWindowUI::serverStartText());
        button->setEnabled(true);
        button->setToolTip(tr("起这个服务端。右键那一列里还有控制台、目录、删除。"));
        Theme::markButton(button, Theme::ButtonKind::Accent);
    }
}

void MainWindow::onServerStatusChanged(const QString &serverId, ServerModel::Status status) {
    updateServerRow(serverId);
    updateServerConsole();
    appendLog(tr("%1：%2").arg(serverId, ServerModel::statusName(status)));
}

void MainWindow::onServerLineAppended(const QString &serverId, const QString &line) {
    // 启动器日志里留一份（排查"它怎么起不来"时我们要的就是它自己的原话）。
    // ★ 只有长到离谱的那些才截断，而且**只截写进日志的这一份** —— 控制台那一页照旧是原话。
    QString logged = line;
    if (logged.size() > kMaxLoggedServerLineChars) {
        logged = logged.left(kMaxLoggedServerLineChars) + QStringLiteral("…（这一行太长，截断了）");
    }
    appendLog(QStringLiteral("[%1] %2").arg(serverId, logged));
    // 控制台那一页正开着它的话，这一行也进那一页（认人认"这一页现在说的是谁"，见控制器）。
    m_serverConsole->appendLine(serverId, line);
}

void MainWindow::onServerForceKillAvailable(const QString &serverId) {
    const ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    const int seconds = (process != nullptr ? process->gracefulMs() : 0) / 1000;
    appendLog(tr("%1：%2").arg(serverId, ServerRun::forceKillNote(seconds)));
    // 正开着它那一页的话，往控制台里也插一行 + 把状态重摆一遍（那颗按钮为什么突然亮了
    // 得有个说法）—— 页不在就不做，判据在控制器里。
    m_serverConsole->announceForceKill(serverId, seconds);
    updateServerRow(serverId);
}

void MainWindow::onServerExited(const QString &serverId, int exitCode) {
    const ServerRun::ServerProcess *process = m_serverWorkspace->process(serverId);
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    const QString shown = entry.name.isEmpty() ? serverId : entry.name;
    // ---------- 这一次是怎么退的（§9.7 重启策略）----------
    const bool userStopped = m_serverStopRequested.remove(serverId) > 0;
    const bool forceKilled = process != nullptr && process->wasForceKilled();
    const ServerRestart::Why why = ServerRestart::whyFor(userStopped, forceKilled, exitCode);
    // 跑够久（`kResetAfterSeconds()`）再崩算新的一轮：一台"每周崩一次"的服务端不该在
    // 第三次之后永远不再自动起来。
    const QDateTime startedAt = m_serverStartedAt.take(serverId);
    if (startedAt.isValid()
        && startedAt.secsTo(QDateTime::currentDateTime())
               >= ServerRestart::kResetAfterSeconds) {
        m_serverRestartAttempts.remove(serverId);
    }
    if (!ServerRestart::isAbnormal(why)) {
        // 正常退出 / 我们自己停的：这一轮结束，计数没必要留着。
        m_serverRestartAttempts.remove(serverId);
    }
    updateServerRow(serverId);
    updateServerConsole();
    if (exitCode == 0) {
        appendLog(tr("%1 已经退出。").arg(shown));
        return;
    }
    if (process != nullptr && process->wasForceKilled()) {
        // "你让我强杀的"和"它自己崩了"是两件事 —— 后者才该弹一个预警框。
        appendLog(tr("%1 已经被强制结束。").arg(shown));
        return;
    }
    appendLog(tr("%1 异常退出（退出码 %2）。").arg(shown).arg(exitCode));
    // ---------- 崩了之后要不要自动拉起（§9.7 重启策略 + 上限）----------
    //
    // 判据全在 `ServerRestart::decide()`（verify ZAF 一节钉死）。这一层只做两件事：
    // 把盘上记的策略读出来、按它的结论排那一发 `singleShot`。
    const ServerModel::Info info = ServerRegistry::readMeta(serverId);
    ServerRestart::Policy policy;
    policy.enabled = info.restartOnCrash;
    policy.maxAttempts = info.restartMaxAttempts;
    const int used = m_serverRestartAttempts.value(serverId);
    const ServerRestart::Decision decision = ServerRestart::decide(policy, why, used);
    if (decision.restart) {
        m_serverRestartAttempts.insert(serverId, used + 1);
        appendLog(tr("%1：%2").arg(shown, decision.note));
        // ★ 这一步**不弹**那张阻塞的预警卡：用户多半不在跟前，弹了他不点，重启就一直等着
        //   那层嵌套事件循环（"自动重启"变成"等用户回来才重启"）。日志里有原话，控制台里
        //   也有它退出前最后那几行。
        QTimer::singleShot(decision.delaySeconds * 1000, this, [this, serverId, shown] {
            const ServerRun::ServerProcess *fresh = m_serverWorkspace->process(serverId);
            if (fresh != nullptr && fresh->isRunning()) {
                return;  // 用户在这几秒里自己又起来了
            }
            appendLog(tr("%1：自动重启来了。").arg(shown));
            startServer(serverId, /*automatic=*/true);
        });
        return;
    }
    if (!decision.note.isEmpty()) {
        // 次数用完了：这一句必须说出来，不然用户会以为它还会自己起来。
        appendLog(tr("%1：%2").arg(shown, decision.note));
    }
    // §11「错误既进入日志，也在用户当前流程中可见」：日志那一半上面已经写了，这一半是
    // 弹窗 —— 用户点了启动之后常常就去干别的了，不弹一下他看不见。
    InlineMessage::warning(
        m_ui->overlay(), tr("这个服务端退出了"),
        tr("「%1」异常退出（退出码 %2）。\n\n控制台里最后那几行写着它退出之前在说什么 ——"
           "右键这一行点「打开控制台」就能看见。")
            .arg(shown)
            .arg(exitCode));
}

int MainWindow::countServerMods(const QString &dirPath) {
    return ServerWorkspaceController::countEnabledMods(dirPath);
}

qint64 MainWindow::serverWorldBytes(const QString &dirPath) {
    return ServerWorkspaceController::defaultWorldBytes(dirPath);
}

void MainWindow::updateNewServerSummary(NewServerPage *page) {
    if (page == nullptr) {
        return;
    }
    // 名字先过一遍（洗空了就没得建）。
    const QString desiredId = ServerRegistry::sanitizeId(page->serverName());
    if (desiredId.isEmpty()) {
        page->setTargetDir(QString());
        page->setSummary(QString(), QStringLiteral("先给它起个名字（会当成目录名）。"));
        return;
    }
    // 「目录」那一行：服务端根目录只有一份（`<数据目录>/Servers`，就是左栏那一列里的根），
    // 这一格是**算出来的** —— 走的和按下「创建」之后真建的那一个**同一条路**（sanitize →
    // unique），所以这一行写的地址和它最后落在盘上的那一个是同一个。
    page->setTargetDir(QDir::toNativeSeparators(
        AppPaths::serverDir(ServerRegistry::uniqueServerId(desiredId))));

    // 再让 `ServerInstall` 判这一份填法成不成立（缺哪一格它会说人话）。
    ServerInstall::Request request;
    ServerInstall::Flavor flavor = ServerInstall::Flavor::Vanilla;
    if (ServerInstall::flavorFromToken(page->flavorToken(), &flavor)) {
        request.flavor = flavor;
    }
    // ★ Spigot：这一轮没接（官方不发成品，要用它的 BuildTools 现编，那一步要本机有 git）。
    //   **按用户 2026-10-13 定的来**：那一档照旧留在下拉框里（用户得知道有这一档），选中它
    //   时明说原因，并把「创建」收起来 —— 不留一颗按下去什么都不发生的按钮。
    //   判据读的是 `ServerInstallRun::flavorSupported()` 这一个出处。
    if (!ServerInstallRun::flavorSupported(request.flavor)) {
        page->setSummary(QString(), ServerInstallRun::unsupportedReason(request.flavor));
        return;
    }
    request.gameVersion = page->gameVersion();
    request.loaderVersion = page->loaderVersion();
    request.installerVersion = page->installerVersion();
    request.buildId = page->buildId();
    const ServerInstall::Plan plan = ServerInstall::planFor(request);
    if (!plan.ok) {
        // `planFor` 给的 `problem` 就是给用户看的那句话（那一层承诺过）。
        page->setSummary(QString(), plan.problem);
        return;
    }
    // 成立了：把"这一档要下什么、装完是什么样"说清楚（`plan.note` 是补充，可空）。
    QStringList lines;
    if (plan.downloads.isEmpty()) {
        lines.append(QStringLiteral("这一档的下载地址要联网现查（列表拉回来才知道下哪个）。"));
    } else {
        QStringList names;
        for (const ServerInstall::Download &download : plan.downloads) {
            names.append(download.fileName);
        }
        lines.append(QStringLiteral("要下：%1").arg(names.join(QStringLiteral("、"))));
    }
    if (plan.firstLaunchNeedsNetwork) {
        lines.append(QStringLiteral("第一次启动还要联网拉东西。"));
    }
    if (!plan.note.isEmpty()) {
        lines.append(plan.note);
    }
    lines.append(ServerInstall::installMethodText(plan.method));
    // 按下「创建」会发生什么，直说一句：不是"建完一个空壳"，是当场排一趟安装。
    lines.append(QStringLiteral("按「创建」会直接开始装（进度在任务中心那一格）。"));
    page->setSummary(lines.join(QStringLiteral("\n")), QString());
}

void MainWindow::openUpdateCenter(const QString &instanceId) {
    OverlayHost *overlay = m_ui->overlay();
    if (overlay == nullptr || instanceId.isEmpty()) {
        return;
    }
    if (m_updateCenterPage == nullptr) {
        m_updateCenterPage = new UpdateCenterPage(this);
        connect(m_updateCenterPage, &UpdateCenterPage::updateRequested, this,
                &MainWindow::onUpdateCenterUpdateRequested);
        connect(m_updateCenterPage, &UpdateCenterPage::ignoreToggled, this,
                &MainWindow::onUpdateCenterIgnoreToggled);
        connect(m_updateCenterPage, &UpdateCenterPage::pinToggled, this,
                &MainWindow::onUpdateCenterPinToggled);
        connect(m_updateCenterPage, &UpdateCenterPage::undoRequested, this,
                &MainWindow::onUpdateCenterUndoRequested);
    }
    // 这一页说的是哪个实例：**动作处理函数全靠它**（页面是复用的，而点动作时它可能已经
    // 关了 —— 不许去问"现在选中的是哪个实例"，同 m_integrityReportVersionId 那条）。
    m_updateCenterInstanceId = instanceId;
    // 摆结论。**必须在 execPage 之前**：那一下阻塞在嵌套事件循环里。
    m_updateCenterPage->setReport(updateCenterReportForPage(instanceId));
    overlay->execPage(m_updateCenterPage, UpdateCenterPage::windowTitle());
    // 收页了：这一页说的那个实例不再作数（下一次开的时候重新记）。
    m_updateCenterInstanceId.clear();
}

UpdateCenterPage::Report MainWindow::updateCenterReportForPage(const QString &instanceId) const {
    // **唯一翻译点**（和 integrityReportForPage() / taskCenterItem() 同一个路数）。
    UpdateCenterPage::Report out;
    out.instanceName = instanceDisplayName(instanceId);

    const auto result = m_instanceUpdateResults.constFind(instanceId);
    if (result == m_instanceUpdateResults.constEnd()) {
        // 没查过：这一页**没有结论**可摆（列表整个空着），用户要看到的是这句话 + 下一步。
        out.error = tr("这个实例还没查过更新。");
        return out;
    }
    const QString instanceDir = AppPaths::versionDir(instanceId);
    int outdatedCount = 0;
    for (const ModDependencies::UpdateVerdict &verdict : result.value().verdicts) {
        UpdateCenterPage::Row row;
        row.path = verdict.item.path;
        row.fileName = QFileInfo(verdict.item.path).fileName();
        // 名字取不到时就空着 —— 页面会退回文件名（`updateCenterRowLabelText()`，
        // 和下面日志里那个 displayName 逐字同一个规矩）。
        row.name = verdict.item.title;
        row.currentSha1 = verdict.item.sha1;
        switch (verdict.state) {
        case ModDependencies::UpdateState::Outdated:
            row.state = UpdateCenterPage::State::Outdated;
            row.targetVersionId = verdict.latestVersionId;
            row.targetText = verdict.latestVersionNumber;
            ++outdatedCount;
            break;
        case ModDependencies::UpdateState::UpToDate:
            row.state = UpdateCenterPage::State::UpToDate;
            // 「已是最新（1.2.3）」—— 版本号就是判"最新"时那一条（`judgeUpdate()` 看的是
            // 列表第一条，`latestVersionNumber` 记的正是它）。
            row.currentText = verdict.latestVersionNumber;
            break;
        case ModDependencies::UpdateState::Unknown:
            row.state = UpdateCenterPage::State::Unknown;
            row.note = verdict.reason;
            break;
        }
        // 「忽略 / 固定」那一档：**这一处算，页面不再判一遍**（判两遍迟早对不上：用户会
        // 看到那一行写着"已忽略"、而它还挂在"可更新"那一组的计数里）。
        const UpdatePolicy::Entry entry = UpdatePolicy::entryOf(
            m_updatePolicy, instanceId,
            UpdatePolicy::storeKey(instanceDir, InstanceFilesPage::identityPath(row.path)));
        switch (UpdatePolicy::suppressed(entry, row.currentSha1, row.targetVersionId)) {
        case UpdatePolicy::Suppress::Ignored:
            row.suppress = UpdateCenterPage::Suppress::Ignored;
            break;
        case UpdatePolicy::Suppress::Pinned:
            row.suppress = UpdateCenterPage::Suppress::Pinned;
            break;
        case UpdatePolicy::Suppress::None:
            break;
        }
        out.rows.append(row);
    }
    out.summary = ModDependencies::updateSummaryText(result.value().verdicts.size(), outdatedCount);
    out.note = result.value().lookupNote;

    // 回滚点：**每次开页都重新读一遍盘**（不缓存 —— 撤销那一下就是把它用掉，缓存一份就
    // 会出现"按了撤销、那一颗还在"）。
    bool hasSnapshot = false;
    const UpdateRollback::Snapshot snapshot = UpdateRollback::read(instanceId, &hasSnapshot);
    if (hasSnapshot) {
        out.undoFileCount = snapshot.fileCount();
        out.undoText = tr("撤销上一次更新（%1 个文件）").arg(snapshot.fileCount());
    }
    return out;
}

void MainWindow::onUpdateCenterUpdateRequested(const QStringList &paths) {
    const QString instanceId = m_updateCenterInstanceId;
    if (instanceId.isEmpty() || paths.isEmpty()) {
        return;
    }
    // 先把这一页收掉（它正卡在 execPage 的嵌套事件循环里），**排到下一轮**再动手 ——
    // 更新那一批失败了会往同一条覆盖层上弹提示，现在不排会撞上还没收干净的这一页。
    // （和完整性报告页那颗「补齐…」是同一个路数。）
    if (m_updateCenterPage != nullptr) {
        m_updateCenterPage->reject();
    }
    const QStringList selected = paths;
    QTimer::singleShot(0, this, [this, instanceId, selected] {
        // 页键给空串：这一批是**跨类**勾出来的，落点全按每条自己那个目录算（见头文件那段）。
        updateInstanceFiles(instanceId, QString(), selected);
    });
}

void MainWindow::onUpdateCenterIgnoreToggled(const QString &path, bool ignored) {
    const QString instanceId = m_updateCenterInstanceId;
    if (instanceId.isEmpty() || path.isEmpty()) {
        return;
    }
    const QString key = UpdatePolicy::storeKey(
        AppPaths::versionDir(instanceId), InstanceFilesPage::identityPath(path));
    if (key.isEmpty()) {
        return;  // 不在实例目录底下（正常走不到）：宁可不记，也不拿一个空键去混记录
    }
    // 要忽略的是**要换上的那一版**（不是"这个项目"）—— 那一版的 id 从上一趟检测的原始
    // 结论里取（`m_instanceUpdateTargets` 那一份是**全部**可更新的，忽略过的那条也在里面）。
    QString targetVersionId;
    if (ignored) {
        const QHash<QString, ResourceCatalog::VersionEntry> targets =
            m_instanceUpdateTargets.value(instanceId);
        const auto it = targets.constFind(path);
        if (it != targets.constEnd()) {
            targetVersionId = it.value().id;
        }
        if (targetVersionId.isEmpty()) {
            // 拼不出"要换成哪一版"就**不许记这一条**：记下去的是"忽略某一版"，而那一版是谁
            // 都不知道 —— 那样记出来的记录永远对不上任何一次检测（等于一颗点了没反应的按钮）。
            InlineMessage::warning(m_ui->overlay(), tr("忽略不了这一条"),
                                   tr("这一条没查出一版可以记的（多半是这一趟检测的结果已经"
                                      "过期了）。重新点一次「检查更新」再试。"));
            return;
        }
    }
    UpdatePolicy::setIgnored(m_updatePolicy, instanceId, key, ignored ? targetVersionId : QString());
    saveUpdatePolicy();
    appendLog(ignored ? tr("「更新中心」：%1 的这一版以后不再提示更新。")
                            .arg(QFileInfo(path).fileName())
                      : tr("「更新中心」：%1 又参与更新提示了。").arg(QFileInfo(path).fileName()));
    rebuildInstanceOutdatedPaths(instanceId);
    // ★ 2026-10-13：按完**当场重喂这一页**。不喂的话那一行照旧写着「可更新到 X」、三颗
    //   chip 也照旧——用户按的是"以后别再提示我"，屏幕上却一点变化都没有，他只能靠"再点
    //   一次看看会不会还写那句话"来判断自己按中了没有。重喂之后那一行写「已忽略」、那一颗
    //   变成「不再忽略」、底下那颗「更新选中的 N 项」的计数也跟着减一。
    //   （`setReport()` 的重建走 `deleteLater()`，所以从 chip 自己的 clicked 里重喂是安全的；
    //     撤销那条路早就是这么做的。）
    if (m_updateCenterPage != nullptr && m_updateCenterInstanceId == instanceId) {
        m_updateCenterPage->setReport(updateCenterReportForPage(instanceId));
    }
}

void MainWindow::onUpdateCenterPinToggled(const QString &path, bool pinned) {
    const QString instanceId = m_updateCenterInstanceId;
    if (instanceId.isEmpty() || path.isEmpty()) {
        return;
    }
    const QString key = UpdatePolicy::storeKey(
        AppPaths::versionDir(instanceId), InstanceFilesPage::identityPath(path));
    if (key.isEmpty()) {
        return;
    }
    // 固定记的是**盘上那一份现在的内容 sha1**（见 UpdatePolicy 文件头：判据手上只有 sha1）。
    // 算不出来（文件读不动）就**不许记** —— 记一条空 sha1 等于记了一条永远判不出来的记录。
    QString sha1;
    if (pinned) {
        for (const ModDependencies::UpdateVerdict &verdict : m_instanceUpdateResults.value(instanceId).verdicts) {
            if (verdict.item.path == path) {
                sha1 = verdict.item.sha1;
                break;
            }
        }
        if (sha1.isEmpty()) {
            InlineMessage::warning(m_ui->overlay(), tr("固定不了这一条"),
                                   tr("这一份文件的内容读不出来（或者上一趟检测没算出它的 sha1），"
                                      "没法记「固定这一版」。"));
            return;
        }
    }
    UpdatePolicy::setPinned(m_updatePolicy, instanceId, key, pinned ? sha1 : QString());
    saveUpdatePolicy();
    appendLog(pinned ? tr("「更新中心」：%1 固定在这一版上，以后不再提示更新。")
                           .arg(QFileInfo(path).fileName())
                     : tr("「更新中心」：%1 不再固定了。").arg(QFileInfo(path).fileName()));
    rebuildInstanceOutdatedPaths(instanceId);
    // 同上（忽略那一档）：按完当场重喂，让那一行写成「已固定」、chip 翻成「不再固定」。
    if (m_updateCenterPage != nullptr && m_updateCenterInstanceId == instanceId) {
        m_updateCenterPage->setReport(updateCenterReportForPage(instanceId));
    }
}

void MainWindow::onUpdateCenterUndoRequested() {
    const QString instanceId = m_updateCenterInstanceId;
    if (instanceId.isEmpty()) {
        return;
    }
    bool hasSnapshot = false;
    QString problem;
    const UpdateRollback::Snapshot snapshot =
        UpdateRollback::read(instanceId, &hasSnapshot, &problem);
    if (!hasSnapshot) {
        InlineMessage::warning(m_ui->overlay(), tr("没有可撤销的更新"),
                               problem.isEmpty() ? tr("这个实例没有回滚点。") : problem);
        return;
    }
    // ★ 问一次再动手：这一下**会覆盖盘上现在的文件**（用户可能已经在游戏里玩了半天、
    //   自己换过东西）。撤销不是一个"点错了也没事"的动作。
    const QString text =
        tr("把「%1」退回上一次更新之前：\n\n"
           "· 换回去 %2 个文件\n"
           "· 删掉那次更新新添的 %3 个文件\n\n"
           "这一趟更新之后你自己改过的东西**会一起被改回去**。")
            .arg(instanceDisplayName(instanceId))
            .arg(snapshot.fileCount())
            .arg(snapshot.addedCount());
    // 就一颗「继续」：这一问的目的是把"会发生什么"说在前面（不是二选一），所以
    // `destructiveIndex` 留空 —— 撤销本身是要用户想清楚才按的，但把它标成一档"危险动作"
    // 的红按钮，和这一段想说的"可以退回去"反而矛盾。
    if (InlineMessage::ask(m_ui->overlay(), MessageKind::Warning, tr("撤销上一次更新"), text,
                           {tr("撤销")}, 0, -1)
        != 0) {
        return;
    }

    const UpdateRollback::RestoreResult result =
        UpdateRollback::restore(snapshot, AppPaths::versionDir(instanceId));
    appendLog(tr("撤销「%1」上一次更新：换回去 %2 个、删掉 %3 个、失败 %4 个。")
                  .arg(instanceDisplayName(instanceId))
                  .arg(result.restored)
                  .arg(result.removed)
                  .arg(result.failed));
    if (!result.ok()) {
        // **一条失败不影响其余**（见 UpdateRollback::restore 那段）：照实说清现在是半新半旧，
        // 并且**留着回滚点**（下一次再按还能把没退回去的那几条退回来）。
        InlineMessage::warning(
            m_ui->overlay(), tr("没能全退回去"),
            tr("换回去 %1 个、删掉 %2 个，有 %3 个没能动。\n\n%4\n\n"
               "回滚点留着，可以再按一次。")
                .arg(result.restored)
                .arg(result.removed)
                .arg(result.failed)
                .arg(result.problems.join(QStringLiteral("\n"))));
    } else {
        // 全退回去了：这一份回滚点**用掉了**（删掉 —— 留着的话下一次开页还会露一颗
        // 「撤销上一次更新」，而它指向的那次更新已经不存在了）。
        UpdateRollback::discard(instanceId);
        appendLog(tr("「%1」已经退回更新前的样子。").arg(instanceDisplayName(instanceId)));
    }
    // 这一批文件的内容换过了 ⇒ 「固定这一版」那几条的判据（sha1）多半已经对不上，
    // 顺手清一遍过期的（不清的话那几条会一直挂着"已固定"，而它其实早就失效了）。
    QHash<QString, QString> sha1ByKey;
    for (const ModDependencies::UpdateVerdict &verdict : m_instanceUpdateResults.value(instanceId).verdicts) {
        const QString entryKey = UpdatePolicy::storeKey(
            AppPaths::versionDir(instanceId), InstanceFilesPage::identityPath(verdict.item.path));
        if (!entryKey.isEmpty()) {
            sha1ByKey.insert(entryKey, verdict.item.sha1);
        }
    }
    if (UpdatePolicy::dropStalePins(m_updatePolicy, instanceId, sha1ByKey) > 0) {
        saveUpdatePolicy();
    }
    // 重摆这一页 + 让文件管理那几页的「可更新」跟着更新（盘上那几个文件已经不是原来那份了）。
    rebuildInstanceOutdatedPaths(instanceId);
    if (m_updateCenterPage != nullptr && m_updateCenterInstanceId == instanceId) {
        m_updateCenterPage->setReport(updateCenterReportForPage(instanceId));
    }
    // 文件管理页那一份"可更新"名单也得重摆（它记的是路径，路径没变，但内容变了）。
    // NOTE: 这一页现在没开着（撤销是从更新中心按的，而更新中心占着覆盖层），所以这里
    //       只需要把**会话缓存**写对 —— 下一次开那一页时由开页那条路补上。
}

void MainWindow::onUpdateBatchFileFinished(const QString &label, const QString &fileName,
                                           bool success, const QString &errorMessage) {
    if (m_updateBatch.label.isEmpty() || m_updateBatch.label != label) {
        return;  // 不是这一批（别的一批资源下载也在发同一个信号）
    }
    QList<int> &pending = m_updateBatch.pendingSteps[fileName];
    if (pending.isEmpty()) {
        return;  // 这一条不在我们登记过的名单里（正常走不到）
    }
    const int index = pending.takeFirst();
    if (m_activityCenter->tasks()->has(m_updateBatch.task)) {
        m_activityCenter->tasks()->setStepState(m_updateBatch.task, index,
                                    success ? Tasks::TaskStepState::Done
                                            : Tasks::TaskStepState::Failed,
                                    success ? QString() : errorMessage);
    }
    if (success) {
        const QString path = m_updateBatch.pathByFileName.value(fileName);
        if (!path.isEmpty()) {
            m_updateBatch.succeeded.append(path);
        }
        m_updateBatch.lines.append(tr("· %1 —— 已经换成新版本").arg(fileName));
    } else {
        ++m_updateBatch.failedSteps;
        m_updateBatch.lines.append(
            tr("· %1 —— 没换成功：%2").arg(fileName, errorMessage));
    }
}

void MainWindow::finishUpdateBatch(bool success, const QString &errorMessage) {
    const UpdateBatchRun run = m_updateBatch;
    m_updateBatch = UpdateBatchRun{};
    if (run.label.isEmpty()) {
        return;
    }
    if (m_activityCenter->tasks()->has(run.task)) {
        m_activityCenter->tasks()->setDetail(run.task, tr("换好 %1 个，失败 %2 个。")
                                               .arg(run.succeeded.size())
                                               .arg(run.failedSteps));
        finishTask(run.task, success && run.failedSteps == 0,
                   run.failedSteps > 0 ? tr("有 %1 个文件没能换掉。").arg(run.failedSteps)
                                       : errorMessage);
    }
    appendLog(success ? tr("更新「%1」结束：换好 %2 个，失败 %3 个。")
                            .arg(instanceDisplayName(run.instanceId))
                            .arg(run.succeeded.size())
                            .arg(run.failedSteps)
                      : tr("更新「%1」没跑完：%2").arg(instanceDisplayName(run.instanceId), errorMessage));
    // ★ §7.1 那条「不只显示总数」：**逐个资源**写进去（日志里那一行一行就是每个资源的结果）。
    for (const QString &line : run.lines) {
        appendLog(QStringLiteral("  ") + line);
    }

    if (!run.succeeded.isEmpty()) {
        // 换掉的这几条从「可更新」名单里摘掉（不摘的话文件管理页上那几行还标着"可更新"，
        // 而它们刚被换成了最新那一版 —— 再点一次「更新」会白下一遍）。
        // NOTE: 只摘**这一批真换成功的**那几条；失败的留在名单里（那一行仍然是对的）。
        const QStringList outdated = m_instanceOutdatedPaths.value(run.instanceId);
        if (!outdated.isEmpty()) {
            QStringList keep;
            for (const QString &path : outdated) {
                if (!run.succeeded.contains(path)) {
                    keep.append(path);
                }
            }
            m_instanceOutdatedPaths.insert(run.instanceId, keep);
        }
        // 那一份"换成哪一版"的账也一起摘（同一次动，见 m_instanceUpdateTargets 那段）。
        QHash<QString, ResourceCatalog::VersionEntry> targets =
            m_instanceUpdateTargets.value(run.instanceId);
        for (const QString &path : run.succeeded) {
            targets.remove(path);
        }
        m_instanceUpdateTargets.insert(run.instanceId, targets);
    }

    const QString text = run.lines.isEmpty() ? errorMessage : run.lines.join(QLatin1Char('\n'));
    if (!success || run.failedSteps > 0) {
        InlineMessage::warning(m_ui->overlay(), tr("更新没能全都换上"),
                               tr("有 %1 个文件没能换掉。\n\n%2\n\n"
                                  "可以在「更新中心」里把上一次更新撤销回去。")
                                   .arg(run.failedSteps)
                                   .arg(text));
    }
    // 收尾之后**再开一次更新中心**：用户刚按的就是它上面那一颗，他下一步要么看看剩下的
    // 那几条、要么撤销（失败了的话）。排到下一轮 + 等覆盖层空下来，和 finishUpdateCheck()
    // 那条路同一个规矩。
    const QString instanceId = run.instanceId;
    runWhenOverlayFree([this, instanceId] { openUpdateCenter(instanceId); });
}

void MainWindow::installResourceVersionNow(ResourceCatalog::Kind kind,
                                           const QString &displayTitle,
                                           const ResourceCatalog::VersionEntry &version,
                                           const QList<ModDependencies::Resolved> &extras,
                                           const QString &forcedInstanceId) {
    const bool isModpack = kind == ResourceCatalog::Kind::Modpack;

    // 目标实例：**优先用调用方递进来的**（前置检查那一趟回来时，页面可能已经关了，而
    // 那一趟在 `m_dependencyCheck.instanceId` 里记着它当时算的是哪个实例 —— 现算会算错），
    // 其次是 `dependencyInstanceId()`（见那一处的两条来路）。整合包那一路不用它 ——
    // 下回来的是 .mrpack，接着走导入，实例名是在那条路上问出来的。
    const QString instanceId =
        isModpack ? QString()
                  : (forcedInstanceId.isEmpty() ? dependencyInstanceId() : forcedInstanceId);
    // "这一趟是从某个实例里点进来的吗" —— 只决定要不要弹"存哪儿"那个框（见 LauncherSettings
    // 里 downloadAutoIntoInstance 那一节）。调用方点名了实例也算。
    const bool cameFromInstance = !forcedInstanceId.isEmpty() || downloadCameFromInstance();
    if (!isModpack && instanceId.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("没有选中实例"),
                             tr("先在「已安装版本」里选一个实例，再挑要装进它的版本。"));
        return;
    }

    // 下到哪儿去。实例那几类是 `<实例目录>/<子目录>`（mods / datapacks / resourcepacks /
    // shaderpacks）；整合包走缓存目录 —— 它不能进 versions/（那底下每一个子目录都会被当成
    // 一个实例）。
    const QString folder = ResourceCatalog::folderForKind(kind);
    const QString defaultDir = isModpack
                                   ? AppPaths::modpackCacheDir()
                                   : AppPaths::versionDir(instanceId) +
                                         QLatin1Char('/') + folder;
    if (!isModpack && folder.isEmpty()) {
        return;  // folderForKind 说了"这一类不走这条安装路"，而它又不是整合包
    }

    // ★★ 2026-09-28：**要不要问一句"存哪儿"**（用户原话见 LauncherSettings 里那一节，
    //    那里是这一支判据唯一的语义出处）。判据就一条：
    //
    //        弹框 = 设置里没勾  或者  这一栏不是从实例设置里点进来的
    //
    //    "是不是从实例设置里点进来的"读的是 `m_downloadFromInstanceId` —— 和那颗
    //    「← 返回实例设置」用的是**同一个值**，因为它俩说的本来就是同一件事（这一栏是从哪个
    //    实例来的）。不另开一个布尔：两个值一旦分开，"露着返回键、却弹了保存框"这种事迟早
    //    会出现。
    //
    // NOTE: 这一支是**在哪里下载**那一层唯一的岔路，所以它写在"算落点"这一句上，而不是
    //       写在按钮那一路：详情页里点一行、下载页里双击一行、文件管理里点名字进详情页再
    //       点一行 —— 三条路都会走到这儿，分流只有一处才不会有哪一条路漏掉。
    // NOTE: 用户在下载页里待久了之后（比如从实例 A 点进来、又自己把「已安装版本」换成 B），
    //       这一位还是非空、于是不再问。这是**刻意的**：那时候落点仍然是"当前实例"（下拉框
    //       里那一个），而这一项勾选框上写的就是"直接放进当前实例"。
    //
    // ★ 2026-10-09：判据从 `m_downloadFromInstanceId.isEmpty()` 换成 `downloadCameFromInstance()`
    //   —— 后者多认一条来路（资源详情页是从实例点进来的那一趟，见那一处）。那条路原来会
    //   在这儿弹一个"存哪儿"的框，而用户明明是从**那个实例的模组列表**里点进来的。
    //   `forcedInstanceId` 非空同样是"这一趟就是给这个实例下的"（前置检查回来那一趟）。
    QString destinationDir = defaultDir;
    // 这一批算"谁在动盘"。默认是那个实例；用户挑了别处的目录时它必须是空的（那底下没有任何
    // 实例在被写，锁着它等于白挡别人启动）。
    QString busyInstanceId = instanceId;
    if (!m_settings->downloadAutoIntoInstance() || !cameFromInstance) {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, tr("下载到哪个文件夹"), defaultDir);
        if (chosen.isEmpty()) {
            return;  // 用户取消 = 这一版不下（一个字都不写盘）
        }
        destinationDir = chosen;
        if (QDir(destinationDir) != QDir(defaultDir)) {
            busyInstanceId.clear();
        }
    }

    const ResourceCatalog::FileEntry *file = version.primaryFile();
    if (file == nullptr || file->url.isEmpty()) {
        // 「接口没给地址」有两种，**不是同一句话**：一种是这一版真的没有文件；
        // 另一种是作者在项目设置里关掉了第三方分发（CurseForge 那条路，`downloadUrl`
        // 给的就是 null，见 FileEntry::unavailableNote）。后者要是也说成"没有可下载的文件"，
        // 用户会当成启动器坏了，然后一遍遍重试 —— 那句话在那儿就写好了，用它。
        const QString note = file == nullptr ? QString() : file->unavailableNote;
        InlineMessage::warning(m_ui->overlay(), tr("下载失败"),
                               note.isEmpty() ? tr("这一版没有可下载的文件。") : note);
        return;
    }

    // 加载器对不对得上。**这一层没有按加载器筛过**（见 onResourceItemActivated 里那段
    // NOTE），所以在这儿补一道闸：给一个 Fabric 实例装一个 Forge 的模组，症状是"游戏进去就
    // 崩"，而那一刻没人会想到是刚才那个模组。
    //
    // NOTE: 这道闸**只管模组**（2026-09-25 加数据包时收的）。理由：`VersionEntry::loaders`
    //       不是只有模组加载器 —— 资源包那一版列的是 `minecraft`、数据包列的是 `datapack`、
    //       光影列的是 `iris/optifine`，它们跟"这个实例装的是什么模组加载器"根本没有关系。
    //       不收的话，给一个 Forge 实例装资源包/数据包会**每次都弹一次**「加载器对不上」，
    //       而那句话（"这一版是给 minecraft 用的"）本身就说不通 —— 那种框弹多了，用户就
    //       再也不看这类提醒了，真正该拦的那一次也跟着一起被忽略。
    const bool loaderMatters = kind == ResourceCatalog::Kind::Mod;
    // 判据刻意做得**窄**（只拦"知道实例是什么加载器、且这一版只列了另一种"这一种）：
    //   · 不是模组（资源包 / 数据包 / 光影 / 整合包）→ 放行；
    //   · 认不出实例的加载器（纯原版，或者 json 里没写）→ 放行；
    //   · 这一版列了不止一种加载器（Quilt 之类兼容好几种）→ 放行；
    //   · 纯原版实例 + 一个带加载器的模组 → 也放行（认不出来的和纯原版在这里分不开，
    //     宁可漏拦也不要拦住一个本来装得上的）。
    // 拦下来的是**问一句**，不是禁止 —— 用户说"就要这么装"就照装。
    //
    // ★ 2026-09-27：比的这一下从前是 `contains()` —— 子串比较。于是这一个闸**恰好漏掉了它
    //   最该拦的那一对**：实例是 Forge、这一版是 NeoForge 的模组时，
    //   `"neoforge".contains("forge")` 为真 ⇒ 不报，直接装下去；而反过来（NeoForge 实例装
    //   只列了 forge 的模组）也一样漏。两个名字是**互为子串**的，这一对照的是"同一族里的
    //   另一个"，恰恰是症状最像"装上了、进去就崩"的那一种。
    //   改成一字不差地比（大小写不敏感：实例那边是显示名 `NeoForge`，模组那边是小写）。
    const VersionDisplayParts parts = m_versionManager->displayParts(instanceId);
    // ★ 加载器名走 instanceLoaderName()，**不是** `parts.loaderName`：后者只有我们自己的
    //   安装器会写，PCL 那类合并实例（用户盘上 70 个里 67 个）读出来是空的 —— 于是这道闸
    //   对它们**恒不放行**，"加载器对不上"那句话一次都弹不出来（它最该拦的那一对恰好就
    //   在这些实例上）。理由同 instanceModsUsable() 那段。
    QString instanceLoaderVersion;
    const QString instanceLoader =
        instanceLoaderName(parts, m_versionManager, &instanceLoaderVersion);
    if (loaderMatters && !instanceLoader.isEmpty() && version.loaders.size() == 1
        && version.loaders.first().compare(instanceLoader, Qt::CaseInsensitive) != 0) {
        const auto answer = InlineMessage::question(m_ui->overlay(), tr("加载器对不上"),
            tr("这个实例是 %1 %2，而这一版是给 %3 用的。\n\n"
               "装进去多半不会生效（游戏进去可能直接崩）。还要装吗？")
                .arg(instanceLoader, instanceLoaderVersion, version.loaders.first()), tr("是"), tr("否"), false);
        if (!answer) {
            return;
        }
    }

    QList<VersionManager::ResourceFile> files;
    VersionManager::ResourceFile resourceFile;
    resourceFile.url = file->url;
    resourceFile.filename = file->filename;
    resourceFile.sha1 = file->sha1;
    resourceFile.size = file->size;
    files.append(resourceFile);

    // ★ 2026-09-28：一起下的前置**并进同一批**（用户在框里按了「一起下载」）。
    //   并成一批而不是"接着再下一批"是有缘故的：`downloadResourceFiles()` 一批一个进度、
    //   一条"下好了"的日志、一次 `resourceDownloadFinished`。分两批的话，用户会先看到
    //   "下好了"、然后前置才开始下 —— 而那一刻他多半已经点进游戏了（那时候缺的正是前置）。
    //
    // NOTE: 前置的落点就是这个实例的 mods（`destinationDir` 是按 kind=模组算出来的）——
    //       前置本来就是模组，和主文件同一个目录，这一点不用另判。
    for (const ModDependencies::Resolved &extra : extras) {
        if (extra.file.url.isEmpty() || extra.file.filename.isEmpty()) {
            continue; // 走不到（resolveDependency 已经挡过一道），挡第二道不费什么
        }
        VersionManager::ResourceFile extraFile;
        extraFile.url = extra.file.url;
        extraFile.filename = extra.file.filename;
        extraFile.sha1 = extra.file.sha1;
        extraFile.size = extra.file.size;
        files.append(extraFile);
    }

    QString label = QStringLiteral("%1 %2").arg(displayTitle, version.versionNumber);
    if (!extras.isEmpty()) {
        // 日志和"下好了"那句话上写清楚这一批里**还有前置**（不然用户看到下了好几个文件、
        // 而屏幕上只说了一个模组的名字，会以为下错了）。
        label += tr("（含 %1 个前置）").arg(extras.size());
    }
    // 下完接着走导入那条路（那边会问实例名、挑 Java，然后解包、下清单里那几百个文件）。
    // 路径按**接口给的文件名**算 —— 落盘用的就是它（见 downloadResourceFiles），这里
    // 不另编一套命名。
    const QString modpackPath =
        isModpack ? destinationDir + QLatin1Char('/') + file->filename : QString();
    appendLog(tr("正在下载 %1（%2）。").arg(label, QDir::toNativeSeparators(destinationDir)));
    // busyInstanceId 传空的有两路：整合包下到缓存里（盘上没有任何实例在被写），以及用户
    // 自己挑了实例之外的目录那一路 —— 两路都不该锁住谁（见上面算落点那一段）。
    const bool started =
        m_versionManager->downloadResourceFiles(destinationDir, busyInstanceId, label, files);
    // ★ 登记**必须**在这一批真的开起来之后。被拒绝时上面那次调用**照样会发**一个
    //   `resourceDownloadFinished(label, false, …)`，label 还是同一个字符串 —— 先登记的话，
    //   那一下就会把记录 take() 走。这不是假想的：双击那一行"点它就是下它"时，第二发
    //   撞的正是这道闸，于是真的整合包下完时记录已经没了 → 界面弹「已经装进实例里了」，
    //   而实际**一个字节都没导入**。先问"开起来没有"就没有这个窗口。
    if (started && !modpackPath.isEmpty()) {
        m_pendingModpackImports.insert(label, modpackPath);
    }
}

// ============================ 资源详情页 ============================
//
// 用户原话：「下载页选择了模组以后进入一个独立界面，上方写模组图像 名字 来源，然后有转到
// modrinth/curseforge/mcmod.cn 的选项，中间一排版本，下方排列版本，和 pcl 类似」。
// "独立界面"按那条总目标落成**整页覆盖层**（`OverlayHost::execPage`，顶上四个选项卡和
// 底栏照旧露着，不另开窗）。
//
// 三件事的分工（写清楚免得下次改错地方）：
//   - **界面的样子**全在 ui/ResourceDetailPage.{hpp,cpp}（它不 include 任何 src/ 下的头）；
//   - **数据**全在 src/download/ResourceCatalog（拼网址、分组、时间那句、渠道那一档）；
//   - **下面这一片**是接线：谁去问网络、点哪颗按钮干什么、哪一行是"下它"。
//     所以这里没有一行"这一页长什么样"的代码，那一页也没有一处"要不要联网"的决定。

void MainWindow::setupResourceDetailPage() {
    if (m_resourceDetailPage != nullptr) {
        return;
    }
    m_resourceDetailPage = new ResourceDetailPage(this);
    connect(m_resourceDetailPage, &ResourceDetailPage::modSettingsRequested, this, [this] {
        if (m_resourcePage.open && m_resourceDetailPage->modSettingsSlotShown()
            && !m_resourcePage.instanceId.isEmpty()) {
            m_resourceDetailPage->accept();
        }
    });
    // NOTE: 这一页只发"用户想做什么"，一件事都不自己干 —— 收到信号之后干什么全在下面
    //       这几个槽里。所以它能在 ui_check 里被单独构造、单独驱动（不联网、不读盘）。
    connect(m_resourceDetailPage, &ResourceDetailPage::chipChanged, this,
            &MainWindow::onResourcePageChipChanged);
    connect(m_resourceDetailPage, &ResourceDetailPage::showSnapshotsChanged, this,
            &MainWindow::onResourcePageShowSnapshotsChanged);
    connect(m_resourceDetailPage, &ResourceDetailPage::rowActivated, this,
            &MainWindow::downloadResourcePageVersion);
    // 开浏览器 / 复制名字**由这一层动手**（那一页只发信号）：开浏览器要走
    // QDesktopServices（验证工程把 url handler 换掉就能拦，见那一页的文件头），
    // 复制要能进日志。
    connect(m_resourceDetailPage, &ResourceDetailPage::openExternalRequested, this,
            [this](const QString &url) {
                if (url.isEmpty()) {
                    return;
                }
                appendLog(tr("正在打开 %1。").arg(url));
                // 和"打开实例文件夹"那条走的是同一个东西（都是"把这件事交给系统"）。
                // 开不了要说一句：那颗按钮看着像是我们自己开的页面，静静地什么都不发生，
                // 用户只会一遍遍点它。
                if (!QDesktopServices::openUrl(QUrl(url))) {
                    InlineMessage::warning(m_ui->overlay(), tr("打不开浏览器"),
                                           tr("没能打开这一条网址：\n%1").arg(url));
                }
            });
    connect(m_resourceDetailPage, &ResourceDetailPage::copyNameRequested, this,
            [this](const QString &text) {
                if (text.isEmpty()) {
                    return;
                }
                // 复制完**不弹话**：这一下是用户自己按的，而剪贴板里那串东西他自己
                // 粘一下就看得见。进日志留个痕就够（排查"我复制的怎么不对"时有用）。
                if (QClipboard *clipboard = QApplication::clipboard()) {
                    clipboard->setText(text);
                    appendLog(tr("已复制名称：%1").arg(text));
                }
            });
}

void MainWindow::applyResourcePageChips(const QStringList &declaredGameVersions,
                                        bool showSnapshots, const QString &wanted) {
    if (m_resourceDetailPage == nullptr) {
        return;
    }
    // 第一颗是「全部」（key 空串 = 不筛）。其余每一颗的 key **和文字是同一串** ——
    // 这一排上没有"显示名"这回事（游戏版本号本身就是用户在界面上要找的那个东西）。
    QList<ResourceDetail::Chip> chips;
    ResourceDetail::Chip all;
    all.text = tr("全部");
    chips.append(all);
    for (const QString &gameVersion :
         ResourceCatalog::gameVersionChoices(declaredGameVersions, showSnapshots)) {
        ResourceDetail::Chip chip;
        chip.key = gameVersion;
        chip.text = gameVersion;
        chips.append(chip);
    }
    m_resourceDetailPage->setChips(chips, wanted);
}

int MainWindow::openResourceDetailPage(const QString &iconKey, const QString &kindKey,
                                       DetailSource from, const QString &instanceId) {
    OverlayHost *overlay = m_ui->overlay();
    if (overlay == nullptr || overlay->isOpen()) {
        // 已经有覆盖层开着（另一页、或者一条提示）。这一层不做页面栈，这一次就不开了 ——
        // 和 `openPage()` 返回 nullptr 是同一件事（见 OverlayHost.hpp 那段）。
        return kResourcePageNotOpened;
    }
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    if (!ResourceCatalog::kindFromCategoryKey(kindKey, &kind)) {
        return kResourcePageNotOpened;  // 这一行不是我们建的（正常走不到）
    }
    const auto hit = m_resourceHits.constFind(iconKey);
    if (iconKey.isEmpty() || hit == m_resourceHits.constEnd()) {
        // 条目上那把钥匙在表里找不到 = 这一行是上一批搜索留下的（重搜过）。用户没做错
        // 什么，不弹窗（和点第三层时那句"清单里没它"同一个规矩）。
        return kResourcePageNotOpened;
    }
    setupResourceDetailPage();
    ResourceDetailPage *page = m_resourceDetailPage;
    if (page == nullptr) {
        return kResourcePageNotOpened;
    }

    // 这一次说的是哪个资源、列的是哪一份。**开页之前就要摆好**：下面 execPage 一进去
    // 就卡住了，那之后写的每一句都只有等用户点返回才执行得到。
    m_resourcePage.open = true;
    m_resourcePage.kind = kind;
    m_resourcePage.source = hit->source;
    m_resourcePage.projectId = hit->projectId;
    m_resourcePage.iconKey = iconKey;
    m_resourcePage.title = hit->title;
    m_resourcePage.chip.clear();
    m_resourcePage.showSnapshots = false;   // 开页一律从"不显示快照版"开始（勾选框的默认值）
    m_resourcePage.versions.clear();
    m_resourcePage.loaded = false;
    // 钥匙里游戏版本和加载器都空 = **不筛**（"这个资源一共发过哪些版本"）。
    // 这也正是它和树那条路的钥匙不会撞的原因：树上那把一定带着一个游戏版本号。
    m_resourcePage.requestKey = ResourceCatalog::versionsRequestKey(
        hit->projectId, QString(), QString(), hit->source);

    ResourceDetail::Header header;
    header.title = hit->title;
    header.description = hit->description;
    header.metaLine = ResourceCatalog::hitMetaLine(*hit);
    // 「转到 Modrinth / CurseForge」那两颗：网址是**那家自己网页的形状**（见
    // projectPageUrl，类名拼错了回 404 而不报错）。拼不出来（slug 空）那一页就不画那颗
    // 按钮 —— 摆一颗点了没反应的键比不摆糟。
    header.openLabel = ResourceCatalog::sourceLabel(hit->source);
    header.openUrl = ResourceCatalog::projectPageUrl(hit->source, kind, hit->slug);
    // MC 百科那边**没有按 id 直接跳的路**（它是另一套号），只能拿名字去搜 —— 这是
    // "跳到那儿附近"，不是"跳到那一页"。
    header.mcmodUrl = ResourceCatalog::mcmodSearchUrl(hit->title);
    header.nameToCopy = hit->title;
    page->setHeader(header);

    // 中间那一排游戏版本 chip：**搜索响应里本来就带着**"这个资源支持哪些游戏版本"
    // （`Hit::gameVersions`，顺序由 gameVersionChoices 统一成新的在前），所以开页这一下
    // 不用为它多发一条请求 —— 勾「显示快照版」那一下也一样，重算用的是存下来的这一份。
    //
    // NOTE: 这一份**声明表要留着**（不止是拿来摆一次 chip）：勾选框一拨，chip 那一排整个
    //       换一份（折 / 不折是两份不一样的表），那时候手上只有开页时存下来的它。
    m_resourcePage.declaredGameVersions = hit->gameVersions;
    // 勾选框一律从不勾开始（那一页自己的默认值，见 ResourcePageState::showSnapshots）。
    page->setShowSnapshots(false);

    // ---------- 从某个实例进来的那一次：预选到**那个实例的**版本 / 加载器（2026-09-28）----------
    //
    // 用户原话：「点开某个模组等的时候将版本自动选到当前实例对应的那个，对应的模组加载器
    // 的版本放在前面，资源包等同理，展示为对应版本适用的」。三件事一起落到这儿：
    //   ① chip 预选到那个实例的游戏版本 ⇒ 底下那一份版本列表**只列适用于那个版本的**
    //      （筛就是 `versionsForGameVersion()`，本来就是每按一颗 chip 做一次的事）；
    //   ② 分组时把那个实例的加载器的组排到前面（`groupVersions()` 的 `preferredLoader`）；
    //   ③ 预选**存进 `m_resourcePage`**：勾「显示快照版」那一下要把 chip 那一排整个重摆，
    //      那时还得知道"按着的是哪一个"，不然用户拨一下勾选框就被甩回「全部」了。
    //
    // NOTE: 那两份值是**现在**读的（`instanceGameVersion` 读盘）。开页这一下只读一次，
    //       一点问题没有；**别**把它挪进 `fillResourcePageGroups()` 那种每按一颗 chip 就
    //       跑一遍的路径（那是每按一下读一次盘）。
    // NOTE: 加载器要从**显示名**（「Fabric」/「NeoForge」）换算成**小写那一套**
    //       （`VersionGroup::loader` 的写法），换算在下面那个 helper 里 —— 直接递显示名进去
    //       会一个字都比不上，症状是"排在前面"永久不生效而屏幕上一点异常都没有。
    m_resourcePage.instanceId = instanceId;
    m_resourcePage.preferredLoader.clear();
    QString wantedChip;
    if (!instanceId.isEmpty()) {
        m_resourcePage.preferredLoader = loaderGroupKey(
            instanceLoaderName(m_versionManager->displayParts(instanceId), m_versionManager, nullptr));
        // 换算走 `gameVersionAfterToggle()`（那一份规则只有那一处，别自己比大版本）：实例报
        // 的是**原样**那个版本号（可能是 26.2-rc1），而 chip 那一排这会儿是**折过**的那一份
        // （不勾「显示快照版」）—— 换算不出来（这个资源压根没声明过那个版本）回空串，
        // 于是停在「全部」那颗上，和用户自己点了一颗表里没有的 chip 是同一个结果。
        wantedChip = ResourceCatalog::gameVersionAfterToggle(
            hit->gameVersions, m_resourcePage.showSnapshots,
            m_versionManager->instanceGameVersion(instanceId));
    }
    applyResourcePageChips(hit->gameVersions, m_resourcePage.showSnapshots, wantedChip);
    // ★ 按着的是哪一颗**以页面为准**（和 onResourcePageShowSnapshotsChanged 里那一句逐字
    //   同一个理由）：上一步要是把"表里没有的那个 key"纠正成了「全部」，两位得说的是同一
    //   件事。**这一句不能省** —— `m_resourcePage.chip` 就是 `fillResourcePageGroups()`
    //   拿来筛的那一份，光把 chip 按下去而这一位还空着，症状是"那一颗亮着、底下却列着
    //   全部版本"（看着像筛没生效）。
    m_resourcePage.chip = m_resourceDetailPage->currentChip();

    // ★ 右上角那一格**每次开页都显式摆一次**（2026-09-27）：这一页是**同一个对象、建一次
    //   留着的**，两个入口（下载页 / 实例的文件管理页）共用它 —— 不摆的话就留着上一次那
    //   一格（先下载页点个模组、再从实例点个模组进来，看见的还是「显示快照版」）。
    //
    //   判据是**两样都要**：来路是实例的文件管理页，**而且**这一条是个模组。少一样都不摆
    //   —— 从实例点一个**资源包**进来照旧摆「显示快照版」（那一颗在那一页上是有用的：那一
    //   排 chip 折的正是"这个资源支持哪些游戏版本"）。
    //   ★ 上面那一发 `applyResourcePageChips(…, wantedChip)` **就是**这一段原来那一发
    //     （原来写死 `QString()` = 一律从「全部」开始，2026-09-28 起改成预选到实例的版本）。
    //     别再在这儿补一次 `QString()` 的那一发 —— 那会把上面刚按下去的那一颗整个抹回
    //     「全部」，而 `m_resourcePage.chip` 还留着实例那个值：屏幕上一颗亮的都没有、底下
    //     列的却是按实例版本筛过的（正是 fillResourcePageGroups 那段点名的"两位各说各的"）。
    page->setModSettingsSlot(from == DetailSource::InstanceFiles && kind == ResourceCatalog::Kind::Mod);
    // 图标是异步来的：先清掉上一次那一张（不然会留着上一个资源的图标，比空着糟），
    // 那一格的位置照旧留着，图回来时整张卡不会往左跳一下。
    page->setIcon(QPixmap());
    page->setPlaceholder(tr("正在读版本列表…"));
    // 中间那一行也**每次开页清一次**（同上：这一页是建一次留着的，不清就留着上一个
    // 资源的"要装的前置"，而那一行看着像是这个资源的）。
    m_resourcePage.dependencies.clear();
    m_resourcePage.dependencyRequestKey.clear();
    page->setDependencies(QString());

    // 图标：列表那几行早就为这一条要过了（见 fillResourceHitRows 最后那句），内存/盘上
    // 命中就是**同步**回来的 —— 于是这一句常常在下面 execPage 之前就把图摆好了。
    m_resourceStore->requestIcon(*hit);
    // 版本列表（整份，不筛）。答复落在 onResourceVersionsFinished 里，判据是
    // `m_resourcePage.requestKey` —— 和树那条路按行重算钥匙的做法不同：那一页没有
    // 条目可查（它自己那份清单是空的），所以这儿直接比钥匙。
    m_resourceStore->fetchVersions(m_resourcePage.requestKey, hit->projectId, hit->source,
                                   QString());

    // **最后**才开页：这一句会一直卡在这儿（内部那个嵌套事件循环）转到用户点返回。
    // NOTE: 上面发出去的两条请求都是异步的，答复会在那个循环里落地 —— 所以"先发请求、
    //       再开页"和"先开页、再发请求"对用户是同一件事；写成这样只是因为这一句之后
    //       的控制流要等用户。
    const int result = overlay->execPage(page, page->titleText());
    // 点了返回（或者 Esc）：状态落下去。**页面本身留着**（它是懒建的那一份），下次点开
    // 还是它，setHeader/setChips 会把上一次那些字全换掉。
    m_resourcePage.open = false;
    return result;
}

void MainWindow::onResourcePageChipChanged(const QString &gameVersion) {
    if (!m_resourcePage.open) {
        return;
    }
    m_resourcePage.chip = gameVersion;
    fillResourcePageGroups();
}

void MainWindow::onResourcePageShowSnapshotsChanged(bool showSnapshots) {
    if (!m_resourcePage.open || m_resourceDetailPage == nullptr) {
        return;
    }
    if (m_resourcePage.showSnapshots == showSnapshots) {
        // 值没变（比如开页那一下把勾选框摆回去）。**不往下走**：下面那一段会把 chip 那一排
        // 整个重建，而重建会清掉用户正按着的那一颗的按压状态（虽然换算之后还是它）——
        // 白白闪一下不值当。
        return;
    }
    m_resourcePage.showSnapshots = showSnapshots;
    // chip 那一排整个换一份（折与不折是两份不一样的表），现在按着的那一颗**换算成同一个
    // 大版本在另一档里的说法**（26.2-rc1 ↔ 26.2）—— 勾这一下不该把用户从他正在看的
    // 那条线上甩回「全部」。换算的规则只有一处，见 ResourceCatalog::gameVersionAfterToggle。
    const QString wanted = ResourceCatalog::gameVersionAfterToggle(
        m_resourcePage.declaredGameVersions, showSnapshots, m_resourcePage.chip);
    applyResourcePageChips(m_resourcePage.declaredGameVersions, showSnapshots, wanted);
    // 按着的是哪一颗**以页面为准**：换算出来的那一颗要是表里没有（对不上），`setChips`
    // 会把它纠正成「全部」，这儿跟着抄回来 —— 两个状态各说各的才是真出问题的地方。
    m_resourcePage.chip = m_resourceDetailPage->currentChip();
    // setChips **不发** chipChanged（它不是"用户点了"那一发）—— 所以这一句得自己来，
    // 它就是用户要的「更改以后自动刷新版本列表」。
    fillResourcePageGroups();
}

void MainWindow::fillResourcePageGroups() {
    if (!m_resourcePage.open || m_resourceDetailPage == nullptr) {
        return;
    }
    // 还没回来：chip 是在这一页上点的，而版本列表可能还在路上（用户手快）。
    // **不能**在这儿说"这一档没有版本" —— 那是在替一份还没到的数据回答。
    if (!m_resourcePage.loaded) {
        m_resourceDetailPage->setPlaceholder(tr("正在读版本列表…"));
        return;
    }
    // ★ 这一页当下摆在**最上面那一版**（= 用户一眼看到、最可能点的那一条）。下面那一段
    //   版本列表中间那一行说的是"这一版要装的前置"（用户 2026-09-28 要的），而"这一版"
    //   指的就是它。取的是**分组之后**的最前一条（`groupVersions` 把新游戏版本那一组摆在
    //   最前、组内也是新的在前），不是 filtered 的第一条 —— 两条在"筛了某一档"时能不一样。
    //   NOTE: 存的是**一份拷贝**不是指针：`groupVersions()` 那一串是这一句里的临时表，
    //         指着它出了这个 for 就是悬空的（而这一页的状态要活到下一次刷）。
    ResourceCatalog::VersionEntry topmost;
    bool hasTopmost = false;

    // 筛（看的是这一版**声明过**的每一个游戏版本，不是它落在哪个组里 —— 那条规矩和它的
    // 坑都在 versionsForGameVersion 的注释里），再按「游戏版本 + 加载器」分组。
    // 两步**分开**：合并了就是上面那条坑（筛 1.20.1 会漏掉一半支持它的版本）。
    const QList<ResourceCatalog::VersionEntry> filtered = ResourceCatalog::versionsForGameVersion(
        m_resourcePage.versions, m_resourcePage.chip, m_resourcePage.showSnapshots);
    QList<ResourceDetail::Group> groups;
    // 第三个参数 = 从实例进来时那个实例的加载器（**小写**，见 loaderGroupKey）：同一个游戏
    // 版本里把它的组排到前面（用户原话「对应的模组加载器的版本放在前面」）。不是从实例
    // 进来的那一次是空串 = 照旧按固定的 fabric/forge/neoforge/quilt 次序。
    for (const ResourceCatalog::VersionGroup &group :
         ResourceCatalog::groupVersions(filtered, m_resourcePage.showSnapshots,
                                        m_resourcePage.preferredLoader)) {
        ResourceDetail::Group out;
        // 组头写的是**这一条落在哪个游戏版本下**（分组规则见 groupVersions）。和上面选的
        // 那一档不一致是正常的（一条支持两档的版本落在新的那一档下），**不加**「（所选
        // 版本）」那种后缀：每一组都挂一句一样的话，那一句就不是信息了。
        out.title = group.title();
        for (const ResourceCatalog::VersionEntry &entry : group.versions) {
            if (!hasTopmost) {
                topmost = entry;
                hasTopmost = true;
            }
            ResourceDetail::Row row;
            row.key = entry.id;
            // 正文写**版本号**（不是文件名）：这一页一屏几十条版本，认人的是版本号；
            // 文件名在小字那一截里（versionRowSubText 的第二截）。树那条路正相反
            // （那一层是"挑一个文件下来"，写的就是落盘那个名字）—— 两处不一样是有意的。
            row.title = entry.versionNumber;
            row.subText = ResourceCatalog::versionRowSubText(entry);
            row.channel = ResourceCatalog::releaseChannelRank(entry.versionType);
            const ResourceCatalog::FileEntry *file = entry.primaryFile();
            if (file != nullptr && !file->filename.isEmpty()) {
                // 鼠标停上去看**要下哪个文件**（正文放不下、也不该放）。
                row.tooltip = file->filename;
            }
            out.rows.append(row);
        }
        groups.append(out);
    }
    // 中间那一行（"这一版要装的前置"）跟着**这一次摆上去的那一份**走 —— 列表空的（下面
    // 两种空）就整个收起来：那时候屏幕上一条版本都没有，"这一版"指的是哪一版就说不清了。
    m_resourcePage.dependencies =
        hasTopmost ? ModDependencies::displayedRequiredDependencies(topmost)
                   : QList<ResourceCatalog::Dependency>();
    updateResourcePageDependencies();

    if (groups.isEmpty()) {
        // 两种"空"**不是同一句话**：选了某一档之后一条都没有（这一档上没有），和这个资源
        // 一条版本都没发过（那一页自己那句，`setGroups()` 收到空表时摆的就是它）。
        if (m_resourcePage.chip.isEmpty()) {
            m_resourceDetailPage->setGroups({});
        } else {
            m_resourceDetailPage->setPlaceholder(tr("这个游戏版本上没有可下载的版本。"));
        }
        return;
    }
    m_resourceDetailPage->setGroups(groups);
}

void MainWindow::updateResourcePageDependencies() {
    if (!m_resourcePage.open || m_resourceDetailPage == nullptr) {
        return;
    }
    const QList<ResourceCatalog::Dependency> &dependencies = m_resourcePage.dependencies;
    if (dependencies.isEmpty()) {
        m_resourceDetailPage->setDependencies(QString());   // 没有前置 = 这一行不画
        m_resourcePage.dependencyRequestKey.clear();
        return;
    }

    // 名字：先查会话缓存，缺的那几个凑成一批去问（**只问缺的**——已经知道的不该再问一遍，
    // 而这一行每换一次 chip 就要重算一次）。
    QStringList names;
    names.reserve(dependencies.size());
    QStringList unknownIds;
    for (const ResourceCatalog::Dependency &dependency : dependencies) {
        const QString name =
            m_projectTitles.value(projectTitleKey(m_resourcePage.source, dependency.projectId));
        if (name.isEmpty()) {
            if (!unknownIds.contains(dependency.projectId)) {
                unknownIds.append(dependency.projectId);
            }
        }
        names.append(name);   // 空名字也占一格（下面按"有没有空串"判整个名单全不全）
    }

    if (unknownIds.isEmpty()) {
        m_resourceDetailPage->setDependencies(ModDependencies::detailPrerequisiteText(names));
        m_resourcePage.dependencyRequestKey.clear();
        return;
    }

    // 名字还没到：**先摆"有几条"那句**，别空着 —— 空着的症状是"这一版到底要不要前置"
    // 看不出来，而那正是用户要这一行的理由。名字回来之后再换成那一串。
    m_resourceDetailPage->setDependencies(
        ModDependencies::detailPrerequisiteCountText(dependencies.size()));
    m_resourcePage.dependencyRequestKey = ModDependencies::projectsKeyFor(
        QStringLiteral("detail"), unknownIds, m_resourcePage.source);
    m_resourceStore->fetchProjects(m_resourcePage.dependencyRequestKey, unknownIds,
                                   m_resourcePage.source);
}

void MainWindow::onResourceProjectsFinished(const QString &requestKey,
                                            const QList<ResourceCatalog::Hit> &hits,
                                            const QString &problem) {
    // 不是这一页等的那一发就丢。**别在这里顺手替"前置那一趟"收名字**：两边的来源可能
    // 不一样（详情页那个资源是 Modrinth 的、要装进去的那个实例里那个模组是 CurseForge
    // 的），而来源是那把键的一半 —— 拿错来源写进缓存的名字会挂在**错误的那把键**上，
    // 于是另一个人永远查不到它（症状和"名字没取到"一模一样）。各自收各自的。
    if (!m_resourcePage.open || requestKey.isEmpty()
        || requestKey != m_resourcePage.dependencyRequestKey) {
        return;
    }
    rememberProjectTitles(hits, m_resourcePage.source);
    // 没取到**不是错误**（那一页上还有"有几条"那句），所以不弹窗、只记一句日志 ——
    // 和前置那一趟同一条规矩（见 onDependencyProjectsFinished）。
    if (!problem.isEmpty()) {
        appendLog(tr("前置模组的名字没取到：%1").arg(problem));
    }
    // 名字已经进缓存了：再走一次那一段就会走到"全都有了"那条路上去（**不会**再发一发 ——
    // 缺的那几个现在是空的，见上面 unknownIds 那段）。
    updateResourcePageDependencies();
}

QString MainWindow::projectTitleKey(ResourceCatalog::Source source, const QString &projectId) {
    // `Hit::iconCacheId()` 是那个拼法（`<来源>-<项目号>`）的**唯一出处** —— 别在这儿再拼
    // 一遍字符串：拼法一改，图标缓存、实例里那份身份表、这个缓存三处就各认各的了。
    ResourceCatalog::Hit hit;
    hit.projectId = projectId;
    hit.source = source;
    return hit.iconCacheId();
}

void MainWindow::rememberProjectTitles(const QList<ResourceCatalog::Hit> &hits,
                                       ResourceCatalog::Source source) {
    for (const ResourceCatalog::Hit &hit : hits) {
        if (hit.projectId.isEmpty() || hit.title.isEmpty()) {
            continue;
        }
        m_projectTitles.insert(projectTitleKey(source, hit.projectId), hit.title);
    }
}

void MainWindow::downloadResourcePageVersion(const QString &versionId) {
    if (!m_resourcePage.open || versionId.isEmpty()) {
        return;
    }
    for (const ResourceCatalog::VersionEntry &entry : m_resourcePage.versions) {
        if (entry.id == versionId) {
            // 和树上点第三层那一下**是同一件事**（选哪个实例、落到哪个目录、加载器那道闸、
            // 整合包下完接导入，全在那边那一个函数里）。
            installResourceVersion(m_resourcePage.kind, m_resourcePage.title, entry,
                                   m_resourcePage.source);
            return;
        }
    }
    // 行在、这份清单里没有它：正常走不到（那些行就是从这份清单摆出来的）。
}

void MainWindow::onResourceDownloadFinished(const QString &label, bool success,
                                            const QString &errorMessage) {
    // 整合包那一页下回来的 .mrpack：下完要接着走导入那条路。这一单**只活一次**，所以先
    // 取走再判成功与否 —— 不清的话，用户手动导入过一个包之后，下一个下完的模组也会顺手
    // 再触发一次导入。
    //
    // NOTE: 按 `label` **只取自己那一单**（不是整根成员一刀切）。以前不分单，于是任何一次
    //       下载完成都会把这笔记录读走：整合包还在下、用户又点了个模组，模组先下完就把
    //       记录吃了 —— 拿半截 .mrpack 去导入，而且整合包真下完时再没人接它。
    //       用 take() 而不是 value()+remove()：这一单**只活一次**，取走本身就是"用掉了"。
    const QString pendingModpack = m_pendingModpackImports.take(label);

    // ★ 2026-10-13（§7.1）：这一批是不是「更新中心」那一批？是的话收尾归那一边 —— 那边
    //   要说的是"**每个资源**的结果"（任务账本里那几步 + 一行一条的日志），而不是一句
    //   "下好了 / 没下下来"。所以这里**整个交出去**（不弹那个通用框：那边失败时会弹一份
    //   带着逐条结果的框，两个框叠着等于让用户白点一次）。
    if (!m_updateBatch.label.isEmpty() && m_updateBatch.label == label) {
        finishUpdateBatch(success, errorMessage);
        return;
    }

    if (!success) {
        appendLog(tr("✗ %1 没有下下来：%2").arg(label, errorMessage));
        InlineMessage::warning(m_ui->overlay(), tr("下载失败"),
                             tr("%1 有文件没能下下来。\n\n%2").arg(label, errorMessage));
        return;
    }

    appendLog(tr("✓ %1 下好了：%2").arg(label, errorMessage));
    if (!pendingModpack.isEmpty()) {
        // 不弹"下载完成"：整合包下回来只是第一步，紧接着那个导入框就是用户的下一步，
        // 中间夹一个"下好了"等于让他白点一次。
        //
        // NOTE: **排到下一轮**再走导入，不能在这里直接调。这个函数是在
        //       DownloadManager 的 allDownloadsFinished 信号栈里被调到的，而导入那条路
        //       马上就会 beginBatch() 开新的一批 —— 在别人的信号栈里开批次正是 2026-09-22
        //       那次崩溃的形状（取证见 build/tools/crashdump/，VersionManager 里那几处
        //       deferXxxStep 是同一个理由）。
        QTimer::singleShot(0, this, [this, pendingModpack] { importModpackFromFile(pendingModpack); });
        return;
    }
    InlineMessage::information(m_ui->overlay(), tr("下载完成"),
                             tr("%1 已经装进实例里了。\n\n%2").arg(label, errorMessage));
}

void MainWindow::updateInstallName() {
    QLineEdit *edit = m_ui->getInstallNameEdit();
    if (!m_installNameTouched) {
        // 用户还没自己改过名字：默认名跟着"勾中的加载器 + 它选的那个版本"走。
        // 挡信号是必须的 —— 不挡的话这一句会触发 textChanged，于是"程序填的名字"
        // 被当成"用户改过"，从此默认名再也不跟着加载器变了。
        const QSignalBlocker blocker(edit);
        edit->setText(defaultInstanceName());
    }
    updateInstallNameHint();
}

void MainWindow::updateInstallNameHint() {
    QLineEdit *edit = m_ui->getInstallNameEdit();
    const QString name = edit->text().trimmed();

    // 名字合不合法：判据在数据层（InstanceFiles::validateDirName()，和"重命名实例"、
    // "整合包导入"用的是同一套规矩）。这里只用它的结论，不抄一份规则过来。
    const QString invalid = InstanceFiles::validateDirName(name);
    const bool canInstall = invalid.isEmpty();

    // 上面那行**灰字**：说"装下去会怎样"。名字能不能用**都照说** —— 2026-09-23 起名字
    // 不合法的原因不再顶掉它，而是另起一行红字（用户原话："如果版本名字存在非法字符，
    // 不要替换掉那个「装好后在实例...」，而是在下面添加一行红色字"）。
    // 名字空着时这行没话可说（没有名字就没有目录），就让它空着。
    QString hint;
    if (!name.isEmpty()) {
        // 重名：**只提示、不拦**（用户原话："如果文件夹里有重名则在下方小字提示"）。
        // 同名就是同一个实例 —— 装下去不是新建一个，而是校验并补齐那个目录里的文件
        // （「重下 = 校验跳过」）。这句话必须说清这一点，否则用户会以为会多出一个。
        // NOTE: 名字不合法时**不查**重名 —— 那个名字当不了目录名，AppPaths::versionDir()
        //       拼出来的路径没有意义（`..` 那种会拼到数据目录外面去），查了也只是白查。
        const QString dir = canInstall ? AppPaths::versionDir(name) : QString();
        if (!dir.isEmpty() && QDir(dir).exists()) {
            // 「已经装过了」和「这个目录是别的东西」在用户眼里是两回事，分开说。
            const bool installed = QFile::exists(AppPaths::versionJsonPath(name));
            hint = installed ? tr("已经有叫「%1」的实例了：同一个名字就是同一个实例，再装一次"
                                  "会校验并补齐它的文件（已经下好的跳过）。")
                                   .arg(name)
                             : tr("%1 已经存在了，但它不像一个装好的实例。装下去会往这个目录"
                                  "里写文件。")
                                   .arg(dir);
        } else {
            hint = tr("装好之后在「实例」页里能看到它，目录是 versions/%1。").arg(name);
        }
    }
    QLabel *hintLabel = m_ui->getInstallNameHint();
    hintLabel->setText(hint);
    // 没话可说的时候连那一**行**一起收掉，别只把字清空。理由：这是 wordWrap 的标签，
    // 空着照样占一整行（实测 693x14），于是名字清空那一刻底下那行红字上面会凭空多出
    // 一个空行 —— 用户 2026-09-23 报的正是这个（"把名字弄空了以后上面那行消失了但是
    // 下面那一行空提示会换行怪怪的"）。空着的时候这一行没内容，收掉不影响别的。
    hintLabel->setVisible(!hint.isEmpty());

    // 下面那行**红字**：名字不能用的原因（就是 validateDirName() 那句话的字面意思）。
    // 能用的时候空着 + 藏起来 —— 藏起来（而不是留一个空行）是为了正常情况下名称框底下
    // 那几行的位置和以前一模一样（这一页上加载器树和安装按钮的位置都跟着它走）。
    QLabel *error = m_ui->getInstallNameError();
    error->setText(invalid);
    error->setVisible(!canInstall);
    // 「安装」按钮：名字不合法就点不动（省掉一次"点了才弹一句名字不对"的往返）。
    // 忙的时候也不给点（那是 setDownloadUiBusy 的活，这里只是别把它顶回来）。
    m_ui->getInstallButton()->setEnabled(canInstall && !m_downloadInFlight);
}

void MainWindow::onInstallNameEdited() {
    // 用户动过这个名字了：此后默认名不再跟着加载器变（不然他敲到一半的名字会被
    // 一次"勾了 Forge"顶掉）。
    m_installNameTouched = true;
    updateInstallNameHint();
}

// ---------------------------------------------------------------------------
// 安装页：实例预设（coding plan §7）
// ---------------------------------------------------------------------------

QTreeWidgetItem *MainWindow::installPresetRowItem() const {
    QTreeWidget *tree = m_ui->getLoaderTree();
    if (tree == nullptr) {
        return nullptr;
    }
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->data(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole).toInt() ==
            MainWindowUI::installPresetRowRole()) {
            return item;
        }
    }
    return nullptr;
}

void MainWindow::updateInstallPresetSummary() {
    QTreeWidgetItem *item = installPresetRowItem();
    if (item == nullptr) {
        return;
    }
    int selected = 0;
    for (auto it = m_installPresetChoices.constBegin();
         it != m_installPresetChoices.constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            ++selected;
        }
    }
    item->setData(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowSubTextRole(),
                  MainWindowUI::installPresetSummaryText(selected));
}

QList<QPair<QString, QString>> MainWindow::currentInstallPresetSelections() const {
    QList<QPair<QString, QString>> out;
    // 按四类的次序（不是 QMap 的字典序）：显示 / 应用 / 日志里那几句话的次序要一样。
    for (const PresetController::InstallCategory &category : PresetController::installCategories()) {
        const QString id = m_installPresetChoices.value(category.key);
        if (!id.isEmpty()) {
            out.append({category.key, id});
        }
    }
    return out;
}

void MainWindow::openInstallPresetPicker() {
    if (m_presetController == nullptr) {
        InlineMessage::warning(m_ui->overlay(), tr("预设不可用"),
                               tr("这一份启动器里预设那一层没有初始化，没法挑预设。"));
        return;
    }

    // 这一趟弹窗里改的东西**只落在这张本地草稿上**：取消 / 点「前往预设管理」都
    // 不写 `m_installPresetChoices`（§7.1 验收：取消不丢安装配置）。
    QMap<QString, QString> draft = m_installPresetChoices;

    const QString instanceName = m_ui->getInstallNameEdit()->text().trimmed();
    struct StatusText {
        QString status;
        QStringList reasons;
        bool problem = false;
    };
    // 一份预设"对着这个还没装出来的实例"会做什么 —— **只读**。状态行只说结论，
    // 原因（缺失 / 未知 / 跳过 / 覆盖）收在下面那块可展开的地方。
    const auto describe = [this, &instanceName](const QString &presetId) {
        StatusText out;
        if (presetId.isEmpty()) {
            return out;
        }
        if (instanceName.isEmpty()) {
            out.status = tr("先在上面填好版本名称。");
            out.problem = true;
            return out;
        }
        const PresetController::InstallPreview preview =
            m_presetController->previewForInstall(presetId, instanceName);
        if (!preview.problem.isEmpty()) {
            out.status = preview.problem;
            out.problem = true;
            return out;
        }
        if (!preview.blockedReason.isEmpty()) {
            out.status = preview.blockedReason;
            out.problem = true;
            return out;
        }
        if (!preview.ok) {
            out.status = tr("这一份算不出计划。");
            out.problem = true;
            return out;
        }
        QString text = tr("会动 %1 项").arg(preview.itemCount);
        if (preview.skippedCount > 0) {
            text += tr("，跳过 %1 项").arg(preview.skippedCount);
        }
        if (preview.blockedCount > 0) {
            text += tr("，%1 段做不了").arg(preview.blockedCount);
        }
        if (preview.needsConfirm) {
            text += tr("，有一段「覆盖」要先确认");
        }
        out.status = text;
        out.reasons = preview.reasons;
        // 「要做不了 / 要确认」才染红；"跳过几项"是正常计划的一部分。
        out.problem = preview.blockedCount > 0 || preview.needsConfirm;
        return out;
    };

    QList<PresetInstallPicker::Category> categories;
    // 「模组」那一类：状态行让给兼容检查（§7.2），计划那几行收进原因里。
    const QString modsCategoryKey = Presets::presetCategoryKey(Presets::PresetCategory::Mods);
    for (const PresetController::InstallCategory &one : PresetController::installCategories()) {
        PresetInstallPicker::Category category;
        category.key = one.key;
        category.title = one.title;
        for (const Presets::PresetSummary &preset :
             m_presetController->presetsInCategory(one.key)) {
            category.options.append({preset.id, preset.name, true});
        }
        category.selectedId = draft.value(one.key);
        // 选过、但盘上已经不在的那一份：**留在下拉框里**并标出来（§7.1「已删除/损坏的
        // 所选项明确提示，不静默换为另一项」）。名字也读不出来了，只能拿 id 认。
        if (!category.selectedId.isEmpty()) {
            bool found = false;
            for (const PresetInstallPicker::Option &option : category.options) {
                if (option.id == category.selectedId) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                category.options.append({category.selectedId, category.selectedId, false});
            }
            if (one.key == modsCategoryKey) {
                // 模组引用要联网查（本地/缓存那部分立刻就出来），先摆一句"正在检查"。
                category.status = tr("正在检查模组兼容性…");
                category.problem = false;
            } else {
                const StatusText status = describe(category.selectedId);
                category.status = status.status;
                category.reasons = status.reasons;
                category.problem = status.problem;
            }
        }
        categories.append(category);
    }

    // ★ 这一块**必须在堆上**：`InlineMessageBox::setContent()` 会 `setParent(box)`，
    //   弹窗析构时按 QObject 的老规矩把子控件一起 delete。栈对象被 delete 是未定义行为
    //   （2026-10-18 实测：mw_check 点到这一行时整个进程直接没了）。
    auto *picker = new PresetInstallPicker();
    const QString pickerLoaderName =
        VersionManager::loaderKindFromIndex(m_selectedLoaderIndex, nullptr)
            ? MainWindowUI::modLoaderOptionTexts().value(m_selectedLoaderIndex)
            : QString();
    picker->setContext(m_installGameVersion, pickerLoaderName);
    picker->setCategories(categories);

    InlineMessageBox box(MessageKind::Question, MainWindowUI::installPresetPickerTitle(),
                         QString(),
                         {MainWindowUI::installPresetConfirmText(),
                          MainWindowUI::installPresetCancelText()},
                         0, -1, this);
    box.setContent(picker);  // box 接管它（弹窗析构时一起 delete）
    box.setNote(tr("取消不会改动这里已经选好的预设；装好之后按这里选的那几类应用。"));

    // 兼容检查那几行：一条引用一行（匹配的不列，只说有问题的 + 暂无法确认的）。
    const auto modCompatReasons = [](const QList<Presets::CompatibilityResult> &results) {
        QStringList out;
        for (const Presets::CompatibilityResult &result : results) {
            if (result.state == Presets::CompatibilityState::Matched) {
                continue;
            }
            QString stateText;
            switch (result.state) {
            case Presets::CompatibilityState::Missing:
                stateText = tr("缺失");
                break;
            case Presets::CompatibilityState::Unknown:
                stateText = tr("暂无法确认");
                break;
            case Presets::CompatibilityState::Invalid:
                stateText = tr("引用无法识别");
                break;
            case Presets::CompatibilityState::Pending:
            case Presets::CompatibilityState::Checking:
                stateText = tr("检查中");
                break;
            case Presets::CompatibilityState::Matched:
                continue;
            }
            const QString name = result.name.isEmpty() ? result.projectId : result.name;
            QString line = QStringLiteral("%1（%2）").arg(name, stateText);
            if (!result.detail.isEmpty()) {
                line += QStringLiteral("：%1").arg(result.detail);
            }
            line += result.required ? tr("【必要项】") : tr("【可选项】");
            out.append(line);
        }
        return out;
    };

    // 兼容检查的每一发都只写到「模组」那一页 —— 且只有现在选的还是同一份时才写
    // （换了一份 / 收掉弹窗前那一刻，旧 generation 的快照不许盖掉新的）。
    QObject::connect(m_presetController, &PresetController::modCheckUpdated, &box,
                     [picker, modsCategoryKey, modCompatReasons](
                         const PresetController::ModCheckUpdate &update) {
                         if (update.presetId.isEmpty() ||
                             picker->selectedId(modsCategoryKey) != update.presetId) {
                             return;
                         }
                         const bool problem = update.summary.requiredMissing > 0 ||
                                              update.summary.invalid > 0;
                         picker->setStatus(modsCategoryKey, update.summary.text(),
                                           modCompatReasons(update.results), problem);
                     });
    // 弹窗一关就作废这一代（在途请求的回调按 generation 丢掉，不再擦已经关掉的那几行）。
    QObject::connect(&box, &QObject::destroyed, m_presetController,
                     [this]() { m_presetController->cancelModCheck(); });

    // 换一份就现算一次（还是只读）。算不出来也照实写在那一行上 —— 别把上一份的数留着。
    // ★ 模组那一类的状态行归兼容检查（§7.2），计划那几句收进「查看计划与原因」里。
    QObject::connect(
        picker, &PresetInstallPicker::selectionChanged, &box,
        [this, picker, &describe, modsCategoryKey, pickerLoaderName](const QString &key,
                                                                    const QString &presetId) {
            const StatusText status = describe(presetId);
            if (key != modsCategoryKey) {
                picker->setStatus(key, status.status, status.reasons, status.problem);
                return;
            }
            if (presetId.isEmpty()) {
                m_presetController->cancelModCheck();
                picker->setStatus(key, QString(), {}, false);
                return;
            }
            QStringList reasons = status.reasons;
            if (!status.status.isEmpty()) {
                reasons.prepend(status.status);
            }
            picker->setStatus(key, tr("正在检查模组兼容性…"), reasons, status.problem);
            m_presetController->startModCheck(presetId, m_installGameVersion, pickerLoaderName,
                                              true);
        });

    // 打开时已经选着的那一份：立刻开查（本地能判的那几条同步就出来，其余排队）。
    const QString initialModsPreset = draft.value(modsCategoryKey);
    if (!initialModsPreset.isEmpty()) {
        m_presetController->startModCheck(initialModsPreset, m_installGameVersion,
                                          pickerLoaderName, true);
    }

    // 「前往预设管理」：收掉这张弹窗，**把用户带过去**，安装上下文一个字都不动。
    bool goManage = false;
    QObject::connect(picker, &PresetInstallPicker::manageRequested, &box, [this, &goManage]() {
        goManage = true;
        // 走 dismiss()：提示那一档发 promptDismissed ⇒ execPrompt 的嵌套循环醒过来。
        // 直接 closeOverlay() 的话调用方要一直等下去（见 OverlayHost 那段）。
        m_ui->overlay()->dismiss();
    });

    m_ui->overlay()->execPrompt(&box);
    if (goManage) {
        // 安装页那一趟的状态全在内存里（版本 / 名字 / 已选的几类）—— 切过去再切回来
        // 还是原来那一份（§7.1「保留安装上下文并返回」）。
        m_ui->getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
        setInstanceMode(MainWindowUI::InstanceMode::Preset);
        return;
    }
    if (box.clickedIndex() != 0) {
        // 取消 / Esc / 点外面：草稿整个丢掉，界面上的摘要一个字都不变。
        return;
    }

    // 点「确定选择」：**整张表按弹窗里现在那几项重建**（不能只往里 insert —— 用户把
    // 某一类改回「不使用」时，旧的那一条必须消失）。
    m_installPresetChoices.clear();
    for (const QPair<QString, QString> &one : picker->selections()) {
        m_installPresetChoices.insert(one.first, one.second);
    }
    updateInstallPresetSummary();
}

void MainWindow::onInstallButtonClicked() {
    if (m_downloadInFlight) {
        appendLog(tr("已有下载任务正在进行，请等它结束。"));
        return;
    }
    const QString gameVersion = m_installGameVersion;
    if (gameVersion.isEmpty()) {
        // 正常走不到（安装页只能从树里一个具体版本进来）。真走到了就是界面状态错了，
        // 宁可不做 —— 猜一个版本装下去会装到用户没点过的东西上。
        InlineMessage::warning(m_ui->overlay(), tr("没有选中版本"),
                             tr("不知道要基于哪个原版安装，请回到版本列表重新挑一个。"));
        return;
    }

    const QString name = m_ui->getInstallNameEdit()->text().trimmed();
    const QString invalid = InstanceFiles::validateDirName(name);
    if (!invalid.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("名字不能用"), invalid);
        return;
    }

    const int loaderIndex = selectedLoaderIndex();
    VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
    const bool withLoader = VersionManager::loaderKindFromIndex(loaderIndex, &kind);

    // 已经下过就不给"直接重下"（几 GB 白跑一趟），改成校验并补齐缺失/损坏的文件 ——
    // 这是本启动器一贯的规矩。名字和原版号一样时问的是"这个版本"，不一样时问的是
    // "这个实例"，两句话都得像人说的。
    if (QFile::exists(AppPaths::versionJsonPath(name))) {
        const bool sameAsVanilla = name == gameVersion;
        const auto answer = InlineMessage::question(m_ui->overlay(), sameAsVanilla ? tr("版本已存在") : tr("实例已存在"),
            sameAsVanilla
                ? tr("版本 %1 已经下载过了。\n是否校验并修复缺失或损坏的文件？").arg(name)
                : tr("实例「%1」已经装过了。\n是否校验并修复缺失或损坏的文件？").arg(name), tr("是"), tr("否"), true);
        if (!answer) {
            appendLog(tr("已取消对 %1 的校验。").arg(name));
            return;
        }
        appendLog(tr("开始校验 %1 的文件（已存在的会跳过）...").arg(name));
    }

    // ---- 实例预设（coding plan §7.1）：带着走的那几份先把计划摊开 ----
    //
    // 这一步在**发请求之前**：用户看到的是"装好之后会改哪些文件、有没有覆盖"的精确计划，
    // 而不是装完了才发现某个段是「覆盖」。点了取消 ⇒ 整个安装也不开始（他取消的是这一次
    // "带预设的安装"，不是"不要装了"再点一次——但那两种在屏幕上分不开，宁可不做）。
    //
    // ★ pending 在这一刻定死：从点下安装到 versionDownloadFinished 之间用户还可能去改
    //   弹窗里的选择，而那一动不该改变这一趟已经发出去的安装（见 MainWindow.hpp 那段）。
    m_installPendingPresets.clear();
    m_installPendingPresetInstance.clear();
    m_installPresetFingerprints.clear();
    const QList<QPair<QString, QString>> presetSelections = currentInstallPresetSelections();
    if (!presetSelections.isEmpty()) {
        if (m_presetController == nullptr) {
            InlineMessage::warning(m_ui->overlay(), tr("预设不可用"),
                                   tr("这一份启动器里预设那一层没有初始化，没法应用预设。"));
            return;
        }
        // 四类里那几句标题（"游戏设置 / 模组 …"）只从 controller 拿，界面这边不抄一份。
        const QList<PresetController::InstallCategory> categories =
            PresetController::installCategories();

        QStringList summaryLines;
        QList<QPair<QString, bool>> pending;
        QMap<QString, QString> fingerprints;
        for (const QPair<QString, QString> &one : presetSelections) {
            const PresetController::InstallPreview preview =
                m_presetController->previewForInstall(one.second, name);
            if (!preview.ok || !preview.blockedReason.isEmpty() || !preview.problem.isEmpty()) {
                const QString why =
                    !preview.problem.isEmpty() ? preview.problem : preview.blockedReason;
                QString title;
                for (const PresetController::InstallCategory &category : categories) {
                    if (category.key == one.first) {
                        title = category.title;
                        break;
                    }
                }
                InlineMessage::warning(
                    m_ui->overlay(), tr("这份预设现在不能应用"),
                    tr("%1那一份「%2」：%3")
                        .arg(title, preview.presetName,
                             why.isEmpty() ? tr("这一份算不出计划。") : why));
                return;
            }
            // ★ 每一份自己那一道「覆盖」闸：装好之后按这一格决定要不要再问一次。
            pending.append({one.second, preview.needsConfirm});
            fingerprints.insert(one.second, preview.fingerprint);
            summaryLines.append(
                tr("· %1：%2").arg(preview.presetName, preview.summary));
        }
        InlineMessageBox box(
            MessageKind::Question, tr("安装完成后应用预设"),
            tr("装好「%1」之后会把这些预设应用上去：\n\n%2")
                .arg(name, summaryLines.join(QLatin1Char('\n'))),
            {tr("安装并应用"), tr("取消")}, 0, -1, this);
        box.setNote(tr("应用前会把要改写的文件先备份到预设目录下的 backups/ 里。"));
        m_ui->overlay()->execPrompt(&box);
        if (box.clickedIndex() != 0) {
            appendLog(tr("已取消这次安装（带预设的那一趟）。"));
            return;
        }
        m_installPendingPresets = pending;
        m_installPendingPresetInstance = name;
        m_installPresetFingerprints = fingerprints;
    }

    QString installerJava = m_javaSettings->javaPath();
    if (withLoader && !pickInstallerJava(gameVersion, loaderIndex, &installerJava)) {
        return;  // 里面已经弹过窗说清原因了，界面还没进忙态，直接退出就是干净的状态
    }

    // 忙要在点击这一刻就置上：版本 json / 加载器列表都是异步拉回来的，中间这段时间
    // 按钮还能点的话，用户会重复触发（和别处同一个理由）。
    //
    // NOTE: **顺序不能反**（m_installingInstanceId 必须在 setDownloadUiBusy(true) 之前）。
    //       setDownloadUiBusy() 内部会调 refreshInstalledVersions()，而那份"能启动的实例"
    //       列表是按 m_installingInstanceId 过滤出来的（installingVersionIds()）。反过来的
    //       话，置忙那一刻读到的还是**上一次**的值：对一个已经装好的实例点「安装」（校验并
    //       修复）时，它整段安装期间都留在可启动列表里 —— 用户点得动它，点下去却被
    //       onStartButtonClicked() 那道按版本的闸口弹一句"文件还没有下载完成"。
    //       那正是 MainWindow.hpp 写明不许出现的"列表里能选、点下去被拦"。
    //       （applyModpackImport() 那边本来就是"先记 id 再置忙"，这里对齐它。）
    m_installingInstanceId = name;
    setDownloadUiBusy(true);
    m_progressThrottle.invalidate();
    if (withLoader) {
        m_versionManager->installNamedLoader(name, gameVersion, kind, pinnedLoaderVersion(loaderIndex),
                                            installerJava);
    } else {
        m_versionManager->installNamedVanilla(name, gameVersion);
    }
}

bool MainWindow::pickInstallerJava(const QString &gameVersion, int loaderIndex, QString *out) {
    // 安装器自己就是个 Java 程序（Forge/NeoForge 是 `java -jar <安装器>.jar --installClient
    // <目录>`，OptiFine 那个是无界面入口 + 换 APPDATA，见 VersionManager::runInstaller），
    // 所以要的运行环境和"这个版本要的 Java"是同一件事：版本 json 里的 javaVersion。
    // Fabric 那条路只拉 json + 下 maven 库，一个进程都不起，所以这一段只管要跑安装器的那些。
    //
    // 拿不到版本 json（原版还没装）就不知道要求，此时原样用配的那份 —— 不在这一步拦人：装
    // 加载器本来就会把原版补齐，而"要哪个 Java"从那份 json 里读最准，猜一个主版本比不猜更
    // 危险（见启动那条路上"拿不到 version.json 就不能猜"那段注释）。
    // 参数收下标、在这里翻一次枚举（和界面别处同一处翻译，见 VersionManager 里那段注释）。
    // 翻不出来 = 没勾加载器 / 勾的是「不装」：一个外部安装器都不跑，直接放行。
    VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
    if (!VersionManager::loaderKindFromIndex(loaderIndex, &kind)) {
        return true;
    }
    if (kind == VersionManager::LoaderKind::Fabric) {
        return true;
    }
    const QString configuredJava = m_javaSettings->javaPath();
    QString installerJava = configuredJava;
    int requiredJavaMajor = 0;
    const QJsonObject gameJson = m_versionManager->getVersionJson(gameVersion);
    if (!gameJson.isEmpty()) {
        requiredJavaMajor = gameJson["javaVersion"].toObject()["majorVersion"].toInt();
    }
    QString javaReason;
    if (!m_javaSettings->installerJavaFor(requiredJavaMajor, configuredJava, &installerJava,
                                          &javaReason)) {
        appendLog(tr("这个版本要的 Java 拿不到：%1").arg(javaReason));
        InlineMessage::warning(m_ui->overlay(), tr("Java 版本不够"), javaReason);
        return false;
    }
    if (installerJava != configuredJava) {
        appendLog(tr("这个版本要 Java %1，装的时候改用 %2").arg(requiredJavaMajor).arg(installerJava));
    }
    *out = installerJava;
    return true;
}

// ======================================================================
// 「设置」选项卡
// ======================================================================
//
// 分工：MainWindowUI 只把控件搭出来（含"选项名称在左、控件在右"的两列布局），值和
// 持久化全在这一层。枚举 → 文案的映射也在这里 —— 界面层不该为了画一个下拉框去
// include src/settings，而 tr() 的文案本来就该和业务逻辑待在一起。
//
// 落盘时机逐项不同，这是这块最容易做错的地方，所以每一项都在自己的函数里说清楚：
//   Java / 内存 / 启动后 / 下载源 / 版本名格式 —— 改完立刻存
//   JVM 参数 —— 点「应用」才存（那是直接进命令行的东西，不能存半条）

/// 「同时最多下几个」那颗下拉框里每一行的文字（2026-10-13，§9.2）。
///
/// 档位表（6/12/24/48、上下限、越界怎么办）在 `DownloadConcurrency.hpp` —— 那里是唯一出处；
/// 这里只有**这一层的文字**（和枚举 → 文案那些一样，界面层不该为了画一个下拉框去 include
/// src/ 的东西）。默认那一档多两个字：「（默认）」标的是**出厂**那一档，不跟着用户当前选谁走
/// —— 它的用处是"我不知道该选哪个"，所以不写的话这一格就没有那个可以照抄的答案。
static QString downloadConcurrencyChoiceText(int value) {
    if (value == DownloadConcurrency::kDefault) {
        return QObject::tr("%1 个（默认）").arg(value);
    }
    return QObject::tr("%1 个").arg(value);
}

/// 「下载速度上限」那颗下拉框里每一行的文字（2026-10-18，§9.2）。
///
/// 档位表在 `DownloadSpeedLimit.hpp`（字节/秒，**唯一出处**）；这里只有这一层的文字。
/// 0 那一档写「不限」而不是「0 B/s」—— 用户读的是"限没限速"，不是那个数。
static QString downloadSpeedLimitChoiceText(qint64 bytesPerSecond) {
    if (bytesPerSecond <= 0) {
        return QObject::tr("不限");
    }
    if (bytesPerSecond >= 1024 * 1024) {
        return QObject::tr("%1 MB/s").arg(QLocale().toString(
            double(bytesPerSecond) / (1024.0 * 1024.0), 'g', 3));
    }
    return QObject::tr("%1 KB/s").arg(bytesPerSecond / 1024);
}

/// 「代理」那颗下拉框里每一行的文字（2026-10-18，§9.2）。
///
/// 三档的语义在 `DownloadProxy.hpp`（唯一出处）；这里只有这一层的文字。
static QString downloadProxyChoiceText(DownloadProxy::Mode mode) {
    switch (mode) {
    case DownloadProxy::Mode::Direct:
        return QObject::tr("直连（不走代理）");
    case DownloadProxy::Mode::Manual:
        return QObject::tr("手动指定");
    case DownloadProxy::Mode::System:
        break;
    }
    return QObject::tr("跟随系统");
}

void MainWindow::applySettingsToUi() {
    // 灌值时把信号全挡上：下面这些控件现在反映的是**盘上的值**，不是用户改的。
    // 不挡的话每条 currentIndexChanged/valueChanged 都会触发一遍"保存"，白写几次盘，
    // 更要紧的是那几条槽里还夹着真动作（改「版本名格式」会重刷实例列表）。
    const QSignalBlocker blockBehaviour(m_ui->getLauncherBehaviourCombo());
    const QSignalBlocker blockSource(m_ui->getDownloadSourceCombo());
    const QSignalBlocker blockConcurrency(m_ui->getDownloadConcurrencyCombo());
    const QSignalBlocker blockSpeedLimit(m_ui->getDownloadSpeedLimitCombo());
    const QSignalBlocker blockProxyMode(m_ui->getDownloadProxyCombo());
    const QSignalBlocker blockProxyManual(m_ui->getDownloadProxyManualEdit());
    const QSignalBlocker blockShowLog(m_ui->getShowGameLogCheck());
    const QSignalBlocker blockWarnDuplicate(m_ui->getWarnDuplicateLaunchCheck());
    const QSignalBlocker blockWarnWorldLoss(m_ui->getWarnWorldLossCheck());
    const QSignalBlocker blockDownloadInto(
        m_ui->getDownloadAutoIntoInstanceCheck());
    const QSignalBlocker blockWarnDeps(m_ui->getWarnMissingDependenciesCheck());
    const QSignalBlocker blockPriority(m_ui->getProcessPriorityCombo());
    const QSignalBlocker blockWindowWidth(m_ui->getGameWindowWidthSpin());
    const QSignalBlocker blockWindowHeight(m_ui->getGameWindowHeightSpin());
    const QSignalBlocker blockTitleText(m_ui->getTitleTextEdit());
    const QSignalBlocker blockCustom(m_ui->getVersionNameCustomCheck());
    const QSignalBlocker blockReal(m_ui->getVersionNameRealCheck());
    const QSignalBlocker blockLoader(m_ui->getVersionNameLoaderCheck());
    const QSignalBlocker blockLoaderVersion(m_ui->getVersionNameLoaderVersionCheck());

    // ---------- Java 版本 ----------
    // 这里只剩那行说明（「自动：上次用的是 …」/「手动指定：…」）。本机扫到的那些运行时
    // **不在这儿管**：扫描要逐个认版本，放到第一次切到设置页时再做（refreshJavaList()），
    // 结果进的是「Java 管理」那一页（见 openJavaManage）。
    updateJavaPathLabel();
    // 提醒总闸。挡信号：这是"把值画出来"，不挡的话 setChecked 会触发
    // onJavaWarnMismatchChanged() 又回头落一次盘。
    {
        const QSignalBlocker blockWarn(m_ui->getJavaWarnMismatchCheck());
        m_ui->getJavaWarnMismatchCheck()->setChecked(m_javaSettings->warnVersionMismatch());
    }

    // ---------- 内存 ----------
    // 交给 refreshMemoryUi()：开窗时把盘上那个数灌进条和数字框（★ 2026-10-18 起**不重算**
    // —— 自动档撤掉了）、把上限收到本机物理内存、写那句说明。两个控件的信号在那边自己挡
    // （这里的局部 blocker 就不再重复了）。
    refreshMemoryUi();

    // ---------- 启动后 ----------
    // 枚举值存在 itemData 里，文案是这里的 tr()。两边的对应关系只在这四行里。
    QComboBox *behaviourCombo = m_ui->getLauncherBehaviourCombo();
    behaviourCombo->addItem(tr("不变"), int(LauncherBehaviour::Unchanged));
    behaviourCombo->addItem(tr("最小化启动器"), int(LauncherBehaviour::Minimize));
    behaviourCombo->addItem(tr("收起启动器"), int(LauncherBehaviour::Close));
    behaviourCombo->addItem(tr("收起启动器，游戏退出后自动回来"),
                            int(LauncherBehaviour::CloseAndRestore));
    selectByEnumData(behaviourCombo, m_settings->launcherBehaviour());

    // ---------- JVM 参数 ----------
    m_ui->getExtraJvmArgsEdit()->setPlainText(m_javaSettings->extraJvmArgs());

    // ---------- 外观 ----------
    // 条上那行字有**两个**去处：设置页那一格、还有条自己。同一次调用里一起灌 ——
    // 分成两处写的话，以后改一处就会出现"框里是用户改过的名字、条上还是默认那五个字母"。
    // 两边喂的都是盘上那份（空串 = 没改过，条自己会退回 defaultBrandText()）。
    m_ui->getTitleTextEdit()->setText(m_settings->titleText());
    m_ui->titleBar()->setBrandText(m_settings->titleText());

    // ---------- 背景图 ----------
    // 那两行控件画的是 `Theme::current()` 里那两块图的样子（**不是** `m_settings` 里的
    // 东西：主题不归 LauncherSettings 管，它在同一个 QSettings 里另有一组键，见 Theme.hpp）。
    // 信号在 refreshThemeImageRows() 里自己挡（它和上面这一串 blocker 是两回事：
    // 那边连滑块带下拉框一起挡，理由写在那儿）。
    refreshThemeImageRows();

    // 「主题」那一节（配色 / 主题色 / 文字不透明度 / 语义色 / 动效开关）。和上面那一句一样，
    // 画的是 `Theme::current()` 和 `Anim::userEnabled()` 里的东西 —— 主题和动效都不归
    // LauncherSettings 管，各自另有一组键（见 Theme.hpp / Animations.hpp）。
    // NOTE: 这一句**必须**排在 `Theme::setSpec(Theme::load())` 之后（那一步在 src/main.cpp
    //       里、比 MainWindow 构造还早），不然画出来的是出厂那一份。
    refreshThemeControls();

    // ---------- 启动选项 ----------
    m_ui->getShowGameLogCheck()->setChecked(m_settings->showGameLog());
    // 两条启动提醒（上面那两个 blocker 已经挡上了）。这里**不能**顺手把值抄成
    // "界面 = 盘上"以外的任何默认：默认开这件事只有 LauncherSettings 一处出处。
    m_ui->getWarnDuplicateLaunchCheck()->setChecked(m_settings->warnDuplicateLaunch());
    m_ui->getWarnWorldLossCheck()->setChecked(m_settings->warnWorldLoss());
    // 优先级：枚举值存在 itemData 里，文案是这里的 tr()。五档一一对应 ProcessPriority，
    // 顺序也按枚举来（Low → High）—— 界面从上往下就是"越来越抢 CPU"。
    QComboBox *priorityCombo = m_ui->getProcessPriorityCombo();
    priorityCombo->addItem(tr("低（空闲时才算）"), int(ProcessPriority::Low));
    priorityCombo->addItem(tr("低于正常"), int(ProcessPriority::BelowNormal));
    priorityCombo->addItem(tr("正常（默认）"), int(ProcessPriority::Normal));
    priorityCombo->addItem(tr("高于正常"), int(ProcessPriority::AboveNormal));
    priorityCombo->addItem(tr("高"), int(ProcessPriority::High));
    selectByEnumData(priorityCombo, m_settings->processPriority());
    // 窗口模式：同样把枚举值存在 itemData 里。三档的顺序就是"窗口从花到满" ——
    // 和 ProcessPriority 那条一样，界面从上往下读就是越来越满。
    // NOTE: 文案里点明了各档"下面那个窗口大小还管不管用"，因为「最大化」那一档不吃它
    //       （原版没有最大化参数，是照屏幕工作区算的，见 GameWindowSettings.hpp）。
    QComboBox *windowModeCombo = m_ui->getGameWindowModeCombo();
    windowModeCombo->addItem(tr("小窗（按下面的窗口大小开一个窗口）"),
                             int(GameWindowMode::Windowed));
    windowModeCombo->addItem(tr("最大化（铺满屏幕，任务栏留出来）"),
                             int(GameWindowMode::Maximized));
    windowModeCombo->addItem(tr("全屏（原版自己的 --fullscreen）"), int(GameWindowMode::Fullscreen));
    selectByEnumData(windowModeCombo, m_settings->gameWindowMode());
    // 窗口大小：两个框各自 setValue（信号已经被上面那两个 blocker 挡上了）。
    m_ui->getGameWindowWidthSpin()->setValue(m_settings->gameWindowWidth());
    m_ui->getGameWindowHeightSpin()->setValue(m_settings->gameWindowHeight());
    // 灌完模式那一格要把"窗口大小"那一行按档位灰/亮一次 —— 上面那次 selectByEnumData()
    // 是有 blocker 的（走不到槽里），所以这一下不能省，否则盘上存着「最大化」时，
    // 下面两个框会亮着一个"其实不生效"的值。
    updateGameWindowSizeEnabled();

    // ---------- 下载源 ----------
    QComboBox *sourceCombo = m_ui->getDownloadSourceCombo();
    sourceCombo->addItem(tr("官方源（Mojang）"), int(DownloadSource::Official));
    sourceCombo->addItem(tr("镜像源（BMCLAPI）"), int(DownloadSource::Mirror));
    selectByEnumData(sourceCombo, m_settings->downloadSource());

    // ---------- 「同时最多下几个」（2026-10-13，§9.2） ----------
    // 四档那个表在 DownloadConcurrency.hpp（**唯一出处**）；这里的文字只能在这一层写
    // （ui/ 不许 include src/，所以档位值由这里灌成 itemData，和下载源、窗口模式同一形状）。
    QComboBox *concurrencyCombo = m_ui->getDownloadConcurrencyCombo();
    const QList<int> presets = DownloadConcurrency::presets();
    for (int value : presets) {
        concurrencyCombo->addItem(downloadConcurrencyChoiceText(value), value);
    }
    {
        const int concurrency = m_settings->downloadConcurrency();
        // 盘上是个**表外的、但落在合法区间里的**数（手改过配置）时补一格 —— 不补的话下拉框
        // 会指着一个"不是盘上那个值"的档，界面就在撒谎（而这一格正好是"用户以为自己调过"）。
        if (!presets.contains(concurrency)) {
            concurrencyCombo->addItem(tr("%1 个（自定义）").arg(concurrency), concurrency);
        }
        selectByEnumData(concurrencyCombo, concurrency);
    }

    // ---------- 「下载速度上限」（2026-10-18，§9.2） ----------
    // 和上面那格同一个形状。★ itemData 存的是 **int**（最大 1 GiB = 1073741824，装得下），
    // 读的时候再转回 qint64 —— 别在这里用 toLongLong 之后又 findData(int)，两边对不上
    // 就会"盘上有值、框里却停在第一项"。
    QComboBox *speedLimitCombo = m_ui->getDownloadSpeedLimitCombo();
    const QList<qint64> speedPresets = DownloadSpeedLimit::presets();
    for (qint64 value : speedPresets) {
        speedLimitCombo->addItem(downloadSpeedLimitChoiceText(value), int(value));
    }
    {
        const qint64 limit = m_settings->downloadSpeedLimit();
        // 手改过配置、落在合法区间里的表外值：补一格（理由和上面并发数那条一样）。
        if (!speedPresets.contains(limit)) {
            speedLimitCombo->addItem(
                tr("%1（自定义）").arg(downloadSpeedLimitChoiceText(limit)), int(limit));
        }
        selectByEnumData(speedLimitCombo, int(limit));
    }

    // ---------- 「代理」（2026-10-18，§9.2） ----------
    // 三档那个表在 DownloadProxy.hpp（**唯一出处**）；itemData 存 int(枚举)。
    // 手动地址那一格只在「手动」档露出来（藏起来时连位置都不占 —— 它是这一行里的第二块，
    // 露着的话另外两档会白白多出一格空白输入框）。
    QComboBox *proxyCombo = m_ui->getDownloadProxyCombo();
    for (DownloadProxy::Mode mode : DownloadProxy::modes()) {
        proxyCombo->addItem(downloadProxyChoiceText(mode), int(mode));
    }
    selectByEnumData(proxyCombo, int(m_settings->proxyMode()));
    {
        QLineEdit *manual = m_ui->getDownloadProxyManualEdit();
        manual->setText(m_settings->proxyManualText());
        manual->setVisible(m_settings->proxyMode() == DownloadProxy::Mode::Manual);
    }

    // CurseForge 的 API key（同一个「下载源」组里）。信号已经被上面那几个 blocker 挡上了。
    m_ui->getCurseForgeApiKeyEdit()->setText(m_settings->curseForgeApiKey());

    // 「下载新模组时直接放进当前实例」（2026-09-28）。默认值同样只有 LauncherSettings
    // 那一处出处（默认勾上）—— 这里不写任何默认，灌的就是盘上那份。
    m_ui->getDownloadAutoIntoInstanceCheck()->setChecked(m_settings->downloadAutoIntoInstance());
    // 「下载缺前置时提醒」（2026-10-09）。同一个做法：不在这儿写默认。
    m_ui->getWarnMissingDependenciesCheck()->setChecked(m_settings->warnMissingDependencies());

    // ---------- 版本名格式 ----------
    pushVersionNameOptionsToUi(m_settings->versionNameOptions());

    // 下载源得立刻交给发请求的那一层：构造函数里紧接着的
    // fetchAvailableVersions() 就会走它。
    syncDownloadSource();
    // 「同时最多下几个」同理：不等用户去设置页改，开机那一刻就按盘上那一档发请求。
    syncDownloadConcurrency();
    // 「下载速度上限」同理：开机那一刻就按盘上那一档压着（0 = 不限）。
    syncDownloadSpeedLimit();
    // 「代理」同理：**这是要在任何请求发出去之前装的** —— 别的几项只影响下载器，
    // 这一项是进程级的（版本清单、皮肤、资源搜索全走它）。
    syncDownloadProxy();
    // CurseForge 那两项同理（虽然要等到导入整合包才用得上）。
    syncCurseForgeSettings();
}

void MainWindow::pushVersionNameOptionsToUi(const VersionNameOptions &options) {
    m_launcherPreferences->pushVersionNameOptions(options);
}

void MainWindow::updateJavaPathLabel() {
    m_javaSettingsController->updatePathLabel();
}

void MainWindow::scanLocalJava() {
    m_javaSettingsController->refreshList();
}

void MainWindow::applyDetectedJavaVersions(const QMap<int, QString> &versions) {
    m_javaSettingsController->applyDetected(versions);
}

void MainWindow::refreshJavaList() {
    m_javaSettingsController->refreshList();
}

void MainWindow::onMainTabChanged() {
    updateBottomBar();
    if (m_ui->getTabWidget()->currentIndex() == MainWindowUI::settingsTabIndex()) {
        refreshJavaList();
        refreshMemoryUi();
    }
}

void MainWindow::openJavaManage() {
    runWhenOverlayFree([this] { openJavaManageNow(); });
}

void MainWindow::openJavaManageNow() {
    m_javaSettingsController->openManagePage();
}

void MainWindow::pushJavaRowsToPage() {
    // JavaSettingsController owns and refreshes the page now.
}

void MainWindow::onJavaUseAuto() {
    m_javaSettingsController->useAuto();
}

void MainWindow::onJavaUseRuntime(int major) {
    m_javaSettingsController->useRuntime(major);
}

void MainWindow::onJavaDetectRequested() {
    m_javaSettingsController->detect();
}

void MainWindow::onJavaDownloadRequested(int major) {
    m_javaSettingsController->download(major);
}

void MainWindow::onJavaDownloadAllMissing() {
    m_javaSettingsController->downloadAllMissing();
}

void MainWindow::refreshMemoryUi() {
    m_javaSettingsController->refreshMemory();
}

void MainWindow::applyMemoryLimits(qint64 totalMb) {
    m_javaSettingsController->applyMemoryLimits(totalMb);
}

bool MainWindow::clampXmsToXmx(int xmxMb) {
    return m_javaSettingsController->clampXmsToXmx(xmxMb);
}

void MainWindow::onJavaWarnMismatchChanged() {
    m_javaSettingsController->setWarnMismatchFromUi();
}

void MainWindow::onMemoryChanged() {
    m_javaSettingsController->commitMemoryFromSpin();
}

void MainWindow::onMemorySliderMoved() {
    m_javaSettingsController->syncMemorySlider();
}
void MainWindow::onLauncherBehaviourChanged() {
    m_launcherPreferences->launcherBehaviourChanged();
}

void MainWindow::onDownloadSourceChanged() {
    m_launcherPreferences->downloadSourceChanged();
}

void MainWindow::onDownloadConcurrencyChanged() {
    m_launcherPreferences->downloadConcurrencyChanged();
}

void MainWindow::onDownloadSpeedLimitChanged() {
    m_launcherPreferences->downloadSpeedLimitChanged();
}

void MainWindow::onDownloadProxyModeChanged() {
    m_launcherPreferences->downloadProxyModeChanged();
}

void MainWindow::onDownloadProxyManualChanged() {
    m_launcherPreferences->downloadProxyManualChanged();
}

void MainWindow::onDownloadAutoIntoInstanceChanged() {
    m_launcherPreferences->downloadAutoIntoInstanceChanged();
}

void MainWindow::onWarnMissingDependenciesChanged() {
    m_launcherPreferences->warnMissingDependenciesChanged();
}

void MainWindow::onShowGameLogChanged() {
    m_launcherPreferences->showGameLogChanged();
}

void MainWindow::onWarnDuplicateLaunchChanged() {
    m_launcherPreferences->warnDuplicateLaunchChanged();
}

void MainWindow::onWarnWorldLossChanged() {
    m_launcherPreferences->warnWorldLossChanged();
}

void MainWindow::onProcessPriorityChanged() {
    m_launcherPreferences->processPriorityChanged();
}

void MainWindow::onGameWindowSizeChanged() {
    m_launcherPreferences->gameWindowSizeChanged();
}

void MainWindow::onGameWindowModeChanged() {
    m_launcherPreferences->gameWindowModeChanged();
}

void MainWindow::updateGameWindowSizeEnabled() {
    m_launcherPreferences->updateGameWindowSizeEnabled();
}
// ---------------------------------------------------------------------------
// 「背景图」那一节（设置页「个性化」）
// ---------------------------------------------------------------------------
// 用户 2026-09-28 原话：「能给主页左侧自定义图像吗？其他页面的背景能换图吗」。
//
// 这一节动的是**主题规格**（`ui/Theme.hpp`），不是 LauncherSettings —— 主题另有一组
// QSettings 键（`Theme/…`），落盘/读盘都走 `Theme::save()` / `Theme::load()`，而启动时
// 铺开那一次在 `src/main.cpp` 里。所以这边改完的顺序是固定的三步：
// **改进内存里那份规格 → 落盘 → `Theme::apply()` 立刻铺开**（少最后一步就是"改了没生效，
// 下次启动才出现"，那是这一节最要命的失败模式）。

void MainWindow::onThemeImageChooseClicked(int index) {
    m_themeSettingsController->chooseImage(index);
}

void MainWindow::onThemeImageClearClicked(int index) {
    m_themeSettingsController->clearImage(index);
}

void MainWindow::onThemeImageFitChanged(int index) {
    m_themeSettingsController->changeImageFit(index);
}

void MainWindow::onThemeImageOpacityChanged(int index) {
    m_themeSettingsController->changeImageOpacity(index);
}

void MainWindow::setThemePartImage(const QString &partKey, const QString &imagePath,
                                   const QString &imageName, int fit, int opacity) {
    m_themeSettingsController->setPartImage(partKey, imagePath, imageName, fit, opacity);
}

void MainWindow::refreshThemeImageRows() {
    m_themeSettingsController->refresh();
}

void MainWindow::applyThemeSpec(const Theme::Spec &spec) {
    m_themeSettingsController->applySpec(spec);
}

void MainWindow::refreshThemeControls() {
    m_themeSettingsController->refresh();
}

void MainWindow::onThemePresetChanged() {
    m_themeSettingsController->changePreset();
}

void MainWindow::onThemeAccentClicked() {
    m_themeSettingsController->chooseAccent();
}

void MainWindow::onThemeAccentResetClicked() {
    m_themeSettingsController->resetAccent();
}

void MainWindow::onThemeSemanticClicked(int index) {
    m_themeSettingsController->chooseSemantic(index);
}

void MainWindow::onThemeSemanticResetClicked() {
    m_themeSettingsController->resetSemantic();
}

void MainWindow::onThemeResetAllClicked() {
    m_themeSettingsController->resetAll();
}

void MainWindow::onThemeExportClicked() {
    m_themeSettingsController->exportTheme();
}

void MainWindow::onThemeImportClicked() {
    m_themeSettingsController->importTheme();
}

void MainWindow::onThemeTextOpacityChanged(int value) {
    m_themeSettingsController->changeTextOpacity(value);
}

void MainWindow::onAnimationsToggled(bool enabled) {
    m_themeSettingsController->setAnimationsEnabled(enabled);
}

void MainWindow::onTitleTextChanged() {
    m_themeSettingsController->updateTitleText();
}
void MainWindow::syncDownloadSource() {
    m_launcherPreferences->syncDownloadSource();
}

void MainWindow::syncDownloadConcurrency() {
    m_launcherPreferences->syncDownloadConcurrency();
}

void MainWindow::syncDownloadSpeedLimit() {
    m_launcherPreferences->syncDownloadSpeedLimit();
}

void MainWindow::syncDownloadProxy() {
    m_launcherPreferences->syncDownloadProxy();
}

void MainWindow::syncCurseForgeSettings() {
    m_launcherPreferences->syncCatalogCredentials();
}

void MainWindow::onCurseForgeApiKeyEdited() {
    m_launcherPreferences->curseForgeApiKeyEdited();
    // 兼容检查那条路读的是同一份设置（用户改完立刻生效，不用重启）。
    if (m_presetController != nullptr) {
        m_presetController->setCurseForgeApiKey(m_settings->curseForgeApiKey());
    }
}

void MainWindow::onVersionNameOptionChanged() {
    m_launcherPreferences->versionNameOptionChanged();
}

void MainWindow::onApplyExtraJvmArgsClicked() {
    m_launcherPreferences->applyExtraJvmArgs();
}
void MainWindow::applyLauncherBehaviourAfterLaunch() {
    switch (m_settings->launcherBehaviour()) {
    case LauncherBehaviour::Minimize:
        showMinimized();
        break;
    case LauncherBehaviour::Close:
        // hide() 而不是 close()：close() 是"关窗口"，启动器是最后一个窗口时那等于
        // 退出进程，而游戏是它的子进程 —— 用户以为只是把启动器收起来，游戏跟着没了。
        hide();
        break;
    case LauncherBehaviour::CloseAndRestore:
        m_gameSessionController->noteHiddenForGame();
        hide();
        break;
    case LauncherBehaviour::Unchanged:
        break;
    }
}

void MainWindow::onVersionsFetched(const QStringList &versions) {
    refreshAvailableVersions();
    // 下载页那五页的「版本」筛子也灌一遍：它的选项就是这份在线清单（用户要"按版本搜"，
    // 而"有哪些版本"这件事只有这一份是一手来源）。放在**失败也照灌**的位置 —— 空清单时
    // 灌出来是空框，而空框仍然能自己打字（这一格本来就是可输入的）。
    fillDownloadFilterVersionOptions();

    // 顺带把已安装的也刷一遍：一次网络往返之后两个下拉框都是最新的，这条不变式
    // 很便宜（就是一次 QDir 扫描），但能兜住"清单在拉的这段时间用户往
    // .minecraft/versions 里拷了个版本目录"这种情况。
    refreshInstalledVersions();

    if (versions.isEmpty()) {
        // 拉清单失败时 DownloadManager 也会发这个信号，发的是**上一次成功的结果**
        // （首次拉取就失败才是空的），所以这一支实际拦的是"从来没拉到过"。别让用户
        // 对着一棵永远空的树等下去 —— 日志里说清楚，树上那四条照样在（它们本来就是空的）。
        appendLog(tr("在线版本列表获取失败（网络异常？）。"));
        return;
    }
    appendLog(tr("已更新在线版本列表 (%1 个版本)。").arg(versions.size()));
}

QString MainWindow::launchIdentityKey() const {
    return m_launchController->launchIdentityKey();
}

bool MainWindow::confirmLaunchWhileRunning(const QString &versionId) {
    return m_launchController->confirmLaunchWhileRunning(versionId);
}

QStringList MainWindow::launchArguments(const QString &versionId, const QJsonObject &versionJson,
                                        const EffectiveJavaSettings &javaSettings,
                                        const QString &token, bool offlineMode,
                                        const QuickPlay::Target &quickPlay) const {
    return m_launchController->launchArguments(versionId, versionJson, javaSettings, token,
                                               offlineMode, quickPlay);
}

void MainWindow::onStartButtonClicked() {
    m_launchController->onStartButtonClicked();
}

void MainWindow::appendLaunchRecord(const QString &versionId, const QuickPlay::Target &quickPlay,
                                    qint64 startedAtMs) {
    m_launchController->appendLaunchRecord(versionId, quickPlay, startedAtMs);
}

void MainWindow::updateHomeRecent() {
    if (m_ui == nullptr) {
        return;  // 构造还没走到那儿（和那几个访问器拿空指针当判据是同一个意思）
    }
    QComboBox *combo = m_ui->getInstalledVersionCombo();
    const QString currentId = combo != nullptr ? combo->currentData().toString() : QString();
    QList<HomeQuickController::Instance> installed;
    if (combo != nullptr) {
        installed.reserve(combo->count());
        for (int row = 0; row < combo->count(); ++row) {
            HomeQuickController::Instance instance;
            instance.id = combo->itemData(row).toString();
            if (instance.id.isEmpty()) {
                continue;
            }
            instance.name = combo->itemText(row);
            if (m_versionManager != nullptr) {
                const VersionDisplayParts parts = m_versionManager->displayParts(instance.id);
                QString loaderVersion;
                const QString loader = instanceLoaderName(parts, m_versionManager, &loaderVersion);
                instance.version = QStringLiteral("Minecraft %1 · %2")
                    .arg(parts.realVersion.isEmpty() ? parts.id : parts.realVersion,
                         loader.isEmpty() ? QStringLiteral("原版")
                                          : (loader + QLatin1Char(' ') + loaderVersion).trimmed());
            }
            instance.icon = combo->itemIcon(row);
            if (instance.icon.isNull() && m_versionManager != nullptr) {
                instance.icon = QIcon(instanceRowMark(
                    m_versionManager->displayParts(instance.id), m_versionManager, 32));
            }
            instance.favorite = m_groupStore != nullptr && m_groupStore->isPinned(instance.id);
            installed.append(instance);
        }
    }
    const HomeQuickController::Rows rows = HomeQuickController::build(
        installed, m_launchHistory->records(), currentId, m_settings->homeFavoriteCount(),
        m_settings->homeRecentCount());
    m_ui->setHomeQuickRows(rows.current, rows.favorites, rows.recent);
}

void MainWindow::openHomeInstancePicker() {
    if (m_ui == nullptr || m_folderManager == nullptr || m_versionManager == nullptr) {
        return;
    }

    const QString currentFolder = m_folderManager->currentFolder();
    QList<MainWindowUI::HomePickerFolder> folders;
    QList<MainWindowUI::HomePickerInstance> instances;

    QSet<QString> recentIds;
    for (const LaunchHistory::Record &record : m_launchHistory->records()) {
        if (record.isValid() && record.result != LaunchHistory::Result::StartFailed) {
            recentIds.insert(record.instanceId);
        }
    }

    const auto loaderFromId = [](const QString &id) {
        const QString lower = id.toLower();
        // NeoForge 必须排在 Forge 前面，否则名字里那段 "forge" 会先误命中。
        if (lower.contains(QStringLiteral("neoforge"))) {
            return QStringLiteral("neoforge");
        }
        if (lower.contains(QStringLiteral("fabric"))) {
            return QStringLiteral("fabric");
        }
        if (lower.contains(QStringLiteral("optifine"))) {
            return QStringLiteral("optifine");
        }
        if (lower.contains(QStringLiteral("forge"))) {
            return QStringLiteral("forge");
        }
        return QString();
    };

    for (const QString &folderPath : m_folderManager->folders()) {
        MainWindowUI::HomePickerFolder folder;
        folder.path = folderPath;
        folder.title = folderTitle(folderPath);
        folder.subtitle = QDir::toNativeSeparators(folderPath);
        folder.current = (folderPath == currentFolder);
        folders.append(folder);

        if (!QFileInfo(folderPath).isDir()) {
            continue;
        }
        if (folder.current) {
            for (const VersionDisplayParts &part : m_instancePageParts) {
                MainWindowUI::HomePickerInstance row;
                row.folderPath = folderPath;
                row.id = part.id;
                row.title = formatVersionName(part, m_settings->versionNameOptions());
                const QString loader = instanceLoaderName(part, m_versionManager);
                row.loaderKey = loaderGroupKey(loader);
                row.searchKeywords = QStringList{part.customName, part.realVersion, part.loaderName,
                                                 part.loaderVersion, folder.title}.join(QLatin1Char(' '));
                row.subtitle = loader.isEmpty()
                                   ? (part.realVersion.isEmpty() ? QStringLiteral("原版")
                                                                 : part.realVersion)
                                   : QStringLiteral("%1 · %2")
                                         .arg(part.realVersion.isEmpty() ? part.id
                                                                        : part.realVersion,
                                              loader);
                row.icon = m_instancePageMarks.value(part.id);
                row.favorite = m_groupStore != nullptr
                               && m_groupStore->isPinned(instanceKey(part.id));
                row.recent = recentIds.contains(part.id);
                instances.append(row);
            }
            continue;
        }

        // 非当前文件夹不能借用 VersionManager（它绑定着当前 AppPaths），这里只做一次轻量目录
        // 枚举；真正选中后 FolderManager 会切根并由 refreshInstalledVersions() 完整校验。
        QDir versionsDir(QDir(folderPath).filePath(QStringLiteral("versions")));
        const QFileInfoList entries = versionsDir.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &entry : entries) {
            const QString id = entry.fileName();
            const QDir versionDir(entry.absoluteFilePath());
            if (!QFileInfo::exists(versionDir.filePath(id + QStringLiteral(".json")))
                && !QFileInfo::exists(versionDir.filePath(id + QStringLiteral(".jar")))) {
                continue;
            }
            MainWindowUI::HomePickerInstance row;
            row.folderPath = folderPath;
            row.id = id;
            row.title = id;
            row.loaderKey = loaderFromId(id);
            row.searchKeywords = folder.title;
            row.subtitle = row.loaderKey.isEmpty() ? QStringLiteral("原版") : row.loaderKey;
            row.icon = QIcon(VersionIcons::grassBlock(MainWindowUI::instanceTreeIconSize()));
            instances.append(row);
        }
    }

    m_ui->openHomeInstancePicker(
        folders, instances,
        [this](const QString &folderPath) {
            if (m_folderManager != nullptr) {
                m_folderManager->setCurrentFolder(folderPath);
            }
        },
        [this](const QString &folderPath, const QString &instanceId) {
            if (m_folderManager == nullptr || m_ui == nullptr) {
                return;
            }
            if (folderPath != m_folderManager->currentFolder()
                && !m_folderManager->setCurrentFolder(folderPath)) {
                return;
            }
            QComboBox *combo = m_ui->getInstalledVersionCombo();
            const int row = combo == nullptr ? -1 : combo->findData(instanceId);
            if (row >= 0) {
                combo->setCurrentIndex(row);
                // 选中实例后回到“当前”，让收起列表后的启动区立刻说清将启动谁。
                m_ui->setHomeQuickMode(MainWindowUI::HomeQuickMode::Current);
            }
        });
}

// ============================ 「账号与外观」那个模块 ============================
//
// 三件事在这个模块里的分工（写清楚免得下次改错地方）：
//   - **界面的样子**全在 ui/AccountSkinDialog.{hpp,cpp}（它不 include 任何 src/ 下的头）；
//   - **盘上的账**在 src/auth/AccountStore（多账号列表）+ TokenStore（凭据字节格式）；
//   - **下面这一片**是接线：谁来读谁、失败怎么报、哪个动作之后要刷新什么。
// 所以这里没有任何"界面布局"的代码，那边也没有任何"读注册表"的代码。

void MainWindow::onAccountSkinButtonClicked() {
    // ★ 2026-10-18（coding plan §1.4 那两处之一）：这一颗原来在"覆盖层已经开着"时**直接
    //   return** —— 屏幕上什么都不发生。典型场面：用户在别的选项卡上逛，实例设置那一页
    //   还收着占住那一层；这时按「账号与外观」看着就是"按钮点坏了"。现在并进同一套规矩：
    //   **静默收掉占着的那一层，再开**（让位那一段的唯一出处是 `runWhenOverlayFree()`）。
    runWhenOverlayFree([this] {
        setupAccountSkinDialog();
        OverlayHost *overlay = m_ui->overlay();
        if (overlay == nullptr) {
            return;  // 没有覆盖层就开不了（主界面建的时候就有，正常到不了这一支）
        }
        // 从"另开一个窗"改成"把内容区整块盖掉"（2026-09-25 用户给的方向：最终不出现独立窗口）。
        // 原来是**非模态** show()，理由是登录那一步会把浏览器推到前台、用户要在两边来回切；
        // 嵌进内容区之后这一点照旧成立 —— execPage 内部那个嵌套事件循环一直在转，界面是活的，
        // 浏览器本来也是外面的独立窗，谁都拦不着它。
        //
        // NOTE: 顺序照旧是**先摆出来、再刷新**。refreshAccountSkin() 最后那一段会去拉披风列表
        //       （真联网，最长能等十几秒），倒过来做的话头一次点这个入口就是"什么也没发生"
        //       —— 页面还没画出来，那个请求已经在跑了，用户只会以为按钮点坏了。
        //       这里排一发队：execPage 一进它自己的嵌套循环就先跑这一发，那时页面已经在屏幕上了。
        QMetaObject::invokeMethod(this, [this] { refreshAccountSkin(); }, Qt::QueuedConnection);
        // 那一发刷新排在 execPage 的嵌套循环里才跑；先把「皮肤」那一行摆对，免得开页的第一帧
        // 露的是正版才有的「上传皮肤…」（离线账号按下去只会失败）。
        m_accountSkinDialog->setOfflineSkinState(offlineIdentity(),
                                                 OfflineSkin::hasSkin(AccountStore::activeUuid()));
        overlay->execPage(m_accountSkinDialog, AccountSkinDialog::windowTitle());
    });
}

void MainWindow::setupAccountSkinDialog() {
    if (m_accountSkinDialog != nullptr) {
        return;
    }
    m_accountSkinDialog = new AccountSkinDialog(this);
    // NOTE: 这个窗只发"用户想做什么"，一件事都不自己干 —— 收到信号之后干什么全在下面
    //       这几个槽里。所以它能在 ui_check 里被单独构造、单独驱动（不登录、不联网）。
    connect(m_accountSkinDialog, &AccountSkinDialog::loginRequested, this,
            &MainWindow::onAccountLoginRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::offlineAccountRequested, this,
            &MainWindow::onOfflineAccountRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::switchRequested, this,
            &MainWindow::onAccountSwitchRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::removeRequested, this,
            &MainWindow::onAccountRemoveRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::refreshRequested, this,
            [this] { refreshAccountSkin(); });
    connect(m_accountSkinDialog, &AccountSkinDialog::skinUploadRequested, this,
            &MainWindow::onSkinUploadRequested);
    // 离线账号那一档（「换一张…」/「恢复默认外观」）：改的是本机那份，不是官方档案。
    connect(m_accountSkinDialog, &AccountSkinDialog::offlineSkinPickRequested, this,
            &MainWindow::onOfflineSkinPickRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::offlineSkinResetRequested, this,
            &MainWindow::onOfflineSkinResetRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::capeSelectRequested, this,
            &MainWindow::onCapeSelectRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::hideCapeRequested, this,
            &MainWindow::onCapeHideRequested);
}

bool MainWindow::offlineIdentity() const {
    // ★ 2026-10-15：判据（连那段"为什么读盘不读内存副本"的道理）整个搬进
    //   `PlayerSkinController::offlineIdentity()` 了 —— 外观那一套要按它决定"这一趟能不能
    //   联网"，而这一层还有十几处读它，所以留一句转手。**唯一出处仍然是那一个静态函数**：
    //   谁要判"现在是不是离线身份"都该走它，不要在这儿另写一份。
    return PlayerSkinController::offlineIdentity();
}

QString MainWindow::accountSkinStatusText() const {
    // 当前账号是离线账号时先说这一句：名字就在上面那一列里躺着，比任何泛泛的说法都具体。
    //
    // NOTE: 2026-09-24 之前这里有两档"离线" —— 这一档，和"账号是正版的、但主页那个
    //       「离线模式」勾着"那一档。勾选框去掉之后只剩这一档了：**离线 = 当前账号本身
    //       是离线账号**，没有别的意思。
    const QString active = AccountStore::activeUuid();
    if (AccountStore::isOffline(active)) {
        return AccountSkinDialog::offlineIdentityText(AccountStore::nameOf(active));
    }
    if (!m_authManager->loggedIn()) {
        return AccountSkinDialog::notLoggedInNote();
    }
    return AccountSkinDialog::identityText(m_authManager->playerName());
}

void MainWindow::refreshAccountSkin() {
    // NOTE: **这一句只挡窗里那些东西**（账号列表 / 状态行 / 披风），预览那一份照刷 ——
    //       2026-09-24 用户报的 bug 就是它挡多了：「现在从未登录切换到选择离线账号不会随机
    //       给皮肤了」。主页那个小人、底栏头像、那个窗里的 3D 预览**都只在
    //       startPlayerSkinLoad() → applyPlayerSkin() 里更新**，所以原来这句 return 一挡，
    //       没点开过「账号与外观…」的会话里切账号根本不换脸，一直停在上一个人的脸上
    //       （从"未登录"切过去时停的就是那张兜底占位图）。
    //
    //       以前没人发现是因为切账号只能从「账号与外观…」里点 —— 那个窗开着，这句 return
    //       进不去；09-23 把切换搬到右面板之后，这条路才第一次被走到。
    if (m_accountSkinDialog == nullptr) {
        startPlayerSkinLoad();  // 窗没建 → 没有列表/状态行/披风要刷，但脸得换
        return;
    }

    // 1) 账号列表：**纯本地**（读 SavedAccounts 那个键），不联网、不碰令牌。
    QList<AccountSkinDialog::AccountRow> rows;
    const QList<SavedAccount> accounts = AccountStore::accounts();
    rows.reserve(accounts.size());
    for (const SavedAccount &account : accounts) {
        AccountSkinDialog::AccountRow row;
        row.uuid = account.uuid;
        row.name = account.name;
        row.savedAt = account.savedAt;
        row.current = account.isCurrent;
        row.offline = account.offline;
        rows.append(row);
    }
    m_accountSkinDialog->setAccounts(rows);

    // 2) 状态行回到"当前身份"那一档，忙态也在这里松开 —— 能走到这一句就说明上一步那个
    //    同步请求已经回来了（下面第 4 步会再离开这个方法去联网）。
    m_accountSkinDialog->setBusy(false);
    m_accountSkinDialog->setStatusText(accountSkinStatusText());
    // 「皮肤」那一行露哪一档（正版：上传；离线：换一张 / 恢复默认）。判据和下面那句
    // `if (offlineIdentity())` 同一个出处，只是这里要的是"有没有自己挑过"那一格。
    {
        const QString activeUuid = AccountStore::activeUuid();
        m_accountSkinDialog->setOfflineSkinState(
            offlineIdentity(), OfflineSkin::hasSkin(activeUuid));
    }

    // 3) 3D 预览走**和主页同一条路**（同一份缓存、同一次后台刷新）：applyPlayerSkin()
    //    会把同一份贴图同时喂给主页那个小人和这个窗里的预览。
    //
    //    NOTE: 这一句**不保证不联网**（2026-09-24 之前这里是那么写的，现在不成立了）：
    //          离线账号第一次进来时它要 MineSkin 那一页随机皮肤（见 startPlayerSkinLoad()
    //          里 offlineIdentity() 那一档）。挑过一次之后就只在盘上读那一行。
    //          这也正是这个窗**只在离线账号那一档开**的原因（见 onAccountSkinButtonClicked()
    //          里那段）：正版那一档开窗会顺带拉披风，那才是真联网的那一下。
    startPlayerSkinLoad();

    // 4) 披风列表要令牌，是这一整块里唯一会真的去联网的地方（过期令牌也是在这里顺带刷新的）。
    if (offlineIdentity()) {
        // 当前是离线账号 = "别联网"，而拉披风列表正是联网。跳过。
        m_accountSkinDialog->setCapes({});
        return;
    }
    // ★ 2026-09-27：这一页**现在不在屏幕上**的时候，这一整段（下面那两发同步联网）跳过。
    //
    // 为什么非得另立一条判据：上面那句 `m_accountSkinDialog == nullptr` 分不出"关掉了" ——
    // 这个对话框是**建一次就留着**的（setupAccountSkinDialog() 见非空就返回，父对象是本
    // 窗口），页面关掉只是把它藏起来，指针永远不为空。于是"开过一次账号页、之后从右面板
    // 切账号"这一条路上，每一次切换都会在**没有人看着的那个页面上**点起忙态、然后同步
    // 等一次网络往返（loadSavedToken 还可能真去换一次令牌）：用户看到的是"切个账号整个
    // 启动器卡几秒"，而卡住的理由（「正在获取披风列表…」）摆在一个藏起来的页面里。
    //
    // NOTE: 判据是"**现在真摆在眼前**"（isVisible），和 OverlayHost 里 retargetCoveredPage()
    //       那条 NOTE 同一条规矩：页面收在别的选项卡上时它不在屏幕上，一样跳过。
    // NOTE: 上半截（账号列表 / 状态行 / 3D 预览）**照样刷了** —— 那些不联网。披风列表会在
    //       用户真的打开这一页时补上：开页那条路必走一次本函数（见 onAccountSkinButtonClicked），
    //       而那时页面已经在屏幕上了。
    if (!m_accountSkinDialog->isVisible()) {
        return;
    }
    // 下面这两下是**同步**的（2026-09-25 补忙态）：loadSavedToken() 可能要真去换一次令牌、
    // fetchOwnedCapes() 一定发一次 GET —— 两个函数都是"调完才返回"，于是点「刷新」之后的
    // 这几秒钟里 UI 线程一直在这两行里面出不来：窗不重绘、什么都按不动，Windows 过一会儿
    // 还会给标题挂上"(无响应)"。
    //
    // NOTE: 上面 onAccountSkinButtonClicked() 那段写着"先 show 出来，那一趟就变成『窗开着、
    //       状态行写着在忙』"—— 那句在改动前是**不成立**的：第 2 步的 setBusy(false)
    //       之后到这里谁也没把忙态点起来，用户看到的是一个画着旧内容、点什么都没反应的窗。
    //       忙态 + 一次 processEvents（排掉用户输入）和 enterSyncWait() 是同一个手法：
    //       进去之前先把话说出来。下面每条返回路上都要松开它。
    m_accountSkinDialog->setBusy(true, tr("正在获取披风列表…"));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QString error;
    const QString token = m_authManager->loadSavedToken(&error);
    if (token.isEmpty()) {
        // 没登录（或者令牌换不出来）。原因写进状态行 —— 它比"当前身份"那句更要紧，
        // 而"当前是哪个账号"在列表里那个（当前）标记上照样看得到。
        m_accountSkinDialog->setCapes({});
        if (!error.isEmpty()) {
            m_accountSkinDialog->setStatusText(error);
        }
        m_accountSkinDialog->setBusy(false);
        return;
    }

    QList<OwnedCape> capes;
    if (!m_skinApi->fetchOwnedCapes(token, &capes, &error)) {
        // 背景刷新失败**不弹窗**（这是这个窗里唯一一条不弹窗的错误路）：账号列表和预览
        // 都已经正常出来了，弹一个窗拦在中间会让用户以为整个模块坏了。正文写在状态行里，
        // 日志留一行备查。
        appendLog(tr("拉取披风列表失败：%1").arg(error));
        m_accountSkinDialog->setCapes({});
        m_accountSkinDialog->setStatusText(error);
        m_accountSkinDialog->setBusy(false);
        return;
    }

    QList<AccountSkinDialog::CapeRow> capeRows;
    capeRows.reserve(capes.size());
    for (const OwnedCape &cape : capes) {
        AccountSkinDialog::CapeRow row;
        row.id = cape.id;
        row.alias = cape.alias;
        row.active = cape.active;
        capeRows.append(row);
    }
    m_accountSkinDialog->setCapes(capeRows);
    // 到这里那一趟联网才算走完（这一条是上面那个忙态唯一的"成功"出口）。
    m_accountSkinDialog->setBusy(false);
}

void MainWindow::onAccountLoginRequested() {
    // 和设置页那个登录按钮**同一条路**：AuthManager 那三个信号是共用的，所以登录成功
    // 之后 onLoginSucceeded() 会把凭据收进列表、把这个窗刷新一遍，一个字都不用在这里补。
    // 返回值 true = 流程开了头；两条 false 的路径 AuthManager 会先发一次 loginFailed()，
    // 弹窗由 onLoginFailed() 统一负责。
    // 返回值刻意不管：失败那两条路径 AuthManager 会先发一次 loginFailed()，弹窗由
    // onLoginFailed() 统一负责（直接连接，所以这里返回之前那个窗已经弹出来了）。
    // 忙态也不在这里摆 —— loginMicrosoft() 一起头就会发 loginStarted()，由
    // onLoginStarted() 统一处理（见那段说明：一个来源，不给界面留第二份状态）。
    m_authManager->loginMicrosoft();
}

void MainWindow::onOfflineAccountRequested(const QString &name) {
    QString error;
    // 名字合不合法在这里判（其实是 AccountStore 判，唯一出处）：空名字、名字里有空格、
    // 超过 16 个字符。失败按产品约定弹窗 —— 这是用户主动发起、而且被挡住了的操作。
    if (!AccountStore::addOffline(name, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("添加离线账号失败"), error);
        return;
    }
    const QString trimmed = name.trimmed();
    // 离线账号 = "用这个名字离线进游戏"。**"这一趟走离线"这件事不需要另外打开** ——
    // addOffline() 已经把它设成当前账号了，而启动那条路读的正是"当前账号是不是离线账号"
    // （offlineIdentity()）。2026-09-24 之前这里还要顺手勾一下主页那个「离线模式」。
    //
    // 名字进 AuthManager：底栏左下那行和启动参数里的 --username 取的都是它。
    m_authManager->setOfflinePlayer(trimmed);
    if (m_accountSkinDialog != nullptr) {
        // 添加成功之后才清输入框：失败时留着让用户改一下再点（重打一遍是白费）。
        m_accountSkinDialog->clearOfflineNameDraft();
    }
    updateBottomBar();
    appendLog(tr("已添加离线账号「%1」并切换到它。").arg(trimmed));
    // 列表、状态行、预览、披风这一整套刷新 —— 和切换账号之后做的事情一样。
    //
    // NOTE: 离线账号那一档**不是"整条路都不联网"**（2026-09-24 之前这句注释是那么写的，
    //       现在不成立了）：披风确实不拉（离线账号没有披风可拉），但这个账号**还没挑过**
    //       随机皮肤时，startPlayerSkinLoad() 会去 MineSkin 要一页列表（见 RandomSkinStore）。
    //       挑过一次之后就只在盘上读那一行 —— 本地命中，一个包都不发。
    refreshAccountSkin();
}

void MainWindow::onBottomBarPanelAction(bool account, const QString &key, int action) {
    if (account) {
        if (action != kAccountPanelUseAction) {
            return;  // 右面板那一行只有一颗按钮；别的编号是接线错了，什么都不做
        }
        if (key == kAccountPanelManageKey) {
            onAccountSkinButtonClicked();
            return;
        }
        // 这一行那颗按钮**是哪种**由行的状态决定（当前账号那颗是「登出」，别人是「选择」/
        // 「登录」），而行的状态是 recentAccountPanelRows() 一家算的 —— 这里用**同一个判据**
        // 分流，绝不看按钮上画着什么。key = 账号 uuid。
        if (key == AccountStore::activeUuid()) {
            logoutCurrentAccount();
            return;
        }
        useAccountFromPanel(key);
        return;
    }

    // 左面板（key = 会话号，见 runningGamePanelRows()）。两个编号各是一颗按钮，
    // "关"和"改设置"是两件完全不同的事 —— 认编号，不认按钮上画着什么。
    switch (action) {
        case kRunningPanelStopAction:
            stopGameSession(key.toInt());
            return;
        case kRunningPanelSettingsAction: {
            // 进的是**这一行那个实例**的设置 —— 面板那一行自己带着版本号，和"当前选中的
            // 那个实例"没关系（用户点的就是这一行）。
            const LauncherManager::GameSession game = m_launcherManager->session(key.toInt());
            if (game.sessionId == 0) {
                appendLog(tr("这个实例刚刚已经结束了。"));
                return;
            }
            openInstanceSettings(game.versionId);
            return;
        }
        case kRunningPanelLogAction:
            openGameLogForSession(key.toInt());
            return;
        default:
            return;
    }
}

void MainWindow::openGameLogForSession(int sessionId) {
    // 保留这个兼容入口：底栏和验证工程按这个名字调用。
    m_gameSessionController->openGameLogForSession(sessionId);
}

void MainWindow::logoutCurrentAccount() {
    // 认人用 activeUuid()，不用面板那一行的 key —— 面板弹出来到用户点下这一颗之间隔着
    // 他看那两眼的时间，这中间账号是可能被换掉的（recentAccountPanelRows() 那段注释里
    // 说的是同一件事）。眼下这两个值必然相等（onBottomBarPanelAction() 就是拿它分流的），
    // 但"登出谁"这件事只该有一个出处。
    const QString uuid = AccountStore::activeUuid();
    if (uuid.isEmpty()) {
        return;  // 本来就没登录（那一行不该长成「登出」）
    }
    const QString name = AccountStore::nameOf(uuid);
    if (!AccountStore::logout()) {
        InlineMessage::warning(m_ui->overlay(), tr("退出登录失败"),
                             tr("盘上的登录状态没能清掉，现在还是登录着「%1」。").arg(name));
        return;
    }
    // 内存里那一份也得跟着清 —— AuthManager 只在构造和换账号那两处读过盘，不重读的话
    // 底栏、启动参数、外观那几条路拿到的还是**上一个人的名字**（等于"看着登出了，
    // 手里还攥着他的状态"）。清干净之后 loggedIn() 是 false，界面自己退回未登录那一档。
    m_authManager->reloadStoredAccount();
    updateBottomBar();
    appendLog(tr("已退出账号「%1」（账号还留在列表里，点那一行的「选择」/「登录」就能回来）。")
                  .arg(name));
    // 外观也得跟着回默认：预览、账号列表里那个「当前」标记、披风列表都要重画一遍。
    // 没登录这一档走上半截就返回了（loadSavedToken() 取不到令牌），不会去联网。
    refreshAccountSkin();
}

void MainWindow::useAccountFromPanel(const QString &uuid) {
    if (uuid.isEmpty() || uuid == AccountStore::activeUuid()) {
        // 已经是它了。那一行的按钮本来就是灰的（见 recentAccountPanelRows()），走到这儿
        // 只是防一手。
        return;
    }
    const bool offline = AccountStore::isOffline(uuid);
    // 切换本身走**现成那一条**：onAccountSwitchRequested() 已经把"离线走 setOfflinePlayer、
    // 正版走 reloadStoredAccount"和"重画底栏 + 刷外观"都做完了。这里只补正版那半句。
    onAccountSwitchRequested(uuid);
    // 没切过去就到此为止。**这一句不是为了兜底而兜底**：onAccountSwitchRequested() 失败时
    // （列表里那份副本不完整会走那条）没有换人，活动槽里还是**上一个**账号的凭据 ——
    // 再往下走就会拿上一个人的令牌去做"够不够 6 小时"的判断，甚至去刷新他的令牌，
    // 而弹出来的话会把那个账号的名字写成这一行的名字。失败的框它自己已经弹过了。
    if (AccountStore::activeUuid() != uuid) {
        return;
    }
    if (offline) {
        return;  // 离线身份没有令牌这回事
    }

    // 正版：看一眼**盘上属于这个账号的那张**令牌还剩多少（onAccountSwitchRequested() 刚
    // 把这一份读进内存）。还够用（剩 ≥6 小时）就到此为止 —— 这一下不联网。
    //
    // NOTE: 这里刻意**不用 loadSavedToken()**：它只在"已经过期"时才去换，而用户要的是
    //       "剩不到 6 小时就换"（见 AuthManager::tokenRefreshWindowSecs 那段）。
    if (m_authManager->tokenSecondsLeft() >= AuthManager::tokenRefreshWindowSecs) {
        return;
    }
    QString error;
    // 这一段**是同步的**（15 秒超时的那种请求），先给用户一句话再进去。
    enterSyncWait(tr("正在刷新登录令牌..."));
    const bool refreshed = m_authManager->refreshMinecraftToken(&error);
    leaveSyncWait();
    if (refreshed) {
        appendLog(tr("「%1」的登录令牌剩余不足 6 小时，已经顺手换了一张新的。")
                      .arg(AccountStore::nameOf(uuid)));
        return;
    }
    // 失败：**账号已经切过去了**，只是令牌没换到新的 —— 这两件事必须分开说，不然用户会
    // 以为切换失败了（那他就会再点一次，而再点一次还是这个结果）。
    //
    // NOTE: 已知的两种失败原因（Azure 应用没被 Mojang 批准 / 盘上凭据属于另一个应用）
    //       都不是这里能修的，只能重新登录一次 —— 所以那句话得指出**下一步**，不能只说
    //       "刷新失败"。
    appendLog(tr("「%1」的登录令牌没能刷新：%2").arg(AccountStore::nameOf(uuid), error));
    InlineMessage::warning(m_ui->overlay(), tr("登录令牌没能刷新"),
                         tr("已经切到账号「%1」，但它保存的登录凭据换不出新令牌：\n\n%2\n\n"
                            "这个账号在「账号与外观…」里重新登录一次就好了。")
                             .arg(AccountStore::nameOf(uuid), error));
}

void MainWindow::onAccountSwitchRequested(const QString &uuid) {
    if (uuid.isEmpty() || uuid == AccountStore::activeUuid()) {
        return;  // 已经是它了：写盘一遍再读回来，白折腾
    }
    // 先问清是哪一种：activate() 之后盘上的状态已经变了，那时候再问容易问错。
    const bool offline = AccountStore::isOffline(uuid);
    QString error;
    if (!AccountStore::activate(uuid, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("切换账号失败"), error);
        return;
    }
    // 盘上换人了，内存里那一份也得跟着换 —— 不换的话底栏和启动参数里还是上一个人的名字
    // （AuthManager 只在构造时读过盘，见 reloadStoredAccount()）。
    if (offline) {
        // 离线账号没有凭据可读（活动槽刚被它清空），名字从账号列表里来。
        // "这一趟走离线"不用另外记一笔：activate() 已经把当前账号换成它了，
        // 启动那条路读的就是这个（offlineIdentity()）。
        m_authManager->setOfflinePlayer(AccountStore::nameOf(uuid));
    } else {
        m_authManager->reloadStoredAccount();
    }
    updateBottomBar();
    appendLog(tr("已切换到账号「%1」。").arg(AccountStore::nameOf(uuid)));
    // 外观也得跟着换：新账号是另外一套皮肤和披风。这一步会把预览、披风列表、账号列表
    // 一起刷一遍。
    refreshAccountSkin();
}

void MainWindow::onAccountRemoveRequested(const QString &uuid) {
    if (uuid.isEmpty()) {
        return;
    }
    const QString name = AccountStore::nameOf(uuid);
    // 删掉的正好是**当前**账号时，活动槽也一起清空（等于退出登录）—— 这件事必须在确认框
    // 里说清楚，用户点下去才知道自己会退出登录（见 AccountStore.hpp 文件头）。
    const bool wasCurrent = AccountStore::activeUuid() == uuid;
    const QString question =
        wasCurrent ? tr("删掉当前账号「%1」？\n\n它保存的登录凭据会一并删除，删完之后就是"
                        "未登录状态（可以重新登录）。")
                         .arg(name)
                   : tr("删掉账号「%1」？\n\n它保存的登录凭据会一并删除。").arg(name);
    if (!InlineMessage::question(m_ui->overlay(), tr("删除账号"), question, tr("是"), tr("否"), true,
                                            /*destructive=*/true)) {
        return;
    }
    if (!AccountStore::remove(uuid)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除账号失败"), tr("这个账号不在已保存的列表里。"));
        return;
    }
    if (wasCurrent) {
        m_authManager->reloadStoredAccount();
        updateBottomBar();
    }
    appendLog(tr("已删除账号「%1」。").arg(name));
    refreshAccountSkin();
}

void MainWindow::onSkinUploadRequested(const QString &path, const QString &variant) {
    // 先把文件读出来。**尺寸那条规矩只在 SkinApi::isAcceptableSkinSize() 里**（唯一出处），
    // 这里不自己再判一遍。但"读不出来"得在这里说 —— 它连尺寸都没有。
    QFile file(path);
    const QString fileName = QFileInfo(path).fileName();
    if (!file.open(QIODevice::ReadOnly)) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"),
                             tr("读不到「%1」这个文件。").arg(fileName));
        return;
    }
    const QByteArray png = file.readAll();
    file.close();
    const QImage image(path);  // 用 QImage 再读一遍只为拿宽高（下面换的是原始字节）
    if (image.isNull()) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"),
                             tr("「%1」不是一张能读出来的图片。").arg(fileName));
        return;
    }
    if (!SkinApi::isAcceptableSkinSize(image.width(), image.height())) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"),
                             tr("皮肤图必须是 64×64（新格式）或者 64×32（老格式），这张是 "
                                "%1×%2。")
                                 .arg(image.width())
                                 .arg(image.height()));
        return;
    }

    QString error;
    const QString token = m_authManager->loadSavedToken(&error);
    if (token.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"), error);
        return;
    }
    if (!m_skinApi->uploadSkin(token, png, variant, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"), error);
        return;
    }
    appendLog(tr("皮肤已上传（模型 %1）。").arg(variant));
    // 换完立刻重拉一遍：预览要显示**刚传上去的**那一张，而不是等下次开窗。
    refreshAccountSkin();
}

void MainWindow::onOfflineSkinPickRequested(const QString &path, const QString &variant) {
    // 离线账号那一颗。**尺寸那条规矩仍然只在 SkinApi::isAcceptableSkinSize() 里**
    // （和正版上传那条路同一个出处，见 onSkinUploadRequested）。
    const QString uuid = AccountStore::activeUuid();
    if (!AccountStore::isOffline(uuid)) {
        // 界面上这一颗只在离线档露着；真走到这里说明状态已经变了（比如另一处刚切了账号）。
        InlineMessage::warning(m_ui->overlay(), tr("换皮肤失败"),
                               tr("当前不是离线账号，这一张没有换。"));
        return;
    }
    const QString fileName = QFileInfo(path).fileName();
    const QImage image(path);
    if (image.isNull()) {
        InlineMessage::warning(m_ui->overlay(), tr("换皮肤失败"),
                               tr("读不到「%1」这张图（或者它不是一张能读出来的图片）。")
                                   .arg(fileName));
        return;
    }
    if (!SkinApi::isAcceptableSkinSize(image.width(), image.height())) {
        InlineMessage::warning(m_ui->overlay(), tr("换皮肤失败"),
                               tr("皮肤图必须是 64×64（新格式）或者 64×32（老格式），这张是 "
                                  "%1×%2。")
                                   .arg(image.width())
                                   .arg(image.height()));
        return;
    }
    QString error;
    if (!OfflineSkin::save(uuid, image, variant, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("换皮肤失败"), error);
        return;
    }
    appendLog(tr("已换成自己挑的皮肤（模型 %1）。").arg(OfflineSkin::loadModel(uuid)));
    // 重走一遍：主页的小人、底栏的头像、这一页的 3D 预览都是同一份图（见
    // PlayerSkinController::reload() 里手动那张排在最前面）。
    refreshAccountSkin();
}

void MainWindow::onOfflineSkinResetRequested() {
    const QString uuid = AccountStore::activeUuid();
    if (!AccountStore::isOffline(uuid)) {
        return;  // 这一颗只在离线档露着，同上
    }
    if (!OfflineSkin::clear(uuid)) {
        InlineMessage::warning(m_ui->overlay(), tr("恢复默认外观失败"),
                               tr("删不掉之前挑的那张皮肤，检查一下 data 目录的权限。"));
        return;
    }
    appendLog(tr("已恢复默认外观。"));
    refreshAccountSkin();
}

void MainWindow::onCapeSelectRequested(const QString &capeId) {
    if (capeId.isEmpty()) {
        return;
    }
    QString error;
    const QString token = m_authManager->loadSavedToken(&error);
    if (token.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("更换披风失败"), error);
        return;
    }
    if (!m_skinApi->setActiveCape(token, capeId, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("更换披风失败"), error);
        return;
    }
    appendLog(tr("已更换披风。"));
    refreshAccountSkin();
}

void MainWindow::onCapeHideRequested() {
    QString error;
    const QString token = m_authManager->loadSavedToken(&error);
    if (token.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("取消披风失败"), error);
        return;
    }
    if (!m_skinApi->hideCape(token, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("取消披风失败"), error);
        return;
    }
    appendLog(tr("已取消披风（不显示）。"));
    refreshAccountSkin();
}

void MainWindow::onLoginStarted() {
    // 从这一刻到浏览器回调回来可能有几十秒（用户要输账号密码、可能还要过二次验证）。
    // 界面上要说明自己在等什么的地方是**那个窗**：按钮一起变灰、状态行写清在等什么。
    //
    // NOTE: 写在这里（信号）而不是点下去那一下（槽）：「在登录中」就只有一个来源
    //       （AuthManager 自己），以后再多一个入口起这个流程，界面照样跟得上。
    //       超时兜底也在那边 —— 浏览器再也不回来时它会发一次 loginFailed()，
    //       忙态由 onLoginFailed() 松开。
    if (m_accountSkinDialog != nullptr) {
        m_accountSkinDialog->setBusy(true, tr("正在等浏览器完成登录…"));
    }
}

void MainWindow::onLoginSucceeded(const QString &playerName) {
    // 底栏左下的用户名和头像都换成真名 —— 这是"启动器有反应"里最该看见的那一下
    updateBottomBar();
    // 外观也要跟着换成**这个账号**的：底下那行名字已经变了，右栏还画着上一个人的
    // 皮肤，是这一屏里最容易看出来"没跟上"的一处。这里走的是和开窗同一条路
    // （先默认外观兜底，再拿缓存，最后后台刷），所以离线勾着时也一样不联网。
    startPlayerSkinLoad();
    // 这次登录的凭据要**收进多账号列表**（列表里就多一条，或者更新已有的那一条）：
    // 不收的话，用户切到别的账号之后就再也回不到这一个了 —— 那是这一整块功能最要紧的
    // 一步，而不是可选的记账。
    QString accountError;
    if (!AccountStore::captureActive(&accountError)) {
        // 收不进列表**不影响这一次登录**（凭据已经在活动槽里，启动游戏照旧），所以不弹窗；
        // 但要说一声，否则用户下次开那个窗会发现"我刚登录的账号怎么不在列表里"。
        appendLog(tr("保存账号到列表失败：%1").arg(accountError));
    }
    if (m_accountSkinDialog != nullptr) {
        refreshAccountSkin();
    }
    // 用户刚在浏览器里看到过「登录成功」，这里再说一声是为了把**登录到的是哪个账户**
    // 摆出来：拿错微软账户登录（家里共用的机器上很常见）只有这句话能看出来。
    InlineMessage::information(m_ui->overlay(), tr("登录成功"), tr("已登录为「%1」。").arg(playerName));
}

void MainWindow::onLoginFailed(const QString &errorMessage) {
    if (m_accountSkinDialog != nullptr) {
        // 那个窗上也可能有一个人在等（它是从「登录新账号…」起的流程）。忙态必须在这里
        // 松开，否则弹窗关掉之后它那几个按钮一直灰着，看着像卡死了。
        m_accountSkinDialog->setBusy(false);
        m_accountSkinDialog->setStatusText(accountSkinStatusText());
    }
    // ★ 2026-09-26：登录失败还可能是"**上一个在线账号已经不在盘上了**"。
    //   登录那一趟开头会把活动槽里的 Minecraft 凭据清掉（防"旧 uuid 配新刷新令牌"的错配，
    //   见 AuthManager::exchangeAuthorizationCode），而 AuthManager 在失败那条路上已经把
    //   内存里那份身份跟着盘清了（见 handleNewConnection 里那段）。**界面得跟着画一遍**：
    //   底栏那个名字是上一次 updateBottomBar() 摆上去的，不重画就会一直停在上一个人的名字
    //   上，而底栏第二行"未登录"的判断（offlineIdentity()，读盘）与启动那句判据都说"没人"。
    //
    //   条件写全是为了**别动离线账号那一档**：那时候盘上什么都没变（离线账号没有凭据），
    //   底栏该继续写离线那个名字。而"从来没登录过 + 登录失败"也会走进来一次，那一趟是
    //   幂等的：画出来还是原来那张默认脸、占位名字。
    if (!offlineIdentity() && !m_authManager->loggedIn()) {
        updateBottomBar();
        // 外观同理：脸还是上一个人的（m_avatarPixmap 只在 applyPlayerSkin() 里换），
        // 这里走的是"未登录 ⇒ 默认外观"那一支，**不联网**（见 startPlayerSkinLoad() 里
        // !loggedIn() 那个早退）。刻意**不调** refreshAccountSkin()：那一支最后会去
        // loadSavedToken() 拉披风，而盘上此刻正躺着**新账号**那半份微软凭据 —— 一次同步的
        // 令牌刷新会把"登录失败"当场变成"其实登上了另一个人"，用户看到的弹窗就成了假话。
        startPlayerSkinLoad();
    }
    // 登录失败是**挡住启动**的错误（没有令牌就点不动「启动游戏」），按产品约定必须弹窗。
    // 正文是 AuthManager 那边给的、能直接给用户看的话（"这个微软账户还没注册过 Xbox 账户，
    // 到 xbox.com 登录一次"这种），不是一句"登录失败"。
    InlineMessage::warning(m_ui->overlay(), tr("登录失败"), errorMessage);
}

void MainWindow::onAddFolderClicked() {
    m_folderController->add();
}

void MainWindow::removeFolderFromList(const QString &folder) {
    m_folderController->removeFromList(folder);
}

void MainWindow::onFolderActionRequested(const QString &path, int action) {
    m_folderController->action(path, action);
}

void MainWindow::onFolderRefreshRequested(const QString &path) {
    m_folderController->refresh(path);
}

void MainWindow::onFolderRenameRequested(const QString &path) {
    m_folderController->rename(path);
}

void MainWindow::onFolderOpenRequested(const QString &path) {
    m_folderController->open(path);
}

void MainWindow::onFolderDeleteRequested(const QString &path) {
    m_folderController->removeFromDisk(path);
}

bool MainWindow::applyRename(const QString &oldId, const QString &newId) {
    // ★ 2026-10-15：那四道闸、"改完谁跟着走"、以及"哪一半成了/哪一半没成"这套分账，全在
    //   `InstanceSettingsController::rename()` 里。这一层只是个转发。
    //
    // 为什么留着这一层而不是把调用点全改成问 controller：验证工程按**名字**驱动这个槽
    // （`QMetaObject::invokeMethod(this, "applyRename", ...)`，见 mw_check 的 S 段）——
    // 那是"这个槽驱动得起来"这条判据本身，不是实现细节。同 applyGroupList / applyInstanceSettings。
    return m_instanceSettings->rename(oldId, newId);
}

void MainWindow::showInstanceSettingsNotice(const InstanceSettingsController::Notice &notice) {
    // 这一层的活是**把事实说成人话**：controller 那边只有"哪一档、是谁、数据层说的原因"，
    // 标题和正文在这里挑。所以下面每一句话都是**逐字**从原来的 applyRename() 里搬过来的
    // （验证工程按行数/原文对数，改一个字就是回归）。
    switch (notice.kind) {
    case InstanceSettingsController::Notice::Kind::RenameBlockedRunning:
        // 「先关掉游戏再改名」这句不能省：用户很可能以为"改个名字而已"，看到"拒绝访问"
        // 会去查杀毒软件。
        InlineMessage::warning(m_ui->overlay(), tr("重命名实例"),
                               tr("「%1」正在运行，先关掉游戏再改名。")
                                   .arg(instanceDisplayName(notice.instanceId)));
        break;
    case InstanceSettingsController::Notice::Kind::RenameBlockedScanning:
        // 说清楚"等它完了"（不是"重开启动器"）：那一趟还在跑，等它自己结束就行。
        InlineMessage::warning(m_ui->overlay(), tr("重命名实例"),
                               tr("「%1」正在做完整性检查，等它完了再改名。")
                                   .arg(instanceDisplayName(notice.instanceId)));
        break;
    case InstanceSettingsController::Notice::Kind::RenameRejected:
        // `detail` 就是 `InstanceFiles::validateRenameTarget()` 给的那一整句，原样说。
        InlineMessage::warning(m_ui->overlay(), tr("重命名实例"), notice.detail);
        break;
    case InstanceSettingsController::Notice::Kind::RenameFailed:
        // 这句原因可能带"回滚到什么程度"，原样说 —— 加工它只会丢掉那个信息。
        InlineMessage::critical(m_ui->overlay(), tr("重命名失败"), notice.detail);
        break;
    case InstanceSettingsController::Notice::Kind::RenameGroupNotMoved:
        // 说清楚"名字改成了、归属没搬"：不能只弹"改名失败"，那会让人再点一次。
        InlineMessage::warning(m_ui->overlay(), tr("分组归属没跟着搬"),
                               tr("实例已经改名为「%1」，但它原来的分组归属没能搬过去：\n%2\n\n"
                                  "可以在「分组…」里重新归一次。")
                                   .arg(notice.instanceId, notice.detail));
        break;
    case InstanceSettingsController::Notice::Kind::SettingsIdFailed:
        // 连身份都写不下去 ⇒ **整次没保存**（说清这一点的同时把"为什么"也给出来：
        // 文件被占着 / 目录不可写，这两条用户自己能查）。
        InlineMessage::critical(m_ui->overlay(), tr("实例设置没能保存"),
                                tr("「%1」的 wtfml.meta 写不进去，这次的设置没有保存。\n\n"
                                   "（这份文件可能被别的程序占着，或者它所在的目录不可写。）")
                                    .arg(instanceDisplayName(notice.instanceId)));
        break;
    case InstanceSettingsController::Notice::Kind::SettingsGroupNotMoved:
        // 和改名那一条同款，只是没有新名字：设置**存上了**，只是归属没搬 ⇒ 不能说成
        // "没能保存"（那会让用户重来一次）。
        InlineMessage::warning(m_ui->overlay(), tr("分组归属没跟着搬"),
                               tr("「%1」的分组归属没能搬过去：\n%2\n\n"
                                  "可以在「分组…」里重新归一次。")
                                   .arg(instanceDisplayName(notice.instanceId), notice.detail));
        break;
    case InstanceSettingsController::Notice::Kind::IconNotSaved:
        // 只有图标这一项没成 —— 说清"别的东西照常保存了"（否则用户会以为整次白改）。
        InlineMessage::warning(m_ui->overlay(), tr("图标没能保存"), notice.detail);
        break;
    case InstanceSettingsController::Notice::Kind::SettingsSaveFailed:
        // 数据层不弹窗是约定，所以弹在这一层。detail 里已经写清了"是哪个文件、为什么"。
        InlineMessage::critical(m_ui->overlay(), tr("实例设置没能保存"), notice.detail);
        break;
    }
}

void MainWindow::showServerListNotice(const ServerListController::Notice &notice) {
    // 和上面那一条同款：controller 那边只有"哪一档、数据层说的原因"，标题和正文在这儿挑。
    // 每一句话都是**逐字**从原来那三个 lambda 里搬过来的（验证工程按原文对数）。
    switch (notice.kind) {
    case ServerListController::Notice::Kind::UpdateFailed:
        InlineMessage::warning(m_ui->overlay(), tr("没能改这一台"), notice.detail);
        break;
    case ServerListController::Notice::Kind::AddRejected:
        // 地址为空这一档**没有** detail（`detail` 是数据层给的原因，而这一档根本没走到
        // 数据层）—— 所以这句话写在弹窗这一边，和 `updateEntry` 那边是同一句。
        InlineMessage::warning(m_ui->overlay(), tr("没能加这一台"),
                               tr("地址不能是空的 —— 没有地址，"
                                  "这一行在游戏里是一条点不动的空气。"));
        break;
    case ServerListController::Notice::Kind::AddFailed:
        InlineMessage::warning(m_ui->overlay(), tr("没能加这一台"), notice.detail);
        break;
    case ServerListController::Notice::Kind::RemoveFailed:
        InlineMessage::warning(m_ui->overlay(), tr("没能删这一台"), notice.detail);
        break;
    }
}

void MainWindow::pinInstance(const QString &versionId) {
    // 「置顶」= 把这个实例挪进**置顶组**（用户 2026-09-25 原话：「置顶以后进选了这个的组」），
    // 同时记下"它置顶之前待在哪一组"（2026-09-26 原话：「加一个记录置顶前这玩意属于哪一组
    // 的」）。两件事一起做是 store 那一层的实现（InstanceGroupStore::pinInstance()）——
    // 分两次写的话，中间那一份文件自相矛盾，取消置顶就会给出一个不一样的答案。
    // 哪一组是置顶组由 store 回答（清单里勾了「设为置顶组」的那一组；一个都没勾就退回名字
    // 叫「置顶」的那个默认组，那是老用户的迁移路；都没有则是空串）。
    //
    // 没有置顶组 ⇒ 那一格本来就**没画**（fillInstanceTree 里那一格判的就是它）。走到这儿
    // 只可能是"树画出来之后、点下去之前"用户把那一组删了。什么都不做、也不弹窗：这一下
    // 本来就没有去处，而屏幕上那颗箭头此刻也已经不在了，屏幕底下那条"已置顶"的日志更不该
    // 写出来（store 那一层同样不写盘，见 InstanceGroupStore::pinInstance()）。
    if (m_groupStore->pinnedGroupId().isEmpty()) {
        return;
    }

    // 归属键走 instanceKey()（有稳定 id 用 id、没有用目录名）—— 读和写必须同一个键，
    // 否则存下去的那一刻就对不上了（见 InstanceGroupStore 文件头）。
    QString error;
    if (!m_groupStore->pinInstance(instanceKey(versionId), &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("收藏失败"), error);
        return;
    }
    // 写成功会发 groupsChanged ⇒ 整棵树重灌（那个实例挪进置顶组、它那一行那颗上箭头消失，
    // 因为已经在里面了）。**不再手动刷一遍**：手动刷只会和信号那条路撞在一起。
    appendLog(tr("已收藏：%1").arg(instanceDisplayName(versionId)));
}

void MainWindow::unpinInstance(const QString &versionId) {
    // 「取消置顶」= 把这个实例**送回它置顶之前待的那一组**（用户 2026-09-25 原话：
    // 「置顶组里的实例悬停时右侧显示下箭头，即取消置顶」；落点由 2026-09-26 那句定死：
    // 「加一个记录置顶前这玩意属于哪一组的，用户直接塞的取消置顶才归进未分类」）。
    // 三种记录三种落点都写在 InstanceGroupStore::unpinInstance() / 那个文件头那一段里：
    // 记着组 id ⇒ 回那个组；记着"明确选了未分组"⇒ 回未分组；记着 null（置顶前压根没表过
    // 态）⇒ 把那一条归属**删掉**、让显示层按版本类型重新归类。
    //
    // NOTE: **没有记录**（用户自己用「移入分组」把它塞进置顶组的，或者这一版之前就在里面的
    //       老数据）⇒ 落未分组，和「移出分组」逐字同一个落点。这一档**不猜**它本来该归哪
    //       一组：那条信息真的不存在（从来没被记下来过），猜错的表现是"取消置顶之后它跳去
    //       了一个我没选过的组"，比"落在未分组里、用户自己再挪一次"糟得多 —— 而"用户直接
    //       塞的才归未分类"正是用户点名要的那一档。
    //
    // NOTE: 不判"它到底在不在置顶组里" —— 那一格只在 rowUnpinRole() 为真的行上画得出来，
    //       走到这儿就说明在。真要在外面，这一下也只是写一条"未分组"，无害。
    QString error;
    if (!m_groupStore->unpinInstance(instanceKey(versionId), &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("取消收藏失败"), error);
        return;
    }
    // 写成功会发 groupsChanged ⇒ 整棵树重灌（那个实例回到记录里那一组、它那一行那颗箭头
    // 换回朝上的）。**不再手动刷一遍**，理由同 pinInstance()。
    appendLog(tr("已取消收藏：%1").arg(instanceDisplayName(versionId)));
}

bool MainWindow::confirmDeleteInstance(const QString &versionId) {
    if (versionId.isEmpty()) {
        return false;
    }
    const QString name = instanceDisplayName(versionId);

    // 闸口一：**正在跑的不能删**。放在最前面、并且必须排在真正动手之前 —— 运行中的实例在
    // Windows 上 jar 被 JVM 锁着，删到一半才失败的话用户看到的是一句"拒绝访问"，而真正
    // 该说的是这一句（和「重命名」那一条逐字同款）。
    if (m_launcherManager->isVersionRunning(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在运行，先关掉游戏再删。").arg(name));
        return false;
    }
    // 闸口二：**正在装 / 正在导入的也不能删**。判据用的是 `installingVersionIds()` ——
    // 「现在谁动不得」的唯一出处（底栏那颗启动按钮、下拉框都是问它）。在这里删掉的后果
    // 很具体：那一路还在往这个目录里写，写到收尾那一步会**把目录重新建出来**（json /
    // meta 那两笔），用户看到的是"删掉了、过一会儿它自己又回来了，而且是半截的"。
    if (installingVersionIds().contains(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在安装，等它装完再删。").arg(name));
        return false;
    }
    // 闸口三：**正在体检的也不能删**（coding plan §4.1「同一实例不允许同时扫描和删除/
    // 改名」）。后台那一趟还在拿这个实例的绝对路径 stat 文件，删到一半它就在扫一堆不存在的
    // 路径，报告里全是假的"缺文件"。
    if (integrityScanningVersion(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在做完整性检查，等它完了再删。").arg(name));
        return false;
    }

    // 这一趟**不可逆**，所以先问一句（用户原话：「删除前需要弹窗提示」）。正文里把目录的
    // 完整路径写出来：用户要能看清删的是哪一个目录、它长在哪，而不是只知道"某个实例"。
    const auto answer = InlineMessage::question(
        m_ui->overlay(), tr("删除实例"),
        tr("要删除实例「%1」吗？\n\n"
           "会把 %2 这整个目录删掉（jar、json、以及这个实例自己的设置都在里面），"
           "删掉之后找不回来。\n\n"
           "它所在的游戏文件夹、以及别的实例都不受影响。")
            .arg(name, QDir::toNativeSeparators(AppPaths::versionDir(versionId))),
        tr("删除"), tr("取消"), false, /*destructive=*/true);
    if (!answer) {
        return false;  // 取消不是错误，不写日志
    }
    return deleteInstance(versionId);
}

bool MainWindow::deleteInstance(const QString &versionId) {
    // 真正动手的那一半（问的那一半在 confirmDeleteInstance() 里）。和「重命名」是同一个
    // 切法：验证工程直接驱动这一半，弹窗那一半它驱动不了。切在这里的另一个好处是
    // **守卫和动作在同一个函数里** —— 任何调用点（含以后新加的）都绕不过去。
    if (versionId.isEmpty()) {
        return false;
    }
    const QString name = instanceDisplayName(versionId);
    if (m_launcherManager->isVersionRunning(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在运行，先关掉游戏再删。").arg(name));
        return false;
    }
    // 闸口二（和上面那一半逐字同款，理由也逐字相同）：守卫和动作留在同一个函数里，
    // 任何调用点都绕不过去 —— 包括以后直接从别处调 deleteInstance() 的那些。
    if (installingVersionIds().contains(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在安装，等它装完再删。").arg(name));
        return false;
    }
    // 闸口三（同上，理由见 confirmDeleteInstance 那一半）：正在体检的不能删。
    if (integrityScanningVersion(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在做完整性检查，等它完了再删。").arg(name));
        return false;
    }

    QString error;
    if (!m_versionManager->deleteInstance(versionId, &error)) {
        // 失败原因是数据层给的**一整句**（可能带"删到一半"那种状态说明），原样弹出来
        // —— 和「删除游戏文件夹」那条路一个规矩。
        InlineMessage::critical(m_ui->overlay(), tr("删除实例失败"), error);
        return false;
    }

    // 分组归属**故意不动**：wtfml.folders 里可能还留着这个实例的一条归属（它指向的组还在，
    // 所以不会被 store 的清成员规则扫掉）。留着是有意的，理由是**它的代价是零**：
    //   - 它对界面没有任何影响：归属只有在"这个实例还在"的时候才会被问到，而那张树是
    //     按**盘上的实例**建的（遍历 installedVersions()，每条再拿 instanceKey() 去问归属）
    //     ⇒ 指向已删实例的那些条目从来不会被遍历到。「组名 (N)」里的 N 也是从树上数的，
    //     不是从归属表里数的。换句话说：既没有幽灵行，也没有虚高的计数。
    //   - 为它写一条"删实例时顺手清归属"要新加一个 store 的 API（现在只有整份
    //     replaceGroups()），换来的只是少几个字节的死数据。
    //
    // ★★ 2026-09-27 修：这一段原来还写着"用户把同一个版本号重装一遍时它会回到原来那一组"
    //    ——**那是假的**（只有一半情况成立）。归属的键是 instanceKey() = `wtfml.meta` 里的
    //   稳定 id（没有才退回目录名），而 deleteInstance() 把整个目录连 `wtfml.meta` 一起删了
    //    ⇒ 重装出来的是一个**没有 id 的新目录**，键退回目录名，而那条残留的键是旧 UUID，
    //    永远不可能再对上。而那个 id 是**惰性生成**的：用户只要开过一次实例设置按过确定
    //    （ensureInstanceId()）或者改过名就会落一个 —— 所以"重装回原组"只对**从来没过
    //    拿过 id 的老实例**（键就是目录名）成立，其余的残留就是永远的死数据。
    //   ⇒ 上面那两条"代价为零"的理由照样成立，但别再拿"重装能回原组"当理由用。
    //
    // 真嫌它脏的话，下一次在「分组设置」里按确定会整份重写这个文件（那时成员表按现有的
    // 组重建，指向已删实例的那些条目照旧保留）。
    refreshInstalledVersions();
    appendLog(tr("已删除实例：%1").arg(versionId));
    return true;
}

QString MainWindow::instanceKey(const QString &versionId) const {
    // 稳定 id / 目录名的取舍必须和实例设置写侧共用同一条规则。
    return InstanceSettingsController::instanceKeyFor(m_versionManager, versionId);
}

void MainWindow::onGroupInstanceClicked() {
    // 被占着就先让位（静默收掉那一层 + 排到下一轮），见 runWhenOverlayFree 那段。
    runWhenOverlayFree([this] { openGroupDialogNow(); });
}

void MainWindow::openGroupDialogNow() {
    // 每个组里有多少个实例：从**当前那张树**上数，不重新扫盘。它就是用户此刻看到的
    // 东西，而对话框拿它显示「组名 (N)」和"删这个组会有多少实例变成未分组"——
    // 两处都该和屏幕上的数字一致。
    //
    // NOTE: 一个实例都没有的组**不在树上**（用户要的"空白的分组隐藏"）⇒ 这里数出来是 0，
    //       那也正是实话；它在对话框里照样列着、照样能改名排序，只是括号里写着 0。
    QHash<QString, int> counts;
    const QTreeWidget *tree = m_ui->getInstanceList();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *item = tree->topLevelItem(i);
        const QVariant groupId = item->data(0, MainWindowUI::groupIdRole());
        if (groupId.isValid()) {
            counts.insert(groupId.toString(), item->childCount());
        }
    }

    // 「恢复默认分组设置」之后每个组里会有几个实例：恢复 = 用户自己建的组全没了 ⇒ 那些
    // 实例回到"没表过态"，于是按版本类型自动归类（defaultGroupIdFor()）—— 落点只有这里
    // 算得出来，而对话框要在那一句话里说清"是不是真的一个实例都不删"。
    //
    // 取的实例集合和上面那张树是同一份（在装的那几个不算）：两处数的要是同一批东西，
    // 不然"恢复之后原版有 3 个"和屏幕上看到的数字对不上。
    const QList<InstanceGroupStore::Group> defaults = InstanceGroupStore::defaultGroups();
    QSet<QString> defaultIds;
    for (const InstanceGroupStore::Group &group : defaults) {
        defaultIds.insert(group.id);
    }
    const QSet<QString> installing = installingVersionIds();
    QHash<QString, int> defaultCounts;
    for (const QString &id : m_versionManager->installedVersions()) {
        if (installing.contains(id)) {
            continue;
        }
        const QString groupId =
            defaultGroupIdFor(m_versionManager->displayParts(id), m_versionManager, defaultIds);
        defaultCounts[groupId] = defaultCounts.value(groupId) + 1;
    }

    // NOTE: 这张窗**不读选中的实例**（用户原话：「不再是针对单个实例的设置」）——
    //       一个实例都没选也开得出来，那是"先建几个组、再往里放东西"的正常顺序。
    //       把实例放进某个组是组行行尾那颗齿轮的事（见 onGroupActionRequested()）。
    GroupDialog dialog(toDialogGroups(m_groupStore->groups()), counts, toDialogGroups(defaults),
                       defaultCounts, this);
    // 整页盖在内容区上（见 OverlayHost::execPage）。这一页自己没有标题行，页眉那句
    // 就是它唯一一处标题（和它自己的窗口标题同一个出处）。
    if (m_ui->overlay()->execPage(&dialog, GroupDialog::windowTitle()) != QDialog::Accepted) {
        // 取消 / 返回箭头：磁盘一个字节都没动（对话框只改自己手里那份工作副本）。
        return;
    }
    applyGroupList(&dialog);
}

bool MainWindow::applyGroupList(const GroupDialog *dialog) {
    if (dialog == nullptr) {
        return false;
    }
    const QList<InstanceGroupStore::Group> wanted = toStoreGroups(dialog->groups());

    // 变了才写：每点一次「确定」都重写一遍文件（连带刷新 mtime）而其实什么都没改，是
    // 最常见的一次操作。比较是**逐字段**的（连"展开还是收着"那一格一起比）—— 用户完全
    // 可能只动了那一格，而它一样要留住。
    if (wanted == m_groupStore->groups()) {
        return true;
    }
    QString error;
    if (!m_groupStore->replaceGroups(wanted, &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("分组保存失败"), error);
        return false;
    }
    // NOTE: 这里**不**手动重灌那张树。这条写路径成功之后数据层会发 groupsChanged，
    //       界面接的是同一个刷新函数（见 setupConnections）—— 在这里再来一次就是
    //       同一件事做两遍，而且"哪些路径会刷新"会变成两处各自维护的约定。
    return true;
}

void MainWindow::onGroupActionRequested(const QString &groupId, int action) {
    // 这一组此刻的样子（名字、实例数、展开状态）。**动作菜单里每一格都用的同一份**：
    // 菜单是照快照摆的，而这里要的是"现在"—— 菜单开着的时候别处可能已经把它删了，
    // 那几种情况一律当"什么都没发生"（点了个过期的菜单项不该弹一个用户看不懂的框）。
    MainWindowUI::GroupRow row;
    if (!groupRowFor(m_ui, m_groupStore, groupId, &row)) {
        return;
    }

    switch (action) {
    case MainWindowUI::GroupActionMoveInto: {
        // "把选中的实例移进哪一组"：列的是**全部**分组（含树上一个实例都没有、藏起来的
        // 那些）—— 那是把实例放进一个空组的唯一入口（见 MainWindowUI::openGroupPicker()）。
        const QString selectedId = selectedVersionId(m_ui->getInstalledVersionCombo());
        if (selectedId.isEmpty()) {
            InlineMessage::information(m_ui->overlay(), tr("移入分组"), tr("先选中一个实例。"));
            return;
        }
        // 弹在**原来那颗齿轮**下面（用户看到的是"刚才那块框换了个内容"）。那一行理论上
        // 一定还在（菜单就是从它那儿弹出来的）；万一不在了就退回光标那里，框照样出得来，
        // 不会神秘消失。
        QRect anchor = gearAnchorFor(m_ui, groupId);
        if (anchor.isEmpty()) {
            anchor = QRect(QCursor::pos(), QSize(1, 1));
        }
        // 灰哪一格按那个实例的**老家**判（不是"它那一条画在哪个组下面"—— 置顶着的有两行，
        // 点哪一行会给出不同答案，理由见 groupRowsForMenu() 那条 NOTE）。
        const QString selectedKey = instanceKey(selectedId);
        m_ui->openGroupPicker(
            groupRowsForMenu(m_ui, m_groupStore, selectedKey,
                             homeGroupIdOf(m_groupStore, selectedKey,
                                           m_versionManager->displayParts(selectedId),
                                           m_versionManager, knownGroupIdsOf(m_groupStore))),
            anchor);
        return;
    }
    case MainWindowUI::GroupActionClearMembers:
        clearGroupMembers(groupId, row.name);
        return;
    case MainWindowUI::GroupActionRename: {
        // 「问」和「做」分开（和实例页那颗「重命名…」同一个拆法）：做的那一半
        // renameGroupTo() 能在验证工程里直接驱动。
        //
        // NOTE: 原来这半句还写着"因为 QInputDialog 在 offscreen 下驱动不了" —— 2026-09-25
        //       那一档换成 InlineMessage::getText() 之后，**这个理由不成立了**：问名字的那一格
        //       现在就是一个能按 objectName 找得到的 QLineEdit（见 InlineMessageBox::
        //       setLineEdit），验证工程可以真的把字打进去再点确定。拆成两步照旧有它的道理
        //       （"做"那一半不用开界面就能验），但不再是"因为没法驱动"。
        bool ok = false;
        const QString name =
            InlineMessage::getText(m_ui->overlay(), tr("重命名分组"), tr("组名："), row.name, &ok);
        if (ok) {
            renameGroupTo(groupId, name);
        }
        return;
    }
    case MainWindowUI::GroupActionToggleExpand: {
        // 先摆树里那一行，再记 —— rememberGroupExpanded() 只管写盘（见它的 NOTE），
        // 所以"这一下要让用户看见什么"是调用方的事。两处管的是同一格，摆完就是它。
        //
        // NOTE: 状态从**那一行自己**读（`!item->isExpanded()`）而不是拿菜单那份快照里的
        //       `row.expanded`：菜单是弹出来的那一刻拍的，中间用户完全可能又点了一下组头
        //       （那一下也会落盘）。和版本树/加载器树那几处"先算再做"同一个教训。
        QTreeWidget *tree = m_ui->getInstanceList();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = tree->topLevelItem(i);
            if (item->data(0, MainWindowUI::groupIdRole()).toString() != groupId) {
                continue;
            }
            const bool expand = !item->isExpanded();
            item->setExpanded(expand);
            rememberGroupExpanded(groupId, expand);
            return;
        }
        // 那一行不在树上（这一组空了 —— 空组是藏着的 —— 或者刚被别处删了）：没有"看得见的
        // 那一行"要摆，把状态记下来就好。
        rememberGroupExpanded(groupId, !row.expanded);
        return;
    }
    case MainWindowUI::GroupActionDelete: {
        // 先问再删，而且说清**实例不会被删**：用户看到"删除"两个字时的第一反应就是怕
        // 连里面的东西一起没了，而这里确实不会（和对话框里那句一模一样）。
        const bool answer = InlineMessage::question(m_ui->overlay(), tr("删除分组"),
            tr("「%1」会被删除，组里的 %2 个实例不会被删掉，它们只是变成未分组。\n\n"
               "确定要删除吗？")
                .arg(row.name)
                .arg(row.memberCount), tr("是"), tr("否"), false, /*destructive=*/true);
        if (answer) {
            deleteGroupById(groupId);
        }
        return;
    }
    }
    // 越界（理论上到不了：菜单是照 MainWindowUI::groupActionCount() 循环摆的）。
}

void MainWindow::clearGroupMembers(const QString &groupId, const QString &groupName) {
    QTreeWidget *tree = m_ui->getInstanceList();
    QTreeWidgetItem *groupItem = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->data(0, MainWindowUI::groupIdRole()).toString() == groupId) {
            groupItem = item;
            break;
        }
    }
    if (groupItem == nullptr) {
        return;  // 藏起来的空组（或者刚被删了）：没有可移的东西
    }
    QStringList keys;
    for (int i = 0; i < groupItem->childCount(); ++i) {
        const QString versionId = instanceIdOf(groupItem->child(i));
        if (!versionId.isEmpty()) {
            keys.append(instanceKey(versionId));
        }
    }
    if (keys.isEmpty()) {
        return;
    }

    // 「移出分组」对**没表过态**的那些实例也得写一条：不写的话它们下一次刷新又被自动归类
    // 拉回这一组来（写下去的是"明确选了未分组"那个保留值，见 InstanceGroupStore::
    // ungroupedMarker()）—— 表现就是"点了移出，可它们还在里面"。
    QString error;
    int moved = 0;
    {
        // 挡信号：下面每写一条都会发一次 groupsChanged，而界面接的那条路是**重灌整棵树**
        // （连带重扫一遍盘）。不挡的话 N 个实例就是 N 次全盘重扫，而真机上实例可以有 61 个。
        // 写完自己刷一次就够 —— 和 seedDefaultGroups() 那一段同一个路数。
        const QSignalBlocker blocker(m_groupStore);
        for (const QString &key : keys) {
            if (!m_groupStore->assign(key, QString(), &error)) {
                break;
            }
            ++moved;
        }
    }
    refreshInstalledVersions();
    if (moved < keys.size()) {
        // 半路写失败了。**已经写下去的那些是落盘了的**（说不说它都变了），所以只说清
        // 哪几个没成，别让用户以为整件事都没发生。
        InlineMessage::critical(m_ui->overlay(), tr("移出分组失败"),
            tr("「%1」里还有 %2 个实例没能移出来：\n%3").arg(groupName).arg(keys.size() - moved).arg(error));
        return;
    }
    appendLog(tr("已把「%1」里的 %2 个实例移出分组").arg(groupName).arg(moved));
}

bool MainWindow::setSelectedInstanceHomeGroup(const QString &groupId) {
    // 取值口径和另外两颗实例级按钮一样：**下拉框**说了算（它就是"当前选中的那个实例"），
    // 不是树的 currentItem()（点一下组头它也会变）。
    return setInstanceHomeGroup(selectedVersionId(m_ui->getInstalledVersionCombo()), groupId);
}

bool MainWindow::setInstanceHomeGroup(const QString &versionId, const QString &groupId) {
    if (versionId.isEmpty()) {
        // 挑目标那个框只在"有选中的实例"时才点得动（groupActionEnabled 那一条），
        // 而菜单开着的时候那个实例可能被删掉了 —— 真走到这儿就说一句，别静默。
        InlineMessage::information(m_ui->overlay(), tr("移入分组"), tr("先选中一个实例。"));
        return false;
    }
    const QString key = instanceKey(versionId);
    // 已经在这一组里了（挑目标那个框里那一格本来就灰着，这儿是给别的入口的保险）。
    //
    // ★ 置顶着的那一档**不在这里返回**：它的老家和 members 指着的是两个组，而"盘上已经是
    //   什么样"这件事得由 store 那一层按老家判（下面那句 setInstanceHomeGroup() 自己做）。
    //   在这里拿 groupIdOf() 当代替的话，"把它移进它老家那一组"会被判成"已经做好了"而
    //   直接返回 —— 看着像没事，其实是要写的那一格没写。
    //
    // NOTE: 目标非空时"没表过态"和"明确选了它"要分开看 —— 光比"存的是不是它"会把
    //       "明确选了它"当成已经做好了；目标为空（未分组）时**一律写**：那条保留值
    //       才是"别再自动归类"的意思，而一个从来没表过态的实例正需要它（不写的话它
    //       下一次刷新又被拉回默认组去，表现就是"选了未分组但没生效"）。
    if (!groupId.isEmpty() && !m_groupStore->isPinned(key) &&
        m_groupStore->groupIdOf(key) == groupId) {
        return true;
    }

    QString error;
    // ★ 走 setInstanceHomeGroup() 而不是 assign()：置顶着的实例这一档**只改老家那一格**，
    //   members 继续指着置顶组（那是在不在置顶组里的判据）。理由（以及"走 assign 会把老家
    //   一起丢掉"那两条）写在 InstanceGroupStore::setInstanceHomeGroup() 上面。没置顶的
    //   实例它内部就是原样委托给 assign()，一个字都没变。
    if (!m_groupStore->setInstanceHomeGroup(key, groupId, &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("移入分组失败"), error);
        return false;
    }
    QString destination = MainWindowUI::ungroupedGroupText();
    for (const InstanceGroupStore::Group &group : m_groupStore->groups()) {
        if (group.id == groupId) {
            destination = group.name;
            break;
        }
    }
    appendLog(tr("实例已分组：%1 → %2").arg(key, destination));
    // NOTE: 这里**不**手动重灌那张树 —— 写成功之后数据层会发 groupsChanged（见上面那段）。
    return true;
}

bool MainWindow::renameGroupTo(const QString &groupId, const QString &newName) {
    QList<InstanceGroupStore::Group> groups = m_groupStore->groups();
    int row = -1;
    for (int i = 0; i < groups.size(); ++i) {
        if (groups.at(i).id == groupId) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        return false;  // 这一组已经被别处删掉了：什么都不做，也不弹窗（不是用户此刻做的事）
    }

    const QString name = newName.trimmed();
    // 两条规矩和 GroupDialog 里那一份**逐字一样**（名字是用户认这个组的东西，两个入口
    // 得用同一把尺子）：空名字和重名都不收。那份校验在 ui/ 里，而 ui/ 不 include src/
    // 是反过来也成立的（那边给的是 main 这一侧拿不到的类型），所以这两句是各写一份的
    // —— 改了这边记得连那边一起改。
    if (name.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("重命名分组失败"), tr("组名不能是空的。"));
        return false;
    }
    if (name.size() > GroupDialog::maxGroupNameLength()) {
        InlineMessage::warning(m_ui->overlay(), tr("重命名分组失败"),
                             tr("组名最多 %1 个字，现在有 %2 个。")
                                 .arg(GroupDialog::maxGroupNameLength())
                                 .arg(name.size()));
        return false;
    }
    for (const InstanceGroupStore::Group &group : groups) {
        if (group.id != groupId && group.name.compare(name, Qt::CaseInsensitive) == 0) {
            InlineMessage::warning(m_ui->overlay(), tr("重命名分组失败"),
                                 tr("已经有一个叫「%1」的组了。").arg(group.name));
            return false;
        }
    }
    if (groups.at(row).name == name) {
        return true;  // 改成一模一样的名字：不写盘
    }

    // 组改名**不动归属** —— 归属挂的是组的 id，不是名字（见 InstanceGroupStore::Group）。
    groups[row].name = name;
    QString error;
    if (!m_groupStore->replaceGroups(groups, &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("重命名分组失败"), error);
        return false;
    }
    return true;
}

bool MainWindow::deleteGroupById(const QString &groupId) {
    QList<InstanceGroupStore::Group> groups = m_groupStore->groups();
    QString name;
    for (int i = 0; i < groups.size(); ++i) {
        if (groups.at(i).id == groupId) {
            name = groups.at(i).name;
            groups.removeAt(i);
            break;
        }
    }
    if (name.isEmpty()) {
        return false;  // 找不到（已经被别处删了）
    }

    QString error;
    if (!m_groupStore->replaceGroups(groups, &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("删除分组失败"), error);
        return false;
    }
    // 组里的实例**一个都不删**：它们指向的组没了，那些归属按 replaceGroups() 的规矩一起
    // 被清掉 —— 于是回到"没表过态"，下一次刷新按版本类型自动归类。和对话框里删一个组
    // 走的是同一条路、同一个说法。
    appendLog(tr("分组已删除：%1").arg(name));
    return true;
}

void MainWindow::rememberGroupExpanded(const QString &groupId, bool expanded) {
    QString error;
    // NOTE: 写盘但**不发 groupsChanged**（理由见 InstanceGroupStore::setGroupExpanded()）——
    //       所以那一行的展开状态由调用方自己摆好，这里只管记。
    if (!m_groupStore->setGroupExpanded(groupId, expanded, &error)) {
        // 只记一句日志、不弹窗：这一下**已经生效了**（树上那一格就是刚摆的），没留住的
        // 只是"下次打开还照这样"——它挡不住用户做任何事，而每点一下弹一个框反倒是把用户
        // 从手上的事里打断（产品约束管的是"会挡住操作"的那一类错误）。
        appendLog(error);
    }
}

void MainWindow::openGroupMenuFor(QTreeWidgetItem *item) {
    if (item == nullptr || !item->data(0, MainWindowUI::rowGroupGearRole()).toBool()) {
        return;
    }
    const QString groupId = item->data(0, MainWindowUI::groupIdRole()).toString();
    // 快照里带上"此刻有没有选中的实例、它在不在这组里"：菜单第一项能不能点、挑目标那个
    // 框里哪一格灰着，都由它决定。
    MainWindowUI::GroupRow row;
    const QString selectedId = selectedVersionId(m_ui->getInstalledVersionCombo());
    const QString selectedKey = instanceKey(selectedId);
    if (!groupRowFor(m_ui, m_groupStore, groupId, &row, selectedKey,
                     homeGroupIdOf(m_groupStore, selectedKey,
                                   m_versionManager->displayParts(selectedId),
                                   m_versionManager, knownGroupIdsOf(m_groupStore)))) {
        return;  // 那一组已经不在了（菜单还没来得及弹它就被删了）
    }
    // 锚点 = 那一行行尾那颗齿轮占的格子（几何的唯一出处见 InstanceGroupDelegate::gearRect()）。
    m_ui->openGroupMenu(row, gearAnchorFor(m_ui, groupId));
}

namespace {

/// 「结构/原理图」那一页顶上那两排「来源」（★ 2026-10-10，见
/// `InstanceFilesPage::setStructureSources()`）。
///
/// 用户原话：「那就像版本树一样根据存档来列…两个下拉框并排」+「**上面一排是存档的，
/// 下面一排是投影的**」。其中"**哪几个存档真有结构**"要读盘（结构方块存下来的东西落在
/// 存档自己的 `generated/` 底下），那件事由 `InstancePagesController::worldStructures()`
/// 算 —— 这一层只把那份事实摆成那一页要的形状。
///
/// 摆出来的规矩（逐条都是用户点名的）：
///   - **没有结构的存档一个都不列**（「没有结构的存档不显示在列表里面」）；
///   - **没装投影模组就没有「投影」那一排**（「没有装投影图则不显示投影图下拉」）；
///   - 一个存档都没有、可投影装着 ⇒ 存档那一排照旧露着，里面是那**一行灰字**
///     （「如果没有任何存档含有结构那么存档总下拉打开显示无可用的结构」）；
///   - 两样都没有 ⇒ 返回**空表**，那一页只剩"这个实例还没有结构 / 原理图"那句提示
///     （「如果存档里面没有且投影也没装的话就直接不显示任何框，只保留没有结构的提示
///     文本」）。
///
/// ★★ 2026-10-10：**这一页"用不上"的判据跟着放宽了**（见调用处那一段）：`schematic`
///    从"只认 Litematica"变成"两半里有一半有东西就露"。理由就是上面第一条 —— 结构那一
///    半是**游戏自己**的东西，没装投影模组照样该看得见，"这一页没人会读"那句话在那一半
///    上根本不成立。
///
/// NOTE: 两排的**排头**（「存档」/「投影」）走 `InstanceFilesPage` 上那三个静态函数 ——
///       界面上写着的那几个字只有一个出处，这一层不自己抄一份（验证工程也读它们）。
///       所以 `InstancePagesController` 那边给的是"哪几个存档有结构"（两张平行的表），
///       排头是在这儿戴上去的。
[[maybe_unused]] QList<InstanceFilesPage::StructureSource> structureSourcesFor(
    const InstancePagesController::WorldStructures &worlds, bool litematica,
    const QString &schematicsDir) {
    QList<InstanceFilesPage::StructureSource> sources;
    if (!worlds.labels.isEmpty() || litematica) {
        // 一个存档都没有结构、可投影装着 ⇒ 这一排**露着但空着**（那行灰字在下拉框里，
        // 见 InstanceFilesPage::setStructureSources()）。
        InstanceFilesPage::StructureSource worldSource;
        worldSource.title = InstanceFilesPage::structureWorldTitle();
        worldSource.labels = worlds.labels;
        worldSource.paths = worlds.paths;
        sources.append(worldSource);
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

}  // namespace

void MainWindow::openInstanceSettings(const QString &versionId, const QString &pageKey) {
    if (versionId.isEmpty()) {
        InlineMessage::information(m_ui->overlay(), tr("实例设置"), tr("先选中一个实例。"));
        return;
    }
    // ★ 这一层被别的页面/提示占着时：**静默把它收掉**，然后把"开"排到下一轮（用户
    //   2026-09-29 原话见 runWhenOverlayFree 那段）。空着的时候同一轮就开出来。
    // NOTE: 那一句空 id 的判断必须排在**这一发之前** —— 它是"用户还没选实例"这种普通
    //       情况，不该顺手把别的页面收掉。
    runWhenOverlayFree([this, versionId, pageKey] { openInstanceSettingsNow(versionId, pageKey); });
}

bool MainWindow::instanceSupportsQuickPlay(const QString &versionId,
                                           QuickPlay::Kind kind) const {
    if (m_versionManager == nullptr) {
        return false;
    }
    return QuickPlay::supports(m_versionManager->getVersionJson(versionId), kind);
}

void MainWindow::startQuickPlay(const QString &versionId, QuickPlay::Kind kind,
                                const QString &value) {
    // 第一道闸：**这个版本认不认这一条参数**（判据见 QuickPlay，界面上那两颗按钮在支持的
    // 版本上才露，这是兜底）。发一条游戏不认识的选项不是"被忽略"，是 jopt-simple 抛
    // UnrecognizedOptionException、游戏当场退出 —— 所以宁可在这儿说一句。
    if (!instanceSupportsQuickPlay(versionId, kind)) {
        InlineMessage::warning(m_ui->overlay(), tr("这个版本用不了"),
                               QuickPlay::unsupportedText(kind));
        return;
    }

    QuickPlay::Target target;
    target.kind = kind;
    target.value = value;
    // 让位 + 排到下一轮：调用点在这个信号里，实例设置那一页正卡在它自己的嵌套事件循环里，
    // 当场去启动等于"页还开着就开始挑 Java / 下运行时"（见 runWhenOverlayFree 那段）。
    runWhenOverlayFree([this, versionId, target] { startQuickPlayNow(versionId, target); });
}

void MainWindow::startQuickPlayNow(const QString &versionId, const QuickPlay::Target &target) {
    QComboBox *combo = m_ui->getInstalledVersionCombo();
    const int row = combo == nullptr ? -1 : combo->findData(versionId);
    if (row < 0) {
        // 这一页开着的时候那个实例被删了 / 改名了（下拉框按 id 找不到了）。**不启动**：
        // 启动走的是"下拉框里选中的那一个"，硬塞一个找不到的值只会开起别的实例来。
        InlineMessage::warning(m_ui->overlay(), tr("找不到这个实例"),
                               tr("「%1」现在不在已安装列表里，没有启动。")
                                   .arg(instanceDisplayName(versionId)));
        return;
    }
    // 排上这一发，再把下拉框切到那个实例 —— 顺序不能反：`setCurrentIndex()` 那一发会同步
    // 牵动一堆界面（启动按钮、底栏、实例列表的选中），中间任何一处都不该看见"有 quick play
    // 但版本还没切过去"的状态。
    m_pendingQuickPlay = target;
    combo->setCurrentIndex(row);
    // 走**正常那条启动路**：令牌 / 挑 Java / 启动提醒 / 「正在下载的那一个不能启动」
    // 那几道闸一道都不绕过（用户要的是"试试看能不能生效"，而不是"绕过启动器直接塞参数"）。
    onStartButtonClicked();
}

void MainWindow::openInstanceSettingsNow(const QString &versionId, const QString &pageKey) {
    m_instanceDialogController->open(versionId, pageKey);
}

QHash<QString, QString> MainWindow::instanceFileDirs(const QString &versionId) const {
    return InstancePagesController::fileDirs(versionId);
}

QStringList MainWindow::instanceCommonFolderKeys(const QString &versionId,
                                                 const QHash<QString, QString> &dirs) const {
    // 同前：判断在 `InstancePagesController::commonFolderKeys()`。那一份"有哪几条常用目录"
    // 是界面的事（`InstanceSettingsDialog::shortcutPathKeys()`），所以从这儿递进去。
    return InstancePagesController::commonFolderKeys(versionId, dirs,
                                                     InstanceSettingsDialog::shortcutPathKeys());
}

void MainWindow::openDownloadCategory(const QString &categoryKey, const QString &returnInstanceId) {
    // 认行用**条目自己的 UserRole**（= 那一栏的键），不是行号：左栏那一列的行序是界面
    // 自己按 downloadCategoryKeys() 摆出来的，这一层不去假设"第几个就是哪一个"
    // （和 MainWindow::refreshDownloadCategory 那边认身份的做法一致）。
    QListWidget *categories = m_ui->getDownloadCategoryList();
    const QStringList keys = MainWindowUI::downloadCategoryKeys();
    int row = -1;
    for (int i = 0; categories != nullptr && i < categories->count(); ++i) {
        if (categories->item(i)->data(Qt::UserRole).toString() == categoryKey) {
            row = i;
            break;
        }
    }
    if (row < 0 || !keys.contains(categoryKey)) {
        // 认不出来就**说出来**：悄悄什么都不做的症状是"按了那颗按钮没反应"，
        // 而这一层是唯一知道"下载页到底有哪几栏"的地方，出错时不说就没人查得到。
        qWarning() << "下载页没有这一栏：" << categoryKey;
        return;
    }

    // ② 这一栏是不是**从某个实例的设置页里点「下载新模组」进来的**（2026-09-28）。
    //
    //    是的话记下那个实例，并且把「← 返回实例设置」露出来 —— 用户原话：「点击下载新模组/
    //    资源包/...时左上角保留一个返回键，返回到实例设置里面，和资源包详情啥的一致」。
    //    上面那一下 `dismiss()` 会把那一页收掉（`OverlayHost` 不做页面栈），所以"来路"只能
    //    由这一层自己记着、自己放回去（和 openFileResourcePage 那边同一个手法）。
    //
    //    ★ 2026-09-28：那颗键现在**整个下载页共用一颗**、摆在左栏顶上（用户原话「返回按钮
    //      也是放在左上角（就是左边栏的顶上）」），所以不再"只露这一栏那一颗"，翻到别的栏
    //      它照旧在那儿 —— 这一点比原来对：能不能回去和用户此刻在逛哪一栏无关。
    //
    //    ★ `m_downloadFromPageKey` 和这一位**同生共死**：它就是"来路那一页"。也正因为
    //      「下载新模组」那一栏**就是**实例设置里那一页（`filesDownloadCategoryKey()` 那张
    //      表是"这两处是一回事"的唯一出处），它不用另要一个参数，写的就是 `categoryKey`。
    m_downloadFromInstanceId = returnInstanceId;
    m_downloadFromPageKey = returnInstanceId.isEmpty() ? QString() : categoryKey;
    m_ui->setDownloadBackVisible(!returnInstanceId.isEmpty());

    // ① 先收掉当前这一页。**只能走 dismiss()**：它发的就是顶上那颗返回箭头的同一发信号
    //    （见 OverlayHost::dismiss() 里那段 ★）—— 这一页正卡在 `execPage()` 的嵌套事件
    //    循环里，只 `closeOverlay()` 的话一个信号都不发，那个循环永远不返回（用户看到的
    //    是"按了没反应"，测试是直接挂在那儿）。
    //
    //    NOTE: 这一发只是**尽力**先收（常见情况下它收完就完了）；收干净了没有由下一轮的
    //          `continueOpenDownloadCategory()` 判 —— 上面一层压着一条提示时 `dismiss()`
    //          收掉的是**提示**、那一页还在原处。
    m_ui->overlay()->dismiss();

    // ② 摆选项卡与那一栏**排到下一轮事件循环**：这一句这会儿还在上面那个嵌套循环的调用栈
    //    里（按钮的 clicked 处理函数），`execPage()` 还没返回、覆盖层也还盖在内容区上 ——
    //    现在就把选项卡翻过去，翻的是它**底下**那一层，而覆盖层收干净的那一下还没发生。
    //    排到下一轮，那时这一页已经关好了（和 openResourceDetailPage 那边同一个手法）。
    QTimer::singleShot(0, this, [this, row, categoryKey, returnInstanceId] {
        continueOpenDownloadCategory(row, categoryKey, returnInstanceId, kDownloadOpenRounds);
    });
}

void MainWindow::continueOpenDownloadCategory(int row, const QString &categoryKey,
                                             const QString &returnInstanceId, int roundsLeft) {
    // ---------- 收页：收到干净为止，收不干净就**什么都不做**（2026-09-28）----------
    //
    // 为什么还要再收一次：`openDownloadCategory()` 里那一发 `dismiss()` 收的是**最上面
    // 那一层** —— 提示压在实例设置上面时收掉的是提示，那一页还在原处占着覆盖层那一位。
    //
    // ★ 收不干净时**绝不能切选项卡**：切了就是把那一页孤零零地丢在别的选项卡上（它收着、
    //   看不见），而它占着覆盖层那一位 ⇒ 用户此后既回不去那一页、也开不了新的一页，还会
    //   被一次一次拽到下载页 —— 用户报的"点来点去点多了会卡住进不去实例设置（而且会变到
    //   下载页去了）"就是这个形状。宁可这一次什么都不做，也不要把他丢进那个状态。
    //   （正常路径上收不掉是走不到的：那一页归「实例」那一颗，而它的按钮只在它露着的时候
    //     点得着 —— 见 openInstanceSettings() 里那段。留着这一条是因为"走不到"和"保证了
    //     不会走不到"是两件事。）
    OverlayHost *overlay = m_ui->overlay();
    if (overlay != nullptr && overlay->pageOpen()) {
        if (roundsLeft > 0 && overlay->dismiss()) {
            // 收到一半（这一发收回来的那一层要等 `execEmbedded` 收尾才真正落下去）—— 再排一轮。
            QTimer::singleShot(0, this, [this, row, categoryKey, returnInstanceId, roundsLeft] {
                continueOpenDownloadCategory(row, categoryKey, returnInstanceId, roundsLeft - 1);
            });
            return;
        }
        qWarning() << "下载页：实例设置那一页没能收掉，这一次不切选项卡";
        return;
    }

    m_ui->getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
    // 从实例设置进来的那一次，顺手做三件事 —— 都是"让屏幕上说的和用户以为的是同一件事"：
    //   ① 「已安装版本」那一格选到**那个实例**上：这一栏下来的东西是落进"当前选中的实例"
    //      的（见 installResourceVersionNow），不替用户选上的话，"从实例 A 的页面点进来、
    //      东西却下进了实例 B"——而屏幕上没有任何东西说过会这样（用户那句「下载新模组就
    //      自动放进当前实例文件夹」说的正是这一件事）；
    //   ② 版本筛子填成那个实例的游戏版本；
    //   ③ 模组加载器筛子填成那个实例的加载器。
    // ②③ 是用户 2026-09-28 那条：「点击下载新模组/...后自动在搜索框里填入对应的版本和
    // 加载器」。
    if (!returnInstanceId.isEmpty()) {
        QComboBox *combo = m_ui->getInstalledVersionCombo();
        const int index = combo->findData(returnInstanceId);
        if (index >= 0) {
            combo->setCurrentIndex(index);
            updateStartButtonText();
        }
    }
    QListWidget *categories = m_ui->getDownloadCategoryList();
    if (categories == nullptr) {
        return;
    }
    categories->setCurrentRow(row);
    // ★ 已经在那一行上时 `setCurrentRow()` **不发** currentRowChanged（Qt 只在"变了"
    //   的时候才发），而"第一次进这一页才去搜一次"那一发正挂在它身上 ⇒ 这里补一次。
    //   同一条路、同一个函数，重复调用是安全的（它自己判"树上已经有东西就不动"）。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const bool known = ResourceCatalog::kindFromCategoryKey(categoryKey, &kind);
    if (!returnInstanceId.isEmpty() && known) {
        fillDownloadFiltersFromInstance(kind, returnInstanceId);
    }
    if (known) {
        ensureResourcePageLoaded(kind);
    }
}

void MainWindow::fillDownloadFiltersFromInstance(ResourceCatalog::Kind kind,
                                                const QString &instanceId) {
    const QString key = ResourceCatalog::kindKey(kind);
    // 版本那一格**读的是框里的字**（`downloadFilterGameVersion()` 就是这么读的，那一格是
    // 可输入的），所以写也要写进"字"里：`setCurrentText()` 两样都写了（有那一项就选中它、
    // 没有就把字填进编辑框）—— 那个实例的版本号在在线清单里通常有，可万一没有（离线装
    // 的自定义版本），填进去照样是对的，而那正是用户要的那一格该显示的东西。
    const QString gameVersion = m_versionManager->instanceGameVersion(instanceId);
    if (!gameVersion.isEmpty()) {
        if (QComboBox *versionCombo = m_ui->getDownloadFilterVersionCombo(key)) {
            versionCombo->setCurrentText(gameVersion);
        }
    }
    // 加载器那一格**只有模组页有**（别的页 getDownloadFilterLoaderCombo 回空指针）。
    //
    // ★ 按**文字**认（「Fabric」/「Forge」/「NeoForge」/「Quilt」）而不是按 `data()`：
    //   两个来源的键**不是一套**（Modrinth 是词、CurseForge 是数字，见 loaderOptions），
    //   而文字两边一模一样 —— 按 data 认的话，用户把来源换成 CurseForge 之后这一格就再也
    //   填不上了（而且不报错、看不出来）。
    QString loaderName;
    if (m_versionManager->instanceModLoader(instanceId, &loaderName) && !loaderName.isEmpty()) {
        if (QComboBox *loaderCombo = m_ui->getDownloadFilterLoaderCombo(key)) {
            const int index = loaderCombo->findText(loaderName);
            if (index >= 0) {
                loaderCombo->setCurrentIndex(index);
            }
        }
    }
}

void MainWindow::returnToInstanceSettingsFromDownload() {
    const QString instanceId = m_downloadFromInstanceId;
    // ★ 回来时开在**来路那一页**上（用户原话「从模组点进去，返回也在模组而非概览，
    //   其余同理」）。这一位和上面那一位同生共死，所以下面两句话并排写。
    const QString pageKey = m_downloadFromPageKey;
    // 先收干净再开那一页：`openInstanceSettings` 里第一件事就是问覆盖层开没开，
    // 而这一颗键通常是在"没有覆盖层"的时候点的（不冲突），可万一撞上一条提示，
    // 那一位留着就会让下一次进来时这颗键莫名地还露着。
    m_downloadFromInstanceId.clear();
    m_downloadFromPageKey.clear();
    m_ui->setDownloadBackVisible(false);
    if (instanceId.isEmpty()) {
        return;  // 正常走不到：那颗键只在这一位非空时才露面
    }
    openInstanceSettings(instanceId, pageKey);
}

void MainWindow::onNavigationApplied(int sourceIndex, int index, bool reselected) {
    // 顶上那四颗导航，这一层只管**下载页那两件**（用户 2026-09-28 原话「在这个情况下点击
    // 顶部下载按钮就刷新页面回到主页」+「如果点击了下载新模组按钮后再点击实例按钮，功能
    // 就和返回一样，如果点两下那就是先返回再回到实例主页」）。翻页、以及"点的是当前这一
    // 颗就把上面那层收掉"那两件，仍旧是 MainWindowUI 自己接的那一条在管（那边**先接**，
    // 所以它先跑完，见 setupConnections 里那条连接的注释）—— 这里不重复做，也不去改它。
    //
    // ---------- 「实例」那一颗 = 按了「← 返回实例设置」（用户原话见上） ----------
    //
    // ★ 这**不是**那一颗返回键的第二条接线（那颗键仍旧只有一处出口，见 setupConnections
    //   里那段）—— 这里只是"同一件事从另一颗键上够得着"。两条都走
    //   `returnToInstanceSettingsFromDownload()`，所以"清哪两位、开在哪一页"一个字都不
    //   用写两遍。
    //
    // ★ 判据四条：
    //   ① 点的是「实例」那一颗；
    //   ② **刚才是从下载页按的**（`sourceIndex`）。少了这一条，"从实例进来 →
    //      切去主页逛了一圈 → 点实例"也会被当成"回实例设置"，可那一下用户要的是切到实例
    //      列表（他手上那一页早就切走了）。这一位**只有 MainWindowUI 说得出口**：翻页那一下
    //      是它先跑的，这边醒过来时 `currentIndex()` 已经是新的一颗了（见那个访问器的注释）。
    //   ③ 这一趟**确实是从某个实例点进来的** —— 也就是那颗返回键正露着。
    //   ④ 这一层**没有别的页面占着**（典型：用户先从下载页点开了一个资源的详情页）。
    //      ★ 2026-09-29 起 `openInstanceSettings()` 自己会让位（静默收掉占着的那一层、
    //        排到下一轮再开，见 `runWhenOverlayFree()`）—— 所以这一条判据**不再是**
    //        "那一页开不出来"，而是"这一按到底算不算'回实例设置'"：占着的时候按「实例」
    //        就照常翻页（和按「主页」「设置」一样），让位那一套交给上面那一发。
    //        留着这一条的理由是**顺序**：来路（`m_downloadFromInstanceId`）要排在那一次
    //        开页之前清掉，而"让位"是异步的 —— 两下里撞在一起会变成"返回键没了、页也没开"。
    //
    // ★ 第二下为什么自动就成了"回到实例主页"：上面那一发把这一页开起来之后，当前选项卡
    //   **就是**「实例」，于是再按一下正中 MainWindowUI 那条的 ② 档（点的是当前这一颗
    //   ⇒ `overlayHost->dismiss()` = 收掉这一页，露出底下的实例列表）。那一下这一位已经
    //   被上面清空了 ⇒ 这里不接手，两下没人打架。
    const bool overlayFree = m_ui->overlay() == nullptr || !m_ui->overlay()->isOpen();
    if (index == MainWindowUI::instancesTabIndex()
        && sourceIndex == MainWindowUI::downloadTabIndex()
        && !m_downloadFromInstanceId.isEmpty() && overlayFree) {
        returnToInstanceSettingsFromDownload();
        return;
    }

    //
    // ★ 判据三条，缺一不可：
    //   ① 点的**就是**「下载」那一颗；
    //   ② **这一按是重点当前页**（`reselected`）—— 也就是"这一按没有翻页"。
    //      少了这一条，"从实例进来、切去主页逛了一圈、再点下载"也会被当成"回主页" —— 可那
    //      一下用户要的是**切回来**（原来那一栏、连筛子带实例都在，见 MainWindowUI 搭导航
    //      那段①），顺手清掉就成了"我明明没点返回，回实例设置那颗键却没了"；
    //   ③ 这一趟**确实是从某个实例点进来的**（那一位非空 = 「← 返回实例设置」正露着）。
    //      用户原话里这一条是跟着"点了下载新模组"来的；平白从顶部点进下载页时，左栏挑的
    //      哪一栏、框里填的什么都是他自己摆的，那一发不该替他重置。
    //
    // ★★ 2026-09-28 更正 ② 的**量法**：它原来写的是 `tabs->currentIndex() == index`，那一格
    //    **量不出东西来** —— 上面那条 MainWindowUI 的槽先跑，而它自己就会把选项卡翻过来，
    //    于是"切去了主页再点下载"走到这里时 `currentIndex()` 早就是下载页了 ⇒ 判据永远为真，
    //    等于没有这一条（注释说的那个症状照样发生，只有 `mw_check` T25.8 量得出来）。
    if (index != MainWindowUI::downloadTabIndex()
        || !reselected
        || m_downloadFromInstanceId.isEmpty()) {
        return;
    }

    // 来路那一栏先认下来：下面头一件事就是把这几位清掉。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const bool known = ResourceCatalog::kindFromCategoryKey(m_downloadFromPageKey, &kind);

    // ① 把"从实例进来"这件事整个收起来。返回键露不露只看这一位（见 setDownloadBackVisible
    //    的调用点），所以两位一起清，再把它收起来。
    m_downloadFromInstanceId.clear();
    m_downloadFromPageKey.clear();
    m_ui->setDownloadBackVisible(false);

    // ② 那一栏的筛子是**我们**灌进去的（fillDownloadFiltersFromInstance），回主页就该一并
    //    还回去 —— 留着的话，用户下一次点进那一栏看到的是"我没填过"的条件。
    //
    //    ★ 清完**还得把那一页的搜索结果扔掉**：筛子是新条件、树上那批是按旧条件搜出来的，
    //      两下里对不上 —— 正是上面筛子那一段点名的"界面上筛了、请求里没筛"。这里**不发
    //      新请求**：清空之后，下一次进那一页时 ensureResourcePageLoaded() 自然会按新的
    //      筛子搜一次（既不替用户白等一下，也不白烧一次限流额度）。
    if (known) {
        resetDownloadFilters(kind);
        if (QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind))) {
            tree->clear();
        }
    }

    // ③ 左栏回到第一栏 —— 那**就是**下载页的首页（「游戏」：在线版本清单，见
    //    downloadCategoryGameIndex()）。
    //
    //    ★ 摆的是**下标**，不是"现在选中的那一栏"：这件事跟用户此刻在哪一栏无关，起点是
    //      确定的。（切过去会触发 currentRowChanged → 右栏翻页；第一栏是「游戏」，那一页
    //      不走资源搜索，所以这一下不会多发任何请求。）
    if (QListWidget *categories = m_ui->getDownloadCategoryList()) {
        categories->setCurrentRow(MainWindowUI::downloadCategoryGameIndex());
    }
}

void MainWindow::openFileResourcePage(const QString &versionId, const QString &iconKey,
                                      const QString &kindKey) {
    // ★★ 2026-09-27 用户原话：「点击下载界面返回后应该退回到实例设置而不是实例列表」。
    //    这一页是**从实例设置里点出来的**，用户按返回要回的是**那一页** —— 不接的话他落在
    //    实例列表上，原来那一页（连页码带勾选）整个没了。`OverlayHost` 不做页面栈
    //    （那一层的规矩，见它的 .hpp），所以"来路"得由这一层自己放回去。
    //
    // ★ 判据是 `QDialog::Rejected`（= 用户把这一页**收掉**了）：返回箭头和 Esc 走的都是
    //   它。Accepted 专用于「模组设置」，转入当前实例的配置文件管理。
    //
    // ★★ 必须和 `kResourcePageNotOpened` 分开认：那一档是"这一页压根没开出来"（上面第
    //   ① ② 步认完之后、`m_resourceHits` 里那条已经不在了之类）。不分开的话，用户点一下
    //   名字 = 实例设置被收掉、详情页没出来、实例设置又被摆回去，看着就是白闪一下。
    //
    // ★ 来路是 `InstanceFiles`：只有这一条路上那一页才可能摆「模组设置」（见
    //   openResourceDetailPage 里那一句 —— 还要**同时**是个模组）。
    //
    // ★ 2026-09-28：`versionId` 就是"当前实例"，一并传进去（开页时按它的版本预选、把它的
    //   加载器排前面）。原来它只用来"返回时把实例设置放回去"，现在两件事都用它。
    const int result = openResourceDetailPage(iconKey, kindKey, DetailSource::InstanceFiles, versionId);
    if (result == QDialog::Accepted && !versionId.isEmpty() && kindKey == QStringLiteral("mod")) {
        openInstanceSettings(versionId, QStringLiteral("config"));
        return;
    }
    if (result != QDialog::Rejected || versionId.isEmpty()) {
        return;
    }
    // ★ 回来时开在**刚才那一页**上（`kindKey` 就是那一页的键 —— 「这一页是哪一类资源」
    //   和「实例设置里哪一页」现在恰好同名，那个等式在 filesDownloadCategoryKey() 一处）。
    //   用户原话：「从模组点进去，返回也在模组而非概览，其余同理」。
    openInstanceSettings(versionId, kindKey);
}

void MainWindow::openInstanceFileResource(const QString &versionId, const QString &pageKey,
                                          const QString &path) {
    // ★ 2026-09-27 补，用户原话：「除了模组以外的几个都是点击模组名称自动进入下载界面
    //   （前提是匹配上了，模组的待会再说）」。进的是**那个资源自己的那一页**（下载页里点
    //   一行开的就是它，见 openResourceDetailPage）——「前提是匹配上了」那句话只有这样
    //   才说得通：换成"切到下载页那一栏"就跟匹配不匹配没关系了。
    //
    // ① 这一页是"哪一类资源"：判据就是 filesDownloadCategoryKey() 那张表 —— 页键和下载页
    //    分类键现在恰好同名，可那一处才是"这两件事是一回事"的唯一出处。认不出来就回去，
    //    连覆盖层都不碰：悄悄地少收一页比收错了强。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const QString kindKey = InstanceSettingsDialog::filesDownloadCategoryKey(pageKey);
    if (kindKey.isEmpty() || !ResourceCatalog::kindFromCategoryKey(kindKey, &kind)) {
        return;
    }
    // ② 这一条是哪个资源。认不出来 = 没匹配上 ⇒ **什么都不做**（那一行的名字本来也没
    //    变成链接，正常走不到这儿；测试里点一下量得到的就是"没反应"）。
    //    表在那一层（反查那一趟填的，见 `InstanceIconController`）；这一层只拿那把钥匙。
    QString iconKey;
    if (!m_instanceIconController->fileResourceIconKey(path, &iconKey)) {
        return;
    }
    // ③ 先收掉实例设置那一页 —— **只能走 dismiss()**，理由和 openDownloadCategory() 里
    //    逐字相同：那一页正卡在 `execPage()` 的嵌套事件循环里，只 `closeOverlay()` 的话
    //    一个信号都不发，那个循环永远不返回。
    if (m_ui->overlay() == nullptr || !m_ui->overlay()->isOpen()) {
        // 页已经关掉了（连着点两下、或者返回键和它撞上）。没有那一层嵌套循环要收，
        // 直接开新的就完了。
        openFileResourcePage(versionId, iconKey, kindKey);
        return;
    }
    m_ui->overlay()->dismiss();
    // ④ 摆到下一轮：这一句这会儿还在那个嵌套循环的调用栈里（名字上那个鼠标松开事件），
    //    execPage() 还没返回、覆盖层也还盖着 —— 现在就去开新页的话，openResourceDetailPage
    //    开头那条 `overlay->isOpen()` 会当场把这次点击吃掉。
    //
    //   NOTE: 键与键都在这一层认完了，排到下一轮带过去的只有那三个**字符串**：下一轮
    //         里这一页早就没了，那时候再去查"这一条是哪个资源"就等于查一份已经被清掉的
    //         上下文（和 openDownloadCategory 那边带 `row` 过去是同一个道理）。
    //         `versionId` 也在这一层就绑好：它是"回来时把哪一页放回去"的唯一依据，而实例
    //         设置那一页这会儿已经在往下撤了。
    QTimer::singleShot(0, this, [this, versionId, iconKey, kindKey] {
        openFileResourcePage(versionId, iconKey, kindKey);
    });
}

// ---------- 「世代备份」那三件事（见 .hpp 那一段）----------

namespace {

/// 每个实例的备份留最近多少份。**不按文件算、按"存了几次"算**：备份是"某一刻整个 config/
/// 的一批"，用不着按单个文件各留二十份（那会长出二十个文件夹、每个里面一个文件）。
///
/// NOTE: 20 这个数是拍的，不是量出来的 —— 它只影响"最多能回退几步"，而每一步都比上一步
///       更不像用户想要的。真要按量出的数来定，得先知道用户平均多久存一次（今天没有这个
///       数）。这里宁可留够：20 份 × 一份配置那几十 KB 是几个 MB。
[[maybe_unused]] constexpr int kConfigBackupGenerations = 20;

}  // namespace

QString MainWindow::instanceIdOfPath(const QString &path) const {
    // cleanPath 顺手把 `\` 换成 `/`（Windows 上真有可能递进来一份原生分隔符的）—— 后面
    // 每一步都只认一种写法，省得两个方向的字符串各比一遍。
    const QString clean = QDir::cleanPath(path);
    const QString prefix = AppPaths::versionsDir() + QLatin1Char('/');
    if (!clean.startsWith(prefix)) {
        return QString();  // 不在任何实例底下（理论上到不了这儿：这一格只列 config/ 里的文件）
    }
    const QString rest = clean.mid(prefix.size());
    const int slash = rest.indexOf(QLatin1Char('/'));
    // `slash <= 0` 一并把"versions/ 底下直接一个文件"那种挡掉了（那不是实例）。
    return slash <= 0 ? QString() : rest.left(slash);
}

QString MainWindow::latestConfigBackup(const QString &instanceId, const QString &path) const {
    return m_instanceEditorController->latestConfigBackup(instanceId, path);
}

void MainWindow::pruneConfigBackups(const QString &instanceId, int keep) {
    m_instanceEditorController->pruneConfigBackups(instanceId, keep);
}

void MainWindow::openConfigFile(InstanceSettingsDialog *dialog, const QString &path) {
    m_instanceEditorController->openConfigFile(dialog, path);
}

void MainWindow::saveConfigFile(InstanceSettingsDialog *dialog, const QString &path,
                                const QByteArray &bytes) {
    m_instanceEditorController->saveConfigFile(dialog, path, bytes);
}

void MainWindow::restoreConfigFile(InstanceSettingsDialog *dialog, const QString &path) {
    m_instanceEditorController->restoreConfigFile(dialog, path);
}

bool MainWindow::backupWorldLevelData(const QString &worldDir, QString *problemOut) {
    return m_instanceEditorController->backupWorldLevelData(worldDir, problemOut);
}

QString MainWindow::latestWorldBackup(const QString &worldDir) const {
    return m_instanceEditorController->latestWorldBackup(worldDir);
}

void MainWindow::showWorldSettingsPage(InstanceSettingsDialog *dialog, const QString &worldDir) {
    m_instanceEditorController->showWorldSettingsPage(dialog, worldDir);
}

void MainWindow::leaveWorldSettings(InstanceSettingsDialog *dialog) {
    m_instanceEditorController->leaveWorldSettings(dialog);
}

void MainWindow::saveWorldSettings(InstanceSettingsDialog *dialog) {
    m_instanceEditorController->saveWorldSettings(dialog);
}

void MainWindow::leaveConfigEditor(InstanceSettingsDialog *dialog) {
    m_instanceEditorController->leaveConfigEditor(dialog);
}

bool MainWindow::confirmDiscardConfigEdits() {
    return m_instanceEditorController->confirmDiscardConfigEdits();
}

bool MainWindow::confirmDiscardWorldEdits() {
    return m_instanceEditorController->confirmDiscardWorldEdits();
}

bool MainWindow::instanceHasMod(const QString &versionId, const QString &nameToken) const {
    // 判断（文件名切词、命中判据）在 `InstancePagesController::hasMod()` /
    // `jarNameHasToken()`。这个成员留着是因为它也有别的读者（常用目录那一列认 ReplayMod、
    // 自动归类那一带），它们拿的是主窗口。
    return InstancePagesController::hasMod(versionId, nameToken);
}

bool MainWindow::instanceShaderUsable(const QString &versionId) const {
    return InstancePagesController::shaderUsable(m_versionManager, versionId);
}

bool MainWindow::instanceModsUsable(const QString &versionId) const {
    return InstancePagesController::modsUsable(m_versionManager, versionId);
}

void MainWindow::loadServerList(InstanceSettingsDialog *dialog, const QString &versionId) const {
    if (dialog == nullptr) {
        return;
    }
    // 「文件不在盘上」（Missing）不算 problem：那一页会说「这个实例还没有加过服务器。」
    // —— 那正是实话。只有"文件在但是读不动"才把原因递过去。
    dialog->setServerList(serverListOf(versionId), serverListProblem(versionId));
}

QList<QPair<QString, QString>> MainWindow::serverListOf(const QString &versionId) const {
    QList<NbtUtil::ServerEntry> entries;
    QString problem;
    // 路径从 `ServerListFile::pathFor()` 来 —— 和写侧（改 / 加 / 删）**同一份实现**。
    NbtUtil::readServerList(ServerListFile::pathFor(versionId), &entries, &problem);
    QList<QPair<QString, QString>> servers;
    servers.reserve(entries.size());
    for (const NbtUtil::ServerEntry &entry : entries) {
        servers.append(qMakePair(entry.name, entry.address));
    }
    return servers;
}

QString MainWindow::serverListProblem(const QString &versionId) const {
    QList<NbtUtil::ServerEntry> entries;
    QString problem;
    NbtUtil::readServerList(ServerListFile::pathFor(versionId), &entries, &problem);
    return problem;
}

void MainWindow::pingServers(InstanceSettingsDialog *dialog, const QString &versionId) {
    m_serverPingController->ping(dialog, versionId);
}

bool MainWindow::toggleInstanceFile(const QString &path, QString *problem) {
    return m_instanceFileController->toggle(path, problem);
}

bool MainWindow::overlayAcceptsNewPage() const {
    // **只回答"空着没有"，一个字节都不往外弹**（用户 2026-09-29 原话见 .hpp 那段：
    // 「先关闭当前页面再继续」那一类情况现在一律**静默收掉**再继续）。
    OverlayHost *overlay = m_ui->overlay();
    return overlay == nullptr || !overlay->isOpen();
}

void MainWindow::runWhenOverlayFree(const std::function<void()> &open, int roundsLeft) {
    if (!open) {
        return;
    }
    OverlayHost *overlay = m_ui->overlay();
    if (overlay == nullptr || !overlay->isOpen()) {
        // 绝大多数时候走的是这一支：这一层本来就空着，**同一轮就把页开出来**（和从前
        // 一模一样 —— 那一页仍然卡在自己的嵌套事件循环里转到用户点返回）。
        open();
        return;
    }
    // ---------- 这一层被占着：先让位，再开 ----------
    //
    // 占着它的可能是**提示**，也可能是**整页**；整页那一档还可能是"收着的"（用户在别的
    // 选项卡上逛，而它一直占着这一位 —— 从前正是这一档逼着用户"回到那一颗选项卡、点返回
    // 箭头"）。用户 2026-09-29 定了方向：**静默收掉，直接继续**。
    if (roundsLeft <= 0 || !overlay->evict()) {
        // 收不掉（那一层不认这一发 / 轮数用完了）。**到这一步才出声** —— 老那两句话原样
        // 留着：挡住用户操作的事必须说清楚，而且得说清怎么办。
        warnOverlayBusy();
        return;
    }
    // ★ 收页那一发只是**请求**（见 OverlayHost::evict() 那段）：那一页正卡在自己的嵌套
    //   事件循环里，`backPressed()` 要等这一趟调用栈整个退出去、`execPage()` 醒过来，才
    //   真的把它收干净。∴ 排到下一轮，而且**下一轮再从头上过一遍**（那一轮里可能又有别的
    //   东西把这一层占上了）。
    QTimer::singleShot(0, this, [this, open, roundsLeft] {
        runWhenOverlayFree(open, roundsLeft - 1);
    });
}

void MainWindow::warnOverlayBusy() {
    OverlayHost *overlay = m_ui->overlay();
    if (overlay == nullptr) {
        return;
    }
    // 挡在这儿的是**已经开着的那一层**（一层只放得下一个页面，见 OverlayHost.hpp 那段）。
    // 它可能是"正铺在眼前"（用户点的是底栏那些没被盖住的按钮），也可能是"收着"。
    //
    // NOTE: **这一条不能不出声。** 在这之前这几个入口一个判据都没有：`execPage()` 拿到
    //       nullptr 直接返回 Rejected，对话框建了、一个字节没写、原地销毁 —— 用户点了实例
    //       行（或者组行那颗齿轮），屏幕上**什么都没发生**，也没有任何提示。
    //
    // NOTE: 2026-09-29 起这一句只在**收不掉**的时候才出现（正常那一档是静默收掉 + 直接
    //       继续，见 runWhenOverlayFree）—— 真走到这儿说明那一层卡在自己的嵌套循环里、
    //       连收页那一发都没认，这时候唯一能做的是把话说清楚。
    const QString title = overlay->pageTitle();
    // ★ 2026-09-27：标题**可能是空的** —— 这一层开着的若是**提示**（`promptOpen()`）而不是
    //   整页，pageTitle() 返回空串，照老写法那句正文会变成「「」那一页还开着」。
    if (title.isEmpty()) {
        InlineMessage::warning(overlay, tr("先关掉现在这一条提示"),
                               tr("现在有一条提示开着，先说清它或者按 Esc 关掉它，再开这个。"));
        return;
    }
    InlineMessage::warning(overlay, tr("先关掉现在这一页"),
                           tr("「%1」那一页还开着。切到别的选项卡时它会收起来，但它一直占着"
                              "这个位置 —— 顶上那四颗导航按钮一次只放得下一个页面。\n\n"
                              "回到「%1」那一颗选项卡，点左上角的「←」把它关掉，再回来开"
                              "这个。")
                               .arg(title));
}

bool MainWindow::deleteInstanceFile(const QString &path) {
    return m_instanceFileController->remove(path);
}

bool MainWindow::removeInstanceFileUnasked(const QString &path, QString *problem) {
    return m_instanceFileController->removeUnasked(path, problem);
}

// ---------- 文件管理那几页的批量按钮（2026-10-09）----------
//
// 入口全是那一页发出的**一批路径**（见 `InstanceFilesPage::selectionBar()`）。三颗按钮
// 各自一件：更新 / 禁用·启用一把 / 删除一把。**全选 / 反选 / 取消**三颗不在这儿 —— 它们
// 只动选中集，一个字节的盘都不碰，所以那边自己就办了。
//
// 【为什么「更新」是一批下，不是一条一条走 `installResourceVersion()`】
// 计划里写的是"逐条走"（顺带过一遍前置检查），**做不到**：`downloadResourceFiles()`
// 头一道闸就是 `downloadManager->isBusy()`，第二发会被直接拒掉 —— 那一圈里只有第一条真
// 下得动，剩下 N-1 条全是"已有下载任务正在进行"的失败框。理由写在头文件那段里。

void MainWindow::updateInstanceFiles(const QString &instanceId, const QString &pageKey,
                                     const QStringList &paths) {
    if (instanceId.isEmpty() || paths.isEmpty()) {
        return;
    }
    // 落点由**页键**算（`mods` / `datapacks` / …，唯一出处在 instanceFileDirs()）。
    //
    // ★ 2026-10-13（§7.1）：这一份现在只剩**"这一页认不认得"这一道校验** —— 真正往哪儿写
    //   由每条自己带（见下面 `ResourceFile::dir`）。`pageKey` 空 = 不是从某一页来的
    //   （更新中心那一批是跨类勾出来的），那时候没有可校验的东西，跳过这一道。
    const QString pageDir = instanceFileDirs(instanceId).value(pageKey);
    if (!pageKey.isEmpty() && pageDir.isEmpty()) {
        // 正常走不到：这套东西只在那四个"能下载的"页上开（见 InstanceFilesPage）——
        // 但"哪几页"这个判据在 ui/ 那一边，这边不能假设递进来的键一定认得。
        InlineMessage::warning(m_ui->overlay(), tr("更新失败"),
                               tr("不知道这一页的文件该下到哪儿去。"));
        return;
    }
    // 一趟检测的结果（实例 id → 路径 → 换成哪一版）。**没查过 = 空表** —— 那时候一条都
    // 拼不出文件清单，下面那句会拦住（见那一段）。
    const QHash<QString, ResourceCatalog::VersionEntry> targets =
        m_instanceUpdateTargets.value(instanceId);

    QList<VersionManager::ResourceFile> files;
    QStringList skipped;
    /// 和 `files` **一一对应**的那两条：实例内相对路径（回滚点要它）和盘上那个绝对路径
    /// （收尾时按文件名找回来用）。**不是 `paths`** —— 那里面还夹着被跳过的那几条。
    QStringList relativePaths;
    QStringList updatedPaths;
    const QString instanceDir = AppPaths::versionDir(instanceId);
    for (const QString &path : paths) {
        const auto it = targets.constFind(path);
        const ResourceCatalog::FileEntry *file =
            it == targets.constEnd() ? nullptr : it->primaryFile();
        if (file == nullptr || file->url.isEmpty() || file->filename.isEmpty()) {
            // 两种来路合成一句：①这一条没在上一趟检测的结果里（用户是先勾上再点更新、
            // 而那份结果里没有它 —— 比如他勾的是一条"说不清"的）；②那一版接口没给地址
            // （CurseForge 的作者关掉了第三方分发）。
            skipped.append(QFileInfo(path).fileName());
            continue;
        }
        // ★★ 2026-10-13（§7.1）：**就地换** —— 落回它自己那个目录、用它自己那个文件名。
        //
        // 从前是按**接口给的文件名**下到"这一页那个目录"里，而两家接口给的文件名常常和盘上
        // 那一个不一样（`sodium-fabric-0.6.13.jar` vs `sodium-0.6.13.jar`）⇒ 换完之后盘上
        // **多出一个 jar、旧的还在**，游戏同时加载两个同名模组那一类怪事就是从这儿来的，
        // 而且"按几次更新就多几份"。就地换之后"更新"这个名字才名副其实。
        // NOTE: 这一条也让回滚点那本账成立 —— 它记的是**盘上这个文件**（相对路径），
        //       换个名字的话还原回去的就不是原来那一个了。
        const QFileInfo info(path);
        VersionManager::ResourceFile resourceFile;
        resourceFile.url = file->url;
        resourceFile.filename = info.fileName();
        resourceFile.dir = info.absolutePath();
        resourceFile.sha1 = file->sha1;
        resourceFile.size = file->size;
        files.append(resourceFile);
        // `relativePaths` 是**相对实例目录**那一条（`mods/foo.jar`，不是 `foo.jar`）——
        // 回滚点照着它去实例里找回原文件（见 UpdateRollback::create）。写成光文件名的
        // 话，回滚点会以为"原来没有这个文件"，撤销那一下做的就是**删掉它**。
        relativePaths.append(QDir(instanceDir).relativeFilePath(path));
        updatedPaths.append(path);
    }
    if (files.isEmpty()) {
        // 一条都拼不出来：多半是**还没查过**（那一页上「可更新」的计数是 0、按钮本该是灰的，
        // 所以正常也走不到这儿）。说清下一步怎么办，别只是"没反应"。
        InlineMessage::warning(m_ui->overlay(), tr("更新失败"),
                               tr("这几条都没查到该换成哪一版。先在概览那一栏点一下「检查更新」，"
                                  "再点「更新中心」那一颗。"));
        return;
    }
    if (!skipped.isEmpty()) {
        // 只记账不弹框：这一批**已经开起来了**，再插一个框等于让用户白点一次。
        appendLog(tr("这几条查不到要换成哪一版，没有一起更新：%1").arg(skipped.join(QStringLiteral("、"))));
    }

    // ---------- 回滚点（§7.1：「更新前创建轻量回滚点，失败时可恢复原文件」）----------
    //
    // ★ **动手之前**摆好，摆不成就**整个停下来**（见 UpdateRollback::create 那段：用户是
    //   看着"能退回来"这句话才按下去的，抄不出来还硬更新比没有这个功能更糟）。
    //
    // NOTE: 相对路径就是 `files` 里那些**文件名**（四个"能下载的资源"目录都在实例目录的
    //       下一层，所以"这个文件在实例里的相对路径"就是它自己那个名字）—— 这一点由
    //       `instanceFileDirs()` 保证（那些目录都是 `<实例目录>/<目录名>`）。
    QString rollbackProblem;
    const UpdateRollback::Snapshot snapshot =
        UpdateRollback::create(instanceDir, instanceId, relativePaths, &rollbackProblem);
    if (!snapshot.isValid()) {
        InlineMessage::warning(m_ui->overlay(), tr("没有更新"),
                               tr("没能给这一批文件留一份回滚点，所以一个都没动。\n\n%1")
                                   .arg(rollbackProblem));
        appendLog(tr("更新「%1」没开始：回滚点没摆成（%2）。")
                      .arg(instanceDisplayName(instanceId), rollbackProblem));
        return;
    }
    appendLog(tr("更新前留了一份回滚点：%1 个文件（%2）。")
                  .arg(snapshot.fileCount())
                  .arg(InstanceFilesPage::formatSize(snapshot.totalBytes())));

    // ---------- 这一批的名字 + 任务账本 ----------
    //
    // 名字里带上数量 —— 收尾那句要能自己说清是哪一批。★ 这个名字**同时是**账本认这一批的
    // 钥匙（见 m_updateBatch 那段）：下载器回来的只有"文件名"，而"文件名 → 第几步"这层
    // 对应关系只有这一处知道。
    const QString label = tr("「%1」里的 %2 个更新")
                              .arg(instanceDisplayName(instanceId))
                              .arg(files.size());
    m_updateBatch = UpdateBatchRun{};
    m_updateBatch.label = label;
    m_updateBatch.instanceId = instanceId;
    // 一个资源一步（§7.1：「更新任务显示每个资源的结果，不只显示总数」）。
    QList<Tasks::TaskStep> steps;
    for (int i = 0; i < files.size(); ++i) {
        const QString name = files.at(i).filename;
        steps.append({name, Tasks::TaskStepState::Pending, QString()});
        // 文件名 → 步骤下标。★ 同名多条时按**先后**取（`takeFirst()`），所以这里 append
        // 的顺序就是它们下完报回来的顺序。
        m_updateBatch.pendingSteps[name].append(i);
        // 文件名 → 盘上那条的路径（成功之后要从「可更新」名单里摘的**就是它**）。
        m_updateBatch.pathByFileName.insert(name, updatedPaths.value(i));
    }
    m_updateBatch.task = beginTask(
        Tasks::TaskKind::Download, tr("更新「%1」的 %2 个资源")
                                       .arg(instanceDisplayName(instanceId))
                                       .arg(files.size()),
        instanceId, steps);
    m_activityCenter->tasks()->setProgress(m_updateBatch.task, 0, static_cast<qint64>(files.size()));

    appendLog(tr("正在更新 %1（%2）。")
                  .arg(label, QDir::toNativeSeparators(files.first().dir)));
    // `busyInstanceId` 给这个实例：正在往它的 `mods/` 里写文件（见 downloadResourceFiles
    // 里那段 —— 界面靠它把这一个实例从"能启动"里滤掉）。
    //
    // NOTE: 那个 `destinationDir` 形参现在只当**兜底**用（每条自己带 `dir`，见上面那段）；
    //       传第一条的目录 —— 它非空就行（那一处只判空）。
    if (!m_versionManager->downloadResourceFiles(files.first().dir, instanceId, label, files)) {
        // 没开起来（已经有别的批次在跑 / 那三套安装状态机占着）：账本里那一条**当场收尾**，
        // 别留一条永远"正在跑"的记录，回滚点也一起撤掉（一个字节都没动过，留着只会让
        // 更新中心上那颗「撤销上一次更新」指向一次没发生的更新）。
        m_activityCenter->tasks()->fail(m_updateBatch.task, tr("没能开始这一批更新。"));
        m_updateBatch = UpdateBatchRun{};
        UpdateRollback::discard(instanceId);
    }
    // NOTE: 这一批下完之后**不刷新那一页**。理由：换的是同一个文件名，列表上那一行
    //       一个字都不会变；而"可更新"那几颗计数在收尾时按**真换成功的**那几条摘掉
    //       （见 finishUpdateBatch）。
}

void MainWindow::toggleInstanceFiles(const QStringList &paths, bool disable) {
    m_instanceFileController->toggleMany(paths, disable);
}

void MainWindow::deleteInstanceFiles(const QStringList &paths) {
    m_instanceFileController->removeMany(paths);
}

bool MainWindow::copyInstanceTo(const QString &sourceId, const QString &destGameFolder,
                                const QString &newName) {
    return m_instanceTransferController->copyInstance(sourceId, destGameFolder, newName);
}

void MainWindow::copyInstanceToInBackground(const QString &sourceId,
                                            const QString &destGameFolder,
                                            const QString &newName) {
    m_instanceTransferController->copyInstanceInBackground(sourceId, destGameFolder, newName);
}

bool MainWindow::importWorldInto(const QString &instanceId, const QString &worldDir,
                                 const QString &name) {
    return m_instanceTransferController->importWorld(instanceId, worldDir, name);
}

bool MainWindow::renameInstanceWorld(const QString &instanceId, const QString &worldDir,
                                     const QString &newName) {
    return m_instanceTransferController->renameWorld(instanceId, worldDir, newName);
}

// ---------- 「文件管理」那几页的图标（2026-09-27）----------
//
// 入口由 InstanceDialogController 调用；扫描状态、缓存和资源映射归 InstanceIconController。

void MainWindow::requestInstanceFileIcons(InstanceSettingsDialog *dialog, const QString &versionId) {
    m_instanceIconController->requestInstanceFileIcons(dialog, versionId);
}

void MainWindow::pushInstanceFileUpdates(InstanceSettingsDialog *dialog,
                                         const QString &instanceId) {
    if (dialog == nullptr) {
        // 这一页已经收掉了（或者压根没开着）——那就不递，盘上那份账照样记着
        //（下一次开这一页时由开页那条路补上，见调用点）。
        return;
    }
    // 键在不在 = **查过没有**，值是查出来可更新的那些路径。
    //
    // ★ 这一份账（`m_instanceOutdatedPaths`）是**检查更新**那一趟的，不是图标那一套 ——
    //   所以这一下留在这一层（2026-10-15 从 InstanceIconController 挪回来的）。
    const auto it = m_instanceOutdatedPaths.constFind(instanceId);
    const bool checked = it != m_instanceOutdatedPaths.constEnd();
    dialog->setFileUpdates(checked ? it.value() : QStringList(), checked);
}

bool MainWindow::applyInstanceSettings(const QString &versionId,
                                      const InstanceSettingsDialog &dialog) {
    // ★ 2026-10-15：那一整套（补稳定 id → 搬归属键 → 拼字段 → 图标那几个字节 → 写 meta →
    //   判要不要重扫树 → 分组/置顶那三条规则）搬进了 `InstanceSettingsController::apply()`。
    //   这一层剩下的是**只有它能做的那一半**：把对话框现读成一份 `Edit`（那些值），以及
    //   回答"它老家本来在哪一组"（`homeGroupIdOf()`，显示层那一套）。
    //
    // NOTE: 这个适配器只能留在这一层 —— `InstanceSettingsDialog` 是 `ui/` 的类型，而
    //       controller 那一层不许 include `ui/`。所以"能验得了"这件事靠的是把 `Edit`
    //       切出来：`applyInstanceSettings()` 收对话框引用、验证工程驱动不了它，而
    //       `apply()` 收的是纯值，验得了。
    const InstanceSettingsDialog::Override override = dialog.override();

    InstanceSettingsController::Edit edit;
    edit.java = toSettingsOverride(override);
    edit.extraJvmArgs = dialog.appendJvmArgs();
    edit.window = toWindowOverride(override);
    edit.warnJavaMismatch = dialog.warnJavaMismatch();
    edit.iconChoice = dialog.iconChoice();
    edit.pickedIconPath = dialog.pickedIconPath();
    // 这个常量住在 ui/ 那一侧（它是对话框上那一格显示成「本地那张」的判据）⇒ 从这里填进去，
    // controller 那边**不抄一份**。
    edit.localIconValue = InstanceSettingsDialog::localIconValue();
    edit.pageChoices = dialog.pageChoices();
    edit.pinned = dialog.pinned();
    edit.chosenGroupId = dialog.chosenGroupId();
    // "老家"要按版本分类现算 —— 那一位住在这一层（`homeGroupIdOf()`，实例树上那一行画在
    // 哪个组里用的也是它）。
    edit.homeGroupAtOpen =
        homeGroupIdOf(m_groupStore, instanceKey(versionId),
                      m_versionManager->displayParts(versionId), m_versionManager,
                      knownGroupIdsOf(m_groupStore));

    return m_instanceSettings->apply(versionId, edit);
}

/// 把一个参数按 cmd.exe 的规矩包起来（「导出启动脚本」那一串参数要用它）。
///
/// 两条规矩，都得照做：
///   - `%` 在批处理里是**变量展开**（`%PATH%`），而且写在双引号里**照样展开** ⇒ 一律写 `%%`；
///   - 含空格/制表符、空串、或者带 `&|<>()^"` 的参数要加双引号（不加的话那个参数会被 cmd
///     拆成好几段，或者整个消失）。
///
/// NOTE: 参数里真出现一个 `"` 是**没救**的 —— cmd 没有"引号里的引号"这种写法。这条路上
///       的参数来自版本 json 和用户设置（路径、UUID、玩家名），到不了那一步；真到了也只是
///       那一个参数被拆开，脚本里看得见，不是静默错。
/// NOTE: 故意**不**动 `!`：它只在"开了延迟展开"的 cmd 里有意义（脚本自己 `@echo off`，
///       默认就没开），而按延迟展开去转义（`^^!`）会让正常那一档凭空多出一个 `^`。
static QString batchQuote(const QString &arg) {
    QString escaped = arg;
    escaped.replace(QLatin1Char('%'), QStringLiteral("%%"));
    static const QString kSpecial = QStringLiteral(" \t&|<>()^\"");
    bool needsQuotes = escaped.isEmpty();
    for (const QChar &ch : escaped) {
        if (kSpecial.contains(ch)) {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return escaped;
    }
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

// ============================ 日志窗那两颗导出 ============================
//
// 用户 2026-09-28：「把日志和错误报告导出功能加上，就在那个日志窗口下方，右下角放着」。
// 三件事的分工：**界面**（两颗键、长什么样）在 ui/GameLogWindow；**文本怎么拼**（头部、
// 分段）在 util/ErrorReport；**这一片**是接线 —— 弹保存框、写盘、说结果。
//
// 为什么保存框在这一层而不是窗那一层：`ui/` 不认识数据目录（那一层的规矩，见
// GameLogWindow 的文件头），而"默认存哪儿"是个和数据目录一样属于这一层的问题。

namespace {

/// 导出那两份文件时保存框默认落在哪儿：桌面。
///
/// 退路是"我的文档"，再拿不到就给个空串（空串 = 交给保存框自己决定用上次那个地方）——
/// 比硬编一个绝对路径强：那台机器上不一定有那个目录。
QString exportDefaultDirectory() {
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty()) {
        return desktop;
    }
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

/**
 * @brief 一段文本数出来是几行。**两份导出文件共用这一处**（报告头部的"共 N 行"读它）。
 *
 * NOTE: 末尾那半行（最后没有换行的那截）也算一行 —— 它和别的行一样占着屏幕上的一行，
 *       不数它的话报告里那个数字会比用户眼睛数出来的少一个。
 */
int lineCountOf(const QString &text) {
    if (text.isEmpty()) {
        return 0;
    }
    int count = text.count(QLatin1Char('\n'));
    if (!text.endsWith(QLatin1Char('\n'))) {
        ++count;
    }
    return count;
}

/**
 * @brief 把一段文本写进那个文件。
 * @return 空串 = 写成功了；否则是给用户看的原因（原样来自系统，已经能读懂）。
 *
 * QSaveFile：写不下去时**不会留下一个半截的文件** —— 导出的日志/报告是用户要拿去问人
 * 的东西，一个半截的文件发出去比没有更坏（和导出启动脚本那边同一个理由）。
 */
QString writeExportFile(const QString &path, const QString &text) {
    // 换行写成 CRLF：这两份文件的主要用途是在 Windows 上双击打开（记事本）——
    // 纯 LF 在旧一点的记事本里是一整坨。**不改内存里那一份**，只改写出去的那一份。
    QString body = text;
    body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    body.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    const QByteArray bytes = body.toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return file.errorString();
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        return file.errorString();
    }
    return QString();
}

}  // namespace

// ========================= 游戏异常退出（coding plan §4.3） =========================
//
// 用户报的是「游戏崩了，除了一个不会自己关的日志窗，什么也没有」。§4.3 要的是把这件事
// **记成账本上的一项**：底栏那一格亮一颗红点、任务中心里看得到、点进去有「打开日志」
// 「导出错误报告」「打开崩溃报告目录」「复制摘要」四件事可做。
//
// 三层的分工（要改先看这一段）：
//   - **那几句话怎么得出来**全在 `src/diag/GameExitDiagnosis`（纯函数、规则一张表，
//     verify 能逐格念一遍）；
//   - **这一份事实怎么留住**在 `GameSessionController` 那一层（2026-10-15 连那几本账一起
//     搬走了）—— 会话一结束 `LauncherManager` 那份记录就没了，启动事实也在同一趟里清掉，
//     而用户按「导出错误报告」可能是很久以后；这一层只把它摆成 `CrashReportController`
//     要的那一份；
//   - **写盘 / 开目录**也在这一层（`ui/` 不认识数据目录，那是那一层的规矩）。

void MainWindow::reportGameCrash(int sessionId, int exitCode, bool crashed) {
    // 这一趟的启动事实和那份日志都**在会话还活着的时候**留在了控制器那边（会话一结束，
    // LauncherManager 那份记录和日志窗里的两段日志就都没了），这里只把它们摆成
    // `CrashReportController` 要的那一份。
    const GameSessionController::GameSessionFacts launch =
        m_gameSessionController->sessionFacts(sessionId);
    CrashReportController::LaunchFacts facts;
    facts.instanceId = launch.instanceId;
    facts.instanceName = launch.instanceName;
    facts.accountName = launch.accountName;
    facts.processId = launch.processId;
    facts.startedAtMs = launch.startedAtMs;
    facts.requiredJavaMajor = launch.requiredJavaMajor;
    facts.javaPath = launch.javaPath;
    facts.xmxMb = launch.xmxMb;
    facts.xmsMb = launch.xmsMb;
    facts.loaderName = launch.loaderName;
    facts.loaderVersion = launch.loaderVersion;
    QString errors;
    QString all;
    if (const GameLogWindow *window = m_gameSessionController->gameLogWindow(sessionId)) {
        errors = window->logTextFrom(GameLogWindow::LogLevel::Error);
        all = window->allLogText();
    }
    m_crashReports->report(sessionId, exitCode, crashed, facts, errors, all);
}

void MainWindow::exportCrashReport(const QString &key) {
    m_crashReports->exportReport(key);
}

void MainWindow::openCrashReportsDir(const QString &key) {
    Q_UNUSED(key);
    m_crashReports->openReportsDirectory();
}
void MainWindow::exportGameLog(int sessionId) {
    GameLogWindow *window = m_gameSessionController->gameLogWindow(sessionId);
    if (window == nullptr) {
        return;  // 走不到：这两颗键就长在那个窗上
    }
    const QDateTime now = QDateTime::currentDateTime();
    // ★ 2026-10-19：窗口是**带 WA_DeleteOnClose 的**（见 `GameSessionController.hpp` 的
    //   说明），而下面那个保存框是一层嵌套事件循环 —— 这期间游戏可能正好以 0 退出，
    //   `autoCloseOnExit()` 会把这一扇 close() 掉、随之销毁。所以：要读的东西**在弹框
    //   之前**读好（那一份日志也正好是用户点这颗键那一刻看到的），框回来之后只用
    //   `QPointer` 判一下还能不能写回那行"已导出"。
    const QString instanceName = window->instanceName();
    const QString logText = window->allLogText();
    QPointer<GameLogWindow> windowGuard(window);
    const QString suggested =
        QDir(exportDefaultDirectory())
            .filePath(ErrorReport::suggestedLogFileName(instanceName, now));
    // 保存框**挂在那个日志窗上**（不是主窗）：它是用户刚点过的那扇窗，弹出来的框该压着它，
    // 而不是被它盖住（日志窗没有父窗，是独立的顶层窗口，见它的文件头）。
    // NOTE: 不用 `DontUseNativeDialog` —— 这一颗是**导出到哪儿**，用户要的是那个熟悉的
    //       Windows 框（四处 QFileDialog 里另外那几处也是这么留的，见 setupConnections 里
    //       那一段说明）。
    const QString path = QFileDialog::getSaveFileName(
        window, tr("导出日志"), suggested,
        tr("文本文件 (*.log *.txt);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;  // 用户取消
    }
    const QString writeError = writeExportFile(path, logText);
    if (!writeError.isEmpty()) {
        appendLog(tr("导出日志失败：%1").arg(writeError));
        // 错误要弹窗（这一条是产品的硬约束）—— 弹的是启动器那一层，日志窗这时还在旁边
        // 摆着，用户回头看得到。
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
                               tr("这个文件没能写出来（%1）：\n%2")
                                   .arg(QDir::toNativeSeparators(path), writeError));
        return;
    }
    const QString shown = QDir::toNativeSeparators(path);
    appendLog(tr("✓ 日志已导出：%1").arg(shown));
    // 成功那一下**写回那个日志窗**：用户就在那儿点的那颗键，眼睛也在那儿 —— 弹一个框
    // 还要他再点一次"知道了"，而这件事没有需要他决定的东西。写法跟着窗里已有的那几行
    // 自己补的话（"[游戏已退出，退出码：0]"），一样带方括号。
    // 窗在保存框开着的时候被自动关掉的话就不写了（文件已经导出成功，主日志里也有记录）。
    if (!windowGuard.isNull()) {
        windowGuard->appendText(tr("\n[日志已导出：%1]\n").arg(shown));
    }
}

void MainWindow::exportGameErrorReport(int sessionId) {
    GameLogWindow *window = m_gameSessionController->gameLogWindow(sessionId);
    if (window == nullptr) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    // 同上：窗带 WA_DeleteOnClose，保存框那一层嵌套循环里游戏可能以 0 退出、窗跟着没了。
    // 报告正文全在弹框之前读好；框回来之后只用 `QPointer` 决定要不要写回那行"已导出"。
    QPointer<GameLogWindow> windowGuard(window);

    // 事实从**快照**里取（理由见 GameSessionController::GameLogFacts 的说明：会话一结束，
    // 那份记录就查不到了，而"崩溃之后导一份报告"正是这件事最主要的用法）。
    const GameSessionController::GameLogFacts saved = m_gameSessionController->logFacts(sessionId);
    ErrorReport::Facts facts;
    facts.launcherVersion = QCoreApplication::applicationVersion();
    facts.instanceName = window->instanceName();
    facts.instanceId = saved.instanceId;
    facts.accountName = saved.accountName;
    facts.systemDescription = ErrorReport::systemDescription();
    facts.processId = saved.processId;
    facts.startedAt = saved.startedAtMs > 0 ? QDateTime::fromMSecsSinceEpoch(saved.startedAtMs)
                                            : QDateTime();
    facts.finished = saved.finished;
    facts.crashed = saved.crashed;
    facts.exitCode = saved.exitCode;

    // 两段正文都从窗里读**它自己那一份模型**：错误摘录走 `logTextFrom(Error)`（哪几档算
    // 错误只有那边一个出处），完整那份走 `allLogText()`（屏幕上筛掉的行也在里面 ——
    // 用户勾着 ERROR 的时候导出，报告里照样要是完整日志）。
    const QString errors = window->logTextFrom(GameLogWindow::LogLevel::Error);
    const QString log = window->allLogText();
    facts.errorLines = lineCountOf(errors);
    facts.totalLines = lineCountOf(log);

    const QString suggested =
        QDir(exportDefaultDirectory())
            .filePath(ErrorReport::suggestedFileName(window->instanceName(), now));
    const QString path = QFileDialog::getSaveFileName(
        window, tr("导出错误报告"), suggested,
        tr("文本文件 (*.txt);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    const QString writeError =
        writeExportFile(path, ErrorReport::text(facts, now, errors, log));
    if (!writeError.isEmpty()) {
        appendLog(tr("导出错误报告失败：%1").arg(writeError));
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
                               tr("这个文件没能写出来（%1）：\n%2")
                                   .arg(QDir::toNativeSeparators(path), writeError));
        return;
    }
    const QString shown = QDir::toNativeSeparators(path);
    appendLog(tr("✓ 错误报告已导出：%1").arg(shown));
    if (!windowGuard.isNull()) {
        windowGuard->appendText(tr("\n[错误报告已导出：%1]\n").arg(shown));
    }
}

void MainWindow::exportLaunchScript(const QString &versionId, const QString &path) {
    if (versionId.isEmpty() || path.isEmpty()) {
        // 产品里走不到（对话框那边是"问出来的路径非空才算数"）。验证工程打一条空路径进来时
        // 什么都不做，比去写一个叫 "" 的文件强。
        return;
    }

    const QJsonObject versionJson = m_versionManager->getVersionJson(versionId);
    if (versionJson.isEmpty()) {
        appendLog(tr("导出启动脚本失败：找不到版本 %1 的 version.json").arg(versionId));
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
                               tr("找不到版本 %1 的 version.json，导出来的脚本跑不起来。")
                                   .arg(versionId));
        return;
    }

    // 身份照**启动那条路**判（唯一出处 offlineIdentity()），令牌也一样要换 —— 这一份脚本
    // 要能直接跑起来，就得带着和启动那一刻同一个身份。
    const bool offlineMode = offlineIdentity();
    QString token;
    if (!offlineMode) {
        QString tokenError;
        // 这一段是同步的（要换令牌时它要在 UI 线程上发请求，单次超时 15 秒）—— 和启动一样
        // 先给一句话再进去。
        enterSyncWait(tr("正在校验登录状态..."));
        token = m_authManager->loadSavedToken(&tokenError);
        leaveSyncWait();
        if (token.isEmpty()) {
            // 导一份没有令牌的脚本 = 导一份起不来的脚本，所以这一档**不导**，说清为什么。
            appendLog(tr("导出启动脚本失败：%1").arg(tokenError));
            InlineMessage::warning(
                m_ui->overlay(), tr("导出失败"),
                tokenError.isEmpty()
                    ? tr("导出启动脚本要先能拿到登录令牌。请先登录 Microsoft 账户，"
                         "或者在「账号与外观…」里添加一个离线账号。")
                    : tr("%1\n\n导出启动脚本要先能拿到登录令牌，请重新登录后再试。")
                          .arg(tokenError));
            return;
        }
    }

    // 这次要用的那一套 Java 值：和启动一样合成（全局 + 这个实例的覆盖），自动档的内存也在
    // 这一步按**这一刻**的机器状态定下来。
    // NOTE: 这里**不挑 Java、也不下运行时**（ensureJavaForVersion 那一整套）：用户点的是
    //       "导出这个文件"，顺手在后台下几百 MB 的运行时不是他要的。路径空着就写裸 `java`，
    //       和 LauncherManager 那边"从 PATH 找"是同一个兜底。
    const JavaSettingsOverride override =
        javaOverrideFromJson(m_versionManager->versionMeta(versionId));
    EffectiveJavaSettings javaSettings =
        resolveJavaSettings(m_javaSettings->effective(), override);
    resolveMemoryForLaunch(versionId, javaSettings, override);

    const QString program = javaSettings.javaPath.isEmpty()
                                ? QStringLiteral("java")
                                : QDir::toNativeSeparators(javaSettings.javaPath);
    const QStringList args =
        launchArguments(versionId, versionJson, javaSettings, token, offlineMode);

    // ---------- 拼那个 .bat ----------
    //
    // 编码写**本地 8 位**，不写 UTF-8 + `chcp 65001`：cmd 是按当前代码页读文件的，而"双击
    // 一个 .bat"时先跑起来的正是 cmd 自己 —— 代码页要等它读到那一行 `chcp` 才能改，在那之前
    // 的字节已经按旧代码页解过了。写本地代码页则两边天然一致。
    //
    // 第一句必须是 `cd /d`：参数里 classpath 那几段是**相对 <数据目录>** 写的（启动器那边
    // 靠 QProcess 的 setWorkingDirectory(minecraftDir) 兜住），脚本不在那个目录下跑就找不到
    // jar。`/d` 是连盘符一起换 —— 数据目录在 E 盘、脚本导到 C 盘桌面上是最常见的情形。
    QString script;
    // ★ 2026-10-17：注释也要按**本地 8 位**写得出来。英文版 Windows 的代码页装不下汉字，
    //   `toLocal8Bit()` 会把它们变成 '?' —— 那一行就成了一句问号。注释是给人看的，宁可
    //   换一种写得出来的语言，也不要写一串乱码（CI 的英文 runner 上就是这么红的）。
    //   参数里那种非 ASCII 没法替（那是用户的路径/名字），只能照实写。
    const auto localOrAscii = [](const QString &local, const QString &ascii) {
        return QString::fromLocal8Bit(local.toLocal8Bit()) == local ? local : ascii;
    };
    script += QStringLiteral("@echo off\r\n");
    script += localOrAscii(
        QStringLiteral("rem 由 WTFML 导出：按这个实例现在的参数启动。\r\n"),
        QStringLiteral("rem Exported by WTFML: starts with this instance's current arguments.\r\n"));
    if (!offlineMode) {
        script += localOrAscii(
            QStringLiteral("rem 这个脚本里带着你的登录令牌，别发给别人。\r\n"),
            QStringLiteral("rem This script contains your login token. Do not share it.\r\n"));
    }
    script += QStringLiteral("cd /d ") +
              batchQuote(QDir::toNativeSeparators(AppPaths::minecraftDir())) +
              QStringLiteral("\r\n");
    script += batchQuote(program);
    for (const QString &arg : args) {
        script += QLatin1Char(' ') + batchQuote(arg);
    }
    script += QStringLiteral("\r\n");

    // QSaveFile：写不下去时**不会留下一个半截的 .bat**（那个东西双击起来的行为是随机的，
    // 而用户会以为"导出的就是用不了的"）。和写版本 json 那边同一个理由。
    const QByteArray bytes = script.toLocal8Bit();
    QSaveFile file(path);
    bool written = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    if (written) {
        written = file.write(bytes) == bytes.size() && file.commit();
    }
    if (!written) {
        QString writeError = file.errorString();
        if (writeError.isEmpty()) {
            writeError = tr("系统没给出原因。");
        }
        appendLog(tr("导出启动脚本失败：%1").arg(writeError));
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
                               tr("这个文件没能写出来（%1）：\n%2")
                                   .arg(QDir::toNativeSeparators(path), writeError));
        return;
    }

    const QString shown = QDir::toNativeSeparators(path);
    appendLog(tr("✓ 启动脚本已导出：%1").arg(shown));
    QStringList notes;
    notes.append(offlineMode ? tr("这一份用的是离线身份（游戏里就是这个名字）。")
                             : tr("★ 这份脚本里带着你的登录令牌：别发给别人，也别传网盘。"));
    if (javaSettings.javaPath.isEmpty()) {
        notes.append(tr("脚本里写的是 PATH 上的 java（这个实例还没挑过 Java）。"));
    }
    notes.append(tr("参数和启动器这次启动用的那一份是同一份；要改就改完再导一遍。"));
    InlineMessage::information(
        m_ui->overlay(), tr("导出完成"),
        tr("已经写到：\n%1\n\n%2").arg(shown, notes.join(QStringLiteral("\n"))));
}

void MainWindow::exportModpack(const QString &versionId, const QString &targetPath) {
    m_modpackExportController->exportPack(versionId, targetPath);
}

void MainWindow::repairInstanceFiles(const QString &versionId) {
    if (versionId.isEmpty()) {
        return;
    }

    // 三道守卫，各说各的：用户点的是"检查这个实例"，说清"现在为什么不行"比一句笼统的
    // "忙"有用。第一道看的是**这个实例自己**（不等于"有下载在跑"——别的实例在装不该拦它，
    // 但这里点的是补全，而补全要开一批下载，所以第二道照样得拦）。
    const bool thisInstanceBusy = versionId == m_installingInstanceId ||
                                  versionId == m_importingInstanceId ||
                                  versionId == m_repairingInstanceId;
    if (thisInstanceBusy) {
        InlineMessage::information(
            m_ui->overlay(), tr("现在做不了"),
            tr("「%1」这会儿正在装（或者正在导入 / 正在检查）。等它完了再来。")
                .arg(instanceDisplayName(versionId)));
        return;
    }
    if (m_downloadInFlight || m_versionManager->isDownloading()) {
        InlineMessage::information(
            m_ui->overlay(), tr("现在做不了"),
            tr("已经有一个下载或者安装任务在跑了，等它结束再检查这个实例。"));
        return;
    }

    // 读不出 json / 继承链不完整：**先说清楚**，别把忙态置上再让数据层回一句内部话。
    // 尤其继承链那一条 —— "缺文件"和"父版本整个没装"是两件事，后者补不出来。
    const QString inheritanceProblem = m_versionManager->inheritanceProblem(versionId);
    if (!inheritanceProblem.isEmpty()) {
        appendLog(tr("无法校验 %1 的文件：版本继承链不完整").arg(versionId));
        InlineMessage::warning(m_ui->overlay(), tr("版本不完整"), inheritanceProblem);
        return;
    }
    const QJsonObject versionJson = m_versionManager->getVersionJson(versionId);
    if (versionJson.isEmpty()) {
        appendLog(tr("无法校验 %1 的文件：找不到 version.json").arg(versionId));
        InlineMessage::warning(
            m_ui->overlay(), tr("版本信息缺失"),
            tr("找不到版本 %1 的 version.json，没法知道它要哪些文件。").arg(versionId));
        return;
    }

    // 不弹一次"确定吗"：这颗按钮自己就是那一下确认（它写着"开始检查…"，下面那句说明也把
    // 要干的事说全了），再问一遍只是让用户多点一次。真的会花时间的地方在底栏那根进度条上。
    //
    // ★ 体检点过名的那几条要一起交下去（见 VersionManager::repairVersionFiles 的 @param）：
    //   体检说"这个资源对象是坏的"时，补全那一侧的判据（只问在不在）看不见它，会跳过 ——
    //   两份界面就互相打脸。这个实例**最近一趟**体检的清单从控制台那一层拿（体检的是别的
    //   实例、或者压根没体检过时是空表，那就是老行为）。修完照旧留着：报告是"最近一趟"的
    //   快照，再点一次补全照样会把这几条重下一遍（无害，它们本来就是坏的那几条）。
    const QStringList flagged = m_integrityController->flaggedPaths(versionId);
    appendLog(tr("开始校验「%1」的文件（已存在的会跳过）...").arg(instanceDisplayName(versionId)));
    // 顺序：**先记名字再置忙**。setDownloadUiBusy() 里那句 refreshInstalledVersions() 读的
    // 就是这个判据（正在补全的实例不该出现在启动列表里），反过来的话它漏得出那一次刷新。
    m_repairingInstanceId = versionId;
    setDownloadUiBusy(true);
    m_versionManager->repairVersionFiles(versionId, flagged);
}

// ---------------------------------------------------------------------------
// 完整性检查与修复（coding plan §4.1，2026-10-12）
// ---------------------------------------------------------------------------

bool MainWindow::integrityScanningVersion(const QString &versionId) const {
    return m_integrityController->scanning(versionId);
}

void MainWindow::submitIntegrityCheck(const QString &versionId) {
    IntegrityController::BusyFacts busy;
    busy.targetBeingModified = versionId == m_installingInstanceId
                               || versionId == m_importingInstanceId
                               || versionId == m_repairingInstanceId;
    busy.anotherDownloadRunning = m_downloadInFlight || m_versionManager->isDownloading();
    m_integrityController->submit(versionId, busy);
}

void MainWindow::onIntegrityScanProgress(int checked, int total, int groupIndex) {
    m_integrityController->progress(checked, total, groupIndex);
}

void MainWindow::onIntegrityScanFinished(const VersionIntegrity::Report &report) {
    m_integrityController->finished(report);
}

IntegrityReportPage::Report MainWindow::integrityReportForPage(
    const VersionIntegrity::Report &report, const QString &instanceName) const {
    IntegrityReportPage::Report out;
    out.instanceName = instanceName;
    out.summary = report.summary();
    out.error = report.error;
    for (VersionIntegrity::Group group : VersionIntegrity::allGroups()) {
        IntegrityReportPage::Group item;
        item.name = VersionIntegrity::groupName(group);
        item.missing = report.missingIn(group);
        item.corrupt = report.corruptIn(group);
        for (const VersionIntegrity::Verdict &v : report.problemsInGroup(group))
            item.rows.append({v.label, v.detail(), v.path});
        out.groups.append(item);
    }
    return out;
}

void MainWindow::openIntegrityReportPage(const QString &versionId) {
    m_integrityController->openReport(versionId);
}

void MainWindow::onIntegrityReportAction(IntegrityReportPage::Action action) {
    m_integrityController->reportAction(int(action));
}

void MainWindow::copyIntegrityReport() {
    m_integrityController->copyReport();
}
// ===========================================================================
// 世界与实例备份（coding plan §4.4，2026-10-13）
// ===========================================================================
//
// 四个入口（世界页三颗 + 概览那两颗）都收在这一段里。**干活的那一层在
// `src/manage/BackupFiles`**（走目录、算 sha1、起 PowerShell 打包、还原之前做安全快照），
// 这一段只负责：把描述拼出来、起一趟后台活、把结果记进任务账本并弹给用户。

/// 一份备份有多大（正文里那句"共 N MB"）。
///
/// ★ **自己算**，不用 `QLocale::formattedDataSize()`：那个的写法跟着系统语言走
///   （"1.5 MiB" / "1,5 MB"），而这几句要在判据里逐字断言 —— 跟着区域设置变的字符串
///   没法钉。
static QString backupSizeText(qint64 bytes) {
    if (bytes >= 1024LL * 1024 * 1024) {
        return QStringLiteral("%1 GB").arg(QString::number(double(bytes) / (1024.0 * 1024 * 1024), 'f', 2));
    }
    if (bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(QString::number(double(bytes) / (1024.0 * 1024), 'f', 1));
    }
    return QStringLiteral("%1 KB").arg(QString::number(double(bytes) / 1024.0, 'f', 1));
}

void MainWindow::openBackupFolder(BackupFiles::Kind kind) {
    m_backupController->openFolder(kind);
}

void MainWindow::backupWorldNow(const QString &worldDir) {
    m_backupController->backupWorld(worldDir);
}

void MainWindow::restoreWorldBackup(const QString &worldDir) {
    m_backupController->restoreWorld(worldDir);
}

void MainWindow::snapshotInstance(const QString &versionId) {
    m_backupController->snapshotInstance(versionId);
}

void MainWindow::restoreInstanceSnapshot(const QString &versionId,
                                         InstanceSettingsDialog *dialog) {
    m_backupController->restoreInstance(versionId, dialog);
}

Tasks::TaskId MainWindow::startBackupRestore(const QString &zipPath, const QString &targetParentDir,
                                            const QString &title, const QString &targetId,
                                            const QString &successTitle) {
    // ★★ 2026-10-15：这一发**一个界面调用点都没有**（界面上那两条路是上面那两个：世界页
    //    「恢复备份…」和概览「从快照恢复…」，zip 和落点由它们自己决定）。留着它只为一件事：
    //    **mw_check 是照着名字驱动它的**（`QMetaObject::invokeMethod(&w, "startBackupRestore",
    //    …)`，T35.3 / T35.4 那一节拿它验"还原那一趟跑完了""别人的包 ⇒ 账本上是失败"）。
    //
    // ★ 这一条正是"搬功能之前先想清楚谁还按名字喊它"：d2717c9 拆分时它跟着旧实现一起没了，
    //   而那三处调用**不会报错** —— invokeMethod 找不到就返回 false，于是那七条断言静默
    //   失效，屏幕上看着像"还原功能坏了"。挪走实现的时候留一个同名的薄壳，是这一层和
    //   验证工程之间唯一的那条约定（见 tests/mw_check 那一节的说明）。
    return m_backupController->startRestore(zipPath, targetParentDir, title, targetId, successTitle);
}

MainWindow::JavaMismatchAnswer MainWindow::askAboutJavaMismatch(
    const QString &versionId, const JavaSettingsOverride &override, int expectedMajor) {
    // ① 只有"实例钉了 Java"才有这个问题。全局档不匹配时 ensureJavaForVersion() 会
    //    静默换一个匹配的 —— 那是有意的老行为（用户没钉任何东西，替他挑一个正确的
    //    运行时不叫"换掉他的选择"），不动它。
    if (!override.javaPath.has_value()) {
        return JavaMismatchAnswer::NotAsked;
    }

    // ② 全局总闸关着，③ 或者这个实例表过态"以后别再问了"。
    //
    // 两条的语义**一样**，都是"别问了，照他钉的那个启动" —— 返回 KeepInstanceJava
    // 正是那个意思（它唯一的作用就是把 allowMismatchedPath 打开）。这里不能返回
    // NotAsked：那条路会落到手动档的硬错误上（路径不匹配就 fail()），也就是说，
    // 用户关了提醒之后反而**再也启动不了**这个实例 —— 一个纯负作用的开关。
    if (!m_javaSettings->warnVersionMismatch() ||
        m_versionManager->skipJavaVersionWarning(versionId)) {
        return JavaMismatchAnswer::KeepInstanceJava;
    }

    // ④ 那条路径到底是不是另一个主版本。探不出主版本（-1）时**不问** —— 那是"路径
    //    不存在/认不出来"，和"版本不符"是两件事，它继续落到现有的「Java 运行时缺失」
    //    硬错误那条路上。主版本相同当然更不用问。
    QString actualVersionText;
    const int actualMajor =
        GlobalJavaSettings::probeJavaMajorVersion(*override.javaPath, &actualVersionText);
    if (actualMajor <= 0 || actualMajor == expectedMajor) {
        return JavaMismatchAnswer::NotAsked;
    }

    // 从"另开一个窗"改成"内容区中间那一块"（用户点名的「像 java 设置这种小弹窗」）。
    // 关掉 / 按 Esc / 点外面都**不动 `choice`**，它的初值就是 Abort —— 和原来
    // `dialog.exec()` 被关掉时是同一个结果（见那个对话框的 .cpp 末尾）。
    JavaMismatchDialog dialog(instanceDisplayName(versionId), expectedMajor, actualMajor,
                              actualVersionText, *override.javaPath);
    m_ui->overlay()->execPrompt(&dialog, false);
    const JavaMismatchDialog::Choice choice = dialog.choice();

    if (choice == JavaMismatchDialog::Choice::Abort) {
        appendLog(tr("已取消启动：「%1」指定的是 Java %2，而这个版本要求 Java %3。")
                      .arg(instanceDisplayName(versionId))
                      .arg(actualMajor)
                      .arg(expectedMajor));
        return JavaMismatchAnswer::AbortLaunch;
    }

    // 「以后不再提醒这个实例」是用户刚点下的一下**表态**，立刻落盘才对得上他的意思
    // （等他下次想起来去「实例设置…」里关，那个框会一直问下去）。
    //
    // NOTE: 写失败**只记日志、不拦启动**。这是全程唯一一处"错误不弹窗"：存不下一个
    //       偏好和"这次启动能不能跑"是两件事，不该因为它把用户刚授权的启动掐掉；
    //       而它自己的后果（下次还问一句）是无害的、也是看得见的。
    //
    // NOTE: 勾选框是**独立于选了哪条路**的一句表态（它的字面意思就是这样），所以
    //       「改用匹配的 Java」那一次也照写不误 —— 要是他后来又给这个实例钉了个
    //       对不上的 Java，"别问了"这句话仍然算数。
    if (dialog.dontRemindAgain()) {
        QString error;
        if (!m_versionManager->updateVersionMeta(
                versionId, {{VersionManager::skipJavaVersionWarningField(), QJsonValue(true)}},
                &error)) {
            appendLog(tr("没能记住「以后不再提醒」：%1").arg(error));
        } else {
            appendLog(tr("「%1」以后不再提醒 Java 版本不符。").arg(instanceDisplayName(versionId)));
        }
    }

    return choice == JavaMismatchDialog::Choice::UseMatchingJava
               ? JavaMismatchAnswer::UseMatchingJava
               : JavaMismatchAnswer::KeepInstanceJava;
}

int MainWindow::instanceModCount(const QString &versionId) const {
    const QString modsDir = instanceFileDirs(versionId).value(QStringLiteral("mod"));
    if (modsDir.isEmpty()) {
        return 0;
    }
    return QDir(modsDir).entryList({QStringLiteral("*.jar")}, QDir::Files).size();
}

void MainWindow::resolveMemoryForLaunch(const QString &versionId, EffectiveJavaSettings &io,
                                       const JavaSettingsOverride &override) {
    if (!io.autoMemory) {
        // 手动档：io 里那两个数字是"用户定的值"（可能来自全局、也可能来自这个实例
        // 的覆盖），就是这次要用的值，一个都不许动。
        return;
    }

    // NOTE: 自动档下 xmx/xms 的覆盖**在这里被丢掉**，这是有意的：自动档的定义就是
    //       "按系统内存现算"，用户单独钉一个 Xmx 而让 Xms 自动算，只会得到一对
    //       谁也说不清的数字。对话框那边是同一件事的另一半 —— 自动档下那两行整行
    //       禁用，点确定时也不把它们写进 meta。
    if (!override.autoMemory.has_value()) {
        // 自动这一档来自**全局**：走老路径。refreshSuggestedMemory() 会重算、落盘
        // （注册表）、并按全局档更新自己的值 —— 和加实例覆盖之前逐字节一致。
        m_javaSettings->refreshSuggestedMemory();
        io.xmx = m_javaSettings->xmx();
        io.xms = m_javaSettings->xms();
        return;
    }

    // 自动这一档是**这个实例**自己要的（全局可能是手动档）。现算一份给这次启动用，
    // 全局一个字节都不动 —— 实例 A 开着自动，不能让全局那份内存配置跟着变，
    // 那是跨作用域污染的同一种错（见 ensureJavaForVersion 的值版那段注释）。
    //
    // NOTE: 算出来的数**不写回 meta**。写回去的话，那个实例的 meta 里就多出一对
    //       xmx/xms（它们本来没有），而这对值是死的 —— 自动档每次启动都重算，
    //       下次读出来只会让「实例设置…」里那两行显示成"自定义 + 一个用户没填过的数"。
    //       跨作用域污染的反面不是"多写一份"，而是"只写在这次要用的值上"。
    const SystemMemory::Usage usage = SystemMemory::read();
    // 数一数这个实例装了多少模组（见 .hpp 那条 NOTE：只在这一支数）。
    // 加了这一档之后，同一个 200 模组的整合包会比原版多要一截内存 —— 这正是
    // FEATURES.md 待办里点名的"自动档不认整合包规模"。
    io.xmx = GlobalJavaSettings::suggestXmxMb(usage.totalMb, usage.availableMb,
                                              instanceModCount(versionId));
    io.xms = GlobalJavaSettings::suggestXmsMb(io.xmx);
}

void MainWindow::onFolderSelectionChanged() {
    const QListWidgetItem *item = m_ui->getFolderList()->currentItem();
    if (item == nullptr) {
        return;
    }
    // 同一个目录时 FolderManager 直接返回、不发信号，所以这条链路是幂等的：
    // refreshFolderList() 里挡了信号，重灌不会把用户刚做的选择又跑一遍。
    m_folderManager->setCurrentFolder(item->data(Qt::UserRole).toString());
    updateFolderButtons();
}

/**
 * @brief 一个没起过名字的游戏文件夹在列表**上面那行**默认显示什么。
 * @param path 根目录的绝对路径。
 * @return 目录名（`D:\MC\.minecraft` → `.minecraft`）。取不出目录名时（`D:\` 这种根）
 *         返回整条路径 —— 那一行总得有字，空的会变成一个看不出是什么的空白行。
 *
 * NOTE: 这条路只在**没起过 customname** 时走到（起过名字的显示那个名字）。
 */
static QString folderDisplayNameOf(const QString &path) {
    const QString name = QDir(path).dirName();
    return name.isEmpty() ? path : name;
}

QString MainWindow::folderTitle(const QString &path) const {
    // 三档，从最具体的往最泛的排：用户起的名字 → 自带那份那个固定说法 → 目录名。
    const QString custom = m_folderManager->displayName(path);
    if (!custom.isEmpty()) {
        return custom;
    }
    if (FolderManager::isDefaultFolder(path)) {
        return MainWindowUI::folderAssistantTitle();
    }
    return folderDisplayNameOf(path);
}

void MainWindow::refreshFolderList() {
    const QString current = m_folderManager->currentFolder();
    QList<MainWindowUI::FolderRow> rows;
    for (const QString &path : m_folderManager->folders()) {
        MainWindowUI::FolderRow row;
        row.path = path;
        row.assistant = FolderManager::isDefaultFolder(path);
        row.current = (path == current);
        // 上面那行：用户起的 customname 优先；没起过时是默认那个显示名（见 folderTitle()）。
        row.title = folderTitle(path);
        // 下面那行：完整路径（系统分隔符，别把 QDir 的 `/` 露给用户）。目录不在盘上时
        // 还要缀一句 —— 光看路径看不出"这一份现在扫不出东西"。
        row.subtitle = QDir::toNativeSeparators(path);
        if (!QFileInfo(path).isDir()) {
            row.subtitle += tr("（目录不在了）");
        }
        rows.append(row);
    }
    m_ui->setFolderRows(rows);
    updateFolderButtons();
}

void MainWindow::updateFolderButtons() {
    // 下载期间整列都不给动（见 setDownloadUiBusy）：换文件夹会把后面那些文件写到
    // 另一份 .minecraft 里去，半批在里面半批在外面，比"这次下不了"难查得多。
    //
    // NOTE: 这里原来还按"当前选中的那一份是不是自带那份"给「移除文件夹」按钮算过
    //       enabled，那个按钮 2026-09-24 删掉了。「移除」现在不在这儿管：齿轮菜单里
    //       那一项的灰/亮由它自己按"这颗齿轮属于哪一行"算（见 FolderMenu 里那段
    //       protectedFolder）—— 菜单是照着**哪一行那颗齿轮**弹的，和"列表当前选中
    //       哪一行"可以不是同一行，这是它比按钮准的地方。
    m_ui->getAddFolderButton()->setEnabled(!m_downloadInFlight);
}

void MainWindow::applyCurrentFolderToPaths() {
    // 全局路径的**唯一**切换点。此后 AppPaths 的每一个函数（versions / assets /
    // libraries / java）都对着这一份 .minecraft，直到下一次切换。
    const QString root = m_folderManager->currentFolder();
    // 自带那份**不设覆盖**（传空串 = 用默认那份）：启动器被整个拷到别处之后，默认路径
    // 跟着 exe 走，而不是钉死在上次记下的那个老位置。
    AppPaths::setMinecraftDir(FolderManager::isDefaultFolder(root) ? QString() : root);
    appendLog(tr("当前游戏文件夹：%1").arg(QDir::toNativeSeparators(root)));
}

/**
 * @brief 日志行前面那个"[实例名] "。
 * @param sessionId 哪一份会话；0 或者查不到（会话已经结束了）时返回空串。
 *
 * 同时开着好几份时，一串"[游戏输出] xxx"根本分不出是谁说的 —— 多开这件事本身就把
 * "哪一行是谁"从显然变成了不显然。
 *
 * NOTE: 查不到就**不加前缀**、也不写"(未知)"：那种行是"又开一个被拦下了"这类不挂在
 *       任何实例上的话（sessionId == 0），或者会话刚好在这一行之前结束了。硬安一个名字
 *       比不写更糟。
 */
[[maybe_unused]] static QString sessionLogTag(const LauncherManager::GameSession &game) {
    if (game.sessionId == 0 || game.displayName.isEmpty()) {
        return QString();
    }
    return QStringLiteral("[%1] ").arg(game.displayName);
}

void MainWindow::onProcessOutput(int sessionId, const QString &text) {
    m_gameSessionController->onProcessOutput(sessionId, text);
}

void MainWindow::onProcessError(int sessionId, const QString &text) {
    m_gameSessionController->onProcessError(sessionId, text);
}

void MainWindow::showGameLogLine(int sessionId, const QString &text) {
    m_gameSessionController->showGameLogLine(sessionId, text);
}

GameLogWindow *MainWindow::ensureGameLogWindow(int sessionId, bool userAsked) {
    return m_gameSessionController->ensureGameLogWindow(sessionId, userAsked);
}

void MainWindow::onGameFinished(int sessionId, int exitCode, QProcess::ExitStatus exitStatus) {
    m_gameSessionController->onGameFinished(sessionId, exitCode, exitStatus);
}

QString MainWindow::launchingInstanceName() const {
    return m_gameSessionController->launchingInstanceName();
}

QString MainWindow::launchingStatusName() const {
    return m_gameSessionController->launchingStatusName();
}

bool MainWindow::confirmStopGame(const QString &displayName, int sessions) {
    return m_gameSessionController->confirmStopGame(displayName, sessions);
}

void MainWindow::stopGameSession(int sessionId) {
    m_gameSessionController->stopGameSession(sessionId);
}

void MainWindow::onImportModpackClicked() {
    m_importController->onImportModpackClicked();
}

void MainWindow::importModpackFromFile(const QString &zipPath, const QString &namePrefill) {
    m_importController->importModpackFromFile(zipPath, namePrefill);
}

bool MainWindow::applyModpackImport(const QString &zipPath, const QString &instanceName,
                                    const QByteArray &manifestJson,
                                    ModpackImport::Format format) {
    return m_importController->applyModpackImport(zipPath, instanceName, manifestJson, format);
}

bool MainWindow::installLocalResourceFile(const QString &versionId,
                                          ResourceCatalog::Kind kind, const QString &path,
                                          QString *problemOut) {
    return m_importController->installLocalResourceFile(versionId, kind, path, problemOut);
}

void MainWindow::handleInstanceSettingsDrop(QWidget *page, const QString &versionId,
                                            const QStringList &paths) {
    m_importController->handleInstanceSettingsDrop(page, versionId, paths);
}

void MainWindow::importDroppedModpack(const QString &zipPath, const QString &namePrefill) {
    m_importController->importDroppedModpack(zipPath, namePrefill);
}

void MainWindow::handleWindowDrop(const QStringList &paths) {
    m_importController->handleWindowDrop(paths);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    // 准入判据**就是**实例设置那一页用的那一个（同一个函数，见 .hpp 里那段 NOTE）：
    // 拖的是一串本地文件才接。
    if (InstanceSettingsDialog::droppedFilePaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QStringList paths = InstanceSettingsDialog::droppedFilePaths(event->mimeData());
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    handleWindowDrop(paths);
}

void MainWindow::onVersionDownloadFinished(const QString &versionId, bool success,
                                           const QString &errorMessage) {
    // Keep the original installation task and busy guard alive until the asynchronous
    // preset download step completes. Re-enter once to use the existing finalization.
    if (success && m_presetController && !m_installPresetModsReady &&
        versionId == m_installingInstanceId && versionId == m_installPendingPresetInstance &&
        !m_installPendingPresets.isEmpty()) {
        m_installPresetModsReady = true;
        auto steps = m_activityCenter->tasks()->snapshot(m_downloadTaskId).steps;
        steps.append({tr("应用实例预设"), Tasks::TaskStepState::Running, tr("检查模组、前置与配置")});
        m_activityCenter->tasks()->setSteps(m_downloadTaskId, steps);
        m_activityCenter->tasks()->setProgress(m_downloadTaskId, 0, 0);
        m_activityCenter->tasks()->setDetail(m_downloadTaskId, tr("正在检查并应用实例预设…"));
        QStringList ids;
        for (const auto &choice : m_installPendingPresets) ids.append(choice.first);
        for (const auto &id : ids) {
            if (m_installPresetFingerprints.value(id).isEmpty() ||
                m_presetController->presetFingerprint(id) != m_installPresetFingerprints.value(id)) {
                m_installPresetModsOk = false;
                m_installPresetModsReport = tr("预设在确认后发生变化或已损坏，未继续下载模组；请重新预览并确认。");
                onVersionDownloadFinished(versionId, true, errorMessage);
                return;
            }
        }
        appendLog(tr("正在应用实例预设：检查模组及必要前置…"));
        m_presetController->installModsAfterInstall(ids, versionId,
            [this, versionId, errorMessage](bool ok, const QString &report) {
                m_installPresetModsOk = ok;
                m_installPresetModsReport = report;
                onVersionDownloadFinished(versionId, true, errorMessage);
            });
        return;
    }
    // 这两笔要在 setDownloadUiBusy(false) **之前**清：那个函数会重灌"能启动的实例"，
    // 而判据里就有它们两个（见 installingVersionIds()）—— 顺序反了的话，刚装完的实例
    // 第一次刷新时还被滤着（要等下面那句再刷一次才出现），中间那一帧用户看到的是
    // "装完了可它不在列表里"。先清、后恢复界面，那一帧就不存在。
    const bool wasImporting =
        !m_importingInstanceId.isEmpty() && versionId == m_importingInstanceId;
    const bool wasInstallingInstance =
        !m_installingInstanceId.isEmpty() && versionId == m_installingInstanceId;
    if (wasImporting) {
        m_importingInstanceId.clear();
    }
    if (wasInstallingInstance) {
        m_installingInstanceId.clear();
    }
    // 「补全文件」那颗按钮那一笔（2026-09-26）。理由和上面两条逐字一样：它也走公共的
    // versionDownloadFinished(id, ok, msg)，也得在 setDownloadUiBusy(false)（那把"能启动的
    // 实例"重灌一遍，判据里就有它）之前清掉。
    const bool wasRepairing =
        !m_repairingInstanceId.isEmpty() && versionId == m_repairingInstanceId;
    if (wasRepairing) {
        m_repairingInstanceId.clear();
    }

    // 安装任务的最后一步：把这一趟带着的那份预设落上去（coding plan §7 那条
    // 「生成并显示 ApplyPlan → 备份 → 应用预设 → 写入实例元数据和任务结果」）。
    //
    // ★ 必须**在任务收尾之前**：预设应用本来就是安装任务的一步，它成没成要写进任务结果
    //   那句话里。这一趟写的是刚装出来的实例目录（几十 KB 的配置文件），而它此刻还在
    //   `installingVersionIds()` 里 —— 不会有人在这期间把它启动起来。
    //
    // ★ 应用失败**不把安装任务改判成失败**：游戏和加载器都装好了，能玩；预设没落上是
    //   另一句话。所以 `success` 原样传下去，预设那一句并进结果文本里（见下面那一段）。
    QString resultMessage = errorMessage;
    QString presetApplyProblem;
    if (!m_installPresetModsOk) presetApplyProblem = m_installPresetModsReport;
    if (wasInstallingInstance && success && !m_installPendingPresets.isEmpty() &&
        m_installPendingPresetInstance == versionId) {
        if (m_presetController != nullptr) {
            QStringList applied;
            QStringList failed;
            if (!m_installPresetModsReport.isEmpty()) {
                if (m_installPresetModsOk) applied.append(m_installPresetModsReport);
                else failed.append(m_installPresetModsReport);
            }
            for (const QPair<QString, bool> &one : m_installPendingPresets) {
                if (m_installPresetFingerprints.value(one.first).isEmpty() ||
                    m_presetController->presetFingerprint(one.first) != m_installPresetFingerprints.value(one.first)) {
                    presetApplyProblem = tr("预设在确认后发生变化或已损坏，未使用旧确认覆盖新内容，请重新预览。");
                    failed.append(presetApplyProblem);
                    continue;
                }
                // 四类各一份（§7.1）—— 每一份都走同一条路；一份失败不把前面已经落上去的
                // 那几份从结果里抹掉（它们确实写进去了）。
                const PresetController::InstallApply outcome =
                    m_presetController->applyAfterInstall(one.first, versionId, one.second);
                if (outcome.ok) {
                    applied.append(outcome.message);
                } else {
                    presetApplyProblem = outcome.message;
                    failed.append(outcome.message);
                }
            }
            if (failed.isEmpty()) {
                resultMessage = errorMessage + QStringLiteral("\n\n") +
                                applied.join(QLatin1Char('\n'));
            } else {
                resultMessage = errorMessage + QStringLiteral("\n\n实例可用，预设部分应用：\n") +
                                (applied + failed).join(QLatin1Char('\n'));
            }
        }
    }
    // 不管这一趟成没成、带没带预设，pending 都在这一刻清干净：下一次安装不该继承这一次的。
    if (m_installPresetModsReady) {
        const auto steps = m_activityCenter->tasks()->snapshot(m_downloadTaskId).steps;
        if (!steps.isEmpty() && steps.last().name == tr("应用实例预设"))
            m_activityCenter->tasks()->setStepState(m_downloadTaskId, steps.size() - 1,
                presetApplyProblem.isEmpty() ? Tasks::TaskStepState::Done : Tasks::TaskStepState::Failed,
                presetApplyProblem);
    }
    m_installPendingPresets.clear();
    m_installPendingPresetInstance.clear();
    m_installPresetModsReady = false;
    m_installPresetModsOk = true;
    m_installPresetModsReport.clear();
    m_installPresetFingerprints.clear();

    // 这一批在账本上收尾。**必须在 setDownloadUiBusy(false) 之前**：那个函数是"这一批
    // 结束了"的公共通道，它自己会在末尾兜一次底（见那边）——先在这儿按成败如实收，
    // 兜底那一句就什么都不做；反过来顺序的话，兜底会先把任务按失败收掉，这一句再说什么
    // 都晚了（终态只落一次）。
    finishTask(m_downloadTaskId, success, resultMessage);
    m_downloadTaskId = Tasks::invalidTaskId();

    // 整批结束，无论成败都要把界面恢复回来
    setDownloadUiBusy(false);
    m_progressThrottle.invalidate();

    // 这一条讲的是整合包导入（数据层那边"一次点击 = 一个终态"，导入也走这同一个信号）。
    // 收尾那句话是**说给用户看的**（"整合包「X」导入好了…"），所以这里不再往上叠
    // 「版本 X 的文件已全部就绪」那一句 —— 对导入来说那句是错的：下的是这个整合包的
    // 模组，不是"某个版本的文件"。成功的样子是**实例列表里多一个**（顺便选中它）。
    if (wasImporting) {
        if (success) {
            appendLog(tr("✓ %1").arg(errorMessage));
            // 刷新会把下拉框和实例页那张树一起重灌（同一个函数里做的），新实例就在里面。
            refreshInstalledVersions();
            QComboBox *combo = m_ui->getInstalledVersionCombo();
            // 刷新只"保住原来选中的"，保不住"原来压根没选"：刚导入出来的这个顶上，
            // 省得用户还得自己去列表里找一遍（和下载完成那条路一个口径）。
            // NOTE: 那条口径是**两件事**：不但要"顶上"，还得"原来已经有选中的就别动"
            //       —— 用户自己挑的版本比"刚做完的那个"更重要。这里以前只有前半句，
            //       于是导入一个整合包会把他特意选好的版本顶掉（另外两条路都有
            //       currentIndex() < 0 这道守卫，只有这一条漏了）。
            if (combo->currentIndex() < 0) {
                if (const int index = combo->findData(versionId); index >= 0) {
                    combo->setCurrentIndex(index);
                }
            }
            updateStartButtonText();
            InlineMessage::information(m_ui->overlay(), tr("导入完成"), errorMessage);
        } else {
            appendLog(tr("✗ 整合包没有导入：%1").arg(errorMessage));
            InlineMessage::warning(m_ui->overlay(), tr("导入失败"), errorMessage);
        }
        return;
    }

    // 这一条讲的是"装一个带名字的实例"（安装页上点了「安装」）。它走的也是这同一个
    // 信号（数据层那边"一次点击 = 一个终态"），收尾那句话说的是**用户起的那个名字**。
    // 和整合包那段同一个道理：不再往上叠「版本 X 的文件已全部就绪」那一句。
    if (wasInstallingInstance) {
        if (success) {
            appendLog(tr("✓ %1").arg(resultMessage));
            // 装出来的实例要能被启动：刷"已安装版本"那一摊，并把它选上（刷新只保得住
            // "原来选中的"，保不住"原来压根没选"）。
            refreshInstalledVersions();
            QComboBox *installedCombo = m_ui->getInstalledVersionCombo();
            if (installedCombo->currentIndex() < 0) {
                if (const int index = installedCombo->findData(versionId); index >= 0) {
                    installedCombo->setCurrentIndex(index);
                }
            }
            updateStartButtonText();
            // 预设没落上：安装本身成了，所以**不改标题**（「安装失败」是假话），
            // 但那一句必须是 warning 的颜色 —— 用户这会儿最该看见的正是它。
            if (!presetApplyProblem.isEmpty()) {
                InlineMessage::warning(m_ui->overlay(), tr("安装完成，但预设没有应用上"),
                                       resultMessage);
            } else {
                InlineMessage::information(m_ui->overlay(), tr("安装完成"), resultMessage);
            }
        } else {
            appendLog(tr("✗ %1").arg(errorMessage));
            InlineMessage::warning(m_ui->overlay(), tr("安装失败"), errorMessage);
        }
        return;
    }

    // 这一条讲的是「补全文件」（实例设置页「启动」栏那颗按钮，2026-09-26）。走的也是公共的
    // versionDownloadFinished(id, ok, msg)：数据层那边"一次点击 = 一个终态"这条约定不破。
    // 这里要说的是**用户认得的那个名字**（实例名，不是目录名），而且不能说"下载完成" ——
    // 用户点的是"检查"，那就报"检查完"。
    if (wasRepairing) {
        const QString displayName = instanceDisplayName(versionId);
        if (success) {
            appendLog(tr("✓ 实例「%1」的文件校验完成：%2").arg(displayName, errorMessage));
            // 补全期间它被滤出了"能启动的实例"（installingVersionIds() 里有它），回来之后
            // 把它选中 —— 和上面那条同一种情况：刷新只保得住"原来选中的"，保不住"原来被
            // 滤掉、选中的那一格顺位落到了别人身上"。
            QComboBox *installedCombo = m_ui->getInstalledVersionCombo();
            if (installedCombo->currentIndex() < 0) {
                if (const int index = installedCombo->findData(versionId); index >= 0) {
                    installedCombo->setCurrentIndex(index);
                }
            }
            updateStartButtonText();
            InlineMessage::information(m_ui->overlay(), tr("检查完成"),
                                       tr("实例「%1」的文件都齐了。\n\n%2")
                                           .arg(displayName, errorMessage));
        } else {
            appendLog(tr("✗ 实例「%1」的文件没补齐：%2").arg(displayName, errorMessage));
            InlineMessage::warning(m_ui->overlay(), tr("文件没补齐"),
                                   tr("实例「%1」有文件没能拿到。\n\n%2")
                                       .arg(displayName, errorMessage));
        }
        return;
    }

    if (success) {
        appendLog(tr("✓ 版本 %1 下载完成：%2").arg(versionId, errorMessage));

        // 刷新"已安装版本"下拉框：client jar 已经落到
        // .minecraft/versions/<id>/<id>.jar，installedVersions() 下次扫盘就能看到它。
        // NOTE: 老代码注释里写着"这里不刷新列表：refreshVersionList() 会 clear() 掉
        // 列表、连用户选中的版本一起丢"—— 那个顾虑现在没有了，因为
        // fillVersionCombo() 自己负责把原来选中的版本捡回来。
        refreshInstalledVersions();

        // 刷新只能"保住"原来的选中项，保不住"原来压根没选"：那种情况下把刚下完的
        // 版本顶上，省得用户下完还要自己去下拉框里再找一遍。原来已经有选中项时
        // 不覆盖 —— 用户自己挑的版本比"最新下完的那个"更重要。
        QComboBox *installedCombo = m_ui->getInstalledVersionCombo();
        if (installedCombo->currentIndex() < 0) {
            // 按 id 找（findData），不是按显示文本（findText）——刚下完的版本可能
            // 已经有自定义名，显示文本不一定是版本号。
            if (const int index = installedCombo->findData(versionId); index >= 0) {
                installedCombo->setCurrentIndex(index);
            }
        }

        InlineMessage::information(m_ui->overlay(), tr("下载完成"),
                                 tr("版本 %1 的文件已全部就绪。\n\n%2").arg(versionId, errorMessage));
    } else {
        // 失败就不刷新了：没成功就不该有新条目冒出来（jar 大概率没落地）
        appendLog(tr("✗ 版本 %1 下载失败：%2").arg(versionId, errorMessage));
        InlineMessage::warning(m_ui->overlay(), tr("下载失败"),
                             tr("版本 %1 有文件未能下载完成。\n\n%2").arg(versionId, errorMessage));
    }
}

void MainWindow::onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson) {
    if (versionJson.isEmpty()) {
        appendLog(tr("获取版本 %1 的信息失败。").arg(versionId));
        // 账本上那件任务也到此为止（这一步还没进到"下载文件"，但用户点的那件事已经黄了）。
        // NOTE: 收尾也在 setDownloadUiBusy(false) **之前**，理由和 onVersionDownloadFinished
        //       那边逐字一样。
        finishTask(m_downloadTaskId, false, tr("没能取到版本 %1 的信息。").arg(versionId));
        m_downloadTaskId = Tasks::invalidTaskId();
        setDownloadUiBusy(false);
        InlineMessage::warning(m_ui->overlay(), tr("下载失败"),
                             tr("无法获取版本 %1 的信息，请检查网络后重试。").arg(versionId));
        return;
    }

    appendLog(tr("版本信息获取成功，开始下载游戏文件..."));
    const QString versionsPath = AppPaths::versionDir(versionId);
    // 这条槽是 VersionManager 的 versionDetailsFetched 唤起来的，而那条信号是在
    // DownloadManager 收完版本 json 的那个槽里同步发出来的 —— 直接在这里起下载批次，
    // 批次就整个跑在那条栈上。排到下一轮（见 VersionManager::deferNextStep 的 NOTE）。
    m_versionManager->deferNextStep([this, versionId, versionsPath] {
        m_versionManager->downloadVersion(versionId, versionsPath);
    });
}
