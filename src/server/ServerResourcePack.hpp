/**
 * @file ServerResourcePack.hpp
 * @brief 服务端资源包（`server.properties` 里的 `resource-pack` / `resource-pack-sha1`）：
 *        选一份本地 zip、看它像不像资源包、算出 SHA-1。
 * @author 0x7fffff
 * @date 2026-10-18
 * @note coding plan §9.7 那一句：「MOTD 提供格式化预览；服务端资源包选择本地 zip、计算 SHA-1，
 *       并明确 URL 托管仍需用户提供，**启动器不假装本地路径能被其他玩家访问**」。
 *
 * 【为什么这里只算 SHA-1，不替用户填地址】
 *
 * `resource-pack` 那一格是**别的玩家下载用的网址**。用户选的是本机上的一个 zip —— 本机
 * 路径（`C:\Users\…\pack.zip`）在别人的电脑上打不开，填进去的后果是每个人进服务器都被
 * 一个打不开的资源包卡一下（客户端还会弹"下载失败"）。所以这一层**只管两件事**：
 *
 *   1. 看一眼这份 zip 像不像资源包（`pack.mcmeta` 在最外层吗）；
 *   2. 算出它的 SHA-1 —— 用户把 zip 传到自己的 Web / 网盘 / 对象存储之后，那一格就填它。
 *
 * 地址得用户自己给（界面那边明说这一句）。这一层**不把本机路径当地址**，也不猜什么
 * `file:///`。
 *
 * 【"像不像资源包"的判据（照游戏要的来）】
 *
 * 游戏那边读服务端资源包时，看的是 **zip 最外层**那一份 `pack.mcmeta`（不是套在某个
 * 文件夹里的）。所以：
 *
 *   - 最外层有 `pack.mcmeta` ⇒ 可以；
 *   - 只有 `某某/pack.mcmeta` ⇒ **不行**，而且要明说"把里面的东西重新压到最外层" ——
 *     这是用户做包时最常见的一种错，不说他就得自己盯着一个打不开的包猜；
 *   - 一个 `pack.mcmeta` 都没有 ⇒ 不是资源包；
 *   - 只有 `data/` 没有 `assets/` ⇒ 那多半是**数据包**（放错地方了），也明说。
 *
 * NOTE: `pack.mcmeta` 的**大小写**照游戏严格比（zip 的条目名区分大小写）。少数"一键
 *       压缩"工具会写反斜杠，那一层归一化在 `checkEntries()` 里做。
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace ServerResourcePack {

/// 只看条目表得出的结论（不读文件字节、不算哈希 ⇒ verify 里能直接钉）。
struct EntriesCheck {
    /// 能不能当服务端资源包用。
    bool ok = false;
    /// 最外层有没有 `pack.mcmeta`（`ok` 为真时一定是 true）。
    bool hasPackMeta = false;
    /// 最外层有没有 `assets/`（资源包改的是客户端那一侧的东西）。
    bool hasAssets = false;
    /// 里面像不像数据包（有 `data/` 而没有 `assets/`）—— 放错地方时用来改说法。
    bool looksLikeDataPack = false;
    /// 不能用时那句人话（`ok` 为真时是空串）。
    QString problem;
};

/**
 * @brief 一份 zip 的条目表能不能当服务端资源包。
 *
 * @param entries 条目名（`ZipUtil::listEntries()` 那一份；目录条目也在里面）。
 *
 * 失败时 `problem` 那句话直接摆给用户看 —— 所以它要**说清怎么办**（"把里面的东西重新
 * 压到最外层"），不能只说"不合法"。
 */
EntriesCheck checkEntries(const QStringList &entries);

/// 读一份本地 zip 的结果。成功与"为什么没成"都在里面（同 `ServerPing::Status` 那条规矩）。
struct Inspect {
    bool ok = false;
    /// 失败时那句人话（直接显示）。
    QString problem;
    /// 小写十六进制 SHA-1（40 位）。`ok` 为假时是空串。
    QString sha1;
    /// 这个 zip 多大（字节）。`ok` 为假时是 -1。
    qint64 bytes = -1;
    /// 条目表里的文件数（不含目录条目）。给"这个包里有多少个文件"那句提示用。
    int fileCount = 0;
    /// 条目表那一层的结论（`ok` 为真时就是它过了）。
    EntriesCheck entries;
};

/**
 * @brief 看一眼这份本地 zip 并算出 SHA-1。
 *
 * @param zipPath 用户选的那份文件。
 *
 * 三件事按"先便宜后贵"排：先看文件在不在（一次 `QFileInfo`），再列条目表（一个
 * PowerShell 进程），最后才流式读整份算 SHA-1（一个几百 MB 的包要把每个字节读一遍）。
 * **前一步不过就不做下一步** —— 选错一个 4 GB 的文件时不该先卡半分钟再告诉用户
 * "它不是 zip"。
 */
Inspect inspect(const QString &zipPath);

/// 文件数那句话（「123 个文件」）—— 界面/日志里唯一出处。
QString fileCountText(const Inspect &inspect);

}  // namespace ServerResourcePack
