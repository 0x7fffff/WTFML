#include "Support.hpp"
#include "controllers/InstanceUpdateCheckController.hpp"
void runUpdateCheckSourceFallbackTests() {
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "T32.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }

    FakeLookupApi api;
    check(api.start(), "T32.0 假接口服务器起来了");

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

    // 报得出游戏版本号和加载器的实例（和 T28 同一个手法、同一个理由）。
    const QString idV = QStringLiteral("1.96.3");
    const QDir versionsDir(root.path() + QStringLiteral("/versions"));
    makeInstalledVersion(versionsDir, idV);
    makeVersionMeta(versionsDir, idV,
                    QByteArrayLiteral("{\"loaderName\":\"Fabric\",\"loaderVersion\":\"0.15.11\"}"));
    const QString dirV = AppPaths::versionDir(idV);

    const QString dualJar = dirV + QStringLiteral("/mods/双认模组.jar");
    const QString cfOnlyJar = dirV + QStringLiteral("/mods/只有CF认得的模组.jar");
    writeFileAt(dualJar, QByteArrayLiteral("T32-DUAL-SOURCE-MOD"));
    writeFileAt(cfOnlyJar, QByteArrayLiteral("T32-CURSEFORGE-ONLY-MOD"));

    ModpackExport::FileHashes hDual;
    ModpackExport::FileHashes hOnly;
    QString hashError;
    const bool hashed = ModpackExport::hashFile(dualJar, true, &hDual, &hashError) &&
                        ModpackExport::hashFile(cfOnlyJar, true, &hOnly, &hashError) &&
                        hDual.fingerprintOk && hOnly.fingerprintOk;
    check(hashed, "T32.0 两个文件的 sha1 + 指纹都算出来了（走产品自己那份实现）", hashError);
    if (!hashed) {
        restoreSettings();
        return;
    }

    const QString dualProject = QStringLiteral("DUALMOD");
    const QString dualTitle = QStringLiteral("双认模组");
    const QString cfOnlyTitle = QStringLiteral("只有CF认得的模组");
    const int dualModId = 9001;
    const int cfOnlyModId = 9002;

    // ① Modrinth 反查：**只映射两家都认得出的那一个**（第二个压根不在这一家的表里 ——
    // 那正是"只有 CurseForge 认得出"的样子）。
    api.modrinthReply = QByteArrayLiteral("{\"") + hDual.sha1.toUtf8() +
                        QByteArrayLiteral("\":{\"project_id\":\"") + dualProject.toUtf8() +
                        QByteArrayLiteral("\",\"version_number\":\"1.0\",\"files\":[{\"hashes\":{"
                                          "\"sha1\":\"") +
                        hDual.sha1.toUtf8() +
                        QByteArrayLiteral("\",\"sha512\":\"cafe\"},\"url\":\"https://cdn."
                                          "modrinth.com/data/dual.jar\",\"filename\":"
                                          "\"双认模组.jar\",\"size\":10}]}}");
    api.modrinthProjectsReply =
        QByteArrayLiteral("[{\"id\":\"") + dualProject.toUtf8() +
        QByteArrayLiteral("\",\"title\":\"") + dualTitle.toUtf8() +
        QByteArrayLiteral("\",\"slug\":\"dual\",\"description\":\"两家都有的模组\","
                          "\"game_versions\":[\"1.96.3\"],\"loaders\":[\"fabric\"]}]");
    // 批量那一发**什么都不答**（一个空对象）—— 这就是"这个哈希在 Modrinth 的表里没有"
    // （或者它在这个游戏版本+加载器下没有版本）。这一条正是 T32 的正题：这一拨必须
    // **退回问 CurseForge**，而不是把这一条判成"说不清"了事。
    api.modrinthVersionFilesUpdateReply = QByteArrayLiteral("{}");
    // 把批量那一发压后 400 毫秒 ⇒ 只有 CurseForge 认得的那一个**先答**，于是这一趟
    // 走到过"单发那一拨先回来、而批量还押着"那一态（见 `modrinthVersionFilesUpdateDelayMs`
    // 那段）。收尾判据漏了 `pendingBatchKeys` 的话，报告里就只剩那一条。
    api.modrinthVersionFilesUpdateDelayMs = 400;
    // ② CurseForge 指纹：**两个文件都在**（第一个是那个"两家都认得出"的）。
    api.curseForgeReply =
        QByteArrayLiteral("{\"data\":{\"exactMatches\":["
                          "{\"id\":11,\"file\":{\"id\":11,\"modId\":") +
        QByteArray::number(dualModId) +
        QByteArrayLiteral(",\"displayName\":\"dual\",\"fileName\":\"双认模组.jar\","
                          "\"fileFingerprint\":") +
        QByteArray::number(hDual.fingerprint) +
        QByteArrayLiteral(",\"fileLength\":10,\"isAvailable\":true,\"downloadUrl\":null,"
                          "\"hashes\":[]}},"
                          "{\"id\":22,\"file\":{\"id\":22,\"modId\":") +
        QByteArray::number(cfOnlyModId) +
        QByteArrayLiteral(",\"displayName\":\"cfonly\",\"fileName\":\"只有CF认得的模组.jar\","
                          "\"fileFingerprint\":") +
        QByteArray::number(hOnly.fingerprint) +
        QByteArrayLiteral(",\"fileLength\":10,\"isAvailable\":true,\"downloadUrl\":null,"
                          "\"hashes\":[]}}],"
                          "\"unmatchedFingerprints\":[]}}");
    api.curseForgeModsReply =
        QByteArrayLiteral("{\"data\":[{\"id\":") + QByteArray::number(dualModId) +
        QByteArrayLiteral(",\"name\":\"") + dualTitle.toUtf8() +
        QByteArrayLiteral("\",\"slug\":\"dual\",\"summary\":\"两家都有的模组\","
                          "\"logo\":{\"thumbnailUrl\":\"https://media.forgecdn.net/t.png\","
                          "\"url\":\"https://media.forgecdn.net/u.png\"}},"
                          "{\"id\":") +
        QByteArray::number(cfOnlyModId) + QByteArrayLiteral(",\"name\":\"") +
        cfOnlyTitle.toUtf8() +
        QByteArrayLiteral("\",\"slug\":\"cfonly\",\"summary\":\"只有这一家认得\","
                          "\"logo\":{\"thumbnailUrl\":\"https://media.forgecdn.net/t2.png\","
                          "\"url\":\"https://media.forgecdn.net/u2.png\"}}]}");
    // ③ 两家各自的版本列表（退回那一发问的就是它）。
    // · 9001（那个双认的）：**两条** —— 最新那条的文件不是盘上这一份，第二条才正好是
    // ⇒ 判得出"有新版本"（只有一条的话判出来是"说不清"，而屏幕上和坏了一个样）。
    // · 9002（只有 CF 认得的）：**一条，就是盘上这一份** ⇒ "已是最新"。
    api.curseForgeVersionListByMod.insert(
        QString::number(dualModId),
        QByteArrayLiteral("{\"data\":[{\"id\":90011,\"displayName\":\"2.0 for Fabric 1.96.3\","
                          "\"fileName\":\"双认模组-2.0.jar\",\"fileDate\":\"2026-09-01T00:00:00Z\","
                          "\"releaseType\":1,\"downloadCount\":100,"
                          "\"gameVersions\":[\"1.96.3\",\"Fabric\"],\"fileLength\":10,"
                          "\"hashes\":[{\"algo\":1,\"value\":"
                          "\"2222222222222222222222222222222222222222\"}],"
                          "\"downloadUrl\":\"https://edge.forgecdn.net/files/dual-2.0.jar\"},"
                          "{\"id\":90010,\"displayName\":\"1.0 for Fabric 1.96.3\","
                          "\"fileName\":\"双认模组.jar\",\"fileDate\":\"2025-01-01T00:00:00Z\","
                          "\"releaseType\":1,\"downloadCount\":10,"
                          "\"gameVersions\":[\"1.96.3\",\"Fabric\"],\"fileLength\":10,"
                          "\"hashes\":[{\"algo\":1,\"value\":\"") +
            hDual.sha1.toUtf8() +
            QByteArrayLiteral("\"}],"
                              "\"downloadUrl\":\"https://edge.forgecdn.net/files/dual-1.0.jar\"}]}"));
    api.curseForgeVersionListByMod.insert(
        QString::number(cfOnlyModId),
        QByteArrayLiteral("{\"data\":[{\"id\":90021,\"displayName\":\"1.0 for Fabric 1.96.3\","
                          "\"fileName\":\"只有CF认得的模组.jar\","
                          "\"fileDate\":\"2025-06-01T00:00:00Z\",\"releaseType\":1,"
                          "\"downloadCount\":5,\"gameVersions\":[\"1.96.3\",\"Fabric\"],"
                          "\"fileLength\":10,\"hashes\":[{\"algo\":1,\"value\":\"") +
            hOnly.sha1.toUtf8() + QByteArrayLiteral("\"}],"
                                                     "\"downloadUrl\":\"https://edge.forgecdn."
                                                     "net/files/cfonly-1.0.jar\"}]}"));

    TokenStore::clear(TokenStore::microsoftKey());
    TokenStore::clear(TokenStore::minecraftKey());
    TokenStore::clear(TokenStore::accountsKey());
    TokenStore::clear(TokenStore::currentUuidKey());
    const QString accountName = QStringLiteral("T32User");
    QString offlineError;
    check(AccountStore::addOffline(accountName, &offlineError),
          "T32.0 建一个离线账号并设成当前身份（这一节一个外网请求都不发）", offlineError);

    {
        MainWindow w;
        w.show();
        QApplication::processEvents();
        OverlayHost *overlay = w.ui() != nullptr ? w.ui()->overlay() : nullptr;
        check(overlay != nullptr, "T32.1 覆盖层拿得到（那一页开在它上面）");
        if (overlay == nullptr) {
            restoreSettings();
            return;
        }

        // 点完按钮**马上收掉实例设置那一页**：新页只能在覆盖层空着的时候开，而"让位"
        // 要等这一页自己的嵌套循环退出去才生效（理由逐字写在 T28 那一节里）。
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
        // 这一层空着 ⇒ 那一趟结果回来时自己开出「更新中心」，`closer` 记一条再收掉它。
        pumpUntil([&closer] { return !closer.seen.isEmpty(); }, 20000);
        closer.stop();
        pumpUntil([] { return currentOverlay() == nullptr; }, 8000);

        // 发出去的那几发（请求行里"含"这个串的有几发）。
        const auto countMatching = [&api](const QString &needle) {
            int n = 0;
            for (const QString &line : api.requests) {
                if (line.contains(needle)) {
                    ++n;
                }
            }
            return n;
        };

        check(closer.seen.size() == 1,
              "T32.1 那一趟真的跑完了、而且**只开了一页**（结论现在是开成一整页，不再弹一段字）",
              QStringLiteral("开了 %1 页").arg(closer.seen.size()));
        const PageCloser::Seen seen = closer.seen.isEmpty() ? PageCloser::Seen() : closer.seen.first();
        check(seen.title == UpdateCenterPage::windowTitle(),
              "T32.1 开出来的那一页就是「更新中心」（页眉上那行字）", seen.title);
        check(seen.updateCenterSummary.contains(ModDependencies::updateSummaryText(2, 1)),
              "★★ T32.2 顶上那句说的是「检查了 **2** 个资源，1 个有新版本」—— 盘上就是两个文件"
              "（那个双认的在两家各留了一条记录，否则这里会写成 3、而名单里同一行出现两次）",
              seen.updateCenterSummary);
        // 这一条是这一节的正题：**一条都不能是「说不清」**。退回问 CurseForge 那一发
        // 要是没发出去（或者发出去没接上），那个双认的文件那一行就只剩"说不清"。
        const QString unknownText = MainWindowUI::updateCenterRowStateText(2, 0, QString());
        check(!seen.updateCenterRowStates.contains(unknownText),
              "★★ T32.2 名单里**一条「说不清」都没有** —— 那一条已经在 CurseForge 那边"
              "问出口了（批量那一发对它一个答案都没给，全靠退回那一发兜住）",
              seen.updateCenterRowStates.join(QStringLiteral(" / ")));
        // 行数 = 2（每一条一行）。多于 2 = 同一个文件出了两行。
        check(seen.updateCenterRowNames.size() == 2,
              "★★ T32.2 名单正好两行（一个文件一行 —— 同一个模组出两行的话用户会以为盘上"
              "有两个一样的模组）",
              QStringLiteral("%1 行").arg(seen.updateCenterRowNames.size()));
        check(seen.updateCenterRowNames.count(dualTitle) == 1,
              "★★ T32.2 那个两家都认得出的模组在名单里**只出现一次**",
              QStringLiteral("%1 次").arg(seen.updateCenterRowNames.count(dualTitle)));
        check(seen.updateCenterRowNames.contains(dualTitle) &&
                  seen.updateCenterRowNames.contains(cfOnlyTitle),
              "★★ T32.2 两行写的是**名字**（不是文件名）：双认的和只有 CF 认得的那两个都在"
              "（名字取不到时才退回文件名）",
              seen.updateCenterRowNames.join(QStringLiteral(" / ")));
        // 状态那一格：双认的那条「可更新到 …」（答它的是 CurseForge 那一发）、只有 CF 认得
        // 的那条「已是最新（…）」。
        check(seen.updateCenterRowStates.size() == 2 &&
                  seen.updateCenterRowStates.at(0).startsWith(QStringLiteral("可更新到")) &&
                  seen.updateCenterRowStates.at(1).startsWith(QStringLiteral("已是最新")),
              "★★ T32.2 两行分别判出了结论：双认的「可更新到 X」（答它的是 CurseForge 那一发）、"
              "只有 CF 认得的「已是最新」",
              seen.updateCenterRowStates.join(QStringLiteral(" / ")));
        // 两组那一节：**组头自己带条数**，而且是"能动手的那一组在上面"。
        check(seen.updateCenterGroupTitles.size() == 2 &&
                  seen.updateCenterGroupTitles.at(0) ==
                      MainWindowUI::updateCenterOutdatedGroupTitleText(1) &&
                  seen.updateCenterGroupTitles.at(1) ==
                      MainWindowUI::updateCenterRestGroupTitleText(1),
              "★★ T32.2 两组都摆着、条数各是 1，而且**能动手的那一组在上面**",
              seen.updateCenterGroupTitles.join(QStringLiteral(" / ")));

        // ---------- 那几发请求本身 ----------
        check(countMatching(QStringLiteral("/version_files/update")) == 1,
              "★★ T32.3 那个双认的文件走的是**批量那一发**（Modrinth 那一拨一个文件只问一家）",
              QStringLiteral("%1 发").arg(countMatching(QStringLiteral("/version_files/update"))));
        check(countMatching(QStringLiteral("/project/DUALMOD/version")) == 0,
              "★★ T32.3 而它**没有**再被单独问一遍 Modrinth 的版本列表"
              "（同一件事问两家 = 请求数翻倍，那正是真机上撞 429 的那条路）",
              QStringLiteral("%1 发")
                  .arg(countMatching(QStringLiteral("/project/DUALMOD/version"))));
        check(countMatching(QStringLiteral("/mods/9001/files")) == 1,
              "★★ T32.3 批量那一发没答它 ⇒ **退回问 CurseForge 的版本列表**（那一发真的"
              "发出去了，而且只有一发）",
              QStringLiteral("%1 发").arg(countMatching(QStringLiteral("/mods/9001/files"))));
        check(countMatching(QStringLiteral("/mods/9002/files")) == 1,
              "T32.3 只有 CurseForge 认得的那一个也问了它这一家（一个文件至少被问过一次）",
              QStringLiteral("%1 发").arg(countMatching(QStringLiteral("/mods/9002/files"))));
        // 批量那一发的正文里带的是那个双认文件的 sha1（带错了 = 那一发白问）。
        QByteArray batchBody;
        for (int i = 0; i < api.requests.size(); ++i) {
            if (api.requests.at(i).contains(QStringLiteral("/version_files/update"))) {
                batchBody = api.bodies.value(i);
            }
        }
        check(batchBody.contains(hDual.sha1.toUtf8()) &&
                  !batchBody.contains(hOnly.sha1.toUtf8()),
              "★★ T32.3 批量那一发的正文里只有那个双认文件的 sha1"
              "（只有 CurseForge 认得的那个压根不该进这一发 —— 它连项目号都没有）",
              QString::fromUtf8(batchBody));
    }

    {
        api.modrinthVersionFilesUpdateReply = QByteArrayLiteral("{}");
        api.curseForgeVersionListByMod[QString::number(dualModId)] = QByteArrayLiteral("{\"data\":[]}");
        api.curseForgeVersionListByMod[QString::number(cfOnlyModId)] = QByteArrayLiteral("{\"data\":[]}");
        MainWindow w;
        w.show();
        QApplication::processEvents();
        auto *activity = w.findChild<ActivityCenterController *>();
        activity->tasks()->clearFinished();
        InstanceUpdateCheckController *controller = nullptr;
        for (auto *child : w.children())
            if (auto *candidate = dynamic_cast<InstanceUpdateCheckController *>(child)) controller = candidate;
        QTimer::singleShot(0, [&] {
            auto *host = currentPageOverlay();
            auto *page = host ? host->findChild<InstanceSettingsDialog *>() : nullptr;
            if (!page || !controller) return;
            page->showPage(QStringLiteral("mod"));
            auto *mods = page->filesPage(QStringLiteral("mod"));
            check(activity->tasks()->finished().isEmpty(), "no partial update notification before checking finishes");
            check(pumpUntil([&] { return mods->updateChecked() && !controller->findChild<QThread *>(); }, 20000),
                  "all failed resource checks finish before notification");
            const auto records = activity->tasks()->finished();
            check(records.size() == 1 && records.first().errorSummary.startsWith(QStringLiteral("更新检查结果可能不完整"))
                  && records.first().errorSummary.contains(dualTitle) && records.first().errorSummary.contains(cfOnlyTitle),
                  "multiple unknown resources produce one incomplete-result report with per-resource details");
            QMetaObject::invokeMethod(page, "checkUpdatesRequested", Qt::DirectConnection);
            check(pumpUntil([&] { return !controller->findChild<QThread *>(); }, 20000), "repeated background check completes");
            check(activity->tasks()->finished().size() == 1, "identical incomplete results are not repeatedly notified");
            host->closeOverlay();
        });
        QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection, Q_ARG(QString, idV));
        QApplication::processEvents();
    }
    restoreSettings();
    check(AppPaths::minecraftDir() == originalDir, "T32.9 数据根目录换回来了");
}

