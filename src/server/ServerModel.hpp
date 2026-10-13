/**
 * @file ServerModel.hpp
 * @brief 服务端工作区（coding plan §6 / 里程碑 M4）的**判据层**：一个服务端是什么、
 *        它现在处于哪一档状态、`eula.txt` 与 `server.properties` 怎么读怎么写。
 * @author 0x7fffff
 * @date 2026-10-13
 *
 * 【为什么单独一层，和 `download/ServerInstall.hpp` 不是一回事】
 *
 * `ServerInstall.hpp`（2026-09-28 就有的那一层）回答的是「**装**这一档要下什么、跑不跑安装器、
 * 装完必须看到哪个文件、启动它发什么参数」——它是**一次安装动作的计划**，纯函数，参数进、
 * 计划出，一个服务端目录都不认识。
 *
 * 这一层回答的是另一个问题：「**已经装好的那些服务端**，各是哪个目录、哪一档、现在什么状态、
 * 配置文件里写着什么」。它照样是纯函数，但手上那点原料是**一个服务端目录的事实**（目录里有
 * 哪些文件、`eula.txt`/`server.properties` 的字节），不是"要下什么"。
 *
 * 两层的交界只有一处：`inferFlavor()` 拿目录里的文件反推这一档是哪个 flavor、`isInstalled()`
 * 拿 `ServerInstall::Plan::installedMarkers` 判"装好了没有"。别的各管各的 —— 硬揉成一个文件
 * 的话，那个文件会同时长出"网络地址怎么拼"和"用户那几个旋钮存在哪"两套完全无关的东西。
 *
 * 【这一层里没有 QObject、没有网络、不看时钟】
 *
 * 全是值 + 纯函数：喂一串字节进来、回一个结构出去。所以 `verify` 能直接钉（见 tests 的
 * **ZW** 一节）——不用真开一个服务端目录、不用真起进程。真读盘、真起进程留给
 * `ServerRegistry`（清单/落盘）和 `ServerRunner`（进程）。
 */

#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>

#include "download/ServerInstall.hpp"

