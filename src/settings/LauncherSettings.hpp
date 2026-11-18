/**
 * @file LauncherSettings.hpp
 * @brief 启动器自己的持久化设置：上次启动的版本、启动后行为、下载源。
 * @note 和 GlobalJavaSettings 分开：那边是 Java 运行时的配置，这边是启动器界面的状态。
 * 两边都走 QSettings，键名各自带前缀（Java/ 、Version/ 、Launcher/ 、Download/）。
 */
#pragma once

#include <QDate>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "../download/DownloadConcurrency.hpp"
#include "../download/DownloadProxy.hpp"
#include "../download/DownloadSpeedLimit.hpp"
#include "../launcher/ProcessPriority.hpp"
#include "GameWindowSettings.hpp"

/**
 * @brief 游戏起来之后，启动器自己怎么办。
 *
 * NOTE: 「关掉」这一档刻意用 hide() 而不是 close()：close() 是"关窗口"，
 * 启动器是最后一个窗口时那等于退出进程，而游戏进程是它的子进程 ——
 * 用户以为只是把启动器收起来，结果游戏跟着没了。
 */
enum class LauncherBehaviour {
    Unchanged,       ///< 不变：启动器保持原样
    Minimize,        ///< 最小化启动器
    Close,           ///< 收起启动器（游戏退出后不自动回来）
    CloseAndRestore, ///< 收起启动器，游戏退出后自动重新显示
};

/**
 * @brief 下载走哪个源。
 *
 * NOTE: 存的是**字符串**而不是枚举下标：以后加一档、或者两档调个顺序，
 * 用户存下来的旧值不会被解读成另一个源（下载源选错是要重下几个 GB 的）。
 */
enum class DownloadSource {
    Official, ///< Mojang 官方源
    Mirror,   ///< 镜像源（BMCLAPI）
};

/**
 * @brief 游戏进程跑在哪个优先级上（Windows 的 priority class）。
 *
 * NOTE: 枚举本体在 ../launcher/ProcessPriority.hpp（那边是"进程"那一侧的看法，
 * 这里是"设置"这一侧的看法，值只有一份）。和 DownloadSource 同一个道理，
 * 存进盘里的是**字符串**不是下标 —— 优先级存错档轻则影响手感、重则让游戏跟系统
 * 抢 CPU，不能靠"下标顺序没变"这种默契。
 */

class LauncherSettings : public QObject {
    Q_OBJECT

public:
    explicit LauncherSettings(QObject *parent = nullptr);

    /**
     * @brief 上次成功启动过的版本号。
     * @return 版本目录名；从没启动过就是空串。
     *
     * 启动时用它把"已安装版本"下拉框预选上，免得每次打开都停在没有选中项的状态、
     * 让用户以为一个版本都没装。
     */
    QString lastLaunchedVersion() const;

    /**
     * @brief 记住这次启动的版本号，并立刻落盘。
     * @param versionId 版本目录名（不是显示名）。
     */
    void setLastLaunchedVersion(const QString &versionId);

    /// 上次在主页 / 实例页手选的实例（版本目录名）。启动时排在「上次启动」之后，
    /// 用来挑当前实例（见 resolveCurrentInstance()）。
    QString lastSelectedVersion() const { return m_lastSelectedVersion; }
    void setLastSelectedVersion(const QString &versionId);

    int homeFavoriteCount() const { return m_homeFavoriteCount; }
    int homeRecentCount() const { return m_homeRecentCount; }
    void setHomeFavoriteCount(int count);
    void setHomeRecentCount(int count);

    // ------------------------------------------------------------------
    // 「游戏文件夹」（实例页左边那张列表）
    // ------------------------------------------------------------------
    // 存的是**用户自己添加**的根目录，不含启动器自带的 <exe 目录>/.minecraft ——
    // 那一份由 AppPaths::defaultMinecraftDir() 算出来，存下来反而会在启动器被挪到
    // 别处之后变成一个指向旧地方的死路径。
    // 「自带那份永远排第一、删不掉、当前目录不在盘上怎么办」这些语义在 FolderManager 里。
    // ------------------------------------------------------------------

    /// 用户添加过的根目录，按添加顺序。
    QStringList folders() const;

    /// 覆盖用户添加的根目录列表并立刻落盘。
    void setFolders(const QStringList &folders);

