#include "Support.hpp"
#include "controllers/DownloadBatchController.hpp"
#include "controllers/ActivityCenterController.hpp"
void runUpdateCenterActionTests() {
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "T38.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }
    struct DefaultRootGuard {
        QVariant previous = qApp->property("wtfml.testDefaultMinecraftDir");
        ~DefaultRootGuard() { qApp->setProperty("wtfml.testDefaultMinecraftDir", previous); }
    } defaultRoot;
    qApp->setProperty("wtfml.testDefaultMinecraftDir", root.filePath("launcher-data"));

    FakeLookupApi api;
    check(api.start(), "T38.0 假接口服务器起来了");

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

    // 报得出游戏版本号和加载器的实例（和 T28/T32 同一个手法、同一个理由）。
    const QString idV = QStringLiteral("1.97.4");
    const QDir versionsDir(root.path() + QStringLiteral("/versions"));
    makeInstalledVersion(versionsDir, idV);
    makeVersionMeta(versionsDir, idV,
                    QByteArrayLiteral("{\"loaderName\":\"Fabric\",\"loaderVersion\":\"0.15.11\"}"));
    const QString dirV = AppPaths::versionDir(idV);

    const QByteArray originalBytes = QByteArrayLiteral("T38-ORIGINAL-MOD");
    const QString modJar = dirV + QStringLiteral("/mods/更新中心模组.jar");
    writeFileAt(modJar, originalBytes);
    // ② 一条**禁用着**的、内容和上面那一份**逐字节一样**（⇒ 同一个 sha1 ⇒ 真查的话它照样
    // 判得出"可更新"）。它的用处只有一个：量这一趟**整个跳过**了它，而且"更新"那一批
    // 也**没有**把它启用回来（「禁用资源保持跳过，不擅自启用或更新」）。
    const QString disabledJar = dirV + QStringLiteral("/mods/禁用模组.jar.disabled");
    writeFileAt(disabledJar, originalBytes);

    ModpackExport::FileHashes hMod;
    QString hashError;
    const bool hashed = ModpackExport::hashFile(modJar, true, &hMod, &hashError);
    check(hashed, "T38.0 那个模组的 sha1 算出来了（用产品自己那份实现）", hashError);
    if (!hashed) {
        restoreSettings();
        return;
    }

    const QString project = QStringLiteral("UCPROJ38");
    const QString modTitle = QStringLiteral("更新中心模组");
    const QString targetVersionId = QStringLiteral("VN38");
    const QString targetVersionNumber = QStringLiteral("2.0");
    const QString targetFileName = QStringLiteral("更新中心模组-2.0.jar");
    // 这一版给的地址指向**假服务器上一个不存在的路径**（那台机器只认得那几条反查路由，
    // 别的一律 404）—— 于是这一批**必定失败**，而且**一个字节的外网都不碰**。选 404 而不
    // 选别的：回滚点是**动手之前**摆的，所以"失败了"和"留了回滚点"这两件事同时成立，
    // 正好把「更新前创建轻量回滚点，失败时可恢复原文件」那半句量完整。
    const QString deadUrl = api.modrinthBase() + QStringLiteral("/nope-38.jar");

    // `POST /v2/version_files`：哈希 → 项目号。
    api.modrinthReply = QByteArrayLiteral("{\"") + hMod.sha1.toUtf8() +
                        QByteArrayLiteral("\":{\"project_id\":\"") + project.toUtf8() +
                        QByteArrayLiteral("\",\"version_number\":\"1.0\",\"files\":[{\"hashes\":{"
                                          "\"sha1\":\"") +
                        hMod.sha1.toUtf8() +
                        QByteArrayLiteral("\",\"sha512\":\"cafe\"},"
                                          "\"url\":\"https://cdn.modrinth.com/data/uc.jar\","
                                          "\"filename\":\"更新中心模组.jar\",\"size\":10}]}}");
    api.modrinthProjectsReply =
        QByteArrayLiteral("[{\"id\":\"") + project.toUtf8() +
        QByteArrayLiteral("\",\"title\":\"") + modTitle.toUtf8() +
        QByteArrayLiteral("\",\"slug\":\"uc\",\"description\":\"一个模组\","
                          "\"game_versions\":[\"1.97.4\"],\"loaders\":[\"fabric\"]}]");
    // `POST /v2/version_files/update`：那一拨要换上的那一版（判"可更新"靠的就是它 ——
    // 这一条的文件 sha1 和盘上那份**不一样**）。
    api.modrinthVersionFilesUpdateReply =
        QByteArrayLiteral("{\"") + hMod.sha1.toUtf8() +
        QByteArrayLiteral("\":{\"id\":\"") + targetVersionId.toUtf8() +
        QByteArrayLiteral("\",\"version_number\":\"") + targetVersionNumber.toUtf8() +
        QByteArrayLiteral("\",\"date_published\":\"2026-09-01T00:00:00Z\","
                          "\"game_versions\":[\"1.97.4\"],\"loaders\":[\"fabric\"],"
                          "\"files\":[{\"hashes\":{"
                          "\"sha1\":\"2222222222222222222222222222222222222222\","
                          "\"sha512\":\"aa\"},"
                          "\"url\":\"") +
        deadUrl.toUtf8() +
        QByteArrayLiteral("\",\"filename\":\"") + targetFileName.toUtf8() +
        QByteArrayLiteral("\",\"size\":10,\"primary\":true}]}}");
    // CurseForge 那一支也要答得上话（那一趟反查每回都发指纹那一发）：空结果、没有未匹配的。
    api.curseForgeReply =
        QByteArrayLiteral("{\"data\":{\"exactMatches\":[],\"unmatchedFingerprints\":[]}}");

    TokenStore::clear(TokenStore::microsoftKey());
    TokenStore::clear(TokenStore::minecraftKey());
    TokenStore::clear(TokenStore::accountsKey());
    TokenStore::clear(TokenStore::currentUuidKey());
    const QString accountName = QStringLiteral("T38User");
    seedOfflineRandomSkin(accountName, kSeededSkinUrl, QColor(0x60, 0x40, 0x90));
    QString offlineError;
    check(AccountStore::addOffline(accountName, &offlineError),
          "T38.0 建一个离线账号并设成当前身份（这一节一个外网请求都不发）", offlineError);

    // 那一笔记录在这本账里的键（**相对实例目录**那一条，唯一出处 `UpdatePolicy::storeKey()`）。
    const QString policyKey = UpdatePolicy::storeKey(dirV, modJar);
    check(!policyKey.isEmpty(), "T38.0 那一笔在账本里的键拼得出来", policyKey);
    check(!QFileInfo::exists(UpdatePolicy::storePath()) &&
              !QFileInfo::exists(UpdateRollback::snapshotDir(idV)),
          "T38.0 开局是干净的：账本和回滚点都还不存在（后面每一条「写下去了」才说得通）");

    {
        MainWindow w;
        w.show();
        QApplication::processEvents();
        OverlayHost *overlay = w.ui() != nullptr ? w.ui()->overlay() : nullptr;
        check(overlay != nullptr, "T38.1 覆盖层拿得到（那一页开在它上面）");
        if (overlay == nullptr) {
            restoreSettings();
            return;
        }

        // 每一回都从**盘上**重新读一遍那本账：读内存里那份等于让产品自己证明自己。
        const auto policyEntry = [&]() {
            QString problem;
            const UpdatePolicy::Store store =
                UpdatePolicy::load(UpdatePolicy::storePath(), &problem);
            return UpdatePolicy::entryOf(store, idV, policyKey);
        };
        const auto snapshotFiles = [&]() -> int {
            bool has = false;
            return UpdateRollback::read(idV, &has).fileCount();
        };
        const auto centrePageOpen = [&]() -> bool {
            OverlayHost *host = currentPageOverlay();
            if (host == nullptr || !host->pageOpen()) {
                return false;
            }
            auto *title = host->findChild<QLabel *>(OverlayHost::overlayTitleLabelName());
            if (title == nullptr || title->text() != UpdateCenterPage::windowTitle()) {
                return false;
            }
            auto *page = host->findChild<UpdateCenterPage *>(MainWindowUI::updateCenterPageName());
            return page != nullptr && page->isVisible();
        };
        const auto rowState = [&]() -> QString {
            OverlayHost *host = currentPageOverlay();
            if (host == nullptr) {
                return QString();
            }
            auto *label = newestByObjectName<QLabel>(
                host, MainWindowUI::updateCenterRowStateName(0));
            return label == nullptr ? QString() : label->text();
        };
        const auto chipText = [&](const QString &name) -> QString {
            OverlayHost *host = currentPageOverlay();
            if (host == nullptr) {
                return QString();
            }
            auto *button = newestByObjectName<QPushButton>(host, name);
            return button == nullptr ? QString() : button->text();
        };
        const auto clickChip = [&](const QString &name) -> bool {
            OverlayHost *host = currentPageOverlay();
            if (host == nullptr) {
                return false;
            }
            auto *button = newestByObjectName<QPushButton>(host, name);
            if (button == nullptr) {
                return false;
            }
            button->click();
            return true;
        };
        const auto countRows = [&]() -> int {
            OverlayHost *host = currentPageOverlay();
            if (host == nullptr) {
                return 0;
            }
            int n = 0;
            while (host->findChild<QWidget *>(MainWindowUI::updateCenterRowName(n)) != nullptr) {
                ++n;
            }
            return n;
        };
        const auto undoButton = [&]() -> QPushButton * {
            OverlayHost *host = currentPageOverlay();
            if (host == nullptr) {
                return nullptr;
            }
            return newestByObjectName<QPushButton>(host, MainWindowUI::updateCenterUndoName());
        };

        // 那一批失败时会弹一份"逐条结果"的框、撤销时会问一句 —— 都排一个 0 号答话器答掉。
        PromptAnswerer answerer({0});
        answerer.start();

        const QString stateOutdated =
            MainWindowUI::updateCenterRowStateText(0, 0, targetVersionNumber);
        const QString stateIgnoredText =
            MainWindowUI::updateCenterRowStateText(0, 1, targetVersionNumber);
        const QString statePinnedText = MainWindowUI::updateCenterRowStateText(0, 2, QString());

        int stage = 0;
        QElapsedTimer driving;
        driving.start();
        int rowsOnPage = -1;
        QString stateInitial;
        QString ignoreChipInitial;
        QString stateIgnored;
        QString ignoreChipToggled;
        QString stateUnignored;
        QString statePinned;
        QString pinChipToggled;
        QString stateUnpinned;
        bool policyIgnored = false;
        bool policyCleared = false;
        bool policyPinned = false;
        bool policyUnpinned = false;
        bool undoShownAfterFail = false;
        QString undoTextAfterFail;
        int snapshotAfterFail = -1;
        bool undoHiddenAfterUndo = false;
        int snapshotAfterUndo = -1;
        bool jarBackAfterUndo = false;
        QString stateAfterUndo;
        bool updatePaused = false, updateResumed = false, originalWhilePaused = false, pausing = false;
        QStringList pauseObservations;
        auto *activity = w.findChild<ActivityCenterController *>();
        DownloadBatchController *batches = nullptr;
        for (auto *child : w.children()) if (auto *candidate = dynamic_cast<DownloadBatchController *>(child)) batches = candidate;
        if (activity && batches) QObject::connect(activity->tasks(), &Tasks::TaskManager::taskChanged, &w,
            [&](Tasks::TaskId id) {
                if (pausing || updatePaused) return;
                const auto task = activity->tasks()->snapshot(id);
                if (task.title.contains(QStringLiteral("更新")) && pauseObservations.size() < 20)
                    pauseObservations.append(QString("id=%1 target=%2 owner=%3 pause=%4 state=%5").arg(id).arg(task.targetId).arg(batches->owns(id)).arg(task.capabilities.canPause).arg(int(task.state)));
                if (!batches->owns(id)) return;
                if (task.targetId != idV || !task.capabilities.canPause) return;
                pausing = true;
                updatePaused = batches->pause(id);
                QFile original(modJar);
                originalWhilePaused = original.open(QIODevice::ReadOnly) && original.readAll() == originalBytes;
                original.close();
                if (updatePaused) QTimer::singleShot(0, &w, [&, id] { updateResumed = batches->resume(id); });
                pausing = false;
            });

        QTimer driver;
        driver.setInterval(10);
        QObject::connect(&driver, &QTimer::timeout, [&] {
            if (driving.elapsed() > 90000) {
                driver.stop();  // 兜底：不让测试挂在一个永远不来的阶段上
                return;
            }
            switch (stage) {
            case 0: {
                if (!centrePageOpen()) {
                    return;  // 那一页还没开出来（这会儿多半还开着实例设置）
                }
                rowsOnPage = countRows();
                stateInitial = rowState();
                ignoreChipInitial = chipText(MainWindowUI::updateCenterRowIgnoreName(0));
                if (!stateInitial.startsWith(QStringLiteral("可更新到"))) {
                    return;  // 结论还没摆上来
                }
                clickChip(MainWindowUI::updateCenterRowIgnoreName(0));
                stage = 1;
                return;
            }
            case 1: {
                // 等它**当场重喂**过一遍：那一行得写成「已忽略这一版（2.0）」。
                if (!centrePageOpen() || rowState() != stateIgnoredText) {
                    return;
                }
                stateIgnored = rowState();
                ignoreChipToggled = chipText(MainWindowUI::updateCenterRowIgnoreName(0));
                policyIgnored = policyEntry().ignoreVersionId == targetVersionId;
                clickChip(MainWindowUI::updateCenterRowIgnoreName(0));  // 「不再忽略」
                stage = 2;
                return;
            }
            case 2: {
                if (!centrePageOpen() || rowState() != stateOutdated) {
                    return;
                }
                stateUnignored = rowState();
                policyCleared = policyEntry().ignoreVersionId.isEmpty();
                clickChip(MainWindowUI::updateCenterRowPinName(0));
                stage = 3;
                return;
            }
            case 3: {
                if (!centrePageOpen() || rowState() != statePinnedText) {
                    return;
                }
                statePinned = rowState();
                pinChipToggled = chipText(MainWindowUI::updateCenterRowPinName(0));
                policyPinned = policyEntry().pinnedSha1 == hMod.sha1;
                clickChip(MainWindowUI::updateCenterRowPinName(0));  // 「不再固定」
                stage = 4;
                return;
            }
            case 4: {
                if (!centrePageOpen() || rowState() != stateOutdated) {
                    return;
                }
                stateUnpinned = rowState();
                policyUnpinned = policyEntry().pinnedSha1.isEmpty();
                clickChip(MainWindowUI::updateCenterRowUpdateName(0));  // 就更新这一条
                stage = 5;
                return;
            }
            case 5: {
                // 那一批跑完了、更新中心**自己又开回来了**（失败 ⇒ 底下那颗「撤销上一次
                // 更新」露着）。等它露出来再动手。
                if (!centrePageOpen()) {
                    return;
                }
                QPushButton *undo = undoButton();
                if (undo == nullptr || undo->isHidden()) {
                    return;
                }
                undoShownAfterFail = true;
                undoTextAfterFail = undo->text();
                snapshotAfterFail = snapshotFiles();
                // 把那一个文件改成别的字节（"更新之后你自己又动过它"那种样子）—— 撤销必须
                // 真把它退回去，才说明退的是**内容**，而不是"反正一样就什么都没干"。
                writeFileAt(modJar, QByteArrayLiteral("T38-CHANGED-BY-USER"));
                undo->click();  // → 问一句（答话器答它）→ 真还原
                stage = 6;
                return;
            }
            case 6: {
                if (!centrePageOpen()) {
                    return;
                }
                QPushButton *undo = undoButton();
                if (undo == nullptr || !undo->isHidden()) {
                    return;  // 撤销那条路还没走完（那一问还开着 / 还没重喂）
                }
                undoHiddenAfterUndo = undo->isHidden();
                snapshotAfterUndo = snapshotFiles();
                jarBackAfterUndo = readBytesAt(modJar) == originalBytes;
                stateAfterUndo = rowState();
                OverlayHost *host = currentPageOverlay();
                if (auto *back = host->findChild<QPushButton *>(
                        OverlayHost::overlayBackButtonName())) {
                    back->click();  // 收页（返回箭头 = reject）
                } else {
                    host->closeOverlay();
                }
                stage = 7;
                return;
            }
            default:
                return;
            }
        });
        driver.start();

        // 先开实例设置 → 点「检查更新」→ **立刻收掉这一页**（理由逐字写在 T28 那一节里：
        // 结果回来时要开的新页只能在覆盖层空着的时候开，而"让位"要等这一页自己的嵌套循环
        // 退出去才生效）→ 回到底下那一层，让驱动器按阶段往下走。
        bool checkedFound = false;
        QTimer::singleShot(0, [&] {
            OverlayHost *host = currentPageOverlay();
            auto *page = host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
            if (page == nullptr) {
                return;
            }
            page->showPage(QStringLiteral("mod"));
            if (auto *button = page->findChild<QPushButton *>(
                    InstanceSettingsDialog::checkUpdatesButtonName())) {
                checkedFound = true;
                button->click();
            }
            if (auto *back = host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
                back->click();
            } else {
                host->closeOverlay();
            }
        });
        QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                  Q_ARG(QString, idV));
        QApplication::processEvents();

        const bool reachedEnd = pumpUntil([&] { return stage >= 7; }, 90000);
        pumpUntil([] { return currentOverlay() == nullptr; }, 8000);
        driver.stop();
        answerer.stop();

        check(checkedFound, "T38.1 实例设置那一页上「检查更新」那颗按钮找到了");
        check(updatePaused && updateResumed && originalWhilePaused,
              "T38.1 更新中心实际文件批次归原任务所有，暂停保留原件，继续后沿原完成/回滚接线收尾",
              QString("paused=%1 resumed=%2 original=%3 activity=%4 batches=%5; %6").arg(updatePaused).arg(updateResumed).arg(originalWhilePaused).arg(activity != nullptr).arg(batches != nullptr).arg(pauseObservations.join("; ")));
        check(reachedEnd,
              "★★ T38.2 整条路走完了：忽略这一版 → 不再忽略 → 固定这一版 → 不再固定 → "
              "更新 → 撤销",
              QStringLiteral("停在第 %1 阶段").arg(stage));
        if (!reachedEnd) {
            check(false, "T38.2 上面那一步没走完，后面那几条断言不看了");
        }
        check(rowsOnPage == 1,
              "★★ T38.2 那一页上只有**一行** —— 那个禁用着的文件（内容和它逐字节一样、真查的"
              "话照样判得出「可更新」）**整个跳过了**（禁用资源保持跳过）",
              QStringLiteral("%1 行").arg(rowsOnPage));

        // ---------- 「忽略这一版」/「不再忽略」----------
        check(stateInitial == stateOutdated,
              "T38.2 还没动过的时候那一行写的是「可更新到 2.0」", stateInitial);
        check(ignoreChipInitial == MainWindowUI::updateCenterRowIgnoreText(),
              "T38.2 那一颗写着「忽略这一版」", ignoreChipInitial);
        check(stateIgnored == stateIgnoredText && ignoreChipToggled
                  == MainWindowUI::updateCenterRowIgnoreUndoText(),
              "★★ T38.3 按下「忽略这一版」**当场**就变了：那一行写成「已忽略这一版（2.0）」、"
              "那一颗翻成「不再忽略」（不重喂的话屏幕上一点变化都没有 —— 用户只能靠「再点"
              "一次看看会不会还写那句话」来判断自己按中了没有）",
              stateIgnored + QStringLiteral(" / ") + ignoreChipToggled);
        check(policyIgnored,
              "★★ T38.3 账本（wtfml.updates）里记的是**要换上的那一版**的 id（VN38）—— "
              "写的是版本号而不是文件名：作者再发一版时这一条要自己回来");
        check(stateUnignored == stateOutdated && policyCleared,
              "★★ T38.3 「不再忽略」把那一笔清掉了、那一行也回到「可更新到 2.0」",
              stateUnignored);

        // ---------- 「固定这一版」/「不再固定」----------
        check(statePinned == statePinnedText && pinChipToggled
                  == MainWindowUI::updateCenterRowPinUndoText(),
              "★★ T38.3 按下「固定这一版」：那一行写「已固定（不再提示）」、那一颗翻成"
              "「不再固定」",
              statePinned + QStringLiteral(" / ") + pinChipToggled);
        check(policyPinned,
              "★★ T38.3 固定记的是**盘上那一份的内容 sha1**（不是版本号）：判据手上只有 "
              "sha1，编一个不存在的判据就是这个功能塌掉的形状");
        check(stateUnpinned == stateOutdated && policyUnpinned,
              "T38.3 「不再固定」也把那一笔清掉了、那一行回到「可更新到 2.0」", stateUnpinned);

        // ---------- 「更新」那一批：失败、回滚点、逐条结果 ----------
        check(undoShownAfterFail && undoTextAfterFail == MainWindowUI::updateCenterUndoText(1),
              "★★ T38.4 那一批失败之后，更新中心**自己又开回来了**、底下那颗写的是"
              "「撤销上一次更新（1 个文件）」（用户按完那一颗之后下一步就在这里：要么看看"
              "剩下的、要么退回去）",
              undoTextAfterFail);
        check(snapshotAfterFail == 1,
              "★★ T38.4 **动手之前**真留了一份回滚点（盘上那份清单里就是那 1 个文件）—— "
              "「更新前创建轻量回滚点」这一条在界面按下去的那条路上真的走到了",
              QStringLiteral("清单里 %1 个").arg(snapshotAfterFail));
        {
            // 失败那一框里**逐条**说着每个资源的结果（「不只显示总数」）。这一条只有
            // 用户看得见的地方说得清 —— `m_taskManager` 是私有的，而屏幕上的那一框就是
            // 产品对用户交的账。
            bool reported = false;
            QString reportedText;
            for (const PromptAnswerer::Answered &one : answerer.answered) {
                if (one.title == QStringLiteral("更新没能全都换上")) {
                    reported = true;
                    reportedText = one.text;
                }
            }
            check(reported
                      && reportedText.contains(QFileInfo(modJar).fileName()
                                              + QStringLiteral(" —— 没换成功")),
                  "★★ T38.4 失败那一框里**逐条**列着每个资源的结果（「· 更新中心模组.jar —— "
                  "没换成功：…」），不是只说一句「有 1 个没换掉」",
                  reportedText);
        }
        check(QFile::exists(disabledJar)
                  && !QFile::exists(dirV + QStringLiteral("/mods/禁用模组.jar")),
              "★★ T38.4 那个禁用着的文件**还是禁用着**（那一批没有顺手把它启用回来 —— "
              "禁止资源保持跳过，不擅自启用或更新）");

        // ---------- 「撤销上一次更新」----------
        check(undoHiddenAfterUndo && jarBackAfterUndo,
              "★★ T38.5 「撤销上一次更新」真把文件退回更新前那一份了（内容逐字节回到原来"
              "那些字节）",
              QStringLiteral("退回来了：%1").arg(jarBackAfterUndo ? QStringLiteral("是")
                                                                   : QStringLiteral("不是")));
        check(snapshotAfterUndo == 0 && !QFileInfo::exists(UpdateRollback::snapshotDir(idV)),
              "★★ T38.5 那一份回滚点**用掉了**（撤销成功之后清单和目录都不在了）—— 留着的"
              "话下一次开页还会露一颗「撤销上一次更新」，而它指向的那次更新已经不存在了",
              QStringLiteral("清单里 %1 个").arg(snapshotAfterUndo));
        check(stateAfterUndo.startsWith(QStringLiteral("可更新到")),
              "T38.5 退回原样之后那一行照样写着「可更新到 2.0」（盘上那份确实还是老的）",
              stateAfterUndo);
    }

    restoreSettings();
    check(AppPaths::minecraftDir() == originalDir, "T38.9 数据根目录换回来了");
}

