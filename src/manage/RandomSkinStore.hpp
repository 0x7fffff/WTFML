/**
 * @file RandomSkinStore.hpp
 * @brief 给**离线账号**挑一张随机皮肤（挑地址 + 记盘），图本身还是走 PlayerSkinStore 去下。
 * @author 0x7fffff
 * @date 2026-09-24
 * @note 只用 QtCore + QtNetwork，一个 QtGui 类都不碰 —— 同 PlayerSkinStore（它发出去的
 *       也是"一个地址"，不是 QImage）。挑哪一张、地址合不合法、盘上那份还算不算数，
 *       这几件事**没有一行能靠肉眼看截图验准**，所以全做成纯函数留在主 verify 工程里钉住。
 *
 * 【为什么不从 NameMC 挑（2026-09-24 实测）】
 *
 * 用户问的原话是「默认皮肤能自动在 namemc 去随机下一个吗」。NameMC **走不通**，而且是
 * 三重走不通，三条都实测过：
 *
 *   1. **它每一页都在 Cloudflare 的 JS 挑战后面**。裸 curl 拿到 403，换上真浏览器的 UA
 *      还是 403，正文是 `Just a moment...`。`/`、`/skins`、`/api`、`/api/v2/skins`、
 *      `/profile/<uuid>` 全是 403 —— 只有 `/favicon.ico`、`/robots.txt` 这种静态文件放行。
 *      要过挑战得有 JS 引擎解 Turnstile，而这里只有 QNetworkAccessManager。
 *   2. **它没有公开 API**。`api.namemc.com` 这个 host 存在（根路径 404 而不是连接失败），
 *      但公开文档里没有任何"随机皮肤"端点，站上那个随机也是页面自己的功能。
 *   3. 顺带一提，它的 robots.txt 只对 GPTBot 全禁、对其他人只禁 `/minecraft-names?` 和
 *      `/i/emoji/` —— 所以**挡住的不是规矩，是那道挑战**。这话要说准，别把技术障碍
 *      说成人家不让你抓。
 *
 * 【那用什么：MineSkin 的公开皮肤库】
 *
 * `https://api.mineskin.org/v2/skins?size=N`（免 key，N 上限 128，超过 400）返回**最近被
 * 生成的一批皮肤**，每项长这样：
 *
 *     { "uuid": …, "shortId": …, "name": null, "texture": "<64 位十六进制>",
 *       "timestamp": 1790216929364 }
 *
 * 关键在于那个 `texture` 就是官方 CDN 上那张图的**内容哈希** —— 图本身躺在
 * `https://textures.minecraft.net/texture/<hash>`，也就是 `PlayerSkinStore` 已经在认的那个
 * host。于是这个类**只负责挑地址**：下不下得来、存哪、怎么画，全是现成那条路
 * （PlayerSkinStore::fetchTexture / cachePathForUrl / MainWindow::requestSkinTexture）。
 *
 * NOTE: 这个库是**别人生成上传的，没有审核**。看到一张奇怪的图是可能的（都是 64×64 的
 *       像素画），要换成别的源就换 listRequestUrl() 和 parseTextureHashes() 这两处。
 *
 * 【只给离线账号挑】
 *
 * 正版账号的皮肤是官方的，随便塞一张进去就成了**骗人** —— 界面会指着一张不是他的皮肤说
 * 这是他的外观。离线账号不一样：它本来就没有官方皮肤（见下一条），画什么都只是好看。
 *
 * 【挑一次就记盘，不是每次启动重挑】
 *
 * 盘上 `<skinsDir>/random-<uuidKey>.txt` 里那一行就是这个人挑中的地址。理由是底栏那颗
 * 头像：每次开启动器都换一张脸，用户会以为账号串了。想换一张就删掉那个文件（或者在界面
 * 上再点一次「刷新外观」，见 MainWindow::refreshAccountSkin()）。
 *
 * 【取不到就退回默认外观，不弹窗】
 *
 * 和 PlayerSkinStore 同一个理由（那边文件头写全了）：这是**装饰**，用户还是能启动游戏，
 * 而"每次开窗弹一个『皮肤没拿到』"对一个可选功能纯属打扰。日志留一行备查。
 *
 * 【离线账号在游戏里看不到这张皮肤】
 *
 * 这一点别弄混：这里挑的图只影响**启动器里**的三处预览（主页右栏那个小人、底栏那颗
 * 头像、那个窗里的 3D）。进游戏之后客户端是去官方会话服务要皮肤，离线账号要不到，
 * 游戏里那张脸是它自己定的。
 */

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;

