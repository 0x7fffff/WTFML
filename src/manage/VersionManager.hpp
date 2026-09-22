/**
 * @file VersionManager.hpp
 * @brief Declares version discovery, metadata fetching, and download orchestration.
 * @author LoserKugua
 * @date 2026-04-10
 * @note 该模块负责版本列表、版本详情获取和版本下载管理。
 */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTimer;

#include "../download/DownloadManager.hpp"
#include "VersionDisplay.hpp"

class VersionManager : public QObject {
    Q_OBJECT

public:
    explicit VersionManager(QObject *parent = nullptr);
    ~VersionManager();

    // Version management
    // 扫 .minecraft/versions，返回磁盘上真正装好了的版本号。判据是"认得出客户端 jar"
    // —— 先认 <id>/<id>.jar，认不到再看顶层是不是**恰好一个** jar（用户自己在资源管理器
    // 里改过目录名的实例就靠这条被认出来）。判据本体在 InstanceFiles。
    // 一个都没有时返回空列表，不再塞 "自定义版本" 占位项。
    //
    // warnings 是可选的收集器：某个目录里**有 jar、但认不出哪个是客户端 jar**（顶层不止
    // 一个、又没有和目录名同名的那个）时，往里追加一句说明。一个 jar 都没有的目录
    // （下载残骸、纯空目录、误丢进来的整合包 zip）不报 —— 那些本来就不像实例。
    // 传 nullptr 时只是不收告警，返回的列表逐项一致。
    QStringList installedVersions(QStringList *warnings = nullptr) const;
    QStringList availableVersions() const;

    // Version fetching from Mojang API
    void fetchAvailableVersions();
    void fetchVersionDetails(const QString &versionId);

    // Version downloading
    void downloadVersion(const QString &versionId, const QString &destinationPath);

    // ---- 带模组加载器的版本 ----

    /// 下载页那四个单选钮对应的加载器。
    enum class LoaderKind { Fabric, Forge, NeoForge, OptiFine };

    /// 这个加载器现在接通了没有。**Fabric / Forge / NeoForge 接通了，OptiFine 还没有。**
    //
    // 为什么要有这个函数、而不是让界面自己判断：界面要据此决定"点了下载之后弹哪一种话"
    // （能做 / 还没做）。两边各写一份 switch 的话，接通一个加载器时漏改一处，症状是
    // "明明能装却弹一句『还没做』"—— 或者更糟，反过来。
    //
    // NOTE: Fabric 和另外两个**不是同一条路**：Fabric 是我们自己拉 json + 下库，
    //       Forge/NeoForge 是**另起一个 Java 进程跑它们自己的安装器**。这个函数只说
    //       "能不能装"，不说"怎么装"，所以两者在它眼里一样。
    static bool loaderInstallSupported(LoaderKind kind);

    /// 写进 wtfml.meta 的 `loaderName`（显示名里那一段）。比如 "Fabric"。
    static QString loaderDisplayName(LoaderKind kind);

    /// 一共几个加载器（界面那组单选钮里加载器选项的个数）。
    static int loaderKindCount();

    /**
     * @brief 界面那组单选钮里的下标 -> 加载器。
     * @param index QButtonGroup 的 id（= MainWindowUI 那两张表里的下标）
     * @param out 成功时写入；失败时不碰
     * @return 这个下标对应一个加载器吗（-1「一个都没勾」、以及「不装」那个下标都是 false）
     *
     * 为什么要有这么一层：界面上"勾了哪个"是个下标，而"装什么"是个枚举，中间那次翻译
     * 要是各写各的 switch，多一个加载器时漏改一处就是"勾了 Fabric 装出 Forge" —— 不报错，
     * 装完才发现。所以翻译只有这一处，verify 里按名字逐项对过。
     */
    static bool loaderKindFromIndex(int index, LoaderKind *out);