namespace ServerModel {

// ---------------------------------------------------------------------------
// 状态：§6.2 点名的那七档，一个不多一个不少
// ---------------------------------------------------------------------------

/**
 * @brief 一个服务端**现在**处于哪一档。
 *
 * NOTE: 这里面有两类东西，别混：
 *
 *   * **装没装好**（`NotInstalled`）—— 判据是"入口文件在不在"（见 `isInstalled()`）；
 *   * **进程在不在跑**（`Starting` / `Running` / `Stopping`）—— 判据是进程本身的死活，
 *     和盘上的文件**无关**（一个装在盘上、进程没起的服务端是 `Ready` 或 `Stopped`）。
 *
 * `Stopped` 和 `Crashed` 是"没在跑、但**上一次退出**留下了一个事实"的那两档：前者是干净
 * 退出（退出码 0 / 被请出来），后者是异常退出（非 0 / 崩）。`Ready` 则是"没在跑、也没有
 * 上一次退出的说法"（刚装好、或者从没在这一趟里跑过）——它俩在界面上是两句话，但都能启动。
 */
enum class Status {
    NotInstalled,  ///< 没装好（入口文件缺失）
    Ready,         ///< 装好了、没在跑（可以启动）
    Starting,      ///< 进程起来了、还没认到它真的在跑
    Running,       ///< 在跑
    Stopping,      ///< 发了停、进程还在退出中
    Stopped,       ///< 刚停完（上一次干净退出）
    Crashed,       ///< 上一次异常退出
};

/// 界面上那句状态文字。**唯一出处** —— 界面别自己另写一套。
inline QString statusName(Status status) {
    switch (status) {
    case Status::NotInstalled:
        return QStringLiteral("未安装");
    case Status::Ready:
        return QStringLiteral("可启动");
    case Status::Starting:
        return QStringLiteral("启动中");
    case Status::Running:
        return QStringLiteral("运行中");
    case Status::Stopping:
        return QStringLiteral("停止中");
    case Status::Stopped:
        return QStringLiteral("已停止");
    case Status::Crashed:
        return QStringLiteral("异常退出");
    }
    return QString();
}

/// 这一档算不算"进程在里面活着"（决定界面上那几颗按钮谁亮、底栏状态格画哪个色）。
inline bool isLive(Status status) {
    return status == Status::Starting || status == Status::Running
           || status == Status::Stopping;
}

/// 这一档现在按得动「启动」吗。
inline bool canStart(Status status) {
    return status == Status::Ready || status == Status::Stopped || status == Status::Crashed;
}

// ---------------------------------------------------------------------------
// `eula.txt`
// ---------------------------------------------------------------------------

/**
 * @brief `eula.txt` 里是不是写着接受（`eula=true`）。
 *
 * ★ 判据是**逐行找 `eula=` 那一项、把值小写后比 `true`**，不是"文件里含 true 这个词"：
 *   服务端自己写的那份文件头有一大段注释，里面就有 "https://aka.ms/MinecraftEULA" 这种
 *   带 `eula` 字样的行，而且用户可能在下面手动敲了一行 `eula=FALSE` 又跟了一行 `eula=true`。
 *   逐行取**最后**一个有效赋值，和 Java 的 Properties 语义一致（后面的盖前面的）。
 *
 * NOTE: 传进来的字节可以是空的（文件还没被服务端生成）——那是"还没接受"。
 */
inline bool eulaAccepted(const QString &text) {
    bool accepted = false;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))
            || trimmed.startsWith(QLatin1Char('!'))) {
            continue;
        }
        const int eq = trimmed.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString key = trimmed.left(eq).trimmed();
        if (key.compare(QLatin1String("eula"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        // 最后一个有效赋值说了算（后面的盖前面的）。
        accepted = trimmed.mid(eq + 1).trimmed().compare(QLatin1String("true"),
                                                         Qt::CaseInsensitive)
                   == 0;
    }
    return accepted;
}

/**
 * @brief 我们要写下去的那份 `eula.txt`。
 *
 * ★ **不是**把用户原来那份改一个字节就完事：服务端第一次跑自己写的那份文件头有一句
 *   `By changing the setting below to TRUE you are indicating your agreement to our EULA`
 *   ——我们等于替用户说了这句话，所以**这一行必须原样留着**（它是"用户接受了"这件事的
 *   文本凭证）。用户自己那份长什么样我们不知道（可能是游戏写的、也可能是手改的），
 *   所以由我们自己写一份**内容确定**的、带同一句声明的版本，写进去就是"接受了"。
 *
 * @param accepted 传 false 就是拒绝（写 eula=false）——留个口子，产品里只会写 true。
 */
inline QString eulaFileText(bool accepted) {
    QString text;
    text += QStringLiteral("#By changing the setting below to TRUE you are indicating your "
                           "agreement to our EULA (https://aka.ms/MinecraftEULA).\n");
    text += QStringLiteral("#\n");
    text += QStringLiteral("#我们（启动器）是在用户**主动勾选**之后才写这一行的，见 ServerModel 的文件头。\n");
    text += QStringLiteral("eula=%1\n").arg(accepted ? QStringLiteral("true")
                                                    : QStringLiteral("false"));
    return text;
}

// ---------------------------------------------------------------------------
// `server.properties`
// ---------------------------------------------------------------------------

/**
 * @brief 一份 `server.properties`：**保序**的 key/value，外加原样的注释与空行。
 *
 * 为什么要"保序 + 留注释"：这一页有两种编辑方式（表单 / 原始文本，§6.3），而两种都要能
 * **原地往返**——用户在表单里改了端口、保存，文件里别的东西（注释、他不知道的那些键、
 * 键的顺序）**一个字节都不该动**。所以这里不是 `QHash`，是一条一条的**行**：认出是赋值的
 * 那些行才带 key/value，其余（注释、空行、看不懂的行）原样留着，`toString()` 时照旧吐出去。
 *
 * NOTE: 判据和 Java 的 Properties 对齐到"够用"为止：`key=value` / `key:value`、`#` 与 `!`
 *       开头是注释、两侧空白裁掉。**不**做转义与续行（Minecraft 自己那份文件里没有）。
 */
struct Properties {
    /// 一"行"。`key` 空 = 这一行原样保留（注释 / 空行 / 看不懂的）。
    struct Line {
        QString key;    ///< 空 = 原样保留行
        QString value;  ///< key 非空时才有意义
        QString raw;    ///< key 为空时**原样**吐出去的那一行（含缩进；**不含**行尾的 \r\n）
        /// 这一行原来是用 `\r\n` 结尾的吗。★ 必须记着它，不然"没动过的文件解了再写出来"
        /// 会把整份文件的行尾从 CRLF 翻成 LF —— 那是**整份文件每一个字节都变了**，而用户
        /// 只是想让表单里的端口多一格。
        bool cr = false;
    };
    QList<Line> lines;
    /// 这份文件的行尾是不是 CRLF（解析时看出来的）。**新加的行**跟着它走，别在一份 CRLF 的
    /// 文件里凭空插进一行 LF。
    bool crlf = false;

    /// 找某一项的**最后一个**值（后面的盖前面的，和 Properties 语义一致）。找不到回默认。
    QString value(const QString &key, const QString &fallback = QString()) const {
        QString found = fallback;
        bool seen = false;
        for (const Line &line : lines) {
            if (!line.key.isEmpty()
                && line.key.compare(key, Qt::CaseInsensitive) == 0) {
                found = line.value;
                seen = true;
            }
        }
        return seen ? found : fallback;
    }

    bool contains(const QString &key) const {
        for (const Line &line : lines) {
            if (!line.key.isEmpty() && line.key.compare(key, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    }

    /// 改/加一项（改的是**第一个**出现的同名键；没有就追加到末尾）。返回是否真的改动了。
    bool setValue(const QString &key, const QString &newValue) {
        for (Line &line : lines) {
            if (!line.key.isEmpty() && line.key == key) {
                if (line.value == newValue) {
                    return false;
                }
                line.value = newValue;
                return true;
            }
        }
        Line added;
        added.key = key;
        added.value = newValue;
        added.cr = crlf;  // 跟着这份文件原来的行尾走（别在一份 CRLF 里插一行 LF）
        lines.append(added);
        return true;
    }

    /// 吐回文本。**没动过的行一个字节不动**（含行尾：原来 CRLF 的还是 CRLF）。
    QString toString() const {
        QString out;
        for (const Line &line : lines) {
            if (line.key.isEmpty()) {
                out += line.raw;
            } else {
                out += line.key + QLatin1Char('=') + line.value;
            }
            out += line.cr ? QStringLiteral("\r\n") : QStringLiteral("\n");
        }
        return out;
    }
};

/// 解一份 `server.properties`。**不抛、不报错**：读不懂的行原样留着。
inline Properties parseProperties(const QString &text) {
    Properties props;
    // ★ 行尾那个 `\r` 要**记下来**（不是丢掉）：Windows 上游戏自己写出来的是 CRLF，而
    //   "解了再写回去要逐字一样"是这一页两种编辑方式（表单 / 原始文本）都靠的那条命根子。
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i];
        if (i == lines.size() - 1 && line.isEmpty()) {
            break;  // 末尾那个空串是 split 出来的，不是真一行
        }
        bool cr = false;
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
            cr = true;
            props.crlf = true;
        }
        const QString trimmed = line.trimmed();
        Properties::Line out;
        out.cr = cr;
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))
            || trimmed.startsWith(QLatin1Char('!'))) {
            out.raw = line;  // 原样（含缩进；行尾的 \r 由 toString() 按 cr 补回去）
            props.lines.append(out);
            continue;
        }
        int sep = trimmed.indexOf(QLatin1Char('='));
        const int colon = trimmed.indexOf(QLatin1Char(':'));
        if (sep < 0 || (colon >= 0 && colon < sep)) {
            sep = colon;
        }
        if (sep <= 0) {
            out.raw = line;
            props.lines.append(out);
            continue;
        }
        out.key = trimmed.left(sep).trimmed();
        out.value = trimmed.mid(sep + 1).trimmed();
        // NOTE: 这里把原值两侧的空白裁掉是刻意的（写回去时由 toString() 重新拼 `key=value`）；
        //       换行那一位**没有**裁掉（走 out.cr），所以"没改过的文件解了再写"仍是逐字一样。
        props.lines.append(out);
    }
    return props;
}