    /// 当前选中的根目录。空串 = 用启动器自带的那一份。
    QString currentFolder() const;

    /// 记下当前选中的根目录并立刻落盘。传空串 = 回到自带的那一份。
    void setCurrentFolder(const QString &dir);

    // ------------------------------------------------------------------
    // 各个游戏文件夹的**自定义名字**（实例页左栏每行上面那一行字）
    // ------------------------------------------------------------------
    // 这是纯显示用的：它**不碰磁盘上的目录名**（要求：「一个 customname，
    // 不是直接改，毕竟 .minecraft 改起来容易出事」）。所以它和 folders() 是两条独立的
    // 记录，谁都不由谁推出来 —— 目录挪走了名字还在列表里，删掉了名字才跟着没。
    //
    // 存的是**原始形态**：每项是 `路径 + '\t' + 名字`。用制表符而不是再开一组 QSettings
    // 键：键里带不了路径（`/` 是 QSettings 的分组分隔符，`D:/MC/.minecraft` 会被切成
    // 好几层组），而 Windows 的文件名里根本不允许出现制表符，所以这个分隔符永远撞不上。
    // ------------------------------------------------------------------

    /// 全部自定义名字，原始形态（见上）。**拆开是 FolderManager 的事**。
    QStringList folderNames() const;

    /// Launcher-only server favorites; implicit default ports retain SRV semantics.
    static QString serverFavoriteKey(const QString &address);
    bool isServerFavorite(const QString &address) const;
    bool setServerFavorite(const QString &address, bool favorite);

    /// 覆盖整份自定义名字表并立刻落盘。
    void setFolderNames(const QStringList &names);

    /**
     * @brief 从 QSettings 读回全部设置。构造函数里已经调过一次。
     */
    void load();

    /**
     * @brief 把这一趟**改过的**那几项写进 QSettings 并 sync()，立刻落盘。
     *
     * 写盘走 `SettingsSyncRetry`（和 TokenStore / AccountStore 同一条路）：Windows 上
     * QSettings 落盘要靠 rename 覆盖目标文件，杀毒/管家之类的过滤驱动会短暂占住它、
     * 报一次 AccessError。只试一次的话这一次改动就丢了（重启后设置又变回去），所以
     * 这里最多试 4 次、间隔 5/20/50 ms；4 次都失败才放弃，并留一条日志。
     *
     * NOTE: 它**不再整份写回**。全局设置那一棵注册表（`HKCU\Software\WTFML\WTFML`）是按
     * 机器共用的：装在两个不同目录的启动器各有自己的数据目录、可以同时开着，但盘上这份
     * 设置是同一份。整份写回的后果是"最后存的那一份赢"—— 甲只改了标题，乙只改了内存，
     * 谁后按保存，谁就把对方刚改的那一项打回自己启动时读到的旧值（用户看到的是"我改的
     * 那份没了"）。现在只写本进程真动过的那几项，没动过的一项都不碰。
     *
     * 哪几项"动过"由这里和 `load()` 留下的那一份快照比出来（见 .cpp 里
     * `persistedValues()`）：`load()` 之后一项都没改时，save() 一个字节都不写。
     */
    void save() const;

    // ------------------------------------------------------------------
    // 启动游戏之后启动器怎么办（设置界面「启动后」那一项）
    // ------------------------------------------------------------------

    /// 当前档位；盘上没有这一项、或者存的是不认识的值时是 Unchanged。
    LauncherBehaviour launcherBehaviour() const { return m_launcherBehaviour; }

    /// 设置档位并立刻落盘。
    void setLauncherBehaviour(LauncherBehaviour behaviour);

    // ------------------------------------------------------------------
    // 下载源
    // ------------------------------------------------------------------

    /// 当前下载源；盘上没有这一项、或者存的是不认识的值时是 Official。
    DownloadSource downloadSource() const { return m_downloadSource; }

    /// 设置下载源并立刻落盘。
    void setDownloadSource(DownloadSource source);

    /// 同时最多下几个文件（设置 → 下载 → 并发数）。
    ///
    /// 出厂 = `DownloadConcurrency::kDefault`（24），也就是"谁都不动这一项时，行为一个字都
    /// 不变"。四档、上下限、越界的值怎么归一，**唯一出处**是 `DownloadConcurrency.hpp`
    /// （这一层只管存取那一个 int）。
    int downloadConcurrency() const { return m_downloadConcurrency; }