    /**
     * @brief 装一个带模组加载器的版本。
     * @param gameVersion 它基于的**原版**版本号（必须在"可安装版本"那张清单里）。
     * @param kind 装哪一种加载器。
     * @param javaPath 用哪个 Java 跑**外部安装器**（Fabric 用不上它，传空即可）。
     *        空 = 用 PATH 上的 `java`。界面上传的是"设置"里配的那个。
     *
     * 【Fabric】一次点击，两批下载：
     *   1. 拉加载器列表 → 挑一个版本（"最新那个正式版"）；
     *   2. 拉它的 profile json —— 那份**薄 json** 就是要落盘的版本 json；
     *   3. 原版没装就先把它按老路装好（客户端 jar + 库 + 资源，那才是能玩的那一半）；
     *   4. 再下加载器自己那几个库（maven 风格，见 LibraryRules::artifactFor），
     *      并把 `realVersion`/`loaderName`/`loaderVersion` 写进 <id>/wtfml.meta。
     *
     * 【Forge / NeoForge】它们装出来的东西里有**二进制补丁**（拿原版的客户端 jar 打出一份
     * srg 名 jar、再打出一份 client jar），不是靠下载能凑出来的，所以这条路是：
     *   1. 拉版本表（Forge 是 promotions_slim.json、NeoForge 是 maven-metadata.xml）→ 挑一个；
     *   2. 把它们的安装器 jar 下到 <数据目录>/cache/installers/；
     *   3. **另起一个 Java 进程**跑 `java -jar <安装器> --installClient <游戏目录>`
     *      （异步 QProcess + 心跳 + 硬超时，见 runInstaller）；
     *   4. 核对它留下的那份版本 json（目录名与 inheritsFrom，见 checkInstallerResult）；
     *   5. 安装器**不下资源文件**，所以再走一遍 `downloadVersion(原版)` 把原版补齐
     *      （json / 客户端 jar / 库 / 资源，已有的按老规矩校验后跳过）；
     *   6. 写 wtfml.meta，发完成信号。
     * 它的前置条件只有一条：游戏目录里得有 `launcher_profiles.json`（安装器拿它认目录），
     * 没有就补一份最小的（见 ensureLauncherProfiles）。
     *
     * NOTE: 两条路都在第 3 步（原版那一批）**复用** `downloadVersion()` 那条已经验过的
     *       老路，不是另写一份；所以它绑定的那几个信号（versionDetailsFetched /
     *       versionDownloadFinished）在安装期间会被本类截住，UI 只会在整件事结束时收到
     *       **一个**完成信号 —— 否则用户会在"原版下完"那一刻看到一个"下载完成"的框，
     *       然后加载器还在下/还在装，而按钮已经重新亮起来能点了。
     *
     * 失败一律以 `versionDownloadFinished(loaderId, false, 原因)` 收尾（还没算出
     * loaderId 时用 gameVersion 当 id），`loaderInstallMessage()` 里说清卡在哪一步。
     */
    void installLoaderVersion(const QString &gameVersion, LoaderKind kind,
                              const QString &javaPath = QString());

    /// 有加载器安装正在进行（含"先装原版"那一段）。
    /// UI 用它来兜住"两批下载之间那一瞬间按钮亮着"的缝。
    bool isLoaderInstalling() const;

    // 是否有一批下载正在进行（含"正在校验并跳过已存在文件"的整批）。
    // UI 用它来决定"启动游戏"能不能点。
    bool isDownloading() const;

    // 下载源：官方 / 镜像。转发给内部的 DownloadManager，界面只跟这一层打交道。
    // 改完立刻对**之后发出的**请求生效：库和客户端 jar 的地址在批次开头一次算完，那部分
    // 不会改道；但 5000+ 个 asset 的地址是解析完 asset index 才拼的（而索引回复要等网络），
    // 所以下载中途切源会让资源那一半整体换源。别以为这条路走不通 —— 设置页那个下拉框
    // 在下载期间并没有被禁用。
    void setUseMirror(bool useMirror);

