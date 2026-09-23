/**
 * @file ResourceStore.hpp
 * @brief 下载页那四类资源的**取数**：搜一次、问一个资源的版本列表、取一个图标。
 *        地址怎么拼、JSON 怎么读全在 ResourceCatalog 那一层（纯函数），这里只管"发出去、
 *        收回来、认出来是给谁的"。
 * @author 0x7fffff
 * @date 2026-09-23
 *
 * 【为什么是异步的，不用 HttpClient::send】
 *
 * HttpClient::send 是**同步**的（内嵌事件循环）。它的调用方都是"点一下、本来就等着一个结果"
 * 的动作（登录、换皮肤），那没问题。但搜索不是：用户会在搜索框里连敲几次回车、会点着左边的
 * 分类来回换，每一次都卡住界面十几秒的话，这个页面就等于不能用 —— 这个项目在"点启动那条路
 * 三段同步等"上已经吃过一次这个亏了（memory 里那条）。
 * 所以这里照 VersionManager 那套来：QNetworkAccessManager + finished 回调，界面一次都不停。
 *
 * 【每一发都带一把"钥匙"】
 *
 * 一次请求要认得出回来的是不是**我现在等的那一发**：用户连敲两次回车，第一发回来得比第二发
 * 晚的话，晚到的那份结果会把新的那份盖掉（屏幕上显示的是上一个搜索词的结果，而且不报错）。
 * 所以每个频道各有一把钥匙（同一个 key 上新的一发把旧的掐掉），信号也把 key 原样带回去，
 * 界面按 key 判断"这是不是我现在那一页要的"。
 * 和 VersionManager 那边"看 reply 指针还是不是手上这个"是同一个道理，只是这里跨了函数边界，
 * 得有个名字。
 */
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

#include "download/ResourceCatalog.hpp"

class QNetworkAccessManager;
class QNetworkReply;

class ResourceStore : public QObject {
    Q_OBJECT

public:
    explicit ResourceStore(QObject *parent = nullptr);
    ~ResourceStore() override;

    /**
     * @brief 搜一次。
     * @param requestKey 这一发的钥匙（界面自己定，信号会原样带回来）。同一个 key 上还有
     *        一发没回来就先掐掉 —— 用户又敲了一次回车，他要的是后一次的结果。
     * @param kind 搜哪一类（决定 project_type 那个 facet）。
     * @param query 搜索词；**空串是合法的**（那就是"这类里最热门的"，进页面时就是这么问的）。
     * @param gameVersion 非空则只搜支持这个游戏版本的。
     * @param loader 非空则只搜这个加载器的（fabric/forge/...）。
     *
     * 结果走 searchFinished（**成功和失败都发**，"没搜到"和"没搜成"在 result 里分得开）。
     */
    void search(const QString &requestKey, ResourceCatalog::Kind kind, const QString &query,
                const QString &gameVersion = QString(), const QString &loader = QString());

    /**
     * @brief 问一个资源在某个游戏版本下有哪些版本可下。
     * @param requestKey 这一发的钥匙（用 ResourceCatalog::versionsRequestKey() 拼的那把）。
     * @param gameVersion 空 = 不筛（"这个资源的全部版本"，界面上还没选游戏版本时不该这么问）。
     *
     * 结果走 versionsFinished。**三条路要分得开**：失败（problem 非空）/ 问到了但一条都没有
     * （versions 空、problem 空）/ 正常 —— 前两条在界面上是两句不同的话
     *（加载器树那边「无可用版本」和「列表没取到」踩过同一个坑）。
     */
    void fetchVersions(const QString &requestKey, const QString &projectId,
                       const QString &gameVersion, const QString &loader = QString());

