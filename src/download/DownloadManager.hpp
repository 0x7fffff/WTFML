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
#include <QSet>
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
    ///
    /// NOTE: 这个函数的调用点全是"镜像地址由本类自己拼出来"的那几处（endpoint()、
    ///       fetchVersionJson()、资源索引、客户端 jar），它们的镜像主机**都是 BMCLAPI 那一个**
    ///       —— 所以它问的就是那一个主机的降级状态（见 mirrorDemotedFor()）。
    ///       整合包那条路不一样：它的首选地址是清单里给的（每条文件一个），那一条走
    ///       startDownload() 里按主机问的判据。
    bool mirrorPreferred() const;

    /// 镜像源（BMCLAPI）那个主机是不是正处在"太慢、先不用"的降级期里。
    /// 见 mirrorDemotedFor() —— 降级是**按主机**记的，这一条问的就是镜像源那个主机。
    bool isMirrorDemoted() const;

    /// **某一个主机**正不正在降级期里。
    ///
    /// 降级为什么必须按主机记（2026-09-23 实测的那次事故）：整合包里的"镜像"是**每条文件
    /// 各自的首选地址**（Modrinth 的清单给 2~3 条地址，`preferredUrlOrder()` 把
    /// `cdn.modrinth.com` 排前面），不是全启动器共用的那一个 BMCLAPI。所以一个全局的降级位
    /// 意味着一件事：**一个慢 CDN 上的几条回复，会把别的 CDN 一起关掉**。那次导入里
    /// mediafilez.forgecdn.net 上 8 个文件不动（那条 CDN 单连接实测 22.9 KB/s，它确实慢），
    /// 8 条回复记满 strike 之后降级的是**全体**，把 89% 的字节（1405.9 MB、332 个第一跳本该走
    /// cdn.modrinth.com 的文件）一起赶到了慢 CDN 上 —— 之后 240 秒只进了 42 MB
    /// （0.175 MB/s），而那条 CDN 自己单跑 24 路是 7.2 MB/s。
    ///
    /// @param host 主机名（空串 = 查不到，返回 false）。
    bool mirrorDemotedFor(const QString &host) const;

    /// 把**这个主机**降级一段时间：这期间"首选地址是这个主机"的作业直接走备用地址。
    /// 由失速看门狗按**失速那一条回复自己所在的主机**调用；验证工程也直接调它
    /// （`verify.exe` 的 V6e 那几条，本地造不出"慢到触发看门狗"的服务器）。
    /// NOTE: 重复调用会把期限**往后顺延**（不是每叫一次重开一段），所以连着失速只会让
    ///       这段降级更长，不会来回切。顺延是**按主机**各自算的。
    void demoteMirrorForSlowDownload(const QString &host);

    /// 降级期多长：5 分钟。短了会在"镜像只是刚才那一条慢"上反复切换，长了会在镜像已经
    /// 好了以后还用不上它。
    static constexpr qint64 kMirrorDemoteMs = 5 * 60 * 1000;

    /// 三个**纯函数**（时间与字节数的判据），verify 里按边界钉过。
    /// @{
    /// 降级期过了没有。@param demotedUntilMs 0 = 从来没降级过。
    static bool mirrorDemotedAt(qint64 nowMs, qint64 demotedUntilMs);
    /// 这一跳"没在动"吗（进账不到下限就算）。
    static bool mirrorStalled(qint64 bytesDelta, qint64 minBytesPerTick);
    /// 这一跳该不该记一次"失速"。**一个字节都还没收到的不算**（bytesSeen 0）。
    ///
    /// 为什么要把"还没收到第一个字节"单独摘出去（2026-09-23 那次的直接根因）：这一跳的
    /// 进账 delta 对一条还没开始吐字节的回复来说**天然是 0**，所以 mirrorStalled() 会把它
    /// 判成"链子太慢" —— 可它那根本不是慢，是"还没轮到它"（建连、或者排在一条 h2 连接上
    /// 另外 23 条流后面）。两跳 = 10 秒，于是**任何"建连 + 首字节超过 10 秒"的回复都会记满
    /// strike**，而 strike 满了的动作是 abort 它 + 把整个主机降级 300 秒。
    /// 这种回复自己的判据本来就够用，而且都比 10 秒晚：Qt 那条 30 秒传输超时（从请求建好
    /// 起算）会先掐掉它，走"退回备用地址、不算失败"那条路（见 retryWithOfficialUrl()）；
    /// 真的一点动静都没有的，还有 kNoProgressGiveUpMs 那条兜底。
    ///
    /// NOTE: 这条只摘掉"一次都没报过字节"。**吐了几个字节之后停下来照样算失速** ——
    ///       镜像那条 302 到 MinIO 的链子（0.85~10 KB/s）正是这种细水长流，它一直是这条
    ///       判据存在的主要理由（Qt 的不活动超时对细水长流永远不触发）。
    /// NOTE: `bytesSeen` 的**出处**是 onStallWatchdogTick 里问回复自己要的
    ///       `bytesAvailable()`，不是 downloadProgress（Qt 那个报数对"头 + 一小段 body
    ///       同段到达后停住"的形状到超时才报一次，实测见那边的 (0)）。所以这条判据看的是
    ///       "到手的字节"，跟 Qt 什么时候通知我们无关。
    static bool countsTowardStallStrike(qint64 bytesSeen, qint64 bytesDelta,
                                        qint64 minBytesPerTick);
    /// 又发现一次失速之后，降级期该到什么时候 —— 取晚的那个（**顺延**，不提前）。
    static qint64 demoteDeadline(qint64 nowMs, qint64 currentUntilMs);

    /// 同时最多发出去几个下载。
    ///
    /// 这个数**不是**"Qt 能开几条连接"，而是"我们允许几条在飞"—— 两者在 HTTP/1.1 时代是同一个
    /// 数（Qt 每个主机上限 6），今天不是了：官方那几台 CDN 都会 ALPN 协商出 **HTTP/2**，Qt 6
    /// 默认就用它，几十条流跑在**一条** TCP 连接上（2026-09-23 实测：200 条回复的
    /// `Http2WasUsedAttribute` 全为真；6/24/48 三档下每文件的往返都是 140~143 ms，与并发无关）。
    /// 所以"调大就是把请求塞回 Qt 那个会超时的内部队列"这条老顾虑，只在两种情形下还成立：
    /// 服务端把并发流数（SETTINGS_MAX_CONCURRENT_STREAMS）压得很低（本机实测没有，见下面那组数），
    /// 或者这个主机**根本不是 h2**。后一种由 kMaxConcurrentPerHost 那条判据守着（它没证明自己
    /// 是 h2 之前只放 6 条），所以这个 24 是"全局上限"，不是"每个主机都能 24 条"。
    ///
    /// 2026-09-23 用真端点 + **本类**量了一遍（1.20.1 的全部 3598 个资源对象 / 620.43 MB，
    /// 官方源，一次性探针，不进仓库）：
    ///     并发  6：138.76 s  4.47 MB/s   ← 改之前的值
    ///     并发 12： 90.52 s  6.85 MB/s
    ///     并发 24： 72.63 s  8.54 MB/s   ← 选它
    ///     并发 48： 67.05 s  9.25 MB/s
    /// 四档全部 0 失败、0 超时。6 那一档是**延迟** bound（每个文件一个 ~150 ms 的往返 ÷ 6 条），
    /// 24 那一档已经是**带宽** bound（620 MB ÷ 8.54 MB/s = 73 s ≈ 实测 72.63 s）—— 也就是说
    /// 24 就是这条网线的天花板，48 只再多 8%，不值得再翻一倍并发。
    /// 顺带把"是不是硬盘"这件事量死了：照本类的写盘顺序（mkpath → 写 → 关 → 重开全读算 SHA1）
    /// 跑 3000 个 20 KB 文件，两块 NVMe 各只要 0.86~1.15 s，3598 个对象折算 1 秒多。
    /// **慢的从来不是硬盘，是"每个文件一个网络往返"这件事本身。**
    ///
    /// 当初为什么是 6（2026-09-22 之前）：不限流时一次性把 3071 个对象全交给
    /// QNetworkAccessManager，2545 个报 `Operation timed out` —— 请求拿不到连接，30 秒的
    /// 不活动超时照样在走。那条**结论**今天仍然成立（排队必须发生在我们自己手里，见
    /// mayIssueAnotherDownload()），变的只是"我们手里"能放几条。
    /// NOTE: 那句「限流后每个对象约 2.5 秒、全程约 20 分钟」今天复现不出来了 —— 同样的 6 路
    ///       今天每对象 39 ms、全程 139 秒，差 8.7 倍；最可能是当时的网络/路由与今天不同。
    /// NOTE: 镜像源（BMCLAPI）在 24 路下反而会**触发失速看门狗**（它每条要过 302 到 MinIO 预
    ///       签名，实测每文件 ~500 ms，是官方的 3 倍），降级后自动改走官方 —— 那条兜底就是为
    ///       这种情形写的，不用跟着调。
    /// 放在 public 是给验证工程用的：V6i 的边界断言要按**同一个数**钉"到顶就不发"，硬写一个
    /// 字面量的话，下次调这个数用例会静默失效（2026-09-23 从 6 调到 24 时就是这么发现的）。
    static constexpr int kMaxConcurrentDownloads = 24;

    /// 还能不能再往外发一条（排队那一步问它）。
    ///
    /// **纯函数**，就为了能钉住那条上限：activeCount 到顶之后**不管还排着多少条**都不发。
    /// 这不是省资源，是**正确性**问题 —— 交给 QNetworkAccessManager 的请求要是当下拿不到
    /// 连接，就进了 Qt 内部那个队列，而 30 秒的传输超时**从请求建好就开始算**，于是它们会
    /// 在拿到连接之前成批地报超时（1.18.2 的 3071 个资源对象实测 2545 个这样失败）。
    /// 2026-09-22 一个 422 个文件的整合包导入又踩了同一个坑，而且更深一层：排在后面的回复
    /// 一个字节都收不到，失速看门狗两跳（10 秒）就把它们全判成"镜像太慢"（380 条 abort +
    /// 重发），重发又是 380 条一起发 —— 最后整批挂死不动、进度条停在 1%。
    /// verify 的 V6i 按边界钉过。
    static bool mayIssueAnotherDownload(int activeCount, int queuedCount);

    /// 一个**还没证明**自己走 HTTP/2 的主机，一次最多放几条在飞。
    ///
    /// 就是 Qt 对 HTTP/1.1 每主机（确切说是每 host:port）的连接上限。超过这个数的请求会被
    /// Qt 自己排进它的内部队列，而 30 秒的传输超时**从请求建好就开始算** —— 于是它们会在
    /// 拿到连接之前成批报 `Operation timed out`，正是 kMaxConcurrentDownloads 那段注释里
    /// 「2545 个对象超时」的形状。2026-09-23 本地用假服务器把这个形状复现了一遍：**h1.1、
    /// 每条连接 12 秒才回，24 路一放 → 40 条作业里 28 条超时，而且从第 3 轮开始成批死**
    /// （6 路那条对照是一次都不死）。
    ///
    /// 所以"排队必须发生在我们自己的 m_pendingJobs 里"这条不变量，在 kMaxConcurrentDownloads
    /// 从 6 调大之后要**按主机**守着：全局 24 条，但同一个主机在它证明自己走 h2 之前只放 6 条。
    ///
    /// ★★ 2026-09-27：**改道那条路也算数**。首选地址那个主机正被降级时 startDownload() 会
    ///    当场改道到备用地址，而这条判据问的必须是**备用**那个主机 —— 判断只有一处
    ///    （`effectiveDownloadUrl()`），"发给谁"和"问谁"不会漂开。原来问的是首选主机那个，
    ///    而回复是按改道后的主机记账的 ⇒ 这条闸对改道的作业永远放行，一次 pump 就能把
    ///    备用主机塞满 kMaxConcurrentDownloads(24) 条。
    static constexpr int kMaxConcurrentPerHost = 6;

    /// 还能不能再往**某一个主机**发一条。和上面那条一起判（两条都过才发）。
    /// @param activeForHost 这个主机当下有几条在飞。
    /// @param hostKnownHttp2 这个主机已经证明过走 HTTP/2（见 m_http2Hosts）。
    ///
    /// 已知 h2 的主机：能 —— 几十条流跑在**一条** TCP 连接上，放几条由全局上限说了算。
    /// 还不知道的主机：只能 kMaxConcurrentPerHost 条。这是保守估计，**代价只有一次往返**：
    /// 这个主机上第一条回复一回来，Http2WasUsedAttribute 就把答案给出来了 —— 真是 h2 就
    /// 立刻升到全局上限（官方 CDN 上实际等于安装开始时多花约 150 ms），真是 h1.1 就一直按 6 守。
    ///
    /// NOTE: 服务端要是把 SETTINGS_MAX_CONCURRENT_STREAMS 压得比 24 小，多出来的流**还是会**
    ///       排在 Qt 里（这一条判据看不出来）。官方 CDN 实测 ≥48（3598 个对象、0 超时），
    ///       别的主机没量过；真撞上的表现和上面一样（成批 `Operation timed out`），那时候
    ///       要么把 kMaxConcurrentDownloads 调小，要么把这条判据改成"第一轮回退"。
    static bool mayIssueAnotherDownloadToHost(int activeForHost, bool hostKnownHttp2);

    /// 一条回复"多久一点动静都没有"就判它废了（毫秒）。
    ///
    /// 为什么要有**按时间**这一条兜底：QNetworkReply **不保证**一定会发 finished。那次导入
    /// 里 380 条回复在 HTTP/2 连接被对端掐掉之后就那么挂着，二十分钟没动静、也没有终态，
    /// pendingDownloads 永远归不了零 → 整批永远不结束（界面表现是"下载中"停在 1%，没有错误、
    /// 没有弹窗，用户只能重启）。传输超时是"不活动"超时，本该在 30 秒时收拾它们 —— 这一条
    /// 是**它没收拾干净时的兜底**，所以取它的三倍。
    /// 只认"一点动静都没有"：downloadProgress 报过一次字节就重新计时，所以一条细水长流、
    /// 但一直在掉字节的下载不会被它掐掉。
    static constexpr qint64 kNoProgressGiveUpMs = 90 * 1000;

    /// 改上面那个期限。**只给验证工程用**：那一条只能靠时间验，而 90 秒对这种逐条的用例
    /// 太贵了（真机上它是固定值，界面上没有、也不该有这个开关）。
    void setNoProgressGiveUpMs(qint64 ms) { m_noProgressGiveUpMs = ms; }
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
    /// NOTE: 每一条下载都是**先排进队列再发**（见 mayIssueAnotherDownload）：作业少的时候
    ///       队列当场就把它发出去，和"直接发"没有任何区别；作业一多（几千个资源对象、
    ///       几百个整合包文件）就只有 kMaxConcurrentDownloads 条在飞，其余留在我们自己的
    ///       队列里等 —— 在 Qt 的队列里等会被它的传输超时算进去，而那正是"成批失败"的来源。
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
    ///
    /// ★ **但"不再有完成信号"只挡住了"这一批的最后一条"那一次**（2026-09-25 修）：
    ///   已经在飞的那几条回来时仍然是照常 --pendingDownloads / m_doneJobs++ /
    ///   emit downloadFinished 的。放弃之后如果**又开了一批**（用户点取消、再点一次下载
    ///   就是这么个次序），上一批那几条残留就会把新批的计数加花：m_doneJobs 可能超过
    ///   m_totalJobs（进度条上的数字比总数还大），也可能刚好把
    ///   checkAllDownloadsFinished() 的几条判据凑齐 → **提前宣布整批下完**，界面报"下载
    ///   完成"而文件还在路上。所以每一条回复都记着"我是第几批发出去的"，不属于当前这一批
    ///   的那几条只收连接、不记账、不发信号（见 m_batchGeneration）。
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
    // reply -> 期望的字节数（没登记过 = 不知道）。
    //
    // NOTE: 和 expectedSha1s 成对：**两处都要摘**（成功 / 失败 / 退回官方 / 看门狗判死）。
    //       只摘一张的症状是"一条刚从那条主机发出去的下载立刻被判成字节数不对" —— 键是
    //       回复指针，Qt 释放之后下一批可能落在同一个地址上。
    QMap<QNetworkReply *, qint64> expectedSizes;
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

    /// 一条**元数据作业**（版本清单 / 版本 json）的全部信息。
    ///
    /// 元数据作业**不落盘、也不计进 pendingDownloads**（见 fetchVersionManifest()）—— 但它们
    /// 要进 m_stallMeters，否则一条卡住的元数据请求没人判它失败，整条流程就挂在半路
    /// （2026-09-22 那次就是这么挂的）。这张表就是给 failWedgedDownload() 用的判据：看门狗
    /// 收一条回复时，得知道"该不该动那套计数"。以前没有这个判据，看门狗掐掉一条元数据回复
    /// 时会照样 `--pendingDownloads`（那条计数它从来没加过）→ 真在飞的文件作业就少算一个 →
    /// checkAllDownloadsFinished() 提前宣布整批下完，而文件还在路上。
    struct MetadataJob {
        /// 日志里人看得懂的名字（「version_manifest.json」/「version_json」）。
        /// 空 = 这条回复不是元数据作业（表里查不到时 .value() 给的就是这个）。
        QString label;
        /// 这一份该是哪个版本（版本 json 才有；清单那条为空 = 不核这一项）。
        QString expectedId;
        /// 这一份该是什么字节（取自官方 URL 里的 sha1；镜像那条自拼的地址没有 → 空 = 不核）。
        QString expectedSha1;
    };
    QMap<QNetworkReply *, MetadataJob> m_metadataReplies;

    /// 批次代次：**每 beginBatch() 加一**。发出去的每一条回复都把自己发出去那一刻的代次
    /// 记在 m_replyGenerations 里，收尾那几步（计数 / 失败清单 / 进度 / 完成判定）先问一遍
    /// "这一条属于当前这一批吗"。
    ///
    /// NOTE: 为什么需要它：abandonBatch() 只是把 m_batchEmitted 置真，已经在飞的那几条回复
    ///       回来时**照样**会 --pendingDownloads、m_doneJobs++、发 downloadFinished。之前这些
    ///       数字没人用（那一批已经报过了），所以看不出来；但现在"放弃之后又开一批"是常事
    ///       （取消、再点一次下载），上一批的残留就会算进新批 —— 计数错乱，甚至提前宣布整批
    ///       下完。代次是唯一能分清"这条回复是哪一批的"的东西：指针不行（Qt 会复用地址）、
    ///       路径不行（同一批里每个文件也各不相同）。
    quint64 m_batchGeneration = 0;
    /// 回复 -> 它发出去时的代次。**每一条**发出去的回复都要登记（文件作业在 startDownload，
    /// 资源索引在 downloadAssets），并在收尾时摘掉；漏登记的那条会按"当前这一批"处理
    /// （默认值就是当前代次）—— 也就是退回老行为，只影响那一批的计数，不会崩。
    QHash<QNetworkReply *, quint64> m_replyGenerations;

    /// 这一条回复属于**当前**这一批吗。批次之外的一次性作业（版本清单、加载器版本列表、
    /// 安装器下载……）查不到记录，按当前这一批处理 —— 它们本来就各算各的，从来不在批次里。
    bool replyIsFromCurrentBatch(QNetworkReply *reply) const;

    /// 等着发的作业。**每一条下载都从这里过**（资源对象、库、客户端 jar、整合包清单里
    /// 那几百个文件都一样）：一次资源补齐是 3000 多个对象，全塞给 QNetworkAccessManager
    /// 的话 Qt 每个主机只开 6 条连接，其余的在它内部干等，而 30 秒的传输超时**从请求
    /// 建好就开始算**，于是它们排到队尾之前就成批地报 `Operation timed out` —— 网络其实
    /// 是通的。发出去的条件就是那两个纯函数（mayIssueAnotherDownload / ...ToHost），
    /// 一问"全局还有位置吗"、一问"这个主机还有位置吗"。
    /// NOTE: 排在这里的作业已经计进 m_totalJobs（进度条的分母），但**不计进**
    /// pendingDownloads（那是"已经发出去的"），所以 checkAllDownloadsFinished()
    /// 与 isBusy() 都必须把这条队列也算上，否则会在还排着几百个的时候就宣布
    /// 整批下完 / 报告"没在忙"。
    struct PendingJob {
        QString url;
        QString destinationPath;
        QString expectedSha1;
        // 期望的字节数，-1 = 调用方也不知道。
        //
        // NOTE: 和 expectedSha1 一样**必须排队时就带上** —— 它是"这条回复回来时拿什么核
        //       这份正文"的一部分（见 onDownloadReplyFinished）。以前它在 scheduleFile()
        //       这一层就被丢掉了，于是写盘那一步只剩 sha1 一条判据；而拿不到 sha1 的那些
        //       库（maven 风格的那几个，见 LibraryRules.hpp 里那条说明）就变成"任何 200
        //       正文都算成功"。
        qint64 expectedSize = -1;
        // 排队时也得把它带上：作业在队列里等的这段时间，是没法回头问调用方要的。
        QString officialUrl;
        // 这个作业要去哪个主机。排队时算一次（QUrl 解析一次），因为发送那一步要拿它按主机
        // 计数，而那一步发生在**每次**有回复收尾时、要扫一遍队列 —— 几百个作业的时候
        // 一圈就是几百次解析。
        // NOTE: 它是**排队那一刻**那条地址的主机（镜像源下就是镜像主机），而真正发出去时
        //       可能已经被"镜像降级"改道到官方（见 startDownload）。所以极端情况下官方那个
        //       主机的额度会被少算一条（这条按镜像的额度放过去了）—— 这个方向是**故意**的：
        //       宁可偶尔多发一条，也不要因为一个对不上的计数把队列卡死。
        QString host;
    };
    QQueue<PendingJob> m_pendingJobs;

    /// 已经证明过走 HTTP/2 的主机。**只增不减**：一个主机是不是 h2 不会变。
    ///
    /// 由 QNetworkAccessManager::finished 一处统一填（见 noteReplyProtocol()），不在这四个
    /// finished 槽里各写一遍 —— 那四个槽里任何一个漏了，症状都只是"这个主机的并发上不去"，
    /// 不报警，很难发现。
    QSet<QString> m_http2Hosts;

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
    /// 一条在飞回复的记账。**每一条**发出去的回复都进这张表（不是只有走镜像那些）：
    ///   - bytesSeen / lastTickBytes / strikes 只有"走了镜像、而且登记过官方备用地址"的
    ///     回复才看 —— 那种才有退路，abort 掉它是"换一条路再试"；别的回复 abort 掉就是
    ///     纯粹把这次下载弄失败；
    ///   - lastSignOfLifeMs 对**所有**回复都看：发出去之后一直没动静的那条，谁都不该一直
    ///     攥着它（见 kNoProgressGiveUpMs，那一条救的正是"没有退路"的回复）。
    struct StallMeter {
        /// 到手的累计字节（高水位，只增不减）。取的是 `bytesAvailable()` —— 为什么不取
        /// downloadProgress 报的那个数，见 onStallWatchdogTick 的 (0)。
        qint64 bytesSeen = 0;
        qint64 lastTickBytes = 0;  ///< 上一跳时看到的那个值
        int strikes = 0;           ///< 连着几跳"没动"
        /// 最近一次**有任何动静**的时刻（发出去那一刻就先记一次），m_clock 的毫秒读数。
        qint64 lastSignOfLifeMs = 0;
    };
    QHash<QNetworkReply *, StallMeter> m_stallMeters;
    QTimer *m_stallWatchdog = nullptr;
    QElapsedTimer m_clock;
    /// 主机名 → 降级期的截止时刻（m_clock 的毫秒读数）。表里没有 = 没降级过。
    /// **按主机各记各的**，理由见 mirrorDemotedFor()：整合包那条路上"镜像"是每条文件
    /// 各自的首选地址，一个全局的降级位会让一个慢 CDN 把别的 CDN 一起关掉。
    QHash<QString, qint64> m_mirrorDemotedUntilByHost;
    /// 判"卡死"的期限，默认 kNoProgressGiveUpMs（只有验证工程会改它）。
    qint64 m_noProgressGiveUpMs = kNoProgressGiveUpMs;

    // kMaxConcurrentDownloads 的声明在上面 public 那一节（紧挨着 mayIssueAnotherDownload），
    // 因为验证工程要按那个数钉边界；理由和实测记录都写在那一处。


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
                       const QString &expectedSha1 = QString(), qint64 expectedSize = -1,
                       const QString &officialUrl = QString());
    /// downloadFile() 的本体：登记进批次、跳过已经下好的、剩下的排进 m_pendingJobs，
    /// 再由 pumpDownloadQueue() 按连接预算发出去。
    void scheduleFile(const QString &url, const QString &destinationPath,
                      const QString &expectedSha1, qint64 expectedSize, VerifyMode mode,
                      const QString &officialUrl = QString());
    /// 把队列里排着的作业发出去，直到连接用完（判据 = mayIssueAnotherDownload() 加
    /// mayIssueAnotherDownloadToHost()）。
    void pumpDownloadQueue();
    /// 这个主机当下有几条在飞。**从 activeDownloads 现算**，不另开一个计数器：
    /// 那张表有一堆摘除点（成功 / 失败 / 退回官方 / 看门狗判死），多一个计数器就多一处
    /// 能对不上的地方，而对不上的症状是队列**永远发不出去**（整批挂死）。
    /// 主机名从回复自己的 request() 取 —— 从队列那头是推不出来的，镜像那条 302 之后
    /// 落在哪个主机上只有回复知道。
    int activeCountForHost(const QString &host) const;
    /// 一条回复收尾时，顺手记下它用的是不是 HTTP/2（挂在 QNetworkAccessManager::finished 上，
    /// 每一条回复都走这里，一条连接的事只问一次）。
    void noteReplyProtocol(QNetworkReply *reply);
    /// 把一条"发出去之后一直没有动静、而且大概永远不会结束"的回复**判失败**，自己做完整套
    /// 记账。为什么不能只 abort() 了等它自己走 finished：这条路的整个前提就是"它可能永远
    /// 不结束"，指着 abort 收尾等于把整批挂死（见 kNoProgressGiveUpMs）。
    void failWedgedDownload(QNetworkReply *reply);
    /// 日志里给一条回复起个名字：文件作业是落地路径，元数据作业是「版本清单」/「版本 json」。
    /// 以前这几处日志一律打 activeDownloads 里的路径，元数据那条打出来是空的。
    QString jobLabel(QNetworkReply *reply) const;
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