class RandomSkinStore : public QObject {
    Q_OBJECT

public:
    explicit RandomSkinStore(QObject *parent = nullptr);

    // ---------- 纯函数：地址、解析、挑选 ----------

    /// 要那一批皮肤用的地址。**唯一出处**（想换源就改这里和 parseTextureHashes()）。
    /// @param size 一次要几条；> maxListSize() 时按上限算（官方对超限回的是 400，
    ///       不是一个截短了的列表 —— 与其拿一个 400 去猜，不如自己先钳住）。
    static QUrl listRequestUrl(int size);

    /// 一次最多能要几条。**128 是实测出来的**：100、128 回 200，200、256、500 回 400。
    /// 一次要满：列表是按时间倒序的，一页 128 条大约覆盖最近十分钟生成的东西。
    static int maxListSize() { return 128; }

    /// 默认要几条。取满 —— 这个列表**不是**翻页取的（只有 next 游标、没有"跳到第 N 页"），
    /// 所以"随机"的范围就是这一页，当然越大越好。
    static int defaultListSize() { return maxListSize(); }

    /// 解一批皮肤哈希出来（**只留 64 位十六进制那些**，保持原顺序）。
    ///
    /// 会**去重**：同一张图在刚生成的一小段时间里会连着出现好几次，留着它等于给那张图
    /// 加权，而"随机"的意思就是每一张一样大。
    ///
    /// @param errorOut 解不出来时那句给日志看的原因（**不弹窗**，见文件头）。
    /// @return 解出来的哈希；空列表 = 没解出来（此时 *errorOut 非空）。
    static QStringList parseTextureHashes(const QByteArray &body, QString *errorOut = nullptr);

    /// 从 @p hashes 里挑第 @p roll 张（@p roll 必须落在 [0, size)，越界返回空串）。
    ///
    /// NOTE: 单独一个函数是为了**能验**：真跑那条路用的是 QRandomGenerator，而"随机"这件事
    ///       本身没法断言（断言"两次不一样"是在赌运气）。把掷骰子和取值拆开之后，
    ///       取值那一半每一条边界都能钉死。
    static QString pickIndex(const QStringList &hashes, int roll);

    /// 这个人挑中的那张在盘上记在哪：`<skinsDir>/random-<uuidKey>.txt`。
    /// uuid 认不出来时返回空串（**不是**拼一个野路径）。
    static QString chosenPathFor(const QString &uuid);

    /// 读盘上记着的那张。**每一行都要过一遍 textureRequestUrl()** ——那是"这个地址算不算
    /// 数"的唯一判据（认官方那个 host、升级成 https）。文件不在 / 读不懂 / 地址不合法，
    /// 一律返回空串（当"还没挑过"处理，调用方会去要一张新的）。
    static QString loadChosenSkinUrl(const QString &uuid);

    /// 把挑中的那张记下来（下次开启动器还是它）。
    /// @param errorOut 可选，写失败的原因（给日志看）。
    static bool saveChosenSkinUrl(const QString &uuid, const QString &skinUrl,
                                  QString *errorOut = nullptr);

    // ---------- 联网 ----------

    /**
     * @brief 异步要那一页皮肤，随便挑一张，回来发 skinReady()。
     *
     * 挑中的是**官方 CDN 上的地址**（不是 MineSkin 的），调用方直接把它当一份档案的
     * skinUrl 用即可。失败发 failed()（静默降级，见文件头）。
     */
    void fetchRandomSkin();

signals:
    /// 挑好了。@param skinUrl `https://textures.minecraft.net/texture/<hash>`，可以直接喂给
    ///        PlayerSkinStore::fetchTexture()。
    void skinReady(const QString &skinUrl);
    /// 出错了。**调用方只记日志**（见文件头）。
    void failed(const QString &message);

private:
    /// 自己一个管理器，理由同 PlayerSkinStore：不和 DownloadManager 共用（那个的超时是给
    /// 几十兆的 jar 用的），这是可选的外观请求。
    QNetworkAccessManager *m_network;

    /// 已经有一次在飞了。**必须有这个闸**：fetchRandomSkin() 会被反复调到（每次刷新外观、
    /// 每次切账号都会重新 startPlayerSkinLoad()），不挡一下就会攒出一串一模一样的请求。
    bool m_inFlight = false;
};
