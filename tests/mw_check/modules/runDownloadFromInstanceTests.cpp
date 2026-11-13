#include "Support.hpp"
void runDownloadFromInstanceTests() {
    // 那项设置**存在盘上的名字**。字面量在这儿写一遍（`LauncherSettings` 那边那个常量在
    // 匿名命名空间里，外面看不见）—— 这一节要的就是"盘上那个键"，写出来反而更清楚。
    const QString kAutoIntoKey = QStringLiteral("Download/AutoIntoInstance");

    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "T25.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }

    FakeLookupApi api;
    check(api.start(), "T25.0 假接口服务器起来了");

    QSettings sandbox;
    const QVariant foldersBefore = sandbox.value(QStringLiteral("Launcher/Folders"));
    const QVariant currentBefore = sandbox.value(QStringLiteral("Launcher/CurrentFolder"));
    const QVariant sourceBefore = sandbox.value(QStringLiteral("Download/Source"));
    const QVariant modrinthBefore = sandbox.value(QStringLiteral("Launcher/ModrinthApiBase"));
    const QVariant curseForgeBefore = sandbox.value(QStringLiteral("Launcher/CurseForgeApiBase"));
    const QVariant autoIntoBefore = sandbox.value(kAutoIntoKey);
    const auto restoreSettings = [&] {
        sandbox.setValue(QStringLiteral("Launcher/Folders"), foldersBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), currentBefore);
        sandbox.setValue(QStringLiteral("Download/Source"), sourceBefore);
        sandbox.setValue(QStringLiteral("Launcher/ModrinthApiBase"), modrinthBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurseForgeApiBase"), curseForgeBefore);
        sandbox.setValue(kAutoIntoKey, autoIntoBefore);
        sandbox.sync();
        AppPaths::setMinecraftDir(originalDir);
    };
    sandbox.setValue(QStringLiteral("Launcher/Folders"), QStringList({root.path()}));
    sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), root.path());
    sandbox.setValue(QStringLiteral("Download/Source"),
                     LauncherSettings::downloadSourceKey(DownloadSource::Official));
    sandbox.setValue(QStringLiteral("Launcher/ModrinthApiBase"), api.modrinthBase());
    sandbox.setValue(QStringLiteral("Launcher/CurseForgeApiBase"), api.curseForgeBase());
    sandbox.remove(kAutoIntoKey);
    sandbox.sync();
    AppPaths::setMinecraftDir(root.path());
    QDir().mkpath(root.path() + QStringLiteral("/libraries"));
    QDir().mkpath(root.path() + QStringLiteral("/assets"));

    // 这个实例**报得出游戏版本号和一个加载器**：④b 那两条（"筛子自动
    // 填成对应版本和加载器"，要求「点击下载新模组/...后自动在搜索框里填入对应的版本
    // 和加载器」）走的就是这两份值 —— 一个报不出东西的实例量不出来（填出来是空的，和
    // "接线断了"长得一模一样）。
    const QString idV = QStringLiteral("1.96.1");
    const QString instanceLoader = QStringLiteral("Fabric");
    const QDir versionsDir(root.path() + QStringLiteral("/versions"));
    makeInstalledVersion(versionsDir, idV);
    makeVersionMeta(versionsDir, idV,
                    QByteArrayLiteral("{\"loaderName\":\"Fabric\",\"loaderVersion\":\"0.15.11\"}"));
    rememberLastVersion(idV);

    TokenStore::clear(TokenStore::microsoftKey());
    TokenStore::clear(TokenStore::minecraftKey());
    TokenStore::clear(TokenStore::accountsKey());
    TokenStore::clear(TokenStore::currentUuidKey());
    const QString accountName = QStringLiteral("T25User");
    seedOfflineRandomSkin(accountName, kSeededSkinUrl, QColor(0x40, 0x90, 0x60));
    QString offlineError;
    check(AccountStore::addOffline(accountName, &offlineError),
          "T25.0 建一个离线账号并设成当前身份（这一节一个外网请求都不发）", offlineError);

    // ---------- ① 那项设置的默认值（用户要的默认：勾上） ----------
    //
    // 盘上那个键这一节开头**特意删掉了**（`sandbox.remove`），所以读出来的就是"从没配过
    // 的用户"看到的那个值。界面那一层量不了这件事（它按规矩不自己猜默认值），
    // 「默认勾上」是**设置层**的事实，只有在这儿钉得住。
    {
        LauncherSettings settings;
        check(settings.downloadAutoIntoInstance(),
              "T25.1 「下载新模组直接放进当前实例」**默认是开的**（用户要的默认）");
        // 来回一趟：写进去读得回来（这两条真红了的话，上面那条默认值是假绿 ——
        // 一个永远回 true 的读法也能让"默认勾上"看着过）。
        settings.setDownloadAutoIntoInstance(false);
        LauncherSettings reread;
        check(!reread.downloadAutoIntoInstance(), "T25.1 关掉之后读得回来（盘上那个键真的写了）");
        settings.setDownloadAutoIntoInstance(true);
        LauncherSettings reread2;
        check(reread2.downloadAutoIntoInstance(), "T25.1 再打开也读得回来");
    }

    {
        MainWindow w;
        w.show();
        QApplication::processEvents();

        MainWindowUI *ui = w.ui();
        OverlayHost *overlay = ui != nullptr ? ui->overlay() : nullptr;
        QTabWidget *tabs = ui != nullptr ? ui->getTabWidget() : nullptr;
        QListWidget *categories = ui != nullptr ? ui->getDownloadCategoryList() : nullptr;
        check(overlay != nullptr && tabs != nullptr && categories != nullptr,
              "T25.1 覆盖层 / 选项卡 / 左栏分类都拿得到（下面每一条都要读它们）");
        if (overlay == nullptr || tabs == nullptr || categories == nullptr) {
            restoreSettings();
            return;
        }

        // 那一栏的键从**左栏条目自己的 UserRole** 取（行序是界面按分类表摆的，这一层不去
        // 假设"第几个就是哪一个"）——和产品那边认行的做法同一个规矩。
        const QString modKey = QStringLiteral("mod");
        int modRow = -1;
        for (int i = 0; i < categories->count(); ++i) {
            if (categories->item(i)->data(Qt::UserRole).toString() == modKey) {
                modRow = i;
                break;
            }
        }
        check(modRow >= 0, "T25.1 左栏里找得到「模组」那一行");

        // ---------- ② 从实例设置里点「下载新模组」 ----------
        bool openedPage = false;
        bool clickedDownload = false;
        QString settingsHeader;
        // 开页**不阻塞**（`OverlayHost::showPage()`）：点完 `invokeMethod` 这一页就已经在
        // 屏幕上了，所以量它的那一段是当场跑的普通函数体（不再排 0 毫秒定时器等循环）。
        const auto openModDownload = [&] {
            OverlayHost *host = currentPageOverlay();
            auto *page =
                host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
            if (page == nullptr) {
                return;
            }
            openedPage = true;
            settingsHeader = host->pageTitle();
            InstanceFilesPage *mods = page->filesPage(modKey);
            if (mods == nullptr || mods->downloadButton() == nullptr) {
                return;
            }
            clickedDownload = true;
            // 这一下**同步**就把那一页收掉了（`openDownloadCategory` 的第一件事就是
            // `overlay()->dismiss()`），所以点完这一行这个函数体就继续往下走 ——
            // 后面**不许**再补一次"收页"（会撞在已经收干净的那一层上）。
            mods->downloadButton()->click();
        };
        QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                  Q_ARG(QString, idV));
        openModDownload();
        // 收起来那一份对话框是 `deleteLater()` 收的：让它先落地，免得后面再开时
        // `findChild<InstanceSettingsDialog *>()` 摸到这一份已经收起来的。
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(openedPage && clickedDownload,
              "T25.2 实例设置开起来了、模组那一页顶上那颗「下载新模组」也在");
        check(settingsHeader.isEmpty() == false, "T25.2 记下了那一页页眉上写的字（最后要比它）");

        // ---------- ③ 落到下载页的模组那一栏，而且返回键露出来了 ----------
        //
        // 摆选项卡那一发是**排到下一轮**的（见 openDownloadCategory 里那段：这会儿还卡在
        // 那个嵌套循环的调用栈里），所以这里必须泵一次事件循环。
        for (int i = 0; i < 20 && tabs->currentIndex() != MainWindowUI::downloadTabIndex(); ++i) {
            QApplication::processEvents();
        }
        check(tabs->currentIndex() == MainWindowUI::downloadTabIndex(),
              "T25.3 按完那颗键，选项卡翻到**下载页**了",
              QString::number(tabs->currentIndex()));
        check(modRow >= 0 && categories->currentRow() == modRow,
              "T25.3 而且左栏选中的就是「模组」那一栏（不是停在「游戏」上）",
              QString::number(categories->currentRow()));
        check(!overlay->isOpen(),
              "T25.3 实例设置那一页自己收起来了（屏幕上只剩下载页）");

        QPushButton *back = ui->getDownloadBackButton();
        check(back != nullptr && !back->isHidden(),
              "T25.3 左上角那颗「← 返回实例设置」露出来了",
              back == nullptr ? QStringLiteral("<没有这颗键>") : QString());
        {
            // 它**只有一颗**（整个下载页共用一颗、摆在左栏顶上）。
            // NOTE: 别的栏压根没有第二颗可按 —— 共用这一颗。
            QWidget *downloadPage = tabs->widget(MainWindowUI::downloadTabIndex());
            const int backCount =
                downloadPage == nullptr
                    ? -1
                    : downloadPage
                          ->findChildren<QPushButton *>(MainWindowUI::downloadBackButtonName())
                          .size();
            check(backCount == 1,
                  "T25.3 而且整个下载页**只有这一颗**（它不再按分类复制）",
                  QStringLiteral("找到 %1 颗").arg(backCount));
        }

        // ---------- ④ 「已安装版本」得选到**那个实例**上 ----------
        //
        // 用户那句「下载新模组就自动放进当前实例文件夹」靠的就是这一格：下来的东西落进
        // "当前选中的实例"。不替用户选上的话，屏幕上没有任何东西说过"东西下进了另一个实例"。
        QComboBox *installed = ui->getInstalledVersionCombo();
        check(installed != nullptr && installed->currentData().toString() == idV,
              "T25.4 「已安装版本」选到的是**那个实例**（下来的东西落进它）",
              installed == nullptr ? QStringLiteral("<没有这一格>")
                                   : installed->currentData().toString());

        // ---------- ④b 筛子也替用户填成那个实例的版本和加载器 ----------
        //
        // 要求：「点击下载新模组/...后自动在搜索框里填入对应的版本和加载器」。
        // 这一条和 ④ 是**两件事**：④ 管"下来的东西落在哪个实例"，这一条管"搜出来的是不是
        // 那个实例能用的东西"。
        //
        // 两格都**按文字**量（版本那一格是可输入的、加载器那一格两个来源的键不是一套 ——
        // 见 fillDownloadFiltersFromInstance 那段 ★）：填的是不是用户眼睛里那个词。
        QComboBox *filterVersion = ui->getDownloadFilterVersionCombo(modKey);
        QComboBox *filterLoader = ui->getDownloadFilterLoaderCombo(modKey);
        check(filterVersion != nullptr && filterVersion->currentText() == idV,
              "★★ T25.4 版本那一格自动填成了**那个实例的游戏版本**"
              "（用户原话「自动在搜索框里填入对应的版本和加载器」）",
              filterVersion == nullptr ? QStringLiteral("<没有这一格>")
                                       : filterVersion->currentText());
        check(filterLoader != nullptr && filterLoader->currentText() == instanceLoader,
              "★★ T25.4 模组加载器那一格填成了**那个实例的加载器**（按文字认，不按 data ——"
              "两个来源的键不是一套：Modrinth 是词、CurseForge 是数字）",
              filterLoader == nullptr ? QStringLiteral("<没有这一格>")
                                      : filterLoader->currentText());

        // ---------- ⑤ 点那颗返回键：回到**原来那一页** ----------
        //
        // `openInstanceSettings()` 会**卡在嵌套事件循环里**，所以"量那一页"只能自己排一发
        // 0 毫秒定时器（那一发在它那层循环里跑），量完点顶上那颗返回箭头收页 —— 外面这一句
        // `click()` 才拿得回控制权。
        QString returnedHeader;
        // 页眉那行字（「实例设置」）**不随页走**，所以"退回的还是模组那一页、
        // 不是落回「概览」"这件事页眉量不出来 —— 得问 `currentPageKey()`（唯一出处就是
        // 它，见 InstanceSettingsDialog.hpp 那段 ★）。
        QString returnedPageKey;
        bool returnedPage = false;
        QTimer::singleShot(0, [&] {
            OverlayHost *host = currentPageOverlay();
            auto *page =
                host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
            if (page == nullptr) {
                return;
            }
            returnedPage = true;
            returnedHeader = host->pageTitle();
            returnedPageKey = page->currentPageKey();
            if (auto *arrow =
                    host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
                arrow->click();
                QApplication::processEvents();
            } else {
                host->dismiss();
            }
        });
        if (back != nullptr) {
            back->click();  // 这一下同步进嵌套循环，上面那发定时器在里面跑完才返回
        }
        QApplication::processEvents();
        check(returnedPage, "T25.5 点返回键之后，实例设置那一页**又开起来了**");
        check(!returnedHeader.isEmpty() && returnedHeader == settingsHeader,
              "T25.5 而且退回的是**同一页**（页眉那行字一模一样）",
              returnedHeader + QStringLiteral(" vs ") + settingsHeader);
        check(returnedPageKey == QStringLiteral("mod"),
              "★★ T25.5 且落点还是**模组那一页**（页眉认不出这个 —— 它不随页变，所以这条"
              "单独量 `currentPageKey()`；用户原话「返回也在模组而非概览」）",
              returnedPageKey.isEmpty() ? QStringLiteral("(认不出是哪一页)") : returnedPageKey);
        check(back != nullptr && back->isHidden(),
              "T25.5 回来之后那颗返回键也收起来了（不然下次进下载页它莫名还露着）");

        // ---------- ⑥ 再走一遍：这一次点**顶上那颗「下载」** ----------
        //
        // 要求：「在这个情况下点击顶部下载按钮就刷新页面回到主页」——"这个
        // 情况"指的就是上一步那种（从实例设置里点「下载新模组」进来的），而"主页"是下载页
        // 的首页（「游戏」那一栏，见 downloadCategoryGameIndex）。
        //
        // 为什么非要**再走一遍**：上面 T25.5 那一颗返回键把"从实例进来"这件事整个消费掉了
        // （`returnToInstanceSettingsFromDownload` 把两位清了、键也收了，见那段 ★）——
        // 状态已经不在，这时候点顶上那颗什么都不会发生，量出来的是个假绿。
        bool reopened = false;
        bool clickedAgain = false;
        // 和 ② 同一套：开页不阻塞，点完 `invokeMethod` 当场量。
        const auto reopenAndDownload = [&] {
            OverlayHost *host = currentPageOverlay();
            auto *page =
                host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
            if (page == nullptr) {
                return;
            }
            reopened = true;
            InstanceFilesPage *mods = page->filesPage(modKey);
            if (mods == nullptr || mods->downloadButton() == nullptr) {
                return;
            }
            clickedAgain = true;
            mods->downloadButton()->click();
        };
        QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                  Q_ARG(QString, idV));
        reopenAndDownload();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(reopened && clickedAgain,
              "T25.6 前提：又走了一遍「进实例设置 → 点下载新模组」（上一趟那两位已经被返回键"
              "消费掉了，不复现一遍就是拿空状态量这一条）");
        for (int i = 0; i < 20 && tabs->currentIndex() != MainWindowUI::downloadTabIndex(); ++i) {
            QApplication::processEvents();
        }
        check(tabs->currentIndex() == MainWindowUI::downloadTabIndex() &&
                  categories->currentRow() == modRow && !overlay->isOpen() &&
                  back != nullptr && !back->isHidden(),
              "T25.6 前提：又落到下载页的模组那一栏、返回键又露出来了");

        // 点的是**真的那一颗导航**（`TitleBar::navButton`），不是直接发信号：这一条要量的
        // 就是"那颗按钮到底接上了没有"，绕开发射源去调槽函数等于把被验的那一段自己挖掉。
        QAbstractButton *navDownload =
            ui->titleBar() != nullptr ? ui->titleBar()->navButton(MainWindowUI::downloadTabIndex())
                                      : nullptr;
        check(navDownload != nullptr, "T25.6 顶上那颗「下载」找得到");
        // 顶上那颗「实例」（T25.7 用）：那一节量的是"从下载页按它 = 按了返回"。
        QAbstractButton *navInstance =
            ui->titleBar() != nullptr ? ui->titleBar()->navButton(MainWindowUI::instancesTabIndex())
                                      : nullptr;
        if (navDownload != nullptr) {
            navDownload->click();  // 同步：下面几条全部**不泵事件**直接读
        }
        // 这几条读的是"点完的那一刻"，**一个 processEvents 都不许有**：清了筛子那一发里
        // 还会把那一页的搜索结果扔掉，泵一下就可能把下一发（下一次进那一页才发的搜索）
        // 提前派下去，读到的就不是"刚点完"的样子了。
        check(back != nullptr && back->isHidden(),
              "★★ T25.6 点完顶上那颗「下载」，那颗「← 返回实例设置」**收起来了**"
              "（这件事已经整个收场：再点它已经没有「来路」可回）");
        check(categories->currentRow() == MainWindowUI::downloadCategoryGameIndex(),
              "★★ T25.6 左栏回到**第一栏**（那才是下载页的首页 —— 在线版本清单那一页）",
              QStringLiteral("现在在第 %1 行，首页是第 %2 行")
                  .arg(categories->currentRow())
                  .arg(MainWindowUI::downloadCategoryGameIndex()));
        check(filterVersion != nullptr && filterVersion->currentText().isEmpty(),
              "★★ T25.6 版本那一格**还回去了**（那是我们自己灌进去的，不是用户填的）",
              filterVersion == nullptr ? QStringLiteral("<没有这一格>")
                                       : filterVersion->currentText());
        check(filterLoader != nullptr && filterLoader->currentIndex() == 0 &&
                  filterLoader->currentData().toString().isEmpty(),
              "★★ T25.6 加载器那一格回到「全部」（第一项，键是空串 = 不筛）",
              filterLoader == nullptr
                  ? QStringLiteral("<没有这一格>")
                  : QStringLiteral("第 %1 项「%2」")
                        .arg(filterLoader->currentIndex())
                        .arg(filterLoader->currentText()));
        QTreeWidget *modTree = ui->getDownloadResourceTree(modKey);
        check(modTree != nullptr && modTree->topLevelItemCount() == 0,
              "★★ T25.6 而且那一栏的搜索结果**扔掉了**（筛子是新条件、树上那批是按旧条件搜"
              "出来的，两下里对不上 —— 下一次进那一页时自然会按新条件重搜一次）",
              modTree == nullptr ? QStringLiteral("<没有这棵树>")
                                 : QStringLiteral("还剩 %1 行").arg(modTree->topLevelItemCount()));

        // 下面 ⑦ ⑧ 两节都要"再走一遍那个来路"（上一趟那两位已经被消费掉了，不复现就是拿
        // 空状态量），所以这一段提成一个 lambda：开实例设置 → 点「下载新模组」→ 落到下载页。
        // 判据 = 那一页真开出来过 **且** 那颗按钮真按下去了（不然下面量的是个空转）。
        const auto reopenInstanceToDownload = [&] {
            bool opened = false;
            bool clicked = false;
            QTimer::singleShot(0, [&] {
                OverlayHost *host = currentPageOverlay();
                auto *page =
                    host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
                if (page == nullptr) {
                    return;
                }
                opened = true;
                InstanceFilesPage *mods = page->filesPage(modKey);
                if (mods == nullptr || mods->downloadButton() == nullptr) {
                    return;
                }
                clicked = true;
                mods->downloadButton()->click();
            });
            QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                      Q_ARG(QString, idV));
            for (int i = 0;
                 i < 20 && tabs->currentIndex() != MainWindowUI::downloadTabIndex(); ++i) {
                QApplication::processEvents();
            }
            return opened && clicked;
        };

        // ---------- ⑦ 顶上那颗「实例」：第一下 = 返回，第二下 = 回实例主页 ----------
        //
        // 要求：「如果点击了下载新模组按钮后再点击实例按钮，功能就和返回
        // 一样，如果点两下那就是先返回再回到实例主页」。
        bool reopenedAgain = reopenInstanceToDownload();
        check(reopenedAgain &&
                  tabs->currentIndex() == MainWindowUI::downloadTabIndex() && back != nullptr &&
                  !back->isHidden(),
              "T25.7 前提：再走一遍「进实例设置 → 点下载新模组」（落到下载页、返回键露着）");
        // 数一下**那一刻**当前是哪一颗选项卡：下面那条判据读的是"刚才那一颗"，它得先是
        // 下载页（`m_previousMainTabIndex` 记的是上一位，见 MainWindow.hpp 那段）。
        check(tabs->currentIndex() == MainWindowUI::downloadTabIndex() && navInstance != nullptr,
              "T25.7 前提：此刻停在下载页上（那颗「实例」是从这儿按下去的）");

        // 两下都在**同一次信号里**做完：第一下把页开出来（非阻塞，`showPage()`），那一发
        // 定时器记下"开出来的那一页长什么样"，**再按第二下**把这一页收掉 —— 两次读的都是
        // 真状态，中间没有假手。
        bool navOpened = false;
        bool navSecondClicked = false;
        QString navPageKey;
        QTimer::singleShot(0, [&] {
            OverlayHost *host = currentPageOverlay();
            auto *page =
                host != nullptr ? host->findChild<InstanceSettingsDialog *>() : nullptr;
            if (page == nullptr) {
                return;
            }
            navOpened = true;
            navPageKey = page->currentPageKey();
            if (navInstance != nullptr) {
                navInstance->click();
                navSecondClicked = true;
            }
        });
        if (navInstance != nullptr) {
            navInstance->click();  // 第一下：这一下会卡在嵌套事件循环里，上面那发定时器在里面跑
        }
        QApplication::processEvents();
        check(navOpened,
              "★★ T25.7 按完「实例」那一颗，实例设置那一页**开起来了**（和按那颗返回键一样）"
              "—— 用户原话「点击了下载新模组按钮后再点击实例按钮，功能就和返回一样」");
        check(navPageKey == QStringLiteral("mod"),
              "★★ T25.7 而且开在**模组那一页**（「返回」回到哪儿，这一颗就回到哪儿）",
              navPageKey.isEmpty() ? QStringLiteral("(认不出是哪一页)") : navPageKey);
        check(back != nullptr && back->isHidden(),
              "★★ T25.7 那条路走完，返回键也收起来了（`m_downloadReturn` 那一份取干净）");
        check(navSecondClicked && !overlay->isOpen(),
              "★★ T25.7 第二下「实例」= **回到实例主页**：上面那一页收掉了、露出底下的实例列表"
              "（用户原话「如果点两下那就是先返回再回到实例主页」）",
              navSecondClicked ? (overlay->isOpen() ? QStringLiteral("那一页还开着")
                                                    : QStringLiteral("收掉了"))
                               : QStringLiteral("第二下压根没按下去"));
        check(tabs->currentIndex() == MainWindowUI::instancesTabIndex(),
              "T25.7 而且停的是「实例」那一颗（不是被弹回下载页）",
              QStringLiteral("停在第 %1 颗").arg(tabs->currentIndex()));

        // ---------- ⑧ 逛一圈再回来：「下载」那颗**不许**把"从实例进来"顺手清掉 ----------
        //
        // 这一段量的是 `onNavigationApplied()` 里那条判据 ② 的**原话**：「"从实例进来、切去主页
        // 逛了一圈、再点下载"……那一下用户要的是**切回来**（原来那一栏、连筛子带实例都在），
        // 顺手清掉就成了"我明明没点返回，回实例设置那颗键却没了"」。
        check(reopenInstanceToDownload() && categories->currentRow() == modRow,
              "T25.8 前提：再走一遍来路（落到下载页的模组那一栏）",
              QStringLiteral("现在在第 %1 行").arg(categories->currentRow()));
        QAbstractButton *navHome =
            ui->titleBar() != nullptr ? ui->titleBar()->navButton(MainWindowUI::homeTabIndex())
                                      : nullptr;
        check(navHome != nullptr, "T25.8 前提：顶上那颗「主页」找得到");
        if (navHome != nullptr) {
            navHome->click();  // 切去主页逛一圈（那一下不该动下载页上的任何一位）
            QApplication::processEvents();
        }
        check(tabs->currentIndex() == MainWindowUI::homeTabIndex(),
              "T25.8 前提：真的切到主页去了");
        if (navDownload != nullptr) {
            navDownload->click();  // 再点「下载」切回来 —— 同步读，不泵事件
        }
        check(tabs->currentIndex() == MainWindowUI::downloadTabIndex() &&
                  back != nullptr && !back->isHidden(),
              "★★ T25.8 从别处点「下载」切回来，那颗「← 返回实例设置」**还在**"
              "（这一按是「切回来」、不是「回主页」—— 用户没点返回，来路不该被顺手清掉）");
        check(categories->currentRow() == modRow,
              "★★ T25.8 而且左栏还在**原来那一栏**上（不是被弹回「游戏」那一页）",
              QStringLiteral("现在在第 %1 行，刚才在第 %2 行")
                  .arg(categories->currentRow())
                  .arg(modRow));

        // ---------- ⑨ 这一层已经被别的页面占着时，「实例」那一颗**不许**把来路吃掉 ----------
        //
        // 场景：用户从实例进来 → 在下载页点开一个资源的详情页 → 又按顶上「实例」。
        // `openInstanceSettings()` 撞上"占着"时**不拒绝**（静默收掉那一页、
        // 排到下一轮再开，见 `runWhenOverlayFree()`）。这一条断言量的是**这一按算不算
        // "回实例设置"**（占着的时候按「实例」就照常翻页、来路留给上面那一发去用）。
        //
        // 这里拿**一张提示**当"这一层被占着"的最小样本（真详情页要一整套假资源清单，
        // 那是 ZJ 那一节的活）；判据是同一句 `overlay->isOpen()`。
        check(back != nullptr && !back->isHidden() && categories->currentRow() == modRow,
              "T25.9 前提：还在「从实例进来」那一趟上（返回键露着、停在模组那一栏）");
        if (overlay != nullptr) {
            overlay->openPrompt(false);
            QApplication::processEvents();
        }
        check(overlay != nullptr && overlay->isOpen(),
              "T25.9 前提：这一层被占着了（一张提示开在下载页上）");
        if (navInstance != nullptr) {
            navInstance->click();  // 同步读
        }
        check(back != nullptr && !back->isHidden(),
              "★★ T25.9 这一层有人占着、那一页开不出来 ⇒ **来路一位都不许动**"
              "（返回键照旧露着 —— 吃掉了就再也回不去实例设置了）");
        check(navInstance != nullptr && tabs->currentIndex() == MainWindowUI::instancesTabIndex(),
              "★★ T25.9 这一按就照常翻页（和按「主页」「设置」一样），不是弹一句"
              "「先关掉现在这一页」",
              QStringLiteral("停在第 %1 颗").arg(tabs->currentIndex()));
        if (tabs->currentIndex() != MainWindowUI::downloadTabIndex() && navDownload != nullptr) {
            navDownload->click();  // 切回下载页，把下面那条清理做完
            QApplication::processEvents();
        }
        if (overlay != nullptr && overlay->isOpen()) {
            // 提示那一档要的是 `closeOverlay()`：`dismiss()` 在提示上**只发一个
            // `promptDismissed()`**（那一发是给 `execEmbedded` 那层嵌套循环用的），而这一张
            // 是我们自己拿 `openPrompt()` 摆的，没人接它 —— 收不掉。反过来，页面那一档
            // **只能**走 `dismiss()`（见 OverlayHost::dismiss() 里那段 ★）。
            overlay->closeOverlay();
            QApplication::processEvents();
        }
        check(overlay != nullptr && !overlay->isOpen(), "T25.9 收干净了（提示关掉了）");

        // ---------- ⑩ 「返回」把来路整个还回去：哪颗根页 / 哪一页 / 滚到哪儿 ----------
        //
        // 这一节钉的是「逐根页迁移明确路由栈（返回恢复进入前状态）」。三样东西以前散在
        // 三个成员上（`m_downloadFromInstanceId` / `m_downloadFromPageKey` / 那一份
        // `FileScrollReturn`），而且"回来归哪颗根页"是拿**当下**那一颗现算的 —— 从主页
        // 快捷入口进去的那一趟，回来会落在「实例」上。现在是一条 `NavigationReturn`：
        // 进的时候三样一次记下，回来时原样摆回去。
        const QString modsDir = AppPaths::versionDir(idV) + QStringLiteral("/mods");
        QDir().mkpath(modsDir);
        int madeFiles = 0;
        for (int i = 0; i < 40; ++i) {
            QFile file(modsDir +
                       QStringLiteral("/mod-%1.jar").arg(i, 2, 10, QLatin1Char('0')));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QByteArrayLiteral("not a real jar"));
                file.close();
                ++madeFiles;
            }
        }
        check(madeFiles == 40, "T25.10 前提：造够四十条（不够一屏就量不出\"跳回顶端\"）",
              QStringLiteral("造了 %1 条").arg(madeFiles));

        // 来路先摆成「主页」：下面回来时它得落回主页，而不是被"当下这一颗（下载页）"
        // 现算成「实例」。
        if (navHome != nullptr) {
            navHome->click();
            QApplication::processEvents();
        }
        check(tabs->currentIndex() == MainWindowUI::homeTabIndex(),
              "T25.10 前提：此刻停在主页上（这一趟的来路归主页）");
        QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                  Q_ARG(QString, idV));
        OverlayHost *homeHost = currentPageOverlay();
        auto *homeDialog =
            homeHost != nullptr ? homeHost->findChild<InstanceSettingsDialog *>() : nullptr;
        check(homeDialog != nullptr, "T25.10 前提：从主页开出了实例设置");
        if (homeDialog != nullptr) {
            homeDialog->showPage(modKey);
        }
        InstanceFilesPage *homeFiles =
            homeDialog != nullptr ? homeDialog->filesPage(modKey) : nullptr;
        QListWidget *homeList = homeFiles != nullptr ? homeFiles->list() : nullptr;
        QScrollBar *homeBar = homeList != nullptr ? homeList->verticalScrollBar() : nullptr;
        for (int i = 0; i < 20 && (homeList == nullptr || homeList->count() != 40); ++i) {
            QApplication::processEvents();
        }
        check(homeList != nullptr && homeList->count() == 40 && homeBar != nullptr &&
                  homeBar->maximum() > 0,
              "T25.10 前提：模组那一页摆出了四十条、真的滚得动",
              homeList == nullptr ? QStringLiteral("<没有这一页>")
                                  : QStringLiteral("%1 条，滚动条 0..%2")
                                        .arg(homeList->count())
                                        .arg(homeBar != nullptr ? homeBar->maximum() : -1));
        InstanceFilesPage::ScrollPosition leftAt;
        if (homeBar != nullptr && homeBar->maximum() > 0) {
            homeBar->setValue(homeBar->maximum() / 2);
            QApplication::processEvents();
            if (homeFiles != nullptr) {
                leftAt = homeFiles->scrollPosition();
            }
        }
        check(!leftAt.identity.isEmpty() && homeBar != nullptr && homeBar->value() > 0,
              "★★ T25.10 前提：离开那一刻量得出一份位置（顶上那一行是谁、滚到中间了）",
              QStringLiteral("顶上 %1（value=%2）")
                  .arg(leftAt.identity)
                  .arg(homeBar != nullptr ? homeBar->value() : -1));

        QAbstractButton *homeDownload =
            homeFiles != nullptr ? homeFiles->downloadButton() : nullptr;
        check(homeDownload != nullptr, "T25.10 前提：模组那一页顶上那颗「下载新模组」在");
        if (homeDownload != nullptr) {
            homeDownload->click();  // 同步收页 + 记下来路；摆选项卡那一发排在下一轮
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (int i = 0; i < 20 && tabs->currentIndex() != MainWindowUI::downloadTabIndex(); ++i) {
            QApplication::processEvents();
        }
        check(tabs->currentIndex() == MainWindowUI::downloadTabIndex() && back != nullptr &&
                  !back->isHidden(),
              "T25.10 前提：落到下载页、返回键露着");

        // 按那颗返回键：来路整个摆回去（非阻塞 —— 点完这一页就已经在屏幕上了）。
        if (back != nullptr) {
            back->click();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        OverlayHost *backHost = currentPageOverlay();
        auto *backDialog =
            backHost != nullptr ? backHost->findChild<InstanceSettingsDialog *>() : nullptr;
        check(backDialog != nullptr, "★★ T25.10 点返回键，实例设置又开起来了");
        check(tabs->currentIndex() == MainWindowUI::homeTabIndex(),
              "★★ T25.10 而且落回的是**原来那颗根页**（主页）—— 不是拿"
              "「现在停在下载页」现算成「实例」",
              QStringLiteral("停在第 %1 颗").arg(tabs->currentIndex()));
        check(backDialog != nullptr && backDialog->currentPageKey() == modKey,
              "★★ T25.10 而且开在**模组那一页**（来路当时停在哪一栏就回到哪一栏）",
              backDialog == nullptr ? QStringLiteral("<没开出来>")
                                    : backDialog->currentPageKey());

        InstanceFilesPage *backFiles =
            backDialog != nullptr ? backDialog->filesPage(modKey) : nullptr;
        QListWidget *backList = backFiles != nullptr ? backFiles->list() : nullptr;
        QScrollBar *backBar = backList != nullptr ? backList->verticalScrollBar() : nullptr;
        // 还原那一发排在 `rebuildRows()` 末尾的 0 毫秒定时器里（见 restoreScrollAnchor），
        // 所以这里要泵到它落地为止，而不是读一次就走。
        for (int i = 0; i < 40; ++i) {
            QApplication::processEvents();
            if (backFiles != nullptr && !leftAt.identity.isEmpty() &&
                backFiles->scrollPosition().identity == leftAt.identity) {
                break;
            }
        }
        check(backFiles != nullptr && backList != nullptr && backList->count() == 40 &&
                  !leftAt.identity.isEmpty() &&
                  backFiles->scrollPosition().identity == leftAt.identity &&
                  backBar != nullptr && backBar->value() > 0,
              "★★ T25.10 而且那一页**还是离开时停的那一行**（换了一个对话框也认得回来 —— "
              "靠的是「顶上那一行是谁」，不是「滚动条停在几」）",
              backFiles == nullptr
                  ? QStringLiteral("<没有这一页>")
                  : QStringLiteral("顶上 %1（该 %2，value=%3）")
                        .arg(backFiles->scrollPosition().identity, leftAt.identity)
                        .arg(backBar != nullptr ? backBar->value() : -1));
    }

    TokenStore::clear(TokenStore::microsoftKey());
    TokenStore::clear(TokenStore::minecraftKey());
    TokenStore::clear(TokenStore::accountsKey());
    TokenStore::clear(TokenStore::currentUuidKey());
    check(AccountStore::accounts().isEmpty() && AccountStore::activeUuid().isEmpty(),
          "T25.6 这一节加的账号清干净了");

    restoreSettings();
    check(AppPaths::minecraftDir() == originalDir, "T25.6 数据根目录换回来了");
}

