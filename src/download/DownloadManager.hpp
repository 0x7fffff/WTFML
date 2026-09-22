#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

/**
 * @file DownloadManager.hpp
 * @brief WTFML的下载与版本列表功能声明。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 定义版本和 asset 的数据结构和信号。
 *
 * 负责获取 mojang 版本列表、 library 下载、 asset 和客户端 jar 文件、验证 SHA1 完整性。
 */

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQueue>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>

class QTimer;

// Minecraft 数据的结构
struct MinecraftVersion {
    QString id;
    QString type;
    QString url;
    QString time;
    QString releaseTime;
};

struct Library {
    QString name;
    QString url;
    QString sha1;
    qint64 size = 0;
    QString path;
    // 下面两个字段由 extractLibraries() 从 LibraryRules::nativeClassifier() 的结果填好。
    // 产品代码里下载和启动都不看它们（natives 的解压在 LauncherManager::prepareNatives()，
    // 那边自己重新解析一遍 version.json），但**验证工程会读 isNative**（拿它核对"这个库
    // 是 natives classifier 还是普通库"），所以别按"没人用"删掉。
    //
    // 1.18.2 及以下的 natives 是 downloads.classifiers 里的独立 jar，
    // 它不进 classpath，而是启动时解压到 versions/<id>/natives/。
    bool isNative = false;
    // 该 jar 内需要跳过的条目前缀，来自 library 的 extract.exclude（通常是 META-INF/）。
    // 真正用它的地方是 prepareNatives（从 version.json 重新算一遍），这个字段本身
    // 目前**只写不读**。
    QStringList extractExclude;
};

struct AssetIndex {
    QString id;
    QString sha1;
    qint64 size;
    QString url;
    qint64 totalSize;
};

/**
 * @brief 决定一个已经存在于磁盘上的文件要不要重新下载。
 *
 * 重新点"下载"时用它来跳过已经完好的文件，避免整包重下。
 */
enum class VerifyMode {
    ExistsOnly,   // 文件存在就算好（用于海量 asset，全量哈希会让界面卡住几秒）
    SizeAndSha1,  // 先比字节数，再比 SHA1（用于客户端 jar 和 library）
};

/**
 * @brief 一批下载任务的统计，供 UI 显示进度和汇总失败原因。
 */
struct DownloadStats {
    int total = 0;    // 本批登记的任务总数（含被跳过的）
    int done = 0;     // 已结束的（成功、失败、跳过都算）
    int failed = 0;   // 其中失败的
    int skipped = 0;  // 其中因为文件已存在而跳过的
    QStringList failedPaths;  // 失败文件的完整路径，比 40 位哈希文件名可读
};

class DownloadManager : public QObject {
    Q_OBJECT

public:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager();

    // ------------------------------------------------------------------
    // 下载源：官方 / 镜像
    // ------------------------------------------------------------------
    // 界面那边是「下载源」下拉框（LauncherSettings::DownloadSource，存 QSettings），
    // 这里只留一个布尔：DownloadManager 不需要知道这个设定存在哪、怎么持久化。
    // 设置是**发请求那一刻**读的，所以改完立刻对下一次下载生效，不用重建对象。

    /// @param useMirror true = 走镜像源（BMCLAPI，第三方服务）。
    void setUseMirror(bool useMirror) { m_useMirror = useMirror; }
    bool useMirror() const { return m_useMirror; }

    /// 现在这一刻还该不该走镜像 = 用户选了镜像源，**而且**镜像没因为太慢被临时降级。
    /// 凡是"要不要换成镜像地址"的判断都问它，别直接读 m_useMirror —— 降级是运行期发生的，
    /// 直接读会绕过它。
    ///
    /// 为什么要降级而不是"镜像慢就等着"：镜像的**非 maven 路由**在 2026-09-22 实测一律
    /// 302 到 `minio.749333.xyz`（预签名 URL，60 秒过期），那条链子本机上只有 0.85~10 KB/s
    /// （209 KB 的版本清单 21 秒、34 KB 的版本 json 40 秒、13 KB 的资源对象 25 秒），
    /// 而同一时刻官方那条是 1.2~2.9 秒。更要命的是 Qt 的 setTransferTimeout() 是"不活动
    /// 超时"，细水长流地掉字节永远重置它 —— 于是既不失败（等不到兜底）也完不成，整条安装
    /// 挂着不动（实测：三个 socket 挂着、一个文件不落、CPU 冻在 0.125 秒）。
    bool mirrorPreferred() const;