    // Version JSON access
    //
    // **返回的是走完继承链、合并好的那一份**（见 VersionInheritance）：带模组加载器的版本
    // 自己那份 json 里没有主 jar、assetIndex、javaVersion、游戏参数，也没有主类 —— 那些
    // 全在它基于的原版那份里。要是把原始那份直接交给启动路径，症状是"assetIndex 退回 29、
    // Java 退回 21、主类还是原版的 Main"，也就是**加载器像没装一样**。
    //
    // 合并结果按 id 缓存（版本 JSON 一旦落盘就不再变，合并结果同理）。原始那份另有一个
    // 缓存，下载路径用的仍是原始的 —— 写进磁盘的必须是原样的 json，不能是我们合并出来的
    // 那一份（加载器的安装器、别的启动器都按原样读它）。见 rawVersionJson()。
    QJsonObject getVersionJson(const QString &versionId) const;

    // 这个版本的继承链有没有问题：没问题返回空串，有问题返回给用户看的一句话（父版本
    // 没装 / 链成环 / 链太长）。
    //
    // 为什么单独一个函数：`getVersionJson()` 遇到父版本缺失时只能给出"合不完整的那一份"，
    // 而它的调用方（启动路径）分不出"这份 json 本来就长这样"和"父版本没装" —— 后者
    // 必须弹窗说清楚（要用户去装原版），照抄原来那句"请重新下载该版本"会把人引到死路上。
    //
    // NOTE: 只读，不写盘。它在启动路径上被调用。
    QString inheritanceProblem(const QString &versionId) const;

    // 读某个版本参与"显示名"的那些部分（自定义名、模组加载器名/版本）。
    // 数据源是 <id>/wtfml.meta 这个可选文件，读不到就全是空串、显示名退化成纯版本号。
    // `id` 永远用参数填，不从文件里取。
    //
    // NOTE: 文件名是 wtfml.meta 而不是 wtfml.json，这是必须的——<id>.json 是版本 JSON
    // 的既定名字，万一真去下载一个 id 就叫 "wtfml" 的版本，versions/wtfml/wtfml.json
    // 会同时是版本 JSON 和元数据文件。后果不是崩溃，是更糟的静默错：getVersionJson()
    // 读到一份没有 "id" 字段的对象**仍然返回它**，调用方只判 isEmpty() 就会一路绿灯
    // 走到兜底的 assetIndex/javaMajorVersion，也就是"静默选错 Java"那个老 bug。
    // 换个扩展名，这个撞名在结构上就不可能发生。
    VersionDisplayParts displayParts(const QString &versionId) const;

    // 读某个版本自定义的 JVM 参数（wtfml.meta 里可选的 "jvmArgs" 字符串数组）。
    // 读不到就是空列表，调用方不必区分"文件没有"和"字段没有"。
    //
    // 用途：PCL 允许用户自定义 Java 启动参数，这里预留"按版本"的那半边 ——
    // "全局"那半边是 GlobalJavaSettings::extraJvmArgs()，早就生效了。
    // NOTE: 参数本身可以做任何事（包括把 JVM 起不来），不做白名单校验 —— 这是用户
    //       自己写的文件、自己指定的版本，和直接改命令行是一回事。
    QStringList versionJvmArgs(const QString &versionId) const;

    // meta 里"追加 JVM 参数"那个字段名。
    //
    // 公开出来只为一件事：写侧（每实例设置对话框点确定时）在 MainWindow 里，它得
    // 往 updateVersionMeta() 传一个 key。读侧（versionJvmArgs）和写侧各写一遍
    // "jvmArgs" 这个字面量，迟早会有一边写成 "jvmargs" —— 而那个错的形态是
    // **静默不生效**：字符串不匹配不报错，参数就是不进命令行。
    static QString jvmArgsField();

    // 这个实例的「Java 版本与要求不符时不再提醒」开关（wtfml.meta 里可选的
    // "skipJavaVersionWarning"）。字段缺失、读到坏文件、类型不对，全都是 false = **要提醒**。
    //
    // NOTE: 默认为什么是"提醒"而不是"免打扰"：这个键只有用户点过「以后不再提醒这个实例」
    //       才会被写下，没写过就是"没表过态"；而"没表态"在加这个功能之前等于一律报错拦下。
    //       反过来默认（缺省即免打扰）会让老实例一升级就静默放行一个版本不对的 Java，
    //       而静默选错 Java 正是这一层最怕的错。
    // NOTE: 类型不对（写成字符串、写成数字）也按"没表态"处理，但要 qWarning 一声 ——
    //       这是用户手写的文件，写错了必须让他看得出来，否则表现成"明明写了却不生效"。
    bool skipJavaVersionWarning(const QString &versionId) const;