    /// 设置并发数并立刻落盘（存进去的是归一化之后的那个数）。
    void setDownloadConcurrency(int count);

    /// 下载速度上限（字节/秒，0 = 不限；设置 → 下载 → 速度上限）。
    ///
    /// 出厂 = `DownloadSpeedLimit::kUnlimited`（0），也就是"谁都不动这一项时行为一个字都
    /// 不变"。几档、上下限、归一化**唯一出处**是 `DownloadSpeedLimit.hpp`（这一层只管
    /// 存取那一个数）。它只约束 `DownloadManager` 那条路；工具箱里的直链下载不受它管。
    qint64 downloadSpeedLimit() const { return m_downloadSpeedLimit; }

    /// 设置速度上限并立刻落盘（存进去的是归一化之后的那个数）。
    void setDownloadSpeedLimit(qint64 bytesPerSecond);

    // ------------------------------------------------------------------
    // 代理（设置 → 下载 →「代理」）
    // ------------------------------------------------------------------
    // 三档的语义与 `host:port` 怎么解析**唯一出处**是 `DownloadProxy.hpp`
    // （这一层只管存取那两个值）。出厂 = 跟随系统 —— 谁都不动这一项时行为一个字都不变。

    /// 当前档位；盘上没有这一项、或者存的是不认识的值时是 System。
    DownloadProxy::Mode proxyMode() const { return m_proxyMode; }

    /// 设置档位并立刻落盘。
    void setProxyMode(DownloadProxy::Mode mode);

    /// 手动那一档填的地址（原样存；空串 = 还没填）。别的不档位不读它。
    QString proxyManualText() const { return m_proxyManualText; }

    /// 设置手动地址并立刻落盘（两头的空白会修掉；不在这里校验格式 —— 认不认得出由
    /// `DownloadProxy::parseManual()` 说了算，界面会把那句原因显示出来）。
    void setProxyManualText(const QString &text);

    /// 资源下载是否使用内置目标实例选择器；关闭时用系统文件夹选择，整合包不读此项。
    bool downloadAutoIntoInstance() const { return m_downloadAutoIntoInstance; }

    /// 设置这一项并立刻落盘。
    void setDownloadAutoIntoInstance(bool autoIntoInstance);

    // ------------------------------------------------------------------
    // 「下载缺前置时提醒」（设置页「下载」栏）
    // ------------------------------------------------------------------
    //
    // 要求（这里是**唯一**的语义出处）：
    // 「弹窗里灰字写可以在设置里面关掉弹窗，并在设置里添加对应条目」
    // 「就是点击下载模组时检测当前实例里面是否存在所有的前置，如果有缺失的前置，弹出
    // 此模组缺失必要的前置：…」
    //
    // 翻成一句判据：**点下载那一刻要不要先查前置** = `这一项 && 知道目标实例是谁`。
    // 关掉之后的动作用户也定死了：「什么都不装，只下用户点的那一版」——
    // 也就是说这一项管的是**整段检查**，不只是那一句提醒。关掉 = 那条路一个字不变。
    //
    // 默认 **true**（勾上）：前置缺了不装，游戏会崩在启动那一刻（而崩溃画面里没有一个字
    // 提到"少了个模组"）—— 多问一句是省事的那个方向。
    // ------------------------------------------------------------------

    /// 点下载模组 / 光影包时，要不要**先查一遍它要装进的那个实例缺不缺前置**。
    bool warnMissingDependencies() const { return m_warnMissingDependencies; }

    /// 设置这一项并立刻落盘。
    void setWarnMissingDependencies(bool warn);

    // ------------------------------------------------------------------
    // CurseForge
    // ------------------------------------------------------------------

    /// 用户自己填的 CurseForge API key。**空 = 走那台不要 key 的社区镜像**。
    QString curseForgeApiKey() const { return m_curseForgeApiKey; }

    /// 设置 CurseForge 的 key 并立刻落盘。传空串 = 回到默认那条路（社区镜像）。
    void setCurseForgeApiKey(const QString &apiKey);