    /// 镜像是不是正处在"太慢、先不用"的降级期里（见 demoteMirrorForSlowDownload()）。
    bool isMirrorDemoted() const;

    /// 把镜像降级一段时间：这期间所有下载直接走官方。由失速看门狗调用；验证工程也直接调它
    /// （`verify.exe` 的 V6e 那几条，本地造不出"慢到触发看门狗"的服务器）。
    /// NOTE: 重复调用会把期限**往后顺延**（不是每叫一次重开一段），所以连着失速只会让
    ///       这段降级更长，不会来回切。
    void demoteMirrorForSlowDownload();

    /// 降级期多长：5 分钟。短了会在"镜像只是刚才那一条慢"上反复切换，长了会在镜像已经
    /// 好了以后还用不上它。
    static constexpr qint64 kMirrorDemoteMs = 5 * 60 * 1000;

    /// 两个**纯函数**（时间与字节数的判据），verify 里按边界钉过。
    /// @{
    /// 降级期过了没有。@param demotedUntilMs 0 = 从来没降级过。
    static bool mirrorDemotedAt(qint64 nowMs, qint64 demotedUntilMs);
    /// 这一跳"没在动"吗（进账不到下限就算）。
    static bool mirrorStalled(qint64 bytesDelta, qint64 minBytesPerTick);
    /// 又发现一次失速之后，降级期该到什么时候 —— 取晚的那个（**顺延**，不提前）。
    static qint64 demoteDeadline(qint64 nowMs, qint64 currentUntilMs);
    /// @}

    /// 按**现在这一刻**的下载源，把一条"地址自带身份"的官方地址落到实际请求的地址上
    /// （镜像被降级时给回官方那条）。按版本号/索引号拼的那几处走 mirrorVersionJsonUrl()
    /// 之类的函数，不经过这里。
    QString endpoint(const QString &officialUrl) const;

    // ------------------------------------------------------------------
    // 官方地址 → 镜像地址。**纯函数**，不碰成员、不发请求。
    // NOTE: "纯函数所以好测"这个理由当初只是写在这里，四年没人兑现 —— 2026-09-22 才补上
    //       第一批断言（verify 的 V8 那一节，含镜像映射和 mirrorMavenRootUrl() 的结尾斜杠）。
    //       这批函数不需要 QApplication，是现有代码里最容易钉住的一批，以后改地址先看 V8。
    // ------------------------------------------------------------------
    // 为什么要三个专门的函数、而不是一个"换 host"的：BMCLAPI 对一部分资源用了
    // 完全不同的路径形状，必须拿**版本号 / 索引号**重拼。
    //   piston-meta 的 /v1/packages/<sha1>/<id>.json → /version/<id>/json（或 /indexes/<id>.json）
    //   piston-data 的 /v1/objects/<sha1>/client.jar → /version/<id>/client
    // 而这三处的 sha1 在地址里、id 在调用方手上，光看 URL 分不出是版本 JSON 还是
    // 资源索引（两者形状一模一样，只能靠上下文区分是哪个 id）。

    /// 把一条**地址自带身份**的官方地址换成镜像地址。
    /// @param officialUrl 官方地址（版本清单 / 资源对象 / 库文件）。
    /// @return 镜像地址；**认不出来的地址原样返回**。
    ///
    /// 认不出来就原样返回是刻意的：宁可这一条走官方，也别拼一个不存在的镜像地址出来
    /// ——拼错的表现是"下载全失败"，比"有一部分没走镜像"难查得多。
    ///
    /// 认得的 host（每一条都在 .cpp 里写着实测日期和证据）：版本清单（launchermeta）、资源
    /// 对象（resources.download）、标准 maven 布局的那四个（libraries.minecraft.net /
    /// maven.minecraftforge.net / files.minecraftforge.net / maven.neoforged.net 的 releases 支）。
    /// 现在还会原样过去的是 Fabric 的库（maven.fabricmc.net，没实测过就不加）和 NeoForge 的
    /// snapshots 支（镜像上确实没有）。
    static QString mirrorUrl(const QString &officialUrl);

    /// 镜像源里 maven 那一支的根地址，**结尾带斜杠**。
    ///
    /// 只有一个用处：喂给 Forge / NeoForge 安装器的 `--mirror`。那两个安装器是同一套框架，
    /// 支持用命令行覆盖它自己那份镜像列表；不给的话它挑中的是 creeperhost（本机上实测：
    /// 48 个库全从那儿来，61 秒；给这个地址 30 秒、48 个全从 bmclapi 来、一次回退都没有）。
    static QString mirrorMavenRootUrl();

