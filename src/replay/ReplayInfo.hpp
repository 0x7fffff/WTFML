/**
 * @file ReplayInfo.hpp
 * @brief 读一份 ReplayMod 录像（`.mcpr`）的**元数据**。
 *
 * 【这是拿来干什么的】
 *
 * 实例设置左栏「Replay」那一页今天只是把 `.mcpr` 列出来（文件名 + 修改时间），双击
 * 交给系统打开。用户站在一行上看不出这份录像是什么版本录的、录了多久、是单人还是
 * 在哪个服务器上 —— 要的「元数据」在这一页的落点就是它。
 *
 * 这一层**只读盘、只算**：进一个文件路径，出一份 `Info`；界面怎么摆由 ui/ 决定。
 * 它不 include 任何 ui/、也不 include src/controllers。
 *
 * 【`.mcpr` 里有什么】
 *
 * 它就是一个 zip（ReplayMod 自己写的），我们只读其中两样：
 *
 * · `metaData.json` —— 录制开始时写的那一份（版本、时长、时间、作者那一批玩家）；
 * · `recording.tmcpr` —— 真正的数据流（`int32 时间戳 + int32 长度 + 包体`）。
 *
 * **只读 `metaData.json`**：`recording.tmcpr` 动辄几百 MB，把它整个读出来只为数一数
 * 有几个包，是拿界面的一次点击换一次几百 MB 的 IO。所以这里**只起一个 PowerShell
 * 进程**（`ZipUtil::readEntry()` 那一下）；想再列一遍条目表去判"完整不完整"，就是
 * 为一句几乎没有的信息多起一个进程 —— 不做。
 *
 * 【读不出来怎么说】
 *
 * 和 `SchematicInfo` 一个口径：认不出来就如实说，**不猜**。
 *
 * · 不是 zip / 坏了 ⇒ `problem` 里说「打不开」；
 * · 是 zip 但没有 `metaData.json` ⇒ 说「不是 Replay 录像」；
 * · JSON 解析失败 ⇒ 说「元数据读不懂」。
 *
 * 【哪些字段可能没有】
 *
 * ReplayMod 的格式跨了十来年，老录像里 `generator` / `players` / `date` 都可能缺。
 * 缺的字段留"不知道"（`-1` / 空串），**不拿 0 顶**：0 毫秒和"没记录时长"是两件事，
 * 前者会被格式化成「不足 1 秒」摆给用户看。
 */

#pragma once

#include <QString>

namespace ReplayInfo {

struct Info {
    bool ok = false;
    /// 读不出来时的人类可读原因（`ok == true` 时为空）。
    QString problem;

    /// 录制时那个游戏版本（`"1.20.1"`；老录像可能是 `"1.8.9"`）。
    QString mcVersion;
    /// 录制它的客户端（`"ReplayMod v2.6.11"`、`"Flashback"` 之类），可能为空。
    QString generator;
    /// 多人录像的服务器地址；单人录像为空。
    QString serverName;
    /// 单人 / 多人。老录像里没有这一格时按"单人"算（ReplayMod 一直是这个默认）。
    bool singleplayer = true;

    /// 录了多久（毫秒）。**-1 = 没记录**。
    qint64 durationMs = -1;
    /// 录制开始的时间（Unix 毫秒）。**-1 = 没记录**。
    qint64 dateMs = -1;
    /// 录像里记了多少位玩家（含自己）。**-1 = 没记录**。
    int playerCount = -1;
    /// 协议号（老录像可能没有）。**-1 = 没记录**。
    int protocol = -1;

    /// 这份 `.mcpr` 自己的字节数（给列表行那一句用）。
    qint64 fileBytes = 0;
};

/// 读一份 `.mcpr`。失败时 `ok == false`，原因在 `problem` 里。
Info read(const QString &path);

/// 把毫秒数说成人话：`不足 1 秒` / `42 秒` / `3 分 20 秒` / `1 小时 05 分`。
/// 负数返回空串（= 不知道）。
QString durationText(qint64 ms);

/// 把 Unix 毫秒说成本地时间 `yyyy-MM-dd HH:mm`；负数 / 0 返回空串（= 不知道）。
QString dateTimeText(qint64 ms);

/// 列表底下那一格的主句：`3 分 20 秒 · 1.20.1 · 单人 · ReplayMod v2.6.11`。
/// 知道的字段才拼进去；一个都不知道时返回空串（调用方不该把空格子露出来）。
QString summaryText(const Info &info);

/// 悬停提示：上面那句 + 服务器 / 玩家数 / 录制时间 / 文件大小，一行一条。
QString detailText(const Info &info);

}  // namespace ReplayInfo
