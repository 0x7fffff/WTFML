/**
 * @file ServerPing.hpp
 * @brief 问一台 Minecraft 服务器"你现在什么样"（Server List Ping / 状态协议）。
 * @author 0x7fffff
 * @date 2026-09-27
 * @note 用户 2026-09-26 问的：「服务器列表能实际 ping 服务器并显示服务器图标和 motd 吗」。
 *       能 —— 就是**游戏自己那个多人列表**背后的那套东西：游戏在多人界面里给每个服务器
 *       显示的那个小图标、那两行 MOTD、在线人数和信号格，全都来自同一个协议。
 *
 * 【这个协议是什么】
 *
 * 一条普通 TCP 连接，客户端发两个包、服务器答一个包（**不登录、不进游戏**）：
 *   ① 握手（packet id 0x00）：协议版本（VarInt）、主机名（String）、端口（uint16）、
 *      下一状态（VarInt，**1 = status**）；
 *   ② 状态请求（packet id 0x00，空正文）；
 *   ③ 服务器答一个包（id 0x00），正文是一段 **JSON**：`description`（MOTD）、
 *      `favicon`（`data:image/png;base64,…`）、`players.online/max`、`version.name`。
 * 全都是 VarInt 长度前缀 + 正文，没有加密、没有压缩。README 级别的说明在
 * https://wiki.vg/Server_List_Ping （离线也能照上面那几行写出来，本条注释就是全部依据）。
 *
 * 【为什么同步】
 *
 * 和 `HttpClient` 同一条理由：调用方是界面上的一次点击，本来就等着一个结果；异步化只会让
 * 两边都多出一堆状态机。**一次问一台**，串起来问的那一段（每台之间让界面转一圈）在
 * MainWindow 里 —— 那是界面的事，不是协议的事，所以不放进这个文件。
 *
 * NOTE: 这个文件**只读**：连上去、问一句、把答案拆开，一个字节都不往下发。它不发聊天、
 *       不登录、不传账号 —— 服务器那边看到的和"有人在多人界面里刷了一下列表"一模一样。
 */

#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

namespace ServerPing {

/// 没写端口时用这个（游戏的默认值）。
constexpr int kDefaultPort = 25565;

/// 一次 ping 的超时（毫秒）。**唯一出处** —— 验证工程把它压到几十毫秒来验"连不上"那一档。
///
/// 4 秒是"能等、但不会让人以为界面死掉"的那一档：真连不上的服务器大多是
/// **连接被拒**（立刻回）或者 DNS 立刻失败；只有丢包的服务器才会耗满它。
int timeoutMs();
void setTimeoutMs(int ms);

/// 一个地址拆出来的两半（见 splitAddress）。
struct Address {
    QString host;
    int port = kDefaultPort;
    bool valid = false;
};

/**
 * @brief 把 `example.com` / `example.com:25566` / `[::1]:25566` 拆成主机和端口。
 *
 * 规矩（就这几条）：
 *   - 两头空白一律去掉；
 *   - `[…]` 包着的是 IPv6 字面量，方括号**要去掉**（Qt 的 connectToHost 收的是裸地址），
 *     方括号后面可以再跟 `:端口`；
 *   - 其余情况按**最后一个** `:` 拆 —— 主机名里本来不会有冒号，最后一个才是"端口那个"；
 *     所以没加方括号的 IPv6（`::1`，冒号不止一个）**看不懂** —— 按最后一个冒号拆会得到
 *     主机 `::`、端口 `1`，一个看着合法、其实谁也不是的地址；
 *   - 没写端口 = kDefaultPort；端口不是数字、或者不在 1..65535 里 = 看不懂（valid=false）。
 *
 * NOTE: 空串 / 只有端口 / 端口写成 `0` 都算看不懂，**不当成"默认端口"** —— 把看不懂的地址
 *       悄悄改成 25565 去连，用户看到的是"这个服务器连不上"，而真正的原因是这行地址坏了。
 */
Address splitAddress(const QString &text);

/// 一次 ping 的结果。**OK 和"为什么没 OK"两件事都在这里面**（同 HttpClient::Reply）。
struct Status {
    bool ok = false;
    /// 没通的原因（**中文**，按 connectionErrorText() 那张表翻；"连上了但没人答"那一档是
    /// ping() 自己写的一句）。ok 为真时是空串。
    QString error;
    /// 从开始连接到收完整个响应花了多久（毫秒）。ok 为假时是 -1。
    int latencyMs = -1;
    /// MOTD：已经把 `§` 颜色码去掉、多行合成一行（见 motdText()）。服务器没给就是空串。
    QString motd;
    /// 服务器自己报的版本名（比如 `Paper 1.20.4`）。没给就是空串。
    QString versionName;
    /// 在线 / 上限人数。**服务器没报就是 -1**（不是 0 —— "一个人都没有"和"它没说"不同）。
    int playersOnline = -1;
    int playersMax = -1;
    /// 图标：`favicon` 那串 data URL 解出来的**原始 PNG 字节**（没给就是空）。
    /// 解成图片是界面那一层的事（这里不碰 QPixmap，util 层不开图形依赖）。
    QByteArray iconPng;
};

/// 问一台（同步，最多 timeoutMs()）。
Status ping(const QString &address);

// ---------- 下面这几个是"拆包和拆 JSON"的纯函数，验证工程直接拿它们核对 ----------

/// VarInt 编出来长什么样（协议里每个长度前缀都是它）。**负数按 5 字节**（补码）。
QByteArray varIntBytes(int value);

/**
 * @brief 从一个字节串的 **offset 处**读一个 VarInt。
 * @param data 待读的字节串。
 * @param offset 进出自：进来是起点，成功时推到 VarInt 后面那个字节。
 * @param out 成功时写入读到的值。
 * @return 读到没有：**false 有两种**（"这一段还不够一个 VarInt"和"超过了 5 字节还是没完"），
 *         两者都只是"现在读不了"，包一层判断由调用方去做 —— 这个函数不做区分，因为
 *         在那两种情况下调用方要做的事是同一件（等更多字节进来 / 判这个包坏了）。
 */
bool readVarInt(const QByteArray &data, int *offset, int *out);

/// `description` 那一格（可能是字符串，也可能是 `{text, extra:[…]}` 的对象）展平成一行。
/// 见 .cpp 里那段"这块 JSON 的形态"。
QString motdText(const QJsonValue &description);

/// `§` 颜色码去掉、换行/制表压成空格、连续空格合并成一个。motdText() 收尾会用一次。
QString cleanMotd(const QString &raw);

/// `data:image/png;base64,…` 解成原始 PNG 字节。**认不出前缀就返回空**（不猜别的形态）。
QByteArray decodeFavicon(const QString &dataUrl);

/// 这个 socket 错误对应哪句话。**自己写一张表，不用 QAbstractSocket::errorString()** ——
/// 那是 Qt 翻译文件里的英文句子，而我们的部署里没有 Qt 的 .qm（`--no-translations`）。
QString connectionErrorText(int socketError);

}  // namespace ServerPing