    /// 这条作业要不要登记"官方备用地址"（镜像取不回来时拿它再试一次）。
    ///
    /// **纯函数**，就为了能钉住那两条边界（verify 的 V6d）。判据是"两个地址**不一样**":
    /// 一样的时候"退回官方"等于原样再发一次，只会把同一个失败重复一遍、还多花一个
    /// 30 秒传输超时。空串 = 调用方没给，也就是这条作业没有第二条路（本来就走官方、
    /// 或者镜像上压根没有对应的官方地址）。
    static bool shouldRecordOfficialFallback(const QString &requestUrl,
                                            const QString &officialUrl) {
        return !officialUrl.isEmpty() && officialUrl != requestUrl;
    }

    /// 版本 JSON：官方是 /v1/packages/<sha1>/<id>.json，镜像按版本号取。
    static QString mirrorVersionJsonUrl(const QString &versionId);

    /// 客户端 jar：官方是 /v1/objects/<sha1>/client.jar，镜像按版本号取。
    static QString mirrorClientJarUrl(const QString &versionId);

    /// 资源索引 JSON：官方是 /v1/packages/<sha1>/<id>.json，镜像按索引号取。
    ///
    /// NOTE: 这一条是整套镜像里唯一**实测当下是坏的**：2026-09-22 拿 legacy/1.18/1.20/1.21/5/32
    ///       挨个打过去，镜像上全是 404（同一时刻 /version/<id>/json、/version/<id>/client、
    ///       /assets/<hash> 都是 200）。地址本身没拼错（它自己的文档里就写着这个形状），所以
    ///       这里保留映射、由 downloadAssets() 那一侧兜底退回官方 —— 别把这条删了当"没用"。
    static QString mirrorAssetIndexUrl(const QString &assetIndexId);

    /// 版本清单的官方地址。
    static QString officialVersionManifestUrl();

    // 版本列表操作
    void fetchVersionManifest();
    QList<MinecraftVersion> getAvailableVersions() const;

    // 版本 JSON 文件操作
    void fetchVersionJson(const QString &versionId);
    QJsonObject getVersionJson() const;

    // 下载操作
    // @param expectedSize 文件应有的字节数，传 -1 表示未知（旧版本 JSON 和 natives
    //        classifier 都没有 size 字段），此时退化成只比 SHA1。
    // @param mode 见 VerifyMode，决定已存在的文件要不要重下。
    // @param officialUrl 这条地址**换成镜像之前**的那条官方地址（可选）。给了它，
    //        镜像取不回来时会拿它再试一次（见 m_officialFallbacks）。
    //        不给 = 没有"退一步"这回事（本来就走官方、或者镜像上就没有第二条路）。
    void downloadFile(const QString &url, const QString &destinationPath,
                      const QString &expectedSha1 = QString(), qint64 expectedSize = -1,
                      VerifyMode mode = VerifyMode::SizeAndSha1,
                      const QString &officialUrl = QString());
    void downloadLibraries(const QList<Library> &libraries, const QString &basePath);
    /// 资源索引和资源对象都落在 AppPaths::assetsDir() 下，也就是 --assetsDir 指的那个
    /// 目录。调用方**不能**指定别处：索引和 objects 必须同源，否则游戏按 --assetsDir
    /// 去找 indexes/<id>.json 就会扑空。
    /// @param assetIndex version.json 的 assetIndex 节点，必须带 id / url / sha1。
    void downloadAssets(const AssetIndex &assetIndex);
    /// @param versionId 版本号。镜像源下客户端 jar 是**按版本号**取的
    ///        （官方是按 sha1 取），所以这个参数是必需的，不能只给 URL。
    void downloadClientJar(const QString &versionId, const QString &url,
                           const QString &destinationPath, const QString &expectedSha1,
                           qint64 expectedSize = -1);