/// 默认端口（Minecraft 的默认值，`server.properties` 里没写 `server-port` 时用它）。
inline int defaultServerPort() { return 25565; }

/// 从 properties 里读端口。读不出/越界回 -1（调用方按"不知道"处理，别编一个数）。
inline int serverPortOf(const Properties &props) {
    const QString raw = props.value(QStringLiteral("server-port"));
    if (raw.isEmpty()) {
        return defaultServerPort();
    }
    bool ok = false;
    const int port = raw.toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        return -1;
    }
    return port;
}

// ---------------------------------------------------------------------------
// 这一个服务端目录是哪一档（**只认文件，不认我们记的那一份**）
// ---------------------------------------------------------------------------

/**
 * @brief 拿一个服务端目录里的文件反推它是哪一档。
 *
 * ★ 用途是"用户从外面拖进来一个已经装好的服务端"：他没有我们的 `.wtfml-server`，我们要
 *   自己看出来这一档怎么启动。判据全在**入口文件**上（和 `ServerInstall::Plan::installedMarkers`
 *   同一套判法），所以我们**只在自己记的那一份没有时才问它**——自己记的永远优先（用户可能
 *   手动换了加载器版本，那个事实只有 `.wtfml-server` 说得清）。
 *
 * NOTE: 光靠文件**分不出 Jar 那四档**（原版 / Paper / Purpur / Spigot 都是 `server.jar`，
 *       或者 Spigot 那个带后缀的 jar）—— 但这四档的**启动命令一模一样**
 *       （`-jar server.jar nogui`），所以分不出也不影响启动，回 `Vanilla` 当缺省即可。
 *       Spigot 那一档靠"没有 `server.jar`、但有 `spigot-*.jar`"认出来（见 `pickSpigotJar`）。
 *
 * @param fileNames 目录**根**下的文件名（不含子目录）。
 * @return 认不出来回 `Vanilla`（四档同命，缺省最稳）。
 */
