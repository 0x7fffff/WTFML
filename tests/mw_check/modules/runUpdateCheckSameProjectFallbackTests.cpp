#include "Support.hpp"
void runUpdateCheckSameProjectFallbackTests() {
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "T32b.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }

    FakeLookupApi api;
    check(api.start(), "T32b.0 假接口服务器起来了");

    QSettings sandbox;
    const QVariant foldersBefore = sandbox.value(QStringLiteral("Launcher/Folders"));
    const QVariant currentBefore = sandbox.value(QStringLiteral("Launcher/CurrentFolder"));
    const QVariant sourceBefore = sandbox.value(QStringLiteral("Download/Source"));
    const QVariant modrinthBefore = sandbox.value(QStringLiteral("Launcher/ModrinthApiBase"));
    const QVariant curseForgeBefore = sandbox.value(QStringLiteral("Launcher/CurseForgeApiBase"));
    const QString lastVersionBefore = LauncherSettings().lastLaunchedVersion();
    const auto restoreSettings = [&] {
        sandbox.setValue(QStringLiteral("Launcher/Folders"), foldersBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), currentBefore);
        sandbox.setValue(QStringLiteral("Download/Source"), sourceBefore);
        sandbox.setValue(QStringLiteral("Launcher/ModrinthApiBase"), modrinthBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurseForgeApiBase"), curseForgeBefore);
        sandbox.sync();
        LauncherSettings().setLastLaunchedVersion(lastVersionBefore);
        AppPaths::setMinecraftDir(originalDir);
    };
    sandbox.setValue(QStringLiteral("Launcher/Folders"), QStringList({root.path()}));
    sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), root.path());
    sandbox.setValue(QStringLiteral("Download/Source"),
                     LauncherSettings::downloadSourceKey(DownloadSource::Official));
    sandbox.setValue(QStringLiteral("Launcher/ModrinthApiBase"), api.modrinthBase());
    sandbox.setValue(QStringLiteral("Launcher/CurseForgeApiBase"), api.curseForgeBase());
    sandbox.sync();
    AppPaths::setMinecraftDir(root.path());
    QDir().mkpath(root.path() + QStringLiteral("/libraries"));
    QDir().mkpath(root.path() + QStringLiteral("/assets"));

    const QString idV = QStringLiteral("1.98.1");
    const QDir versionsDir(root.path() + QStringLiteral("/versions"));
    makeInstalledVersion(versionsDir, idV);
    makeVersionMeta(versionsDir, idV,
                    QByteArrayLiteral("{\"loaderName\":\"Fabric\",\"loaderVersion\":\"0.15.11\"}"));
    const QString dirV = AppPaths::versionDir(idV);

    const QString jarA = dirV + QStringLiteral("/mods/同项目A.jar");
    const QString jarB = dirV + QStringLiteral("/mods/同项目B.jar");
    writeFileAt(jarA, QByteArrayLiteral("T32B-SAME-PROJECT-A"));
    writeFileAt(jarB, QByteArrayLiteral("T32B-SAME-PROJECT-B"));

    ModpackExport::FileHashes hA;
    ModpackExport::FileHashes hB;
    QString hashError;
    const bool hashed = ModpackExport::hashFile(jarA, true, &hA, &hashError) &&
                        ModpackExport::hashFile(jarB, true, &hB, &hashError) &&
                        hA.fingerprintOk && hB.fingerprintOk;
    check(hashed, "T32b.0 两个文件的 sha1 + 指纹都算出来了", hashError);
    if (!hashed) {
        restoreSettings();
        return;
    }

    const QString project = QStringLiteral("SAMEPROJ");
    const QString modTitle = QStringLiteral("同项目模组");
    const int cfModId = 9101;

    // 反查：**两个都认得出**，而且指向同一个项目。
    api.modrinthReply = QByteArrayLiteral("{\"") + hA.sha1.toUtf8() +
                        QByteArrayLiteral("\":{\"project_id\":\"") + project.toUtf8() +
                        QByteArrayLiteral("\",\"version_number\":\"1.0\",\"files\":[{\"hashes\":{"
                                          "\"sha1\":\"") +
                        hA.sha1.toUtf8() +
                        QByteArrayLiteral("\",\"sha512\":\"a1\"},\"url\":\"https://cdn."
                                          "modrinth.com/data/a.jar\",\"filename\":\"同项目A.jar\","
                                          "\"size\":19}]},\"") +
                        hB.sha1.toUtf8() +
                        QByteArrayLiteral("\":{\"project_id\":\"") + project.toUtf8() +
                        QByteArrayLiteral("\",\"version_number\":\"1.0\",\"files\":[{\"hashes\":{"
                                          "\"sha1\":\"") +
                        hB.sha1.toUtf8() +
                        QByteArrayLiteral("\",\"sha512\":\"b1\"},\"url\":\"https://cdn."
                                          "modrinth.com/data/b.jar\",\"filename\":\"同项目B.jar\","
                                          "\"size\":19}]}}");
    api.modrinthProjectsReply =
        QByteArrayLiteral("[{\"id\":\"") + project.toUtf8() +
        QByteArrayLiteral("\",\"title\":\"") + modTitle.toUtf8() +
        QByteArrayLiteral("\",\"slug\":\"same\",\"description\":\"同项目两个文件\","
                          "\"game_versions\":[\"1.98.1\"],\"loaders\":[\"fabric\"]}]");
    // 批量那一发**只答 A 那个 sha1**：B 那个哈希在 Modrinth 这边"没有版本"——
    // 它必须退回问 CurseForge，否则这一条就从报告里消失了。
    api.modrinthVersionFilesUpdateReply =
        QByteArrayLiteral("{\"") + hA.sha1.toUtf8() +
        QByteArrayLiteral("\":{\"id\":\"VNA\",\"version_number\":\"1.0\","
                          "\"date_published\":\"2025-01-01T00:00:00Z\","
                          "\"game_versions\":[\"1.98.1\"],\"loaders\":[\"fabric\"],"
                          "\"files\":[{\"hashes\":{\"sha1\":\"") +
        hA.sha1.toUtf8() +
        QByteArrayLiteral("\",\"sha512\":\"a1\"},\"url\":\"https://cdn.modrinth.com/data/a.jar\","
                          "\"filename\":\"同项目A.jar\",\"size\":19,\"primary\":true}]}}");
    // CurseForge 指纹：**两条，同一个 modId**。
    api.curseForgeReply =
        QByteArrayLiteral("{\"data\":{\"exactMatches\":["
                          "{\"id\":11,\"file\":{\"id\":11,\"modId\":") +
        QByteArray::number(cfModId) +
        QByteArrayLiteral(",\"displayName\":\"same-a\",\"fileName\":\"同项目A.jar\","
                          "\"fileFingerprint\":") +
        QByteArray::number(hA.fingerprint) +
        QByteArrayLiteral(",\"fileLength\":19,\"isAvailable\":true,\"downloadUrl\":null,"
                          "\"hashes\":[]}},"
                          "{\"id\":12,\"file\":{\"id\":12,\"modId\":") +
        QByteArray::number(cfModId) +
        QByteArrayLiteral(",\"displayName\":\"same-b\",\"fileName\":\"同项目B.jar\","
                          "\"fileFingerprint\":") +
        QByteArray::number(hB.fingerprint) +
        QByteArrayLiteral(",\"fileLength\":19,\"isAvailable\":true,\"downloadUrl\":null,"
                          "\"hashes\":[]}}],\"unmatchedFingerprints\":[]}}");
    api.curseForgeModsReply =
        QByteArrayLiteral("{\"data\":[{\"id\":") + QByteArray::number(cfModId) +
        QByteArrayLiteral(",\"name\":\"") + modTitle.toUtf8() +
        QByteArrayLiteral("\",\"slug\":\"same\",\"summary\":\"同项目两个文件\","
                          "\"logo\":{\"thumbnailUrl\":\"https://media.forgecdn.net/t.png\","
                          "\"url\":\"https://media.forgecdn.net/u.png\"}}]}");
    // 版本列表：**一条，正好是 B 那一份** ⇒ 退回那一发问回来能判"已是最新"。
    api.curseForgeVersionListByMod.insert(
        QString::number(cfModId),
        QByteArrayLiteral("{\"data\":[{\"id\":91011,\"displayName\":\"1.0 for Fabric 1.98.1\","
                          "\"fileName\":\"同项目B.jar\",\"fileDate\":\"2025-06-01T00:00:00Z\","
                          "\"releaseType\":1,\"downloadCount\":5,"
                          "\"gameVersions\":[\"1.98.1\",\"Fabric\"],\"fileLength\":19,"
                          "\"hashes\":[{\"algo\":1,\"value\":\"") +
            hB.sha1.toUtf8() +
            QByteArrayLiteral("\"}],"
                              "\"downloadUrl\":\"https://edge.forgecdn.net/files/same-b.jar\"}]}"));

    TokenStore::clear(TokenStore::microsoftKey());
    TokenStore::clear(TokenStore::minecraftKey());
    TokenStore::clear(TokenStore::accountsKey());
    TokenStore::clear(TokenStore::currentUuidKey());
    const QString accountName = QStringLiteral("T32bUser");
    QString offlineError;
    check(AccountStore::addOffline(accountName, &offlineError),
          "T32b.0 建一个离线账号并设成当前身份（这一节一个外网请求都不发）", offlineError);

    {
        MainWindow w;
        w.show();
        QApplication::processEvents();
        OverlayHost *overlay = w.ui() != nullptr ? w.ui()->overlay() : nullptr;
        check(overlay != nullptr, "T32b.1 覆盖层拿得到");
        if (overlay == nullptr) {
            restoreSettings();
            return;
        }

        PageCloser closer;
        closer.setOnlyTitle(UpdateCenterPage::windowTitle());
        closer.start();
        QTimer::singleShot(0, [&] {
            OverlayHost *host = currentPageOverlay();
            auto *page = host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
            if (page == nullptr) {
                return;
            }
            page->showPage(QStringLiteral("mod"));
            auto *mods = page->filesPage(QStringLiteral("mod"));
            check(pumpUntil([mods] { return mods && mods->updateChecked(); }, 20000),
                  "background update check finishes without navigation");
            check(currentPageOverlay() == host && host->findChild<InstanceSettingsDialog *>(),
                  "background result preserves the active instance page");
            QMetaObject::invokeMethod(page, "updateCenterRequested", Qt::DirectConnection);

            if (auto *back =
                    host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
                back->click();
            } else {
                host->closeOverlay();
            }
        });
        QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                  Q_ARG(QString, idV));
        QApplication::processEvents();
        pumpUntil([&closer] { return !closer.seen.isEmpty(); }, 20000);
        closer.stop();
        pumpUntil([] { return currentOverlay() == nullptr; }, 8000);

        const auto countMatching = [&api](const QString &needle) {
            int n = 0;
            for (const QString &line : api.requests) {
                if (line.contains(needle)) {
                    ++n;
                }
            }
            return n;
        };

        check(closer.seen.size() == 1, "T32b.1 那一趟真的跑完了、而且只开了一页",
              QStringLiteral("开了 %1 页").arg(closer.seen.size()));
        const PageCloser::Seen seen =
            closer.seen.isEmpty() ? PageCloser::Seen() : closer.seen.first();
        check(seen.updateCenterSummary.contains(ModDependencies::updateSummaryText(2, 0)),
              "★★ T32b.2 顶上那句说的是「检查了 **2** 个资源，0 个有新版本」—— 两个文件都在"
              "盘上，一个都不能少（整组跳过的写法在这里会写 1）",
              seen.updateCenterSummary);
        check(seen.updateCenterRowNames.size() == 2,
              "★★ T32b.2 名单正好两行（每一条文件各一行）",
              QStringLiteral("%1 行").arg(seen.updateCenterRowNames.size()));
        const QString unknownText = MainWindowUI::updateCenterRowStateText(2, 0, QString());
        check(!seen.updateCenterRowStates.contains(unknownText),
              "★★ T32b.2 一条「说不清」都没有 —— B 那个文件真的退回问过 CurseForge 了",
              seen.updateCenterRowStates.join(QStringLiteral(" / ")));
        check(countMatching(QStringLiteral("/mods/9101/files")) == 1,
              "★★ T32b.3 退回问 CurseForge 的版本列表**发出去了、而且只有一发**"
              "（同一把钥匙只问一次 —— 两条文件共用这份答案）",
              QStringLiteral("%1 发").arg(countMatching(QStringLiteral("/mods/9101/files"))));
        check(countMatching(QStringLiteral("/version_files/update")) == 1,
              "T32b.3 批量那一发还是只有一发",
              QStringLiteral("%1 发").arg(countMatching(QStringLiteral("/version_files/update"))));
    }

    restoreSettings();
    check(AppPaths::minecraftDir() == originalDir, "T32b.9 数据根目录换回来了");
}

