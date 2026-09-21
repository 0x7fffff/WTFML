/**
 * @file MainWindow.hpp
 * @brief 声明主启动器窗口及其UI交互。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 按钮事件与版本选择在此文件中统一管理。
 */

#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>

class MainWindowUI;
class LauncherManager;
class AuthManager;
class FolderManager;
class VersionManager;
class GlobalJavaSettings;
class LauncherSettings;
class InstanceGroupStore;
// 「分组…」那个对话框。这里只按 const 引用传，前置声明就够 —— 没必要为了一个参数类型
// 让每个 include 了 MainWindow.hpp 的地方都把 ui/ 拖进来。
class GroupDialog;
// 「实例设置…」那个对话框。同上，只按 const 引用传。
class InstanceSettingsDialog;
// 「每实例设置」那两层的值类型（src/settings/EffectiveJavaSettings.hpp）。这里只按
// const 引用传，前置声明就够 —— 让每个 include 了 MainWindow.hpp 的地方都拖一份
// 值结构的定义没有意义。
struct EffectiveJavaSettings;
struct JavaSettingsOverride;
// 「版本名格式」开关的数据模型。这里只按 const 引用传，前置声明就够，
// 不必为了一个参数类型把 VersionDisplay.hpp 拖进每个 include 了 MainWindow.hpp 的地方。
struct VersionNameOptions;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStartButtonClicked();
    void onLoginButtonClicked();
    // 登录三态（AuthManager 的三个信号）。登录是异步的：点完按钮到浏览器回调回来之间
    // 可能有几十秒（用户还得输账号密码），所以这三条都得在界面上有反应 ——
    // 尤其 loginFailed，它以前**根本没有人接**，失败只在浏览器那个页面里看得见。
    void onLoginStarted();
    void onLoginSucceeded(const QString &playerName);
    void onLoginFailed(const QString &errorMessage);
    void onAddFolderClicked();
    void onRemoveFolderClicked();
    /// 「重命名…」：问一个新名字，真的把磁盘上的目录改掉（含 jar / json 一起改）。
    /// 校验、执行、回滚全在 VersionManager::renameInstance() 里，这里只负责
    /// 三道闸口（有没有选中、是不是正在跑、名字合不合法）和弹窗。
    void onRenameInstanceClicked();
    /// 改名的**执行部分**：校验名字 → 改磁盘 → 同步状态（LastLaunched / 选中项）→ 刷界面。
    /// 失败时弹窗并返回 false。
    ///
    /// NOTE: 和"问新名字"那个 QInputDialog 分成两步，是为了**能被验证工程驱动** ——
    ///       模态窗在 offscreen 下驱动不了（QInputDialog::getText 会一直等），而这一半
    ///       里藏着最容易漏的那件事：改完名要同步 Version/LastLaunched，不然用户下次
    ///       打开启动器，选中项会空着，他会以为实例丢了。那条只有真的跑一遍才验得出来。
    bool applyRename(const QString &oldId, const QString &newId);
    /// 「分组…」：给当前选中的实例选一个组，顺便管组清单（新建/重命名/删除）。
    ///
    /// 不分组也能用 —— 对话框里那份工作副本只有在点「确定」之后才由 applyGrouping()
    /// 落盘，取消的话磁盘一个字节都不动。
    void onGroupInstanceClicked();
    /// 分组的**执行部分**：把对话框里摆好的组清单和归属落到磁盘上。失败时弹窗并返回 false。
    ///
    /// NOTE: 拆成两步的理由和 applyRename() 一样 —— QDialog::exec() 会阻塞在事件循环里，
    ///       验证工程驱动不了它。而这一半里藏着真会错的东西（哪一步该写、写失败了怎么
    ///       说清"另一半成了没有"），必须验得了。
    bool applyGrouping(const QString &instanceKey, const GroupDialog &dialog);
    /// 「实例设置…」：给当前选中的实例覆盖启动五项（每项两级：跟随全局 / 自定义）。
    ///
    /// 对话框只把控件读成一份值，落盘在 applyInstanceSettings() 里 —— 取消的话
    /// wtfml.meta 一个字节都不动。
    void onInstanceSettingsClicked();
    /// 实例设置的**执行部分**：把对话框里摆好的覆盖写进这个实例的 wtfml.meta。
    /// 失败时弹窗并返回 false，磁盘保持原样。
    ///
    /// NOTE: 拆成两步的理由和 applyRename() / applyGrouping() 一样 ——
    ///       QDialog::exec() 阻塞在事件循环里，offscreen 的验证工程驱动不了它。
    ///       而这一半里藏着真会错的东西（"跟随全局"的项要**删键**而不是写一个等于
    ///       全局的值、写盘前得先确保实例有稳定 id），必须验得了。
    bool applyInstanceSettings(const QString &versionId, const InstanceSettingsDialog &dialog);
    /// 用户在左边那张列表里换了「游戏文件夹」。真正干活的是 FolderManager 发出的
    /// currentFolderChanged()，这里只负责把选中项翻译成一次 setCurrentFolder()。
    void onFolderSelectionChanged();
    void onProcessOutput(const QString &text);
    void onProcessError(const QString &text);
    void onGameFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onDownloadButtonClicked();
    void onVersionsFetched(const QStringList &versions);
    void onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson);
    void onVersionDownloadStats(int done, int total, int failed, const QString &currentFile);
    void onVersionDownloadFinished(const QString &versionId, bool success,
                                   const QString &errorMessage);