    // meta 里那个开关的字段名。公开的理由和 jvmArgsField() 一样：写侧在 MainWindow 里，
    // 它得往 updateVersionMeta() 传一个 key，两边各写一遍字面量的错法是**静默不生效**。
    static QString skipJavaVersionWarningField();

    // 读这个实例的整份 wtfml.meta（文件没有、读不到、解析不了都是空对象）。
    //
    // 这是"每实例设置"的读侧入口：MainWindow 拿它喂给 javaOverrideFromJson()。
    // 单独开一个公开读口而不是让调用方去凑 displayParts + versionJvmArgs 两个
    // 半截的读侧 —— 覆盖那五项里有四项（javaPath / xmx / xms / autoMemory）现在
    // **没有**任何专门的读函数，逐个加会变成五个功能重复的小函数。
    //
    // NOTE: 只读，不写盘、不生成 instanceId。它在启动路径上被调用。
    QJsonObject versionMeta(const QString &versionId) const;

    // ---- wtfml.meta 的写侧 ----
    //
    // 这个文件以前只有读侧（上面那些）。重命名 / 分组 / 每实例设置三件事都要写它，
    // 写侧放在同一个类里，是因为"什么算坏文件、坏了要不要吭声、类型不对怎么办"
    // 这套判断已经在 readVersionMeta 里成文了，另立一处必然要抄第二遍、然后慢慢抄歪。

    // 把一批字段**合并**写进 <id>/wtfml.meta，其余字段原样保留。
    //
    // @param fields 要写的字段；值为 QJsonValue::Null 表示**删掉**该字段。
    //               NULL 这个约定是给"改回跟随全局"用的 —— 那种情况要的是"这个键不存在"，
    //               而不是"这个键等于全局的值"（后者会让这个实例在用户改全局之后不跟着走）。
    // @param errorOut 失败原因（给用户看的一句话）。数据层不弹窗、也不只写 qWarning
    //                 （用户看不见），一律交给调用方去弹。
    // @return 写成功返回 true。
    //
    // NOTE: 合并而不是整体替换。这个文件是用户可以手写的，里面可能已经有 jvmArgs /
    //       nameFormat / realVersion —— 整体覆盖写会把它们抹掉，而那正是读侧花了力气
    //       支持的东西。同理，文件**存在但解析不了**时**拒绝写**：往一份读不懂的内容上
    //       合并等于把用户手写的部分丢掉，比"这次没写成"糟得多。
    // NOTE: 非 const —— 它真的往磁盘上写东西。"看起来是只读"的假 const 会诱使人在
    //       扫描路径上调用它，而扫描一次会话要跑无数遍、绝不能带副作用。
    bool updateVersionMeta(const QString &versionId, const QJsonObject &fields,
                           QString *errorOut = nullptr);

    // 这个实例的稳定 id（wtfml.meta 里的 "instanceId"）；没有或读不到就是空串。
    // **只读，不生成、不写盘** —— 它在扫描路径上被调用。
    QString instanceId(const QString &versionId) const;

    // 确保这个实例有稳定 id：没有就生成一个写进 meta，返回它；写盘失败返回空串。
    //
    // 生成时机是**懒的**，只在真正需要它的三条写路径上（重命名 / 加入分组 / 改实例设置），
    // 不是扫描时。绝大多数实例一辈子不会被写过 meta，也就不该因为这个安静地多出一个文件。
    QString ensureInstanceId(const QString &versionId);

