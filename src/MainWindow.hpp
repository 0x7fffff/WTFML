/**
 * @file MainWindow.hpp
 * @brief 声明主启动器窗口及其UI交互。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 按钮事件与版本选择在此文件中统一管理。
 */

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPixmap>
#include <QProcess>

// PlayerSkinStore::PlayerSkinProfile —— 主窗口按值存一份"最近要过的那份档案"（见
// m_skinProfile），所以这里非得 include 不可（前置声明只够拿指针/引用）。
// 它是 Core+Network 的头，拖进来的东西不多。
#include "manage/PlayerSkinStore.hpp"

class MainWindowUI;
// 安装页那棵加载器树上的一个条目（loaderItem()）。这里只在私有函数的参数/返回值里按
// 指针用，前置声明就够 —— 不值得为了一个参数类型让每个 include 了 MainWindow.hpp 的
// 地方都把 QtWidgets 里那个大块头拖进来。
class QTreeWidgetItem;
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
// 「账号与外观」那个对话框（多账号 + 换皮肤披风）。这里**只按指针**持有它，所以
// 前置声明就够 —— 它在 MainWindow 的构造函数之后才懒建（第一次点那个入口才建）。
class AccountSkinDialog;
// 换皮肤披风那几条要令牌的接口（src/auth/SkinApi.hpp）。同上，只按指针持有。
class SkinApi;
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
    void onSkinProfileReady(const PlayerSkinProfile &profile);
    /// 一张贴图在本地就绪了。皮肤和披风是**分开**回来的，这里按 URL 认领。
    void onSkinTextureReady(const QString &url, const QString &path);
    /// 要不到（网络失败、下回来的不是皮肤图、存不下来）。**只记日志、不弹窗** ——
    /// 这是全仓唯一一处不弹窗的错误，理由见 PlayerSkinStore.hpp 文件头。
    void onSkinFailed(const QString &message);
    /// 把一份外观贴到界面上（右栏那个小人 + 底栏头像 + 右栏那句话）。
    /// @param skinPath 空串 = 用默认外观。载不进来（文件没了/下了一半）也走这一档。
    /// @param slim 纤细模型（手臂 3 宽）。
    ///
    /// NOTE: 是**槽**不只是私有函数：验证工程靠 QMetaObject::invokeMethod 驱动它
    ///       （沙盒里现写两张 PNG 喂进来），**不联网**就能验完整条界面路径。
    void applyPlayerSkin(const QString &skinPath, const QString &capePath, bool slim);
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
    /// 「导入整合包…」：选文件 → 读清单 → 问名字 → 确认 → 交给 applyModpackImport()。
    ///
    /// 读的是压缩包根目录里那一份 modrinth.index.json（ZipUtil::readEntry，**不整包解开**：
    /// 用户随手选一个几百 MB 的包，为了知道它不是整合包先解压几分钟是白等）。
    void onImportModpackClicked();
    /// 导入的**执行部分**：把清单解析出来、该拦的拦掉、算出安装器要的 Java，然后开始导入。
    /// @param zipPath 用户选的那个整合包文件。
    /// @param instanceName 落地的实例名（= `versions/<id>` 那个目录名）。
    /// @param indexJson 压缩包里那份 `modrinth.index.json` 的**原始字节**。
    ///        传字节而不是传解析结果，是为了让这一半能被验证工程驱动（自定义类型过不了
    ///        QMetaObject::invokeMethod）；里面会再解析一遍 —— 纯函数，94 KB 的清单也就
    ///        一瞬，换来的是"确认框里说的那个包"和"真的去装的那个包"是同一串字节。
    /// @return 真的开始导入了返回 true（失败/被拦下时弹窗并返回 false）。
    ///
    /// NOTE: 拆成两步的理由和 applyRename() 一样 —— QFileDialog / QInputDialog / 确认框
    ///       都是模态的，offscreen 的验证工程驱动不了它们，而这一半里藏着真会错的东西
    ///       （重名要拦、不支持的加载器要拦、忙态几时置上、结束之后列表要刷）。
    bool applyModpackImport(const QString &zipPath, const QString &instanceName,
                            const QByteArray &indexJson);
    /// 用户在左边那张列表里换了「游戏文件夹」。真正干活的是 FolderManager 发出的
    /// currentFolderChanged()，这里只负责把选中项翻译成一次 setCurrentFolder()。
    void onFolderSelectionChanged();
    void onProcessOutput(const QString &text);
    void onProcessError(const QString &text);
    void onGameFinished(int exitCode, QProcess::ExitStatus exitStatus);
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
                                 const QStringList &versions, const QString &errorMessage);

    void onVersionsFetched(const QStringList &versions);
    void onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson);
    void onVersionDownloadStats(int done, int total, int failed, const QString &currentFile);
    void onVersionDownloadFinished(const QString &versionId, bool success,
                                   const QString &errorMessage);