    /**
     * @brief 要一个图标。
     *
     * 内存里已经有了就**立刻**发 iconReady（不等下一轮事件循环），所以要接这个信号之后再
     * 灌那批条目，不然已经缓存过的那些图标会错过这一发。
     * 取不到（404 / 那条资源本来就没传图标）时发一个**空 QPixmap** —— 那是"没有"，不是"失败"，
     * 界面上画占位方块，也不该弹窗。
     *
     * 【两级缓存：内存 + 盘上那一份】
     *
     * 盘上有一份**新鲜**的（见 kIconCacheFreshMs）就一个请求都不发、同步把图给出去 ——
     * "第二次进这一页是秒开"就是这一条。有一份**过期**的也先给出去（用户马上能看到图），
     * 然后再**后台**问一遍；问到了就再发一次 iconReady，界面把那一格换成新的。
     *
     * NOTE: 缓存是**按时间**作废的，不是 PlayerSkinStore 那种"内容寻址、永不失效"：那边
     *       的 URL 里本来就带内容哈希（同一个地址永远同一张图），图标这条不是 —— 能用的
     *       那个 PNG 地址是 `data/<project_id>/icon.png`，固定名字、内容会变（作者换了图标）。
     *       代价是"作者换了图标，用户最迟一周后看到新的"，图标是装饰，这个代价是接受的。
     *
     * 【为什么收的是整条 Hit 而不是一个 id：要挑地址】
     *
     * 同一个图标有两条地址（见 ResourceCatalog 文件头）：接口给的 `Hit::iconUrl` 大多是
     * 一条**小得多**的 96px 缩略图 webp（样张 4800 B，而同一张的 PNG 是 35 KB；按张会变），
     * 自己拼的 `Hit::iconPngUrl()` 是 PNG。**怎么选**：
     *   * 这台机器解得了 webp（ImageDecode::webpAvailable）**而且**接口给的这条确实是
     *     webp → 先要那条小的；
     *   * 否则 → 直接要 PNG（一次请求，别为一条解不动的地址白发一发）。
     * 先要 webp 的那条路**带一个兜底**：那一条没解出来（或者 404）就再要一次 PNG，
     * 而这一发**不计失败**（详见 startIconRequest）。
     *
     * 盘上那份缓存的扩展名是 `.icon` 不是 `.png` —— 里面两种字节都可能躺着（见
     * iconCachePath），叫什么名字只说明「这是个图标」，格式由 ImageDecode 按内容判。
     */
    void requestIcon(const ResourceCatalog::Hit &hit);

    // ---------- 图标缓存（纯函数，离线能钉住）----------

    /// 盘上那张图标活多久（毫秒）。过了这个岁数 = 该刷新了（见 requestIcon 的 NOTE）。
    static constexpr qint64 kIconCacheFreshMs = 7LL * 24 * 60 * 60 * 1000;

    /// 这份缓存还算不算新鲜？
    /// NOTE: `writtenAt` 无效 = 文件系统没给出时间（罕见）—— 当成**不新鲜**：宁可多问一次，
    ///       也不要让一张"不知道什么时候存的"图永远画在那儿。
    static bool iconCacheIsFresh(const QDateTime &writtenAt, const QDateTime &now);

    /// 某个 project_id 的图标缓存路径：`<数据目录>/cache/icons/<project_id>.icon`。
    ///
    /// project_id 不是文件名安全的那一类（含 `/`、`\`、`:`、`..` 之类）时回**空串** ——
    /// 宁可不在盘上缓存，也不要拼出一个能写到别的目录去的路径。这个 id 是从接口正文里来的，
    /// 按"外部输入"对待。
    ///
    /// NOTE: 后缀是 `.icon` 不是 `.png`。这一格两种字节都会落进来（先要 webp 那条成功的
    ///       话存的就是 webp），叫 `.png` 就是一句假话 —— 而这个假话会害到下一个「看见
    ///       .png 就当 PNG 去读」的人。**认格式一律按内容**（ImageDecode::decode）。
    static QString iconCachePath(const QString &projectId);

    /// 读盘上那份图标缓存。
    /// @param bytesOut 命中时写入文件内容（没命中不动它）。
    /// @param freshOut 命中时写入"还算不算新鲜"（没命中不动它）。
    /// @return 文件在不在。**"在不在"和"新不新鲜"是两件事**：在但不新鲜 = 先拿它顶上、
    ///         同时后台再问一遍（见 requestIcon）。
    static bool loadIconFromCache(const QString &projectId, QByteArray *bytesOut, bool *freshOut);

    /// 把一份刚下回来的（能解码成图的）图标字节写到盘上。
    /// best-effort：写不进去就算了，**不是错误**（这次照样把图发给界面）。
    static bool saveIconToCache(const QString &projectId, const QByteArray &bytes);

    /// 把盘上那份图标缓存删掉（读到坏字节时用，见 requestIcon）。
    static void dropIconFromCache(const QString &projectId);

    // ---------- "别一张张干等"那道闸 ----------

    /// 连着这么多张图标都倒在**主机级**的失败上就不再一张张问了（见 m_iconHostFailures）。
    static constexpr int kIconGiveUpAfter = 3;

    /// 这一发失败算不算"主机级"（= 这一路整条不通，而不是"这张图有问题"）？
    ///
    /// 404 是"这个资源没有图标"、429 是"你问得太勤了" —— 两条都说明**主机是活的**，
    /// 不算。别的（0 = 没连上/超时、5xx）= 整条路不通。
    static bool isHostLevelIconFailure(int httpStatus);

    /// 掐掉所有还在飞的请求（窗口关了、或者要重来一遍时用）。
    void abortAll();

    /// 一页取多少条。**没有"加载更多"**：这一轮就是搜一页出来给用户挑，翻页还没做
    /// （要做得连 offset 和"结果到底有没有下一页"一起做，不是只把数字改大）。
    ///
    /// NOTE: 放在 signals 段**外面**（这里、而不是下面）—— 那一整段是给 moc 看的，
    ///       里面放一个数据成员 moc 会直接报 `Not a signal or slot declaration`。
    static constexpr int kSearchPageSize = 20;

signals:
    /// 搜索回来了（成功和失败都发）。
    void searchFinished(const QString &requestKey, const ResourceCatalog::SearchResult &result);