    /// 把 CurseForge 的接口指到别的地址上（如 `http://127.0.0.1:8080/v1`）。
    ///
    /// NOTE: 这个键**只读不写** —— `save()` 里刻意没有它。它存在的唯一理由是让验证工程把
    /// 接口指到本机那个假服务器上（整合包导入的离线端到端那条路），所以产品这一侧
    /// 不可能"因为某个 bug 把用户的接口地址改掉"。填了它时它的优先级最高。
    QString curseForgeApiBaseOverride() const { return m_curseForgeApiBaseOverride; }

    /// 把 Modrinth 的接口（`https://api.modrinth.com/v2`）指到别的地址上。**和上面那条
    /// 同一个形状、同一条 NOTE**：只读不写、只有验证工程会填、填了优先级最高。
    ///
    /// 它罩的是**导出整合包**那一半：导入那半走 CurseForge 那个口子（`/fingerprints`），
    /// 而 Modrinth 那条反查（`POST /v2/version_files`）没有别的办法离线跑。
    QString modrinthApiBaseOverride() const { return m_modrinthApiBaseOverride; }

    /// 版本清单 / 免鉴权皮肤档案 / 贴图 / Minecraft Services 的地址覆盖。四条都是
    /// **只读不写**、只有验证工程会填；空串 = 官方地址。产品路径永远不写它们。
    QString versionManifestUrlOverride() const { return m_versionManifestUrlOverride; }
    QString sessionServerBaseOverride() const { return m_sessionServerBaseOverride; }
    QString textureBaseOverride() const { return m_textureBaseOverride; }
    QString minecraftServicesBaseOverride() const { return m_minecraftServicesBaseOverride; }

    // ------------------------------------------------------------------
    // 「启动选项」那三项（设置页「游戏」栏里那一组）
    // ------------------------------------------------------------------

    /// 启动游戏时要不要给它开一个日志窗。默认关（见 ui/GameLogWindow 的说明）。
    bool showGameLog() const { return m_showGameLog; }

    /// 设置「显示游戏日志」并立刻落盘。
    void setShowGameLog(bool show);

    /// 主页「每日一言」那一行：默认开。关掉 = 不发任何请求、主页不显示也不占位。
    /// 说明文字在设置页「个性化 → 外观」那一组里（见 MainWindowSettingsPage）。
    bool dailyQuote() const { return m_dailyQuote; }

    /// 设置「每日一言」并立刻落盘。
    void setDailyQuote(bool enabled);

    /// 上次打开启动器的本地日期（主页问候用，见 util/HomeGreeting）。没记录时无效。
    QDate lastOpenedDate() const { return m_lastOpenedDate; }

    /// 记下这次打开的日期并立刻落盘。启动时先拿旧值算完问候再调它。
    void setLastOpenedDate(const QDate &date);

    /// 游戏窗口的**模式**：小窗 / 最大化 / 全屏。默认小窗。
    ///
    /// 三档各自发什么参数、以及"最大化"为什么要照屏幕工作区算大小，全写在
    /// GameWindowSettings.hpp 那一段里 —— 别在这儿重述一遍（说两遍就会有一天对不上）。
    GameWindowMode gameWindowMode() const { return m_gameWindowMode; }

    /// 设置窗口模式并立刻落盘。
    void setGameWindowMode(GameWindowMode mode);

    /// 游戏窗口的初始宽度（像素）。默认就是原版自己的那个 854。
    int gameWindowWidth() const { return m_gameWindowWidth; }

    /// 游戏窗口的初始高度（像素）。默认就是原版自己的那个 480。
    int gameWindowHeight() const { return m_gameWindowHeight; }

    /**
     * @brief 设置游戏窗口大小并立刻落盘。
     * @param width 宽度（像素），内部按 gameWindowMinWidth()…gameWindowMaxWidth() 钳位。
     * @param height 高度（像素），内部按 gameWindowMinHeight()…gameWindowMaxHeight() 钳位。
     *
     * 宽高一起收进来是刻意的：一行界面上两个框，只改一个也得能一次落盘。
     */
    void setGameWindowSize(int width, int height);

