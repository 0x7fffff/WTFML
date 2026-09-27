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
 * 这三发的请求体 / 解析都是**现成的纯函数**（`ModpackExport::` 和 `ModpackImport::`，
 * 导出整合包那条路在用）—— 这一层只负责"发"和"把两份结果拼成一张路径表"。
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
};

/// 一个文件认出来的资源身份。
struct Found {
    /// 递进来的那个**绝对路径**，原样回来（调用方按它认行）。
    QString path;
    QString projectId;
    ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
    /// 接口给的那条图标地址。Modrinth 这条路是**空串**（那个接口不给图标，兜底那条由
    /// `Hit::iconPngUrl()` 按 id 自己拼）；CurseForge 是 `logo.thumbnailUrl`。
    QString iconUrl;
    /// 兜底那条。Modrinth 同样空着；CurseForge 是 `logo.url`。
    QString iconFallbackUrl;
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

}  // namespace InstanceIconLookup
