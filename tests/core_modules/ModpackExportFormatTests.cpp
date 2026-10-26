#include "Support.hpp"
#include "ModpackDomainChecks.hpp"
#include <QUuid>

void runModpackExportFormatTests(const QString &sandbox) {
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

}