    /// 上面三格**捆成一份**（模式 + 宽 + 高）。
    ///
    /// 之所以还要这么一个口子：启动那一条路要的就是"这一次用哪一档"，一次拿一份比拿三个
    /// 值少两处"少拿了一个"的机会；而「实例设置」那一页构造 `GlobalValues` 时也要一份。
    /// **它不是第四个出处** —— 里面那三个值就是上面那三个 getter。
    GameWindowSettings gameWindowSettings() const;

    /// 当前优先级。默认 Normal（**显式设**，理由见 ProcessPriority 的 NOTE）。
    ProcessPriority processPriority() const { return m_processPriority; }

    /// 设置优先级并立刻落盘。
    void setProcessPriority(ProcessPriority priority);

    /// 游戏窗口大小的取值范围；load()/save() 都拿它钳位。
    ///
    /// NOTE: 界面那一份是 MainWindowUI::windowMinWidth() 那一组 —— **必须和这里一样**
    /// （ui/ 不许 include src/，所以是各写一遍，验证工程里有断言盯着它们相等）。
    /// 和「内存分配」那对上下限是同一个约定：两边不一致的话，界面允许填的值会在
    /// 读回来时被悄悄改掉，用户看到的是"填了不生效"。
    static int gameWindowMinWidth() { return 320; }
    static int gameWindowMaxWidth() { return 7680; }
    static int gameWindowMinHeight() { return 240; }
    static int gameWindowMaxHeight() { return 4320; }

    // ------------------------------------------------------------------
    // 启动器主窗口的大小（只记大小，不记位置）
    // ------------------------------------------------------------------

    /// 上次记住的主窗口大小；空（0×0）= 没记过，界面按默认 1024×620 开。
    /// 读回来已经收拾过（非正数当没记过、过大截到 `mainWindowMaxExtent()`）；夹到最小尺寸和屏幕
    /// 可用区是界面那一侧的事（MainWindowUI::restoreWindowSize()，最小尺寸只在 ui/ 写一份）。
    QSize mainWindowSize() const { return m_mainWindowSize; }
    /// 记下主窗口大小并立刻落盘（调用方传还原后的大小，最大化时也是）。空尺寸不记。
    void setMainWindowSize(const QSize &size);
    /// 宽、高各自的上限：盘上的值不可信，比任何屏幕都大的数截到这里。
    static int mainWindowMaxExtent() { return 16384; }

    // ------------------------------------------------------------------
    // 「启动提醒」（设置页「提示」栏里那一组）
    // ------------------------------------------------------------------
    // 两条都是**启动之前挡一下**的那种确认框：同一份游戏存档被两个进程同时打开，
    // 谁的进度都不算数（后写的覆盖先写的），而作者这边没有任何办法事后补救 ——
    // 所以默认**开**，用户嫌烦可以在设置页里关掉（提示框上那句小字就是告诉他去哪儿关）。
    //
    // 两个开关**各管各的那一条**：A 管"同一个人的同一个实例"（手快连点两下），
    // B 管"这个实例已经有进程了"（换个人、或者上一问选了『仍要启动』）。
    // 关掉其中一条不影响另一条 —— 它们挡的是两种不同的手滑。
    // ------------------------------------------------------------------

    /// 十秒内用同一个身份再启动同一个实例时要不要拦一下。
    /// 默认 **true**（见上）。判据与那个十秒的来历见 MainWindow::confirmLaunchWhileRunning()。
    bool warnDuplicateLaunch() const { return m_warnDuplicateLaunch; }

    /// 设置这一项并立刻落盘。
    void setWarnDuplicateLaunch(bool warn);

    /// 这个实例已经有进程在跑时要不要提醒一句"可能丢进度"。
    /// 默认 **true**（见上）。
    bool warnWorldLoss() const { return m_warnWorldLoss; }

    /// 设置这一项并立刻落盘。
    void setWarnWorldLoss(bool warn);

    // ------------------------------------------------------------------
    // 枚举 <-> 存储值
    // ------------------------------------------------------------------
    // 两个方向都放在这一处，免得"写进去"和"读回来"各写一套字符串、
    // 慢慢就拼歪了（拼歪的后果是静默退回默认档，用户看不出来）。

    /// @return 存进 QSettings 的字符串。
    static QString behaviourKey(LauncherBehaviour behaviour);
    /// @return 认不出来的值一律当 Unchanged。
    static LauncherBehaviour behaviourFromKey(const QString &key);