    // 把这个实例改名：**真改磁盘上的目录名**，并把新名字写进 meta 的显示名。
    //
    // @param oldId 现在的目录名。
    // @param newName 新名字（会 trim）。校验规则在 InstanceFiles::validateRenameTarget()。
    // @param errorOut 失败原因（给用户看的一句话，可能有多行）。
    // @return 全部成功返回 true。
    //
    // 执行顺序是刻意的，每一步都能反悔：
    //   1. 全部前置检查（名字非法 / 重名 / 目录不在）—— 磁盘一个字节都不动；
    //   2. 写 meta（显示名 + 稳定 id）—— 排最前是因为它可能失败，而它失败时我们
    //      **还什么都没破坏**；
    //   3. `<老名>.jar` → `<新名>.jar`，4. `<老名>.json` → `<新名>.json`，
    //      5. 目录 `versions/<老名>` → `versions/<新名>`。
    // 3~5 失败时按 5→4→3 逆序回滚，回滚自身也失败时 errorOut 里会写清**当前磁盘是
    // 什么状态**，绝不静默留下一半改动。
    //
    // 先内后外的另一个好处：崩在第 3、4 步之后留下的状态（jar 名和新目录名还没对上）
    // 恰好被 InstanceFiles 判据的第 ② 条认得出，用户在列表里看得见、还能再改一次。
    //
    // NOTE: "这个实例正在跑"**不在这里判** —— VersionManager 看不见游戏进程，那是
    //       LauncherManager 的状态。运行中的实例在 Windows 上 jar 被 JVM 锁着，等到
    //       rename 失败再报错，用户看到的是一句"拒绝访问"，而该说的是"先关掉游戏"。
    //       所以那道闸口在 MainWindow 的槽里，且必须在调这一句**之前**判完。
    // NOTE: 非 const —— 它真的改磁盘。
    bool renameInstance(const QString &oldId, const QString &newName, QString *errorOut = nullptr);

signals:
    void versionsFetched(const QStringList &versions);
    void versionDetailsFetched(const QString &versionId, const QJsonObject &versionJson);
    // 进度只走这个信号，UI 把它显示成进度条，不再往日志里写。
    void versionDownloadStats(int done, int total, int failed, const QString &currentFile);
    // 语义是"整批结束"，一次下载只会发一次（改动前是每完成一个文件发一次）。
    // errorMessage 在成功时是给用户看的汇总，失败时是失败文件清单。
    void versionDownloadFinished(const QString &versionId, bool success,
                                 const QString &errorMessage = QString());
    // 安装加载器期间给用户看的**过程**话术（"正在获取 Fabric 的版本列表…"、
    // "原版 1.19 还没装，先把它装好…"）。UI 把它写进日志，不弹窗。
    // 单独一个信号而不是复用 stats：stats 那四个数是给进度条用的，往里塞话术会让
    // "进度条显示的是文件数" 这件事变得不可信。
    void loaderInstallMessage(const QString &text);

private slots:
    void onVersionManifestFetched(const QList<MinecraftVersion> &versions);
    void onVersionJsonFetched(const QJsonObject &versionJson);
    void onAllDownloadsFinished(bool success);

private:
    // 读 <id>/wtfml.meta 并解析成对象。文件不存在、打不开、格式错都返回空对象
    // （格式错会告警）。displayParts() 和 versionJvmArgs() 共用这一份读盘逻辑，
    // 免得把"什么算坏文件、坏了要不要吭声"的判断抄两遍、慢慢抄歪。
    // metaPathOut 非空时回填路径，供调用方在告警里指出是哪个文件。
    // unreadableOut 非空时回填"文件在、但读不懂"（打不开或格式错）—— 返回空对象有
    // 两种含义（没有这个文件 / 有这个文件但坏了），写侧必须分得清：前者可以新建，
    // 后者得拒绝写，否则就是把用户手写的内容覆盖掉。
    QJsonObject readVersionMeta(const QString &versionId, QString *metaPathOut = nullptr,
                                bool *unreadableOut = nullptr) const;

    // 磁盘/网络上的那一份，**原样**，不走继承链。getVersionJson() 和继承链的走法都从
    // 这里取数；下载路径（写 `<id>.json` 落盘）也只用它。
    QJsonObject rawVersionJson(const QString &versionId) const;

