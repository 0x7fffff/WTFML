/**
 * @file ServerPlayers.hpp
 * @brief 服务端那四份玩家名单的**判据层**（coding plan §9.7「玩家管理」）。
 *
 * 四份文件（都由服务端自己写，格式是 Gson 的漂亮 JSON）：
 * * `whitelist.json` —— `[{uuid, name}]`（白名单开关在 `server.properties` 的
 * `white-list`，不在这份文件里）；
 * * `ops.json` —— `[{uuid, name, level, bypassesPlayerLimit}]`；
 * * `banned-players.json` —— `[{uuid, name, created, source, expires, reason}]`；
 * * `banned-ips.json` —— `[{ip, created, source, expires, reason}]`。
 *
 * 这一层**不碰盘**（读文件、写文件在 `ServerSettingsController` 那一边），全是值 + 纯函数，
 * 所以 `verify` 能直接钉。格式上的规矩只有三条：
 *
 * ① **读要宽容**：文件不在 = 空名单（不是错误）；读不懂 = `ok = false` + 一句毛病，
 * **绝不拿空名单去覆盖它**（用户手改坏的 JSON 里可能还留着他自己的记录）；
 * ② **写按游戏那一份的字段形状**：别加自定义键 —— 服务端下次启动读不懂就整份丢掉；
 * ③ 认人：白名单 / op / 封禁玩家认 `uuid`（空的话退回名字），封禁 IP 认 `ip`。
 *
 * 【离线 UUID】
 *
 * 离线模式（`online-mode=false`）的服务端按 `OfflinePlayer:<名字>` 的 MD5 算 UUID
 * （把版本位改成 3、变体位改成 RFC 4122）。所以「按名字加一条」在离线服务器上是**确定的**，
 * 不用登录 Mojang；正版模式下名单里那个 UUID 是 Mojang 的，控制台 `whitelist add` 才会解析
 * 对 —— 界面上那句提醒写在 `MainWindow` 那边。
 */

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace ServerPlayers {

/// 四份名单。**下标就是界面上那个下拉框的下标**（`ServerPlayers::listCount()` 是上限）。
enum class List {
    Whitelist = 0,       ///< 白名单
    Ops = 1,             ///< 管理员
    BannedPlayers = 2,   ///< 封禁的玩家
    BannedIps = 3,       ///< 封禁的 IP
};

inline int listCount() {
    return 4;
}

/// 这份名单落在哪个文件名上（`whitelist.json` 那一类）。
QString fileName(List list);
/// 界面上那一格的标题（「白名单」「管理员」…）。
QString title(List list);

/// 一条记录。四份名单共用这一个形状：用不上的字段留空。
struct Entry {
    QString uuid;   ///< 白名单 / 管理员 / 封禁玩家
    QString name;   ///< 同上
    QString ip;     ///< 封禁 IP
    int level = 4;  ///< 管理员那一档（默认 4，和服务端 `op` 命令一致）
    bool bypassesPlayerLimit = false;  ///< 管理员那一档
    QString reason;   ///< 封禁那两档
    QString source;   ///< 封禁那两档（谁封的）
    QString created;  ///< 封禁那两档（"2026-10-18 12:00:00 +0800"）
    QString expires;  ///< 封禁那两档（`forever` 或同一个时间格式）
};

/// 一份名单读出来的结果。
struct Content {
    bool ok = true;
    QString problem;  ///< `ok = false` 时那一句
    QList<Entry> entries;
};

/// 读一份名单。@p json 是文件字节（文件不在就传空）。
Content parse(List list, const QByteArray &json);

/// 写一份名单（回完整的文件字节，含结尾换行）。**不排序、不丢条目**：顺序照传进来的。
QByteArray serialize(List list, const QList<Entry> &entries);

/// 这一条"是谁"（认人的键）：白名单 / op / 封禁玩家 = uuid（空则名字），封禁 IP = ip。
QString identityOf(List list, const Entry &entry);

/// 按身份找一条（找不到回 -1）。
int indexOf(const QList<Entry> &entries, List list, const QString &identity);

/// 离线服务端给 @p name 算的 UUID（带连字符、小写）。
QString offlineUuid(const QString &name);

/// 造一条新记录：白名单 / op / 封禁玩家 按名字算离线 UUID；封禁 IP 直接收那个地址。
/// @p now 用来写 `created`（封禁那两档）；`expires` 一律 `forever`。
Entry makeEntry(List list, const QString &nameOrIp, const QString &source, const QDateTime &now);

/// 这一条过期了吗（只看封禁那两档的 `expires`）。`forever` / 空 / 认不出来的时间 ⇒ false
/// —— **不猜**：把一条看不懂的到期时间当成"已过期"会让封禁看起来不存在。
bool isExpired(const Entry &entry, const QDateTime &now);

/// 「到什么时候为止」那一句：`forever` → 「永久」；到点 → 「到 <那一刻>」；过期 → 「已经过期」。
QString expiryText(const Entry &entry, const QDateTime &now);

}  // namespace ServerPlayers
