#include "controllers/ServerVersionPicker.hpp"
/**
 * @file MainWindow.cpp
 * @brief 实现主应用程序窗口及用户交互逻辑。
 * @note 本文件实现了 UI 事件处理函数并委托给各个管理器的方法。
 *
 * 本文件将 UI 控件连接到启动器，处理用户命令，
 * 并协调身份验证、下载和启动操作。
 */

#include "MainWindow.hpp"
#include "TextMarks.hpp"
#include "presets/PresetTargetSnapshot.hpp"
#include "controllers/ResourceSearchController.hpp"
#include "controllers/InstanceUpdateCheckController.hpp"
#include "controllers/UpdateCenterController.hpp"
#include "controllers/ServerSettingsPageController.hpp"
#include "controllers/InstallPageController.hpp"
#include "controllers/ServerRunController.hpp"
#include "controllers/ServerMenuController.hpp"
#include "controllers/DownloadCompletionController.hpp"
#include "controllers/DownloadTaskLedgerController.hpp"
#include "controllers/ResourceInstallController.hpp"
#include "manage/UpdatePolicy.hpp"
#include "launcher/LaunchScriptExport.hpp"
#include "util/SecretRedactor.hpp"
#include "util/LaunchScriptCredentials.hpp"
#include "manage/BackupFiles.hpp"
#include "operations/InstanceUpgrade.hpp"

#include "..\ui\Animations.hpp"  // 换游戏文件夹时那一栏淡一下
#include "..\ui\ConfigFileEditorPage.hpp"
#include "..\ui\ConfigFileText.hpp"
#include "..\ui\GroupDialog.hpp"
#include "..\ui\InstanceGroupDelegate.hpp"
#include "..\ui\InstanceFilesPage.hpp"
#include "..\ui\InstanceSettingsDialog.hpp"
#include "..\ui\IntegrityReportPage.hpp"  // 完整性检查报告页
#include "..\ui\JavaMismatchDialog.hpp"
#include "..\ui\JavaManageDialog.hpp"
#include "..\ui\AccountSkinDialog.hpp"
#include "..\ui\ResourceDetailPage.hpp"
// 轻量回滚点：`updateInstanceFiles()` 动手之前
// 摆一份、`onUpdateCenterUndoRequested()` 照着它退回去。
#include "manage/UpdateRollback.hpp"
#include "..\ui\GameLogWindow.hpp"
#include "..\ui\MainWindowUI.hpp"
// 任务中心面板：底栏中间那格点开之后向上弹出来的那块。
// 这一边给它两样东西 —— 一行长什么样（`TaskCenter::Item`，那层是**纯显示**的 DTO）和
// 「清除已完成」被点了怎么办（见 setupConnections() 里 setTaskCenterContent() 那一段）。
#include "..\ui\TaskCenterPanel.hpp"
#include "..\ui\TaskDetailPage.hpp"
// 那一页本身（存档那支笔点开的「世界设置」）。**它摆什么、怎么摆只有它自己知道**，
// 这一边只负责把盘上读出来的东西翻译成 `WorldSettingsPage::WorldView`（见
// worldViewOf() 那段），以及把页面上改过的翻译回去。
#include "..\ui\WorldSettingsPage.hpp"
// 内容区上面那一层：设置界面和各种提示都开在它里面，不再另开独立窗口。
// 见它的文件头 —— ui/ 那边只管"开在哪儿、长什么样"，不认识业务。
#include "..\ui\OverlayHost.hpp"
#include <QDialog>
#include <QLayout>
// 安装页「预设」那一行点开的紧凑弹窗：四类各挑一份、看一眼计划。
// 它只画外面喂进去的值（`ui/` 不读盘），所以这一边负责把 controller 算好的东西递进去。
#include "..\ui\PresetInstallPicker.hpp"
// QMessageBox 的替身：`InlineMessage::warning(overlay, 标题, 正文)` 顶掉
// `QMessageBox::warning(this, 标题, 正文)`。调用点一行一行对得上，
// 语义一个字没变 —— 变的只是"这一句说在哪"。
#include "..\ui\InlineMessage.hpp"
// 主页右栏那个小人怎么画：几何、投影、默认外观、头像裁切全在它里面（纯函数 + 值）。
#include "..\ui\SkinModel.hpp"
#include "..\ui\Theme.hpp"
// 顶上那条自己画的标题栏。这个头单独 include：`MainWindowUI::titleBar()` 只前置
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
#include "util/ImageDecode.hpp"
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
#include "controllers/FileOperationRecoveryController.hpp"
#include "controllers/DownloadBatchController.hpp"
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
#include "controllers/ServerManagementActions.hpp"
#include "controllers/ThemeSettingsController.hpp"
#include "controllers/InstancePagesController.hpp"
#include "controllers/PlayerSkinController.hpp"
// 「主窗口各控件的视图模型」那一层（实例树 / 分组行 / 底栏面板行 / 版本下拉框）。
#include "controllers/MainWindowViewData.hpp"
// 下载页那几类资源的取数（搜索 / 版本列表 / 图标）。头文件那边只前置声明了这个类
// （那里用不到它的定义），这里要用真的。
#include "download/ResourceStore.hpp"
#include "launcher/AsciiPathAlias.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/DefaultSkin.hpp"
// 拖进来的那一份文件是哪一类（纯判断，见那个头文件）。
#include "manage/DroppedResource.hpp"
#include "manage/FolderManager.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/InstanceGroupStore.hpp"
#include "manage/ModpackExport.hpp"
#include "manage/ModpackImport.hpp"
// 离线账号自己挑的那张皮肤（「账号与外观」→「换一张…」）。
#include "manage/OfflineSkin.hpp"
#include "manage/PlayerSkinStore.hpp"
#include "manage/RandomSkinStore.hpp"
#include "manage/VersionDisplay.hpp"
#include "manage/VersionManager.hpp"
#include "manage/WorldFiles.hpp"
// 游戏异常退出那几句怎么得出来：纯函数，规则全在它自己那个文件里。
#include "diag/GameExitDiagnosis.hpp"
#include "settings/EffectiveJavaSettings.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "settings/LauncherSettings.hpp"
// 任务账本 —— 底栏中间那格和任务中心面板都读它。
// 账本由 ActivityCenterController 持有；MainWindow 通过 controller 更新执行进度。
#include "tasks/TaskManager.hpp"
#include "util/AppPaths.hpp"
#include "util/ErrorReport.hpp"
// 「服务器列表」那一页要读 servers.dat（NBT，旧存档还是 gzip 过的）。
#include "util/NbtUtil.hpp"
// 同一页的「编辑」「添加」「删除」：改名字和地址、加一台、拿掉一台都要
// **写** servers.dat —— NbtUtil 那一层故意只读，写侧单独一层（格式、按值匹配、gzip
// 保留的理由都在它的文件头）。`ServerListFile::pathFor()` 也是这一份路径的唯一出处。
#include "manage/ServerListFile.hpp"
// 服务端工作区那一层：判据层（状态 / EULA / properties）和
// 扫盘那一层（`<数据目录>/Servers` 底下谁在住）。界面那一页只摆控件，判据全在这儿。
#include "download/ServerInstall.hpp"
#include "server/ServerModel.hpp"
#include "server/ServerRegistry.hpp"
#include "server/ServerRestart.hpp"  // 崩溃自动重启那几条判据（纯头）
#include "server/ServerResourcePack.hpp"  // 本地资源包那一份 zip 的判据 + SHA-1
#include "server/ServerContent.hpp"  // 服务端内容（模组 / 数据包）的名字与分类判据
// 服务端安装那四段的**判据层**（"还差哪一份元数据、取回来怎么读、分成几步"）：
// 纯函数，`verify` 的 ZY 一节直接钉它。这里只用到那几个翻译。
#include "server/ServerInstallRun.hpp"
#include "server/ServerRun.hpp"  // 服务端"跑"的那几条判据（纯头，见它的文件头）
// 同一页的「刷新」：真的连上去问一遍每台现在什么样（图标 / MOTD / 人数 / 延迟）。
#include "util/ServerPing.hpp"
#include "util/SystemMemory.hpp"
// 「导入整合包…」要读压缩包根目录里的 modrinth.index.json（ZipUtil::readEntry）。
#include "util/ZipUtil.hpp"

using namespace MainWindowViewData;

