/**
 * @file ServerPing.cpp
 * @brief ServerPing 的实现（协议说明在 .hpp 开头那一段）。
 * @author 0x7fffff
 * @date 2026-09-27
 */
#include "ServerPing.hpp"

#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

namespace {

/// 一次 ping 的超时。见 timeoutMs()（唯一出处）。
int g_timeoutMs = 4000;

/// 一个状态包最大认多大（字节）。**必须有上限**：长度前缀是服务器给的，一个坏掉的
/// （或者故意使坏的）服务器报一个 20 亿的长度，我们会一直等一堆永远不来的字节
/// —— 那正好是"超时"这条路上最难看的一种死法。真实的状态包（含图标）几 KB。
constexpr int kMaxPacketBytes = 1024 * 1024;

/// 协议里那个"我是什么版本"的数。**发 -1**（= 我不说）而不是我们自己猜一个：
/// 猜错的话，带版本检查的服务器（代理、跨版本插件）会照着我们报的那个数答一句
/// 「版本不兼容」，MOTD 就成了一行跟我们无关的话。发 -1 它照自己的版本报。
constexpr int kUnknownProtocolVersion = -1;

/// 一个字符串在协议里的写法：VarInt 长度 + UTF-8 正文。
QByteArray stringBytes(const QString &text) {
    const QByteArray utf8 = text.toUtf8();
    return ServerPing::varIntBytes(utf8.size()) + utf8;
}

/// 端口（协议里是**大端** uint16，不是 VarInt）。
QByteArray portBytes(int port) {
    QByteArray out;
    out.append(static_cast<char>((port >> 8) & 0xFF));
    out.append(static_cast<char>(port & 0xFF));
    return out;
}

/// 把"正文"套上一个 VarInt 长度前缀，就是一个包。
QByteArray packet(const QByteArray &body) {
    return ServerPing::varIntBytes(body.size()) + body;
}

/// `description` 那一格展平成**没洗过**的一行（见 motdText()）。
///
/// 这块 JSON 的形态（都不是我们编的，wiki.vg 上那几种真机上都见过）：
///   - `"description": "一句纯文本"`
///   - `"description": {"text": "前半", "extra": [{"text":"后半","color":"red"}, "再来一段"]}`
///   - `extra` 里还能再嵌 `extra`（递归下去）；`color` / `bold` 那些**不要**（我们不画富文本）
///   - 1.20.3 之后 `description` 也可能是**数组**（一段一段的），所以数组那一档也认
QString flattenMotd(const QJsonValue &value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isArray()) {
        QString all;
        const QJsonArray parts = value.toArray();
        for (const QJsonValue &one : parts) {
            all += flattenMotd(one);
        }
        return all;
    }
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        QString all = obj.value(QStringLiteral("text")).toString();
        const QJsonValue extra = obj.value(QStringLiteral("extra"));
        if (extra.isArray()) {
            const QJsonArray parts = extra.toArray();
            for (const QJsonValue &one : parts) {
                all += flattenMotd(one);
            }
        }
        return all;
    }
    return QString();
}

}  // namespace

namespace ServerPing {

int timeoutMs() { return g_timeoutMs; }

void setTimeoutMs(int ms) {
    // 下限 1 毫秒：0 会让每一次 waitFor* 立刻返回，"连不上"和"连上了但还没答"就分不开了
    // （验证工程要压到几十毫秒，那也够）。
    g_timeoutMs = qMax(1, ms);
}

Address splitAddress(const QString &text) {
    Address out;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return out;
    }

    QString hostPart = trimmed;
    QString portPart;
    if (trimmed.startsWith(QLatin1Char('['))) {
        // IPv6 字面量：`[::1]` 或者 `[::1]:25566`。方括号是**给地址里的冒号**用的，
        // 交给我们连的时候要去掉（Qt 收的是裸地址）。
        const int close = trimmed.indexOf(QLatin1Char(']'));
        if (close < 0) {
            return out;
        }
        hostPart = trimmed.mid(1, close - 1);
        const QString rest = trimmed.mid(close + 1);
        if (!rest.isEmpty()) {
            if (!rest.startsWith(QLatin1Char(':'))) {
                return out;
            }
            portPart = rest.mid(1);
        }
    } else {
        // 裸的 IPv6（没加方括号）**不猜**：`::1` 按"最后一个冒号"拆出来是主机 `::`、端口
        // `1` —— 一个看上去完全合法、其实谁也不是的地址。那正是这个函数最不该做的事
        // （把看不懂的地址悄悄改成别的去连，用户看到的会是"这台服务器连不上"）。冒号不止
        // 一个又没方括号，就是看不懂。
        if (trimmed.count(QLatin1Char(':')) > 1) {
            return out;
        }
        const int colon = trimmed.lastIndexOf(QLatin1Char(':'));
        if (colon >= 0) {
            hostPart = trimmed.left(colon);
            portPart = trimmed.mid(colon + 1);
        }
    }

