/**
 * @file MainWindow.hpp
 * @brief 声明主启动器窗口及其UI交互。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 按钮事件与版本选择在此文件中统一管理。
 */

#pragma once

#include <QAtomicInt>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPixmap>
// std::function —— `runWhenOverlayFree()` 收的是"要开的那一整段"（见它那段）。它是个
// 模板类，前置声明不够用，非得 include。
#include <functional>
// QPointer<InstanceSettingsDialog> —— 那一趟测速里对"设置页还在不在"的那一份弱引用
// （见 ServerPingRun）。用裸指针的话，用户把设置页关掉之后回来的那些结果会往一个已经
// 析构了的对话框上写：那是 **use-after-free**，不是"多画一笔"。
#include <QPointer>
#include <QProcess>
#include <QSet>

// PlayerSkinStore::PlayerSkinProfile —— 主窗口按值存一份"最近要过的那份档案"（见
// m_skinProfile），所以这里非得 include 不可（前置声明只够拿指针/引用）。
// 它是 Core+Network 的头，拖进来的东西不多。
#include "manage/PlayerSkinStore.hpp"
// SavedAccount —— accountFacePixmap() 收的就是它（底栏账号面板里那一行是谁）。
// 这个头是**纯数据 + 静态函数**（QList/QString，连 QtGui 都不碰），所以按值用就 include。
#include "auth/AccountStore.hpp"
// LoaderMeta::LoaderVersionList —— 下面那个槽和 fillLoaderVersionItems() 都按它收
// （每一条带着 stable 标记，"哪一条是测试版""稳定那一档有没有货"都要靠它，见
// MainWindow::fillLoaderVersionItems）。这是个**纯数据的头**（只有 QString/QList/QJson
// 那些 QtCore 的东西，不拖 QtWidgets），所以按值用就 include，不去走前置声明那一套。
#include "download/LoaderMeta.hpp"
#include "download/ModDependencies.hpp"
#include "download/ResourceCatalog.hpp"
// ServerStatusCache::Entry / ServerPing::Status —— 「服务器列表」那一趟测速的状态里
// 两样都按值存一份（见 m_serverPing）。两个头都很轻（QByteArray/QString 那一档），
// 所以按值用就 include，不去走"前置声明 + 指针"那一套。
#include "manage/ServerStatusCache.hpp"
// 只为了 applyModpackImport() 签名里那个 ModpackImport::Format（两种整合包清单）。
// 那个枚举没有固定的底层类型，前置声明不了，所以这里带进来。
#include "manage/ModpackImport.hpp"
// InstanceIconLookup::Result —— 「文件管理」那几页的图标反查结果按值收一份（见
// requestInstanceFileIcons）。这个头只碰 QtCore + ResourceCatalog 那个枚举。
#include "manage/InstanceIconLookup.hpp"
// ServerPing::Status / ServerPing::Pinger —— 上面那个"一趟测速"的状态，以及跑它的那件
// 东西（见 pingServers()）。这个头只碰 QtCore（QObject/QByteArray/QJsonValue），很轻。
#include "launcher/QuickPlay.hpp"
#include "util/ServerPing.hpp"
// TaskManager / TaskSnapshot / TaskStep —— 底栏那格的"任务摘要"和任务中心那几行都按值用
// （见 m_taskManager 那一段）。这个头只碰 QtCore（QObject/QDateTime/QList/QString），
// 和上面那几个同一个理由：按值用就 include，不走"前置声明 + 指针"那一套。
#include "tasks/TaskManager.hpp"
// TaskCenter::Item —— taskCenterItems() 按值回一串（那是给任务中心面板的一份**纯显示**
// DTO，见 ui/TaskCenterPanel.hpp 文件头）。同样是因为按值用它才 include 到这里。
#include "..\ui\TaskCenterPanel.hpp"
// TaskDetailPage::Action —— onTaskDetailAction() 的参数类型。理由同上：**按值/按枚举用它
// 就得 include**（前置声明说得出 `TaskDetailPage *`，说不出它的嵌套枚举）。
#include "..\ui\TaskDetailPage.hpp"
// IntegrityReportPage::Report / ::Action —— integrityReportForPage() 按值回一份，动作枚举
// 也按值传。理由同上：按值用就得 include。
#include "..\ui\IntegrityReportPage.hpp"
// VersionIntegrity::Report —— 体检结论留一份在成员里（报告页每次开都重新喂一遍）。
#include "manage/VersionIntegrity.hpp"
// 世界 / 实例的备份与还原（coding plan §4.4，2026-10-13）：那几颗按钮起的那一趟后台活
// 直接调它，界面上挑备份 / 弹结果也读它的 `Manifest` / `WriteResult` / `RestoreResult`。
#include "manage/BackupFiles.hpp"
// GameExitDiagnosis::Facts —— 游戏异常退出那一项（§4.3）留一份事实在成员里，事后
// 「导出错误报告」还要用它，所以是按值存的。
#include "diag/GameExitDiagnosis.hpp"
// 「上一次启动」那本账（coding plan §5.1，2026-10-13）：主页左栏那张卡要按值持有一条
// `Record`（`latestPlayable()` 的返回值），所以得 include 而不是前置声明。
#include "manage/LaunchHistory.hpp"
// 实例页的搜索与筛选（coding plan §7.2，2026-10-13）：`Query` / `Facts` 都是按值传的小
// 结构（`instanceFilterQuery()` / `instanceFilterFacts()` 的返回值），所以得 include。
#include "manage/InstanceFilter.hpp"

class MainWindowUI;
// 安装页那棵加载器树上的一个条目（loaderItem()）。这里只在私有函数的参数/返回值里按
// 指针用，前置声明就够 —— 不值得为了一个参数类型让每个 include 了 MainWindow.hpp 的
// 地方都把 QtWidgets 里那个大块头拖进来。
class QDragEnterEvent;  // 拖进来的文件（只用指针，所以前置声明够）
class QDropEvent;
class QTreeWidget;  // 下载页那几张资源表（只用指针和引用，所以前置声明够）
class QTreeWidgetItem;
class QThread;  // 图标反查那一趟（见 m_instanceIconThread），同上只按指针持有
class LauncherManager;
class AuthManager;
class FolderManager;
class VersionManager;
// 下载页那几类资源的取数（src/download/ResourceStore.hpp）。同上，只按指针持有。
class ResourceStore;
class GlobalJavaSettings;
class LauncherSettings;
class InstanceGroupStore;
// 「分组…」那个对话框。这里只按 const 引用传，前置声明就够 —— 没必要为了一个参数类型
// 让每个 include 了 MainWindow.hpp 的地方都把 ui/ 拖进来。
class GroupDialog;
// 「实例设置…」那个对话框。同上，只按 const 引用传。
class InstanceSettingsDialog;
// 「账号与外观」那个对话框（多账号 + 换皮肤披风）。这里**只按指针**持有它，所以
// 前置声明就够 —— 它在 MainWindow 的构造函数之后才懒建（第一次点那个入口才建）。
class AccountSkinDialog;
// 「资源详情」那一整页（下载页点一个模组进去看的那一页）。同上：只按指针持有，
// 懒建（第一次点开才建）。
class ResourceDetailPage;
/// 任务详情页（`ui/TaskDetailPage.hpp`，2026-10-12 加，coding plan §3.2）。
class TaskDetailPage;
// 「Java 管理」那一整页（设置页那颗按钮开出来的四个版本）。同上：只按指针持有，
// 懒建（第一次点开才建），而且它**不长记性**（四行每次开页现灌）。
class JavaManageDialog;
class JavaRuntimeController;
// 「游戏日志」窗（一份游戏一个，见 ui/GameLogWindow.hpp）。同上，只按指针持有，
// 而且是**按需**才建（收到第一行输出那一刻，见 showGameLogLine()）。
class GameLogWindow;
// 换皮肤披风那几条要令牌的接口（src/auth/SkinApi.hpp）。同上，只按指针持有。
class SkinApi;
// 给**离线账号**挑随机皮肤那一层（src/manage/RandomSkinStore.hpp）。同上，只按指针持有 ——
// 它挑出来的还是官方 CDN 上的地址，下不下来、画不画得出来都交给 PlayerSkinStore 那条路。
class RandomSkinStore;
// 「每实例设置」那两层的值类型（src/settings/EffectiveJavaSettings.hpp）。这里只按
// const 引用传，前置声明就够 —— 让每个 include 了 MainWindow.hpp 的地方都拖一份
// 值结构的定义没有意义。
struct EffectiveJavaSettings;
struct JavaSettingsOverride;
namespace ModpackExport {
enum class Format;
struct ExportPlan;
}  // namespace ModpackExport
// 「版本名格式」开关的数据模型。这里只按 const 引用传，前置声明就够，
// 不必为了一个参数类型把 VersionDisplay.hpp 拖进每个 include 了 MainWindow.hpp 的地方。
struct VersionNameOptions;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /// 这一窗里那个界面对象（`MainWindowUI`，不是 QObject，findChild 找不着它）。
    ///
    /// **给验证工程摆界面状态用**（ui_shot 那一类要把底栏摆成某个样子截图：它没有真会话，
    /// 摆不出"有实例在跑"这个前提，只能直接调 `setRunPanelAvailable()` 之类）。
    /// 产品代码里**不要拿它抄近路** —— 那个类的方法本来就是给 MainWindow 调的，但绕开
    /// 这里去直接摆状态，等于把"谁说了算"挪到一个不该有决定权的地方。
    MainWindowUI *ui() const { return m_ui; }

    /**
     * @brief **给验证工程摆现场用**：把"上一次点启动是什么时刻"直接摆成某个值。
     * @param identity 身份键（`launchIdentityKey()` 那个规矩：正版是 uuid、离线是账号名）。
     * @param versionId 实例（= versions/ 下面那个目录名）。
     * @param ms 毫秒时间戳（`QDateTime::currentMSecsSinceEpoch()`）；**0 或负数 = 抹掉这一格**。
     *
     * 「十秒内重复启动」那个窗口只有两种量法：真等十秒（验证工程每一跑多花十秒），或者把
     * 这一格的输入直接摆出来。判据的**全部**输入就是这一个时刻，所以摆它 == 摆现场；而
     * "窗口外那一侧"和"时钟被往回拨"这两档，只有摆得出来才验得到（等是等不出来的）。
     *
     * NOTE: 产品代码里**不要**调它 —— 真正的写点只有 confirmLaunchWhileRunning() 末尾
     *       那一处（写在"两条都放行了之后"，见那里的说明）。
     */
    void seedLastLaunchAttemptForTest(const QString &identity, const QString &versionId, qint64 ms) {
        if (ms <= 0) {
            m_lastLaunchAttempts[identity].remove(versionId);
            return;
        }
        m_lastLaunchAttempts[identity][versionId] = ms;
    }

    /**
     * @brief **给验证工程摆现场用**：把某一号会话的"启动事实"直接摆出来。
     *
     * `m_sessionLaunchFacts` 正常只有一条写点（`startGameGame()` 里拿到会话号之后那一段），
     * 而验证工程验不了那一段 —— 它要真的起一个游戏进程。可"异常退出那一项说了什么"完全由
     * 这一份事实决定，摆它 == 摆现场：摆上"要求 Java 21 / 实际用 17 / Xmx 比物理内存还大"，
     * 那三条提示就该一条不差地出现。
     *
     * NOTE: 产品代码里**不要**调它。
     */
    void seedSessionFactsForTest(int sessionId, const QString &versionId, const QString &displayName,
                                 int requiredJavaMajor, const QString &javaPath, int xmxMb,
                                 int xmsMb, const QString &loaderName,
                                 const QString &loaderVersion) {
        GameSessionFacts facts;
        facts.instanceId = versionId;
        facts.instanceName = displayName;
        facts.requiredJavaMajor = requiredJavaMajor;
        facts.javaPath = javaPath;
        facts.xmxMb = xmxMb;
        facts.xmsMb = xmsMb;
        facts.loaderName = loaderName;
        facts.loaderVersion = loaderVersion;
        m_sessionLaunchFacts.insert(sessionId, facts);
    }

    /**
     * @brief **给验证工程摆现场用**：把"刚刚成功启动了一次"这件事直接记进「上一次启动」
     *        那本账（coding plan §5.1）。
     *
     * 产品那一侧的唯一写点在 `onStartButtonClicked()` 里"会话号 > 0"那一段 —— 真起一个
     * 游戏进程是验证工程做不到的事，而主页那张卡上会写出什么**完全由账上那一条决定**
     * （哪一份、什么时候、进的是哪儿、结局如何），所以摆账 == 摆现场。
     *
     * ★ 摆进去的字节走的是**产品同一条写路**（同一个 `appendLaunchRecord()`、同一个落盘），
     *   不是测试另抄一份 —— 否则这一节验的就只是测试自己。
     *
     * @param versionId 实例 id（= versions/ 下面那个目录名）。
     * @param startedAtMs 启动时刻（毫秒）；**照产品记的是"进程真的起来"那一刻**。
     *
     * NOTE: 产品代码里**不要**调它。
     */
    void seedLaunchRecordForTest(const QString &versionId, qint64 startedAtMs) {
        appendLaunchRecord(versionId, QuickPlay::Target(), startedAtMs);
    }
    /// 同上，但这一趟是**快捷启动**（走 quick play 直接进某个世界 / 某台服务器）。
    void seedQuickPlayRecordForTest(const QString &versionId, QuickPlay::Kind kind,
                                    const QString &value, qint64 startedAtMs) {
        QuickPlay::Target target;
        target.kind = kind;
        target.value = value;
        appendLaunchRecord(versionId, target, startedAtMs);
    }
    /**
     * @brief **给验证工程摆现场用**：把账上那一条的结局改写成"这一趟结束了"。
     *
     * @param startedAtMs 启动时刻 —— 账上那一条就是靠它认的（产品那一侧也只有这一个键
     *        能从"退出那一刻"追回"启动那一刻"记的那一条）。
     * @param crashed 异常退出（`QProcess::CrashExit` 或非 0 退出码，判据是
     *        `GameExitDiagnosis::isAbnormal()`）。
     * @param neverStarted 游戏**连第一行日志都没打出来**就结束了 ⇒ 这一趟不算"玩过"
     *        （`Result::StartFailed`）。产品的判据是"退出时它还在 `m_launchingSessions`
     *        里"，验证工程摆不出真进程，所以这一位直接由调用方给。
     */
    void seedLaunchFinishForTest(qint64 startedAtMs, bool crashed, bool neverStarted) {
        finishLaunchRecord(startedAtMs, launchHistoryResult(crashed, neverStarted),
                           QDateTime::currentMSecsSinceEpoch());
    }

    /**
     * @brief **给验证工程摆现场用**：把某一趟「检查更新」的结果直接摆出来（coding plan
     *        §7.2 实例页那一颗「可更新」用的就是它）。
     *
     * 产品那一侧只在一个地方写这份名单（`finishUpdateCheck()` 里那一句 insert），而走到
     * 那里要先跑一整趟反查 + 版本列表（真网络）。实例页的筛选**只读这一份名单**（键在
     * 不在 = 查过没有，见 m_instanceOutdatedPaths 那段），所以摆它 == 摆现场。
     *
     * NOTE: 只摆**名单**那一位（`m_instanceOutdatedPaths`）—— 和它一起写的
     *       `m_instanceUpdateTargets`（"换成哪一版"）是给"点更新"那条路用的，实例页的
     *       筛子不读它，所以这里不去编一份假的。摆完顺手重灌一次树，那颗 chip 当场生效。
     * NOTE: 产品代码里**不要**调它。
     *
     * @param versionId 实例 id。
     * @param paths 查出来可更新的那些路径（**绝对路径**，禁用着的那条带 `.disabled`）；
     *              给空表 = "查过了、一条都没有"（和"没查过"是两句话）。
     */
    void seedOutdatedPathsForTest(const QString &versionId, const QStringList &paths) {
        m_instanceOutdatedPaths.insert(versionId, paths);
        rebuildInstanceTree();
    }
