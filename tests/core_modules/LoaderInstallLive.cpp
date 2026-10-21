#include "Support.hpp"

int runLoaderInstallLive(QCoreApplication &app, const QString &sandbox,
                                const QString &gameVersion, VersionManager::LoaderKind kind,
                                bool useMirror) {
    const QString versionsRoot = sandbox + "/versions";
    const QString parentDir = versionsRoot + "/" + gameVersion;
    const QString loaderName = VersionManager::loaderDisplayName(kind);
    out() << "loader-install: " << loaderName << " / 原版 " << gameVersion << " 在沙箱里吗 -> json="
          << (QFile::exists(parentDir + "/" + gameVersion + ".json") ? "有" : "没有")
          << " jar=" << (QFile::exists(parentDir + "/" + gameVersion + ".jar") ? "有" : "没有")
          << "\n";
    out().flush();
    if (!QFile::exists(parentDir + "/" + gameVersion + ".json") ||
        !QFile::exists(parentDir + "/" + gameVersion + ".jar")) {
        // 这个模式只负责"把加载器装到已经装好的原版上"，原版自己都没装全就不是它的活：
        // 直接拒绝，别让产品替它补原版（那会把两种失败混在一起，看不出是谁的问题）。
        out() << "  原版不完整（json/jar 缺一个），先把 " << gameVersion << " 装全再来\n";
        return 2;
    }

    VersionManager vm;
    // 镜像源开关：产品里是「设置 -> 下载源」在下载那一刻交给它的（MainWindow 里那一行），
    // 这里直接拨，好让这条路也能按镜像跑一遍。
    vm.setUseMirror(useMirror);
    out() << "  下载源 = " << (useMirror ? "镜像源（bmclapi）" : "官方源") << "\n";
    out().flush();
    QString installedId;
    QString message;
    bool finished = false;
    bool succeeded = false;
    QObject::connect(&vm, &VersionManager::loaderInstallMessage, &app,
                     [](const QString &text) { out() << "  · " << text << "\n"; out().flush(); });

    QEventLoop loop;
    QObject::connect(&vm, &VersionManager::versionDownloadFinished, &app,
                     [&](const QString &id, bool ok, const QString &msg) {
                         installedId = id;
                         message = msg;
                         succeeded = ok;
                         finished = true;
                     });
    QObject::connect(&vm, &VersionManager::versionDownloadFinished, &loop, [&] { loop.quit(); });

    // --- 安装器要用的那份 Java ---------------------------------------------------
    //
    // 断言写成**与机器无关**的形式（live 模式在哪台机器上跑都得站得住）：
    // - 说成功 → 挑中的那份主版本必须真的够新（拿 Java 8 去跑一个要 17 的安装器，用户
    // 看到的是 UnsupportedClassVersionError 那串鬼话，不是"该装个新 Java"）；
    // - 说失败 → 必须给出一句能照做的理由。
    // 本机（Java 21）走的是前一条。
    if (kind != VersionManager::LoaderKind::Fabric) {
        int requiredMajor = 0;
        QFile versionFile(parentDir + "/" + gameVersion + ".json");
        if (versionFile.open(QIODevice::ReadOnly)) {
            requiredMajor = QJsonDocument::fromJson(versionFile.readAll())
                                .object()["javaVersion"]
                                .toObject()["majorVersion"]
                                .toInt();
            versionFile.close();
        }
        check(requiredMajor > 0, "沙箱里那份原版 json 说了它要哪个 Java",
              QString::number(requiredMajor));

        const QString kBogusJava = QStringLiteral("C:/没有这个目录/java.exe");
        QString picked;
        QString reason;
        const bool javaOk =
            GlobalJavaSettings::installerJavaFor(requiredMajor, kBogusJava, &picked, &reason);
        out() << "  · 安装器要 Java " << requiredMajor << "，配的那份认不出来 -> "
              << (javaOk ? "挑中 " + picked : "挑不出来：" + reason) << "\n";
        out().flush();
        if (javaOk) {
            const int pickedMajor = GlobalJavaSettings::probeJavaMajorVersion(picked);
            check(!picked.isEmpty() && picked != kBogusJava && pickedMajor >= requiredMajor,
                  "挑中的那份真的够新（挑不出来时必须给理由，不能拿着旧的硬上）",
                  picked + " = Java " + QString::number(pickedMajor));
            check(QFile::exists(picked), "挑中的那份在盘上", picked);
        } else {
            check(!reason.isEmpty() && reason.contains(QString::number(requiredMajor)),
                  "挑不出来时给了一句能照做的理由", reason);
        }

        // 反过来那条 —— 这次改动的**要害**就在这儿：配置里那份**已经够新**时必须原样用它。
        // "顺手换个更贴近的"是没道理的：用户配着 Java 21 去装一个要 17 的版本，换掉他选的
        // 那份只会让人莫名其妙（而且旧实现还会把它写回全局设置）。
        const QString onPath = QStandardPaths::findExecutable(QStringLiteral("java"));
        if (!onPath.isEmpty() &&
            GlobalJavaSettings::probeJavaMajorVersion(onPath) >= requiredMajor) {
            QString kept;
            QString why;
            const bool keptOk =
                GlobalJavaSettings::installerJavaFor(requiredMajor, onPath, &kept, &why);
            check(keptOk && kept == onPath, "配的那份够新就原样用它（不换）",
                  kept + " / 原配置 " + onPath);
        } else {
            out() << "  · PATH 上那份没有或不够新，本机造不出「够新就原样用」那条\n";
            out().flush();
        }
    }

    out() << "开始装：installLoaderVersion(" << gameVersion << ", " << loaderName << ")\n";
    out().flush();
    QElapsedTimer timer;
    timer.start();
    // javaPath 传空 = 用 PATH 上的 `java`。这个模式不读用户的设置（它跑在沙箱里，
    // 而沙箱里那个 QSettings 是空的），指望"设置里配过"会在这台机器上跑不起来。
    vm.installLoaderVersion(gameVersion, kind, QString());
    if (!finished) {
        // 超时兜底：卡住时也要退出来，不然是个永远不返回的验证。
        //
        // NOTE: 这个数**必须**比产品自己的硬超时（VersionManager 里那个 30 分钟）长。
        // 原来是 300 秒，结果是"产品还在正常跑，验证先放弃了"——实测 Forge 40.3.0
        // 在 1.18.2 上装一次，光从 creeperhost 那个镜像下十几个库就超过 300 秒，
        // 报出来的是"失败"，而产品那一路其实一切正常。留 35 分钟：比产品的 30 分钟
        // 多 5 分钟，这样"真卡住了"报出来的是产品那句中文（带它最后的输出），
        // 而不是这里一句光秃秃的"超时"。
        constexpr int kLiveWaitMs = 35 * 60 * 1000;
        QTimer::singleShot(kLiveWaitMs, &loop, [&] {
            out() << "  超时 " << kLiveWaitMs << " ms，结束等待（产品自己那条硬超时早该报了）\n";
            out().flush();
            loop.quit();
        });
        loop.exec();
    }
    out() << "装完用了 " << timer.elapsed() << " ms：" << (succeeded ? "成功" : "失败")
          << "（" << installedId << "）\n"
          << message << "\n";
    out().flush();
    check(succeeded, "installLoaderVersion 报成功", message);
    if (!succeeded) {
        return 1;
    }

    // --- 装出来的东西长什么样 ---------------------------------------------------
    //
    // 目录名的约定**四种各不相同**，而且都不是我们发明的（是安装器/加载器服务自己定的，
    // 别的启动器按它找目录）。所以这里按 kind 分别钉，不能"统一一下"。
    const QString loaderId = installedId;
    if (kind == VersionManager::LoaderKind::Fabric) {
        check(loaderId.startsWith(QStringLiteral("fabric-loader-")) &&
                  loaderId.contains(gameVersion),
              "版本目录名是 fabric-loader-<加载器>-<游戏>", loaderId);
    } else if (kind == VersionManager::LoaderKind::Forge) {
        check(loaderId.startsWith(gameVersion + QStringLiteral("-forge-")),
              "版本目录名是 <游戏>-forge-<加载器>", loaderId);
    } else if (kind == VersionManager::LoaderKind::NeoForge) {
        check(loaderId.startsWith(QStringLiteral("neoforge-")) &&
                  !loaderId.contains(gameVersion + QLatin1Char('-')),
              "版本目录名是 neoforge-<加载器>（**不带**游戏版本前缀）", loaderId);
    } else {
        // OptiFine 这个目录名**不是我们拼的**（上面三条都是）：它的安装器按自己那份 jar 里
        // 的版本号拼目录名，我们压根不知道会拼出什么，是装完去临时目录里认出来的（见
        // VersionManager::adoptOptifineProducts）。所以这里只能钉形状 —— 具体版本号是版本表
        // 里挑中的那个，下面拿 wtfml.meta 里的 loaderVersion 反过来对。
        const QString prefix = gameVersion + QStringLiteral("-OptiFine_");
        check(loaderId.startsWith(prefix) && loaderId.mid(prefix.size()).contains(QLatin1Char('_')),
              "版本目录名是 <游戏>-OptiFine_<版本>（安装器自己拼的，我们只是认出来）", loaderId);
    }
    const QString loaderDir = versionsRoot + "/" + loaderId;
    const QJsonObject onDisk = loadJson(loaderDir + "/" + loaderId + ".json");
    check(!onDisk.isEmpty() && onDisk[QStringLiteral("id")].toString() == loaderId &&
              onDisk[QStringLiteral("inheritsFrom")].toString() == gameVersion,
          "json 落盘了，而且是原样那份 profile（id / inheritsFrom 都对）",
          loaderDir + "/" + loaderId + ".json");

    // 库里那几个 maven 库都得真在盘上（这条是"下载列表和 classpath 用同一个解析"的落地）
    const QString librariesDir = AppPaths::minecraftDir() + "/libraries";
    int missingLibs = 0;
    QString firstMissing;
    const QJsonArray libs = onDisk[QStringLiteral("libraries")].toArray();
    for (const QJsonValue &value : libs) {
        const QJsonObject libObj = value.toObject();
        if (!LibraryRules::libraryAllowed(libObj)) {
            continue;
        }
        const LibraryRules::Artifact artifact = LibraryRules::artifactFor(libObj);
        // 判据是 hasPath() 不是 valid()：OptiFine 那两个库没有下载地址（jar 是它的安装器
        // 自己放进 libraries/ 的），但对它们来说"盘上得有一份"照样成立 —— 那正是这条要钉的。
        const QString path =
            artifact.hasPath() ? librariesDir + "/" + artifact.path : QString();
        if (path.isEmpty() || !QFile::exists(path)) {
            ++missingLibs;
            if (firstMissing.isEmpty()) {
                firstMissing = artifact.path;
            }
        }
    }
    check(libs.size() > 0 && missingLibs == 0, "profile 里那几个库全下到 libraries/ 下了",
          QString("库 %1 个，缺 %2 个（%3）").arg(libs.size()).arg(missingLibs).arg(firstMissing));

    const QJsonObject meta = loadJson(AppPaths::versionMetaPath(loaderId));
    // loaderName 跟外面的 loaderName 比，**不写死 "Fabric"**：这条原来是 Fabric 单干时写的，
    // 加了 forge/neoforge 之后就成了"产品对了、断言红"（ 真装时撞上）。
    check(meta[QStringLiteral("realVersion")].toString() == gameVersion &&
              meta[QStringLiteral("loaderName")].toString() == loaderName &&
              !meta[QStringLiteral("loaderVersion")].toString().isEmpty(),
          "wtfml.meta 里写进了 realVersion / loaderName / loaderVersion",
          QString("realVersion=%1 loaderName=%2 loaderVersion=%3")
                  .arg(meta[QStringLiteral("realVersion")].toString(),
                       meta[QStringLiteral("loaderName")].toString(),
                       meta[QStringLiteral("loaderVersion")].toString()));
    if (kind == VersionManager::LoaderKind::OptiFine) {
        // 这一条是 OptiFine 独有的：目录名是**安装器装出来**的，而写进 wtfml.meta 的
        // loaderVersion 是我们从那个目录名里切出来的 —— 两者必须一致，不然「已安装版本」
        // 那一栏显示的版本号会和盘上那个目录对不上（用户核不了、我们也核不了）。
        check(meta[QStringLiteral("loaderVersion")].toString() ==
                  loaderId.mid(gameVersion.size() + QStringLiteral("-OptiFine_").size()),
              "wtfml.meta 里的 loaderVersion 就是目录名里那一段（不是另算的一个）",
              meta[QStringLiteral("loaderVersion")].toString());
    }

    // --- 装出来的这个版本能不能启动 ---------------------------------------------
    const QJsonObject merged = vm.getVersionJson(loaderId);
    // 合并结果的主类必须是**加载器**那个，而不是原版的 —— 这一条就是"启动真的会走加载器"
    // 这件事的判据：主类错了游戏起来就是纯原版（模组全不加载）。
    //
    // 判据写法：主类**非空**、等于 json 里那个、而且**不等于原版 json 里的主类**。
    // 不写死具体类名（`KnotClient` / `cpw.mods.bootstraplauncher.BootstrapLauncher` 这种）
    // 是因为那是加载器自己的版本细节 —— 它哪天换个引导类，这里不该跟着红。
    const QJsonObject parentJson = loadJson(parentDir + "/" + gameVersion + ".json");
    const QString parentMain = parentJson[QStringLiteral("mainClass")].toString();
    const QString loaderMain = merged[QStringLiteral("mainClass")].toString();
    check(!loaderMain.isEmpty() && loaderMain == onDisk[QStringLiteral("mainClass")].toString() &&
              loaderMain != parentMain,
          "顺着继承链读出来的主类是加载器那个（不是原版那个）",
          QStringLiteral("%1（原版是 %2）").arg(loaderMain, parentMain));
    if (kind == VersionManager::LoaderKind::Fabric) {
        // Fabric 这一条留着写死，因为 V 节那份真 profile 里就是它，两边能对上。
        check(loaderMain.contains(QStringLiteral("Knot")),
              "Fabric 的是 KnotClient（V 节那份真 profile 里就是这个）", loaderMain);
    }
    // 两样分开判，因为"继承"对它们不是同一条规矩：
    // - downloads 是**父版本独有**的键（加载器的 profile 里根本没有），它非空就直接证明
    // 继承发生了。
    // - logging 要按"子赢"核对：子声明了就取子的 —— Forge 1.18.2 装出来的 profile 写着
    // `"logging": {}`，那是它有意清掉原版那份 log4j 配置（官方启动器也一样），子没声明
    // 才继承父的。**所以这里不能断言"logging 非空"**： 就是这条把真装的
    // 断言顶红的（当时怀疑是 merge 丢了键，其实数据里就是个空对象）。
    const QJsonValue expectedLogging = onDisk.contains(QStringLiteral("logging"))
                                           ? onDisk[QStringLiteral("logging")]
                                           : parentJson[QStringLiteral("logging")];
    check(!merged[QStringLiteral("downloads")].toObject().isEmpty() &&
              flatJson(merged[QStringLiteral("logging")]) == flatJson(expectedLogging),
          "原版那两样（downloads 从父版本继承 / logging 按子赢）合对了",
          QString("downloads=%1 logging=%2（子的是 %3）")
              .arg(merged[QStringLiteral("downloads")].toObject().isEmpty() ? "空" : "有",
                   flatJson(merged[QStringLiteral("logging")]),
                   flatJson(expectedLogging)));
    check(vm.inheritanceProblem(loaderId).isEmpty(), "这个版本没有继承链问题",
          vm.inheritanceProblem(loaderId));
    if (kind == VersionManager::LoaderKind::OptiFine) {
        // **这个加载器和别的都不一样**：它的安装器把原版那份客户端 jar **逐字节**抄进了自己
        // 的版本目录（实测 sha1 = 2e9a3e3107cca00d6bc9c97bf7d149cae163ef21，就是原版那份），
        // 所以这个目录里有自己的 jar —— jar 归属是它自己，不是原版。别的三个目录里只有 json。
        check(InstanceFiles::jarOwnerId(loaderId) == loaderId,
              "OptiFine 的版本目录里有自己那份客户端 jar（安装器抄进去的），归属是它自己",
              InstanceFiles::jarOwnerId(loaderId));
    } else {
        check(InstanceFiles::jarOwnerId(loaderId) == gameVersion,
              "客户端 jar 认到原版那个目录（加载器目录里只有 json，没有 jar）",
              InstanceFiles::jarOwnerId(loaderId));
    }

    // 安装器**不下资源文件**（assets），而且 Forge 连原版的 json 都不写（只在
    // versions/<原版>/ 留一份 sha1 与官方一致的客户端 jar）。所以这条路必须自己把原版
    // 补齐 —— 缺了那一半，用户拿到的是一个能装模组、一进游戏就崩的版本。
    check(QFile::exists(parentDir + "/" + gameVersion + ".json"),
          "装完之后原版的 json 也在沙箱里（加载器自己只写了它那一份）",
          parentDir + "/" + gameVersion + ".json");
    check(QFile::exists(AppPaths::assetsDir() + "/indexes/" +
                        parentJson[QStringLiteral("assetIndex")].toObject()
                            [QStringLiteral("id")]
                                .toString() +
                        QStringLiteral(".json")),
          "资源索引也补齐了（安装器一个资源文件都不下）",
          AppPaths::assetsDir());
    check(QFile::exists(InstanceFiles::jarPath(loaderId)), "那个 jar 真的在盘上",
          InstanceFiles::jarPath(loaderId));

    QStringList warnings;
    const QStringList installed = vm.installedVersions(&warnings);
    check(installed.contains(loaderId), "装完就出现在「已安装版本」列表里",
          QString("列表里 %1 个：%2").arg(installed.size()).arg(installed.join(QStringLiteral(", "))));

    LauncherManager launcher;
    QStringList errors;
    check(launcher.checkPreLaunch(loaderId, &errors), "启动前检查过了",
          errors.join(QStringLiteral("; ")));

    // classpath：客户端 jar 得是**原版那个**，加载器自己的库也得在上面
    EffectiveJavaSettings javaSettings;
    javaSettings.javaPath = QStringLiteral("C:/does-not-matter/java.exe");  // 只拼参数，不起进程
    javaSettings.xms = 1024;
    javaSettings.xmx = 2048;
    const QStringList args = launcher.generateJvmArguments(
        QString(), loaderId, QString(), merged, javaSettings, true, 17);
    const QString joined = args.join(QLatin1Char(' '));
    // 客户端 jar 在 classpath 上**必须是实际被启动的那一个**：别的三个加载器是原版目录里
    // 那份，OptiFine 是它自己目录里那份（安装器抄进去的）—— 拼错了的症状是启动时一个
    // NoClassDefFound，而不是装的时候报错。
    const QString expectJar =
        kind == VersionManager::LoaderKind::OptiFine ? loaderId : gameVersion;
    check(joined.contains(QStringLiteral("versions/%1/%1.jar").arg(expectJar)),
          "classpath 上的客户端 jar 是实际被启动的那一份",
          joined.left(400));
    int libsOnClasspath = 0;
    for (const QJsonValue &value : libs) {
        const LibraryRules::Artifact artifact = LibraryRules::artifactFor(value.toObject());
        // 同上：classpath 这一条也要算上 localOnly 的那几个（它们没地址、但有 jar，而且
        // 少了 launchwrapper 那个主类就起不来）。
        if (artifact.hasPath() &&
            joined.contains(QDir::toNativeSeparators(QStringLiteral("libraries/%1").arg(artifact.path)))) {
            ++libsOnClasspath;
        }
    }
    check(libsOnClasspath == libs.size(), "加载器自己的库也全在 classpath 上",
          QString("%1/%2").arg(libsOnClasspath).arg(libs.size()));

    out() << "\n===== " << (g_failures == 0 ? "全部通过" : "有失败项") << ": "
          << (g_checks - g_failures) << "/" << g_checks << " =====\n";
    out().flush();
    return g_failures == 0 ? 0 : 1;
}