namespace {
/**
 * @brief 「这件事对账本上那件任务的收尾责任」——从建出来到 `disarm()` 之间任何一个早退
 * （`return`）都由析构里那一发兜住。
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
 * · 成功（进程真的起来了 ⇒ 由 `onProcessOutput()` 那边收）；
 * · 用户自己取消（该记 `cancel()` 而不是失败，免得账本上多一次"失败的启动"）；
 * · 转交给别人（比如后面那条异步的路自己会收）。
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
      m_playerSkin(
          new PlayerSkinController(m_authManager, m_skinStore, m_randomSkinStore, this)) {
    // 安装页那一层（要装哪个原版、选中哪条加载器、名称框里那份"用户改过没有"）。
    // NOTE: 它的构造里**特意不碰那棵树的界面** —— 第一次重画是进安装页的 `openPage()`；
    // 构造只把「选中」这一位初始化成 `MainWindowUI::modLoaderNoneIndex()`。
    m_installPage = new InstallPageController(this);
    // 服务端**起 / 停 / 退出之后怎么办**那一套（见 `controllers/ServerRunController.hpp`）。
    // 它不拥有进程 —— 进程的账在 `m_serverWorkspace` 里，这一层只按 id 去问。
    m_serverRun = new ServerRunController(this);
    // 「服务端」那一页的右键菜单：摆哪几项 + 点下去做什么（见
    // `controllers/ServerMenuController.hpp` 那段"为什么这一层值得单独站着"）。
    m_serverMenu = new ServerMenuController(this);
    // 下载收尾那六种来路（见 `controllers/DownloadCompletionController.hpp`）。
    m_downloadCompletion = new DownloadCompletionController(this);
    // 「这一批下载的账」：任务号 / 谁能重试 / 父任务 / 资源名 / 取消标志 / 进度节流。
    m_downloadTaskLedger = new DownloadTaskLedgerController(this);
    setAcceptDrops(true);
    // 「资源详情」那一页的控制器**构造里就建**（只是状态 + 一个 QObject，页面对象仍然懒建）。
    // 不能跟着页面一起懒建：图标那一发、版本列表那一发回来时都可能比"开过页"早到，
    // 而它们按 `m_resourcePage->state().open` 认人 —— 那一位没有"还没建"这一档。
    m_resourcePage = new ResourcePageController(this);
    // 「把这一份资源装进那个实例」：落点 / 谁在动盘 / 存不存别处 / 加载器那道闸。
    m_resourceInstall = new ResourceInstallController(this);
    m_downloadFilter = new DownloadFilterController(this);
    m_instanceUpdateCheck = new InstanceUpdateCheckController(this);
    // 「更新中心」那一页：账 + 页面对象 + 开页时记的实例 id 全在这一层（构造里就建，
    // 页面仍然懒建 —— 理由同上面 `m_resourcePage` 那段）。
    m_updateCenter = new UpdateCenterController(this);
    m_resourceSearch = new ResourceSearchController(this);
    // ---------- 任务账本 ----------
    // **唯一**记录"现在有哪些事在跑、各自跑到哪儿了"的地方。它自己一个网络都不开、一个
    // 进程都不起（执行者照旧是 DownloadManager / VersionManager / LauncherManager）——
    // 这一层负责把它们的信号翻成 begin() / setProgress() / succeed()（不把任务中心
    // 做成第二套业务逻辑）。
    m_activityCenter = new ActivityCenterController(this);
    m_serverSettingsController = new ServerSettingsController(m_serverWorkspace);
    // 「服务端设置」那一页：页对象 + 现在说的是谁 + 能不能改 + 端口那一次的结论全在这一层
    // （构造里就建，页面仍然懒建 —— 理由同上面 `m_resourcePage` 那段）。
    m_serverSettings = new ServerSettingsPageController(this);
    m_themeSettingsController = new ThemeSettingsController(m_ui, m_settings, this, this);
    m_javaSettingsController = new JavaSettingsController(
        m_ui, m_javaSettings, m_javaRuntimeController, this,
        JavaSettingsController::Host{[this](const QString &text) { appendLog(text); }}, this);
    // 「实例预设」那一页：页面、增删改、提取、计划、备份、落盘
    // 全在 PresetController 里；这儿只把"实例叫什么 / 在不在跑 / 日志往哪儿写"三样递过去，
    // 以及实例页第三档那个宿主（页面由 controller 自己塞进去）。
    // 兼容性提供方在构造时就固定端点；不能等后面的 applySettingsToUi() 才注入。
    m_versionManager->setModrinthApiBaseOverride(m_settings->modrinthApiBaseOverride());
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
            [this](const QString &id) {
                // 导入整合包那一路也会把 overrides 整棵解压进实例根目录，和预设写的是
                // 同一片地方。它不在搬文件那个计数集合里，单独并进来。
                //
                // NOTE: 只认**导入**这一档，不用 `InstallOccupancy::modifying()`：这是窄的
                // 那道闸（宽的那一道在下面），安装 / 补全期间落预设是允许的。
                return m_fileOperationInstanceIds.contains(id)
                       || m_installOccupancy.is(InstallOccupancy::Batch::Import, id);
            },
            // `applyPlan()` 专用、宽的那一份 —— 正被安装 / 补全文件的
            // 实例也不能这时候落预设。它只从预设页那一颗「应用」进来，不存在
            // "安装向导收尾自己挡自己"的问题（那一步走的是下面窄的那道闸）。
            [this](const QString &id) {
                return installingVersionIds().contains(id);
            },
            // 收尾之后重灌实例清单：这一趟期间别的刷新（下载收尾、改设置）会把这一份
            // 按 `installingVersionIds()` 滤掉，不重刷它就一直不在列表里。
            [this]() { refreshInstalledVersions(); },
            // 预设编辑器那一页铺在内容区上（和实例设置同一条路，见 OverlayHost::showPage：
            // 摆上去就返回，收页之后才叫 onClosed）。
            // 归属传 -1 = 现在这一颗选项卡：这一页只从实例页「预设」那一档开得出来。
            [this](QDialog *dialog, const QString &title, int ownerTab,
                   std::function<void(int)> onClosed) {
                OverlayHost *overlay = m_ui->overlay();
                if (!overlay->showPage(dialog, title, ownerTab)) {
                    return false;
                }
                // 收页结果接在 `pageClosed()` 上：确定 / 取消 / 返回箭头 / Esc /
                // 被别的页顶掉 —— 全汇到那一处；结果从对话框自己那份 `result()` 读
                // （确定 = Accepted，和原来 `execPage()` 的返回值同一套）。
                //
                // NOTE: 接在 `dialog` 上（不是 `this` 上）当上下文：这一页收掉之后
                // 对话框就 `deleteLater()` 了，连接跟着一起没；接在 this 上的话，每开
                // 一次就多留一条永远匹配不上的连接。
                connect(
                    overlay, &OverlayHost::pageClosed, dialog,
                    [dialog, onClosed](QWidget *page) {
                        if (page == dialog && onClosed) {
                            onClosed(dialog->result());
                        }
                    },
                    Qt::SingleShotConnection);
                return true;
            },
            // 页头那句「N 份预设」：和「当前文件夹：…（N 个实例）」是页头同一格，
            // 换档时由 MainWindowUI 自己换着写（见 setInstanceMode）。
            [this](const QString &text) { m_ui->setPresetSubtitle(text); }},
        this);
    m_presetController->setEditorPrompt([this](const QString &title, const QString &text, const QStringList &buttons) {
        return InlineMessage::ask(m_ui->overlay(), MessageKind::Question, title, text, buttons, buttons.size()-1, buttons.size()-1);
    });
    m_ui->navigationGuard = [this] { return !m_presetController || m_presetController->leaveEditor(); };
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
    // 工具箱那五件小工具：页面、算术、线程全在那个 controller 里，这儿只有
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
            [this](std::function<void()> open) { openPageWhenIdle(open); },
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
            [this](const QString &serverId) { m_serverSettings->refreshIfShowing(serverId); },
            // 还原一份实例期间它算「正在搬文件」：不能启动、不能删（见 restoreInstance()）。
            [this](const QString &id, bool busy) {
                if (busy) {
                    markFileOperationBusy(id);
                } else {
                    releaseFileOperationBusy(id);
                }
            },
            [this] { refreshInstalledVersions(); },
            // 服务端世界正在备份 / 还原：那一个服务端不能启动、不能删（见
            // `ServerRunController::fileBusy()` / `removeServer()` 那两道闸）。服务端不是
            // 实例，记不进 `installingVersionIds()`。
            [this](const QString &id, bool busy) { m_serverRun->setFileBusy(id, busy); }},
        this);
    m_serverPingController = new ServerPingController(
        ServerPingController::Host{
            [this](const QString &id) { return serverListOf(id); }}, this);
    // 控制器的分工：服务端控制台、实例文件图标反查、启动历史、游戏会话、
    // 任务详情和实例设置写侧已经各自持有页面/状态/业务判断。MainWindow 只保留跨域协调所需的
    // Host 回调和兼容入口；具体所有权与生命周期约束写在各 Controller 的文件头，不在这里重复。
    m_serverConsole = new ServerConsoleController(
        m_serverWorkspace, m_ui, this,
        ServerConsoleController::Host{
            [this](const QString &text) { appendLog(text); },
            [this](const QString &id) { m_serverRun->stop(id); },
            [this](const QString &id) { m_serverRun->forceKill(id); }},
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
            // 任务中心那一格。★ 收尾**不走 `MainWindow::finishTask()`**：
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
            [this](const QString &text) { appendLog(text); },
            [this](const QString &path) -> QString {
                const QString id = instanceIdOfPath(path);
                if (id.isEmpty()) return QString();
                if (m_launcherManager != nullptr && m_launcherManager->isVersionRunning(id)) {
                    return tr("「%1」正在运行。先关掉游戏，再动这里的文件。")
                        .arg(instanceDisplayName(id));
                }
                if (installingVersionIds().contains(id)) {
                    return tr("「%1」正在被另一项文件操作占用（复制 / 备份 / 还原 / 安装），"
                              "等它完成后再动这里的文件。")
                        .arg(instanceDisplayName(id));
                }
                return QString();
            },
            [this](const QString &path) { return instanceIdOfPath(path); },
            [this](const QString &id, bool busy) {
                if (busy) {
                    markFileOperationBusy(id);
                } else {
                    releaseFileOperationBusy(id);
                }
            }},
        this);
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
            // 任务中心那一格：「复制实例」后台那一趟。收尾同样**不走**
            // `MainWindow::finishTask()` —— 复制没有"可重试的下载批次"。
            [this](Tasks::TaskKind kind, const QString &title, const QString &targetId,
                   const QList<Tasks::TaskStep> &steps) {
                return m_activityCenter->begin(kind, title, targetId, steps);
            },
            // 升级父任务底下的子任务：源实例更新这一趟的"复制 / 快照"不该各占一行。
            [this](Tasks::TaskId parent, Tasks::TaskKind kind, const QString &title,
                   const QString &targetId, const QList<Tasks::TaskStep> &steps) {
                return m_activityCenter->tasks()->beginChild(parent, kind, title, targetId,
                                                              steps, Tasks::TaskCapabilities());
            },
            [this](Tasks::TaskId id, const QString &detail) {
                m_activityCenter->tasks()->setDetail(id, detail);
            },
            [this](Tasks::TaskId id, qint64 done, qint64 total) {
                m_activityCenter->tasks()->setProgress(id, done, total);
            },
            [this](Tasks::TaskId id, qint64 bytesDone, qint64 bytesTotal) {
                m_activityCenter->tasks()->setBytes(id, bytesDone, bytesTotal);
            },
            [this](Tasks::TaskId id, int index, Tasks::TaskStepState state) {
                m_activityCenter->tasks()->setStepState(id, index, state);
            },
            [this](Tasks::TaskId id, const Tasks::TaskCapabilities &caps) {
                m_activityCenter->tasks()->setCapabilities(id, caps);
            },
            [this](Tasks::TaskId id, bool success, const QString &errorSummary) {
                m_activityCenter->finish(id, success, errorSummary, false);
            },
            [this](Tasks::TaskId id, const QString &token) {
                m_activityCenter->tasks()->setRecovery(id, token, !token.isEmpty(), QString());
            },
            [this](const OperationReport::Report &report) {
                return m_fileRecovery != nullptr && m_fileRecovery->registerReport(report);
            },
            // 用户按了「取消」、后台导入真的停下来之后：收成 Cancelled 而不是 Failed
            // （取消不是失败，底栏不该亮红点）。
            [this](Tasks::TaskId id) { m_activityCenter->tasks()->cancel(id); }},
        this);
    m_fileRecovery = new FileOperationRecoveryController(
        m_ui->overlay(),
        FileOperationRecoveryController::Host{
            [this](const QString &id) { return instanceDisplayName(id); },
            [this](const QString &id) {
                return (m_launcherManager != nullptr && m_launcherManager->isVersionRunning(id))
                       || installingVersionIds().contains(id);
            },
            [this] { refreshInstalledVersions(); },
            [this](const QString &text) { appendLog(text); },
            [this](Tasks::TaskKind kind, const QString &title, const QString &targetId,
                   const QList<Tasks::TaskStep> &steps) {
                return m_activityCenter->begin(kind, title, targetId, steps);
            },
            [this](Tasks::TaskId id, const QString &detail) {
                m_activityCenter->tasks()->setDetail(id, detail);
            },
            [this](Tasks::TaskId id, int index, Tasks::TaskStepState state) {
                m_activityCenter->tasks()->setStepState(id, index, state);
            },
            [this](Tasks::TaskId id, bool ok, const QString &error) {
                m_activityCenter->finish(id, ok, error, false);
            },
            [this](Tasks::TaskId id, const QString &token) {
                m_activityCenter->tasks()->setRecovery(id, token, !token.isEmpty(), QString());
            }},
        this);
    // 上一次会话留下的、已经没有报告在用的本模块暂存，启动时收掉（只碰自己的目录名）。
    m_fileRecovery->cleanupStaleStaging(AppPaths::minecraftDir());
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
    // ---------- 「上一次启动」那本账 ----------
    // 它在这儿建（不是初始化列表里），和上面那份任务账本同一个理由：它要读盘，得等前面
    // 几样都就位了；而且必须**赶在第一次 `updateHomeRecent()` 之前** —— 那之后凡是
    // "实例列表变了"的路（重扫实例树那一条在构造里就会走）都会去问它要账本。
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
            // 进程起来的这一刻把账本上那件「启动 X」和会话号对上。
            [this](Tasks::TaskId id, int sessionId) {
                m_activityCenter->bindSession(id, sessionId);
            },
            [this](Tasks::TaskId id, bool ok, const QString &note) { finishTask(id, ok, note); },
            // 用户自己停掉、一行日志都没出的那一次启动：按"取消"收，不记失败（见
            // `GameSessionController::Host::cancelLaunchTask` 的说明）。
            [this](Tasks::TaskId id) {
                if (id != Tasks::invalidTaskId()) m_activityCenter->tasks()->cancel(id);
            },
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
            [this](const QString &key) { retryTaskFromDetail(key); },
            [this](const QString &id) { m_importController->openImportReport(id); },
            [this](const QString &key) {
                bool ok = false;
                const Tasks::TaskId id = key.toULongLong(&ok);
                if (ok && m_toolboxController && m_toolboxController->ownsDownloadTask(id)) {
                    m_toolboxController->resumeDownloadTask(id); return;
                }
                if (ok && m_downloadBatches && m_downloadBatches->owns(id)) { m_downloadBatches->resume(id); return; }
                if (ok && m_presetController && m_presetController->ownsModTask(id)) { m_presetController->resumeModDownload(id); return; }
                if (!ok || m_fileRecovery == nullptr) return;
                const Tasks::TaskSnapshot task = m_activityCenter->tasks()->snapshot(id);
                if (!task.recoveryToken.isEmpty()) m_fileRecovery->resume(task.recoveryToken);
            },
            [this](const QString &key) {
                const auto id = key.toULongLong();
                if (m_downloadBatches && m_downloadBatches->owns(id)) m_downloadBatches->pause(id);
                else if (m_presetController && m_presetController->ownsModTask(id)) m_presetController->pauseModDownload(id);
                else if (m_toolboxController) m_toolboxController->pauseDownloadTask(id);
            }},
        this);
    connect(m_serverWorkspace, &ServerWorkspaceController::message, this,
            [this](const QString &text) { appendLog(text); });
    connect(m_serverWorkspace, &ServerWorkspaceController::statusChanged, this,
            &MainWindow::onServerStatusChanged);
    connect(m_serverWorkspace, &ServerWorkspaceController::lineAppended, this,
            &MainWindow::onServerLineAppended);
    connect(m_serverWorkspace, &ServerWorkspaceController::forceKillAvailable, this,
            &MainWindow::onServerForceKillAvailable);
    connect(m_serverWorkspace, &ServerWorkspaceController::exited, m_serverRun,
            &ServerRunController::onExited);
    // 收起态那一格的重画时机里有一档是"刚完成那一项露到头了"（「完成任务短暂显示后
    // 收进历史」）—— 那个到期**没有任何信号会来**，只能自己起一个单发定时器。
    m_taskSummaryTimer = new QTimer(this);
    m_taskSummaryTimer->setSingleShot(true);
    connect(m_taskSummaryTimer, &QTimer::timeout, this, &MainWindow::updateBottomBarTaskSummary);
    // 账本一变就重画底栏那**两**格（中间那格 + 右下那格）。**不去分辨"变的是哪一项"**：
    // 收起态显示谁是把整张账本放在一起算出来的（`Tasks::collapsedSummary()`），只重画
    // "变了的那一行"在这里没有对应的东西可画（那一格只有一个整体）。
    //
    // NOTE: 整条底栏（`updateBottomBar()`）**不许**接在这上面：账本每来一帧进度都会发这个
    // 信号（下载那份是 200 ms 一帧），接上去等于每帧连头像和账号名一起重刷。
    connect(m_activityCenter, &ActivityCenterController::activityChanged, this, [this]() {
        updateBottomBarTaskSummary();
        updateBottomBarRunState();
    });
    // 「已完成」那一栏变了（收尾 / 清除 / 读盘）：面板要是开着，跟着重画一遍。
    connect(m_activityCenter, &ActivityCenterController::historyChanged, this, [this]() {
        m_ui->refreshTaskCenterPanel();
    });
    // ---------- 「实例设置」写侧那一套 ----------
    // 它自己一块界面都不碰（**一个字节的中文都没有**），要问、要说的那几件事全走下面这六个
    // 回调：这几件恰恰是"只有主窗口做得成"的事（谁在跑要问 LauncherManager、那句话怎么说要
    // 挑中文、重扫实例树是主窗口的活）。
    //
    // 为什么这几个回调是**同步**的：`notice` 是在 controller 那一句代码的位置上当场执行的，
    // 顺序变了用户看到的东西就变（提示卡片是一张盖一张的）。所以这里一行都不许改成排队/异步。
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
            // 谁正被别的作业占着（安装 / 导入 / 复制 / 快照 / 还原）。问的是那一份合集
            // —— `installingVersionIds()` 是"谁动不得"的唯一出处，启动和删除读的也是它。
            [this](const QString &versionId) {
                return installingVersionIds().contains(versionId);
            },
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
    // 所以它不能是 const 的、更不能被别人当"读一下"随便调。
    m_ui->setTaskCenterContent([this]() { return taskCenterItems(); },
                               [this]() { m_activityCenter->clearFinished(); });
    // 点任务中心里的**一行** = 开任务详情页（点击任务进入内嵌 TaskDetailPage）。
    m_ui->setTaskCenterRowActivatedHandler([this](const QString &key) { openTaskDetailPage(key); });
    // 行尾那颗「取消」：走的是和详情页里那一颗**完全同一条路**（`onTaskDetailAction`）——
    // 两处各写一遍的话，"取消"的语义迟早会分家（一处删文件、一处不删）。
    m_ui->setTaskCenterRowActionHandler(
        [this](const QString &key, TaskCenter::RowAction action) {
            switch (action) {
            case TaskCenter::RowAction::Pause:
                onTaskDetailAction(key, TaskDetailPage::Action::Pause);
                break;
            case TaskCenter::RowAction::MoveUp:
            case TaskCenter::RowAction::MoveDown:
                m_activityCenter->moveQueued(key.toULongLong(), action == TaskCenter::RowAction::MoveUp ? -1 : 1);
                break;
            case TaskCenter::RowAction::Viewed:
                m_activityCenter->markViewed(key);
                break;
            case TaskCenter::RowAction::Seen:
                m_activityCenter->markSeen(key);
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
            case TaskCenter::RowAction::RepairImport:
                onTaskDetailAction(key, TaskDetailPage::Action::RepairImport);
                break;
            case TaskCenter::RowAction::Recover:
                onTaskDetailAction(key, TaskDetailPage::Action::Recover);
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
    // NOTE: 「这一趟走离线」这件事**只由"当前账号是离线账号"决定**（见 offlineIdentity()），
    // 主页没有离线模式勾选框 —— 不接名字的话启动那条路会去要凭据，而盘上一份都没有，
    // 用户拿到的是"请先登录"，一个死胡同。
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
    // 上次那本任务账读回来（只存摘要与错误；**没跑完的标成"已中断"，不假装继续**）。
    //
    // 账本是启动器级的固定文件，不随当前游戏文件夹切换。这样正在下载/启动时切换目录，
    // 内存里的任务不会在下一次落盘时被写进另一份 .minecraft。
    m_activityCenter->loadHistory();
    m_presetController->setRecoveryHost(m_activityCenter, [this] { return m_downloadTaskLedger->task(); });
    m_downloadBatches = new DownloadBatchController(m_activityCenter, m_versionManager->downloads(),
        [this] {
            if (m_serverInstall && m_serverInstall->ownsDownloadBatch()) return m_serverInstallTaskId;
            if (!m_versionManager->ownsFileBatch()) return Tasks::invalidTaskId();
            return m_downloadTaskLedger->task() != Tasks::invalidTaskId()
                       ? m_downloadTaskLedger->task() : m_updateBatch.task;
        }, this);
    m_downloadBatches->setContinuation([this] { return snapshotDownloadContinuation(); },
        [this](Tasks::TaskId id, const QJsonObject &intent) { return continueDownloadInstallation(id, intent, true); },
        [](const QJsonObject &intent, const std::atomic_bool *cancel) { return Presets::unchangedInitializationContinuation(intent, cancel); },
        // 「要写预设的那几趟」要拍的那棵配置树：**在后台算**（见 `requestContinuationTargets`）。
        // 只读意图里的值，不碰界面/启动器状态，所以可以放心在 worker 里跑。
        [](const QJsonObject &intent, const std::atomic_bool *cancel) {
            const auto root = intent.value("root").toString(), instance = intent.value("instance").toString();
            if (!QDir::isAbsolutePath(root) || instance.isEmpty()) return QJsonObject();
            return Presets::snapshotInitializationTargets(QDir(root).filePath(QStringLiteral("versions/") + instance), cancel);
        });
    m_downloadBatches->setRestoreAllowed([this] {
        return !m_downloadInFlight && runningVersionIds().isEmpty() && !m_launchController->isStarting()
            && m_serverWorkspace->runningServerIds().isEmpty()
            && installingVersionIds().isEmpty() && (!m_serverInstall || !m_serverInstall->isRunning());
    });
    m_downloadBatches->load();
    // 「忽略这一版 / 固定这一版」那一本账：**启动时读一次**。
    //
    // 读不回来（文件被改坏了 / 权限不对）**不拦任何事**：那是"以后别再提示我"这一档的
    // 记忆，读丢了的最坏结果是那几条又提示了一次 —— 比"启动时弹一个框说这本账读不动、
    // 然后用户什么也做不了"轻得多。所以只写日志。
    {
        QString policyProblem;
        m_updateCenter->policy() = UpdatePolicy::load(UpdatePolicy::storePath(), &policyProblem);
        if (!policyProblem.isEmpty()) {
            appendLog(tr("「更新中心」那份记录读不回来：%1").arg(policyProblem));
        }
    }
    // 本地已安装的先填上（同步扫盘）；在线清单是异步拉的，回来之后由
    // onVersionsFetched() 填"可安装"下拉框 —— 在那之前它显示占位文案。
    // Pending upgrade journals use rename-only recovery, before scanning installed instances.
    // 2.1.5a：游戏根可能被改名/搬走，所以扫描**已配置的游戏根**，每个根用 journal 里的
    // 根指纹验证。只扫文件夹列表，不全盘搜索；指纹对不上的事务不动盘。
    {
        QStringList upgradeRoots = m_folderManager != nullptr ? m_folderManager->folders()
                                                              : QStringList();
        upgradeRoots.append(AppPaths::minecraftDir());
        for (const QString &message : InstanceUpgrade::recoverInterruptedInRoots(upgradeRoots)) {
            appendLog(message);
        }
    }
    refreshInstalledVersions();
    // 顺序要紧：必须在 refreshInstalledVersions() **之后**（下拉框里得先有条目可选），
    // 并且在 fetchAvailableVersions() 之前（免得用户在等待网络期间就手选了一个，
    // 那样启动时反而会把用户的选择抢掉）。也**只在启动时**跑这一次 ——
    // 不能塞进 refreshInstalledVersions()，那个函数每次清单到达、每次下载完成都会跑，
    // 塞进去就破坏了它"只保住、不新增"的语义。
    restoreLastLaunchedVersion();
    // 启动历史账本：读回来并刷新主页“最近游玩”排（读完发一声 `changed()`，接在
    // `updateHomeRecent()` 上；那一发是**同步**的，所以下面几行看到的已经是刷新过的界面）。
    //
    // 位置有一处讲究：必须在 `refreshInstalledVersions()` **之后** —— 那张卡上「继续游玩」
    // 还是「启动实例」要看"那一份实例还在不在已安装列表里"，列表空着的时候问它只会得到
    // "不在"，卡片就会显示成降级的那一档（明明能玩）。
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
    // 行号 —— 面板弹出来到某个按钮被点之间隔着用户看的那两眼，那中间列表是会变的
    // （见 MainWindowUI::PanelRow::key 那段）。
    m_ui->setBottomBarPanelContent(
        [this](bool account) -> QList<MainWindowUI::PanelRow> {
            if (account) {
                // 「最后登录的三个账户」—— 3 这个数只在这一处。
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
    if (m_downloadBatches) m_downloadBatches->shutdown();
    // 日志窗**没有父窗口**（理由见 GameSessionController::ensureGameLogWindow() 那段），
    // 所以 Qt 不会顺手把它们带走 —— 这里自己收，否则关掉启动器之后屏幕上会留着几扇无主的
    // 日志窗。那一份表连同收法都在那一位控制器里（它建的那些窗，它自己收）。
    m_gameSessionController->closeAllGameLogWindows();
    // 体检那一趟可能还在跑（几千次 stat，慢盘上要好几秒）。**先把收手旗子举起来再等它**
    // —— worker 每比几条看一眼那面旗，看到就退（见 submitIntegrityCheck 里那段）。
    //
    // NOTE: 不等的话是**崩**，不是"悄悄漏一个线程"：m_integrityThread 的父对象是 this（下面
    // 这个对象要散了），而 Qt 对"QThread 还在跑、父对象却析构了"是直接 abort
    // （`QThread: Destroyed while thread is still running`）。
    m_integrityController->shutdown();
    // 工具箱里"算哈希"那一条也是 `QThread::create()` 出来的（父对象是 controller 自己，
    // 同一个坑，同一个收法）。
    m_toolboxController->shutdown();
    // 备份 / 还原那一趟同理。**这一趟没法中途收手**（见 .hpp 里 m_backupAbort 那段
    // —— 停下来只会留一个半截的包或者删了一半的目录），所以这里只能**等它跑完**：
    // 一个几百 MB 的实例快照要几十秒，关窗会晚这么多才真的关上。
    //
    // NOTE: 这一位真正的用处是"跑完之后别把结果排回一个正在拆的窗口" —— 引擎不读它。
    // NOTE: 不等的话同上：`m_backupThread` 的父对象是 this，Qt 直接 abort。
    m_backupController->shutdown();
    // 剩下两趟后台活。它们和上面那两趟是**同一个坑**：
    // `QThread::create()` 出来的这几个对象，父对象都是 this（下面这个对象要散了），而 Qt
    // 对"线程还在跑、父对象却析构了"是直接 abort。这两趟又都是**网络活** ——
    // · 图标反查：打开实例设置（或文件管理那几页）就起，几个 sha1 + 一发反查；
    // · 检查更新：点「检查更新」就起。
    // 关窗那一刻正卡在等响应上是最平常的一件事（`HttpClient` 那一条要等 15 秒）——
    // 不在这里等，那十几秒里关启动器就是崩一下。
    //
    // 这一段的规矩**照着这张单子来**：**这一层（或者这一层的子控制器）**起了的线程，
    // 析构里必须**一个一个等过来**。眼下是四条（体检 / 备份 / 图标反查 / 检查更新）——
    // 再加一趟就得多一条，加的时候连同这句一起改。四条都把"等"收进了各自的
    // 控制器（`shutdown()` / `waitForLookup()`），这里在 UI 析构前统一调用。
    // （`ServerPing::Pinger` 那八条不在这张单子上：它们的父对象是 Pinger，
    // 而 `~Pinger()` 自己就 abort+wait，见那边。）
    //
    // NOTE: 这两趟**没有收手旗子**（`InstanceIconLookup::lookup()` 和检查更新那一趟里头没有
    // "看着旗子退出来"的口子），所以这里只能**等它跑完** —— 卡在超时上的话，关窗就会
    // 晚那十几秒才真的关上。那比崩一下强（同一个取舍，见上面备份那一段）。
    // NOTE: 图标那一趟起它的线程父对象是**那个控制器**（不是这一层）——
    // 和"父对象是 this"是同一个坑（控制器也是 this 的子对象，一样要散），所以这一
    // 条照样得留：等的活儿在控制器里，等这一下得在这一层发。
    m_instanceIconController->waitForLookup();
    m_instanceUpdateCheck->waitForLookup();
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
    if (m_presetController && !m_presetController->leaveEditor()) { event->ignore(); return; }
    const QList<LauncherManager::GameSession> games = m_launcherManager->runningGames();
    // 还在跑的那些服务端（键 = serverId，**按名字排序** —— 提示里报出来的名字每次都得是
    // 同一个顺序，不然同一件事两句话看着像两件事）。
    const QStringList serverIds = m_serverWorkspace->runningServerIds();

    if (m_launchController->isStarting()) {
        const int choice = InlineMessage::ask(
            m_ui->overlay(), MessageKind::Question, tr("正在启动"),
            tr("启动器正在启动游戏（校验登录、准备 Java 运行时）。\n\n"
               "现在关掉会取消这次启动，游戏不会开起来。\n要现在关掉吗？"),
            QStringList{tr("取消启动并关闭"), tr("继续启动")}, 1, 0);
        if (choice < 0) {
            // `ask()` 回 -1 不是"用户选了继续启动"，而是**这一问根本没被摆出来**
            // （已经有一条提示开着；提示不叠提示，见 InlineMessage::ask）。那时候底下什么
            // 反馈都没有 —— 用户点关闭、窗口不动、也没有一句解释，只能再点一次。照旧不关
            // （启动还在半路），但把原因说出来；那条提示答完以后再点关闭会照常问。
            appendLog(tr("关闭被搁置：还有一条提示没答完，答完再关一次。"));
            event->ignore();
            return;
        }
        if (choice != 0) {
            event->ignore();
            return;
        }
        m_launchController->abortStart();
    }

    if (games.isEmpty() && serverIds.isEmpty()) {
        QMainWindow::closeEvent(event);
        return;
    }

    if (!games.isEmpty()) {
        // 游戏还在跑。**什么都不做**的实际后果是**把游戏强杀**：
        // MainWindow 是 main() 里的栈对象，窗口关掉 → exec() 返回 → MainWindow 析构 →
        // LauncherManager（它的子对象）析构 → m_gameProcess（它的子对象）析构，而
        // QProcess 的析构是这么写的（Qt 6.10，qprocess.cpp）：
        //
        // if (d->processState != NotRunning) { qWarning() << ...; kill(); waitForFinished(); }
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
                ? tr("游戏「%1」还在运行。\n\n关掉启动器不会关掉游戏 —— 它会继续跑，"
                     "存档、联机都照常。\n要现在关掉启动器吗？")
                      .arg(names.first())
                : tr("还有 %1 个游戏在运行：%2。\n\n关掉启动器不会关掉游戏 —— "
                     "它们会继续跑，存档、联机都照常。\n要现在关掉启动器吗？")
                      .arg(games.size())
                      .arg(names.join(QStringLiteral("、")));
        // 按钮文字自己写：启动器没装 Qt 的中文翻译，标准按钮在界面上是英文的 Yes/No。
        // NOTE: 关掉窗口这一发**本身**在跑 closeEvent，不能开独立窗（界面上只有内容区
        // 那一块覆盖层）—— 内嵌提示在 closeEvent 里同样作数：窗口这时候
        // 还没关掉，`execPrompt` 那个嵌套事件循环照转，"事件循环里再开事件循环"
        // 这件事和阻塞弹窗一样。
        // 默认那一边照旧是**取消**（下标 1），回车 = 别动游戏。
        // 第三格是 destructiveIndex —— 两个都不是危险动作（关的是启动器，游戏照跑），
        // 所以给 -1。
        const int choice = InlineMessage::ask(
            m_ui->overlay(), MessageKind::Question, tr("游戏正在运行"), text,
            QStringList{tr("关掉启动器"), tr("取消")}, 1, -1);
        if (choice != 0) {
            event->ignore();
            return;
        }
    }

    if (!serverIds.isEmpty()) {
        // 服务端和游戏**不是一回事**，所以单独问一次（问一句再决定
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
                     "「一起停掉」会让它存完盘再退（可能要等一会儿）；"
                     "「留着它继续跑」的话，启动器关了它照样开着。")
                      .arg(names.first())
                : tr("还有 %1 个服务端在运行：%2。\n\n关掉启动器时要把它们怎么办？\n"
                     "「一起停掉」会让它们存完盘再退（可能要等一会儿）；"
                     "「留着它们继续跑」的话，启动器关了它们照样开着。")
                      .arg(serverIds.size())
                      .arg(names.join(QStringLiteral("、")));
        // 默认那一边是**留着它们继续跑**（下标 1）：和游戏那一条同理 —— 不主动动别人的
        // 进程更安全（突然停下来那一发万一赶在写存档的中间，就是丢存档的姿势）。
        const int choice = InlineMessage::ask(
            m_ui->overlay(), MessageKind::Question, tr("服务端正在运行"), text,
            QStringList{tr("一起停掉"), tr("留着它们继续跑")}, 1, -1);
        if (choice == 0) {
            // 停要**等它走完**，而且等的是宽限那两个数算出来的时间（`ServerRun::
            // gracefulStopMs()` —— 模组越多、存档越大，给的越宽）。这一句是阻塞的：
            // 界面在这几秒里不动。选择让它静态地等，是因为另一种做法（发完 stop 就让
            // 析构去杀）等于是**把这一问白问了** —— 那正是丢存档的那条路。
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
    m_syncWaitStack.append(m_syncWaitStatus);
    m_syncWaitStatus = statusText;
    updateBottomBar();
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    // 不刷这一次，上面那些改动要等窗口重新绘制时才看得见 —— 而那要等到这段路走完。
    // 排掉用户输入是**必须的**：不排的话这次 processEvents() 会把用户在这期间的点击
    // （比如"再点一次启动"）送回来，于是同一段路走两遍，比卡着更糟。
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::leaveSyncWait() {
    // 多出来的 leave 直接忽略：那样说明调用方出了错，但这里能做的只有别把忙指针再还一次
    // （restoreOverrideCursor() 多还一次会把**别人的**忙指针弹掉）。
    if (m_syncWaitStack.isEmpty()) {
        return;
    }
    m_syncWaitStatus = m_syncWaitStack.takeLast();
    QGuiApplication::restoreOverrideCursor();
    updateBottomBar();
}

void MainWindow::setDownloadUiBusy(bool busy) {
    m_downloadInFlight = busy;
    // ---------- 这几项**故意不灰** ----------
    // 启动按钮、「已安装版本」下拉框、实例页那张树不能跟着 busy 一起灰：理由是
    // **"在下载"和"能启动什么"是两件事**，而且现在按版本分开了 ——
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
    // 「模组加载器」那棵树一起灰，理由同上。忙完解禁走 m_installPage->updateLoaderRows() 一处重算
    // （那棵树的样子全由它一处决定）；「安装」那个按钮还要看名称合法不合法，走
    // m_installPage->updateInstallNameState() 一处。
    m_ui->getLoaderTree()->setEnabled(!busy);
    m_installPage->updateLoaderRows();
    m_installPage->updateInstallNameState();
    // 重灌一遍"能启动的实例"：上面那条过滤（在装的那个先不出现）要立刻生效 ——
    // 置忙那一刻它该从列表里消失，置闲那一刻它该回来（成功的话还在
    // `DownloadCompletionController::onFinished` 里被选中，那句也在这个函数之后跑）。
    refreshInstalledVersions();
    updateFolderButtons();

    // ---------- 底栏那一格那个数字：从这里起/到这里止只由账本说了算 ----------
    // 进度条和"已下载 d / t"那几样都在**任务自己身上**（`Tasks::TaskSnapshot`），
    // 这一格只是账本的一块投影 —— 要清的不是三个成员，而是"把这一批的那件任务收掉"。
    if (busy) {
        m_downloadTaskLedger->begin();
    } else {
        // 这一批到此为止（"为什么这是兜底、为什么不会把成功改写成失败"逐字在
        // `DownloadTaskLedgerController::finish()` 里）。
        m_downloadTaskLedger->finish(tr("这一批下载没有正常结束。"));
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
    // 令牌过期了它照样是 true —— 那没关系：这一行说的是**身份**，令牌过没过期是
    // 启动那一刻刷新才知道的事（那时失败会有弹窗说清原因）。
    const QString accountType =
        offlineIdentity()
            ? MainWindowUI::accountTypeOfflineText()
            : (m_authManager->loggedIn() ? MainWindowUI::accountTypeOnlineText()
                                         : MainWindowUI::accountTypeNotLoggedInText());
    m_ui->getAccountTypeLabel()->setText(accountType);
    // 主页是账号的唯一可见入口；底栏那份标签只为兼容保留数据，不再承担身份展示。
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
    // 所以这里不用自己清。
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

    // ---------- 中：任务摘要 ----------
    // 这一格的内容**不在这里算**：显示谁是 `Tasks::collapsedSummary()` 那条纯规则的事，
    // 画成什么样是 `MainWindowUI::setBottomBarTaskSummary()` 的事。这里只负责"状态变了，
    // 重画一遍"。
    //
    // NOTE: 这一格**不能**写成三支抢优先级（启动 / 下载 / 同步等待）。要的是
    // "启动不再抢走并覆盖下载"：那两件事现在是账本里并列的两行，收起态挑一行展示而已，
    // 没有"借走"这回事，也就没有"借完要照原样还回去"。同步等待是唯一还压在上面的一层，
    // 理由写在 updateBottomBarTaskSummary() 里。
    updateBottomBarTaskSummary();

    // ---------- 右：当前实例运行状态 ----------
    // 运行状态单独一个函数（和中间那格 `updateBottomBarTaskSummary()` 对称）。
    // 理由不是"函数太长"：红那一档现在看的是**账本**（账上还有没有没处理的异常退出），
    // 而账本每变一次都会来一发信号 —— 一发信号就重画整条底栏（连头像、账号名一起）是浪费，
    // 也把"这事儿归谁刷新"弄糊了。拆开之后：账本变 ⇒ 只叫这一格和中间那一格。
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
    // · 正在启动优先（黄）—— 那一段里进程已经起来了（所以 runningGames() 里已经有它），
    // 但用户等的是"游戏窗口出来"，这时候该说的是"启动中"，不是"运行中"；
    // · 然后是**账上还有没处理的异常退出**（红）——
    // "出过事"比"此刻还在跑"更需要被看见；
    // · 然后是"一个都没有"（灰）；
    // · 剩下的就是真的在跑（绿）。
    //
    // NOTE: 判据里**没有**"选中的是哪个实例"——要的是"这台机器上有没有
    // 实例在跑"，那件事跟下拉框选着谁没关系（第二行的显示规则才看那个，见
    // runStateShowsDetail()）。
    //
    // NOTE: 这个判据（`launching`）是**这一格自己的**，和中间那格的"任务摘要"没有关系：
    // 中间那格现在看的是账本（哪件任务该被显示），左边这一格看的是"会话起没起来"。
    // 一件 Launch 任务已经收过尾（游戏出话了）而这一份还在"启动中"是可能的
    // —— 两者本来就是先后关系，谁也不该去问对方要判据。
    //
    // 红那一档问的是 `CrashReportController::pendingRecordIds()`（"账上还有没有那一项"），
    // **不是** `runningGames()`：红说的是"发生过的事"，跟此刻有没有进程在跑是两个维度
    // （两份东西可以同时为真）。灰那一档是"一个都没有且没欠着事"。
    // 三档的判据：黄和绿不变，红色 = 有实例报错，没有实例用灰色。
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
    runStateMarkLabel->setPixmap(
        MainWindowUI::runStateMarkPixmap(mark, m_ui->getBottomBar()->palette()));
    runStateMarkLabel->setVisible(true);

    // 左下那一格**现在点不点得开**。判据就是"左面板里会不会有行"—— 那几行是拿
    // runningGames() 翻出来的（见 setBottomBarPanelContent() 里那个 provider），所以
    // 空不空和这里**同源**。没有运行中的实例时让那个实例栏点不开，箭头隐藏。
    // NOTE: 黄灯那一档（"正在启动"）在这里也是**点得开**的：那一段里会话已经在
    // runningGames() 里了（见上面三态那段注释），面板里列得出它。
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
        // 红：有实例异常退出、账上那一项还没处理掉。
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
        // 没有实例在跑：写「当前没有运行中的实例」。**不是藏起来** —— 这一格要的
        // 就是这句话，而且那一格现在还兼着"点开看实例列表"那颗按钮，藏起来连按钮一起没了。
        // NOTE: 连带的好处是那一格的文字**任何时候都非空**，布局上不再有"从无到有"这一步
        // （那一步正是"中间那根条突然缩短一截"的来源）。
        // NOTE: 这一档的方块是灰的（红只留给"出过事"）—— 那句话本身
        // 一个字没动，见 runStateNotRunningText() 那段。
        runStateLabel->setText(MainWindowUI::runStateNotRunningText());
        runStateLabel->setVisible(true);
        runStateDetailLabel->setText(QString());
        runStateDetailLabel->setVisible(false);
        return;
    }

    // 只有一份在跑时用「实例运行中」那句，多份时报个数。同一句话在两个地方
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

// ---------- 任务中心（底栏那格的投影 + 任务动作） ----------
// 在 `src/MainWindowTasks.cpp`（同一个类，另一个翻译单元）。


// ---------- 脸上那张图（外观拉取 / 落盘 / 铺到窗口上） ----------
// 在 `src/MainWindowSkin.cpp`（同一个类，另一个翻译单元）。


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
    // 任务中心面板都是从账本投影出来的，没有第二处要同步（账本是唯一真相）。
    const Tasks::TaskId task = m_downloadTaskLedger->task();

    // 标题下面那行小字。**没报过总数时只说"已下载多少"，不许编一个百分比出来**。
    // `total <= 0` 在这里是"这一批还没报过总数"（版本 json 还在路上、
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
        // 上面那句文案里的 done/total 是**说给人听**的，不要把 0 也传进去。
        m_activityCenter->tasks()->setProgress(task, done > 0 ? done : 0, total > 0 ? total : 0);
        m_activityCenter->tasks()->setDetail(task, detail);
        // ⚠ 这一段里**不要**再自己写进度条和状态栏。它们由 `taskChanged` → 底栏重画那一发
        // 统一摆（`Anim::smoothProgress()` 也在那条路上）——两处各写一次，用户看到的是
        // 同一格在 240 ms 的追值动画里被第二只手按住，实际表现是"条子偶尔一顿"。
    }

    // 这里**不需要**"这一格现在不归下载就一个像素都不许动"那种闸：这一句只改
    // **任务自己的** detail，改不到底栏那一格的标题（那一格的标题由
    // `Tasks::collapsedSummary()` 现算，而"在同步等待"那一层压在它上面，见
    // updateBottomBarTaskSummary()）。"启动那几句话被 200 ms 一帧
    // 的下载顶掉"在结构上不可能发生。
}

void MainWindow::onVersionDownloadStats(int done, int total, int failed,
                                        const QString &currentFile) {
    // 前缘节流：200 ms 内的后续更新直接丢掉。不这么做的话，5000 多个 asset 会疯狂重排
    // 状态栏，界面反而卡住。
    // NOTE: 丢掉的那些**没有**"结束时补刷"这回事 —— 底栏那一格停在上一次越过阈值的
    // 那一帧，最多比真实进度旧 200 ms。整批结束由 setDownloadUiBusy(false) 把那一件
    // 任务收掉（收尾之后那一格换成"（已完成）"，停在哪一帧都看不出来了）。
    if (!m_downloadTaskLedger->takeProgressTick()) {
        return;
    }
    updateDownloadProgressUi(done, total, failed, currentFile);
}

void MainWindow::appendLog(const QString &text) {
    // 界面里**没有**日志面板：命令行里本来就有
    // LauncherManager 的全套输出，多加一块面板只是把同一批东西抄第二遍。
    //
    // NOTE: 所以这条通道是"给人事后看"的，不是"必须让人看见"的。**必须让用户看到**
    // 的消息（缺 Java、下载失败、未选版本、找不到 version.json）一律走
    // `InlineMessage`（`ui/InlineMessage.hpp`，全仓已无一处 `QMessageBox`）挡住
    // 操作，绝不能只写这里。
    qDebug().noquote() << QStringLiteral("[WTFML]") << SecretRedactor::redact(text);
}

// ---------- 实例树 / 已安装版本 / «启动» 那一行 ----------
// 在 `src/MainWindowInstanceTree.cpp`（同一个类，另一个翻译单元）。


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
                // 这一下取消的是"装这个加载器"这件事 —— 退回「不装（纯原版）」。
                // NOTE: **钉住的版本号留着**（不清 m_chosenVersion）：那是用户自己挑的
                // 一版，按一下叉就把它忘掉，下次再选中这条加载器还得从头找一遍。
                // 它只在"这一条被选中"的时候才显示，留着不会让人看到不该看的东西。
                m_installPage->selectLoader(MainWindowUI::modLoaderNoneIndex());
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
    // （见 ui/RowActivation.hpp 那段），只接 Press/Release 的话"连点两下齿轮"
    // 的第二下会漏给下面那条"点中了这一行"的路。
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
    // 同一个实例树上**实例行**行尾那一排
    // （实例设置放在每一条右侧；悬停时右侧显示上箭头和垃圾桶，分别对应置顶和删除
    // 实例，删除前弹窗提示）。
    // 行内三颗：**常显的齿轮**（= 实例设置）+ 悬停才出现的**上箭头**（= 置顶）和**垃圾桶**
    // （= 删除这个实例）。谁画的、画在哪一格，见 InstanceGroupDelegate::instanceGearRect()
    // 那几个函数 —— 这里读的是**同一个函数**，差一个像素就是"看着点中了垃圾桶、其实点的是
    // 那一行"（那一行现在也有一件事要做：进实例设置，见 onInstanceTreeActivated）。
    //
    // 判据"这是不是实例行"用**没有** groupIdRole()：和代理那边 paint()/sizeHint() 是同一条
    // （组行有那个角色、实例行没有）。
    //
    // NOTE: 三格的命中判定顺序无所谓（探针量过：三格之间零重叠、零空隙），但中间那一格
    // 要先问**这一行到底摆没摆它**（rowPinRole() / rowUnpinRole()）—— 这份
    // .minecraft 里没有置顶组时那一格是空的：指着一块空白弹出「置顶」、点下去还
    // 真的挪了一次，是最难解释的一种。
    // NOTE: 中间那一格是**两个动作共用的一格**（不在置顶组 ⇒ ↑ 置顶 / 已经在了 ⇒ ↓
    // 取消置顶，见 MainWindowUI::rowUnpinRole()）—— 所以这里比的是"在里面不在里面"，
    // 不是"角色号是哪一个"。
    // NOTE: 也接双击，理由和上面组行齿轮那一段逐字相同（点得快时第二发及以后到的是
    // 双击事件，不接的话会漏给"点中了这一行"那条路，于是连点垃圾桶的第二下变成
    // 进实例设置）。
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
                // 就该有反应。齿轮 = 进这个实例的设置（点空白区域、或者左边的图标 /
                // 版本名，都进实例设置 —— 那一行别的地方也是同一件事）。
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
    // NOTE: 组行行尾那两颗「上移 / 下移」（齿轮左边那两颗实心三角）不在这棵树上 ——
    // 它们在「分组设置…」那张表里，认下那一下的是 `ui/GroupDialog::eventFilter()`
    // （挂在那张表的视口上），"每一发都算"和"Shift = 置顶/置底"那两条手势跟着它。
    // 实例行行尾那一排的接线在**上面**（就在组行齿轮那段后面）—— 它和那套是同一个
    // 位置、同一族判据，别再往下找。
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
    // 下载页那四张资源表：**点行身 = 打开这一条资源的详情页**（下载页选中一个模组之后
    // 进入一个独立界面）；点行尾那个箭头仍然是原有规矩（展开 / 收起，
    // 判据是同一个 arrowRect()）—— 于是"展开这一条"这件事从此只挂在箭头上。
    //
    // NOTE: 这一块**只认第一层**（一个资源）。第二层（游戏版本）点开是"问这个资源在这个
    // 游戏版本上有哪些版本可下"、第三层（版本号）点它是"下它"，两条原有的路留着
    // （详情页是**另一条**看整份清单的路，不是把树上那条拆掉）。
    // NOTE: 三发都吃掉（按下 / 松开 / 双击）：`connectRowActivation` 那边给"开关行"补的
    // 那一发双击也会落到"展开"上 —— 而这一下已经算"进这一页"了，不该再翻一次树。
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
                // **排到下一轮事件循环**再开页：这一发还在视图的事件处理里（列表那一侧
                // 的状态机悬在半路），开页那一句不该在这里跑（和 `openPage()` 那条
                // "排到下一轮再开"是同一个理由）。这里传的是两个字符串**值**，不是条目
                // 指针 —— 下一轮里树可能已经被重搜清空了。
                //
                // 来路钉死是 `Download`：这条路上这一页**永远**摆「显示
                // 快照版」，哪怕点的是一个模组（不能把下载页的行为带偏）。
                // 实例号从下载页那条来路里读（`m_downloadReturn`）。
                const QString instanceId = m_downloadReturn.instanceId;
                QTimer::singleShot(0, this, [this, iconKey, kindKey, instanceId] {
                    // NOTE: 覆盖层被别的页占着时（典型场面：实例设置那一页**收着**、
                    // 用户切到下载页逛），`openResourceDetailPage()` 开头那条 `isOpen()`
                    // 会把这一下当场吃掉 —— 用户点了行身，屏幕上一个反应都没有。所以
                    // 先让位：静默收掉占着的那一层，再开（让位那一段的唯一出处是
                    // `runWhenOverlayFree()`）。
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

void MainWindow::onLoaderVersionsFetched(const QString &gameVersion, int loaderIndex, bool success,
                                         const LoaderMeta::LoaderVersionList &versions,
                                         const QString &errorMessage) {
    m_installPage->onLoaderVersionsFetched(gameVersion, loaderIndex, success, versions,
                                           errorMessage);
}

// ---------------------------------------------------------------------------
// 下载页后面那几页：搜索 → 挑游戏版本 → 挑版本号 → 装进实例
// ---------------------------------------------------------------------------
//
// 这几页（整合包 / 模组 / 数据包 / 资源包 / 光影）走的是**同一条路**，区别只有"搜哪一类"
// 和"下到哪儿去"。
// 三层树里每一层都是用户点开才动：
// 第一层 资源 —— 进页面时空搜索词问一次（= 这一类里最热门的），回车重搜
// 第二层 游戏版本 —— 搜索响应里本来就带着这个资源支持哪些游戏版本，**不用再问接口**
// 第三层 版本号 —— 点开第二层那一下才问（一个资源 × 一个游戏版本 = 一条请求）
// 点第三层那一下 = 下它（下到实例的 mods/ 、datapacks/ 之类；整合包那条走缓存 + 导入）。
//
// 三层都是**平条目**（不是自绘控件），用的是版本树/加载器树那同一个代理，所以长得一样
// （见 VersionTreeDelegate 第三个用户那段）。

/// 「这一条在第几层」。0 = 资源 / 1 = 游戏版本 / 2 = 版本号；不在树上回 -1。
///
/// NOTE: 代理那边认层用的是"有没有爹"（isChannel），这边认层用的是"往上数几层" ——
/// 两处必须说的是同一件事，而"往上数几层"这句话在这里写一次（见头文件那段）。
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
    // 整行淡 50% 的（见 VersionTreeDelegate 里 rowOpacity），禁用位才是"灰"的唯一出处。
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

// ---------- 下载页每一页顶上那一行筛子：实现在 controllers/DownloadFilterController ----------
//
// 留下的这几条是**薄壳**（主窗口别处按名字叫它们：灌筛子、开分类、拼搜索请求钥匙）。
// 那一份"读也只从这几个访问器读"的规矩没变，只是它们现在都在控制器里。
bool MainWindow::kindForSearchRequestKey(const QString &key, ResourceCatalog::Kind *out,
                                         ResourceCatalog::Source *outSource) const {
    return m_downloadFilter->kindForSearchRequestKey(key, out, outSource);
}

void MainWindow::ensureResourcePageLoaded(ResourceCatalog::Kind kind) {
    m_downloadFilter->ensureResourcePageLoaded(kind);
}

void MainWindow::refreshDownloadCategory(int row) {
    m_downloadFilter->refreshDownloadCategory(row);
}

ResourceCatalog::Source MainWindow::downloadFilterSource(ResourceCatalog::Kind kind) const {
    return m_downloadFilter->downloadFilterSource(kind);
}

QString MainWindow::downloadFilterGameVersion(ResourceCatalog::Kind kind) const {
    return m_downloadFilter->downloadFilterGameVersion(kind);
}

QString MainWindow::downloadFilterLoader(ResourceCatalog::Kind kind) const {
    return m_downloadFilter->downloadFilterLoader(kind);
}

QString MainWindow::downloadFilterCategory(ResourceCatalog::Kind kind) const {
    return m_downloadFilter->downloadFilterCategory(kind);
}

void MainWindow::fillDownloadFilterVersionOptions() {
    m_downloadFilter->fillDownloadFilterVersionOptions();
}

void MainWindow::refreshDownloadFilterOptions(ResourceCatalog::Kind kind) {
    m_downloadFilter->refreshDownloadFilterOptions(kind);
}

void MainWindow::fillDownloadFilters() {
    m_downloadFilter->fillDownloadFilters();
}

void MainWindow::resetDownloadFilters(ResourceCatalog::Kind kind) {
    m_downloadFilter->resetDownloadFilters(kind);
}


void MainWindow::startResourceSearch(ResourceCatalog::Kind kind) {
    m_resourceSearch->startResourceSearch(kind);
}

void MainWindow::fillResourceHitRows(QTreeWidget *tree, ResourceCatalog::Kind kind,
                                     const ResourceCatalog::SearchResult &result) {
    m_resourceSearch->fillResourceHitRows(tree, kind, result);
}

void MainWindow::onResourceSearchFinished(const QString &requestKey,
                                         const ResourceCatalog::SearchResult &result) {
    m_resourceSearch->onResourceSearchFinished(requestKey, result);
}

void MainWindow::onResourceIconReady(const QString &iconId, const QPixmap &icon) {
    m_resourceSearch->onResourceIconReady(iconId, icon);
}

void MainWindow::onResourceItemActivated(QTreeWidgetItem *item) {
    m_resourceSearch->onResourceItemActivated(item);
}

void MainWindow::fillResourceVersionRows(QTreeWidgetItem *gameItem,
                                        const QList<ResourceCatalog::VersionEntry> &versions,
                                        const QString &problem) {
    m_resourceSearch->fillResourceVersionRows(gameItem, versions, problem);
}

void MainWindow::onResourceVersionsFinished(const QString &requestKey,
                                           const QList<ResourceCatalog::VersionEntry> &versions,
                                           const QString &problem) {
    m_resourceSearch->onResourceVersionsFinished(requestKey, versions, problem);
}

void MainWindow::downloadResourceVersion(QTreeWidgetItem *item) {
    m_resourceSearch->downloadResourceVersion(item);
}

void MainWindow::installResourceVersion(ResourceCatalog::Kind kind, const QString &displayTitle,
                                        ResourceCatalog::VersionEntry version,
                                        ResourceCatalog::Source source) {
    // 薄壳：缺前置那一趟、以及"下到哪儿"全在 `ResourceInstallController`（见那边头文件
    // 第一段 —— 这一趟的形状和每一条判断的理由都写在那儿）。
    m_resourceInstall->install(kind, displayTitle, version, source);
}

QString MainWindow::dependencyInstanceId() const {
    // ① 资源详情页**正开着**、而且它记着来路实例 ⇒ 用它。
    // 那一格是"这一版是给哪个实例的"最具体的一句：用户就是站在那个实例的页面上点的
    // 这一版（`InstanceFiles` 那条路，见 `openFileResourcePage()`；`Download` 那条路
    // 递进来的本来就是下拉框里那一个，两份值一样）。
    // NOTE: 判据带上 `open`，是为了不让**关掉那一页之后**残留的旧值去顶替下拉框 ——
    // 用户关掉详情页、回到下载页直接下另一个模组时，目标该是下拉框上那一个。
    // （异步那一趟不受影响：`ResourceInstallController::finishDependencyCheck()` 把实例**显式传下去**了。）
    if (m_resourcePage->state().open && !m_resourcePage->state().instanceId.isEmpty()) {
        return m_resourcePage->state().instanceId;
    }
    // ② 下载页顶上那个「已安装版本」下拉框。
    return selectedVersionId(m_ui->getInstalledVersionCombo());
}

bool MainWindow::downloadCameFromInstance() const {
    // ① 从实例设置点「下载新模组」切到下载页那一条（`m_downloadReturn` —— 就是那
    // 颗「← 返回实例设置」用的同一份来路）。
    if (m_downloadReturn.valid()) {
        return true;
    }
    // ② 资源详情页是从某个实例点进来的（实例设置 → 模组 → 点名字，见
    // `openInstanceFileResource()`）。那一条路不经过下载页，所以上面那一位是空的 ——
    // 而用户站在**那个实例**的模组列表里点进来的这一版，意思就是"装进它"。
    return m_resourcePage->state().open && !m_resourcePage->state().instanceId.isEmpty();
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
    // `parts.realVersion` 那一格**只有 `wtfml.meta` 里写着 `realVersion` 才有值**，
    // 而**没有任何代码会写它**（见 VersionDisplayParts 那段自己的 NOTE）—— 所以
    // 多数实例递出去的是空串（mw_check 的 `update-target` 探针量的就是这件事）。
    //
    // `instanceGameVersion()` 读的是**同一份 meta 的同一格**（① realVersion），读不出来
    // 再顺继承链上 `clientVersion`（PCL / HMCL 那类把整份 json 合进实例目录的实例唯一
    // 说得出口的地方）和链末端那个目录名 —— **导出整合包那一趟用的就是它**，两处要的
    // 本来就是同一件事（"这个实例是哪个原版"），不该各有一份判据。（它比 ① 严一点：
    // 那个值还得**像**一个真能装出来的版本号，认不出来就回空串 = 退回"不筛"，
    // 也就是这一行今天的样子。）
    target.gameVersion = m_versionManager->instanceGameVersion(instanceId);
    // 加载器名走 instanceLoaderName()（理由同下面那道加载器闸）：PCL 那类合并实例
    // 从 parts.loaderName 读出来是空的，而那两个筛参数空着等于"不筛" —— 问回来的
    // 可能是任意加载器的前置。
    // 名字要**归一成小写那一套**（`loaderGroupKey()`）：`instanceLoaderName()` 回的是
    // 显示名（「Fabric」），而下游（`versionsUrl()` / `versionsRequestKey()`）按小写的词写。
    // 直接递显示名两边一个都对不上 —— 而接口不会报错，只是**筛不出东西**：给 Fabric
    // 实例问回来的前置可能是任意加载器的（装进去就崩）。
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
    // `InstanceIconController` 里）。
    keys = m_instanceIconController->recognizedResourceKeysUnder(modsDir);
    // `known` 说的是"这个实例的 mods 到底认过没有"，而不是"认出来几个" ——
    // 一个模组都没认出（新实例、全是自己改过的 jar）和"没进过文件管理页"都表现为空表，
    // 而界面上那两句话不一样（见 `ResourceInstallController::startDependencyCheck()`）。
    if (known != nullptr) {
        *known = m_instanceIconController->recognizedInstanceIs(instanceId);
    }
    return keys;
}

// ============================ 实例更新检测 ============================
//
// 【这一趟在做什么（顺序不能换）】
//
// 【为什么别的事都不做】
//
// 不自动下载、不自动改盘、不问"要不要更新" —— 要的是"检测"。报告里只说事实
// （哪几个有新版本、最新的那一版叫什么），后面那一步是他自己的事。

void MainWindow::checkInstanceUpdates(const QString &instanceId, InstanceSettingsDialog *dialog,
                                     const QStringList &selectedPaths) {
    m_instanceUpdateCheck->checkInstanceUpdates(instanceId, dialog, selectedPaths);
}

void MainWindow::onUpdateCheckLookupFinished(const InstanceIconLookup::Result &result,
                                            const QString &instanceId) {
    m_instanceUpdateCheck->onUpdateCheckLookupFinished(result, instanceId);
}

// 更新检测：**按钥匙发那一发"问这个项目的版本列表"**（一把钥匙一发）。
//
// 单独一个函数的缘由是它有两个调用点 —— 反查那一趟回来时对 `itemsByKey` 里每把钥匙发一遍，
// 以及批量那一发回来时给"退回问 CurseForge"的那些补发（见 `onUpdateCheckLatestFinished()`）。
// 两处要发的东西是**逐字同一句**，抄第二份的话迟早有一边忘了带上来源。
void MainWindow::requestUpdateCheckVersions(const QString &requestKey) {
    m_instanceUpdateCheck->requestUpdateCheckVersions(requestKey);
}

void MainWindow::onUpdateCheckLatestFinished(
    const QString &requestKey, const QHash<QString, ResourceCatalog::VersionEntry> &latest,
    const QString &problem) {
    m_instanceUpdateCheck->onUpdateCheckLatestFinished(requestKey, latest, problem);
}

void MainWindow::onUpdateCheckVersionsFinished(const QString &requestKey,
                                              const QList<ResourceCatalog::VersionEntry> &versions,
                                              const QString &problem) {
    m_instanceUpdateCheck->onUpdateCheckVersionsFinished(requestKey, versions, problem);
}

// 参数**按值收**（别改回 `const QString &`）：下面那句 `m_updateCheck =
// UpdateCheckRun{}` 会把这个成员清空，而 `onUpdateCheckVersionsFinished` 那边递进来的
// 正是 `m_updateCheck.instanceId` **本身** —— 按引用收的话，函数体第一句就把自己这个参数
// 指的那块 QString 抹成空的，于是整份名单记在**空键**底下（`m_instanceOutdatedPaths`）。
// 症状非常有欺骗性：**当着一页的面筛得出来**（那一趟是拿 `run.instanceId` 直接递过去的，
// 而 `run` 是赋值前拷出来的），关掉这一页再进来就永远筛不出东西、还重新挂着「还没有查过
// 更新」—— 而那一趟明明刚跑完。屏幕上看不出任何异常，日志也是干净的。
// QString 是隐式共享的，按值收不多拷一个字节的正文。
void MainWindow::finishUpdateCheck(QString instanceId) {
    m_instanceUpdateCheck->finishUpdateCheck(instanceId);
}

// ===========================================================================
// 更新中心
// ===========================================================================
//
// 这一段现在只剩两条薄壳：那一页的**全部状态**（"忽略 / 固定"那一本账、页面对象、开页时
// 记下的实例 id）以及把它翻成动作的那几条，都在 `controllers/UpdateCenterController`
// （检测那一趟自己另有一层，见 `controllers/InstanceUpdateCheckController`）。
//
// 【为什么还留着这两条壳】
//
// 按名字叫它们的**不止这里**：检测收尾那条路要清一遍过期的固定、再把「可更新」名单重算
// 一次（`InstanceUpdateCheckController::finishUpdateCheck()`）。壳的意思只是"主窗口仍然认
// 这个名字"，实现随状态一起搬走了 —— 状态和动它的那几条在同一个 .cpp 里。

void MainWindow::rebuildInstanceOutdatedPaths(const QString &instanceId) {
    m_updateCenter->rebuildOutdatedPaths(instanceId);
}

bool MainWindow::saveUpdatePolicy() {
    return m_updateCenter->savePolicy();
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

// ---------- 服务端那一页的接线 ----------
// 列表怎么重扫、行怎么点、控制台怎么开、设置页那几组动作、修 / 删 / 起停，以及「新建服务端」
// 那一页，都在 `src/MainWindowServer.cpp`（同一个类，另一个翻译单元）。


// 这两条是「更新中心」那一页在**主窗口这一侧**的入口：状态和动作都在
// `controllers/UpdateCenterController`（见那一段的说明）。
void MainWindow::openUpdateCenter(const QString &instanceId) {
    m_updateCenter->open(instanceId);
}

UpdateCenterPage::Report MainWindow::updateCenterReportForPage(const QString &instanceId) const {
    return m_updateCenter->reportFor(instanceId);
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
        m_updateBatch.lines.append(tr("• %1 —— 已经换成新版本").arg(fileName));
    } else {
        ++m_updateBatch.failedSteps;
        m_updateBatch.lines.append(
            tr("• %1 —— 没换成功：%2").arg(fileName, errorMessage));
    }
}

void MainWindow::finishUpdateBatch(bool success, const QString &errorMessage) {
    const UpdateBatchRun run = m_updateBatch;
    m_updateBatch = UpdateBatchRun{};
    if (run.label.isEmpty()) {
        return;
    }
    // 取消和真失败走同一条终态信号（见 `DownloadTaskLedgerController::userCancelInFlight()`）——
    // 用户按了取消就记 Cancelled，不记 Failed、不点亮底栏那颗"没看过的失败"红点。
    const bool userCancelled = !success && m_downloadTaskLedger->userCancelInFlight();
    if (userCancelled && m_activityCenter->tasks()->has(run.task)) {
        m_activityCenter->tasks()->cancel(run.task);
    } else if (m_activityCenter->tasks()->has(run.task)) {
        m_activityCenter->tasks()->setDetail(run.task, tr("换好 %1 个，失败 %2 个。")
                                               .arg(run.succeeded.size())
                                               .arg(run.failedSteps));
        finishTask(run.task, success && run.failedSteps == 0,
                   run.failedSteps > 0 ? tr("有 %1 个文件没能换掉。").arg(run.failedSteps)
                                       : errorMessage);
    }
    // finishTask() 认领过就会自己清掉；这一段是"那一件已经不在账本里"的兜底，
    // 别让重试资格留在一次已经收尾的更新上（见 `clearOwnerIf()`）。
    m_downloadTaskLedger->clearOwnerIf(run.task);
    appendLog(success ? tr("更新「%1」结束：换好 %2 个，失败 %3 个。")
                            .arg(instanceDisplayName(run.instanceId))
                            .arg(run.succeeded.size())
                            .arg(run.failedSteps)
                      : tr("更新「%1」没跑完：%2").arg(instanceDisplayName(run.instanceId), errorMessage));
    // 「不只显示总数」：**逐个资源**写进去（日志里那一行一行就是每个资源的结果）。
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
    // 那几条、要么撤销（失败了的话）。这是后台收的尾（下载可能跑了好几分钟，用户早翻到
    // 别的页去了）⇒ 走 `openPageWhenIdle()`：**不驱逐**他正看着的那一页，等它收干净再开。
    const QString instanceId = run.instanceId;
    openPageWhenIdle([this, instanceId] { openUpdateCenter(instanceId); });
}

// ============================ 资源详情页 ============================
//
// 下载页点中一个模组后进入一个**整页**：上方是模组图像、名字、来源和转到
// modrinth/curseforge/mcmod.cn 的入口，中间一排版本，下方排列版本。
// "独立界面"落成**整页覆盖层**（`OverlayHost::showPage`，顶上四个选项卡和
// 底栏照旧露着，不另开窗）。
//
// 三件事的分工（写清楚免得下次改错地方）：
// - **界面的样子**全在 ui/ResourceDetailPage.{hpp,cpp}（它不 include 任何 src/ 下的头）；
// - **数据**全在 src/download/ResourceCatalog（拼网址、分组、时间那句、渠道那一档）；
// - **下面这一片**是接线：谁去问网络、点哪颗按钮干什么、哪一行是"下它"。
// 所以这里没有一行"这一页长什么样"的代码，那一页也没有一处"要不要联网"的决定。

// ---------- 「资源详情」那一页：状态与那几条函数都搬进 controllers/ResourcePageController ----------
//
// 留下的这几条是**薄壳**：`onResourceDownloadFinished` 被 `ConnectionsController` 按名字
// 接走（`&MainWindow::onResourceDownloadFinished`），另两条被主窗口别处直接叫。页面状态
// 与"开页/拨勾选框/筛分组"那几条内部函数全在控制器里，这里只转发。
void MainWindow::setupResourceDetailPage() {
    if (m_resourcePage == nullptr) {
        m_resourcePage = new ResourcePageController(this);
    }
    m_resourcePage->setup();
}

bool MainWindow::openResourceDetailPage(const QString &iconKey, const QString &kindKey,
                                        DetailSource from, const QString &instanceId,
                                        std::function<void(int)> onClosed) {
    if (m_resourcePage == nullptr) {
        m_resourcePage = new ResourcePageController(this);
    }
    return m_resourcePage->open(iconKey, kindKey, from, instanceId, onClosed);
}

void MainWindow::fillResourcePageGroups() {
    if (m_resourcePage != nullptr) {
        m_resourcePage->fillGroups();
    }
}

void MainWindow::onResourceDownloadFinished(const QString &label, bool success,
                                            const QString &errorMessage) {
    if (m_resourcePage != nullptr) {
        m_resourcePage->onDownloadFinished(label, success, errorMessage);
    }
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

void MainWindow::onInstallNameEdited() {
    m_installPage->noteNameEdited();
}

// ---------------------------------------------------------------------------
// 安装页：实例预设
// ---------------------------------------------------------------------------

void MainWindow::openInstallPresetPicker() {
    // 薄壳：「预设」那一行的选择、弹窗、摘要都在 `InstallPageController`（见那边）。
    m_installPage->openPresetPicker();
}


// ---------- 「设置」选项卡那一片接线 ----------
// `applySettingsToUi()` 与各颗 `on…Changed()` 在 `src/MainWindowSettings.cpp`（同一个类，另一个翻译单元）。

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
                instance.version = QStringLiteral("Minecraft %1 / %2")
                    .arg(parts.realVersion.isEmpty() ? parts.id : parts.realVersion,
                         loader.isEmpty() ? QStringLiteral("原版")
                                          : (loader + QLatin1Char(' ') + loaderVersion).trimmed());
            }
            instance.icon = combo->itemIcon(row);
            if (instance.icon.isNull() && m_versionManager != nullptr) {
                instance.icon = QIcon(instanceRowMark(
                    m_versionManager->displayParts(instance.id), m_versionManager, 32));
            }
            instance.favorite = m_groupStore != nullptr && m_groupStore->isPinned(instanceKey(instance.id));
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
                                   : QStringLiteral("%1 / %2")
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

// ---------- 「账号与外观」那一扇（登录 / 切号 / 皮肤 / 披风）----------
// 在 `src/MainWindowAccount.cpp`（同一个类，另一个翻译单元）。


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

// ---------- 实例这一行能做的事（改名 / 固定 / 删除 / 两处 Notice 投影）----------
// 在 `src/MainWindowInstanceActions.cpp`（同一个类，另一个翻译单元）。


QString MainWindow::instanceKey(const QString &versionId) const {
    // 稳定 id / 目录名的取舍必须和实例设置写侧共用同一条规则。
    return InstanceSettingsController::instanceKeyFor(m_versionManager, versionId);
}

// ---------- 实例分组（组行 / 「分组设置」那一扇 / 成员与改名删除）----------
// 在 `src/MainWindowGroup.cpp`（同一个类，另一个翻译单元）。


namespace {

/// 「结构/原理图」那一页顶上那两排「来源」（★ ，见
/// `InstanceFilesPage::setStructureSources()`）。
///
/// 存档那一排和投影那一排并排，**上面一排是存档的、下面一排是投影的**。其中"**哪几个
/// 存档真有结构**"要读盘（结构方块存下来的东西落在
/// 存档自己的 `generated/` 底下），那件事由 `InstancePagesController::worldStructures()`
/// 算 —— 这一层只把那份事实摆成那一页要的形状。
///
/// 摆出来的规矩：
/// - **没有结构的存档一个都不列**；
/// - **没装投影模组就没有「投影」那一排**；
/// - 一个存档都没有、可投影装着 ⇒ 存档那一排照旧露着，里面是那**一行灰字**
/// （结构下拉打开时显示"无可用的结构"）；
/// - 两样都没有 ⇒ 返回**空表**，那一页只剩"这个实例还没有结构 / 原理图"那句提示
/// （不显示任何框，只保留没有结构的提示文本）。
///
/// NOTE: 两排的**排头**（「存档」/「投影」）走 `InstanceFilesPage` 上那三个静态函数 ——
/// 界面上写着的那几个字只有一个出处，这一层不自己抄一份（验证工程也读它们）。
/// 所以 `InstancePagesController` 那边给的是"哪几个存档有结构"（两张平行的表），
/// 排头是在这儿戴上去的。
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
    // 这一层被别的页面/提示占着时：**静默把它收掉**，然后把"开"排到下一轮（见
    // runWhenOverlayFree 那段）。空着的时候同一轮就开出来。
    // NOTE: 那一句空 id 的判断必须排在**这一发之前** —— 它是"用户还没选实例"这种普通
    // 情况，不该顺手把别的页面收掉。
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
    // 启动**已经在走**的时候又来了这一发（换令牌 / 下 Java 的同步等待里，
    // 用户回到实例设置页点了「加入世界」）。这一发不能排进 `m_pendingQuickPlay` ——
    // 正在走的那一趟在开头就把待办取走了，这里排上的会一直留到**下一次**用户自己点
    // 「启动」时才被消费：游戏莫名其妙地进了一个他早就忘了的世界。直接丢掉并说一句。
    if (m_launchController->isStarting()) {
        InlineMessage::warning(m_ui->overlay(), tr("正在启动"),
                               tr("上一次启动还没走完（正在校验登录 / 准备 Java 运行时），"
                                  "等它结束之后再试。"));
        return;
    }
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
    // 那几道闸一道都不绕过（要的是"试试看能不能生效"，而不是"绕过启动器直接塞参数"）。
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

void MainWindow::openDownloadCategory(const QString &categoryKey,
                                      const NavigationReturn &origin) {
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

    // ② 这一栏是不是**从某个实例的设置页里点「下载新模组」进来的**（@p origin 有效）。
    //
    // 是的话记下来路，并且把「← 返回实例设置」露出来 —— 从实例设置里点
    // 「下载新模组/资源包…」进来时，左上角保留一个返回键，返回到实例设置里面，
    // 和资源包详情一致。
    // 来路**只记在这一处**：`NavigationReturn` 里已经写清归哪颗根页、哪个实例、哪一页、
    // 文件页滚到哪儿（见那份头文件）；下面 `returnToInstanceSettingsFromDownload()`
    // 按同一份摆回去。
    //
    // 那颗键**整个下载页共用一颗**、摆在左栏顶上（就是左边栏的顶上），翻到别的栏它照旧在那儿 ——
    // 能不能回去和用户此刻在逛哪一栏无关。
    m_downloadReturn = origin;
    m_ui->setDownloadBackVisible(origin.valid());

    // ① 先收掉当前这一页。**只能走 dismiss()**：它发的就是顶上那颗返回箭头的同一发信号
    // （见 OverlayHost::dismiss() 里那段 ★）—— 用户按的那颗按钮可能正开着一条提示/另一页，
    // 只 `closeOverlay()` 的话那一页自己收不到任何信号。
    //
    // NOTE: 这一发只是**尽力**先收（常见情况下它收完就完了）；收干净了没有由下一轮的
    // `continueOpenDownloadCategory()` 判 —— 上面一层压着一条提示时 `dismiss()`
    // 收掉的是**提示**、那一页还在原处。
    m_ui->overlay()->dismiss();

    // ② 摆选项卡与那一栏**排到下一轮事件循环**：这一句这会儿还在按钮的 clicked 处理函数
    // 里，收页的收尾（`pageClosed` 那一发）还没跑完、覆盖层也还盖在内容区上 ——
    // 现在就把选项卡翻过去，翻的是它**底下**那一层，而覆盖层收干净的那一下还没发生。
    // 排到下一轮，那时这一页已经关好了（和 openResourceDetailPage 那边同一个手法）。
    QTimer::singleShot(0, this, [this, row, categoryKey, origin] {
        continueOpenDownloadCategory(row, categoryKey, origin, kDownloadOpenRounds);
    });
}

void MainWindow::continueOpenDownloadCategory(int row, const QString &categoryKey,
                                             const NavigationReturn &origin, int roundsLeft) {
    // ---------- 收页：收到干净为止，收不干净就**什么都不做**----------
    //
    // 为什么还要再收一次：`openDownloadCategory()` 里那一发 `dismiss()` 收的是**最上面
    // 那一层** —— 提示压在实例设置上面时收掉的是提示，那一页还在原处占着覆盖层那一位。
    //
    // 收不干净时**绝不能切选项卡**：切了就是把那一页孤零零地丢在别的选项卡上（它收着、
    // 看不见），而它占着覆盖层那一位 ⇒ 用户此后既回不去那一页、也开不了新的一页，还会
    // 被一次一次拽到下载页。宁可这一次什么都不做，也不要把他丢进那个状态。
    // （正常路径上收不掉是走不到的：那一页归「实例」那一颗，而它的按钮只在它露着的时候
    // 点得着 —— 见 openInstanceSettings() 里那段。留着这一条是因为"走不到"和"保证了
    // 不会走不到"是两件事。）
    OverlayHost *overlay = m_ui->overlay();
    if (overlay != nullptr && overlay->pageOpen()) {
        if (roundsLeft > 0 && overlay->dismiss()) {
            // 收到一半（这一发收回来的那一层要等收尾才真正落下去）—— 再排一轮。
            QTimer::singleShot(0, this, [this, row, categoryKey, origin, roundsLeft] {
                continueOpenDownloadCategory(row, categoryKey, origin, roundsLeft - 1);
            });
            return;
        }
        qWarning() << "下载页：实例设置那一页没能收掉，这一次不切选项卡";
        return;
    }

    m_ui->getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
    // 从实例设置进来的那一次，顺手做三件事 —— 都是"让屏幕上说的和用户以为的是同一件事"：
    // ① 「已安装版本」那一格选到**那个实例**上：这一栏下来的东西是落进"当前选中的实例"
    // 的（见 ），不替用户选上的话，"从实例 A 的页面点进来、
    // 东西却下进了实例 B"——而屏幕上没有任何东西说过会这样（用户那句「下载新模组就
    // 自动放进当前实例文件夹」说的正是这一件事）；
    // ② 版本筛子填成那个实例的游戏版本；
    // ③ 模组加载器筛子填成那个实例的加载器。
    // ②③ 是「点击下载新模组/...后自动在搜索框里填入对应的版本和加载器」那条要求。
    const bool cameFromInstance = origin.valid();
    if (cameFromInstance) {
        QComboBox *combo = m_ui->getInstalledVersionCombo();
        const int index = combo->findData(origin.instanceId);
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
    // 已经在那一行上时 `setCurrentRow()` **不发** currentRowChanged（Qt 只在"变了"
    // 的时候才发），而"第一次进这一页才去搜一次"那一发正挂在它身上 ⇒ 这里补一次。
    // 同一条路、同一个函数，重复调用是安全的（它自己判"树上已经有东西就不动"）。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const bool known = ResourceCatalog::kindFromCategoryKey(categoryKey, &kind);
    if (cameFromInstance && known) {
        fillDownloadFiltersFromInstance(kind, origin.instanceId);
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
    // 的自定义版本），填进去照样是对的，而那正是这一格该显示的东西。
    const QString gameVersion = m_versionManager->instanceGameVersion(instanceId);
    if (!gameVersion.isEmpty()) {
        if (QComboBox *versionCombo = m_ui->getDownloadFilterVersionCombo(key)) {
            versionCombo->setCurrentText(gameVersion);
        }
    }
    // 加载器那一格**只有模组页有**（别的页 getDownloadFilterLoaderCombo 回空指针）。
    //
    // 按**文字**认（「Fabric」/「Forge」/「NeoForge」/「Quilt」）而不是按 `data()`：
    // 两个来源的键**不是一套**（Modrinth 是词、CurseForge 是数字，见 loaderOptions），
    // 而文字两边一模一样 —— 按 data 认的话，用户把来源换成 CurseForge 之后这一格就再也
    // 填不上了（而且不报错、看不出来）。
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
    // 来路整个取走（一次性）：下面无论开得成开不成，这一份都不该留着。
    const NavigationReturn origin = m_downloadReturn;
    m_downloadReturn = NavigationReturn();
    m_ui->setDownloadBackVisible(false);
    if (!origin.valid()) {
        return;  // 正常走不到：那颗键只在有来路时才露面
    }
    // 回去时先把**来路那颗根页**翻回来（主页的快捷入口开出来的那页归主页，其余归实例）。
    // 「返回恢复进入前状态」里"进"的那一半就是这颗根页：不翻的话，`openInstanceSettings()`
    // 会按**当前**选项卡（下载）认家，于是从主页进来的一趟回到实例页 —— 屏幕换了一颗根页，
    // 用户没按过它。
    if (QTabWidget *tabs = m_ui->getTabWidget()) {
        if (origin.rootTab >= 0 && origin.rootTab < tabs->count()) {
            tabs->setCurrentIndex(origin.rootTab);
        }
    }
    // 回来时开在**来路那一页**上（要求「从模组点进去，返回也在模组而非概览，其余同理」），
    // 文件页的滚动位置也一起摆回去 —— 两个值同一个来源（`NavigationReturn`）。
    // 挪到 `m_instanceReturn` 是因为真正摆页的是 `openInstanceSettings()` 那一趟
    // （它读的是一位"开页时塞回去"的来路，见 `InstanceDialogController::open()`）。
    m_instanceReturn = origin;
    openInstanceSettings(origin.instanceId, origin.pageKey);
}

void MainWindow::onNavigationApplied(int sourceIndex, int index, bool reselected) {
    // 顶上那四颗导航，这一层只管**下载页那两件**（要求「在这个情况下点击
    // 顶部下载按钮就刷新页面回到主页」+「如果点击了下载新模组按钮后再点击实例按钮，功能
    // 就和返回一样，如果点两下那就是先返回再回到实例主页」）。翻页、以及"点的是当前这一
    // 颗就把上面那层收掉"那两件，仍旧是 MainWindowUI 自己接的那一条在管（那边**先接**，
    // 所以它先跑完，见 setupConnections 里那条连接的注释）—— 这里不重复做，也不去改它。
    //
    // ---------- 「实例」那一颗 = 按了「← 返回实例设置」（要求见上） ----------
    //
    // 这**不是**那一颗返回键的第二条接线（那颗键仍旧只有一处出口，见 setupConnections
    // 里那段）—— 这里只是"同一件事从另一颗键上够得着"。两条都走
    // `returnToInstanceSettingsFromDownload()`，所以"取哪一份来路、翻回哪颗根页、开在哪一页"
    // 一个字都不用写两遍。
    //
    // 判据四条：
    // ① 点的是「实例」那一颗；
    // ② **刚才是从下载页按的**（`sourceIndex`）。少了这一条，"从实例进来 →
    // 切去主页逛了一圈 → 点实例"也会被当成"回实例设置"，可那一下要的是切到实例
    // 列表（他手上那一页早就切走了）。这一位**只有 MainWindowUI 说得出口**：翻页那一下
    // 是它先跑的，这边醒过来时 `currentIndex()` 已经是新的一颗了（见那个访问器的注释）。
    // ③ 这一趟**确实是从某个实例点进来的** —— 也就是那颗返回键正露着。
    // ④ 这一层**没有别的页面占着**（典型：用户先从下载页点开了一个资源的详情页）。
    // `openInstanceSettings()` 自己会让位（静默收掉占着的那一层、
    // 排到下一轮再开，见 `runWhenOverlayFree()`）—— 所以这一条判据是
    // "这一按到底算不算'回实例设置'"，不是"那一页开不出来"：占着的时候按「实例」
    // 就照常翻页（和按「主页」「设置」一样），让位那一套交给上面那一发。
    // 留着这一条的理由是**顺序**：来路（`m_downloadReturn`）要排在那一次
    // 开页之前清掉，而"让位"是异步的 —— 两下里撞在一起会变成"返回键没了、页也没开"。
    //
    // 第二下为什么自动就成了"回到实例主页"：上面那一发把这一页开起来之后，当前选项卡
    // **就是**来路那颗根页（`NavigationReturn::rootTab`，通常是「实例」），于是再按一下
    // 正中 MainWindowUI 那条的 ② 档（点的是当前这一颗 ⇒ `overlayHost->dismiss()` = 收掉
    // 这一页，露出底下的实例列表）。那一下这一份已经被上面取走了 ⇒ 这里不接手，
    // 两下没人打架。
    const bool overlayFree = m_ui->overlay() == nullptr || !m_ui->overlay()->isOpen();
    if (index == MainWindowUI::instancesTabIndex()
        && sourceIndex == MainWindowUI::downloadTabIndex()
        && m_downloadReturn.valid() && overlayFree) {
        returnToInstanceSettingsFromDownload();
        return;
    }

    // 判据三条，缺一不可：
    // ① 点的**就是**「下载」那一颗；
    // ② **这一按是重点当前页**（`reselected`）—— 也就是"这一按没有翻页"。
    // 少了这一条，"从实例进来、切去主页逛了一圈、再点下载"也会被当成"回主页" —— 可那
    // 一下要的是**切回来**（原来那一栏、连筛子带实例都在，见 MainWindowUI 搭导航
    // 那段①），顺手清掉就成了"我明明没点返回，回实例设置那颗键却没了"；
    // ③ 这一趟**确实是从某个实例点进来的**（那一位非空 = 「← 返回实例设置」正露着）。
    // 要求里这一条是跟着"点了下载新模组"来的；平白从顶部点进下载页时，左栏挑的
    // 哪一栏、框里填的什么都是他自己摆的，那一发不该替他重置。
    //
    // ② 的**量法**必须用 `reselected`，不能写成
    // `tabs->currentIndex() == index`：上面那条 MainWindowUI 的槽先跑，它自己就会把
    // 选项卡翻过来，于是"切去了主页再点下载"走到这里时 `currentIndex()` 早就是下载页了
    // ⇒ 判据永远为真，等于没有这一条（那个症状照样发生，只有 `mw_check` T25.8 量得出来）。
    if (index != MainWindowUI::downloadTabIndex()
        || !reselected
        || !m_downloadReturn.valid()) {
        return;
    }

    // 来路那一栏先认下来：下面头一件事就是把它整个取走。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const bool known = ResourceCatalog::kindFromCategoryKey(m_downloadReturn.pageKey, &kind);

    // ① 把"从实例进来"这件事整个收起来。返回键露不露只看这一份（见 setDownloadBackVisible
    // 的调用点），所以先取走再收起来。
    m_downloadReturn = NavigationReturn();
    m_ui->setDownloadBackVisible(false);

    // ② 那一栏的筛子是**我们**灌进去的（fillDownloadFiltersFromInstance），回主页就该一并
    // 还回去 —— 留着的话，用户下一次点进那一栏看到的是"我没填过"的条件。
    //
    // 清完**还得把那一页的搜索结果扔掉**：筛子是新条件、树上那批是按旧条件搜出来的，
    // 两下里对不上 —— 正是上面筛子那一段点名的"界面上筛了、请求里没筛"。这里**不发
    // 新请求**：清空之后，下一次进那一页时 ensureResourcePageLoaded() 自然会按新的
    // 筛子搜一次（既不替用户白等一下，也不白烧一次限流额度）。
    if (known) {
        resetDownloadFilters(kind);
        if (QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind))) {
            tree->clear();
        }
    }

    // ③ 左栏回到第一栏 —— 那**就是**下载页的首页（「游戏」：在线版本清单，见
    // downloadCategoryGameIndex()）。
    //
    // 摆的是**下标**，不是"现在选中的那一栏"：这件事跟用户此刻在哪一栏无关，起点是
    // 确定的。（切过去会触发 currentRowChanged → 右栏翻页；第一栏是「游戏」，那一页
    // 不走资源搜索，所以这一下不会多发任何请求。）
    if (QListWidget *categories = m_ui->getDownloadCategoryList()) {
        categories->setCurrentRow(MainWindowUI::downloadCategoryGameIndex());
    }
}

void MainWindow::openFileResourcePage(const QString &versionId, const QString &iconKey,
                                      const QString &kindKey) {
    // 来路是 `InstanceFiles`：只有这一条路上那一页才可能摆「模组设置」（见
    // openResourceDetailPage 里那一句 —— 还要**同时**是个模组）。
    //
    // 收页之后怎么办**全在回调里**（`showPage()` 不等这一页收掉就返回了）。判据还是
    // `QDialog::Rejected`（= 用户把这一页**收掉**了）：返回箭头和 Esc 走的都是它；
    // `Accepted` 专用于「模组设置」，转入当前实例的配置文件管理。
    //
    // 必须和"压根没开出来"分开认：那一档是上面第 ① ② 步认完之后、`m_resourceHits`
    // 里那条已经不在了之类 —— 没开出来就**什么也别做**。不分开的话，用户点一下名字 =
    // 实例设置被收掉、详情页没出来、实例设置又被摆回去，看着就是白闪一下。
    //
    // 「回来时翻回哪颗根页」在这一刻量：这一页摆在哪颗根页上，来路就归哪一颗。
    // 收页那一刻再比一次 —— 中间用户可能已经切走、又从别的根页开了别的事
    // （那种收掉是 `evict()` 的"让位"，**不是**他按的返回，见下面那段）。
    QTabWidget *tabs = m_ui->getTabWidget();
    const int originTab = tabs != nullptr ? tabs->currentIndex() : -1;
    openResourceDetailPage(
        iconKey, kindKey, DetailSource::InstanceFiles, versionId,
        [this, versionId, kindKey, originTab](int result) {
            // 这一发是从 `closeOverlay()` 里发出来的（`pageClosed` 就发在那儿，那一页
            // 还没从"正在收"里走出来）。就在这一发里开别的一页等于在收尾途中又进一层，
            // 所以**排到下一轮**再开（和 `openDownloadCategory()` 那边同一个手法）。
            QTimer::singleShot(0, this, [this, versionId, kindKey, result, originTab] {
                if (result == QDialog::Accepted && !versionId.isEmpty() &&
                    kindKey == QStringLiteral("mod")) {
                    openInstanceSettings(versionId, QStringLiteral("config"));
                    return;
                }
                if (result != QDialog::Rejected || versionId.isEmpty()) {
                    return;
                }
                // ---------- 只在"来路还在"的时候回去 ----------
                //
                // `Rejected` 有两种来路：用户按了返回箭头 / Esc（要回去），以及这一页
                // 被**别处**顶掉（`OverlayHost::evict()` 的"让位"—— 典型：用户切到
                // 另一颗根页，又从那儿开了另一页）。后者这一页不是他收的、他也不在等
                // "回到刚才那一页"；照着回去就会把刚开起来那一页顶掉（或者两发并排
                // 排着、谁后到谁赢）。判据用**根页**：收页这一刻还停在这一页开出来的
                // 那颗根页上 ⇒ 来路还在。那一份"回去滚到哪儿"也一并作废 —— 它属于
                // 这条被放弃的路。
                if (QTabWidget *nowTabs = m_ui->getTabWidget(); nowTabs != nullptr &&
                        originTab >= 0 && nowTabs->currentIndex() != originTab) {
                    m_instanceReturn = NavigationReturn();
                    return;
                }
                // 回来时开在**刚才那一页**上（`kindKey` 就是那一页的键 —— 「这一页是哪
                // 一类资源」和「实例设置里哪一页」现在恰好同名，那个等式在
                // filesDownloadCategoryKey() 一处）。从模组点进去，返回也落在模组那一页
                // 而非概览，其余同理。
                openInstanceSettings(versionId, kindKey);
            });
        });
}

void MainWindow::openInstanceFileResource(const QString &versionId, const QString &pageKey,
                                          const QString &path) {
    // 模组以外的几类都是点资源名自动进入下载界面
    // （前提是匹配上了，模组的待会再说）。进的是**那个资源自己的那一页**（下载页里点
    // 一行开的就是它，见 openResourceDetailPage）——「前提是匹配上了」那句话只有这样
    // 才说得通：换成"切到下载页那一栏"就跟匹配不匹配没关系了。
    //
    // ① 这一页是"哪一类资源"：判据就是 filesDownloadCategoryKey() 那张表 —— 页键和下载页
    // 分类键现在恰好同名，可那一处才是"这两件事是一回事"的唯一出处。认不出来就回去，
    // 连覆盖层都不碰：悄悄地少收一页比收错了强。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const QString kindKey = InstanceSettingsDialog::filesDownloadCategoryKey(pageKey);
    if (kindKey.isEmpty() || !ResourceCatalog::kindFromCategoryKey(kindKey, &kind)) {
        return;
    }
    // ② 这一条是哪个资源。认不出来 = 没匹配上 ⇒ **什么都不做**（那一行的名字本来也没
    // 变成链接，正常走不到这儿；测试里点一下量得到的就是"没反应"）。
    // 表在那一层（反查那一趟填的，见 `InstanceIconController`）；这一层只拿那把钥匙。
    QString iconKey;
    if (!m_instanceIconController->fileResourceIconKey(path, &iconKey)) {
        return;
    }
    // ③ 先收掉实例设置那一页 —— 走 `dismiss()`（和 openDownloadCategory() 里同一个规矩）：
    // 它发的就是"用户按了返回箭头"那一发，收页该走的收尾（`pageClosed` 那一串）一条不少；
    // 只 `closeOverlay()` 的话那一页自己一个信号都收不到。
    if (m_ui->overlay() == nullptr || !m_ui->overlay()->isOpen()) {
        // 页已经关掉了（连着点两下、或者返回键和它撞上）。没有那一层要收，
        // 直接开新的就完了。
        openFileResourcePage(versionId, iconKey, kindKey);
        return;
    }
    m_ui->overlay()->dismiss();
    // ④ 摆到下一轮：这一句这会儿还在收页的调用栈里（名字上那个鼠标松开事件），
    // 覆盖层也还盖着 —— 现在就去开新页的话，openResourceDetailPage 开头那条
    // `overlay->isOpen()` 会当场把这次点击吃掉。
    //
    // NOTE: 键与键都在这一层认完了，排到下一轮带过去的只有那三个**字符串**：下一轮
    // 里这一页早就没了，那时候再去查"这一条是哪个资源"就等于查一份已经被清掉的
    // 上下文（和 openDownloadCategory 那边带 `row` 过去是同一个道理）。
    // `versionId` 也在这一层就绑好：它是"回来时把哪一页放回去"的唯一依据，而实例
    // 设置那一页这会儿已经在往下撤了。
    QTimer::singleShot(0, this, [this, versionId, iconKey, kindKey] {
        openFileResourcePage(versionId, iconKey, kindKey);
    });
}

// ---------- 实例设置那一扇窗背后的页面数据与动作（配置 / 世界 / 模组 / 服务器列表）----------
// 在 `src/MainWindowInstanceSettingsWiring.cpp`（同一个类，另一个翻译单元）。


bool MainWindow::overlayAcceptsNewPage() const {
    // **只回答"空着没有"，一个字节都不往外弹**（要求见 .hpp 那段：
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
        // 绝大多数时候走的是这一支：这一层本来就空着，**同一轮就把页开出来**。
        open();
        return;
    }
    // ---------- 这一层被占着：先让位，再开 ----------
    //
    // 占着它的可能是**提示**，也可能是**整页**；整页那一档还可能是"收着的"（用户在别的
    // 选项卡上逛，而它一直占着这一位）。这里的规矩是**静默收掉，直接继续**。
    if (roundsLeft <= 0 || !overlay->evict()) {
        // 收不掉（那一层不认这一发 / 轮数用完了）。**到这一步才出声**：挡住用户操作的
        // 事必须说清楚，而且得说清怎么办。
        warnOverlayBusy();
        return;
    }
    // 收页那一发对"非阻塞开的那一页"是同步落定的（reject → finished → closeOverlay），
    // 可就当它是**请求**来排：这一趟调用栈随后还要退出，而下一轮里可能又有别的东西
    // 把这一层占上了。∴ 排到下一轮，而且**下一轮再从头上过一遍**。
    QTimer::singleShot(0, this, [this, open, roundsLeft] {
        runWhenOverlayFree(open, roundsLeft - 1);
    });
}

void MainWindow::openPageWhenIdle(const std::function<void()> &open) {
    if (!open) {
        return;
    }
    OverlayHost *overlay = m_ui->overlay();
    if (overlay == nullptr) {
        open();
        return;
    }
    // 空着 / 被占着两条路都在 OverlayHost 里（排队 + 收干净之后下一轮再摆）。
    overlay->queuePageOpen(open);
}

void MainWindow::warnOverlayBusy() {
    OverlayHost *overlay = m_ui->overlay();
    if (overlay == nullptr) {
        return;
    }
    // 挡在这儿的是**已经开着的那一层**（一层只放得下一个页面，见 OverlayHost.hpp 那段）。
    // 它可能是"正铺在眼前"（用户点的是底栏那些没被盖住的按钮），也可能是"收着"。
    //
    // NOTE: **这一条不能不出声。** 开页那一下拿到 nullptr / false 时，调用方把对话框
    // 建了、一个字节没写、原地销毁 —— 用户点了实例行（或者组行那颗齿轮），屏幕上
    // **什么都没发生**，也没有任何提示。
    //
    // NOTE: 起这一句只在**收不掉**的时候才出现（正常那一档是静默收掉 + 直接
    // 继续，见 runWhenOverlayFree）—— 真走到这儿说明那一层连收页那一发都没认
    // （页面早就不卡嵌套循环了，只可能是这一层的状态出了意外），
    // 这时候唯一能做的是把话说清楚。
    const QString title = overlay->pageTitle();
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

// ---------- 实例文件与实例设置的读写（读一页 / 改一批 / 删 / 拷 / 落盘）----------
// 在 `src/MainWindowInstanceFiles.cpp`（同一个类，另一个翻译单元）。


// ---------- 日志 / 崩溃 / 导出（日志窗、错误报告、启动脚本、整合包）----------
// 在 `src/MainWindowSessionExport.cpp`（同一个类，另一个翻译单元）。


void MainWindow::repairInstanceFiles(const QString &versionId) {
    if (versionId.isEmpty()) {
        return;
    }

    // 三道守卫，各说各的：用户点的是"检查这个实例"，说清"现在为什么不行"比一句笼统的
    // "忙"有用。第一道看的是**这个实例自己**（不等于"有下载在跑"——别的实例在装不该拦它，
    // 但这里点的是补全，而补全要开一批下载，所以第二道照样得拦）。
    const bool thisInstanceBusy = m_installOccupancy.modifying(versionId);
    if (thisInstanceBusy) {
        InlineMessage::information(
            m_ui->overlay(), tr("现在做不了"),
            tr("「%1」这会儿正在装（或者正在导入 / 正在检查）。等它完了再来。")
                .arg(instanceDisplayName(versionId)));
        return;
    }
    if (m_downloadInFlight || m_versionManager->isDownloading()
        || (m_serverInstall != nullptr && m_serverInstall->ownsDownloadBatch())) {
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
    const QStringList flagged = m_integrityController->flaggedPaths(versionId);
    appendLog(tr("开始校验「%1」的文件（已存在的会跳过）...").arg(instanceDisplayName(versionId)));
    // 顺序：**先记名字再置忙**。setDownloadUiBusy() 里那句 refreshInstalledVersions() 读的
    // 就是这个判据（正在补全的实例不该出现在启动列表里），反过来的话它漏得出那一次刷新。
    m_installOccupancy.setBatch(InstallOccupancy::Batch::Repair, versionId);
    setDownloadUiBusy(true);
    m_versionManager->repairVersionFiles(versionId, flagged);
}

void MainWindow::maybePrefetchAssetDirector(const QString &versionId) {
    if (versionId.isEmpty() || m_versionManager == nullptr) {
        return;
    }
    // 先问一句"要不要做"：绝大多数实例认不出消费者模组，那一支**连任务行都不该有**。
    // 这一步只读盘（`mods/` 文件名 + 盘上已有的对象），不发信号、不写盘。
    const VersionManager::AssetDirectorPrefetchPlan plan =
        m_versionManager->assetDirectorPrefetchPlan(versionId);
    if (!plan.applicable) {
        return;
    }
    if (!plan.refusal.isEmpty()) {
        // 超上限：说清是哪套、多大，一个字节都不下。这条只在真的点了导入时出现。
        appendLog(TextMarks::failed(tr("「%1」的首启资源没有预取：%2"))
                      .arg(instanceDisplayName(versionId), plan.refusal));
        InlineMessage::warning(m_ui->overlay(), tr("首启资源没有预取"),
                               tr("「%1」的首启资源没有预取：\n\n%2\n\n"
                                  "不影响启动：游戏第一次启动时 AssetDirector 会自己下。")
                                   .arg(instanceDisplayName(versionId), plan.refusal));
        return;
    }
    if (plan.upToDate) {
        appendLog(tr("「%1」的首启资源本来就齐了（%2 个对象），没有下载。")
                      .arg(instanceDisplayName(versionId))
                      .arg(plan.objects));
        return;
    }
    // 三道守卫：这个实例（或别的批次）正忙就不抢批次状态。数据层那边还会再挡一次。
    if (m_downloadInFlight || m_versionManager->isDownloading()
        || m_installOccupancy.anyModifying()) {
        appendLog(tr("「%1」的首启资源这次没有预取（有别的下载或安装正在进行）。")
                      .arg(instanceDisplayName(versionId)));
        return;
    }
    // 顺序：**先记名字再置忙**（`DownloadTaskLedgerController::begin()` 的判据就是这几个 id）。
    m_installOccupancy.setBatch(InstallOccupancy::Batch::Prefetch, versionId);
    setDownloadUiBusy(true);
    appendLog(tr("正在预取「%1」的 GTNH 首启资源（%2 个对象，预计 %3 MiB，写入 %4）…")
                  .arg(instanceDisplayName(versionId))
                  .arg(plan.objects)
                  .arg(plan.bytesToDownload / (1024.0 * 1024.0), 0, 'f', 1)
                  .arg(QDir::toNativeSeparators(AppPaths::assetsDir() + QStringLiteral("/asset_director"))));
    if (!m_versionManager->prefetchAssetDirectorResources(versionId)) {
        // 数据层一个字节都没动（不适用 / 被拒 / 已齐）：它可能已经同步发过终态（那时上面那个
        // 分支已经收完尾了），也可能什么都没发 —— 后者要在这里把忙态和任务收回来。
        if (m_installOccupancy.is(InstallOccupancy::Batch::Prefetch, versionId)) {
            m_installOccupancy.clearBatch(InstallOccupancy::Batch::Prefetch);
            setDownloadUiBusy(false);
        }
    }
}

// ---------------------------------------------------------------------------
// 完整性检查与修复
// ---------------------------------------------------------------------------

bool MainWindow::integrityScanningVersion(const QString &versionId) const {
    return m_integrityController->scanning(versionId);
}

void MainWindow::submitIntegrityCheck(const QString &versionId) {
    IntegrityController::BusyFacts busy;
    busy.targetBeingModified = m_installOccupancy.modifying(versionId);
    busy.anotherDownloadRunning = m_downloadInFlight || m_versionManager->isDownloading()
                                  || (m_serverInstall != nullptr
                                      && m_serverInstall->ownsDownloadBatch());
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
// 世界与实例备份
// ===========================================================================
//
// 四个入口（世界页三颗 + 概览那两颗）都收在这一段里。**干活的那一层在
// `src/manage/BackupFiles`**（走目录、算 sha1、起 PowerShell 打包、还原之前做安全快照），
// 这一段只负责：把描述拼出来、起一趟后台活、把结果记进任务账本并弹给用户。

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
    // 这一条正是"搬功能之前先想清楚谁还按名字喊它"：**mw_check 是照着名字驱动它的**
    // （`QMetaObject::invokeMethod(&w, "startBackupRestore", …)`，T35.3 / T35.4 那一节
    // 拿它验"还原那一趟跑完了""别人的包 ⇒ 账本上是失败"）。invokeMethod 找不到就返回
    // false，断言会静默失效，屏幕上看着像"还原功能坏了"。所以实现挪走时这里要留一个
    // 同名的薄壳 —— 这是这一层和验证工程之间唯一的那条约定（见 tests/mw_check 那一节的说明）。
    //
    // 这一发**一个界面调用点都没有**（界面上那两条路是上面那两个：世界页
    // 「恢复备份…」和概览「从快照恢复…」，zip 和落点由它们自己决定）。
    return m_backupController->startRestore(zipPath, targetParentDir, title, targetId, successTitle);
}

MainWindow::JavaMismatchAnswer MainWindow::askAboutJavaMismatch(
    const QString &versionId, const JavaSettingsOverride &override, int expectedMajor) {
    // ① 只有"实例钉了 Java"才有这个问题。全局档不匹配时 ensureJavaForVersion() 会
    // 静默换一个匹配的 —— 那是有意的行为（用户没钉任何东西，替他挑一个正确的
    // 运行时不叫"换掉他的选择"），不动它。
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
    // 不存在/认不出来"，和"版本不符"是两件事，它继续落到现有的「Java 运行时缺失」
    // 硬错误那条路上。主版本相同当然更不用问。
    QString actualVersionText;
    const int actualMajor =
        GlobalJavaSettings::probeJavaMajorVersion(*override.javaPath, &actualVersionText);
    if (actualMajor <= 0 || actualMajor == expectedMajor) {
        return JavaMismatchAnswer::NotAsked;
    }

    // 内容区中间那一块（也就是「像 java 设置这种小弹窗」），不是独立窗口。
    // 关掉 / 按 Esc / 点外面都**不动 `choice`**，它的初值就是 Abort
    // （见那个对话框的 .cpp 末尾）。
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
    // 偏好和"这次启动能不能跑"是两件事，不该因为它把用户刚授权的启动掐掉；
    // 而它自己的后果（下次还问一句）是无害的、也是看得见的。
    //
    // NOTE: 勾选框是**独立于选了哪条路**的一句表态（它的字面意思就是这样），所以
    // 「改用匹配的 Java」那一次也照写不误 —— 要是以后给这个实例钉了个
    // 对不上的 Java，"别问了"这句话仍然算数。
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
        // 手动档：io 里那两个数字是人定的值（可能来自全局、也可能来自这个实例
        // 的覆盖），就是这次要用的值，一个都不许动。
        return;
    }

    // NOTE: 自动档下 xmx/xms 的覆盖**在这里被丢掉**，这是有意的：自动档的定义就是
    // "按系统内存现算"，用户单独钉一个 Xmx 而让 Xms 自动算，只会得到一对
    // 谁也说不清的数字。对话框那边是同一件事的另一半 —— 自动档下那两行整行
    // 禁用，点确定时也不把它们写进 meta。
    if (!override.autoMemory.has_value()) {
        // 自动这一档来自**全局**：refreshSuggestedMemory() 会按当前系统内存
        // 重算并更新全局那份值（落盘由设置页 / 下次 save() 负责）。
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
    // xmx/xms（它们本来没有），而这对值是死的 —— 自动档每次启动都重算，
    // 下次读出来只会让「实例设置…」里那两行显示成"自定义 + 一个用户没填过的数"。
    // 跨作用域污染的反面不是"多写一份"，而是"只写在这次要用的值上"。
    const SystemMemory::Usage usage = SystemMemory::read();
    // 数一数这个实例装了多少模组（见 .hpp 那条 NOTE：只在这一支数）。
    // 同一个 200 模组的整合包会比原版多要一截内存 —— 自动档要认整合包规模。
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
 * 返回整条路径 —— 那一行总得有字，空的会变成一个看不出是什么的空白行。
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
    // NOTE: 「移除」不在这儿管：齿轮菜单里那一项的灰/亮由它自己按"这颗齿轮属于哪一行"
    // 算（见 FolderMenu 里那段 protectedFolder）—— 菜单是照着**哪一行那颗齿轮**弹的，
    // 和"列表当前选中哪一行"可以不是同一行，这是它比按"当前选中行"算准的地方。
    m_ui->getAddFolderButton()->setEnabled(!m_downloadInFlight);
}

void MainWindow::applyCurrentFolderToPaths() {
    // 全局路径的**唯一**切换点。此后 AppPaths 的每一个函数（versions / assets /
    // libraries / java）都对着这一份 .minecraft，直到下一次切换。
    const QString root = m_folderManager->currentFolder();
    // 自带那份**不设覆盖**（传空串 = 用默认那份）：启动器被整个拷到别处之后，默认路径
    // 跟着 exe 走，而不是钉死在上次记下的那个老位置。
    AppPaths::setMinecraftDir(FolderManager::isDefaultFolder(root) ? QString() : root);
    for (const QString &message : BackupFiles::recoverInterruptedRestores(AppPaths::minecraftDir()))
        appendLog(message);
    // 切到一个“之后才被搬过来”的游戏根时也补一次；启动时已经扫过全部根，这里是幂等的。
    for (const QString &message : InstanceUpgrade::recoverInterruptedInRoots({root}))
        appendLog(message);
    appendLog(tr("当前游戏文件夹：%1").arg(QDir::toNativeSeparators(root)));
    // 借文件的来源跟着当前文件夹一起变（当前那一份由下载器自己跳过）。
    syncDownloadReuseSources();
}

void MainWindow::syncDownloadReuseSources() {
    if (m_versionManager == nullptr || m_folderManager == nullptr) {
        return;
    }
    // 唯一出处见 FolderManager::reuseSourceFolders()：用户添加的 + 官方启动器那份。
    // 这里**不做**任何"再补几个可能的位置"的加工 —— 用户明确要求不许扫盘。
    m_versionManager->setReuseSourceFolders(m_folderManager->reuseSourceFolders());
}

/**
 * @brief 日志行前面那个"[实例名] "。
 * @param sessionId 哪一份会话；0 或者查不到（会话已经结束了）时返回空串。
 *
 * 同时开着好几份时，一串"[游戏输出] xxx"根本分不出是谁说的 —— 多开这件事本身就把
 * "哪一行是谁"从显然变成了不显然。
 *
 * NOTE: 查不到就**不加前缀**、也不写"(未知)"：那种行是"又开一个被拦下了"这类不挂在
 * 任何实例上的话（sessionId == 0），或者会话刚好在这一行之前结束了。硬安一个名字
 * 比不写更糟。
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

void MainWindow::onSessionDiscarded(int sessionId) {
    m_gameSessionController->onSessionDiscarded(sessionId);
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

void MainWindow::onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson) {
    if (versionJson.isEmpty()) {
        appendLog(tr("获取版本 %1 的信息失败。").arg(versionId));
        // 账本上那件任务也到此为止（这一步还没进到"下载文件"，但用户点的那件事已经黄了）。
        // NOTE: 收尾也在 setDownloadUiBusy(false) **之前**，理由和
        // `DownloadCompletionController::onFinished` 那边逐字一样。
        m_downloadTaskLedger->finish(tr("没能取到版本 %1 的信息。").arg(versionId));
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
