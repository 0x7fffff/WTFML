#include "Support.hpp"
#include "ModpackDomainChecks.hpp"
#include <QUuid>

void runModpackExportPipelineTests(const QString &sandbox) {
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
            check(problem.isEmpty() && key == QStringLiteral("quilt-loader")
                      && version == QStringLiteral("0.20.0"),
                  "V5m Quilt 实例可以导出（键是 quilt-loader，版本取盘上那一份）", problem);
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
            check(problem.isEmpty() && key == QStringLiteral("quilt-loader")
                      && version == QStringLiteral("0.20.0"),
                  "V5o 合过的 json 里装了 Quilt：现在可以导出（键/版本都对）", problem);
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
        // 打的备份），不许上来就 `QFile::remove()` 它们。换一个没人用的名字，用户那两份
        // 必须原样留在盘上（逐字节），而这一趟照样得打成功。
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
    // createZip 不能把整张 `@('条目名','盘上路径')` 表**拼进脚本正文**：脚本是经
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

}