private:
    void setupConnections();
    // ---------- 「设置」选项卡 ----------
    /// 把设置对象里的值灌进设置页的控件。构造函数里调一次；**不要**在用户改动之后
    /// 再调它 —— 那会把用户正在编辑的东西顶回盘上的旧值。
    void applySettingsToUi();
    /// 填「Java 版本」下拉框。要跑几次 `java -version`（每次几十到几百毫秒），
    /// 所以**第一次切到设置页**才扫，不在启动路径上加这段延迟。
    void refreshJavaList();
    /// 把「Java 版本」那一行下面的说明文字刷成**当前设定**：手动/自动两档 × 路径有值/
    /// 无值，四种说法。它说的是"设定了什么"，不是"这次启动一定会用哪个" —— 自动档下真正
    /// 挑中的 Java 要到启动那一刻才知道（所以文案写的是"上次用的是…"）。
    void updateJavaPathLabel();
    /// 把归一化之后的「版本名格式」开关回灌到四个勾选框上（内部挡信号）。
    void pushVersionNameOptionsToUi(const VersionNameOptions &options);
    /// 选项卡切换：底栏左下角要不要显示实例名、以及要不要扫 Java，都在这里收口。
    void onMainTabChanged();
    void onJavaSelectionChanged();
    /// 把「内存分配」那一行画成当前状态：自动档按**现在**的系统内存重算并落盘、禁用两个
    /// 数字框，手动档原样显示用户定的值；两种情况都要写那句"这台机器有多少内存"的说明。
    /// 调用点：开窗灌值时（applySettingsToUi）、切到设置页时、用户切自动/手动那一刻、
    /// 真的要启动游戏之前（那一刻的可用内存才算数）。
    void refreshMemoryUi();
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
    /// @param io 进参是合成好的有效值，出参是定下来的内存。
    /// @param override 这个实例的覆盖（用来判断"自动这一档是谁要的"）。
    void resolveMemoryForLaunch(EffectiveJavaSettings &io, const JavaSettingsOverride &override);
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
    void onLauncherBehaviourChanged();
    void onDownloadSourceChanged();
    void onVersionNameOptionChanged();
    void onApplyExtraJvmArgsClicked();
    /// 把「下载源」交给真正发请求的那一层（VersionManager → DownloadManager）。
    /// 启动时读盘、用户在设置页改，两条路都走这一句，别各写一遍。
    void syncDownloadSource();
    /// 启动成功后按「启动后」那一项处理启动器自己。
    void applyLauncherBehaviourAfterLaunch();

    /// 把一条消息交给 Qt 的日志系统（命令行里看）。界面里已经没有日志面板了，
    /// 必须让用户看到的消息走的是 QMessageBox，不靠这一条 —— 见 .cpp 里的说明。
    void appendLog(const QString &text);
    /// 刷新底部状态条那三格（头像/用户名、进度、实例运行状态）。
    /// 显示规则见 .cpp；界面那边只负责把控件搭出来。
    void updateBottomBar();
    /// 设置页那个「微软登录」按钮的文字与可用性：没登录是「微软登录」、登录过是
    /// 「重新登录」、正在走流程是灰的「正在登录…」。
    void updateLoginButton();
    /// 一个版本号在界面上的显示名（"已安装版本"下拉框里的那条文本）。
    /// 下拉框里没有它时退回版本号本身。
    QString instanceDisplayName(const QString &versionId) const;
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
    // 重灌实例页左边那张「游戏文件夹」列表，并把选中项停在 FolderManager 的当前目录上
    // （内部挡信号：重灌不是用户改选，不该反过来触发一次切换）。
    void refreshFolderList();
    // 「删除文件夹」能不能点：自带那份删不掉，另外下载期间整列都不给动。
    void updateFolderButtons();
    // 把 FolderManager 的当前目录交给 AppPaths（此后所有 <数据目录>/... 都对着它），
    // 并打一行日志。换文件夹的那条链路只有这一处改全局路径。
    void applyCurrentFolderToPaths();
    /// 重灌"可安装版本"那一对控件（下拉框 + 列表）。在线清单是它们唯一的数据源，
    /// 清单还没拉回来时什么都不动（占位文案得留着）。
    void refreshAvailableVersions();
    // 启动时把"上次启动过的版本"选回来。**只在构造函数里调一次**——
    // 名单每次刷新（清单到达、下载完成）都调它就会变成"用户手选完又被抢回去"。
    // 记着的那个版本已经不在磁盘上时留空不选，占位文案会说明该怎么选。
    void restoreLastLaunchedVersion();
    // 下载期间禁用"启动游戏""下载版本"、两个版本下拉框和两张列表，并把底栏中间
    // 那格的进度条切成"正在准备下载"的不确定态。进度条和底栏的显隐不在这里定
    // （取决于"在下载"和"在启动"两条），统一由 updateBottomBar() 说了算。
    void setDownloadUiBusy(bool busy);
    // 把"启动游戏"按钮的文字刷成两行：第一行固定是"启动游戏"，第二行是「已安装版本」
    // 下拉框当前选中的**显示名**（可能带自定义名/加载器名，不是版本号；没选时是
    // "未选择版本"）。顺带把裸版本号的 tooltip 挂到同一个下拉框上。见 .cpp 里的说明。
    void updateStartButtonText();
    void updateDownloadProgressUi(int done, int total, int failed, const QString &currentFile);

    MainWindowUI *m_ui;
    // NOTE: 声明顺序就是构造顺序。m_settings 必须排在 m_folderManager **前面** ——
    //       后者在构造函数里就要读盘（「游戏文件夹」列表），晚于它构造的话拿到的
    //       指针是空的，读出来就是"一份文件夹都没添加过"。
    LauncherSettings *m_settings;
    LauncherManager *m_launcherManager;
    AuthManager *m_authManager;
    FolderManager *m_folderManager;
    VersionManager *m_versionManager;
    GlobalJavaSettings *m_javaSettings;
    // 实例分组（<数据目录>/wtfml.folders）。**不需要**在换游戏文件夹时重置它：
    // 它每个访问器都会比一下"这次问的是哪一份 .minecraft"，不一样就自己重读。
    InstanceGroupStore *m_groupStore;
    // 下载是否在进行中。注意不能只问 m_versionManager：从点击"下载"到版本 JSON
    // 拉回来之前，VersionManager 那边还没有任何批次，这段时间也得算"忙"。
    bool m_downloadInFlight = false;
    // "有版本目录但 jar 名对不上"这件事是否已经写过日志。refreshInstalledVersions()
    // 一次会话会被调很多次，不记一笔就是同样的告警反复刷。
    bool m_reportedUnrecognizedVersions = false;
    // 启动进度是否还在走。从"进程真的起来了"（startGame 返回 true）到游戏自己吐出
    // 第一行输出为止 —— 中间那段是 JVM 和游戏自己在初始化，界面上没别的反馈。
    // 命名刻意不叫 m_launching：它表示的是"这段进度要显示"而不是"正在启动"。
    bool m_launchInFlight = false;
    // 上面那段进度说的是哪个版本（中间那格要显示"正在启动 <显示名>"）。
    QString m_launchingVersionId;
    // 进度按网络分片到达（5000 多个 asset），节流一下再刷界面
    QElapsedTimer m_progressThrottle;
    // 「设置」页的 Java 列表扫过没有。扫描要逐个跑 `java -version`，只在第一次
    // 切到设置页时做一次；用户后来装了新 Java 想让它重扫，重开启动器即可。
    bool m_javaListScanned = false;
    // 窗口是不是为了「收起启动器，游戏退出后回来」这一档才藏起来的。
    // 只有它为真时 onGameFinished() 才会把窗口叫回来 —— 用户自己最小化的窗口
    // 不该被游戏的退出给弹出来。
    bool m_hiddenForGame = false;
};