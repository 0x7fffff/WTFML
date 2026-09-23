/**
 * @file ResourceCatalog.hpp
 * @brief 下载页「模组 / 整合包 / 资源包 / 光影」四类资源的**纯函数**部分：
 *        「哪一类 + 搜什么」→ 请求地址 → 解析出来的结果。不联网、不碰磁盘。
 * @author 0x7fffff
 * @date 2026-09-23
 * @note 真正发请求、落图标缓存的是 ResourceStore。这一层拆出来是为了两件事：
 *       ① 解析能拿真响应当夹具离线测（build/verify 里喂 json，一个字节的网络都不走）；
 *       ② 地址是怎么拼出来的能单独钉住 —— 拼错了和解析错了是完全两回事，混在一起
 *          报出来的错会指向错的地方。
 *
 * 【数据源是 Modrinth，不带 key】
 *
 * 实测（2026-09-23）：`GET https://api.modrinth.com/v2/search` 不要任何鉴权，
 * 响应头带 `X-Ratelimit-Limit: 300`（每分钟 300 次）—— 一次搜索一条请求的量级够用。
 * CurseForge 那份的清单里没有下载地址、要带 key 去问接口（见 ModpackImport 里那段），
 * 所以**这一轮只做 Modrinth**，将来接 CurseForge 时这一层照着加一个来源即可。
 *
 * 【图标那件事：两条地址，一条小一条大】
 *
 * 同一个图标有两条地址（实测 2026-09-23）：
 *   * 接口给的 `icon_url` —— 大多是 `…/<hash>_96.webp`（96px 缩略图，**4800 字节**），
 *     也有项目直接给 `…/icon.png`（`Bh37bMuy` 实测就是），所以**它是什么就存什么**；
 *   * 自己按 id 拼的 `…/data/<id>/icon.png` —— 原图，**35218 字节**，这份 Qt 直接能解。
 * 小那条值 7 倍（实测同一时刻交替各取 5 次：png 中位 3.09 s、webp 中位 1.99 s），
 * 但它是 **webp**，而这份 Qt 没有 webp 解码器（supportedImageFormats 实测无 webp，
 * `plugins/imageformats/` 里也没有 `qwebp.dll`）—— 得走系统的 WIC，而**不是每台机器都
 * 有那个编解码器**（见 ImageDecode::webpAvailable）。
 *
 * 所以这一层只做两件纯事：**把接口给的地址存下来**（Hit::iconUrl，顺手验一下它是不是
 * 我们认的那个域名）和**认出它是不是一条 webp**（isWebpUrl）。"这一台要走哪条、
 * 解不动怎么办"是 ResourceStore 的事。
 *
 * 相册/截图那部分只有 webp（连 raw_url 都是），这一轮仍然不碰 —— 用户的原话是
 * 「我只要那个程序图标，不要详情里的图片」。
 */
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace ResourceCatalog {

/// 下载页那四类能"先搜出来再下"的资源。
///
/// **顺序不重要，键才重要**：kindKey() 出来的字符串要和下载页左栏那份分类表
/// （MainWindowUI::downloadCategoryKeys()）里对应的那一条**逐字相同** —— 界面上"点的是
/// 哪一类"和"去问哪一类"靠这个字符串对起来。验证工程里有一条断言把这个等号钉住。
enum class Kind {
    Mod,          ///< 模组
    Modpack,      ///< 整合包（下回来是 .mrpack，接着走既有的导入流程）
    Shader,       ///< 光影
    ResourcePack, ///< 资源包
};

/// 四类都在这里列一遍（界面按它建页面、验证按它循环）。
QList<Kind> allKinds();

/// API 里那一类的名字，也正是下载页分类的键：mod / modpack / shader / resourcepack。
QString kindKey(Kind kind);

/// 从下载页那份分类键认回 Kind；不是这四类（比如 "game"）时返回 false 并且不动 *out。
bool kindFromCategoryKey(const QString &key, Kind *out);

/// 这一类下回的文件装进实例的哪个子目录。
/// 整合包**没有**子目录（它走导入那条路，清单说往哪放就往哪放），所以回空串。
QString folderForKind(Kind kind);

/// 人看的名字（「模组」「整合包」…）。界面上那几个字从这里取，免得两处各写一份。
QString kindLabel(Kind kind);

// ---------- 搜索 ----------

/// 一条搜索结果。这里每个字段都是搜索接口**本来就给了**的 —— 没有为了它多发一次请求。
struct Hit {
    QString projectId; ///< 唯一 id（图标地址、版本查询都靠它）
    QString slug;      ///< 网址里的那一段（`/mod/sodium` 的 sodium），出错时给用户看更认得出
    QString title;
    QString description; ///< 一行简介（界面上按一行省略号画）
    QString author;
    qint64 downloads = 0;
    QStringList categories;   ///< 原始的那一串（加载器 + 分类标签混在一起）
    QStringList gameVersions; ///< 它支持哪些 MC 版本 —— "选游戏版本"那一步就用它

    /// 接口给的那个图标地址（实测大多是 `…_96.webp`，也有直接给 `icon.png` 的）。
    ///
    /// NOTE: **存的是原样那一串**，只在 isUsableIconUrl() 过不去时留空 —— 它是什么格式、
    ///       这一台解不解得动，都不该在这一层判（这一层不碰网络、也不碰解码器）。
    ///       解不动那件事的处置在 ResourceStore 里：走不通就退回 iconPngUrl()。
    QString iconUrl;

    /// 加载器（`categories` 里属于加载器的那几个）。界面上当标签显示、以及挑版本时筛。
    QStringList loaders() const;

    /// 兜底那个图标地址：`https://cdn.modrinth.com/data/{id}/icon.png`（**200 image/png**）。
    /// 项目没传过图标时它是 404 —— 那不算错，画占位方块。
    QString iconPngUrl() const;
};

struct SearchResult {
    bool ok = false;
    QString problem; ///< 给用户看的中文话术（空 = 没问题）
    int totalHits = 0;
    int offset = 0;
    int limit = 0;
    QList<Hit> hits;
};

/// 搜索请求的地址。`limit` 由调用方定（界面上是一页 20 条，`offset` 用于"加载更多"）。
/// `gameVersion` / `loader` 为空 = 不按它筛（facets 里就不出现那一段）。
QString searchUrl(Kind kind, const QString &query, int offset, int limit,
                  const QString &gameVersion = QString(), const QString &loader = QString());

/// 解析搜索响应。**读不懂就如实报**（`problem` 里写清缺了什么），不半懂不懂地返回空列表 ——
/// 空列表在界面上就是"什么都没搜到"，那是另一句话。
SearchResult parseSearch(const QByteArray &json);

/// 「选游戏版本」那一步要摆的一串（**新的在前**）。
///
/// NOTE: 接口给的 `Hit::gameVersions` 是**旧的在前**（Sodium 那份实测从 1.16.3 一路排到
///       26.3），而这个资源"有哪些版本可下"那个接口给的**正好相反**（新的在前，见
///       parseVersions）。同一个"版本"两个字，两个接口的顺序不一样 —— 所以这里单独有个
///       函数、单独钉一条断言，而不是在各个调用点上就地反向一次：就地反的话，将来谁多反
///       一次、谁少反一次，症状都只是"顺序有点怪"，没人会去查。
QStringList gameVersionChoices(const Hit &hit);

/// 兜底那个图标地址（`https://cdn.modrinth.com/data/{id}/icon.png`）。
///
/// 它是**兜底**不是唯一：接口给的 `Hit::iconUrl` 常常是一条小得多的 webp（96px 缩略图），
/// 解得动就先用
/// 那一条（见文件头）。项目没上传图标时这个地址是 404 —— 取不到就画占位方块，不当成错误。
QString iconUrlForProject(const QString &projectId);

/// 这条地址是不是一条 webp（**按路径后缀判**，不联网）。整条 URL 都空时是 false。
///
/// 为什么要有它：webp 那条只在"这台机器解得动"时才值得要（见 ImageDecode::webpAvailable），
/// 而"解得动"和"这一条是不是 webp"是两件事 —— 接口有时直接给 `icon.png`，那时两条地址
/// 是同一个东西，再去"退回"一次就是白发一个请求。
bool isWebpUrl(const QString &url);

/// 接口给的图标地址能不能拿来发请求。
///
/// NOTE: 这是**外部输入**（从接口正文里读的），所以只放行 `https` + `cdn.modrinth.com`，
///       别的域名、别的协议、以及空串一律当"没给"（那一步退回自己拼的 iconPngUrl()）。
///       理由和 ResourceStore 里 isSafeFileStem() 那段一样：这个字符串的下一个用途是
///       "拿去发一个请求"，是整条链上最不该由别人说了算的地方。顺带也挡住
///       `cdn.modrinth.com.别的什么.com` 这种看着像的域名（比的是 host 全等）。
bool isUsableIconUrl(const QString &url);

/// 下载量那串字（`2.3 亿` / `1234 万` / `5678`）。
/// 这是个纯函数是有缘故的：它是**唯一**一处决定"多少万以上换个单位"的地方，
/// 界面上三处都用它，钉成断言才不会三处各说各的。
QString formatDownloads(qint64 downloads);

// ---------- 版本 ----------

/// 清单里要下的那个文件。
struct FileEntry {
    QString url;
    QString filename;
    qint64 size = -1;
    QString sha1;
    bool primary = false; ///< 主文件（一个版本里通常只有一个；界面默认下它）
};

/// 前置模组（这一轮只读出来，不出按钮，见 requiredDependenciesOf）。
struct Dependency {
    QString projectId;
    QString versionId;
    QString type; ///< required / optional / incompatible / embedded

    /// "必须有它才能跑"那一类。
    bool required() const { return type == QStringLiteral("required"); }
};

struct VersionEntry {
    QString id;
    QString versionNumber;
    QString name;
    QString versionType; ///< release / beta / alpha
    QString datePublished;
    QStringList gameVersions;
    QStringList loaders;
    QList<FileEntry> files;
    QList<Dependency> dependencies;

    /// 界面上默认要下的那个文件（`primary` 那条；一个都没有时回第一条；空清单回 nullptr）。
    const FileEntry *primaryFile() const;
};

/// 某个资源在某个 MC 版本（+可选加载器）下的版本列表地址。
/// `gameVersion` / `loader` 为空 = 不筛（界面在没选游戏版本时不该问这条，见调用点注释）。
QString versionsUrl(const QString &projectId, const QString &gameVersion,
                    const QString &loader = QString());

// ---- 请求的"钥匙" ----
//
// 一次异步请求要认得出回来的是不是**我现在等的那一发**：用户连敲两次回车，第一发回来得比
// 第二发晚的话，晚到的那份会把新的那份盖掉（屏幕上显示的是上一个搜索词的结果，而且不报错）。
// 所以每一发都带一把钥匙（见 ResourceStore），这两把是拼钥匙的**唯一出处**。

/// 一次搜索的钥匙（按分类分，一个页面上同时只有一发）。
QString searchRequestKey(Kind kind);

/// 一次"问某个资源的版本列表"的钥匙。带上游戏版本和加载器，因为它们会改变问出来的东西。
QString versionsRequestKey(const QString &projectId, const QString &gameVersion,
                           const QString &loader = QString());

/// 解析版本列表。返回的顺序**就是接口给的顺序**：实测量过，是**新的在前**。
///
/// 2026-09-23 拿两份真响应逐条核过 `date_published`（各 12 条和 22 条，都严格递减），
/// 所以界面直接从上往下画就是了，**不要再反向遍历一次**。这个坑值得写下来：一开始
/// 按"接口大概是从旧到新"的直觉写了个反向，结果列表头条是最老的那个版本，而且不报错。
///
/// 这一层刻意不自己排 —— 写一个"1.9 和 1.10 谁新"的比较器，错了同样不会有任何东西报警，
/// 而接口本来就是按发布时间排好的。
QList<VersionEntry> parseVersions(const QByteArray &json);

// ---------- 前置模组（这一轮只留接口）----------

/// 某一条版本"启动前必须一起下"的那些前置，解析成待下载清单。
///
/// NOTE: 用户要求前置模组**先预留接口、不出按钮**（原话「前置模组记得预留一键下载的接口，
///       先不急」）。所以这里只把"要下谁"算出来，**没有**任何调用方真去下它。
///       下一步接的时候大致是：拿到这张清单 → 逐个问 `versionsUrl(projectId, ...)` →
///       取每个的 primaryFile() 一起塞进下载批次。缺的要如实列给用户（有的前置作者
///       删过版本，问不到东西），别默默少下。
QList<Dependency> requiredDependenciesOf(const QList<VersionEntry> &versions,
                                         const QString &versionId);

} // namespace ResourceCatalog