protected:
    /// 关窗口 = 退出启动器（全仓没有别的退出路径：没有托盘图标、没有 qApp->quit()）。
    ///
    /// 游戏还在跑的时候这里要拦一下，理由见 .cpp 里那段：MainWindow 是 main() 里的栈对象，
    /// 它一析构，链子上那个 QProcess 的析构就会把游戏 **kill()** 掉。
    void closeEvent(QCloseEvent *event) override;

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
    // 「移除文件夹」能不能点：自带那份移不掉，另外下载期间整列都不给动。
    void updateFolderButtons();
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
    /// 某一条加载器钉住的版本号（它在树上选的那一条）；空串 = 不钉，让安装器按老规矩挑。
    QString pinnedLoaderVersion(int loaderIndex) const;
    /// 刷新那棵树的样子：哪一条选中了、其余几条写着"与 X 不兼容"、选中的那条第二行
    /// 写着它选的版本号。忙态变化之后必须重新调一次（装到一半时整棵树是灰的）。
    ///
    /// NOTE: "选中了哪一条"**不在树上**（QTreeWidget 的选中项是"光标停在哪"，
    ///       和"我要装哪一个"是两件事）—— 它住在 m_selectedLoaderIndex 里，这里是唯一
    ///       把它画出来的地方。
    void updateLoaderRows();
    /// 选中某一条加载器（点顶层那一条走的就是这里）：改选中项、刷那棵树。
    /// 它在树上**不**负责展开（展开折叠是点击那一下自己的事），也**不**发任何请求 ——
    /// 版本列表是进安装页时一次性全问好的（见 requestAllLoaderVersions()）。
    void selectLoader(int loaderIndex);
    /// 四个加载器能装哪些版本，**进安装页时一次性全问出去**（用户原话："获取列表不能
    /// 点开了再获取，得全获取完"）：先给每条摆一个「正在获取…」占位，再各发一发请求。
    /// 四条各占一格，所以四发真能同时在飞（见 VersionManager::m_loaderVersionRequests）。
    void requestAllLoaderVersions();
    /// 把一份版本列表灌成某个加载器的子条目（第 0 条永远是"自动（推荐）"）。
    void fillLoaderVersionItems(int loaderIndex, const QStringList &versions);
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
    // 下载期间禁用这些：启动按钮、已安装版本下拉框、实例页那棵树 / 文件夹列表、
    // 以及安装页那一整层（名称框、四个加载器、安装按钮、返回），并把底栏中间那格的
    // 进度条切成"正在准备下载"的不确定态。进度条和底栏的显隐不在这里定
    // （取决于"在下载"和"在启动"两条），统一由 updateBottomBar() 说了算。
    //
    // NOTE: 这条注释以前还点着一个"下载版本"按钮和"两个版本下拉框" —— 都是上一版界面
    //       的东西了：「下载版本」已经没了（下载页现在是"点版本号进安装页"），下拉框
    //       也只剩"已安装版本"那一个（另一个是安装页上按加载器分的几个）。清单在下面
    //       实现里逐条写着，要核对就照着那些 setEnabled() 看。
    void setDownloadUiBusy(bool busy);
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
    /// 要一张贴图。@param allowDownload false（离线模式勾着）时**只认已经在缓存里的那一份**
    /// —— fetchTexture() 在缓存不命中时会真的去下一个，那正是离线模式要求别做的事。
    /// @return 这一张是不是**已经在手上了**（缓存命中）。不是"发出去请求了"。
    bool requestSkinTexture(const QString &url, bool allowDownload);
    // ---------- 「账号与外观」那个窗 ----------
    /// 把这个窗建出来并接好线（幂等：已经建过就什么都不做）。
    void setupAccountSkinDialog();
    /// 刷新这个窗里的三样东西：账号列表（本地）、3D 预览（走和主页同一条路）、
    /// 披风列表（**要令牌**，所以这一步会联网）。窗还没建时什么都不做。
    void refreshAccountSkin();
    /// 这个窗顶部那句话应该写什么（当前身份）。**唯一一处**决定它 —— 离线模式的
    /// 提醒、未登录、已登录三种状态要是散在各条路上，迟早出现"底栏说离线、这里说正版"。
    QString accountSkinStatusText() const;

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
    GlobalJavaSettings *m_javaSettings;
    // 实例分组（<数据目录>/wtfml.folders）。**不需要**在换游戏文件夹时重置它：
    // 它每个访问器都会比一下"这次问的是哪一份 .minecraft"，不一样就自己重读。
    InstanceGroupStore *m_groupStore;
    // 玩家外观的数据层（免鉴权问官方 + 内容寻址的本地缓存）。
    PlayerSkinStore *m_skinStore;
    // 改外观那一层（**要令牌**：换皮肤、换披风）。和上面那个是两条路，见 SkinApi.hpp
    // 文件头那句分工。
    SkinApi *m_skinApi;
    // 「账号与外观」那个窗。**懒建**：没点过那个入口就是空的（见
    // setupAccountSkinDialog()）。它是 this 的子对象、也没开 WA_DeleteOnClose，所以关掉
    // 之后指针照样有效 —— 再点一次是把它 show 回来，不是重建。
    AccountSkinDialog *m_accountSkinDialog = nullptr;
    // 现在屏幕上用的是哪两张贴图（本地路径）。**只记真的载入成功了的** —— 所以空串
    // 就等于"现在显示的是默认外观"，右栏那句话的判据之一就是它。
    QString m_skinPath;
    QString m_capePath;
    // 最近一次**要过**的那份档案。唯一的用途：贴图是按 URL 分开回来的（皮肤一个、
    // 披风一个），回来时得知道"这一张是皮肤还是披风"，也有个东西能判"和上次一样吗"。
    PlayerSkinProfile m_skinProfile;
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
    // 启动进度是否还在走。从"进程真的起来了"（startGame 返回 true）到游戏自己吐出
    // 第一行输出为止 —— 中间那段是 JVM 和游戏自己在初始化，界面上没别的反馈。
    // 命名刻意不叫 m_launching：它表示的是"这段进度要显示"而不是"正在启动"。
    bool m_launchInFlight = false;
    // 上面那段进度说的是哪个版本（中间那格要显示"正在启动 <显示名>"）。
    QString m_launchingVersionId;
    // 现在正卡在哪一段同步等待上（空串 = 没在等）。内容就是底栏那格要写的那句话，
    // 由 enterSyncWait() 写进来、leaveSyncWait() 清掉 —— 做成字符串而不是布尔量，
    // 是因为"在等什么"只有进那一段的人知道（换令牌 / 挑 Java / 等进程起来）。
    QString m_syncWaitStatus;
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