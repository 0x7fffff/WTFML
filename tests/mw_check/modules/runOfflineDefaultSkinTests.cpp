#include "Support.hpp"
void runOfflineDefaultSkinTests() {
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "ZG0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }

    const QString name = QStringLiteral("DefSkin");
    const QString uuid = AccountStore::offlineUuidFor(name);
    const QColor tintModern(0x21, 0x80, 0xC1);
    const QColor tintOld(0x90, 0x60, 0x30);
    const QColor tintRandom(0x7A, 0xC8, 0x66);
    // 第二张随机皮肤：「随机那条路有没有被动过」的刻度（见 ZG1 末尾那一段）。
    const QColor tintRandom2(0xE0, 0x70, 0x20);
    const QString modernId = QStringLiteral("1.20.1-defskin");
    const QString oldId = QStringLiteral("1.18.2-defskin");
    const QString noRuleId = QStringLiteral("1.19.9-noskin");
    const QString brokenId = QStringLiteral("1.7.10-broken");

    // 数据根目录**两头都要改**。AppPaths 那一份是这一节自己算路径用的，而真的说了算的是
    // 盘上那个「当前游戏文件夹」：MainWindow 构造里 applyCurrentFolderToPaths() 会拿它
    // **再改一次** AppPaths。只改 AppPaths 的话窗口一起来就把根目录换回沙箱里记着的那一份
    // （这一节之前那些节各自记过），于是下面造的版本目录、预置的那张缓存全都看不见 ——
    // 表现是下拉框 0 条、那句话一直停在「正在获取…」，而所有断言都会指向错的地方。
    // 写 QSettings 这两下和 P 段那些节是同一个做法。
    QSettings sandboxSettings;
    const QVariant foldersBefore = sandboxSettings.value(QStringLiteral("Launcher/Folders"));
    const QVariant currentBefore = sandboxSettings.value(QStringLiteral("Launcher/CurrentFolder"));
    sandboxSettings.setValue(QStringLiteral("Launcher/Folders"), QStringList({root.path()}));
    sandboxSettings.setValue(QStringLiteral("Launcher/CurrentFolder"), root.path());
    sandboxSettings.sync();

    AppPaths::setMinecraftDir(root.path());
    // 构造 MainWindow 会扫这几样。扫不到只会写日志，但那些话会混进这一节真正要看的
    // 那几行里（ZG6 数的是日志条数）。
    QDir().mkpath(root.path() + QStringLiteral("/libraries"));
    QDir().mkpath(root.path() + QStringLiteral("/assets"));
    const QDir versionsDir(root.path() + QStringLiteral("/versions"));

    const bool madeModern = makeFakeJar(
        versionsDir.filePath(modernId + QLatin1Char('/') + modernId + QStringLiteral(".jar")),
        {{QStringLiteral("assets/minecraft/textures/entity/player/slim/zuri.png"),
          makePngBytes(tintModern)}});
    const bool madeOld = makeFakeJar(
        versionsDir.filePath(oldId + QLatin1Char('/') + oldId + QStringLiteral(".jar")),
        {{QStringLiteral("assets/minecraft/textures/entity/steve.png"), makePngBytes(tintOld)}});
    const bool madeNone = makeFakeJar(
        versionsDir.filePath(noRuleId + QLatin1Char('/') + noRuleId + QStringLiteral(".jar")),
        {{QStringLiteral("README.txt"), QByteArrayLiteral("不是原版客户端")}});
    check(madeModern && madeOld && madeNone, "ZG0 三个假 JAR 造出来了（真 zip、条目名带斜杠）");
    // 第四个故意的：stub 字节，不是一个 zip。
    makeInstalledVersion(versionsDir, brokenId);

    // 随机那张**先预置好**（理由见 seedOfflineRandomSkin 那段）：起窗口那一下还没选中
    // 任何版本 → 走的正是退路。不预置的话这里会真去问 MineSkin。
    seedOfflineRandomSkin(name, kSeededSkinUrl, tintRandom);
    QString offlineError;
    check(AccountStore::addOffline(name, &offlineError),
          "ZG0 建一个离线账号并设成当前身份（只有离线账号会算这个）", offlineError);

    {
        MainWindow w;
        w.show();
        QApplication::processEvents();
        QComboBox *combo = comboByObjectName(&w, MainWindowUI::installedVersionComboName());
        SkinViewer *viewer = byObjectName<SkinViewer>(&w, MainWindowUI::skinViewerName());
        QLabel *hint = byObjectName<QLabel>(&w, MainWindowUI::skinHintLabelName());
        check(combo != nullptr && viewer != nullptr && hint != nullptr,
              "ZG1 版本下拉框和右栏那三样都按 objectName 找得到");
        if (combo != nullptr && viewer != nullptr && hint != nullptr) {
            const int failedBefore =
                countOccurrences(messagesSnapshot(), QStringLiteral("算离线默认皮肤失败"));

            // ---- ZG1 还没选版本：算不出默认皮肤，退回随机那张 ----
            // 起窗口时**一个版本都不该被选中**（fillVersionCombo 不替用户选），所以这一条
            // 同时也是"没有版本 → 不许瞎猜一张"的前提。
            check(combo->currentIndex() < 0,
                  "ZG1 前提：刚起来时一个版本都没选中（停在占位文案那档）",
                  QString::number(combo->currentIndex()));
            check(viewer->skin().pixelColor(0, 0) == tintRandom &&
                      hint->text() == MainWindowUI::skinHintOfflineLoadedText(name),
                  "ZG1 没选中版本 → 画的是随机挑的那张，那句话也还是随机那档", hint->text());

            // 把「随机挑的那一张」换成**第二张**（图本体 + 盘上那一行，和 AF 段同一个做法）。
            // 下面选中版本之后，盘上那行还是第二张、而屏幕上已经是 JAR 里那一张 —— 两件事
            // 同时成立就等于"随机那条路一个字节都没动"（比"看日志里没有失败"强：那句只在
            // 失败时才说话）。**不能把记录删掉** —— 删了就是"还没挑过"，ZG4 那一档会真去
            // 问 MineSkin，这一节就联网了（这一整节的约定是全程离线）。
            seedOfflineRandomSkin(name, kSeededSkinUrl2, tintRandom2);

            // ---- ZG2 新规矩那条在：画的就是 JAR 里那张，而且是窄模型 ----
            combo->setCurrentIndex(combo->findData(modernId));
            QApplication::processEvents();
            const QString shownModern = combo->itemText(combo->currentIndex());
            check(viewer->skin().pixelColor(0, 0) == tintModern && viewer->isSlim(),
                  "ZG2 选中新版 → 画的是假 JAR 里那张，而且模型是**窄**的（下标 8 = slim/zuri）",
                  QStringLiteral("%1 / slim=%2")
                      .arg(viewer->skin().pixelColor(0, 0).name())
                      .arg(viewer->isSlim() ? QStringLiteral("true") : QStringLiteral("false")));
            check(hint->text() == MainWindowUI::skinHintOfflineDefaultSkinText(name, shownModern),
                  "ZG2 那句话改口说「游戏会给的默认皮肤」，而且点名是哪个实例", hint->text());
            check(hint->text() != MainWindowUI::skinHintOfflineLoadedText(name),
                  "ZG2 而且不再说随机那句（两张图长得一样也不许说错话）");
            check(hint->text() != MainWindowUI::skinHintLoadingText(name) &&
                      RandomSkinStore::loadChosenSkinUrl(uuid) == kSeededSkinUrl2,
                  "ZG2 算得出来时随机那条路**整条没走**：没在加载、盘上那行还是上一张",
                  RandomSkinStore::loadChosenSkinUrl(uuid));

            // ---- ZG3 换到只有旧版布局那条的版本：图换了，模型也跟着换 ----
            // 这一条同时验了"换版本会重算"（下拉框 currentIndexChanged → startPlayerSkinLoad）
            // 和"两套规矩算出来的是两张不同的图"—— 少了任一条，屏幕上都不会变。
            combo->setCurrentIndex(combo->findData(oldId));
            QApplication::processEvents();
            const QString shownOld = combo->itemText(combo->currentIndex());
            check(viewer->skin().pixelColor(0, 0) == tintOld && !viewer->isSlim(),
                  "ZG3 换到低版本 → 换成老规矩那张（steve，**宽**模型）",
                  QStringLiteral("%1 / slim=%2")
                      .arg(viewer->skin().pixelColor(0, 0).name())
                      .arg(viewer->isSlim() ? QStringLiteral("true") : QStringLiteral("false")));
            check(hint->text() == MainWindowUI::skinHintOfflineDefaultSkinText(name, shownOld) &&
                      shownOld != shownModern,
                  "ZG3 那句话跟着换成这个实例（两句不一样，所以上面那条不是碰巧对的）",
                  hint->text());

            // ---- ZG4 两条规矩都不在（真机上就是"第三方安装器做的薄壳 JAR"）：退回随机 ----
            combo->setCurrentIndex(combo->findData(noRuleId));
            QApplication::processEvents();
            check(viewer->skin().pixelColor(0, 0) == tintRandom2 &&
                      hint->text() == MainWindowUI::skinHintOfflineLoadedText(name),
                  "ZG4 jar 里两条都没有 → 退回随机那张（**常态**，不报错不弹窗）", hint->text());
            check(RandomSkinStore::loadChosenSkinUrl(uuid) == kSeededSkinUrl2,
                  "ZG4 而且退的是**盘上记着的那一张**（同一个账号不会每次开窗都换脸）",
                  RandomSkinStore::loadChosenSkinUrl(uuid));

            // ---- ZG5 jar 打不开：记一行日志，然后照样退回随机 ----
            combo->setCurrentIndex(combo->findData(brokenId));
            QApplication::processEvents();
            check(countOccurrences(messagesSnapshot(), QStringLiteral("算离线默认皮肤失败")) >
                          failedBefore &&
                      viewer->skin().pixelColor(0, 0) == tintRandom2,
                  "ZG5 jar 不是 zip → 日志里留一行，屏幕上照样有脸（不是空白、不是内置 Steve）");
            // 那一行**不能是个悬空的冒号**：这一档（NotAnArchive）底层不给原因，照抄过来
            // 就是「算离线默认皮肤失败：」后面什么都没有 —— 而那行日志的全部意义正是
            // "怎么老是随机那张"有个交代。
            QString lastFailedLine;
            for (const QString &message : messagesSnapshot()) {
                if (message.contains(QStringLiteral("算离线默认皮肤失败"))) {
                    lastFailedLine = message;
                }
            }
            // 收下来的那条是 qDebug 格式化过的（前面挂 [WTFML]、带空格的文本会被引号包起来），
            // 所以先把这两层剥掉再判 —— 不剥的话"后面什么也没有"也会因为末尾那个引号
            // 而看起来"有内容"（这条断言就永远绿了）。
            QString body = lastFailedLine.trimmed();
            if (body.startsWith(QStringLiteral("[WTFML]"))) {
                body.remove(0, 8);
            }
            body = body.trimmed();
            if (body.size() >= 2 && body.startsWith(QLatin1Char('"')) &&
                body.endsWith(QLatin1Char('"'))) {
                body = body.mid(1, body.size() - 2);
            }
            body = body.trimmed();
            const int colon = body.lastIndexOf(QStringLiteral("："));
            check(colon >= 0 && colon + 1 < body.size() &&
                      body.contains(QStringLiteral("无法打开")),
                  "ZG5 而且那行日志说得出原因（这一档底层不给话，得自己补：说清是 JAR 无法打开）",
                  body);

            // ---- ZG6 算出来那张只落在数据目录的缓存里 ----
            const QString cachePath = DefaultSkin::cachePathFor(
                uuid, modernId,
                QStringLiteral("assets/minecraft/textures/entity/player/slim/zuri.png"));
            check(!cachePath.isEmpty() && QFile::exists(cachePath) &&
                      QFileInfo(cachePath).absolutePath() == AppPaths::skinsDir(),
                  "ZG6 算出来那张落在 <数据目录>/skins 底下（只进缓存，不缝进资源、不写仓库）",
                  cachePath);

            // ---- ZG8 离线账号「换一张…」：手动挑的那张压过官方默认与随机那张 ----
            // 这一节起的是**真的 MainWindow**，所以验的是接线：按钮那边发出来的意图 →
            // MainWindow 落盘 → PlayerSkinController 重走一遍 → 屏幕上那张图 / 那句话。
            const QColor tintManual(0xC0, 0x30, 0x40);
            check(!OfflineSkin::hasSkin(uuid), "ZG8 前提：这个账号还没手动挑过");
            const QString pickedPath = root.filePath(QStringLiteral("picked-skin.png"));
            QImage picked(64, 64, QImage::Format_ARGB32_Premultiplied);
            picked.fill(tintManual);
            check(picked.save(pickedPath, "PNG"), "ZG8 前提：造一张 64×64 的源图", pickedPath);
            const bool pickedOk = QMetaObject::invokeMethod(
                &w, "onOfflineSkinPickRequested", Q_ARG(QString, pickedPath),
                Q_ARG(QString, QStringLiteral("slim")));
            QApplication::processEvents();
            check(pickedOk && OfflineSkin::hasSkin(uuid),
                  "ZG8 「换一张…」那一发落进了 manual-<账号键>.png");
            check(viewer->skin().pixelColor(0, 0) == tintManual && viewer->isSlim(),
                  "ZG8 屏幕上当场换成手动挑的那张（模型也照选的来：窄）",
                  QStringLiteral("%1 / slim=%2")
                      .arg(viewer->skin().pixelColor(0, 0).name())
                      .arg(viewer->isSlim() ? QStringLiteral("true") : QStringLiteral("false")));
            check(hint->text() == MainWindowUI::skinHintOfflineCustomSkinText(name),
                  "ZG8 那句话说的是「你自己换的」，不再说默认/随机那两句", hint->text());

            // 换版本：手动挑的那张**不按实例算**，不许被默认皮肤顶掉。
            combo->setCurrentIndex(combo->findData(modernId));
            QApplication::processEvents();
            check(viewer->skin().pixelColor(0, 0) == tintManual &&
                      hint->text() == MainWindowUI::skinHintOfflineCustomSkinText(name),
                  "ZG8 换实例之后还是手动挑的那张（它压过算出来的官方默认）");

            // 「恢复默认外观」：两张文件一起没，屏幕上回落到这个实例算出来的那张。
            const bool resetOk =
                QMetaObject::invokeMethod(&w, "onOfflineSkinResetRequested");
            QApplication::processEvents();
            check(resetOk && !OfflineSkin::hasSkin(uuid) &&
                      !QFile::exists(OfflineSkin::modelPathFor(uuid)),
                  "ZG8 「恢复默认外观」把图与模型那一行一起删掉");
            check(viewer->skin().pixelColor(0, 0) == tintModern,
                  "ZG8 恢复之后画的是当前实例算出来的官方默认皮肤",
                  viewer->skin().pixelColor(0, 0).name());
            check(hint->text() == MainWindowUI::skinHintOfflineDefaultSkinText(
                                      name, combo->itemText(combo->currentIndex())),
                  "ZG8 那句话也退回「…的默认皮肤」", hint->text());
        }
    }

    check(AccountStore::remove(uuid),
          "ZG7 把临时账号删掉（后面的节不该知道它存在过）");

    // 那两行设置也放回去：它们说的是"当前游戏文件夹"，留着的话下一个起的窗口会指着一个
    // 马上要被删掉的临时目录。
    sandboxSettings.setValue(QStringLiteral("Launcher/Folders"), foldersBefore);
    sandboxSettings.setValue(QStringLiteral("Launcher/CurrentFolder"), currentBefore);
    sandboxSettings.sync();

    AppPaths::setMinecraftDir(originalDir);
    check(AppPaths::minecraftDir() == originalDir, "ZG7 数据根目录换回来了");
}