    out.host = hostPart.trimmed();
    if (out.host.isEmpty()) {
        return out;
    }
    if (portPart.isEmpty()) {
        // 没写端口 —— 那是**默认值**，不是"看不懂"。
        out.port = kDefaultPort;
    } else {
        bool numeric = false;
        const int port = portPart.trimmed().toInt(&numeric);
        if (!numeric || port <= 0 || port > 65535) {
            return out;
        }
        out.port = port;
    }
    out.valid = true;
    return out;
}

bool readVarInt(const QByteArray &data, int *offset, int *out) {
    if (offset == nullptr || out == nullptr) {
        return false;
    }
    int result = 0;
    int shift = 0;
    int pos = *offset;
    while (true) {
        // 两种情况都只是"现在读不了"：手上这些字节还不够一个完整的 VarInt，
        // 或者连着 5 个字节都还带着"后面还有"那一位（VarInt 最多 5 字节）。
        if (pos >= data.size() || shift > 28) {
            return false;
        }
        const unsigned char byte = static_cast<unsigned char>(data.at(pos));
        ++pos;
        result |= (byte & 0x7F) << shift;
        shift += 7;
        if ((byte & 0x80) == 0) {
            break;
        }
    }
    *offset = pos;
    *out = result;
    return true;
}

QByteArray varIntBytes(int value) {
    QByteArray out;
    quint32 rest = static_cast<quint32>(value);
    while (true) {
        unsigned char byte = static_cast<unsigned char>(rest & 0x7F);
        rest >>= 7;
        if (rest != 0) {
            byte |= 0x80;
        }
        out.append(static_cast<char>(byte));
        if (rest == 0) {
            break;
        }
    }
    return out;
}

QString cleanMotd(const QString &raw) {
    QString out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QChar ch = raw.at(i);
        // `§` 是两字符一对（`§a` / `§l` / `§r` …），**连着后面那个字符一起丢**。
        // 只丢 `§` 的话，屏幕上会留下一串孤零零的 `a` `l` `r`。
        if (ch == QChar(0x00A7)) {
            ++i;
            continue;
        }
        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QLatin1Char('\t')) {
            out += QLatin1Char(' ');
            continue;
        }
        out += ch;
    }
    // 制表/多行压成空格之后可能连着好几个空格，收一下（顺带把两头的空白去掉）。
    return out.simplified();
}

QString motdText(const QJsonValue &description) {
    return cleanMotd(flattenMotd(description));
}

QByteArray decodeFavicon(const QString &dataUrl) {
    // 只认这一种形态（wiki.vg 上写死的就是它）。别的形态**不猜**：宁可不画图标，
    // 也不要画一个"猜出来的"东西 —— 那看起来像是服务器给了个坏图标。
    static const QString prefix = QStringLiteral("data:image/png;base64,");
    if (!dataUrl.startsWith(prefix)) {
        return QByteArray();
    }
    return QByteArray::fromBase64(dataUrl.mid(prefix.size()).toLatin1());
}

QString connectionErrorText(int socketError) {
    switch (static_cast<QAbstractSocket::SocketError>(socketError)) {
    case QAbstractSocket::ConnectionRefusedError:
        return QStringLiteral("没有回应（对面没开，或者端口不对）");
    case QAbstractSocket::HostNotFoundError:
        return QStringLiteral("找不到这台主机（域名是不是拼错了）");
    case QAbstractSocket::SocketTimeoutError:
    case QAbstractSocket::NetworkError:
        return QStringLiteral("超时（网络不通）");
    case QAbstractSocket::RemoteHostClosedError:
        return QStringLiteral("对面把连接断了");
    case QAbstractSocket::SocketAccessError:
        return QStringLiteral("本机不让连（防火墙或者权限）");
    default:
        return QStringLiteral("连不上");
    }
}

