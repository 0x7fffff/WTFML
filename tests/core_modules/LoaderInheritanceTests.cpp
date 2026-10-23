#include "Support.hpp"

void runLoaderInheritanceTests(const QString &sandbox) {
    section("U. 加载器版本的继承（inheritsFrom 合并 / 主类 / maven 库）");

    const QString metaDir = QStringLiteral(REPO_ROOT_PATH) + "/tests/meta";
    const QJsonObject vanilla = loadJson(metaDir + "/1.19.json");
    const QJsonObject loader = loadJson(metaDir + "/fabric-loader-0.19.5-1.19.json");
    check(!vanilla.isEmpty() && !loader.isEmpty(), "两份 fixture 都读到了");
    if (vanilla.isEmpty() || loader.isEmpty()) {
        return;
    }

    const QString loaderId = loader[QStringLiteral("id")].toString();
    const QJsonArray vanillaLibs = vanilla[QStringLiteral("libraries")].toArray();
    const QJsonArray loaderLibs = loader[QStringLiteral("libraries")].toArray();

    // --- U1. 先钉住"这份 profile 确实是薄的" -------------------------------------
    // 这几条同时是后面所有断言的前提：合并要是没做事，下面每一条都会红。
    check(VersionInheritance::parentIdOf(loader) == vanilla[QStringLiteral("id")].toString(),
          "U1 Fabric profile 的 inheritsFrom 指向原版",
          VersionInheritance::parentIdOf(loader));
    check(VersionInheritance::parentIdOf(vanilla).isEmpty(), "U1 原版的 json 没有 inheritsFrom");
    check(!loader.contains(QStringLiteral("downloads")) &&
              !loader.contains(QStringLiteral("assetIndex")) &&
              !loader.contains(QStringLiteral("javaVersion")) &&
              !loader.contains(QStringLiteral("logging")),
          "U1 Fabric profile 里没有 jar/assetIndex/javaVersion/logging（都是继承来的）");
    check(!loader[QStringLiteral("mainClass")].toString().isEmpty() &&
              loader[QStringLiteral("mainClass")].toString() !=
                  vanilla[QStringLiteral("mainClass")].toString(),
          "U1 主类跟原版不一样（否则「主类从 json 读」这件事根本验不出来）");
    check(loaderLibs.size() == 8, "U1 Fabric profile 有 8 个库", QString::number(loaderLibs.size()));

    int mavenStyle = 0;
    for (const QJsonValue &v : loaderLibs) {
        const QJsonObject lib = v.toObject();
        if (lib[QStringLiteral("downloads")].toObject().isEmpty() &&
            !lib[QStringLiteral("name")].toString().isEmpty() &&
            !lib[QStringLiteral("url")].toString().isEmpty()) {
            ++mavenStyle;
        }
    }
    check(mavenStyle == loaderLibs.size(), "U1 这 8 个库全是「只有 name+url」的 maven 风格",
          QString::number(mavenStyle));

    // --- U2. 纯合并 -------------------------------------------------------------
    section("U2. VersionInheritance::merge（父在前、子覆盖）");
    const QJsonObject merged = VersionInheritance::merge(loader, vanilla);

    check(merged[QStringLiteral("id")].toString() == loaderId, "U2 id 用子的",
          merged[QStringLiteral("id")].toString());
    check(!merged.contains(QStringLiteral("inheritsFrom")) &&
              VersionInheritance::parentIdOf(merged).isEmpty(),
          "U2 结果里没有 inheritsFrom（合完就不该再往上指，否则链会重复走）");
    check(merged[QStringLiteral("mainClass")].toString() ==
              loader[QStringLiteral("mainClass")].toString(),
          "U2 mainClass 用子的（KnotClient）",
          merged[QStringLiteral("mainClass")].toString());
    check(flatJson(merged[QStringLiteral("assetIndex")]) ==
              flatJson(vanilla[QStringLiteral("assetIndex")]),
          "U2 assetIndex 从父版本继承");
    check(flatJson(merged[QStringLiteral("javaVersion")]) ==
              flatJson(vanilla[QStringLiteral("javaVersion")]),
          "U2 javaVersion 从父版本继承（决定拿哪个 Java 跑）");
    check(flatJson(merged[QStringLiteral("downloads")]) ==
              flatJson(vanilla[QStringLiteral("downloads")]),
          "U2 downloads 从父版本继承（客户端 jar 的地址/哈希）");
    check(flatJson(merged[QStringLiteral("logging")]) ==
              flatJson(vanilla[QStringLiteral("logging")]),
          "U2 logging 从父版本继承（少了它游戏起不来）");

    // 这条是上面那条的另一半：子**声明了**这个键，哪怕值是空对象也是子赢（不回退到父的）。
    // 真数据里就有这一号 —— Forge 装出来的 profile 写着 `"logging": {}`，用意正是把原版
    // 那份 log4j 配置清掉，官方启动器的合并同样是"子赢"。只有 null / 根本没有这个键才
    // 算"没写"。**判据不能写成"logging 必须非空"**：真装出来就是一个空对象。
    {
        QJsonObject childClearingLogging = loader;
        childClearingLogging.insert(QStringLiteral("logging"), QJsonObject());
        const QJsonObject mergedCleared =
            VersionInheritance::merge(childClearingLogging, vanilla);
        check(mergedCleared.contains(QStringLiteral("logging")) &&
                  flatJson(mergedCleared[QStringLiteral("logging")]) ==
                      flatJson(QJsonObject()),
              "U2 子写了空对象也算声明了（Forge 的 logging:{} 就是这么清掉原版那份的）",
              flatJson(mergedCleared[QStringLiteral("logging")]));
    }

    // 上面那条的前提是"父那份自己没有 inheritsFrom"（真数据里父就是原版，它不指别人）。
    // 父自己也指着上一级那种（子 → 父 → 祖父）才是会漏的那个形状：`out = parent` 会把
    // 父的 inheritsFrom 一起抄过来，于是合出来的这份 json 还指着祖父 —— 读它的人
    // （启动参数展开、classpath 拼接）会再去找祖父合一遍，多走一层。合完就是**什么都不缺**
    // 的一份，那个字段一条都不该留。
    {
        QJsonObject chainedParent = vanilla;
        chainedParent.insert(QStringLiteral("inheritsFrom"), QStringLiteral("1.19.2"));
        const QJsonObject mergedChain = VersionInheritance::merge(loader, chainedParent);
        check(!mergedChain.contains(QStringLiteral("inheritsFrom")) &&
                  VersionInheritance::parentIdOf(mergedChain).isEmpty(),
              "U2 父那一份自己也写着 inheritsFrom 时，结果里同样一条都不留",
              VersionInheritance::parentIdOf(mergedChain));
    }

    // 库：父的全部在，子的全在，按 name 去重、子赢、且**留在父原来的位置上**
    {
        QSet<QString> names;
        for (const QJsonValue &v : vanillaLibs) {
            names.insert(v.toObject()[QStringLiteral("name")].toString());
        }
        const int parentOnly = names.size();
        for (const QJsonValue &v : loaderLibs) {
            names.insert(v.toObject()[QStringLiteral("name")].toString());
        }
        const QJsonArray mergedLibs = merged[QStringLiteral("libraries")].toArray();
        check(mergedLibs.size() == names.size(), "U2 库是两边名字的并集（没有重复）",
              QString("%1 vs %2").arg(mergedLibs.size()).arg(names.size()));

        // 前 parentOnly 条逐条等于父的（原版原顺序，一位不差）
        bool prefixSame = mergedLibs.size() >= vanillaLibs.size();
        for (int i = 0; prefixSame && i < vanillaLibs.size(); ++i) {
            prefixSame = flatJson(mergedLibs.at(i)) == flatJson(vanillaLibs.at(i));
        }
        check(prefixSame, "U2 父的库原顺序铺在前面");

        bool childPresent = true;
        for (const QJsonValue &v : loaderLibs) {
            const QString name = v.toObject()[QStringLiteral("name")].toString();
            bool found = false;
            for (const QJsonValue &m : mergedLibs) {
                if (m.toObject()[QStringLiteral("name")].toString() == name) {
                    found = flatJson(m) == flatJson(v);  // 子赢：值也是子的
                    break;
                }
            }
            if (!found) {
                childPresent = false;
                out() << "        缺/被顶掉: " << name << "\n";
            }
        }
        check(childPresent, "U2 子的 8 个库全在，且值用的是子的那份");
        check(parentOnly == vanillaLibs.size(), "U2 原版 1.19 的 88 个库名字本来就互不相同",
              QString::number(parentOnly));
    }

    // 覆盖语义的合成用例：同名库要"子赢 + 留在父的位置上"，不能挪到队尾
    {
        const QJsonObject parentJson = QJsonDocument::fromJson(R"JSON({
            "id": "P", "mainClass": "p.Main",
            "libraries": [
                {"name": "g:same:1.0", "downloads": {"artifact": {"path": "old/same.jar"}}},
                {"name": "g:onlyParent:1.0", "downloads": {"artifact": {"path": "old/only.jar"}}}
            ]
        })JSON").object();
        // 去重是**按 name 全串**做的（Mojang 那边也是：name 就是坐标，带版本号），
        // 所以要验"覆盖"就必须两边写成一模一样的 name。
        const QJsonObject childJson = QJsonDocument::fromJson(R"JSON({
            "id": "C", "inheritsFrom": "P", "mainClass": "c.Main",
            "libraries": [
                {"name": "g:same:1.0", "url": "https://example.invalid/", "sha1": "beef"},
                {"name": "g:onlyChild:1.0", "url": "https://example.invalid/", "sha1": "cafe"}
            ]
        })JSON").object();
        const QJsonArray libs = VersionInheritance::merge(childJson, parentJson)
                                    [QStringLiteral("libraries")].toArray();
        check(libs.size() == 3, "U2 同名库被顶掉、不同名的都在", QString::number(libs.size()));
        check(libs.at(0).toObject()[QStringLiteral("sha1")].toString() == QStringLiteral("beef"),
              "U2 被覆盖的库留在**父原来的位置**、值是子的（挪到队尾会改变 classpath 里谁盖住谁）",
              libs.at(0).toObject()[QStringLiteral("sha1")].toString());
        check(libs.at(1).toObject()[QStringLiteral("name")].toString() ==
                  QStringLiteral("g:onlyParent:1.0"),
              "U2 父独有的库留在原位置");
        check(libs.at(2).toObject()[QStringLiteral("name")].toString() ==
                  QStringLiteral("g:onlyChild:1.0"),
              "U2 子独有的库接在最后");
    }

    // arguments：jvm/game 都是"父在前、子在后"，而且**不拆分**那一整条 argv
    {
        const QJsonArray parentJvm = vanilla[QStringLiteral("arguments")]
                                         .toObject()[QStringLiteral("jvm")].toArray();
        const QJsonArray childJvm = loader[QStringLiteral("arguments")]
                                        .toObject()[QStringLiteral("jvm")].toArray();
        const QJsonArray jvm = merged[QStringLiteral("arguments")]
                                   .toObject()[QStringLiteral("jvm")].toArray();
        check(jvm.size() == parentJvm.size() + childJvm.size(),
              "U2 arguments.jvm 是两边拼起来的（父在前）",
              QString::number(jvm.size()));
        bool jvmPrefix = jvm.size() >= parentJvm.size();
        for (int i = 0; jvmPrefix && i < parentJvm.size(); ++i) {
            jvmPrefix = flatJson(jvm.at(i)) == flatJson(parentJvm.at(i));
        }
        check(jvmPrefix, "U2 父的 jvm 参数在前面（手写的规则对象也原样带着）");
        check(jvm.size() > 0 && jvm.last().toString() == childJvm.first().toString() &&
                  jvm.last().toString().contains(QStringLiteral("-DFabricMcEmu=")),
              "U2 子的 -DFabricMcEmu 那一行在**最后**（要盖住原版主类就得后写）",
              jvm.last().toString());

        const QJsonArray game = merged[QStringLiteral("arguments")]
                                    .toObject()[QStringLiteral("game")].toArray();
        check(flatJson(game) == flatJson(vanilla[QStringLiteral("arguments")]
                                             .toObject()[QStringLiteral("game")]),
              "U2 arguments.game 原样继承（子的 game 是空数组，不能把父的顶掉）",
              QString::number(game.size()));
    }

    // complianceLevel / minimumLauncherVersion 取大的
    {
        const int compliance = qMax(vanilla[QStringLiteral("complianceLevel")].toInt(),
                                    loader[QStringLiteral("complianceLevel")].toInt());
        const int minLauncher = qMax(vanilla[QStringLiteral("minimumLauncherVersion")].toInt(),
                                     loader[QStringLiteral("minimumLauncherVersion")].toInt());
        check(merged[QStringLiteral("complianceLevel")].toInt() == compliance,
              "U2 complianceLevel 取大的",
              QString::number(merged[QStringLiteral("complianceLevel")].toInt()));
        check(merged[QStringLiteral("minimumLauncherVersion")].toInt() == minLauncher,
              "U2 minimumLauncherVersion 取大的",
              QString::number(merged[QStringLiteral("minimumLauncherVersion")].toInt()));
    }
    check(VersionInheritance::merge(loader, vanilla).size() == merged.size(),
          "U2 合并是纯函数（同样的输入再来一次，结果一样大）");

    // --- U3. 合并后的 json 拿去启动 -------------------------------------------------
    section("U3. 合并后的 json 走启动路径（主类 / classpath）");
    {
        GlobalJavaSettings s;
        LauncherManager launcher;
        const QStringList args = launcher.generateJvmArguments(
            QString(), loaderId, QStringLiteral("1"), merged, s, true, 17);

        const int cpIndex = args.indexOf(QStringLiteral("-cp"));
        const QStringList cp = cpIndex >= 0
                                   ? args.at(cpIndex + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts)
                                   : QStringList();
        check(cpIndex >= 0 && cpIndex + 2 < args.size(), "U3 参数里有 -cp 和主类");
        check(cpIndex + 2 < args.size() &&
                  args.at(cpIndex + 2) == loader[QStringLiteral("mainClass")].toString(),
              "U3 主类是 KnotClient（从 json 读，不是写死的 net.minecraft.client.main.Main）",
              cpIndex + 2 < args.size() ? args.at(cpIndex + 2) : QString());

        bool emuKept = false;
        for (const QString &a : args) {
            if (a == QStringLiteral("-DFabricMcEmu= net.minecraft.client.main.Main ")) {
                emuKept = true;
            }
        }
        check(emuKept, "U3 -DFabricMcEmu 那一行是**一个** argv 元素（带空格也没被拆开）");

        // 加载器的 8 个库必须都在 classpath 上（maven 风格，没有 downloads.artifact）
        int libsOnCp = 0;
        for (const QJsonValue &v : loaderLibs) {
            const QJsonObject lib = v.toObject();
            const QString path = LibraryRules::mavenPathForName(lib[QStringLiteral("name")].toString());
            const QString want = absoluteMcPath(QStringLiteral("libraries/%1").arg(path));
            if (!path.isEmpty() && cp.contains(want)) {
                ++libsOnCp;
            } else {
                out() << "        不在 classpath 上: " << lib[QStringLiteral("name")].toString()
                      << " -> " << path << "\n";
            }
        }
        check(libsOnCp == loaderLibs.size(),
              "U3 加载器的 8 个库都在 classpath 上（不然 NoClassDefFoundError）",
              QString("%1/%2").arg(libsOnCp).arg(loaderLibs.size()));

        // 原版的库也必须在（加载器是"套在"原版上的，两边都要）
        int vanillaOnCp = 0;
        for (const QJsonValue &v : vanillaLibs) {
            const QJsonObject lib = v.toObject();
            if (!LibraryRules::libraryAllowed(lib)) {
                continue;
            }
            const QString path = LibraryRules::artifactFor(lib).path;
            if (!path.isEmpty() &&
                cp.contains(absoluteMcPath(QStringLiteral("libraries/%1").arg(path)))) {
                ++vanillaOnCp;
            }
        }
        check(vanillaOnCp > 50, "U3 原版的库也照样在 classpath 上（两边都不少）",
              QString::number(vanillaOnCp));

        // 客户端 jar 的路径：加载器目录里没有 jar，所以这里拼出来的是**原版那个目录**
        // 底下那个 jar —— 这一段是 launch 模式的真正风险点（找不到就 "Could not find
        // or load main class"）。
        check(!cp.isEmpty() &&
                  cp.first() == absoluteMcPath(InstanceFiles::jarRelativePath(loaderId)),
              "U3 classpath 第一条走 InstanceFiles::jarRelativePath（绝对化之后）",
              cp.isEmpty() ? QString() : cp.first());
    }

    // --- U4. DownloadManager 认得出 maven 风格的库 ---------------------------------
    section("U4. maven 风格的库（只有 name + url）");
    {
        DownloadManager dm;
        const QList<Library> picked = dm.extractLibraries(loader);
        check(picked.size() == loaderLibs.size(), "U4 8 个库一个不少地被选中",
              QString::number(picked.size()));

        int withUrl = 0;
        int withSha1 = 0;
        bool insideLibraries = true;
        for (const Library &lib : picked) {
            if (lib.url.startsWith(QStringLiteral("https://maven.fabricmc.net/")) &&
                lib.url.endsWith(QLatin1Char('/') + QFileInfo(lib.path).fileName())) {
                ++withUrl;
            }
            if (!lib.sha1.isEmpty() && lib.size > 0) {
                ++withSha1;
            }
            if (lib.path.contains(QStringLiteral("..")) || lib.path.startsWith(QLatin1Char('/'))) {
                insideLibraries = false;
            }
        }
        check(withUrl == picked.size(), "U4 每条的地址都是 maven 仓库 + 拼出来的路径",
              QString::number(withUrl));
        // 最后两个（intermediary / fabric-loader）本来就没有 sha1，所以只能断言"给了的都在"
        check(withSha1 == 6, "U4 给了 sha1 的 6 个都带上了校验信息（能校验的还是校验）",
              QString::number(withSha1));
        check(insideLibraries, "U4 路径都在 libraries/ 里面（没有 ../ 越界）");

        // 每条的路径都必须等于"用它自己的 name 现算一遍"的结果
        bool mavenPathOk = true;
        for (const Library &lib : picked) {
            if (lib.path != LibraryRules::mavenPathForName(lib.name)) {
                mavenPathOk = false;
                out() << "        路径不对: " << lib.name << " -> " << lib.path << "\n";
            }
        }
        check(mavenPathOk, "U4 每条的 path 都是按它自己的坐标拼出来的");
        bool hasAsm = false;
        for (const Library &lib : picked) {
            if (lib.path == QStringLiteral("org/ow2/asm/asm/9.10.1/asm-9.10.1.jar")) {
                hasAsm = true;
            }
        }
        check(hasAsm, "U4 坐标拼出来的路径正确（org.ow2.asm:asm:9.10.1 那个）");
    }

    // 坐标 -> 路径的纯函数真值表（含"脏名字一律拒绝"）
    {
        // 三段（没有 classifier）是**最常见**的形状，7/8 个 Fabric 坐标都是它 ——
        // 白名单循环不能把"空的 classifier"当成脏段，否则每一个不带 classifier 的坐标
        // 都解析失败（库全被静默丢掉）。
        check(LibraryRules::mavenPathForName(QStringLiteral("net.fabricmc:fabric-loader:0.19.5")) ==
                  QStringLiteral("net/fabricmc/fabric-loader/0.19.5/fabric-loader-0.19.5.jar"),
              "U4 坐标 -> maven 路径（三段、没有 classifier）");
        check(LibraryRules::mavenPathForName(
                  QStringLiteral("org.lwjgl:lwjgl:3.3.1:natives-windows")) ==
                  QStringLiteral("org/lwjgl/lwjgl/3.3.1/lwjgl-3.3.1-natives-windows.jar"),
              "U4 带 classifier 的坐标");
        check(LibraryRules::mavenPathForName(QStringLiteral("net.fabricmc:intermediary:1.19@zip")) ==
                  QStringLiteral("net/fabricmc/intermediary/1.19/intermediary-1.19.zip"),
              "U4 @扩展名照收");
        // 版本号里的 `+` 是 Fabric 真在用的（sponge-mixin），白名单漏了它这个库就没了
        check(LibraryRules::mavenPathForName(
                  QStringLiteral("net.fabricmc:sponge-mixin:0.17.4+mixin.0.8.7")) ==
                  QStringLiteral("net/fabricmc/sponge-mixin/0.17.4+mixin.0.8.7/"
                                 "sponge-mixin-0.17.4+mixin.0.8.7.jar"),
              "U4 版本号里的 + 照收（Maven 允许，Fabric 真的在用）");
        check(LibraryRules::mavenPathForName(QStringLiteral("../../evil:pwn:1.0")).isEmpty(),
              "U4 group 里带 ../ 的坐标直接拒绝（拼出来会写到 libraries/ 外面去）");
        check(LibraryRules::mavenPathForName(QStringLiteral("g:a")).isEmpty(),
              "U4 段数不够的坐标拒绝");
        check(LibraryRules::mavenPathForName(QStringLiteral("g:a:1:bad name")).isEmpty(),
              "U4 段里有空格（白名单外的字符）也拒绝");
        check(LibraryRules::mavenPathForName(QStringLiteral("g:a:1:2:3")) ==
                  QStringLiteral("g/a/1/a-1-2.jar"),
              "U4 第 5 段往后当作不存在（maven 没有那种形式）");

        // 坏名字不能变成"幽灵下载条目"
        const QJsonObject evil = QJsonDocument::fromJson(R"JSON({
            "libraries": [
                {"name": "../../evil:pwn:1.0", "url": "https://example.invalid/"},
                {"name": "g:good:1.0", "url": "https://example.invalid/"}
            ]
        })JSON").object();
        DownloadManager dm;
        const QList<Library> picked = dm.extractLibraries(evil);
        check(picked.size() == 1 && picked.first().name == QStringLiteral("g:good:1.0"),
              "U4 坏坐标被丢掉、好坐标照收", QString::number(picked.size()));
    }

    // --- U5. 真磁盘：VersionManager 顺着链合并，坏链要说得清 ------------------------
    section("U5. 真磁盘上的继承链（VersionManager / InstanceFiles）");
    {
        const QString versionsRoot = sandbox + "/versions";
        const QString parentId = QStringLiteral("U段-父");
        const QString childId = QStringLiteral("U段-加载器");
        const QString orphanId = QStringLiteral("U段-孤儿");
        const QString ringA = QStringLiteral("U段-环A");
        const QString ringB = QStringLiteral("U段-环B");
        const QString missingId = QStringLiteral("U段-没有这个");

        const auto writeJson = [&](const QString &id, const QJsonObject &json) {
            QDir().mkpath(versionsRoot + "/" + id);
            writeFileAt(versionsRoot + "/" + id + "/" + id + ".json",
                        QJsonDocument(json).toJson(QJsonDocument::Indented));
        };

        // 父版本：目录里有 jar，而且**名字不是 id**（外部改过名的实例）
        QDir().mkpath(versionsRoot + "/" + parentId);
        writeJson(parentId, vanilla);
        {
            QFile jar(versionsRoot + "/" + parentId + "/真名.jar");
            if (jar.open(QIODevice::WriteOnly)) {
                jar.write("dummy");
                jar.close();
            }
        }

        // 加载器版本：目录里**只有一份 json**，inheritsFrom 指向那个改过名的父版本
        QJsonObject child = loader;
        child[QStringLiteral("id")] = childId;
        child[QStringLiteral("inheritsFrom")] = parentId;
        writeJson(childId, child);

        VersionManager vm;
        const QJsonObject diskMerged = vm.getVersionJson(childId);
        check(diskMerged[QStringLiteral("mainClass")].toString() ==
                  loader[QStringLiteral("mainClass")].toString(),
              "U5 getVersionJson 返回的是合并结果（主类是 KnotClient）",
              diskMerged[QStringLiteral("mainClass")].toString());
        check(diskMerged[QStringLiteral("libraries")].toArray().size() ==
                  vanillaLibs.size() + loaderLibs.size(),
              "U5 合并结果里两边库都在",
              QString::number(diskMerged[QStringLiteral("libraries")].toArray().size()));
        check(flatJson(diskMerged[QStringLiteral("assetIndex")]) ==
                  flatJson(vanilla[QStringLiteral("assetIndex")]),
              "U5 原版那份 assetIndex 也合进来了");
        check(!diskMerged.contains(QStringLiteral("inheritsFrom")),
              "U5 合并结果里没有 inheritsFrom");
        check(vm.inheritanceProblem(childId).isEmpty(),
              "U5 这条链本身没问题（inheritanceProblem 为空）", vm.inheritanceProblem(childId));

        // 加载器目录里没有 jar，客户端 jar 在原版那个（被改过名的）目录里
        {
            QJsonObject bootstrap = diskMerged;
            bootstrap["mainClass"] = QStringLiteral("cpw.mods.bootstraplauncher.BootstrapLauncher");
            bootstrap["arguments"] = QJsonObject{{"jvm", QJsonArray{
                QStringLiteral("-DignoreList=client-extra,${version_name}.jar")}}};
            GlobalJavaSettings settings;
            LauncherManager launcher;
            const auto args = launcher.generateJvmArguments({}, childId, "1", bootstrap, settings, true, 17);
            check(args.contains(QStringLiteral("-DignoreList=client-extra,%1.jar,真名.jar").arg(childId)),
                  "U5 Bootstrap ignoreList includes actual inherited/renamed client jar");
            check(args.value(args.indexOf("-cp") + 1).contains("真名.jar"),
                  "U5 ignored vanilla jar remains available on classpath");
            bootstrap["arguments"] = QJsonObject{{"jvm", QJsonArray{
                QStringLiteral("-DignoreList=client-extra,真名.jar")}}};
            const auto dedup = launcher.generateJvmArguments({}, childId, "1", bootstrap, settings, true, 17);
            check(dedup.contains(QStringLiteral("-DignoreList=client-extra,真名.jar")),
                  "U5 existing client ignore entry is not duplicated");
            bootstrap["mainClass"] = QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient");
            bootstrap["arguments"] = QJsonObject{{"jvm", QJsonArray{QStringLiteral("-DignoreList=custom")}}};
            check(launcher.generateJvmArguments({}, childId, "1", bootstrap, settings, true, 17)
                      .contains(QStringLiteral("-DignoreList=custom")),
                  "U5 non-Bootstrap launch leaves ignoreList unchanged");
        }
        check(InstanceFiles::jarOwnerId(childId) == parentId,
              "U5 jar 归属找到父版本", InstanceFiles::jarOwnerId(childId));
        check(InstanceFiles::jarRelativePath(childId) ==
                  QStringLiteral("versions/%1/真名.jar").arg(parentId),
              "U5 classpath 指向的是父版本目录里那个 jar",
              InstanceFiles::jarRelativePath(childId));
        {
            GlobalJavaSettings s;
            LauncherManager launcher;
            const QStringList args = launcher.generateJvmArguments(
                QString(), childId, QStringLiteral("1"), diskMerged, s, true, 17);
            const int cpIndex = args.indexOf(QStringLiteral("-cp"));
            const QStringList cp =
                cpIndex >= 0 ? args.at(cpIndex + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts)
                             : QStringList();
            check(!cp.isEmpty() &&
                      cp.first() == absoluteMcPath(InstanceFiles::jarRelativePath(childId)),
                  "U5 「装了加载器、jar 在原版那边」这条链拼得出真的 classpath",
                  cp.isEmpty() ? QString() : cp.first());
            check(cpIndex >= 0 &&
                      cp.contains(absoluteMcPath(
                          QStringLiteral("libraries/org/ow2/asm/asm/9.10.1/asm-9.10.1.jar"))),
                  "U5 加载器的库也在同一条 classpath 上");
        }

        // 父版本没装：必须说"请先安装原版「X」"，而不是"重新下载这个版本"
        const QString orphanProblem = vm.inheritanceProblem(orphanId);
        writeJson(orphanId, [&] {
            QJsonObject j = loader;
            j[QStringLiteral("id")] = orphanId;
            j[QStringLiteral("inheritsFrom")] = missingId;
            return j;
        }());
        const QString problem = vm.inheritanceProblem(orphanId);
        check(!problem.isEmpty(), "U5 父版本没装 -> inheritanceProblem 非空");
        check(problem.contains(missingId), "U5 那句话点名了缺的是哪个版本", problem);
        check(orphanProblem.isEmpty(), "U5 没写 inheritsFrom 之前是没问题的（对照）");
        // 合并只能给出"合不完整的那一份"，但不能崩、也不能返回空
        VersionManager vmOrphan;
        const QJsonObject orphanMerged = vmOrphan.getVersionJson(orphanId);
        check(!orphanMerged.isEmpty() &&
                  orphanMerged[QStringLiteral("mainClass")].toString() ==
                      loader[QStringLiteral("mainClass")].toString(),
              "U5 父版本缺失时 getVersionJson 交出自己能给的那份（不是空）");
        check(orphanMerged[QStringLiteral("libraries")].toArray().size() == loaderLibs.size(),
              "U5 父版本缺失时只有加载器自己的库");

        // 成环：A -> B -> A
        {
            QJsonObject a = loader;
            a[QStringLiteral("id")] = ringA;
            a[QStringLiteral("inheritsFrom")] = ringB;
            QJsonObject b = vanilla;
            b[QStringLiteral("id")] = ringB;
            b[QStringLiteral("inheritsFrom")] = ringA;
            writeJson(ringA, a);
            writeJson(ringB, b);

            VersionManager vmRing;
            const QJsonObject ringMerged = vmRing.getVersionJson(ringA);
            check(!ringMerged.isEmpty(), "U5 成环时 getVersionJson 也会返回（不挂死）");
            const QString ringProblem = vmRing.inheritanceProblem(ringA);
            check(!ringProblem.isEmpty() && ringProblem.contains(QStringLiteral("环")),
                  "U5 成环 -> inheritanceProblem 说是环", ringProblem);
        }

        // 链太长：18 层
        {
            const int chainLength = VersionInheritance::maxDepth() + 2;
            for (int i = chainLength - 1; i >= 0; --i) {
                QJsonObject j = loader;
                j[QStringLiteral("id")] = QStringLiteral("U段-链%1").arg(i);
                if (i + 1 < chainLength) {
                    j[QStringLiteral("inheritsFrom")] = QStringLiteral("U段-链%1").arg(i + 1);
                } else {
                    j.remove(QStringLiteral("inheritsFrom"));
                }
                writeJson(QStringLiteral("U段-链%1").arg(i), j);
            }
            VersionManager vmChain;
            const QJsonObject chainMerged = vmChain.getVersionJson(QStringLiteral("U段-链0"));
            check(!chainMerged.isEmpty(), "U5 链太长时也会返回（不挂死）");
            const QString chainProblem = vmChain.inheritanceProblem(QStringLiteral("U段-链0"));
            check(!chainProblem.isEmpty() && chainProblem.contains(QStringLiteral("长")),
                  "U5 超过 maxDepth 的链 -> inheritanceProblem 说链太长", chainProblem);
            check(vmChain.inheritanceProblem(QStringLiteral("U段-链%1").arg(chainLength - 1)).isEmpty(),
                  "U5 链尾自己没问题（对照，说明上面那条不是因为别的原因）");
        }

        // 收摊：把这一节建的目录全删掉，后面的用例/扫描看到的还是干净的沙箱
        for (const QString &id : {parentId, childId, orphanId, ringA, ringB}) {
            QDir(versionsRoot + "/" + id).removeRecursively();
        }
        for (int i = 0; i < VersionInheritance::maxDepth() + 2; ++i) {
            QDir(versionsRoot + QStringLiteral("/U段-链%1").arg(i)).removeRecursively();
        }
        check(!QDir(versionsRoot).exists() || !QDir(versionsRoot).entryList(QDir::Dirs)
                                                   .filter(QStringLiteral("U段")).size(),
              "U5 沙箱恢复原样（这一节建的目录都删了）");
    }
}

