#include "Support.hpp"
void runInstanceFileResourceTests() {
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "T24.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }

    FakeLookupApi api;
    check(api.start(), "T24.0 假接口服务器起来了");

    QSettings sandbox;
    const QVariant foldersBefore = sandbox.value(QStringLiteral("Launcher/Folders"));
    const QVariant currentBefore = sandbox.value(QStringLiteral("Launcher/CurrentFolder"));
    const QVariant sourceBefore = sandbox.value(QStringLiteral("Download/Source"));
    const QVariant modrinthBefore = sandbox.value(QStringLiteral("Launcher/ModrinthApiBase"));
    const QVariant curseForgeBefore = sandbox.value(QStringLiteral("Launcher/CurseForgeApiBase"));
    const auto restoreSettings = [&] {
        sandbox.setValue(QStringLiteral("Launcher/Folders"), foldersBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), currentBefore);
        sandbox.setValue(QStringLiteral("Download/Source"), sourceBefore);
        sandbox.setValue(QStringLiteral("Launcher/ModrinthApiBase"), modrinthBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurseForgeApiBase"), curseForgeBefore);
        sandbox.sync();
        AppPaths::setMinecraftDir(originalDir);
    };
    // 数据根目录**两头都改**（理由见 T19/T20：MainWindow 构造里会拿盘上那个「当前游戏
    // 文件夹」再把 AppPaths 改一次）。两个接口地址也从设置键推下去 —— 那一趟反查读的就是
    // 这两个键（见 InstanceIconLookup::Options）。
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

    // ---------- 夹具：一个实例，里面三个文件 ----------
    //
    // 这个实例**报得出游戏版本号（1.20.1）和一个加载器（NeoForge）**（ 加）：
    // "点开一个资源时按当前实例的版本预选、把它的加载器排前面"这件事（要求
    // 「点开某个模组等的时候将版本自动选到当前实例对应的那个，对应的模组加载器的版本
    // 放在前面」）走的就是这两份值，而**一个报不出东西的实例量不出来**（没有版本号 ⇒
    // 预选为空 ⇒ 停在「全部」，和"接线断了"的样子一模一样）。
    // 版本号出处：没有 `realVersion`、目录名就是个像样的版本号 ⇒ `instanceGameVersion()`
    // 取链尾那个 id（见 VersionManager::instanceGameVersion）。
    //
    // 这个版本号**必须是清单里真有的**那一个。`instanceGameVersion()` 的
    // 每一格都要过 `isKnownGameVersion()`：清单**已经拉到**时它只认清单里的 id，清单
    // **还没到**（离线第一趟）才退回"长得像版本号"的放宽判据。原来写的是凭空的
    // `1.97.1` —— 开发机（连不上 Mojang、清单是空的）一直绿，CI（网络好、清单在
    // T24 之前就到了）稳定红：chip 停在「全部」、底下也就不筛，两条断言一起红。
    // 用清单里真有的 `1.20.1`（而**不是**反例要用的 `1.21.1`）两种情形都成立。
    const QString idV = QStringLiteral("1.20.1");
    const QString instanceLoader = QStringLiteral("NeoForge");
    const QDir versionsDir(root.path() + QStringLiteral("/versions"));
    makeInstalledVersion(versionsDir, idV);
    makeVersionMeta(versionsDir, idV,
                    QByteArrayLiteral("{\"loaderName\":\"NeoForge\",\"loaderVersion\":\"21.1.0\"}"));
    const QString dirV = AppPaths::versionDir(idV);

    // ① 认得出的资源包（这一节的主角）、② 认不出来的资源包（名字该点不动）、
    // ③ **认得出的模组**（名字该点不动 —— 这是规矩，不是"没认出来"）。
    const QString packZip = dirV + QStringLiteral("/resourcepacks/材质包甲.zip");
    const QString unknownZip = dirV + QStringLiteral("/resourcepacks/没认出来的.zip");
    const QString modJar = dirV + QStringLiteral("/mods/模组甲.jar");
    writeFileAt(packZip, QByteArrayLiteral("T24-PACK-A"));
    writeFileAt(unknownZip, QByteArrayLiteral("T24-NOBODY"));
    writeFileAt(modJar, QByteArrayLiteral("T24-MOD-A"));

    // sha1 用**产品自己那份实现**算（和 T22 同一个手法）：这一节验的不是哈希算法。
    ModpackExport::FileHashes hPack;
    ModpackExport::FileHashes hMod;
    QString hashError;
    const bool hashed = ModpackExport::hashFile(packZip, true, &hPack, &hashError) &&
                        ModpackExport::hashFile(modJar, true, &hMod, &hashError);
    check(hashed, "T24.0 两个夹具文件的 sha1 算出来了", hashError);
    if (!hashed) {
        restoreSettings();
        return;
    }

    const QString packProject = QStringLiteral("PACKPROJ");
    const QString modProject = QStringLiteral("MODPROJX");
    const QString packTitle = QStringLiteral("材质包甲");
    const QString modTitle = QStringLiteral("模组甲");
    // `POST /v2/version_files`：哈希 → 项目号。**只映射那两个认得出的**；第三个文件
    // 不出现在里面 —— 那正是"没匹配上"的那一档。
    //
    // `files[]` **不能是空的**：`parseModrinthVersionFiles()` 要在这个版本的文件里挑
    // 「sha1 和键相同」的那一份，挑不到就不认这一条（见那个函数里 `fileSha1 != key`
    // 那一段）。空数组 ⇒ 一条 hit 都没有 ⇒ 后面那一发 `/projects?ids=` 压根不发，
    // 而屏幕上的样子是「名字点不动」—— 看着像产品没接线，其实是夹具没给全。
    api.modrinthReply = QByteArrayLiteral("{\"") + hPack.sha1.toUtf8() +
                        QByteArrayLiteral("\":{\"project_id\":\"") + packProject.toUtf8() +
                        QByteArrayLiteral("\",\"version_number\":\"1.0\",\"files\":[{\"hashes\":{"
                                          "\"sha1\":\"") +
                        hPack.sha1.toUtf8() +
                        QByteArrayLiteral("\",\"sha512\":\"deadbeef\"},\"url\":\"https://cdn."
                                          "modrinth.com/data/pack-a.zip\",\"filename\":"
                                          "\"材质包甲.zip\",\"size\":10}]},\"") +
                        hMod.sha1.toUtf8() + QByteArrayLiteral("\":{\"project_id\":\"") +
                        modProject.toUtf8() +
                        QByteArrayLiteral("\",\"version_number\":\"1.0\",\"files\":[{\"hashes\":{"
                                          "\"sha1\":\"") +
                        hMod.sha1.toUtf8() +
                        QByteArrayLiteral("\",\"sha512\":\"cafebabe\"},\"url\":\"https://cdn."
                                          "modrinth.com/data/mod-a.jar\",\"filename\":"
                                          "\"模组甲.jar\",\"size\":10}]}}");
    // `GET /v2/projects?ids=…`：那一份**项目对象**。名字就在这儿 —— 页面顶上那行字、
    // 行上点得动那句话，两处要的都是这一份里的东西。
    //
    // 资源包那一条**声明里带上实例那个版本号**（`1.20.1`，）：chip 那一排是照着
    // 这份声明表摆的，声明里没有它 ⇒ 预选换算不出来 ⇒ 停在「全部」上，而"接线断了"也是
    // 这个形状。倒着摆（接口的规矩是旧的在前）不打紧，`gameVersionChoices()` 自己会翻。
    // 模组那一条**故意只有 1.21.1** —— 它是下面"表里没有那个版本时停在「全部」"的反例。
    api.modrinthProjectsReply =
        QByteArrayLiteral("[{\"id\":\"") + packProject.toUtf8() +
        QByteArrayLiteral("\",\"title\":\"") + packTitle.toUtf8() +
        QByteArrayLiteral("\",\"slug\":\"pack-a\",\"description\":\"一个资源包\","
                          "\"game_versions\":[\"1.21.1\",\"1.20.1\"],\"loaders\":[]},{\"id\":\"") +
        modProject.toUtf8() + QByteArrayLiteral("\",\"title\":\"") + modTitle.toUtf8() +
        QByteArrayLiteral("\",\"slug\":\"mod-a\",\"description\":\"一个模组\","
                          "\"game_versions\":[\"1.21.1\"],\"loaders\":[\"fabric\"]}]");
    // `GET /v2/project/<项目号>/version`：详情页底下那一份版本列表。三条版本、**两个游戏
    // 版本**（一条只支持 1.21.1 —— 它必须在预选到 1.20.1 之后**从列表里消失**）、
    // **两个加载器**（1.20.1 上一条 neoforge 一条 fabric —— 实例报的是 NeoForge，那一条
    // 那一组必须排在前面）。
    api.modrinthVersionListReply = QByteArrayLiteral(
        "[{\"id\":\"VNEO\",\"version_number\":\"9.0.0-neo\",\"date_published\":\"2026-09-01T00:00:00Z\","
        "\"game_versions\":[\"1.20.1\"],\"loaders\":[\"neoforge\"]},"
        "{\"id\":\"VFAB\",\"version_number\":\"9.0.0-fab\",\"date_published\":\"2026-09-01T00:00:00Z\","
        "\"game_versions\":[\"1.20.1\"],\"loaders\":[\"fabric\"]},"
        "{\"id\":\"VOLD\",\"version_number\":\"1.0.0-fab\",\"date_published\":\"2025-01-01T00:00:00Z\","
        "\"game_versions\":[\"1.21.1\"],\"loaders\":[\"fabric\"]}]");
    // CurseForge 那一支也要**答得上话**（每一趟都会发那一发）：空结果、没有未匹配的。
    // 让它 200 而不是 404，是为了这一趟的 `problem` 是空的 —— 名字点得动这件事不该
    // 取决于另一个来源通不通。
    api.curseForgeReply =
        QByteArrayLiteral("{\"data\":{\"exactMatches\":[],\"unmatchedFingerprints\":[]}}");

    TokenStore::clear(TokenStore::microsoftKey());
    TokenStore::clear(TokenStore::minecraftKey());
    TokenStore::clear(TokenStore::accountsKey());
    TokenStore::clear(TokenStore::currentUuidKey());
    const QString accountName = QStringLiteral("T24User");
    seedOfflineRandomSkin(accountName, kSeededSkinUrl, QColor(0x80, 0x60, 0x20));
    QString offlineError;
    check(AccountStore::addOffline(accountName, &offlineError),
          "T24.0 建一个离线账号并设成当前身份（这一节一个外网请求都不发）", offlineError);

    {
        MainWindow w;
        w.show();
        QApplication::processEvents();

        OverlayHost *overlay = w.ui() != nullptr ? w.ui()->overlay() : nullptr;
        check(overlay != nullptr, "T24.1 覆盖层拿得到（下面每一步都挂在它身上）");

        // 页面**开起来之后**才量得了的三件事。整段跑在那个嵌套事件循环里。
        bool dialogFound = false;
        bool packRowFound = false;
        bool unknownRowFound = false;
        bool modRowFound = false;
        bool packClickable = false;
        bool unknownClickable = true;
        bool modClickable = false;
        QString packTip;
        QString unknownTip;
        QString modTip;
        // 实例设置那一页页眉上写的那行字（开起来的时候量一次）。下面"按了返回之后退回到
        // 的是不是**同一页**"拿它比 —— 比"某个内部标志位"结实：那是用户眼睛看到的东西。
        QString settingsHeader;

        QTimer::singleShot(0, [&] {
            OverlayHost *host = currentPageOverlay();
            auto *page = host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
            if (page == nullptr) {
                return;
            }
            dialogFound = true;
            settingsHeader = host->pageTitle();
            InstanceFilesPage *packs = page->filesPage(QStringLiteral("resourcepack"));
            InstanceFilesPage *mods = page->filesPage(QStringLiteral("mod"));
            if (packs == nullptr || mods == nullptr || packs->list() == nullptr) {
                return;
            }
            page->showPage(QStringLiteral("resourcepack"));

            // 反查那一趟在**后台线程**里（读那两个文件算哈希 + 发三发请求），行尾那颗「详情」
            // 要等 `pushInstanceFileIcons()` 摆上去才会从灰的变亮。
            const auto detailForPath = [](InstanceFilesPage *files,
                                          const QString &path) -> QToolButton * {
                for (int row = 0; row < files->list()->count(); ++row) {
                    QListWidgetItem *item = files->list()->item(row);
                    if (item != nullptr &&
                        item->data(InstanceFilesPage::pathRole()).toString() == path) {
                        return files->detailButtonAt(row);
                    }
                }
                return nullptr;
            };
            // 等的是**那一颗从灰的变成能按的** —— 一个用户看得见的终点，不是"某个内部标志位"。
            // （从前等的是名字的手型光标，那条路整条退场了。）
            const bool arrived = pumpUntil(
                [&] {
                    QToolButton *detail = detailForPath(packs, packZip);
                    return detail != nullptr && detail->isEnabled();
                },
                15000);
            check(arrived,
                  "★★ T24.2 那一趟反查跑完之后，「材质包甲.zip」那一行尾巴上的那颗「详情」"
                  "**点得动了**（还灰着 = 身份表没填上，或者钥匙对不上 —— 而屏幕上就是"
                  "「这一条点不动」）",
                  QStringLiteral("requests=%1")
                      .arg(QStringList(api.requests.mid(qMax(0, api.requests.size() - 4)))
                               .join(QStringLiteral(" | "))));

            // ---------- ① 模组那一页：认出来了，名字**也点得动了** ----------
            //
            // 先证明"它是认出来了的"：那一发里带着模组那个项目号。然后量它的名字。
            bool askedModProject = false;
            for (const QString &line : api.requests) {
                if (line.contains(modProject) && line.contains(QStringLiteral("/projects?"))) {
                    askedModProject = true;
                }
            }
            check(askedModProject,
                  "★★ T24.3 前提：模组那一个文件**身份也认出来了**（项目号真的问回来了）"
                  "—— 拿一个压根没认出来的文件去量「名字点得动」是量不出规矩的",
                  modProject);
            page->showPage(QStringLiteral("mod"));
            QToolButton *modDetail = detailForPath(mods, modJar);
            modRowFound = modDetail != nullptr;
            modTip = modDetail != nullptr ? modDetail->toolTip() : QString();
            modClickable = modDetail != nullptr && modDetail->isEnabled();
            // 用户后半句：「对于模组，点进去和资源包差不多…」。以前这一条钉的是
            // **反向**（模组故意不接，用户那句"待会再说"）；"待会"到了 ⇒ 现在和资源包三页
            // 一样必须**点得动**，提示就是「打开…的下载页…」那一句。判据仍然只在
            // `filesNameOpensResource()` 一处。
            check(modRowFound && modClickable &&
                      modTip == InstanceSettingsDialog::filesResourceTip(modTitle),
                  "★★ T24.3 「模组甲」那一行尾巴上的「详情」**点得动**，提示就是「打开…的"
                  "下载页（这一页会关掉）。」那句（用户 2026-09-27：「对于模组，点进去和"
                  "资源包差不多」）",
                  QStringLiteral("tip=%1 能点=%2")
                      .arg(modTip.isEmpty() ? QStringLiteral("(空)") : modTip.left(40))
                      .arg(modClickable ? QStringLiteral("是") : QStringLiteral("不是")));
            // 点它这一下留给第 ⑦ 段：那一段要完整走一趟"点模组 ⇒ 详情页 ⇒ 右上角那一格"
            // （点在这儿的话那一页会当场开出来，把下面资源包那一整段挤进另一层嵌套循环里）。

            // ---------- ② 资源包那一页：两个文件，一个点得动、一个点不动 ----------
            QToolButton *packDetail = detailForPath(packs, packZip);
            QToolButton *unknownDetail = detailForPath(packs, unknownZip);
            packRowFound = packDetail != nullptr;
            unknownRowFound = unknownDetail != nullptr;
            packTip = packDetail != nullptr ? packDetail->toolTip() : QString();
            unknownTip = unknownDetail != nullptr ? unknownDetail->toolTip() : QString();
            packClickable = packDetail != nullptr && packDetail->isEnabled();
            unknownClickable = unknownDetail != nullptr && unknownDetail->isEnabled();
            check(packRowFound && unknownRowFound,
                  "T24.4 前提：资源包那一页上那两个文件都在列表里",
                  QStringLiteral("%1 / %2")
                      .arg(packRowFound ? QStringLiteral("有") : QStringLiteral("没有"),
                           unknownRowFound ? QStringLiteral("有") : QStringLiteral("没有")));
            check(packRowFound && unknownRowFound && packClickable && !unknownClickable,
                  "★★ T24.4 **只有认出来的那一条**那颗「详情」点得动（「没认出来的.zip」接口里"
                  "压根没有它）—— 一个「看着能点、按下去没反应」的按钮比点不动更糟",
                  QStringLiteral("认得出：能点=%1；认不出：能点=%2")
                      .arg(packClickable ? QStringLiteral("是") : QStringLiteral("不是"),
                           unknownClickable ? QStringLiteral("是") : QStringLiteral("不是")));
            // 那颗按钮的悬停提示就是**这一条资源**（认出来了才有）—— 差一个字都说明拿错了
            // 那一行的身份（拿文件名当提示的话，屏幕上看着也对，可就说不清点进去是谁）。
            // 而没认出来的那一条，提示是"这里点不动"那句（**不是**空着：空着的按钮和
            // 灰着的按钮在屏幕上是同一件事，可用户不会去悬停一个灰按钮）。
            check(packTip == InstanceSettingsDialog::filesResourceTip(packTitle) &&
                      !unknownClickable &&
                      !unknownTip.contains(InstanceSettingsDialog::filesResourceTip(packTitle)),
                  "★ T24.4 认出来的那一条那颗「详情」的提示就是**这条资源**那句；"
                  "没认出来的那一条的提示里**没有**那一句（它本来也点不动）",
                  QStringLiteral("认得出：%1 ｜ 认不出：%2").arg(packTip, unknownTip));

            // ---------- ③ 按下去：这一页收起来、那一页开出来 ----------
            //
            // 这一发之后**一个 processEvents 都不许有**：收页那一发是同步的
            // （`dismiss()` 让那个嵌套循环退出），而"开新页"那一发是**排到下一轮**的
            // （见 openInstanceFileResource() 里那段 ★）—— 在这儿泵一下事件，
            // 新页就会在那个嵌套循环还没退出的时候就开，被 `overlay->isOpen()` 吃掉。
            // （ZJ 那一段里同一条规矩的另一种写法。）
            // 按的是**行尾那颗「详情」**（用户：「把点击条目进入对应详情页的
            // 功能变成一个详情按钮…单击改为选中」）。`click()` 走的是 `QToolButton::clicked`
            // —— 这颗按钮接的就是 `emit detailRequested(path)`（和从前名字上那一发同一条路）。
            if (packDetail != nullptr) {
                packDetail->click();
            }

            // 兜底（不是"顺手加的保险"，是**必须**有的）：上面这一下要是没能把这一页
            // 收掉（身份没认出来 / 那一行没找到），`execPage()` 那个嵌套事件循环就永远
            // 不返回 —— 下面那句 `invokeMethod` 会挂死在那儿，整份 mw_check 一行日志都
            // 不出（ZJ 那一段里同一条坑的另一种写法）。这一发让"没做到"退化成一条红的
            // 断言，而不是把测试卡住。
            //
            // 判据必须是**"那一下本来就不会有反应"**，不能是"这一页还开着"：点中的
            // 那一下是**同步**收页的（`dismiss()` 只发 signal，`m_mode` 要等嵌套循环
            // 退完才归 None），所以刚点完这一页**照旧是开着的** —— 按"还开着"进这一支
            // 的话，下面那句 `processEvents()` 会把产品排队的那一发"开新页"提前吃掉
            // （那一刻覆盖层还盖着，`openResourceDetailPage` 开头那条 `isOpen()` 顺手
            // 把这次点击扔掉），于是红的变成"那一页没开出来"，而真凶在这一行。
            if (packDetail == nullptr || !packClickable) {
                if (auto *back = host->findChild<QPushButton *>(
                        OverlayHost::overlayBackButtonName())) {
                    back->click();
                    QApplication::processEvents();
                }
            }
        });

        QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                  Q_ARG(QString, idV));

        check(dialogFound, "T24.1 实例设置那一页开起来了（下面每一条都要读它）");
        check(packRowFound && unknownRowFound && modRowFound,
              "T24.4 三个夹具文件都在各自的页上找到了行");

        // ---------- ④ 那一页真的收起来了 ----------
        //
        // 直接读，**不许泵事件**：那一页是同步收的（`invokeMethod` 已经返回 ⇒ 嵌套循环
        // 退完了 ⇒ 覆盖层那一层已经拆了），而"再开一页"那一发还排在队列里没派 ——
        // 这时候泵一下就等于把它派进下面那个嵌套循环里，卡死在那儿（pumpUntil 是先
        // processEvents 再看判据的，所以"看着只是读一下"的那一句也会踩上）。
        const bool closed = !overlay->isOpen();
        check(closed,
              "★★ T24.5 点完那颗「详情」**这一页自己关掉了** —— 用户要的是「换个地方」，不是就地"
              "多出一个东西（收页那一下走的是 dismiss()：那一页正卡在 execPage 的嵌套事件"
              "循环里，只 closeOverlay() 的话那个循环永远不返回）");

        // ---------- ⑤ 开出来的是**那个资源**的详情页 ----------
        //
        // 这几样必须在**排到下一轮的那一发里面**量：外面泵事件那一句虽然会把产品那
        // 一发"开新页"派下去，可新页是用 `execPage()` 开的 —— 它当场又开一层嵌套事件
        // 循环，从外面泵事件的那一句**再也拿不回控制权**（整份 mw_check 挂在那儿，
        // 一行日志都不出）。所以量这一页的动作自己排一个 0 毫秒定时器：它会在那一层
        // 循环里面跑（产品的定时器排在前面，先进循环、再派到我们这一发），量完顺手点
        // 返回把那一层收掉 —— 这才轮得回下面那几条断言。
        struct DetailProbe {
            bool ran = false;
            bool opened = false;
            bool hasPage = false;
            QString header;
            QString openLabel;
            QString title;
            // 右上角那一格：这一段是从**资源包**那一页点进来的 ⇒ 必须是
            // 「显示快照版」那一档，那颗「模组设置」连画都不该画出来。
            bool slotShown = true;              // modSettingsSlotShown()
            bool modButtonVisible = true;       // 那颗占位按钮真的画着吗
            bool checkboxVisible = false;       // 「显示快照版」那颗真的露着吗
            QString chip;                       // currentChip()
            int chipCount = 0;
            int versionRows = -1;
            int groups = -1;
            QString firstGroupTitle;
        };
        // 补，要求：「上一步中，点击下载界面返回后应该退回到实例设置而
        // 不是实例列表」。**按了返回之后落在哪儿**，只能在**再往下一层**里量 —— 返回
        // 箭头一点，详情页那一层嵌套循环退出，`openFileResourcePage()` 紧接着把实例设置
        // 又开起来（那也是一层嵌套循环），量它只能自己再排一发 0 毫秒定时器：它会在
        // **新那一层循环里**跑。不排这一发的话，外面那一句 `pumpUntil` 永远拿不回控制
        // 权（新那一页没人收），整份 mw_check 卡死在这儿 —— 所以下面这一发里点返回
        // **不是"顺手收个尾"，是这一节能不能跑完的前提**。
        struct ReturnProbe {
            bool ran = false;
            bool opened = false;
            bool hasSettings = false;
            QString header;        // 覆盖层页眉上那行字
            QString instanceName;  // 那一页「实例名」那一格现在写着的（= 目录名）
            // **停在哪一页**。页眉上那行字是「实例设置：<实例名>」，**换页
            // 不换字** —— 光比它只量得出"退回到同一个实例设置"，量不出用户 那句
            // 「从模组点进去，返回也在模组而非概览」。判据只能问这一页自己（唯一出处
            // InstanceSettingsDialog::currentPageKey()）。
            QString pageKey;
        };
        ReturnProbe returned;
        DetailProbe probe;
        QTimer::singleShot(0, [&probe, &returned, overlay, &w] {
            probe.ran = true;
            probe.opened = overlay->pageOpen();
            ResourceDetailPage *detail = w.findChild<ResourceDetailPage *>();
            probe.hasPage = detail != nullptr;
            const QLabel *titleLabel =
                overlay->findChild<QLabel *>(OverlayHost::overlayTitleLabelName());
            probe.header = titleLabel != nullptr ? titleLabel->text() : QString();
            probe.openLabel = detail != nullptr ? detail->openButtonText() : QString();
            probe.title = detail != nullptr ? detail->titleText() : QString();
            if (detail != nullptr) {
                probe.slotShown = detail->modSettingsSlotShown();
                probe.modButtonVisible =
                    detail->modSettingsButton() != nullptr &&
                    detail->modSettingsButton()->isVisible();
                if (QCheckBox *box = detail->findChild<QCheckBox *>(
                        ResourceDetailPage::showSnapshotsCheckBoxName())) {
                    probe.checkboxVisible = box->isVisible();
                }
                // 版本列表那一发（开页前发出去的）答复就在**这一层循环**里落地 —— 等到
                // 底下真有行为止。**等到的东西是什么**留给外面那几条断言说（等不到时
                // versionRows 还是 0，那几条会自己红）。
                pumpUntil([detail] { return detail->versionRowCount() > 0; }, 5000);
                probe.chip = detail->currentChip();
                probe.chipCount = detail->chips().size();
                probe.versionRows = detail->versionRowCount();
                probe.groups = detail->groupCount();
                probe.firstGroupTitle = detail->groupTitleAt(0);
            }
            // 收这一页（= 用户按返回）之后，**紧接着**排下一发去量"退回到哪儿了"。
            // 排在这一发里（而不是点完返回之后在外面排）是因为返回一点下去，这一层循环
            // 马上就要退出、实例设置那一层马上就要起来 —— 在外面排就晚了一轮。
            QTimer::singleShot(0, [&returned, overlay] {
                returned.ran = true;
                returned.opened = overlay->pageOpen();
                const QLabel *label =
                    overlay->findChild<QLabel *>(OverlayHost::overlayTitleLabelName());
                returned.header = label != nullptr ? label->text() : QString();
                // 实例设置那个对话框：**这会儿只剩它一个**了（外面那一份是外面那次调用
                // 的栈上局部，收页时已经析构了），所以 findChild 拿到的就是刚开起来的这个。
                auto *settings = overlay->findChild<InstanceSettingsDialog *>();
                returned.hasSettings = settings != nullptr;
                returned.instanceName = settings != nullptr ? settings->instanceName() : QString();
                returned.pageKey = settings != nullptr ? settings->currentPageKey() : QString();
                // 收页只能走返回箭头（见 OverlayHost::dismiss() 那段）—— 这一发不点，
                // 这一层循环没人收，整份测试就停在这儿了。
                if (auto *back =
                        overlay->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
                    back->click();
                }
            });
            // 收页只能走返回箭头（见 OverlayHost::dismiss() 那段）。
            if (auto *back =
                    overlay->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
                back->click();
            }
        });
        const bool probed = pumpUntil([&probe] { return probe.ran; }, 5000);
        check(probed && probe.opened && probe.hasPage,
              "★★ T24.5 而**那一页真的开出来了**（排到下一轮的那一发落地了）",
              probed ? (probe.opened ? QStringLiteral("开了") : QStringLiteral("没开"))
                     : QStringLiteral("那一发压根没跑"));
        check(probe.header == packTitle,
              "★★ T24.5 页眉上写的就是接口给的那个资源名（不是文件名、不是空）——"
              "「点一条文件的『详情』进它自己的那一页」，这一条是那句话的落点",
              probe.header);
        check(probe.openLabel == QStringLiteral("转到 Modrinth"),
              "★ T24.5 而且来源是 Modrinth（这一条是从哈希那一支认出来的）", probe.openLabel);
        // ⓐ 走的是**下载页里点一行**那条路：头卡上的简介、芯片那一排都跟着这一份 Hit。
        check(probe.title == packTitle,
              "★ T24.5 头卡上的名字也是它（同一个 Hit，不是又查了一次）", probe.title);
        check(!probe.slotShown && probe.checkboxVisible && !probe.modButtonVisible,
              "★★ T24.5 而这一趟点的是**资源包**：右上角照旧是「显示快照版」"
              "（那颗「模组设置」只有「从实例文件页点一个**模组**」才摆 —— 少一样都不摆）",
              QStringLiteral("槽位=%1 勾选框露着=%2 模组设置按钮露着=%3")
                  .arg(probe.slotShown ? QStringLiteral("模组设置") : QStringLiteral("快照版"),
                       probe.checkboxVisible ? QStringLiteral("是") : QStringLiteral("不是"),
                       probe.modButtonVisible ? QStringLiteral("是") : QStringLiteral("不是")));

        // ---------- ⑤b 按当前实例的版本预选 + 把它的加载器排前面----------
        //
        // 要求：「点开某个模组等的时候将版本自动选到当前实例对应的那个，对应的模组加载器
        // 的版本放在前面，资源包等同理，展示为对应版本适用的」。这一趟点的是**资源包**，实例
        // 报的是 1.20.1 / NeoForge；假接口给的版本清单里三条版本、两个游戏版本、两个加载器。
        //
        // 四条判据合起来才是用户那半句话，少一条都还差一截：
        // ① chip 那一排摆的是这个资源**声明过**的那几档（表在这一页上，不是凭空的）；
        // ② 按着的是**实例那个版本**（"自动选到当前实例对应的那个"）；
        // ③ 底下只剩**适用于它**的那些（"展示为对应版本适用的" —— 只支持 1.21.1 的那一条
        // 必须不在了，不然"筛"这件事等于没发生）；
        // ④ 它的加载器那一组**排在最前**（"对应的模组加载器的版本放在前面"）。
        //
        // 备注：`chipCount == 3` 就是【全部 + 1.20.1 + 1.21.1】。写死这个数是有意的 ——
        // 它同时钉住了「声明表里两条版本号 ⇒ 两颗 chip」（不是三条、也不是一颗）。
        check(probe.chipCount == 3, "★★ T24.5 前提：chip 那一排是**这个资源声明过的那几档**"
                                    "（全部 + 声明表里那两个版本号）",
              QStringLiteral("一共 %1 颗").arg(probe.chipCount));
        check(!probe.chip.isEmpty() && probe.chip == idV,
              "★★ T24.5 从某个实例点进来 ⇒ chip **自动选到那个实例的游戏版本**"
              "（用户原话「点开某个模组等的时候将版本自动选到当前实例对应的那个」）",
              QStringLiteral("按着：%1 ｜ 实例报：%2")
                  .arg(probe.chip.isEmpty() ? QStringLiteral("(全部)") : probe.chip, idV));
        check(probe.versionRows == 2 && probe.groups == 2,
              "★★ T24.5 底下剩的**只有适用于那个版本的**（用户原话「展示为对应版本适用的」）"
              "—— 夹具里那条只支持 1.21.1 的必须已经被筛掉，不然「筛」这件事等于没发生",
              QStringLiteral("行数：%1 组数：%2").arg(probe.versionRows).arg(probe.groups));
        check(probe.firstGroupTitle == instanceLoader + QLatin1Char(' ') + idV,
              "★★ T24.5 而**实例那个加载器的那一组排在最前**（用户原话「对应的模组加载器的版本"
              "放在前面」）—— 夹具里同一个游戏版本下 neoforge 排在 fabric **后面**，所以这一条"
              "真红了就说明排序没生效，而屏幕上一点异常都没有",
              QStringLiteral("第一组：%1").arg(probe.firstGroupTitle));

        // ---------- ⑥ 按返回之后**退回到实例设置**（用户 那句）----------
        //
        // 「上一步中，点击下载界面返回后应该退回到实例设置而不是实例列表」—— 上一节里
        // 用户点的是**实例设置里**一条文件的名字，返回就该回到他原来那一页上。不接这一下
        // 的话（或者接成"退回到实例列表"）他手上那一页整个没了。
        //
        // NOTE: 这一发是**上面 probe 里排的**（见那段 ★★）：返回箭头一点下去，详情页
        // 那一层循环退出，`openFileResourcePage()` 紧接着又开一层 —— 量"退回到
        // 哪儿"只能在那一层里面量。
        check(returned.ran && returned.opened && returned.hasSettings,
              "★★ T24.5 按返回之后**实例设置那一页又摆回来了**（不是落在实例列表上）"
              "—— 用户原话「上一步中，点击下载界面返回后应该退回到实例设置而不是实例列表」",
              returned.ran ? QStringLiteral("页开着：%1；设置页在：%2")
                                 .arg(returned.opened ? QStringLiteral("是")
                                                      : QStringLiteral("不是"),
                                      returned.hasSettings ? QStringLiteral("是")
                                                           : QStringLiteral("不是"))
                           : QStringLiteral("那一发压根没跑"));
        check(!returned.header.isEmpty() && returned.header == settingsHeader &&
                  returned.header != packTitle,
              "★★ T24.5 而且摆回来的就是**原来那一页**（页眉上那行字和点「详情」之前逐字相同）"
              "—— 判据取用户眼睛看到的那行字，不取某个内部标志位",
              QStringLiteral("回来：%1 ｜ 原来：%2 ｜ 详情页那个：%3")
                  .arg(returned.header, settingsHeader, packTitle));
        check(returned.instanceName == idV,
              "★ T24.5 而且开的是**同一个实例**（那一页「实例名」那一格写着的目录名）"
              "—— 详情页回来时带的那个 id 就是发起这一下的实例",
              returned.instanceName);
        // 这一条才是用户 那句的**落点**：「从模组点进去，返回也在模组而非概览，
        // 其余同理」。上面那条比的是页眉那行字，而它换页不换字（「实例设置：<实例名>」）
        // —— 落回「概览」也照样通过。停在哪一页只能问那一页自己。
        check(returned.pageKey == QStringLiteral("resourcepack"),
              "★★ T24.5 而且**还停在原来那一页上**（资源包那一页，不是落回「概览」）"
              "—— 用户原话「从模组点进去，返回也在模组而非概览，其余同理」",
              returned.pageKey.isEmpty() ? QStringLiteral("(认不出是哪一页)") : returned.pageKey);

        // ---------- ⑦ 从实例的文件管理页点一个**模组**（用户 后半句）----------
        //
        // 要求：「对于模组，点进去和资源包差不多，但是右上角显示快照版那里替换为
        // 模组设置（先占位），而且和下载页有点区别，不要不小心把下载页改了」。
        //
        // 走法和 ③⑤⑥ 那一段**逐字同一套**（点名字 → 这一页收起来 → 那一页开出来 →
        // 按返回退回实例设置），因为模组和资源包走的就是同一条路。多量的只有**右上角那一
        // 格** —— 那儿的判据是"来路 + 这一条是个模组"两样都要，非走这一趟量不出来。
        //
        // 版本列表那一发在这里**换成空表**：上面资源包那一趟的夹具是这个
        // 项目号的版本，留在那儿会让模组那一页底下摆着"资源包的版本"—— 这一节不比那几行，
        // 可一份摆错门的夹具迟早会骗到某一条断言。
        api.modrinthVersionListReply = QByteArrayLiteral("[]");
        bool modClickableAgain = false;
        {
            QTimer::singleShot(0, [&] {
                OverlayHost *host = currentPageOverlay();
                auto *page = host != nullptr
                                 ? host->findChild<InstanceSettingsDialog *>()
                                 : nullptr;
                if (page == nullptr) {
                    return;
                }
                page->showPage(QStringLiteral("mod"));
                InstanceFilesPage *mods2 = page->filesPage(QStringLiteral("mod"));
                if (mods2 == nullptr || mods2->list() == nullptr) {
                    return;
                }
                const auto modDetailAt = [&]() -> QToolButton * {
                    for (int row = 0; row < mods2->list()->count(); ++row) {
                        QListWidgetItem *item = mods2->list()->item(row);
                        if (item != nullptr &&
                            item->data(InstanceFilesPage::pathRole()).toString() == modJar) {
                            return mods2->detailButtonAt(row);
                        }
                    }
                    return nullptr;
                };
                // 反查那一趟是**会话缓存**（m_instanceFileIdentities），上一段已经问回来了
                // ⇒ 这一趟那颗「详情」多半是同步就亮了的。等一小会儿仍然等的是"那一颗真
                // 点得动"这个用户看得见的终点。
                modClickableAgain = pumpUntil(
                    [&] {
                        QToolButton *detail = modDetailAt();
                        return detail != nullptr && detail->isEnabled();
                    },
                    5000);
                // 按的仍是行尾那颗「详情」（和上面资源包那一趟同一条路，见那一段 ★）。
                if (QToolButton *detail = modDetailAt()) {
                    detail->click();
                }
            });
            QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                      Q_ARG(QString, idV));
        }
        check(modClickableAgain, "★★ T24.7 再从实例那一页打开时，模组那一条那颗「详情」"
                                 "**还是点得动**（身份表是会话缓存，不用再问一趟）");
        check(!overlay->isOpen(),
              "★★ T24.7 点完模组那一行的「详情」，实例设置那一页也**自己收起来了**"
              "（和点资源包那一趟走的是同一条路，见 T24.5）");

        // 这一层和 ⑤⑥ 那一段同构：详情页是 `execPage()` 开的，量它只能自己排一发 0 毫秒
        // 定时器（那一发在它那层嵌套循环里跑），量完点返回把那一层收掉 —— 外面那句
        // `pumpUntil` 才拿得回控制权。
        struct ModDetailProbe {
            bool ran = false;
            bool opened = false;
            bool hasPage = false;
            QString header;
            bool slotShown = false;        // modSettingsSlotShown()
            bool modButtonVisible = false; // 模组设置入口是否可见
            bool modButtonEnabled = false; // 入口应当启用
            QString modButtonText;
            bool checkboxVisible = true;   // 「显示快照版」那一颗露着吗（不该露）
            bool checkboxChecked = true;   // 按了裸 Alt 之后它被拨了吗（不该被拨）
            QString chip;
        };
        struct ModReturnProbe {
            bool ran = false;
            bool opened = false;
            bool hasSettings = false;
            QString header;
            QString instanceName;
            QString pageKey;  // 见 ReturnProbe 里那一段 ★（这一趟该是「mod」）
        };
        ModDetailProbe modProbe;
        ModReturnProbe modReturned;
        QTimer::singleShot(0, [&modProbe, &modReturned, overlay, &w] {
            modProbe.ran = true;
            modProbe.opened = overlay->pageOpen();
            auto *detail = w.findChild<ResourceDetailPage *>();
            modProbe.hasPage = detail != nullptr;
            const QLabel *titleLabel =
                overlay->findChild<QLabel *>(OverlayHost::overlayTitleLabelName());
            modProbe.header = titleLabel != nullptr ? titleLabel->text() : QString();
            if (detail != nullptr) {
                modProbe.chip = detail->currentChip();
                modProbe.slotShown = detail->modSettingsSlotShown();
                if (QPushButton *button = detail->modSettingsButton()) {
                    modProbe.modButtonVisible = button->isVisible();
                    modProbe.modButtonEnabled = button->isEnabled();
                    modProbe.modButtonText = button->text();
                }
                if (QCheckBox *box = detail->findChild<QCheckBox *>(
                        ResourceDetailPage::showSnapshotsCheckBoxName())) {
                    modProbe.checkboxVisible = box->isVisible();
                }
                // 裸 Alt 那一下：那一格上**没有**「显示快照版」可拨，所以按了必须
                // **什么都不发生**（不挡的话用户按一下只会得出"这软件没反应"的结论）。
                QKeyEvent alt(QEvent::KeyPress, Qt::Key_Alt, Qt::NoModifier);
                QApplication::sendEvent(detail, &alt);
                if (QCheckBox *box = detail->findChild<QCheckBox *>(
                        ResourceDetailPage::showSnapshotsCheckBoxName())) {
                    modProbe.checkboxChecked = box->isChecked();
                }
            }
            // 点击模组设置，下一轮检查是否进入同一实例的配置管理。
            QTimer::singleShot(0, [&modReturned, overlay] {
                modReturned.ran = true;
                modReturned.opened = overlay->pageOpen();
                const QLabel *label =
                    overlay->findChild<QLabel *>(OverlayHost::overlayTitleLabelName());
                modReturned.header = label != nullptr ? label->text() : QString();
                auto *settings = overlay->findChild<InstanceSettingsDialog *>();
                modReturned.hasSettings = settings != nullptr;
                modReturned.instanceName =
                    settings != nullptr ? settings->instanceName() : QString();
                modReturned.pageKey = settings != nullptr ? settings->currentPageKey() : QString();
                if (auto *back =
                        overlay->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
                    back->click();
                }
            });
            if (detail != nullptr && detail->modSettingsButton() != nullptr)
                detail->modSettingsButton()->click();
            else overlay->dismiss();
        });
        const bool modProbed = pumpUntil([&modProbe] { return modProbe.ran; }, 5000);
        check(modProbed && modProbe.opened && modProbe.hasPage,
              "★★ T24.7 模组那一行点下去**也开出了那一页**（和资源包那一趟一模一样）",
              modProbed ? (modProbe.opened ? QStringLiteral("开了") : QStringLiteral("没开"))
                        : QStringLiteral("那一发压根没跑"));
        check(modProbe.header == modTitle,
              "★ T24.7 页眉上写的就是接口给的那个资源名（是那个资源，不是那条文件名）",
              modProbe.header);
        // 这一条就是用户那半句话的落点：**同一格换成了「模组设置」**。
        check(modProbe.slotShown && modProbe.modButtonVisible,
              "★★ T24.7 从实例的文件管理页点一个**模组** ⇒ 右上角那一格摆的是「模组设置」"
              "（用户 2026-09-27：「右上角显示快照版那里替换为模组设置」）",
              QStringLiteral("槽位=%1 按钮露着=%2")
                  .arg(modProbe.slotShown ? QStringLiteral("模组设置") : QStringLiteral("快照版"),
                       modProbe.modButtonVisible ? QStringLiteral("是") : QStringLiteral("不是")));
        check(modProbe.modButtonText == ResourceDetailPage::modSettingsText() &&
                  modProbe.modButtonText == QStringLiteral("模组设置"),
              "★★ T24.7 那四个字就是「模组设置」（唯一出处 ResourceDetailPage::modSettingsText()）",
              modProbe.modButtonText);
        check(modProbe.modButtonEnabled,
              "★★ T24.7 模组设置已接通配置文件管理，按钮可用");
        check(modProbe.chip.isEmpty(),
              "★★ T24.7 反例：模组这一条的声明表里**没有**实例那个版本号 ⇒ chip 停在「全部」"
              "（换算不出来时不猜 —— 和用户自己点了一颗表里没有的 chip 是同一个结果）。"
              "同一个页面对象上一趟刚被按到 1.20.1 上，所以这一条同时钉住了「再开一次从「全部」"
              "重来」",
              modProbe.chip.isEmpty() ? QStringLiteral("(全部)") : modProbe.chip);
        check(!modProbe.checkboxVisible,
              "★★ T24.7 摆它的时候「显示快照版」那颗勾选框**整个收起来**了"
              "（「替换」就是同一格只露一个，不是两颗并排）");
        check(!modProbe.checkboxChecked,
              "★★ T24.7 这一档上裸 Alt 那一下**什么都不拨**（那一格上压根没有勾选框可拨 ——"
              "拨一个看不见的勾，用户只会得出「这软件没反应」的结论）");
        check(modReturned.ran && modReturned.opened && modReturned.hasSettings &&
                  modReturned.instanceName == idV,
              "★★ T24.7 点击模组设置进入同一实例的设置，不误开其他实例",
              modReturned.ran ? QStringLiteral("页开着：%1；设置页在：%2；实例：%3")
                                    .arg(modReturned.opened ? QStringLiteral("是")
                                                            : QStringLiteral("不是"),
                                         modReturned.hasSettings ? QStringLiteral("是")
                                                                 : QStringLiteral("不是"),
                                         modReturned.instanceName)
                              : QStringLiteral("那一发压根没跑"));
        check(modReturned.pageKey == QStringLiteral("config"),
              "★★ T24.7 模组设置打开配置文件管理页，而非概览或模组列表",
              modReturned.pageKey.isEmpty() ? QStringLiteral("(认不出是哪一页)")
                                            : modReturned.pageKey);

        if (overlay->isOpen()) {
            if (auto *back =
                    overlay->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
                back->click();
                QApplication::processEvents();
            }
        }
    }

    TokenStore::clear(TokenStore::microsoftKey());
    TokenStore::clear(TokenStore::minecraftKey());
    TokenStore::clear(TokenStore::accountsKey());
    TokenStore::clear(TokenStore::currentUuidKey());
    check(AccountStore::accounts().isEmpty() && AccountStore::activeUuid().isEmpty(),
          "T24.6 这一节加的账号清干净了");

    restoreSettings();
    check(AppPaths::minecraftDir() == originalDir, "T24.6 数据根目录换回来了");
}

