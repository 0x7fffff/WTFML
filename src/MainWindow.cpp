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

// ============================ 任务中心 ============================
//
// 这一小片是**产品侧通往账本的唯一一道门**：上面那些执行者（下载 / 版本 / 启动）的信号
// 在这里被翻成一次 `begin()` / `setProgress()` / `succeed()`，反过来底栏和任务中心面板
// 只读账本、不认识任何一个执行者。
//
// NOTE: 这几个函数里一个汉字都不该进日志 —— 规矩是「进度不进日志」（日志里那
// 一份是给人事后排查的，几千个资源对象各写一行会把它淹掉）。

void MainWindow::updateBottomBarTaskSummary() {
    const QDateTime now = QDateTime::currentDateTime();
    // 收起态显示谁 —— **唯一出处**在账本层那条纯函数里（那一串优先级 + "刚完成那一
    // 项还露着"的时间窗）。这里一个字都不加规则：加了就等于有两套优先级。
    const Tasks::TaskSummary picked = m_activityCenter->collapsedSummary(
        now, MainWindowUI::taskSummaryRecentWindowMs());

    MainWindowUI::BottomBarTaskSummary summary;
    summary.hasTask = picked.id != Tasks::invalidTaskId();
    summary.state = ActivityCenterController::presentationState(picked.state);
    summary.percent = picked.percent;
    summary.extraCount = picked.extraCount;
    summary.title = picked.title;
    if (picked.state == Tasks::TaskState::Running && m_activityCenter->transferSpeed(picked.id) >= 0)
        summary.title += QStringLiteral(" / %1/s").arg(
            InstanceFilesPage::formatSize(m_activityCenter->transferSpeed(picked.id)));
    // 红点看的是"**有没有没看过的失败**"，和正显示的是哪一项无关（正在下载而上一件
    // 事失败了，两个信号都该看得见）—— 这条也是账本层算的。
    summary.unreadFailure = picked.hasFailure;

    // ---------- 同步等待压在账本上面 ----------
    // 界面这一刻**是真的不响应**的（见 enterSyncWait()：UI 线程在等网络或等进程），
    // "在等什么"这句话比"哪件任务排第一"更该被看见。
    //
    // 不压的话还有一个具体的坏处：一份正在跑的下载每 200 ms 来一帧（见
    // onVersionDownloadStats），那一帧会把这句话顶掉。那几帧只改**任务自己的** detail，
    // 改不到这一格的标题。
    //
    // NOTE: 它**不假装是一件任务**（不往账本里塞一行）：要的只是"别画成占位话、条子画成
    // 不确定态"。点开的那块面板画的是**账本**，所以那句等待话在面板里看不到 ——
    // 它属于哪件任务，那件任务的 detail 里会写（启动那一路就是启动任务）。
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
/// 判据必须和 `onTaskDetailAction()` 里那条取消分派**逐字对齐** —— 那儿能做的只有三件
/// 事（服务端安装那一趟、`VersionManager` 那一批下载、后台导入世界那一趟）。所以这里也只问这几处，
/// **不在这一层按 kind 或状态另推一遍**（推错的下场：装作某一阶段还能取消）。
///
/// 为什么非要现问一遍：账本里那位 `canCancel` 是 `begin()` 按**种类**记的（"这一类活
/// **允许**取消"），可这一类活里有几段**就是取消不掉**的 —— 加载器的安装器进程正在跑
/// 的时候（那是个 `QProcess`，不在下载批次里）。整合包后台解压和最终校验则已有合作式取消，
/// 由 `VersionManager::canCancelDownload()` 现问工人状态，不再仅依赖下载器是否繁忙。
bool MainWindow::cancelWouldWork(const Tasks::TaskSnapshot &task) const {
    if (m_modTransferCancels.contains(task.id)) return true;
    // ⓪ 「实例更新」父任务：准备核心那一段可以取消（文件事务那一段由流程自己
    // 把能力位关掉，这里也问同一份入口）。
    if (m_updateFlowTask != Tasks::invalidTaskId() && task.id == m_updateFlowTask
        && m_updateFlowCancel && m_updateFlowCancelAvailable
        && m_updateFlowCancelAvailable()) {
        return true;
    }
    // ③ 后台导入世界 / 原地升级 / 回滚：工人和引擎都在安全点看这一位，取消得动。
    // 判据和 `cancelTaskFromDetail()` 里那一发逐字对齐。
    if (m_instanceTransferController != nullptr
        && m_instanceTransferController->canCancelTask(task.id)) {
        return true;
    }
    // ④ 备份 / 快照 / 还原：引擎在快照、解压、校验与提交前检查处看取消位
    // （仅最终改名那一下不可中断）。判据和 `cancelTaskFromDetail()` 同口径。
    if (m_backupController != nullptr && m_backupController->canCancelTask(task.id)) {
        return true;
    }
    if (m_presetController && m_presetController->ownsModTask(task.id)) return true;
    if (m_toolboxController && m_toolboxController->ownsDownloadTask(task.id))
        return m_toolboxController->canCancelDownloadTask(task.id);
    if (m_downloadBatches && m_downloadBatches->canCancel(task.id)) return true;
    // ① 服务端安装那一趟：自己跑 `QProcess` + 自己的元数据请求，不走下载批次。
    if (m_serverInstall != nullptr && m_serverInstall->isRunning()
        && m_serverInstallTaskId != Tasks::invalidTaskId() && task.id == m_serverInstallTaskId) {
        return true;
    }
    // ② 其余的（下载 / 安装 / 导入）底下都是 `VersionManager` 那一批：有批次在飞才真能掐。
    // 判据里**必须**带"这一项自己就是那一批"。只看"下载器忙不忙"的话，
    // 任何被记成 Install 的**别种**任务（典型：升级副本那种后台搬文件，见
    // InstanceDialogController 的 begin）都会长出一颗「取消」，按下去掐掉的却是当时
    // 正好在飞的**另一批**版本下载。`cancelTaskFromDetail()` 里那一句分派必须同口径。
    // 再补：`VersionManager` 上可以同时挂两批，账本里也就有两个号 ——
    // ① `m_downloadTaskLedger->task()`（`DownloadTaskLedgerController::begin()` 记的版本 /
    //    整合包 / 资源下载）；
    // ② `m_updateBatch.task`（「更新某个实例的 N 个资源」，见
    // `updateInstanceResources()`）。两批都是 `canCancelDownload()` 的真主人，少认一个
    // 就是"取消按钮整个消失"。别的号（升级副本、导入搬文件…）仍然一律不认。
    const bool ownsVersionBatch =
        task.id == m_downloadTaskLedger->task()
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
    // 兼容入口：面板的 rowActivated 和验证工程按这个名字调用。
    m_taskDetail->open(key);
}

void MainWindow::onTaskDetailAction(const QString &key, TaskDetailPage::Action action) {
    m_taskDetail->action(key, action);
}

void MainWindow::cancelTaskFromDetail(const QString &key) {
    bool transferParsed = false;
    const auto transferId = key.toULongLong(&transferParsed);
    // 「实例更新」父任务：准备阶段这一颗接到真的那一批下载上（文件事务阶段流程会把
    // 能力位关掉，所以这里不会把提交中的副本/快照打断）。
    if (transferParsed && m_updateFlowTask != Tasks::invalidTaskId()
        && transferId == m_updateFlowTask && m_updateFlowCancel && m_updateFlowCancel()) {
        return;
    }
    if (transferParsed && m_modTransferCancels.contains(transferId)) {
        m_modTransferCancels.value(transferId)();
        return;
    }
    // ③ 后台导入世界 / 原地升级 / 回滚：和 `cancelWouldWork()` 里那一问同口径 ——
    // 只有这一趟自己的取消位会被置上，不会顺手掐到别人的下载或别的实例的活。
    // 「取消之后会怎样」那句话由控制器按任务种类写（见 `InstanceTransferController::cancelTask()`）。
    if (transferParsed && m_instanceTransferController != nullptr
        && m_instanceTransferController->cancelTask(transferId)) {
        return;
    }
    // ④ 备份 / 快照 / 还原：和 `cancelWouldWork()` 里那一问同口径 —— 只掐这一趟。
    if (transferParsed && m_backupController != nullptr
        && m_backupController->cancelTask(transferId)) {
        appendLog(tr("已请求取消备份 / 还原；停下的那一趟不会发布半成品。"));
        return;
    }
    bool directKey = false;
    const auto directId = key.toULongLong(&directKey);
    if (directKey && m_toolboxController && m_toolboxController->cancelDownloadTask(directId)) return;
    if (directKey && m_downloadBatches && m_downloadBatches->cancel(directId)) return;
    if (directKey && m_presetController && m_presetController->ownsModTask(directId) && m_presetController->cancelModInstall()) return;
    // 取消要**按这一项是谁**分派：服务端安装那一趟和版本/资源下载那一批不是同一份
    // 状态（一个自己跑 QProcess + 自己的元数据请求，一个走 `VersionManager` 的批次）。
    // 两边都无脑叫一遍的话，取消一次服务端安装会顺手把别人正在跑的下载掐掉 ——
    // 而下载那一批可能是用户在下载页上排的、跟这一项毫无关系。
    //
    // NOTE: 这一条**留在 MainWindow**（`TaskDetailController` 只是按名字转手过来）：判据
    // 必须和 `cancelWouldWork()` 那条**逐字对齐** —— "那颗按钮露不露"和"按下去做的
    // 是哪一件事"是同一件事的两面，摆在一起才看得住。
    if (m_serverInstall != nullptr && m_serverInstall->isRunning()
        && m_serverInstallTaskId != Tasks::invalidTaskId()
        && key == QString::number(m_serverInstallTaskId)) {
        m_serverInstall->cancel();
        return;
    }
    // 和 `cancelWouldWork()` 的 ② 逐字同口径：版本下载 / 整合包导入那一批认
    // `m_downloadTaskLedger->task()`，「更新某个实例资源」那一批认 `m_updateBatch.task`。
    // 两者底下是同一个下载器，所以都是同一条 `cancelActiveDownload()`。
    if (m_versionManager != nullptr
        && (key == QString::number(m_downloadTaskLedger->task())
            || (m_updateBatch.task != Tasks::invalidTaskId()
                && key == QString::number(m_updateBatch.task)))) {
        // 取消走的是和"真失败"同一条终态信号（success=false），那几个槽
        // 分不出来 —— 于是用户刚点完取消就被弹一句「下载失败」、账本上记成 Failed、底栏
        // 还亮起一颗"没看过的失败"红点。这一位只在下面这一发里为真：终态信号是**同步**
        // 发完的（`cancelActiveDownload()` 返回之前），所以槽里读到的就是"这一次是取消"。
        m_downloadTaskLedger->setUserCancelInFlight(true);
        const bool cancelled = m_versionManager->cancelActiveDownload();
        m_downloadTaskLedger->setUserCancelInFlight(false);
        if (cancelled) {
            appendLog(tr("下载已取消。已经下好的文件留在原处。"));
        }
    }
}

void MainWindow::retryTaskFromDetail(const QString &key) {
    // 只认**这一项自己**那一批（`retryWouldWork()` 就是这条判据的唯一出处）：界面那边
    // 已经把不合条件的那颗按钮收起来了，这里是第二道闸 —— 页对象是**建一次留着**的，
    // 用户完全可能在页开着的时候又失败了一批，而按钮画的还是上一帧那份。
    bool parsed = false;
    // 行钥匙是 64 位的 `TaskId`（见 `ActivityCenterController::markViewed()` 那段的 NOTE）。
    const Tasks::TaskId retryId = key.toULongLong(&parsed);
    if (!parsed || m_versionManager == nullptr) {
        return;
    }
    const Tasks::TaskSnapshot retryTask = m_activityCenter->tasks()->snapshot(retryId);
    if (!retryWouldWork(retryTask)) {
        return;
    }
    // 忙的时候要**说一句**：`retryDownload()` 会因为
    // 「一次只有一批」返回 false，界面一声不吭就是"点了没反应"。
    //
    // 判据里补上 `m_downloadInFlight`。只问 `isDownloading()` 有一个
    // **真的窗口**：版本安装刚点下去、正在取 version.json 的那一段（元数据作业，
    // 不计进 `DownloadManager::isBusy()`），忙态已经是 true、账本里"安装 X"那一件
    // 也已经建好了，可这里会放行 —— 于是 `adopt(newTask)` 把安装那一件
    // 的号**顶掉**。之后安装那一路收尾（`onVersionDetailsFetched()` 失败支 /
    // `onAllDownloadsFinished()`）收的是**别人的号**，自己那件永远停在"进行中"。
    // 服务端安装那一段同理：它的下载批次是共用下载器上的，正在下的那几秒 `isDownloading()`
    // 已经为真；这里补的是"它正占着那一批"那一位（安装器跑着的那几分钟不算）。
    if (m_downloadInFlight || m_versionManager->isDownloading()
        || (m_serverInstall != nullptr && m_serverInstall->ownsDownloadBatch())) {
        InlineMessage::information(
            m_ui->overlay(), tr("现在做不了"),
            tr("已经有一个下载或者安装任务在跑了，等它结束再重试这几个文件。"));
        return;
    }
    // 重试也要在任务中心里看得见：账本里原来那一件已经是终态了（终态只落一次），
    // 不另起一行的话，这一次重试期间看不到进度、也取消不了。名字沿用原来那一件。
    //
    // 号和名字都要在 `retryDownload()` **之前**摆好：失败清单里那几个文件要是盘上
    // 其实都已经好好的（"重下 = 校验跳过"），这一批会**同步**收尾 —— 先摆好才认领得到
    // 那一条终态。名字从数据层取（`retryBatchLabel()`），因为 retryDownload() 一进去
    // 就把失败清单吃掉了；空名字 = 版本文件那一路，终态走 `versionDownloadFinished`。
    const QString retryLabel = m_versionManager->retryBatchLabel();
    const Tasks::TaskId newTask = beginTask(
        retryTask.kind, tr("重试：%1").arg(retryTask.title), retryTask.targetId);
    m_downloadTaskLedger->adopt(newTask);
    m_downloadTaskLedger->setResourceLabel(retryLabel);
    if (!m_versionManager->retryDownload()) {
        // 正常走不到（忙态上面刚问过）：别在账本里留一条永远"进行中"的行。
        m_activityCenter->tasks()->fail(newTask, tr("没能开始重试。"));
        m_downloadTaskLedger->clear();
        return;
    }
    // 那一格失败清单**刚被这一按吃掉了**（`retryDownload()` 自己 reset）⇒ 号也跟着
    // 松开，不然同一颗按钮留在页上还能再点一次（再点一次什么都不会发生）。
    m_activityCenter->markRetryConsumed();
    appendLog(tr("正在重试上一次没下下来的那几个文件…"));
}

Tasks::TaskId MainWindow::beginTask(Tasks::TaskKind kind, const QString &title,
                                    const QString &targetId,
                                    const QList<Tasks::TaskStep> &steps) {
    return m_activityCenter->begin(kind, title, targetId, steps);
}

void MainWindow::finishTask(Tasks::TaskId id, bool success, const QString &errorSummary) {
    // 那格失败清单只认**这一批的主人**（见 `DownloadTaskLedgerController::owner()`）。
    // **不能**问 `canRetryDownload()`（全局）：那样任何一件任务在"清单还攥着"
    // 的时候收尾，账本都会把重试资格记到**它**头上：
    // · 真失败的那一件（比如补全文件）上的「重试」凭空消失；
    // · 服务端安装 / 导出 / 启动这一类**跟下载批次毫无关系**的任务反而长出「重试」，
    // 点下去重做的是别人的文件。
    const bool ownsDownloadBatch = m_downloadTaskLedger->owns(id);
    const bool retryPayloadAvailable = ownsDownloadBatch && m_versionManager != nullptr
                                       && m_versionManager->canRetryDownload();
    m_activityCenter->finish(id, success, errorSummary, retryPayloadAvailable);
    // 收过尾了，这一批不再"占着"重试资格 —— 清单本身（m_failedBatch）还在，
    // 露不露由账本里那一位 m_retryableTaskId 说了算。
    if (ownsDownloadBatch) {
        m_downloadTaskLedger->clearOwner();
    }
}

void MainWindow::setUpdateFlowCancel(Tasks::TaskId task, std::function<bool()> cancel,
                                     std::function<bool()> available) {
    m_updateFlowTask = task;
    m_updateFlowCancel = std::move(cancel);
    m_updateFlowCancelAvailable = std::move(available);
}

void MainWindow::clearUpdateFlowCancel(Tasks::TaskId task) {
    if (task == Tasks::invalidTaskId() || m_updateFlowTask == task) {
        m_updateFlowTask = Tasks::invalidTaskId();
        m_updateFlowCancel = nullptr;
        m_updateFlowCancelAvailable = nullptr;
    }
    m_downloadTaskLedger->dropParentIf(task);
}

void MainWindow::startPlayerSkinLoad() {
    // NOTE: 六处调用点（开窗末尾、登录成功、切账号、换版本、换完外观、退出登录）都走
    // 这条"把外观拉一遍"的路。
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
    // 收到的两张图一定是**画得出来的**（载不进来时那一边已经退回默认外观了），所以这里只管
    // 摆，不再有一句"这张图打不开怎么办"。
    m_ui->getSkinViewer()->setSkin(skin, cape, slim);
    if (m_toolboxController) m_toolboxController->setPlayerSkin(skin, cape, slim);
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
    // 这条路**只读本地已经躺着的东西**：不联网、不换令牌、不读客户端 JAR。
    // 面板是"点一下就弹出来"的，要求的前提也是「（因为选择/登录过，所以肯定会有缓存，
    // 应该不会遇到丢失的情况）」—— 所以这里按"缓存里有就用、没有就自己画一个"来收尾，
    // 而不是去把缺的那张拉下来（拉皮肤要网络、要令牌，还要为每一行各等一次）。
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
    // 缩一下就是这一行的图。
    //
    // 这一条必须排在最前面。它是"面板上这一行"和"底栏那颗"是同一张脸的**唯一保证** ——
    // 走下面那几条缓存路的话，当前账号正在"随机皮肤还没下回来"那一档时，面板会给出一张
    // 缓存里的旧图（或者干脆一个首字母方块），同一个窗口里同一个人两种脸。
    // NOTE: 缩放的插值方式跟着 headAvatar() 那套走（最近邻）—— 那张图本来就是 8×8
    // 放大出来的像素画，平滑插值只会把脸糊掉。
    if (account.isCurrent && !m_avatarPixmap.isNull()) {
        return m_avatarPixmap.scaled(size, size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    if (!account.offline) {
        // ② 正版账号：缓存里那份档案写着他的皮肤地址，图按地址的 sha1 躺在同一个缓存目录里
        // （PlayerSkinStore 的两级缓存，见那边的文件头）。地址是内容哈希，所以"本地有这张
        // 图"就等于"这是他当前的皮肤"。
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
        // 它排在这里是为了和 PlayerSkinController::reload() 的次序一致：那个函数里手动
        // 挑的也排在随机 / 算出来的前面。次序不一致的症状就是"主页是这张、面板是那张"。
        const QPixmap manualFace = faceFromTexture(OfflineSkin::skinPathFor(account.uuid));
        if (!manualFace.isNull()) {
            return manualFace;
        }
        // ④ 其次是**随机挑的那张**（RandomSkinStore 记在盘上的地址）。挑过就有地址，图和
        // 正版走同一个缓存 —— 同一个账号在底栏、面板、主页看到的是同一张脸。
        // NOTE: 认人用 account.uuid（离线账号的 uuid 是 md5("OfflinePlayer:"+名字)，
        // 和 startPlayerSkinLoad() 里读的是同一个值），不是 m_authManager 那个
        // —— 后者对离线账号是空串。
        const QString chosen = RandomSkinStore::loadChosenSkinUrl(account.uuid);
        if (!chosen.isEmpty()) {
            const QPixmap face = faceFromTexture(PlayerSkinStore::cachePathForUrl(chosen));
            if (!face.isNull()) {
                return face;
            }
        }
        // ⑤ 再退一步：**算出来的**那张官方默认皮肤的缓存图（见 DefaultSkin）。
        //
        // 它的缓存名带三段（账号 / 版本 / 条目），所以这里得先猜"当初是按哪个实例算的"。
        // 猜错只是 QFile::exists 落空，**绝不会触发读 JAR** —— 那一条路要起一次
        // PowerShell（几百毫秒），挂在"点开面板"这一下上是不行的。所以这里**直接拼
        // cachePathFor()**，一步都不走 resolve()。
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
    // 账号（列表里那条只是个凭据副本）、刚加进来还没挑过皮肤的离线账号，本来就没有图。
    // 面板不能因为某一项没图就空着那一格（三行的行首一高一低比一个方块更难认）。
    return QPixmap::fromImage(SkinModel::letterAvatar(account.name, size));
}

void MainWindow::updateSkinHint() {
    // 外观那些成员在 PlayerSkinController 里，这里不去读它们本身，
    // 而是问它要**几格事实**再挑一句。分工是：那一边说"现在是什么状况"，这里说"这种状况
    // 该讲哪一句中文" —— 文案仍然只在这一层出现（`MainWindowUI` 那五句），那一层一个中文字
    // 都不认识（也不许认识 `ui/`）。
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
            // 那一层拿不到下拉框，也不该拿到。
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

void MainWindow::markFileOperationBusy(const QString &versionId) {
    if (versionId.isEmpty()) {
        return;
    }
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
    if (m_downloadBatches && m_downloadBatches->isRestoring()) {
        const auto all = QDir(AppPaths::versionsDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &id : all) ids.insert(id);
    }
    const QString busy = m_versionManager->busyVersionId();
    if (!busy.isEmpty()) {
        ids.insert(busy);
    }
    // 那四档界面自己记的占用（导入 / 安装 / 补全 / 预取）只在"用户点的是它"那一刻置上、
    // 终态时清掉，覆盖面比 VersionManager 那个宽 —— 正好补上前奏那几段。
    ids.unite(m_installOccupancy.instanceIds());
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
    // installedVersions() 走的是不带 out 参数的那条路径。
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
    // 早就装好的 1.18.2 照样在列表里、照样能启动（见 onStartButtonClicked 里的闸口）。
    // **不能**改成"有任何一批下载在跑，启动按钮就整片灰掉+下拉框禁用"：那既拦不住真正
    // 危险的那一种，又把完全无关的另一种一起堵死了。
    const QSet<QString> installing = installingVersionIds();
    QStringList launchable;
    launchable.reserve(installed.size());
    for (const QString &id : installed) {
        if (!installing.contains(id)) {
            launchable.append(id);
        }
    }

    QList<VersionDisplayParts> parts;
    QList<VersionDisplayParts> managementParts;
    QSet<QString> previouslyVisible;
    for (const VersionDisplayParts &part : m_instancePageParts) {
        previouslyVisible.insert(part.id);
    }
    parts.reserve(launchable.size());
    managementParts.reserve(installed.size());
    for (const QString &id : installed) {
        // 每个版本读一次 <id>/wtfml.meta（几百字节的可选文件，绝大多数版本没有）。
        // 这个文件会被外面改写（用户手写、别的启动器），所以读侧缓存靠"在不在 + 修改
        // 时刻 + 字节数"失效，重命名这类走写侧的路写完当场丢掉那一格 —— 见
        // VersionManager::readVersionMeta()。本函数之后 instanceKey() / homeGroupIdOf()
        // 还要再问同一个文件两三次，那些问都落在同一格上。
        const VersionDisplayParts part = m_versionManager->displayParts(id);
        if (!installing.contains(id)) {
            parts.append(part);
        }
        // 文件操作只暂时禁止启动，不应让已经安装的实例从管理页消失。
        // 尚在安装、之前没露过面的新实例仍然不出现。
        if (!installing.contains(id)
            || (m_fileOperationInstanceIds.contains(id) && previouslyVisible.contains(id))) {
            managementParts.append(part);
        }
    }
    // 第一次用这一份 .minecraft：把五个默认组（置顶 / 可安装mod / 原版 / 快照版 /
    // 远古版）种进去。
    //
    // 时机就选在这里：**有实例可摆**的时候才种（一个实例都没有时种下去，树里就是五个
    // 空组头加一个「未分组」，纯噪声）。这个函数每次刷实例都走一遍，但种只在文件还不
    // 存在的那一次真的发生（见 seedDefaultGroups()），代价是一次 QFile::exists。
    //
    // NOTE: 挡住信号。这一趟本来就要把树重画一遍（下面那一段），而 groupsChanged 接的
    // 正是本函数 —— 不挡的话会把自己再叫一遍，白扫一次盘。
    // NOTE: 只在文件不存在时种，所以这是**一次**的事：用户把那五个删光之后不会再长回来
    // （"默认可以删"就体现在 seedDefaultGroups() 里）。
    // NOTE: 种不下去（盘写不了）只说一句日志 —— 默认组是锦上添花，"没分过组就
    // 平铺"的样子照样能用，为它弹一个框反而吓人。
    if (!parts.isEmpty()) {
        const QSignalBlocker blocker(m_groupStore);
        QString seedError;
        if (!m_groupStore->seedDefaultGroups(&seedError)) {
            appendLog(seedError);
        }
    }

    // 主页上"已安装版本"只有下拉框这一个控件（右栏那张列表不存在，见
    // MainWindowUI 里主页那一段），所以这里只灌下拉框，同步选中项也只剩实例页那张列表。
    fillVersionCombo(combo, parts, m_settings->versionNameOptions());

    // 实例页那张树：**扫盘这一段先把原料留好**（见 m_instancePageParts 那段 —— 搜索框每
    // 敲一个字都要重灌树，那一条路不许再扫盘），然后重灌一遍。筛选条件此刻是什么样，
    // 那一趟自己会读（`instanceFilterQuery()`）。
    m_instancePageParts = managementParts;
    m_instancePageMarks.clear();
    m_instancePageMarks.reserve(managementParts.size());
    for (const VersionDisplayParts &part : managementParts) {
        m_instancePageMarks.insert(part.id, QIcon(instanceRowMark(
                                                  part, m_versionManager,
                                                  MainWindowUI::instanceTreeIconSize())));
    }
    rebuildInstanceTree();

    // 占位文案随上下文变。**两个串都必须非空**：空占位文案会让 QComboBox 在往空
    // 模型里插第一批数据时静默选中第 0 项（qcombobox.cpp:1753-1759），而这里必须
    // 停在"没选"，否则用户会在毫无察觉的情况下启动一个自己没挑过的版本。
    //
    // 占位文案必须按 launchable 重设：写死成"暂无已安装版本"的话，灌完数据后
    // 明明装了好几个版本，画面上却说着一个都没有。
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
    // 「管理页右侧每次打开都在最底下」：下面 syncTreeToCombo() 选中的是当前实例，Qt 会
    // 滚到它那儿；开机那一趟树还藏着，这一滚被攒到第一次显示、排完版才补，用户看到的就
    // 永远是列表底部。记下重建前的位置，等排版真结束再放回去（见 restoreTreeScroll()）。
    const int keepTreeScroll = tree->verticalScrollBar()->value();

    // ---- ① 筛：过不了的实例**根本不进桶**（不是"画好了再藏起来"）----
    //
    // 这一页不需要"画好了再手工隐藏行"：这棵树**每次都是整个重建的**（fillInstanceTree
    // 里第一句就是 tree->clear()），要少画几行只有一个正当办法 —— 不建它。
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
    // 归属键走 instanceKey()（稳定 id，没有就退回目录名）。
    //
    // NOTE: 每个实例这里要读两次 wtfml.meta（上面 displayParts() 一次、这里 instanceId()
    // 一次）。两者不合并：把 instanceId 塞进 VersionDisplayParts 会让"显示名怎么拼"和
    // "归属怎么认"纠缠在一起，那是个纯显示用的结构。第二次读现在不再开文件 ——
    // `VersionManager::readVersionMeta()` 按"在不在 / 修改时刻 / 字节数"缓存了读回来的
    // 那一份，这里是一次 stat（见那边 `m_metaCache` 那段）。
    const QList<InstanceGroupStore::Group> groups = m_groupStore->groups();
    QList<InstanceGroupBucket> buckets;
    QHash<QString, int> bucketOfGroup;
    const QSet<QString> knownGroupIds = knownGroupIdsOf(m_groupStore);
    for (const InstanceGroupStore::Group &group : groups) {
        InstanceGroupBucket bucket;
        bucket.groupId = group.id;
        bucket.name = group.name;
        // 筛着的时候**每一组都摆成展开的**（「清空搜索恢复原展开状态」前半句）。
        // 不这么做的话，命中条目躲在收起来的组里 = 用户看着像"搜不到"。那个展开状态
        // **不落盘**（下面灌树只管画，写盘那条路在 onInstanceTreeActivated 里、被
        // `filtering` 挡着），所以清空之后各组回到 wtfml.folders 里记着的样子。
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

        // 两边都显示：置顶着的那一个在置顶组里**再摆一条同名
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
    for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
        if (m_fileOperationInstanceIds.contains(instanceIdOf(*it))) {
            (*it)->setText(0, (*it)->text(0) + tr("（文件操作中）"));
            (*it)->setToolTip(0, tr("正在处理实例文件，完成后可再次启动或修改。"));
        }
    }
    syncTreeToCombo(m_ui->getInstalledVersionCombo(), tree);
    restoreTreeScroll(tree, keepTreeScroll);

    // ---- ③ 顶上那行说明：这一份文件夹里有几个实例（筛着的时候再说一句筛出来几个）----
    //
    // 数字与管理树一致：包含暂时文件操作中的已安装实例，不含尚在安装的新实例。这一行是
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
    m_ui->setInstanceFolderText(folderText);

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
            QString text = instanceTreeItemText(m_versionManager->displayParts(id), options,
                                                m_launcherManager->isVersionRunning(id));
            if (m_fileOperationInstanceIds.contains(id)) {
                text += tr("（文件操作中）");
            }
            (*it)->setText(0, text);
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
    // 1. 下拉框设了 minimumContentsLength(12)，长显示名会被省略号截断；
    // 2. 更要紧的是，id 在界面上**任何开关组合下都不会出现** —— 显示名那边参与
    // 拼装的"实际版本"是 parts.realVersion（为空才退化成 id），所以只要
    // wtfml.meta 里写了 realVersion，目录名就彻底从界面上消失了。而它才是真正
    // 进 --version 和文件路径的那个值。用户要排查"怎么启动错了版本"时，
    // 悬停是唯一能看到它的地方。
    // tooltip 必须走这条函数（而不是在 refreshInstalledVersions 里直接设），因为
    // fillVersionCombo() 套了 QSignalBlocker，刷新路径只能靠这一处收尾。
    const QString id = selectedVersionId(combo);
    combo->setToolTip(id.isEmpty() ? QString() : tr("版本目录名：%1").arg(id));
}

void MainWindow::refreshAvailableVersions() {
    // 只填树里那几条（开头是「最新版本」那一组 + 那四条分类）：它们唯一的数据源
    // 就是在线的版本清单（和已安装那一摊完全不相干 —— 清单为空时也不许退回显示本地
    // 已安装版本：两个数据源混在一张列表里，用户分不清点下去到底是启动还是下载）。
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
    // NOTE: 不能写成 if/else 只记"选中项所在的那个分类"：那样同时展开着"正式版"和
    // "快照"、并且在正式版里点过一个版本号之后，下次重灌会把快照那条收起来
    // —— 而清单刷新（启动时那一次、以及拉回来之后那一次）本来就不该动用户
    // 看的那一摊。展开状态和选中项是两件互不相干的事，得分别记。
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

    // 「最新版本」那一组是**默认展开**的：树最上方放一个「最新版本」类，里面放最新的
    // 正式版和快照。这一格只**摆一次**：用户把它收起来之后，
    // 清单刷新（每次启动那一次、以及拉回来那一次）不该又把它撑开 —— 而"记住哪些分类是展开的"
    // 这笔账上面刚记完，靠的就是这份集合，所以这里直接往里面塞一格，走的是**和用户自己展开
    // 完全一样**的那条路（重灌之后照旧按 isExpanded() 记回来），不是"每一发都强行展开"。
    //
    // NOTE: 为什么需要一个成员来记"摆过一次"：头一发时树是空的 ⇒ expandedKeys 也是空的
    // ⇒ 不加这一格它从一开始就是收着的。而"树是不是空的"这个判据不能当替身 ——
    // refreshAvailableVersions() 之外还有别人能清这棵树（那样用户收起来的东西会被
    // 又撑开一次），用成员就与"谁清了树"无关。
    if (!mLatestGroupExpandSeeded) {
        expandedKeys.insert(VersionManager::latestGroupKey());
        mLatestGroupExpandSeeded = true;
    }

    const QSignalBlocker blocker(tree);
    const bool updatesEnabled = tree->updatesEnabled();
    tree->setUpdatesEnabled(false);
    tree->clear();

    // 摆在**最上面**：它讲的是"现在最值得装的这两条"，放在四个分类后面等于没摆（那四栏各自
    // 几百条，谁也不会滚到底去看）。它不是第五个分类，所以有几件事刻意和那四条不一样：
    // · 那四条是清单的**全集划分**（一条版本不重不漏地落进某一栏），这一组是**从里面挑
    // 两条出来** —— 同一个版本号在这两处各有一份（它自己那一栏里那条还在）；
    // · 身份不来自 VersionCategory::channelKey()，走 VersionManager::latestGroupKey()
    // （那个键保证和四个分类键两两不同，展开状态那笔账才串不了）；
    // · 里面那两条**只认版本号**（和别处的版本号一样：点一下就是"基于它装"），
    // 所以它前面那个小图标按它**自己属于哪一类**给（正式版草方块 / 快照版覆雪草块）。
    //
    // NOTE: 清单没拉回来时（离线、或者第一发就失败）这一组照旧摆着，只是里面空的 ——
    // 和那四条空分类是同一个规矩（"这一条压根不出现"和"点开是空的"在界面上是两件
    // 很不一样的事，见 availableVersionChannels() 那段）。
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
        // 一个小三角"的话，那是关错了地方：QTreeModel::hasChildren() 是按这个
        // policy 直接 return false 的（QTreeWidget 源码里那条 switch），于是
        // **整个分类的版本号一条都排不进布局**，树上只剩四条光秃秃的分类、
        // 点开也是空的。真要把左边那个三角去掉，走 rootIsDecorated(false)
        // （见 MainWindowUI 里建树那一段）：它只影响装饰格的绘制，不动
        // "有没有孩子"这件事。
        // 右边那个箭头仍然由 VersionTreeDelegate 画在行最右边，
        // 一个版本都没有的分类它连箭头都不画。
        // 版本号前面那个小图标**按分类**给（快照版用白色覆雪草块，远古版用土块，
        // 愚人节版用幸运方块，原版不变）。分类 → 图标那张表在
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
        // 两个都是真的、点哪个都是"基于它装"。这一搜要是从头开始（「最新版本」现在在
        // 第一条），用户本来在"正式版"里选着的那条**每次刷新都会悄悄跳到最上面那一组**
        // 去 —— 他没动过手，高亮自己跑了。只在分类里找：高亮永远落在用户自己点过的
        // 那一栏里。代价：要是用户当初是从「最新版本」那一组点进去的，
        // 刷新之后高亮会落到它自己那一栏里 —— 那也仍在屏幕上、仍是同一条版本。
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

    m_ui->applyVersionFilter();
    tree->setUpdatesEnabled(updatesEnabled);
}

void MainWindow::openInstallPage(const QString &gameVersion) {
    // 薄壳：这一页的全部状态与摆法在 `InstallPageController`（见那个头文件开头的解释）。
    m_installPage->openPage(gameVersion);
}

void MainWindow::backToVersionTree() {
    m_installPage->backToVersionTree();
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
    if (m_ui == nullptr) {
        return;
    }
    QWidget *formHost = m_ui->getServerFormHost();
    QStackedWidget *stack = m_ui->getServerStack();
    if (formHost == nullptr || stack == nullptr || formHost->layout() == nullptr) {
        return;
    }
    if (m_newServerPage == nullptr) {
        // 它**不是**单独开的一页：和「下载 → 游戏」那张安装页一样，就住在服务端那一栏的
        // 第二层里（产品要求"服务端下载和游戏下载一个样，不要搞独立的新界面"）。
        // 降成普通控件嵌进去之后，它的 accepted / rejected 只当"这一层该干什么"的通知。
        m_newServerPage = new NewServerPage(formHost);
        new ServerVersionPicker(m_newServerPage, m_versionManager);
        m_newServerPage->setWindowFlags(Qt::Widget);
        m_newServerPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        formHost->layout()->addWidget(m_newServerPage);
        m_newServerPage->show();
        connect(m_newServerPage, &NewServerPage::fieldsChanged, this,
                [this] { updateNewServerSummary(m_newServerPage); });
        connect(m_newServerPage, &QDialog::accepted, this,
                &MainWindow::createServerFromNewServerPage);
        connect(m_newServerPage, &QDialog::rejected, this, &MainWindow::leaveNewServerPage);
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
    // 默认名是 `Server<版本>`。用当前主页选中的那一个版本当缺省
    // （`selectedVersionId()` 是"下拉框里那个 id 是什么"的唯一出处，见本文件上面那个
    // 自由函数 —— 它已经处理了"空着 = 没选"）
    const bool fromDownload = sender() == m_ui->getDownloadNewServerButton();
    const QString selected = fromDownload ? m_ui->getDownloadNewServerButton()->property("selectedVersion").toString() : QString();
    const QString currentVersion = selected.isEmpty() ? selectedVersionId(m_ui->getInstalledVersionCombo()) : selected;
    m_newServerPage->setDefaults(
        ServerRegistry::defaultServerName(ServerInstall::Flavor::Vanilla, currentVersion),
        currentVersion);
    // 返回行右边那句（和游戏安装页「将基于原版 26.3 安装」同一格）：版本已经在列表里选好了。
    m_ui->setServerFormIntro(currentVersion.isEmpty()
                                 ? tr("将安装一个新服务端")
                                 : tr("将安装 %1 服务端").arg(currentVersion));
    if (auto *version = m_newServerPage->findChild<QLineEdit *>(MainWindowUI::newServerGameVersionEditName()))
        version->setReadOnly(fromDownload && !selected.isEmpty());
    if (auto *install = m_newServerPage->findChild<QPushButton *>(MainWindowUI::newServerCreateButtonName()))
        install->setText(fromDownload ? tr("安装") : tr("创建"));
    updateNewServerSummary(m_newServerPage);
    // accept()/reject() 会把这一页藏起来（`QDialog` 的老规矩）—— 再开一次要能露出来。
    m_newServerPage->show();
    // 走「下载 → 服务端」同一条路（和「游戏」下载一致）：切到下载页、翻到「服务端」那一栏、
    // 停在第二层那张安装表单上。不再开覆盖层 —— 用户要的就是"别搞独立的新界面"。
    if (QTabWidget *tabs = m_ui->getTabWidget()) {
        const int tab = MainWindowUI::downloadTabIndex();
        if (tab >= 0 && tab < tabs->count()) {
            tabs->setCurrentIndex(tab);
        }
    }
    if (QListWidget *categories = m_ui->getDownloadCategoryList()) {
        if (categories->currentRow() != MainWindowUI::downloadCategoryServerIndex()) {
            categories->setCurrentRow(MainWindowUI::downloadCategoryServerIndex());
        }
    }
    stack->setCurrentIndex(MainWindowUI::downloadServerFormIndex());
    if (auto *name = m_newServerPage->findChild<QLineEdit *>(MainWindowUI::newServerNameEditName())) {
        name->setFocus();
        name->selectAll();
    }
}

void MainWindow::createServerFromNewServerPage() {
    if (m_ui == nullptr || m_newServerPage == nullptr) {
        return;
    }
    // 「创建」真按下去了 —— 建这个服务端：名字洗成目录名、重名加后缀、写 `.wtfml-server`。
    const QString desired = ServerRegistry::sanitizeId(m_newServerPage->serverName());
    if (desired.isEmpty()) {
        // 话说完就停在表单上：刚填的东西不能因为一句提醒就没收。
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
    // 建好了：表单那一层退回去（下次进来从版本列表开始），并切到「管理 → 服务端」——
    // 用户刚建的那一行就在那儿。
    leaveNewServerPage();
    setInstanceMode(MainWindowUI::InstanceMode::Server);
    if (QTabWidget *tabs = m_ui->getTabWidget()) {
        const int tab = MainWindowUI::instancesTabIndex();
        if (tab >= 0 && tab < tabs->count()) {
            tabs->setCurrentIndex(tab);
        }
    }
    // 「创建」= 建目录 + 记一份账 + **当趟就把东西下下来 / 跑安装器**（不是"建完让
    // 用户自己再点一下安装"）。排在切页之后：那一行先以「未安装」露出来，
    // 紧接着任务中心就动起来 —— 用户看到的是"这一行正在变成能用的"。
    ServerInstall::Request request;
    request.flavor = info.flavor;
    request.gameVersion = info.gameVersion;
    request.loaderVersion = info.loaderVersion;
    request.installerVersion = info.installerVersion;
    request.buildId = info.buildId;
    installServer(serverId, request);
}

void MainWindow::leaveNewServerPage() {
    if (m_ui == nullptr) {
        return;
    }
    if (QStackedWidget *stack = m_ui->getServerStack()) {
        stack->setCurrentIndex(MainWindowUI::downloadServerListIndex());
    }
    // `accept()` / `reject()` 会把这一页藏起来；它就住在那层里，下次进来要还看得见。
    if (m_newServerPage != nullptr) {
        m_newServerPage->show();
    }
}

void MainWindow::installServer(const QString &serverId, const ServerInstall::Request &request) {
    if (m_serverInstall == nullptr || serverId.isEmpty()) {
        return;
    }
    // 先单任务聚合。下载那一层只有一份批次状态，同时开两批会让两边的收尾互相算错
    // —— 所以忙的时候说清楚，而不是让它排着队（排队还没做）。
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
    // 服务端安装的总量（一共要下多少字节）要等元数据取回来才知道，在那之前报任何
    // 百分数都是编的 —— 同 `Tasks::percentOf()` 那条"测不出就不显示"的规矩。
    m_activityCenter->tasks()->setProgress(id, 0, qMax(1, names.size()));

    // 起手先切到服务端那一页：用户刚点的就是那儿，而"装到哪个实例里"这件事在这一页上
    // （任务中心那一条也带 targetId，点「打开目标位置」就是它的目录）。
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
// 服务端的「跑起来 / 停下来 / 控制台」（操作、停止时序）
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
        m_serverRun->stop(serverId);
        return;
    }
    m_serverRun->start(serverId);
}

void MainWindow::openServerConsole(const QString &serverId) {
    // 兼容入口：服务端菜单和 ConnectionsController 按这个名字调用。
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
    // 世界正在备份 / 还原：那一趟还在往这个目录里读 / 写，删掉它等于把正在解压的落点
    // 从底下抽走（备份那一趟则会把"删到一半的目录"打进包里）。
    if (m_serverRun->fileBusy(serverId)) {
        InlineMessage::warning(m_ui->overlay(), tr("这一份这会儿不能删"),
                               tr("「%1」的世界正在备份或者还原。等那一趟完了再删。")
                                   .arg(shown));
        return;
    }
    // 这是**唯一一处会删用户东西的地方**，而且删的是别人几个月的存档 —— 所以：默认那
    // 一边是「取消」（`defaultYes = false`，回车不会把它删掉）、那颗按钮按危险档染红。
    const bool go = InlineMessage::question(
        m_ui->overlay(), tr("删掉这个服务端"),
        tr("「%1」这一整个目录会被删掉 —— 里面的世界（world/）、模组、配置一起没。\n\n"
           "删掉的东西会进回收站，要恢复可以从那里找回来。\n\n确定要删吗？")
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

// ===========================================================================
// 「服务端设置」那一页 —— 这里只留两条薄壳
// ===========================================================================
//
// 这一页的**全部状态**（页对象 / 现在说的是哪个服务端 / 能不能改 / 端口那一次的结论）
// 和决定它的那几条（开页、重摆整页、保存、放弃改动、切页合编辑、体检）都搬进了
// `src/controllers/ServerSettingsPageController` —— 为什么搬、搬走了什么，见那个 .hpp。
//
// 主窗口这边只留两个名字，因为还有三处按名字叫它们：
//
// ① `openServerSettingsPage()`：服务端列表的右键菜单（`ServerMenuController::run()`），
//    以及 mw_check 的 T42（**经元对象系统**驱动 —— 所以它必须继续是 `private slots:`）；
// ② `refreshServerSettingsPage()`：备份收尾那一发（`BackupController` 的宿主回调），
//    以及 `ServerManagementActions` 里那五处"改完盘上那份、把页面重摆一遍"。
//
// NOTE: 其余的（保存 / 放弃 / 合编辑 / 保存带 / 体检）原来也只有这一页自己的信号在叫，
// 搬走之后一个外人都没有，所以**没有留壳** —— 留一个没人叫的空壳只会让人以为"主窗口还管
// 这一页"。

void MainWindow::openServerSettingsPage(const QString &serverId) {
    m_serverSettings->open(serverId);
}

void MainWindow::refreshServerSettingsPage() {
    m_serverSettings->refresh();
}


// ---------------------------------------------------------------------------
// 玩家名单（「玩家管理」）
// ---------------------------------------------------------------------------

void MainWindow::refreshServerPlayers() {
    ServerManagementActions::refreshServerPlayers(this);
}

void MainWindow::addServerPlayer() {
    ServerManagementActions::addServerPlayer(this);
}

void MainWindow::removeServerPlayer(const QString &identity) {
    ServerManagementActions::removeServerPlayer(this, identity);
}

void MainWindow::pickServerResourcePack() {
    ServerManagementActions::pickServerResourcePack(this);
}

QString MainWindow::serverContentFolder() const {
    return ServerManagementActions::serverContentFolder(this);
}

void MainWindow::refreshServerContent() {
    ServerManagementActions::refreshServerContent(this);
}

void MainWindow::addServerContent() {
    ServerManagementActions::addServerContent(this);
}

void MainWindow::toggleServerContent(const QString &name, bool enable) {
    ServerManagementActions::toggleServerContent(this, name, enable);
}

void MainWindow::openServerContentFolder() {
    ServerManagementActions::openServerContentFolder(this);
}

void MainWindow::kickServerPlayer(const QString &name) {
    ServerManagementActions::kickServerPlayer(this, name);
}

void MainWindow::runServerCheckAction(const QString &token) {
    ServerManagementActions::runServerCheckAction(this, token);
}

QString MainWindow::serverWorldName(const QString &serverId) const {
    return ServerManagementActions::serverWorldName(this, serverId);
}

QString MainWindow::serverWorldDir(const QString &serverId) const {
    return ServerManagementActions::serverWorldDir(this, serverId);
}

int MainWindow::serverWorldBackupCount(const QString &serverId) const {
    return ServerManagementActions::serverWorldBackupCount(this, serverId);
}

void MainWindow::backupServerWorld(const QString &serverId) {
    ServerManagementActions::backupServerWorld(this, serverId);
}

void MainWindow::restoreServerWorldBackup(const QString &serverId, const QString &zipPath) {
    ServerManagementActions::restoreServerWorldBackup(this, serverId, zipPath);
}

void MainWindow::deleteServerBackup(const QString &zipPath) {
    ServerManagementActions::deleteServerBackup(this, zipPath);
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
    // 默认那一颗给 **-1**：这一格上一颗都不许被回车打中 —— "把同意两个字打在屏幕上"
    // 这件事不能被一次误按回车办了。
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
    item->setText(0, QStringLiteral("%1  —  %2 / %3")
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

    // 第 1 列：那颗按钮。**一行只有一颗**，按状态换身份
    // （行尾按钮 + 右键菜单；其余几项全在右键里）。
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
    // 只有长到离谱的那些才截断，而且**只截写进日志的这一份** —— 控制台那一页照旧是原话。
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
    // Spigot：这一轮没接（官方不发成品，要用它的 BuildTools 现编，那一步要本机有 git）。
    // **那一档故意留在下拉框里**（用户得知道有这一档），选中它时明说原因，并把
    // 「创建」收起来 —— 不留一颗按下去什么都不发生的按钮。
    // 判据读的是 `ServerInstallRun::flavorSupported()` 这一个出处。
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
    // 成立了：**不再摆"这一档要下什么、按下去会发生什么"那一段说明** —— 用户点名
    // 那是废话（下载地址联不联网、jar 放哪、进度在任务中心，界面上不必先讲一遍）。
    // 这里只把上一档留下的问题句清掉；真正拦人的那几句（填法不成立 / 这一档不支持）
    // 由上面那几处 `setSummary(…, problem)` 露出来。
    page->setSummary(QString(), QString());
}

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


// ======================================================================
// 「设置」选项卡
// ======================================================================
//
// 分工：MainWindowUI 只把控件搭出来（含"选项名称在左、控件在右"的两列布局），值和
// 持久化全在这一层。枚举 → 文案的映射也在这里 —— 界面层不该为了画一个下拉框去
// include src/settings，而 tr() 的文案本来就该和业务逻辑待在一起。
//
// 落盘时机逐项不同，这是这块最容易做错的地方，所以每一项都在自己的函数里说清楚：
// Java / 内存 / 启动后 / 下载源 / 版本名格式 —— 改完立刻存
// JVM 参数 —— 点「应用」才存（那是直接进命令行的东西，不能存半条）

/// 「同时最多下几个」那颗下拉框里每一行的文字。
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

/// 「下载速度上限」那颗下拉框里每一行的文字。
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

/// 「代理」那颗下拉框里每一行的文字。
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
    // 测试地址覆盖（**产品永远是空串**）：必须在第一次 fetchAvailableVersions() / 皮肤
    // 刷新之前落到数据层，否则那一发已经打到官方地址上了。
    m_versionManager->setVersionManifestUrlOverride(m_settings->versionManifestUrlOverride());
    PlayerSkinStore::setEndpointOverrides(m_settings->sessionServerBaseOverride(),
                                          m_settings->textureBaseOverride());
    SkinApi::setBaseUrlOverride(m_settings->minecraftServicesBaseOverride());
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

    updateJavaPathLabel();
    // 提醒总闸。挡信号：这是"把值画出来"，不挡的话 setChecked 会触发
    // onJavaWarnMismatchChanged() 又回头落一次盘。
    {
        const QSignalBlocker blockWarn(m_ui->getJavaWarnMismatchCheck());
        m_ui->getJavaWarnMismatchCheck()->setChecked(m_javaSettings->warnVersionMismatch());
    }

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
    // 里、比 MainWindow 构造还早），不然画出来的是出厂那一份。
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
    // （原版没有最大化参数，是照屏幕工作区算的，见 GameWindowSettings.hpp）。
    QComboBox *windowModeCombo = m_ui->getGameWindowModeCombo();
    windowModeCombo->addItem(tr("窗口"),
                             int(GameWindowMode::Windowed));
    windowModeCombo->addItem(tr("最大化"),
                             int(GameWindowMode::Maximized));
    windowModeCombo->addItem(tr("全屏"), int(GameWindowMode::Fullscreen));
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
    sourceCombo->addItem(tr("优先官方源，速度缓慢时使用国内源"), int(DownloadSource::Official));
    sourceCombo->addItem(tr("优先国内源，速度缓慢时使用官方源"), int(DownloadSource::Mirror));
    selectByEnumData(sourceCombo, m_settings->downloadSource());

    // ---------- 「同时最多下几个」 ----------
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

    // ---------- 「下载速度上限」 ----------
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

    // ---------- 「代理」 ----------
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

    // 「下载新模组时直接放进当前实例」。默认值同样只有 LauncherSettings
    // 那一处出处（默认勾上）—— 这里不写任何默认，灌的就是盘上那份。
    m_ui->getDownloadAutoIntoInstanceCheck()->setChecked(m_settings->downloadAutoIntoInstance());
    // 「下载缺前置时提醒」。同一个做法：不在这儿写默认。
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

void MainWindow::onMemoryModeChanged() {
    m_javaSettingsController->setMemoryModeFromUi();
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
// 主页左栏可以自定义图像，其他页面的背景也能换图。
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

// ============================ 「账号与外观」那个模块 ============================
//
// 三件事在这个模块里的分工（写清楚免得下次改错地方）：
// - **界面的样子**全在 ui/AccountSkinDialog.{hpp,cpp}（它不 include 任何 src/ 下的头）；
// - **盘上的账**在 src/auth/AccountStore（多账号列表）+ TokenStore（凭据字节格式）；
// - **下面这一片**是接线：谁来读谁、失败怎么报、哪个动作之后要刷新什么。
// 所以这里没有任何"界面布局"的代码，那边也没有任何"读注册表"的代码。

void MainWindow::onAccountSkinButtonClicked() {
    // 覆盖层已经开着时**不能直接 return** —— 屏幕上什么都不发生：用户在别的选项卡上逛，
    // 实例设置那一页还收着占住那一层；这时按「账号与外观」看着就是"按钮点坏了"。
    // 所以并进同一套规矩：**静默收掉占着的那一层，再开**（让位那一段的唯一出处是
    // `runWhenOverlayFree()`）。
    runWhenOverlayFree([this] {
        setupAccountSkinDialog();
        OverlayHost *overlay = m_ui->overlay();
        if (overlay == nullptr) {
            return;  // 没有覆盖层就开不了（主界面建的时候就有，正常到不了这一支）
        }
        // 页面把内容区整块盖掉，不是独立窗口。登录那一步会把浏览器推到前台、用户要
        // 在两边来回切 —— 界面一直是活的，浏览器本来也是外面的独立窗，谁都拦不着它。
        //
        // NOTE: 顺序是**先摆出来、再刷新**。refreshAccountSkin() 最后那一段会去拉披风列表
        // （真联网，最长能等十几秒），倒过来做的话头一次点这个入口就是"什么也没发生"
        // —— 页面还没画出来，那个请求已经在跑了，用户只会以为按钮点坏了。
        // 这里排一发队：开页是**非阻塞**的（`OverlayHost::showPage()`），返回之后主循环照常
        // 转，这一发就排在"页面已经在屏幕上"之后跑。
        QMetaObject::invokeMethod(this, [this] { refreshAccountSkin(); }, Qt::QueuedConnection);
        // 那一发刷新排在页面摆上来之后；先把「皮肤」那一行摆对，免得开页的第一帧
        // 露的是正版才有的「上传皮肤…」（离线账号按下去只会失败）。
        m_accountSkinDialog->setOfflineSkinState(offlineIdentity(),
                                                 OfflineSkin::hasSkin(AccountStore::activeUuid()));
        if (!overlay->showPage(m_accountSkinDialog, AccountSkinDialog::windowTitle())) {
            appendLog(tr("账号与外观那一页没开成：覆盖层刚被另一页占上了。"));
        }
    });
}

void MainWindow::setupAccountSkinDialog() {
    if (m_accountSkinDialog != nullptr) {
        return;
    }
    m_accountSkinDialog = new AccountSkinDialog(this);
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
    // 判据（连那段"为什么读盘不读内存副本"的道理）在
    // `PlayerSkinController::offlineIdentity()` 里 —— 外观那一套要按它决定"这一趟能不能
    // 联网"，而这一层还有十几处读它，所以留一句转手。**唯一出处仍然是那一个静态函数**：
    // 谁要判"现在是不是离线身份"都该走它，不要在这儿另写一份。
    return PlayerSkinController::offlineIdentity();
}

QString MainWindow::accountSkinStatusText() const {
    // 当前账号是离线账号时先说这一句：名字就在上面那一列里躺着，比任何泛泛的说法都具体。
    //
    // NOTE: **离线 = 当前账号本身是离线账号**，没有第二种"离线"。
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
    // 主页那个小人、底栏头像、那个窗里的 3D 预览**都只在
    // startPlayerSkinLoad() → applyPlayerSkin() 里更新**，这一句 return 若把预览也挡掉，
    // 没点开过「账号与外观…」的会话里切账号根本不换脸，一直停在上一个人的脸上
    // （从"未登录"切过去时停的就是那张兜底占位图）。
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
    // 同步请求已经回来了（下面第 4 步会再离开这个方法去联网）。
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
    // 会把同一份贴图同时喂给主页那个小人和这个窗里的预览。
    //
    // NOTE: 这一句**不保证不联网**：离线账号第一次进来时它要 MineSkin 那一页随机皮肤
    // （见 startPlayerSkinLoad() 里 offlineIdentity() 那一档）。挑过一次之后就只在盘上
    // 读那一行。
    // 这也正是这个窗**只在离线账号那一档开**的原因（见 onAccountSkinButtonClicked()
    // 里那段）：正版那一档开窗会顺带拉披风，那才是真联网的那一下。
    startPlayerSkinLoad();

    // 4) 披风列表要令牌，是这一整块里唯一会真的去联网的地方（过期令牌也是在这里顺带刷新的）。
    if (offlineIdentity()) {
        // 当前是离线账号 = "别联网"，而拉披风列表正是联网。跳过。
        m_accountSkinDialog->setCapes({});
        return;
    }
    // 为什么非得另立一条判据：上面那句 `m_accountSkinDialog == nullptr` 分不出"关掉了" ——
    // 这个对话框是**建一次就留着**的（setupAccountSkinDialog() 见非空就返回，父对象是本
    // 窗口），页面关掉只是把它藏起来，指针永远不为空。于是"开过一次账号页、之后从右面板
    // 切账号"这一条路上，每一次切换都会在**没有人看着的那个页面上**点起忙态、然后同步
    // 等一次网络往返（loadSavedToken 还可能真去换一次令牌）：用户看到的是"切个账号整个
    // 启动器卡几秒"，而卡住的理由（「正在获取披风列表…」）摆在一个藏起来的页面里。
    //
    // NOTE: 判据是"**现在真摆在眼前**"（isVisible），和 OverlayHost 里 retargetCoveredPage()
    // 那条 NOTE 同一条规矩：页面收在别的选项卡上时它不在屏幕上，一样跳过。
    // NOTE: 上半截（账号列表 / 状态行 / 3D 预览）**照样刷了** —— 那些不联网。披风列表会在
    // 用户真的打开这一页时补上：开页那条路必走一次本函数（见 onAccountSkinButtonClicked），
    // 而那时页面已经在屏幕上了。
    if (!m_accountSkinDialog->isVisible()) {
        return;
    }
    // 下面这两下是**同步**的（补忙态）：loadSavedToken() 可能要真去换一次令牌、
    // fetchOwnedCapes() 一定发一次 GET —— 两个函数都是"调完才返回"，于是点「刷新」之后的
    // 这几秒钟里 UI 线程一直在这两行里面出不来：窗不重绘、什么都按不动，Windows 过一会儿
    // 还会给标题挂上"(无响应)"。
    //
    // NOTE: 到这儿必须先把忙态点起来并 processEvents 一次：第 2 步的 setBusy(false)
    // 之后到这里谁也没把忙态点起来的话，用户看到的是一个画着旧内容、点什么都没反应的窗。
    // 忙态 + 一次 processEvents（排掉用户输入）和 enterSyncWait() 是同一个手法：
    // 进去之前先把话说出来。下面每条返回路上都要松开它。
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
    // （offlineIdentity()）。
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
    // NOTE: 离线账号那一档**不是"整条路都不联网"**：披风确实不拉
    // （离线账号没有披风可拉），但这个账号**还没挑过**
    // 随机皮肤时，startPlayerSkinLoad() 会去 MineSkin 要一页列表（见 RandomSkinStore）。
    // 挑过一次之后就只在盘上读那一行 —— 本地命中，一个包都不发。
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
    // NOTE: 这里刻意**不用 loadSavedToken()**：它只在"已经过期"时才去换，而要的是
    // "剩不到 6 小时就换"（见 AuthManager::tokenRefreshWindowSecs 那段）。
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
    // 都不是这里能修的，只能重新登录一次 —— 所以那句话得指出**下一步**，不能只说
    // "刷新失败"。
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

namespace {

/// 用户挑的皮肤图**文件**的字节上限（一张合法的皮肤不过几 KB；这里挡的是"把一整部
/// 电影当皮肤选进来"那种 —— 读进来的每个字节都要占内存）。
constexpr qint64 kMaxPickedSkinBytes = 16LL * 1024 * 1024;

/// 皮肤图的解码上限。皮肤只可能是 64×64 / 64×32（真正的判据是
/// `SkinApi::isAcceptableSkinSize()`），256 只是给将来放宽留余量；这一道挡的是
/// "照一个荒唐的声明去分配"（见 `ImageDecode::Limits`）。
constexpr ImageDecode::Limits kPickedSkinLimits{256, 256 * 256};

} // namespace

void MainWindow::onSkinUploadRequested(const QString &path, const QString &variant) {
    // 先把文件读出来。**尺寸那条规矩只在 SkinApi::isAcceptableSkinSize() 里**（唯一出处），
    // 这里不自己再判一遍。但"读不出来"得在这里说 —— 它连尺寸都没有。
    QFile file(path);
    const QString fileName = QFileInfo(path).fileName();
    // 文件本身也封一个顶：一张合法皮肤不过几 KB，读进来的每一字节都要占内存。
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0
        || file.size() > kMaxPickedSkinBytes) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"),
                             tr("读不到「%1」这个文件（或者它大得不像一张皮肤图）。")
                                 .arg(fileName));
        return;
    }
    const QByteArray png = file.readAll();
    file.close();
    // 宽高从**同一份字节**里拿（不再按路径读第二遍）。皮肤图是用户挑的本机文件，
    // 也可能来自群里那个"皮肤包" —— 先问尺寸、超了不解（见 `ImageDecode::Limits`）。
    const QImage image = ImageDecode::decode(png, kPickedSkinLimits);
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
    // 和正版上传那条路同一个上限、同一个理由（那是一张用户挑的本机文件，而
    // "皮肤包"里的图不一定是好心的）。
    QImage image;
    QFile skinFile(path);
    if (skinFile.open(QIODevice::ReadOnly) && skinFile.size() > 0
        && skinFile.size() <= kMaxPickedSkinBytes) {
        image = ImageDecode::decode(skinFile.readAll(), kPickedSkinLimits);
        skinFile.close();
    }
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
    // （AuthManager 自己），以后再多一个入口起这个流程，界面照样跟得上。
    // 超时兜底也在那边 —— 浏览器再也不回来时它会发一次 loginFailed()，
    // 忙态由 onLoginFailed() 松开。
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
    // 登录失败还可能是"**上一个在线账号已经不在盘上了**"。
    // 登录那一趟开头会把活动槽里的 Minecraft 凭据清掉（防"旧 uuid 配新刷新令牌"的错配，
    // 见 AuthManager::exchangeAuthorizationCode），而 AuthManager 在失败那条路上已经把
    // 内存里那份身份跟着盘清了（见 handleNewConnection 里那段）。**界面得跟着画一遍**：
    // 底栏那个名字是上一次 updateBottomBar() 摆上去的，不重画就会一直停在上一个人的名字
    // 上，而底栏第二行"未登录"的判断（offlineIdentity()，读盘）与启动那句判据都说"没人"。
    //
    // 条件写全是为了**别动离线账号那一档**：那时候盘上什么都没变（离线账号没有凭据），
    // 底栏该继续写离线那个名字。而"从来没登录过 + 登录失败"也会走进来一次，那一趟是
    // 幂等的：画出来还是原来那张默认脸、占位名字。
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
    // 为什么留着这一层而不是把调用点全改成问 controller：验证工程按**名字**驱动这个槽
    // （`QMetaObject::invokeMethod(this, "applyRename", ...)`，见 mw_check 的 S 段）——
    // 那是"这个槽驱动得起来"这条判据本身，不是实现细节。同 applyGroupList / applyInstanceSettings。
    return m_instanceSettings->rename(oldId, newId);
}

void MainWindow::showInstanceSettingsNotice(const InstanceSettingsController::Notice &notice) {
    // 这一层的活是**把事实说成人话**：controller 那边只有"哪一档、是谁、数据层说的原因"，
    // 标题和正文在这里挑。下面每一句话都被验证工程按行数/原文对数，改一个字就是回归。
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
    case InstanceSettingsController::Notice::Kind::RenameBlockedBusy:
        // 装 / 导入 / 复制 / 快照 / 还原这几类作业都攥着改名前的绝对路径。把话说全，
        // 用户才知道"等一会儿再改"是等什么。
        InlineMessage::warning(m_ui->overlay(), tr("重命名实例"),
                               tr("「%1」正在被另一项操作占用（安装、导入、复制、备份或还原），"
                                  "等它结束后再改名。")
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
    // 每一句话都被验证工程按原文对数，改一个字都会红。
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
    // 「置顶」= 把这个实例挪进**置顶组**，同时记下"它置顶之前待在哪一组"。
    // 两件事一起做是 store 那一层的实现（InstanceGroupStore::pinInstance()）——
    // 分两次写的话，中间那一份文件自相矛盾，取消置顶就会给出一个不一样的答案。
    // 哪一组是置顶组由 store 回答（清单里勾了「设为置顶组」的那一组；一个都没勾就退回名字
    // 叫「置顶」的那个默认组，那是没有勾选时的兜底；都没有则是空串）。
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
    // 「取消置顶」= 把这个实例**送回它置顶之前待的那一组**。三种记录三种落点都写在
    // InstanceGroupStore::unpinInstance() / 那个文件头那一段里：
    // 记着组 id ⇒ 回那个组；记着"明确选了未分组"⇒ 回未分组；记着 null（置顶前压根没表过
    // 态）⇒ 把那一条归属**删掉**、让显示层按版本类型重新归类。
    //
    // NOTE: **没有记录**（用户自己用「移入分组」把它塞进置顶组的，或者置顶组里一直就有的
    // 旧数据）⇒ 落未分组，和「移出分组」逐字同一个落点。这一档**不猜**它本来该归哪
    // 一组：那条信息真的不存在（从来没被记下来过），猜错的表现是"取消置顶之后它跳去
    // 了一个我没选过的组"，比"落在未分组里、用户自己再挪一次"糟得多 —— 用户直接塞的
    // 就是归未分类那一档。
    //
    // NOTE: 不判"它到底在不在置顶组里" —— 那一格只在 rowUnpinRole() 为真的行上画得出来，
    // 走到这儿就说明在。真要在外面，这一下也只是写一条"未分组"，无害。
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
        // 这一份集合里不只有"正在安装"：复制 / 快照 / 还原也在这棵目录树上读写，删除
        // 会让它们写到一半的目标消失（然后被 `mkpath()` 重新建出一个半截的）。话说全。
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在被另一项操作占用（安装、导入、复制、备份或还原），"
                                 "等它结束后再删。")
                                  .arg(name));
        return false;
    }
    // 闸口三：**正在体检的也不能删**（「同一实例不允许同时扫描和删除/
    // 改名」）。后台那一趟还在拿这个实例的绝对路径 stat 文件，删到一半它就在扫一堆不存在的
    // 路径，报告里全是假的"缺文件"。
    if (integrityScanningVersion(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在做完整性检查，等它完了再删。").arg(name));
        return false;
    }

    // 这一趟会动用户的东西（虽然进回收站可还原），所以先问一句。正文里把目录的
    // 完整路径写出来：用户要能看清删的是哪一个目录、它长在哪，而不是只知道"某个实例"。
    const auto answer = InlineMessage::question(
        m_ui->overlay(), tr("删除实例"),
        tr("要删除实例「%1」吗？\n\n"
           "会把 %2 这整个目录删掉（jar、json、以及这个实例自己的设置都在里面）。\n\n"
           "删掉的东西会进回收站，要恢复可以从那里找回来。\n\n"
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
        // 这一份集合里不只有"正在安装"：复制 / 快照 / 还原也在这棵目录树上读写，删除
        // 会让它们写到一半的目标消失（然后被 `mkpath()` 重新建出一个半截的）。话说全。
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在被另一项操作占用（安装、导入、复制、备份或还原），"
                                 "等它结束后再删。")
                                  .arg(name));
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
    // - 它对界面没有任何影响：归属只有在"这个实例还在"的时候才会被问到，而那张树是
    // 按**盘上的实例**建的（遍历 installedVersions()，每条再拿 instanceKey() 去问归属）
    // ⇒ 指向已删实例的那些条目从来不会被遍历到。「组名 (N)」里的 N 也是从树上数的，
    // 不是从归属表里数的。换句话说：既没有幽灵行，也没有虚高的计数。
    // - 为它写一条"删实例时顺手清归属"要新加一个 store 的 API（现在只有整份
    // replaceGroups()），换来的只是少几个字节的死数据。
    //
    // 注意：**别拿"重装能回原组"当理由**。归属的键是 instanceKey() = `wtfml.meta` 里的
    // 稳定 id（没有才退回目录名），而 deleteInstance() 把整个目录连 `wtfml.meta` 一起删了
    // ⇒ 重装出来的是一个**没有 id 的新目录**，键退回目录名，而那条残留的键是旧 UUID，
    // 永远不可能再对上。那个 id 是**惰性生成**的：用户只要开过一次实例设置按过确定
    // （ensureInstanceId()）或者改过名就会落一个 —— 所以"重装回原组"只对**从来没拿过 id
    // 的老实例**（键就是目录名）成立，其余的残留就是永远的死数据。
    // ⇒ 上面那两条"代价为零"的理由照样成立。
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
    // NOTE: 一个实例都没有的组**不在树上**（"空白的分组隐藏"）⇒ 这里数出来是 0，
    // 那也正是实话；它在对话框里照样列着、照样能改名排序，只是括号里写着 0。
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

    // NOTE: 这张窗**不读选中的实例** ——
    // 一个实例都没选也开得出来，那是"先建几个组、再往里放东西"的正常顺序。
    // 把实例放进某个组是组行行尾那颗齿轮的事（见 onGroupActionRequested()）。
    // NOTE: 这张页**每次开都新建一份**（组清单 / 人数是开页那一刻的快照，留着重开会
    // 拿旧数字）。它不归 `m_groupStore` 也不归覆盖层：收页之后由下面那条钩子
    // `deleteLater()` 收掉。
    auto *dialog = new GroupDialog(toDialogGroups(m_groupStore->groups()), counts,
                                   toDialogGroups(defaults), defaultCounts, this);
    m_groupDialog = dialog;
    // 整页盖在内容区上（见 OverlayHost::showPage：摆上去就返回，不卡调用方）。
    // 这一页自己没有标题行，页眉那句就是它唯一一处标题（和它自己的窗口标题同一个出处）。
    //
    // 收页 = 用户把这一页收掉（确定 / 取消 / 返回箭头 / Esc / 被别的页顶掉 —— 全汇到
    // `pageClosed()`）。只有「确定」才落盘；其余几档磁盘一个字节都没动（对话框只改自己
    // 手里那份工作副本）。`result()` 和 `execPage()` 的返回值是同一套：确定 = Accepted。
    //
    // NOTE: 接在 `dialog` 上（不是 `this` 上）当上下文：这一页收掉之后对话框就
    // `deleteLater()` 了，连接跟着一起没；不接在 this 上的话，每开一次就多留一条永远
    // 不会匹配上的连接。
    connect(
        m_ui->overlay(), &OverlayHost::pageClosed, dialog,
        [this, dialog](QWidget *page) {
            if (page != dialog) {
                return;
            }
            m_groupDialog = nullptr;
            if (dialog->result() == QDialog::Accepted) {
                applyGroupList(dialog);
            }
            dialog->deleteLater();
        },
        Qt::SingleShotConnection);
    if (!m_ui->overlay()->showPage(dialog, GroupDialog::windowTitle())) {
        // 覆盖层刚被另一页占上了（正常走不到：入口已经先 `runWhenOverlayFree()` 让过位）。
        m_groupDialog = nullptr;
        dialog->deleteLater();
        appendLog(tr("分组设置那一页没开成：覆盖层刚被另一页占上了。"));
    }
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
    // 界面接的是同一个刷新函数（见 setupConnections）—— 在这里再来一次就是
    // 同一件事做两遍，而且"哪些路径会刷新"会变成两处各自维护的约定。
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
        // NOTE: 拆成两步的道理是"做"那一半（renameGroupTo()）不用开界面就能验。
        // 问名字那一格是 InlineMessage::getText()，验证工程能按 objectName 找到
        // QLineEdit、真的把字打进去再点确定（见 InlineMessageBox::setLineEdit）。
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
        // `row.expanded`：菜单是弹出来的那一刻拍的，中间用户完全可能又点了一下组头
        // （那一下也会落盘）。和版本树/加载器树那几处"先算再做"同一个教训。
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
        // （连带重扫一遍盘）。不挡的话 N 个实例就是 N 次全盘重扫，而实例可以有几十个。
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
    // 置顶着的那一档**不在这里返回**：它的老家和 members 指着的是两个组，而"盘上已经是
    // 什么样"这件事得由 store 那一层按老家判（下面那句 setInstanceHomeGroup() 自己做）。
    // 在这里拿 groupIdOf() 当代替的话，"把它移进它老家那一组"会被判成"已经做好了"而
    // 直接返回 —— 看着像没事，其实是要写的那一格没写。
    //
    // NOTE: 目标非空时"没表过态"和"明确选了它"要分开看 —— 光比"存的是不是它"会把
    // "明确选了它"当成已经做好了；目标为空（未分组）时**一律写**：那条保留值
    // 才是"别再自动归类"的意思，而一个从来没表过态的实例正需要它（不写的话它
    // 下一次刷新又被拉回默认组去，表现就是"选了未分组但没生效"）。
    if (!groupId.isEmpty() && !m_groupStore->isPinned(key) &&
        m_groupStore->groupIdOf(key) == groupId) {
        return true;
    }

    QString error;
    // 走 setInstanceHomeGroup() 而不是 assign()：置顶着的实例这一档**只改老家那一格**，
    // members 继续指着置顶组（那是在不在置顶组里的判据）。理由（以及"走 assign 会把老家
    // 一起丢掉"那两条）写在 InstanceGroupStore::setInstanceHomeGroup() 上面。没置顶的
    // 实例它内部就是原样委托给 assign()。
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
    // 所以那一行的展开状态由调用方自己摆好，这里只管记。
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

// ---------- 「世代备份」那三件事（见 .hpp 那一段）----------

namespace {

/// 每个实例的备份留最近多少份。**不按文件算、按"存了几次"算**：备份是"某一刻整个 config/
/// 的一批"，用不着按单个文件各留二十份（那会长出二十个文件夹、每个里面一个文件）。
///
/// NOTE: 20 这个数是拍的，不是量出来的 —— 它只影响"最多能回退几步"，而每一步都比上一步
/// 更不像用户想要的。真要按量出的数来定，得先知道用户平均多久存一次（今天没有这个
/// 数）。这里宁可留够：20 份 × 一份配置那几十 KB 是几个 MB。
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
    QStringList favorites;
    for (const auto &address : dialog->serverAddresses())
        if (m_settings->isServerFavorite(address)) favorites.append(address);
    dialog->setServerFavorites(favorites);
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

bool MainWindow::toggleInstanceFileForTest(const QString &path, QString *problem) {
    return toggleInstanceFile(path, problem);
}

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

bool MainWindow::deleteInstanceFile(const QString &path) {
    return m_instanceFileController->remove(path);
}

bool MainWindow::removeInstanceFileUnasked(const QString &path, QString *problem) {
    return m_instanceFileController->removeUnasked(path, problem);
}

// ---------- 文件管理那几页的批量按钮----------
//
// 入口全是那一页发出的**一批路径**（见 `InstanceFilesPage::selectionBar()`）。三颗按钮
// 各自一件：更新 / 禁用·启用一把 / 删除一把。**全选 / 反选 / 取消**三颗不在这儿 —— 它们
// 只动选中集，一个字节的盘都不碰，所以那边自己就办了。
//
// 【为什么「更新」是一批下，不是一条一条走 `installResourceVersion()`】
// 逐条走做不到：`downloadResourceFiles()` 头一道闸就是 `downloadManager->isBusy()`，
// 第二发会被直接拒掉 —— 那一圈里只有第一条真下得动，剩下 N-1 条全是"已有下载任务正在
// 进行"的失败框。理由写在头文件那段里。

void MainWindow::updateInstanceFiles(const QString &instanceId, const QString &pageKey,
                                     const QStringList &paths) {
    if (instanceId.isEmpty() || paths.isEmpty()) {
        return;
    }
    // 落点由**页键**算（`mods` / `datapacks` / …，唯一出处在 instanceFileDirs()）。
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

    // 先看下载器空不空，**在摆回滚点之前**。下面那句
    // `downloadResourceFiles()` 本来就失败得快（另一个批次开着时它返回 false），可那时
    // 候回滚点已经摆好了 —— 这一批几百 MB 的文件会先被原样抄一份、再原样撤掉，用户
    // 看到的是"点了更新，卡一下，然后说没能开始"。判据和「补全文件」那道闸同一个，
    // 外加那三套安装状态机（`downloadResourceFiles()` 拒绝它们的理由见那边）和
    // "服务端正占着共用下载器"那一位（它开的是同一批，被我们 beginBatch() 顶掉之后
    // 它那一条任务永远等不到收尾）。
    if (m_downloadInFlight || m_versionManager->isDownloading()
        || m_versionManager->isLoaderInstalling() || m_versionManager->isModpackImporting()
        || m_versionManager->isNamedInstanceInstalling()
        || (m_serverInstall != nullptr && m_serverInstall->ownsDownloadBatch())) {
        InlineMessage::information(
            m_ui->overlay(), tr("现在做不了"),
            tr("已经有一个下载或者安装任务在跑了，等它结束再更新这几个文件。"));
        return;
    }

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
        // 必须按**盘上这个文件自己的名字**就地换：按接口给的文件名下到"这一页那个
        // 目录"里的话，两家接口给的文件名常常和盘上那一个不一样（`sodium-fabric-0.6.13.jar`
        // vs `sodium-0.6.13.jar`）⇒ 换完之后盘上**多出一个 jar、旧的还在**，游戏同时加载
        // 两个同名模组那一类怪事就是从这儿来的，而且"按几次更新就多几份"。
        // NOTE: 这一条也让回滚点那本账成立 —— 它记的是**盘上这个文件**（相对路径），
        // 换个名字的话还原回去的就不是原来那一个了。
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

    // ---------- 回滚点（「更新前创建轻量回滚点，失败时可恢复原文件」）----------
    //
    // **动手之前**摆好，摆不成就**整个停下来**（见 UpdateRollback::create 那段：用户是
    // 看着"能退回来"这句话才按下去的，抄不出来还硬更新比没有这个功能更糟）。
    //
    // NOTE: 相对路径就是 `files` 里那些**文件名**（四个"能下载的资源"目录都在实例目录的
    // 下一层，所以"这个文件在实例里的相对路径"就是它自己那个名字）—— 这一点由
    // `instanceFileDirs()` 保证（那些目录都是 `<实例目录>/<目录名>`）。
    QString rollbackProblem;
    const UpdateRollback::Snapshot snapshot =
        UpdateRollback::create(instanceDir, instanceId, relativePaths, &rollbackProblem,
                               /*keepPrevious=*/true);
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
    // 一个资源一步（「更新任务显示每个资源的结果，不只显示总数」）。
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
    // 这一批的失败清单归它（`finishUpdateBatch()` 会先把 `m_updateBatch` 清空，所以
    // "重试"资格不能等到收尾时现读，见 finishTask()）。
    m_downloadTaskLedger->setOwner(m_updateBatch.task);

    appendLog(tr("正在更新 %1（%2）。")
                  .arg(label, QDir::toNativeSeparators(files.first().dir)));
    // `busyInstanceId` 给这个实例：正在往它的 `mods/` 里写文件（见 downloadResourceFiles
    // 里那段 —— 界面靠它把这一个实例从"能启动"里滤掉）。
    //
    // NOTE: 那个 `destinationDir` 形参现在只当**兜底**用（每条自己带 `dir`，见上面那段）；
    // 传第一条的目录 —— 它非空就行（那一处只判空）。
    if (!m_versionManager->downloadResourceFiles(files.first().dir, instanceId, label, files)) {
        // 没开起来（已经有别的批次在跑 / 那三套安装状态机占着）：账本里那一条**当场收尾**，
        // 别留一条永远"正在跑"的记录，回滚点也一起撤掉（一个字节都没动过，留着只会让
        // 更新中心上那颗「撤销上一次更新」指向一次没发生的更新）。
        m_activityCenter->tasks()->fail(m_updateBatch.task, tr("没能开始这一批更新。"));
        m_downloadTaskLedger->clearOwnerIf(m_updateBatch.task);
        m_updateBatch = UpdateBatchRun{};
        UpdateRollback::discard(instanceId);
    } else {
        UpdateRollback::commit(instanceId);
    }
    // NOTE: 这一批下完之后**不刷新那一页**。理由：换的是同一个文件名，列表上那一行
    // 一个字都不会变；而"可更新"那几颗计数在收尾时按**真换成功的**那几条摘掉
    // （见 finishUpdateBatch）。
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

// ---------- 「文件管理」那几页的图标----------
//
// 入口由 InstanceDialogController 调用；扫描状态、缓存和资源映射归 InstanceIconController。

void MainWindow::requestInstanceFileIcons(InstanceSettingsDialog *dialog, const QString &versionId) {
    m_instanceIconController->requestInstanceFileIcons(dialog, versionId);
}

void MainWindow::pushInstanceFileUpdates(InstanceSettingsDialog *dialog,
                                         const QString &instanceId) {
    if (dialog == nullptr) {
        // 这一页已经收掉了（或者压根没开着）——那就不递，盘上那份账照样记着
        // （下一次开这一页时由开页那条路补上，见调用点）。
        return;
    }
    // 键在不在 = **查过没有**，值是查出来可更新的那些路径。
    //
    // 这一份账（`m_instanceOutdatedPaths`）是**检查更新**那一趟的，不是图标那一套 ——
    // 所以留在这一层，不归 InstanceIconController。
    const auto it = m_instanceOutdatedPaths.constFind(instanceId);
    const bool checked = it != m_instanceOutdatedPaths.constEnd();
    dialog->setFileUpdates(checked ? it.value() : QStringList(), checked);
}

bool MainWindow::applyInstanceSettings(const QString &versionId,
                                      InstanceSettingsDialog &dialog) {
    // 那一整套（补稳定 id → 搬归属键 → 拼字段 → 图标那几个字节 → 写 meta →
    // 判要不要重扫树 → 分组/置顶那三条规则）归 `InstanceSettingsController::apply()`。
    // 这一层剩下的是**只有它能做的那一半**：把对话框现读成一份 `Edit`（那些值），以及
    // 回答"它老家本来在哪一组"（`homeGroupIdOf()`，显示层那一套）。
    //
    // NOTE: 这个适配器只能留在这一层 —— `InstanceSettingsDialog` 是 `ui/` 的类型，而
    // controller 那一层不许 include `ui/`。所以"能验得了"这件事靠的是把 `Edit`
    // 切出来：`applyInstanceSettings()` 收对话框引用、验证工程驱动不了它，而
    // `apply()` 收的是纯值，验得了。
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

    bool pickedIconStored = false;
    const bool ok = m_instanceSettings->apply(versionId, edit, &pickedIconStored);
    // 那张图**真落进实例目录了** ⇒ 让对话框忘掉源路径。留着的话，往后每一
    // 次"改了就写"（改个内存、勾个开关）都会**再从那个源文件抄一遍** —— 而那张图早就不
    // 需要了；用户把源文件挪走/删掉之后，下一次保存还会平白弹一句「图标没保存」（盘上
    // 明明已经是这张图）。meta 没写成（`!ok`）时**不清**：这一次等于没成，还得靠它重试。
    if (ok && pickedIconStored) {
        dialog.markPickedIconSaved();
    }
    return ok;
}

// ============================ 日志窗那两颗导出 ============================
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
 */
QString writeExportFile(const QString &path, const QString &text) {
    // 换行写成 CRLF：这两份文件的主要用途是在 Windows 上双击打开（记事本）——
    // 纯 LF 在旧一点的记事本里是一整坨。**不改内存里那一份**，只改写出去的那一份。
    QString body = SecretRedactor::redact(text);
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

// ========================= 游戏异常退出 =========================

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
    } else {
        // **没有日志窗才是常态** ——「显示游戏日志」默认关
        // （`LauncherSettings::m_showGameLog = false`），用户也可以把窗随手关掉。
        // 窗不在时读控制器那边留的每份会话最近 4000 行原文（见 sessionLogTail 的说明）；
        // 窗在的时候仍然读窗（那边留 20000 行，还带着没换行的半行）—— 这份报告正是给崩溃看的。
        errors = m_gameSessionController->sessionLogTailErrors(sessionId);
        all = m_gameSessionController->sessionLogTail(sessionId);
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
    // 窗口是**带 WA_DeleteOnClose 的**（见 `GameSessionController.hpp` 的
    // 说明），而下面那个保存框是一层嵌套事件循环 —— 这期间游戏可能正好以 0 退出，
    // `autoCloseOnExit()` 会把这一扇 close() 掉、随之销毁。所以：要读的东西**在弹框
    // 之前**读好（那一份日志也正好是用户点这颗键那一刻看到的），框回来之后只用
    // `QPointer` 判一下还能不能写回那行"已导出"。
    const QString instanceName = window->instanceName();
    const QString logText = window->allLogText();
    QPointer<GameLogWindow> windowGuard(window);
    const QString suggested =
        QDir(exportDefaultDirectory())
            .filePath(ErrorReport::suggestedLogFileName(instanceName, now));
    // 保存框**挂在那个日志窗上**（不是主窗）：它是用户刚点过的那扇窗，弹出来的框该压着它，
    // 而不是被它盖住（日志窗没有父窗，是独立的顶层窗口，见它的文件头）。
    // NOTE: 不用 `DontUseNativeDialog` —— 这一颗是**导出到哪儿**，要的是那个熟悉的
    // Windows 框（四处 QFileDialog 里另外那几处也是这么留的，见 setupConnections 里
    // 那一段说明）。
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
    appendLog(TextMarks::done(tr("日志已导出：%1").arg(shown)));
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
    const int droppedErrors = window->droppedLineCountFor(GameLogWindow::LogLevel::Error);
    const int droppedAll = window->droppedLineCount();
    facts.errorLines = lineCountOf(errors) + droppedErrors;
    facts.totalLines = lineCountOf(log) + droppedAll;
    facts.errorTruncated = droppedErrors > 0;
    facts.logTruncated = droppedAll > 0;

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
    appendLog(TextMarks::done(tr("错误报告已导出：%1").arg(shown)));
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
    // "导出这个文件"，顺手在后台下几百 MB 的运行时不是他要的。路径空着就写裸 `java`，
    // 和 LauncherManager 那边"从 PATH 找"是同一个兜底。
    const JavaSettingsOverride override =
        javaOverrideFromJson(m_versionManager->versionMeta(versionId));
    EffectiveJavaSettings javaSettings =
        resolveJavaSettings(m_javaSettings->effective(), override);
    resolveMemoryForLaunch(versionId, javaSettings, override);

    // 正文（引号规矩、`%` 写两遍、本地代码页那句注释、令牌换成环境变量引用、拼完再过一道
    // 已知凭据）归 `LaunchScriptExport::compose()` —— 那些规矩错一个字节都要等用户双击脚本
    // 才看得见，那边验证工程能逐条钉住。这一层只凑值：数据目录、Java 程序、和启动器这一趟
    // 同一份参数、以及这个身份。返回空串 = 参数里带着已知凭据，这一份不写。
    LaunchScriptExport::Input scriptInput;
    scriptInput.dataDir = AppPaths::minecraftDir();
    scriptInput.program = javaSettings.javaPath;
    scriptInput.args = launchArguments(
        versionId, versionJson, javaSettings,
        offlineMode ? token : LaunchScriptCredentials::placeholder(), offlineMode);
    scriptInput.token = token;
    scriptInput.offlineMode = offlineMode;
    const QString script = LaunchScriptExport::compose(scriptInput);
    if (script.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
            tr("启动参数中仍包含已知登录凭据，未写入脚本。请移除自定义参数中的凭据后重试。"));
        return;
    }

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
    appendLog(TextMarks::done(tr("启动脚本已导出：%1").arg(shown)));
    QStringList notes;
    notes.append(offlineMode ? tr("这一份用的是离线身份（游戏里就是这个名字）。")
                             : tr("脚本不保存登录令牌。正版建议从启动器启动；高级用法需在运行环境中提供 WTFML_MC_ACCESS_TOKEN。"));
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
