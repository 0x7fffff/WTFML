#include "Support.hpp"

void runServerRegistryTests() {
    section("ZX. 服务端工作区：Servers 底下谁在住");

    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir temp;
    check(temp.isValid(), "ZX0 临时数据根目录建出来了");
    if (!temp.isValid()) {
        return;
    }
    AppPaths::setMinecraftDir(temp.path());

    // ---------- ZX1. 根目录与空表 ----------
    check(ServerRegistry::rootDir() == temp.path() + QStringLiteral("/Servers"),
          "ZX1 根就是 `<数据目录>/Servers`", ServerRegistry::rootDir());
    check(ServerRegistry::serverIds().isEmpty(),
          "★ ZX1 根还没建出来 ⇒ 空表（不是错误）");
    QString rootProblem;
    check(ServerRegistry::ensureRoot(&rootProblem) && QDir(ServerRegistry::rootDir()).exists(),
          "ZX1 ensureRoot 把根建出来了", rootProblem);

    // ---------- ZX2. 建一个（我们记的那一份说了算） ----------
    {
        ServerModel::Info info;
        info.name = QStringLiteral("我的服");
        info.flavor = ServerInstall::Flavor::Vanilla;
        info.flavorKnown = true;
        info.gameVersion = QStringLiteral("1.20.1");
        QString problem;
        check(ServerRegistry::createServer(QStringLiteral("Srv1"), info, &problem),
              "ZX2 建出来了", problem);
        check(QFileInfo::exists(AppPaths::serverMetaPath(QStringLiteral("Srv1"))),
              "ZX2 `.wtfml-server` 落在服务端目录里");

        QString describeProblem;
        const ServerRegistry::Entry entry =
            ServerRegistry::describe(QStringLiteral("Srv1"), &describeProblem);
        check(entry.serverId == QStringLiteral("Srv1"), "ZX2 身份是目录名");
        check(entry.name == QStringLiteral("我的服"),
              "ZX2 显示名取我们记的那一份（不是目录名）", entry.name);
        check(entry.flavorKnown && entry.flavor == ServerInstall::Flavor::Vanilla,
              "★ ZX2 自己记的那一档是**已知**的（不打「反推」那句）");
        check(!entry.installed && entry.note.contains(QStringLiteral("缺")),
              "★★ ZX2 还没放 server.jar ⇒ **没装好**，而且说得出缺什么", entry.note);
        check(entry.port == ServerModel::defaultServerPort(),
              "ZX2 没有 server.properties ⇒ 端口是缺省那个", QString::number(entry.port));
        check(!entry.eulaAccepted, "ZX2 还没有 eula.txt ⇒ 没接受");
    }

    // ---------- ZX3. 放一个 server.jar 进去 ⇒ 装好了 ----------
    {
        QFile jar(AppPaths::serverDir(QStringLiteral("Srv1"))
                  + QStringLiteral("/server.jar"));
        if (jar.open(QIODevice::WriteOnly)) {
            jar.write("jar");
            jar.close();
        }
        const ServerRegistry::Entry entry = ServerRegistry::describe(QStringLiteral("Srv1"));
        check(entry.installed,
              "★★ ZX3 盘上出现了 server.jar ⇒ 判成「装好了」（判据是文件，不是我们记的那一句）");
        check(entry.note.isEmpty(), "ZX3 装好之后那句「缺 …」就没了", entry.note);
    }

    // ---------- ZX4. EULA 真存真读 ----------
    {
        QString problem;
        check(ServerRegistry::acceptEula(QStringLiteral("Srv1"), &problem),
              "ZX4 写下一份接受的 eula.txt", problem);
        check(ServerRegistry::readEula(QStringLiteral("Srv1")),
              "★ ZX4 写下去之后读回来就是「已接受」");
        check(ServerRegistry::describe(QStringLiteral("Srv1")).eulaAccepted,
              "ZX4 describe 那一行也跟着是「已接受」");
    }

    // ---------- ZX5. server.properties 真存真读 ----------
    {
        QString problem;
        ServerModel::Properties props;
        props.setValue(QStringLiteral("server-port"), QStringLiteral("25566"));
        props.setValue(QStringLiteral("motd"), QStringLiteral("我的服务器"));
        check(ServerRegistry::writeProperties(QStringLiteral("Srv1"), props, &problem),
              "ZX5 写 server.properties", problem);
        QString readProblem;
        const ServerModel::Properties back =
            ServerRegistry::readProperties(QStringLiteral("Srv1"), &readProblem);
        check(readProblem.isEmpty(), "ZX5 读回来没报毛病", readProblem);
        check(ServerModel::serverPortOf(back) == 25566 &&
                  back.value(QStringLiteral("motd")) == QStringLiteral("我的服务器"),
              "★ ZX5 端口和中文都逐字回来了（按 UTF-8 写、按 UTF-8 读）");
        check(ServerRegistry::describe(QStringLiteral("Srv1")).port == 25566,
              "ZX5 describe 那一行的端口跟着变");
    }

    // ---------- ZX6. 从外面拖进来的：没有我们记的那一份 ⇒ 从文件反推 ----------
    {
        const QString dir = AppPaths::serverDir(QStringLiteral("Paper1"));
        QDir().mkpath(dir + QStringLiteral("/libraries/net/minecraftforge/forge/1.20.1-47.2.0"));
        const auto touch = [](const QString &path) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write("x");
                file.close();
            }
        };
        touch(dir + QStringLiteral(
                       "/libraries/net/minecraftforge/forge/1.20.1-47.2.0/win_args.txt"));
        touch(dir + QStringLiteral("/user_jvm_args.txt"));

        const ServerRegistry::Entry entry = ServerRegistry::describe(QStringLiteral("Paper1"));
        check(!entry.flavorKnown,
              "★★ ZX6 没有 `.wtfml-server` ⇒ 那一档是**反推**的（标成「猜的」）");
        check(entry.flavor == ServerInstall::Flavor::Forge,
              "★★ ZX6 从 libraries 那张 win_args.txt 反推成 Forge", entry.name);
        check(entry.installed,
              "ZX6 两张参数文件都在 ⇒ 判成装好了");
        check(entry.note.contains(QStringLiteral("反推")),
              "★ ZX6 那一行写着「这一档是反推的」，让用户点一下确认");
        check(entry.name == QStringLiteral("Paper1"),
              "ZX6 没有我们记的名字 ⇒ 显示名退回目录名");
    }

    // ---------- ZX7. 名字的清洗 / 去重 ----------
    {
        check(ServerRegistry::sanitizeId(QStringLiteral("a/b:c")) == QStringLiteral("a_b_c"),
              "ZX7 路径分隔符和冒号换成下划线", ServerRegistry::sanitizeId(QStringLiteral("a/b:c")));
        check(ServerRegistry::sanitizeId(QStringLiteral("  .hidden ")) == QStringLiteral("hidden"),
              "★ ZX7 首尾的点和空白摘掉（Windows 存下去会自己吃掉，不如先摘）");
        check(ServerRegistry::sanitizeId(QStringLiteral("Server1.20.1")) ==
                  QStringLiteral("Server1.20.1"),
              "★ ZX7 名字里的点**不摘**（`Server1.20.1` 是用户真机上那种叫法）");
        // 空格必须原样留着（真机上的名字就有带空格的）。这一条专门盯"s 那串禁字符里
        // 混进一个空格"那种写法 —— 症状是每个空格都被换成下划线。
        check(ServerRegistry::sanitizeId(QStringLiteral("我的 服务端 2")) ==
                  QStringLiteral("我的 服务端 2"),
              "★★ ZX7 名字里的空格**原样留着**（不许被当成禁字符换掉）",
              ServerRegistry::sanitizeId(QStringLiteral("我的 服务端 2")));
        check(ServerRegistry::sanitizeId(QString()).isEmpty(),
              "ZX7 洗空了 ⇒ 空串（调用方得让用户重敲）");
        check(ServerRegistry::uniqueServerId(QStringLiteral("Srv1")) == QStringLiteral("Srv1-2"),
              "★★ ZX7 撞名了就加后缀（Srv1 已存在 ⇒ Srv1-2）",
              ServerRegistry::uniqueServerId(QStringLiteral("Srv1")));
        check(ServerRegistry::uniqueServerId(QStringLiteral("全新")) == QStringLiteral("全新"),
              "ZX7 没撞名的原样");
        check(ServerRegistry::defaultServerName(ServerInstall::Flavor::Vanilla,
                                                QStringLiteral("1.20.1")) ==
                  QStringLiteral("Server1.20.1"),
              "ZX7 默认名照用户真机上那份叫法（Server<版本>）");
        check(ServerRegistry::defaultServerName(ServerInstall::Flavor::Vanilla, QString()) ==
                  QStringLiteral("Server"),
              "ZX7 不知道版本就回 Server");
    }

    // ---------- ZX8. 删之前那三道闸 ----------
    {
        check(!ServerRegistry::removeServer(QString(), nullptr),
              "★★ ZX8 空 id 不许删");
        check(!ServerRegistry::removeServer(QStringLiteral("."), nullptr) &&
                  !ServerRegistry::removeServer(QStringLiteral(".."), nullptr),
              "★★ ZX8 `.` / `..` 不许删（那是根）");
        check(!ServerRegistry::removeServer(QStringLiteral("a/b"), nullptr) &&
                  !ServerRegistry::removeServer(QStringLiteral("a\\b"), nullptr),
              "★★ ZX8 带路径分隔符的不许删（别删到根外面去）");
        check(ServerRegistry::removeServer(QStringLiteral("不存在的服"), nullptr),
              "ZX8 本来就不在 = 已经达成目的（回 true）");
    }

    // ---------- ZX9. 真删一个 ----------
    {
        const QString dir = AppPaths::serverDir(QStringLiteral("Srv1"));
        check(QDir(dir).exists(), "ZX9 删之前那个目录在");
        check(ServerRegistry::removeServer(QStringLiteral("Srv1"), nullptr),
              "ZX9 删成功");
        check(!QDir(dir).exists(), "ZX9 目录连带里面的文件都没了");
        check(!ServerRegistry::serverIds().contains(QStringLiteral("Srv1")),
              "ZX9 列表里也不再是它");
    }

    AppPaths::setMinecraftDir(originalDir);
    check(AppPaths::minecraftDir() == originalDir, "ZX9 数据根目录换回来了");
}

