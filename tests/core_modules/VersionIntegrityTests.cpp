#include "Support.hpp"

void runVersionIntegrityTests() {
    section("ZN. 体检（只报不装）：plan / check / Report");

    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir temp;
    check(temp.isValid(), "ZN0 临时数据根目录建出来了");
    if (!temp.isValid()) {
        return;
    }
    AppPaths::setMinecraftDir(temp.path());

    const auto sha1Of = [](const QByteArray &bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
    };

    // ---------- ZN1. 清单：产品那两个 extract 函数 + plan() ----------
    // 手搓一份**合并过**的 json（形状照 meta 里那份真的抄），先交给产品自己的
    // extractLibraries() / extractAssetIndex() 解一遍再进 plan() —— 「哪些库算数、哪个
    // 资源索引」的唯一出处就是那两个函数，跳过它们这一节就变成自证了。
    const QByteArray jarBytes = QByteArrayLiteral("client-jar-body");
    const QByteArray libBytes = QByteArrayLiteral("lib-body");
    const QByteArray indexBytes = QByteArrayLiteral("{\"objects\":{}}");

    QJsonObject json;
    json[QStringLiteral("id")] = QStringLiteral("1.99.1");
    json[QStringLiteral("downloads")] = QJsonObject{
        {QStringLiteral("client"),
         QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example.invalid/c.jar")},
                     {QStringLiteral("sha1"), sha1Of(jarBytes)},
                     {QStringLiteral("size"), static_cast<qint64>(jarBytes.size())}}}};
    json[QStringLiteral("libraries")] = QJsonArray{
        QJsonObject{
            {QStringLiteral("name"), QStringLiteral("synth:art:1.0")},
            {QStringLiteral("downloads"),
             QJsonObject{{QStringLiteral("artifact"),
                          QJsonObject{
                              {QStringLiteral("path"), QStringLiteral("synth/art/1.0/art-1.0.jar")},
                              {QStringLiteral("url"), QStringLiteral("https://example.invalid/a.jar")},
                              {QStringLiteral("sha1"), sha1Of(libBytes)},
                              {QStringLiteral("size"), static_cast<qint64>(libBytes.size())}}}}}},
        // 地址和 artifact 都没有的库：装配侧（extractLibraries）就不收它，清单里当然也不该有。
        QJsonObject{{QStringLiteral("name"), QStringLiteral("synth:noartifact:1.0")}}};
    json[QStringLiteral("assetIndex")] =
        QJsonObject{{QStringLiteral("id"), QStringLiteral("5")},
                    {QStringLiteral("sha1"), sha1Of(indexBytes)},
                    {QStringLiteral("size"), static_cast<qint64>(indexBytes.size())},
                    {QStringLiteral("url"), QStringLiteral("https://example.invalid/5.json")}};

    DownloadManager mgr;
    const QList<Library> productLibs = mgr.extractLibraries(json);
    check(productLibs.size() == 1, "ZN1 产品那两个解析里只有一个库带得下地址（另一个是幽灵）",
          QString::number(productLibs.size()));

    const QList<VersionIntegrity::ExpectedFile> files =
        VersionIntegrity::plan(json, QStringLiteral("1.99.1"), productLibs,
                               mgr.extractAssetIndex(json));
    // 索引是空的（这一份 objects 里一条都没有）⇒ 三条：jar / 库 / 索引文件。
    check(files.size() == 3, "ZN1 清单三条：客户端 jar、库、资源索引（objects 是空的）",
          QString::number(files.size()));
    if (files.size() == 3) {
        check(files.at(0).label == QStringLiteral("客户端 jar")
                  && files.at(0).path == InstanceFiles::jarPath(QStringLiteral("1.99.1"))
                  && files.at(0).sha1 == sha1Of(jarBytes)
                  && files.at(0).size == jarBytes.size(),
              "★ ZN1 jar 那一条：落点走 InstanceFiles::jarPath()（装出来的加载器版本没有"
              "自己的 jar，那个函数认得主人是父版本）",
              files.at(0).path);
        check(files.at(1).label == QStringLiteral("synth:art:1.0")
                  && files.at(1).path == AppPaths::libraryDir()
                                             + QStringLiteral("/synth/art/1.0/art-1.0.jar")
                  && files.at(1).sha1 == sha1Of(libBytes) && files.at(1).size == libBytes.size(),
              "ZN1 库那一条落在**共用的** <数据目录>/libraries 底下（不是实例目录里）",
              files.at(1).path);
        check(files.at(2).path == AppPaths::assetsDir() + QStringLiteral("/indexes/5.json")
                  && files.at(2).sha1 == sha1Of(indexBytes),
              "ZN1 资源索引那一条落在 <数据目录>/assets/indexes/<id>.json",
              files.at(2).path);
    }

    // 边界：url / id 有一个空的索引，装配侧一句都不下（downloadAssets 开头那条早退），
    // 所以清单里也不该有它 —— 列了就是无中生有的一条"缺文件"。
    {
        AssetIndex noUrl = mgr.extractAssetIndex(json);
        noUrl.url.clear();
        const QList<VersionIntegrity::ExpectedFile> onlyJarAndLib =
            VersionIntegrity::plan(json, QStringLiteral("1.99.1"), productLibs, noUrl);
        check(onlyJarAndLib.size() == 2,
              "ZN2 ★ 索引没有 url ⇒ 那一条不列（装配时根本不会去下它，报了就是假警报）",
              QString::number(onlyJarAndLib.size()));
        const QList<VersionIntegrity::ExpectedFile> noIndex =
            VersionIntegrity::plan(json, QStringLiteral("1.99.1"), productLibs, AssetIndex());
        check(noIndex.size() == 2, "ZN2 整个 assetIndex 都没有（老 json）⇒ 同样不列");
    }

    // plan() 自己的那两条判据（拿手搓的 Library 直接喂，绕开 extractLibraries）：
    // path 空的跳过（和 downloadLibraries 一样 —— 那种库装配时落不了盘）。
    {
        QList<Library> hand;
        Library noPath;
        noPath.name = QStringLiteral("synth:nopath:1.0");
        hand.append(noPath);
        Library noHash;
        noHash.name = QStringLiteral("synth:nohash:1.0");
        noHash.path = QStringLiteral("synth/nohash/1.0/nohash-1.0.jar");
        hand.append(noHash);
        const QList<VersionIntegrity::ExpectedFile> planned =
            VersionIntegrity::plan(QJsonObject(), QString(), hand, AssetIndex());
        check(planned.size() == 1 && planned.at(0).path == AppPaths::libraryDir()
                                                          + QStringLiteral("/synth/nohash/1.0/"
                                                                           "nohash-1.0.jar"),
              "ZN2 path 为空的库不进清单（装配侧也是这么跳的），没有 sha1 的照进",
              QString::number(planned.size()));
    }

    // ---------- ZN3. check()：六种结论各自怎么判 ----------
    const QString jarPath = InstanceFiles::jarPath(QStringLiteral("1.99.1"));
    const VersionIntegrity::ExpectedFile jarExpected{
        QStringLiteral("客户端 jar"), jarPath, sha1Of(jarBytes), static_cast<qint64>(jarBytes.size())};

    check(VersionIntegrity::check(jarExpected).status == VersionIntegrity::Status::Missing,
          "ZN3 盘上没有 ⇒ Missing");

    writeFileAt(jarPath, jarBytes);
    check(VersionIntegrity::check(jarExpected).ok(),
          "ZN3 内容逐字节一样 ⇒ Ok");

    // 「大小一样、内容不对」：这一档判的只能是 sha1（T19 那个夹具就是这个形状）。
    writeFileAt(jarPath, QByteArray(jarBytes.size(), 'x'));
    {
        const VersionIntegrity::Verdict verdict = VersionIntegrity::check(jarExpected);
        check(verdict.status == VersionIntegrity::Status::WrongSha1,
              "★ ZN3 字节数一样、内容不对 ⇒ WrongSha1（这一档只能是 sha1 判的：字节数那一关"
              "它已经过了）",
              verdict.detail());
        check(verdict.detail() == QStringLiteral("校验不过（内容与 sha1 对不上）"),
              "ZN3 那句话是给用户看的，钉住它", verdict.detail());
    }

    // 0 字节：**在比字节数之前**判 —— 这条判据和 fileAlreadyValid() 里那句同一个理由。
    writeFileAt(jarPath, QByteArray());
    {
        const VersionIntegrity::Verdict verdict = VersionIntegrity::check(jarExpected);
        check(verdict.status == VersionIntegrity::Status::Empty,
              "★ ZN3 0 字节判的是 Empty，不是 WrongSize（下载写了一半被中断留下的那种）",
              verdict.detail());
    }

    writeFileAt(jarPath, QByteArray(jarBytes.size() + 3, 'x'));
    {
        const VersionIntegrity::Verdict verdict = VersionIntegrity::check(jarExpected);
        check(verdict.status == VersionIntegrity::Status::WrongSize
                  && verdict.actualSize == jarBytes.size() + 3
                  && verdict.expectedSize == jarBytes.size(),
              "★ ZN3 字节数对不上就直接判大小，**不去算哈希**（省掉一次 41 MB 的读）",
              verdict.detail());
    }

    // 判据里没有字节数这一档（<= 0）时只比 sha1。
    {
        writeFileAt(jarPath, jarBytes);
        VersionIntegrity::ExpectedFile noSize = jarExpected;
        noSize.size = -1;
        check(VersionIntegrity::check(noSize).ok(),
              "ZN3 判据里没有字节数（-1）时照样能靠 sha1 判对");
        VersionIntegrity::ExpectedFile noHashNoSize = noSize;
        noHashNoSize.sha1.clear();
        check(VersionIntegrity::check(noHashNoSize).ok(),
              "★ ZN3 什么判据都没有的那一条**只要文件在、不是 0 字节**就算好"
              "（资源对象那一档就是这个形状）");
    }

    // NOTE: Unreadable 那一档这里够不着 —— 要造出一份"QFileInfo 说得清楚、真读又读不动"
    // 的文件得占住它（真机上就是"游戏正跑着，jar 被独占"）。这条留到真机上报错了
    // 再看：default 分支那句「读不动」就是为了它在那儿摆着。

    // ---------- ZN4. 资源对象：照索引 json 里那份 objects 表列 ----------
    // 索引 json 的落点由产品自己说了算（<数据目录>/assets/indexes/<id>.json），先按它算出来
    // 再写一份**真的**索引（上面那份 3 条清单用的还是个空的）。
    {
        const QString goodHash = sha1Of(QByteArrayLiteral("object-body"));
        const QString shortHash = QStringLiteral("abc");  // 长度不是 40 ⇒ 装配侧也跳过
        QJsonObject objects;
        objects[QStringLiteral("minecraft/sounds/bark.ogg")] =
            QJsonObject{{QStringLiteral("hash"), goodHash}, {QStringLiteral("size"), 11}};
        objects[QStringLiteral("minecraft/sounds/moo.ogg")] =
            QJsonObject{{QStringLiteral("hash"), shortHash}, {QStringLiteral("size"), 11}};
        writeFileAt(AppPaths::assetsDir() + QStringLiteral("/indexes/5.json"),
                    QJsonDocument(QJsonObject{{QStringLiteral("objects"), objects}})
                        .toJson(QJsonDocument::Compact));

        const QList<VersionIntegrity::ExpectedFile> withObjects =
            VersionIntegrity::plan(json, QStringLiteral("1.99.1"), productLibs,
                                   mgr.extractAssetIndex(json));
        // 索引 json 换了内容 ⇒ 它自己那一条的 sha1/size 也跟着对不上了，但**清单条数**不变:
        // 三条 + 一个资源对象（长度不对的那条被跳过）。
        check(withObjects.size() == 4,
              "★ ZN4 资源对象照 objects 表列出来了，长度不是 40 的那条跳过（装配侧也是）",
              QString::number(withObjects.size()));
        const VersionIntegrity::ExpectedFile &object = withObjects.at(3);
        check(object.path == AppPaths::assetsDir() + QStringLiteral("/objects/")
                                 + goodHash.left(2) + QLatin1Char('/') + goodHash,
              "★ ZN4 落点是 objects/<hash 前两位>/<hash>（和 parseAndDownloadAssets 同一条"
              "规矩）",
              object.path);
        check(object.sha1.isEmpty() && object.size == 11,
              "★ ZN4 判据里**故意没有 sha1**（文件名就是那个 sha1，再哈希一遍等于把最贵的那"
              "一步做两遍），但字节数比（索引里本来就有）");

        // 三档结论：在的、缺的、大小不对的。
        writeFileAt(object.path, QByteArrayLiteral("object-body"));  // 11 字节，对
        check(VersionIntegrity::check(object).ok(), "ZN4 资源对象在、字节数也对 ⇒ Ok");
        VersionIntegrity::ExpectedFile missingObject = object;
        missingObject.path = AppPaths::assetsDir() + QStringLiteral("/objects/ff/");
        missingObject.path += QString(40, QLatin1Char('f'));
        check(VersionIntegrity::check(missingObject).status
                  == VersionIntegrity::Status::Missing,
              "ZN4 不在 ⇒ Missing（那 3000 个对象里少几个是能启动失败的）");
        writeFileAt(object.path, QByteArrayLiteral("short"));
        check(VersionIntegrity::check(object).status == VersionIntegrity::Status::WrongSize,
              "★ ZN4 字节数不对 ⇒ WrongSize（这就是「写盘写到一半」在资源对象上的样子）");
    }

    // 索引文件自己缺了：清单里那条会报 Missing，而 objects **一条都不列**（不然会叠 3000
    // 条噪声，用户修索引就够了）。
    {
        QDir(AppPaths::assetsDir() + QStringLiteral("/indexes")).removeRecursively();
        const QList<VersionIntegrity::ExpectedFile> noIndexFile =
            VersionIntegrity::plan(json, QStringLiteral("1.99.1"), productLibs,
                                   mgr.extractAssetIndex(json));
        check(noIndexFile.size() == 3,
              "★ ZN4 索引 json 不在盘上 ⇒ objects 一条都不列（只有索引自己那一条）",
              QString::number(noIndexFile.size()));
    }

    // 索引在、但里面不是 json（下坏了的）：同样一条都不列，也不崩。
    {
        writeFileAt(AppPaths::assetsDir() + QStringLiteral("/indexes/5.json"),
                    QByteArrayLiteral("<html>404</html>"));
        const QList<VersionIntegrity::ExpectedFile> brokenIndex =
            VersionIntegrity::plan(json, QStringLiteral("1.99.1"), productLibs,
                                   mgr.extractAssetIndex(json));
        check(brokenIndex.size() == 3, "ZN4 索引是个 200+HTML 错误页 ⇒ 照样一条都不列");
    }

    // ---------- ZN5. Report：数得对、那句话拼得对 ----------
    {
        VersionIntegrity::Report report;
        report.checked = 3120;
        check(report.ok() && report.summary() == QStringLiteral("3120 个文件都完好"),
              "★ ZN5 全好的那一句是「N 个文件都完好」", report.summary());

        const auto make = [](VersionIntegrity::Status status) {
            VersionIntegrity::Verdict verdict;
            verdict.status = status;
            return verdict;
        };
        report.problems << make(VersionIntegrity::Status::Missing)
                        << make(VersionIntegrity::Status::Empty)
                        << make(VersionIntegrity::Status::WrongSha1);
        check(!report.ok() && report.missing() == 2 && report.corrupt() == 1,
              "★ ZN5 「缺」= Missing + Empty（两个都在里头）、「坏」= 剩下那些",
              QStringLiteral("missing=%1 corrupt=%2").arg(report.missing()).arg(report.corrupt()));
        check(report.summary()
                  == QStringLiteral("3120 个文件里 3 个要修：缺 2 个、坏 1 个"),
              "★ ZN5 summary() 那一句（界面直接拿它显示，「几个缺几个坏」不许在别处再数一遍）",
              report.summary());

        VersionIntegrity::Report failed;
        failed.error = QStringLiteral("读不出这个实例的 version.json，没法知道它该有哪些文件。");
        check(failed.summary() == failed.error && !failed.ok() && failed.checked == 0,
              "★ ZN5 没跑起来那一档：summary() 原样回 error（和「跑了、发现一堆问题」分开）",
              failed.summary());
    }

    // ---------- ZN6. 分组（结果按客户端 / 库 / 资源索引 / 资源对象 分）----------
    {
        check(VersionIntegrity::allGroups().size() == 4, "ZN6 四组");
        check(VersionIntegrity::groupName(VersionIntegrity::Group::ClientJar)
                      == QStringLiteral("客户端")
                  && VersionIntegrity::groupName(VersionIntegrity::Group::Libraries)
                         == QStringLiteral("库")
                  && VersionIntegrity::groupName(VersionIntegrity::Group::AssetIndex)
                         == QStringLiteral("资源索引")
                  && VersionIntegrity::groupName(VersionIntegrity::Group::AssetObjects)
                         == QStringLiteral("资源对象"),
              "★ ZN6 四组的名字（报告页标题和那段纯文本都读它）");
        // groupIndex() 必须和 allGroups() 的顺序对得上（界面把"正在比哪一组"翻成"第几步"靠它）。
        for (VersionIntegrity::Group group : VersionIntegrity::allGroups()) {
            const int index = VersionIntegrity::groupIndex(group);
            check(index >= 0 && index < VersionIntegrity::allGroups().size()
                      && VersionIntegrity::allGroups().at(index) == group,
                  "ZN6 groupIndex() 回的下标在 allGroups() 里指回它自己",
                  QString::number(index));
        }

        // plan() 在**列清单那一刻**就盖好了组（上面 ZN1 那三条）。
        if (files.size() == 3) {
            check(files.at(0).group == VersionIntegrity::Group::ClientJar
                      && files.at(1).group == VersionIntegrity::Group::Libraries
                      && files.at(2).group == VersionIntegrity::Group::AssetIndex,
                  "★ ZN6 plan() 给每一条盖了组：jar / 库 / 索引各归各的那一格");
        }
        // check() 把组一路带到 Verdict 上（界面按组摊结果时读的就是它）。
        const VersionIntegrity::ExpectedFile missingJar{
            QStringLiteral("客户端 jar"),
            AppPaths::minecraftDir() + QStringLiteral("/nope.jar"), QString(), 100,
            VersionIntegrity::Group::ClientJar};
        const VersionIntegrity::Verdict verdict = VersionIntegrity::check(missingJar);
        check(verdict.group == VersionIntegrity::Group::ClientJar
                  && verdict.status == VersionIntegrity::Status::Missing,
              "★ ZN6 check() 把组带到 Verdict 上（缺文件那一档也带着）");
    }

    {
        const auto problem = [](VersionIntegrity::Status status, VersionIntegrity::Group group,
                                const QString &label) {
            VersionIntegrity::Verdict verdict;
            verdict.status = status;
            verdict.group = group;
            verdict.label = label;
            verdict.path = QStringLiteral("D:/x/") + label;
            return verdict;
        };
        VersionIntegrity::Report report;
        report.checked = 3120;
        report.problems
            << problem(VersionIntegrity::Status::Missing, VersionIntegrity::Group::Libraries,
                       QStringLiteral("libA"))
            << problem(VersionIntegrity::Status::WrongSha1, VersionIntegrity::Group::Libraries,
                       QStringLiteral("libB"))
            << problem(VersionIntegrity::Status::Empty, VersionIntegrity::Group::AssetObjects,
                       QStringLiteral("objA"));

        check(report.missingIn(VersionIntegrity::Group::Libraries) == 1
                  && report.corruptIn(VersionIntegrity::Group::Libraries) == 1
                  && report.problemsIn(VersionIntegrity::Group::Libraries) == 2,
              "★ ZN7 「库」那一组：缺 1 个、坏 1 个、合计 2 个",
              QStringLiteral("m=%1 c=%2")
                  .arg(report.missingIn(VersionIntegrity::Group::Libraries))
                  .arg(report.corruptIn(VersionIntegrity::Group::Libraries)));
        check(report.problemsIn(VersionIntegrity::Group::ClientJar) == 0
                  && report.problemsIn(VersionIntegrity::Group::AssetIndex) == 0,
              "ZN7 没问题的组报 0（说的是「这组没有要修的」，不是「这组没有文件」）");
        check(report.missingIn(VersionIntegrity::Group::AssetObjects) == 1
                  && report.corruptIn(VersionIntegrity::Group::AssetObjects) == 0,
              "ZN7 资源对象那一组：缺 1 个");
        check(report.problemsInGroup(VersionIntegrity::Group::Libraries).size() == 2
                  && report.problemsInGroup(VersionIntegrity::Group::Libraries).at(0).label
                         == QStringLiteral("libA"),
              "ZN7 problemsInGroup() 只挑这一组的、且保持原来那个顺序");
        // 按组数出来的总和 = 总数（两处口径必须一致 —— 别让分组那一层多算或者漏算）。
        int byGroup = 0;
        for (VersionIntegrity::Group group : VersionIntegrity::allGroups()) {
            byGroup += report.problemsIn(group);
        }
        check(byGroup == report.problems.size()
                  && byGroup == report.missing() + report.corrupt(),
              "ZN7 四组加起来 = 总条数（分组这一层不会漏也不会重）", QString::number(byGroup));

        const QStringList lines =
            VersionIntegrity::plainReport(report, QStringLiteral("1.99.1")).split(QLatin1Char('\n'));
        check(lines.value(0) == QStringLiteral("实例「1.99.1」完整性检查")
                  && lines.value(1)
                         == QStringLiteral("3120 个文件里 3 个要修：缺 2 个、坏 1 个"),
              "★ ZN7 报告头两行：标题 + summary()",
              lines.value(0) + QStringLiteral(" / ") + lines.value(1));
        // 四组一个都不省（哪怕是 0 个要修的那两组）。
        const auto hasLine = [&lines](const QString &needle) {
            for (const QString &line : lines) {
                if (line.contains(needle)) {
                    return true;
                }
            }
            return false;
        };
        for (VersionIntegrity::Group group : VersionIntegrity::allGroups()) {
            check(hasLine(VersionIntegrity::groupName(group) + QStringLiteral("（")),
                  "★ ZN7 报告里四组都有一行（含 0 个要修的那两组）",
                  VersionIntegrity::groupName(group));
        }
        check(hasLine(QStringLiteral("库（2 个要修）"))
                  && hasLine(QStringLiteral("客户端（0 个要修）")),
              "ZN7 组标题写成「名字（N 个要修）」");
        check(hasLine(QStringLiteral("libA：缺少文件")),
              "ZN7 逐条问题写成「名字：那句话」");
        check(lines.contains(QStringLiteral("    D:/x/libA")),
              "ZN7 路径缩进换一行（4 个空格）");

        const QStringList paths = report.problemPaths();
        check(paths == QStringList({QStringLiteral("D:/x/libA"), QStringLiteral("D:/x/libB"),
                                    QStringLiteral("D:/x/objA")}),
              "★ ZN7 problemPaths(): 要修的那几条的路径，按 problems 原来的顺序（补全照它点名）",
              paths.join(QStringLiteral(" | ")));
        VersionIntegrity::Report nothingToFix;
        nothingToFix.checked = 3120;
        check(nothingToFix.problemPaths().isEmpty(),
              "★ ZN7 一个要修的都没有 ⇒ 空表（补全那边：问盘、缺什么补什么）");
        VersionIntegrity::Report notRun;
        notRun.error = QStringLiteral("读不出这个实例的 version.json");
        check(notRun.problemPaths().isEmpty(),
              "★ ZN7 体检压根没跑起来 ⇒ 也是空表，不会点名去重下任何东西");

        // 没跑起来那一档：只出两行。
        VersionIntegrity::Report failed;
        failed.error = QStringLiteral("读不出这个实例的 version.json，没法知道它该有哪些文件。");
        const QString failedText = VersionIntegrity::plainReport(failed, QStringLiteral("X"));
        check(failedText == QStringLiteral("实例「X」完整性检查\n") + failed.error,
              "★ ZN7 没跑起来那一档：纯文本只有标题 + 那句 error", failedText);
    }

    AppPaths::setMinecraftDir(originalDir);  // 收摊
}

