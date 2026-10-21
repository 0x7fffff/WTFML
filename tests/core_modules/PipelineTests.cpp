#include "Support.hpp"

int runPipelineTests(QCoreApplication &app, const QString &sandbox) {
    const QString work = sandbox + "/synth";
    QDir(work).removeRecursively();
    const QString remoteDir = work + "/remote";

    // "远端"资源：客户端 jar + 2 个库
    const synth::RemoteFile jar =
        synth::makeRemote(remoteDir, QStringLiteral("client.jar"), QByteArray("client-jar-body"));
    QList<synth::RemoteFile> libs;
    for (int i = 0; i < 2; ++i) {
        libs << synth::makeRemote(remoteDir, QStringLiteral("libs/lib%1.jar").arg(i),
                                 QStringLiteral("lib-body-%1").arg(i).toUtf8());
    }
    // 资产索引（DownloadManager 会无条件重取它）
    // asset 对象预先放进 objects/<hash 前两位>/<hash>，名字就是内容的 SHA1
    QJsonObject objects;
    const QString objectsRoot = sandbox + "/assets/objects";
    for (int i = 0; i < 3; ++i) {
        const QByteArray content = QStringLiteral("asset-body-%1").arg(i).toUtf8();
        const QString hash = synth::sha1Of(content);
        const QString path = objectsRoot + "/" + hash.left(2) + "/" + hash;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(content);
            f.close();
        }
        objects[QStringLiteral("minecraft/sound%1.ogg").arg(i)] =
            QJsonObject{{QStringLiteral("hash"), hash}, {QStringLiteral("size"), content.size()}};
    }
    const synth::RemoteFile index = synth::makeRemote(
        remoteDir, QStringLiteral("indexes/synthidx.json"),
        QJsonDocument(QJsonObject{{QStringLiteral("objects"), objects}}).toJson());

    const auto buildJson = [&](const QString &jarUrl, const QString &jarSha1, qint64 jarSize) {
        QJsonArray libArr;
        for (int i = 0; i < libs.size(); ++i) {
            libArr.append(QJsonObject{
                {QStringLiteral("name"), QStringLiteral("synth:lib%1:1.0").arg(i)},
                {QStringLiteral("downloads"),
                 QJsonObject{{QStringLiteral("artifact"),
                              QJsonObject{
                                  {QStringLiteral("url"), libs[i].url},
                                  {QStringLiteral("sha1"), libs[i].sha1},
                                  {QStringLiteral("size"), libs[i].size},
                                  {QStringLiteral("path"),
                                   QStringLiteral("synth/lib%1/1.0/lib%1-1.0.jar").arg(i)}}}}}});
        }
        return QJsonObject{
            {QStringLiteral("id"), QStringLiteral("synth-1")},
            {QStringLiteral("libraries"), libArr},
            {QStringLiteral("downloads"),
             QJsonObject{{QStringLiteral("client"),
                          QJsonObject{{QStringLiteral("url"), jarUrl},
                                      {QStringLiteral("sha1"), jarSha1},
                                      {QStringLiteral("size"), jarSize}}}}},
            {QStringLiteral("assetIndex"),
             QJsonObject{{QStringLiteral("id"), QStringLiteral("synthidx")},
                         {QStringLiteral("url"), index.url},
                         {QStringLiteral("sha1"), index.sha1},
                         {QStringLiteral("size"), index.size},
                         {QStringLiteral("totalSize"), 0}}}};
    };
    const QJsonObject goodJson = buildJson(jar.url, jar.sha1, jar.size);

    // 一次批次**登记**的作业 = jar 1 + 库 2 + 索引 1 + asset 对象 3
    const int kRegisteredJobs = 7;
    // asset 对象（索引里点名的内容）和索引本身都落在 AppPaths::assetsDir() 下，也就是沙盒的
    // .minecraft/assets —— downloadAssets() 现在不收目录参数了，两边必然同源。所以这 3 个
    // 对象在任何一批里都"已经存在"，会走 ExistsOnly 的跳过分支（也正好验证了这条分支）。
    const int kAssetObjects = 3;
    // 冷批次**真的下**的那几个：jar 1 + 库 2。3 个对象全在盘上（跳过），索引不计进分母。
    const int kColdDownloads = 3;
    // 暖批次里只有索引还值得重取（它走 onAssetIndexDownloaded 那条无条件重取的路，
    // 不经过 fileAlreadyValid，和 VerifyMode 无关），其余 6 项全部跳过 —— 而索引不进分母，
    // 所以暖批次的分母是 **0**。
    const int kWarmSkipped = kRegisteredJobs - 1;

    const auto startGood = [&](const QString &base) {
        return [&, base](DownloadManager &mgr) {
            const QJsonObject client = goodJson[QStringLiteral("downloads")].toObject()
                                           [QStringLiteral("client")].toObject();
            mgr.downloadClientJar(QStringLiteral("synth-1"),
                                  client[QStringLiteral("url")].toString(),
                                  base + "/versions/synth-1/synth-1.jar",
                                  client[QStringLiteral("sha1")].toString(),
                                  client[QStringLiteral("size")].toVariant().toLongLong());
            mgr.downloadLibraries(mgr.extractLibraries(goodJson), base + "/libraries");
            mgr.downloadAssets(mgr.extractAssetIndex(goodJson));
        };
    };

    const QString coldBase = work + "/cold";
    const QString warmBase = work + "/warm";

    section("F. 下载批次记账（DownloadManager）");

    // F1 冷批次：jar/库/索引都不存在，真下；3 个 asset 对象已在盘上，跳过
    {
        const BatchObservation o = runBatch(app, startGood(coldBase));
        out() << "        冷批次: emissions=" << o.emissions << " total=" << o.stats.total
              << " done=" << o.stats.done << " failed=" << o.stats.failed
              << " skipped=" << o.stats.skipped << "\n";
        check(o.emissions == 1, "F1 冷批次: allDownloadsFinished 恰好发射 1 次",
              QString::number(o.emissions));
        check(o.success, "F1 冷批次: success == true");
        check(o.stats.total == kColdDownloads && o.stats.done == kColdDownloads,
              "F1 冷批次: total == done == 3（分母里只有真要下的 jar + 两个库）",
              QString("%1/%2").arg(o.stats.done).arg(o.stats.total));
        check(o.stats.failed == 0, "F1 冷批次: 无失败");
        check(o.stats.skipped == kAssetObjects,
              "F1 冷批次: 只有已在盘上的 asset 对象被跳过（ExistsOnly）",
              QString("skipped=%1").arg(o.stats.skipped));
        // 用户看进度条时读到的就是这两个数。从前 total 里塞着"跳过的 + 索引"，
        // 两个数加不出登记总数；现在分母 + 跳过 = 登记的作业 - 索引那一格。
        check(o.stats.total + o.stats.skipped == kRegisteredJobs - 1,
              "★ F1 冷批次: 分母 + 跳过 = 登记总数 - 索引那一格（两个数终于对得上）",
              QString("%1 + %2 vs %3").arg(o.stats.total).arg(o.stats.skipped).arg(kRegisteredJobs));
        check(o.busyWhileInFlight && o.busyAfterEndBatch,
              "F1 冷批次: 作业在途时 isBusy() 为真（含 endBatch 之后）");
        check(!o.busyAfter, "F1 冷批次: 全部结束后 isBusy() 为假");
    }

    // F2 暖批次：先把文件铺好，再跑第二次 —— 除索引（Always）外全部跳过
    {
        const BatchObservation first = runBatch(app, startGood(warmBase));
        check(first.emissions == 1 && first.stats.failed == 0,
              "F2 铺垫: 第一次下载成功且只发一次信号",
              QString("emissions=%1 failed=%2").arg(first.emissions).arg(first.stats.failed));

        const BatchObservation o = runBatch(app, startGood(warmBase));
        out() << "        暖批次: emissions=" << o.emissions << " total=" << o.stats.total
              << " done=" << o.stats.done << " skipped=" << o.stats.skipped << "\n";
        check(o.emissions == 1, "F2 暖批次: allDownloadsFinished 恰好发射 1 次",
              QString::number(o.emissions));
        check(o.success, "F2 暖批次: success == true");
        check(o.stats.skipped == kWarmSkipped,
              "F2 暖批次: 6 个已存在文件被跳过（索引永远重取）",
              QString("skipped=%1").arg(o.stats.skipped));
        check(o.stats.total == 0 && o.stats.done == 0,
              "★ F2 暖批次: 一个都不用下 ⇒ 分母是 0（改动前 total == done == 7："
              "分母里塞着 6 个跳过的 + 1 个索引）",
              QString("%1/%2").arg(o.stats.done).arg(o.stats.total));
        check(!o.busyAfter, "F2 暖批次: 结束后 isBusy() 为假");
    }

    // F3 只刷新索引：其余文件都在，只把索引文件删掉
    // NOTE: 索引现在和 objects 同源，落在 AppPaths::assetsDir()（= 沙盒的 .minecraft/assets）
    // 下，不再是 base + "/assets"。删它其实是多余的（索引无条件重取），留着是为了
    // 让这一节的名字对得上它想证明的事。
    {
        QFile::remove(sandbox + "/assets/indexes/synthidx.json");
        const BatchObservation o = runBatch(app, startGood(warmBase));
        check(o.emissions == 1, "F3 只刷新索引: 仍然发射 1 次（改动前一次都不发）",
              QString::number(o.emissions));
        check(o.success, "F3 只刷新索引: success == true");
        check(o.stats.skipped == kWarmSkipped, "F3 只刷新索引: 其余 6 个跳过",
              QString("skipped=%1").arg(o.stats.skipped));
        check(o.stats.total == 0,
              "★ F3 只刷新索引: 索引那一条不进分母（它是「清单」不是这个实例的文件）",
              QString("total=%1").arg(o.stats.total));
    }

    // F4 失败注入：客户端 jar 指向不存在的文件
    {
        const QString badUrl = QUrl::fromLocalFile(work + "/no-such-file.jar").toString();
        const QString failBase = work + "/fail";
        const QJsonObject badJson = buildJson(badUrl, jar.sha1, jar.size);
        const BatchObservation o = runBatch(app, [&](DownloadManager &mgr) {
            const QJsonObject client = badJson[QStringLiteral("downloads")].toObject()
                                           [QStringLiteral("client")].toObject();
            mgr.downloadClientJar(QStringLiteral("synth-1"),
                                  client[QStringLiteral("url")].toString(),
                                  failBase + "/versions/synth-1/synth-1.jar",
                                  client[QStringLiteral("sha1")].toString(),
                                  client[QStringLiteral("size")].toVariant().toLongLong());
            mgr.downloadLibraries(mgr.extractLibraries(badJson), failBase + "/libraries");
            mgr.downloadAssets(mgr.extractAssetIndex(badJson));
        });
        out() << "        失败注入: emissions=" << o.emissions << " success=" << o.success
              << " failed=" << o.stats.failed << "\n";
        for (const QString &p : o.stats.failedPaths) {
            out() << "        失败路径: " << p << "\n";
        }
        check(o.emissions == 1, "F4 失败: 仍然恰好发射 1 次", QString::number(o.emissions));
        check(!o.success, "F4 失败: success == false（改动前硬编码 true，失败报不出来）");
        check(o.stats.failed == 1, "F4 失败: failed == 1",
              QString::number(o.stats.failed));
        check(o.stats.failedPaths.contains(failBase + "/versions/synth-1/synth-1.jar"),
              "F4 失败: 失败清单里有那一项的完整路径");
        check(!o.busyAfter, "F4 失败: 结束后 isBusy() 为假（按钮不会永久变灰）");
    }

    // F5 同一个实例连跑两批：计数必须重置，不能叠加
    {
        DownloadManager mgr;
        int emissions = 0;
        QList<bool> successes;
        QObject::connect(&mgr, &DownloadManager::allDownloadsFinished, &app,
                         [&](bool ok) { ++emissions; successes << ok; });
        const QString base = work + "/reuse";
        const auto registerAll = [&]() {
            const QJsonObject client = goodJson[QStringLiteral("downloads")].toObject()
                                           [QStringLiteral("client")].toObject();
            mgr.downloadClientJar(QStringLiteral("synth-1"),
                                  client[QStringLiteral("url")].toString(),
                                  base + "/versions/synth-1/synth-1.jar",
                                  client[QStringLiteral("sha1")].toString(),
                                  client[QStringLiteral("size")].toVariant().toLongLong());
            mgr.downloadLibraries(mgr.extractLibraries(goodJson), base + "/libraries");
            mgr.downloadAssets(mgr.extractAssetIndex(goodJson));
        };

        bool finished1 = false;
        QEventLoop loop1;
        QObject::connect(&mgr, &DownloadManager::allDownloadsFinished, &app,
                         [&](bool) { finished1 = true; loop1.quit(); });
        mgr.beginBatch();
        registerAll();
        mgr.endBatch();
        if (!finished1) loop1.exec();
        const DownloadStats s1 = mgr.stats();

        bool finished2 = false;
        QEventLoop loop2;
        QObject::connect(&mgr, &DownloadManager::allDownloadsFinished, &app,
                         [&](bool) { finished2 = true; loop2.quit(); });
        mgr.beginBatch();
        registerAll();
        mgr.endBatch();
        if (!finished2) loop2.exec();
        const DownloadStats s2 = mgr.stats();

        out() << "        复用实例: 第一批 skipped=" << s1.skipped << "，第二批 skipped="
              << s2.skipped << "，emissions=" << emissions << "\n";
        check(emissions == 2, "F5 连跑两批: 每批各发射 1 次", QString::number(emissions));
        check(s1.total == kColdDownloads && s2.total == 0,
              "F5 连跑两批: 每批的分母各自重算（冷 3 / 暖 0 —— 不是上一批留下的 7）",
              QString("%1, %2").arg(s1.total).arg(s2.total));
        check(s1.skipped == kAssetObjects && s2.skipped == kWarmSkipped,
              "F5 连跑两批: 第二批全部走跳过分支",
              QString("skipped=%1/%2").arg(s1.skipped).arg(s2.skipped));
        check(!mgr.isBusy(), "F5 连跑两批: 结束后 isBusy() 为假");
    }

    // F6 索引 SHA1 校验：把 assetIndex.sha1 故意写错。改动前这条路径写完文件就
    // success = true（expectedSha1s 里登记了却没人读），镜像源拼错索引时会拿一份
    // **内容不对的索引**去下几千个资源，而且要等资源层面才炸、看不出根因。
    // 改了之后：整批失败、坏索引删掉、后面的解析根本不跑。
    {
        QJsonObject badIndexJson = goodJson;
        QJsonObject ai = badIndexJson[QStringLiteral("assetIndex")].toObject();
        ai[QStringLiteral("sha1")] = QString(40, QLatin1Char('0'));
        badIndexJson[QStringLiteral("assetIndex")] = ai;

        const QString indexPath = sandbox + "/assets/indexes/synthidx.json";
        QFile::remove(indexPath);  // 先清掉，才能断言"坏索引没被留在盘上"

        const QString badBase = work + "/badindex";
        const BatchObservation o = runBatch(app, [&](DownloadManager &mgr) {
            const QJsonObject client = goodJson[QStringLiteral("downloads")].toObject()
                                           [QStringLiteral("client")].toObject();
            mgr.downloadClientJar(QStringLiteral("synth-1"),
                                  client[QStringLiteral("url")].toString(),
                                  badBase + "/versions/synth-1/synth-1.jar",
                                  client[QStringLiteral("sha1")].toString(),
                                  client[QStringLiteral("size")].toVariant().toLongLong());
            mgr.downloadLibraries(mgr.extractLibraries(goodJson), badBase + "/libraries");
            mgr.downloadAssets(mgr.extractAssetIndex(badIndexJson));
        });
        out() << "        索引 SHA1: emissions=" << o.emissions << " success=" << o.success
              << " failed=" << o.stats.failed << " total=" << o.stats.total << "\n";
        check(!o.success, "F6 索引 SHA1 不对: 整批报失败（改动前 success == true）");
        check(o.stats.failed == 1, "F6 索引 SHA1 不对: 只有索引这一项算失败",
              QString::number(o.stats.failed));
        check(o.stats.failedPaths.contains(indexPath),
              "F6 索引 SHA1 不对: 失败清单里是索引文件本身");
        check(!QFile::exists(indexPath), "F6 索引 SHA1 不对: 坏索引没有留在盘上");
        check(o.stats.total == 4,
              "★ F6 索引 SHA1 不对: 索引没被解析（否则会多出 3 个 asset 任务）—— "
              "分母为 jar 1 + 库 2 + 失败索引 1；成功索引不计进度，失败索引计入并结算",
              QString("total=%1").arg(o.stats.total));
    }

    // F6b 索引"能解析、但不是索引"：镜像/代理回一段合法 JSON 的错误体（{"error":...}）。
    //
    // 资源对象**故意不占** total/done 的格子（见 downloadAssets），所以从
    // 前解析失败只 `qWarning` 一句就 return —— 整批照样以"0 个资源、全部成功"收尾：
    // 版本装好了，材质音效一个都没有，界面上找不到任何异常。现在解析失败必须让索引这
    // 一条算失败（走镜像退官方 → handleDownloadError），用户至少能看见"索引解析失败"。
    {
        const synth::RemoteFile brokenIndex = synth::makeRemote(
            remoteDir, QStringLiteral("indexes/brokenidx.json"),
            QByteArray(R"({"error":"mirror says no"})"));
        QJsonObject brokenJson = goodJson;
        QJsonObject ai = brokenJson[QStringLiteral("assetIndex")].toObject();
        ai[QStringLiteral("id")] = QStringLiteral("brokenidx");
        ai[QStringLiteral("url")] = brokenIndex.url;
        ai[QStringLiteral("sha1")] = brokenIndex.sha1;
        ai[QStringLiteral("size")] = brokenIndex.size;
        brokenJson[QStringLiteral("assetIndex")] = ai;

        const QString indexPath = sandbox + "/assets/indexes/brokenidx.json";
        QFile::remove(indexPath);  // 先清掉，才能断言"坏索引没被留在盘上"

        const QString brokenBase = work + "/brokenindex";
        const BatchObservation o = runBatch(app, [&](DownloadManager &mgr) {
            const QJsonObject client = goodJson[QStringLiteral("downloads")].toObject()
                                           [QStringLiteral("client")].toObject();
            mgr.downloadClientJar(QStringLiteral("synth-1"),
                                  client[QStringLiteral("url")].toString(),
                                  brokenBase + "/versions/synth-1/synth-1.jar",
                                  client[QStringLiteral("sha1")].toString(),
                                  client[QStringLiteral("size")].toVariant().toLongLong());
            mgr.downloadLibraries(mgr.extractLibraries(brokenJson), brokenBase + "/libraries");
            mgr.downloadAssets(mgr.extractAssetIndex(brokenJson));
        });
        out() << "        索引不是索引: emissions=" << o.emissions << " success=" << o.success
              << " failed=" << o.stats.failed << " total=" << o.stats.total << "\n";
        check(!o.success, "★ F6b 索引是合法 JSON 但不是资源索引: 整批报失败（改动前 success == true）");
        check(o.stats.failed == 1 && o.stats.failedPaths.contains(indexPath),
              "F6b 失败清单里是索引文件本身（不是 5000 个资源对象）",
              QStringLiteral("failed=%1 contains=%2")
                  .arg(o.stats.failed)
                  .arg(o.stats.failedPaths.contains(indexPath)));
        check(!QFile::exists(indexPath), "F6b 坏索引没有留在盘上");
        check(o.stats.total == 4,
              "F6b 一个 asset 任务都没排（分母 = jar 1 + 库 2 + 失败索引 1）",
              QString("total=%1").arg(o.stats.total));
    }

    // F7 点名重下（setFlaggedPaths）：盘上那份**好好的**，只有"点名"这一个理由会让它重下。
    //
    // 这是体检 → 补全那条链的最后一环（链子的另外几截在 G5.9-G5.11）：体检按字节数把资源
    // 对象判成"坏"，而补全那一档只看"在不在"（ExistsOnly，几千个文件全量哈希要几秒）——
    // 于是体检说"要修 1 个"、补全说"一个都不用下"。点名之后补全就不问盘上那份了。
    // 这里拿 F2 那个暖批次（文件全都在）当对照：**同一批文件，点不点名分母就不同**。
    {
        const QString warmJar = warmBase + "/versions/synth-1/synth-1.jar";
        check(QFile::exists(warmJar), "F7 前提: 暖批次那份 jar 在盘上（F2 下的）");
        const BatchObservation o = runBatch(app, [&](DownloadManager &mgr) {
            mgr.setFlaggedPaths(QStringList{warmJar});
            startGood(warmBase)(mgr);
        });
        out() << "        点名重下: total=" << o.stats.total << " skipped=" << o.stats.skipped << "\n";
        check(o.emissions == 1 && o.success, "F7 点名重下: 整批照旧一个成功终态");
        check(o.stats.total == 1 && o.stats.done == 1,
              "★ F7 点名的那个进了分母（同一批**不点名**时是 0 —— 见 F2）",
              QString("total=%1").arg(o.stats.total));
        check(o.stats.skipped == kWarmSkipped - 1,
              "★ F7 只少了它一个跳过（其余照旧跳过：点名是**按路径**认人，不是整批重下）",
              QString("skipped=%1").arg(o.stats.skipped));
        check(o.stats.failed == 0, "F7 那条是从 file:// 重取的，没有失败");

        // 反面：点一条**对不上号**的路径 ⇒ 一条都不多下（对照 F2 的 0/0）。
        const BatchObservation bogus = runBatch(app, [&](DownloadManager &mgr) {
            mgr.setFlaggedPaths(QStringList{warmBase + "/nobody/else.jar"});
            startGood(warmBase)(mgr);
        });
        check(bogus.stats.total == 0 && bogus.stats.skipped == kWarmSkipped,
              "★ F7 点名表里对不上号的路径什么都不做（分母 0、6 个照旧跳过）",
              QString("total=%1 skipped=%2").arg(bogus.stats.total).arg(bogus.stats.skipped));
    }

    return g_failures == 0 ? 0 : 1;
}