    // ---- 加载器安装的内部状态机 ----

    // 一次加载器安装走到哪一步了。Idle 之外的任何值都意味着"这件事还没结束" ——
    // 两个地方靠它做判断：onVersionJsonFetched（拦下"原版"那次取 json 的回复，别让
    // UI 再发一次下载）和 onAllDownloadsFinished（那一批结束时是收尾还是接着下一段）。
    struct LoaderInstall {
        enum class Phase {
            Idle,
            FetchingList,           ///< Fabric：正在拉加载器版本列表
            FetchingProfile,        ///< Fabric：正在拉它的 profile json
            FetchingInstallerList,  ///< Forge/NeoForge：正在拉版本表
            WaitingInstallerJar,    ///< Forge/NeoForge：安装器 jar 正在下
            RunningInstaller,       ///< Forge/NeoForge：安装器那个 Java 进程正在跑
            WaitingParentJson,      ///< 正在取原版的 version.json（走 fetchVersionDetails）
            WaitingParentFiles,     ///< 原版那一批正在下（客户端 jar / 库 / 资源）
            WaitingLoaderFiles,     ///< Fabric：加载器自己那几个库正在下
        };

        Phase phase = Phase::Idle;
        LoaderKind kind = LoaderKind::Fabric;
        QString gameVersion;
        QString loaderName;     ///< wtfml.meta 的 loaderName
        QString loaderVersion;  ///< wtfml.meta 的 loaderVersion
        QString loaderId;       ///< 版本目录名，也是最终 finished 信号里的 id
        /// 跑外部安装器用哪个 Java（空 = PATH 上的 `java`）。Fabric 用不上。
        QString javaPath;
        /// 安装器 jar 要下的地址，以及它落在盘上的哪个文件。两条路都在拉完版本表时填好。
        QString installerUrl;
        QString installerPath;

        bool active() const { return phase != Phase::Idle; }
        // 结算用的 id：还没算出 loaderId 时退化成原版号 —— 失败信号总得带一个 id。
        QString reportId() const { return loaderId.isEmpty() ? gameVersion : loaderId; }

        // 收尾（成功、失败都走这里）：回到 Idle，也就是"这件事结束了"。
        // NOTE: 会一起清掉 loaderName/loaderVersion/loaderId —— 要用的字段先在调用前存下来，
        //       忘了存的表现是消息里那个名字变成空的（不崩，但话就说不清了）。
        void reset() { *this = LoaderInstall(); }
    };

    void fetchFabricLoaderList();
    void onFabricLoaderListReply(QNetworkReply *reply);
    void fetchFabricProfile();
    void onFabricProfileReply(QNetworkReply *reply);
    // profile 拿到手之后：先看原版在不在，在就直接下一段，不在就先装原版。
    void startParentOrLoaderFiles();
    // 把加载器 json 落盘并下它自己那几个库（走 downloadVersion 那条老路）。
    void startLoaderFiles();

    // ---- Forge / NeoForge：跑它们自己的安装器 ----

    // 拉版本表（Forge 的 promotions_slim.json / NeoForge 的 maven-metadata.xml）。
    // 两份接口一个 JSON 一个 XML，但都是"拉一段文本、挑一个版本号"，所以共用一条路，
    // 按 kind 在回复处理里分派。
    void fetchInstallerList();
    void onInstallerListReply(QNetworkReply *reply);
    // 把安装器 jar 下到 cache/installers/（一批下载，完成信号回到 onAllDownloadsFinished）。
    void startInstallerDownload();
    // 前置检查 + 起那个 Java 子进程。**异步**：它不阻塞界面，结果由 onInstallerFinished 收。
    void runInstaller();
    // 读子进程的输出（只留尾巴，用来在失败时报"卡在哪"）。
    void onInstallerOutput();
    // 每秒一跳：报心跳，顺带执行硬超时。
    void onInstallerTick();
    void onInstallerError(int processError);
    void onInstallerFinished(int exitCode, bool crashed);
    // 收尾那一路（超时 / 起不来）：先把 phase 挪开再杀进程，然后发失败信号。
    void abortInstaller(const QString &reason);
    // 安装器装完之后把**原版**补齐：它不下 assets，而且 Forge 连原版的 json 都不写
    // （只在 versions/<原版>/ 留下一份客户端 jar）。
    void startParentCompletion();

