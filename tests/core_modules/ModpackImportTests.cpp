#include "Support.hpp"

void runModpackImportTests(const QString &sandbox) {
    section("V. 整合包导入：清单解析 / 路径判据 / 按前缀解压");

    // --- V1. 清单文件叫什么、认哪些加载器 -------------------------------------
    check(ModpackImport::modrinthIndexName() == QStringLiteral("modrinth.index.json"),
          "V1 Modrinth 的清单文件名叫 modrinth.index.json",
          ModpackImport::modrinthIndexName());
    check(ModpackImport::curseForgeManifestName() == QStringLiteral("manifest.json"),
          "V1 CurseForge 的清单文件名叫 manifest.json",
          ModpackImport::curseForgeManifestName());
    // 顺序**钉住**：靠遍历 QJsonObject 的键去挑"包里要哪个加载器"是拿实现细节当规则用，
    // 换一次 Qt 就可能挑到另一个。四条的顺序 = 认的顺序。
    const QStringList keys = ModpackImport::loaderDependencyKeys();
    check(keys.size() == 4 && keys.at(0) == QStringLiteral("fabric-loader") &&
              keys.at(1) == QStringLiteral("forge") && keys.at(2) == QStringLiteral("neoforge") &&
              keys.at(3) == QStringLiteral("quilt-loader"),
          "V1 认的加载器键是固定的四条，顺序也固定", keys.join(QLatin1Char(',')));
    check(ModpackImport::unsupportedLoaderText(QString(), QString()).isEmpty() &&
              ModpackImport::unsupportedLoaderText(QStringLiteral("fabric-loader"),
                                                   QStringLiteral("0.16.14"))
                  .isEmpty() &&
              ModpackImport::unsupportedLoaderText(QStringLiteral("forge"),
                                                   QStringLiteral("47.4.10"))
                  .isEmpty(),
          "V1 纯原版 / Fabric / Forge 都算能用，没有一句拦人的话");
    const QString quiltText = ModpackImport::unsupportedLoaderText(
        QStringLiteral("quilt-loader"), QStringLiteral("0.20.0"));
    check(quiltText.contains(QStringLiteral("Quilt")) &&
              quiltText.contains(QStringLiteral("没有")) &&
              quiltText.contains(QStringLiteral("0.20.0")),
          "V1 Quilt 拦下来，而且说清了是哪个加载器、哪个版本、没有导入", quiltText);
    // NOTE: 这一条 改过。原来钉的是"CurseForge 那一支要把「清单里没有下载
    // 地址」说清楚"—— 那一支已经删了（CurseForge 现在装得了，见 ZG 节），留着
    // 就等于钉一个不存在的分支。现在这个函数只回答一件事：**两个清单都没有**
    // ⇒ 认不出它是什么格式。所以钉的是"把两个名字都写出来"，以及它替用户猜了
    // 一句"多半是个普通压缩包"（不说这一句，用户只能自己猜要不要放弃）。
    const QString cfText = ModpackImport::unsupportedFormatText(ModpackImport::Format::CurseForge);
    check(cfText.contains(QStringLiteral("manifest.json")) &&
              cfText.contains(ModpackImport::modrinthIndexName()) &&
              cfText.contains(QStringLiteral("压缩包")),
          "V1 认不出格式那句话：两个清单文件名都写着 + 替用户猜一句它多半是什么", cfText);
    const QString unknownText = ModpackImport::unsupportedFormatText(ModpackImport::Format::Unknown);
    check(unknownText.contains(QStringLiteral("modrinth.index.json")) &&
              unknownText.contains(QStringLiteral("manifest.json")),
          "V1 认不出格式的话把两种清单的名字都说了出来（用户能自己去核对）", unknownText);

    // --- V2. 路径判据（这条漏一个 = 整合包能往实例目录外面写文件） --------------
    {
        struct Case {
            const char *path;
            bool safe;
            const char *why;
        };
        const Case cases[] = {
            {"mods/sodium.jar", true, "普通的相对路径"},
            {"config/openloader/data/x.zip", true, "多几层也可以"},
            {"mods/../../x.jar", false, "逐段判：中间那段 `..` 拦得住"},
            {"../../x.jar", false, "开头的 `..`"},
            {"..\\..\\x.jar", false, "反斜杠一律拒绝（按 / 切它就是一整段，逐段看不出问题）"},
            {"mods\\sodium.jar", false, "同上：Windows 上反斜杠也是分隔符"},
            {"/etc/passwd", false, "绝对路径"},
            {"C:/x.jar", false, "盘符（冒号本来就在非法字符里）"},
            {"mods/a:b.jar", false, "段里带冒号"},
            {"", false, "空路径"},
            {"mods/", false, "结尾多一个斜杠会切出一个空段"},
            {"a//b", false, "中间空段"},
            {"mods/./x.jar", false, "`.` 不是一段合法的目录名"},
            {"mods/x./y.jar", false, "以点结尾的段（Windows 会悄悄吃掉那个点）"},
            {"mods/x.jar ", false, "以空格结尾的段（同上；整合包这条路不做 trim）"},
            {"mods/CON.jar", false, "段的主名是 Windows 保留设备名"},
            {"mods/con.txt", false, "保留名判据不区分大小写（section('.') 之后是 con）"},
        };
        int wrong = 0;
        for (const Case &c : cases) {
            if (ModpackImport::isSafeRelativePath(QString::fromUtf8(c.path)) != c.safe) {
                ++wrong;
                out() << "        [判错了] " << c.path << "（该是"
                      << (c.safe ? "安全" : "不安全") << "：" << c.why << "）\n";
            }
        }
        check(wrong == 0, "V2 路径安全性真值表 17 条全对", QStringLiteral("错 %1 条").arg(wrong));

        // 长度：240 是上限（Windows 的 MAX_PATH 260 减去实例目录那一段）。边界要判准，
        // 否则"刚好 240 的正常长路径"会被整包拒绝。
        const QString ok240 = QStringLiteral("mods/") + QString(235, QLatin1Char('a'));
        check(ok240.length() == 240 && ModpackImport::isSafeRelativePath(ok240),
              "V2 正好 240 个字符的路径放行", QStringLiteral("长度 %1").arg(ok240.length()));
        const QString tooLong = ok240 + QLatin1Char('a');
        check(!ModpackImport::isSafeRelativePath(tooLong), "V2 241 个字符的路径拒绝");
    }

    // --- V3. 从包名推实例名 ---------------------------------------------------
    {
        check(ModpackImport::suggestInstanceName(QStringLiteral("  一个包  ")) ==
                  QStringLiteral("一个包"),
              "V3 前后的空白去掉");
        check(ModpackImport::suggestInstanceName(
                  QStringLiteral("ATT2: The Journey Of Revelation")) ==
                  QStringLiteral("ATT2- The Journey Of Revelation"),
              "V3 建不出目录的字符换成 `-`（这里是 名字里的 `:`）",
              ModpackImport::suggestInstanceName(
                  QStringLiteral("ATT2: The Journey Of Revelation")));
        check(ModpackImport::suggestInstanceName(QStringLiteral("包.")) == QStringLiteral("包"),
              "V3 结尾的点去掉（Windows 会静默吃掉它）",
              ModpackImport::suggestInstanceName(QStringLiteral("包.")));
        check(ModpackImport::suggestInstanceName(QString()) == QStringLiteral("整合包") &&
                  ModpackImport::suggestInstanceName(QStringLiteral("   ")) ==
                      QStringLiteral("整合包") &&
                  ModpackImport::suggestInstanceName(QStringLiteral("..")) ==
                      QStringLiteral("整合包"),
              "V3 空的 / 全是空白 / `..` 都退化成一句通用的名字");
        const QString con = ModpackImport::suggestInstanceName(QStringLiteral("CON"));
        check(con != QStringLiteral("CON") && con.contains(QStringLiteral("CON")),
              "V3 保留设备名换个说法绕开（不能原样用它建目录）", con);
        const QString longName = ModpackImport::suggestInstanceName(QString(200, QLatin1Char('x')));
        check(longName.length() <= InstanceFiles::kMaxDirNameLength && !longName.isEmpty(),
              "V3 超长的名字收到实例名的长度上限内",
              QStringLiteral("长度 %1").arg(longName.length()));
        // 这一条是这一节里最要紧的：候选名字是**界面预填**到输入框里的，它自己过不了
        // 校验的话，用户一进来就撞一句报错，而他什么都没输。
        // NOTE: 这里比的是 suggestInstanceName() 的**产物**，不是包名本身 —— 包名里带
        // 冒号、带结尾的点、叫 CON 都是正常的（真那份就叫
        // "ATT2: The Journey Of Revelation.1.8.4"，名字里就有冒号）。要证的是
        // "推出来、预填进输入框的那个名字，用户直接点确定就能过"。
        const QStringList packNames = {QStringLiteral("ATT2: The Journey Of Revelation.1.8.4"),
                                       QStringLiteral("包."), QStringLiteral("CON"), QString(),
                                       longName, QStringLiteral("带 空格 的名字"),
                                       QStringLiteral(".."), QStringLiteral("名字里带/斜杠")};
        int bad = 0;
        for (const QString &packName : packNames) {
            const QString candidate = ModpackImport::suggestInstanceName(packName);
            const QString problem = InstanceFiles::validateRenameTarget(candidate, QString());
            if (!problem.isEmpty()) {
                ++bad;
                out() << "        [推出来的名字过不了校验] 包名「" << packName << "」-> 「"
                      << candidate << "」：" << problem << "\n";
            }
        }
        check(bad == 0, "V3 推出来的每一个候选名字都过得了 validateRenameTarget()",
              QStringLiteral("坏 %1 个").arg(bad));
    }

    // --- V3b. 先试哪条地址：cdn.modrinth.com 排最前 -----------------------------
    {
        const QString cf1 = QStringLiteral(
            "https://mediafilez.forgecdn.net/files/8246/47/L_Enders_Cataclysm-3.31.jar");
        const QString cf2 = QStringLiteral(
            "https://edge.forgecdn.net/files/8246/47/L_Enders_Cataclysm-3.31.jar");
        const QString mr = QStringLiteral(
            "https://cdn.modrinth.com/data/46KJle7n/versions/C3H0azzB/L_Enders_Cataclysm-3.31.jar");

        const QStringList tri = ModpackImport::preferredUrlOrder({cf1, cf2, mr});
        check(tri.size() == 3 && tri.value(0) == mr,
              "V3b 三条地址里 modrinth 那条排到最前（另两条只换位次、不丢）",
              tri.join(QStringLiteral(" | ")));
        check(tri.value(1) == cf1 && tri.value(2) == cf2,
              "V3b 剩下那两条保持原来的相对次序（换位次不是打乱）",
              tri.join(QStringLiteral(" | ")));

        // 只有一条 modrinth 的（那个包里 42 个文件是这样）：原样返回。
        const QStringList onlyMr = ModpackImport::preferredUrlOrder({mr});
        check(onlyMr.size() == 1 && onlyMr.value(0) == mr, "V3b 单条地址原样返回");

        // 三条里一条 modrinth 都没有：**次序一个字符都不动** —— 这条最要紧，
        // 它是"认不出就别乱动"那条兜底（全是 forgecdn 的文件有 90 个）。
        const QStringList onlyCf = ModpackImport::preferredUrlOrder({cf1, cf2});
        check(onlyCf == QStringList({cf1, cf2}),
              "V3b 一条 modrinth 都没有时次序不动", onlyCf.join(QStringLiteral(" | ")));

        // host 必须按 QUrl 判，不能拿 QString::contains("modrinth") 那种办法认：
        // 一个**别家**的 host 里带这个字样（比如被抓包的镜像站）不该被当成首选。
        const QStringList trap = ModpackImport::preferredUrlOrder(
            {cf1, QStringLiteral("https://cdn.modrinth.com.evil.example/x.jar")});
        check(trap.value(0) == cf1,
              "V3b 认的是 host 本身，不是串里有没有 modrinth 那几个字",
              trap.join(QStringLiteral(" | ")));

        // 空表（清单里 downloads 是空的那些已经在解析时被剔掉了，这里只是别崩）。
        check(ModpackImport::preferredUrlOrder({}).isEmpty(), "V3b 空表返回空表");
    }

    // --- V4. 解析：一小段一小段地看它拒绝什么、接受什么 ------------------------

    {
        // 最小的一份合法清单：一个文件、一条依赖。
        const QByteArray minimal = R"json({
            "formatVersion": 1,
            "game": "minecraft",
            "name": "测试包",
            "versionId": "1.0",
            "dependencies": {"minecraft": "1.20.2", "fabric-loader": "0.16.14"},
            "files": [
                {"path": "mods/a.jar", "hashes": {"sha1": "aa"},
                 "downloads": ["https://example.invalid/a.jar"], "fileSize": 12}
            ]
        })json";
        const ModpackImport::Result okResult = ModpackImport::parseModrinthIndex(minimal);
        check(okResult.ok && okResult.pack.name == QStringLiteral("测试包") &&
                  okResult.pack.version == QStringLiteral("1.0") &&
                  okResult.pack.gameVersion == QStringLiteral("1.20.2") &&
                  okResult.pack.loaderKey == QStringLiteral("fabric-loader") &&
                  okResult.pack.loaderVersion == QStringLiteral("0.16.14") &&
                  okResult.pack.files.size() == 1 && okResult.pack.filesSkipped == 0 &&
                  okResult.pack.format == ModpackImport::Format::Modrinth,
              "V4 一份最小的合法清单：名字/版本/游戏版本/加载器/文件都对",
              okResult.ok ? QStringLiteral("%1 个文件").arg(okResult.pack.files.size())
                          : okResult.problem);
        if (okResult.ok && okResult.pack.files.size() == 1) {
            const ModpackImport::PackFile &file = okResult.pack.files.first();
            check(file.path == QStringLiteral("mods/a.jar") && file.sha1 == QStringLiteral("aa") &&
                      file.size == 12 && file.urls.size() == 1,
                  "V4 每条文件的路径 / sha1 / 字节数 / 地址都读出来了");
        }

        const auto problemOf = [](const QByteArray &json) {
            const ModpackImport::Result r = ModpackImport::parseModrinthIndex(json);
            return r.ok ? QString() : r.problem;
        };
        check(problemOf(QByteArray("这不是 json")).contains(QStringLiteral("读不懂")),
              "V4 不是 json：说「读不懂」");
        check(problemOf(QByteArray("{}")).contains(QStringLiteral("formatVersion")),
              "V4 缺 formatVersion/files：说它不是整合包清单");
        check(problemOf(R"json({"formatVersion":1,"files":[],"game":"factorio",
                                "dependencies":{"minecraft":"1.20.2"}})json")
                  .contains(QStringLiteral("Minecraft")),
              "V4 别的游戏的包：说「这不是 Minecraft 的整合包」");
        check(problemOf(R"json({"formatVersion":1,"files":[],"game":"minecraft",
                                "dependencies":{}})json")
                  .contains(QStringLiteral("dependencies.minecraft")),
              "V4 没写基于哪个原版：说清了缺的是 dependencies.minecraft");
        check(!problemOf(R"json({"formatVersion":1,"game":"minecraft",
                                 "dependencies":{"minecraft":"1.20.2"},"files":[]})json")
                   .isEmpty(),
              "V4 一条文件都没有：不装（不然就是「装完了，什么都没有」）");

        // 路径不安全 = **整包拒绝**，不是跳过那一条。它碰的是"会写到实例目录外面去"，
        // 放过其余的等于让用户在一个被做过手脚的包上继续走。
        const QByteArray traversal = R"json({
            "formatVersion": 1, "game": "minecraft",
            "dependencies": {"minecraft": "1.20.2"},
            "files": [
                {"path": "mods/a.jar", "downloads": ["https://example.invalid/a.jar"]},
                {"path": "../../外面的文件.txt", "downloads": ["https://example.invalid/b"]}
            ]
        })json";
        const ModpackImport::Result bad = ModpackImport::parseModrinthIndex(traversal);
        check(!bad.ok && bad.problem.contains(QStringLiteral("../../外面的文件.txt")) &&
                  bad.problem.contains(QStringLiteral("整个包")),
              "V4 有一条往外跳的路径：整包拒绝，而且把那一条原样指出来", bad.problem);

        // 跳过的两类：只给服务端用的、没有下载地址的。两个都要**记一笔**（用户看不到
        // 的那部分最容易变成"进了游戏才发现少个 mod"）。
        const QByteArray skipped = R"json({
            "formatVersion": 1, "game": "minecraft",
            "dependencies": {"minecraft": "1.20.2"},
            "files": [
                {"path": "mods/a.jar", "downloads": ["https://example.invalid/a.jar"]},
                {"path": "mods/server.jar", "downloads": ["https://example.invalid/s.jar"],
                 "env": {"client": "unsupported"}},
                {"path": "config/x.txt", "downloads": []},
                {"path": "mods/a.jar", "hashes": {"sha1": "后面那条"},
                 "downloads": ["https://example.invalid/a2.jar"]}
            ]
        })json";
        const ModpackImport::Result s = ModpackImport::parseModrinthIndex(skipped);
        check(s.ok && s.pack.files.size() == 1 && s.pack.filesSkipped == 2,
              "V4 只给服务端用的、没有下载地址的各记一笔跳过；同一个路径出现两次以最后一条为准",
              s.ok ? QStringLiteral("留下 %1、跳过 %2").arg(s.pack.files.size())
                            .arg(s.pack.filesSkipped)
                    : s.problem);
        if (s.ok && s.pack.files.size() == 1) {
            check(s.pack.files.first().sha1 == QStringLiteral("后面那条"),
                  "V4 重复路径：留下的是**最后**那一条");
        }

        // 纯原版的包：依赖表里只有 minecraft。能装，只是没有加载器要塞。
        const QByteArray vanillaPack = R"json({
            "formatVersion": 1, "game": "minecraft",
            "dependencies": {"minecraft": "1.18.2"},
            "files": [{"path": "mods/a.jar", "downloads": ["https://example.invalid/a.jar"]}]
        })json";
        const ModpackImport::Result v = ModpackImport::parseModrinthIndex(vanillaPack);
        check(v.ok && v.pack.loaderKey.isEmpty(),
              "V4 纯原版的包（依赖表里只有 minecraft）认了，加载器键是空的");

        // 名字缺了不能拒绝整个包（它只是"起实例名时的候选"）。
        const QByteArray noName = R"json({
            "formatVersion": 1, "game": "minecraft", "versionId": "9.9",
            "dependencies": {"minecraft": "1.20.2"},
            "files": [{"path": "mods/a.jar", "downloads": ["https://example.invalid/a.jar"]}]
        })json";
        const ModpackImport::Result nn = ModpackImport::parseModrinthIndex(noName);
        check(nn.ok && nn.pack.name == QStringLiteral("9.9") && nn.pack.version == QStringLiteral("9.9"),
              "V4 没有 name：退化成版本号，不因为缺名字拒绝整个包",
              nn.ok ? nn.pack.name : nn.problem);
    }

    // --- V5. ZipUtil：真造一个压缩包、真解一次 -------------------------------
    //
    // 两个坑都是实测踩出来的，都值得钉住：
    // ① PowerShell 的 Compress-Archive 写进去的条目名用的是**反斜杠**。按 `/` 比前缀
    // 的话一条都匹配不上 —— 表现是"解压成功、解出来 0 个文件"，不报任何错。
    // ② 两个前缀的**顺序**就是优先级（client-overrides 盖过 overrides），这是整合包
    // 格式的规矩，不能调换。
    {
        const QString work = sandbox + "/modpack-check";
        QDir(work).removeRecursively();
        const QString src = work + "/src";
        writeFileAt(src + "/overrides/config/a.txt", "来自 overrides");
        writeFileAt(src + "/client-overrides/config/a.txt", "来自 client-overrides");
        writeFileAt(src + "/overrides/config/b.txt", "只在 overrides 里");
        writeFileAt(src + "/overrides/mods/x.jar", "假的 mod");
        writeFileAt(src + "/modrinth.index.json", "{}");
        writeFileAt(src + "/README.txt", "不该被解出来");

        const QString archive = work + "/pack.zip";
        const QString script =
            QStringLiteral("Compress-Archive -Path %1 -DestinationPath %2 -Force")
                .arg(QStringLiteral("'") + QDir::toNativeSeparators(src) + QStringLiteral("\\*'"),
                     QStringLiteral("'") + QDir::toNativeSeparators(archive) + QStringLiteral("'"));
        QProcess ps;
        ps.start(QStringLiteral("powershell"),
                 {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                  QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                  QStringLiteral("-Command"), script});
        const bool compressed = ps.waitForFinished(180000) &&
                                ps.exitStatus() == QProcess::NormalExit && ps.exitCode() == 0;
        check(compressed, "V5 造出一个测试用的压缩包（Compress-Archive）",
              QString::fromLocal8Bit(ps.readAllStandardError()).trimmed().left(200));
        if (!compressed) {
            QDir(work).removeRecursively();
            return;
        }

        // 读单个条目：有 / 没有 / 不是 zip 三种结果必须分得清 —— 界面给的是三句不同的话。
        QByteArray bytes;
        QString error;
        check(ZipUtil::readEntry(archive, ModpackImport::modrinthIndexName(), &bytes, &error) ==
                      ZipUtil::EntryStatus::Ok &&
                  bytes == QByteArray("{}"),
              "V5 readEntry 读到了那一份清单，字节原样", QString::fromUtf8(bytes).left(40));
        check(ZipUtil::readEntry(archive, QStringLiteral("nope.json"), &bytes, &error) ==
                  ZipUtil::EntryStatus::NotFound,
              "V5 压缩包里没有那个条目：报 NotFound（= 「这不是整合包」，不是「文件坏了」）");
        // 上面那次 NotFound 之后 bytes 要被清掉，不能留着上一次的内容（留着的话调用方
        // 拿它去解析，会解出一份**上一个包**的清单）。
        check(bytes.isEmpty(), "V5 没读到东西时 out 被清空了（不然会拿上一次的字节去解析）");
        check(ZipUtil::readEntry(archive, QStringLiteral("MODRINTH.INDEX.JSON"), &bytes,
                                 &error) == ZipUtil::EntryStatus::Ok,
              "V5 条目名大小写不一样也认得（zip 的条目名区分大小写，但整合包工具不一定讲究）");
        const QString notAZip = work + "/假.zip";
        writeFileAt(notAZip, "这不是压缩包");
        check(ZipUtil::readEntry(notAZip, ModpackImport::modrinthIndexName(), &bytes, &error) ==
                  ZipUtil::EntryStatus::NotAnArchive,
              "V5 不是 zip 的文件：报 NotAnArchive（= 「这个文件打不开」）");

        // 按前缀解压：剥掉前缀、"杂项一个都不碰"、后一个前缀盖前一个。
        const QString out = work + "/out";
        int extracted = 0;
        const QStringList roots = {QStringLiteral("overrides/"), QStringLiteral("client-overrides/")};
        const bool extractedOk =
            ZipUtil::extractSubtrees({archive}, out, roots, &extracted, &error);
        check(extractedOk && extracted == 4,
              "V5 extractSubtrees 解出 4 个条目（两个 a.txt、一个 b.txt、一个 x.jar）",
              extractedOk ? QStringLiteral("解出 %1 个").arg(extracted) : error);

        const auto readText = [](const QString &path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        check(!QFile::exists(out + "/overrides/config/a.txt") &&
                  !QFile::exists(out + "/client-overrides/config/a.txt") &&
                  QFile::exists(out + "/config/a.txt"),
              "V5 前缀被剥掉了：解出来的是 <目标>/config/a.txt，不是 <目标>/<前缀>/config/a.txt");
        check(readText(out + "/config/a.txt") == QStringLiteral("来自 client-overrides"),
              "V5 两个前缀撞同一个文件时，client-overrides 盖过 overrides");
        check(readText(out + "/config/b.txt") == QStringLiteral("只在 overrides 里") &&
                  readText(out + "/mods/x.jar") == QStringLiteral("假的 mod"),
              "V5 overrides 底下别的条目也解出来了");
        check(!QFile::exists(out + "/modrinth.index.json") && !QFile::exists(out + "/README.txt"),
              "V5 不属于任何前缀的条目一个都没碰（清单文件和 README 没进实例目录）");

        QDir(work).removeRecursively();
    }

    // --- V5z. 拖进来的那一个文件到底是什么（ 加） --------------------
    //
    // 要求：「打开某个实例设置的时候如果拖入.jar/.zip之类的文件，检测是整合包还是资源包
    // 等」。判据层是 `manage/DroppedResource`（纯函数：输入一条路径，输出一个 Kind）——
    // 它只做判断，问什么、往哪儿放都在 MainWindow 那边，所以这一节能在这儿钉死。
    //
    // 八个包**一趟 PowerShell 造完**（一个 ZipArchive::Create 里连着建八个）：.NET 的
    // `CreateEntry()` 按原字面量写条目名，`Compress-Archive` 会规整掉反斜杠 —— 要测
    // "反斜杠写的包"就只能走 .NET。见 V5c 里那条 NOTE，同一个理由。
    //
    // 优先级那两条是这一节的**主角**：一个包里同时有 `pack.mcmeta`+`assets/` 和
    // `shaders/`（真实世界里的资源包太常见了），判成资源包还是光影，取决于两个判据的
    // 先后 —— 排错了不会崩、不会报错，就是往 shaderpacks/ 里塞一个资源包。
    {
        const QString work = sandbox + "/dropped";
        QDir(work).removeRecursively();
        QDir().mkpath(work);

        const auto buildArchive = [&](const QString &archive,
                                      const QList<QPair<QString, QString>> &entries) {
            // 条目名/正文**只能**自己包单引号：`psQuoted()` 会顺手 toNativeSeparators，
            // 那会把条目名里的 `/` 全折成 `\` —— 造出来的就不再是我们要测的那个包了。
            const auto psq = [](const QString &text) {
                return QStringLiteral("'") + text + QStringLiteral("'");
            };
            QStringList lines;
            lines << QStringLiteral("$ErrorActionPreference='Stop'")
                  << QStringLiteral("Add-Type -AssemblyName System.IO.Compression.FileSystem")
                  << QStringLiteral("$z=[System.IO.Compression.ZipFile]::Open(%1,'Create')")
                         .arg(psQuoted(archive))
                  << QStringLiteral("try{");
            for (const QPair<QString, QString> &entry : entries) {
                if (entry.second.isEmpty()) {
                    // 空目录条目：名字以 `/` 结尾（zip 里合法，而且真有工具这么写）。
                    lines << QStringLiteral("  $z.CreateEntry(%1)").arg(psq(entry.first));
                } else {
                    lines << QStringLiteral("  $e=$z.CreateEntry(%1)").arg(psq(entry.first))
                          << QStringLiteral("  $s=$e.Open()")
                          << QStringLiteral("  $b=[System.Text.Encoding]::UTF8.GetBytes(%1)")
                                 .arg(psq(entry.second))
                          << QStringLiteral("  $s.Write($b,0,$b.Length)")
                          << QStringLiteral("  $s.Close()");
                }
            }
            lines << QStringLiteral("} finally { $z.Dispose() }");
            QProcess ps;
            ps.start(QStringLiteral("powershell"),
                     {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                      QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                      QStringLiteral("-Command"), lines.join(QLatin1Char('\n'))});
            return ps.waitForFinished(180000) && ps.exitStatus() == QProcess::NormalExit &&
                   ps.exitCode() == 0;
        };

        const QString respack = work + "/respack.zip";
        check(buildArchive(respack,
                           {{QStringLiteral("pack.mcmeta"), QStringLiteral("{}")},
                            {QStringLiteral("assets/minecraft/lang/zh_cn.json"), QStringLiteral("{}")}}),
              "V5z 造出一个资源包（pack.mcmeta + assets/）");
        const QString datapack = work + "/datapack.zip";
        check(buildArchive(datapack,
                           {{QStringLiteral("pack.mcmeta"), QStringLiteral("{}")},
                            {QStringLiteral("data/minecraft/tags/function/a.json"), QStringLiteral("{}")}}),
              "V5z 造出一个数据包（pack.mcmeta + data/）");
        const QString shader = work + "/shader.zip";
        check(buildArchive(shader,
                           {{QStringLiteral("shaders/final.fsh"), QStringLiteral("void main(){}")},
                            {QStringLiteral("shaders/gbuffers_terrain.vsh"), QStringLiteral("void main(){}")}}),
              "V5z 造出一个光影包（shaders/）");
        // 同时有 assets/ 和 shaders/：资源包排在前面 ⇒ 必须是资源包。
        const QString both = work + "/respack-with-shader.zip";
        check(buildArchive(both,
                           {{QStringLiteral("pack.mcmeta"), QStringLiteral("{}")},
                            {QStringLiteral("assets/minecraft/lang/zh_cn.json"), QStringLiteral("{}")},
                            {QStringLiteral("shaders/final.fsh"), QStringLiteral("void main(){}")}}),
              "V5z 造出一个「资源包 + 里面还带 shaders/」的包（真实世界里的常客）");
        const QString nested = work + "/nested.zip";
        check(buildArchive(nested,
                           {{QStringLiteral("MyPack/pack.mcmeta"), QStringLiteral("{}")},
                            {QStringLiteral("MyPack/assets/minecraft/lang/zh_cn.json"),
                              QStringLiteral("{}")}}),
              "V5z 造出一个「整个目录一起压」的包（条目都套在 MyPack/ 底下）");
        const QString backslash = work + "/backslash.zip";
        check(buildArchive(backslash, {{QStringLiteral("assets\\minecraft\\x.txt"),
                                        QStringLiteral("反斜杠")}}),
              "V5z 造出一个条目名用反斜杠写的包（国内那些「一键压缩」常这么干）");
        const QString mrpack = work + "/mrpack.zip";
        check(buildArchive(mrpack,
                           {{QStringLiteral("modrinth.index.json"), QStringLiteral("{}")},
                            {QStringLiteral("overrides/mods/a.jar"), QStringLiteral("x")}}),
              "V5z 造出一个 Modrinth 整合包");
        const QString cfpack = work + "/cfpack.zip";
        check(buildArchive(cfpack,
                           {{QStringLiteral("manifest.json"), QStringLiteral("{}")},
                            {QStringLiteral("overrides/config/a.txt"), QStringLiteral("x")}}),
              "V5z 造出一个 CurseForge 整合包");
        const QString junk = work + "/junk.zip";
        check(buildArchive(junk, {{QStringLiteral("README.txt"), QStringLiteral("说明")}}),
              "V5z 造出一个「里面什么都不是」的压缩包");

        const auto kindOf = [](const QString &path) {
            return DroppedResource::classify(path);
        };
        const auto isKind = [&](const QString &path, ResourceCatalog::Kind kind) {
            const DroppedResource::Result r = kindOf(path);
            return r.known && r.kind == kind;
        };

        check(isKind(respack, ResourceCatalog::Kind::ResourcePack),
              "V5z pack.mcmeta + assets/ ⇒ 资源包", kindOf(respack).detail);
        check(isKind(datapack, ResourceCatalog::Kind::DataPack),
              "V5z pack.mcmeta + data/ ⇒ 数据包", kindOf(datapack).detail);
        check(isKind(shader, ResourceCatalog::Kind::Shader),
              "V5z 只有 shaders/ ⇒ 光影", kindOf(shader).detail);
        // 优先级：这条要是红了，用户拖一个带 shaders/ 的资源包进来会进 shaderpacks/。
        check(isKind(both, ResourceCatalog::Kind::ResourcePack),
              "★★ V5z 同时有 assets/ 和 shaders/ ⇒ **资源包**（那一串判据的先后就是优先级，"
              "排错了就是往光影目录里塞资源包）",
              kindOf(both).detail);
        check(isKind(nested, ResourceCatalog::Kind::ResourcePack),
              "V5z 套在一层目录里的包也认得出来（GitHub 的 zipball / 右键压缩都是这个形状）",
              kindOf(nested).detail);
        check(isKind(mrpack, ResourceCatalog::Kind::Modpack),
              "V5z 根上有 modrinth.index.json ⇒ 整合包", kindOf(mrpack).detail);
        check(isKind(cfpack, ResourceCatalog::Kind::Modpack),
              "V5z 根上有 manifest.json ⇒ 整合包（CurseForge 那个格式）",
              kindOf(cfpack).detail);

        // `.jar` **一个字节都不读盘**：它是不是压缩包、里面有什么，都不影响判断
        // （模组就是模组）。这一条同时钉住"后缀大小写不敏感"。
        const QString jar = work + "/某某模组.JAR";
        writeFileAt(jar, "不是真的 jar，只是个名字");
        check(isKind(jar, ResourceCatalog::Kind::Mod),
              "V5z .jar 就是模组（后缀大小写不敏感、**不读盘**：这个文件根本不是压缩包）",
              kindOf(jar).detail);

        // 认不出来的那几档：反斜杠那个包、什么都不是的包、根本不是压缩包的文件、目录、不存在
        // 的路径。它们必须一律 `known == false` —— 界面上那一句"这几个认不出来"就是它。
        check(!kindOf(backslash).known,
              "V5z 条目名用反斜杠写的包：连 assets/ 都拼不出来 ⇒ 认不出来（如实说，不猜）",
              kindOf(backslash).detail);
        check(!kindOf(junk).known, "V5z 里面什么都没有的压缩包 ⇒ 认不出来",
              kindOf(junk).detail);
        const QString notZip = work + "/其实是文本.zip";
        writeFileAt(notZip, "这不是压缩包");
        check(!kindOf(notZip).known,
              "V5z 后缀是 .zip 但不是压缩包 ⇒ 认不出来（不是「整合包」，也不是崩）",
              kindOf(notZip).detail);
        check(!kindOf(work).known, "V5z 拖进来的是个目录 ⇒ 认不出来", kindOf(work).detail);
        check(!kindOf(work + "/根本没有这个文件.zip").known,
              "V5z 盘上没有这个文件 ⇒ 认不出来", kindOf(work + "/根本没有这个文件.zip").detail);
        check(!kindOf(QString()).known, "V5z 空串 ⇒ 认不出来（不崩）");

        // `detail` 那一格是界面拿去写在括号里的"为什么"，认不出来时**必须有一句人话**
        // （空串的话那一行会变成「xxx（)」）。
        check(!kindOf(junk).detail.isEmpty() && !kindOf(notZip).detail.isEmpty(),
              "V5z 认不出来的时候带一句理由（界面上那个括号里就是它）",
              kindOf(notZip).detail);

        // 空目录条目那一档：`Compress-Archive` 会写进 `assets/` 这样以斜杠结尾的记录。
        // 只按 `assets/…` 认前缀的话，一个只有空目录的资源包会被判成"什么都没有"。
        const QString dirOnly = work + "/dir-only.zip";
        check(buildArchive(dirOnly,
                           {{QStringLiteral("pack.mcmeta"), QStringLiteral("{}")},
                            {QStringLiteral("assets/"), QString()}}),
              "V5z 造出一个「assets 只是一条空目录记录」的资源包");
        check(isKind(dirOnly, ResourceCatalog::Kind::ResourcePack),
              "V5z 目录条目本身（`assets/` 这一条）也算数 ⇒ 还是资源包",
              kindOf(dirOnly).detail);

        // `listEntries()` 就是上面那条路读条目表用的那一发：条目名**原样**拿回来
        // （不替我们归一化、不排序，谁用谁自己折），不是压缩包时和 readEntry 分得一样清。
        QStringList names;
        QString error;
        check(ZipUtil::listEntries(respack, &names, &error) == ZipUtil::EntryStatus::Ok &&
                  names.size() == 2 && names.contains(QStringLiteral("pack.mcmeta")) &&
                  names.contains(QStringLiteral("assets/minecraft/lang/zh_cn.json")),
              "V5z listEntries 把两个条目名原样拿回来了", names.join(QLatin1Char(' ')));
        check(ZipUtil::listEntries(backslash, &names, &error) == ZipUtil::EntryStatus::Ok &&
                  names.contains(QStringLiteral("assets\\minecraft\\x.txt")),
              "V5z listEntries **不替我们归一化**反斜杠（折成 `/` 是调用方的事）",
              names.join(QLatin1Char(' ')));
        check(ZipUtil::listEntries(notZip, &names, &error) == ZipUtil::EntryStatus::NotAnArchive,
              "V5z listEntries 对不是压缩包的文件报 NotAnArchive");
        // 盘上根本没有这一份：**和 readEntry 给同一个答案**（打不开 = NotAnArchive，不是
        // NotFound）。`NotFound` 在这个枚举里的意思是"**压缩包里**没有这一条"，一条都还没
        // 读到的时候谈不上"没有哪一条"；两处答得不一样的话，界面同一个故障会说出两句
        // 不同的话。上面 V5z 那一条"盘上没有这个文件 ⇒ 认不出来"是**判据层**自己先挡掉的
        // （它的 detail 是「找不到这份文件」）—— listEntries 根本轮不到。
        QByteArray oneEntry;
        check(ZipUtil::listEntries(work + "/没有这个.zip", &names, &error) ==
                      ZipUtil::EntryStatus::NotAnArchive &&
                  ZipUtil::readEntry(work + "/没有这个.zip", QStringLiteral("a.txt"), &oneEntry,
                                     &error) == ZipUtil::EntryStatus::NotAnArchive,
              "V5z listEntries 和不存在的文件对同一份读不了的东西给同一个答案"
              "（NotAnArchive，和 readEntry 一致）",
              error);
        // 空表也要能读（一个成员都没有的 zip 是合法的）。
        const QString emptyZip = work + "/empty.zip";
        check(buildArchive(emptyZip, {}), "V5z 造得出一个空压缩包");
        names = QStringList{QStringLiteral("上一次留下的")};
        check(ZipUtil::listEntries(emptyZip, &names, &error) == ZipUtil::EntryStatus::Ok &&
                  names.isEmpty(),
              "V5z 空压缩包 ⇒ 空表（而且把 out 里上一次留下的东西清掉了）");

        QDir(work).removeRecursively();
    }

    // --- V5c. extractArchives：那几条恶意条目名一个都不许解出去（zip slip） ------
    //
    // 这一节盯的是 LauncherManager::prepareNatives() 那条路（旧格式的 natives 是从
    // 官方 jar 里解出来的，用的是 ZipUtil::extractArchives）。原来的守卫只判条目名里
    // 有没有 `..`，漏了"条目名本身就是绝对路径"那一类 —— .NET 的 Path.Combine 会把
    // 目标目录整个丢掉，文件落到外面去。
    {
        const QString work = sandbox + "/zip-slip";
        QDir(work).removeRecursively();
        QDir().mkpath(work);  // ZipFile::Open('Create') 不会自己建父目录
        const QString archive = work + "/evil.zip";
        // 带盘符的绝对路径那条：Path.Combine 遇到它会丢掉 $out，于是落点就是它自己。
        // 落点**故意挑在沙盒里** —— 防线漏了也只是在沙盒里多出一个文件，碰不到系统目录。
        const QString escapeTarget =
            QDir::fromNativeSeparators(QDir(work).absolutePath()) + QStringLiteral("/escaped.txt");
        // `/abs.txt` 这类"只有斜杠开头"的条目，落点是**当前盘的根目录**（.NET 拿它当
        // 相对当前盘的绝对路径）。记一下它本来在不在：这条只该在"本来没有、跑完出现了"
        // 时红，不能因为盘根目录本来就有个同名文件而误报。
        const QString driveRootEscape = QDir::rootPath() + QStringLiteral("abs.txt");
        const bool driveRootEscapeExisted = QFile::exists(driveRootEscape);

        const QString script =
            QStringLiteral("$ErrorActionPreference='Stop'\n"
                           "Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
                           "$zip=[System.IO.Compression.ZipFile]::Open(%1,'Create')\n"
                           "try{\n"
                           "  $items=@(\n"
                           "    @{n='ok.txt';c='正常条目'},\n"
                           "    @{n='sub\\ok2.txt';c='反斜杠条目'},\n"
                           "    @{n='../evil.txt';c='翻上去'},\n"
                           "    @{n='/abs.txt';c='斜杠开头的绝对路径'},\n"
                           "    @{n=%2;c='带盘符的绝对路径'},\n"
                           "    @{n='META-INF/MANIFEST.MF';c='签名文件'}\n"
                           "  )\n"
                           "  foreach($it in $items){\n"
                           "    $e=$zip.CreateEntry($it.n)\n"
                           "    $w=New-Object System.IO.StreamWriter($e.Open())\n"
                           "    $w.Write($it.c)\n"
                           "    $w.Dispose()\n"
                           "  }\n"
                           "} finally { $zip.Dispose() }\n")
                .arg(psQuoted(archive),
                     QStringLiteral("'") + escapeTarget + QStringLiteral("'"));
        QProcess ps;
        ps.start(QStringLiteral("powershell"),
                 {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                  QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                  QStringLiteral("-Command"), script});
        const bool madeZip = ps.waitForFinished(180000) &&
                             ps.exitStatus() == QProcess::NormalExit && ps.exitCode() == 0;
        check(madeZip, "V5c 造出一个带恶意条目名的压缩包（ZipArchive::Create 才造得出）",
              QString::fromLocal8Bit(ps.readAllStandardError()).trimmed().left(200));
        if (!madeZip) {
            QDir(work).removeRecursively();
            return;
        }

        const QString out = work + "/out";
        QString error;
        const bool ok = ZipUtil::extractArchives({archive}, out,
                                                  {QStringLiteral("META-INF")}, &error);
        check(ok, "V5c 解压整体算成功（恶意条目是被跳过的，不是整包失败）", error);

        const auto readTextAt = [](const QString &path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        // 正路照样得通：正常的条目要解出来，反斜杠的条目名归一之后也落在目标目录里
        // （Minecraft 那些 jar 是 `/`，但别的工具打的包不一定讲究）。
        check(readTextAt(out + "/ok.txt") == QStringLiteral("正常条目") &&
                  readTextAt(out + "/sub/ok2.txt") == QStringLiteral("反斜杠条目"),
              "V5c 正常条目照解不误，反斜杠条目名归一成 / 之后落在目标目录里");
        // 四条恶意条目：一个都不许出现在目标目录外面。
        check(!QFile::exists(work + "/evil.txt"),
              "V5c `../evil.txt` 没有翻到目标目录上面去");
        check(driveRootEscapeExisted || !QFile::exists(driveRootEscape),
              "V5c `/abs.txt` 没有落到当前盘的根目录下（Combine 会丢掉目标目录）",
              driveRootEscape);
        check(!QFile::exists(escapeTarget),
              "V5c 带盘符的绝对路径条目没有写到外面去", escapeTarget);
        check(!QFile::exists(out + "/META-INF/MANIFEST.MF"),
              "V5c excludePrefixes 那条路照样挡着（META-INF 没进目标目录）");

        QDir(work).removeRecursively();
    }

    // --- V5d. 导出：CurseForge 指纹（murmur2 + 先剔空白字节） ------------------
    //
    // 这一节的期望值**不是**从 C++ 这边抄出来的：它们是一份 Python 独立实现算的
    // （`C:\Users\User\.claude\jobs\b62c43ac\tmp\fp3.py`），而那份实现拿一个真 jar
    // 跟 CurseForge 自己记录里的 `fileFingerprint` 对上过 —— 就是这一节最后那几条
    // （`meta/jei-fingerprint.jar`，sha1 也在断言里钉着，防的是夹具哪天被换掉）。
    // 所以这几条真能发现"哪一步抄错了"，而不是把 C++ 的输出再读一遍。
    {
        struct FingerprintCase {
            const char *what;
            QByteArray bytes;
            quint32 expected;
        };
        // 挑这些长度是为了把 murmur2 的**每一段**都走到：0、正好 4 的倍数、余 1、余 2、余 3，
        // 外加"剔完空白之后长度变了"的两种。空内容与纯空白那两个的期望值是**同一个数** ——
        // 这就是"剔空白"这件事最直白的一条：文件里有没有空白跟指纹无关。
        const QList<FingerprintCase> cases = {
            {"空内容", QByteArray(), 1540447798u},
            {"只有空白字节（\\t\\n\\r 空格）", QByteArray("\t\n\r \t"), 1540447798u},
            {"abcd（正好 4 字节，不进末梢）", QByteArray("abcd"), 3376380438u},
            {"abcde（余 1）", QByteArray("abcde"), 3469237630u},
            {"abcdef（余 2）", QByteArray("abcdef"), 455443312u},
            {"abcdefg（余 3）", QByteArray("abcdefg"), 184182053u},
            {"a b\\tc\\r\\nd  \\n（剔完就是 abcd）", QByteArray("a b\tc\r\nd  \n"), 3376380438u},
            {" abcde （撇号外面有空白，还是 abcde）", QByteArray(" abcde "), 3469237630u},
        };
        for (const FingerprintCase &one : cases) {
            const quint32 got = ModpackExport::curseForgeFingerprintOfBytes(one.bytes);
            check(got == one.expected,
                  QStringLiteral("V5d 指纹：%1").arg(QString::fromUtf8(one.what)),
                  QStringLiteral("期望 %1，实得 %2").arg(one.expected).arg(got));
        }
        // 0x00~0xFF 全都在，其中 0x09/0x0A/0x0D/0x20 会被剔掉，长度仍是 256（4 的倍数）。
        // 单独来一条是因为它同时压着"哪些字节算空白"：多剔一个（比如把 0x0B 也算上）
        // 或者少剔一个，这一个数就变。
        QByteArray allBytes;
        for (int i = 0; i < 256; ++i) {
            allBytes.append(static_cast<char>(i));
        }
        check(ModpackExport::curseForgeFingerprintOfBytes(allBytes) == 2094645347u,
              "V5d 指纹：0x00~0xFF 全字节表（剔掉的只有 09/0A/0D/20 这四个）",
              QString::number(ModpackExport::curseForgeFingerprintOfBytes(allBytes)));

        // 走文件那一趟：内容一样就得是同一个数，而且**跨读缓冲**的那一段（读侧一次 64 KiB，
        // 这个文件 287914 字节）不能把末梢状态丢掉 —— 掉一次，长文件的指纹就全错，
        // 而短文件那条路照样绿。
        QByteArray big;
        for (int i = 0; i < 200000; ++i) {
            big.append(static_cast<char>((i * 37 + 11) & 0xFF));
            if (i % 7 == 0) {
                big.append(QByteArrayLiteral(" \t"));
            }
            if (i % 13 == 0) {
                big.append(QByteArrayLiteral("\r\n"));
            }
        }
        check(big.size() == 287914,
              "V5d 大文件的字节数对得上（造法跟算期望值那份脚本一字不差）",
              QString::number(big.size()));
        check(ModpackExport::curseForgeFingerprintOfBytes(big) == 545617932u,
              "V5d 指纹：287914 字节、其中 91039 个是空白（跨了读缓冲）",
              QString::number(ModpackExport::curseForgeFingerprintOfBytes(big)));

        const QString fpWork = sandbox + "/export-fingerprint";
        QDir(fpWork).removeRecursively();
        QDir().mkpath(fpWork);
        const QString bigPath = fpWork + "/big.bin";
        writeFileAt(bigPath, big);
        quint32 fromFile = 0;
        QString fpError;
        const bool fpOk = ModpackExport::curseForgeFingerprint(bigPath, &fromFile, &fpError);
        check(fpOk && fromFile == 545617932u,
              "V5d 同一个文件走两条路（读文件 / 直接喂字节）得到的指纹一模一样",
              fpOk ? QString::number(fromFile) : fpError);
        // 读不到的文件必须是"失败 + 一句原因"，不能悄悄给出一个 0 或者 1（那会变成一个
        // 假指纹，拿去问接口当然查不到，于是整个文件白进 overrides —— 看着像"没认出来"）。
        const QString missingPath = fpWork + "/没有这个文件.bin";
        quint32 missingOut = 12345;
        QString missingError;
        const bool missingOk =
            ModpackExport::curseForgeFingerprint(missingPath, &missingOut, &missingError);
        check(!missingOk && !missingError.trimmed().isEmpty() && missingOut == 12345,
              "V5d 文件读不了时返回 false、给一句原因，且不碰出参", missingError);

        // 真文件那一条：这个 jar 是 CurseForge 上 JEI 的 `31.7.0.37` 那一份，
        // **`3309664856` 是 CurseForge 自己记录里的 `fileFingerprint`**（不是我们算的）。
        // 夹具的 sha1 一起钉着：本地文件的指纹只由字节决定，夹具被换掉这条就该红。
        const QString jarPath =
            QStringLiteral(REPO_ROOT_PATH) + "/tests/meta/jei-fingerprint.jar";
        QFile jar(jarPath);
        const bool haveJar = jar.open(QIODevice::ReadOnly);
        check(haveJar, "V5d 读到了真 jar 夹具（meta/jei-fingerprint.jar）", jarPath);
        if (haveJar) {
            const QByteArray jarBytes = jar.readAll();
            jar.close();
            check(QCryptographicHash::hash(jarBytes, QCryptographicHash::Sha1).toHex() ==
                      QByteArrayLiteral("c19b8bfb43a753ad8c27612d516ce61c58b0d2e6"),
                  "V5d 夹具就是那一份文件（sha1 与 CurseForge 记录里的一致）");
            quint32 jarFp = 0;
            QString jarError;
            const bool jarOk = ModpackExport::curseForgeFingerprint(jarPath, &jarFp, &jarError);
            check(jarOk && jarFp == 3309664856u,
                  "V5d 真 jar 的指纹与 CurseForge 记录里的 fileFingerprint 一字不差",
                  jarOk ? QString::number(jarFp) : jarError);
        }
        QDir(fpWork).removeRecursively();
    }

    // --- V5e. 导出：两份清单写出来能被我们自己的解析器读回来（往返） ------------
    //
    // 导出和导入是同一件事的两个方向，所以这里不另立一套"期望值"：**让 ModpackImport
    // 去读**我们写出来的东西。它能读回来 = 格式是对的（这一层完全离线能验，而它正好
    // 盖住导出最容易写错的那一层）。
    {
        ModpackExport::Spec spec;
        spec.format = ModpackExport::Format::Modrinth;
        spec.name = QStringLiteral("我的整合包");
        spec.version = QStringLiteral("1.2.3");
        spec.summary = QStringLiteral("一句话简介");
        spec.gameVersion = QStringLiteral("1.21.1");
        spec.loaderKey = ModpackImport::fabricLoaderKey();
        spec.loaderVersion = QStringLiteral("0.16.14");

        ModpackExport::MatchedFile mod;
        mod.path = QStringLiteral("mods/sodium.jar");
        mod.sha1 = QStringLiteral("1111111111111111111111111111111111111111");
        mod.sha512 = QString(128, QLatin1Char('a'));
        mod.urls = {QStringLiteral("https://cdn.modrinth.com/data/AABBCC/versions/xx/sodium.jar")};
        mod.size = 123456;
        mod.envClient = QStringLiteral("required");
        mod.envServer = QStringLiteral("required");
        spec.files.append(mod);

        // 用户自己改过名字的（`.disabled`）：**路径原样写**，状态就不丢（导入回来还是禁用的）。
        ModpackExport::MatchedFile disabled = mod;
        disabled.path = QStringLiteral("mods/别的名字.jar.disabled");
        disabled.sha1 = QStringLiteral("2222222222222222222222222222222222222222");
        spec.files.append(disabled);

        const QByteArray index = ModpackExport::buildModrinthIndex(spec);
        const ModpackImport::Result parsed = ModpackImport::parseModrinthIndex(index);
        check(parsed.ok, "V5e 我们写出来的 modrinth.index.json 自己的解析器读得回来",
              parsed.problem);
        if (parsed.ok) {
            const ModpackImport::Pack &pack = parsed.pack;
            check(pack.format == ModpackImport::Format::Modrinth && pack.name == spec.name &&
                      pack.version == spec.version && pack.gameVersion == spec.gameVersion,
                  "V5e 往返：名字 / 版本号 / 基于哪个原版都对得上",
                  pack.name + QLatin1Char('|') + pack.version + QLatin1Char('|') + pack.gameVersion);
            check(pack.loaderKey == ModpackExport::modrinthDependencyKey(spec.loaderKey) &&
                      pack.loaderVersion == spec.loaderVersion,
                  "V5e 往返：加载器那一行（键与纯版本号）",
                  pack.loaderKey + QLatin1Char('|') + pack.loaderVersion);
            check(pack.fileCount() == 2 && pack.filesSkipped == 0,
                  "V5e 往返：两条文件都在、一条都没被跳过",
                  QStringLiteral("%1 / 跳过 %2").arg(pack.fileCount()).arg(pack.filesSkipped));
            if (pack.fileCount() == 2) {
                const ModpackImport::PackFile &back = pack.files.at(0);
                check(back.path == mod.path && back.urls == mod.urls && back.sha1 == mod.sha1 &&
                          back.size == mod.size,
                      "V5e 往返：路径 / 地址 / sha1 / 字节数都原样读回来",
                      back.path);
                check(pack.files.at(1).path == disabled.path,
                      "V5e 往返：`.disabled` 的文件路径原样（名字没被改写）",
                      pack.files.at(1).path);
            }
        }

        // 纯原版的包：依赖表里只有 minecraft 那一行，不该多出加载器来。
        ModpackExport::Spec vanilla = spec;
        vanilla.loaderKey.clear();
        vanilla.loaderVersion.clear();
        vanilla.name = QStringLiteral("原版包");
        const ModpackImport::Result vanillaParsed =
            ModpackImport::parseModrinthIndex(ModpackExport::buildModrinthIndex(vanilla));
        check(vanillaParsed.ok && vanillaParsed.pack.loaderKey.isEmpty() &&
                  vanillaParsed.pack.gameVersion == spec.gameVersion,
              "V5e 纯原版的包往返回来加载器那一行是空的（不会被当成 Quilt 之类认不出的键）",
              vanillaParsed.problem);

        // 一条都没认出来的包：`files` 那一格照样写出来（空的），但**我们自己装不回去** ——
        // 解析器把"一条文件都没有"判成整包无效。这条钉的是那个已知缺口，免得以后有人
        // 以为"空 files 也没事"：真到那一步（一个都认不出来）得由调用方拦住，
        // 在界面上照实说，而不是导出一个自己都装不回来的包。
        ModpackExport::Spec nothing = spec;
        nothing.files.clear();
        const QByteArray nothingIndex = ModpackExport::buildModrinthIndex(nothing);
        const QJsonObject nothingRoot = QJsonDocument::fromJson(nothingIndex).object();
        check(nothingRoot.contains(QStringLiteral("files")) &&
                  nothingRoot.value(QStringLiteral("files")).toArray().isEmpty() &&
                  !ModpackImport::parseModrinthIndex(nothingIndex).ok,
              "V5e 一条都没认出来时清单里 files 是空数组，而我们自己的解析器拒收这种包"
              "（调用方必须在导出前拦下这一档）");

        // 缺哈希的条目**写不进去**（调用方该把它分到 overrides 去，不是丢）。理由见
        // writableAsIndexFile：清单里的 sha1 是安装时唯一的完整性判据。
        ModpackExport::MatchedFile noHash = mod;
        noHash.path = QStringLiteral("mods/没哈希.jar");
        noHash.sha1.clear();
        check(!ModpackExport::writableAsIndexFile(noHash) &&
                  ModpackExport::writableAsIndexFile(mod),
              "V5e writableAsIndexFile：缺 sha1 的不能写进 files[]，两条都齐的能");
        ModpackExport::Spec withBad = spec;
        withBad.files.append(noHash);
        const ModpackImport::Result withBadParsed =
            ModpackImport::parseModrinthIndex(ModpackExport::buildModrinthIndex(withBad));
        check(withBadParsed.ok && withBadParsed.pack.fileCount() == 2,
              "V5e 缺哈希的那条被写清单这一步拦下（不是写出一条坏的，也不是悄悄少一条）",
              QString::number(withBadParsed.pack.fileCount()));
        // 没地址的同理：导入方拿到它也不知道从哪下。
        ModpackExport::MatchedFile noUrl = mod;
        noUrl.urls.clear();
        check(!ModpackExport::writableAsIndexFile(noUrl),
              "V5e writableAsIndexFile：一条地址都没有的也不能写进 files[]");

        // CurseForge 那份：`files[]` 里只有两个号，加载器是一个字符串。
        ModpackExport::Spec forge = spec;
        forge.format = ModpackExport::Format::CurseForge;
        forge.files.clear();
        forge.loaderKey = ModpackImport::forgeLoaderKey();
        forge.loaderVersion = QStringLiteral("47.2.0");
        forge.author = QStringLiteral("某个人");
        forge.refs.append({238222, 8971390});
        forge.refs.append({0, 0});  // 两个号缺一个的那条：写不出去，也不该写出去
        const QByteArray manifest = ModpackExport::buildCurseForgeManifest(forge);
        const ModpackImport::Result cfParsed = ModpackImport::parseCurseForgeManifest(manifest);
        check(cfParsed.ok && cfParsed.pack.format == ModpackImport::Format::CurseForge,
              "V5e 我们写出来的 manifest.json 自己的解析器读得回来", cfParsed.problem);
        if (cfParsed.ok) {
            check(cfParsed.pack.pendingRefs.size() == 1 &&
                      cfParsed.pack.pendingRefs.at(0).projectId == 238222 &&
                      cfParsed.pack.pendingRefs.at(0).fileId == 8971390,
                  "V5e 往返：两个号原样回来、只有一条（号不全的那条没写出去）",
                  QString::number(cfParsed.pack.pendingRefs.size()));
            check(cfParsed.pack.loaderKey == ModpackImport::forgeLoaderKey() &&
                      cfParsed.pack.loaderVersion == forge.loaderVersion &&
                      cfParsed.pack.gameVersion == forge.gameVersion,
                  "V5e 往返：CurseForge 的加载器字符串拆回来的键与版本",
                  cfParsed.pack.loaderKey + QLatin1Char('|') + cfParsed.pack.loaderVersion);
            check(cfParsed.pack.overridesDir == QStringLiteral("overrides"),
                  "V5e 往返：overrides 那一层目录名（manifest 里明写了 `overrides`）",
                  cfParsed.pack.overridesDir);
        }

        // 加载器 id 的拼法：四个前缀各一条，外加 NeoForge 1.20.1 那个特例。
        // 期望值不是"我觉得该长这样"：`<前缀>-<版本>` 是这个格式的写法，而 1.20.1 那一条
        // 照的是 Prism 的做法（那一代的 NeoForge 版本号本身就带游戏版本）。
        const auto loaderIdIs = [](const QString &key, const QString &version,
                                   const QString &game, const QString &expected) {
            return ModpackExport::curseForgeLoaderId(key, version, game) == expected;
        };
        check(loaderIdIs(ModpackImport::forgeLoaderKey(), QStringLiteral("47.2.0"),
                         QStringLiteral("1.21.1"), QStringLiteral("forge-47.2.0")) &&
                  loaderIdIs(ModpackImport::fabricLoaderKey(), QStringLiteral("0.16.14"),
                             QStringLiteral("1.21.1"), QStringLiteral("fabric-0.16.14")) &&
                  loaderIdIs(ModpackImport::quiltLoaderKey(), QStringLiteral("0.20.0"),
                             QStringLiteral("1.21.1"), QStringLiteral("quilt-0.20.0")),
              "V5e CurseForge 的加载器 id = `<前缀>-<纯版本号>`");
        check(loaderIdIs(ModpackImport::neoforgeLoaderKey(), QStringLiteral("21.1.210"),
                         QStringLiteral("1.21.1"), QStringLiteral("neoforge-21.1.210")) &&
                  loaderIdIs(ModpackImport::neoforgeLoaderKey(), QStringLiteral("47.1.106"),
                             QStringLiteral("1.20.1"), QStringLiteral("neoforge-1.20.1-47.1.106")),
              "V5e NeoForge 只在 1.20.1 上插一段游戏版本（别的版本不加）");
        check(ModpackExport::curseForgeLoaderId(QStringLiteral("OptiFine"), QStringLiteral("H8"),
                                                QStringLiteral("1.8.9"))
                  .isEmpty(),
              "V5e 认不出来的加载器不给 id（不编一个出来，由调用方照实说）");

        // 认不出来的加载器键不许瞎翻译：编一个出来会让导入方去装一个不存在的东西。
        check(ModpackExport::modrinthDependencyKey(QStringLiteral("OptiFine")).isEmpty() &&
                  ModpackExport::modrinthDependencyKey(QStringLiteral("liteloader")).isEmpty(),
              "V5e Modrinth 依赖表：认不出来的加载器返回空串（宁可不写）");
        check(ModpackExport::modrinthDependencyKey(ModpackImport::fabricLoaderKey()) ==
                      QStringLiteral("fabric-loader") &&
                  ModpackExport::modrinthDependencyKey(ModpackImport::forgeLoaderKey()) ==
                      QStringLiteral("forge") &&
                  ModpackExport::modrinthDependencyKey(ModpackImport::neoforgeLoaderKey()) ==
                      QStringLiteral("neoforge") &&
                  ModpackExport::modrinthDependencyKey(ModpackImport::quiltLoaderKey()) ==
                      QStringLiteral("quilt-loader"),
              "V5e Modrinth 依赖表：四个键与 ModpackImport 那套一字不差");

        // 值不值得拿去反查：只对"像资源"的文件算哈希。这一条是**代价**上的取舍：
        // 放进来的是 config/ 底下成千上万个小文件（几十万个字节的哈希、一次接口请求都没有）。
        const auto becomesCandidate = [](const QString &path) {
            return ModpackExport::looksLikeResolvableResource(path);
        };
        check(becomesCandidate(QStringLiteral("mods/sodium.jar")) &&
                  becomesCandidate(QStringLiteral("mods/sub/nested.jar")) &&
                  becomesCandidate(QStringLiteral("mods/foo.jar.disabled")) &&
                  becomesCandidate(QStringLiteral("resourcepacks/包.zip")) &&
                  becomesCandidate(QStringLiteral("shaderpacks/光影.zip")) &&
                  becomesCandidate(QStringLiteral("datapacks/数据包.zip")) &&
                  becomesCandidate(QStringLiteral("coremods/old.jar")),
              "V5e 候选：资源目录下的 jar/zip/… 都拿去反查");
        check(!becomesCandidate(QStringLiteral("config/随便.toml")) &&
                  !becomesCandidate(QStringLiteral("saves/世界/level.dat")) &&
                  !becomesCandidate(QStringLiteral("options.txt")) &&
                  !becomesCandidate(QStringLiteral("mods.txt")) &&
                  !becomesCandidate(QStringLiteral("随便.jar")) &&
                  !becomesCandidate(QStringLiteral("mods/笔记.txt")),
              "V5e 候选：config / 存档 / 根目录下的 jar / 不在资源目录里的 txt 都不问接口");
    }

    // --- V5f. 导出：实例目录里哪些文件进包（排除表 + 递归枚举） ---------------
    //
    // 这一节盯的是"打包时会不会把不该给别人的东西发出去"。最有分量的一条是**凭据**：
    // 别的启动器把账号放在实例根底下（`launcher_accounts.json` 那一串），打包是最容易
    // 把登录信息泄出去的一条路。另有一条是**根目录下的同一个名字不算数**（`mods/icon.png`
    // 是 mod 自己的资源，不能因为撞名就被排掉）。
    {
        const QString tree = sandbox + "/export-tree/我的实例";
        QDir(tree).removeRecursively();
        QDir().mkpath(tree);
        // 该进包的
        writeFileAt(tree + "/mods/sodium.jar", QByteArrayLiteral("假 mod"));
        writeFileAt(tree + "/mods/sub/nested.jar", QByteArrayLiteral("嵌套的"));
        writeFileAt(tree + "/config/模组配置.toml", QByteArrayLiteral("a=1"));
        writeFileAt(tree + "/options.txt", QByteArrayLiteral("fov:70"));
        writeFileAt(tree + "/mods/icon.png", QByteArrayLiteral("mod 自己的图标"));
        writeFileAt(tree + "/mods/logs/我的.log", QByteArrayLiteral("mod 自己的日志"));
        writeFileAt(tree + "/.packignore", QByteArrayLiteral("saves\n"));
        // 不该进包的：启动器自己 / 别的启动器 / 系统的垃圾 / 运行时文件
        writeFileAt(tree + "/wtfml.meta", QByteArrayLiteral("{\"instanceId\":\"x\"}"));
        writeFileAt(tree + "/icon.png", QByteArrayLiteral("启动器缓存的那张图"));
        writeFileAt(tree + "/我的实例.jar", QByteArrayLiteral("这个实例的版本 jar"));
        writeFileAt(tree + "/我的实例.json", QByteArrayLiteral("{}"));
        writeFileAt(tree + "/launcher_accounts.json", QByteArrayLiteral("{\"accounts\":{}}"));
        writeFileAt(tree + "/launcher_msa_credentials.bin", QByteArrayLiteral("二进制"));
        writeFileAt(tree + "/usernamecache.json", QByteArrayLiteral("{}"));
        writeFileAt(tree + "/.DS_Store", QByteArrayLiteral("junk"));
        writeFileAt(tree + "/.hmcl/账号.json", QByteArrayLiteral("{\"账户\""));
        writeFileAt(tree + "/logs/最新.log", QByteArrayLiteral("日志"));
        writeFileAt(tree + "/crash-reports/崩了.txt", QByteArrayLiteral("崩溃"));
        writeFileAt(tree + "/natives/glfw.dll", QByteArrayLiteral("dll"));
        writeFileAt(tree + "/libraries/某个库.jar", QByteArrayLiteral("库"));
        writeFileAt(tree + "/.mixin.out/调试.txt", QByteArrayLiteral("调试"));
        writeFileAt(tree + "/.fabric/remapped.jar", QByteArrayLiteral("重映射过的副本"));
        writeFileAt(tree + "/saves/我的世界/level.dat", QByteArrayLiteral("存档"));
        writeFileAt(tree + "/screenshots/一张图.png", QByteArrayLiteral("截图"));

        // 先单独钉排除表（纯函数，能一条一条说清楚为什么）
        const auto reasonOf = [](const QString &path, const ModpackExport::IncludeOptions &opt) {
            return ModpackExport::instanceExclusionReason(path, QStringLiteral("我的实例"), opt);
        };
        const ModpackExport::IncludeOptions defaults;
        check(!reasonOf(QStringLiteral("launcher_accounts.json"), defaults).isEmpty() &&
                  !reasonOf(QStringLiteral("launcher_msa_credentials.bin"), defaults).isEmpty() &&
                  !reasonOf(QStringLiteral("usernamecache.json"), defaults).isEmpty() &&
                  !reasonOf(QStringLiteral(".hmcl/账号.json"), defaults).isEmpty(),
              "V5f 凭据与别的启动器的账号状态一条都不进包");
        check(!reasonOf(QStringLiteral("wtfml.meta"), defaults).isEmpty() &&
                  !reasonOf(QStringLiteral("icon.png"), defaults).isEmpty() &&
                  !reasonOf(QStringLiteral("我的实例.jar"), defaults).isEmpty() &&
                  !reasonOf(QStringLiteral("我的实例.json"), defaults).isEmpty(),
              "V5f 启动器自己的状态与这个实例的版本文件不进包");
        check(!reasonOf(QStringLiteral("logs/最新.log"), defaults).isEmpty() &&
                  !reasonOf(QStringLiteral("crash-reports/x.txt"), defaults).isEmpty() &&
                  !reasonOf(QStringLiteral("natives/glfw.dll"), defaults).isEmpty() &&
                  !reasonOf(QStringLiteral("libraries/某个库.jar"), defaults).isEmpty() &&
                  !reasonOf(QStringLiteral(".mixin.out/x.txt"), defaults).isEmpty() &&
                  !reasonOf(QStringLiteral(".fabric/remapped.jar"), defaults).isEmpty(),
              "V5f 日志 / 崩溃报告 / natives / libraries / 缓存目录都不进包");
        check(reasonOf(QStringLiteral("mods/logs/我的.log"), defaults).isEmpty() &&
                  reasonOf(QStringLiteral("mods/icon.png"), defaults).isEmpty() &&
                  reasonOf(QStringLiteral("mods/sodium.jar"), defaults).isEmpty(),
              "V5f 排除表只按**顶层**那一段判：`mods/logs/…`、`mods/icon.png` 是 mod 自己的东西");
        ModpackExport::IncludeOptions withSaves = defaults;
        withSaves.saves = true;
        withSaves.screenshots = true;
        check(!reasonOf(QStringLiteral("saves/我的世界/level.dat"), defaults).isEmpty() &&
                  reasonOf(QStringLiteral("saves/我的世界/level.dat"), withSaves).isEmpty() &&
                  !reasonOf(QStringLiteral("screenshots/一张图.png"), defaults).isEmpty() &&
                  reasonOf(QStringLiteral("screenshots/一张图.png"), withSaves).isEmpty(),
              "V5f 存档与截图默认不进包，勾上之后进");

        // 再整棵走一遍
        const ModpackExport::CollectResult collected =
            ModpackExport::collectInstanceFiles(tree, defaults);
        QStringList paths;
        for (const ModpackExport::InstanceFile &file : collected.files) {
            paths.append(file.path);
        }
        check(collected.problem.isEmpty(), "V5f 实例目录枚举没报错", collected.problem);
        // 进包的**恰好**是这五条。写成全等（不是"包含"）是有意的：多出来一条
        // （比如哪个不该进包的漏了）和少一条都该红。
        const QStringList expected = {QStringLiteral("config/模组配置.toml"),
                                      QStringLiteral("mods/icon.png"),
                                      QStringLiteral("mods/logs/我的.log"),
                                      QStringLiteral("mods/sodium.jar"),
                                      QStringLiteral("mods/sub/nested.jar"),
                                      QStringLiteral("options.txt")};
        // 期望值本身就是排好序的，所以这一条同时钉住「内容对」和「顺序是排过的」。
        check(paths == expected, "V5f 枚举出来的正好是这六条，且按路径排过序（多一条少一条都算错）",
              paths.join(QLatin1Char(',')));
        // 再跑一遍：两次导出的条目顺序必须一样（包的字节内容不可复现无所谓，
        // 但"哪些文件、什么顺序"得定死，不然出问题没法对）。
        const ModpackExport::CollectResult again =
            ModpackExport::collectInstanceFiles(tree, defaults);
        QStringList againPaths;
        for (const ModpackExport::InstanceFile &file : again.files) {
            againPaths.append(file.path);
        }
        check(againPaths == paths, "V5f 同一个实例枚举两次，结果一字不差");
        check(collected.skippedDirs.contains(QStringLiteral("logs")) &&
                  collected.skippedDirs.contains(QStringLiteral("saves")) &&
                  collected.skippedDirs.contains(QStringLiteral(".hmcl")) &&
                  !collected.skippedDirs.contains(QStringLiteral("mods")) &&
                  !collected.skippedDirs.contains(QStringLiteral("config")),
              "V5f 被排掉的顶层目录也记下来了（界面上要照实说哪些没进包）",
              collected.skippedDirs.join(QLatin1Char(',')));
        check(collected.skippedFileCount == 18,
              "V5f 被排掉的文件个数 = 顶层文件 9（启动器状态 2 + 版本文件 2 + 凭据 2 + "
              "账号缓存 1 + 打包设置 1 + 系统垃圾 1）+ 被排掉的目录里的 9",
              QString::number(collected.skippedFileCount));
        const ModpackExport::CollectResult withSavesCollected =
            ModpackExport::collectInstanceFiles(tree, withSaves);
        check(withSavesCollected.skippedFileCount == 16,
              "V5f 勾上存档与截图之后，被排掉的个数正好少那两个目录里的文件（18 - 2）",
              QString::number(withSavesCollected.skippedFileCount));
        // 目录不存在时是「读不了」（problem 有话说），**不是**「这个实例是空的」——
        // 后者会导出一个空包，用户拿到手才发现什么都没有。
        const ModpackExport::CollectResult missing =
            ModpackExport::collectInstanceFiles(sandbox + "/export-tree/没有这个目录", defaults);
        check(!missing.problem.isEmpty() && missing.files.isEmpty(),
              "V5f 实例目录不存在时如实报「读不了」，而不是当成空实例", missing.problem);
        QDir(sandbox + "/export-tree").removeRecursively();
    }

    // --- V5g. 导出：一个文件读两趟，三个哈希一起出来 -------------------------
    //
    // 这一节压的是 hashFile 与 curseForgeFingerprint 那两份代码算出来的指纹**必须一样**
    // （它们是两份实现），以及 sha1/sha512 与 Qt 自己的结果一致。
    {
        const QString work = sandbox + "/export-hash";
        QDir(work).removeRecursively();
        QDir().mkpath(work);
        QByteArray content;  // 里面撒上空白，让"剔空白"这条真的起作用
        for (int i = 0; i < 5000; ++i) {
            content.append(static_cast<char>((i * 53 + 7) & 0xFF));
            if (i % 11 == 0) {
                content.append(QByteArrayLiteral(" \t"));
            }
        }
        const QString path = work + "/某个文件.bin";
        writeFileAt(path, content);

        ModpackExport::FileHashes hashes;
        QString hashError;
        const bool hashOk = ModpackExport::hashFile(path, true, &hashes, &hashError);
        check(hashOk, "V5g hashFile 算得出来", hashError);
        check(hashes.sha1 == QString::fromLatin1(
                                 QCryptographicHash::hash(content, QCryptographicHash::Sha1)
                                     .toHex()),
              "V5g sha1 与 Qt 算的一致（喂了两遍的话这里就对不上）");
        check(hashes.sha512 == QString::fromLatin1(
                                   QCryptographicHash::hash(content, QCryptographicHash::Sha512)
                                       .toHex()),
              "V5g sha512 与 Qt 算的一致");
        quint32 standalone = 0;
        QString fpError;
        const bool fpOk = ModpackExport::curseForgeFingerprint(path, &standalone, &fpError);
        check(fpOk && hashes.fingerprintOk && hashes.fingerprint == standalone,
              "V5g hashFile 里的指纹与 curseForgeFingerprint 一致（两份代码同一份算法）",
              fpOk ? QStringLiteral("%1 / %2").arg(hashes.fingerprint).arg(standalone) : fpError);
        // 只算 sha1/sha512 那一档（Modrinth 那条路）：指纹该是"没算"，不是"算出来个 0"。
        ModpackExport::FileHashes shaOnly;
        check(ModpackExport::hashFile(path, false, &shaOnly, &hashError) &&
                  shaOnly.sha1 == hashes.sha1 && !shaOnly.fingerprintOk &&
                  shaOnly.fingerprint == 0,
              "V5g 不要指纹时不碰指纹（fingerprintOk 是假，不是给个 0 冒充）");
        QDir(work).removeRecursively();
    }

    // --- V5h. 导出：反查那一发的形状与回复的读法 -----------------------------
    //
    // 两个夹具都是**真接口录下来的回复**（`meta/modrinth-version-files.json` 是拿 JEI 的
    // sha1 问 `/v2/version_files` 的原文，`meta/curseforge-fingerprints.json` 是拿同一个
    // 文件的指纹问 `/v1/fingerprints` 的原文）。所以这一节验的是"真东西读得懂"，
    // 不是"我们自己造的假回复读得懂"。
    {
        // 请求体：形状写错的话接口回 400，而 400 的正文只有一句 `{"error":"…"}`，
        // 看着和"这个包坏了"一模一样 —— 所以这两发的形状得钉住。
        const QByteArray modrinthBody = ModpackExport::modrinthVersionFilesRequestBody(
            {QStringLiteral("c19b8bfb43a753ad8c27612d516ce61c58b0d2e6")});
        const QJsonObject modrinthJson = QJsonDocument::fromJson(modrinthBody).object();
        check(modrinthJson.value(QStringLiteral("algorithm")).toString() ==
                      QStringLiteral("sha1") &&
                  modrinthJson.value(QStringLiteral("hashes")).toArray().size() == 1 &&
                  modrinthJson.value(QStringLiteral("hashes")).toArray().at(0).toString() ==
                      QStringLiteral("c19b8bfb43a753ad8c27612d516ce61c58b0d2e6"),
              "V5h Modrinth 的请求体是 {\"hashes\":[…],\"algorithm\":\"sha1\"}",
              QString::fromUtf8(modrinthBody));
        check(ModpackExport::modrinthVersionFilesRequestBody({}).isEmpty(),
              "V5h 空表不拼请求体（那一发不该发出去）");

        const QByteArray cfBody = ModpackExport::curseForgeFingerprintsRequestBody({3309664856u});
        const QJsonArray cfList =
            QJsonDocument::fromJson(cfBody).object().value(QStringLiteral("fingerprints")).toArray();
        check(cfList.size() == 1 && cfList.at(0).isDouble() &&
                  static_cast<quint32>(cfList.at(0).toDouble()) == 3309664856u,
              "★★ V5h CurseForge 的请求体里指纹是**数学意义上的数字**（int64）—— 发成字符串"
              "那一支整个不回话：字节看着没问题、只是那几格没图标，唯一会说话的是 400 的正文",
              QString::fromUtf8(cfBody));
        check(ModpackExport::curseForgeFingerprintsRequestBody({}).isEmpty(),
              "V5h 空表不拼请求体");

        // 读真回复：Modrinth 那一份
        const QString metaDir = QStringLiteral(REPO_ROOT_PATH) + "/tests/meta";
        QString modrinthSha1;
        QString curseForgeSha1;
        QFile modrinthFile(metaDir + "/modrinth-version-files.json");
        const bool haveModrinthFixture = modrinthFile.open(QIODevice::ReadOnly);
        check(haveModrinthFixture,
              "V5h 读到了 Modrinth 真回复的夹具（meta/modrinth-version-files.json）");
        if (haveModrinthFixture) {
            QString parseProblem;
            const QHash<QString, ModpackExport::ModrinthHit> hits =
                ModpackExport::parseModrinthVersionFiles(modrinthFile.readAll(), &parseProblem);
            modrinthFile.close();
            const QString jeiSha1 = QStringLiteral("c19b8bfb43a753ad8c27612d516ce61c58b0d2e6");
            check(parseProblem.isEmpty() && hits.contains(jeiSha1),
                  "V5h Modrinth 真回复读得懂，且键就是**我们发过去的那个 sha1**", parseProblem);
            if (hits.contains(jeiSha1)) {
                const ModpackExport::ModrinthHit hit = hits.value(jeiSha1);
                modrinthSha1 = hit.sha1;
                check(hit.sha1 == jeiSha1 && hit.sha512.size() == 128 &&
                          hit.url.startsWith(QStringLiteral("https://cdn.modrinth.com/")) &&
                          hit.size == 2338315 && hit.filename.endsWith(QStringLiteral(".jar")),
                      "V5h 命中的那一条：sha1/sha512/地址/字节数/文件名都对得上",
                      hit.url);
                check(hit.projectId == QStringLiteral("u6dRKJwZ") &&
                          hit.versionNumber == QStringLiteral("31.7.0.37"),
                      "V5h 项目号与版本号（只为了在结果里报数）",
                      hit.projectId + QLatin1Char('|') + hit.versionNumber);
            }
            // 认不出来的哈希不该凭空冒出来
            check(hits.size() == 1, "V5h 回复里有几条就几条（不凭空造）",
                  QString::number(hits.size()));
        }
        // 读真回复：CurseForge 那一份
        QFile cfFile(metaDir + "/curseforge-fingerprints.json");
        const bool haveCfFixture = cfFile.open(QIODevice::ReadOnly);
        check(haveCfFixture,
              "V5h 读到了 CurseForge 真回复的夹具（meta/curseforge-fingerprints.json）");
        if (haveCfFixture) {
            QString parseProblem;
            const QHash<quint32, ModpackExport::CurseForgeHit> hits =
                ModpackExport::parseCurseForgeFingerprints(cfFile.readAll(), &parseProblem);
            cfFile.close();
            check(parseProblem.isEmpty() && hits.contains(3309664856u),
                  "V5h CurseForge 真回复读得懂，键是那个文件自己的 fileFingerprint", parseProblem);
            if (hits.contains(3309664856u)) {
                const ModpackExport::CurseForgeHit hit = hits.value(3309664856u);
                curseForgeSha1 = hit.sha1;
                // 项目号取的是 file.modId、文件号取 file.id —— **不是**外面那一层 `id`
                // （那是项目号，按它归档会和文件号混起来）。
                check(hit.projectId == 238222 && hit.fileId == 8971390 && hit.available &&
                          hit.size == 2338315,
                      "V5h 命中的那一条：项目号/文件号/isAvailable/字节数都对得上",
                      QStringLiteral("%1/%2").arg(hit.projectId).arg(hit.fileId));
                check(hit.fileName == QStringLiteral("jei-26.3-neoforge-31.7.0.37.jar") &&
                          hit.downloadUrl.startsWith(QStringLiteral("https://")),
                      "V5h 文件名与下载地址", hit.downloadUrl);
            }
        }
        // 两个夹具问的是**同一个文件**（JEI 那一份 jar），所以两条路认出来的 sha1 必须一样 ——
        // 这正好也说明两个解析器取的是同一个东西，不是各读各的字段。
        check(!modrinthSha1.isEmpty() && modrinthSha1 == curseForgeSha1,
              "V5h 两条反查路认出来的 sha1 一致（同一份文件）",
              modrinthSha1 + QLatin1Char('/') + curseForgeSha1);
        // 空回复 / 坏回复 / 接口报错：三档都得是「能分辨」的，不能都退化成「没找到」。
        QString problem;
        check(ModpackExport::parseModrinthVersionFiles(QByteArrayLiteral("{}"), &problem).isEmpty() &&
                  problem.isEmpty(),
              "V5h 空 map 是「一个都没认出来」，不是「读不懂」（问题要能分开）", problem);
        check(ModpackExport::parseModrinthVersionFiles(QByteArrayLiteral("不是 json"), &problem)
                      .isEmpty() &&
                  !problem.isEmpty(),
              "V5h 读不懂的回复要有一句话", problem);
        check(ModpackExport::parseCurseForgeFingerprints(
                  QByteArrayLiteral("{\"error\":\"Invalid API key\"}"), &problem)
                      .isEmpty() &&
                  problem.contains(QStringLiteral("Invalid API key")),
              "V5h 接口报错时把那句话原样带出来（别换成我们自己的猜测）", problem);
        check(ModpackExport::parseModrinthVersionFiles(QByteArrayLiteral("{\"不是哈希\":{}}"), &problem)
                      .isEmpty(),
              "V5h 键不是哈希时一条都不认（不猜）");
        check(ModpackExport::isPoisonFingerprint(811513880u) &&
                  !ModpackExport::isPoisonFingerprint(3309664856u),
              "V5h 毒指纹 811513880 认得出来（接口偶尔把它当匹配上了，见到当没找到）");
        // 那一刀切在**解析**这一层：回复里那条记录自己的 `fileFingerprint` 是毒指纹时，
        // 它不代表任何一个具体文件 —— 键就是这个值，收进表里等于给某个"恰好算出这个
        // 指纹"的文件安排了一个假匹配。
        const QByteArray poisonReply = QByteArrayLiteral(
            "{\"data\":{\"exactMatches\":[{\"id\":238222,\"file\":{\"id\":8971390,\"modId\":238222,"
            "\"fileFingerprint\":811513880,\"isAvailable\":true,\"fileName\":\"x.jar\","
            "\"downloadUrl\":\"https://example.com/x.jar\",\"fileLength\":10,\"hashes\":[]}}],"
            "\"unmatchedFingerprints\":[],\"exactFingerprints\":[]}}");
        const QHash<quint32, ModpackExport::CurseForgeHit> poisonHits =
            ModpackExport::parseCurseForgeFingerprints(poisonReply, nullptr);
        check(poisonHits.isEmpty(),
              "V5h 毒指纹那条记录**不进表**（它不是「我们发过去的某个指纹」）",
              QString::number(poisonHits.size()));
    }

    // --- V5i. 导出：枚举 → 算哈希 → 反查 → 拼清单 这一整趟 --------------------
    //
    // 反查那一发是**注入进来的**（产品里是 HttpClient，这里是假的），所以整趟离线可跑。
    // 这里要钉的三句话：①认出来的进清单、不进 overrides；②认不出来的一个不丢、
    // 全在 overrides 里；③反查没成**不是错误**（照旧导得出包，只是全进 overrides）。
    {
        const QString tree = sandbox + "/export-plan/我的实例";
        QDir(tree).removeRecursively();
        QDir().mkpath(tree);
        const QByteArray bytesMatched = QByteArrayLiteral("AAA 认得出");
        const QByteArray bytesUnknown = QByteArrayLiteral("BBB 认不出");
        writeFileAt(tree + "/mods/认得出来.jar", bytesMatched);
        writeFileAt(tree + "/mods/认不出来.jar", bytesUnknown);
        writeFileAt(tree + "/config/配置.toml", QByteArrayLiteral("a=1"));
        writeFileAt(tree + "/options.txt", QByteArrayLiteral("fov:70"));
        writeFileAt(tree + "/logs/不该进包.log", QByteArrayLiteral("日志"));
        writeFileAt(tree + "/我的实例.jar", QByteArrayLiteral("版本文件"));

        const auto sha1OfBytes = [](const QByteArray &bytes) {
            QCryptographicHash hash(QCryptographicHash::Sha1);
            hash.addData(QByteArrayView(bytes));
            return QString::fromLatin1(hash.result().toHex());
        };
        const auto sha512OfBytes = [](const QByteArray &bytes) {
            QCryptographicHash hash(QCryptographicHash::Sha512);
            hash.addData(QByteArrayView(bytes));
            return QString::fromLatin1(hash.result().toHex());
        };
        const QString sha1Matched = sha1OfBytes(bytesMatched);
        const QString sha1Unknown = sha1OfBytes(bytesUnknown);

        ModpackExport::ExportRequest request;
        request.format = ModpackExport::Format::Modrinth;
        request.instanceDir = tree;
        request.name = QStringLiteral("我的整合包");
        request.version = QStringLiteral("1.2.3");
        request.summary = QStringLiteral("一句话简介");
        request.gameVersion = QStringLiteral("1.21.1");
        request.loaderKey = ModpackImport::fabricLoaderKey();
        request.loaderVersion = QStringLiteral("0.16.14");

        QStringList askedSha1;
        int queryCalls = 0;
        const ModpackExport::ModrinthQuery fakeModrinth =
            [&](const QStringList &sha1s, QHash<QString, ModpackExport::ModrinthHit> *out,
                QString *problemOut) {
                ++queryCalls;
                askedSha1 = sha1s;
                if (problemOut != nullptr) {
                    problemOut->clear();
                }
                ModpackExport::ModrinthHit hit;
                hit.projectId = QStringLiteral("u6dRKJwZ");
                hit.versionNumber = QStringLiteral("1.2.3");
                hit.filename = QStringLiteral("认得出来.jar");
                hit.sha1 = sha1Matched;
                hit.sha512 = sha512OfBytes(bytesMatched);
                hit.url = QStringLiteral(
                    "https://cdn.modrinth.com/data/u6dRKJwZ/versions/abc/认得出来.jar");
                hit.size = bytesMatched.size();
                hit.environment = QStringLiteral("client_only");
                out->insert(sha1Matched, hit);
                return true;
            };

        // 进度：记下每一次（done, total, 当前那个文件）。最后一次是反查那一发
        // （没有具体文件，`current` 是空串）——界面靠它切到"正在问接口"。
        QList<QString> progressLines;
        int lastDone = -1;
        int lastTotal = -1;
        const ModpackExport::ExportProgress progress = [&](int done, int total,
                                                          const QString &current) {
            lastDone = done;
            lastTotal = total;
            progressLines.append(QStringLiteral("%1/%2 %3").arg(done).arg(total).arg(current));
        };

        const ModpackExport::ExportPlan plan =
            ModpackExport::buildExportPlan(request, fakeModrinth, ModpackExport::CurseForgeQuery(),
                                          progress);
        check(plan.problem.isEmpty() && !plan.cancelled && !plan.lookupFailed,
              "V5i 整趟跑完没有停下来（枚举/哈希/反查都成了）", plan.problem);
        check(queryCalls == 1, "V5i 反查是**一发**（不是每个文件一发）",
              QString::number(queryCalls));
        check(plan.queried == 2, "V5i 问的是那两个资源文件（config/options.txt 不该去问）",
              QString::number(plan.queried));
        QStringList askedSorted = askedSha1;
        askedSorted.sort();
        QStringList expectedAsked = {sha1Matched, sha1Unknown};
        expectedAsked.sort();
        check(askedSorted == expectedAsked,
              "V5i 发过去的就是本地算出来的**那两个 sha1**（一条不多一条不少）",
              askedSorted.join(QLatin1Char(',')));
        check(plan.matched == 1 && plan.notDistributable == 0,
              "V5i 认出来一条", QString::number(plan.matched));
        check(lastDone == lastTotal && lastTotal == 2,
              "V5i 进度走到了头（total 就是要算哈希的文件数）",
              QStringLiteral("%1/%2").arg(lastDone).arg(lastTotal));
        check(!progressLines.isEmpty() && progressLines.last().endsWith(QLatin1Char(' ')),
              "V5i 最后一发进度没有文件名（那是「正在问接口」那一档）",
              progressLines.join(QLatin1Char('|')));

        // ① 认出来的进清单，而且**不在** overrides 里
        check(plan.spec.files.size() == 1, "V5i 清单里就那一条",
              QString::number(plan.spec.files.size()));
        if (plan.spec.files.size() == 1) {
            const ModpackExport::MatchedFile &matched = plan.spec.files.first();
            check(matched.path == QStringLiteral("mods/认得出来.jar") &&
                      matched.sha1 == sha1Matched &&
                      matched.sha512 == sha512OfBytes(bytesMatched) &&
                      matched.size == bytesMatched.size() &&
                      matched.urls.size() == 1,
                  "V5i 认出来的那条：路径 / sha1 / sha512 / 字节数 / 地址都对得上",
                  matched.path);
            check(matched.envClient == QStringLiteral("required") &&
                      matched.envServer == QStringLiteral("unsupported"),
                  "V5i 接口说 `client_only` 时写 env（只写这一种，见 applyEnvironment）",
                  matched.envClient + QLatin1Char('|') + matched.envServer);
        }
        QStringList overridePaths;
        for (const ModpackExport::InstanceFile &file : plan.overrides) {
            overridePaths.append(file.path);
        }
        const QStringList expectedOverrides = {QStringLiteral("config/配置.toml"),
                                               QStringLiteral("mods/认不出来.jar"),
                                               QStringLiteral("options.txt")};
        check(overridePaths == expectedOverrides,
              "V5i 认不出来的**一个不丢**、全在 overrides 里，且按路径排过序（认出来的不在里面）",
              overridePaths.join(QLatin1Char(',')));
        check(!plan.collect.skippedDirs.isEmpty() &&
                  plan.collect.skippedDirs.contains(QStringLiteral("logs")),
              "V5i 被排掉的目录照实带出来（界面上要说哪些没进包）");

        // 清单往返：我们自己写得出来，自己也得读得回来
        const ModpackImport::Result back =
            ModpackImport::parseModrinthIndex(ModpackExport::buildModrinthIndex(plan.spec));
        check(back.ok && back.pack.fileCount() == 1 &&
                  back.pack.files.first().path == QStringLiteral("mods/认得出来.jar"),
              "V5i 这一趟的产物自己的解析器读得回来（导出/导入是同一件事的两个方向）",
              back.problem);
        check(ModpackExport::reimportable(plan.spec),
              "V5i 有清单条目 ⇒ 这是个我们自己装得回来的包");

        // ② 反查没成：**不是错误**，但一个文件都不许丢 —— 那些"本来能认出来"的现在
        // 也只能进 overrides（断网也能出包，这是 PCL 那套的取舍）。
        const ModpackExport::ModrinthQuery deadQuery =
            [](const QStringList &, QHash<QString, ModpackExport::ModrinthHit> *,
               QString *problemOut) {
                if (problemOut != nullptr) {
                    *problemOut = QStringLiteral("连不上 Modrinth。");
                }
                return false;
            };
        const ModpackExport::ExportPlan offline =
            ModpackExport::buildExportPlan(request, deadQuery, ModpackExport::CurseForgeQuery());
        check(offline.lookupFailed && offline.lookupProblem == QStringLiteral("连不上 Modrinth。") &&
                  offline.problem.isEmpty(),
              "V5i 反查没成如实报出来，但**不算错误**（清单/overrides 照旧拼得出来）",
              offline.lookupProblem);
        QStringList offlineOverrides;
        for (const ModpackExport::InstanceFile &file : offline.overrides) {
            offlineOverrides.append(file.path);
        }
        check(offline.matched == 0 && offline.spec.files.isEmpty() &&
                  offlineOverrides.contains(QStringLiteral("mods/认得出来.jar")) &&
                  offlineOverrides.size() == 4,
              "V5i 反查没成时那份「本来认得出」的文件也进 overrides（一份都不丢）",
              offlineOverrides.join(QLatin1Char(',')));
        check(!ModpackExport::reimportable(offline.spec),
              "V5i 反查没成之后清单是空的 ⇒ 调用点必须拿 reimportable() 拦一下"
              "（我们自己的解析器拒收空清单：ModpackImport.cpp:379）");

        // ③ 取消：一个文件都还没读就停 —— 反查那一发**不许发出去**，清单一条都没有。
        // 这时候整份 plan 作废（`overrides` 里可能有半截东西，见 .hpp 那句），
        // 所以这里不去数 overrides，只钉"叫停之后收手了没有"。
        int cancelQueryCalls = 0;
        const ModpackExport::ModrinthQuery countingQuery =
            [&cancelQueryCalls](const QStringList &, QHash<QString, ModpackExport::ModrinthHit> *,
                                QString *) {
                ++cancelQueryCalls;
                return true;
            };
        const ModpackExport::ExportPlan stopped =
            ModpackExport::buildExportPlan(request, countingQuery, ModpackExport::CurseForgeQuery(),
                                           ModpackExport::ExportProgress(),
                                           []() { return true; });
        check(stopped.cancelled && stopped.spec.files.isEmpty() && stopped.spec.refs.isEmpty() &&
                  stopped.queried == 0 && cancelQueryCalls == 0 && !stopped.lookupFailed,
              "V5i 用户叫停之后立刻收手（一个哈希都不算、反查不发、清单是空的），"
              "且这不是「反查失败」那一档",
              QStringLiteral("%1 / 问了 %2 个").arg(cancelQueryCalls).arg(stopped.queried));

        QDir(sandbox + "/export-plan").removeRecursively();
    }

    // --- V5j. 导出：CurseForge 那一条路（号 + 关分发 + 毒指纹） ---------------
    {
        const QString tree = sandbox + "/export-cf/我的实例";
        QDir(tree).removeRecursively();
        QDir().mkpath(tree);
        const QByteArray bytesOk = QByteArrayLiteral("CCC 能下");
        const QByteArray bytesClosed = QByteArrayLiteral("DDD 关了分发");
        writeFileAt(tree + "/mods/能下的.jar", bytesOk);
        writeFileAt(tree + "/mods/关了分发的.jar", bytesClosed);

        quint32 fpOk = 0;
        quint32 fpClosed = 0;
        QString hashProblem;
        check(ModpackExport::curseForgeFingerprint(tree + "/mods/能下的.jar", &fpOk, &hashProblem) &&
                  ModpackExport::curseForgeFingerprint(tree + "/mods/关了分发的.jar", &fpClosed,
                                                       &hashProblem),
              "V5j 两个文件的指纹都算出来了", hashProblem);

        ModpackExport::ExportRequest request;
        request.format = ModpackExport::Format::CurseForge;
        request.instanceDir = tree;
        request.name = QStringLiteral("我的整合包");
        request.version = QStringLiteral("1.2.3");
        request.author = QStringLiteral("某人");
        request.gameVersion = QStringLiteral("1.21.1");
        request.loaderKey = ModpackImport::neoforgeLoaderKey();
        request.loaderVersion = QStringLiteral("21.1.210");

        int queryCalls = 0;
        QList<quint32> askedFingerprints;
        const ModpackExport::CurseForgeQuery fakeCurseForge =
            [&](const QList<quint32> &fingerprints, QHash<quint32, ModpackExport::CurseForgeHit> *out,
                QString *problemOut) {
                ++queryCalls;
                askedFingerprints = fingerprints;
                if (problemOut != nullptr) {
                    problemOut->clear();
                }
                ModpackExport::CurseForgeHit ok;
                ok.projectId = 394468;
                ok.fileId = 5123456;
                ok.available = true;
                ok.fileName = QStringLiteral("能下的.jar");
                ok.downloadUrl = QStringLiteral("https://edge.forgecdn.net/files/5123/456/x.jar");
                out->insert(fpOk, ok);
                ModpackExport::CurseForgeHit closed;
                closed.projectId = 238222;
                closed.fileId = 8971390;
                closed.available = false;  // 作者关了第三方分发
                closed.availableKnown = true;
                closed.fileName = QStringLiteral("关了分发的.jar");
                out->insert(fpClosed, closed);
                return true;
            };

        const ModpackExport::ExportPlan plan = ModpackExport::buildExportPlan(
            request, ModpackExport::ModrinthQuery(), fakeCurseForge);
        check(queryCalls == 1 && askedFingerprints.size() == 2,
              "V5j CurseForge 也是一发问完（指纹按本地算出来的那两个）",
              QString::number(askedFingerprints.size()));
        check(plan.matched == 1 && plan.notDistributable == 1,
              "V5j 认出来一条、另一条「认出来了但作者关了分发」（分开报数）",
              QStringLiteral("%1 / %2").arg(plan.matched).arg(plan.notDistributable));
        check(plan.spec.refs.size() == 1 && plan.spec.refs.first().projectId == 394468 &&
                  plan.spec.refs.first().fileId == 5123456,
              "V5j 清单里只有两个号（CF 那边没有文件名/地址/哈希）");
        QStringList cfOverrides;
        for (const ModpackExport::InstanceFile &file : plan.overrides) {
            cfOverrides.append(file.path);
        }
        check(cfOverrides == QStringList{QStringLiteral("mods/关了分发的.jar")},
              "V5j 关了分发的那份退回 overrides（写进清单等于给出一个装不上的包）",
              cfOverrides.join(QLatin1Char(',')));
        // 往返：号是那两个号，加载器那一行也是 CF 认得的那一行
        const ModpackImport::Result back = ModpackImport::parseCurseForgeManifest(
            ModpackExport::buildCurseForgeManifest(plan.spec));
        check(back.ok && back.pack.pendingRefs.size() == 1 &&
                  back.pack.pendingRefs.first().projectId == 394468 &&
                  back.pack.pendingRefs.first().fileId == 5123456 &&
                  back.pack.loaderKey == ModpackImport::neoforgeLoaderKey() &&
                  back.pack.loaderVersion == QStringLiteral("21.1.210"),
              "V5j CF 清单往返：号与加载器那一行都对得上", back.problem);
        check(ModpackExport::reimportable(plan.spec),
              "V5j 有 refs ⇒ 我们自己装得回来");

        QDir(sandbox + "/export-cf").removeRecursively();
    }

    // --- V5s. 导出：名字长到清单写不下的那一条，必须退回 overrides ---------------
    //
    // 读侧对清单里**每一条**路径都跑 isSafeRelativePath()（ModpackImport.cpp:313），
    // 不过就是**整包拒绝**（"所以整个包都没有导入"）。所以产出这一侧不许写出那样一条。
    // 从前写得出：实例里有一个嵌套很深、拼起来超过 240 个字符的文件时，我们导出的包
    // 连自己都装不回来，而且**当场看不出来**（清单写得出来、zip 打得出来）。
    // 退回 overrides 之后文件一个不少 —— 那一条路不经过这个判据。
    {
        const QString tree = sandbox + "/export-long/我的实例";
        QDir(tree).removeRecursively();
        QDir().mkpath(tree);
        const QByteArray bytesOk = QByteArrayLiteral("AAA 正常的那一个");
        const QByteArray bytesLong = QByteArrayLiteral("BBB 名字超长的那一个");
        writeFileAt(tree + "/mods/认得出来.jar", bytesOk);
        // 整条相对路径 = 5 + 120 + 1 + 120 + 1 + 6 = 253 个字符（> 240），而**每一段**
        // 都在名字允许的长度里 —— 超的是"拼起来以后"那一条，也就只有长度那一条判据拦得住
        // 它（段那几条都在 InstanceFiles::validatePathSegment 里，那里没有长度限制）。
        const QString longPath = QStringLiteral("mods/") + QString(120, QLatin1Char('x')) +
                                 QLatin1Char('/') + QString(120, QLatin1Char('y')) +
                                 QStringLiteral("/长名字.jar");
        writeFileAt(tree + QStringLiteral("/") + longPath, bytesLong);
        check(longPath.length() > 240, "V5s 造出来的那条路径真的超过 240 个字符",
              QString::number(longPath.length()));

        const auto sha1Of = [](const QByteArray &bytes) {
            QCryptographicHash hash(QCryptographicHash::Sha1);
            hash.addData(QByteArrayView(bytes));
            return QString::fromLatin1(hash.result().toHex());
        };
        const auto sha512Of = [](const QByteArray &bytes) {
            QCryptographicHash hash(QCryptographicHash::Sha512);
            hash.addData(QByteArrayView(bytes));
            return QString::fromLatin1(hash.result().toHex());
        };
        const QString sha1Ok = sha1Of(bytesOk);
        const QString sha1Long = sha1Of(bytesLong);

        ModpackExport::ExportRequest request;
        request.format = ModpackExport::Format::Modrinth;
        request.instanceDir = tree;
        request.name = QStringLiteral("我的整合包");
        request.version = QStringLiteral("1.2.3");
        request.gameVersion = QStringLiteral("1.21.1");
        request.loaderKey = ModpackImport::fabricLoaderKey();
        request.loaderVersion = QStringLiteral("0.16.14");

        // 反查**两条都答得出来** —— 这一节要钉的不是"认不认得出"，而是"认出来了也不许写"。
        const ModpackExport::ModrinthQuery query =
            [&](const QStringList &, QHash<QString, ModpackExport::ModrinthHit> *out, QString *) {
                const auto put = [&](const QString &sha1, const QString &sha512, qint64 size) {
                    ModpackExport::ModrinthHit hit;
                    hit.projectId = QStringLiteral("abcd1234");
                    hit.sha1 = sha1;
                    hit.sha512 = sha512;
                    hit.url = QStringLiteral("https://cdn.modrinth.com/data/abcd1234/versions/x/a.jar");
                    hit.size = size;
                    out->insert(sha1, hit);
                };
                put(sha1Ok, sha512Of(bytesOk), bytesOk.size());
                put(sha1Long, sha512Of(bytesLong), bytesLong.size());
                return true;
            };

        const ModpackExport::ExportPlan plan =
            ModpackExport::buildExportPlan(request, query, ModpackExport::CurseForgeQuery());
        check(plan.problem.isEmpty() && plan.lookupFailed == false,
              "V5s 整趟跑完没有停下来", plan.problem);
        check(plan.queried == 2 && plan.matched == 1 && plan.spec.files.size() == 1 &&
                  plan.spec.files.first().path == QStringLiteral("mods/认得出来.jar"),
              "V5s 只有那条短的写进清单（超长的那条**认出来了也不许写**）",
              QStringLiteral("%1 / %2").arg(plan.queried).arg(plan.matched));
        QStringList overridePaths;
        for (const ModpackExport::InstanceFile &file : plan.overrides) {
            overridePaths.append(file.path);
        }
        check(overridePaths == QStringList{longPath},
              "V5s 超长的那条退回 overrides（跟着包走，一个文件都不丢）",
              QString::number(overridePaths.size()));
        // 直接问那句判据本身（纯函数）：这两条一真一假，才是"退回去"这个动作的来路。
        ModpackExport::MatchedFile longEntry;
        longEntry.path = longPath;
        longEntry.sha1 = sha1Long;
        longEntry.sha512 = sha512Of(bytesLong);
        longEntry.urls.append(QStringLiteral("https://cdn.modrinth.com/data/abcd1234/versions/x/a.jar"));
        check(!ModpackExport::writableAsIndexFile(longEntry),
              "V5s writableAsIndexFile 说这条不能写进清单（路径过不了读侧那条判据）");
        // 反证：真写进去的话，**读侧就是整包拒绝** —— 这一条把"为什么必须退回"钉在读侧
        // 那句实现上（哪天读侧把长度那一条去掉了，这里会红，因为拒绝不再发生）。
        // NOTE: 是拿**这一趟真写出来的那份清单**改的（不是手搓一份）：手搓的话，
        // "被拒"可能来自别的缺项（少个 dependencies 之类），那就证明不了这一条。
        // 也不能拿 badSpec 走 buildModrinthIndex —— 那个函数自己也会先问
        // writableAsIndexFile，会把这条**跳过不写**（见它里面那段），于是清单里
        // 根本没有这一条，验的是一句空话。
        QJsonObject tampered =
            QJsonDocument::fromJson(ModpackExport::buildModrinthIndex(plan.spec)).object();
        QJsonArray tamperedFiles = tampered.value(QStringLiteral("files")).toArray();
        QJsonObject hashes;
        hashes.insert(QStringLiteral("sha1"), sha1Long);
        hashes.insert(QStringLiteral("sha512"), sha512Of(bytesLong));
        QJsonObject longEntryJson;
        longEntryJson.insert(QStringLiteral("path"), longPath);
        longEntryJson.insert(QStringLiteral("hashes"), hashes);
        longEntryJson.insert(
            QStringLiteral("downloads"),
            QJsonArray{QStringLiteral("https://cdn.modrinth.com/data/abcd1234/versions/x/a.jar")});
        tamperedFiles.append(longEntryJson);
        tampered.insert(QStringLiteral("files"), tamperedFiles);
        const ModpackImport::Result rejected = ModpackImport::parseModrinthIndex(
            QJsonDocument(tampered).toJson(QJsonDocument::Compact));
        check(!rejected.ok,
              "V5s 反证：这样的路径一旦写进清单，读侧整包拒绝（所以产出这一侧必须拦住）",
              rejected.problem);
        // 而真正导出的那一份清单读得回来（清单里没有那条超长路径）
        const ModpackImport::Result back =
            ModpackImport::parseModrinthIndex(ModpackExport::buildModrinthIndex(plan.spec));
        check(back.ok && back.pack.fileCount() == 1,
              "V5s 这一趟的产物自己的解析器读得回来", back.problem);

        QDir(sandbox + "/export-long").removeRecursively();
    }

    // --- V5t. 导出（CF）：没说能不能分发 ≠ 作者关了分发；改过名/禁用着的退回 overrides
    //
    // 三条都是"报告里那句话必须对得上盘上的事实"：
    // ① 回复里**没有** `isAvailable` 这一项时，`toBool(false)` 也是假，但那是"接口没说"，
    // 不是"作者关了第三方分发" —— 两种分开报；
    // ② 用户自己改过名（`改过名.jar`）或者禁用着（`禁用着的.jar.disabled`）的 mod，
    // 写进 CF 清单等于让收包的人那边落回**作者给的原名**：禁用着的那个会变成启用的。
    // 它们只有原样 copy 才留得住状态 ⇒ 退回 overrides；
    // ③ 接口根本没接上时 `queried` 是 **0**（一个字节都没发出去），不是"本来打算问几个"。
    {
        const QString tree = sandbox + "/export-cf-state/我的实例";
        QDir(tree).removeRecursively();
        QDir().mkpath(tree);
        const QByteArray bytesOk = QByteArrayLiteral("AAA 能下的");
        const QByteArray bytesClosed = QByteArrayLiteral("BBB 作者关了分发");
        const QByteArray bytesSilent = QByteArrayLiteral("CCC 接口没提这一项");
        const QByteArray bytesOff = QByteArrayLiteral("DDD 用户禁用着的");
        const QByteArray bytesRenamed = QByteArrayLiteral("EEE 用户改过名的");
        writeFileAt(tree + "/mods/能下的.jar", bytesOk);
        writeFileAt(tree + "/mods/关了分发的.jar", bytesClosed);
        writeFileAt(tree + "/mods/没说.jar", bytesSilent);
        writeFileAt(tree + "/mods/禁用着的.jar.disabled", bytesOff);
        writeFileAt(tree + "/mods/改过名.jar", bytesRenamed);

        const auto fingerprintOf = [&](const QByteArray &bytes, const QString &name) {
            quint32 value = 0;
            QString problem;
            if (!ModpackExport::curseForgeFingerprint(tree + QStringLiteral("/mods/") + name, &value,
                                                      &problem)) {
                check(false, "V5t 指纹算得出来", problem);
            }
            return value;
        };
        const quint32 fpOk = fingerprintOf(bytesOk, QStringLiteral("能下的.jar"));
        const quint32 fpClosed = fingerprintOf(bytesClosed, QStringLiteral("关了分发的.jar"));
        const quint32 fpSilent = fingerprintOf(bytesSilent, QStringLiteral("没说.jar"));
        const quint32 fpOff = fingerprintOf(bytesOff, QStringLiteral("禁用着的.jar.disabled"));
        const quint32 fpRenamed = fingerprintOf(bytesRenamed, QStringLiteral("改过名.jar"));

        ModpackExport::ExportRequest request;
        request.format = ModpackExport::Format::CurseForge;
        request.instanceDir = tree;
        request.name = QStringLiteral("我的整合包");
        request.version = QStringLiteral("1.2.3");
        request.gameVersion = QStringLiteral("1.21.1");
        request.loaderKey = ModpackImport::forgeLoaderKey();
        request.loaderVersion = QStringLiteral("47.2.0");

        int queryCalls = 0;
        const ModpackExport::CurseForgeQuery query =
            [&](const QList<quint32> &, QHash<quint32, ModpackExport::CurseForgeHit> *out, QString *) {
                ++queryCalls;
                const auto put = [&](quint32 key, int projectId, int fileId, const QString &fileName,
                                     bool available, bool availableKnown) {
                    ModpackExport::CurseForgeHit hit;
                    hit.projectId = projectId;
                    hit.fileId = fileId;
                    hit.fileName = fileName;
                    hit.available = available;
                    hit.availableKnown = availableKnown;
                    out->insert(key, hit);
                };
                put(fpOk, 394468, 5123456, QStringLiteral("能下的.jar"), true, true);
                // 作者关了分发：接口**明说**了（有 `isAvailable`，值是假）。
                put(fpClosed, 238222, 8971390, QStringLiteral("关了分发的.jar"), false, true);
                // 接口**没说**：回复里连这一项都没有（缺席 ⇒ toBool(false) 也是假）。
                put(fpSilent, 111111, 2222222, QStringLiteral("没说.jar"), false, false);
                // 这两条接口都说能下（`fileName` = 作者给的原名），但**用户盘上的名字不一样**：
                // 一个是禁用着（后缀），一个是改过名。
                put(fpOff, 333333, 4444444, QStringLiteral("禁用着的.jar"), true, true);
                put(fpRenamed, 555555, 6666666, QStringLiteral("原来的名字.jar"), true, true);
                return true;
            };

        const ModpackExport::ExportPlan plan =
            ModpackExport::buildExportPlan(request, ModpackExport::ModrinthQuery(), query);
        check(plan.problem.isEmpty() && !plan.lookupFailed && queryCalls == 1 && plan.queried == 5,
              "V5t 一发问完五个（queried 是**发出去那些**）",
              QStringLiteral("%1 / 问了 %2").arg(queryCalls).arg(plan.queried));
        check(plan.matched == 1 && plan.notDistributable == 1 &&
                  plan.notDistributableUnknown == 1 && plan.keptLocalState == 2,
              "V5t 四种原因分开报：认出来 1 / 作者关了 1 / 接口没说 1 / 名字带状态 2",
              QStringLiteral("%1 / %2 / %3 / %4")
                  .arg(plan.matched)
                  .arg(plan.notDistributable)
                  .arg(plan.notDistributableUnknown)
                  .arg(plan.keptLocalState));
        check(plan.spec.refs.size() == 1 && plan.spec.refs.first().projectId == 394468,
              "V5t 清单里只有那条能下的（另外四种都没写）",
              QString::number(plan.spec.refs.size()));
        QStringList overridePaths;
        for (const ModpackExport::InstanceFile &file : plan.overrides) {
            overridePaths.append(file.path);
        }
        check(overridePaths.size() == 4 &&
                  overridePaths.contains(QStringLiteral("mods/关了分发的.jar")) &&
                  overridePaths.contains(QStringLiteral("mods/没说.jar")) &&
                  overridePaths.contains(QStringLiteral("mods/禁用着的.jar.disabled")) &&
                  overridePaths.contains(QStringLiteral("mods/改过名.jar")) &&
                  !overridePaths.contains(QStringLiteral("mods/能下的.jar")),
              "V5t 那四种全退回 overrides（原样带着用户那个名字），认出来的那条不在里面",
              overridePaths.join(QLatin1Char(',')));
        // ③ 接口没接上：一个字节都没发出去 ⇒ queried 必须是 0。从前它在这里就已经是 5 了
        // （「本来打算问几个」），于是报告里那句「问过 5 个文件，认出来 0 个」会让人去查
        // 网络、查限流，而真相是这一趟根本没联网。
        const ModpackExport::ExportPlan noQuery = ModpackExport::buildExportPlan(
            request, ModpackExport::ModrinthQuery(), ModpackExport::CurseForgeQuery());
        check(noQuery.queried == 0 && noQuery.lookupFailed && noQuery.matched == 0 &&
                  noQuery.spec.refs.isEmpty() && noQuery.overrides.size() == 5,
              "V5t 接口没接上：queried 是 0（不是「本来打算问几个」），文件一个不丢",
              QStringLiteral("queried=%1 / overrides=%2")
                  .arg(noQuery.queried)
                  .arg(noQuery.overrides.size()));

        QDir(sandbox + "/export-cf-state").removeRecursively();
    }

    // --- V5k. 导出：落成一个 zip，再用**我们自己的导入路**读回来 ---------------
    //
    // 这一节是整条链上最像"真跑一次"的地方：写文件 → createZip → 用 ZipUtil::readEntry
    // 读清单、用 ZipUtil::extractSubtrees 解 overrides（那正是整合包导入用的那两条路）。
    // 条目名拼错了是**静默**的（我们自己读得回来），所以这里除了"读得回来"还要直接去
    // zip 的字节里找一遍 `overrides/` —— 反斜杠那条在 .NET 那一侧看不见。
    {
        const QString work = sandbox + "/export-zip";
        QDir(work).removeRecursively();
        const QString tree = work + "/我的实例";
        QDir().mkpath(tree);
        writeFileAt(tree + "/mods/认不出来.jar", QByteArrayLiteral("BBB 认不出"));
        writeFileAt(tree + "/config/配置.toml", QByteArrayLiteral("a=1"));
        writeFileAt(tree + "/options.txt", QByteArrayLiteral("fov:70"));
        writeFileAt(work + "/staging/modrinth.index.json",
                    QByteArrayLiteral("{\"formatVersion\":1}"));

        ModpackExport::ExportRequest request;
        request.format = ModpackExport::Format::Modrinth;
        request.instanceDir = tree;
        request.name = QStringLiteral("我的整合包");
        request.version = QStringLiteral("1.2.3");
        request.gameVersion = QStringLiteral("1.21.1");
        request.loaderKey = ModpackImport::fabricLoaderKey();
        request.loaderVersion = QStringLiteral("0.16.14");
        // 反查一律"没成"：这一节要看的是**一个文件都不丢**地打进 overrides。
        const ModpackExport::ExportPlan plan = ModpackExport::buildExportPlan(
            request, ModpackExport::ModrinthQuery(), ModpackExport::CurseForgeQuery());
        check(plan.overrides.size() == 3, "V5k 三个文件都进 overrides",
              QString::number(plan.overrides.size()));

        // 清单那一条从盘上那份来（这里用个占位字节，内容和这一节无关）
        const QList<ZipUtil::ZipSource> entries =
            ModpackExport::archiveEntries(plan, work + "/staging/modrinth.index.json");
        check(entries.size() == 4 && entries.first().entryName == QStringLiteral("modrinth.index.json"),
              "V5k 条目表：清单在第一条，后面跟着 overrides 里那三个",
              QString::number(entries.size()));
        check(entries.at(1).entryName == QStringLiteral("overrides/config/配置.toml"),
              "V5k 条目名是 `overrides/<相对路径>`（`/` 分隔）", entries.at(1).entryName);

        const QString zipPath = work + "/我的整合包.mrpack";
        QString zipError;
        check(ZipUtil::createZip(zipPath, entries, &zipError), "V5k createZip 打出来了", zipError);
        QFile zipFile(zipPath);
        const bool haveZip = zipFile.open(QIODevice::ReadOnly);
        check(haveZip && zipFile.size() > 0, "V5k 盘上那个 zip 不是空的",
              QString::number(zipFile.size()));
        const QByteArray zipBytes = haveZip ? zipFile.readAll() : QByteArray();
        // zip 中央目录里的条目名是**原样的字节**：找得到 `overrides/` 说明写进去的确实是
        // 正斜杠。这条断言之所以必要，是因为刚才那条（readEntry 读得回来）**在反斜杠的
        // 情况下也绿** —— .NET 读的时候会归一。
        check(zipBytes.contains(QByteArrayLiteral("overrides/config/")) &&
                  zipBytes.contains(QByteArrayLiteral("modrinth.index.json")),
              "V5k zip 字节里条目名是正斜杠的 `overrides/…`（反斜杠那种别的启动器认不出）");

        // 第一条路：清单读得回来
        QByteArray manifest;
        QString readError;
        const ZipUtil::EntryStatus status =
            ZipUtil::readEntry(zipPath, QStringLiteral("modrinth.index.json"), &manifest, &readError,
                               QStringLiteral("读清单"));
        check(status == ZipUtil::EntryStatus::Ok &&
                  manifest == QByteArrayLiteral("{\"formatVersion\":1}"),
              "V5k 清单在 zip 根、读得回来", readError);

        // 第二条路：overrides 解出来（**这条就是整合包导入走的那条**）
        const QString unpacked = work + "/解开";
        int extracted = 0;
        QString extractError;
        check(ZipUtil::extractSubtrees({zipPath}, unpacked, {QStringLiteral("overrides/")},
                                       &extracted, &extractError),
              "V5k overrides 解得开（走导入那一条路）", extractError);
        check(extracted == 3, "V5k 解出来三个覆盖文件", QString::number(extracted));
        QFile backConfig(unpacked + "/config/配置.toml");
        QFile backOptions(unpacked + "/options.txt");
        check(backConfig.open(QIODevice::ReadOnly) && backConfig.readAll() == QByteArrayLiteral("a=1") &&
                  backOptions.open(QIODevice::ReadOnly) &&
                  backOptions.readAll() == QByteArrayLiteral("fov:70"),
              "V5k 解出来的内容与实例里那份一字不差（前缀被剥掉了）");
        check(!QFile::exists(unpacked + "/modrinth.index.json") &&
                  !QFile::exists(unpacked + "/overrides/options.txt"),
              "V5k 不属于 overrides/ 的条目一个都没落进实例目录（连前缀那一层都不留）");

        // 失败那一路：来源缺一个 → 报错 + **半成品删掉**（盘上留着一个坏包最坏）
        QList<ZipUtil::ZipSource> bad = entries;
        bad.append({QStringLiteral("overrides/没有这个文件.txt"), tree + "/没有这个文件.txt"});
        const QString badZip = work + "/坏包.mrpack";
        QString badError;
        check(!ZipUtil::createZip(badZip, bad, &badError) && !badError.isEmpty(),
              "V5k 来源文件不存在时如实报错", badError);
        check(!QFile::exists(badZip), "V5k 失败之后盘上不留半成品");
        // 条目名里带反斜杠的那种**在写之前**就拦下来（写进去就晚了，见 .hpp）
        QString slashError;
        check(!ZipUtil::createZip(work + "/斜杠.mrpack",
                                  {{QStringLiteral("overrides\\x.txt"), tree + "/options.txt"}},
                                  &slashError) &&
                  !QFile::exists(work + "/斜杠.mrpack"),
              "V5k 条目名带反斜杠的当场拒绝，不是写完了才说", slashError);

        QDir(work).removeRecursively();
    }

    // --- V5l. 清单的名字与正文是同一套（buildIndexFile 那个唯一分岔） -----------
    //
    // 这一节盯的是一个**静默**的错法：文件名写 `manifest.json`、正文却是 Modrinth 那一份。
    // 那种包自己打得出来、.NET 也读得回来（它不看内容），只有别人的启动器会当场说不认识。
    {
        ModpackExport::Spec modrinth;
        modrinth.format = ModpackExport::Format::Modrinth;
        modrinth.name = QStringLiteral("我的整合包");
        modrinth.version = QStringLiteral("1.2.3");
        modrinth.gameVersion = QStringLiteral("1.21.1");
        modrinth.loaderKey = ModpackImport::fabricLoaderKey();
        modrinth.loaderVersion = QStringLiteral("0.16.14");
        ModpackExport::MatchedFile file;
        file.path = QStringLiteral("mods/认得出来.jar");
        file.sha1 = QStringLiteral("c19b8bfb43a753ad8c27612d516ce61c58b0d2e6");
        file.sha512 = QString(128, QLatin1Char('a'));
        file.urls.append(QStringLiteral("https://cdn.modrinth.com/data/u6dRKJwZ/versions/abc/x.jar"));
        file.size = 12345;
        modrinth.files.append(file);

        check(ModpackExport::buildIndexFile(modrinth) ==
                  ModpackExport::buildModrinthIndex(modrinth),
              "V5l Modrinth 那一档走的就是 modrinth.index.json 那一份正文");
        const ModpackImport::Result round =
            ModpackImport::parseModrinthIndex(ModpackExport::buildIndexFile(modrinth));
        check(round.ok, "V5l 它自己解析得回来（名字和正文认的是同一个枚举）", round.problem);

        ModpackExport::Spec curseforge;
        curseforge.format = ModpackExport::Format::CurseForge;
        curseforge.name = QStringLiteral("我的整合包");
        curseforge.version = QStringLiteral("1.2.3");
        curseforge.gameVersion = QStringLiteral("1.21.1");
        curseforge.loaderKey = ModpackImport::neoforgeLoaderKey();
        curseforge.loaderVersion = QStringLiteral("21.1.210");
        ModpackExport::MatchedRef ref;
        ref.projectId = 394468;
        ref.fileId = 5123456;
        curseforge.refs.append(ref);

        check(ModpackExport::buildIndexFile(curseforge) ==
                  ModpackExport::buildCurseForgeManifest(curseforge),
              "V5l CurseForge 那一档走的就是 manifest.json 那一份正文");
        const ModpackImport::Result round2 =
            ModpackImport::parseCurseForgeManifest(ModpackExport::buildIndexFile(curseforge));
        check(round2.ok, "V5l CF 那份也自己解析得回来", round2.problem);

        check(ModpackExport::indexFileName(ModpackExport::Format::Modrinth) ==
                      QStringLiteral("modrinth.index.json") &&
                  ModpackExport::indexFileName(ModpackExport::Format::CurseForge) ==
                      QStringLiteral("manifest.json"),
              "V5l 两份规范的文件名都钉着（改名字的只有 writers 那一处）");
    }

    // --- V5m. 这个实例装的是什么加载器（导出清单里那一行的唯一出处） -----------
    //
    // 判据的第一条路是**盘上的 id**，第二条路是 wtfml.meta 里我们自己记的显示名。这一节
    // 把"认得出来""认得出来但写不下""认不出来""确实是原版"四档都钉住 —— 因为第一档错
    // 了只是包差一点，第三档错了会把一个装了几百个模组的实例导成纯原版包（用户装回来
    // 会发现模组都在、游戏起不来，而包看上去是好的）。
    {
        const QString versionsRoot = sandbox + "/versions";
        const QString idChain = QStringLiteral("V5m-链上认");
        const QString idMeta = QStringLiteral("V5m-meta认");
        const QString idOptifine = QStringLiteral("V5m-OptiFine");
        const QString idQuilt = QStringLiteral("V5m-Quilt");
        const QString idVanilla = QStringLiteral("V5m-原版");
        const QString idMetaBad = QStringLiteral("V5m-meta不认识");
        const QString idCycleA = QStringLiteral("V5m-环A");
        const QString idCycleB = QStringLiteral("V5m-环B");

        // 实例 → 加载器版本 → 原版：三层链（真机上的形状就是这么来的）
        writeFileAt(versionsRoot + "/" + idChain + "/" + idChain + ".json",
                    QByteArrayLiteral("{\"id\":\"V5m-链上认\","
                                      "\"inheritsFrom\":\"fabric-loader-0.16.14-1.21.1\"}"));
        writeFileAt(versionsRoot + "/fabric-loader-0.16.14-1.21.1/"
                                 "fabric-loader-0.16.14-1.21.1.json",
                    QByteArrayLiteral("{\"id\":\"fabric-loader-0.16.14-1.21.1\","
                                      "\"inheritsFrom\":\"1.21.1\"}"));
        writeFileAt(versionsRoot + "/1.21.1/1.21.1.json", QByteArrayLiteral("{\"id\":\"1.21.1\"}"));

        // 第一条路不认识任何东西，只有 meta 里记着加载器（用户手写过的 meta 就是这个形状）
        writeFileAt(versionsRoot + "/" + idMeta + "/" + idMeta + ".json",
                    QByteArrayLiteral("{\"id\":\"V5m-meta认\",\"inheritsFrom\":\"1.21.1\"}"));
        writeFileAt(versionsRoot + "/" + idMeta + "/wtfml.meta",
                    QByteArrayLiteral("{\"loaderName\":\"Forge\",\"loaderVersion\":\"47.4.10\","
                                      "\"realVersion\":\"1.21.1\"}"));

        // OptiFine / Quilt：认得出来，但都没有能写进清单的键
        writeFileAt(versionsRoot + "/" + idOptifine + "/" + idOptifine + ".json",
                    QByteArrayLiteral("{\"id\":\"V5m-OptiFine\","
                                      "\"inheritsFrom\":\"1.18.2-OptiFine_HD_U_H9\"}"));
        writeFileAt(versionsRoot + "/" + idQuilt + "/" + idQuilt + ".json",
                    QByteArrayLiteral("{\"id\":\"V5m-Quilt\","
                                      "\"inheritsFrom\":\"quilt-loader-0.20.0-1.20.1\"}"));

        // 确实是原版：链上没有加载器，meta 里也没有 loaderName
        writeFileAt(versionsRoot + "/" + idVanilla + "/" + idVanilla + ".json",
                    QByteArrayLiteral("{\"id\":\"V5m-原版\",\"inheritsFrom\":\"1.21.1\"}"));
        writeFileAt(versionsRoot + "/" + idVanilla + "/wtfml.meta",
                    QByteArrayLiteral("{\"realVersion\":\"1.21.1\"}"));

        // meta 里写了一个我们不认识的名字
        writeFileAt(versionsRoot + "/" + idMetaBad + "/" + idMetaBad + ".json",
                    QByteArrayLiteral("{\"id\":\"V5m-meta不认识\",\"inheritsFrom\":\"1.21.1\"}"));
        writeFileAt(versionsRoot + "/" + idMetaBad + "/wtfml.meta",
                    QByteArrayLiteral("{\"loaderName\":\"LiteLoader\",\"loaderVersion\":\"1.12\"}"));

        // 继承链成环：不许死循环，也不许因为"走不动了"就报错
        writeFileAt(versionsRoot + "/" + idCycleA + "/" + idCycleA + ".json",
                    QByteArrayLiteral("{\"id\":\"V5m-环A\",\"inheritsFrom\":\"V5m-环B\"}"));
        writeFileAt(versionsRoot + "/" + idCycleB + "/" + idCycleB + ".json",
                    QByteArrayLiteral("{\"id\":\"V5m-环B\",\"inheritsFrom\":\"V5m-环A\"}"));

        VersionManager vm;
        const auto ask = [&vm](const QString &instanceId, QString *key, QString *version) {
            key->clear();
            version->clear();
            return vm.instanceLoaderInfo(instanceId, key, version);
        };

        {
            QString key;
            QString version;
            const QString problem = ask(idChain, &key, &version);
            check(problem.isEmpty() && key == ModpackImport::fabricLoaderKey() &&
                      version == QStringLiteral("0.16.14"),
                  "V5m 链上的 fabric-loader-<版本>-<游戏版本> 拆得开",
                  QStringLiteral("%1 / %2 %3").arg(problem, key, version));
        }
        {
            // Fabric 那条要**从左边切第一刀**：游戏版本里真能带 `-`，加载器版本不会。
            const QString id = QStringLiteral("V5m-带横杠");
            writeFileAt(versionsRoot + "/" + id + "/" + id + ".json",
                        QByteArrayLiteral("{\"id\":\"V5m-带横杠\","
                                          "\"inheritsFrom\":\"fabric-loader-0.16.14-1.21-pre1\"}"));
            QString key;
            QString version;
            check(ask(id, &key, &version).isEmpty() && key == ModpackImport::fabricLoaderKey() &&
                      version == QStringLiteral("0.16.14"),
                  "V5m 游戏版本里带横杠时，切出来的加载器版本还是 0.16.14",
                  QStringLiteral("%1 / %2").arg(key, version));
        }
        {
            QString key;
            QString version;
            const QString problem = ask(idMeta, &key, &version);
            check(problem.isEmpty() && key == ModpackImport::forgeLoaderKey() &&
                      version == QStringLiteral("47.4.10"),
                  "V5m 链上没有、meta 里记着的那一档也认得出来（Forge 47.4.10）",
                  QStringLiteral("%1 / %2 %3").arg(problem, key, version));
        }
        {
            QString key;
            QString version;
            const QString problem = ask(idVanilla, &key, &version);
            check(problem.isEmpty() && key.isEmpty() && version.isEmpty(),
                  "V5m 确实是原版的实例：空键 + 空版本 + 没有抱怨（那是正常结果）", problem);
        }
        {
            QString key;
            QString version;
            const QString problem = ask(idOptifine, &key, &version);
            check(!problem.isEmpty() && key.isEmpty(),
                  "V5m OptiFine 认得出来但**拒绝导出**（两种清单里都没有它那一格）", problem);
            check(problem.contains(QStringLiteral("OptiFine")),
                  "V5m 那句话里点明了是 OptiFine（而不是一句通用的失败）", problem);
        }
        {
            QString key;
            QString version;
            const QString problem = ask(idQuilt, &key, &version);
            check(!problem.isEmpty() && key.isEmpty(),
                  "V5m Quilt 也拒绝导出（清单写得下，我们装不了）", problem);
        }
        {
            QString key;
            QString version;
            const QString problem = ask(idMetaBad, &key, &version);
            check(!problem.isEmpty() && problem.contains(QStringLiteral("LiteLoader")),
                  "V5m meta 里那个不认识的名字照实报出来（不静默当成纯原版）", problem);
        }
        {
            QString key;
            QString version;
            const QString problem = ask(idCycleA, &key, &version);
            check(problem.isEmpty() && key.isEmpty(),
                  "V5m 继承链成环时不死循环、也不报错（该认的上面几层已经认过了）", problem);
        }
    }

    // --- V5o. PCL / HMCL 那类"合并过的"实例：基于哪个原版 + 装的是什么加载器 --------
    //
    // 用户在真机上撞的那一条：日志里只有一句「导出失败：读不出 海波篮子 基于哪个
    // 原版」。真因是那个实例是 PCL 装的 —— 整份 json 都合进了实例目录（`inheritsFrom` 是
    // 空的，`id` 就是用户起的名字），所以 V5m 那几条按 id 认的路一步都走不出去；而它的加载器
    // （`net.fabricmc:fabric-loader:0.19.5`）和原版（`clientVersion: 26.2`）就明明白白写在
    // 那份 json 自己身上。**真机上大多数实例都是这个形状**，所以这不是一条边界用例。
    //
    // 这一节钉两件**方向相反**的事：
    // ① 这两格读得出来（读不出来，那些实例一个都导不出去）；
    // ② 读不出来时**一个字都不许猜** —— 目录名以数字开头但带空格（`1.20.2-Fabric 0.17.2`）
    // 不许顶成游戏版本；认不出加载器时不许悄悄当成纯原版（那正是这个功能最怕的失败：
    // 包看上去是好的、装回来模组都在、游戏起不来）。
    {
        const QString versionsRoot = sandbox + "/versions";
        const auto writeJson = [&versionsRoot](const QString &id, const QByteArray &body) {
            writeFileAt(versionsRoot + "/" + id + "/" + id + ".json", body);
        };
        const auto writeMeta = [&versionsRoot](const QString &id, const QByteArray &body) {
            writeFileAt(versionsRoot + "/" + id + "/wtfml.meta", body);
        };

        VersionManager vm;
        const auto loaderOf = [&vm](const QString &id, QString *key, QString *version) {
            key->clear();
            version->clear();
            return vm.instanceLoaderInfo(id, key, version);
        };

        // ① 真机上那个实例，一比一抄下来的形状：合过的 json、id 是名字、没有 inheritsFrom。
        const QString idPcl = QStringLiteral("海波篮子");
        writeJson(idPcl, QByteArrayLiteral("{\"id\":\"海波篮子\",\"clientVersion\":\"26.2\","
                                           "\"mainClass\":\"net.fabricmc.loader.impl.launch.knot."
                                           "KnotClient\",\"libraries\":["
                                           "{\"name\":\"net.fabricmc:intermediary:26.2\"},"
                                           "{\"name\":\"net.fabricmc:fabric-loader:0.19.5\"},"
                                           "{\"name\":\"org.ow2.asm:asm:9.7\"}]}"));
        // meta 里只有 instanceId（我们导入时写的那一份），既没有 realVersion 也没有 loaderName
        writeMeta(idPcl,
                  QByteArrayLiteral("{\"instanceId\":\"535107a2-4b43-4915-bd38-c7d85ebf3586\"}"));
        check(vm.instanceGameVersion(idPcl) == QStringLiteral("26.2"),
              "V5o PCL 那种合过的 json：原版从 clientVersion 读得出来（26.2）",
              vm.instanceGameVersion(idPcl));
        {
            QString key;
            QString version;
            const QString problem = loaderOf(idPcl, &key, &version);
            check(problem.isEmpty() && key == ModpackImport::fabricLoaderKey() &&
                      version == QStringLiteral("0.19.5"),
                  "V5o PCL 那种合过的 json：加载器从 libraries 认出来了（fabric 0.19.5）",
                  QStringLiteral("%1 / %2 %3").arg(problem, key, version));
        }

        // ② 目录名以数字开头、但不是版本号（真机上有 `1.20.2-Fabric 0.17.2` 这种）。
        // 这条钉的是判据本身：**"以数字开头"不够**。
        const QString idSpacey = QStringLiteral("1.20.2-Fabric 0.17.2");
        writeJson(idSpacey, QByteArrayLiteral("{\"id\":\"1.20.2-Fabric 0.17.2\",\"libraries\":["
                                             "{\"name\":\"net.fabricmc:fabric-loader:0.17.2\"}]}"));
        check(vm.instanceGameVersion(idSpacey).isEmpty(),
              "V5o 目录名以数字开头但带空格 ⇒ 不许顶成游戏版本（写进清单就是个假版本号）",
              vm.instanceGameVersion(idSpacey));
        {
            QString key;
            QString version;
            // 同一个实例的加载器照旧认得出：原版那一格没读出来，不代表加载器也不认。
            check(loaderOf(idSpacey, &key, &version).isEmpty() &&
                      key == ModpackImport::fabricLoaderKey() &&
                      version == QStringLiteral("0.17.2"),
                  "V5o 原版那格读不出来时，加载器那一格照旧照实认（fabric 0.17.2）",
                  QStringLiteral("%1 / %2").arg(key, version));
        }

        // ③ 纯原版：目录名就是版本号，json 里什么都没有。
        const QString idVanilla = QStringLiteral("1.21.4");
        writeJson(idVanilla, QByteArrayLiteral("{\"id\":\"1.21.4\"}"));
        check(vm.instanceGameVersion(idVanilla) == QStringLiteral("1.21.4"),
              "V5o 纯原版实例：目录名 / 它自己那份 json 的 id 就是版本号", vm.instanceGameVersion(idVanilla));
        {
            QString key;
            QString version;
            check(loaderOf(idVanilla, &key, &version).isEmpty() && key.isEmpty(),
                  "V5o 纯原版实例：空键 + 空版本 + 没有抱怨（那是正常结果）",
                  QStringLiteral("%1 / %2").arg(key, version));
        }

        // ④ 目录名完全不像版本号（真机上有 `原版ATT2地图（无模组）` 这种）：宁可返回空串
        // （调用点会拦住并报一句），也不许编一个版本号出来。
        const QString idOdd = QStringLiteral("历史存档（无模组）");
        writeJson(idOdd, QByteArrayLiteral("{\"id\":\"历史存档（无模组）\"}"));
        check(vm.instanceGameVersion(idOdd).isEmpty(),
              "V5o 目录名完全不像版本号 ⇒ 返回空串（不编、也不报错）", vm.instanceGameVersion(idOdd));

        // ⑤ 薄 json（我们自己的整合包导入就是这个形状）：inheritsFrom 走到原版那一份。
        const QString idThin = QStringLiteral("V5o-薄");
        writeJson(idThin, QByteArrayLiteral("{\"id\":\"V5o-薄\",\"inheritsFrom\":\"1.21.4\"}"));
        check(vm.instanceGameVersion(idThin) == QStringLiteral("1.21.4"),
              "V5o 薄 json 顺着 inheritsFrom 走到原版那一份", vm.instanceGameVersion(idThin));

        // ⑥ realVersion 仍然排在 clientVersion 前面（它是装的时候从清单里抄的真值）。
        const QString idReal = QStringLiteral("V5o-realVersion");
        writeJson(idReal, QByteArrayLiteral("{\"id\":\"V5o-realVersion\",\"clientVersion\":\"26.2\"}"));
        writeMeta(idReal, QByteArrayLiteral("{\"realVersion\":\"1.21.1\"}"));
        check(vm.instanceGameVersion(idReal) == QStringLiteral("1.21.1"),
              "V5o meta 里的 realVersion 优先于 json 里的 clientVersion",
              vm.instanceGameVersion(idReal));

        // ⑦ realVersion 是能手改的文本：它自己不像版本号时作废，往下问 clientVersion。
        const QString idRealBad = QStringLiteral("V5o-realVersion坏");
        writeJson(idRealBad,
                  QByteArrayLiteral("{\"id\":\"V5o-realVersion坏\",\"clientVersion\":\"1.20.2\"}"));
        writeMeta(idRealBad, QByteArrayLiteral("{\"realVersion\":\"1.20.2-Fabric 0.17.2\"}"));
        check(vm.instanceGameVersion(idRealBad) == QStringLiteral("1.20.2"),
              "V5o meta 里那个不像版本号的 realVersion 作废，clientVersion 顶上",
              vm.instanceGameVersion(idRealBad));

        // ---- 加载器：从 libraries 的 maven 坐标认（形状都照真机上量过的那几种抄）----
        const auto loaderCase = [&](const QString &id, const QByteArray &libraries) {
            writeJson(id, QByteArrayLiteral("{\"id\":\"") + id.toUtf8() +
                              QByteArrayLiteral("\",\"libraries\":") + libraries +
                              QByteArrayLiteral("}"));
        };
        const auto expectLoader = [&](const QString &id, const QString &wantKey,
                                      const QString &wantVersion, const QString &title) {
            QString key;
            QString version;
            const QString problem = loaderOf(id, &key, &version);
            check(problem.isEmpty() && key == wantKey && version == wantVersion, title,
                  QStringLiteral("%1 / %2 %3").arg(problem, key, version));
        };

        const QString idOldForge = QStringLiteral("V5o-旧Forge");
        loaderCase(idOldForge, QByteArrayLiteral("[{\"name\":\"net.minecraftforge:forge:1.12.2-"
                                                "14.23.5.2860\"}]"));
        expectLoader(idOldForge, ModpackImport::forgeLoaderKey(), QStringLiteral("14.23.5.2860"),
                     "V5o 1.12 那种 forge 坐标：切掉的是原版那一段，不是加载器版本");

        // GTNH 那种后面还挂着一个游戏版本的坐标：**从左边切第一刀**（切最后一个横杠是反的）。
        const QString idGtnh = QStringLiteral("V5o-GTNH");
        loaderCase(idGtnh, QByteArrayLiteral("[{\"name\":\"net.minecraftforge:forge:"
                                            "1.7.10-10.13.4.1614-1.7.10\"}]"));
        expectLoader(idGtnh, ModpackImport::forgeLoaderKey(),
                     QStringLiteral("10.13.4.1614-1.7.10"),
                     "V5o GTNH 那种带尾巴的 forge 坐标切得开（10.13.4.1614-1.7.10）");

        const QString idNewForge = QStringLiteral("V5o-新Forge");
        writeJson(idNewForge,
                  QByteArrayLiteral("{\"id\":\"V5o-新Forge\",\"libraries\":[{\"name\":"
                                    "\"net.minecraftforge:fmlloader:1.20.1-47.2.0\"}],"
                                    "\"arguments\":{\"game\":[\"--fml.forgeVersion\",\"47.2.0\"]}}"));
        expectLoader(idNewForge, ModpackImport::forgeLoaderKey(), QStringLiteral("47.2.0"),
                     "V5o 1.13 以后的 forge 走 fmlloader 那一格（47.2.0）");

        // 只有加载器自己声明的启动参数、没有坐标：那也是证据（和 id 那条路不同的一条法）。
        const QString idArgOnly = QStringLiteral("V5o-只认参数");
        writeJson(idArgOnly, QByteArrayLiteral("{\"id\":\"V5o-只认参数\",\"arguments\":{\"game\":"
                                              "[\"--fml.forgeVersion\",\"47.3.33\"]}}"));
        expectLoader(idArgOnly, ModpackImport::forgeLoaderKey(), QStringLiteral("47.3.33"),
                     "V5o 只有 --fml.forgeVersion 时也认（47.3.33）");

        const QString idNeo = QStringLiteral("V5o-NeoForge");
        writeJson(idNeo,
                  QByteArrayLiteral("{\"id\":\"V5o-NeoForge\",\"libraries\":[{\"name\":"
                                    "\"net.neoforged.fancymodloader:loader:4.0.41\"}],"
                                    "\"arguments\":{\"game\":[\"--fml.neoForgeVersion\","
                                    "\"21.1.184\"]}}"));
        {
            QString key;
            QString version;
            const QString problem = loaderOf(idNeo, &key, &version);
            check(problem.isEmpty() && key == ModpackImport::neoforgeLoaderKey() &&
                      version == QStringLiteral("21.1.184"),
                  "V5o NeoForge 的版本号取自 --fml.neoForgeVersion（21.1.184）",
                  QStringLiteral("%1 / %2 %3").arg(problem, key, version));
            // 那条参数就是为这一格存在的：4.0.41 是 fancymodloader 的版本，**不是** neoforge 的。
            check(version != QStringLiteral("4.0.41"),
                  "V5o 写进清单的不是 fancymodloader 那个 4.0.41");
        }

        // 认得出来是 NeoForge、但读不出是哪一版 ⇒ **停下来报一句**，不许拿 4.0.41 顶上。
        const QString idNeoNoVer = QStringLiteral("V5o-NeoForge没号");
        loaderCase(idNeoNoVer,
                   QByteArrayLiteral("[{\"name\":\"net.neoforged.fancymodloader:loader:4.0.41\"}]"));
        {
            QString key;
            QString version;
            const QString problem = loaderOf(idNeoNoVer, &key, &version);
            check(!problem.isEmpty() && key.isEmpty() &&
                      problem.contains(QStringLiteral("NeoForge")),
                  "V5o 认得出是 NeoForge、读不出哪一版时：报一句、不导出（不拿 4.0.41 顶）",
                  problem);
        }

        // OptiFine / Quilt：认得出来，但都没有能写进清单的键 —— 这一档混进纯原版就是灾难。
        const QString idOf = QStringLiteral("V5o-OptiFine");
        loaderCase(idOf, QByteArrayLiteral("[{\"name\":\"optifine:OptiFine:1.20.1_HD_U_I6\"}]"));
        {
            QString key;
            QString version;
            const QString problem = loaderOf(idOf, &key, &version);
            check(!problem.isEmpty() && key.isEmpty() &&
                      problem.contains(QStringLiteral("OptiFine")),
                  "V5o 合过的 json 里装了 OptiFine：照旧拒绝导出（那两句说明只有一个出处）",
                  problem);
        }
        const QString idQuilt = QStringLiteral("V5o-Quilt");
        loaderCase(idQuilt, QByteArrayLiteral("[{\"name\":\"org.quiltmc:quilt-loader:0.20.0\"}]"));
        {
            QString key;
            QString version;
            const QString problem = loaderOf(idQuilt, &key, &version);
            check(!problem.isEmpty() && key.isEmpty() &&
                      problem.contains(QStringLiteral("Quilt")),
                  "V5o 合过的 json 里装了 Quilt：也拒绝导出（清单写得下、我们装不了）", problem);
        }

        // 两种都装（Fabric + OptiFine 是真机上很常见的一种）：OptiFine 那一档优先，
        // 和 id 那条路同一个顺序。
        const QString idBoth = QStringLiteral("V5o-Fabric加OptiFine");
        loaderCase(idBoth,
                   QByteArrayLiteral("[{\"name\":\"net.fabricmc:fabric-loader:0.16.14\"},"
                                     "{\"name\":\"optifine:OptiFine:1.20.1_HD_U_I6\"}]"));
        {
            QString key;
            QString version;
            const QString problem = loaderOf(idBoth, &key, &version);
            check(!problem.isEmpty() && problem.contains(QStringLiteral("OptiFine")),
                  "V5o Fabric 与 OptiFine 同时装着时报 OptiFine 那一句（顺序也和 id 那条路一致）",
                  problem);
        }

        // mainClass **不是**判据：Forge 与 NeoForge 共用 BootstrapLauncher，而 1.12.2 的
        // 纯原版用的就是 launchwrapper。拿它当判据的话，纯原版会被认成装了加载器。
        const QString idMainOnly = QStringLiteral("V5o-只看mainClass");
        writeJson(idMainOnly,
                  QByteArrayLiteral("{\"id\":\"V5o-只看mainClass\",\"mainClass\":"
                                    "\"cpw.mods.bootstraplauncher.BootstrapLauncher\"}"));
        {
            QString key;
            QString version;
            const QString problem = loaderOf(idMainOnly, &key, &version);
            check(problem.isEmpty() && key.isEmpty(),
                  "V5o mainClass 不参与判断（BootstrapLauncher 被 Forge/NeoForge 共用、"
                  "launchwrapper 纯原版也在用）",
                  QStringLiteral("%1 / %2").arg(key, version));
        }
    }

    // --- V5u. 实例设置里「数据包」那一页该不该出现（纯函数， 加） ------
    //
    // 用户 补的一句：「对于没有数据包的版本，数据包也默认隐藏且不可勾选」。
    // 判据落在 `VersionManager::gameVersionSupportsDatapacks()` 上，**纯函数**（喂版本号
    // 与 releaseTime 两串文字），所以这一节不碰盘、不联网，喂多少种版本号都行。
    //
    // 数据包是快照 `17w43a`引进的。两格判据按**先准后糙**排：
    // ① 盘上那份**合并过的** json 有 `releaseTime` ⇒ 和 比。这一格离线也准，
    // 快照 / `a1.2.6` / `b1.7.3` 那种"版本号看着不像数字"的也分得对；
    // ② 没有 ⇒ 看版本号本身，以数字开头的按 `主.次 >= (1,13)` 比；
    // ③ 两条都问不出来 ⇒ **给**（宁可多一页，不要静默藏掉一页）。
    //
    // 这一节另一半钉的是**"哪一格说了算"**：releaseTime 在的时候**盖过**版本号本身，
    // 而且两格都不许悄悄把"读不出来"当成"没有"。
    {
        const auto supports = [](const QString &id, const QString &stamp) {
            return VersionManager::gameVersionSupportsDatapacks(id, stamp);
        };

        // ① releaseTime 说了算 —— 而且用的是**真清单里那种形状**（`+00:00` 那个后缀）。
        // 这一格要是解析不出来，整条规则会**静默**退到版本号那一档（1.12.2 照样对、
        // 快照就全错），症状是"看着都对、就是那几个版本多出一页"。
        check(!supports(QStringLiteral("1.12.2"),
                        QStringLiteral("2017-09-18T11:05:56+00:00")),
              "V5u 1.12.2 那一份的 releaseTime 早于 2017-11-01 ⇒ 没有数据包那一页");
        check(supports(QStringLiteral("1.13"),
                       QStringLiteral("2018-07-18T15:22:36+00:00")),
              "V5u 1.13（数据包引进之后的第一个正式版）⇒ 有");
        check(!supports(QStringLiteral("1.20.1"), QStringLiteral("2010-01-01T00:00:00+00:00")),
              "V5u releaseTime 在手时**盖过**版本号本身：1.20.1 配一个 2010 的时间戳 ⇒ "
              "按时间戳判（判成没有）");
        // 界线本身：`17w43a` 就是把数据包引进来的那一个快照，而它的日期在
        // **之前**。这一条故意钉住"按日期切"这个口径 —— 不为几个界上的快照编例外表。
        check(!supports(QStringLiteral("17w43a"),
                        QStringLiteral("2017-10-25T15:00:00+00:00")),
              "V5u 界线就是界线：17w43a 自己的 releaseTime 早于 2017-11-01 ⇒ 判成没有"
              "（界上那几个快照不值得为它编一张例外表）");
        // 版本号看着不像数字的那些：只有 releaseTime 这一格分得对。
        check(!supports(QStringLiteral("a1.2.6"),
                        QStringLiteral("2010-10-30T00:00:00+00:00")),
              "V5u a1.2.6：版本号那一格认不出来，靠 releaseTime 判 ⇒ 没有");
        check(!supports(QStringLiteral("b1.7.3"),
                        QStringLiteral("2011-07-08T00:00:00+00:00")),
              "V5u b1.7.3 同理 ⇒ 没有（版本号那一格认不出来，2011 那一份的时间戳照实读）");
        check(supports(QStringLiteral("b1.7.3"),
                       QStringLiteral("2020-01-01T00:00:00+00:00")),
              "V5u 同一个 b1.7.3 换一个 2020 的时间戳 ⇒ 有 —— releaseTime 那一格说了算，"
              "版本号长得像不像数字它都不管");

        // ② 没有 releaseTime：退回版本号本身（以数字开头的按 主.次 >= (1,13) 比）。
        check(!supports(QStringLiteral("1.12.2"), QString()),
              "V5u 没有 releaseTime 时看版本号：1.12.2 ⇒ 没有");
        check(!supports(QStringLiteral("1.7.10"), QString()),
              "V5u 1.7.10 ⇒ 没有");
        check(supports(QStringLiteral("1.13"), QString()), "V5u 1.13 ⇒ 有（下界含在内）");
        check(supports(QStringLiteral("1.13.1"), QString()), "V5u 1.13.1 ⇒ 有");
        check(supports(QStringLiteral("1.20.1"), QString()), "V5u 1.20.1 ⇒ 有");
        check(supports(QStringLiteral("2.0"), QString()), "V5u 主版本高于 1 ⇒ 有");
        // 真机上大量的实例目录名是 `1.12.2-Forge` 这种：版本号那一截取到第一个不是数字
        // 也不是点号的字符为止，别把 `-Forge` 当成版本的一部分。
        check(!supports(QStringLiteral("1.12.2-Forge"), QString()),
              "V5u 实例目录名那种写法（1.12.2-Forge）：版本号那一截读到 1.12.2 为止 ⇒ 没有");
        check(supports(QStringLiteral("1.20.1-Fabric 0.16.14"), QString()),
              "V5u 同理（1.20.1-Fabric 0.16.14）⇒ 有");
        // 明知的不精确，写在 NOTE 里也钉在测试里：周快照那个数字是**年份**，会被读成
        // "主版本 13/26"。真机上这类实例盘上都有 releaseTime，走不到这一档。
        check(supports(QStringLiteral("13w41a"), QString()),
              "V5u 13w41a 这种周快照在「没有 releaseTime」这一档会被判成有 —— 这是明知的"
              "不精确（那个数字是年份），真机上它们都带 releaseTime");
        // ③ 两条都问不出来 ⇒ 给。
        check(supports(QString(), QString()),
              "V5u 两格都空 ⇒ **给**（宁可多一页，别静默藏掉用户真需要的一页）");
        check(supports(QStringLiteral("a1.2.6"), QString()),
              "V5u 版本号不像数字 + 没有 releaseTime ⇒ 给（同上：不猜「没有」）");
        // 认不出来的 releaseTime 当作"没有这一格"，别静默当成"早于界线"。
        check(!supports(QStringLiteral("1.12.2"), QStringLiteral("不是时间")),
              "V5u releaseTime 认不出来时当作没有这一格，退到版本号那一档（1.12.2 ⇒ 没有）");
        check(supports(QStringLiteral("1.20.1"), QStringLiteral("不是时间")),
              "V5u 同上：1.20.1 ⇒ 有（认不出来的时间戳不许把结果拽成「没有」）");

        // ④ meta 里那个键的名字只有一个出处（写盘/读盘/验证工程都走它）。
        check(VersionManager::settingsPagesField() == QStringLiteral("settingsPages"),
              "V5u 「哪几页收起来了」那个键的名字走 settingsPagesField() 那一个出处",
              VersionManager::settingsPagesField());
    }

    // --- V5p. 版本号那一格的判据（纯函数，喂**真清单夹具**） ---------------------
    //
    // 规矩：**清单在手时以清单为准，清单没取到时看长相**。两边各自防一种错：
    // ① 清单在手时不看长相 ⇒ 老版本号认不出来。`rd-132211`（Minecraft 最早的开发版之一）
    // **不以数字开头**，而真机上就躺着一份 —— 拿只读探针
    // `mw_check instance-scan <数据目录>` 扫出来的，它导不出整合包没有道理。
    // ② 清单在手时也不放过"长相" ⇒ 反过来那一半：`20w14∞`（正式 id 是 `20w14infinite`）、
    // `26.2hyperland`（私服客户端）长得都像版本号，可 Mojang 没有这两个 id，写进清单
    // 收包的人那边装不回来 —— 也是同一个病（包看上去是好的）。
    // ③ 清单没取到（离线第一趟）⇒ 退回长相那条，宁可放行也不让"离线时一个都导不出去"。
    // ④ 两条都挡着的：`原版ATT2地图（无模组）` 和 `1.20.2-Fabric 0.17.2` 这种明显的假版本号。
    {
        // 真清单夹具：走产品自己那条解析路（`DownloadManager::parseVersionManifest`），
        // 不在这儿另写一份 —— 和 Y 那一节同一个理由。
        QList<MinecraftVersion> manifest;
        {
            QFile fixture(QStringLiteral(REPO_ROOT_PATH) +
                          QStringLiteral("/tests/meta/manifest.json"));
            if (fixture.open(QIODevice::ReadOnly)) {
                DownloadManager downloadManager;
                downloadManager.parseVersionManifest(
                    QJsonDocument::fromJson(fixture.readAll()));
                manifest = downloadManager.getAvailableVersions();
            }
            check(manifest.size() > 500, "V5p 真清单夹具读进来了（下面几条判据都指望它）",
                  QString::number(manifest.size()));
        }

        const QList<MinecraftVersion> empty;
        check(VersionManager::isKnownGameVersion(QStringLiteral("1.20.1"), manifest),
              "V5p 真清单里有的普通版本号认得出（1.20.1）");
        check(VersionManager::isKnownGameVersion(QStringLiteral("rd-132211"), manifest),
              "★ V5p **不以数字开头的老版本号**靠清单认得出（rd-132211）——"
              "光看长相这一档会被整个漏掉");
        check(!VersionManager::isKnownGameVersion(QStringLiteral("20w14∞"), manifest),
              "★ V5p **长得像版本号但 Mojang 没这个 id** 的照旧不许（20w14∞：正式 id 是 "
              "20w14infinite）—— 清单在手时长相不算数");
        check(VersionManager::isKnownGameVersion(QStringLiteral("20w14∞"), empty),
              "★ V5p 但清单没取到时就放行它（离线那一档宁可放行，也不能一个实例都导不出去）");
        check(!VersionManager::isKnownGameVersion(QStringLiteral("原版ATT2地图（无模组）"), manifest),
              "★ V5p 目录名不像版本号、清单里也没有 ⇒ 不许（写进清单是个假版本号）");
        check(!VersionManager::isKnownGameVersion(QStringLiteral("1.20.2-Fabric 0.17.2"), manifest),
              "★ V5p **以数字开头但不是版本号**的目录名同样不许（带空格的这条判据就是为它写的）");

        // 清单没取到那一档：行为**退化**成改前那样（能认的照认，认不出的照不认）。
        check(VersionManager::isKnownGameVersion(QStringLiteral("1.20.1"), empty),
              "V5p 清单没取到时：长得像版本号的照认（1.20.1）");
        check(!VersionManager::isKnownGameVersion(QStringLiteral("rd-132211"), empty),
              "V5p 清单没取到时：rd-132211 认不出来（**只是退化**：调用点会报一句、不写假版本号）");
        check(!VersionManager::isKnownGameVersion(QStringLiteral(""), empty) &&
                  !VersionManager::isKnownGameVersion(QStringLiteral("   "), manifest),
              "V5p 空串 / 纯空白一律不许");
        check(VersionManager::isKnownGameVersion(QStringLiteral(" 1.20.1 "), manifest),
              "V5p 两头带空白的先 trim 再判（那份 json 是能手改的文本）");
        // "清单比实例旧"这一种：新快照不在夹具里 ⇒ 清单在手时**报一句**（重启取了新清单就好），
        // 而不是硬写一个自己都不认识的号进清单。
        check(!VersionManager::isKnownGameVersion(QStringLiteral("26.9-snapshot-9"), manifest) &&
                  VersionManager::isKnownGameVersion(QStringLiteral("26.9-snapshot-9"), empty),
              "V5p 比清单还新的快照：清单在手时不放行（报一句，重启就恢复）、没清单时放行");
    }

    // --- V5n. 后半段：清单进暂存目录 → 打成一个 zip → 改名到目标 -----------------
    //
    // 这一节要的东西比 V5k 多一层：**上面那一趟 plan 是真的跑出来的**（假接口喂一条命中），
    // 然后由 VersionManager 自己写清单、打包。钉的是三件事：①打出来的包我们自己读得回来
    // （名字、清单、overrides 三样都对）；②失败/取消时盘上不留半成品，也不碰用户原来那份；
    // ③覆盖已存在的目标文件这条路是通的（用户点"替换"之后不该报错）。
    {
        const QString work = sandbox + "/export-archive";
        QDir(work).removeRecursively();
        const QString tree = work + "/我的实例";
        QDir().mkpath(tree);
        const QByteArray bytesMatched = QByteArrayLiteral("AAA 认得出");
        const QByteArray bytesUnknown = QByteArrayLiteral("BBB 认不出");
        writeFileAt(tree + "/mods/认得出来.jar", bytesMatched);
        writeFileAt(tree + "/config/配置.toml", bytesUnknown);

        const auto sha1OfBytes = [](const QByteArray &bytes) {
            QCryptographicHash hash(QCryptographicHash::Sha1);
            hash.addData(QByteArrayView(bytes));
            return QString::fromLatin1(hash.result().toHex());
        };
        const auto sha512OfBytes = [](const QByteArray &bytes) {
            QCryptographicHash hash(QCryptographicHash::Sha512);
            hash.addData(QByteArrayView(bytes));
            return QString::fromLatin1(hash.result().toHex());
        };
        const QString sha1Matched = sha1OfBytes(bytesMatched);

        ModpackExport::ExportRequest request;
        request.format = ModpackExport::Format::Modrinth;
        request.instanceDir = tree;
        request.name = QStringLiteral("我的整合包");
        request.version = QStringLiteral("1.2.3");
        request.gameVersion = QStringLiteral("1.21.1");
        request.loaderKey = ModpackImport::fabricLoaderKey();
        request.loaderVersion = QStringLiteral("0.16.14");

        const ModpackExport::ModrinthQuery fakeModrinth =
            [&](const QStringList &sha1s, QHash<QString, ModpackExport::ModrinthHit> *out,
                QString *problemOut) {
                if (problemOut != nullptr) {
                    problemOut->clear();
                }
                ModpackExport::ModrinthHit hit;
                hit.projectId = QStringLiteral("u6dRKJwZ");
                hit.versionNumber = QStringLiteral("1.2.3");
                hit.filename = QStringLiteral("认得出来.jar");
                hit.sha1 = sha1Matched;
                hit.sha512 = sha512OfBytes(bytesMatched);
                hit.url = QStringLiteral(
                    "https://cdn.modrinth.com/data/u6dRKJwZ/versions/abc/认得出来.jar");
                hit.size = bytesMatched.size();
                for (const QString &sha1 : sha1s) {
                    if (sha1 == sha1Matched) {
                        out->insert(sha1Matched, hit);
                    }
                }
                return true;
            };

        const ModpackExport::ExportPlan plan = ModpackExport::buildExportPlan(
            request, fakeModrinth, ModpackExport::CurseForgeQuery());
        check(plan.spec.files.size() == 1 && plan.overrides.size() == 1,
              "V5n 前提：一条认出来了、一条没有（两条路都要走到）",
              QStringLiteral("%1 / %2").arg(plan.spec.files.size()).arg(plan.overrides.size()));

        VersionManager vm;
        const QString target = work + "/打包测试.mrpack";
        const QString problem = vm.writePackArchive(plan, target);
        check(problem.isEmpty(), "V5n 打包成功", problem);
        check(QFile::exists(target), "V5n 目标文件在盘上");
        check(!QFile::exists(target + QStringLiteral(".part")),
              "V5n 改成目标名之后不留 .part");

        // ① 清单读得回来，而且是**我们自己的解析器**读得回来（这一条才是"装得回去"）
        QByteArray manifest;
        QString readError;
        const ZipUtil::EntryStatus status = ZipUtil::readEntry(
            target, ModpackExport::indexFileName(ModpackExport::Format::Modrinth), &manifest,
            &readError, QStringLiteral("读清单"));
        check(status == ZipUtil::EntryStatus::Ok && !manifest.isEmpty(),
              "V5n 清单在 zip 根、名字就是 modrinth.index.json", readError);
        const ModpackImport::Result round = ModpackImport::parseModrinthIndex(manifest);
        check(round.ok, "V5n 打出来的包我们自己解析得回来", round.problem);
        if (round.ok) {
            check(round.pack.name == QStringLiteral("我的整合包") &&
                      round.pack.gameVersion == QStringLiteral("1.21.1") &&
                      round.pack.loaderKey == ModpackImport::fabricLoaderKey() &&
                      round.pack.loaderVersion == QStringLiteral("0.16.14"),
                  "V5n 包名 / 游戏版本 / 加载器那一行都对",
                  QStringLiteral("%1 %2 %3").arg(round.pack.name, round.pack.gameVersion,
                                                 round.pack.loaderKey));
            check(round.pack.files.size() == 1 &&
                      round.pack.files.first().path == QStringLiteral("mods/认得出来.jar"),
                  "V5n 认出来的那条在清单里，路径是它在实例里的相对路径",
                  QString::number(round.pack.files.size()));
        }

        // ② 认不出来的那条原样躺在 overrides 里，字节一字不差
        const QString unpacked = work + "/解开";
        int extracted = 0;
        QString extractError;
        check(ZipUtil::extractSubtrees({target}, unpacked, {QStringLiteral("overrides/")},
                                       &extracted, &extractError),
              "V5n overrides 解得开（导入走的那条路）", extractError);
        QFile backUnknown(unpacked + "/config/配置.toml");
        check(extracted == 1 && backUnknown.open(QIODevice::ReadOnly) &&
                  backUnknown.readAll() == bytesUnknown,
              "V5n 认不出来的那个文件一个字节都没丢", QString::number(extracted));

        // ③ 覆盖一份已经存在的目标：用户点"替换"之后必须成（Windows 上 rename 不覆盖）
        writeFileAt(target, QByteArrayLiteral("这是用户原来的东西"));
        check(QFile::exists(target), "V5n 前提：目标位置上现在有一个别的文件");
        const QString overwrite = vm.writePackArchive(plan, target);
        check(overwrite.isEmpty(), "V5n 覆盖一份已存在的目标也成", overwrite);
        QByteArray replaced;
        check(ZipUtil::readEntry(target, ModpackExport::indexFileName(ModpackExport::Format::Modrinth),
                                 &replaced, &readError, QStringLiteral("读清单")) ==
                  ZipUtil::EntryStatus::Ok &&
                  replaced == manifest,
              "V5n 覆盖之后盘上那份是新包（原来那份已经不在了）");

        // ③b `★ `：目标旁边那两个"陪跑名"**不归我们处置**。
        // `<目标>.part` / `<目标>.bak` 这两个后缀太常见（别的下载器的半截、用户自己手
        // 打的备份），原来那一趟上来就 `QFile::remove()` 它们。现在换一个没人用的名字，
        // 用户那两份必须原样留在盘上（逐字节），而这一趟照样得打成功。
        {
            const QByteArray userPart = QByteArrayLiteral("用户的半截（别的下载器留下的）");
            const QByteArray userBak = QByteArrayLiteral("用户的备份（自己手打的）");
            writeFileAt(target + QStringLiteral(".part"), userPart);
            writeFileAt(target + QStringLiteral(".bak"), userBak);
            const QString beside = vm.writePackArchive(plan, target);
            check(beside.isEmpty(), "V5n 目标旁边已经有一个 .part / .bak 时照样打得成", beside);
            QFile partFile(target + QStringLiteral(".part"));
            QFile bakFile(target + QStringLiteral(".bak"));
            check(partFile.open(QIODevice::ReadOnly) && partFile.readAll() == userPart,
                  "★★ V5n 那个现成的 `.part`（不是我们留下的）**一个字节都没被删/被改**");
            check(bakFile.open(QIODevice::ReadOnly) && bakFile.readAll() == userBak,
                  "★★ V5n 那个现成的 `.bak` 也原样在（别把用户自己的备份当残骸删了）");
            QByteArray besideManifest;
            check(ZipUtil::readEntry(target,
                                     ModpackExport::indexFileName(ModpackExport::Format::Modrinth),
                                     &besideManifest, &readError, QStringLiteral("读清单")) ==
                      ZipUtil::EntryStatus::Ok &&
                      besideManifest == manifest,
                  "V5n 这一趟打出来的还是新包（陪跑名换了，结果没变）");
        }

        // ④ 取消 / 出问题 / 没有实例目录：一律不写盘
        ModpackExport::ExportPlan cancelled = plan;
        cancelled.cancelled = true;
        const QString cancelTarget = work + "/取消的.mrpack";
        check(!vm.writePackArchive(cancelled, cancelTarget).isEmpty() &&
                  !QFile::exists(cancelTarget) &&
                  !QFile::exists(cancelTarget + QStringLiteral(".part")),
              "V5n 用户叫停时什么都不写（连 .part 都不留）");
        ModpackExport::ExportPlan noDir = plan;
        noDir.instanceDir.clear();
        const QString noDirTarget = work + "/没有实例目录.mrpack";
        check(!vm.writePackArchive(noDir, noDirTarget).isEmpty() &&
                  !QFile::exists(noDirTarget),
              "V5n 计划里没有实例目录时如实报错，不打一个缺东西的残包");

        // ⑤ CurseForge 那一档：清单的名字和正文都得跟着换
        ModpackExport::ExportPlan cfPlan = plan;
        cfPlan.spec.format = ModpackExport::Format::CurseForge;
        cfPlan.spec.files.clear();
        ModpackExport::MatchedRef cfRef;
        cfRef.projectId = 394468;
        cfRef.fileId = 5123456;
        cfPlan.spec.refs.append(cfRef);
        const QString cfTarget = work + "/cf包.zip";
        const QString cfProblem = vm.writePackArchive(cfPlan, cfTarget);
        check(cfProblem.isEmpty(), "V5n CurseForge 那一档也打得出来", cfProblem);
        QByteArray cfManifest;
        check(ZipUtil::readEntry(cfTarget, QStringLiteral("manifest.json"), &cfManifest, &readError,
                                 QStringLiteral("读清单")) == ZipUtil::EntryStatus::Ok &&
                  ModpackImport::parseCurseForgeManifest(cfManifest).ok,
              "V5n 那一个包里装的是 manifest.json（不是 modrinth.index.json），自己解析得回来",
              readError);

        QDir(work).removeRecursively();
    }

    // --- V5q. 条目表长到命令行塞不下时也要打得出来 -----------------------------
    //
    // 在真实例上撞的（用户的「海波篮子」「永利制造翻译测试版」都是这个死法）：
    // createZip 原来把整张 `@('条目名','盘上路径')` 表**拼进脚本正文**，而脚本是经
    // `-EncodedCommand` 用**命令行**递给 PowerShell 的 —— Windows 一条命令行只有 32767 个
    // 字符，Base64（×4/3）再加 UTF-16LE（×2）之后脚本只剩一万二千来个字符。133 个模组的
    // 实例光那张表就一万七千多字符 ⇒ CreateProcess 直接失败，用户看到「打包失败」
    // （日志里是「解压没跑起来（Process failed to start: 文件名或扩展名太长。）」，
    // 连主语都是错的 —— 见 runPowershell 的 what 参数）。
    //
    // **小夹具看不见这个**：V5n 那两条条目、T21 那几个文件，拼出来的命令行都短得很。
    // 所以这一节**故意摆两百条**、名字照真实例的长度来，并且顺手把"表本身超过命令行能装
    // 下的量"这条前提也钉住 —— 谁要是把条目数改小，前提那条会先红。
    {
        const QString work = sandbox + "/export-long-table";
        QDir(work).removeRecursively();
        QDir().mkpath(work + "/src");

        QList<ZipUtil::ZipSource> entries;
        int tableChars = 0;
        for (int i = 0; i < 200; ++i) {
            const QString name =
                QStringLiteral("某个名字挺长的模组文件-%1.jar").arg(i, 3, 10, QLatin1Char('0'));
            ZipUtil::ZipSource entry;
            entry.entryName = QStringLiteral("overrides/mods/") + name;
            entry.sourcePath = work + QStringLiteral("/src/") + name;
            writeFileAt(entry.sourcePath, QByteArrayLiteral("bytes-") + QByteArray::number(i));
            entries.append(entry);
            // 老实现拼出来的那一行（`@('名','路径'),`）大约是多长：这条前提算的就是它。
            tableChars += entry.entryName.size() + entry.sourcePath.size() + 7;
        }
        check(entries.size() == 200 && tableChars > 12000,
              "V5q 前提：这张表本身就超过了一条命令行能装下的量（老实现拼进脚本必然起不来）",
              QStringLiteral("%1 条 / %2 字符").arg(entries.size()).arg(tableChars));

        const QString target = work + QStringLiteral("/两百条.mrpack");
        QString problem;
        const bool built = ZipUtil::createZip(target, entries, &problem);
        check(built && QFile::exists(target),
              "★ V5q 两百条条目也打得出来（表留在命令行上时这里是「文件名或扩展名太长」）",
              problem);

        // 真读回来一遍：这是导入那条路用的同一个口，条目数对不上就说明包是残的。
        int extracted = -1;
        QString extractError;
        const bool readBack =
            ZipUtil::extractSubtrees({target}, work + QStringLiteral("/out"),
                                     {QStringLiteral("overrides")}, &extracted, &extractError);
        check(readBack && extracted == 200,
              "★ V5q 两百条一条不落地解得回来（条目名里的中文与长度都没被压坏）",
              QStringLiteral("%1 条 %2").arg(extracted).arg(extractError));

        QByteArray got;
        QString entryError;
        const QString firstName = QStringLiteral("overrides/mods/某个名字挺长的模组文件-007.jar");
        check(ZipUtil::readEntry(target, firstName, &got, &entryError,
                                 QStringLiteral("读条目")) == ZipUtil::EntryStatus::Ok &&
                  got == QByteArrayLiteral("bytes-7"),
              "V5q 随便挑一条，名字与字节都对得上", entryError);

        QDir(work).removeRecursively();
    }

    // --- V5r. 只读探针：把**用户真那个实例**就地打一遍（要环境变量才跑） --------------
    //
    // V5q 是"照真实例的量级编一份"，这一节是"就地拿真的那份"—— 用户那两台机器上的死法
    // 只有真的那份文件清单能复现（133 个模组、真名字、真路径长度）。
    //
    // 默认**不跑**：路径写在别人的机器上不存在，做成常驻断言就是一条必然假红。要跑就：
    // WTFML_PROBE_DIRS=<一行一个实例目录的 UTF-8 清单> WTFML_PROBE_OUT=<产物落点>
    // 名字走**清单文件**而不是环境变量：实例名里全是中文，而 Windows 上 qgetenv 拿的是
    // 本地 8 位（GBK），从环境变量读中文路径必然踩编码。
    //
    // **被探的那棵目录一个字节都不写**（只枚举 + 只读），产物（zip）落进 WTFML_PROBE_OUT。
    {
        const QString probeList = QString::fromLocal8Bit(qgetenv("WTFML_PROBE_DIRS"));
        const QString probeOut = QString::fromLocal8Bit(qgetenv("WTFML_PROBE_OUT"));
        if (!probeList.isEmpty() && !probeOut.isEmpty()) {
            QFile listFile(probeList);
            const bool haveList = listFile.open(QIODevice::ReadOnly);
            if (!haveList) {
                out() << "  PROBE 读不了清单 " << probeList << "\n";
            } else {
                QDir().mkpath(probeOut);
                const QStringList dirs =
                    QString::fromUtf8(listFile.readAll())
                        .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString &line : dirs) {
                    const QString instanceDir = line.trimmed();
                    if (instanceDir.isEmpty()) {
                        continue;
                    }
                    // 反查一律"没成"：这一节量的是**打包**（一个文件都不许丢），不是认不认得出。
                    const ModpackExport::ModrinthQuery deadQuery =
                        [](const QStringList &, QHash<QString, ModpackExport::ModrinthHit> *,
                           QString *) { return false; };
                    ModpackExport::ExportRequest request;
                    request.format = ModpackExport::Format::Modrinth;
                    request.instanceDir = instanceDir;
                    request.name = QDir(instanceDir).dirName();
                    request.version = QStringLiteral("1.0.0");
                    request.gameVersion = QStringLiteral("1.20.1");
                    request.loaderKey = ModpackImport::fabricLoaderKey();
                    request.loaderVersion = QStringLiteral("0.16.14");
                    const ModpackExport::ExportPlan plan = ModpackExport::buildExportPlan(
                        request, deadQuery, ModpackExport::CurseForgeQuery());
                    // 清单要**先落到盘上**（打包那一步只认盘上那份，和生产那条路一样）。
                    QTemporaryDir staging;
                    const QString manifestPath = QDir(staging.path()).absoluteFilePath(
                        ModpackExport::indexFileName(plan.spec.format));
                    writeFileAt(manifestPath, ModpackExport::buildIndexFile(plan.spec));
                    const QList<ZipUtil::ZipSource> entries =
                        ModpackExport::archiveEntries(plan, manifestPath);
                    int tableChars = 0;
                    for (const ZipUtil::ZipSource &entry : entries) {
                        tableChars += entry.entryName.size() + entry.sourcePath.size() + 7;
                    }
                    const QString target =
                        probeOut + QLatin1Char('/') + request.name + QStringLiteral(".mrpack.part");
                    QFile::remove(target);
                    QElapsedTimer timer;
                    timer.start();
                    QString problem;
                    const bool built = ZipUtil::createZip(target, entries, &problem);
                    const qint64 elapsed = timer.elapsed();
                    QFile written(target);
                    const qint64 zipBytes = written.size();
                    out() << "  PROBE " << request.name << " : " << (built ? "OK" : "FAIL")
                          << "  条目 " << entries.size() << " / overrides " << plan.overrides.size()
                          << " / 认出来 " << plan.matched << "  表 " << tableChars << " 字符"
                          << "  " << zipBytes << " 字节  " << elapsed << " ms\n";
                    if (!built) {
                        out() << "        " << problem << "\n";
                        continue;
                    }
                    // 三条抽查：头一条、中间一条、末一条 overrides，名字与字节都要和盘上那份
                    // 一字不差（条目名拼错是**静默**的，只有比字节才看得见）。
                    const QList<qsizetype> spots = {0, plan.overrides.size() / 2,
                                                    plan.overrides.size() - 1};
                    for (const qsizetype index : spots) {
                        if (index < 0 || index >= plan.overrides.size()) {
                            continue;
                        }
                        const ModpackExport::InstanceFile &one = plan.overrides.at(index);
                        const QString entryName = QStringLiteral("overrides/") + one.path;
                        QByteArray got;
                        QString entryError;
                        const ZipUtil::EntryStatus status =
                            ZipUtil::readEntry(target, entryName, &got, &entryError,
                                               QStringLiteral("读条目"));
                        QFile source(instanceDir + QLatin1Char('/') + one.path);
                        const bool sourceOpen = source.open(QIODevice::ReadOnly);
                        const bool same = sourceOpen && source.readAll() == got;
                        out() << "        抽查 " << entryName << " : "
                              << (status == ZipUtil::EntryStatus::Ok ? "读到了" : "读不到")
                              << " / " << (same ? "字节一致" : "字节不一致") << "（" << got.size()
                              << " 字节）\n";
                    }
                }
            }
        }
    }

    // --- V5v. 用户挑的那张背景图是怎么收进数据目录的（主题那两张图） -----------------
    //
    // 这一整节在**临时数据根目录**里跑（`AppPaths::setMinecraftDir()` 一处切换，和
    // runIconCacheTests 那一段同一形状）：下面这些断言全都要"看着 cache/theme 里现在有哪
    // 几个文件"，不隔离就是拿用户盘上已经存着的那张图做实验。跑完必须还原 —— 所以这一节里
    // **一条 return 都不许有**（连"读不到夹具就 return"那种也不行，夹具那一条只在有它时再往下）。
    {
        const QString originalDir = AppPaths::minecraftDir();
        QTemporaryDir themeRoot;
        QTemporaryDir sourceRoot;
        check(themeRoot.isValid() && sourceRoot.isValid(),
              "V5v 两个临时目录建出来了（临时数据根目录 + 放源图的那个）");
        AppPaths::setMinecraftDir(themeRoot.path());
        const QString src = sourceRoot.path();
        const QString slotA = QStringLiteral("homeLeft");
        const QString slotB = QStringLiteral("pageBackground");
        const QString slotC = QStringLiteral("titleBar");
        QString error;

        // 前提：这会儿 cache/theme **还不存在**。下面第一条 store() 能成，才说明那个目录是
        // 它自己按需建的（写盘前那一次 mkpath），不是靠谁先替它摆好了。
        check(!QFileInfo::exists(AppPaths::themeImageDir()),
              "V5v 前提：cache/theme 这会儿还不存在");

        // 源图三张，各有各要证的那一条：带透明的 PNG（必须存成 .png）、不带透明的 JPEG
        // （存成 .jpg）、3000 px 宽的大图（长边必须被缩到 2560 以内）。
        const QString alphaPng = src + QStringLiteral("/带透明.png");
        const QString plainJpg = src + QStringLiteral("/不透明.jpg");
        const QString bigJpg = src + QStringLiteral("/大图.jpg");
        {
            QImage withAlpha(64, 32, QImage::Format_ARGB32);
            withAlpha.fill(QColor(255, 0, 0, 128));
            check(withAlpha.save(alphaPng, "PNG"), "V5v 造出一张带透明的 PNG");
            QImage opaque(64, 32, QImage::Format_RGB32);
            opaque.fill(QColor(0, 0, 255));
            check(opaque.save(plainJpg, "JPG", 92), "V5v 造出一张不带透明的 JPEG");
            QImage big(3000, 1500, QImage::Format_RGB32);
            big.fill(QColor(0, 128, 0));
            check(big.save(bigJpg, "JPG", 92), "V5v 造出一张 3000x1500 的大图");
        }

        // ① 带透明的那张：落点固定 `<数据目录>/cache/theme/<槽位>.png`。
        const QString storedPng = ThemeImage::store(slotA, alphaPng, &error);
        check(!storedPng.isEmpty() && error.isEmpty(), "V5v 带透明的 PNG 收得进来", error);
        check(storedPng == AppPaths::themeImageDir() + QLatin1Char('/') + slotA +
                               QStringLiteral(".png") &&
                  !storedPng.contains(QLatin1Char('\\')),
              "V5v 落点是 <数据目录>/cache/theme/<槽位>.png（正斜杠，槽位名就是文件名）",
              storedPng);
        {
            const QImage back(storedPng);
            check(!back.isNull() && back.size() == QSize(64, 32),
                  "V5v 收进来那一份读得回来、尺寸没变",
                  QStringLiteral("%1x%2").arg(back.width()).arg(back.height()));
        }

        // ② 不带透明的那张存成 .jpg（省下一半以上的地方）。
        const QString storedJpg = ThemeImage::store(slotB, plainJpg, &error);
        check(storedJpg == AppPaths::themeImageDir() + QLatin1Char('/') + slotB +
                               QStringLiteral(".jpg"),
              "V5v 不带透明的存成 .jpg", storedJpg);

        // ③ 名字撒谎的那张：同一段 JPEG 字节换个 .png 的名字挑进来，**后缀仍然跟着内容走**。
        // 这一条是"用户把桌面上的图重命名过"的日常 —— 认名字的话这里就存出一份 .png。
        const QString lyingPng = src + QStringLiteral("/名字是png其实是jpeg.png");
        QFile::copy(plainJpg, lyingPng);
        const QString storedLying = ThemeImage::store(slotB, lyingPng, &error);
        check(storedLying == storedJpg &&
                  !QFileInfo::exists(AppPaths::themeImageDir() + QLatin1Char('/') + slotB +
                                     QStringLiteral(".png")),
              "★ V5v 扩展名跟着内容走：名字叫 .png 的 JPEG 也没留下一份 .png",
              storedLying);

        // ④ 大图 + "一个槽位只有一份"：换过扩展名的那份旧图必须被删掉，不能攒着。
        const QString storedBig = ThemeImage::store(slotA, bigJpg, &error);
        check(!storedBig.isEmpty(), "V5v 大图也收得进来", error);
        {
            const QImage back(storedBig);
            check(back.size() == QSize(2560, 1280),
                  "★ V5v 长边被缩到 2560（3000 原样就是 48 MB 位图换一块 1200 px 宽的底）",
                  QStringLiteral("%1x%2").arg(back.width()).arg(back.height()));
        }
        check(!QFileInfo::exists(AppPaths::themeImageDir() + QLatin1Char('/') + slotA +
                                 QStringLiteral(".png")) &&
                  QFileInfo::exists(storedBig),
              "★ V5v 同一个槽位换图之后盘上只剩一份（先是 .png，这回是 .jpg，旧那份删掉了）",
              storedBig);

        // ⑤ 挑了一张坏的（一段文字当图片）：如实拒绝，而且**原来那张一个字节都不许动**
        // —— 用户手滑挑错一次，不该顺手把他现在用的那张也弄没。
        const QString notImage = src + QStringLiteral("/不是图片.png");
        writeFileAt(notImage, QByteArrayLiteral("这不是一张图片，只是一段文字。"));
        QByteArray keepBytes;
        {
            QFile keeper(storedBig);
            if (keeper.open(QIODevice::ReadOnly)) {
                keepBytes = keeper.readAll();
            }
        }
        check(!keepBytes.isEmpty(), "V5v 前提：记下了现在那一份的字节");
        const QString broken = ThemeImage::store(slotA, notImage, &error);
        check(broken.isEmpty() && error.contains(QStringLiteral("不是一张能打开的图片")),
              "V5v 一段文字当成图挑进来时如实拒绝（给用户的话里说清了是哪一张）", error);
        QByteArray afterBytes;
        {
            QFile keeper(storedBig);
            if (keeper.open(QIODevice::ReadOnly)) {
                afterBytes = keeper.readAll();
            }
        }
        check(afterBytes == keepBytes,
              "★ V5v 挑坏图那一发没能动到原来那张（字节一模一样）");

        // ⑥ 太大：**只 resize、内容一个字节都不写**（全是 0 也够了 —— 这一条判的是文件大小，
        // 而且判在解码之前，所以它挡的正是"手滑选了个视频/压缩包"）。
        const QString huge = src + QStringLiteral("/太大.png");
        {
            QFile file(huge);
            if (file.open(QIODevice::WriteOnly)) {
                file.resize(ThemeImage::kMaxSourceBytes + 1);
            }
        }
        check(QFileInfo(huge).size() > ThemeImage::kMaxSourceBytes,
              "V5v 前提：造出一个超过 64 MB 的源文件",
              QStringLiteral("%1 字节").arg(QFileInfo(huge).size()));
        const QString tooBig = ThemeImage::store(slotA, huge, &error);
        check(tooBig.isEmpty() && error.contains(QStringLiteral("太大")) &&
                  QFileInfo::exists(storedBig),
              "V5v 超过 64 MB 的源文件当场拒绝（拒绝的那一发也没动原来那张）", error);

        // ⑦ 源文件根本不在（用户挑完就把图删了 / 挪走了）。
        const QString missing =
            ThemeImage::store(slotA, src + QStringLiteral("/根本没这个文件.png"), &error);
        check(missing.isEmpty() && !error.isEmpty(), "V5v 源文件不在时如实拒绝", error);

        // ⑧ 槽位名不安全：这是我们自己那张表给的字符串，这一道防的是"以后有人拿用户输入喂进来"。
        const QString escape = ThemeImage::store(QStringLiteral("../跑出去"), alphaPng, &error);
        check(escape.isEmpty() && !error.isEmpty(),
              "V5v 槽位名里带 ../ 时拒绝（不拿它去拼路径）", error);
        check(!QFileInfo::exists(themeRoot.path() + QStringLiteral("/cache/跑出去.png")) &&
                  !QFileInfo::exists(themeRoot.path() + QStringLiteral("/跑出去.png")),
              "★ V5v 那一发一个文件都没写出去（槽位名要是被当路径用，它就跑出 cache/theme 了）");

        // ⑨ 这份 Qt 自己解不了的那种（**webp**）：挑图那个对话框的筛选里写着 `*.webp`，
        // 它就得真的打得开 —— 走的是 `ImageDecode`（WIC）那第二条路。夹具就是 ZC 段
        // 用的同一份（从 Modrinth CDN 上真取下来的 96px 缩略图）。
        QByteArray webpBytes;
        {
            QFile fixture(QStringLiteral(REPO_ROOT_PATH) + "/tests/meta/icon-sample.webp");
            if (fixture.open(QIODevice::ReadOnly)) {
                webpBytes = fixture.readAll();
            }
        }
        check(!webpBytes.isEmpty(), "V5v 读到了 webp 夹具（meta/icon-sample.webp）");
        if (!webpBytes.isEmpty()) {
            const QString webpPath = src + QStringLiteral("/浏览器存下来的.webp");
            writeFileAt(webpPath, webpBytes);
            const QString storedWebp = ThemeImage::store(slotC, webpPath, &error);
            // 判据跟着**这条路真能不能解**走，不跟 `webpAvailable()` 那个
            // 枚举结果走 —— Windows Server 镜像上它会说有、真解却失败（见 ZC2 那段）。
            // ThemeImage 自己先让 Qt 试、再走 `ImageDecode::decode`，这里用的正是同一个
            // 判据（同一份夹具、同一条路）。
            if (!ImageDecode::decode(webpBytes).isNull()) {
                check(!storedWebp.isEmpty() && QFileInfo::exists(storedWebp),
                      "★ V5v webp 也收得进来（Qt 认不得，退给 WIC 那条路）", error);
                const QImage back(storedWebp);
                check(back.size() == QSize(96, 96),
                      "V5v webp 收进来的那一份读得回来、尺寸对",
                      QStringLiteral("%1x%2").arg(back.width()).arg(back.height()));
            } else {
                // 这台机器没有那个编解码器（Win10 上多半没有，见 ImageDecode.hpp）：如实拒绝
                // —— 那时候**硬解出个什么来**才是最糟的（存进去一张残图，屏幕上还说是好的）。
                check(storedWebp.isEmpty() && !error.isEmpty(),
                      "V5v 这台机器解不了 webp 时如实拒绝", error);
            }
        }

        // ⑩ remove()：删点名的那一个、不碰别的槽位、没有就什么都不做（不崩、不乱删）。
        ThemeImage::remove(slotA);
        check(!QFileInfo::exists(storedBig), "V5v remove() 把点名那个槽位的那一份删掉了");
        check(QFileInfo::exists(storedJpg), "V5v remove() 不碰别的槽位（另一张还在）");
        ThemeImage::remove(slotA);
        ThemeImage::remove(QStringLiteral("../跑出去"));
        check(QFileInfo::exists(storedJpg),
              "V5v 再删一次 / 槽位名不安全的那一发都什么都不做（盘上一个文件没少）");

        ThemeImage::remove(slotB);
        ThemeImage::remove(slotC);
        check(QDir(AppPaths::themeImageDir()).entryList(QDir::Files).isEmpty(),
              "V5v 三个槽位都清干净了，cache/theme 里一份图都不剩");

        AppPaths::setMinecraftDir(originalDir);
        check(AppPaths::minecraftDir() == originalDir, "V5v 数据根目录还原回去了");
    }

    // --- V6. 真清单（779 MB 那个 ATT2 的 modrinth.index.json） -----------------
    {
        const QString metaDir = QStringLiteral(REPO_ROOT_PATH) + "/tests/meta";
        QFile fixture(metaDir + "/modrinth-index-att2.json");
        const bool haveFixture = fixture.open(QIODevice::ReadOnly);
        check(haveFixture, "V6 读到了真清单 fixture（meta/modrinth-index-att2.json）");
        if (!haveFixture) {
            return;
        }
        const QByteArray json = fixture.readAll();
        fixture.close();

        const ModpackImport::Result r = ModpackImport::parseModrinthIndex(json);
        check(r.ok, "V6 真的那一份清单解析通过", r.problem);
        if (!r.ok) {
            return;
        }
        const ModpackImport::Pack &pack = r.pack;
        check(pack.gameVersion == QStringLiteral("1.20.2") &&
                  pack.loaderKey == QStringLiteral("fabric-loader") &&
                  pack.loaderVersion == QStringLiteral("0.16.14"),
              "V6 依赖表读对了：1.20.2 + fabric-loader 0.16.14",
              QStringLiteral("%1 / %2 %3").arg(pack.gameVersion, pack.loaderKey,
                                               pack.loaderVersion));
        check(pack.files.size() >= 100 && pack.filesSkipped == 0,
              "V6 一百多条文件全在，一条都没被误判成该跳过",
              QStringLiteral("留下 %1、跳过 %2").arg(pack.files.size()).arg(pack.filesSkipped));

        // 这一条是这一节里最值钱的：真实的 150 条路径，逐条过判据。判据要是严错了一点
        // （比如把"名字里带点"当非法），真包会被整包拒绝 —— 而那个症状是"用户选了自己
        // 的整合包，启动器说这个包的文件清单不安全"。
        int unsafe = 0;
        QString firstUnsafe;
        int longest = 0;
        for (const ModpackImport::PackFile &file : pack.files) {
            if (!ModpackImport::isSafeRelativePath(file.path)) {
                ++unsafe;
                if (firstUnsafe.isEmpty()) {
                    firstUnsafe = file.path;
                }
            }
            if (file.path.size() > longest) {
                longest = static_cast<int>(file.path.size());
            }
        }
        check(unsafe == 0, "V6 真清单里 150 条路径一条都没被判成不安全",
              unsafe == 0 ? QStringLiteral("最长的 %1 个字符").arg(longest)
                          : QStringLiteral("%1 条，第一条是 %2").arg(unsafe).arg(firstUnsafe));

        // 界面预填的那个名字（包名推出来的）必须过得了数据层的校验，否则用户一进
        // 输入框就撞一句报错，而他什么都没输。
        const QString suggested = ModpackImport::suggestInstanceName(pack.name);
        check(InstanceFiles::validateRenameTarget(suggested, QString()).isEmpty(),
              "V6 从真包名推出来的实例名过得了校验",
              QStringLiteral("「%1」→「%2」").arg(pack.name, suggested));
    }
}

