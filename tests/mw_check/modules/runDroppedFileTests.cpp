#include "Support.hpp"
void runDroppedFileTests() {
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "T30.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }

    QSettings sandbox;
    const QVariant foldersBefore = sandbox.value(QStringLiteral("Launcher/Folders"));
    const QVariant currentBefore = sandbox.value(QStringLiteral("Launcher/CurrentFolder"));
    const auto restore = [&] {
        sandbox.setValue(QStringLiteral("Launcher/Folders"), foldersBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), currentBefore);
        sandbox.sync();
        AppPaths::setMinecraftDir(originalDir);
    };
    sandbox.setValue(QStringLiteral("Launcher/Folders"), QStringList({root.path()}));
    sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), root.path());
    sandbox.sync();
    AppPaths::setMinecraftDir(root.path());
    QDir().mkpath(root.path() + QStringLiteral("/libraries"));
    QDir().mkpath(root.path() + QStringLiteral("/assets"));

    const QString idV = QStringLiteral("1.95.1");
    const QDir versionsDir(root.path() + QStringLiteral("/versions"));
    makeInstalledVersion(versionsDir, idV);
    // 「上次启动的是它」——「装到哪个实例」那一问的**预选**就是照这一格算的（下拉框里当前
    // 选中的那一个）。不写这一格的话预选是空的，那一条断言就成了"恒真"。
    rememberLastVersion(idV);
    // 「模组」那一页要**用得上**才会列出来（判据是 instanceModsUsable()：有加载器 **或**
    // mods/ 里有 jar）—— 这里给个 mods/ 目录 + 一个 jar，让那一页真的在。
    const QString modsDir = root.path() + QStringLiteral("/versions/") + idV +
                            QStringLiteral("/mods");
    writeFileAt(modsDir + QStringLiteral("/本来就有的.jar"), "原本就在的那一个");
    // 另外三类各建一个目录，好让"落点对不对"有个确凿的判据（`folderForKind` 那三个名字）。
    const QString instanceDir = root.path() + QStringLiteral("/versions/") + idV;

    // 拖进来的东西：两个模组 + 一个资源包 + 一个认不出来的。
    const QString work = root.path() + QStringLiteral("/drop-src");
    const QString modA = work + QStringLiteral("/第一个模组.jar");
    const QString modB = work + QStringLiteral("/第二个模组.jar");
    const QString respack = work + QStringLiteral("/一个资源包.zip");
    const QString notRes = work + QStringLiteral("/说明.txt");
    writeFileAt(modA, "A");
    writeFileAt(modB, "B");
    // 资源包那一份必须是**真的** zip：认类型要读压缩包的条目表（`pack.mcmeta` + `assets/`，
    // 判据在 `DroppedResource::classify()` 里）。拿一个"改名叫 .zip 的文本文件"来喂，
    // 症状是那一句话里写的是「下面这几个不是能装进实例的东西」—— 红的理由
    // 看着像"产品认不出资源包"，其实是**喂进去的东西本来就不是资源包**。
    // NOTE: `.jar` 那几份不受这条影响：模组是**看后缀**认的（一个 jar 也可能是别的什么，
    // 但凡是 jar 就按模组装，永远不去读它的条目表 —— 见 classify() 里那段）。
    writeFileAt(work + QStringLiteral("/包源/pack.mcmeta"), QByteArrayLiteral("{}"));
    writeFileAt(work + QStringLiteral("/包源/assets/minecraft/lang/zh_cn.json"),
                QByteArrayLiteral("{}"));
    check(ZipUtil::createZip(respack,
                             {{QStringLiteral("pack.mcmeta"),
                               work + QStringLiteral("/包源/pack.mcmeta")},
                              {QStringLiteral("assets/minecraft/lang/zh_cn.json"),
                               work + QStringLiteral("/包源/assets/minecraft/lang/zh_cn.json")}}),
          "T30.0 造出一个**真的**资源包 zip（pack.mcmeta + assets/）");
    writeFileAt(notRes, "这不是能装进实例的东西");
    // 同名冲突那一档：盘上已经有一个 `早就有的.jar`，拖进来的那个和它同名、内容不同。
    const QString sameName = work + QStringLiteral("/本来就有的.jar");
    writeFileAt(sameName, "新拖进来的那一份");

    {
        MainWindow w;
        w.show();
        QApplication::processEvents();

        // ---------- ① 准入：主窗自己收拖放 ----------
        //
        // 这一条**非有不可**：`dropEvent` 会不会被调用，全看这一格。它是 `QWidget` 自己
        // 那一套（事件沿父链往上找第一个 `acceptDrops()` 为真的），漏了它症状是"拖上去
        // 松手什么都没有发生"，不报错、不崩。
        check(w.acceptDrops(),
              "★ T30.1 主窗自己开着接收拖放（漏了这一格，拖上去松手是**什么都不会发生**）");
        const bool mainEnter = dropFilesOn(&w, {modA}, /*enterOnly=*/true);
        check(mainEnter, "T30.1 拖一个本地文件进来：主窗接下这一发（光标是「能放」那个形状）");
        {
            QMimeData mime;
            mime.setText(QStringLiteral("随便一段文字"));
            QDragEnterEvent event(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton,
                                  Qt::NoModifier);
            QApplication::sendEvent(&w, &event);
            check(!event.isAccepted(),
                  "T30.1 拖的是**文字**（没有文件 URL）⇒ 主窗不接（不然会把它当成一条路径）");
        }

        // ---------- ② `droppedFilePaths()`：那一发 MIME 里到底留哪几个 ----------
        //
        // 它是"准入判据"和"真拖了什么"**共用**的那一个（`dragEnterEvent` 判空也是它），
        // 所以只可能有一份实现。这里量的是它挑什么、扔什么。
        {
            const QString dirPath = work;  // 一个真的存在的**目录**
            QMimeData mime;
            mime.setUrls({QUrl::fromLocalFile(modA),
                          QUrl(QStringLiteral("https://example.com/网上的.jar")),
                          QUrl::fromLocalFile(dirPath),
                          QUrl::fromLocalFile(work + QStringLiteral("/根本没有这个文件.jar"))});
            const QStringList got = InstanceSettingsDialog::droppedFilePaths(&mime);
            check(got == QStringList{modA},
                  "★ T30.2 只留下**真的存在的一个本地文件**：网上的链接、目录、盘上没有的"
                  "路径一个都不收",
                  got.join(QLatin1Char(' ')));
        }
        {
            QMimeData mime;
            mime.setText(QStringLiteral("一段文字"));
            check(InstanceSettingsDialog::droppedFilePaths(&mime).isEmpty(),
                  "T30.2 没有 URL 的 MIME ⇒ 空表（= 不接这一发）");
            check(InstanceSettingsDialog::droppedFilePaths(nullptr).isEmpty(),
                  "T30.2 空 MIME ⇒ 空表（不崩）");
        }

        // ---------- ③ 开着实例设置的时候拖进去 ----------
        //
        // `openInstanceSettings()` 卡在自己的嵌套事件循环里，所以每一步都得写在一个
        // `QTimer::singleShot(0, …)` 里（和 T20/T27 同一手法）；收页走顶上的返回箭头
        // （它发 reject），不能用 `closeOverlay()`（那一发不叫醒嵌套循环，整个测试挂死）。
        const auto withPage = [&w, &idV](const QString &pageKey, auto &&act) {
            QTimer::singleShot(0, [&, pageKey] {
                OverlayHost *host = currentPageOverlay();
                auto *page =
                    host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
                if (page != nullptr) {
                    page->showPage(pageKey);
                    QApplication::processEvents();
                    act(page);
                }
                // 那一发兜底（万一哪一次没答上）：判据过期了就当没这一发。
                ++g_modalHandlerGeneration;
                if (host != nullptr) {
                    auto *back =
                        host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName());
                    if (back != nullptr) {
                        back->click();
                        QApplication::processEvents();
                    } else {
                        host->dismiss();
                    }
                }
            });
            QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                      Q_ARG(QString, idV));
            QApplication::processEvents();
        };

        const auto exists = [](const QString &path) { return QFileInfo::exists(path); };
        const auto readText = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        };

        // ---- (a) 模组拖到「模组」那一页：一句话都不问，直接塞进去 ----
        bool droppedA = false;
        bool acceptedA = false;
        QString pageKeyA;
        int pageDropCount = 0;
        withPage(QStringLiteral("mod"), [&](InstanceSettingsDialog *page) {
            pageKeyA = page->currentPageKey();
            // 这一发是**这一页**接的，还是"沿父链往上"落到主窗身上了？接一条自己的线数
            // 一发 —— 这个数是"拖到这一页"这件事本身有没有发生，和落盘成不成无关。
            QObject::connect(page, &InstanceSettingsDialog::filesDropped, page,
                             [&pageDropCount](const QStringList &) { ++pageDropCount; });
            // 先备一发"如果弹了框就点最后一颗（否）并记下来" —— 这一档**不该**弹任何框，
            // 真弹了的话这一发会把它收掉，测试不会挂在嵌套事件循环里；下面照 `g_modalText`
            // 一量就知道弹没弹。
            dismissNextPrompt();
            acceptedA = dropFilesOn(page, {modA});
            droppedA = true;
            QApplication::processEvents();
        });
        check(droppedA && pageKeyA == QStringLiteral("mod"),
              "T30.3 前提：进得去实例设置，而且当前就是「模组」那一页", pageKeyA);
        check(pageDropCount == 1,
              "T30.3b 那一发真的是**这一页**接住的（`filesDropped` 响了 1 次）",
              QString::number(pageDropCount));
        check(acceptedA && exists(instanceDir + QStringLiteral("/mods/第一个模组.jar")),
              "★★ T30.4 模组拖到「模组」那一页 ⇒ **直接塞进 mods/**"
              "（用户点名：「那就直接塞进去」）");
        check(readText(instanceDir + QStringLiteral("/mods/第一个模组.jar")) == QStringLiteral("A"),
              "T30.4 而且落下去的就是拖进来那一份的**内容**（不是建了个空文件）");
        check(g_modalText.isEmpty(),
              "★ T30.4 这一档**一句话都没问**（文件已经在那儿了，再问一句就是白挡一下）",
              g_modalTitle + QStringLiteral(" / ") + g_modalText);

        // ---- (b) 模组拖到「资源包」那一页：弹那一句原话 ----
        bool acceptedB = false;
        withPage(QStringLiteral("resourcepack"), [&](InstanceSettingsDialog *page) {
            answerNextPrompt(0);  // 「继续」
            acceptedB = dropFilesOn(page, {modB});
            QApplication::processEvents();
        });
        check(acceptedB, "T30.5 拖放那一发被接住了（`acceptProposedAction()`）");
        check(g_modalText.contains(QStringLiteral("将在此实例中安装 模组，是否继续")),
              "★★ T30.5 不在对应那一页 ⇒ 弹的就是用户点名那句话（`模组` 是**认出来的**类型，"
              "不是后缀）",
              g_modalText);
        check(g_modalTitle == QStringLiteral("装进这个实例") && g_modalWasWarning == false,
              "T30.5 那句话框的标题是「装进这个实例」（不是报错那一档）", g_modalTitle);
        check(exists(instanceDir + QStringLiteral("/mods/第二个模组.jar")),
              "T30.5 答「继续」⇒ 文件真的进了 mods/");

        // ---- (c) 同一档答「不装」⇒ 盘上什么都不多 ----
        const QString modC = work + QStringLiteral("/第三个模组.jar");
        writeFileAt(modC, "C");
        withPage(QStringLiteral("resourcepack"), [&](InstanceSettingsDialog *page) {
            answerNextPrompt(1);  // 「不装」
            dropFilesOn(page, {modC});
            QApplication::processEvents();
        });
        check(g_modalText.contains(QStringLiteral("将在此实例中安装 模组，是否继续")),
              "T30.6 又弹了同一句（一个文件也是「模组」，不是「1 个模组」）", g_modalText);
        check(!exists(instanceDir + QStringLiteral("/mods/第三个模组.jar")),
              "★★ T30.6 答「不装」⇒ **盘上什么都没多**（用户答了不装，就不许装）");

        // ---- (d) 资源包拖到「光影」那一页 ⇒ 那句话里写的是「资源包」 ----
        withPage(QStringLiteral("shader"), [&](InstanceSettingsDialog *page) {
            answerNextPrompt(0);
            dropFilesOn(page, {respack});
            QApplication::processEvents();
        });
        check(g_modalText.contains(QStringLiteral("将在此实例中安装 资源包，是否继续")),
              "★★ T30.7 换一类，那句话跟着换成「资源包」（类型是**读出来的**，不是写死的）",
              g_modalText);
        check(exists(instanceDir + QStringLiteral("/resourcepacks/一个资源包.zip")),
              "T30.7 落点也是那一类自己的目录（resourcepacks/，不是 mods/）");

        // ---- (e) 认不出来的东西 ⇒ 一句人话，一个字节都不落 ----
        withPage(QStringLiteral("mod"), [&](InstanceSettingsDialog *page) {
            dismissNextPrompt();
            dropFilesOn(page, {notRes});
            QApplication::processEvents();
        });
        check(g_modalTitle.contains(QStringLiteral("认不出来")) &&
                  g_modalText.contains(QStringLiteral("说明.txt")),
              "★ T30.8 认不出来的东西：说一句人话，而且**指名道姓**说是哪一个文件",
              g_modalTitle + QStringLiteral(" / ") + g_modalText);
        check(!exists(instanceDir + QStringLiteral("/mods/说明.txt")) &&
                  !exists(instanceDir + QStringLiteral("/resourcepacks/说明.txt")),
              "T30.8 而且它一个目录都没进去");

        // ---- (f) 同名文件：问一句，答「不盖」⇒ 旧那份一个字节没动 ----
        const QString oldFile = instanceDir + QStringLiteral("/mods/本来就有的.jar");
        const QString beforeOld = readText(oldFile);
        withPage(QStringLiteral("mod"), [&](InstanceSettingsDialog *page) {
            answerNextPrompt(1);  // 「不盖」
            dropFilesOn(page, {sameName});
            QApplication::processEvents();
        });
        check(g_modalTitle.contains(QStringLiteral("已经有一个同名的")) &&
                  g_modalText.contains(QStringLiteral("本来就有的.jar")),
              "★★ T30.9 同名文件**问一句**（不静默盖），而且说的是哪一个名字",
              g_modalTitle + QStringLiteral(" / ") + g_modalText);
        check(readText(oldFile) == beforeOld && beforeOld == QStringLiteral("原本就在的那一个"),
              "★★ T30.9 答「不盖」⇒ 旧那份**一个字节没动**（顺手拖一下就把手改过的那份"
              "盖掉，是这条路最糟的失败方式）");

        // ---- (g) 同名文件答「盖掉」⇒ 真的换上新的 ----
        withPage(QStringLiteral("mod"), [&](InstanceSettingsDialog *page) {
            answerNextPrompt(0);  // 「盖掉」（这一颗才是那一问的第 0 颗）
            dropFilesOn(page, {sameName});
            QApplication::processEvents();
        });
        check(readText(oldFile) == QStringLiteral("新拖进来的那一份"),
              "T30.10 答「盖掉」⇒ 换上的是新拖进来的那一份（写盘走 `QSaveFile`："
              "先写同目录的临时文件、`commit()` 那一下才改名换上去 —— 中途失败旧的还在）",
              readText(oldFile));

        // ---- (g2) 拖进来的正好是**这个实例里本来就有的那一份**（自己拖自己）----
        withPage(QStringLiteral("mod"), [&](InstanceSettingsDialog *page) {
            dismissNextPrompt();
            dropFilesOn(page, {oldFile});
            QApplication::processEvents();
        });
        check(g_modalText.isEmpty(),
              "★★ T30.10b 拖进来的就是那一份本身（同一个路径）⇒ **一句话都不问**"
              "（问「盖掉吗」的话，用户答「盖掉」就等于亲手把自己那份删了）",
              g_modalTitle + QStringLiteral(" / ") + g_modalText);
        check(readText(oldFile) == QStringLiteral("新拖进来的那一份"),
              "★★ T30.10b 而且那一个文件**还在**、内容一个字节没动 —— 「先删旧的再拷」"
              "在这一档上删掉的正是源自己",
              readText(oldFile));

        // ---- (h) 没开实例设置时：问"装到哪个实例" ----
        //
        // 这一档走的是 `handleWindowDrop()`：主窗自己接住（没有哪一页开着，装到哪儿要问）。
        // 选框是 `InlineMessage::pickFromList()` —— 卡片里那张列表就是"哪些实例"。
        const QString modD = work + QStringLiteral("/第四个模组.jar");
        writeFileAt(modD, "D");
        // 先确认这一刻**没有**哪一页开着（不然这一发会被那一页接走，量的就是别的东西）。
        check(currentPageOverlay() == nullptr,
              "T30.11 前提：这一刻没有哪一页开着（这一发才会落到主窗自己身上）");

        // ① 弹出来的框里那张列表 = 盘上现在装着的实例。
        int listCount = -1;
        int listRow = -2;
        QString comboSelection;
        QTimer::singleShot(0, &w, [&] {
            InlineMessageBox *box = currentPrompt();
            if (box == nullptr) {
                ++g_modalHandlerGeneration;
                return;
            }
            recordPrompt(box);
            if (auto *list = box->listWidget()) {
                listCount = list->count();
                listRow = box->listRow();
                // 把选中**清掉**，这一档要量的才是"用户没选"（比如 Ctrl+点一下把那一行
                // 取消掉）。**必须 `setCurrentItem(nullptr)`**：它连当前项一起清；只调
                // `clearSelection()` 不够 —— 当前项还在，`currentRow()` 照样回 0，于是
                // "没选就点确定"变成了"选了第 0 行再点确定"，产品**真的会装进去**，接着
                // 它还要弹一条「装好了」—— 那条没有人在旁边答，测试**挂在**它上面
                // （实测会挂）。
                list->setCurrentItem(nullptr);
            }
            if (QComboBox *combo = w.ui() != nullptr ? w.ui()->getInstalledVersionCombo() : nullptr) {
                comboSelection = combo->currentData().toString();
            }
            // 一行都没选中就点「确定」——「没选」必须是「没选」，不许替用户挑第 0 行。
            clickPromptButton(0);
            ++g_modalHandlerGeneration;
        });
        dropFilesOn(&w, {modD});
        QApplication::processEvents();
        check(listCount >= 1,
              "★ T30.11 没开实例设置时拖进来 ⇒ 弹出「装到哪个实例」那张列表"
              "（= 盘上装着的那些）",
              QString::number(listCount));
        // 预选那一格：**有**预选的话，它必须是下拉框里当前选中的那一个实例（用户多半就想
        // 装给他刚点开的那一份）。下拉框这一刻没选中东西时（沙盒里没有"上次启动"记录）
        // 一行都不选 —— 那也是对的，所以判据写成"要么没选，要么选的就是那一个"。
        check(listRow < 0 || (comboSelection == idV && listRow == 0),
              "★ T30.11 有预选的话，预选的就是下拉框里当前那一个实例",
              QStringLiteral("combo=%1 row=%2").arg(comboSelection).arg(listRow));
        check(!exists(instanceDir + QStringLiteral("/mods/第四个模组.jar")),
              "★★ T30.11 一行都没选中就点「确定」⇒ **什么都没装**（不替用户挑第 0 行）");

        // ② 选中一行再点「确定」⇒ 装进那一个。
        int answered = 0;
        QTimer answerer;
        answerer.setInterval(10);
        QObject::connect(&answerer, &QTimer::timeout, [&] {
            InlineMessageBox *box = currentPrompt();
            if (box == nullptr) {
                return;  // 这会儿没有框开着：下一拍再看
            }
            ++answered;
            if (answered == 1) {
                // 第一条：那张"装到哪个实例"的列表 —— 挑**最后一个**（证明用的是选中的
                // 那一行，而不是第 0 行），再点「确定」。
                if (auto *list = box->listWidget()) {
                    list->setCurrentRow(list->count() - 1);
                }
                clickPromptButton(0);
                return;
            }
            // 第二条：装完之后那句报信（断言读的就是它记下来的标题）。
            recordPrompt(box);
            clickPromptButton(0);
            answerer.stop();
        });
        answerer.start();
        dropFilesOn(&w, {modD});
        QApplication::processEvents();
        answerer.stop();
        check(answered == 2,
              "T30.12 这一档弹的是**两条**：先那张列表，后一条「装好了」（第二条是在第一条"
              "答完之后才弹的 —— 只排一发的写法会挂在这儿）",
              QString::number(answered));
        check(exists(instanceDir + QStringLiteral("/mods/第四个模组.jar")),
              "★★ T30.12 选中一行再点「确定」⇒ 真的装进**那一个**实例（沙盒里只有这一个，"
              "所以落点就是它）");
        check(!g_modalTitle.isEmpty(),
              "T30.12 装完之后还报了信（别让用户对着列表发呆）", g_modalTitle);

        // ---- (i) 收尾：把这一节拖进去的东西清掉，免得影响后面几节对目录的计数 ----
        ++g_modalHandlerGeneration;
        QDir(instanceDir + QStringLiteral("/mods")).removeRecursively();
        QDir(instanceDir + QStringLiteral("/resourcepacks")).removeRecursively();
        QDir(work).removeRecursively();
        check(!exists(instanceDir + QStringLiteral("/mods")) &&
                  !exists(instanceDir + QStringLiteral("/resourcepacks")),
              "T30.13 收尾：这一节拖进去的那几个文件都清掉了");
    }

    restore();
    check(AppPaths::minecraftDir() == originalDir, "T30.14 收尾：数据根目录还原了");
}