    /// 版本列表回来了。problem 非空 = 没取到；problem 空 + versions 空 = 问到了、就是没有。
    void versionsFinished(const QString &requestKey,
                          const QList<ResourceCatalog::VersionEntry> &versions,
                          const QString &problem);

    /// 图标到了（空 QPixmap = 这条资源没有图标，画占位）。
    void iconReady(const QString &projectId, const QPixmap &icon);

private:
    /// 失败话术（**不是** 429：那条有自己唯一的一句话，见 HttpClient::throttledText）。
    /// 正文里带着接口自己那句 `description` 时把它带上 —— 实测接口报错就是这个形状，
    /// 它比我们编的"搜索失败"有用得多。
    static QString failureText(QNetworkReply *reply, const QByteArray &body, const QString &what);

    /// 发一发图标请求（`requestIcon` 挑完地址之后的那一步，**自己会往 m_iconPending 里记**）。
    ///
    /// @param url         这一发要的地址。
    /// @param fallbackUrl 这一发没成时再要一次的那个地址；空 = 没有下一发。
    ///
    /// 【两条地址的那条路，规则都在这】
    ///
    /// 只有"先要 webp"那一条路会带 fallbackUrl（见 requestIcon）。什么时候真退回去：
    /// **这一发答话了、但那不是一张能解的图** —— 404（地址不在）、403、或者 200 配一段
    /// 解不出来的正文（本机没有 webp 编解码器时理论上不会走到这，但"理论上"不该当判据）。
    /// 这一退**不计失败**：它不是"这一路不通"，是"这条地址这条格式不行，换一条"。
    ///
    /// NOTE: **状态 0 和 5xx 不退**（`isHostLevelIconFailure`）。那两条说明整条路都不通，
    ///       换到一个同主机、同一条链子上的地址只会再耗一个 8 秒超时，还白推一格那道闸
    ///      （症状是"明明只是网断了，三次之后这一页再也不问了"）。
    void startIconRequest(const QString &projectId, const QString &url,
                          const QString &fallbackUrl);

    /// 这一发没取到新图标，该怎么回这一发：手上还有一张（内存里的）就把**它**重发一遍，
    /// 确实没有才发空 QPixmap。
    ///
    /// NOTE: 这是 iconReady 那条"每一发请求都会回一次"的契约里唯一的弯，为什么必须弯：
    ///       过期刷新那一条路上，盘上那张旧的**已经画在界面上了** —— 这时候再发一个空
    ///       QPixmap 会把那一格擦成透明方块（症状是"作者换了图标、这次没问到，图标反而
    ///       没了"）。重发同一张 = 契约不破，那一格也不动。
    void answerIcon(const QString &projectId);

    /// 记一次"主机级"的图标失败，够数了就**放弃这一批**（见 m_iconGaveUp）。
    void noteIconFailure(int httpStatus);

    QNetworkAccessManager *m_net = nullptr;
    /// 每个频道手上那一发（key -> reply）。回来的东西不是这一发就丢掉。
    QHash<QString, QNetworkReply *> m_replies;
    /// 图标：projectId -> 图。前面还有一层盘上的（见上面的静态函数），这里是这一趟运行
    /// 的副本，命中它连盘都不用读。
    QHash<QString, QPixmap> m_icons;
    /// 正在问的图标（同一个别问两遍）。
    QSet<QString> m_iconPending;
    /// 明确没有图标的（HTTP 404）。注意这和"这次没取到"**不是一回事**：网络失败不进这里，
    /// 下次还会再试一次。
    QSet<QString> m_iconMissing;

    /// 连着几次图标请求倒在"主机级"的失败上（成功一次就清零）。够 kIconGiveUpAfter 次就
    /// 把 m_iconGaveUp 立起来。
    ///
    /// 这件事是 2026-09-23 实测出来的：Modrinth 的 CDN 把每一张图 307 到
    /// `cdn-alt.modrinth.com`，而那个域名从国内**连不上** —— 于是二十来个图标各自把超时
    /// 耗完（六条连接轮着来），界面上的格子一个都不出。第 3 张还失败的时候就该认账：
    /// 用户要的是那一页能看，不是让启动器一张张去撞墙。盘上那些已经存下来的**照给**
    ///（它们一个请求都不用发，见 requestIcon）。
    int m_iconHostFailures = 0;
    /// 已经放下了。**不是永久状态**：下一次搜索会清掉它（用户又动了 = 网络可能刚变好，
    /// 比如他刚把代理打开）—— 这道闸只管"这一次、这一屏别干等"，不该管一整个会话。
    bool m_iconGaveUp = false;
};
