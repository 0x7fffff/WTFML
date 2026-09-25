/**
 * @file LauncherSettings.hpp
 * @brief 启动器自己的持久化设置：上次启动的版本、版本名格式、启动后行为、下载源。
 * @author 0x7fffff
 * @date 2026-09-20
 * @note 和 GlobalJavaSettings 分开：那边是 Java 运行时的配置，这边是启动器界面的状态。
 *       两边都走 QSettings，键名各自带前缀（Java/ 、Version/ 、Launcher/ 、Download/）。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "../launcher/ProcessPriority.hpp"
#include "../manage/VersionDisplay.hpp"

/**
 * @brief 游戏起来之后，启动器自己怎么办。
 *
 * NOTE: 「关掉」这一档刻意用 hide() 而不是 close()：close() 是"关窗口"，
 *       启动器是最后一个窗口时那等于退出进程，而游戏进程是它的子进程 ——
 *       用户以为只是把启动器收起来，结果游戏跟着没了。
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
 *       用户存下来的旧值不会被解读成另一个源（下载源选错是要重下几个 GB 的）。
 */
enum class DownloadSource {
    Official, ///< Mojang 官方源
    Mirror,   ///< 镜像源（BMCLAPI）
};

/**
 * @brief 游戏进程跑在哪个优先级上（Windows 的 priority class）。
 *
 * NOTE: 枚举本体在 ../launcher/ProcessPriority.hpp（那边是"进程"那一侧的看法，
 *       这里是"设置"这一侧的看法，值只有一份）。和 DownloadSource 同一个道理，
 *       存进盘里的是**字符串**不是下标 —— 优先级存错档轻则影响手感、重则让游戏跟系统
 *       抢 CPU，不能靠"下标顺序没变"这种默契。
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
    // 这是纯显示用的：它**不碰磁盘上的目录名**（用户 2026-09-24 原话：「一个 customname，
    // 不是直接改，毕竟 .minecraft 改起来容易出事」）。所以它和 folders() 是两条独立的
    // 记录，谁都不由谁推出来 —— 目录挪走了名字还在列表里，删掉了名字才跟着没。
    //
    // 存的是**原始形态**：每项是 `路径 + '\t' + 名字`。用制表符而不是再开一组 QSettings
    // 键：键里带不了路径（`/` 是 QSettings 的分组分隔符，`D:/MC/.minecraft` 会被切成
    // 好几层组），而 Windows 的文件名里根本不允许出现制表符，所以这个分隔符永远撞不上。
    // ------------------------------------------------------------------

    /// 全部自定义名字，原始形态（见上）。**拆开是 FolderManager 的事**。
    QStringList folderNames() const;

    /// 覆盖整份自定义名字表并立刻落盘。
    void setFolderNames(const QStringList &names);

    // ------------------------------------------------------------------
    // 「版本名格式」
    // ------------------------------------------------------------------
    // NOTE: 界面入口在设置页的「版本名格式」那一行 —— 四个勾选框
    // （versionNameCustomCheck / RealCheck / LoaderCheck / LoaderVersionCheck，
    // ui/MainWindowUI.cpp），改动经 MainWindow::onVersionNameOptionChanged() 归一化后落盘。
    //
    // 当初留的那条余地仍然有效：这四个布尔也可以改摆成一个"预设档位"的下拉框、由它去
    //   反写这四个布尔——数据模型就是下面这个 struct，界面想怎么摆都行，别为了界面去改模型。
    //   归一化由 normalizeVersionNameOptions() 负责，界面只要照它约束勾选即可。
    //   默认值见 VersionDisplay.hpp 里 VersionNameOptions 的成员初始值。
    // ------------------------------------------------------------------

    /**
     * @brief 当前的「版本名格式」开关，已归一化。
     */
    VersionNameOptions versionNameOptions() const;

    /**
     * @brief 设置「版本名格式」开关并立刻落盘。
     * @param options 原始开关；内部会先 normalizeVersionNameOptions()。
     */
    void setVersionNameOptions(const VersionNameOptions &options);

    /**
     * @brief 从 QSettings 读回全部设置。构造函数里已经调过一次。
     */
    void load();

    /**
     * @brief 把当前值写进 QSettings 并 sync()，立刻落盘。
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

    // ------------------------------------------------------------------
    // CurseForge
    // ------------------------------------------------------------------

    /// 用户自己填的 CurseForge API key。**空 = 走社区反代**（`api.curse.tools`，不要 key）。
    ///
    /// 【为什么要用户自己填】官方那份 `api.curseforge.com` 要 `x-api-key`（不带 = 403，
    /// 2026-09-25 实测）。而 key 一旦编进 exe 就等于公开（谁都能从二进制里抠出来），
    /// 泄露的 key 会被吊销 —— 所以默认走反代，用户想走官方就自己填一份。
    /// 判据、两条路的取舍、以及"反代可能哪天就没了"那件事写在
    /// `ModpackImport::curseForgeApiBase()` 上面。
    QString curseForgeApiKey() const { return m_curseForgeApiKey; }

    /// 设置 CurseForge 的 key 并立刻落盘。传空串 = 回到默认那条路（社区反代）。
    void setCurseForgeApiKey(const QString &apiKey);

    /// 把 CurseForge 的接口指到别的地址上（如 `http://127.0.0.1:8080/v1`）。
    ///
    /// NOTE: 这个键**只读不写** —— `save()` 里刻意没有它。它存在的唯一理由是让验证工程把
    ///       接口指到本机那个假服务器上（整合包导入的离线端到端那条路），所以产品这一侧
    ///       不可能"因为某个 bug 把用户的接口地址改掉"。填了它时它的优先级最高。
    QString curseForgeApiBaseOverride() const { return m_curseForgeApiBaseOverride; }

    // ------------------------------------------------------------------
    // 「启动选项」那三项（设置页里同一组）
    // ------------------------------------------------------------------

    /// 启动游戏时要不要给它开一个日志窗。默认关（见 ui/GameLogWindow 的说明）。
    bool showGameLog() const { return m_showGameLog; }

    /// 设置「显示游戏日志」并立刻落盘。
    void setShowGameLog(bool show);

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

    /// 当前优先级。默认 Normal（**显式设**，理由见 ProcessPriority 的 NOTE）。
    ProcessPriority processPriority() const { return m_processPriority; }

    /// 设置优先级并立刻落盘。
    void setProcessPriority(ProcessPriority priority);

    /// 游戏窗口大小的取值范围；load()/save() 都拿它钳位。
    ///
    /// NOTE: 界面那一份是 MainWindowUI::windowMinWidth() 那一组 —— **必须和这里一样**
    ///       （ui/ 不许 include src/，所以是各写一遍，验证工程里有断言盯着它们相等）。
    ///       和「内存分配」那对上下限是同一个约定：两边不一致的话，界面允许填的值会在
    ///       读回来时被悄悄改掉，用户看到的是"填了不生效"。
    static int gameWindowMinWidth() { return 320; }
    static int gameWindowMaxWidth() { return 7680; }
    static int gameWindowMinHeight() { return 240; }
    static int gameWindowMaxHeight() { return 4320; }

    // ------------------------------------------------------------------
    // 顶上那行字（设置页「外观」那一项，2026-09-25 加的）
    // ------------------------------------------------------------------

    /// 条上写着的那行字。
    ///
    /// **默认是空串** = "没改过"，条上那时候用的是 `TitleBar::defaultBrandText()` 那五个
    /// 字母 —— 所以这里刻意**不抄一份默认值**：默认值只有条那一个出处（`ui/TitleBar.hpp`），
    /// 抄一份在这里就会出现"两边都叫默认、改了一边就对不上"。
    QString titleText() const { return m_titleText; }

    /// 设置条上那行字并立刻落盘。内部先去掉两头的空白、再按 titleTextMaxLength() 截断。
    void setTitleText(const QString &text);

    /// 这一项的长度上限；load()/save() 都拿它截断（界面那一侧是输入框的 maxLength）。
    ///
    /// NOTE: 界面那一份是 MainWindowUI::titleTextMaxLength() —— **必须和这里一样**，
    ///       理由和上面那对窗口大小一模一样（ui/ 不许 include src/，所以是各写一遍，
    ///       验证工程里有断言盯着它们相等）。不一样的话症状是"打上去的字自己少了几个"。
    ///       16 是按条上留给名字的那点地方量的，见 MainWindowUI 那一处的说明。
    static int titleTextMaxLength() { return 16; }

    // ------------------------------------------------------------------
    // 「启动提醒」（设置页里同一组，2026-09-25 加的）
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
    /// 默认 **true**（见上）。判据与那个十秒的来历见 MainWindow::confirmDuplicateLaunch()。
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
    QString m_lastLaunchedVersion;
    QStringList m_folders;
    QString m_currentFolder;
    QStringList m_folderNames;
    VersionNameOptions m_versionNameOptions;
    LauncherBehaviour m_launcherBehaviour = LauncherBehaviour::Unchanged;
    DownloadSource m_downloadSource = DownloadSource::Official;
    bool m_showGameLog = false;
    /// 默认值取原版的默认窗口大小（1.12.2 的 Main 里那对 defaultsTo(854, 480)）：
    /// 谁都不动这一项时，启动器发出去的那两个参数就等于游戏自己本来要用的值 ——
    /// "加了这个功能"不该让任何人的窗口大小变一下。
    int m_gameWindowWidth = 854;
    int m_gameWindowHeight = 480;
    ProcessPriority m_processPriority = ProcessPriority::Normal;
    /// 顶上那行字。**空串是默认档**（= 条上那五个字母），不是"没有名字"。
    QString m_titleText;
    /// 两条启动提醒。**默认开**：它们是"挡一下手滑"的，默认关就等于没做。
    bool m_warnDuplicateLaunch = true;
    bool m_warnWorldLoss = true;
    /// CurseForge 那两项（见上面那两个访问器）。**都是空串 = 默认档**：
    /// 走社区反代、不要 key。
    QString m_curseForgeApiKey;
    QString m_curseForgeApiBaseOverride;
};
