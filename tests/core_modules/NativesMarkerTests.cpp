#include "Support.hpp"

void runNativesMarkerTests() {
    section("ZNat. natives 解压的完成标记（半套目录不许复用）");

    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir temp;
    check(temp.isValid(), "ZNat0 临时数据根目录建出来了");
    if (!temp.isValid()) {
        return;
    }
    AppPaths::setMinecraftDir(temp.path());

    const QString versionId = QStringLiteral("1.99.9-natives");
    const QString libRelative = QStringLiteral("synth/natives/1.0/natives-1.0.zip");
    const QString libFile = QDir(AppPaths::libraryDir()).absoluteFilePath(libRelative);
    const QString nativesDir = AppPaths::nativesDir(versionId);
    // 产品侧那个标记的名字（`LauncherManager.cpp` 里的 `kNativeReadyMarker`，文件内
    // static —— 这一条故意写成字面量：它一变，这里就该红）。
    const QString marker = nativesDir + QStringLiteral("/.wtfml-natives-ok");
    QDir().mkpath(QFileInfo(libFile).absolutePath());

    // 形状照真 version.json 里旧格式的 natives 库抄：natives 字段 + downloads.classifiers。
    // `url` 非空是 `NativeClassifier::valid()` 的一部分，这里不会真去下它（文件已在盘上）。
    QJsonObject versionJson;
    versionJson[QStringLiteral("libraries")] = QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("synth:natives:1.0")},
        {QStringLiteral("natives"),
         QJsonObject{{QStringLiteral("windows"), QStringLiteral("natives-windows")}}},
        {QStringLiteral("downloads"),
         QJsonObject{{QStringLiteral("classifiers"),
                      QJsonObject{{QStringLiteral("natives-windows"),
                                   QJsonObject{
                                       {QStringLiteral("path"), libRelative},
                                       {QStringLiteral("url"),
                                        QStringLiteral("https://example.invalid/n.jar")}}}}}}}}};

    // 造一份"里面只有一个 DLL"的压缩包（和 V5 一样借 Compress-Archive）。
    const auto buildLib = [&](const QString &dllName) {
        const QString src = temp.path() + QStringLiteral("/src-natives");
        QDir(src).removeRecursively();
        writeFileAt(src + QLatin1Char('/') + dllName, QByteArray("dll"));
        QFile::remove(libFile);
        const QString script =
            QStringLiteral("Compress-Archive -Path %1 -DestinationPath %2 -Force")
                .arg(QStringLiteral("'") + QDir::toNativeSeparators(src) + QStringLiteral("\\*'"),
                     QStringLiteral("'") + QDir::toNativeSeparators(libFile) + QStringLiteral("'"));
        QProcess ps;
        ps.start(QStringLiteral("powershell"),
                 {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                  QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                  QStringLiteral("-Command"), script});
        const bool ok = ps.waitForFinished(180000) &&
                        ps.exitStatus() == QProcess::NormalExit && ps.exitCode() == 0;
        check(ok, QStringLiteral("ZNat 造出 natives 压缩包（%1）").arg(dllName),
              QString::fromLocal8Bit(ps.readAllStandardError()).trimmed().left(200));
        return ok;
    };

    LauncherManager launcher;
    QStringList errors;

    // ---------- ZNat1. 第一次：解出来、DLL 在、标记写上了 ----------
    if (!buildLib(QStringLiteral("lwjgl64.dll"))) {
        AppPaths::setMinecraftDir(originalDir);
        return;
    }
    const QString ready = launcher.prepareNatives(versionJson, versionId, &errors);
    check(ready == nativesDir && errors.isEmpty(), "ZNat1 第一次解压成功，回的是 natives 目录",
          errors.join(QStringLiteral(" / ")));
    check(QFileInfo(nativesDir + QStringLiteral("/lwjgl64.dll")).isFile(),
          "ZNat1 DLL 解出来了");
    check(QFileInfo(marker).isFile(), "ZNat1 ★ 全部解完之后写了完成标记");
    check(launcher.prepareNatives(versionJson, versionId, &errors) == nativesDir,
          "ZNat1 第二趟走的是复用那条路（标记 + DLL 都在）");

    // ---------- ZNat2. ★ 半套目录（DLL 在、标记不在）必须重解，不许复用 ----------
    // 这就是"第一组成功、第二组失败"留下的形状：目录里有 DLL，但没有完成标记。
    // 旧判据（只看有没有 .dll）会在这里直接返回 —— 换一份**多了个 DLL** 的压缩包，
    // 复用的话那个新 DLL 永远不会出现。
    QFile::remove(marker);
    if (buildLib(QStringLiteral("lwjgl2.dll"))) {
        const QString repaired = launcher.prepareNatives(versionJson, versionId, &errors);
        check(repaired == nativesDir, "ZNat2 半套目录照样能再解一遍",
              errors.join(QStringLiteral(" / ")));
        check(QFileInfo(nativesDir + QStringLiteral("/lwjgl2.dll")).isFile(),
              "★★ ZNat2 没有标记 ⇒ **重新解压**了（旧判据看到那个 DLL 就直接复用，"
              "这一份永远不会出现 —— 崩在 UnsatisfiedLinkError 的那条路）");
        check(QFileInfo(marker).isFile(), "ZNat2 重解成功之后标记补回来了");
    }

    // ---------- ZNat3. 解压失败：整套删掉，不留半截 ----------
    QFile::remove(marker);
    QDir().mkpath(QFileInfo(libFile).absolutePath());
    {
        QFile garbage(libFile);
        if (garbage.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            garbage.write("这不是压缩包");
        }
    }
    QStringList failureErrors;
    const QString failed = launcher.prepareNatives(versionJson, versionId, &failureErrors);
    check(failed.isEmpty() && !failureErrors.isEmpty(),
          "ZNat3 坏掉的压缩包 ⇒ 返回空 + 一句错误（调用方据此弹窗、不启动）",
          failureErrors.join(QStringLiteral(" / ")));
    check(!QDir(nativesDir).exists(),
          "★★ ZNat3 解压失败之后那半个目录被**整个删掉**了（留着它，下一趟会被当成"
          "「解过了」复用）");
    check(!QFileInfo(marker).isFile(), "ZNat3 当然也没有留下完成标记");

    AppPaths::setMinecraftDir(originalDir);  // 收摊
}

