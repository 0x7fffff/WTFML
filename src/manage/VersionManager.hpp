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
    QJsonObject getVersionJson(const QString &versionId) const;

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

    DownloadManager *downloadManager;
    // versionId -> versionJson。mutable 是因为 getVersionJson() 是 const 的，
    // 但它在回读磁盘后要顺手填充这个缓存。
    mutable QMap<QString, QJsonObject> versionJsons;
    QString m_currentVersionDownloading;
    // 正在取 version.json 的版本号。拿到手的 JSON 里没有 "id" 字段时（网络失败、
    // manifest 里没有这个版本），就靠它告诉 UI"这次失败了"，否则 UI 会一直等下去。
    QString m_pendingVersionId;
    // 版本列表还没拉过时要先拉列表，拿到之后再回来取 version.json。
    // 这里记下待取的版本号，续接动作放在 onVersionManifestFetched 里。
    QString m_fetchJsonAfterManifest;
};