    // 批次记账。beginBatch() / endBatch() 之间的所有下载被看作一批，
    // 整批结束时 allDownloadsFinished 只会发射一次；两者之间不允许提前 return，
    // 否则 m_batchOpen 永远为真，完成信号永不发射，UI 会一直停在"下载中"。
    void beginBatch();
    void endBatch();
    /// 放弃当前批次：**一个完成信号都不发**，只把"批次开着"这个状态清掉。
    ///
    /// 只有一个调用者：VersionManager 的异常兜底（见那边 deferNextStep() 的 NOTE）。
    /// 正常的收尾一律走 endBatch()。异常正好打在 beginBatch() 和 endBatch() 之间时，
    /// m_batchOpen 会一直是 true，之后每一次下载都会被 isBusy() 顶回来（界面表现是
    /// "已有下载任务正在进行"，只能重启）—— 这个方法就是给那条路兜底的。
    ///
    /// 为什么不能拿 endBatch() 代替：endBatch() 会跑 checkAllDownloadsFinished()，那一句
    /// 有可能**同步**发出 allDownloadsFinished，于是又回到"在槽里接着干下一件事"这件正在
    /// 收尾的事上，而且那一次并不是真的收尾、报出去的话也是错的。所以这里把 m_batchEmitted
    /// 直接置真：之后所有"这一批结束了"的判断都会被挡掉。已经在飞的那几个回复照样跑完
    /// （文件照落盘），只是不再有任何完成信号。
    void abandonBatch();
    bool isBusy() const;
    DownloadStats stats() const;