    // launcher_profiles.json 是那两个安装器的硬前置（缺了它安装器直接拒绝干活）。
    // 没有就补一份最小的；已经有就一个字都不动 —— 里面可能是用户真正的启动器档案。
    // @param createdOut 非空时回填"这次是不是我们新建的"（只有新建才值得跟用户说一声）。
    bool ensureLauncherProfiles(bool *createdOut, QString *errorOut) const;

    // 把 realVersion / loaderName / loaderVersion 合并写进 <id>/wtfml.meta。
    void writeLoaderMeta();
    // 成功/失败的**唯一**终态：写 meta（成功时）、清状态、发 versionDownloadFinished。
    // @param message 那批下载的汇总（"N 个文件已全部就绪"那种），拼进终态话术里。
    void finishLoaderInstall(bool success, const QString &message);
    // 中途失败：清状态、把缓存里那份"还没落盘的 json"扔掉、发一个失败信号收尾。
    // @param reason 给用户看的一句话（谁卡住了、下一步能做什么）。
    void failLoaderInstall(const QString &reason);
    // 安装器输出的最后一行非空内容（心跳和失败报告都用它）。
    QString installerLastOutputLine() const;

    DownloadManager *downloadManager;
    // 加载器元数据那两次 GET 用**自己**的 network manager，不复用 DownloadManager 的：
    // 后者的 fetchVersionJson 是"一次只等一个回复"的（m_pendingVersionId 那种），
    // 混用会让两条路互相把对方的等待状态顶掉。
    QNetworkAccessManager *loaderNetwork = nullptr;
    LoaderInstall m_loaderInstall;
    // 跑 Forge/NeoForge 的安装器那个子进程。懒建（装 Forge/NeoForge 之前一直是 nullptr）
    // —— Fabric 那条路一个进程都不起。
    //
    // NOTE: 它**不能**同步跑（waitForFinished）。实测 Forge 47.4.10 在空目录上装一次要
    //       100 秒上下，NeoForge 更久；阻塞在槽里就是界面整体冻住那么久，连"未响应"
    //       都没有（Qt 的事件循环根本没在转）。所以走异步信号，另外配一个定时器报心跳 +
    //       兜硬超时。
    QProcess *installerProcess = nullptr;
    QTimer *installerTimer = nullptr;
    /// 安装器输出的尾巴（stdout + stderr 混在一起）。只留最后 64 KB：失败时要报"卡在哪
    /// 一行"，但没必要把几万行 "Patching xxx 1/1" 都攥在内存里。
    QByteArray m_installerOutput;
    qint64 m_installerStartedAt = 0;  ///< QDateTime::currentMSecsSinceEpoch()，算超时用
    int m_installerTicks = 0;         ///< 定时器跳了几次，用来决定该不该报心跳
    // versionId -> versionJson。mutable 是因为 getVersionJson() 是 const 的，
    // 但它在回读磁盘后要顺手填充这个缓存。
    mutable QMap<QString, QJsonObject> versionJsons;
    // versionId -> **合并后**的 versionJson（走完 inheritsFrom 链）。单独一张表：
    // 往 versionJsons 里塞合并结果会把"原始那份"盖掉，而下载路径要靠原始那份落盘。
    mutable QMap<QString, QJsonObject> mergedVersionJsons;
    QString m_currentVersionDownloading;
    // 正在取 version.json 的版本号。拿到手的 JSON 里没有 "id" 字段时（网络失败、
    // manifest 里没有这个版本），就靠它告诉 UI"这次失败了"，否则 UI 会一直等下去。
    QString m_pendingVersionId;
    // 版本列表还没拉过时要先拉列表，拿到之后再回来取 version.json。
    // 这里记下待取的版本号，续接动作放在 onVersionManifestFetched 里。
    QString m_fetchJsonAfterManifest;
};
