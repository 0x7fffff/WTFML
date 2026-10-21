#include "Support.hpp"

int runInstanceFileTests(const QString &sandbox) {
    section("N. 实例判据（认得出哪个是客户端 jar）");

    const auto listing = [](const QString &dir, const QStringList &jars,
                            const QStringList &jsons) {
        InstanceFiles::DirListing l;
        l.dirName = dir;
        l.jarNames = jars;
        l.jsonNames = jsons;
        return l;
    };

    {
        // ① 精确匹配优先 —— 即使还有别的 jar 也轮不到它们。
        const auto r = InstanceFiles::resolve(
            listing(QStringLiteral("1.20.1"),
                    {QStringLiteral("1.20.1.jar"), QStringLiteral("别的.jar")},
                    {QStringLiteral("1.20.1.json")}));
        check(r.recognized() && r.jarName == QStringLiteral("1.20.1.jar"),
              "N1 有 <目录名>.jar 时用它（哪怕旁边还有别的 jar）", r.jarName);
        check(r.jsonName == QStringLiteral("1.20.1.json"), "N1 json 也精确匹配", r.jsonName);
    }
    {
        // ② 顶层恰好一个 jar —— 用户自己在资源管理器里改过目录名的那类。
        const auto r = InstanceFiles::resolve(
            listing(QStringLiteral("名字对不上"), {QStringLiteral("旧名字.jar")},
                    {QStringLiteral("旧名字.json")}));
        check(r.recognized() && r.jarName == QStringLiteral("旧名字.jar"),
              "N1 顶层恰好一个 jar 时用它（目录名被外部改过）", r.jarName);
        check(r.jsonName == QStringLiteral("旧名字.json"), "N1 json 同理", r.jsonName);
    }
    {
        // 大小写：Windows 文件系统不区分大小写，改名时只改了大小写不该认不出来。
        const auto r = InstanceFiles::resolve(listing(QStringLiteral("mypack"),
                                                     {QStringLiteral("MyPack.jar")}, {}));
        check(r.recognized() && r.jarName == QStringLiteral("MyPack.jar"),
              "N1 精确匹配不区分大小写，但返回磁盘上的真实拼写", r.jarName);
    }
    {
        // ③ 两个 jar 又没有精确匹配 → 不认，且候选都留着给告警用。
        const auto r = InstanceFiles::resolve(listing(QStringLiteral("两个jar"),
                                                     {QStringLiteral("aaa.jar"),
                                                      QStringLiteral("bbb.jar")},
                                                     {}));
        check(!r.recognized(), "N1 顶层两个 jar 又没有同名的那一个 → 不认");
        check(r.jarCandidates.size() == 2, "N1 认不出来时候选 jar 都留着（告警要用）",
              QString::number(r.jarCandidates.size()));
    }
    check(!InstanceFiles::resolve(listing(QStringLiteral("空的"), {}, {})).recognized(),
          "N1 一个 jar 都没有的目录不认");
    check(!InstanceFiles::resolve(listing(QStringLiteral("只有json"), {},
                                          {QStringLiteral("x.json")}))
               .recognized(),
          "N1 只有 json 没有 jar 的目录不认（下载残骸）");
    {
        // json 缺席**不**否决实例。
        const auto r = InstanceFiles::resolve(listing(QStringLiteral("缺json"),
                                                     {QStringLiteral("缺json.jar")}, {}));
        check(r.recognized() && r.jsonName.isEmpty(),
              "N1 有 jar 没有 json 时仍然认得出（json 缺席不否决）");
    }
    {
        // 两个 json 又没有精确匹配 → json 解析不出来，但**不影响**认不认得出。
        const auto r = InstanceFiles::resolve(listing(QStringLiteral("两个json"),
                                                     {QStringLiteral("两个json.jar")},
                                                     {QStringLiteral("a.json"),
                                                      QStringLiteral("b.json")}));
        check(r.recognized() && r.jsonName.isEmpty(),
              "N1 两个 json 又没同名的那一个 → json 解析不出来，但仍算实例");
    }
    {
        // ④ 一个 jar 都没有、但 json 声明了父版本 → 认得出。带模组加载器的版本长这样：
        // Fabric 装出来的目录里只有一份 json，客户端 jar 在原版那个目录里。
        InstanceFiles::DirListing l =
            listing(QStringLiteral("fabric-loader-0.15.11-1.20.1"), {},
                    {QStringLiteral("fabric-loader-0.15.11-1.20.1.json")});
        l.inheritsFrom = QStringLiteral("1.20.1");
        const auto r = InstanceFiles::resolve(l);
        check(r.recognized(), "N1 只有 json、但 json 声明了父版本 → 认得出（Fabric 那种）");
        check(r.jarName.isEmpty(), "N1 它自己没有客户端 jar（jarName 是空的）", r.jarName);
        check(r.inheritsFrom == QStringLiteral("1.20.1"), "N1 父版本号跟着解析结果出来",
              r.inheritsFrom);
    }
    {
        // 顶层有垃圾 jar、又认不出哪个是客户端 jar，但 json 声明了父版本 → 仍然认得出。
        // 那两个 jar 跟这个版本没关系（用户丢进来的、装别的加载器剩下的），客户端 jar
        // 反正在父版本那边 —— 按 ①/② 硬挑一个才是错的。
        InstanceFiles::DirListing l = listing(QStringLiteral("混乱"),
                                              {QStringLiteral("a.jar"),
                                               QStringLiteral("b.jar")},
                                              {QStringLiteral("混乱.json")});
        l.inheritsFrom = QStringLiteral("1.20.1");
        const auto r = InstanceFiles::resolve(l);
        check(r.recognized() && r.jarName.isEmpty(),
              "N1 有垃圾 jar 但声明了父版本 → 认得出，那些 jar 不算自己的", r.jarName);
    }
    {
        // 声明了父版本、但连 json 都读不懂（inheritsFrom 空）时**不能**认 —— 下载残骸
        // 的那种目录必须继续被挡在外面，判据不能因为"多了一条路"就整体放松。
        const auto r = InstanceFiles::resolve(
            listing(QStringLiteral("坏json"), {}, {QStringLiteral("坏json.json")}));
        check(!r.recognized(), "N1 只有 json、json 又没声明父版本 → 还是不认");
    }

    // 下面两条走真磁盘：纯函数吃得对不对，取决于 scanDir 扫出来的东西对不对。
    const QString versionsRoot = sandbox + "/versions";
    {
        const QString id = QStringLiteral("N段-外部改名");
        writeFileAt(versionsRoot + "/" + id + "/" + QStringLiteral("老名字.jar"), "dummy");
        const auto r = InstanceFiles::resolveInstance(id);
        check(r.recognized() && r.jarName == QStringLiteral("老名字.jar"),
              "N2 走真磁盘扫出来的结果和纯函数一致", r.jarName);
        check(InstanceFiles::jarPath(id) == versionsRoot + "/" + id + "/老名字.jar",
              "N2 jarPath 用的是解析出来的名字，不是硬拼的", InstanceFiles::jarPath(id));

        // 不递归：实例目录底下的 libraries/ 里有几百个库 jar（下载时写在那），
        // 递归扫会把它们当成客户端 jar 的候选，"顶层恰好一个 jar"立刻失效。
        // 这条断言防的是将来有人"顺手"给 scanDir 加上 QDir::Subdirectories。
        writeFileAt(versionsRoot + "/" + id + "/libraries/net/fake/lib.jar", "dummy");
        const auto afterLibs = InstanceFiles::resolveInstance(id);
        check(afterLibs.recognized() && afterLibs.jarName == QStringLiteral("老名字.jar") &&
                  afterLibs.jarCandidates.size() == 1,
              "N2 libraries/ 里的库 jar 不算候选（扫描不递归）",
              QString::number(afterLibs.jarCandidates.size()));
        // NOTE: 清理必须能真的删干净。以前这里就地 new 了一个 QFile 写到作用域结束，
        // 句柄还开着，removeRecursively() 就删不掉 —— 每跑一次留一个目录在
        // 沙盒里，谁也没发现。走 writeFileAt() 它写完就关。
        const bool cleaned = QDir(versionsRoot + "/" + id).removeRecursively();
        check(cleaned && !QDir(versionsRoot + "/" + id).exists(),
              "N2 用完的沙盒目录删干净了（句柄没被自己攥着）");
    }
    // 继承型实例走真磁盘：jar 归谁、路径怎么拼、链走不动时怎么办。
    {
        const QString vanilla = QStringLiteral("N段-原版");
        const QString loader = QStringLiteral("N段-加载器");
        const QString loader2 = QStringLiteral("N段-加载器二层");
        writeFileAt(versionsRoot + "/" + vanilla + "/" + vanilla + ".jar", "dummy");
        writeFileAt(versionsRoot + "/" + vanilla + "/" + vanilla + ".json",
                    "{\"id\":\"" + vanilla.toUtf8() + "\"}");
        // 加载器：**只有一份 json**，一个 jar 都没有（Fabric 装出来就是这样）。
        writeFileAt(versionsRoot + "/" + loader + "/" + loader + ".json",
                    "{\"id\":\"" + loader.toUtf8() + "\",\"inheritsFrom\":\"" +
                        vanilla.toUtf8() + "\"}");
        // 二层：它继承的是另一个"自己也没有 jar"的版本 —— 链要能一路走下去。
        writeFileAt(versionsRoot + "/" + loader2 + "/" + loader2 + ".json",
                    "{\"id\":\"" + loader2.toUtf8() + "\",\"inheritsFrom\":\"" +
                        loader.toUtf8() + "\"}");

        const auto r = InstanceFiles::resolveInstance(loader);
        check(r.recognized() && r.jarName.isEmpty() && r.inheritsFrom == vanilla,
              "N4 走真磁盘：只有 json 的加载器目录被认得出来（jar 不是自己的）",
              r.inheritsFrom);

        check(InstanceFiles::jarOwnerId(loader) == vanilla,
              "N4 jar 的主人是它基于的原版", InstanceFiles::jarOwnerId(loader));
        check(InstanceFiles::jarOwnerId(loader2) == vanilla,
              "N4 链上两层都没有 jar，一路找到原版", InstanceFiles::jarOwnerId(loader2));
        check(InstanceFiles::jarName(loader) == vanilla + QStringLiteral(".jar"),
              "N4 jarName 给的是主人那个 jar 的名字（不是 <id>.jar）",
              InstanceFiles::jarName(loader));
        check(InstanceFiles::jarPath(loader) ==
                  versionsRoot + "/" + vanilla + "/" + vanilla + ".jar",
              "N4 jarPath 指着原版那个目录", InstanceFiles::jarPath(loader));
        check(InstanceFiles::jarRelativePath(loader) ==
                  QStringLiteral("versions/%1/%2.jar").arg(vanilla, vanilla),
              "N4 jarRelativePath 拼进 classpath 的那一段", InstanceFiles::jarRelativePath(loader));
        // 自立型实例（自己有 jar，且 jar 名和目录名不一样）必须与改动前逐字节一致 ——
        // 这条防的是"多了一条继承的路，把老路顺手改歪了"。
        const QString solo = QStringLiteral("N段-自立");
        writeFileAt(versionsRoot + "/" + solo + "/" + QStringLiteral("真名.jar"), "dummy");
        check(InstanceFiles::jarOwnerId(solo) == solo, "N4 自己有 jar 时主人就是自己",
              InstanceFiles::jarOwnerId(solo));
        check(InstanceFiles::jarRelativePath(solo) ==
                  QStringLiteral("versions/%1/真名.jar").arg(solo),
              "N4 自立型实例的 classpath 那一段和改动前一样",
              InstanceFiles::jarRelativePath(solo));

        // 环：A 继承 B、B 继承 A。不能死循环，也不能崩 —— 退化成链上最后一个。
        const QString ringA = QStringLiteral("N段-环A");
        const QString ringB = QStringLiteral("N段-环B");
        writeFileAt(versionsRoot + "/" + ringA + "/" + ringA + ".json",
                    "{\"id\":\"" + ringA.toUtf8() + "\",\"inheritsFrom\":\"" + ringB.toUtf8() +
                        "\"}");
        writeFileAt(versionsRoot + "/" + ringB + "/" + ringB + ".json",
                    "{\"id\":\"" + ringB.toUtf8() + "\",\"inheritsFrom\":\"" + ringA.toUtf8() +
                        "\"}");
        check(InstanceFiles::jarOwnerId(ringA) == ringB,
              "N4 继承链成环时不死循环，退化成链上最后一个",
              InstanceFiles::jarOwnerId(ringA));

        // 父版本没装：仍然算实例（列表里看得见），但 jar 的主人指向那个不存在的父版本 ——
        // 启动时那句话要说的是"父版本没装"，而不是"缺客户端 JAR"指着一个加载器目录。
        const QString orphan = QStringLiteral("N段-孤儿");
        writeFileAt(versionsRoot + "/" + orphan + "/" + orphan + ".json",
                    "{\"id\":\"" + orphan.toUtf8() +
                        "\",\"inheritsFrom\":\"N段-根本没装\"}");
        check(InstanceFiles::resolveInstance(orphan).recognized(),
              "N4 父版本没装时实例仍然认得出来（是「没装原版」，不是「不是实例」）");
        check(InstanceFiles::jarOwnerId(orphan) == QStringLiteral("N段-根本没装"),
              "N4 找不到 jar 时主人指向那个缺失的父版本（报错才指得对地方）",
              InstanceFiles::jarOwnerId(orphan));

        for (const QString &id : {vanilla, loader, loader2, ringA, ringB, orphan, solo}) {
            QDir(versionsRoot + "/" + id).removeRecursively();
        }
        check(!QDir(versionsRoot + "/" + loader).exists(),
              "N4 继承型用例的沙盒目录删干净了");
    }

    // ---- N5：半成品导入 vs 别的启动器装好的加载器实例 ----
    //
    // 这两者**长得一样**（没有 wtfml.meta、没有顶层 jar、json 里都有
    // inheritsFrom）。只按那三条判会把第三方实例从启动列表里弄消失、点启动还被拦一句
    // "还没装完"；所以判据多了第 ④ 条：导入开始时落的标记，或者"薄到只有三格"的 json。
    {
        const QString stub = QStringLiteral("N段-半成品导入");
        writeFileAt(versionsRoot + "/" + stub + "/" + stub + ".json",
                    "{\"id\":\"" + stub.toUtf8() +
                        "\",\"inheritsFrom\":\"1.20.1\",\"type\":\"release\"}");
        check(InstanceFiles::isUnfinishedImport(stub),
              "N5 导入写下的薄 json（id / inheritsFrom / type）认成半成品");
        QString problem;
        check(InstanceFiles::checkImportTargetName(stub, &problem) ==
                  InstanceFiles::ImportNameVerdict::OkResume,
              "N5 撞上它时给的是「接着上次的导入」，不是「换个名字」");

        const QString foreign = QStringLiteral("N段-第三方加载器");
        writeFileAt(versionsRoot + "/" + foreign + "/" + foreign + ".json",
                    "{\"id\":\"" + foreign.toUtf8() + "\",\"inheritsFrom\":\"1.20.1\","
                    "\"mainClass\":\"net.fabricmc.loader.impl.launch.knot.KnotClient\","
                    "\"libraries\":[]}");
        check(!InstanceFiles::isUnfinishedImport(foreign),
              "★ N5 别的启动器装的加载器实例**不是**半成品（否则会从列表里消失）");
        check(InstanceFiles::checkImportTargetName(foreign, &problem) ==
                  InstanceFiles::ImportNameVerdict::Taken,
              "N5 那种目录仍然算「名字被占了」");

        const QString marked = QStringLiteral("N段-有标记");
        writeFileAt(versionsRoot + "/" + marked + "/" + marked + ".json",
                    "{\"id\":\"" + marked.toUtf8() + "\",\"inheritsFrom\":\"1.20.1\","
                    "\"mainClass\":\"x\",\"libraries\":[]}");
        writeFileAt(InstanceFiles::importMarkerPath(marked), "2026-10-20T00:00:00Z");
        check(InstanceFiles::isUnfinishedImport(marked),
              "N5 有导入标记时就算 json 已经被盖成满的也算半成品");

        // 同名**不等于**同一个包。标记里记着上一次那份清单的指纹，这一趟拿
        // 另一个包的指纹来问时不能再走「接着下」—— 否则两个包的文件会混进一个目录，
        // 而清单里 sha1 对不上的那几条会被当成"已经有文件"跳过。
        const QByteArray manifestA =
            QByteArrayLiteral("{\"formatVersion\":1,\"name\":\"A\",\"files\":[]}");
        const QByteArray manifestB =
            QByteArrayLiteral("{\"formatVersion\":1,\"name\":\"B\",\"files\":[]}");
        check(InstanceFiles::importManifestFingerprint(manifestA) ==
                  QStringLiteral(
                      "c3740e8b1638e4cbb02449539cdcf45cccc8f0a59c150e0677391b75d58f71e4"),
              "★ N5 清单指纹 = 清单原文的 sha256（钉死算法：重打包时字节不变才算同一个包）",
              InstanceFiles::importManifestFingerprint(manifestA));
        check(InstanceFiles::importManifestFingerprint(QByteArray()).isEmpty(),
              "N5 没有清单字节就没有指纹（不写、也不拿它拦人）");
        check(InstanceFiles::importMarkerFingerprint(marked).isEmpty(),
              "N5 老格式标记（一行 ISO 时间）读不出指纹");

        const QString printed = QStringLiteral("N段-有指纹");
        writeFileAt(versionsRoot + "/" + printed + "/" + printed + ".json",
                    "{\"id\":\"" + printed.toUtf8() +
                        "\",\"inheritsFrom\":\"1.20.1\",\"type\":\"release\"}");
        writeFileAt(InstanceFiles::importMarkerPath(printed),
                    "{\"startedAt\":\"2026-10-20T00:00:00Z\",\"manifestSha256\":\"" +
                        InstanceFiles::importManifestFingerprint(manifestA).toUtf8() + "\"}");
        check(InstanceFiles::importMarkerFingerprint(printed) ==
                  InstanceFiles::importManifestFingerprint(manifestA),
              "N5 标记里的指纹读得回来");
        check(InstanceFiles::checkImportTargetName(
                  printed, &problem, InstanceFiles::importManifestFingerprint(manifestA)) ==
                  InstanceFiles::ImportNameVerdict::OkResume,
              "N5 同一个包（同一个指纹）仍然给「接着上次的导入」");
        check(InstanceFiles::checkImportTargetName(
                  printed, &problem, InstanceFiles::importManifestFingerprint(manifestB)) ==
                  InstanceFiles::ImportNameVerdict::Taken,
              "★★ N5 同名但是另一个整合包 ⇒ 不算「接着上次」（两个包的文件会混进一个目录）");
        check(problem.contains(QStringLiteral("另一个整合包")) &&
                  problem.contains(QStringLiteral("换一个名字")),
              "N5 那句话说得清是另一个包、并给出两条出路", problem);
        check(InstanceFiles::checkImportTargetName(
                  marked, &problem, InstanceFiles::importManifestFingerprint(manifestB)) ==
                  InstanceFiles::ImportNameVerdict::OkResume,
              "N5 老标记没记指纹 ⇒ 仍然按「接着下」放行（升级不能把没下完的那份锁死）");

        const QString done = QStringLiteral("N段-做完了");
        writeFileAt(versionsRoot + "/" + done + "/" + done + ".json",
                    "{\"id\":\"" + done.toUtf8() +
                        "\",\"inheritsFrom\":\"1.20.1\",\"type\":\"release\"}");
        writeFileAt(AppPaths::versionMetaPath(done), "{}");
        check(!InstanceFiles::isUnfinishedImport(done),
              "N5 有 wtfml.meta 就永远不算半成品（哪怕 json 很薄）");

        for (const QString &id : {stub, foreign, marked, printed, done}) {
            QDir(versionsRoot + "/" + id).removeRecursively();
        }
        check(!QDir(versionsRoot + "/" + marked).exists(), "N5 用例的沙盒目录删干净了");
    }

    // ---- N6：实例改名之后，按目录名认人的缓存备份跟着搬 ----
    {
        const QString configRoot = AppPaths::configBackupDir();
        const QString packRoot = AppPaths::resourcePackBackupDir();
        const QString oldName = QStringLiteral("N段-改名前");
        const QString newName = QStringLiteral("N段-改名后");
        const auto readAll = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll())
                                                  : QString();
        };

        // 1) 两个按名字分的格子都搬过去，里面的代数原样。
        QDir().mkpath(configRoot + "/" + oldName + "/20261020-120000/sub");
        writeFileAt(configRoot + "/" + oldName + "/20261020-120000/sub/a.toml", "keep=1");
        QDir().mkpath(packRoot + "/" + oldName);
        writeFileAt(packRoot + "/" + oldName + "/options.txt.bak", "resourcePacks:[]");
        int moved = 0;
        const QStringList problems = InstanceFiles::migrateInstanceCacheDirs(
            oldName, newName, {configRoot, packRoot}, &moved);
        check(moved == 2 && problems.isEmpty(),
              "N6 两个缓存格子都搬到了新名字底下", problems.join(QStringLiteral(" / ")));
        check(!QDir(configRoot + "/" + oldName).exists() &&
                  readAll(configRoot + "/" + newName + "/20261020-120000/sub/a.toml") ==
                      QStringLiteral("keep=1"),
              "N6 配置回退格子整目录搬过去、代数那一层原样");
        check(!QDir(packRoot + "/" + oldName).exists() &&
                  readAll(packRoot + "/" + newName + "/options.txt.bak") ==
                      QStringLiteral("resourcePacks:[]"),
              "N6 资源包备份格子也搬了");

        // 2) 目标格子已经有东西（以前叫过这个名字的另一个实例）⇒ 不合并、不删，
        // 报一句让用户自己决定。
        const QString takenOld = QStringLiteral("N段-占用旧");
        const QString takenNew = QStringLiteral("N段-占用新");
        QDir().mkpath(configRoot + "/" + takenOld + "/20261020-120000");
        writeFileAt(configRoot + "/" + takenOld + "/20261020-120000/old.txt", "old");
        QDir().mkpath(configRoot + "/" + takenNew + "/20261020-130000");
        writeFileAt(configRoot + "/" + takenNew + "/20261020-130000/new.txt", "new");
        int movedTaken = 0;
        const QStringList takenProblems = InstanceFiles::migrateInstanceCacheDirs(
            takenOld, takenNew, {configRoot}, &movedTaken);
        check(movedTaken == 0 && takenProblems.size() == 1 &&
                  takenProblems.value(0).contains(QStringLiteral("已经有另一个格子")),
              "★ N6 目标格子已经有东西 ⇒ 不合并、说清楚", takenProblems.join(QStringLiteral(" / ")));
        check(readAll(configRoot + "/" + takenOld + "/20261020-120000/old.txt") ==
                      QStringLiteral("old") &&
                  readAll(configRoot + "/" + takenNew + "/20261020-130000/new.txt") ==
                      QStringLiteral("new"),
              "N6 撞格子时两边的备份都一个字节没动");

        // 3) 名字不能当目录名用（空 / `..` / 带分隔符）⇒ 一个字节都不动。
        const QString impossible = QStringLiteral("N段-不该出现");
        const QStringList badProblems = InstanceFiles::migrateInstanceCacheDirs(
            QStringLiteral(".."), impossible, {configRoot});
        check(!badProblems.isEmpty() && !QDir(configRoot + "/" + impossible).exists(),
              "N6 非法的实例名不拼路径（`..` 拼出去就是 cache 根目录本身）",
              badProblems.join(QStringLiteral(" / ")));

        // 4) 存档 level.dat 的备份格子：格子名里同时有实例目录名和世界目录绝对路径的
        // 哈希，改名把两样都改了。这里走一遍真实时序：先备份 → 改目录名 →
        // 用新路径查不到（就是那条 bug）→ 搬家 → 新路径查得到、内容还是那一份。
        const QString slotOld = QStringLiteral("N段-存档改名前");
        const QString slotNew = QStringLiteral("N段-存档改名后");
        const QString worldName = QStringLiteral("世界");
        const QString oldInstanceDir = AppPaths::versionDir(slotOld);
        const QString newInstanceDir = AppPaths::versionDir(slotNew);
        const QString oldWorldDir =
            oldInstanceDir + QStringLiteral("/saves/") + worldName;
        const QString newWorldDir =
            newInstanceDir + QStringLiteral("/saves/") + worldName;
        QDir().mkpath(oldWorldDir);
        writeFileAt(oldWorldDir + QStringLiteral("/level.dat"), "level-数据");
        QString slotProblem;
        check(InstanceMaintenanceController::backupWorldLevelData(oldWorldDir, 20, &slotProblem),
              "N6 前提：改动存档前先留一份 level.dat", slotProblem);
        const QString oldBackupBeforeRename =
            InstanceMaintenanceController::latestWorldBackup(oldWorldDir);
        check(!oldBackupBeforeRename.isEmpty(),
              "N6 前提：旧路径查得到刚留的那一份");
        check(QDir().rename(oldInstanceDir, newInstanceDir),
              "N6 前提：实例目录被改了名（和 renameInstance() 最后一步同一个动作）");
        check(InstanceMaintenanceController::latestWorldBackup(newWorldDir).isEmpty(),
              "★★ N6 不改名搬家的话，新路径查不到旧备份（就是那条 bug）");
        int movedSlots = 0;
        const QStringList slotProblems = InstanceMaintenanceController::migrateWorldBackupSlots(
            oldInstanceDir, newInstanceDir, &movedSlots);
        const QString migrated = InstanceMaintenanceController::latestWorldBackup(newWorldDir);
        check(movedSlots == 1 && slotProblems.isEmpty() && !migrated.isEmpty(),
              "★ N6 改名后存档备份格子跟到了新名字底下",
              slotProblems.join(QStringLiteral(" / ")));
        check(!migrated.isEmpty() && readAll(migrated) == QStringLiteral("level-数据"),
              "N6 搬过去的还是那一份 level.dat");
        check(InstanceMaintenanceController::latestWorldBackup(oldWorldDir).isEmpty(),
              "N6 旧格子已经不在原处（不是复制一份）");

        // 收尾：把这一块在沙盒里造的东西删干净（版本目录 + 缓存格子）。
        // 备份路径的形状是 `<world-backups>/<格子>/<时间戳>/level.dat`，删到格子那一层。
        const auto removeSlotOf = [](const QString &backupPath) {
            const QString stampDir = QFileInfo(backupPath).absolutePath();
            const QString slotDir = QFileInfo(stampDir).absolutePath();
            // 空路径在 QDir 里等于当前目录 —— 那种情况下什么都别删。
            if (backupPath.isEmpty() || slotDir.isEmpty()) {
                return;
            }
            QDir(slotDir).removeRecursively();
        };
        removeSlotOf(migrated);
        removeSlotOf(oldBackupBeforeRename);
        QDir(newInstanceDir).removeRecursively();
        for (const QString &name : {newName, takenOld, takenNew}) {
            QDir(configRoot + "/" + name).removeRecursively();
        }
        QDir(packRoot + "/" + newName).removeRecursively();
        check(!QDir(configRoot + "/" + newName).exists() &&
                  !QDir(newInstanceDir).exists(),
              "N6 用例的沙盒目录删干净了");
    }

    {
        // 解析不出来时退回老名字 —— 只想拿个名字去看看在不在的调用方在正常目录上逐字节不变。
        const QString missing = QStringLiteral("N段-不存在");
        check(InstanceFiles::jarName(missing) == missing + QStringLiteral(".jar"),
              "N3 认不出时 jarName 退回 <id>.jar（老行为）", InstanceFiles::jarName(missing));
        check(InstanceFiles::jsonName(missing) == missing + QStringLiteral(".json"),
              "N3 认不出时 jsonName 退回 <id>.json", InstanceFiles::jsonName(missing));
        check(InstanceFiles::jarPath(missing) ==
                  AppPaths::versionJarPath(missing),
              "N3 认不出时 jarPath 与改动前逐字节一致", InstanceFiles::jarPath(missing));
    }

    return 0;
}