private slots:
    void onStartButtonClicked();
    // 登录三态（AuthManager 的三个信号）。登录是异步的：点完按钮到浏览器回调回来之间
    // 可能有几十秒（用户还得输账号密码），所以这三条都得在界面上有反应 ——
    // 尤其 loginFailed，它以前**根本没有人接**，失败只在浏览器那个页面里看得见。
    void onLoginStarted();
    void onLoginSucceeded(const QString &playerName);
    void onLoginFailed(const QString &errorMessage);

    // ---------- 「账号与外观」这个模块（主页右栏那个入口 → AccountSkinDialog） ----------
    // 入口按钮：第一次点会把这个窗建出来（懒建，没点过就不占内存），然后刷新一遍再显示。
    void onAccountSkinButtonClicked();
    // 用户在这个窗里点了「登录新账号…」。走的是和设置页那个登录按钮**同一条路**
    // （AuthManager::loginMicrosoft()），只是入口在另一个窗上。
    void onAccountLoginRequested();
    // 用户在那个窗里填了个离线名字。合法性（空 / 有空白 / 太长）由
    // AccountStore::addOffline() 判（唯一出处），失败按产品约定弹窗。
    void onOfflineAccountRequested(const QString &name);
    void onAccountSwitchRequested(const QString &uuid);
    void onAccountRemoveRequested(const QString &uuid);
    // 用户选好了一张皮肤图。**尺寸那条规矩在这条路上判**（SkinApi::isAcceptableSkinSize，
    // 唯一出处），失败按产品约定弹窗。
    void onSkinUploadRequested(const QString &path, const QString &variant);
    void onCapeSelectRequested(const QString &capeId);
    void onCapeHideRequested();

    // ---------- 主页右栏那个小人 + 底栏头像（三个都是 PlayerSkinStore 的信号） ----------
    /// 官方回的档案到了。和手上这份一样时**什么都不做**（不重下贴图、不重画）——
    /// 用户在游戏里没换皮肤时，后台那次刷新必须是完全看不见的。
    /// @param uuid 这份档案是**谁的**（PlayerSkinStore 已经规整过的那一串）。回来的时候
    ///        "当下是谁"可能已经变了，落盘和画都得认它，不能认 m_authManager 里那个。
    void onSkinProfileReady(const QString &uuid, const PlayerSkinProfile &profile);
    /// 一张贴图在本地就绪了。皮肤和披风是**分开**回来的，这里按 URL 认领。
    void onSkinTextureReady(const QString &url, const QString &path);
    /// 要不到（网络失败、下回来的不是皮肤图、存不下来）。**只记日志、不弹窗** ——
    /// 这是全仓唯一一处不弹窗的错误，理由见 PlayerSkinStore.hpp 文件头。
    void onSkinFailed(const QString &message);
    /// 离线账号的随机皮肤挑好了（RandomSkinStore::skinReady）。
    ///
    /// 拿到的是**官方 CDN 上的一个地址**，接下来和"官方回的档案"走完全同一条路：记盘 →
    /// 当成这一份档案的 skinUrl → requestSkinTexture() 去下。离线账号没有披风，
    /// 所以 capeUrl 留空（那个人没有披风本来就是常态，见 requestSkinTexture）。
    void onRandomSkinReady(const QString &skinUrl);
    /// 把一份外观贴到界面上（右栏那个小人 + 底栏头像 + 右栏那句话）。
    /// @param skinPath 空串 = 用默认外观。载不进来（文件没了/下了一半）也走这一档。
    /// @param slim 纤细模型（手臂 3 宽）。
    ///
    /// NOTE: 是**槽**不只是私有函数：验证工程靠 QMetaObject::invokeMethod 驱动它
    ///       （沙盒里现写两张 PNG 喂进来），**不联网**就能验完整条界面路径。
    void applyPlayerSkin(const QString &skinPath, const QString &capePath, bool slim);

    /**
     * @brief 底栏账号面板里某一行的行首头像（24×24，就是 `bottomBarPanelIconSize()`）。
     * @param account 那一行是谁（用 `uuid` 认人 —— 名字可以重复）。
     * @return 一张一定有东西的图；实在什么都没有时是**自己画的首字母方块**。
     *
     * 用户原话（2026-09-25）：「在右下角那个上拉框里名字左边加一个头像（因为选择/登录过，
     * 所以肯定会有缓存，应该不会遇到丢失的情况）」—— 于是这条路**只认缓存**，
     * 联网 / 换令牌 / 读客户端 JAR 一个都不做，取不到就自己画（那不算出错）。
     *
     * 五个来源，按优先级：
     *   ① 当前账号 —— 底栏那颗头像现成的（`m_avatarPixmap`），缩一下；
     *   ② 正版账号 —— 缓存里那份档案的 `skinUrl` 对应的贴图（PlayerSkinStore 的两级缓存）；
     *   ③ 离线账号 —— `RandomSkinStore` 记着他挑中的那张；
     *   ④ 离线账号 —— `DefaultSkin` **算出来的**那张官方默认皮肤的缓存图（**只拼路径探测，
     *      绝不走 resolve()**：那一条要起 PowerShell 读 JAR，几百毫秒，不能挂在开面板上）；
     *   ⑤ 都没有 —— `SkinModel::letterAvatar()` 画的首字母方块。
     *
     * NOTE: 每次开面板都要给**全部**账号各算一张（面板里只显示三个，但哪三个是那个
     *       函数按 `saved_at` 排出来的，不在这里再排一遍）。代价是几次本地文件探测，
     *       可以接受；**换来的好处**是这条路永远不会"卡在网络上"。
     */
    QPixmap accountFacePixmap(const SavedAccount &account) const;

    /// 猜"某个离线账号当初是按哪个实例算的官方默认皮肤"（见 accountFacePixmap() ④）。
    /// @param currentVersionId 现在选中的那个实例（由调用方给，好让这个函数不碰界面）。
    /// @return 按可能性从高到低的几个版本号，去重；一个都猜不出来时是空表。
    QStringList offlineDefaultSkinCacheVersions(const QString &currentVersionId) const;
    void onAddFolderClicked();
    /// 改名的**执行部分**：校验名字 → 改磁盘 → 同步状态（LastLaunched / 选中项）→ 刷界面。
    /// 失败时弹窗并返回 false。
    ///
    /// NOTE: 和"问新名字"那一步分成两步，是为了**能被验证工程驱动** —— 这一半里藏着最容易
    ///       漏的那件事：改完名要同步 Version/LastLaunched，不然用户下次打开启动器，选中项
    ///       会空着，他会以为实例丢了。那条只有真的跑一遍才验得出来。
    ///       （2026-09-25 之前这里写的是"因为 QInputDialog 在 offscreen 下驱动不了"——
    ///       那一档换成 InlineMessage::getText() 之后这个理由**不成立了**：问名字那一格现在
    ///       是能按 objectName 找得到、能真的打字的 QLineEdit。见 InlineMessage.hpp。）
    bool applyRename(const QString &oldId, const QString &newId);
    /// 「分组设置…」：管**分组本身**（新建 / 重命名 / 删除 / 上下移 / 默认展开 / 恢复默认）。
    ///
    /// NOTE: 2026-09-25 起它**不读选中的实例**（用户原话：「下方的分组设置不再是针对单个
    ///       实例的设置」）—— 从哪儿进来都是同一张表，没选中实例也能开。把实例放进某个组
    ///       是**组行行尾那颗齿轮**的事（见 onGroupActionRequested()）。
    ///
    /// 不分组也能用 —— 对话框里那份工作副本只有在点「确定」之后才由 applyGroupList()
    /// 落盘，取消的话磁盘一个字节都不动。
    void onGroupInstanceClicked();
    /// 分组的**执行部分**：把对话框里摆好的那份组清单落到磁盘上。失败时弹窗并返回 false。
    ///
    /// NOTE: 拆成两步的理由和 applyRename() 一样 —— QDialog::exec() 会阻塞在事件循环里，
    ///       验证工程驱动不了它。而这一半里藏着真会错的东西（组清单和文件里那份怎么比、
    ///       写失败了怎么说），必须验得了。
    /// NOTE: 过的是**指针**不是引用：验证工程按名字驱动它（QMetaObject::invokeMethod +
    ///       Q_ARG），而 Q_ARG 要的是一份可拷贝的值 —— QDialog 不可拷贝。调用方那个对象
    ///       活得好好的（栈上那个 dialog），指针只是"这一半不接管它的生命周期"。
    bool applyGroupList(const GroupDialog *dialog);
    /// 组行行尾那颗齿轮里某一项被点了。@param groupId 哪一组（虚拟的「未分组」是空串）。
    /// @param action MainWindowUI::GroupAction 那一套里的某一项。
    ///
    /// NOTE: 这几项**都是立刻生效**的（不等谁按确定）：齿轮菜单不是对话框，它是"点一下
    ///       就办事"的一列动作。要问一句的那两项（重命名、删除）自己弹自己的框。
    void onGroupActionRequested(const QString &groupId, int action);
    /// 把某一组里的实例**都移出分组**（齿轮菜单里「把这一组里的实例都移出分组」）。
    /// 组留着、实例一个都不删，它们只是变成未分组。
    /// @param groupName 那一组的名字（只用来写日志和弹窗 —— 名字得是**菜单弹出来那一刻**
    ///        用户的叫法，所以由调用方带着，这里不再查一遍）。
    ///
    /// NOTE: 它动的是"树上挂在这一组底下的那些"实例 —— 那正是用户眼睛里"这一组里的东西"。
    ///       每个实例都要**明确写一条"未分组"**：不写的话没表过态的那些下一次刷新又被
    ///       自动归类拉回来（理由写在实现里那段）。
    void clearGroupMembers(const QString &groupId, const QString &groupName);
    /// 把当前选中的那个实例的**老家**改成 @p groupId 这一组（空串 = 未分组）。
    /// 挑目标那个框的回调。
    ///
    /// 取值口径和另外两颗实例级按钮一样：**下拉框**说了算（它就是"当前选中的那个实例"），
    /// 不是树的 currentItem()（点一下组头它也会变）。
    /// @return 写成功了返回 true（失败弹窗并返回 false）。
    bool setSelectedInstanceHomeGroup(const QString &groupId);
    /// 把一个**指定**的实例的**老家**改成 @p groupId 这一组（空串 = 未分组）。
    ///
    /// NOTE: 拆出来的理由是实例设置对话框那条路 —— 它手上是**那个实例的 id**，而"当前选中的
    ///       是哪一个"（上面那个函数的口径）在对话框开着的时候完全可能已经被改过了。
    ///       两条路必须写同一份归属逻辑，不然"从菜单里移"和"从设置里移"迟早会不一样。
    ///
    /// NOTE: 【为什么叫"老家"而不是 2026-09-26 之前的"移入分组"】那时候"在不在置顶组里"和
    ///       "归在哪一组"是同一格，所以"移进某一组"就等于"把它从置顶组里拿出来"。两边都
    ///       显示之后这两件事分开了：「显示在置顶组中」是勾选框管的，这个函数**一个字都不
    ///       动它**。所以置顶着的实例被"移进 Y"之后仍然置着顶、仍然在置顶组里多摆一条，
    ///       只是老家换成了 Y —— 这正是用户说的"两边都显示"。
    bool setInstanceHomeGroup(const QString &versionId, const QString &groupId);
    /// 把某一组的组名改掉（齿轮菜单里「重命名这一组…」）。
    /// @return 改成功了返回 true。
    bool renameGroupTo(const QString &groupId, const QString &newName);
    /// 删掉某一组（齿轮菜单里「删除这一组…」）。组里的实例一个都不删。
    /// @return 删掉了返回 true。
    bool deleteGroupById(const QString &groupId);
    /// 把某一组的「展开还是收着」记下来（点组行那一格、或者齿轮菜单里那一项）。
    ///
    /// NOTE: 它写盘但**不发 groupsChanged**（理由见 InstanceGroupStore::setGroupExpanded()）
    ///       —— 所以调用方要自己把那一条的展开状态摆好，这里只管记。
    void rememberGroupExpanded(const QString &groupId, bool expanded);
    /// 给某一组弹齿轮菜单（锚点 = 那一行行尾那颗齿轮占的格子）。
    ///
    /// NOTE: 它和 onGroupActionRequested() 之间过的是 MainWindowUI::GroupRow，而那个类型
    ///       在 MainWindowUI.hpp 里 —— 本文件**不 include 它**（见文件头那几条前置声明的
    ///       说明），所以"翻快照"那一步是 .cpp 里的一个文件级自由函数
    ///       （groupRowsForMenu()），不在这里出现。
    void openGroupMenuFor(QTreeWidgetItem *item);
    /// 实例设置那一页的**入口**：给某一个实例打开它。进来的每一处都带着一个具体的
    /// instanceId —— 点实例页那一行（任意处）、那一行行尾那颗常显的齿轮、底栏左面板那行
    /// 的齿轮，三条路都走它。
    ///
    /// NOTE: 所有入口共用这一条（别在入口里另写一遍对话框）—— 里面那段"已经写了的覆盖
    ///       读回来当起点、当前全局是什么"的口径只有一份；**改名也在这条路上**（用户
    ///       2026-09-25 原话「重命名进实例设置」）：页面上改「实例名」那一格、按确定之后
    ///       先 applyRename() 再写设置，顺序的理由写在实现里。
    ///
    /// @param pageKey 开出来之后**停在哪一页**（空 = 不指定，停它自己的第一页「概览」）。
    ///        只有那两条"从别处回来"的路会传（见 `openFileResourcePage()` /
    ///        `returnToInstanceSettingsFromDownload()`）—— 用户原话「从模组点进去，返回
    ///        也在模组而非概览，其余同理」。从实例列表**新**点开一个实例的那几处一律不传：
    ///       那不是"回来"，用户没在等哪一页。
    ///
    /// ★ 这一页**永远归「实例」那一颗选项卡**（`OverlayHost::execPage()` 的 `ownerTab`），
    ///   开之前先把选项卡切过去。不钉的话，从下载页那颗「← 返回实例设置」进来时当前选项卡
    ///   是**下载**，这一页会被记成下载页的页面 —— 用户点「实例」看见的是实例列表、那一页
    ///   收着看不见，而它占着覆盖层那一位 ⇒ 此后每次开实例设置都被挡下，而且**每次都被
    ///   拽回下载页**（用户报的那条："卡住进不去实例设置、还老变到下载页"）。
    void openInstanceSettings(const QString &versionId, const QString &pageKey = QString());

    /// 「加入选中的世界 / 加入服务器」那两颗按钮按下去之后走的那一条（2026-10-10）。
    ///
    /// 用户 2026-10-10 原话：「世界和服务器都做一个临时占位（加入选中的世界（仅限有这个
    /// 参数的版本）/服务器）先试试看能不能生效」—— 所以它不是"说一句还没做"的占位，
    /// 而是**真的把游戏按这个参数开起来**，走的就是点「启动」那一条路。
    ///
    /// 这一半只做两件事：问一句"这个版本认不认"（`QuickPlay::supports()`，界面上那两颗
    /// 按钮在支持的版本上才露，这是兜底），然后**让位 + 排到下一轮**再开
    /// （`runWhenOverlayFree()`）—— 调用点在这个信号里，实例设置那一页正卡在它自己的嵌套
    /// 事件循环里，当场去启动等于"页还开着就开始下载/挑 Java"。
    ///
    /// @param versionId 哪一个实例（按钮所在那一页的实例）。
    /// @param kind 进存档还是连服务器。
    /// @param value Singleplayer 给**存档目录名**（不是路径）；Multiplayer 给服务器地址。
    void startQuickPlay(const QString &versionId, QuickPlay::Kind kind, const QString &value);
    /// 「这个实例（版本）认不认这条 quick play 参数」。**唯一判据出处**（`QuickPlay::supports()`
    /// 是它唯一读的那份逻辑）。
    ///
    /// Why 要收成一个函数：同一句话有两个读者 —— ① `startQuickPlay()` 的第一道闸；
    /// ② 界面上那两颗「加入」按钮**露不露**（见 `refreshInstanceDirs()` 里那两句
    /// `setServerJoinText()` / `setJoinText()`）。两处各读一遍版本 json 的话，判据迟早会
    /// 漂：按钮露着而 `startQuickPlay()` 说不支持（点了弹一句"用不了"），或者反过来
    /// （明明能用，按钮却藏在页面里）。
    ///
    /// NOTE: 读的是**合并过的** json（`getVersionJson()`）而不是加载器自己那一份 —— 快速游玩
    ///       那几条是**原版**的 `arguments.game` 条目，Forge / NeoForge 的 profile 里根本没有，
    ///       靠继承才看得到（见 QuickPlay.hpp 那段）。
    bool instanceSupportsQuickPlay(const QString &versionId, QuickPlay::Kind kind) const;
    /// `startQuickPlay()` 让位之后真正跑的那一半（分成两个函数的理由见
    /// `runWhenOverlayFree()` 那段 NOTE）。它把 `m_pendingQuickPlay` 排上、把「已安装版本」
    /// 下拉框切到 @p versionId，然后**调点「启动」那一条路**（`onStartButtonClicked()`）——
    /// 令牌 / 挑 Java / 启动提醒那几道闸因此一道都不绕过。
    void startQuickPlayNow(const QString &versionId, const QuickPlay::Target &target);
    /// 「导出启动脚本」：把"按这个实例现在的这套参数启动"写成**一个 .bat**。
    ///
    /// 用户 2026-09-26 原话：「弹窗让用户选保存位置，出 .bat」。问路径那一半在对话框里
    /// （那一步开的是 Windows 自己的保存框，见 ui/InstanceSettingsDialog.cpp 里那个调用点），
    /// **这一半是"拿一条路径真的去写"** —— 收两个字符串，所以验证工程可以直接
    /// `invokeMethod` 进来喂一条临时路径，不用去驱动那个保存框。
    ///
    /// @param versionId 实例 id（= 目录名）。
    /// @param path 用户挑好的完整路径（带 .bat）。
    ///
    /// NOTE: 脚本的内容必须和**真正启动那一次**逐字一致（两边调的是同一个
    ///       launchArguments()，见 MainWindow.cpp 里那两处调用点），
    ///       否则"导出来的能跑"这句话就没有依据。唯一**故意**不同的地方是程序名那一格：
    ///       那边由 LauncherManager 从 PATH 兜底，这边写的是同一个兜底值（裸 `java`）。
    void exportLaunchScript(const QString &versionId, const QString &path);
    /**
     * @brief 「导出…」：把这个实例按**整合包规范**打包成一个能直接导入的文件。
     *
     * 和上面那颗 `exportLaunchScript` **不是一件事**（名字像，做的是两件事：那一颗出的是
     * .bat，这一颗出的是整合包）。同一条切法：问路径在对话框里（Windows 自己的保存框），
     * 这一半收一条路径把事情做完 —— 所以验证工程能直接喂一条临时路径把整趟跑完。
     *
     * 格式**从扩展名读**（`.mrpack` = Modrinth / `.zip` = CurseForge），不做第三档参数：
     * 那正是对话框里那两个筛选项已经在问的东西（见那个信号的 NOTE）。
     *
     * 这一趟要枚举实例目录、算哈希、联网反查、再打一个 zip。打包阶段放在后台线程，
     * 进度和结果仍回到主线程展示，不另开独立进度窗口。
     *
     * @param versionId 实例 id（= 目录名）。
     * @param path 用户挑好的完整路径（带 `.mrpack` 或 `.zip`）。
     */
    void exportModpack(const QString &versionId, const QString &path);
    /// 「补全文件」：校验这个实例的文件，只补缺的和损坏的（用户原话：「校验已装的，
    /// 缺哪个补哪个」）。
    ///
    /// 干活的在 VersionManager::repairVersionFiles()，这里管的是"该不该开始、开始之后
    /// 界面成什么样"：三道守卫（这个实例自己在忙 / 别的批次在跑 / 继承链不完整），
    /// 置 m_repairingInstanceId + 界面忙态，收尾在 onVersionDownloadFinished() 里。
    void repairInstanceFiles(const QString &versionId);

    // ------------------------------------------------------------------
    // 完整性检查与修复（coding plan §4.1，2026-10-12）
    // ------------------------------------------------------------------
    // 和上面那颗「补全文件」是同一件事的两半：那一颗缺什么补什么（直接开一批下载），这几
    // 个先**体检**（只算不装，不联网、不改盘），扫完把结论摊在内容区一整页上，页上那颗
    // 「补齐缺失或损坏文件」再回到 repairInstanceFiles() 那条现成的补全路。
    //
    // ★ 扫描在**后台线程**里跑（`QThread::create`）：清单在 UI 线程上取好（快），那几千条
    //   比对（含几十兆 jar 的哈希）交给 worker，进度按 `invokeMethod` 排回主线程记进任务账本。
    //   于是**扫描期间界面是活的**（coding plan §4.1：「扫描期间允许浏览别的页面」）。

    /// 用户点了概览栏那颗「检查并修复」：起一趟体检（`TaskKind::Integrity` 那一项）。
    ///
    /// 三道守卫（和 repairInstanceFiles 同一套）：这个实例自己在忙 / 别的批次在跑 / 继承链
    /// 不完整；再加一道"这个实例已经在扫了"。
    void submitIntegrityCheck(const QString &versionId);
    /// 这个实例是不是正在被体检（删除 / 改名那两处要按它拦一下，见 coding plan §4.1
    /// 「同一实例不允许同时扫描和删除/改名」）。
    bool integrityScanningVersion(const QString &versionId) const;
    /// 开报告页（把最新那一份结论摊开）。扫完由 onIntegrityScanFinished() 排一轮调它。
    void openIntegrityReportPage(const QString &versionId);
    /// 把 `VersionIntegrity::Report` 翻成报告页认的那份形状（**唯一翻译点**，和
    /// `taskCenterItem()` 翻任务账本是同一个路数）。
    IntegrityReportPage::Report integrityReportForPage(const VersionIntegrity::Report &report,
                                                       const QString &instanceName) const;
    /// 用户点了报告页上的某颗动作（复制报告 / 补齐缺失或损坏文件）。
    void onIntegrityReportAction(IntegrityReportPage::Action action);
    /// 把这一份体检结论拼成纯文本放进剪贴板（走 `VersionIntegrity::plainReport()`）。
    void copyIntegrityReport();
    /// 后台那一趟体检报上来的进度（**一定在 UI 线程上**，worker 走 invokeMethod 排过来的）。
    /// `groupIndex` 是"正在比哪一组"（0..3，见 `VersionIntegrity::allGroups()`）。
    void onIntegrityScanProgress(int checked, int total, int groupIndex);
    /// 体检跑完了（同样在 UI 线程上）。收任务、存结论、开报告页。
    void onIntegrityScanFinished(const VersionIntegrity::Report &report);

    // ---------- 世界与实例备份（coding plan §4.4，2026-10-13）----------
    //
    // 四个入口（世界那一页三颗 + 概览那两颗）最后都落到**同一个**引擎
    // （`src/manage/BackupFiles`）和**同一趟**后台活上。分成"打一份"和"还原一份"两条路：
    //
    //   打一份：世界页「立即备份」/ 概览「创建快照…」 → `startBackupWrite()`
    //   还原：  世界页「恢复备份…」/ 概览「从快照恢复…」 → `startBackupRestore()`
    //
    // ★ 一次只跑一趟（共用 `m_backupTaskId`）：备份和还原都是"走一遍整棵目录、算每个文件的
    //   哈希"，两趟叠着跑只会互相抢盘，而且账本里那两行进度会一起跳。
    //
    // NOTE: 走 `QThread::create` + `invokeMethod` 那一套（和体检同一个路数）而不是
    //       `QtConcurrent`：这两趟都**没有**进度回调（"一共多少个文件"要等整棵走完才知道，
    //       见 .cpp 里那一段），所以不需要 `QFutureWatcher` 那一层。

    /// 用户点了世界页的「立即备份」：把整个存档文件夹打成一个 zip（`TaskKind::Backup`）。
    void backupWorldNow(const QString &worldDir);
    /// 用户点了世界页的「恢复备份…」：挑一份这个世界的备份还原回去。
    void restoreWorldBackup(const QString &worldDir);
    /// 用户点了概览栏的「创建快照…」：先问一句"要不要连能重新下回来的那部分一起打包"，
    /// 再把这个实例打成一个 zip。
    void snapshotInstance(const QString &versionId);
    /// 用户点了概览栏的「从快照恢复…」：挑一份这个实例的快照还原回去。
    void restoreInstanceSnapshot(const QString &versionId);
    /// 打开备份目录（`Kind` 只决定进哪一个子目录：世界 / 实例）。
    void openBackupFolder(BackupFiles::Kind kind);
    /// 在 @p dir 底下按**文件名前缀**挑出属于 @p label 的那些备份，让用户选一份。
    ///
    /// @param what 只有"一份都还没有"那句话用得上（「这个世界」/「这个实例」）。
    /// @return 选中的那份 zip 的绝对路径；一份都没有 / 用户取消 = **空串**（该说的话
    ///         已经在这个函数里弹过了，调用方直接 return 就是）。
    QString pickBackupZip(const QString &dir, const QString &label, const QString &what);

    /// 起一趟"打一份备份"的后台活。返回账本里那一项的任务号（没起成 = 无效号）。
    ///
    /// @param manifest 描述（`files` / `totalBytes` 由后台那一趟自己填）
    /// @param sourceDir 要打进去的目录
    /// @param targetZip 落点（父目录由这一层建）
    /// @param title 账本上那一行写什么
    /// @param targetId 这一项挂在哪个实例上（打开日志 / 打开目标位置用它）
    /// @param successTitle 成功之后那个框的标题（世界和实例各说各的）
    Tasks::TaskId startBackupWrite(const BackupFiles::Manifest &manifest, const QString &sourceDir,
                                   const QString &targetZip, const QString &title,
                                   const QString &targetId, const QString &successTitle);
    /// 起一趟"还原一份备份"的后台活（先做安全快照 → 挪走旧的那一份 → 解压 → 对账）。
    Tasks::TaskId startBackupRestore(const QString &zipPath, const QString &targetParentDir,
                                     const QString &title, const QString &targetId,
                                     const QString &successTitle);
    /// 后台那一趟打完了（**一定在 UI 线程上**）：收任务、记日志、弹结果。
    void onBackupWriteFinished(const BackupFiles::WriteResult &result, const QString &successTitle);
    /// 后台那一趟还原完了（同样在 UI 线程上）。
    void onBackupRestoreFinished(const BackupFiles::RestoreResult &result,
                                 const QString &successTitle);
    /// 这一份备份的文件名在**盘上是否真在**（挑备份那两处按前缀筛完再核一遍）。
    /// 顺带把"多大 / 什么时候的"拼成一句给清单用（**读一份清单要起一次 PowerShell**，
    /// 所以挑之前只看得见文件名和这个便宜的大小/时间，见 `BackupFiles::backupFilePrefix`）。
    static QString backupPickLine(const QString &zipPath);
    /// 「复制实例」（2026-09-28，用户原话「实例设置的概览里面加一条复制实例，点击以后让
    /// 用户选择复制到的文件夹，然后给定复制后的实例名称」）。
    ///
    /// 切法和上面几颗一样：**问的那一半**（选文件夹、要名字）在 MainWindow.cpp 里那个
    /// 私有 lambda 里，**真正动手的这一半**收三个字符串 —— 所以验证工程可以直接
    /// `QMetaObject::invokeMethod` 进来喂一条临时路径，不用去驱动那两个原生对话框。
    ///
    /// 四道闸（2026-09-29 起是四道）：名字合不合法 / 这一份正在跑 / 这一份正在装 /
    /// **目标不能落在这一份实例自己里面**（最后那条见 VersionManager::copyInstance 的说明）。
    ///
    /// @param sourceId 源实例 id（= 目录名）。
    /// @param destGameFolder 用户挑的那个**游戏文件夹**（副本落在它的 `versions/` 底下，
    ///        不是直接落在它里面 —— 理由见 VersionManager::copyInstance 的说明）。
    /// @param newName 副本叫什么（新的实例 id）。
    /// @return 做成了 true。失败时原因已经弹出来了。
    bool copyInstanceTo(const QString &sourceId, const QString &destGameFolder,
                        const QString &newName);
    /// 「导入世界」真正动手的那一半（顶上那颗按钮，2026-09-28）：把一个世界目录拷进
    /// 这个实例的 `saves/`。
    ///
    /// 干活的在 WorldFiles::copyWorldInto()（纯文件操作、单测在 verify 工程里），这里管的是
    /// "落点是哪儿"（`AppPaths::versionDir(id) + "/saves"`）、失败怎么报、成了之后让那一页重列。
    ///
    /// @param instanceId 目标实例 id（= 目录名）。
    /// @param worldDir 用户挑的那个世界目录（要含 `level.dat`）。
    /// @param name 拷进去之后那个目录叫什么。
    /// @return 做成了 true。失败时原因已经弹出来了。
    bool importWorldInto(const QString &instanceId, const QString &worldDir, const QString &name);
    /// 「存档」那一页行尾那支笔里的「重命名…」真正动手的那一半（2026-09-28）。
    ///
    /// 干活的在 WorldFiles::renameWorld()，这里只管防卫和报错。
    ///
    /// @param instanceId 这个世界属于哪个实例（**只用来判"游戏在不在跑"**）。
    /// @param worldDir 要改的那个世界目录（含路径）。
    /// @param newName 新名字。
    /// @return 做成了 true。失败时原因已经弹出来了。
    ///
    /// NOTE: 那道"正在跑就不许改"的闸是**早于磁盘的一层保险**，不是唯一的一层：真在跑
    ///       的时候，游戏那边握着 `session.lock` 的句柄，`QDir::rename()` 本来也会失败
    ///       （Windows 会拒）。但那句失败理由只能说到"是不是有别的程序开着"，用户不一定
    ///       能把"游戏在跑"和它联系起来 —— 所以这里先把话说死。
    bool renameInstanceWorld(const QString &instanceId, const QString &worldDir,
                             const QString &newName);
    /// 实例行行尾那颗**上箭头**：把这个实例移进置顶组（哪一组当家见
    /// InstanceGroupStore::pinnedGroupId()），**并记下它置顶之前待在哪一组**（见那个类的
    /// 文件头那一段）。没有置顶组时什么都不做。
    void pinInstance(const QString &versionId);
    /// 同一格反过来那一颗（**下箭头**）：把这个实例送回它**置顶之前**待的那一组（用户
    /// 2026-09-25 原话：「置顶组里的实例悬停时右侧显示下箭头，即取消置顶」；落点由
    /// 2026-09-26 那句定死：「加一个记录置顶前这玩意属于哪一组的，用户直接塞的取消置顶才
    /// 归进未分类」）。**没有记录**（用户自己用「移入分组」塞进去的、或者这一版之前的老
    /// 数据）时落未分组 —— 和「移出分组」逐字同一个落点。
    void unpinInstance(const QString &versionId);
    /// 实例行行尾那个**垃圾桶**，以及实例设置概览页那颗「删除实例」：先问一句（这一趟
    /// 不可逆）再调 deleteInstance()。返回"真的删掉了" —— 唯一的调用方（概览页那一颗）
    /// 要靠它决定"这一页还收不收"（实例都没了，页面上那几格已经没有意义）。
    bool confirmDeleteInstance(const QString &versionId);
    /// 删除实例的**执行部分**：把 versions/<id> 整个目录删掉 + 刷界面。失败返回 false。
    ///
    /// NOTE: 拆成两步的理由和 applyRename() / applyInstanceSettings() 一样 —— 问的那一半
    ///       在覆盖层里跑一个嵌套事件循环，验证工程驱动不了它；而"守卫 + 真的删"这一半
    ///       必须验得了（运行中不许删、删完列表要跟着变）。
    bool deleteInstance(const QString &versionId);
    /// 实例设置的**执行部分**：把对话框里摆好的覆盖写进这个实例的 wtfml.meta。
    /// 失败时弹窗并返回 false，磁盘保持原样。
    ///
    /// NOTE: 拆成两步的理由和 applyRename() / applyGroupList() 一样 ——
    ///       QDialog::exec() 阻塞在事件循环里，offscreen 的验证工程驱动不了它。
    ///       而这一半里藏着真会错的东西（"跟随全局"的项要**删键**而不是写一个等于
    ///       全局的值、写盘前得先确保实例有稳定 id），必须验得了。
    bool applyInstanceSettings(const QString &versionId, const InstanceSettingsDialog &dialog);

    // ---------- 实例设置「文件管理」那八页（2026-09-26）----------
    //
    // 那八页说的是**这个实例自己的目录**（`--gameDir` 就是 `versions/<id>/`，见
    // LauncherManager 里那两处），所以动它们不会碰到别的实例。
    //
    // 分工和「导出启动脚本」那两颗按钮一样：对话框只**显示**和**报"用户想干这件事"**，
    // 真正碰盘、真正弹话的都在这一层 —— 弹话要 OverlayHost，而它在 MainWindow 手里。
    // 动完盘之后由调用点喊 dialog.refreshFilePages() 让它重列一遍。

    /// 「文件管理」那六页各看哪个目录：**页键 → 绝对路径**。
    ///
    /// 四个能下载的类走 ResourceCatalog::folderForKind()（那是目录名的唯一出处，整合包
    /// 那一路也照着它写），原理图 / 存档 / 截图 / 日志不在下载页那几类里，目录名写在这儿。
    QHash<QString, QString> instanceFileDirs(const QString &versionId) const;

    /// 概览那排「常用目录」里**现在真有东西**的那几路（coding plan §5.3）。
    ///
    /// 判据是 §5.3 原话那两条：「目录存在」或「相关模组可识别」—— 逐条理由见 .cpp 里那段。
    /// 它和 `instanceFileDirs()` **不是一回事**：那一份回答"这个实例的每一路目录在哪儿"
    /// （形状是九条，有断言在数），这一份回答"这十三条里哪几条现在值得摆在外面"。
    ///
    /// @param dirs 灌给 `InstanceSettingsDialog::setFileDirs()` 的那一份（含 `game` /
    ///        `replay` / `crash` 三个额外键 —— 它们就在那儿被读的，这一层不自己再算一遍
    ///        路径，两个地方算同一个路径正是"两份值"的经典起手式）。
    QStringList instanceCommonFolderKeys(const QString &versionId,
                                         const QHash<QString, QString> &dirs) const;

    /// 切到下载页的 @p categoryKey 那一栏（2026-09-27）。
    ///
    /// 入口是「文件管理」里那四页顶上的「下载新模组 / 资源包 / 数据包 / 光影」—— 用户原话
    /// 「按了以后导向下载页对应类别」。做的两件事是**收掉当前那一页**（收页只有这一条路，
    /// 见 .cpp 里那段）和**把选项卡与左栏那一列摆到对应的那一栏上**。
    ///
    /// @param categoryKey `MainWindowUI::downloadCategoryKeys()` 里的一个键。认不出来时
    ///        什么都不做，但会 `qWarning()` 说一句（症状是"按了没反应"，静默的话没人查得到）。
    /// 切到下载页的某一栏（实例设置那几个文件页顶上那颗「下载新模组」走的就是它）。
    ///
    /// @param returnInstanceId 非空 = 这一次是**从那个实例的设置页进来的**：左栏顶上那颗
    ///        「← 返回实例设置」会露出来，并且把「已安装版本」选到那个实例上、把两个筛子
    ///        （游戏版本 / 模组加载器）灌成那个实例的（用户 2026-09-28 那三条：「左上角
    ///        保留一个返回键，返回到实例设置里面」/「自动放进当前实例文件夹」/「自动在
    ///        搜索框里填入对应的版本和加载器」）。别的入口一律传空。
    ///
    ///        ★ 回来的那一页**就是 `categoryKey`**：「下载新模组」那一栏就是实例设置里
    ///          那一页（见 `InstanceSettingsDialog::filesDownloadCategoryKey()` ——
    ///          "这两处是一回事"的唯一出处就在它那儿），所以"来路那一页"不用再要一个参数。
    void openDownloadCategory(const QString &categoryKey, const QString &returnInstanceId = QString());
    /// `openDownloadCategory()` 的第二步：**确认那一页收干净了**再切选项卡、挑那一栏、灌
    /// 筛子。收不干净就什么都不做（理由写在 .cpp 里）。
    /// @param roundsLeft 还允许再排几轮（收页要跨事件循环轮次才落定，见 .cpp）。
    void continueOpenDownloadCategory(int row, const QString &categoryKey,
                                      const QString &returnInstanceId, int roundsLeft);
    /// 把某一页那两个筛子灌成**某个实例的**：版本 = 它的游戏版本，加载器 = 它的加载器。
    /// 用户的「自动在搜索框里填入对应的版本和加载器」就是这一条；调它的是上面那个函数。
    void fillDownloadFiltersFromInstance(ResourceCatalog::Kind kind, const QString &instanceId);
    /// 回到刚才那个实例的设置页（左栏顶上那颗「返回实例设置」点的就是它），并把那颗收起来。
    void returnToInstanceSettingsFromDownload();
    /// 顶上那四颗导航里的一颗被点了（`TitleBar::navRequested`）。**只管一件 MainWindowUI
    /// 管不着的事**：点的是当前这一颗、而且现在是「下载」页时，把那一页**刷回它的首页**
    /// （用户原话「点击顶部下载按钮就刷新页面回到主页」）。翻页和"把覆盖层收起来"那两条
    /// 仍旧由 MainWindowUI 自己接的那一条负责（那边先接，所以它先跑）。
    void onNavRequested(int index);

    // ---------- 「配置文件」那一页的编辑器（2026-09-27）----------
    //
    // 三件都是"接的人碰盘、弹话"那一套（同上那一段的分工）。**为什么编辑器那一格在
    // 对话框里、而读和写在这儿**：那一层手上没有 OverlayHost（读失败、写失败都得弹一句），
    // 也没有盘（ui/ 不 include src/，AppPaths 够不着）。而"切格子"是纯显示层的事，留在
    // 对话框自己那边（见 InstanceSettingsDialog::openConfigEditor()）。
    //
    // NOTE: 三件的 `dialog` 都是**栈上的那个局部对象**（openInstanceSettings() 里那一句
    //       `InstanceSettingsDialog dialog(...)`）—— 和上面那几条 lambda 捕获的是同一个，
    //       它活到 execPage 返回为止。

    /// 用户点开了 config/ 底下的一条文件：读进来、解码，然后摆进编辑器那一格。
    ///
    /// NOTE: **解码不等于"看得懂"**（见 ConfigFileText 那一段：这一页压根不解析格式）——
    ///       它只做三件事：认 BOM、认行尾、认这一份是不是 UTF-8。前两样要原样存着，改完
    ///       写回去时一个字都不能变。
    void openConfigFile(InstanceSettingsDialog *dialog, const QString &path);

    /// 用户点了「保存」：把 @p bytes 写到 @p path 上，成功了再告诉那一格。
    ///
    /// NOTE: 走 `QSaveFile`（先写同目录下一个临时文件、最后原子改名）—— 直接 `QFile`
    ///       写一半失败的话，用户手上的配置文件就毁了，而这一页**没有"恢复默认"**
    ///       （模组下次启动重写一份那要等它启动，也不保证每个模组都这么干）。
    ///
    /// ★ 写之前有三道闸（用户 2026-09-27 那份设计评审里的 S 级三条），顺序是硬的：
    ///   ① **游戏在跑这个实例 ⇒ 直接不写**（游戏手里有一份内存副本，退出时整份写回来，
    ///      用户改的东西会被静默盖掉，看起来就像「我改了没用」）；
    ///   ② **盘上那份"还是我打开时那一份"吗** —— 对不上就问一句（覆盖 / 读盘上那份 / 什么都
    ///      不做），**不许静静盖掉**别人的改动；
    ///   ③ **写之前把盘上那一份原样留一份**（世代备份）—— 留不下就不写（见下面那段
    ///      "为什么备份失败要挡住这一次保存"）。
    void saveConfigFile(InstanceSettingsDialog *dialog, const QString &path, const QByteArray &bytes);

    /// 用户点了「还原上一次」：把最近那一份备份读出来，摆回编辑器那一格。
    ///
    /// NOTE: **只摆进框里、不写盘**（见 ConfigFileEditorPage::restoreContent()）——「还原」
    ///       不是一次悄悄的写入，用户按「保存」才算数。所以这一趟只读备份，一个字节都不动
    ///       盘上那一份。
    void restoreConfigFile(InstanceSettingsDialog *dialog, const QString &path);

    /// 这个文件属于哪个实例：**从路径上认**（`<数据目录>/versions/<id>/…` ⇒ `<id>`）。
    ///
    /// NOTE: 不从对话框手里问"现在开着的是哪个实例" —— 那条路隔着一次改名（改完名
    ///       `versions/<老id>` 整个不见了，而这一格手上那份文件还在老路径上）。路径自己
    ///       带着答案，问它就够了。认不出（不在任何 versions/<id>/ 底下）= 空串。
    QString instanceIdOfPath(const QString &path) const;

    /// 这个文件的**最近那一份备份**的绝对路径（没有 = 空串）。
    ///
    /// NOTE: 「有没有备份」和「取哪一份」是**同一个问题**（备份的文件夹名按字典序排就是按
    ///       时间排，见 ConfigFileText::backupFolderName()）—— 所以只有这一个函数，那一格
    ///       那颗按钮画不画也是问它。
    QString latestConfigBackup(const QString &instanceId, const QString &path) const;

    /// 这个实例的备份只留最近 @p keep 份，更老的删掉。
    ///
    /// NOTE: 这是全仓**唯一一处会主动删东西的缓存目录**（见 AppPaths::configBackupDir()）——
    ///       所以只认我们自己生成的那个文件夹名（ConfigFileText::backupFoldersToRemove()
    ///       里那道闸），用户自己放进去的东西一个都不碰。
    void pruneConfigBackups(const QString &instanceId, int keep);

    /// 用户想从编辑器退回文件列表（点了「返回」，或者按了 Esc）。
    ///
    /// ★ **改过的东西不许悄悄没掉**：改过就弹一句问，用户说"丢掉"才真回去。这一问在这儿
    ///   而不是在编辑器里，理由是那一层没有 OverlayHost（见 InstanceSettingsDialog::
    ///   closeConfigEditor() 那条 NOTE）。
    void leaveConfigEditor(InstanceSettingsDialog *dialog);

    /// 弹那句「改过还没保存，要丢掉吗」。
    ///
    /// @return true = 用户选了"丢掉"。
    ///
    /// NOTE: **两个调用点共用这一份字**（`leaveConfigEditor()` 退到列表、以及顶上那颗返回
    ///       箭头要关整页那一趟）—— 同一件事在两处问出两套说法，用户会以为是两件事。
    /// NOTE: 默认那一颗是**「留在这儿」**（`defaultYes = false`）：手一滑按到 Esc、或者点
    ///       到卡片外面，不该把用户刚改的东西丢掉。
    bool confirmDiscardConfigEdits();

    // ---------- 「世界设置」（存档那支笔点开的那一页，2026-09-28）----------
    //
    // 用户原话：「世界根据其存档文件在存档设置（那支笔）里面编辑游戏规则等，打开之前弹出
    // 坏档警告并提供备份按钮」。三步的走法和"每一步都重读一遍盘"的理由见 .cpp 里那一大段。
    //
    // ★ 2026-09-29 起**点笔不再问那一句**（用户原话：「点击笔的时候不要弹备份提示了」）：
    //   原来那个 `openWorldSettings()` 壳（先读一眼 → 弹「打开 / 先备份一份 / 取消」）整个
    //   删了，那支笔直接落到下面这一个函数上。备份仍有两处：页里那颗「备份一份」，以及展开
    //   「修改存档文件」那个折叠框时那一句警告。

    /// 那支笔上的「世界设置…」：读盘 → 翻译 → 摆上去。**坏档也在这里判**（读不出来就摆成
    /// "读不出来"那一档，顶上写「这个世界可能坏了：<目录>」、正文整块收着）。
    ///
    /// @param worldDir **存档目录的绝对路径**（那支笔发出来的就是它）。
    void showWorldSettingsPage(InstanceSettingsDialog *dialog, const QString &worldDir);

    /// 用户点了「返回」：改过就弹一句问，答"丢掉"才切回世界列表。
    void leaveWorldSettings(InstanceSettingsDialog *dialog);

    /// 用户点了「保存」。四道闸 + 写回，顺序见 .cpp。
    void saveWorldSettings(InstanceSettingsDialog *dialog);

    /// 把 `<worldDir>/level.dat` **原样抄一份**到世代备份里（并清理超出的代数）。
    ///
    /// @param problemOut 没留成时的原因（给人看的一句话；成功时清空）。
    /// @return 留成了没有。
    ///
    /// ★ 抄的是**盘上那一份的字节**，不是"我们解出来又压回去的那一份"：备份是原件的逐字节
    ///   复制，"还原"才有确定的意思（也绕开了 gzip 头里那个 mtime 对不上的事）。
    bool backupWorldLevelData(const QString &worldDir, QString *problemOut);

    /// 这个世界的备份里**最近的那一份**（`level.dat` 的绝对路径）；一份都没有返回空串。
    QString latestWorldBackup(const QString &worldDir) const;

    /// 弹那句「改过还没保存，要丢掉吗」（**世界设置那一页的版本**）。
    ///
    /// @return true = 用户选了"丢掉"。
    ///
    /// NOTE: 和 `confirmDiscardConfigEdits()` 长得一样但是**两份**，不合并：两句话说的是
    ///       两样东西（那个是"文件"，这个是"世界"），而且默认那颗都是"别丢"——合并省下的
    ///       那两行字，代价是以后改一处话术要想起另一处。
    bool confirmDiscardWorldEdits();

    /// 这个实例的 mods/ 里有没有"名字里带 @p nameToken 这个词"的 **.jar**（忽略大小写）。
    ///
    /// NOTE: 判据是**文件名里的词**，不是"这个模组是谁"—— 我们没有模组元数据（那要读
    ///       jar 里的 fabric.mod.json）。够用：Iris 的 jar 叫 `iris-<版本>.jar`，
    ///       Litematica 的叫 `litematica-<加载器>-<版本>.jar`。被禁用（`.jar.disabled`）
    ///       的不算 —— 那正是"禁用"的意思。
    ///
    /// ★ 2026-09-27 从"前缀"改成"词"：jar 是可以随便改名的，而**中文整合包和 PCL 真的
    ///   会改** —— 用户盘上就有 `[投影] litematica-fabric-26.2-0.28.7.jar` 和
    ///   `[虹膜] iris-fabric-1.11.4+mc26.2.jar`（前面挂一个中文名是那儿的常态）。按前缀
    ///   认的话这两样都算"没装"，而它们明明装着 —— 那两页于是谎报"用不上"，用户
    ///   2026-09-27 报的「原理图和光影包没正常显示」就是这个。切词规则见 .cpp 里
    ///   `fileNameHasToken()`。
    bool instanceHasMod(const QString &versionId, const QString &nameToken) const;

    /// 这个实例能不能用光影包：mods/ 里装了 **Iris / Oculus / OptiFine**（都是"加载
    /// shaderpacks/ 的东西"），或者这个实例装的加载器**就是 OptiFine**（它自己认
    /// shaderpacks/）。判据由这一层算好递给对话框 —— ui/ 够不着 wtfml.meta 和 mods/ 目录。
    ///
    /// NOTE: Oculus 是 Forge 上那个 Iris 移植（用户盘上「落幕曲」「香草纪元」
    ///       「魔改版ATT2」三个包用的都是它），而 OptiFine 在 1.12.2 及以前是**当普通
    ///       模组装进 mods/ 下的（`OptiFine_1.12.2_HD_U_G5.jar`）—— 这两样原来一条都不认，
    ///       于是那三页在装了光影的实例上照样说"用不上"（2026-09-27 修）。
    bool instanceShaderUsable(const QString &versionId) const;

    /// 这个实例用不用得上「模组」那一页 —— 两条路任一条命中就算用得上：
    /// ① 它基于一个模组加载器（Fabric / Forge / NeoForge / OptiFine / Quilt）；
    /// ② `mods/` 里**已经有 jar**。
    ///
    /// ★ 用户 2026-09-27 报的就是①：「模组存在误判的情况，像海波篮子连 litematica 都有了
    ///   怎么会没有模组加载器呢」。根子是判据原来写的 `displayParts().loaderName` 非空，
    ///   而那一格**只有我们自己的安装器会写** —— PCL 那类"整份 json 合进实例目录"的实例
    ///   （用户盘上 70 个里 67 个）读出来是空的，于是那一页在**每一个**这样的实例上都灰着。
    ///   现在走 VersionManager::instanceModLoader()，那个函数的两条路见它的说明。
    ///
    /// ② 和 ① 是**两件事**，所以它挂在这一层、不塞进 instanceModLoader()：那边回答的是
    /// "基于哪个加载器"，而往 mods/ 里丢一个 jar 不构成任何加载器证据。"这一页对他有没有
    /// 用"才是这里的问题 —— 真机上就有两个实例走了这一条（json 认不出加载器、可 mods/ 里
    /// 躺着 136 / 27 个 jar，那一页显然是该给的）。
    bool instanceModsUsable(const QString &versionId) const;

    /// 读一次 servers.dat 递给对话框（解析在 src/util/NbtUtil 里）。
    ///
    /// NOTE: 「读不动」和「读到了但一条都没有」分开给（见 setServerList()）——
    ///       前者要说清为什么，后者才能说"你还没加过服务器"。
    void loadServerList(InstanceSettingsDialog *dialog, const QString &versionId) const;

    /// 覆盖层那一层现在空着吗（"一层只放得下一页"那条规矩的**只读**版本）。
    ///
    /// NOTE: 从前这个函数在"占着"的时候**自己弹一句**「先关掉现在这一页 / 这一条提示」。
    ///       2026-09-29 之后不弹了 —— 用户原话：「如果页面被递归开启等任何会提示 先关闭当前
    ///       页面再继续 的情况都直接静默关闭那个页面并直接继续」。三个入口改成问
    ///       `runWhenOverlayFree()`（那一条自己会先把占着的那一层收掉，收不掉才出声）。
    bool overlayAcceptsNewPage() const;

    /// 这一层**空着**的时候，`open` 当场就跑（绝大多数情况）；被占着的时候，先把占着的那
    /// 一层**静默收掉**，再把 `open` 排到下一轮去。
    ///
    /// 用户 2026-09-29 原话：「如果页面被递归开启等任何会提示 先关闭当前页面再继续 的情况
    /// 都直接静默关闭那个页面并直接继续」。
    ///
    /// ★ 收页那一发（`OverlayHost::evict()`）只是**请求**：用 `execPage()` 开出来的那一页
    ///   正卡在自己的嵌套事件循环里，要等这一趟调用栈整个退出去它才醒得过来 ⇒ 所以
    ///   `open` **必须**排到下一轮（这条和"重下=校验跳过"那种判断不一样，它是个时序事实）。
    /// ★ 排的时候**再从这一层的头上过一遍**（递归的自己）：那一轮里可能又有别的东西把它
    ///   占上了。
    ///
    /// @param open 真要开的那一整段（**不含**自己那一道判据 —— 让位这件事这儿已经做完了）。
    /// @param roundsLeft 还能再排几轮（`kOverlayOpenRounds`）。**不写"转到收干净为止"**：
    ///        收不掉的时候再转多少轮也收不掉，转下去只是空转（同 `kDownloadOpenRounds`）。
    void runWhenOverlayFree(const std::function<void()> &open,
                            int roundsLeft = kOverlayOpenRounds);

    /// 收不掉的时候那条兜底：老的两句话（一条给"占着的是提示"、一条给"占着的是整页"）。
    ///
    /// NOTE: **正常情况走不到这儿** —— `evict()` 收不掉只有一种可能：那一层正卡在自己的
    ///       嵌套事件循环里等着、这一发不该落在它头上。真有那么一天，也得说句人话
    ///       （"点了没反应"是这一整条路线本来要治的那个毛病）。
    void warnOverlayBusy();

    /// `runWhenOverlayFree()` 让开之后真正跑的那一半（原函数体，一个字没动）。
    /// NOTE: 分成两个函数是为了让"让位"那一段只有一处（`runWhenOverlayFree`），而各个入口
    ///       自己那几百行一行都不用重新缩进。
    void openInstanceSettingsNow(const QString &versionId, const QString &pageKey);
    void openJavaManageNow();
    void openGroupDialogNow();

    /// 这个实例的服务器列表（名字 + 地址，按文件里的顺序）。servers.dat 读不动时是空的。
    ///
    /// NOTE: 和 serverListProblem() 分两个函数是因为**它们读的是同一份文件**，而调用方
    ///       要的东西不一样：setServerList() 两样都要（列表 + 读不动的那句话），
    ///       而 pingServers() 只要地址那一半。合起来收一个"结果结构"也不难，只是这一层
    ///       的文件读得很便宜（几 KB），宁可写成两个一眼看得出在干什么的函数。
    QList<QPair<QString, QString>> serverListOf(const QString &versionId) const;
    /// 上面那份列表读不动的原因（空串 = 读到了）。见 loadServerList() 那条 NOTE。
    QString serverListProblem(const QString &versionId) const;

    /// 问一遍每台服务器现在什么样，把结果灌回那一页（2026-09-27，见 .cpp 里那段）。
    ///
    /// ★ 2026-09-27 起它**不再自己等**：整趟交给后台线程上的 `ServerPing::Pinger`，一台
    ///   问完回来一台（用户原话：「现在如果没测完会锁住其他按钮，不能这样」）。所以这一发
    ///   立刻返回，界面在这几秒里照常能用 —— 「测速」那颗按钮自己显示「测速中…」，
    ///   别的都不禁用。
    ///
    /// NOTE: 重复调用是**安全的**：已经有一趟在跑、而且**地址表一模一样**时，它只把界面
    ///       接回来（换到新开的那一页、把已经攒下的结果摆上去），不重开一趟。
    /// ★★ 2026-09-27：地址表**不一样**时不能这么接 —— 结果是按下标摆的，接回来就是
    ///       张冠李戴（B 那一页的第 3 行显示 A 的第 3 台）。那一路改成"记下欠着的活、
    ///       等这一趟收摊再开"，见 .cpp 里那段和 onServerPingAllFinished。
    void pingServers(InstanceSettingsDialog *dialog, const QString &versionId);

    /// 照一份地址表**真开一趟**（读缓存垫底、把表摆上、丢给 Pinger）。前提：没有别的趟
    /// 在跑（调用方判 `m_serverPing.active`）。
    ///
    /// NOTE: 「这份地址表现在该显示成什么样」那两下（读缓存 → 一行行摆法）**不是成员**：
    ///       它们只用 ServerStatusCache，而 `InstanceSettingsDialog::ServerRowStatus` 是
    ///       嵌套类型、这个头只前向声明了那个类（要在这儿写出返回类型就得把 ui/ 的头拉进来）。
    ///       见 .cpp 里那对文件内的函数。
    void beginServerPingRun(InstanceSettingsDialog *dialog, const QString &versionId,
                            const QStringList &addresses);

    /// 把 `m_serverPing` 现在攒下的东西摆到那一页上。**没开着就什么都不做**。
    void pushServerPingRows();

    /// 启用 / 禁用一条：把文件名改成 `<原名>.disabled`，或者反过来。
    /// @return **动过盘没有** —— 真了调用点才需要让那一页重列（没动过还重列会让用户
    ///         以为"刚才那一下生效了"）。
    ///
    /// ★ 2026-10-09 多一个出参，为的是**批量**那一颗按钮（见 toggleInstanceFiles()）：
    ///   @p problem 非空 = "别弹框，把不成的缘故写在这儿"，调用方自己决定怎么合起来说。
    ///   不给的话行为一个字不变（还是各行弹各行的框）。
    ///   理由：一排二十条里十九条被游戏占着的话，二十个框是要点二十次的。
    bool toggleInstanceFile(const QString &path, QString *problem = nullptr);

    /// 删掉一条（**先问一句**；文件夹连同里面的东西一起删）。@return 同 toggleInstanceFile()。
    bool deleteInstanceFile(const QString &path);

    /// 删掉一条**不问**那一句 —— `deleteInstanceFile()` 问完调的就是它，批量删除
    /// （deleteInstanceFiles()）也是（那一批**只问一次**）。@return 动过盘没有。
    /// @param problem 同 `toggleInstanceFile()` 那个出参（非空 = 别弹框，把缘故写在这儿）。
    bool removeInstanceFileUnasked(const QString &path, QString *problem = nullptr);

    /// 把「文件管理」那几页里那些**资源文件**的图标要回来（2026-09-27）。
    ///
    /// ★ 用户原话：「模组和资源包等都有一个图标，把图标显示出来」。盘上那个文件名**不是
    ///   身份**（用户改过名、各家起名不一样），所以"这是哪个资源"只能按**文件内容**反查
    ///   （sha1 → Modrinth、指纹 → CurseForge，见 `InstanceIconLookup`）。那几发是网络，
    ///   而算哈希要读一遍整个文件 —— 两样都**不能在界面线程上做**（用户对"锁住其他按钮"
    ///   的意见见 pingServers 那段）。
    ///
    /// 所以这一发只做三件事：列一遍候选文件、把**已经知道的**先摆上去、需要问的丢给后台
    /// 线程。重复调用是安全的：已经有一趟在跑时不重开（只把界面接回新开的那一页）。
    void requestInstanceFileIcons(InstanceSettingsDialog *dialog, const QString &versionId);

    /// ★ 2026-09-27 拆出来的那一段：照**现在这一页**（`m_instanceIcons.paths`）里还没问过
    /// 的那些开一趟后台。`requestInstanceFileIcons()` 和"上一趟收尾时发现用户已经换了页"
    /// （见 onInstanceIconLookupFinished 末尾）两处都走它。
    ///
    /// 不这么拆的话，"A 的线程还在跑、用户又开了 B 的设置页"这一形态下 B 会被**静默丢下**：
    /// 那次请求撞在 `active` 闸上直接 return 了，而 A 跑完时谁也不会回头替 B 问 —— B 的
    /// 文件要等用户再进一次这一页才有图标。
    void startInstanceFileIconLookup();

    /// 后台那一趟回来了。见 .cpp 里那一段。
    void onInstanceIconLookupFinished(const InstanceIconLookup::Result &result);

    /// 把**现在知道的**那些图标摆到正开着的那一页上（没开着就什么都不做）。
    /// 「图标到手」和「新开了一页」两条路都走它 —— 两处各摆一遍的话，两边的判据迟早漂开。
    ///
    /// ★ 2026-09-27 起它同时摆"这一条的名字点不点得动"（见 setFileResource()）：两件事
    ///   读的是同一份会话缓存、同一个循环、同一个"这一条现在算不算知道"的判据 ——
    ///   拆成两个函数摆，迟早出现"图标有了、名字点不动"这种两边打架的界面。
    void pushInstanceFileIcons();

    /// 这一条文件的**缓存钥匙**：路径 + 大小 + 修改时间。
    ///
    /// NOTE: 为什么要带上后两样（而不是只按路径）：路径复用了、内容换了的形态是**真的
    ///       会发生的**（删掉一个模组、换上另一个同名文件），只按路径键住的话，那一格会
    ///       一直画着**上一个**资源的图标 —— 不报错、看着也像张正经图标，只有用户认得出来。
    static QString instanceIconCacheKey(const QString &path);

    /// 这个实例里那些**可能查得到图标**的文件（绝对路径）：四个能下载的目录 + 原理图，
    /// 只要 `.jar` / `.zip`（判据是 `InstanceIconLookup::isLookupCandidate()`）。
    QStringList instanceIconCandidatePaths(const QString &versionId) const;

    /// 实例设置里**点一条文件的名字**（2026-09-27，用户原话见 .cpp 里那段）⇒ 开那个资源
    /// 自己的那一页。
    ///
    /// @param versionId 这一下是从**哪个实例**的设置页里发出来的（那一页正看着的实例的
    ///        目录名）。它只有一个用处：那一页关掉之后把**实例设置放回去**（见
    ///        `openFileResourcePage()`）—— 用户原话「点击下载界面返回后应该退回到实例设置
    ///        而不是实例列表」。
    /// @param pageKey 发起这一下的那一页的页键（模组 / 资源包 / 数据包 / 光影 / …）。
    ///        从它认"是哪一类资源"（唯一出处仍是 `filesDownloadCategoryKey()`）。
    /// @param path 那一行对应的文件（绝对路径）。
    ///
    /// 走的是**下载页点一行**那条路（`openResourceDetailPage()`）：同一份 `m_resourceHits`
    /// 里那条 Hit，同一页、同一套 chip。
    /// 没匹配上的（没认出来是哪个资源的）什么都不做 —— 那一行的名字本来也没变成链接。
    ///
    /// NOTE: 收实例设置那一页 + 排到下一轮事件循环的理由写在 .cpp 里（和
    ///       `openDownloadCategory()` 逐字同一套）。
    void openInstanceFileResource(const QString &versionId, const QString &pageKey,
                                  const QString &path);
    /// 开那一页，并在用户**按返回/Esc 收掉它**之后把实例设置放回去（见
    /// `openInstanceFileResource()` 里那段）。两处调用（覆盖层还开着 / 已经关了）都走它，
    /// "回来之后干什么"因此只有一处出处。
    void openFileResourcePage(const QString &versionId, const QString &iconKey,
                              const QString &kindKey);
    /// 「导入整合包…」：选文件 → 读清单 → 问名字 → 换地址 → 确认 → 交给 applyModpackImport()。
    ///
    /// 读的是压缩包根目录里那一份清单（ZipUtil::readEntry，**不整包解开**：用户随手选一个
    /// 几百 MB 的包，为了知道它不是整合包先解压几分钟是白等）。**两种格式各读一个名字**：
    /// Modrinth 是 `modrinth.index.json`，CurseForge 是 `manifest.json`。
    void onImportModpackClicked();
    /// 导入的**执行部分**：把清单解析出来、该拦的拦掉、算出安装器要的 Java，然后开始导入。
    /// @param zipPath 用户选的那个整合包文件。
    /// @param instanceName 落地的实例名（= `versions/<id>` 那个目录名）。
    /// @param manifestJson 压缩包里那份清单的**原始字节**（哪个清单看 format）。
    ///        传字节而不是传解析结果，是为了让这一半能被验证工程驱动（自定义类型过不了
    ///        QMetaObject::invokeMethod）；里面会再解析一遍 —— 纯函数，94 KB 的清单也就
    ///        一瞬，换来的是"确认框里说的那个包"和"真的去装的那个包"是同一串字节。
    /// @param format 这份字节是哪个清单。**CurseForge 那一档在这里还要再换一次地址**
    ///        （那一发的详情见 VersionManager::resolveCurseForgeFiles）—— 于是导入一个
    ///        CF 包会问两次接口，换来的还是上面那句"同一串字节"。
    /// @return 真的开始导入了返回 true（失败/被拦下时弹窗并返回 false）。
    ///
    /// NOTE: 拆成两步的理由和 applyRename() 一样 —— 这一半里藏着真会错的东西（重名要拦、
    ///       不支持的加载器要拦、忙态几时置上、结束之后列表要刷）。
    ///       （这里原来还写着"QFileDialog / QInputDialog 都是模态的、驱动不了"——问名字那
    ///       一格 2026-09-25 已经换成能驱动的内嵌提示了；**QFileDialog 那一步还是驱动不了的**
    ///       ：它开的是 Windows 自己的文件选择框，不是我们的窗，见那个调用点上面那段。）
    bool applyModpackImport(const QString &zipPath, const QString &instanceName,
                            const QByteArray &manifestJson, ModpackImport::Format format);
    /// 用户在左边那张列表里换了「游戏文件夹」。真正干活的是 FolderManager 发出的
    /// currentFolderChanged()，这里只负责把选中项翻译成一次 setCurrentFolder()。
    void onFolderSelectionChanged();
    /// 某一份会话吐了一行 stdout。@param sessionId 哪一份（0 = 连会话都没建起来，
    /// 那种消息是"又开一个被拦下了"这类，不挂在任何实例名下）。
    void onProcessOutput(int sessionId, const QString &text);
    void onProcessError(int sessionId, const QString &text);
    void onGameFinished(int sessionId, int exitCode, QProcess::ExitStatus exitStatus);
    /// 停掉**一份**会话（**现在只有底栏左面板里那行电源走它**）：先问、再停 ——
    /// @param sessionId 那一行是谁（面板弹出来时抄下来的会话号，见 PanelRow::key）。
    ///
    /// NOTE: 2026-09-25 之前这里还有一对 onStopGameClicked() / applyStopGame()，走的是
    ///       "停掉当前选中那个实例的**全部**会话"（实例页那颗「停止游戏…」）。那颗按钮被
    ///       用户删了，那对函数就只剩它俩互相调用，一起删了 —— 所以"一次全停"这个能力
    ///       现在产品里没有入口，多开的几份要一行一行停。别照着旧记性找它们。
    /// NOTE: 会话号可能在"面板弹出来到现在"这几秒里已经结束了 —— 那种情况**不弹窗**，
    ///       只写一行日志（用户没做错什么，弹一个框只会让他以为出事了）。
    void stopGameSession(int sessionId);
    /// 「关掉这个正在跑的游戏」那个人确认过了没有（默认那一边是"别停"）。
    /// @param displayName 实例显示名。
    /// @param sessions 这一下会停掉**几个**游戏进程（现在恒为 1 —— 唯一那个调用者
    ///        stopGameSession() 只停一行；留着这个参数是因为"停几份"问话里要说清）。
    ///
    /// 那句"游戏不会存档"是**产品级约定**（见 .cpp 里那段）—— 这个框就是为了让它只有
    /// 一份出处：两处各写一句迟早会漏掉一处。
    bool confirmStopGame(const QString &displayName, int sessions);
    // ---------- 下载页「游戏」那两层：版本树 → 安装页 ----------
    //
    // 用户原话见 MainWindowUI::downloadGameStackName() 上面那段。这一层只做"界面上的
    // 走动 + 把用户选的东西翻译成一次请求"，真正干活的是 VersionManager 那三个
    // installNamed*()（它们自己会把原版补齐、写薄 json、发**唯一**那个终态信号）。
    /// 点树里一条**版本号**（子条目）→ 进安装页。顶层那四条是分类，点了不做任何事
    /// （展开/折叠交给 Qt 自己）—— 那条判据在 setupConnections() 里那个 lambda 上
    /// （那边 QTreeWidgetItem 是完整类型）。
    /// 进安装页：@param gameVersion 树里点中的那个原版号（这一份要基于它装）。
    /// 也**允许验证工程直接调**（参数只有字符串，QMetaObject::invokeMethod 认得）。
    void openInstallPage(const QString &gameVersion);
    /// 「← 返回版本列表」：回树那一层。**什么都不清** —— 树里那份展开状态、名称框里
    /// 敲过的字都留着（用户按返回是想再看一眼别的版本，不是想放弃刚才那些输入）。
    void backToVersionTree();
    /// 名称框里每敲一个字：记下"用户动过这个名字了"，再重算那句小字（重名提示要"实时"）。
    void onInstallNameEdited();
    /// 点了「安装」。名字校验、重名那一问、Java 挑选、忙态，都在这里收口。
    void onInstallButtonClicked();
    /// requestLoaderVersions() 的回复：填进**那一条**底下的子条目。
    ///
    /// 参数收的是**下标**（不是 VersionManager::LoaderKind）：这是给 moc 看的槽，
    /// 签名里出现别处的枚举就得把 VersionManager.hpp 整个拖进这个头（再说枚举还得
    /// 先 qRegisterMetaType）。枚举→下标那次翻译在 setupConnections() 那个 lambda 里做
    /// （VersionManager::loaderIndexForKind），一处收口，和"下标→枚举"同一个方向表。
    void onLoaderVersionsFetched(const QString &gameVersion, int loaderIndex, bool success,
                                 const LoaderMeta::LoaderVersionList &versions,
                                 const QString &errorMessage);

    void onVersionsFetched(const QStringList &versions);
    void onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson);
    void onVersionDownloadStats(int done, int total, int failed, const QString &currentFile);
    void onVersionDownloadFinished(const QString &versionId, bool success,
                                   const QString &errorMessage);
    /// 「导入整合包…」那个按钮：只是**选文件**，选完交给 importModpackFromFile()。
    /// 拆成两个函数是因为下载页「整合包」那一页也要走同一条路（那边是"下回来一个 .mrpack"，
    /// 路径是从下载结果来的，没有文件对话框那一步）—— 整条导入流程只有一份。
    ///
    /// @param namePrefill 问名字那一格的**预填值**。空串 = 按包名推一个
    ///        （`ModpackImport::suggestInstanceName(pack.name)`，也就是那颗按钮那一档）。
    ///        用户 2026-09-29 那条"拖进来一个整合包"要的是**文件名**
    ///        （「这个自动预填文件名」）—— 拖进来的东西用户刚在资源管理器里看过，
    ///        文件名是他认得的那一串，包名反而未必。
    void importModpackFromFile(const QString &zipPath, const QString &namePrefill = QString());

    // ---------- 拖进来的文件（2026-09-29）----------
    //
    // 用户原话：「打开某个实例设置的时候如果拖入.jar/.zip之类的文件，检测是整合包还是资源包等，
    // 如果是模组且正好在模组设置那一页，那就直接塞进去，不是对应页面就弹窗提示：将在此实例中
    // 安装 资源包/...，是否继续；如果是整合包，则让用户选择安装的文件夹及实例名（这个自动预填
    // 文件名）并离开实例设置（相当于点了返回）。在一些其它的地方拖入模组等时选择要安装在的
    // 目标实例」。
    //
    // 切成三条：**认类型**在 `DroppedResource`（纯判断）、**复制进目录**在
    // `installLocalResourceFile()`、**问什么**在这两个 handle* 里。两条入口各一个
    // （实例设置那一页开着 / 没开着），是因为"装到哪去"这个问题只有前者不用问。

    /// 实例设置那一页上报"落了这几个文件"（`InstanceSettingsDialog::filesDropped`）。
    /// @param page 那一页（要用它读 `currentPageKey()`、装完让它刷一次文件页）。
    /// @param versionId 这一页说的是哪个实例（目录名）。
    void handleInstanceSettingsDrop(QWidget *page, const QString &versionId,
                                    const QStringList &paths);

    /// 拖到**主窗口**上的（没有哪一页开着）：装到哪个实例里要问用户。
    void handleWindowDrop(const QStringList &paths);

    /// 把一个本地文件装进 @p versionId 这个实例的 @p kind 那一类目录里（没有目录就建）。
    ///
    /// 目标里已经有同名文件时会**问一句要不要盖掉**（`kind` 那一类的说法写在那句话里）。
    /// @param problemOut 失败时说人话的原因；成功时清空。
    /// @return 装上去了没有（用户答"不盖"也算 false —— 那一次没装）。
    bool installLocalResourceFile(const QString &versionId, ResourceCatalog::Kind kind,
                                  const QString &path, QString *problemOut);

    /// 拖进来一个整合包：**先问游戏文件夹**、再问实例名（预填 @p namePrefill），
    /// 然后走既有的导入流程（`importModpackFromFile`）。
    ///
    /// ★ 问游戏文件夹那一问是用户点名要的（「让用户选择安装的文件夹及实例名」）——
    ///   手法和「复制实例」那一处**逐字相同**（原生目录选择框 + 问名字），理由见那一处
    ///   那段：`ui/` 不碰 `AppPaths`，"该默认到哪儿"只有这一层知道。
    void importDroppedModpack(const QString &zipPath, const QString &namePrefill);
    /// 下载页那几类资源里挑中的那个版本下完了（见 downloadResourceFiles）。
    /// label 是「Sodium 0.6.13」这种"下的是什么"，界面上只写日志 + 弹窗，**不碰任何实例的
    /// 安装状态**（那是 onVersionDownloadFinished 的事，两者语义不同，见 VersionManager 里
    /// 那个信号的 NOTE）。
    void onResourceDownloadFinished(const QString &label, bool success,
                                    const QString &errorMessage);

    // ---------- 设置页「背景图」那两行（2026-09-28 加的） ----------
    //
    // 用户原话：「能给主页左侧自定义图像吗？其他页面的背景能换图吗」。动的是**主题规格**
    // （`ui/Theme.hpp`），不是 LauncherSettings —— 见 .cpp 里那一段开头写的"改内存里那份
    // → 落盘 → 立刻铺开"这三步。
    //
    // ★ 这五条**放在 `private slots:` 里**（它们本来只被控件上的信号驱动，做不做得成槽对产品
    //   毫无区别）：验证工程要能按名字把 `refreshThemeImageRows()` 调起来，量"用户挑完一张图
    //   之后这两行长什么样" —— 挑图那颗按钮底下是 QFileDialog，测试里点不了，所以那一条只能
    //   从这儿进去（mw_check 的 T29）。
    /// 「选择图片…」：问用户要一张图 → 拷进数据目录 → 记进规格 → 立刻生效。
    /// @param index 第几行（`MainWindowUI::getThemeImageRow()` 那张表的下标）。
    void onThemeImageChooseClicked(int index);
    /// 「清除」：把数据目录里那一份删掉，顺带把这一块的覆盖项整个撤掉。
    void onThemeImageClearClicked(int index);
    /// 摆法 / 不透明度：**立刻生效**（拖滑块时那块底跟着变，预览就是界面本身）。
    void onThemeImageFitChanged(int index);
    void onThemeImageOpacityChanged(int index);
    /// 把 `Theme::current()` 里那两块图的样子画到这两行控件上（含"现在铺的是哪一张"那句
    /// 话、以及没设图时把后面那几颗灰掉）。**灌值那一段和每次改完都要调它一次。**
    void refreshThemeImageRows();