    // 工具方法：哈希/校验，以及把 version JSON 解析成上面那些结构体（版本清单和单个
    // 版本 JSON 各一个入口）。留在 public 是给验证工程用的 —— 它会直接调
    // extractLibraries()/extractAssetIndex() 断言"选中了哪些库、natives classifier
    // 挑得对不对"。verifyFileSha1()/calculateFileSha1() 目前只有本类自己调。
    bool verifyFileSha1(const QString &filePath, const QString &expectedSha1);
    QString calculateFileSha1(const QString &filePath);
    void parseVersionManifest(const QJsonDocument &json);
    void parseVersionJson(const QJsonDocument &json);
    QList<Library> extractLibraries(const QJsonObject &versionJson);
    AssetIndex extractAssetIndex(const QJsonObject &versionJson);

signals:
    void versionManifestFetched(const QList<MinecraftVersion> &versions);
    void versionJsonFetched(const QJsonObject &versionJson);
    void downloadProgress(const QString &fileName, qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(const QString &fileName, bool success,
                          const QString &errorMessage = QString());
    // 进度用标量发，避免每来一个网络分片就拷贝一份 DownloadStats。
    void batchProgress(int done, int total, int failed, const QString &currentFile);
    void allDownloadsFinished(bool success);

private slots:
    /// 看门狗每跳一次（5 秒）：看"在看之下"的那些回复动没动，没动就降级 + abort。
    void onStallWatchdogTick();
    void onVersionManifestReplyFinished();
    void onVersionJsonReplyFinished();
    void onDownloadReplyFinished();
    void onDownloadReplyProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onAssetIndexDownloaded();

private:
    QNetworkAccessManager *networkManager;
    QList<MinecraftVersion> availableVersions;
    QJsonObject currentVersionJson;
    // 两张表都以 QNetworkReply* 为键，回复一结束就从表里摘掉（onDownloadReplyFinished）。
    QMap<QNetworkReply *, QString> activeDownloads;  // reply -> 目标路径
    QMap<QNetworkReply *, QString> expectedSha1s;    // reply -> 期望的 SHA1
    /// **官方备用地址**（reply -> 官方 URL）。只在"这条作业走的是镜像"时登记。
    ///
    /// 一开始只为资源索引开这个口子（镜像那条 `/indexes/<id>.json` 路由实测全 404，
    /// 而索引拿不到 = 那几千个资源对象一个都不知道要下谁）。2026-09-22 的 NeoForge 26.2
    /// 验收把口子撑开了：兜底让索引下成之后，失败变成 **38 个资源对象**，逐条 curl 探
    /// 那 38 个发现镜像不是 404 —— 它 302 跳到 openbmclapi 的兄弟节点（本地节点没缓存
    /// 这个对象时的正常行为），而那个节点慢到 13 KB 要 25 秒、有的 30 秒拿不全；官方那条
    /// 同一时刻 1 秒就给了。于是 30 秒的传输超时一到，整批就报"原版没下全"。
    /// 所以现在是**所有**走镜像的下载都能退一步（库 / 资源对象 / 客户端 jar / 安装器 jar）。
    QMap<QNetworkReply *, QString> m_officialFallbacks;
    int pendingDownloads;

    /// 等着发的资源对象。**只有这一种下载会排队**：一次资源补齐是 3000 多个对象，
    /// 全塞给 QNetworkAccessManager 的话 Qt 每个主机只开 6 条连接，其余的在它内部
    /// 干等，而 30 秒的传输超时**从请求建好就开始算**，于是它们排到队尾之前就成批地
    /// 报 `Operation timed out` —— 网络其实是通的。
    /// NOTE: 排在这里的作业已经计进 m_totalJobs（进度条的分母），但**不计进**
    /// pendingDownloads（那是"已经发出去的"），所以 checkAllDownloadsFinished()
    /// 必须把这条队列也算上，否则会在还排着几千个的时候就宣布整批下完。
    struct AssetJob {
        QString url;
        QString destinationPath;
        QString expectedSha1;
        // 排队时也得把它带上：作业在队列里等的这段时间，是没法回头问调用方要的。
        QString officialUrl;
    };
    QQueue<AssetJob> m_pendingAssetJobs;

    // 批次记账
    int m_totalJobs = 0;
    int m_doneJobs = 0;
    int m_failedJobs = 0;
    int m_skippedJobs = 0;
    QStringList m_failedPaths;
    QString m_currentFile;
    bool m_batchOpen = false;
    bool m_batchEmitted = false;
    // 析构时 abort() 可能同步回调进 finished 处理函数，用它挡住
    bool m_shuttingDown = false;
    // 下载源。false = 官方（默认，也是加这一项之前的行为）。
    bool m_useMirror = false;

    // ------------------------------------------------------------------
    // 失速看门狗（见 .cpp 顶部那段实测记录）
    // ------------------------------------------------------------------
    /// 一条"在看之下"的回复的慢不慢记账。**只有**走了镜像、而且登记过官方备用地址的
    /// 回复才会进这张表 —— 别的回复没有退路，abort 掉它就是纯粹把这次下载弄失败。
    struct StallMeter {
        qint64 bytesSeen = 0;      ///< 最近一次 downloadProgress 报的累计字节
        qint64 lastTickBytes = 0;  ///< 上一跳时看到的那个值
        int strikes = 0;           ///< 连着几跳"没动"
    };
    QHash<QNetworkReply *, StallMeter> m_stallMeters;
    QTimer *m_stallWatchdog = nullptr;
    QElapsedTimer m_clock;
    /// 降级期的截止时刻（m_clock 的毫秒读数）。0 = 从来没降级过。
    qint64 m_mirrorDemotedUntil = 0;


    /// 从镜像取失败时，用官方地址**重发一次**（作业还是同一个，不算失败）。
    /// @param reply 刚失败的那条回复。
    /// @param destinationPath 落盘路径（原样搬给重发出去的那一条）。
    /// @param finishedSlot 重发出去那条回复该接哪个 finished 槽 —— 索引有自己的
    ///        （onAssetIndexDownloaded，成功了还要解析索引），别的都走通用的那个。
    /// @return 真的重发了 = true（调用方必须立刻 return，别走结账那一段）；
    ///         没有备用地址 = false（调用方按普通失败处理）。
    bool retryWithOfficialUrl(QNetworkReply *reply, const QString &destinationPath,
                              void (DownloadManager::*finishedSlot)());

    void startDownload(const QUrl &url, const QString &destinationPath,
                       const QString &expectedSha1 = QString(),
                       const QString &officialUrl = QString());
    /// downloadFile() 的本体。@param throttled 为真时**不立刻发请求**，只排进
    /// m_pendingAssetJobs，等有连接空出来再发（见 pumpAssetQueue()）。
    void scheduleFile(const QString &url, const QString &destinationPath,
                      const QString &expectedSha1, qint64 expectedSize, VerifyMode mode,
                      bool throttled, const QString &officialUrl = QString());
    void pumpAssetQueue();
    void handleDownloadError(QNetworkReply *reply, const QString &errorMessage);
    void checkAllDownloadsFinished();
    void emitBatchProgress(const QString &currentFile);
    bool fileAlreadyValid(const QString &path, const QString &expectedSha1, qint64 expectedSize,
                          VerifyMode mode);
    void parseAndDownloadAssets(const QString &indexPath);
};

Q_DECLARE_METATYPE(MinecraftVersion)
Q_DECLARE_METATYPE(Library)
Q_DECLARE_METATYPE(AssetIndex)

#endif  // DOWNLOADMANAGER_H