inline ServerInstall::Flavor inferFlavor(const QStringList &fileNames) {
    const auto has = [&fileNames](const QString &name) {
        return fileNames.contains(name);
    };
    if (has(QStringLiteral("fabric-server-launch.jar"))) {
        return ServerInstall::Flavor::Fabric;
    }
    if (has(QStringLiteral("quilt-server-launch.jar"))) {
        return ServerInstall::Flavor::Quilt;
    }
    // Forge / NeoForge 的入口不是 jar，是安装器生成的那张 win_args.txt（见 ServerInstall 的
    // planFor 那段：两张参数文件的路径都是安装器定的）。这里只看根下有没有那张**参数文件**
    // ——它落在 libraries/ 深处，得靠调用方把**相对路径**也交上来（见下面的重载）。
    for (const QString &name : fileNames) {
        if (name.endsWith(QLatin1String("win_args.txt"))) {
            return name.contains(QLatin1String("neoforge"))
                       ? ServerInstall::Flavor::NeoForge
                       : ServerInstall::Flavor::Forge;
        }
    }
    return ServerInstall::Flavor::Vanilla;
}

/// 带**相对路径**的那一版：`relPaths` 是从服务端目录根算起的相对路径（`/` 分隔），
/// 用它可以认出 Forge / NeoForge 落在 `libraries/` 深处的那张 `win_args.txt`。
inline ServerInstall::Flavor inferFlavorFromPaths(const QStringList &relPaths) {
    bool seenNeo = false;
    bool seenForge = false;
    for (const QString &path : relPaths) {
        if (!path.endsWith(QLatin1String("win_args.txt"))) {
            continue;
        }
        if (path.contains(QLatin1String("neoforged"))) {
            seenNeo = true;
        } else if (path.contains(QLatin1String("minecraftforge"))) {
            seenForge = true;
        }
    }
    if (seenNeo) {
        return ServerInstall::Flavor::NeoForge;
    }
    if (seenForge) {
        return ServerInstall::Flavor::Forge;
    }
    // 落到根目录那一套（jar 类的几档）。
    QStringList roots;
    for (const QString &path : relPaths) {
        if (!path.contains(QLatin1Char('/'))) {
            roots.append(path);
        }
    }
    return inferFlavor(roots);
}

