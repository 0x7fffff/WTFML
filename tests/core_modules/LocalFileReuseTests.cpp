#include "Support.hpp"

void runLocalFileReuseTests() {
    section("LF1. 目标 → 当前游戏文件夹里的相对路径");
    using LocalFileReuse::relativePathWithin;
    check(relativePathWithin(QStringLiteral("D:/mc/versions/a/a.jar"), QStringLiteral("D:/mc"))
              == QStringLiteral("versions/a/a.jar"),
          "LF1 普通路径");
    check(relativePathWithin(QStringLiteral("D:/mc/versions/a/a.jar"), QStringLiteral("D:/mc/"))
              == QStringLiteral("versions/a/a.jar"),
          "LF1 根目录末尾带不带斜杠都一样");
    check(relativePathWithin(QStringLiteral("D:/mc/libraries/x.jar"), QStringLiteral("d:/MC"))
              == QStringLiteral("libraries/x.jar"),
          "LF1 盘符 / 大小写不敏感");
    check(relativePathWithin(QStringLiteral("D:/mc2/x.jar"), QStringLiteral("D:/mc")).isEmpty(),
          "★ LF1 同前缀的兄弟目录不算在里面（D:/mc2 不是 D:/mc）");
    check(relativePathWithin(QStringLiteral("D:/mc"), QStringLiteral("D:/mc")).isEmpty(),
          "LF1 就是根目录自己 ⇒ 空（它不是根里的某个文件）");
    check(relativePathWithin(QStringLiteral("D:/mcx/y"), QStringLiteral("D:/mc")).isEmpty(),
          "LF1 只差一个字符的目录也不认");
    check(relativePathWithin(QStringLiteral("D:/anything"), QStringLiteral("D:/"))
              == QStringLiteral("anything"),
          "LF1 卷根作根目录（cleanPath 之后是 `D:` 的那种形状）");
    check(relativePathWithin(QStringLiteral("D:/mc/x"), QString()).isEmpty()
              && relativePathWithin(QString(), QStringLiteral("D:/mc")).isEmpty(),
          "LF1 空的一头 ⇒ 空");

    section("LF2. 候选来源怎么排");
    using LocalFileReuse::candidatePaths;
    for (const QString &unsafe : {QStringLiteral("../outside.jar"), QStringLiteral("..\\outside.jar"),
                                 QStringLiteral("D:/outside.jar"), QStringLiteral("/outside.jar"),
                                 QStringLiteral("libraries/x.jar:stream")}) {
        check(candidatePaths(unsafe, {QStringLiteral("D:/source")}, QStringLiteral("D:/current")).isEmpty(),
              "LF2 非相对路径及路径穿越不产生来源候选", unsafe);
    }
    {
        const QStringList got = candidatePaths(
            QStringLiteral("libraries/x/y.jar"),
            {QStringLiteral("D:/source1"), QStringLiteral("D:/current"), QStringLiteral("D:/source2")},
            QStringLiteral("D:/current"));
        check(got
                  == QStringList{QStringLiteral("D:/source1/libraries/x/y.jar"),
                                 QStringLiteral("D:/source2/libraries/x/y.jar")},
              "★★ LF2 当前文件夹自己跳过；其余按调用方给的顺序", got.join(QStringLiteral(" ; ")));

        const QStringList renamed = candidatePaths(
            QStringLiteral("versions/我的实例/1.20.1.jar"), {QStringLiteral("D:/s")},
            QStringLiteral("D:/c"));
        check(renamed
                  == QStringList{QStringLiteral("D:/s/versions/我的实例/1.20.1.jar"),
                                 QStringLiteral("D:/s/versions/1.20.1/1.20.1.jar")},
              "★★ LF2 实例改过名 ⇒ 再试一次 jar 的原版目录（versions/<owner>/<owner>.jar）",
              renamed.join(QStringLiteral(" ; ")));

        const QStringList sameName = candidatePaths(QStringLiteral("versions/1.20.1/1.20.1.jar"),
                                                    {QStringLiteral("D:/s")}, QStringLiteral("D:/c"));
        check(sameName.size() == 1, "LF2 名字本来就一样 ⇒ 不多出一条重复的", sameName.join(" ; "));

        check(candidatePaths(QStringLiteral("versions/../x"), {QStringLiteral("D:/s")},
                             QStringLiteral("D:/c"))
                  .isEmpty(),
              "★ LF2 相对路径里有 `..` ⇒ 一条候选都不拼（不许跑出根目录）");
        check(candidatePaths(QStringLiteral("a/b"), {QStringLiteral("D:/current")},
                             QStringLiteral("D:/current"))
                  .isEmpty(),
              "LF2 只有当前文件夹 ⇒ 没得借");
    }

    section("LF3. 来源算不算数");
    using LocalFileReuse::Check;
    using LocalFileReuse::sourceMatches;
    QTemporaryDir temp(QDir::tempPath() + QStringLiteral("/wtfml_localreuse-XXXXXX"));
    check(temp.isValid(), "LF3 临时目录建出来了");
    const QByteArray payload = QByteArrayLiteral("pretend-jar-bytes-0123456789");
    const QString sourcePath = temp.filePath(QStringLiteral("src.bin"));
    writeFileAt(sourcePath, payload);
    const QString payloadSha1 =
        QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha1).toHex());
    const QString wrongSha1 = QString(40, QLatin1Char('0'));

    check(sourceMatches(sourcePath, payloadSha1, payload.size(), Check::SizeAndSha1),
          "LF3 大小 + SHA-1 都对 ⇒ 能用");
    std::atomic_bool cancelledReuse{true};
    check(!sourceMatches(sourcePath, payloadSha1, payload.size(), Check::SizeAndSha1, &cancelledReuse)
              && !sourceMatches(sourcePath, payloadSha1, payload.size(), Check::Exists, &cancelledReuse),
          "LF3 已取消的校验不再接受来源");
    check(!sourceMatches(sourcePath, wrongSha1, payload.size(), Check::SizeAndSha1),
          "★ LF3 SHA-1 不对 ⇒ 不用（宁可去下，也不把一份可疑的文件当官方的）");
    check(!sourceMatches(sourcePath, payloadSha1, payload.size() + 1, Check::SizeAndSha1),
          "LF3 大小不对 ⇒ 先短掉，连哈希都不算");
    check(sourceMatches(sourcePath, payloadSha1, 0, Check::Exists),
          "LF3 资源对象那一档：文件在、非空就算数");
    check(sourceMatches(sourcePath, wrongSha1, payload.size(), Check::Exists),
          "LF3 Exists 那一档不比哈希（和 fileAlreadyValid() 对盘上资源的信任级别一致）");
    check(!sourceMatches(sourcePath, QString(), payload.size(), Check::SizeAndSha1),
          "★★ LF3 没有 SHA-1 的文件一律不借（同名 ≠ 同内容）");
    const QString emptyPath = temp.filePath(QStringLiteral("empty.bin"));
    writeFileAt(emptyPath, QByteArray());
    check(!sourceMatches(emptyPath, payloadSha1, 0, Check::Exists),
          "★ LF3 0 字节不算数（哪一档都不算）");
    check(!sourceMatches(temp.filePath(QStringLiteral("nope.bin")), payloadSha1,
                         payload.size(), Check::SizeAndSha1),
          "LF3 文件不存在 ⇒ 不用");

    section("LF4. 硬链接 / 复制");
    using LocalFileReuse::materialize;
    using LocalFileReuse::Method;
    QString error;
    const QString linkDest = temp.filePath(QStringLiteral("copy/linked.bin"));
    const Method linked = materialize(sourcePath, linkDest, payloadSha1, true, nullptr,
                                      /*allowHardLink=*/true, &error);
    check(linked == Method::HardLink, "LF4 同一卷 ⇒ 硬链接（一个字节都不拷）", error);
    {
        QFile in(linkDest);
        check(in.open(QIODevice::ReadOnly) && in.readAll() == payload,
              "LF4 硬链接出来的目标内容一模一样");
    }

    error.clear();
    const QString copyDest = temp.filePath(QStringLiteral("copy/deep/copied.bin"));
    const Method copied = materialize(sourcePath, copyDest, payloadSha1, true, nullptr,
                                      /*allowHardLink=*/false, &error);
    check(copied == Method::Copy, "LF4 关掉硬链接 ⇒ 走复制那条路（跨卷时就是它）", error);
    {
        QFile in(copyDest);
        check(in.open(QIODevice::ReadOnly) && in.readAll() == payload,
              "LF4 复制出来的目标内容一模一样");
    }

    error.clear();
    const QString badDest = temp.filePath(QStringLiteral("copy/bad.bin"));
    const Method wrong = materialize(sourcePath, badDest, wrongSha1, true, nullptr,
                                     /*allowHardLink=*/false, &error);
    check(wrong == Method::None, "★ LF4 边写边核、SHA-1 对不上 ⇒ 这一份作废", error);
    check(!QFile::exists(badDest),
          "★★ LF4 作废的那一份**没留在盘上**（QSaveFile 的临时文件也跟着撤了）");

    section("LF5. 一整套 reuseFrom()");
    using LocalFileReuse::reuseFrom;
    const QString currentRoot = temp.filePath(QStringLiteral("current"));
    const QString sourceRoot = temp.filePath(QStringLiteral("source"));
    const QString relPath = QStringLiteral("versions/1.20.1/1.20.1.jar");
    writeFileAt(sourceRoot + QLatin1Char('/') + relPath, payload);
    const QString targetPath = currentRoot + QLatin1Char('/') + relPath;
    error.clear();
    const Method reused = reuseFrom(targetPath, relPath, {sourceRoot}, currentRoot, payloadSha1,
                                    payload.size(), Check::SizeAndSha1, nullptr,
                                    /*allowHardLink=*/true, &error);
    check(reused != Method::None, "LF5 来源里有 ⇒ 借到了", error);
    {
        QFile in(targetPath);
        check(in.open(QIODevice::ReadOnly) && in.readAll() == payload,
              "LF5 目标路径上那份内容正确（父目录不用调用方先建）");
    }

    error.clear();
    const QString missingTarget = currentRoot + QStringLiteral("/versions/9.9/9.9.jar");
    const Method missed =
        reuseFrom(missingTarget, QStringLiteral("versions/9.9/9.9.jar"), {sourceRoot}, currentRoot,
                  payloadSha1, payload.size(), Check::SizeAndSha1, nullptr, true, &error);
    check(missed == Method::None && error == QStringLiteral("no matching source file"),
          "LF5 来源里没有这一份 ⇒ None，并且原因说清楚", error);
    check(!QFile::exists(missingTarget), "LF5 没借到的时候目标一个字节都不留");

    error.clear();
    const Method onlyCurrent = reuseFrom(
        currentRoot + QStringLiteral("/x.bin"), QStringLiteral("x.bin"), {currentRoot},
        currentRoot, payloadSha1, payload.size(), Check::SizeAndSha1, nullptr, true, &error);
    check(onlyCurrent == Method::None, "LF5 只有当前文件夹 ⇒ 没得借（不自己借自己）");

    section("LF6. DownloadManager：借到了就真的不发网络请求");
    // ---------------------------------------------------------------------
    // 目标路径必须是**当前游戏文件夹里**的（相对路径是候选来源的拼法），所以这一小段
    // 换一下全局根目录，跑完还原。
    const QString originalDir = AppPaths::minecraftDir();
    {
        QTemporaryDir gameRoot(QDir::tempPath() + QStringLiteral("/wtfml_reuse-game-XXXXXX"));
        check(gameRoot.isValid(), "LF6 临时游戏文件夹建出来了");
        const QString currentDir = gameRoot.filePath(QStringLiteral("current"));
        const QString sourceDir = gameRoot.filePath(QStringLiteral("source"));
        QDir().mkpath(currentDir);

        const QByteArray jarBytes = QByteArrayLiteral("pretend-client-jar-1.20.1");
        const QString sha = QString::fromLatin1(
            QCryptographicHash::hash(jarBytes, QCryptographicHash::Sha1).toHex());
        const QString jarRel = QStringLiteral("versions/1.20.1/1.20.1.jar");
        writeFileAt(sourceDir + QLatin1Char('/') + jarRel, jarBytes);
        AppPaths::setMinecraftDir(currentDir);

        // 一条**一定取不回来**的地址：真去下就必然失败。于是"这一批成功"本身就是
        // "一个网络请求都没发"的证明（比数日志硬）。
        const QString deadUrl = QStringLiteral("file:///wtfml/no-such-file.jar");

        const auto runBatch = [&deadUrl](DownloadManager &dm, const QString &dest,
                                         const QString &expectedSha1, qint64 expectedSize,
                                         VerifyMode mode, bool flagged) {
            dm.beginBatch();
            if (flagged) {
                dm.setFlaggedPaths({dest});
            }
            QEventLoop loop;
            bool success = false;
            bool timedOut = false;
            const QMetaObject::Connection conn =
                QObject::connect(&dm, &DownloadManager::allDownloadsFinished, &loop,
                                 [&](bool ok) {
                                     success = ok;
                                     loop.quit();
                                 });
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
                timedOut = true;
                loop.quit();
            });
            dm.downloadFile(deadUrl, dest, expectedSha1, expectedSize, mode);
            dm.endBatch();
            timeout.start(10000);
            loop.exec();
            QObject::disconnect(conn);
            return QPair<bool, bool>{success, timedOut};
        };

        {
            DownloadManager dm;
            dm.setReuseSourceFolders({sourceDir});
            const QString dest = currentDir + QLatin1Char('/') + jarRel;
            int finishedSignals = 0;
            bool finishedOk = false;
            QObject::connect(&dm, &DownloadManager::downloadFinished, &dm,
                             [&](const QString &, bool ok, const QString &) {
                                 ++finishedSignals;
                                 finishedOk = ok;
                             });
            const QPair<bool, bool> result =
                runBatch(dm, dest, sha, jarBytes.size(), VerifyMode::SizeAndSha1, false);
            const DownloadStats st = dm.stats();
            check(!result.second, "LF6 借文件在 10 秒内收尾（没挂住）");
            check(result.first && st.failed == 0,
                  "★★ LF6 整批成功 ⇒ 那条一定失败的地址根本没被请求过");
            check(st.total == 0 && st.done == 0,
                  "★ LF6 借来的**不进** total / done（进度条分母 = 真要下的）",
                  QStringLiteral("total=%1 done=%2").arg(st.total).arg(st.done));
            check(st.reused == 1, "LF6 reused = 1", QString::number(st.reused));
            check(finishedSignals == 1 && finishedOk, "LF6 每一个借到的文件也发一次 downloadFinished(true)");
            check(!dm.isBusy(), "★ LF6 收尾之后 isBusy() 为假（否则界面一直忙）");
            QFile in(dest);
            check(in.open(QIODevice::ReadOnly) && in.readAll() == jarBytes,
                  "LF6 目标路径上的内容正确");
        }

        section("LF7. 体检点名 / AlwaysDownload 那两条路**不许**借");
        // -----------------------------------------------------------------
        // 它们的意思就是"盘上/本机任何一份都不算数、重新拿一次"。借过来一份同样不算数，
        // 而那条假地址必然失败 —— 失败本身就是"真去下了"的证明。
        {
            const QString flaggedRel = QStringLiteral("libraries/flag/flag.jar");
            writeFileAt(sourceDir + QLatin1Char('/') + flaggedRel, jarBytes);
            DownloadManager dm;
            dm.setReuseSourceFolders({sourceDir});
            const QString dest = currentDir + QLatin1Char('/') + flaggedRel;
            const QPair<bool, bool> result =
                runBatch(dm, dest, sha, jarBytes.size(), VerifyMode::SizeAndSha1, true);
            const DownloadStats st = dm.stats();
            check(!result.second, "LF7 点名那一条也在 10 秒内收尾");
            check(!result.first && st.failed == 1 && st.reused == 0,
                  "★★ LF7 体检点名 ⇒ 不去别的文件夹借，老老实实重下",
                  QStringLiteral("failed=%1 reused=%2").arg(st.failed).arg(st.reused));
            check(!QFile::exists(dest), "LF7 没借 ⇒ 目标不存在（借了的话它会躺在那儿）");
        }
        {
            const QString alwaysRel = QStringLiteral("libraries/always/always.jar");
            writeFileAt(sourceDir + QLatin1Char('/') + alwaysRel, jarBytes);
            DownloadManager dm;
            dm.setReuseSourceFolders({sourceDir});
            const QString dest = currentDir + QLatin1Char('/') + alwaysRel;
            const QPair<bool, bool> result =
                runBatch(dm, dest, sha, jarBytes.size(), VerifyMode::AlwaysDownload, false);
            const DownloadStats st = dm.stats();
            check(!result.second, "LF7 AlwaysDownload 也在 10 秒内收尾");
            check(!result.first && st.failed == 1 && st.reused == 0,
                  "★★ LF7 AlwaysDownload ⇒ 不去别的文件夹借",
                  QStringLiteral("failed=%1 reused=%2").arg(st.failed).arg(st.reused));
        }

        // 没有来源根 = 这个功能关着（默认形态）：同一条作业老老实实去网络，失败。
        {
            DownloadManager dm;
            const QString rel = QStringLiteral("libraries/nosrc/nosrc.jar");
            writeFileAt(sourceDir + QLatin1Char('/') + rel, jarBytes);
            const QString dest = currentDir + QLatin1Char('/') + rel;
            const QPair<bool, bool> result =
                runBatch(dm, dest, sha, jarBytes.size(), VerifyMode::SizeAndSha1, false);
            const DownloadStats st = dm.stats();
            check(!result.second, "LF7 没有来源根时也在 10 秒内收尾");
            check(!result.first && st.failed == 1 && st.reused == 0,
                  "LF7 来源根为空 ⇒ 一个文件都不去看（默认关着）",
                  QStringLiteral("failed=%1 reused=%2").arg(st.failed).arg(st.reused));
        }
    }
    AppPaths::setMinecraftDir(originalDir == AppPaths::defaultMinecraftDir() ? QString()
                                                                             : originalDir);

    section("LF8. 允许的来源只有那两处");
    {
        const QString appData = qEnvironmentVariable("APPDATA");
        const QString official = FolderManager::officialLauncherFolder();
        if (appData.isEmpty()) {
            check(official.isEmpty(), "LF8 没有 APPDATA ⇒ 说没有这一份（不猜别的位置）");
        } else {
            check(official == QDir::cleanPath(appData + QStringLiteral("/.minecraft")),
                  "★ LF8 官方启动器那份 = %APPDATA%\\.minecraft", official);
        }

        // 用 nullptr 的 LauncherSettings：只看"允许的来源"这一条，不碰用户的设置。
        const FolderManager manager(nullptr);
        const QStringList roots = manager.reuseSourceFolders();
        bool allDirs = true;
        for (const QString &root : roots) {
            allDirs = allDirs && QFileInfo(root).isDir();
        }
        check(allDirs, "LF8 列出来的每一份都是**盘上真的存在**的目录",
              roots.join(QStringLiteral(" ; ")));
        bool duplicate = false;
        for (int i = 0; i < roots.size(); ++i) {
            for (int j = i + 1; j < roots.size(); ++j) {
                duplicate = duplicate
                            || QDir::cleanPath(roots.at(i)).compare(QDir::cleanPath(roots.at(j)),
                                                                    Qt::CaseInsensitive)
                                   == 0;
            }
        }
        check(!duplicate, "LF8 不重复（官方那份往往就是用户添加过的那一份）");
        if (!official.isEmpty() && QFileInfo(official).isDir()) {
            check(roots.contains(official), "LF8 官方那份存在 ⇒ 在里面");
        }
        bool invented = false;
        for (const QString &root : roots) {
            bool known = false;
            for (const QString &folder : manager.folders()) {
                known = known
                        || QDir::cleanPath(folder).compare(QDir::cleanPath(root),
                                                           Qt::CaseInsensitive)
                               == 0;
            }
            known = known
                    || (!official.isEmpty()
                        && QDir::cleanPath(official).compare(QDir::cleanPath(root),
                                                             Qt::CaseInsensitive)
                               == 0);
            invented = invented || !known;
        }
        check(!invented, "★★ LF8 一个来源都不是自己编的（只来自 folders() + 官方那份）");
    }
}

