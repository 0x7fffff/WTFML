#include "Support.hpp"
#include "launcher/LibraryRules.hpp"
#include <QProcess>

void runMultiMCPatchImportTests(const QString &sandbox) {
    section("MultiMC declarative patches / bundled libraries");
    const QByteArray manifest = R"({"formatVersion":1,"components":[{"uid":"net.minecraft","version":"1.7.10"},{"uid":"special.runtime","version":"1"}]})";
    const QByteArray minecraft = R"({"formatVersion":1,"uid":"net.minecraft","version":"1.7.10","order":-2,"mainClass":"net.minecraft.client.main.Main","mainJar":{"downloads":{"artifact":{"url":"https://launcher.mojang.com/client.jar","sha1":"1111111111111111111111111111111111111111","size":4}}},"assetIndex":{"id":"1.7.10","url":"https://piston-meta.mojang.com/index.json","sha1":"2222222222222222222222222222222222222222"},"compatibleJavaMajors":[17,21]})";
    const QByteArray special = R"({"formatVersion":1,"uid":"special.runtime","version":"1","order":3,"mainClass":"com.gtnewhorizons.retrofuturabootstrap.Main","requires":[{"uid":"net.minecraft","equals":"1.7.10"}],"+jvmArgs":["-Dfile.encoding=UTF-8","--add-opens","java.base/java.io=ALL-UNNAMED"],"+tweakers":["cpw.mods.fml.common.launcher.FMLTweaker"],"libraries":[{"name":"test:runtime:1","MMC-hint":"local"},{"name":"org.lwjgl:lwjgl-natives-windows-x86:3.3.3","downloads":{"artifact":{"url":"https://libraries.minecraft.net/x86.jar","sha1":"3333333333333333333333333333333333333333"}}},{"name":"org.lwjgl:lwjgl-natives-windows:3.3.3","downloads":{"artifact":{"url":"https://libraries.minecraft.net/x64.jar","sha1":"4444444444444444444444444444444444444444"}}}]})";
    const QHash<QString, QByteArray> files{{"Pack/libraries/runtime-1.jar", QByteArray("runtime-fixture")}};
    const QHash<QString, QByteArray> patches{{"net.minecraft", minecraft}, {"special.runtime", special}};
    const auto convert = [&](const QHash<QString, QByteArray> &parts, const QHash<QString, QByteArray> &libs = QHash<QString, QByteArray>{}) {
        return ModpackImport::parsePatchedMultiMCInstance(manifest, "name=Patched Pack\n", parts, libs, "Pack/.minecraft/", "fallback");
    };
    const auto result = convert(patches, files);
    check(result.ok, "Patch conversion succeeds without executing code", result.problem);
    check(result.pack.patchedRuntime.value("mainClass").toString() == "com.gtnewhorizons.retrofuturabootstrap.Main" &&
          result.pack.patchedRuntime.value("javaVersion").toObject().value("majorVersion").toInt() == 17,
          "Patch order preserves RFB entry point and Java 17 requirement");
    check(result.pack.patchedRuntime.value("minecraftArguments").toString().contains("--tweakClass cpw.mods.fml.common.launcher.FMLTweaker") &&
          result.pack.patchedRuntime.value("arguments").toObject().value("jvm").toArray().size() == 3,
          "Tweakers and modular JVM arguments remain in generated runtime");
    const auto libraries = result.pack.patchedRuntime.value("libraries").toArray();
    check(libraries.size() == 3 && libraries.at(1).toObject().value("rules").toArray().first().toObject().value("os").toObject().value("arch").toString() == "x86" &&
          libraries.at(2).toObject().value("rules").toArray().first().toObject().value("os").toObject().value("arch").toString() == "x86_64",
          "Prism native suffixes translate to explicit x86 / x64 rules");
    check(result.pack.bundledLibraries.size() == 1 &&
          LibraryRules::artifactFor(libraries.first().toObject()).localOnly,
          "Archive-local runtime is on classpath but not downloaded from an invented URL");
    const QJsonObject nativeOnly{{"name", "net.java.jinput:jinput-platform:2.0.5"},
        {"downloads", QJsonObject{{"classifiers", QJsonObject{{"natives-windows", QJsonObject{{"url", "https://libraries.minecraft.net/native.jar"}}}}}}}};
    check(!LibraryRules::artifactFor(nativeOnly).hasPath() &&
          LibraryRules::artifactFor(QJsonObject{{"name", "test:runtime:1"}}).localOnly,
          "Classifier-only natives do not invent a primary JAR; name-only local libraries remain supported");
    QString error;
    const QString root = sandbox + "/patched-runtime-root";
    QDir().mkpath(root);
    check(ModpackImport::publishBundledLibraries(result.pack, root, &error), "Bundled library publishes in isolated data root", error);
    check(ModpackImport::publishBundledLibraries(result.pack, root, &error), "Identical bundled library can be reused", error);
    writeFileAt(root + "/libraries/test/runtime/1/runtime-1.jar", "user-different-content");
    check(!ModpackImport::publishBundledLibraries(result.pack, root, &error), "Different existing library is never overwritten", error);
    QFile preserved(root + "/libraries/test/runtime/1/runtime-1.jar");
    check(preserved.open(QIODevice::ReadOnly) && preserved.readAll() == QByteArray("user-different-content"), "Conflicting library bytes preserved");
    for (const QByteArray &modified : {QByteArray(special).replace(QByteArray("--add-opens"), QByteArray("-javaagent:evil.jar")),
             QByteArray(special).replace(QByteArray("\"order\":3"), QByteArray("\"order\":3,\"preLaunchCommand\":\"evil\"")),
             QByteArray(special).replace(QByteArray("\"equals\":\"1.7.10\""), QByteArray("\"equals\":\"1.12.2\""))}) {
        auto changed = patches; changed["special.runtime"] = modified;
        check(!convert(changed, files).ok, "Unsafe arguments, unknown patch operators and incompatible requirements rejected");
    }
    // 认得出的那几个"不支持字段"要**点名说清楚**（不是一句"尚不支持补丁字段"就完了）：
    // 它们各自要求启动器多一种能力（合并 jar / 额外 natives / 行为提示），只解析不实现
    // 会让包装上、启动时才炸。
    for (const QByteArray &field : {QByteArray("+jarMods"), QByteArray("+traits"),
                                    QByteArray("+files"), QByteArray("addnatives"),
                                    QByteArray("+luac")}) {
        auto withField = patches;
        withField["special.runtime"] = QByteArray(special).replace(
            QByteArray("\"order\":3"),
            QByteArray("\"order\":3,\"") + field + QByteArray("\":[]"));
        const auto rejected = convert(withField, files);
        check(!rejected.ok && rejected.problem.contains(QString::fromLatin1(field))
                  && rejected.problem.contains(QStringLiteral("未忽略")),
              "已知但不支持的补丁字段：拒绝时点名那一格（而不是静默忽略）", rejected.problem);
    }
    check(!convert(patches).ok, "Missing archive-local JAR is not silently skipped");
    auto changed = patches; changed.remove("special.runtime");
    check(!convert(changed, files).ok, "Missing component patch blocks installation");
    auto bad = result.pack; bad.bundledLibraries[0].path = "../../outside.jar";
    check(!ModpackImport::publishBundledLibraries(bad, root, &error), "Local library cannot escape data directory");

    // ★ 路径上**中间那一层**是链接：`<数据根>/libraries` 整根是一根 junction，指着外面。
    // `isSafeRelativePath()` 与 `pathIsSameOrInside()` 判的都是字面那条路径（`libraries/...`
    // 好得很），能让它停下的只有 `pathHasLinkComponent()`。用户把 `libraries/` 指到另一个盘
    // 是常见摆法，但那不是"这一份整合包的库"，不能顺着它写进去。
    {
        const QString linkRoot = sandbox + QStringLiteral("/patched-runtime-link");
        const QString outside = sandbox + QStringLiteral("/patched-runtime-outside");
        QDir().mkpath(linkRoot);
        QDir().mkpath(outside);
        const QString linkLibraries = linkRoot + QStringLiteral("/libraries");
        const bool madeLink =
            QProcess::execute(QStringLiteral("cmd"),
                              {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                               QDir::toNativeSeparators(linkLibraries),
                               QDir::toNativeSeparators(outside)}) == 0
            && QFileInfo(linkLibraries).isJunction();
        check(madeLink, "Bundled library: a real junction is in place (`<root>/libraries` → outside)");
        if (madeLink) {
            QString linkError;
            check(!ModpackImport::publishBundledLibraries(result.pack, linkRoot, &linkError) &&
                      !linkError.isEmpty(),
                  "Bundled library never writes through a junction on the way", linkError);
            check(!QFileInfo::exists(outside + QStringLiteral("/test/runtime/1/runtime-1.jar")),
                  "The junction target received nothing");
            // 链接本体只能 rmdir（删除链接，不跟着删那一头）。
            QProcess::execute(QStringLiteral("cmd"),
                              {QStringLiteral("/c"), QStringLiteral("rmdir"),
                               QDir::toNativeSeparators(linkLibraries)});
        }
    }

    // Exercise the real import state machine with file-backed original metadata,
    // including rejection of a patch that swaps the official client hash.
    for (const bool wrongClient : {false, true}) {
        const QString previousRoot = AppPaths::minecraftDir();
        const QString fixture = sandbox + (wrongClient ? "/patch-engine-bad" : "/patch-engine-good");
        QDir().mkpath(fixture);
        const QByteArray clientBytes("official-client-fixture");
        const QString clientHash = QString::fromLatin1(QCryptographicHash::hash(clientBytes, QCryptographicHash::Sha1).toHex());
        const QString clientPath = fixture + "/client.bin", basePath = fixture + "/base.json", indexPath = fixture + "/index.json";
        writeFileAt(clientPath, clientBytes);
        const QJsonObject client{{"url", QUrl::fromLocalFile(clientPath).toString()}, {"sha1", clientHash}, {"size", clientBytes.size()}};
        const QJsonObject base{{"id", "1.99.77"}, {"type", "release"}, {"mainClass", "net.minecraft.client.main.Main"},
            {"downloads", QJsonObject{{"client", client}}}, {"libraries", QJsonArray{}}};
        writeFileAt(basePath, QJsonDocument(base).toJson());
        writeFileAt(indexPath, QJsonDocument(QJsonObject{{"versions", QJsonArray{QJsonObject{{"id", "1.99.77"}, {"type", "release"}, {"url", QUrl::fromLocalFile(basePath).toString()}}}}}).toJson());
        writeFileAt(fixture + "/source.txt", "config-fixture");
        const QString archive = fixture + "/pack.zip";
        check(ZipUtil::createZip(archive, {{".minecraft/config/test.txt", fixture + "/source.txt"}}, &error), "Patched engine test archive prepared", error);
        AppPaths::setMinecraftDir(fixture);
        {
            VersionManager manager;
            manager.setUseMirror(false);
            manager.downloads()->setVersionManifestUrlOverride(QUrl::fromLocalFile(indexPath).toString());
            auto pack = result.pack;
            pack.gameVersion = "1.99.77"; pack.overridesDir = ".minecraft";
            pack.bundledLibraries.clear();
            pack.patchedRuntime["libraries"] = QJsonArray{};
            pack.patchedRuntime.remove("assetIndex");
            QJsonObject requested = client;
            if (wrongClient) requested["sha1"] = QString(40, QLatin1Char('f'));
            pack.patchedRuntime["downloads"] = QJsonObject{{"client", requested}};
            QEventLoop loop;
            bool done = false, succeeded = false; QString message;
            QObject::connect(&manager, &VersionManager::versionDownloadFinished, &loop,
                [&](const QString &id, bool ok, const QString &text) {
                    if (id != "patched-engine") return;
                    done = true; succeeded = ok; message = text; loop.quit();
                });
            QTimer::singleShot(15000, &loop, &QEventLoop::quit);
            QTimer::singleShot(0, &manager, [&] { manager.importModpack(archive, "patched-engine", pack, {}); });
            loop.exec();
            check(done && succeeded == !wrongClient, "Patched import state machine succeeds only for the matching original client", message);
            if (!wrongClient) {
                check(QFile::exists(fixture + "/versions/patched-engine/patched-engine.jar") &&
                      QFile::exists(fixture + "/versions/patched-engine/config/test.txt") &&
                      !QFile::exists(fixture + "/versions/patched-engine/wtfml.importing"),
                      "Patched import completes client download, content extraction and marker cleanup");
            }
        }
        AppPaths::setMinecraftDir(previousRoot);
    }

    // Opt-in real GTNH package: parse metadata, publish only bundled libraries into sandbox.
    const QString archive = qEnvironmentVariable("WTFML_TEST_GTNH_ARCHIVE");
    if (archive.isEmpty()) return;
    QStringList entries;
    if (ZipUtil::listEntries(archive, &entries, &error) != ZipUtil::EntryStatus::Ok) {
        check(false, "Real GTNH entry list readable", error); return;
    }
    const auto layout = ModpackImport::detectArchiveLayout(entries);
    QByteArray mmc, cfg;
    ZipUtil::readEntry(archive, layout.mmcPackEntry, &mmc, &error);
    ZipUtil::readEntry(archive, layout.instanceCfgEntry, &cfg, &error);
    QHash<QString, QByteArray> realPatches, realLibraries;
    for (const QString &entry : entries) {
        const QString prefix = layout.rootPrefix + "patches/";
        const bool patch = entry.startsWith(prefix) && entry.endsWith(".json");
        const bool lib = entry.startsWith(layout.rootPrefix + "libraries/") && entry.endsWith(".jar");
        if (!patch && !lib) continue;
        QByteArray bytes;
        const auto status = ZipUtil::readEntry(archive, entry, &bytes, &error, "Read GTNH dependency", 64 * 1024 * 1024);
        check(status == ZipUtil::EntryStatus::Ok, "GTNH patch/local library readable", error);
        if (patch) realPatches.insert(entry.mid(prefix.size()).chopped(5), bytes);
        else realLibraries.insert(entry, bytes);
    }
    const auto gtnh = ModpackImport::parsePatchedMultiMCInstance(mmc, cfg, realPatches, realLibraries, layout.gameDirPrefix, "GTNH");
    check(gtnh.ok, "Real GTNH patches convert", gtnh.problem);
    check(gtnh.pack.gameVersion == "1.7.10" && gtnh.pack.loaderVersion == "10.13.4.1614" &&
          gtnh.pack.patchedRuntime.value("mainClass").toString() == "com.gtnewhorizons.retrofuturabootstrap.Main",
          "Real GTNH retains game / Forge / RFB entry point");
    const auto realLibs = gtnh.pack.patchedRuntime.value("libraries").toArray();
    int nativeWindows = 0, oldLwjgl = 0;
    for (const auto &value : realLibs) {
        const auto lib = value.toObject(); const QString name = lib.value("name").toString();
        if (name.startsWith("org.lwjgl.lwjgl:")) ++oldLwjgl;
        if (name.startsWith("org.lwjgl:") && name.contains(":natives-") && LibraryRules::libraryAllowed(lib)) ++nativeWindows;
    }
    check(oldLwjgl == 0 && nativeWindows == 8, "Real GTNH selects exactly eight native modules for host, without LWJGL2",
          QStringLiteral("native %1 / old %2").arg(nativeWindows).arg(oldLwjgl));
    check(gtnh.ok && ModpackImport::publishBundledLibraries(gtnh.pack, sandbox + "/real-gtnh", &error),
          "Real GTNH forgePatches library publishes only into sandbox", error);
    QFile runtimeFile(sandbox + "/gtnh-converted-runtime.json");
    if (runtimeFile.open(QIODevice::WriteOnly)) runtimeFile.write(QJsonDocument(gtnh.pack.patchedRuntime).toJson());
}