/**
 * @brief 这个目录**装好了没有**。
 *
 * 判据是 `ServerInstall::Plan::installedMarkers` 里的每一条都在（见那一段：Forge/NeoForge
 * 看的是那张参数文件、Quilt 要**两张**都在）。
 *
 * ★ Spigot 那一档的 markers 是**空的**（产物名带它自己的后缀、拼不出来，只能扫，见
 *   `pickSpigotJar`）——空 markers 时退回"目录里有没有 `spigot-*.jar`"。
 *
 * @param relPaths 目录里的文件相对路径（`/` 分隔）。用相对路径而不是文件名，是因为
 *        Forge 那两张 markers 落在 `libraries/…` 深处。
 */
inline bool isInstalled(const ServerInstall::Plan &plan, const QStringList &relPaths) {
    if (plan.installedMarkers.isEmpty()) {
        // Spigot 那一路：扫根下有没有 spigot-*.jar。
        QStringList roots;
        for (const QString &path : relPaths) {
            if (!path.contains(QLatin1Char('/'))) {
                roots.append(path);
            }
        }
        return !ServerInstall::pickSpigotJar(roots).isEmpty();
    }
    for (const QString &marker : plan.installedMarkers) {
        if (!relPaths.contains(marker)) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 我们给每个服务端记的那一份（`<服务端目录>/.wtfml-server`）
// ---------------------------------------------------------------------------

/**
 * @brief 一个**服务端工作区**在启动器眼里的样子。
 *
 * 这是"我们额外记的那些"（用户挑的档、加载器版本、Java、内存），**不是**盘上那个目录本身。
 * 用户从外面拖进来一个装好的服务端时，这一份是空的（文件名/目录名就是身份），档由
 * `inferFlavorFromPaths()` 反推。
 */
struct Info {
    /// 稳定 id（改了目录名也不变），和实例那边 `wtfml.meta` 的 `instanceId` 同一个用意。
    /// 空 = 还没有（第一次被写时才生成）。
    QString serverId;
    /// 显示名。空 = 用目录名（`dirName`）。
    QString name;
    /// 哪一档。`inferFlavor` 反推出来的是猜测，用户装的走这里记的。
    ServerInstall::Flavor flavor = ServerInstall::Flavor::Vanilla;
    /// 我们**知不知道** flavor（false = 上面那个是猜的，界面上别当成事实写）。
    bool flavorKnown = false;
    QString gameVersion;       ///< 游戏版本（八档都要）
    QString loaderVersion;     ///< 加载器版本（Forge / NeoForge / Fabric / Quilt）
    QString installerVersion;  ///< 安装器版本（Fabric / Quilt）
    QString buildId;           ///< 构建号（Paper / Purpur；空 = 最新）
    /// 这个服务端用的 Java（空 = 跟全局那套 / 按版本自动挑）。
    QString javaPath;
    /// 内存上限 MB（<= 0 = 用启动器默认那档）。
    int memoryMb = 0;

    bool isEmpty() const {
        return serverId.isEmpty() && name.isEmpty() && gameVersion.isEmpty()
               && loaderVersion.isEmpty() && installerVersion.isEmpty() && buildId.isEmpty()
               && javaPath.isEmpty() && memoryMb <= 0;
    }
};

/// 格式标记（写第一个字段、读的时候先看它）：认不出的文件按"读不懂"处理，
/// **绝不半信半疑地往里加东西**（和 `UpdatePolicy::formatTag()` 同一条规矩）。
inline QString formatTag() { return QStringLiteral("wtfml-server"); }

/// 这一份记的是格式的第几版。
inline int formatVersion() { return 1; }

inline QString flavorTokenOf(ServerInstall::Flavor flavor) {
    return ServerInstall::flavorToken(flavor);
}

inline QByteArray toJson(const Info &info) {
    QJsonObject object;
    object.insert(QStringLiteral("format"), formatTag());
    object.insert(QStringLiteral("version"), formatVersion());
    if (!info.serverId.isEmpty()) {
        object.insert(QStringLiteral("serverId"), info.serverId);
    }
    if (!info.name.isEmpty()) {
        object.insert(QStringLiteral("name"), info.name);
    }
    if (info.flavorKnown) {
        object.insert(QStringLiteral("flavor"), flavorTokenOf(info.flavor));
    }
    if (!info.gameVersion.isEmpty()) {
        object.insert(QStringLiteral("gameVersion"), info.gameVersion);
    }
    if (!info.loaderVersion.isEmpty()) {
        object.insert(QStringLiteral("loaderVersion"), info.loaderVersion);
    }
    if (!info.installerVersion.isEmpty()) {
        object.insert(QStringLiteral("installerVersion"), info.installerVersion);
    }
    if (!info.buildId.isEmpty()) {
        object.insert(QStringLiteral("buildId"), info.buildId);
    }
    if (!info.javaPath.isEmpty()) {
        object.insert(QStringLiteral("javaPath"), info.javaPath);
    }
    if (info.memoryMb > 0) {
        object.insert(QStringLiteral("memoryMb"), info.memoryMb);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

/**
 * @brief 读回那一份。
 *
 * ★ `ok = false` 有两种情形（和 `UpdatePolicy::fromJson()` 逐字同源）：根本不是一个 json
 *   对象；**或者**格式标记 / 版本号不认。两种都回**空 Info**，差别只在那句话怎么写。
 * ★ 空字节回**空 Info + `ok = true`**（那是真读懂了：文件里什么都没有，或者是空的/不存在的
 *   文件）——所以调用方按 `ok` 办事时，"读不懂"那句话一定会被说出来，而不是被当成"这个
 *   服务端什么设置都没有"。
 */
inline Info fromJson(const QByteArray &bytes, bool *ok = nullptr) {
    if (ok != nullptr) {
        *ok = false;
    }
    if (bytes.trimmed().isEmpty()) {
        if (ok != nullptr) {
            *ok = true;
        }
        return Info{};
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return Info{};
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("format")).toString() != formatTag()
        || object.value(QStringLiteral("version")).toInt() != formatVersion()) {
        return Info{};
    }
    Info info;
    info.serverId = object.value(QStringLiteral("serverId")).toString();
    info.name = object.value(QStringLiteral("name")).toString();
    if (object.contains(QStringLiteral("flavor"))) {
        ServerInstall::Flavor flavor = ServerInstall::Flavor::Vanilla;
        if (ServerInstall::flavorFromToken(object.value(QStringLiteral("flavor")).toString(),
                                           &flavor)) {
            info.flavor = flavor;
            info.flavorKnown = true;
        }
    }
    info.gameVersion = object.value(QStringLiteral("gameVersion")).toString();
    info.loaderVersion = object.value(QStringLiteral("loaderVersion")).toString();
    info.installerVersion = object.value(QStringLiteral("installerVersion")).toString();
    info.buildId = object.value(QStringLiteral("buildId")).toString();
    info.javaPath = object.value(QStringLiteral("javaPath")).toString();
    info.memoryMb = object.value(QStringLiteral("memoryMb")).toInt(0);
    if (ok != nullptr) {
        *ok = true;
    }
    return info;
}

/// 这一份记的档能不能直接拿来启动（不能的话得先反推 / 让用户挑一次）。
inline bool hasLaunchableInfo(const Info &info) {
    switch (info.flavor) {
    case ServerInstall::Flavor::Forge:
    case ServerInstall::Flavor::NeoForge:
        return !info.gameVersion.isEmpty() && !info.loaderVersion.isEmpty();
    case ServerInstall::Flavor::Fabric:
        return !info.gameVersion.isEmpty() && !info.loaderVersion.isEmpty()
               && !info.installerVersion.isEmpty();
    case ServerInstall::Flavor::Quilt:
        return !info.gameVersion.isEmpty() && !info.installerVersion.isEmpty();
    default:
        return !info.gameVersion.isEmpty();
    }
}

}  // namespace ServerModel