protected:
    /// 只为一件事挂着：外框那两条 Win32 消息（WM_NCCALCSIZE / WM_NCHITTEST，见
    /// `ui/WindowChrome.hpp` 开头那两段）。接到手的是**这个窗口**的消息，Qt 已经替我们把
    /// HWND 对上号了，所以这里只是转手，没别的。
    ///
    /// NOTE: 这一条**必须在 MainWindow 上**（不能改挂到别的控件上）：Qt 只把消息交给顶层
    ///       部件 —— `QWidgetWindow::nativeEvent()` 那一句就是 `m_widget->nativeEvent(...)`，
    ///       而 `m_widget` 是那个顶层窗。
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

    /// 关窗口 = 退出启动器（全仓没有别的退出路径：没有托盘图标、没有 qApp->quit()）。
    ///
    /// 游戏还在跑的时候这里要拦一下，理由见 .cpp 里那段：MainWindow 是 main() 里的栈对象，
    /// 它一析构，链子上那个 QProcess 的析构就会把游戏 **kill()** 掉。
    void closeEvent(QCloseEvent *event) override;

    /**
     * @brief 有东西被拖到**窗口**上（2026-09-29）。
     *
     * 这一层接的是"没有哪一页开着"的那一档（实例设置开着时是**那一页**先接住，见
     * `InstanceSettingsDialog::dragEnterEvent()`）：拖进来的文件该装到**哪个实例**里，
     * 这一档没有上下文可推，只能问用户。
     *
     * NOTE: 判据（认不认这一发拖放、拖的是哪几个文件）只有一处 —— `DroppedResource` 判
     *       类型、`InstanceSettingsDialog::droppedFilePaths()` 判"是不是本地文件"。
     *       这一个大类不自己再写一遍，免得两处对"什么算一个能装的文件"给出不同答案。
     */
    void dragEnterEvent(QDragEnterEvent *event) override;

    /// @copydoc dragEnterEvent
    void dropEvent(QDropEvent *event) override;

    /// 只为一件事挂着：安装页那棵加载器树的视口。选中那条加载器右边那个「取消」小叉
    /// （`rowClearRole()`）是**行里的一个按钮**、不是"点这一行"，而 `itemClicked` 只给条目
    /// 不给"点在哪一格" —— 所以那一下得在这里判、在这里吃掉（返回 true），不然点叉会顺手
    /// 把那一列展开/收起（见 .cpp 里那段 NOTE）。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupConnections();
    void continueModpackExport(const ModpackExport::ExportPlan &plan, const QString &targetPath,
                               ModpackExport::Format format);

    // ---------- 「世界设置」那两格状态（2026-09-28）----------
    //
    // ★ 这两条**必须落在 `private:` 里**，跟上面那一堆"世界设置"的函数（它们在
    //   `private slots:` 那一段，因为 MainWindow 的槽都挤在那儿）分开：moc 只认小节里的
    //   **函数声明**，一条数据成员摆在 `slots:` 底下会当场报
    //   `Not a signal or slot declaration`（编译都过不去）。
    /// 世界设置那一格现在看的是哪一个存档目录（**空串 = 没开着**）。
    ///
    /// NOTE: 存这一份而不是每次去问对话框：那一页上"保存"那一发信号**不带任何值**
    ///       （见 WorldSettingsPage::saveRequested()），写哪个世界只有这里知道。
    QString m_worldDir;
    /// 打开世界设置那一刻盘上 `level.dat` 的大小 / 修改时间 ——"这中间别人动过没有"
    /// 那道闸比的就是这两样（和 `ConfigFileText::Stamp` 是同一件事，只是拆开存）。
    ///
    /// NOTE: 这里为什么**不直接存一个 `ConfigFileText::Stamp`**：那个类型在 `ui/` 底下，
    ///       而这个头文件一条 `ui/` 的 include 都没有（整个 `src/` 只在 .cpp 里往 `ui/`
    ///       看，见 CMakeLists 里那两行 include 目录）。为了一个"记着大小和时间"的小结构
    ///       破这个例不值得 —— `mtimeMs == 0` 就是 Stamp 那边 `valid() == false` 的意思
    ///       （文件在打开的那一刻就不在）。
    qint64 m_worldStampSize = 0;
    qint64 m_worldStampMtimeMs = 0;

    // ---------- 下载页后面那几页：搜索 + 三层资源表 ----------
    //
    // 整合包 / 模组 / 数据包 / 资源包 / 光影这几页走的是**同一条路**，只有"搜哪一类"不同，所以
    // 实现是一份、按 ResourceCatalog::Kind 分。那条路就是用户描述的那条：
    //   进页面 → 搜一次（空搜索词 = 这一类的热门）→ 摆第一层**资源**
    //   → 点开一个资源 = 摆第二层**游戏版本**（**不用再问接口**：搜索响应里本来就带着
    //     "这个资源支持哪些游戏版本"）
    //   → 点开一个游戏版本 = 这时才去问 → 摆第三层**版本号**
    //   → 点一个版本号 = 下它、装进实例
    //
    // NOTE: 这几个刻意**不是**槽：参数里带着 ResourceCatalog 那几种类型，声明成槽就得
    //       让 moc 生成一套元调用，而这条路全都用新式 connect（函数指针）连，不需要它。
    /// 进这一页时调：**只有第一次进才搜**（树上已经有东西就不动 —— 用户来回换分类
    /// 不该每次都重新问，那既慢、又是在拿限流额度换同一份东西）。
    void ensureResourcePageLoaded(ResourceCatalog::Kind kind);

    /// 下载页左栏某个分类条目右边那个刷新按钮被点了一下（用户原话："每一项的右边放一个
    /// 刷新按钮"）。`row` 是分类列表的行号（**和 downloadCategoryKeys() 同一个下标**）。
    ///
    /// 它办的事按分类分两种（见实现里的注释）：
    ///   · 「游戏」→ 重新拉一次**在线版本清单**（那棵版本树四条分类的内容就是它）；
    ///   · 其余几类 → 拿搜索框里现在那个词**再搜一次**（和按回车是同一件事）。
    /// 两种都**不动图标缓存**：重新摆出来的那批条目走的还是 ResourceStore::requestIcon，
    /// 命中内存/盘上那份就一个请求都不发（用户要的"图标留缓存"）。
    void refreshDownloadCategory(int row);
    /// 按那一页搜索框里的词搜一次（回车时，或者刚进页面时那句空的）。
    void startResourceSearch(ResourceCatalog::Kind kind);
    /// 树上"这一行被激活了"（鼠标单击、双击的第一下、键盘回车）—— 接线口见
    /// ui/RowActivation.hpp：一次手势只进来一次，所以这里**不需要**再考虑重入。
    void onResourceItemActivated(QTreeWidgetItem *item);
    /// 第三层那一下：把这个版本下回来、装进实例。
    void downloadResourceVersion(QTreeWidgetItem *item);
    /// 真正下它的那一段（上面那个树版本、和详情页那一行，两条路都走到这儿）。
    ///
    /// 树那一条把身份、游戏版本、清单都从条目上读出来再调它；详情页那一条直接给值
    /// （那一页没有树条目）。**加载器那道闸、「没有可下载的文件」那句话、整合包下完接导入
    /// 那笔登记**全在这一个函数里 —— 两条路必须是同一份规矩，抄一份迟早抄岔。
    ///
    /// @param kind 是哪一类资源（决定落到哪个目录、要不要过加载器那道闸）
    /// @param displayTitle 界面上这一个资源叫什么（下载 label 的前半截，另一半是版本号）
    /// @param version 要下的那一版（`primaryFile()` 是它的文件清单）
    /// @param source 这一条资源是**从哪一家搜出来的**（前置也去同一家问 —— 两家的项目号
    ///        不是一套）。★ 2026-09-28 加：原来不用它，因为这一条路上一个跨项目的请求都
    ///        不发；前置要问"那个号叫什么、有哪些版本"，来源就必须跟着走了。
    void installResourceVersion(ResourceCatalog::Kind kind, const QString &displayTitle,
                                const ResourceCatalog::VersionEntry &version,
                                ResourceCatalog::Source source);

    // ---------- 资源详情页（2026-09-26）----------
    //
    // 用户原话：「下载页选择了模组以后进入一个独立界面，上方写模组图像 名字 来源，然后有
    // 转到 modrinth/curseforge/mcmod.cn 的选项，中间一排版本，下方排列版本，和 pcl 类似」。
    // "独立界面"按项目那条总目标落成**整页覆盖层**（`OverlayHost::execPage`，不另开窗）。
    //
    // 界面那一层（ui/ResourceDetailPage）一个字节都不上网，进参出参全是值；下面这四个
    // 函数是它和业务层之间那层皮。
    void setupResourceDetailPage();
    /// 这一页**压根没开出来**（覆盖层正忙 / 那一类的键不认识 / 那把钥匙不在表里）。
    /// ★ `execPage()` 从不让这个数当结果（它只回 `QDialog::Accepted/Rejected`），所以
    ///   拿它跟 `QDialog::Rejected` 比是安全的 —— 调用方**只能靠它**区分"用户把这一页
    ///   收掉了"和"压根没开"，见 `openFileResourcePage()`。
    static constexpr int kResourcePageNotOpened = -1;
    /// 进下载页那条路最多允许**再排几轮**去把实例设置那一页收干净（见
    /// `continueOpenDownloadCategory()`）。3 是个宽裕的上限：正常情况第一轮就已经收干净了
    /// （那一发 `dismiss()` 的收尾在下一轮事件循环之前就落定），只有"上面还压着一条提示"
    /// 那一档才真用得上第二轮。**不写"转到收干净为止"**：收不掉时再转多少轮也收不掉
    /// （那一页收着 = `dismiss()` 不放行），转下去只是空转。
    static constexpr int kDownloadOpenRounds = 3;
    /// `runWhenOverlayFree()` 最多再排几轮（同一个量级、同一条理由 —— 见上面那段）。
    /// 3 是宽裕的上限：正常那一档第一轮就开出来了（本来就没人占着），被占着时第二轮也
    /// 一定见得着空的那一层；只有"上面还压着一条提示"才真用得上第三轮。
    static constexpr int kOverlayOpenRounds = 3;
    /// 这一页是**怎么打开**的（2026-09-27）。只有一处需要知道：右上角那一格摆什么
    /// —— 从实例的「文件管理」页点一个**模组**进来时摆那颗「模组设置」，
    /// 其余一律照旧摆「显示快照版」。
    ///
    /// ★★ 为什么单独立一个枚举而不是让调用方自己去 `setModSettingsSlot()`：那一页是
    ///    **同一个对象、建一次留着**的，两个入口共用它 ⇒ "摆什么"必须在**每次开页都显式
    ///    摆一次**，而摆的地方只能是这里（`execPage` 之前）。写成枚举参数的话，**下一个入口
    ///    一加进来就编译不过**，不会悄悄沿用上一次那一格。
    ///
    /// ★ 用户原话：「对于模组，点进去和资源包差不多，但是右上角显示快照版那里替换为模组
    ///   设置（先占位），而且和下载页有点区别，**不要不小心把下载页改了**」⇒ 下载页那条路
    ///   永远是 `Download`（照旧摆「显示快照版」），这一位不许跟着种类走。
    enum class DetailSource {
        Download,      ///< 下载页点一行（那四张表 / 版本树）。
        InstanceFiles  ///< 实例设置「文件管理」页点一条文件的名字。
    };
    /// 打开这一条资源的详情页（`iconKey` / `kindKey` 是条子上那两个角色）。
    ///
    /// @return 用户是怎么把这一页收掉的（`execPage()` 那一发的结果）：**返回箭头和 Esc
    ///         都是 `QDialog::Rejected`**，而这一页自己没有「确定」那一档（那三颗按钮和
    ///         版本行都只发信号、不 accept）。开不出来时回 `kResourcePageNotOpened` ——
    ///         调用方按它决定"要不要把来路放回去"（见 `openFileResourcePage()`）。
    ///
    /// NOTE: **必须排在事件投递之后**再调（调用点走 `QTimer::singleShot(0, …)`）：里面那句
    ///       `execPage` 会转起一个嵌套事件循环，而调用它的地方（树的事件过滤器）正处在
    ///       一发鼠标事件的投递途中 —— 在那里面再进一层事件循环，视图那一侧的状态机就悬了。
    /// @param instanceId 非空 = 这一页是**从那个实例进来的**：那一排 chip 会预选到那个实例
    ///        的游戏版本上，而版本组里那个实例的加载器排在前面（用户原话「点开某个模组等的
    ///        时候将版本自动选到当前实例对应的那个，对应的模组加载器的版本放在前面，资源包
    ///        等同理，展示为对应版本适用的」）。空 = 不预选（照旧停在「全部」那颗上）。
    ///
    ///        NOTE: 这一位**不是可选的**（不给默认值）：加一个新入口时忘了说"是不是从某个
    ///              实例来的"，编译就过不去 —— 和上面 `DetailSource` 那条设计理由逐字相同。
    int openResourceDetailPage(const QString &iconKey, const QString &kindKey, DetailSource from,
                               const QString &instanceId);
    /// 详情页上某一颗 chip 被按了（参数是那颗的 key，空串 = 「全部」）：重算筛 + 分组。
    void onResourcePageChipChanged(const QString &gameVersion);
    /// 详情页上方那个「显示快照版」勾选框被拨了（点它、或者按 Alt 键）。
    ///
    /// 它换的是**那一排 chip 的表**（折着的那一份 ↔ 原样那一份），所以三件事一起做：
    /// 存下这一位、把 chip 那一排整个换一份（按着的那一颗换算成同一大版本在另一档里的
    /// 说法）、再按新的这一份重算筛 + 分组。**不发请求** —— 版本列表是开页时就整份取回来
    /// 的，折与不折都是在本地的这一份上算。
    void onResourcePageShowSnapshotsChanged(bool showSnapshots);
    /// 把那一排 chip 摆成「这一份声明表 + 这一档（折 / 不折）」该有的样子：第一颗是
    /// 「全部」，其余按 `gameVersionChoices` 摆；@p wanted 是要按下去的那一颗（表里没有、
    /// 或者给的是空串，页面自己会挑「全部」）。
    ///
    /// **两个调用点**（开页、拨勾选框）共用这一份，免得两处各拼一遍、拼出两种表来。
    /// NOTE: 做成"摆到页面上去"而不是"返回一份表"，是为了让这个头**一个 ui/ 的类型都不用
    ///       带进来**（这一整个头对 ui/ 那边只按指针持有，见上面那几行前置声明）。
    void applyResourcePageChips(const QStringList &declaredGameVersions, bool showSnapshots,
                                const QString &wanted);
    /// 按当下这颗 chip 从**全量**那份清单里筛出来、分组、摆进页面（空的那一对也走它）。
    void fillResourcePageGroups();
    /// 摆 chip 与版本列表中间那一行（"这一版要装的前置"）。
    ///
    /// **不是**拿参数进来算，而是读 `m_resourcePage.dependencies` 那一份 —— 名字那一发回来
    /// 的时候"最上面那一版"可能已经换了，这里要填的始终是**当前这一页当下那一份**。
    /// 名字不全时**先摆"有几条"那句、再去问名字**（问完回来再进一次这个函数）。
    void updateResourcePageDependencies();
    /// 详情页要的那几个前置**叫什么**（`ModDependencies::projectsKeyFor("detail", …)`）回来了。
    /// 和 `onDependencyProjectsFinished`（前置那一趟）**分开两个槽**：两件事的生命周期不
    /// 一样，而各自按自己的钥匙认人（见 setupConnections 里那段 ★）。
    void onResourceProjectsFinished(const QString &requestKey,
                                    const QList<ResourceCatalog::Hit> &hits,
                                    const QString &problem);
    /// `m_projectTitles` 那把键（`Hit::iconCacheId()` 那个拼法 —— `<来源>-<项目号>`，
    /// **唯一出处是它**，别在这儿再拼一遍字符串）。
    static QString projectTitleKey(ResourceCatalog::Source source, const QString &projectId);
    /// 把一发问回来的名字写进 `m_projectTitles`。**名字空的那几条不写**（写空串等于把
    /// "还没问过"说成"问过了、它没名字"，而下一个查它的人就再也不去问了）。
    void rememberProjectTitles(const QList<ResourceCatalog::Hit> &hits,
                               ResourceCatalog::Source source);
    /// 详情页上点了某一个具体版本（`Row::key` = `VersionEntry::id`）= 下它。
    void downloadResourcePageVersion(const QString &versionId);

    // ---------- 前置模组：装一个模组时带上它要的那几个（2026-09-28）----------
    //
    // 用户原话：「在模组下载页，选择模组后显示其依赖，点击下载后弹出窗口，让用户可以点击
    // 一键下载所有缺少的依赖」。判断那一半（缺哪些 / 去哪问 / 挑哪一版 / 挑不出来怎么说）
    // 全在 `ModDependencies` 里（纯函数，离线可测），**这里只接线**：
    //
    //   ① 点下载 → `installResourceVersion()` 先看这一版声明的必装前置里还缺哪几个；
    //   ② 一条不缺 → 和从前一模一样（连一个请求都不多发）；
    //   ③ 有缺的 → 问一次"这几个号叫什么"（名字在项目对象里，见 ResourceStore::fetchProjects），
    //      再逐条问"它有哪些版本"（筛的是**目标实例**的游戏版本 + 加载器）；
    //   ④ 全问完 → 弹一个框，把缺的那几个列出来，两颗按钮：「一起下载」/「只下这个」；
    //   ⑤ 一起下载 → 把 `Plan::downloads` 和主模组**并成一批**下（`downloadResourceFiles`
    //      一次收全表，所以它们是同一批、一个进度、一条"下好了"的日志）。
    //
    // NOTE: 那一批里**解不开的那几条**不会让整件事停下（下不下来的前置不代表这个模组不能
    //       装），它们进 `Plan::unresolved`，弹框里原话列出来 —— 用户看到的必须是
    //       "这几个没找到"，不是"什么都没发生"。
    struct DependencyCheck {
        /// 正在跑这一趟（**只有一趟**：用户连点两下"下载"时第二下不该再起一趟 ——
        /// 两趟会各问一遍、各弹一个框，而第一趟已经弹着的那个框正转着嵌套事件循环）。
        bool active = false;
        ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
        QString title;
        ResourceCatalog::VersionEntry version;
        QString instanceId;
        ModDependencies::Target target;
        /// 还缺的那些（`missingRequiredDependencies()` 的结果，顺序原样）。
        QList<ModDependencies::Lookup> missing;
        /// 名字那一发的钥匙（回来的不是这一把就丢 —— 用户完全可能中途又点了别的）。
        QString titleRequestKey;
        // NOTE: 这一趟**自己没有一份"项目号 → 名字"**（2026-09-28 改）：名字从
        //       `m_projectTitles` 那个会话级缓存里查（详情页那一行和这一趟共用它）。
        //       在这儿再存一份的症状是"同一个号在详情页上已经叫 Sodium 了、弹框里还是
        //       一串数字"—— 两份名字各写各的，谁也不报错。
        /// 钥匙 → 那一发的版本列表。**缺席 ≠ 空表**：前者是"没问到"（网络），后者是
        /// "它就是没有版本"（`ModDependencies::buildPlan` 那两句话分得清清楚楚）。
        QHash<QString, QList<ResourceCatalog::VersionEntry>> answers;
        /// 还差几发没回来（0 到齐了 → 收尾）。
        int pending = 0;
        /// 这个实例的 mods **认过没有**（见 `installedModKeys()` 的 `known` 出参）。
        /// 只决定弹框那句话："这几个还没装" ↔ "无法确认你装了没有"——
        /// **空表和"确实一个都没装"是两件事**，混成一句等于替用户认下一件我们不知道的事。
        bool installedKnown = false;
    };
    DependencyCheck m_dependencyCheck;

    /// "这一版要装进哪个实例" —— **这一句话的唯一出处**。
    ///
    /// 两条来路，按优先级：
    ///   ① 下载页顶上那个「已安装版本」下拉框（在下载页里点一行、或从实例设置点「下载新
    ///      模组」切过去的那一趟，`continueOpenDownloadCategory()` 会替用户把它选上）；
    ///   ② 资源详情页记着的来路实例 `m_resourcePage.instanceId` —— 用户从**实例设置 → 模组
    ///      → 点名字**进详情页那一趟走的是它（`openFileResourcePage()` 把实例 id 递了进去，
    ///      但没碰过下拉框）。没有②的话，那一条路上既查不了前置、装的时候还会撞
    ///      「没有选中实例」。
    /// 两条都没有 = 不知道，返回空（调用方按空处理）。
    QString dependencyInstanceId() const;

    /// "这一趟下载是从**某个实例**里点进来的吗" —— 只决定 `installResourceVersionNow()`
    /// 要不要弹那个"存哪儿"的框（见 LauncherSettings 里 `downloadAutoIntoInstance` 那一节，
    /// 那里是这一支判据**唯一**的语义出处）。
    ///
    /// 两条来路（见实现里的注释）：① 从实例设置点「下载新模组」切到下载页
    /// （`m_downloadFromInstanceId`）；② 资源详情页是从实例点进来的（`m_resourcePage`）。
    bool downloadCameFromInstance() const;

    /// "这个前置要装进哪个实例"：游戏版本 + 加载器 + 来源（`ModDependencies::Target`）。
    /// 加载器名走 `instanceLoaderName()`（PCL 那类合并实例只有它能读出来，见那一处）。
    ModDependencies::Target dependencyTargetFor(const QString &instanceId,
                                                ResourceCatalog::Source source);
    /// 这个实例的 mods 里**已经认出来的**那些资源（键 = `<来源>-<项目号>`）。
    ///
    /// ★ 这份判据的**强度就是"这个会话认过多少"**：`m_instanceFileIdentities` 是文件管理页
    ///   那一趟反查填的，没进过那一页就是空的。所以多出来一个 @p known 出参：**空表**
    ///   和"确实一个都没装"是两件事，界面上那两句话不一样（见 `known` 的用法）。
    QSet<QString> installedModKeys(const QString &instanceId, bool *known) const;
    /// 起一趟前置检查（见上面那段）。占着 `m_dependencyCheck`。
    void startDependencyCheck(ResourceCatalog::Kind kind, const QString &displayTitle,
                              const ResourceCatalog::VersionEntry &version,
                              const ModDependencies::Target &target,
                              const QList<ModDependencies::Lookup> &missing,
                              bool installedKnown);
    /// 名字那一发回来了 → 逐条去问版本列表。
    void onDependencyProjectsFinished(const QString &requestKey,
                                      const QList<ResourceCatalog::Hit> &hits,
                                      const QString &problem);
    /// 某一条前置的版本列表回来了 → 记下答案，到齐了就弹框。
    void onDependencyVersionsFinished(const QString &requestKey,
                                      const QList<ResourceCatalog::VersionEntry> &versions,
                                      const QString &problem);
    /// 全问完了：算 Plan、弹框、按用户那一按继续装（或者只装主模组）。
    void finishDependencyCheck();
    /// 真正下那一段（`installResourceVersion()` 的后半截）。
    ///
    /// @param extras 一起下的前置（`Plan::downloads`；空表 = 只下主模组）。
    /// @param forcedInstanceId **这一趟就是给这个实例下的**（空 = 现算，见
    ///        `dependencyInstanceId()`）。只有一条来路会点名它：前置检查回来那一趟 ——
    ///        `finishDependencyCheck()` 是在一轮网络往返**之后**才走到这里的，那时候那一页
    ///        可能已经被用户收掉了，现算会算成别的实例。
    ///        非空还带一个副作用：**不再弹"存哪儿"那个框**（这一趟的落点是点名定死的）。
    ///
    /// NOTE: 实例文件页那颗「更新」（悬浮条上那一颗）**不走这儿** —— 它是一批一起下，见
    ///       `updateInstanceFiles()` 里那段（`downloadResourceFiles()` 一次只收一批）。
    void installResourceVersionNow(ResourceCatalog::Kind kind, const QString &displayTitle,
                                   const ResourceCatalog::VersionEntry &version,
                                   const QList<ModDependencies::Resolved> &extras,
                                   const QString &forcedInstanceId = QString());

    // ---------- 实例更新检测（2026-09-28）----------
    //
    // 用户原话：「实例设置那里做一个更新检测」。判据只有一条：盘上那个文件的 sha1 落在
    // 项目版本列表的第几条上（见 `ModDependencies::judgeUpdate()`）。
    //
    // 【为什么要先反查一遍】
    //
    // 盘上那些 jar 是**谁**、**哪一版**，只有反查说得出来（`InstanceIconLookup`：哈希 →
    // 项目号 → 项目对象，同一趟顺手把 sha1 带出来，见 `Found::sha1`）。所以这一趟的形状是
    // "枚举 mods → 反查一次 → 有几个项目就再问几发版本列表 → 判"，**全在后台线程**吗？
    // 不是：版本列表那几发走的是主线程上的 ResourceStore（界面这一层本来就等结果），
    // 只有"读几十兆 jar 算哈希"那一段在后台线程（沿用 `startInstanceFileIconLookup` 那套）。
    void checkInstanceUpdates(const QString &instanceId, InstanceSettingsDialog *dialog);

    /// 把"这个实例**上一趟查出来可更新**的那些文件"递给那几页（见
    /// InstanceFilesPage::setUpdateResult）。@p dialog 是**现在开着的**那一页（空 =
    /// 没开着，什么都不做）。
    ///
    /// 从两处喊：①开这一页的时候（拿下面那份会话缓存 —— 用户查过一次之后关掉再进来，
    /// 「可更新」还得筛得出东西）；②一趟检测收尾时（这时候才刚有结果）。
    void pushInstanceFileUpdates(InstanceSettingsDialog *dialog, const QString &instanceId);
    /// 反查那一趟回来了（更新检测这条路上的收尾：逐个项目问版本列表）。
    void onUpdateCheckLookupFinished(const InstanceIconLookup::Result &result,
                                     const QString &instanceId);
    /// 某个项目的版本列表回来了 → 判这一条，并且**当场就把报告里那一行准备好**。
    void onUpdateCheckVersionsFinished(const QString &requestKey,
                                       const QList<ResourceCatalog::VersionEntry> &versions,
                                       const QString &problem);
    /// 发那一发"问这个项目的版本列表"（**一把钥匙一发**）。
    ///
    /// 单独一个函数的缘由是它有**两个调用点**：反查那一趟回来时对 `itemsByKey` 里每把钥匙
    /// 发一遍，以及批量那一发回来时给"退回问 CurseForge"的那些补发（见
    /// `UpdateCheckRun::fallbackItems` 那段）。两处要发的东西是逐字同一句（项目号得从
    /// `<来源>-<项目号>` 那把 `iconKey` 上读），抄第二份迟早有一边忘了带上来源。
    void requestUpdateCheckVersions(const QString &requestKey);
    /// **批量那一发**回来了（`POST /version_files/update` —— Modrinth 认出来的那一拨，
    /// 见 `UpdateCheckRun::batchItems` 那段）。判完之后和单发那条路走**同一个收尾**
    /// （`finishUpdateCheck()`）。
    void onUpdateCheckLatestFinished(
        const QString &requestKey,
        const QHash<QString, ResourceCatalog::VersionEntry> &latest, const QString &problem);
    /// 全判完了：把报告写成一份**用户看得懂**的清单（能更新的排前面），弹框 + 日志。
    /// ★ 按值收（理由见 .cpp 里实现那一段：这个函数开头会把 `m_updateCheck` 清空，而
    /// `onUpdateCheckVersionsFinished` 递进来的就是它自己那个成员 —— 按引用收 = 自己把
    /// 自己的实参抹掉，整份结果会记在空键底下）。
    void finishUpdateCheck(QString instanceId);

    // ---------- 文件管理那几页的批量按钮（2026-10-09）----------
    //
    // 用户原话：「在选中任意数量模组时在列表下方的位置（不是表尾而是悬浮）显示几个按钮：
    // 更新 禁用 启用 全选 反选 取消 删除」。全选 / 反选 / 取消三颗**全在界面那一边**
    // （它们只动选中集，一个字节的盘都不碰），所以只有三件事落到这一层：
    // 更新 / 禁用·启用一把 / 删除一把。
    //
    // 【为什么「更新」是一批下、而不是一条一条走 installResourceVersion()】
    //
    // 计划里写的是"逐条走 installResourceVersion()（于是顺带过一遍前置检查）"，**做不到**：
    // `VersionManager::downloadResourceFiles()` 头一道闸就是 `downloadManager->isBusy()`
    // （见那儿那句 qWarning），第二发会被**直接拒掉**并回一句"已有下载任务正在进行"。
    // 也就是说那一圈里只有第一条真下得动，剩下 N-1 条全是失败弹框。
    //
    // 改成一整批，顺带也把前置检查留在了外面 —— 那是**刻意**的：前置是在**装**的时候
    // 要解决的事，而这一颗按钮动的是**已经装好的**那几条（它们当初装进来时前置就过过一遍
    // 了）。真要补前置，用户走的是下载页那条路（那儿有需求 1 的那个框）。
    /// 「更新」：把这一批路径各自换成一趟检测查出来的**最新那一版**，并成**一批**下。
    ///
    /// @param instanceId 哪一页上的（`m_instanceUpdateTargets` 的键）。
    /// @param pageKey `instanceFileDirs()` 的键（`mod` / `resourcepack` / `datapack` /
    ///        `shader`）—— 落点由它算，界面那边只说"是哪一页"。
    void updateInstanceFiles(const QString &instanceId, const QString &pageKey,
                             const QStringList &paths);
    /// 「禁用」/「启用」：@p disable 为真 = 禁用。**只动该动的那几条**（选中集里本来就
    /// 混着已启用和已禁用 —— 灰化规则是"至少有一条能切"，不是"全都是同一种"）。
    void toggleInstanceFiles(const QStringList &paths, bool disable);
    /// 「删除」：**只问一次**，然后逐条落盘。
    void deleteInstanceFiles(const QStringList &paths);
    /// ResourceStore 的三个回复。
    void onResourceSearchFinished(const QString &requestKey,
                                  const ResourceCatalog::SearchResult &result);
    void onResourceVersionsFinished(const QString &requestKey,
                                    const QList<ResourceCatalog::VersionEntry> &versions,
                                    const QString &problem);
    void onResourceIconReady(const QString &iconId, const QPixmap &icon);

    /// 服务器那一趟测速：一台问完了 / 全问完了（都从**工作线程**发出来，Qt 自己排队投递到
    /// 主线程，所以这两个函数里可以随便碰界面）。
    void onServerPingOne(int index, const ServerPing::Status &status);
    void onServerPingAllFinished();
    /// 从请求钥匙反查是哪一类（钥匙是 ResourceCatalog::searchRequestKey() 拼的）。
    /// 拼和认都走那一个函数，所以这里不解析字符串。
    ///
    /// `outSource` 收的是"这把钥匙是发给哪个来源的"（可为空指针）。**回来的结果必须看它**：
    /// 用户可能在等结果的这段时间里把来源换成另一个，两个来源的请求是各自飞的，不看它就会
    /// 把上一家那批摆在一个写着另一家的页面上（见 onResourceSearchFinished）。
    bool kindForSearchRequestKey(const QString &key, ResourceCatalog::Kind *out,
                                 ResourceCatalog::Source *outSource = nullptr) const;
    /// 这个条目在第几层（0 = 资源 / 1 = 游戏版本 / 2 = 版本号；不是树上那三层就回 -1）。
    ///
    /// NOTE: **唯一出处**。代理里认层用的是"有没有爹"（isChannel），这里认层用的是
    ///       "往上数几层"——两处必须说的是同一件事，而这句话在这里写一次。
    static int resourceRowDepth(const QTreeWidgetItem *item);
    /// 摆一行**占位**（"正在搜索…"、"没有搜到…"、"这个游戏版本上没有可用的版本"）。
    /// 占位行的定义就是**没有身份**（UserRole 空）—— 点它什么都不会发生，见
    /// onResourceItemActivated() 开头那个判据。树那一版加在顶层，子那一版加在某一条底下。
    static QTreeWidgetItem *addResourcePlaceholderRow(QTreeWidget *tree, const QString &text);
    static QTreeWidgetItem *addResourcePlaceholderChild(QTreeWidgetItem *parent,
                                                       const QString &text);
    /// 把搜索结果摆进第一层（每条底下顺手把第二层的游戏版本一起建出来）。
    /// `kind` 要写进每一条的身份里（MainWindowUI::resourceKindRole()）—— 点第三层那一下
    /// 靠它算"下到哪儿去"（见那个函数的注释）。
    void fillResourceHitRows(QTreeWidget *tree, ResourceCatalog::Kind kind,
                             const ResourceCatalog::SearchResult &result);
    /// 把版本列表摆进第三层（先清掉"正在获取…"那一行）。
    void fillResourceVersionRows(QTreeWidgetItem *gameItem,
                                 const QList<ResourceCatalog::VersionEntry> &versions,
                                 const QString &problem);

    // ---------- 每一页顶上那一行筛子（2026-09-25）----------
    //
    // 用户原话见 MainWindowUI 里 `downloadFilterVersionComboName()` 上面那段。界面那一层
    // 只把**空框**搭出来（ui/ 不 include src/，见那边的文件头），所以"每一项是什么字、
    // 是什么键"全在这儿灌：来源是 `ResourceCatalog::allSources()`、类型是 `categoryOptions()`、
    // 模组加载器是 `loaderOptions()`、版本是 VersionManager 那份在线清单。
    //
    // 装配是**两个方向**的，别混：
    //   · 往下（灌）：`fillDownloadFilters()` 一次把五页都灌满；来源一变、
    //     版本清单一到，再灌一次（`refreshDownloadFilterOptions()` / onVersionsFetched）。
    //   · 往上（读）：`downloadFilterXxx()` 那四个访问器是**唯一出处** ——
    //     startResourceSearch() 和"清空条件"都从它们读，别在各处 `currentText()` 一遍
    //     （读法不一致的症状是"界面显示筛了、发出去的请求没筛"，而且不报错）。
    /// 把五页的筛子都灌一遍（构造函数里一次）。来源 / 类型 / 加载器那三项是**当时**按来源
    /// 算出来的，所以这里也顺手把"类型那一栏要不要藏"定下来。
    void fillDownloadFilters();
    /// 灌**某一页**上跟着"来源"变的那两项（模组加载器 / 类型），并把类型那一栏按
    /// `categoryOptions()` 有没有回空表决定画不画。用户在来源框里换了一下就调它。
    ///
    /// NOTE: 它**自己不搜索**（这一层只管把选项换一份）。"换完就按新来源搜一发"接在
    ///       构造函数里那条 `activated` 上 —— 「下拉即搜」这条规矩在那一处收口。
    /// NOTE: 换来源会把这一页的加载器/类型两项**退回「全部」**：两边的键根本不是一套
    ///       （Modrinth 的 `fabric` vs CurseForge 的 `4`），留着上一个来源的选中项就是
    ///       拿 A 家的号去问 B 家 —— 而且它不报错，只会搜出个空的。
    void refreshDownloadFilterOptions(ResourceCatalog::Kind kind);
    /// 把那一份**在线版本清单**灌进五页的「版本」框（构造时一次、清单一到又一次）。
    ///
    /// NOTE: 这一格是**可输入**的，所以灌的时候要把用户手里那串字原样放回去（见实现）——
    ///       清单是异步回来的，把它当成"重新填一遍"就会清掉他刚打的版本号。
    void fillDownloadFilterVersionOptions();
    /// 这一页现在选的来源（框里没有值 / 认不出来 = Modrinth，和"没这一栏"时一致）。
    ResourceCatalog::Source downloadFilterSource(ResourceCatalog::Kind kind) const;
    /// 这一页现在选的游戏版本（**框里的字**：这一格是可输入的，用户自己打的版本号
    /// 不在列表里，读 `currentData()` 会读到一个空 —— 那正是"看着筛了、实际没筛"）。
    /// 空串 = 不筛。
    QString downloadFilterGameVersion(ResourceCatalog::Kind kind) const;
    /// 这一页现在选的模组加载器。**只有模组那一页有这一栏**（别的页连控件都没有，
    /// 见 MainWindowUI 里那段），所以别的页回空串 = 不筛。
    QString downloadFilterLoader(ResourceCatalog::Kind kind) const;
    /// 这一页现在选的类型（键来自 `categoryOptions()`）。空串 = 不筛。
    QString downloadFilterCategory(ResourceCatalog::Kind kind) const;
    /// 「清空条件」那颗键：名称框和四个筛子都回到"不筛"。**这一层只清、不搜** —— 搜是
    /// 调用方的事（那颗键上接着"清完重搜一发"，见构造函数里那段接线）。
    void resetDownloadFilters(ResourceCatalog::Kind kind);

    // ---------- 「设置」选项卡 ----------
    /// 把设置对象里的值灌进设置页的控件。构造函数里调一次；**不要**在用户改动之后
    /// 再调它 —— 那会把用户正在编辑的东西顶回盘上的旧值。
    void applySettingsToUi();
    /// 扫一遍这台机器上装了哪些 Java，结果进 m_localJavaVersions。
    ///
    /// NOTE: 这一个是**无条件**重扫。要跑几次 `java -version`（每次几十到几百毫秒），
    ///       所以"切到设置页顺手扫一下"那条路走的是下面那个带节流的 refreshJavaList()。
    void scanLocalJava();
    /// 接收后台扫描结果，并把用户手动钉住但不在常规目录里的 Java 合并进去。
    void applyDetectedJavaVersions(const QMap<int, QString> &versions);
    /// 第一次切到设置页时扫一次（一次会话里只扫这一遍）。加节流是为了不在启动路径上
    /// 加这段延迟 —— 用户专门来「Java 管理」那一页看的时候不走这条，那边要的是当场的
    /// 真实结果（见 openJavaManage）。
    void refreshJavaList();
    /// 把「Java 版本」那一行下面的说明文字刷成**当前设定**：手动/自动两档 × 路径有值/
    /// 无值，四种说法。它说的是"设定了什么"，不是"这次启动一定会用哪个" —— 自动档下真正
    /// 挑中的 Java 要到启动那一刻才知道（所以文案写的是"上次用的是…"）。
    void updateJavaPathLabel();
    /// 把归一化之后的「版本名格式」开关回灌到四个勾选框上（内部挡信号）。
    void pushVersionNameOptionsToUi(const VersionNameOptions &options);
    /// 选项卡切换：底栏左下角要不要显示实例名、以及要不要扫 Java，都在这里收口。
    void onMainTabChanged();
    /// 开「Java 管理」那一页（设置页「启动」栏那颗按钮），打开后在后台重扫一遍盘。
    void openJavaManage();
    /// 把 m_localJavaVersions 里的四个主版本灌进那一页。
    void pushJavaRowsToPage();
    /// 那一页发上来的三件事；实际扫描 / 下载由 JavaRuntimeController 在后台完成。
    void onJavaDetectRequested();
    void onJavaDownloadRequested(int major);
    void onJavaDownloadAllMissing();
    /// 「选哪个 Java」那两颗（原来设置页那个下拉框的语义，2026-09-28 搬进那一页）。
    /// 走的是同一对 setter + save()，所以盘上的格式一个字都没变。
    void onJavaUseAuto();
    void onJavaUseRuntime(int major);
    /// 把「内存分配」那一行画成当前状态：自动档按**现在**的系统内存重算并落盘、禁用两个
    /// 数字框，手动档原样显示用户定的值；两种情况都要写那句"这台机器有多少内存"的说明。
    /// 调用点：开窗灌值时（applySettingsToUi）、切到设置页时、用户切自动/手动那一刻、
    /// 用户拖/填「最大内存」那一刻、真的要启动游戏之前（那一刻的可用内存才算数）。
    void refreshMemoryUi();
    /// 把「最大内存」那一行的**上限**收到这台机器真有那么多上（用户原话：「上限=本机物理
    /// 内存」）。传进来的就是 SystemMemory 读到的物理总量（MB，读不到时 <= 0）。
    ///
    /// 为什么上限得由业务层喂给界面：`ui/` 不许依赖 `src/`（那条老规矩），界面里那个
    /// 65536 是"单独搭出来时"的天花板，不是这台机器的天花板 —— 一台 16 GB 的机器上让它
    /// 能拖到 65536，用户拖到那儿只会在启动时拿到一句 JVM 的内存分配错误。
    ///
    /// NOTE: 它**只改上下限、不动值**（信号也挡着），值由调用方紧接着灌（见 refreshMemoryUi）。
    /// NOTE: 真要算"这台机器该给到多少"的是 MainWindowUI::memoryCapForTotalMb() —— 实例设置
    ///       那一页也要按同一个上限收口（见 InstanceSettingsDialog 里那段），所以算法只有
    ///       那一份，这里只是把它贴到设置页那两个控件上。
    void applyMemoryLimits(qint64 totalMb);
    /// 让「最小内存 ≤ 最大内存」这条一直成立 —— JVM 会拒绝相反的组合：
    ///   Error occurred during initialization of VM
    ///   Initial heap size set to a larger value than the maximum heap size
    /// ★ 2026-09-26 起界面上**没有**最小内存那一格了（用户要求撤掉），替它把门的就是这里：
    /// 凡是改到 Xmx 的地方都得过一下。@return 夹下来了没有（调用方据此决定要不要落盘）。
    bool clampXmsToXmx(int xmxMb);
    /// 启动前把这次真正要用的内存定下来，**只改传进来的那份值**。
    ///
    /// 自动档下按**现在**的系统内存重算一次（见 refreshSuggestedMemory 那段注释：
    /// 同一台机器开着浏览器和什么都没开能差好几 GB）。
    ///
    /// NOTE: 重算的结果**只写在传进来这份值上**，两个作用域都不落盘：
    ///       - 自动这一档来自全局 → 走 refreshSuggestedMemory()，它自己会按老行为
    ///         写全局（那正是"全局是自动档"的意思）；
    ///       - 自动这一档是这个实例自己要的 → 现算一份给这次启动用，全局一个字节
    ///         都不动（实例 A 开着自动，不该改到全局那份内存配置）。
    ///       反过来"顺手写回实例的 meta"也不行：那会给它留下一对死值，下次读出来
    ///       只会让「实例设置…」里那两行显示成"自定义 + 一个用户没填过的数"。
    ///
    /// NOTE: 这是**唯一**该在启动路径上动内存的地方，而且它绝不改设置页。设置页那对
    ///       数字框以前会被"顺手刷成这次实际用的值"，有了实例覆盖之后那句话不再成立：
    ///       实例覆盖了 2048，设置页却显示 2048，用户会以为全局被改了。
    ///
    /// @param versionId 这是哪个实例 —— 自动档要拿它数一数装了多少模组（见下）。
    /// @param io 进参是合成好的有效值，出参是定下来的内存。
    /// @param override 这个实例的覆盖（用来判断"自动这一档是谁要的"）。
    ///
    /// NOTE: 数模组这件事**只有走到"这个实例自己要自动档"那一支才做**（全局那一支走
    ///       refreshSuggestedMemory()，那里没有实例可分）。数一次是读一个目录，但那两处
    ///       调用都在启动/导出的同步路径上，能省则省。
    void resolveMemoryForLaunch(const QString &versionId, EffectiveJavaSettings &io,
                                const JavaSettingsOverride &override);
    /// 这个实例的 `mods/` 目录里有多少个模组（按 `*.jar` 数）。
    ///
    /// 只数 `*.jar`：禁用的模组在盘上是 `X.jar.disabled`（见 InstanceFilesPage 的
    /// identityPath），它**不会被游戏加载**，跟着算进内存建议里只会让数字虚高。
    /// 目录不在、读不了都返回 0（"不知道"和"一个都没有"在这里是同一个落点：不加成）。
    int instanceModCount(const QString &versionId) const;
    /// 这一次启动到底执行什么：**程序**（Java 可执行文件）+ **那一串参数**。
    ///
    /// 收在一个函数里是因为有两个消费者，而它们必须给出**逐字节一样**的答案：
    ///   - onStartButtonClicked()：真的启动，把 arguments 交给 startGame()（program 那一格
    ///     由 LauncherManager 自己从 settings.javaPath 取，取不到就退 PATH 上的 java ——
    ///     和下面那个兜底是同一件事）。
    ///   - exportLaunchScript()：把这两个字段写成 .bat（用户要的是"双击就按这套参数启动"，
    ///     导出一份和启动器**不一样**的命令行等于导出一份假的）。
    ///
    /// @param javaSettings 这次启动要用的那一套 Java 值（全局按实例覆盖合成之后的结果）。
    ///        program 就是它里面的 javaPath（空 = 还没挑过，用裸 `java` 让 cmd 走 PATH）。
    /// @param offlineMode 这一趟走不走离线（判据只有一处：offlineIdentity()）。
    /// @param quickPlay 这一次要不要"开游戏直接进某个存档 / 直接连某台服务器"
    ///        （`--quickPlaySingleplayer` / `--quickPlayMultiplayer`）。默认是**没有这一发**
    ///        （`Target{}`）—— 导出启动脚本那一路就是这么调的（一个"直接进这个存档"的
    ///        .bat 不是用户要的东西），真启动那一路从 `m_pendingQuickPlay` 取。
    ///
    /// NOTE: assetIndexId 与 javaMajorVersion 的兜底值（"29" / 21）也在这条路上算，见
    ///       versionRuntimeIds()。它**不**去挑 Java、也不下运行时 —— 那两件事分别是
    ///       ensureJavaForVersion 和 startGame 的事，都不属于"拼命令行"。
    QStringList launchArguments(const QString &versionId, const QJsonObject &versionJson,
                                const EffectiveJavaSettings &javaSettings, const QString &token,
                                bool offlineMode,
                                const QuickPlay::Target &quickPlay = QuickPlay::Target()) const;
    /// 用户在那个「Java 版本对不上」的框里选的哪一条路。
    enum class JavaMismatchAnswer {
        /// 没问（绝大多数启动都走这条：实例没钉 Java、总闸关着、这个实例表过态不再问、
        /// 或者那条路径压根探不出主版本）。
        NotAsked,
        /// 「仍然用这个 Java 启动」—— 照他钉的那个启动，一个字节的配置都不改。
        KeepInstanceJava,
        /// 「改用匹配的 Java 启动」—— 把自动挑中的匹配路径写回这个实例。
        UseMatchingJava,
        /// 取消 / 关窗 / Esc —— 不启动。
        AbortLaunch,
    };

    /// 启动前问一句「实例指定的 Java 和这个版本要求对不上，你要怎么办」。
    ///
    /// 四个前置条件全满足才问：
    ///   ① 这个实例**覆盖了** javaPath（全局档不匹配时是静默换一个匹配的，那是有意的
    ///      老行为，不动）；
    ///   ② 全局那个总闸开着（GlobalJavaSettings::warnVersionMismatch()）；
    ///   ③ 这个实例没表过"以后别再问了"（wtfml.meta 的 skipJavaVersionWarning）；
    ///   ④ 那条路径探得出主版本、**而且**和这个版本要求的不一样。探不出（-1：路径不存在
    ///      或认不出来）不走这个框 —— "路径无效"和"版本不符"是两件事，前者仍然是现有的
    ///      硬错误。
    /// 代价只是一次文件读（probeJavaMajorVersion 的 release 快路径），所以绝大多数启动
    /// 在这里花的是微秒级。
    ///
    /// NOTE: 这一步只**问**，不动任何状态：值、meta、全局设置都由 onStartButtonClicked()
    ///       按返回的那个答案去改。唯一一处例外是"勾了以后不再提醒"这个长期偏好 ——
    ///       用户刚点的那一下，立刻落盘才对得上他的意思（而且写失败只记日志、不拦启动：
    ///       不该因为存不下一个偏好，把他刚授权的启动掐掉）。
    JavaMismatchAnswer askAboutJavaMismatch(const QString &versionId,
                                            const JavaSettingsOverride &override,
                                            int expectedMajor);
    /// 「自动（按系统内存）」勾选框。
    void onJavaWarnMismatchChanged();
    void onMemoryAutoChanged();
    void onMemoryChanged();
    /// 「最大内存」那根条动了（见 .cpp 里那段：拖动中只跟着走数字、落盘留给松手那一下）。
    void onMemorySliderMoved();
    void onLauncherBehaviourChanged();
    void onDownloadSourceChanged();
    /// 「下载新模组时直接放进当前实例」那一格（设置页「下载」栏）。
    ///
    /// 改了这一项**不影响已经发出的那一批下载** —— 落点在点下那一瞬间就定死了（见
    /// installResourceVersionNow）。这是刻意的：半批落在一个地方、半批落在另一个地方，
    /// 比"这次改的下一批才生效"难查得多。
    void onDownloadAutoIntoInstanceChanged();
    /// 「缺失前置提醒」那一格改了（见 LauncherSettings 里那一节）。
    void onWarnMissingDependenciesChanged();
    /// CurseForge 的 API key 那一格编辑完了（回车 / 焦点离开）。**不是** per-character：
    /// 半串 key 存进去只会换来一次 403，而用户会以为是启动器坏了（见 .cpp 里那段）。
    void onCurseForgeApiKeyEdited();
    // ---------- 「启动选项」那三项（设置页里同一组） ----------
    /// 「显示游戏日志」开关。关掉**不关已经开着的那些窗**（用户可能正在读崩溃信息）；
    /// 打开之后从下一行输出开始就会开窗（这是刻意的，见 showGameLogLine 的说明）。
    void onShowGameLogChanged();
    // ---------- 「启动提醒」那两条（设置页里同一组） ----------
    /// 十秒内重复启动同一实例时要不要先问一句。改了这一项**不影响已经开着的那些游戏**，
    /// 也不清 m_lastLaunchAttempts —— 那个表只是"上次点启动是什么时候"，关掉开关不该
    /// 把它抹掉（再打开时那十秒窗口可能正好还没过）。
    void onWarnDuplicateLaunchChanged();
    /// 同一实例已经在跑时要不要提醒可能丢进度。
    void onWarnWorldLossChanged();
    void onProcessPriorityChanged();
    /// 「窗口模式」那个下拉框（小窗 / 最大化 / 全屏）。落盘之后会调
    /// updateGameWindowSizeEnabled() —— 档位决定了下面那两个数字框还管不管用。
    void onGameWindowModeChanged();
    /// 按当前窗口模式把「窗口大小」那两个数字框（以及中间那个 ×）灰掉/点亮。
    /// 「最大化」那一档不吃那两个数（见 GameWindowSettings.hpp），所以那一档是灰的。
    ///
    /// 灌值那一段也要调它一次：那边走的是有 QSignalBlocker 的 selectByEnumData()，
    /// 走不到 onGameWindowModeChanged() 里去。
    void updateGameWindowSizeEnabled();
    /// 「窗口大小」那两个数字框共用这一个槽（和内存那两个 spin 同一套写法）。
    void onGameWindowSizeChanged();
    /// 「外观」那一格：条上那行字。**边打字边生效也边落盘**（和上面两个不同，理由在
    /// 接线那一处的 NOTE 里）。落盘的是 `setTitleText()` 归一化过的值，条上画的也是它 ——
    /// 于是"条上显示的字"和"盘上存着的字"永远是同一个。
    void onTitleTextChanged();

    // ---------- 「背景图」那一节（设置页「个性化」；2026-09-28 加的） ----------
    // 用户原话：「能给主页左侧自定义图像吗？其他页面的背景能换图吗」。两块底各一行，
    // 动的是**主题规格**（`ui/Theme.hpp`），不是 LauncherSettings —— 见 .cpp 里那一段
    // 开头写的"改内存里那份 → 落盘 → 立刻铺开"这三步。
    //
    // NOTE: 驱动这一节的四条槽（`onThemeImageChooseClicked` 那一组）和 `refreshThemeImageRows()`
    //       都在上面 `private slots:` 那一段里 —— 它们本来就只被控件上的信号驱动，做成槽是
    //       为了验证工程能按名字把 `refreshThemeImageRows()` 调起来（见那一处的 NOTE）。
    /**
     * @brief 把某一块的图改掉，然后**落盘 + 立刻铺开 + 重画这一行**。
     * @param partKey `Theme::partKey()` 的结果（"homeLeft" / "pageBackground"）。
     * @param imagePath 改完之后要铺的那张图（**绝对路径**）；空串 = 这一块不铺图。
     * @param imageName 用户挑的那张**原来叫什么**（换图那一发给 `QFileInfo(picked).fileName()`，
     *        拖滑块/换摆法那两发把上一份的名字原样带过来）。设置页那一行「现在铺的是：…」
     *        写的是它 —— 盘上那份叫 `homeLeft.jpg`，写那个用户等于什么都没看见。
     * @param fit `int(Theme::ImageFit)`。
     * @param opacity 0~100。
     *
     * NOTE: 四个值**都由调用方给全**，不读盘、也不从控件上捡：规格是整份写回去的，所以
     *       "这一次改的是哪一项"必须说清楚（拖滑块那一路只改不透明度，另外三项得原样带过来）。
     * NOTE: `fit` 收的是 int 不是 `Theme::ImageFit` —— 这个头不 include `ui/Theme.hpp`
     *       （那一层的头文件不许被 src/ 按值用进来，见本文件开头那段"前置声明 vs include"）。
     */
    void setThemePartImage(const QString &partKey, const QString &imagePath,
                           const QString &imageName, int fit, int opacity);
    /**
     * @brief 把一行游戏输出写进**那一份会话**的日志窗；窗不在时按需建一个。
     * @param sessionId 哪一份会话。<= 0（不挂在任何实例上的那句话）直接丢。
     * @param text 原样写进去的那段字。
     *
     * NOTE: 判据是"设置里那个开关现在是不是开着"，不是"启动那一刻开没开"。这样用户在
     *       游戏跑着的时候把它打开，从下一行输出起就能看见 —— 比"M 要等下次启动"更符合
     *       「显示游戏日志」这几个字的字面意思。代价是那一窗里没有这个开关打开之前的输出，
     *       这是**明说的**（见设置页那句说明）。
     *
     * NOTE: 用户手动关掉过的窗不会自己再冒出来（m_gameLogDismissedSessions）。
     *       "我刚把它关了、你又给我弹回来"是这类窗最招人烦的失败模式。
     */
    void showGameLogLine(int sessionId, const QString &text);

    /**
     * @brief 把**那一份日志窗里现在的内容**存成一个文本文件（底下那颗「导出日志…」）。
     * @param sessionId 哪一份会话。
     *
     * 问存到哪儿（系统那个保存框）、写、说结果，三件事都在这儿。`ui/` 那边只发一个信号
     * （理由见 GameLogWindow 的文件头：那一层不认识数据目录）。
     *
     * NOTE: 存的是**全部行**（`allLogText()`），不是屏幕上那一份：用户只勾着 ERROR 的时候
     *       导出，拿到的仍然必须是整份日志 —— "我明明导出了、打开只有三行"是这件事最坏的
     *       一种表现。
     */
    void exportGameLog(int sessionId);
    /// 同上，存的是**错误报告**（头部那些事实 + 错误摘录 + 完整日志，见 util/ErrorReport）。
    void exportGameErrorReport(int sessionId);

    void onVersionNameOptionChanged();
    void onApplyExtraJvmArgsClicked();
    /// 把「下载源」交给真正发请求的那一层（VersionManager → DownloadManager）。
    /// 启动时读盘、用户在设置页改，两条路都走这一句，别各写一遍。
    void syncDownloadSource();
    /// 把 CurseForge 那两项设置交给数据层（VersionManager）。同上：读盘和用户改都走这一句。
    void syncCurseForgeSettings();
    /// 启动成功后按「启动后」那一项处理启动器自己。
    void applyLauncherBehaviourAfterLaunch();

    /// 把一条消息交给 Qt 的日志系统（写进 `<数据目录>/logs/wtfml.log`，命令行里也能看）。
    /// 界面里已经没有日志面板了 —— 必须让用户看到的消息走的是 `InlineMessage`，不靠这一条，
    /// 见 .cpp 里的说明。NOTE: 这里原来写的是"走 QMessageBox"，那句话早和代码相反了
    /// （内嵌化那一批把 90 来处全换掉了），2026-09-27 一并改掉。
    void appendLog(const QString &text);
    /// 刷新底部状态条那三格（头像/用户名、**任务摘要**、实例运行状态）。
    /// 显示规则见 .cpp；界面那边只负责把控件搭出来。
    void updateBottomBar();

    // ---------- 任务中心（2026-10-12，coding plan §2 / §3） ----------
    /// 底栏中间那格**现在该说什么**：把账本里那一堆翻成 `MainWindowUI::BottomBarTaskSummary`
    /// 摆上去（谁说话算数由 `Tasks::collapsedSummary()` 定 —— 那是账本层的纯函数，
    /// 这一格自己不再有一套优先级，旧那套 `bottomBarProgressOwner` 整个撤掉了）。
    ///
    /// NOTE: 「刚完成的那一项还露着」这一档是**有期限的**（`taskSummaryRecentWindowMs()`），
    ///       所以这里顺带把那个到期重画的小定时器重新起一次。
    void updateBottomBarTaskSummary();
    /// 任务中心那块面板要画什么（点开/刷新时**现算**一遍，和底栏那两块面板一个路数）。
    /// ★ 它有一处**副作用**：被调一次就算"用户看过了" ⇒ 把那几个没看过的失败标记清掉
    ///   （面板里那几行是唯一能把失败原因看全的地方，"列出来了"就是"看过了"）。
    QList<TaskCenter::Item> taskCenterItems();
    /// **一项** ⇒ 界面那一份（`taskCenterItems()` 和任务详情页共用的唯一翻译点）。
    /// **没有副作用**（面板那个 `taskCenterItems()` 有"看一眼 = 看过了"那一笔，这个没有）。
    TaskCenter::Item taskCenterItem(const Tasks::TaskSnapshot &task) const;
    /// 任务详情页上的那一排按钮被点了（coding plan §3.4「失败能看到失败阶段、简短原因和
    /// 『查看日志/重试』」—— 那一页是这句话落地的地方）。
    ///
    /// 这一页**自己什么活都不干**（`ui/` 不 include `src/`）：它只报"哪一项、点了哪一颗"，
    /// 取消谁、重试什么、日志在哪、目标目录怎么拼全在这里。
    void onTaskDetailAction(const QString &key, TaskDetailPage::Action action);
    /// 任务中心里**某一行被点了** ⇒ 开那一页（`key` 是任务号的十进制字符串）。
    ///
    /// NOTE: 页面对象**建一次留着**（和资源详情页 / 实例设置同一个路数）：每一次点开都
    ///       `setTask()` 重摆一遍。`overlay->execPage()` 是**阻塞**的（它内部那个嵌套
    ///       事件循环要等到用户点返回），所以"开页之后"那句要写在 `execPage()` 之前或
    ///       之后分清楚（见那一处）。
    void openTaskDetailPage(const QString &key);
    /// 把账本里那一项翻成详情页要的一份（找不到那个号时回 false，页照开但摆的是空的）。
    bool fillTaskDetail(const QString &key, TaskCenter::Item *out) const;
    /// 「打开日志」「打开目标位置」「复制摘要」那三颗（见 onTaskDetailAction()）。
    void openTaskLog(const QString &key);
    void openTaskTarget(const QString &key);
    void copyTaskSummary(const QString &key);

    /// 「这一类活**起手**就允许什么」那张表（唯一出处，见 .cpp 里那段说明）。
    /// 会变的那几位（`canRetry`）不在这里 —— 它由 `finishTask()` 在失败那一刻补。
    static Tasks::TaskCapabilities defaultTaskCapabilities(Tasks::TaskKind kind);
    /// 记一件新任务（发号 + 落盘）。回它的号。
    Tasks::TaskId beginTask(Tasks::TaskKind kind, const QString &title,
                            const QString &targetId = QString(),
                            const QList<Tasks::TaskStep> &steps = QList<Tasks::TaskStep>());
    /// 收尾：成功 / 失败（失败时把号记进"没看过"）。**找不到那个号就什么都不做**。
    void finishTask(Tasks::TaskId id, bool success, const QString &errorSummary = QString());
    /// 这一批下载对上的任务号（没有这一批 = 无效号）。
    Tasks::TaskId downloadTaskId() const { return m_downloadTaskId; }
    /// 「正在忙的这一批下载」起手：按当前那三个 id 判断这是哪一类活儿（补全 / 装实例 /
    /// 导入整合包 / 普通下载），记一件任务并把号存进 m_downloadTaskId。
    ///
    /// NOTE: 判据就是 `m_repairingInstanceId` / `m_installingInstanceId` /
    ///       `m_importingInstanceId` 那三个"界面上自己记的" —— 收尾那几条路（
    ///       onVersionDownloadFinished）用的也是它们，所以两头的分档**天然一致**。
    void beginDownloadTask();

    // ---------- 底栏那两块上拉面板（左：正在跑的实例 · 右：最近用过的账号） ----------
    //
    // 面板**本身**（怎么弹、弹在哪、一行长什么样）在 MainWindowUI 里；这里只给两样东西：
    // 点开时现算的"那几行是什么"（setBottomBarPanelContent() 的那个 provider）和
    // 行尾按钮被点之后的动作。两样都在 setupConnections() 里接进去。
    /// 面板里某一行的某个按钮被点了。
    /// @param account true = 右边那块（账号），false = 左边那块（实例）。
    /// @param key 那一行**是谁**（MainWindowUI::PanelRow::key）—— 不是行号，理由见那边
    ///        那段 NOTE（面板开着的时候列表是会变的，按行号回头去查会关错游戏/切错账号）。
    /// @param action 那一行第几颗按钮（下标由放进去的人定义，见 .cpp 里那几个 kXxxAction）。
    ///
    /// NOTE: 走到这里时面板**已经收回去了**（见 MainWindowUI 里 BottomBarPanel 那段：
    ///       这两个动作都要弹模态窗，压在 Qt::Popup 底下会点不动），所以这里可以放心 exec()。
    void onBottomBarPanelAction(bool account, const QString &key, int action);
    /// 底栏右面板里点了某个账号那颗「选择」/「登录」。
    ///
    /// 离线账号：切过去就完事（它本来就没有凭据）。正版账号：切过去之后**看一眼盘上那张
    /// 令牌的剩余有效期**，不到 6 小时就顺手换一张新的（用户 2026-09-24 的原话：
    /// 「看令牌需不需要更新，（过期时间小于6h）要的话就刷新」）—— 换不出来就弹窗说原因，
    /// 并指向"重新登录一次"那条路。
    void useAccountFromPanel(const QString &uuid);
    /// 底栏右面板里点了某个账号那颗「登出」—— 也就是**当前**账号那一行（别人那行写的是
    /// 「选择」/「登录」，见 recentAccountPanelRows()）。
    ///
    /// 清掉盘上的登录状态，但**不删账号**：那一项留在列表里，点它那行的「选择」/「登录」
    /// 就回来了（见 AccountStore::logout()）。想彻底不要这个账号走「账号与外观…」里的
    /// 「删除所选」。清完把底栏、日志、外观一起刷一遍。
    void logoutCurrentAccount();
    /// 一个版本号在界面上的显示名（"已安装版本"下拉框里的那条文本）。
    /// 下拉框里没有它时退回版本号本身。
    QString instanceDisplayName(const QString &versionId) const;
    /// 底栏中间那格"正在启动 X"里的 X：只有一份在启动时是它的显示名，
    /// 两份以上时是"N 个实例"。唯一一处拼它（见 .cpp 里的说明）。
    QString launchingInstanceName() const;
    /// 底栏左下角「启动中...」那句里的实例名。只有一份在启动时是它的名字，
    /// 两份以上时是"N 个实例"。
    ///
    /// 和上面那个 launchingInstanceName() 是**同一件事的两种写法**，差别只有一处：
    /// 这里**不看「版本名格式」那四个开关**（用户 2026-09-24 原话：「文本为版本名（没有就
    /// 版本号-加载器名称，不受名字显示设置影响）」）。上面那个读的是下拉框里那条文本
    /// （也就是用户设的显示名，中间那格"正在启动 X ..."要的就是它），所以两个都得留着。
    QString launchingStatusName() const;
    /// 只把实例页那棵树上"（运行中）"那几处后缀刷新一遍，**不重灌整棵树**。
    ///
    /// 为什么不直接调 refreshInstalledVersions()：那条路会重扫磁盘、还会走
    /// syncTreeToCombo() 的 setCurrentItem()，而那一下会把用户折起来的组重新撑开
    /// （见 wtfml-version-tree-ui 那条踩过的坑）。游戏起落时用户没在做"刷新列表"这件事，
    /// 不该被动了他的树。
    void updateInstanceTreeRunState();
    /// 现在哪些实例**至少有一份会话**在跑（给实例树那几行加"（运行中）"用）。
    QSet<QString> runningVersionIds() const;
    /// 现在哪些实例**还没落地**（正在下 / 正在装加载器 / 正在导入整合包）。
    ///
    /// 「下载完成前该实例不在启动选项中」就靠它：refreshInstalledVersions() 拿它把
    /// 那几个从下拉框、组合框、实例树和"（N 个实例）"计数里一起滤掉，
    /// onStartButtonClicked() 那道闸口也读同一份 —— 列表里滤掉的正是闸口拦的，
    /// 不会出现"列表里能选、点下去被拦"这种自相矛盾。
    ///
    /// 这几个来源缺一不可，它们兜的是同一种缝：活儿还在进行，但此刻**没有任何一批下载
    /// 在跑**（isDownloading() 是 false），于是只看下载状态的话，这个实例看起来已经好了。
    ///   - busyVersionId()：正在下这一批的那一个版本。
    ///   - m_importingInstanceId / m_installingInstanceId：整合包导入和"带名字的新实例"
    ///     都有"等原版版本 json"那一段（元数据请求，不算下载），导入还多一段解压覆盖。
    ///   - m_repairingInstanceId（2026-09-26，「补全文件」那颗按钮）：它**是**一批下载在跑，
    ///     所以 busyVersionId() 那一份本来就盖住它 —— 列在这里是给"按钮点下去、批次还没
    ///     起来"那一小段兜底（那之间隔着一道 isBusy() 判据）。那一段里这个实例的文件马上
    ///     就要被重写，照样不该被启动。
    QSet<QString> installingVersionIds() const;

    // ------------------------------------------------------------------
    // 启动前那两条提醒（设置页「提示」栏，2026-09-25 加的）
    // NOTE: 2026-09-26 设置页分栏时那一栏叫「提示」（启动 / 游戏 / 提示 / 个性化 / 下载），
    //       组标题从「启动提醒」改成「启动前提醒」，同栏里还并进了第三条（实例的 Java
    //       对不上时问一句）。
    // ------------------------------------------------------------------
    // 两条挡的都是**同一件事**：同一份存档被两个进程同时打开。这件事事后救不回来
    // （后保存的那一份盖掉先保存的），所以只有"点在启动上那一下"挡得住才算数 ——
    // 两条都在 onStartButtonClicked() 里、**在换令牌之前**（见那里的调用点）。

    /// 「十秒内重复启动同一实例」那个十秒（毫秒）。**唯一出处**：判据在这里、
    /// 验证工程摆边界也读这里。
    static int duplicateLaunchWindowMs() { return 10000; }

    /**
     * @brief 这一下点启动要不要继续（false = 用户在提示里选了「取消」）。
     * @param versionId 正要启动的那个实例。
     *
     * 按顺序问两条，各自受设置页里那个开关管：
     *   ① 「重复启动」：这个实例**已经有一份是我开的**（同一个身份），而且我上一次点启动
     *      就在十秒之内 ⇒ 手快连点的形状。标题说"已经有一个相同的运行中游戏进程了"，
     *      前提是那一份真的在跑 —— 所以判据里带着"确实有那么一份"，不会报一句假话。
     *   ② 「进度提醒」：这个实例**已经有任何一份在跑**（我开的、别人开的、上一问刚放行的）
     *      ⇒ 单人存档可能被两份游戏同时打开。
     *
     * 两条**都在**这个函数里，不是"二选一"：用户原话要的就是"上一问选了『仍要启动』之后
     * 接着问下一问"（「在启动同一个实例时（在前面选择仍要启动的情况下/不同账户）弹出
     * 进入单人世界可能会丢失进度的提示」）。所以同一个实例上连点两下、两条都开着时，
     * 框是前后两个，不是一个。
     *
     * NOTE: 两条都过了之后这里**记一笔时间**（m_lastLaunchAttempts）—— ① 那个十秒窗口
     *       读的就是它。记在"过了之后"而不是"启动成功之后"是刻意的：这一笔记的是
     *       **点下去的意图**，而下一步（换令牌 / 挑 Java / 起进程）失败与否和"用户现在
     *       想不想再点一下"没关系。
     */
    bool confirmLaunchWhileRunning(const QString &versionId);

    /**
     * @brief 当前账号的身份键（跨账号区分"是不是同一个人"用）。
     *
     * **离线账号的 uuid 是空的**（`AuthManager::setOfflinePlayer()` 会把 uuid 和令牌一起
     * 清掉），所以拿 uuid 当键会把所有离线账号撞成同一个 ⇒ 那种情况退回账号名。
     * 会话那一侧（`GameSession::accountUuid` / `accountName`）用的是同一个规矩，
     * 两处**必须**一致：不一致的症状是"我自己刚启动的那一份被当成别人开的"，于是
     * 「重复启动」那条永远不响，而它恰恰是最该响的那一条。
     */
    QString launchIdentityKey() const;

    // ---------- 「上一次启动」那本账（coding plan §5.1，2026-10-13）----------
    //
    // 主页左栏那张卡（「上次游玩」）的全部数据都在这一小片里：读盘、记账、翻成那句话、
    // 算"还能不能接着玩"。**判据层是 `manage/LaunchHistory`**（纯函数，verify 里逐格念），
    // 这一片只管"什么时候记、记的是谁、界面上什么时候重画"。

    /// 账上那一条的结局：从"这一趟结束时的两个事实"翻过来。
    ///
    /// ★ 这里**不是**"崩没崩"两个分支：游戏连第一行日志都没打出来就结束了的那一趟
    ///   （`neverStarted`）单独算一档 —— §5.1 说"上一次"以**成功创建游戏进程**为准，那种
    ///   情况进程确实起来了、用户却什么都没玩到，拿它当"上次玩的就是这一份"是撒谎。
    static LaunchHistory::Result launchHistoryResult(bool crashed, bool neverStarted);

    /// 账上那个目标 ⇄ 启动参数那一层的 `Kind`（**唯一出处**：写记录、"还能不能接着玩"、
    /// 那一颗按钮按下去三处都读它 —— 三处各写一遍 switch，迟早会有一处写反，症状是
    /// "接着游玩进的是服务器那一页的参数"）。
    static QuickPlay::Kind quickPlayKindFor(LaunchHistory::Target target);

    /// 把"这一趟成功了"记进账本。**产品那一侧唯一的调用点**是 `onStartButtonClicked()`
    /// 里 `sessionId > 0` 那一段（进程真的起来了那一刻）—— §5.1 明写「以成功创建游戏进程
    /// 为准，不以点击启动按钮为准」。
    ///
    /// @param versionId 实例 id。
    /// @param quickPlay 这一趟要进哪儿（空的 = 普通启动）。**`Target::None` 不是"不知道"，
    ///        是"这一趟就是普通启动"** —— §5.1：普通进主菜单那一趟不许猜。
    /// @param startedAtMs 启动时刻（毫秒；取会话自己的 `startedAtMs`，不是"现在"）。
    void appendLaunchRecord(const QString &versionId, const QuickPlay::Target &quickPlay,
                            qint64 startedAtMs);

    /// 那一趟结束了：把账上**最近那一条还写着"还在跑"、且启动时刻对得上**的记录改写成
    /// 真正的结局与时长。
    ///
    /// 认的是**启动时刻**这个键：退出那一刻会话已经从 LauncherManager 的表里摘掉了
    /// （见 `onGameFinished()` 那段 NOTE），追回"启动那一刻记的那一条"只有它。
    void finishLaunchRecord(qint64 startedAtMs, LaunchHistory::Result result, qint64 finishedAtMs);

    /// 账本从盘上读回来（构造末尾那一次）。读不出来 = 空账，**不是错误**：一次都没启动过
    /// 的新用户就是这样。
    void loadLaunchHistory();

    /// 主页左栏那张卡按账上最新的一条重画一遍（`MainWindowUI::setHomeRecent()`）。
    /// 三个时机都会调：构造末尾、启动成功那一刻、那一趟结束那一刻。
    void updateHomeRecent();

    /// 账上那一条现在还能不能「接着玩」——**卡片上那一颗按钮写什么字就是靠它**
    /// （能 ⇒「继续游玩」，不能 ⇒ 降级成「启动实例」，§5.1）。
    ///
    /// 三种"不能"：① 那个实例已经不在已安装列表里了；② 记的是快捷启动，可目标（世界 /
    /// 服务器）已经没了；③ 记的是快捷启动，可这个实例现在认不了那条参数（实例被换成另一个
    /// 版本了）。三种都落到同一句"降级成启动实例" —— 按钮**不消失**。
    bool homeRecentCanContinue(const LaunchHistory::Record &record) const;

    /// 账上那条记录的目标（世界目录 / 服务器地址）现在还在不在。
    /// 世界看 `<实例>/saves/<targetId>` 这个目录；服务器看 `servers.dat` 里还有没有
    /// **地址对得上**的那一条（按值认，不按下标）。
    bool quickPlayTargetAlive(const LaunchHistory::Record &record) const;

    /// 主页那张卡上那颗「继续游玩 / 启动实例」按下去。
    /// 目标还在 ⇒ 走**同一条** `startQuickPlay()`（§5.1：完整校验 / 登录 / Java /
    /// 重复运行提醒一道都不绕过）；否则就是把「已安装版本」切到那一份、点启动。
    void onHomeRecentContinueClicked();
    /// 同上，那颗「实例设置」：开**账上那个实例**的设置页。
    void onHomeRecentSettingsClicked();

    /// 账本**内存里那一份**（`load()` 读过一次之后就只有 `appendLaunchRecord()` /
    /// `finishLaunchRecord()` 会动它）。
    ///
    /// ★ 不每次现读盘：主页那张卡要在"启动成功""游戏退出"两个时机重画，而那是**热路径**
    ///   （启动那一发已经在同步等进程了）。读一次、写的时候顺手更新内存那一份。
    QList<LaunchHistory::Record> m_launchHistory;

    /// 上一次点启动的时刻，按 **身份 → 版本 → 毫秒**（`QDateTime::currentMSecsSinceEpoch()`）。
    /// 只增不减（没人清它）：条目数 = 这台机器上"点过启动的 (身份, 实例) 组合"数，
    /// 几十项封顶，清不清都一样。
    QHash<QString, QHash<QString, qint64>> m_lastLaunchAttempts;

    /// 「加入选中的世界 / 加入服务器」排上的那一发（2026-10-10）。
    ///
    /// **一次性**：`onStartButtonClicked()` 开头把它**取走并清掉**（不是在末尾清）—— 那
    /// 一路上有七八个早退（版本 json 缺了 / 启动提醒被取消 / Java 对不上的框里点了取消…），
    /// 每一个早退都得让"这一次要进某个世界"跟着作废。否则用户取消掉那个启动提醒、过一会儿
    /// 自己点一下启动，游戏会**莫名其妙地进一个他早就放弃的世界**。
    ///
    /// 空值 = 没有这一发（绝大多数时候），命令行上因此多不出任何东西。
    QuickPlay::Target m_pendingQuickPlay;
    /// 分组归属用哪个键认这个实例：有稳定 id（wtfml.meta 的 instanceId）就用它，
    /// 没有就退回**目录名**。
    ///
    /// NOTE: 读归属和写归属必须都走这一个函数。分开写的话，"读的时候按目录名找、
    ///       写的时候按 id 存"会让归属在存下去的那一刻就对不上，而且不报任何东西 ——
    ///       表现是"归好组、重开启动器，它又出来了"。
    QString instanceKey(const QString &versionId) const;
    // 重灌所有"本地已安装实例"的看法：主页那一对（下拉框 + 列表）、实例页右边那张
    // 列表、以及实例页顶上的"当前文件夹"那句。扫的是**当前游戏文件夹**的
    // .minecraft/versions，同步返回。保住用户原来选中的那一项的逻辑在 fillVersionCombo() 里。
    void refreshInstalledVersions();
    /// 实例页那张树重灌一遍 —— **搜索与筛选走的就是这一条**（coding plan §7.2）。
    ///
    /// 它和 `refreshInstalledVersions()` 是两条路：后者要**扫盘**（每个实例读一次
    /// `wtfml.meta` 那份版本信息），这一条只用上一次扫出来的那一份
    /// （`m_instancePageParts` + `m_instancePageMarks`）。用户在搜索框里打字、点一下
    /// 筛选 chip 走的是这一条 —— 每敲一个字扫一遍盘，真机上 61 个实例会明显卡手。
    ///
    /// 【§7.2 那两句话落在这儿】
    ///
    ///   * 「搜索不改变分组归属」：归属照旧由 `homeGroupIdOf()` 答（它只认
    ///     `wtfml.folders` / 版本类型），筛子只决定**哪些实例进桶**，一个字节都不写盘。
    ///   * 「清空搜索恢复原展开状态」：筛着的时候每一组**都摆成展开的**（不展开的话，
    ///     命中条目躲在收起来的组里 = 用户看着像"搜不到"），但那个展开状态**不落盘** ——
    ///     清空之后各组回到 `wtfml.folders` 里记着的样子。
    void rebuildInstanceTree();
    /// 此刻的搜索/筛选条件（读界面上那个搜索框和四颗 chip）。
    InstanceFilter::Query instanceFilterQuery() const;
    /// 一个实例参与筛选的那几样**事实**：可搜索的串（`InstanceFilter::textsFor()`）、
    /// 有没有加载器、有没有更新、账本上有没有它。**后两样只有这一层知道** ——
    /// `m_instanceOutdatedPaths` 和 `m_launchHistory` 都住在这儿。
    InstanceFilter::Facts instanceFilterFacts(const VersionDisplayParts &part) const;
    // 重灌实例页左边那张「游戏文件夹」列表（每一行：上面写 customname、下面写路径、
    // 行尾一颗齿轮），并把选中项停在 FolderManager 的当前目录上（界面那边挡了信号：
    // 重灌不是用户改选，不该反过来触发一次切换）。
    void refreshFolderList();
    // 「移除文件夹」能不能点：自带那份移不掉，另外下载期间整列都不给动。
    void updateFolderButtons();
    /// 某个游戏文件夹在列表**上面那行**显示什么：用户起的 customname → 自带那份那个
    /// 固定说法（「当前文件夹」）→ 目录名。**唯一出处**：灌列表和重命名对话框正文里
    /// 都读它（两处各算一遍的话，对话框里写着"现在显示的是 A"而列表上写着 B）。
    QString folderTitle(const QString &path) const;
    /// 行尾那颗齿轮弹出来的菜单里某一项被点了（@p action 是 MainWindowUI::FolderAction）。
    /// 界面那边只知道"第几项"，翻译成具体做哪件事是这里的事。
    void onFolderActionRequested(const QString &path, int action);
    /// 「刷新」：重新扫一遍这一份文件夹。它是当前在用的那一份时，右边那张实例树跟着重扫。
    void onFolderRefreshRequested(const QString &path);
    /// 「重命名」：改的是**显示用的名字**（FolderManager 里那份 customname），
    /// 磁盘上的目录名一个字都不动。
    void onFolderRenameRequested(const QString &path);
    /// 「打开」：拿系统的文件管理器打开那个目录。
    void onFolderOpenRequested(const QString &path);
    /// 「移除」/底下那颗「移除文件夹…」按钮共用的那一段：只从列表里拿掉，盘上不动。
    void removeFolderFromList(const QString &path);
    /// 「删除」：**连盘上的目录一起删掉**（红色那一项，不可恢复，先弹确认框）。
    void onFolderDeleteRequested(const QString &path);
    // 把 FolderManager 的当前目录交给 AppPaths（此后所有 <数据目录>/... 都对着它），
    // 并打一行日志。换文件夹的那条链路只有这一处改全局路径。
    void applyCurrentFolderToPaths();
    /// 重灌版本树那四条分类（以及它们底下的版本号）。在线清单是它唯一的数据源，
    /// 清单还没拉回来时四条都空着（**不消失** —— 用户要的是四条一条不少），
    /// 展开状态和选中项在重灌之后尽量还原。
    void refreshAvailableVersions();

    // ---------- 安装页（版本树的下一层）----------
    /// 这份要装的实例默认叫什么：目录名的形状照抄真机上那些实例
    /// （`1.18.2-Forge_40.3.12`），没选加载器时就是原版号本身。
    QString defaultInstanceName() const;
    /// 现在选中的是哪一条（modLoaderOptionNames() 的下标；不会出现 -1 ——
    /// 那个「不装（纯原版）」就是"没选加载器"这个状态本身）。
    int selectedLoaderIndex() const;
    /// 那棵树里某一条加载器的条目（顶层第 @p loaderIndex 条）；找不到回 nullptr。
    QTreeWidgetItem *loaderItem(int loaderIndex) const;
    /// 某一条加载器"钉住"的那一格（它在树上点的那一条）。三种可能，别当成"一定是版本号"：
    /// 一个**具体版本号**、那两档的键（`@latest` / `@stable`，见 LoaderMeta::loaderChannelKey）、
    /// 或者**空串**（没挑过 —— 数据层按稳定档算，和改动前"让安装器按老规矩挑"一个意思）。
    QString pinnedLoaderVersion(int loaderIndex) const;
    /// 刷新那棵树的样子：哪一条选中了、其余几条写着"与 X 不兼容"、选中的那条第二行
    /// 写着它选的版本号、以及**没选中的那几条加载器变灰**（用户原话："选择以后让其他
    /// 选项变成不可用（不能点开）的模式"）。忙态变化之后必须重新调一次（装到一半时
    /// 整棵树是灰的）。
    ///
    /// NOTE: "选中了哪一条"**不在树上**（QTreeWidget 的选中项是"光标停在哪"，
    ///       和"我要装哪一个"是两件事）—— 它住在 m_selectedLoaderIndex 里，这里是唯一
    ///       把它画出来的地方。
    void updateLoaderRows();
    /// 选中某一条加载器：改选中项、刷那棵树。走这里的是**点一条版本号**（选中它爹那条
    /// 加载器）和**点「不装（纯原版）」**那一条 —— 点一条加载器**不再**选中它，那一下只
    /// 展开/收起（用户原话："点一下，展开，再点一下 关闭"）。
    /// 它在树上**不**负责展开（展开折叠是点击那一下自己的事），也**不**发任何请求 ——
    /// 版本列表是进安装页时一次性全问好的（见 requestAllLoaderVersions()）。
    void selectLoader(int loaderIndex);
    /// 四个加载器能装哪些版本，**进安装页时一次性全问出去**（用户原话："获取列表不能
    /// 点开了再获取，得全获取完"）：先给每条摆一个「正在获取…」占位，再各发一发请求。
    /// 四条各占一格，所以四发真能同时在飞（见 VersionManager::m_loaderVersionRequests）。
    void requestAllLoaderVersions();
    /// 把一份版本列表灌成某个加载器的子条目。
    /// @param includeChannels 要不要头两条那两档（「下载最新版（alpha/beta）」/
    ///        「下载稳定版（release）」）。**"这一版一个能装的都没有"的时候要传 false**
    ///        （用户原话："如果对应版本不支持，就不要保留自动了"）—— 理由见
    ///        onLoaderVersionsFetched() 里那段（留着它等于让用户挑一条必然装不出来的路），
    ///        那种情况下 versions 也一定是空的。列表**没取到**时传 true：那两档不依赖这份
    ///        列表，所以列表没到手也得让它们能点。
    /// @param versions 每一条都带着 `stable` —— 稳定那一行灰不灰、哪几条标「测试版」都看它。
    void fillLoaderVersionItems(int loaderIndex, const LoaderMeta::LoaderVersionList &versions,
                                bool includeChannels);
    /// 名称框里的默认名（用户没自己改过时才动它），再刷下面那句小字。
    void updateInstallName();
    /// 名称框下面那句小字 + 「安装」按钮的可用性：名字合不合法、盘上有没有重名的
    /// （重名**只提示、不拦** —— 同名就是同一个实例，再装一次是校验并补齐它）。
    void updateInstallNameHint();
    /// 这次安装要哪个 Java 去跑外部安装器（Fabric 用不上，直接回 true）。
    /// @param out 成功时写入；失败时**不碰**（调用方那份值保持不变）。
    /// @return 挑得出来返回 true；挑不出来返回 false（里面已经弹窗说清原因了）。
    ///
    /// 拆出来是因为"装一个带名字的实例"和原来那条路都要它 —— 那段判据（读版本 json 的
    /// javaVersion、按它挑运行时）抄两遍的话，早晚有一处忘了跟着改。
    bool pickInstallerJava(const QString &gameVersion, int loaderIndex, QString *out);
    // 启动时把"上次启动过的版本"选回来。**只在构造函数里调一次**——
    // 名单每次刷新（清单到达、下载完成）都调它就会变成"用户手选完又被抢回去"。
    // 记着的那个版本已经不在磁盘上时留空不选，占位文案会说明该怎么选。
    void restoreLastLaunchedVersion();
    // 下载期间禁用这些：安装页那一整层（名称框、加载器树、安装按钮、返回）、左栏那份
    // 文件夹列表、「导入整合包…」按钮，并把底栏中间那格的进度条切成"正在准备下载"的
    // 不确定态。进度条和底栏的显隐不在这里定
    // （取决于"在下载"和"在启动"两条），统一由 updateBottomBar() 说了算。
    //
    // NOTE: 这条注释改过两次，两次都是"注释写着禁、代码故意不禁"：
    //       ① 以前还点着一个"下载版本"按钮和"两个版本下拉框" —— 都是上一版界面的东西了
    //          （「下载版本」已经没了，下载页现在是"点版本号进安装页"；下拉框也只剩
    //          "已安装版本"那一个）；
    //       ② 以前还把**启动按钮、「已安装版本」下拉框、实例页那棵树**列在"禁用"里 ——
    //          2026-09-23 多账户多实例那批**明确反过来**了（见 setDownloadUiBusy() 里
    //          那段"这几项故意不灰"），理由是"在下载"和"能启动什么"是两件事，正在装的那个
    //          按版本从列表里滤掉就够了。
    //       清单在下面实现里逐条写着，要核对就照着那些 setEnabled() 看。
    //
    // NOTE: Q_INVOKABLE 只为一件事加的：让 build/verify/mw_check 能在**不联网、不起进程**
    //       的前提下把界面摆进"正在下载"这个态。底栏那格"被启动借走之后要原样还回来"这条
    //       规矩只有在"在下载"时才成立，而它以前错过一次（见 updateBottomBar() 里那段
    //       NOTE）；不把这个态摆得出来，那条规矩就只能靠人眼盯。
    //       全项目就这一个 Q_INVOKABLE —— 它把"私有"这层壳朝元对象系统开了一个口子，
    //       别再开第二个；真要被别处调的接口应该在头文件里堂堂正正地公开。
    Q_INVOKABLE void setDownloadUiBusy(bool busy);
    // 进/出"同步阻塞"那一段：启动这条路上有三段是在 UI 线程上同步等的（换令牌的网络
    // 请求、挑/下 Java、startGame() 里等进程起来）。那几段里 Qt 不重绘、不响应输入，
    // Windows 过几秒还会给窗口标题挂上"(无响应)" —— 用户点了「启动游戏」看到的就是
    // "卡死"。没有可测量的进度可报，进之前先把"在等什么"写到底栏那格、套上忙指针、
    // 强制刷一次。
    // NOTE: 里面那次 processEvents() **排掉了用户输入**，所以不会把"在这期间的第二次
    //       点击"送回来、把这段路重走一遍。
    void enterSyncWait(const QString &statusText);
    void leaveSyncWait();
    // 把"启动游戏"按钮的文字刷成两行：第一行固定是"启动游戏"，第二行是「已安装版本」
    // 下拉框当前选中的**显示名**（可能带自定义名/加载器名，不是版本号；没选时是
    // "未选择版本"）。顺带把裸版本号的 tooltip 挂到同一个下拉框上。见 .cpp 里的说明。
    void updateStartButtonText();
    void updateDownloadProgressUi(int done, int total, int failed, const QString &currentFile);

    // ---------- 主页右栏那个小人 + 底栏头像 ----------
    /// 开窗 / 登录成功之后走一遍：**先默认外观兜底**，再拿缓存档案里的贴图（命中缓存时
    /// 是同步的，所以开窗那一瞬间就是上次的样子），最后后台刷一次档案。
    void startPlayerSkinLoad();
    /// 要一张贴图。@param allowDownload false（当前是离线账号时）**只认已经在缓存里的那一份**
    /// —— fetchTexture() 在缓存不命中时会真的去下一个，那正是"离线"要求别做的事。
    /// @return 这一张是不是**已经在手上了**（缓存命中）。不是"发出去请求了"。
    bool requestSkinTexture(const QString &url, bool allowDownload);
    // ---------- 「账号与外观」那个窗 ----------
    /// 把这个窗建出来并接好线（幂等：已经建过就什么都不做）。
    void setupAccountSkinDialog();
    /// 刷新这个窗里的三样东西：账号列表（本地）、3D 预览（走和主页同一条路）、
    /// 披风列表（**要令牌**，所以这一步会联网）。窗还没建时什么都不做。
    void refreshAccountSkin();
    /// 这个窗顶部那句话应该写什么（当前身份）。**唯一一处**决定它 —— 离线身份、未登录、
    /// 正版三种状态要是散在各条路上，迟早出现"底栏说离线、这里说正版"。
    QString accountSkinStatusText() const;

    /// 这一趟启动走的是不是**离线身份**。
    ///
    /// **唯一出处**：当前账号本身是不是一个离线账号（盘上活动槽那一项）。底栏那行身份、
    /// 启动时要不要令牌、外观这条路联不联网，读的都是它 —— 三处各判一次的话，就会出现
    /// "底栏说离线、启动时却去要凭据"。
    ///
    /// NOTE: 2026-09-24 之前判的是主页那个「离线模式」勾选框。用户把那个勾选框去掉了
    ///       （原话：「现在不要启动界面那个离线登录勾选框了，在添加账户那里选择添加离线
    ///       账号」）—— 于是"离线"只剩一种意思：**用一个离线账号**。勾选框能表达的那一种
    ///       额外状态（"有正版账号，但这次离线启动"）随之没有了。
    bool offlineIdentity() const;

    /// 右栏那句话现在是哪一档（未登录 / 正在取 / 正版外观 / 取不到）。
    ///
    /// **唯一一处**决定它写什么。四句文案要是散在四条路上（开窗、拿到档案、拿到贴图、
    /// 失败），迟早出现"皮肤换好了、话还停在上一句"。
    void updateSkinHint();

    MainWindowUI *m_ui;
    // NOTE: 声明顺序就是构造顺序。m_settings 必须排在 m_folderManager **前面** ——
    //       后者在构造函数里就要读盘（「游戏文件夹」列表），晚于它构造的话拿到的
    //       指针是空的，读出来就是"一份文件夹都没添加过"。
    LauncherSettings *m_settings;
    LauncherManager *m_launcherManager;
    AuthManager *m_authManager;
    FolderManager *m_folderManager;
    VersionManager *m_versionManager;
    /// 下载页那几类资源的取数（搜索 / 版本列表 / 图标）。**异步**的 —— 搜索框里连敲几次
    /// 回车也不会把界面卡住（见 ResourceStore.hpp 文件头那段）。
    ResourceStore *m_resourceStore;

    // ---------- 「服务器列表」那一趟测速（2026-09-27）----------
    //
    /// 一趟测速的状态。
    ///
    /// ★ 为什么要有这么一个东西：测速从 2026-09-27 起跑在**后台线程**上、一台一台排队
    ///   回来，而结果回来的时候"用户还在不在那一页""那一页还在不在"都是不确定的
    ///   （这几秒里他完全可能把设置页关掉、去点别的东西）。所以：
    ///     * `dialog` 是**弱引用** —— 关掉之后它是空的，那些回来的结果就只写缓存、不碰界面
    ///       （往一个已经析构的对话框上写是 use-after-free，不是"多画一笔"）；
    ///     * `fresh` / `cached` 的键是**服务器在 servers.dat 里的下标**（`Pinger` 的信号
    ///       带的就是那个下标），所以"哪一台问回来了"和"哪一台还只有上次那份"分得清。
    struct ServerPingRun {
        QPointer<InstanceSettingsDialog> dialog;
        /// 地址表（就是发给 Pinger 的那一份），下标 ↔ servers.dat 里的行。
        QStringList addresses;
        /// 这一趟是**哪个实例**的。只为一件事：`addresses` 一样、实例不一样时（两台服务器
        /// 列表碰巧相同）日志里能说清是哪一趟 —— 摆到界面上按的是地址表，不看这个。
        QString versionId;
        /// ★★ 2026-09-27 加：这一趟跑着的时候用户又问了**另一份地址表**（换了实例、改了
        /// servers.dat 再按「刷新」…）时记在这儿 —— 那一刻不能改账，因为这一趟剩下的结果
        /// 还在队列里、按下标写的是**这一份**表（见 pingServers 里那段）。等这一趟收摊
        /// （onServerPingAllFinished）才照这份表真开一趟。
        ///
        /// NOTE: 空 = 没有欠着的活（绝大多数趟）。
        QStringList pendingAddresses;
        QPointer<InstanceSettingsDialog> pendingDialog;
        QString pendingVersionId;
        /// 盘上缓存里"上一次问到"的那一份（键是下标）。**只在 fresh 里没有它时才用**。
        QHash<int, ServerStatusCache::Entry> cached;
        /// 这一趟真的问回来的（键是下标，同一台问了第二次也覆盖它）。
        QHash<int, ServerPing::Status> fresh;
        /// 现在算不算"有一趟在跑"。**这一位是权威**（`Pinger::running()` 只说线程在不在，
        /// 而"线程跑完了、结果还在排队"的那一小会儿它已经是假了）。
        bool active = false;
    };
    ServerPingRun m_serverPing;
    /// 跑那一趟的东西（见 ServerPing::Pinger）。**整个窗口只有一个**：同一时刻只跑一趟，
    /// 连点两下不会发两遍请求（start() 自己那条"已经在跑就不重开"）。
    ServerPing::Pinger *m_serverPinger;

    // ---------- 「文件管理」那几页的图标（2026-09-27）----------
    //
    /// 一趟图标反查的状态。形状和上面那趟测速**故意是一样的**（弱引用 + 后台线程 +
    /// 结果回来时"那一页还在不在"不确定），理由也逐字相同。
    struct InstanceIconRun {
        /// 现在开着的那一页。**弱引用** —— 关掉之后它是空的，回来的图标就只进缓存、
        /// 不碰界面（往一个析构了的对话框上写是 use-after-free）。
        QPointer<InstanceSettingsDialog> dialog;
        /// 这一趟列出来的候选文件（绝对路径）。回灌那一步走的就是它 —— 只摆这一份里的，
        /// 而不是把整张会话缓存（可能上千条）挨个问那六页一遍。
        ///
        /// NOTE: 这是"**现在这一页**是谁"的快照，每次 requestInstanceFileIcons() 都整个换掉。
        ///       **别拿它当"某一趟问了哪些文件"用** —— 那个是下面 m_instanceIconRunPaths，
        ///       两者在这一页被换掉之后就不一样了。
        QStringList paths;
        /// 这一页是**哪个实例**（versionId）。只为一件事：这一趟在跑的时候用户换了一页
        /// 的话，收尾时看得出"现在这一页已经不是这一趟问的那一页"，得给新那页补一趟。
        QString versionId;
        /// `Hit::iconCacheId()`（带来源前缀）→ 认得出这个资源的那些**文件**。
        ///
        /// 反过来的方向（一个文件 → 它的 iconId）在下面那张表里；这一张是给
        /// `onResourceIconReady()` 用的：图标是按身份回来的，而屏幕上要换的是**行**。
        /// NOTE: 跨对话框累积（同一个资源在别的实例里也有，那张图本来就该两边都画上）。
        QHash<QString, QStringList> pathsByIconKey;
        /// 现在算不算"有一趟在跑"。**这一位是权威**（见 requestInstanceFileIcons 里那道闸）。
        bool active = false;
    };
    InstanceIconRun m_instanceIcons;
    /// ★ 2026-09-27 加：**这一趟真的问了哪些文件**（绝对路径），以及是给哪个实例问的。
    ///
    /// 为什么不读 `m_instanceIcons.paths`：那是"最后一次请求"换上去的那一份，而
    /// requestInstanceFileIcons() 里换它的那一句排在 `active` 闸**之前** —— 于是"A 的线程
    /// 还在读盘、用户又开了 B 的设置页"这一形态下，A 的结果回来时那张表**已经是 B 的了**。
    /// 以前负缓存（m_instanceIconMissing）正是照那张表写的：A 那一趟的"没找到"被逐条记到了
    /// **B 的文件**头上，而且那张表除析构外**从不清理** ⇒ B 这一整个会话都看不到图标，
    /// 连"再点一次刷新"都没用（下一次请求在同一个假钥匙上照样跳过）。按趟记账之后，被记的
    /// 永远是**真的问过**的那一批。
    QStringList m_instanceIconRunPaths;
    /// 这一趟是给哪个实例问的（同上；收尾时拿它和 `m_instanceIcons.versionId` 比一比，
    /// 不一样就说明用户中途换了页，得补一趟）。
    QString m_instanceIconRunVersionId;
    /// 跑那一趟的线程。`QThread::create()` 出来的（省掉"再写一个 QThread 子类"，
    /// 同 ServerPing::Pinger 里那条 NOTE）。跑完就 deleteLater（见 .cpp 里那段）。
    QThread *m_instanceIconThread = nullptr;
    /// **会话级**的真图标缓存，键是 `instanceIconCacheKey()`（路径 + 大小 + 时间）。
    ///
    /// 为什么要有（而不是"每次都重新反查"）：那张图是一次读盘 + 联网换来的，而用户关掉
    /// 设置页再打开是很平常的一件事（改个开关就要进去一次）—— 每次重来一遍的话，一列
    /// 几十个 jar 要重新读一遍（几百 MB），而且那几秒里图标是占位图。值**可以是空
    /// pixmap**（见 m_instanceIconMissing 那段）。
    QHash<QString, QPixmap> m_instanceFileIcons;
    /// 问过了、**没有**图标的那一批（同一把钥匙）。存它只为一件事：别把同一个文件问第二遍。
    ///
    /// ★ 只有"这一趟**完整跑完**了"才会往里写（`Result::problem` 空，见
    ///   onInstanceIconLookupFinished）。断网那一趟要是也记成"没有"，用户这一整个会话就
    ///   再也看不到图标了 —— 而那本来是"网好了就会有"。
    QSet<QString> m_instanceIconMissing;
    /// 认出来的那几条**是哪个资源**：同一把钥匙（路径 + 大小 + 时间）→ `Hit::iconCacheId()`
    /// （`<来源>-<项目号>`），而那一份 Hit 就存在下面的 `m_resourceHits` 里。
    ///
    /// ★ 2026-09-27 补，为的是"点名字进它自己的那一页"（见 openInstanceFileResource()）：
    ///   那件事要的正好就是下载页点一行要的那几栏（标题 / 简介 / slug / 游戏版本），
    ///   而它们**本来就在手上** —— 反查那一趟早就把整个项目对象问回来了（见
    ///   `InstanceIconLookup::Found::hit`），只是从前读完图标地址就把其余几栏丢了。
    ///
    /// NOTE: 和 m_instanceFileIcons 同一把钥匙 ⇒ 同一个名字上换了一个文件就自然作废，
    ///       不会出现"词条写着 A、名字是 B"。
    /// NOTE: 键是**路径的钥匙**而不是路径，所以文件删掉之后这一条留着也不会有人读到它
    ///       （pushInstanceFileIcons() 只走 `m_instanceIcons.paths` 里那些还在的路径）。
    QHash<QString, QString> m_instanceFileIdentities;
    /// ---------- 实例更新检测那一趟（2026-09-28）----------
    ///
    /// 一趟的形状：枚举这个实例的 mods → 后台线程反查一次（`InstanceIconLookup`，顺手
    /// 带出每个文件的 sha1）→ 有几个项目就再问几发版本列表（**新的在前**，按这个实例的
    /// 游戏版本 + 加载器筛）→ `ModDependencies::judgeUpdate()` 逐条判 → 一份报告。
    struct UpdateCheckRun {
        /// 这一趟查的是哪个实例（回来时按它认人：用户在等着的时候完全可能换了实例设置页）。
        QString instanceId;
        /// 还差几发没回来。**只有它到 0 才收尾** —— 少等一发的话，报告里会凭空少几条，
        /// 而屏幕上一点异常都没有（"这个模组已经是最新的"和"没查到"是两句话）。
        int pending = 0;
        /// 钥匙（`versionsRequestKey`）→ 等这个答案的那几条文件。
        ///
        /// 为什么要按钥匙分组、而不是"一个项目一个条目"：同一个资源在 mods/ 里摆两份是
        /// 常事（`X.jar` 和 `X.jar.disabled` 也在候选里），两份**同一次答案就能判**，
        /// 所以一发请求判两条。**反查那一趟回来之后才有它**（那时才知道哪个文件是哪个项目）。
        QHash<QString, QList<ModDependencies::InstalledItem>> itemsByKey;
        /// 还没回来的钥匙（到空 = 收尾）。★ **少等一发就会少几条** —— 报告里那几条会
        /// 凭空消失，而屏幕上一点异常都没有（"已经是最新的"和"没查到"是两句话）。
        QSet<QString> waitingKeys;
        /// ★★ 2026-10-11：**Modrinth 认出来的那些**（小写 sha1 → 等这个答案的那几条文件）。
        ///
        /// 它们**不走上面那些单发请求** —— 那一拨走**批量那一发**（`POST
        /// /version_files/update`，一次带一串 sha1，见 `ResourceStore::fetchLatestVersions()`）。
        /// 缘由是用户 2026-10-11 报的「无法正常获取更新」：一页几百个模组各问一发版本列表，
        /// 实测**整批 429**（Cloudflare 的 `error code: 1015`），报告里于是每条都是"说不清"。
        /// 批量那一发把 295 个模组变成 3 发。
        ///
        /// NOTE: 同一个 sha1 底下可能挂着**两条路径**（`X.jar` 和 `X.jar.disabled`）——
        ///       那一发只问一次，两条照同一份答案判。
        ///
        /// ★★ 2026-10-11：外面**又多了一层按钥匙分组的表**。缘由是同一趟里能出现**两把
        ///   批量钥匙**：模组那一拨要带实例的加载器（`fabric`），资源包 / 光影包 / 数据包
        ///   那一拨**一个加载器都不许带**（见 `ModDependencies::kindUsesLoader()` 那段）。
        ///   只按 sha1 分组的话，两拨只能合成一发 —— 而那一发带不带加载器都是错的：
        ///   带上，资源包那 27 条全判不出来（用户报的就是这个）；不带，模组的"最新"
        ///   可能是另一个加载器的 jar。
        QHash<QString, QHash<QString, QList<ModDependencies::InstalledItem>>> batchItems;
        /// 批量那一发的钥匙 → 发那一发时要递回去的**加载器**（空串 = 不带这一格）。
        /// 刻意**不放进钥匙里再拆**：拆字符串的写法一改就静默对不上（同 `versionsKeyFor`
        /// 那段说的道理），而从 `batchItems` 里读第一条也在读同一个值 —— 存一份最省事。
        QHash<QString, QString> batchLoader;
        /// 批量那些发还没回来的（**空 = 都回来了**）。**和 `waitingKeys` 一样是收尾判据的
        /// 一部分** —— 少看这一位的症状是报告早出来一步（那一拨一条都不在里面）。
        QSet<QString> pendingBatchKeys;
        /// 批量那几发**一共答出来过哪些 sha1**（跨所有批量钥匙攒的一份）。
        ///
        /// 只给"退回问 CurseForge"那一拨当判据用（"Modrinth 到底答没答过这一个哈希"）——
        /// 批量分了几发之后，单看某一发的回复就判不全了，见 `onUpdateCheckLatestFinished()`
        /// 里那两段。
        QSet<QString> batchAnswered;
        /// 这一个文件的路径在哪一类目录下（`kindUsesLoader()` / 后面那些按类分岔的判据的
        /// 输入）。**反查那一趟回来时按它认回类别** —— 那趟只回一个路径，而"这个路径属于
        /// 哪一类"是**收集候选文件那一步**就已经知道的事（目录就是按类走出来的）。
        QHash<QString, ResourceCatalog::Kind> pathKinds;
        /// ★★ 2026-10-11：**两家都认出来的那几条，押着等 Modrinth 先开口**（CurseForge
        /// 的 `versionsRequestKey` → 那几条文件）。**这一栏里的还没发出去**。
        ///
        /// 缘由：`InstanceIconLookup::lookup()` 对**同一个文件**在两家都认出来时会摊成
        /// **两条**记录（一条 Modrinth、一条 CurseForge，路径逐字同一个）—— 实测用户那个
        /// 整合包 352 个模组里 **271 个都是这样**。从前那两条各问一发、各判一条：报告里同一个
        /// 模组**出现两行**（还可能一行"有新版本"、一行"已是最新"—— 两家的版本列表本来就不
        /// 是一套），"检查了 N 个资源"那个数也跟着翻倍（352 个模组报 623 个）。
        ///
        /// 现在**一个文件只查一家**：两家都有时走 Modrinth（这一栏就是等它先答），只有
        /// CurseForge 认出来的那 80 个照旧直接问 CurseForge。而 Modrinth 那一发**没给出
        /// 这个 sha1 的答案**时（作者只往 CF 传过那一版 / 这个哈希不在 Modrinth 的表里），
        /// 这一栏**退回问 CurseForge** —— 见 `onUpdateCheckLatestFinished()` 那一处。
        ///
        /// NOTE: 这一栏非空 ⇒ `batchItems` 非空（能押在这儿的前提就是"同一个文件 Modrinth
        ///       也认出来了"，那种文件一定在 `batchItems` 里）—— 所以"只有它非空才收尾"那句
        ///       判据（`pendingBatchKeys` 非空）一定立着。
        QHash<QString, QList<ModDependencies::InstalledItem>> fallbackItems;
        /// 已经判出来的（收尾时按它排序写报告）。
        QList<ModDependencies::UpdateVerdict> verdicts;
        /// ★ 2026-10-09 加：**判成"可更新"的那几条，它们各自该换成哪一版**（盘上路径 →
        /// 最新那一版**整个条目**）。
        ///
        /// 为什么要整条：文件管理那几页那个悬浮条上的「更新」要把这一批一次下完，而
        /// `downloadResourceFiles()` 要的是 URL / 文件名 / sha1 / 大小 —— 那些全在
        /// `VersionEntry::files` 里。报告里那三行字（版本号 / 文件名 / 落后几版）**不够**。
        ///
        /// NOTE: 值就是 `judgeUpdate()` 看的那一份 `versions.first()`（判"最新"和拿"最新
        ///       那一版"必须是同一句话，不另算一份）。
        QHash<QString, ResourceCatalog::VersionEntry> latestByPath;
        /// 反查那一趟自己的问题（读不动几个文件之类），报告末尾照实说一句。
        QString lookupNote;
        /// 点「检查更新」那一颗按钮时开着的那一页。**弱引用** —— 结果回来的时候这一页
        /// 完全可能已经被收掉了（那几发是异步的），那时候**不碰它**（往一个已经析构了的
        /// 对话框上写是 use-after-free，不是"多画一笔"；同 ServerPingRun::dialog 那段）。
        ///
        /// ★ 2026-09-28 加：这一趟的结果除了弹那份报告，还要喂给文件管理那几页的
        ///   「可更新」那一格（见 InstanceFilesPage::setUpdateResult）。
        QPointer<InstanceSettingsDialog> dialog;
        /// 跑着的时候就把重活派出去的那个线程（跑完 deleteLater，见 startInstanceFileIconLookup
        /// 那套写法）。
        QThread *thread = nullptr;
    };
    UpdateCheckRun m_updateCheck;
    /// **会话级**：每个实例上一趟查出来**可更新**的那些路径（键是实例 id）。
    ///
    /// ★ 2026-09-28 加的一项用途：文件管理那几页状态栏里的「可更新」靠它筛（见
    ///   InstanceFilesPage::setUpdateResult）。不记的话，用户查完关掉设置页再进来，
    ///   那一格就永远是"还没查过"—— 而那趟检测刚跑完。
    ///
    /// NOTE: **键在不在 = 查过没有**（值是空表 = 查过了、一条都没有）。这两件事在界面上
    ///       是两句话（"先点一下检查更新" / "都是最新的"），所以不能只留一份名单。
    /// NOTE: 和别处那几份会话缓存一样**只往里放、不清理**：键是实例 id、值是纯路径，留着
    ///       不占多少，而"哪几个实例该清"没有出处（文件删了就自然筛不出来了 —— 那一条
    ///       已经不在这一页上了）。
    QHash<QString, QStringList> m_instanceOutdatedPaths;
    /// **会话级**：和上面那份名单**一起写、一起失效** —— 实例 id → （盘上路径 → 该换成
    /// 的那一版）。上面那份回答"哪几条要更新"（界面拿它筛「可更新」那一格、算计数），
    /// 这一份回答"换成哪一版"（悬浮条上那颗「更新」拿它拼要下的文件清单）。
    ///
    /// ★ 2026-10-09 加（需求 2 的批量更新）。**不在 `InstanceFilesPage` 里存**：那一层
    ///   不 include src/，`ResourceCatalog::VersionEntry` 是下载那一侧的类型；而且它也没
    ///   地方去拿 —— 唯一的来源就是这趟检测。
    ///
    /// NOTE: 和上面那份一样**只往里放、不清理**。刷新时机只有一处 = 下一次检测收尾
    ///       （`finishUpdateCheck()` 里那一句 insert，和名单同一句旁边）。用户更新完之后
    ///       这一格仍然是旧的（盘上那几个文件其实已经换过了）—— 直到他再点一次
    ///       「检查更新」。这是**刻意**的：现算一份"现在还是不是最新的"要再跑一整趟
    ///       反查 + 版本列表，而那正是他刚点过的那颗按钮。
    QHash<QString, QHash<QString, ResourceCatalog::VersionEntry>> m_instanceUpdateTargets;
    /// 实例页那张树的**原料**：上一次 `refreshInstalledVersions()` 扫出来的那份（能启动
    /// 的实例 + 每一行前面那个图标）。**只有 `rebuildInstanceTree()` 读它**。
    ///
    /// ★ 为什么要有这一格（coding plan §7.2）：搜索框每敲一个字、每点一下筛选 chip 都要
    ///   重灌一遍树，而重灌的原料（`installedVersions()` + 每实例一次 `displayParts()`、
    ///   一次 `instanceRowMark()`）全是**读盘**的 —— 真机上 61 个实例，每敲一个字扫一遍
    ///   会明显卡手。所以扫盘那一段（`refreshInstalledVersions()`）把结果留在这儿，
    ///   筛选那一段（`rebuildInstanceTree()`）只重算分组、重新灌树。
    ///
    /// NOTE: 「什么时候会变」的答案和"哪些事会叫 `refreshInstalledVersions()`"逐字同一句
    ///       —— 安装完、下载完、改名、换文件夹、换「版本名格式」…都走那条路，所以这份缓存
    ///       不会比屏幕上那一棵树更旧。
    QList<VersionDisplayParts> m_instancePageParts;
    /// 和上面那份**一一对应**的行首图标（`instanceRowMark()` 的成品）。缓存它是因为算一张
    /// 要读版本 json，而且它是"行前面那张图"，筛选前后**同一行画的还是同一张**。
    QHash<QString, QIcon> m_instancePageMarks;
    /// 「从资源详情页返回时，那一页停在哪儿」（★★ 2026-10-11）。
    ///
    /// 用户原话：「点进模组详情以后退出来保留刚才的滚动位置」。
    ///
    /// 为什么得由这一层带着走：回来时那一页是**整个重新 new 出来的**
    /// （`openInstanceSettingsNow()` 里那个栈上的对话框，收页那一刻连它一起没了），
    /// 所以 `InstanceFilesPage` 自己那份"重列不跳回顶端"的锚点跨不过这一道重建
    /// （见 `InstanceFilesPage::ScrollPosition`）。位置在**离开那一刻**量下来
    /// （`fileResourceRequested` 那一条接线里），开回来时在 `showPage()` 之前塞回去。
    ///
    /// NOTE: 一次只可能有一份 —— 同一时刻只可能开着一条资源详情页，所以不做表、就这四个
    ///       成员；`instanceId` + `pageKey` 对不上（换了实例、或者开的是另一页）就整份
    ///       作废（那种时候落回顶端才是对的），用完在 `openInstanceSettingsNow()` 里清掉。
    struct FileScrollReturn {
        QString instanceId;
        QString pageKey;
        QString identity;  ///< 最上面那一行的身份（`InstanceFilesPage::identityPath()`）
        int delta = 0;
    };
    FileScrollReturn m_fileScrollReturn;
    /// 已经问回来的版本列表，按**请求钥匙**存（ResourceCatalog::versionsRequestKey()）。
    /// 点第三层那一下要拿它换出 URL / sha1 / 大小。
    /// NOTE: 那些事实**不放在条目上**：条目在重搜时会被整个清掉，而"这一版的文件是哪几个"
    ///       本来就只属于"那一次请求"的范围。放这儿也不会留悬空指针（值是纯数据）。
    QHash<QString, QList<ResourceCatalog::VersionEntry>> m_resourceVersions;
    /// 下载页「整合包」那一页下回来的 .mrpack 落在哪 —— 下完之后要接着走导入那条路
    /// （importModpackFromFile）。键是**那一单的 label**（`downloadResourceFiles()` 收的
    /// 第三个参数，`resourceDownloadFinished` 原样带回来），空表 = 没有在等导入。
    ///
    /// NOTE: 只活一次：onResourceDownloadFinished() 里按 label `take()` 走。不 take 的话，
    ///       用户手动导入一个包之后，下一个下完的模组也会顺手再触发一次导入。
    ///
    /// NOTE: 2026-09-25 之前它是一根 `QString`，**不分是哪一单**：任何一次
    ///       onResourceDownloadFinished（普通模组、资源包、数据包…都算）都会把它读走并清掉。
    ///       整合包还在下、用户又点了个模组，模组先下完 → 那笔记录被模组吃掉，于是
    ///       ①拿一个还没下完的半截 .mrpack 去走导入（导入那条路会当场失败），
    ///       ②等整合包真正下完时记录已经没了 → 弹一句「已经装进实例里了」而**实际没装**。
    ///       按 label 键控之后，"这一单是不是在等导入的那一单"是个能判的问题。
    ///
    /// NOTE: 光键控还不够，还有**登记时机**这一半：记录只在 `downloadResourceFiles()`
    ///       回答"开起来了"之后才写（见 downloadResourceVersion）。被拒绝的那一发会带着
    ///       **同一个 label** 发一个假失败 —— 双击那一行的第二下正是这样。两半都要，
    ///       少一半就还是会被吃掉。
    ///       顺带的好处：两个整合包前后脚开始下也各自记得住（键不同，take() 各取各的）。
    QHash<QString, QString> m_pendingModpackImports;
    /// 树上每一个资源**完整**的那一条（按 `Hit::iconCacheId()` 存，也就是条目上
    /// `resourceIconIdRole()` 里那串）。
    ///
    /// 为什么条目上那四个角色还不够：点开一个资源的详情页要简介、slug（拼"转到 Modrinth /
    /// CurseForge"那条网址）、更新时间（头卡上那行元信息），而条目上扣着放不下这些
    /// （简介挂在 tooltip 上，slug 根本没写进条目）。
    ///
    /// NOTE: 和上面 m_resourceVersions 一样**只往里放、不清理**。键是 `<来源>-<id>`，值是
    ///       一条纯数据；重搜换一批之后旧的那些再也没人问（那些行已经不在了）。要清的话
    ///       得先知道"哪几条属于上一批"，而那件事现在没有出处 —— 靠"清空整张表"会在重搜
    ///       另一页时把别的页正在用的条目一起清掉（那四页各有一棵树，搜索是分开发起的）。
    QHash<QString, ResourceCatalog::Hit> m_resourceHits;
    /// 「资源详情」那一页**这次开着的时候**说的是哪个资源、列的是哪一份版本。
    ///
    /// 一个 struct 装在一起而不是散成六个成员：这六样是**同一份状态**，散开之后
    /// "开页/关页"要记得挨个改，漏一个的症状（比如 open 忘了落下去）很难认。
    struct ResourcePageState {
        bool open = false;
        /// 这一条是从哪一栏点开的。拼网址（`projectPageUrl` 要 kind）、下载时判
        /// "要不要过加载器那道闸"都要它。
        ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
        ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
        QString projectId;
        /// 在 m_resourceHits 里的那把钥匙（`iconCacheId()`）。**同时也是"这一发版本列表
        /// 回来时该不该填进页面"的判据** —— 和树那条路的请求钥匙不是一套（那边带游戏版本）。
        QString iconKey;
        /// 问版本的**请求钥匙**：游戏版本和加载器都空（= 不筛，整份列表）。
        QString requestKey;
        /// 现在按着哪一颗 chip（空 = 「全部」）。筛和分组都由 MainWindow 重算
        /// （`ResourceCatalog::groupVersions` 是那一份规则）。
        QString chip;
        /// 上方右侧那个「显示快照版」勾选框（**默认不勾**）。一位同时管三件事 ——
        /// 那一排 chip 摆哪几颗、chip 怎么筛、组头写什么（见 ResourceCatalog 那三个函数的
        /// 文件注释）；三处传的必须是**同一个值**，不然症状是「点了 26.2 少了几个版本」。
        bool showSnapshots = false;
        /// 头卡上那个资源名。下载时拼 label 的前半截要用它。
        QString title;
        /// 开页时**是从哪个实例进来的**（空 = 不是从某个实例进来的）。`from` 是
        /// `InstanceFiles` 时就是那个实例的目录名，`Download` 时是"用户上一次是从哪个
        /// 实例进下载页的"（`m_downloadFromInstanceId`）。
        ///
        /// 它只喂两件事：开页那一下 chip 预选到**哪个**游戏版本、以及哪个加载器的组排前面
        /// （见 openResourceDetailPage 里那两句）。**存下来**而不是开页时用一次就扔：
        /// 勾「显示快照版」那一下会把 chip 那一排整个重摆（`onResourcePageShowSnapshotsChanged`），
        /// 那时还得知道"预选的是哪一个"—— 否则用户拨一下勾选框，按着的那一颗就被甩回
        /// 「全部」了。
        QString instanceId;
        /// 上面那个实例的加载器，**换算成小写那一套**（`VersionGroup::loader` 的写法，
        /// 见 ResourceCatalog::groupVersions 的 preferredLoader）。
        QString preferredLoader;
        /// 开页时接口给的那一串**声明过的游戏版本**（`Hit::gameVersions`，旧的在前）。
        ///
        /// 留着它只为一件事：勾「显示快照版」那一下要把 chip 那一排整个换一份，而那时候
        /// 手上只有这一份声明表（`Hit` 存在 m_resourceHits 里，但那一份会被后来的重搜
        /// 换掉，而这一页还开着）。
        QStringList declaredGameVersions;
        /// **全量**那份版本清单（chip 只是从它里面筛）。
        QList<ResourceCatalog::VersionEntry> versions;
        /// 那份清单**回来过**了（哪怕回来的是空的）。
        ///
        /// 为什么要单记这一位："还没回来"和"这个资源一个版本都没发过"都表现为 versions
        /// 是空的，而这两件事在界面上是**两句不一样的话**（「正在读版本列表…」和
        /// 「这个资源一个版本都没发过」）。靠 versions.isEmpty() 分不开。
        bool loaded = false;
        /// chip 那一排和版本列表中间那一行说的是"哪几条前置"（2026-09-28 加）。
        ///
        /// 来源是**这一页当下摆在最上面那一版**的 `dependencies`（见 fillResourcePageGroups）
        /// —— 存下来而不是每次重算，是因为名字那一发是异步的：回来的时候"最上面那一版"
        /// 可能已经跟着 chip 换掉了，而这一发回来该填的是**发出去时那一份**。名字一到就往
        /// 这儿这一份上填（`m_projectTitles` 是先按号查、再拼字的）。
        QList<ResourceCatalog::Dependency> dependencies;
        /// 问那几个前置**叫什么**的那一发（`ModDependencies::projectsKeyFor("detail", …)`）。
        /// 空串 = 这一页这会儿没有在问名字。
        QString dependencyRequestKey;
    };
    ResourcePageState m_resourcePage;
    /// 每一页上一次**真的发出去**的搜索词（键 = 分类键）。
    ///
    /// 只为一件事：结果回来时那句「没有搜到「xx」」里的 xx 要是**那一次**的词。读编辑框里
    /// 现在的字是错的 —— 用户完全可能在等结果的时候又敲了几个字（那几下还没发出去，
    /// 回车才算一发）。
    QHash<QString, QString> m_resourceQueries;
    /// 「这个项目号叫什么」的**会话级**缓存（键 = `Hit::iconCacheId()`，也就是
    /// `<来源>-<项目号>`；值 = 名字）。两个问名字的人（前置那一趟、详情页那一行）共用它。
    ///
    /// 两个用处：① 详情页那一行每换一次 chip 就要重算一次，没有缓存的话同一个号会被反复
    /// 问（而名字是**不会变**的东西，问第二遍纯粹是白发一个往返）；② "已经知道了"和
    /// "还没问过"分得开，于是 `updateResourcePageDependencies()` 能只问缺的那几个。
    /// 键里带来源是因为两家的号不是一套（`238222` 和 `nvQzSEkH` 各自在各自的命名空间里）。
    QHash<QString, QString> m_projectTitles;
    /// 这一次进下载页是**从哪个实例的设置页**点进来的（空 = 不是从那儿来的）。
    ///
    /// 三件事读它：① 左栏顶上那颗「返回实例设置」回哪儿；② 从这一栏点下载时要不要弹
    /// explorer 选保存位置（用户 2026-09-28 那条设置的判据 —— 「下载新模组就自动放进
    /// 当前实例文件夹」，见 installResourceVersionNow）；③ 从这一栏点开一个资源的详情页时
    /// 拿它当"当前实例"（chip 预选 + 加载器排前面）。
    QString m_downloadFromInstanceId;
    /// 上面那个实例的设置页**当时停在哪一页**（= `openDownloadCategory()` 收到的
    /// `categoryKey`；空 = 同上那一位也是空的）。
    ///
    /// 只喂一件事：那颗「← 返回实例设置」回去时开在**原来那一页**上，而不是永远回「概览」
    /// （用户原话「从模组点进去，返回也在模组而非概览，其余同理」）。它和
    /// `m_downloadFromInstanceId` **同生共死** —— 那一位在哪儿被赋值/清掉，这一位就在
    /// 哪儿，所以 `returnToInstanceSettingsFromDownload()` 里两句话并排写。
    QString m_downloadFromPageKey;
    /// 「版本」那一格**刚按过回车**的那几页（键 = 分类键，进的是 `QSet`）。
    ///
    /// 只为一件事：那一格是可输入的，在框里按回车时 Qt 会连发两发 —— 先是
    /// `QLineEdit::returnPressed`（我们接的那一发），紧接着 `editingFinished`，而 QComboBox
    /// 自己接了后者、并在"框里那串字正好是清单里某一项"时补一次 setCurrentIndex + activated。
    /// 两条路都通到"搜一发"，所以按一下回车会搜两次。记的这一笔就是那道闸：**同一回合里
    /// 只认第一发**（那一笔由 0 毫秒的单次定时器抹掉，所以不会留到下一次操作）。
    QSet<QString> m_versionEnterSearches;
    GlobalJavaSettings *m_javaSettings;
    /// Java 扫描 / 下载队列。重活在后台线程执行，MainWindow 只负责展示结果。
    JavaRuntimeController *m_javaRuntimeController;
    // 实例分组（<数据目录>/wtfml.folders）。**不需要**在换游戏文件夹时重置它：
    // 它每个访问器都会比一下"这次问的是哪一份 .minecraft"，不一样就自己重读。
    InstanceGroupStore *m_groupStore;
    // 玩家外观的数据层（免鉴权问官方 + 内容寻址的本地缓存）。
    PlayerSkinStore *m_skinStore;
    // 给离线账号挑一张随机皮肤（挑的是一个地址，图还是上面那个 Store 去下）。
    RandomSkinStore *m_randomSkinStore;
    // 改外观那一层（**要令牌**：换皮肤、换披风）。和上面那个是两条路，见 SkinApi.hpp
    // 文件头那句分工。
    SkinApi *m_skinApi;
    // 「账号与外观」那个窗。**懒建**：没点过那个入口就是空的（见
    // setupAccountSkinDialog()）。它是 this 的子对象、也没开 WA_DeleteOnClose，所以关掉
    // 之后指针照样有效 —— 再点一次是把它 show 回来，不是重建。
    AccountSkinDialog *m_accountSkinDialog = nullptr;
    /// 「资源详情」那一整页（下载页点一个资源进去）。和上面那个一样：懒建、是 this 的子对象、
    /// 没开 WA_DeleteOnClose —— 关掉之后指针照样有效（见 setupResourceDetailPage）。
    ResourceDetailPage *m_resourceDetailPage = nullptr;
    /// 「Java 管理」那一整页（设置页「启动」栏那颗按钮开出来的）。同上面两个：懒建、
    /// 是 this 的子对象、没开 WA_DeleteOnClose —— 关掉之后指针照样有效。
    ///
    /// NOTE: 它和别的整页有一处不同：那一页**不长记性**（四行的内容每次开页现灌），
    ///       所以不存在"关掉之后里面的数据过期了"这回事 —— 见 openJavaManage。
    JavaManageDialog *m_javaManagePage = nullptr;
    // 现在屏幕上用的是哪两张贴图（本地路径）。**只记真的载入成功了的** —— 所以空串
    // 就等于"现在显示的是默认外观"，右栏那句话的判据之一就是它。
    QString m_skinPath;
    QString m_capePath;
    // 最近一次**要过**的那份档案。唯一的用途：贴图是按 URL 分开回来的（皮肤一个、
    // 披风一个），回来时得知道"这一张是皮肤还是披风"，也有个东西能判"和上次一样吗"。
    PlayerSkinProfile m_skinProfile;
    // 屏幕上这张脸是**按哪个实例算出来的官方默认皮肤**（空 = 不是算出来的那张：可能是
    // 随机挑的、正版外观、或者默认外观）。
    //
    // NOTE: 它有两个用处，都不是可有可无的：① 右栏那句话要在"随机挑的"和"游戏会给的"
    //       两种离线外观之间选一句说；② 用户换版本时要按新版本**重算**（两套规矩算出来的
    //       是两张不同的图，见 DefaultSkin.hpp 文件头）。存的是目录名（版本 id），
    //       显示的时候才换成显示名。
    QString m_offlineDefaultSkinVersion;
    // 随机挑中的那一张（地址 + 它该记在哪个账号名下）。**挑的那一刻不落盘** ——
    // 等贴图真的到了盘上再由 onSkinTextureReady() 记账，见那边那段。
    // 空串 = 没有待记的（正常态）。
    QString m_pendingRandomSkinUrl;
    QString m_pendingRandomSkinUuid;
    // ★ 2026-09-27 加：**发这一次请求时**当前是哪个离线账号。
    //
    // onRandomSkinReady() 原来只判 `offlineIdentity()`（"现在这位是离线账号"），而那回答的
    // 不是"是不是**同一个**离线账号" —— 从 A 切到 B（两个都是离线账号）之后，A 那一趟的结果
    // 照样通过，于是 A 挑的那张被记进 random-<B>.txt、画在 B 脸上，而 A 那边永远没有记录
    // （每次开启动器重挑一张，脸一直变）。这一位就是那个"同一性"判据的唯一出处：
    // 请求发出时写一次，回来时比一次，不一样就整份丢掉。
    // NOTE: 这一位和 onSkinProfileReady() 那边拿信号里带的 uuid 去比是**同一件事** ——
    //       那条路是信号自己带着身份回来的，这边没有，只好自己记一份。
    QString m_randomSkinRequestUuid;
    // 去官方问的这一次还在路上（决定那句话是"正在获取"还是"取不到"）。
    // NOTE: **只表示"真的发出去了一个请求"**，缓存命中不算 —— 命中是同步回来的，
    //       标成"在路上"会让那句话卡在"正在获取"上下不来。
    bool m_skinFetchInFlight = false;
    // 底栏头像那块图。**不能**在 updateBottomBar() 里现算：那个函数每次进度刷新
    // （几百毫秒一次）都会被调，现算等于一直在重渲染同一张 8×8 的皮肤。
    QPixmap m_avatarPixmap;
    // 下载是否在进行中。注意不能只问 m_versionManager：从点击"下载"到版本 JSON
    // 拉回来之前，VersionManager 那边还没有任何批次，这段时间也得算"忙"。
    bool m_downloadInFlight = false;
    // ★ 2026-10-12（coding plan §2.1、§3.1）：底栏中间那格**不再自己记**"下到哪了"。
    //   以前这里有 m_downloadDone / m_downloadTotal / m_downloadFailed 三个数，专门为了
    //   "启动那一段把这一格借走、借完照原样还回来"（所以得把上一帧记着）。现在没有
    //   "借走"这回事了 —— 启动和下载是任务表里并列的两行，谁被显示出来由
    //   `Tasks::collapsedSummary()` 现算，进度也都记在**任务自己**身上
    //   （`TaskManager::setProgress()`）。三个数因此整个撤掉：留着就是第二份真相。
    // 正在导入的那个整合包会落在哪个实例名下（空 = 没有导入在进行）。
    //
    // 为什么界面要自己记这一笔：导入的那**一个**终态走的是公共的
    // versionDownloadFinished(id, ok, msg) —— 收尾那句话是说给用户看的（"整合包「X」
    // 导入好了"），而这里要判断的是"这一条信号讲的是导入，还是普通的一次版本下载"，
    // 好把那句「版本 X 的文件已全部就绪」换成导入该说的话。数据层那边不另发一个信号，
    // 是为了让"一次点击 = 一个终态"这条约定继续保持唯一（见 importModpack 的说明）。
    QString m_importingInstanceId;
    // 正在装的那个"带名字的实例"叫什么（空 = 没有这种安装在跑）。和上面那个是同一个
    // 道理：装一个带名字的实例走的也是公共的 versionDownloadFinished(id, ok, msg)，
    // 这里要判断"这一条讲的是它，还是普通的一次版本下载"，好把那句「版本 X 的文件已
    // 全部就绪」换成"实例装好了"该说的话。
    QString m_installingInstanceId;
    // 正在**校验/补全**的那个实例（空 = 没有补全在跑）。来路和上面那个一模一样：补全走的
    // 也是公共的 versionDownloadFinished(id, ok, msg)，这里要判断"这一条讲的是补全，还是
    // 普通的一次版本下载"，好把那句「版本 X 的文件已全部就绪」换成"都齐了 / 缺哪几个"。
    // 它也进 installingVersionIds()（见那边），所以补全期间这个实例不在启动列表里。
    QString m_repairingInstanceId;
    // ---------- 安装页（版本树的下一层）----------
    // 安装页现在正对着哪个原版号（空 = 没进过安装页）。四个加载器各自能挑哪些版本、
    // 默认名怎么拼，都以它为准。
    QString m_installGameVersion;
    // 安装页那棵树上**选中**了哪一条（modLoaderOptionNames() 的下标）。「不装（纯原版）」
    // 就是"不装加载器"这个状态，所以它只有一个合法值、没有 -1 —— 每次进安装页都退回它。
    //
    // 为什么不去问那棵树"你选中了哪一条"：QTreeWidget 的 currentItem 是"光标停在哪"，
    // 用户点一下别处它就变了（而"装什么"不该跟着变）；展开/收起更是和"选中"完全无关。
    // 这条状态是产品状态，住在产品这一层。
    // NOTE: 初值在构造函数里给（`MainWindowUI::modLoaderNoneIndex()`）—— 这个头只前置
    //       声明了 MainWindowUI（见文件顶上那个 class 声明），在这儿调它的静态函数编不过。
    int m_selectedLoaderIndex;
    // 每一条加载器**钉住**的版本号（下标 → 版本号；空串 = 不钉，让安装器按老规矩挑）。
    // 没进过 map 的下标等于没钉。进安装页时整个清掉（换了一个游戏版本，上一份能装的
    // 版本就完全不是这一份了）。
    QHash<int, QString> m_chosenLoaderVersion;
    // 用户**自己改过**名称框没有。改过之后默认名就不再跟着加载器变了（不然他敲到一半
    // 的名字会被一次"选了 Forge"顶掉）；每次进安装页重置。
    bool m_installNameTouched = false;
    // "有版本目录但 jar 名对不上"这件事是否已经写过日志。refreshInstalledVersions()
    // 一次会话会被调很多次，不记一笔就是同样的告警反复刷。
    bool m_reportedUnrecognizedVersions = false;
    // 版本树上「最新版本」那一组的"默认展开"是不是已经摆过一次了（见
    // refreshAvailableVersions() 里那一格）。**只摆第一次**：用户把它收起来之后，
    // 清单刷新（每次启动那一次、以及手动刷新拉回来那一次）不该又把它撑开。
    bool mLatestGroupExpandSeeded = false;
    // 哪几份会话的启动进度还在走：从"进程真的起来了"（startGame 返回非零）到那一份自己
    // 吐出第一行输出为止 —— 中间那段是 JVM 和游戏自己在初始化，界面上没别的反馈。
    // 命名刻意不叫 m_launching：它表示的是"这段进度要显示"而不是"正在启动"。
    //
    // 存的是**会话号**不是版本号：同一个实例可以被多开，按版本号记的话第二个起来就把
    // 第一个那笔盖掉了（然后第一份出话时"启动中"照样挂着，直到它退出才收）。
    QSet<int> m_launchingSessions;
    // ------------------------------------------------------------------
    // 任务中心（2026-10-12，coding plan §2 / §3）
    // ------------------------------------------------------------------
    // **唯一**记录"现在有哪些事在跑、各自跑到哪儿了"的地方（见 src/tasks/TaskManager.hpp）。
    // MainWindow 是**唯一**往它里面记的人：`DownloadManager` / `VersionManager` /
    // `LauncherManager` 照旧是执行者，这里把它们的信号翻成一次 `begin()` /
    // `setProgress()` / `succeed()`（§2.1「不把任务中心做成第二套业务逻辑」）。
    Tasks::TaskManager *m_taskManager = nullptr;
    /// 正在跑的那一批下载对应的任务号（无效号 = 没有这一批）。**一批一个号**：
    /// 从 `setDownloadUiBusy(true)` 起、到那一条终态信号为止（见 beginDownloadTask()）。
    Tasks::TaskId m_downloadTaskId = Tasks::invalidTaskId();
    /// 会话号 → 那一次启动的任务号（和 m_launchingSessions / m_sessionDisplayNames 同寿）。
    QHash<int, Tasks::TaskId> m_launchTaskIds;
    /// 用户**还没看过**的那几次失败（面板里那一行挂红点、收起态那格挂红点）。
    /// 打开任务中心面板就算看过了（那是"列给你看"的唯一动作）。
    QSet<Tasks::TaskId> m_unreadFailures;
    /// "刚完成的那一项还在收起态里露着"这件事到期了要重画一次（`taskSummaryRecentWindowMs()`）。
    /// **单发**：每次摆摘要时按"还差多久到期"重新起一次（见 updateBottomBarTaskSummary()）。
    QTimer *m_taskSummaryTimer = nullptr;
    /// 任务详情页那一页（**建一次留着**，每次开页 `setTask()` 重摆 —— 和资源详情页 /
    /// 实例设置同一个路数）。`ui/` 的类，所以是个裸指针 + `deleteLater`（见析构那一段）。
    TaskDetailPage *m_taskDetailPage = nullptr;

    // ---------- 完整性检查（coding plan §4.1，2026-10-12）----------
    // 这几个成员和"那一趟体检"同寿。扫描在后台线程里跑，账本里记成 `TaskKind::Integrity`。
    /// 正在跑的那一趟体检的任务号（无效号 = 没在体检）。
    Tasks::TaskId m_integrityTaskId = Tasks::invalidTaskId();
    /// 正在体检哪个实例（**空 = 没在体检**）—— `integrityScanningVersion()` 读它，删除 /
    /// 改名那两处按它拦一下（§4.1「同一实例不允许同时扫描和删除/改名」）。
    QString m_integrityScanVersionId;
    /// 跑体检的那个线程（`QThread::create`，和 m_instanceIconThread 同一个路数）。
    QThread *m_integrityThread = nullptr;
    /// 关窗 / 收尾时置上，让后台那一趟早点收手（worker 每比几条看一眼它，见 .cpp）。
    QAtomicInt m_integrityAbort{0};
    /// 最新那一份体检结论（报告页每次开都重新喂它一遍）。和 `m_integrityReportVersionId`
    /// 一起用：结论总是**跟着这个实例**走的，别把 A 的结论喂给 B 的报告页。
    VersionIntegrity::Report m_integrityReport;
    QString m_integrityReportVersionId;
    /// 体检报告页（**建一次留着**，每次开页 `setReport()` 重摆 —— 和任务详情页 / 资源详情页
    /// 同一个路数）。`ui/` 的类，所以是个裸指针 + `deleteLater`（见析构那一段）。
    IntegrityReportPage *m_integrityReportPage = nullptr;

    // ---------- 备份 / 还原（coding plan §4.4，2026-10-13）----------
    // 打一份和还原一份共用这一套：只有一趟能跑（见上面那段的 NOTE）。
    /// 正在跑的那一趟的任务号（无效号 = 没在跑）。
    Tasks::TaskId m_backupTaskId = Tasks::invalidTaskId();
    /// 跑那一趟的线程（`QThread::create`，和 m_integrityThread 同一个路数）。
    QThread *m_backupThread = nullptr;
    /// 关窗时置上：让后台那一趟知道主窗口要没了（它跑完不再排回来）。
    /// NOTE: 这两个引擎函数**没有中途收手的口子** —— 走到一半停下来只会留一个半截的包 /
    ///       一个删了一部分的目录，比让它跑完糟得多（同 Export 不给「取消」那条理由）。
    ///       所以这一位只用来"别把结果排回一个正在拆的窗口"，不是"掐断"。
    QAtomicInt m_backupAbort{0};
    /// 这一趟跑完之后要把「世界设置」那一页**重读一遍**（还原换掉的正是那一页上摆着的值）。
    /// 存的是那个世界的目录；**空 = 不用重读**。
    ///
    /// NOTE: 只有**世界还原**那一条路会摆它 —— 打备份一个字节都不改盘，实例快照也不动
    ///       这一页（它改的是实例目录，而那一页读的是 `level.dat`）。
    QString m_backupRefreshWorldDir;
    /// 上面那一页还在不在（`QPointer`，和测速那几处同一个路数）：用户完全可能在备份跑完
    /// 之前就返回、把实例设置收掉了。收掉了就什么都不做（盘上的事已经做完了）。
    QPointer<InstanceSettingsDialog> m_backupRefreshDialog;
    // ★ 2026-09-27 加：会话号 → 那一份的实例显示名。**启动那一刻记一次**，退出时用掉、删掉。
    //
    // 唯一用途是 onGameFinished() 里那行日志的 `[实例名] ` 前缀 —— 那时会话已经从
    // LauncherManager 的表里摘掉了（discard 在 emit 之前，是有意的），`session(id)` 返回的
    // 是空快照，所以名字只能自己留一份。多开时没有它，"游戏进程已结束，退出码：0" 这一行
    // 分不清是谁退了（它上面那些 [游戏输出] 行都有名字，对比之下更刺眼）。
    QHash<int, QString> m_sessionDisplayNames;
    // 现在正卡在哪一段同步等待上（空串 = 没在等）。内容就是底栏那格要写的那句话，
    // 由 enterSyncWait() 写进来、leaveSyncWait() 清掉 —— 做成字符串而不是布尔量，
    // 是因为"在等什么"只有进那一段的人知道（换令牌 / 挑 Java / 等进程起来）。
    QString m_syncWaitStatus;
    // 后台正在写整合包。用它挡住同一实例页上的重复点击；完成回调会清掉。
    bool m_modpackExportBusy = false;
    // 进度按网络分片到达（5000 多个 asset），节流一下再刷界面
    QElapsedTimer m_progressThrottle;
    // 「设置」页的 Java 列表扫过没有。扫描要逐个跑 `java -version`，只在第一次
    // 切到设置页时做一次；用户后来装了新 Java 想让它重扫，去「Java 管理」那一页按
    // 「检测已有 Java」（2026-09-28 起），或者重开启动器。
    bool m_javaListScanned = false;
    // 这台机器上装了哪些 Java（主版本 -> java.exe）。**唯一**的出处是 scanLocalJava()；
    // 认的人只有「Java 管理」那一页那四行。目录换了（folderChanged）时会作废重扫。
    QMap<int, QString> m_localJavaVersions;
    // 窗口是不是为了「收起启动器，游戏退出后回来」这一档才藏起来的。
    // 只有它为真时 onGameFinished() 才会把窗口叫回来 —— 用户自己最小化的窗口
    // 不该被游戏的退出给弹出来。
    bool m_hiddenForGame = false;
    // 哪几份会话现在开着日志窗（会话号 → 那个窗）。**一份会话一个窗**：多开时两份输出
    // 混在一个窗里等于没法看，见 ui/GameLogWindow.hpp 的说明。
    //
    // NOTE: 那个窗建出来就带 WA_DeleteOnClose（关掉即销毁），所以这里的指针**会**失效 ——
    //       窗自己在 finished() 里把这一笔摘掉（见 showGameLogLine 里的接法）。不要在这里
    //       存裸指针却在别处直接用：除了 showGameLogLine() 和 onGameFinished()，
    //       没有第三处读它。
    //
    // NOTE: 那些窗**没有父窗口**（理由见 showGameLogLine 里那段），所以"关启动器顺手关掉
    //       它们"这件事得在 ~MainWindow 里自己办 —— Qt 不会替我们收。
    QHash<int, GameLogWindow *> m_gameLogWindows;
    // 用户**手动关掉过**日志窗的那几份会话。关掉之后这一份不再自动开新窗 ——
    // "我刚把它关了、下一行输出又给我弹回来"是这类窗最招人烦的失败模式。
    // 会话结束（onGameFinished）时清掉：下一份游戏是另一回事。
    QSet<int> m_gameLogDismissedSessions;

    // ---------- 导出那两份文件要用的事实（2026-09-28） ----------
    //
    // 「导出错误报告」的头部要写实例名/id、账号、进程号、启动时间、退出码 —— 这些东西里
    // **大半在会话结束之后就查不到了**：`LauncherManager::session()` 在那一份结束时已经把
    // 记录摘掉了（那是它刻意的，见 gameFinished 的文档），而`m_sessionDisplayNames` 也在
    // onGameFinished() 末尾清掉。崩溃之后导一份报告正是这件事最主要的用法，所以得在
    // **会话还活着的时候**留一份快照。
    //
    // NOTE: 这不是"把会话表抄一份"——抄的是**导出那一份文件要用的那几个值**，而且只在
    //       "这一份开着日志窗"时才有（窗关了、会话结束了，这一笔立刻删掉，见
    //       showGameLogLine 里那个 finished 接法）。不删的话它会随开过的实例数一直长。
    struct GameLogFacts {
        QString instanceId;   ///< `versions/` 下面那个目录名
        QString accountName;  ///< 这一趟用的身份
        qint64 processId = 0;
        qint64 startedAtMs = 0;
        /// 收到过 onGameFinished() 没有（没收到 = 导出时游戏还在跑，报告里会明说）。
        bool finished = false;
        bool crashed = false;
        int exitCode = 0;
    };
    QHash<int, GameLogFacts> m_gameLogFacts;

    // ---------- 游戏异常退出（coding plan §4.3） ----------

    /// 一趟启动**在进程起来之前**就已经定下来、而退出时再也拿不到的那几个值。
    ///
    /// NOTE: Java 可执行文件那条路径是挑完 Java 之后才知道的（`ensureJavaForVersion` 会把它
    ///       写进 `javaSettings.javaPath`），退出时 `javaSettings` 早就不是那一趟的了 ——
    ///       所以必须在启动那一刻抄下来。主版本**不在这里**：这一趟实际用的那个主版本是
    ///       异常退出那一刻现读 `<javaPath>/release` 得到的（见 buildCrashFacts 那段），
    ///       平时一次盘都不读。
    struct GameSessionFacts {
        QString instanceId;
        QString instanceName;
        /// 这一趟用的身份（报告头部要写的那一行）。
        QString accountName;
        qint64 processId = 0;
        qint64 startedAtMs = 0;
        int requiredJavaMajor = 0;
        QString javaPath;
        int xmxMb = 0;
        int xmsMb = 0;
        QString loaderName;
        QString loaderVersion;
    };
    /// 会话号 → 那一趟的启动事实。**启动成功那一刻记、退出那一刻用掉并删掉**（和
    /// `m_sessionDisplayNames` 同寿）—— 不清的话它会随着开过的实例数一直长。
    QHash<int, GameSessionFacts> m_sessionLaunchFacts;

    /// 一项「游戏异常退出」任务留着的那一份东西。**任务收尾之后日志窗可能已经没了**，
    /// 而「导出错误报告」那一刻还要用：退出码、Java / 内存那几个数、以及当时那一段日志。
    struct CrashRecord {
        GameExitDiagnosis::Facts facts;   ///< 诊断那几句读的东西
        /// 这一场是**哪一号会话** —— 「打开日志」那颗键要拿它去找那一扇还开着的日志窗
        /// （异常退出时日志窗不会自动关，见 `GameLogWindow::autoCloseOnExit()`）。
        int sessionId = 0;
        /// 导出那份报告还用的几样（诊断用不上，所以没塞进上面那个结构）。
        QString accountName;
        qint64 processId = 0;
        QDateTime startedAt;
        /// 不轻于 ERROR 的那些行（最多 `GameExitDiagnosis::kExcerptLineLimit` 行）。
        QString errorExcerpt;
        /// 整份输出的**末尾**（最多 `GameExitDiagnosis::kLogTailLineLimit` 行）：报告里
        /// 那一段"完整日志"。
        QString logTail;
        /// 上面两段各自是从多少行里截出来的 —— 报告里要写明"只留了末尾 N 行"。
        int errorLines = 0;
        int totalLines = 0;
    };
    /// 任务号 → 那一份。**和账本同寿**：每次新建这一项时顺手把账本里已经没了的那些清掉
    /// （用户按过「清除已完成」之后这里不该再留着）。
    QHash<Tasks::TaskId, CrashRecord> m_crashRecords;

    /// 这一项「游戏异常退出」要留住的那一份（启动事实 + 那一刻的日志）。
    ///
    /// NOTE: 启动事实可能是空的（早退路径、验证工程直接喂 `onGameFinished` 打的槽）——
    ///       那时候出来的就是"只知道退出码"的一份，`hints()` 相应地少说几句，不编。
    CrashRecord buildCrashRecord(int sessionId, int exitCode, bool crashed) const;
    /// 异常退出了 → 账本上补一项，并留一份 `CrashRecord`。
    void reportGameCrash(int sessionId, int exitCode, bool crashed);

    /// 「导出错误报告」（§4.3）：拿 `m_crashRecords` 里那一份写成一份文本。
    void exportCrashReport(const QString &key);
    /// 「打开崩溃报告目录」（§4.3）：开这个实例的 `crash-reports/`。
    void openCrashReportsDir(const QString &key);
};
