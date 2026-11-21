/**
 * @file SkinApi.hpp
 * @brief 换皮肤、换披风：`api.minecraftservices.com` 上那几条要**令牌**的接口。
 * @note 和 PlayerSkinStore 的分工写清楚，这两个类名看起来很像但是两回事：
 * - `PlayerSkinStore`：**免鉴权**地"看" —— 知道 uuid 就能问，用来画 3D 预览。
 * - `SkinApi`（这里）：**要令牌**地"改" —— 换皮肤、换披风、取消披风。
 * 所以这里每个方法都要一个 Minecraft access_token（调用方从
 * `AuthManager::loadSavedToken()` 拿），而 PlayerSkinStore 一个令牌都不要。
 *
 * 【这几条接口的形状】
 *
 * GET /minecraft/profile 当前档案（含 skins[] / capes[]，各自带 state）
 * PUT /minecraft/profile/capes/active 设成某一件（body: {"capeId": "..."}）
 * DELETE /minecraft/profile/capes/active 不显示披风
 * POST /minecraft/profile/skins multipart：variant=classic|slim + file=PNG
 *
 * 【别按对称性猜路由：披风列表没有自己那条路】
 *
 * 上面第一行那份**档案**里就带着 `capes[]`，它就是这个账号拥有的披风列表。
 * `GET /minecraft/profile/capes` **不存在** —— 不能按"设披风/取消披风
 * 都在 `/capes/active` 下，那列一遍大概也在 `/capes` 下"去推，打过去回
 * **404**。这条 404 的坏处是它和"这个账号没买游戏"用**同一个状态码**：
 * 界面上写着"没有可改的 Minecraft 档案"，看着像账号的问题，实际是路径的问题。
 * （对过 Mojang 的 OpenAPI：`/minecraft/profile/capes` 底下只有 `/capes/active` 那两个
 * 方法；列表只有档案里那一份。）
 *
 * 结论：**披风列表和皮肤/档案走的是同一个地址**（profilePath()）。想加一条新请求之前，
 * 先确认那条路由真的存在 —— 这个接口集里"看起来该有"的路由有好几条是不存在的。
 *
 * 【为什么解析和话术都是静态纯函数】
 *
 * 【variant 有两种写法，别混】
 *
 * 读回来的是大写（`"CLASSIC"` / `"SLIM"`，或者 skins[] 里根本没有 variant 这个字段），
 * 传上去的要小写（`"classic"` / `"slim"`）。转换一律走 normalizeVariant()。
 */

#pragma once

#include <QByteArray>
#include <QDeadlineTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include "../net/HttpClient.hpp"

class QNetworkAccessManager;
namespace RequestBudget { class ControlGet; }

/// 这个人拥有的一件披风。
struct OwnedCape {
    QString id;
    /// 显示名（`Migrator` / `MineCon 2016` 这种）。多数件都带，没带就退回 id。
    QString alias;
    /// 披风 PNG 的地址（64×32，和 PlayerSkinStore 那套缓存吃的是同一种图）。
    QString url;
    /// 现在是不是它显示着。
    bool active = false;
};

/// 当前皮肤的一小段摘要（画预览用不上，只是"换成功了没有"的判据）。
struct SkinSummary {
    QString url;
    /// 已经是小写形态（见文件头）。
    QString variant;
    bool isEmpty() const { return url.isEmpty(); }
};

class SkinApi : public QObject {
    Q_OBJECT

public:
    explicit SkinApi(QObject *parent = nullptr);

    // ---------- 纯函数：解析 ----------

    /// 解 `GET /minecraft/profile`：取 skins[] 里 state == ACTIVE 的那一件（没有就取第一件）。
    /// 解不出来返回空（skinUrl 为空 = 这份档案没有皮肤信息）。
    static SkinSummary parseCurrentSkin(const QByteArray &body);

    /// 解披风列表。**两种形状都认**：外层是对象、披风挂在 `capes` 键上（`GET
    /// /minecraft/profile`，产品走的就是它），以及**裸数组**（旧的 `api.mojang.com`
    /// 那条查披风的路、以及别的启动器从 OpenAPI 那边看到的就是这个形状）。
    ///
    /// NOTE: 产品**不再**打一条独立的"列披风"路由（那条不存在，见文件头）。认裸数组是
    /// 为了这种形状真的存在于别处 —— 只认一种的话另一种解出来是**空列表**，而且
    /// 不报错、不进日志，界面上"一件披风都没有"和"这个账号确实没披风"长得一模一样。
    ///
    /// NOTE: 只认**有 id 的**项 —— 没有 id 就没法设成当前披风（PUT 要的就是它），
    /// 摆一件点了没反应的披风不如不摆。
    static QList<OwnedCape> parseCapes(const QByteArray &body);

    /// `"SLIM"` / `"slim"` / 别的 → `"slim"` / `"classic"`。**只有 slim 认 slim**，
    /// 其余（含空串、含认不出来的值）一律 classic：档案里没有 variant 字段就是经典模型，
    /// 这是协议本身的默认值。
    static QString normalizeVariant(const QString &raw);