    /// @return 存进 QSettings 的字符串。
    static QString downloadSourceKey(DownloadSource source);
    /// @return 认不出来的值一律当 Official。
    static DownloadSource downloadSourceFromKey(const QString &key);

    /// @return 存进 QSettings 的字符串。
    static QString processPriorityKey(ProcessPriority priority);
    /// @return 认不出来的值一律当 Normal。
    static ProcessPriority processPriorityFromKey(const QString &key);

private:
    /**
     * @brief 当前全部可持久化项，**按写进 QSettings 的形态**摆成一份表。
     *
     * 这是键名 → 值映射的唯一出处：`load()` 用它记下"读回来的那一份"，
     * `save()` 拿它和那份比、只写变了的。两处共用同一份表，就不会出现
     * "save() 里漏了一个键、于是那一项永远存不下来"这种只在真机上才发现的错。
     */
    QVariantMap persistedValues() const;

    /// `load()` 读完之后（= 上次落盘）那一份值。`save()` 逐项与它比，只写不同项。
    /// mutable：`save()` 是 const，但写完要把这份快照推进到新值。
    mutable QVariantMap m_persisted;

    QString m_lastLaunchedVersion;
    QString m_lastSelectedVersion;
    int m_homeFavoriteCount = 3;
    int m_homeRecentCount = 3;
    QStringList m_folders;
    QString m_currentFolder;
    QStringList m_folderNames;
    QStringList m_serverFavorites;
    LauncherBehaviour m_launcherBehaviour = LauncherBehaviour::Unchanged;
    DownloadSource m_downloadSource = DownloadSource::Official;
    /// 同时最多几个下载（见上面那一节）。默认取判据层那个出厂值 —— 别在这儿另写一个 24。
    int m_downloadConcurrency = DownloadConcurrency::kDefault;
    /// 下载速度上限（字节/秒，0 = 不限；见上面那一节）。默认取判据层那个出厂值。
    qint64 m_downloadSpeedLimit = DownloadSpeedLimit::kUnlimited;
    /// 代理档位 / 手动地址（见上面那一节）。默认取判据层那个出厂值 = 跟随系统。
    DownloadProxy::Mode m_proxyMode = DownloadProxy::kDefaultMode;
    QString m_proxyManualText;
    bool m_downloadAutoIntoInstance = true;
    /// 点下载模组 / 光影包时要不要先查前置（见上面那一节）。默认勾上。
    bool m_warnMissingDependencies = true;
    bool m_showGameLog = false;
    bool m_dailyQuote = true;
    QDate m_lastOpenedDate;
    /// 默认**小窗**：启动器发的是"照下面那两个数开一个普通窗口" ——
    /// 默认值取这一档的意思是"谁都不动这一项时，行为一个字都不变"。
    GameWindowMode m_gameWindowMode = GameWindowMode::Windowed;
    /// 默认值取原版的默认窗口大小（1.12.2 的 Main 里那对 defaultsTo(854, 480)）：
    /// 谁都不动这一项时，启动器发出去的那两个参数就等于游戏自己本来要用的值 ——
    /// 这一档不该让任何人的窗口大小变一下。
    int m_gameWindowWidth = 854;
    int m_gameWindowHeight = 480;
    ProcessPriority m_processPriority = ProcessPriority::Normal;
    /// 主窗口大小（见 `mainWindowSize()`）。默认空 = 没记过。
    QSize m_mainWindowSize;
    /// 两条启动提醒。**默认开**：它们是"挡一下手滑"的，默认关就等于没做。
    bool m_warnDuplicateLaunch = true;
    bool m_warnWorldLoss = true;
    /// CurseForge 那两项（见上面那两个访问器）。**都是空串 = 默认档**：
    /// 走那台不要 key 的社区镜像。
    QString m_curseForgeApiKey;
    QString m_curseForgeApiBaseOverride;
    /// Modrinth 那条路的地址覆盖（见 `modrinthApiBaseOverride()`）。同样是「空 = 默认档」。
    QString m_modrinthApiBaseOverride;
    QString m_versionManifestUrlOverride;
    QString m_sessionServerBaseOverride;
    QString m_textureBaseOverride;
    QString m_minecraftServicesBaseOverride;
};
