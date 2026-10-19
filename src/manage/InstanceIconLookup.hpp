/**
 * @file InstanceIconLookup.hpp
 * @brief 「这一批文件分别是哪个资源」—— 按文件内容反查 Modrinth / CurseForge。
 * @author 0x7fffff
 * @date 2026-09-27
 * @note 用户 2026-09-27 原话：「模组和资源包等都有一个图标，把图标显示出来」。
 *
 * 【为什么需要反查】
 *
 * 盘上那些 `mods/sodium.jar` 的文件名**不是身份**：用户改过名、整合包里的文件名各家
 * 写得不一样、同一个资源两版的文件名可以完全不同。而"把图标显示出来"要的是那个**资源**
 * 的图标（作者自己传的那一张）。能把这一个文件和一个资源对上的东西只有**文件内容**：
 *
 *   - Modrinth：`sha1` → `POST /v2/version_files`（批量）；
 *   - CurseForge：`fileFingerprint`（murmur2 那一套）→ `POST /v1/fingerprints`（批量），
 *     再拿返回的 `modId` 去 `POST /v1/mods` 换 `logo` 那两条地址。
 *
 * 认出来的项目号还要再问一发 `GET /v2/projects?ids=…` 才拿得到**图标地址**（2026-09-27
 * 补，见 `Found::iconUrl` 和 `Result::iconProblem`）：Modrinth 那两发都给不出项目的图标，
 * 而自己按 id 拼的那条 `<id>/icon.png` **只是老项目的别名**，绝大多数项目根本没有 ——
 * 少了这一发，这一页上"大多数模组只有占位拼图"。
 *
 * 那四发的请求体 / 解析都是**现成的纯函数**（`ModpackExport::`、`ModpackImport::` 和
 * `ResourceCatalog::`，导整合包 / 下载页那两条路在用）—— 这一层只负责"发"和"把几份结果
 * 拼成一张路径表"。
 *
 * 【为什么不复用 VersionManager 那两个 lookup】
 *
 * `VersionManager::lookupModrinthVersionFiles` / `lookupCurseForgeFingerprints` 是**同一件事**
 * 的现成实现，但它俩用的是 `loaderNetworkManager()`（一个归主线程的 QNAM），而
 * `HttpClient::send` 是在**调用线程**上开嵌套事件循环 —— 在工作线程里调它们就是跨线程
 * 碰主线程的 QNAM。在主线程调则会卡住界面几秒（用户抱怨的正是"锁住其他按钮"，见
 * ServerPing 那一课的同一个形状）。
 *
 * 所以这一层在自己的线程里**自建一个 QNAM**（`lookup()` 里那个栈上的局部对象），三发都用
 * 它。两份实现并存是这次改动里唯一一处重复，代价换来的是"反查不进界面线程"。
 *
 * NOTE: 这一层**只在失败时给一句 `problem`，不弹任何东西**。图标取不到是常态（自己改过的
 *       jar、作者没传过图标的、断网的），画个占位图就是正确的界面。那句话是给日志的。
 *
 * 【结果留一份在盘上：同一批文件不查第二遍】
 *
 * ★★ 2026-10-10（用户原话：「模组图标做本地缓存功能，不要每次都拉取」）。上面那两三发
 * 里有一样东西很贵：**为了反查而把这一页每一份 jar 从头读一遍**（算 sha1 + 指纹 —— 一列
 * 几十个模组就是几百 MB 的读盘），再加上三家接口的往返。而"这一页文件分别属于哪个资源"
 * 不会一秒一变：文件没换（钥匙里带着大小和修改时间）、资源也不会自己换。
 *
 * 所以 `lookup()` 自己管一份**盘上缓存**（`Options::cacheDir` / `cacheKey`，空 = 不用）：
 * 进门先把命中的那些**直接摊成 `Found`**（一个字节的网络都不走），只把剩下的交给上面那
 * 两三发；收尾时把这一趟的结果与盘上那份**合并后写回去**。命中和不命中都是**逐文件**
 * 的 —— 一列二十个模组里换掉一个，只有那一个要重查。
 *
 * NOTE: 键是**「禁用」那四个字摘掉之后**的路径 + 大小 + 修改时间（和
 *       `MainWindow::instanceIconCacheKey()` **逐字同一个口径**）—— 用户点一下开关只是
 *       把文件改了个名，盘的字节一个都没动，那种时候不该重查一遍。
 * NOTE: 存阴性（问过了、这两家都没有）有个前提：见 `Result::problem` 那段，只有**整趟
 *       完整跑完**了才敢记 —— 断网那一趟记下去，用户网好了也看不到图标了。
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "../download/ResourceCatalog.hpp"

namespace InstanceIconLookup {

/// 反查要问的那两台接口。
struct Options {
    /// 用户在设置页填的 CurseForge API key。空 = 走那台不要 key 的社区镜像
    /// （规矩和 `ModpackImport::curseForgeApiBase()` 逐字一致，那一句在实现里调它）。
    QString curseForgeApiKey;
    /// 验证工程的口子（`Launcher/ModrinthApiBase` / `Launcher/CurseForgeApiBase` 那个键）。
    /// 产品自己从不写 —— 和 `VersionManager` 里那两个 override 是同一个用途。
    QString modrinthApiBaseOverride;
    QString curseForgeApiBaseOverride;

    /// ---------- 盘上那份缓存的落脚处（★★ 2026-10-10 加，见下面那段）----------
    ///
    /// 缓存目录（产品递 `AppPaths::instanceIconCacheDir()`）。**空串 = 不用缓存**
    /// （验证工程里那些一节一个临时目录的用例不想要它，就不给）。
    QString cacheDir;
    /// 这一批文件属于**哪一堆**（产品递实例 id）—— 缓存文件名是它的 sha1。
    /// 同一份目录表才该共用一份缓存；空串 = 不用缓存（同上）。
    QString cacheKey;
    bool requireVersionMetadata = false;
    bool cacheOnly = false; // Read valid cached identities only; no hashing/network/cache writes.
};

/// 一个文件认出来的资源身份。
struct Found {
    /// 递进来的那个**绝对路径**，原样回来（调用方按它认行）。
    QString path;
    /// 这个文件认出来的那个资源：`projectId` / `source` / 两条图标地址，以及
    /// **详情页要的那几样**（名字 / slug / 简介 / 支持的游戏版本 / 下载量 / 更新时间）。
    ///
    /// ★ 2026-09-27：从前这里散着放着 `projectId` / `source` / 两条图标地址，现在整份
    ///   `Hit` 收一条。缘由是用户那句「除了模组以外的几个都是点击模组名称自动进入下载界面
    ///   （前提是匹配上了）」—— 那一页的头卡要名字和 slug，而**同一份响应里本来就有**
    ///   （见 `ResourceCatalog::parseProjects()` 那段）。要一个文件的身份就得给一整条，
    ///   不然接的人手上永远缺几栏、迟早再往这儿加第四个字段。
    ///
    /// NOTE: 图标那两条取不到时是空串（那一格退回占位拼图），**身份照样是准的**；
    ///       详情没取到时 `title` 那几栏也空着（那一趟 `iconProblem` 立着），点名字那一下
    ///       于是什么都不做 —— 而不是进一个没有名字的空页。
    ResourceCatalog::Hit hit;
    /// 盘上那个文件的 **sha1**（2026-09-28 加）。空串 = 没算出来（文件读不动）。
    ///
    /// ★ 为什么这一层要把它带出来：实例设置那个「检查更新」的判据**只有一条** ——
    ///   盘上这一份的 sha1 落在项目版本列表的第几条上（第 0 条 = 最新的那条），
    ///   见 `ModDependencies::judgeUpdate()`。而 sha1 本来就是这一趟**为了反查而算的**
    ///   （`hashFile(path, true, …)` 一趟把 sha1 和指纹一起算出来），带出来不多读一个字节。
    ///   **不许**改成"按文件名认版本"：模组文件重命名是家常便饭（`xxx-1.2.jar` 和
    ///   `xxx.jar` 是同一份），而同一个文件的 sha1 换不了。
    QString sha1;
    ResourceCatalog::VersionEntry currentVersion;
};

/// 一趟反查的全部结果。
struct Result {
    /// 认出来的那些（**顺序不定** —— 两份回复都是哈希表，别按位置对）。
    QList<Found> found;
    /// 算成了哈希、发出去了的文件数。
    int hashed = 0;
    /// 读不动（不在盘上、被别的进程占着）的文件数 —— 不算错，只是这一批里少几个。
    int unreadable = 0;
    /// 非空 = 这一趟**没有完整跑完**（断网 / 接口挂了 / 回复读不懂）。里面是给日志的一句话。
    ///
    /// ★ 这个字段有一个**判据上的用途**，不只是日志：接的人按它决定"没认出来的那些要不要
    ///   记成'问过了、没有'"（见 MainWindow 里那段）。网络断了那一趟如果记成"没有"，
    ///   用户这一整个会话都再也看不到图标了 —— 而那本来是"下一次网好了就会有"。
    QString problem;

    /// 非空 = **图标地址那一发**没成（`GET /v2/projects?ids=…`，见 `Found::iconUrl`）。
    /// 身份都认出来了，缺的只是那几格的图。里面是给日志的一句话。
    ///
    /// ★ 它和 `problem` **必须分开**，不能合并：`problem` 是"这一趟完整不完整"的判据，
    ///   而这一发失败**不影响那个判断** —— 两份回复（Modrinth / CurseForge）都已经问完，
    ///   没认出来的那些确实不是这两家的资源，照样该记成"问过了、没有"。混进 `problem`
    ///   里的代价是下一趟（用户关掉设置页再打开）会把几十兆 jar **从头重读重算一遍**，
    ///   换来的却只是"图标可能来了"。
    QString iconProblem;
};

/**
 * @brief 认这几个文件（**同步、阻塞**，给工作线程用）。
 *
 * @param paths 绝对路径表。目录、以及不像资源包的文件（不是 `.jar`/`.zip`，见
 *        `isLookupCandidate()`）在这一层会被跳过 —— 调用方**不必先筛**，但筛了更好
 *        （少读几个文件）。
 * @param options 见上。
 *
 * NOTE: 这个函数**在哪个线程被调，就在哪个线程干活**，而且它自己 new 一个 QNAM ——
 *       必须在**将要阻塞**的那个线程里直接调（不要在主线程调，也不要跨线程调）。
 *       进界面线程的那一半（`ResourceStore::requestIcon`）是接的人的事。
 */
Result lookup(const QStringList &paths, const Options &options = Options());

/// 这个文件名值不值得拿去反查：去掉了 `.disabled` 尾巴之后是 `.jar` / `.zip`。
/// （禁用着的模组是 `X.jar.disabled`，内容还是那个 jar，一样查得出来。）
bool isLookupCandidate(const QString &path);

/// 这一堆文件的缓存文件在哪：`<cacheDir>/<cacheKey 的 sha1>.json`。
///
/// ★ **唯一出处** —— 写它的是 `lookup()` 自己，验证工程靠它把这个文件摆好/删掉来验
/// "第二趟一个请求都不发"（见 .cpp 里那段）。`cacheDir` 或 `cacheKey` 是空串时回空串
/// （= 这一趟不缓存）。
/// NOTE: 文件名用 sha1 而不是实例 id 本身：实例 id 就是**目录名**，里面有中文、空格、
///       甚至 `∞`（`20w14∞` 那种）—— 那些字符能不能进文件名要按平台问，不值得。
QString cachePath(const Options &options);

}  // namespace InstanceIconLookup