Status ping(const QString &address) {
    Status status;
    const Address target = splitAddress(address);
    if (!target.valid) {
        status.error = QStringLiteral("这个地址看不懂（要写成 主机名:端口，不写端口就是 25565）");
        return status;
    }

    QTcpSocket socket;
    QElapsedTimer clock;
    clock.start();
    const int budget = timeoutMs();
    const auto left = [&clock, budget] { return qMax(1, budget - static_cast<int>(clock.elapsed())); };

    socket.connectToHost(target.host, static_cast<quint16>(target.port));
    if (!socket.waitForConnected(left())) {
        status.error = connectionErrorText(socket.error());
        return status;
    }

    // ① 握手 ② 状态请求。两发一起写出去（服务器那边一个 read 就能收全）。
    const QByteArray handshakeBody =
        varIntBytes(0x00) + varIntBytes(kUnknownProtocolVersion) + stringBytes(target.host) +
        portBytes(target.port) + varIntBytes(1);
    const QByteArray requestBody = varIntBytes(0x00);
    socket.write(packet(handshakeBody));
    socket.write(packet(requestBody));
    if (!socket.waitForBytesWritten(left())) {
        status.error = QStringLiteral("发不出去（连接刚建立就断了）");
        return status;
    }

    // 收：一个长度前缀 + 它说那么长的正文。**按长度收全再解析**（响应可能分好几包到，
    // 尤其是带图标的那种），少一个字节都不能当成"这个包就这样"。
    QByteArray buffer;
    int bodyStart = -1;
    int need = -1;
    while (true) {
        if (need < 0 && !buffer.isEmpty()) {
            int offset = 0;
            int size = 0;
            if (readVarInt(buffer, &offset, &size)) {
                if (size < 0 || size > kMaxPacketBytes) {
                    status.error = QStringLiteral("服务器答的包看不懂（长度不对）");
                    return status;
                }
                bodyStart = offset;
                need = offset + size;
            } else if (buffer.size() > 5) {
                status.error = QStringLiteral("服务器答的包看不懂（开头不是一个长度）");
                return status;
            }
        }
        if (need >= 0 && buffer.size() >= need) {
            break;
        }
        if (!socket.waitForReadyRead(left())) {
            // 「等不到东西」有两种，而**它们不能用 error() 分**：Qt 在 waitForReadyRead 超时
            // 时也会把 error() 设成 SocketTimeoutError（"网络不通"那句），可这时候连接明明
            // 已经建起来了（上面 waitForConnected 过了）—— 该说的是"它没答话"。所以这里按
            // **走到哪一步了**分：连接这一段的错误（对面把连接断了之类）照表翻，剩下的一律
            // 是"连上了、但它一个字都没回"。
            if (socket.error() == QAbstractSocket::SocketTimeoutError ||
                socket.error() == QAbstractSocket::UnknownSocketError) {
                status.error = QStringLiteral("超时（连上了，但服务器没有回答）");
            } else {
                status.error = connectionErrorText(socket.error());
            }
            return status;
        }
        buffer += socket.readAll();
    }

    int pos = bodyStart;
    int packetId = 0;
    if (!readVarInt(buffer, &pos, &packetId) || packetId != 0x00) {
        status.error = QStringLiteral("服务器答的不是状态包");
        return status;
    }
    int jsonLength = 0;
    if (!readVarInt(buffer, &pos, &jsonLength) || jsonLength < 0 || pos + jsonLength > buffer.size()) {
        status.error = QStringLiteral("服务器答的包看不懂（正文长度不对）");
        return status;
    }
    const QJsonDocument document = QJsonDocument::fromJson(buffer.mid(pos, jsonLength));
    if (!document.isObject()) {
        status.error = QStringLiteral("服务器答的不是一段 JSON");
        return status;
    }
    const QJsonObject root = document.object();

    status.ok = true;
    status.latencyMs = static_cast<int>(clock.elapsed());
    status.motd = motdText(root.value(QStringLiteral("description")));
    status.versionName =
        root.value(QStringLiteral("version")).toObject().value(QStringLiteral("name")).toString();
    const QJsonObject players = root.value(QStringLiteral("players")).toObject();
    if (players.contains(QStringLiteral("online"))) {
        status.playersOnline = players.value(QStringLiteral("online")).toInt(-1);
    }
    if (players.contains(QStringLiteral("max"))) {
        status.playersMax = players.value(QStringLiteral("max")).toInt(-1);
    }
    status.iconPng = decodeFavicon(root.value(QStringLiteral("favicon")).toString());
    return status;
}

}  // namespace ServerPing