    /// 传上去的皮肤尺寸收不收（64×64 = 新格式，64×32 = 老格式）。
    ///
    /// NOTE: 官方那边**也**会校验，但它回的是 400 + 一句英文；在这里先挡一道是为了能说
    /// 中文、并且省掉一次白传（皮肤文件虽然小，但用户等的是那个圈）。判据只看宽高，
    /// 和 PlayerSkinStore::looksLikeSkinPng() 是同一个口径（那边看文件头，这边看图）。
    static bool isAcceptableSkinSize(int width, int height);

    /// multipart 请求体（见文件头那条接口的形状）。
    ///
    /// NOTE: 手写而不是用 QHttpMultiPart —— 那玩意是**异步**接口要配 QNetworkReply 用，
    /// 而这里整条路都是同步的（见 HttpClient）。手写一个两段式的 body 一共二十行，
    /// 换来的是"和别的请求走同一个 send()"这一条。
    static QByteArray skinUploadBody(const QByteArray &png, const QString &variant,
                                    const QByteArray &boundary);

    /// 上传皮肤那一次请求的 Content-Type（带着 boundary）。
    static QByteArray skinUploadContentType(const QByteArray &boundary);

    /// 失败话术（纯函数，验证工程钉得住）。
    /// @param reply 整个请求结果 —— 要的就是两个面都在里面（见下）。
    /// @param what 这一步在做什么（"上传皮肤失败"这种），拼在这句话最前头。
    ///
    /// NOTE: 必须收**整个 Reply**："没连上"和
    /// "服务器答了个错误"是两回事，而前者没有状态码 —— 只收 status 的话，断网时
    /// status 是初值 -1，报出来会是「读取披风失败（HTTP -1）。」，用户既不知道
    /// 该去查网络，还看见一个不存在的状态码。网络那一句在 AuthManager 的
    /// httpFailureText() 里也是第一条分支，这里对齐。
    ///
    /// NOTE: 401 和 403 分开说 —— 它们对用户是两个完全不同的动作：
    /// 401 = 令牌不认了（重新登录），403 = 这个应用没被批准（等审批，见
    /// AuthManager::appRegistrationHint()，这里的口径和它一致）。
    /// 400 之类把正文里那句 errorMessage 带出来（官方会说"必须 64x64 或 64x32"）。
    /// 429 也单拎出来：那是**限流**（用户要做的是等一会儿，不是重登、不是换网络），
    /// 文案的唯一出处是 HttpClient::throttledText()。
    static QString failureText(const HttpClient::Reply &reply, const QString &what);

    // ---------- 联网（同步，见 HttpClient） ----------

    /// 当前档案里的皮肤。@param errorOut 可选，失败原因。
    bool fetchCurrentSkin(const QString &token, SkinSummary *out, QString *errorOut = nullptr);

    /// 按账号异步查询档案里的披风。已经在查询时合并请求。
    void requestOwnedCapes(const QString &uuid, const QString &token);
    const QList<OwnedCape> *cachedCapes(const QString &uuid, bool freshOnly = false) const;
    void discardCapes(const QString &uuid);
    /// 成功设置或隐藏披风后更新缓存里的启用项，并取消旧查询。
    void updateCachedCape(const QString &uuid, const QString &capeId);

    /// 把某一件披风设成当前。@return 成功返回 true。
    bool setActiveCape(const QString &token, const QString &capeId, QString *errorOut = nullptr);

    /// 不显示披风（DELETE）。这个动作**没有反向操作**（想显示回来得再 PUT 一次具体某件）。
    bool hideCape(const QString &token, QString *errorOut = nullptr);

    /// 换皮肤。@param png 皮肤 PNG 的字节（尺寸由界面先过 isAcceptableSkinSize）。
    bool uploadSkin(const QString &token, const QByteArray &png, const QString &variant,
                    QString *errorOut = nullptr);

    /// 这几条接口的根地址。做成函数是为了验证工程能拿它跟别处的常量对齐。
    static QString baseUrl();

    /// 把根地址指到本机假服务器（**只有验证工程会调**）。空串 = 官方地址。
    /// 测试在构造任何窗口之前设置一次，之后只读。
    static void setBaseUrlOverride(const QString &base);

    /// **档案**那条路径。皮肤的当前状态和**披风列表**都在这一份响应里（见文件头）。
    ///
    /// NOTE: 做成公开的静态函数是为了让"披风列表从哪来"这件事能被验证工程钉住 ——
    /// 这个 404 之所以能活到真机上，正是因为路径是 .cpp 里一个私有常量，离线测试
    /// 一行都碰不到它（验证工程只能验**纯函数**，验不了"打的是哪个地址"）。
    static QString profilePath() { return QStringLiteral("/minecraft/profile"); }

signals:
    void ownedCapesFinished(const QString &uuid, const QString &error);

private:
    struct CapeCacheEntry {
        QList<OwnedCape> capes;
        QDeadlineTimer freshness;
    };
    QHash<QString, CapeCacheEntry> m_capeCache;
    QHash<QString, RequestBudget::ControlGet *> m_capeRequests;
    /// 这个类自己的管理器，不和 AuthManager/PlayerSkinStore 共用（各管各的超时与生命周期）。
    QNetworkAccessManager *m_network;
};
