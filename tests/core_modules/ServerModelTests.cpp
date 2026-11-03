#include "Support.hpp"

void runServerModelTests() {
    section("ZW. 服务端判据层：状态 / EULA / properties");

    // ---------- ZW1. 那七档状态 ----------
    check(ServerModel::statusName(ServerModel::Status::NotInstalled) == QStringLiteral("未安装"),
          "ZW1 「未安装」那句话的唯一出处");
    check(ServerModel::statusName(ServerModel::Status::Crashed) == QStringLiteral("异常退出"),
          "ZW1 异常退出是单独一档（和「已停止」不是一句话）");
    check(ServerModel::isLive(ServerModel::Status::Starting) &&
              ServerModel::isLive(ServerModel::Status::Running) &&
              ServerModel::isLive(ServerModel::Status::Stopping) &&
              !ServerModel::isLive(ServerModel::Status::Ready) &&
              !ServerModel::isLive(ServerModel::Status::Stopped),
          "ZW1 「在跑」只有启动中/运行中/停止中三档（可启动/已停止都不算）");
    check(ServerModel::canStart(ServerModel::Status::Ready) &&
              ServerModel::canStart(ServerModel::Status::Stopped) &&
              ServerModel::canStart(ServerModel::Status::Crashed) &&
              !ServerModel::canStart(ServerModel::Status::Running) &&
              !ServerModel::canStart(ServerModel::Status::NotInstalled),
          "★ ZW1 能启动的是可启动/已停止/异常退出；**没装好**和**正在跑**都不能按");

    // ---------- ZW2. eula.txt 算不算"接受了" ----------
    check(!ServerModel::eulaAccepted(QString()),
          "ZW2 空字节（服务端还没跑过）⇒ 还没接受");
    // 服务端自己写的那份文件头里就有 `eula` 字样（网址那段），只按"含 true"判会误判。
    check(!ServerModel::eulaAccepted(
              QStringLiteral("#By changing the setting below to TRUE you are indicating your "
                             "agreement to our EULA (https://aka.ms/MinecraftEULA).\n"
                             "eula=false\n")),
          "★★ ZW2 文件头那段声明里出现 TRUE / eula 字样**不算**接受（判的是 `eula=` 那一项）");
    check(ServerModel::eulaAccepted(QStringLiteral("eula=true\n")),
          "ZW2 eula=true ⇒ 接受");
    check(ServerModel::eulaAccepted(QStringLiteral("eula=TRUE\n")),
          "ZW2 大小写不敏感（服务端自己写的是小写，用户手改的可能是大写）");
    // 后面的盖前面的（Java Properties 语义）：用户先写 false 又跟一行 true ⇒ 以最后那行为准。
    check(ServerModel::eulaAccepted(QStringLiteral("eula=false\neula=true\n")),
          "★ ZW2 两行都有 ⇒ **最后一个有效赋值**说了算");
    check(!ServerModel::eulaAccepted(QStringLiteral("eula=true\neula=false\n")),
          "★ ZW2 反过来（末尾是 false）⇒ 还没接受");
    check(!ServerModel::eulaAccepted(QStringLiteral("agree=true\n")),
          "ZW2 不是 eula 那一项 ⇒ 不算");

    // ---------- ZW3. 我们写下去的那份 eula.txt ----------
    const QString eulaText = ServerModel::eulaFileText(true);
    check(eulaText.contains(QStringLiteral("ka.ms/MinecraftEULA")),
          "★★ ZW3 写下去的那份**留着那句 EULA 声明**（它是「用户接受了」的文本凭证）");
    check(ServerModel::eulaAccepted(eulaText),
          "ZW3 我们自己写的那份，读回来就是「已接受」");
    check(eulaText.endsWith(QStringLiteral("eula=true\n")),
          "ZW3 最后那行就是 eula=true");
    check(!ServerModel::eulaAccepted(ServerModel::eulaFileText(false)),
          "ZW3 写 false 的那一份读回来是「没接受」（留个口子，产品里只会写 true）");

    // ---------- ZW4. server.properties：解 / 改 / 原样写回 ----------
    {
        // 带注释、带 CRLF、键序打乱 —— 解了再写出去，这些**一个字节都不许动**。
        const QString raw = QStringLiteral("#Minecraft server properties\r\n"
                                           "#(这是游戏自己写的文件头)\r\n"
                                           "max-players=20\r\n"
                                           "server-port=25565\r\n"
                                           "motd=A Minecraft Server\r\n");
        const ServerModel::Properties props = ServerModel::parseProperties(raw);
        check(props.value(QStringLiteral("server-port")) == QStringLiteral("25565"),
              "ZW4 读得出一项的值");
        check(props.value(QStringLiteral("motd")) == QStringLiteral("A Minecraft Server"),
              "★ ZW4 值里的空格原样（只裁两侧空白）");
        check(!props.contains(QStringLiteral("max-players")) == false,
              "ZW4 含有的键问得出来");
        check(props.contains(QStringLiteral("server-port")) && !props.contains(QStringLiteral("nope")),
              "ZW4 不存在的键回 false");
        // 没动过 ⇒ 逐字吐回去（含那两行注释、含 CRLF）。
        check(props.toString() == raw,
              "★★ ZW4 没改过 ⇒ 解了再写出来**逐字一样**（注释、键序、CRLF 一个字节都不动）");
    }
    {
        // 改一项：被改的那行重新拼，别的原样。
        ServerModel::Properties props =
            ServerModel::parseProperties(QStringLiteral("#头\nserver-port=25565\nmotd=hi\n"));
        check(props.setValue(QStringLiteral("server-port"), QStringLiteral("25566")),
              "ZW4 改一项返回 true（真改了）");
        check(!props.setValue(QStringLiteral("server-port"), QStringLiteral("25566")),
              "ZW4 改成一样的值返回 false（没改）");
        const QString out = props.toString();
        check(out.contains(QStringLiteral("server-port=25566")) &&
                  out.contains(QStringLiteral("#头")) && out.contains(QStringLiteral("motd=hi")),
              "★ ZW4 只改了那一行，注释和别的键还在");
        // 加一项：没有的键追到末尾。
        check(props.setValue(QStringLiteral("pvp"), QStringLiteral("true")),
              "ZW4 加一项返回 true");
        check(props.toString().endsWith(QStringLiteral("pvp=true\n")),
              "ZW4 新加的键落在末尾");
    }
    {
        // 冒号分隔 / `!` 注释 / 读不懂的行原样留着。
        const ServerModel::Properties props = ServerModel::parseProperties(
            QStringLiteral("!这是感叹号注释\nserver-port:25570\n这行读不懂\n"));
        check(props.value(QStringLiteral("server-port")) == QStringLiteral("25570"),
              "★ ZW4 冒号分隔也认（Java Properties 两种都行）");
        // 读不懂的行走 raw 那条原样吐出。
        check(props.toString().contains(QStringLiteral("这行读不懂")) &&
                  props.toString().contains(QStringLiteral("!这是感叹号注释")),
              "★ ZW4 读不懂的行与 `!` 注释原样保留");
    }

    // ---------- R9. Java Properties 转义：中文 MOTD 在游戏里必须是那个字 ----------
    //
    // Minecraft 服务端按 `Properties.load(InputStream)` 读这份文件：字节按 ISO-8859-1 解，
    // `\uXXXX` 在解析层还原。我们以前按 UTF-8 写中文，游戏里就是一串乱码；而
    // `\uXXXX` 在表单里又被显示成字面量。两条都要能往返。
    {
        // 读：`\uXXXX` 还原成真字（大小写十六进制都认）。
        const ServerModel::Properties decoded =
            ServerModel::parseProperties(
                QStringLiteral("motd=\\u4E2D\\u6587\\u670D\\u52A1\\u5668\n"));
        check(decoded.value(QStringLiteral("motd")) == QStringLiteral("中文服务器"),
              "★ R9 `\\uXXXX` 读回来是真字（表单里不许显示成字面量）",
              decoded.value(QStringLiteral("motd")));
        // 写：非 ASCII 一律转义成大写四位的 `\uXXXX`（游戏按 ISO-8859-1 读也认这一种）。
        check(decoded.toString().contains(
                  QStringLiteral("motd=\\u4E2D\\u6587\\u670D\\u52A1\\u5668")),
              "★★ R9 写盘时非 ASCII 转义成 \\uXXXX（Java Properties 那套）",
              decoded.toString());

        // 反斜杠自己：文件里 `\\` 是一个反斜杠，写回去还是 `\\`（不能越解越少）。
        const ServerModel::Properties slashed =
            ServerModel::parseProperties(QStringLiteral("motd=back\\\\slash\n"));
        check(slashed.value(QStringLiteral("motd")) == QStringLiteral("back\\slash"),
              "★ R9 `\\\\` 读回来是一个反斜杠",
              slashed.value(QStringLiteral("motd")));
        check(slashed.toString().contains(QStringLiteral("motd=back\\\\slash")),
              "★ R9 写回去还是 `\\\\`（往返稳定）", slashed.toString());

        // 控制字符：`\t` 读成制表符、写回去还是 `\t`（不许原样落进行里）。
        const ServerModel::Properties tabbed =
            ServerModel::parseProperties(QStringLiteral("motd=a\\tb\n"));
        check(tabbed.value(QStringLiteral("motd")) == QStringLiteral("a\tb"),
              "★ R9 `\\t` 读成制表符");
        check(tabbed.toString().contains(QStringLiteral("motd=a\\tb")),
              "★ R9 制表符写回去是 `\\t`，不是裸的制表符（裸的会把那一行拆坏）",
              tabbed.toString());

        // 键里的分隔符是转义的：`a\=b=1` 的键是 `a=b`，整行仍然读得回来。
        const ServerModel::Properties keySep =
            ServerModel::parseProperties(QStringLiteral("a\\=b=1\n"));
        check(keySep.value(QStringLiteral("a=b")) == QStringLiteral("1"),
              "★ R9 键里转义过的 `=` 不拿来劈行（Java 的规矩）",
              keySep.toString());
        check(keySep.toString().contains(QStringLiteral("a\\=b=1")),
              "★ R9 那个键写回去仍然转义（否则下一次读就从中间劈开）", keySep.toString());
    }

    // ---------- R16. setValue 和 value()/contains() 说同一套话 ----------
    //
    // 大小写：用户手写的 `Server-Port=` 和表单里的 `server-port` 是同一个键；setValue
    // 大小写敏感的话，表单里改完、value() 读回来还是老值。多个重复键：Java Properties
    // 的规矩是后面的盖前面的，改"第一个"等于没改到生效值。
    {
        ServerModel::Properties props =
            ServerModel::parseProperties(QStringLiteral("Server-Port=25565\n"));
        check(props.setValue(QStringLiteral("server-port"), QStringLiteral("25566")),
              "★ R16 setValue 大小写不敏感（和 value()/contains() 同一套判据）");
        check(props.value(QStringLiteral("server-port")) == QStringLiteral("25566"),
              "★ R16 改完 value() 读得到新值（不是「改了但没生效」）",
              props.value(QStringLiteral("server-port")));

        ServerModel::Properties duplicated =
            ServerModel::parseProperties(QStringLiteral("motd=旧\nmotd=新\n"));
        check(duplicated.value(QStringLiteral("motd")) == QStringLiteral("新"),
              "R16 前提：value() 读的是最后一个");
        check(duplicated.setValue(QStringLiteral("motd"), QStringLiteral("改过")),
              "R16 有重复键时改得动");
        check(duplicated.value(QStringLiteral("motd")) == QStringLiteral("改过"),
              "★★ R16 改的是**最后一个**（生效值），不是第一个",
              duplicated.value(QStringLiteral("motd")));
        // R9 已经把非 ASCII 写成 `\uXXXX`，这里比的是写回盘上的样子，不是内存里的值。
        check(duplicated.toString() ==
                  QStringLiteral("motd=\\u65E7\nmotd=\\u6539\\u8FC7\n"),
              "★ R16 而且只动了那一行（另一行原样，中文照 R9 转义）",
              duplicated.toString());
    }

    // ---------- ZW5. 端口 ----------
    check(ServerModel::serverPortOf(ServerModel::parseProperties(QString())) ==
              ServerModel::defaultServerPort(),
          "ZW5 没写端口 ⇒ 缺省 25565");
    check(ServerModel::serverPortOf(ServerModel::parseProperties(
              QStringLiteral("server-port=25566\n"))) == 25566,
          "ZW5 写了的照读");
    check(ServerModel::serverPortOf(ServerModel::parseProperties(
              QStringLiteral("server-port=abc\n"))) == -1,
          "★ ZW5 读不出来的回 **-1**（「不知道」，调用方别编一个数）");
    check(ServerModel::serverPortOf(ServerModel::parseProperties(
              QStringLiteral("server-port=99999\n"))) == -1,
          "★ ZW5 越界的端口也回 -1");

    // ---------- ZW6. 从文件反推是哪一档 ----------
    const auto infer = [](const QStringList &paths) {
        return ServerModel::inferFlavorFromPaths(paths);
    };
    check(infer(QStringList{QStringLiteral("server.jar")}) == ServerInstall::Flavor::Vanilla,
          "ZW6 只有 server.jar ⇒ 原版那一档（纸/紫水/jar 类同命，缺省最稳）");
    check(infer(QStringList{QStringLiteral("fabric-server-launch.jar"),
                            QStringLiteral("server.jar")}) == ServerInstall::Flavor::Fabric,
          "ZW6 fabric-server-launch.jar ⇒ Fabric");
    check(infer(QStringList{QStringLiteral("quilt-server-launch.jar"),
                            QStringLiteral("server.jar")}) == ServerInstall::Flavor::Quilt,
          "ZW6 quilt-server-launch.jar ⇒ Quilt");
    check(infer(QStringList{QStringLiteral("user_jvm_args.txt"),
                            QStringLiteral(
                                "libraries/net/minecraftforge/forge/1.20.1-47.2.0/win_args.txt")}) ==
              ServerInstall::Flavor::Forge,
          "★ ZW6 认得出 libraries 深处那张 win_args.txt ⇒ Forge");
    check(infer(QStringList{QStringLiteral(
              "libraries/net/neoforged/neoforge/21.1.0/win_args.txt")}) ==
              ServerInstall::Flavor::NeoForge,
          "★ ZW6 neoforged 那一支 ⇒ NeoForge（别和 minecraftforge 混）");

    // ---------- ZW7. 装好了没有 ----------
    {
        ServerInstall::Request vanilla;
        vanilla.flavor = ServerInstall::Flavor::Vanilla;
        vanilla.gameVersion = QStringLiteral("1.20.1");
        const ServerInstall::Plan vPlan = ServerInstall::planFor(vanilla);
        check(ServerModel::isInstalled(vPlan, QStringList{QStringLiteral("server.jar")}),
              "ZW7 原版：server.jar 在 ⇒ 装好了");
        check(!ServerModel::isInstalled(vPlan, QStringList{QStringLiteral("eula.txt")}),
              "ZW7 原版：没有 server.jar ⇒ 没装好");

        ServerInstall::Request quilt;
        quilt.flavor = ServerInstall::Flavor::Quilt;
        quilt.gameVersion = QStringLiteral("1.20.1");
        quilt.installerVersion = QStringLiteral("0.9.0");
        const ServerInstall::Plan qPlan = ServerInstall::planFor(quilt);
        check(!ServerModel::isInstalled(qPlan, QStringList{QStringLiteral("quilt-server-launch.jar")}),
              "★★ ZW7 Quilt 那两张**都要**（光有壳、缺 server.jar 时它一句话不说就退出）");
        check(ServerModel::isInstalled(
                  qPlan, QStringList{QStringLiteral("quilt-server-launch.jar"),
                                     QStringLiteral("server.jar")}),
              "ZW7 Quilt 两张都在 ⇒ 装好了");

        ServerInstall::Request spigot;
        spigot.flavor = ServerInstall::Flavor::Spigot;
        spigot.gameVersion = QStringLiteral("1.20.1");
        const ServerInstall::Plan sPlan = ServerInstall::planFor(spigot);
        check(sPlan.installedMarkers.isEmpty(),
              "ZW7 Spigot 那一档的 markers 是空的（产物名拼不出来）");
        check(ServerModel::isInstalled(
                  sPlan, QStringList{QStringLiteral("spigot-1.20.1-R0.1-SNAPSHOT.jar")}),
              "★★ ZW7 Spigot：扫 spigot-*.jar（名字带它自己的后缀，拼不出来）");
        check(!ServerModel::isInstalled(sPlan, QStringList{QStringLiteral("server.jar")}),
              "ZW7 Spigot：没有 spigot-*.jar ⇒ 没装好");
    }

    // ---------- ZW8. 我们记的那一份（.wtfml-server 的字节） ----------
    {
        ServerModel::Info info;
        info.serverId = QStringLiteral("srv-1");
        info.name = QStringLiteral("我的服");
        info.flavor = ServerInstall::Flavor::Forge;
        info.flavorKnown = true;
        info.gameVersion = QStringLiteral("1.20.1");
        info.loaderVersion = QStringLiteral("47.2.0");
        info.memoryMb = 4096;
        bool ok = false;
        const ServerModel::Info back = ServerModel::fromJson(ServerModel::toJson(info), &ok);
        check(ok, "ZW8 我们写的那一份读得回来");
        check(back.serverId == info.serverId && back.name == info.name &&
                  back.flavor == info.flavor && back.flavorKnown &&
                  back.gameVersion == info.gameVersion &&
                  back.loaderVersion == info.loaderVersion && back.memoryMb == 4096,
              "ZW8 逐字一样地回来了", back.name);
        // 空字节 = 真读懂了（这个服务端什么设置都没有），**不是**读不懂。
        bool emptyOk = false;
        check(ServerModel::fromJson(QByteArray(), &emptyOk).isEmpty() && emptyOk,
              "★★ ZW8 空字节回**空 Info + ok=true**（真读懂了：什么都没有）");
        // 别人写的 / 以后改了格式的 ⇒ 读不懂（别半信半疑地往里加东西）。
        bool foreignOk = false;
        check(ServerModel::fromJson(QByteArrayLiteral("{\"format\":\"别的启动器\",\"version\":1}"),
                                    &foreignOk)
                      .isEmpty() &&
                  !foreignOk,
              "★★ ZW8 格式标记不认 ⇒ 空 Info + **读不懂**");
        bool junkOk = false;
        check(ServerModel::fromJson(QByteArrayLiteral("{ 这不是 json"), &junkOk).isEmpty() &&
                  !junkOk,
              "ZW8 根本不是 json ⇒ 读不懂");

        // 能不能直接拿来启动。
        check(ServerModel::hasLaunchableInfo(info),
              "ZW8 Forge：游戏版本 + 加载器版本都在 ⇒ 能拼出启动命令");
        ServerModel::Info missing = info;
        missing.loaderVersion.clear();
        check(!ServerModel::hasLaunchableInfo(missing),
              "ZW8 Forge 缺加载器版本 ⇒ 拼不出来（得让用户挑一次）");
        ServerModel::Info fabric = info;
        fabric.flavor = ServerInstall::Flavor::Fabric;
        fabric.installerVersion.clear();
        check(!ServerModel::hasLaunchableInfo(fabric),
              "ZW8 Fabric 还要安装器版本");
    }
}

