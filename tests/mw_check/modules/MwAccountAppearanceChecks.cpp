#include "Support.hpp"
int MwAccountAppearanceChecks() {
    // ------------------------------------------------------------------
    // 三件事在这里钉：
    // 1. 底栏名字下面那一行的**三种**取值。三种而不是两种是这次特意定的（见
    // MainWindowUI::accountTypeNotLoggedInText() 那条 NOTE）：没勾离线又没登录的时候
    // 写「离线登录」是骗人 —— 那种状态点开始会被「请先登录」挡回来。
    // 2. 入口按钮的**接线**：点一下才把那个窗建出来（懒建），再点一次是 show 回来；
    // 它顶上那句话和底栏那一行说的是同一件事（都从 accountSkinStatusText() 来）。
    // 3. applyPlayerSkin() 喂外观时，那个窗里的 3D 预览跟着走 —— 同一份贴图两个窗口，
    // 不然会出现「主页换了、那个窗还是旧外观」。
    //
    // NOTE: 这一节**全程不联网**，而且是刻意这么排的。对话框一旦开出来，
    // refreshAccountSkin() 就会去拉披风列表，而那是**同步**请求（HttpClient 默认
    // 15 秒超时）—— 外网不通的机器上点一下就要等 15 秒。所以"切换账号"那两条
    // 排在开窗**之前**（那个窗还没建时 refreshAccountSkin() 直接返回）。
    {
        const QString uuidA = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        const QString uuidB = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

        // 先把自己这一节要用的键清干净：前面 R / R2 两节也动过活动槽，不确认一下
        // "底栏为什么显示正版登录"就没法归因。
        TokenStore::clear(TokenStore::microsoftKey());
        TokenStore::clear(TokenStore::minecraftKey());
        TokenStore::clear(TokenStore::accountsKey());
        TokenStore::clear(TokenStore::currentUuidKey());
        check(!TokenStore::hasValue(TokenStore::minecraftKey()) &&
                  !TokenStore::hasValue(TokenStore::microsoftKey()),
              "Z 前提：沙盒活动槽是空的（前面几节都收拾干净了）");

        // 按**产品自己那套格式**往沙盒活动槽里写一份凭据。用 TokenStore 而不手搓字节，
        // 是因为这一段要验的不是格式（格式在 R / R2 两节），而是"盘上有凭据时界面说哪句话"。
        const auto putCredentials = [](const QString &uuid, const QString &name,
                                       const QString &refreshToken) {
            QJsonObject microsoft;
            microsoft.insert(QStringLiteral("access_token"), QStringLiteral("ms-at-") + name);
            microsoft.insert(QStringLiteral("refresh_token"), refreshToken);
            QJsonObject minecraft;
            minecraft.insert(QStringLiteral("access_token"), QStringLiteral("at-") + name);
            minecraft.insert(QStringLiteral("expires_at"),
                             QStringLiteral("2999-01-01T00:00:00Z"));
            minecraft.insert(QStringLiteral("name"), name);
            minecraft.insert(QStringLiteral("uuid"), uuid);
            return TokenStore::writeJson(TokenStore::microsoftKey(), microsoft) &&
                   TokenStore::writeJson(TokenStore::minecraftKey(), minecraft);
        };

        // ---- Z1 第二行三种取值里那两种"不发凭据就说得出来"的：未登录 ↔ 离线登录 ----
        // 这一节以前是"勾一下主页那个勾选框"，那个勾选框去掉了（，要求：
        // 「不要启动界面那个离线登录勾选框了，在添加账户那里选择添加离线账号」）。现在
        // 这一行**只跟着当前账号走**（MainWindow::offlineIdentity()），所以这里走产品那条
        // 路来换它：加一个离线账号 → 变成「离线登录」；再把它删掉 → 回到「未登录」。
        // 加、删都调 MainWindow 那两个槽（用户在「账号与外观…」里点的就是它们）。
        {
            MainWindow w9;
            w9.show();
            QApplication::processEvents();
            QLabel *accountType =
                byObjectName<QLabel>(&w9, MainWindowUI::accountTypeLabelName());
            check(accountType != nullptr, "Z1 第九个窗口里底栏第二行找得到");
            if (accountType != nullptr) {
                check(accountType->text() == MainWindowUI::accountTypeNotLoggedInText(),
                      "Z1 盘上没有账户 → 「未登录」", accountType->text());
                // 加一个离线账号（用户在「账号与外观…」里填名字点「添加离线账号」落到这里）
                // 外观先预置好：见 seedOfflineRandomSkin 那段（这一句之后 w9 就是离线身份，
                // 它已经开着了，下一次刷新外观就会走到"离线账号要随机皮肤"那条路）。
                seedOfflineRandomSkin(QStringLiteral("Zed"), kSeededSkinUrl,
                                      QColor(0x30, 0x90, 0xD0));
                QMetaObject::invokeMethod(&w9, "onOfflineAccountRequested",
                                          Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("Zed")));
                QApplication::processEvents();
                check(accountType->text() == MainWindowUI::accountTypeOfflineText(),
                      "Z1 加了个离线账号 → 「离线登录」", accountType->text());
                // 再把它删掉：删的正好是当前账号，活动槽一起清空，等于退出登录。
                // 那个确认框要**真的点「是」**（close() 掉返回的是 escape 按钮，等于没删）
                answerNextPrompt(0);
                QMetaObject::invokeMethod(&w9, "onAccountRemoveRequested",
                                          Qt::DirectConnection,
                                          Q_ARG(QString, AccountStore::activeUuid()));
                QApplication::processEvents();
                check(accountType->text() == MainWindowUI::accountTypeNotLoggedInText(),
                      "Z1 把当前那个离线账号删掉 → 回到「未登录」（上面那条不是恒真）",
                      accountType->text());
            }
        }

        // ---- Z2 入口按钮：懒建 + 顶上那句话 + 预览跟着走 ----
        //
        // 这个入口从**非模态 show()** 换成了 `overlay->execPage()`
        // （把内容区整块盖掉 + 内部一个嵌套事件循环，和「实例设置」那一页同形）。
        // 于是"点开之后要断言的东西"就不能再线性地写在 `entry->click()` 后面 ——
        // 那一句会一直卡在嵌套循环里，直到这一页被收掉（实测：整片挂死在这儿）。
        //
        // 这里的形状是：把那一片**排成一发 0 毫秒定时器**，再点。execPage 一进它的
        // 嵌套循环这一发就跑完了（包括最后按返回箭头离开），于是 click() 正常返回。
        // 整个 mw_check 里只有这儿能看见"那一页正开着"的一瞬间。
        //
        // NOTE: 那一发定时器**没有 context 对象**（`QTimer::singleShot(0, lambda)`）。
        // 万一页面根本没开成（比如覆盖层已经被别的东西占着），它会在下面那句
        // processEvents() 里跑到，看到的 host 是"没开着"，于是那几条断言红掉、
        // 而不是把测试挂死 —— 这正是想要的失败形状。
        {
            MainWindow w10;
            w10.show();
            QApplication::processEvents();
            QPushButton *entry =
                byObjectName<QPushButton>(&w10, MainWindowUI::accountSkinButtonName());
            check(entry != nullptr, "Z2 第十个窗口里入口按钮按 objectName 找得到");
            if (entry != nullptr) {
                check(w10.findChild<AccountSkinDialog *>() == nullptr,
                      "Z2 没点之前那个窗**还没有**被建出来（懒建，不点不占内存）");

                entry->click();
                QApplication::processEvents();
                QWidget *panel0 = w10.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
                QPushButton *manageButton = nullptr;
                if (panel0 != nullptr) {
                    // 末行那一颗按**标题**认：账号有几行由缓存里的账号数决定（这里排前头的
                    // 是几个 fixture 账号），记死行号会在别的布景下红在一个和产品无关的地方。
                    for (int i = 0; i < 8 && manageButton == nullptr; ++i) {
                        QLabel *rowLabel = panel0->findChild<QLabel *>(
                            MainWindowUI::bottomBarPanelRowTitleName(i));
                        if (rowLabel == nullptr) {
                            break;
                        }
                        if (rowLabel->text() == MainWindowUI::accountSkinButtonText()) {
                            manageButton = panel0->findChild<QPushButton *>(
                                MainWindowUI::bottomBarPanelRowActionName(i, 0));
                        }
                    }
                }
                check(manageButton != nullptr,
                      "Z2 那一块面板里末行那颗「账号与外观…」找得到（这一页的门在它上面）");
                if (manageButton != nullptr) {
                    manageButton->click();
                }

                QTimer::singleShot(0, [&w10, entry] {
                    OverlayHost *host =
                        w10.findChild<OverlayHost *>(OverlayHost::overlayHostName());
                    check(host != nullptr && host->pageOpen(),
                          "Z2 点一下盖住的是**整页**那一档（不再是另开一个窗）");
                    if (host == nullptr) {
                        // 后面每一条都要靠它。缺了就走人 —— 在这儿往下走是空指针。
                        return;
                    }
                    if (auto *title =
                            host->findChild<QLabel *>(OverlayHost::overlayTitleLabelName())) {
                        check(title->text() == AccountSkinDialog::windowTitle(),
                              "Z2 页眉那句话就是这一页的标题", title->text());
                    } else {
                        check(false, "Z2 页眉那句话按 objectName 找得到");
                    }

                    // 让 onAccountSkinButtonClicked() 排队的那一发 refreshAccountSkin()
                    // 先跑完（它比这一发定时器晚排，所以要手动放行一次）—— 状态行是它写的。
                    QApplication::processEvents();

                    AccountSkinDialog *dialog = w10.findChild<AccountSkinDialog *>();
                    check(dialog != nullptr, "Z2 点一下把它建出来了");
                    if (dialog != nullptr) {
                        QLabel *status =
                            dialog->findChild<QLabel *>(AccountSkinDialog::statusLabelName());
                        // 嵌入覆盖层时 showEvent() 会把 headerBar（状态行在里面）交给
                        // OverlayHost 的页眉，它不再是 dialog 的子对象：单独构造那一档
                        // 按对话框找，嵌入那一档再问覆盖层。
                        if (status == nullptr && host != nullptr) {
                            status = host->findChild<QLabel *>(
                                AccountSkinDialog::statusLabelName());
                        }
                        check(status != nullptr, "Z2 那一页顶上的状态行按 objectName 找得到");
                        if (status != nullptr) {
                            // 这里**不能**断言它等于 notLoggedInNote()：那一页一开出来
                            // refreshAccountSkin() 就先写一句身份、再被"拿不到令牌的原因"
                            // 覆盖掉（见 MainWindow::refreshAccountSkin() 第 2 步和最后那个
                            // token.isEmpty() 分支）—— 具体原因比身份那句更要紧，那是有意的。
                            // 真正的不变式是：**没登录的时候不许说已登录**。
                            check(!status->text().isEmpty() &&
                                      !status->text().contains(QStringLiteral("已登录为")),
                                  "Z2 没登录时状态行说的是没登录，不是一句已登录的身份",
                                  status->text());
                        }

                        // ---- Z3 同一份贴图喂给两个窗口 ----
                        const QString sandbox = AppPaths::minecraftDir();
                        // 护栏：验证工程只许往它自己的 bin/ 下面写东西。
                        check(sandbox.startsWith(QCoreApplication::applicationDirPath()),
                              "Z3 测试贴图落在 mw_check 自己的目录下"
                              "（绝不碰用户那份 .minecraft）",
                              sandbox);
                        const QString skinPath = sandbox + "/z-skin.png";
                        const QString capePath = sandbox + "/z-cape.png";
                        QImage testSkin(64, 64, QImage::Format_ARGB32_Premultiplied);
                        testSkin.fill(Qt::transparent);
                        {
                            QPainter painter(&testSkin);
                            painter.fillRect(0, 0, 64, 64, QColor(30, 90, 200));
                        }
                        QImage testCape(64, 32, QImage::Format_ARGB32_Premultiplied);
                        testCape.fill(QColor(200, 30, 30));
                        check(testSkin.save(skinPath) && testCape.save(capePath),
                              "Z3 沙盒里写出两张测试贴图");

                        SkinViewer *dialogViewer =
                            dialog->findChild<SkinViewer *>(AccountSkinDialog::viewerName());
                        check(dialogViewer != nullptr, "Z3 那一页里的 3D 预览找得到");
                        if (dialogViewer != nullptr) {
                            // 只比一个像素而不是整张图相等：文件里读回来的 QImage 格式和
                            // 我们造的那张不一定一样，而 == 对格式敏感（X 节同一个做法）。
                            const QColor before = dialogViewer->skin().pixelColor(0, 0);
                            QMetaObject::invokeMethod(&w10, "applyPlayerSkin",
                                                      Qt::DirectConnection,
                                                      Q_ARG(QString, skinPath),
                                                      Q_ARG(QString, capePath), Q_ARG(bool, false));
                            QApplication::processEvents();
                            check(dialogViewer->skin().pixelColor(0, 0) == QColor(30, 90, 200),
                                  "Z3 那一页里的预览跟着换成了刚喂进去的那张"
                                  "（主页和这一页同一份贴图）",
                                  QString("%1 -> %2")
                                      .arg(before.name())
                                      .arg(dialogViewer->skin().pixelColor(0, 0).name()));
                            check(!dialogViewer->cape().isNull() &&
                                      dialogViewer->cape().pixelColor(0, 0) == QColor(200, 30, 30),
                                  "Z3 披风也一起进去了");
                        }
                        QFile::remove(skinPath);
                        QFile::remove(capePath);
                    }

                    // 页面开着的时候再点一次入口：**不是**又建一个，也**不是**又盖一层
                    // （`onAccountSkinButtonClicked()` 开头那道 `overlay->isOpen()` 的闸）。
                    // 老写法验的是"再点一次是把它 show 回来"，窗口那会儿才成立。
                    //
                    // NOTE: 这一发**故意不 processEvents** —— 上面那一发
                    // refreshAccountSkin() 要是还没跑，这里放行一次它就会插进来。
                    // click() 本身是同步的，闸也是同步判的，所以不用等。
                    entry->click();
                    check(w10.findChild<AccountSkinDialog *>() == dialog,
                          "Z2 页面开着时再点入口：没有又建一个（还是原来那一个）");
                    check(host->pageOpen(),
                          "Z2 也没有叠出第二页来（同一层只做一个页面）");

                    // 按**返回箭头**离开：execPage() 把它接到 reject 上，嵌套循环这才退出
                    // —— 这一句就是这个定时器存在的理由，别删。
                    if (auto *back = host->findChild<QPushButton *>(
                            OverlayHost::overlayBackButtonName())) {
                        back->click();
                    } else {
                        check(false, "Z2 页眉左边那颗返回箭头按 objectName 找得到");
                        host->closeOverlay();  // 兜底：至少别让测试卡死在这一层上
                    }
                });

                entry->click();
                QApplication::processEvents();
                OverlayHost *after =
                    w10.findChild<OverlayHost *>(OverlayHost::overlayHostName());
                check(after != nullptr && !after->isOpen(),
                      "Z2 返回箭头点过之后覆盖层收干净了（回到没有覆盖层的状态）");
            }
        }

        // ---- Z2b 覆盖层被"收着的整页"占着时，按「账号与外观…」 ----
        //
        // 这一条**不经过面板末行**（那一颗在底栏、不在这一节的判断范围里）：它直接把
        // 设置页那颗入口按下去 —— 那条接线和面板末行那颗接的是**同一个槽**。
        {
            MainWindow w10b;
            w10b.show();
            QApplication::processEvents();
            QTabWidget *tabs = byObjectName<QTabWidget>(&w10b, MainWindowUI::tabWidgetName());
            QPushButton *settingsEntry =
                byObjectName<QPushButton>(&w10b, MainWindowUI::accountSkinSettingsButtonName());
            OverlayHost *host =
                w10b.findChild<OverlayHost *>(OverlayHost::overlayHostName());
            check(tabs != nullptr && settingsEntry != nullptr && host != nullptr,
                  "Z2b 前提：选项卡、设置页那颗「账号与外观…」入口、覆盖层都找得到");
            if (tabs != nullptr && settingsEntry != nullptr && host != nullptr) {
                tabs->setCurrentIndex(MainWindowUI::settingsTabIndex());
                QApplication::processEvents();
                bool reopenedSeen = false;
                QTimer::singleShot(0, [&] {
                    // 这一发跑在第一次 execPage 的嵌套循环里：那一页正开着。
                    check(host->pageOpen(), "Z2b 前提：入口把「账号与外观」开出来了");
                    // 用户切到主页：整页那一档**收起来**（`isOpen()` 仍真 —— 它仍占着
                    // 那一位，这正是"点了没反应"那一档的现场）。
                    tabs->setCurrentIndex(MainWindowUI::homeTabIndex());
                    QApplication::processEvents();
                    check(host->isOpen(),
                          "Z2b 前提：切到主页之后那一页只是收起来、仍占着这一位");
                    // 就在这一档按入口。让位那一发只是**请求**（见 runWhenOverlayFree），
                    // 所以"重开"排在下一轮；这里再排一发断言跟在它后面（同一轮 FIFO），
                    // 重开出来的那一页一露面就用返回箭头收掉，别让上面的 click 卡死。
                    QMetaObject::invokeMethod(&w10b, "onAccountSkinButtonClicked",
                                              Qt::DirectConnection);
                    QTimer::singleShot(0, [&] {
                        reopenedSeen = host->pageOpen();
                        if (auto *back = host->findChild<QPushButton *>(
                                OverlayHost::overlayBackButtonName())) {
                            back->click();
                        } else {
                            host->closeOverlay();  // 兜底：至少别把测试挂死在嵌套循环里
                        }
                    });
                });
                settingsEntry->click();  // 进 execPage 的嵌套循环，直到上面那发断言收页
                QApplication::processEvents();
                check(reopenedSeen,
                      "★★ Z2b 覆盖层被收着的整页占着时，按这道入口会先静默收掉它、"
                      "再开出新页（不是「点了没反应」）");
                check(!host->isOpen(), "Z2b 收尾：覆盖层收干净了");
            }
        }

        // ---- Z4 盘上有凭据的窗口：「正版登录」 + 真名 ----
        check(putCredentials(uuidA, QStringLiteral("Alice"), QStringLiteral("rt-A")) &&
                  AccountStore::captureActive() &&
                  putCredentials(uuidB, QStringLiteral("Bob"), QStringLiteral("rt-B")) &&
                  AccountStore::captureActive(),
              "Z4 前提：沙盒里备好两个账号，活动槽停在 Bob");
        {
            MainWindow w11;
            w11.show();
            QApplication::processEvents();
            QLabel *accountType =
                byObjectName<QLabel>(&w11, MainWindowUI::accountTypeLabelName());
            QLabel *accountName = byObjectName<QLabel>(&w11, MainWindowUI::accountLabelName());
            AuthManager *auth = w11.findChild<AuthManager *>();
            check(accountType != nullptr && accountName != nullptr && auth != nullptr,
                  "Z4 第十一个窗口里三块东西都找得到");
            if (accountType != nullptr && accountName != nullptr && auth != nullptr) {
                check(auth->loggedIn() && auth->playerName() == QStringLiteral("Bob"),
                      "Z4 开窗时从盘上读回了活动槽里那个人", auth->playerName());
                check(accountType->text() == MainWindowUI::accountTypeOnlineText(),
                      "Z4 当前账号是正版账号 → 「正版登录」", accountType->text());
                check(accountName->text() == QStringLiteral("Bob"),
                      "Z4 名字那行是盘上那个真名", accountName->text());

                // ---- Z5 切账号：底栏整行跟着换人 ----
                // 排在这里（而不是等那个窗开出来之后）：那个窗一开，refreshAccountSkin()
                // 就会发同步的披风请求。见本节开头那段 NOTE。
                // NOTE: 之前这一条验的是「勾着离线切号，那句话不动」——
                // 那句"离线"是勾选框给的，而勾选框去掉了。现在这一行**只跟着当前
                // 账号走**，两个账号都是正版，所以切完还是「正版登录」；真正会变的
                // 那一半在 Z7（切到离线账号）。
                QMetaObject::invokeMethod(&w11, "onAccountSwitchRequested", Qt::DirectConnection,
                                          Q_ARG(QString, uuidA));
                QApplication::processEvents();
                check(auth->playerName() == QStringLiteral("Alice"),
                      "Z5 切号之后 AuthManager 内存里也换人了（reloadStoredAccount()）",
                      auth->playerName());
                check(accountName->text() == QStringLiteral("Alice"),
                      "Z5 底栏名字跟着换（不换的话显示的还是上一个人）", accountName->text());
                check(AccountStore::activeUuid() == uuidA, "Z5 盘上的活动槽也换成了 Alice",
                      AccountStore::activeUuid());
                check(accountType->text() == MainWindowUI::accountTypeOnlineText(),
                      "Z5 而第二行还是「正版登录」（换的这一位也是正版账号）",
                      accountType->text());

                // ---- Z6 切一个不在列表里的 uuid：弹窗，而且底栏**不许**变 ----
                dismissNextPrompt();
                QMetaObject::invokeMethod(&w11, "onAccountSwitchRequested", Qt::DirectConnection,
                                          Q_ARG(QString, uuidB.left(30)));
                QApplication::processEvents();
                check(g_modalWasWarning && g_modalTitle == QStringLiteral("切换账号失败"),
                      "Z6 切不过去时弹的是警告框（产品约定：挡住操作的错误必须弹窗）",
                      g_modalTitle + QStringLiteral(" / ") + g_modalText);
                check(auth->playerName() == QStringLiteral("Alice") &&
                          accountName->text() == QStringLiteral("Alice"),
                      "Z6 而且底栏还是 Alice —— 失败不许把界面改一半",
                      accountName->text());

                // ---- Z7 加一个离线账号：进列表、成当前账号、底栏**整行**跟着换 ----
                // 这一步**故意不经过那个窗**（直接调 MainWindow::onOfflineAccountRequested，
                // 用户在「账号与外观…」里点「添加离线账号」落到的是同一个函数）：那个窗一
                // 开出来，refreshAccountSkin() 就会去拉披风列表，而那是**同步**请求 —— 现在
                // 当前账号是正版 Alice，那一档要令牌、真的要联网（见本节开头那段 NOTE）。
                // 先把身份切成离线的，再开窗（Z9）。
                {
                    const QString offlineUuid =
                        AccountStore::offlineUuidFor(QStringLiteral("Steve"));
                    // 外观预置：Z7 之后（一直到这个窗析构）都是离线身份，而这一段还要
                    // 开「账号与外观…」窗（Z9）—— 那个窗会刷新外观。见 seedOfflineRandomSkin。
                    seedOfflineRandomSkin(QStringLiteral("Steve"), kSeededSkinUrl,
                                          QColor(0x30, 0x90, 0xD0));
                    QMetaObject::invokeMethod(&w11, "onOfflineAccountRequested",
                                              Qt::DirectConnection,
                                              Q_ARG(QString, QStringLiteral("Steve")));
                    QApplication::processEvents();

                    check(AccountStore::isOffline(offlineUuid) &&
                              AccountStore::nameOf(offlineUuid) == QStringLiteral("Steve"),
                          "Z7 离线账号进了列表", AccountStore::nameOf(offlineUuid));
                    check(AccountStore::activeUuid() == offlineUuid,
                          "Z7 而且它就是当前账号（加完顺手切过去）",
                          AccountStore::activeUuid());
                    check(auth->playerName() == QStringLiteral("Steve"),
                          "Z7 名字进了 AuthManager（底栏和 --username 取的都是它）",
                          auth->playerName());
                    check(!auth->loggedIn(),
                          "Z7 但它不算登录态 —— 盘上确实没有可用的账户");
                    check(accountName->text() == QStringLiteral("Steve"),
                          "Z7 底栏左下的名字跟着换成它", accountName->text());
                    // 这一条是这次改动的**重心**：这一行以前要两个条件（账号 + 那个勾选框），
                    // 现在只有一个 —— 当前账号本身是不是离线账号。它从「正版登录」变成
                    // 「离线登录」就是因为店主换成了 Steve，没有别的开关。
                    check(accountType->text() == MainWindowUI::accountTypeOfflineText(),
                          "Z7 第二行从「正版登录」整行换成「离线登录」（只跟着账号走）",
                          accountType->text());
                    check(!TokenStore::hasValue(TokenStore::minecraftKey()) &&
                              !TokenStore::hasValue(TokenStore::microsoftKey()),
                          "Z7 活动槽被清空了（离线身份没有凭据那一半）");

                    // ---- Z8 切回在线账号：名字回来，第二行**也得**跟着换回来 ----
                    // NOTE: 之前这里钉的是"切到在线账号**不动**勾选框（那是用户
                    // 的选择）"。勾选框去掉了，那句话也就没有载体了 —— 现在这一行
                    // 必须跟着账号走，钉的正好是反面。
                    QMetaObject::invokeMethod(&w11, "onAccountSwitchRequested",
                                              Qt::DirectConnection, Q_ARG(QString, uuidA));
                    QApplication::processEvents();
                    check(AccountStore::activeUuid() == uuidA,
                          "Z8 盘上的当前账号换回 Alice", AccountStore::activeUuid());
                    check(auth->playerName() == QStringLiteral("Alice") && auth->loggedIn(),
                          "Z8 内存里也换回来了（这一档走 reloadStoredAccount()）",
                          auth->playerName());
                    check(accountName->text() == QStringLiteral("Alice"),
                          "Z8 底栏名字跟着换回来", accountName->text());
                    check(accountType->text() == MainWindowUI::accountTypeOnlineText(),
                          "Z8 第二行从「离线登录」换回「正版登录」", accountType->text());
                    check(!AccountStore::isOffline(uuidA), "Z8 在线账号没被认成离线");

                    // ---- Z9 开窗：顶上那句说的就是当前身份，而且离线这一档**不联网** ----
                    // 那个窗一开，refreshAccountSkin() 第 4 步会去拉披风列表（同步请求）。
                    // 只有"当前是离线账号"这一档是全程不联网的，所以这里先把身份切回
                    // 离线账号 Steve，再点那个入口按钮。
                    QMetaObject::invokeMethod(&w11, "onAccountSwitchRequested",
                                              Qt::DirectConnection,
                                              Q_ARG(QString, offlineUuid));
                    QApplication::processEvents();
                    check(AccountStore::activeUuid() == offlineUuid,
                          "Z9 前提：又切回离线账号 Steve（这一档才不联网）",
                          AccountStore::activeUuid());
                    QPushButton *entry =
                        byObjectName<QPushButton>(&w11, MainWindowUI::accountSkinButtonName());
                    check(entry != nullptr, "Z9 第十一个窗口里入口按钮也找得到");
                    if (entry != nullptr) {
                        entry->click();
                        QApplication::processEvents();
                        QWidget *panel0 =
                            w11.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
                        QPushButton *manageButton = nullptr;
                        if (panel0 != nullptr) {
                            for (int i = 0; i < 8 && manageButton == nullptr; ++i) {
                                QLabel *rowLabel = panel0->findChild<QLabel *>(
                                    MainWindowUI::bottomBarPanelRowTitleName(i));
                                if (rowLabel == nullptr) {
                                    break;
                                }
                                if (rowLabel->text() == MainWindowUI::accountSkinButtonText()) {
                                    manageButton = panel0->findChild<QPushButton *>(
                                        MainWindowUI::bottomBarPanelRowActionName(i, 0));
                                }
                            }
                        }
                        check(manageButton != nullptr,
                              "Z9 面板末行那颗「账号与外观…」也找得到");
                        if (manageButton != nullptr) {
                            manageButton->click();
                        }
                        // 这个入口是 `execPage()`（整页盖住内容区 + 内部一个
                        // 嵌套事件循环），"点开之后要干的那些事"必须排进这一发定时器，
                        // 最后按返回箭头离开，`click()` 才会返回。见 Z2 开头那段（那里把
                        // 理由写全了）。
                        QTimer::singleShot(0, [&w11, &auth, accountName, offlineUuid, entry] {
                            OverlayHost *host =
                                w11.findChild<OverlayHost *>(OverlayHost::overlayHostName());
                            check(host != nullptr && host->pageOpen(),
                                  "Z9 点一下盖住的是整页那一档");
                            if (host == nullptr) {
                                return;  // 后面每一条都要靠它。
                            }
                            // 让排队的那一发 refreshAccountSkin() 先跑完 —— 状态行是它写的
                            // （它比这一发定时器晚排，所以要手动放行一次）。
                            QApplication::processEvents();

                            AccountSkinDialog *dialog = w11.findChild<AccountSkinDialog *>();
                            check(dialog != nullptr, "Z9 点一下也把它建出来了");
                            if (dialog != nullptr) {
                                QLabel *status = dialog->findChild<QLabel *>(
                                    AccountSkinDialog::statusLabelName());
                                // 嵌进覆盖层之后状态行跟着 headerBar 搬到了页眉，
                                // 不在 dialog 子树里了 —— 理由见 Z2 那一段。
                                if (status == nullptr && host != nullptr) {
                                    status = host->findChild<QLabel *>(
                                        AccountSkinDialog::statusLabelName());
                                }
                                check(status != nullptr, "Z9 状态行找得到");
                                if (status != nullptr) {
                                    // 当前是离线账号时 refreshAccountSkin() 在第 4 步（读令牌、
                                    // 拉披风那一段）**之前**就返回了，所以这句话不会被失败原因
                                    // 覆盖 —— 而且那条路上一行网络代码都没走到。这一条同时钉住
                                    // 了那件事。
                                    check(status->text() ==
                                              AccountSkinDialog::offlineIdentityText(
                                                  QStringLiteral("Steve")),
                                          "Z9 开页时顶上那句说的是「离线登录为 Steve」"
                                          "（这一档也说明没去拉披风）",
                                          status->text());
                                }

                                // ---- Z10 名字不合法：弹窗，而且底栏一个像素都不许变 ----
                                // NOTE: 这一条现在验的是"提示盖在那一页上、底下的页还在"
                                // （真窗口那会儿是两个窗叠着）。弹在哪儿是覆盖层自己的
                                // 事，"失败不许把界面改一半"那两条一个字没变。
                                dismissNextPrompt();
                                QMetaObject::invokeMethod(&w11, "onOfflineAccountRequested",
                                                          Qt::DirectConnection,
                                                          Q_ARG(QString, QStringLiteral("有 空格")));
                                QApplication::processEvents();
                                check(g_modalWasWarning &&
                                          g_modalTitle == QStringLiteral("添加离线账号失败"),
                                      "Z10 名字里有空格 → 弹的是警告（挡住操作的错误必须弹）",
                                      g_modalTitle + QStringLiteral(" / ") + g_modalText);
                                check(auth->playerName() == QStringLiteral("Steve") &&
                                          accountName->text() == QStringLiteral("Steve"),
                                      "Z10 而且底栏还是 Steve —— 失败不许把界面改一半",
                                      accountName->text());
                                check(AccountStore::activeUuid() == offlineUuid,
                                      "Z10 当前账号也没被踢（失败不许把人踢下线）",
                                      AccountStore::activeUuid());
                                // 那一条提示收掉之后，底下那一页**还在**（提示是叠上去的）。
                                check(host->pageOpen() && currentPrompt() == nullptr,
                                      "Z10 提示收掉之后底下那一页还在（提示是叠上去的）");
                            }

                            // 按返回箭头离开（= reject）—— 这一句就是这个定时器存在的理由。
                            if (auto *back = host->findChild<QPushButton *>(
                                    OverlayHost::overlayBackButtonName())) {
                                back->click();
                            } else {
                                check(false, "Z9 页眉左边那颗返回箭头按 objectName 找得到");
                                host->closeOverlay();  // 兜底：至少别让测试卡死在这一层上
                            }
                        });
                        entry->click();
                        QApplication::processEvents();
                        OverlayHost *after =
                            w11.findChild<OverlayHost *>(OverlayHost::overlayHostName());
                        check(after != nullptr && !after->isOpen(),
                              "Z9 返回箭头点过之后覆盖层收干净了");
                    }
                }
            }
        }

        // ---- Z11 开窗时盘上当前就是离线账号：名字接回来 + 第二行是「离线登录」 ----
        // 这一条钉的是 MainWindow 构造函数里那一段：AuthManager 只读活动槽，而离线账号在
        // 盘上**没有凭据** —— 不接回来的话底栏和 --username 里会是个占位的 "Player"。
        {
            check(AccountStore::activate(AccountStore::offlineUuidFor(QStringLiteral("Steve"))),
                  "Z11 前提：把沙盒摆成「当前是离线账号 Steve」");
            MainWindow w12;
            w12.show();
            QApplication::processEvents();
            QLabel *accountType =
                byObjectName<QLabel>(&w12, MainWindowUI::accountTypeLabelName());
            QLabel *accountName = byObjectName<QLabel>(&w12, MainWindowUI::accountLabelName());
            AuthManager *auth = w12.findChild<AuthManager *>();
            check(accountType != nullptr && accountName != nullptr && auth != nullptr,
                  "Z11 第十二个窗口里那三块东西都找得到");
            if (accountType != nullptr && accountName != nullptr && auth != nullptr) {
                check(auth->playerName() == QStringLiteral("Steve"),
                      "Z11 开窗时把离线名字接回来了（不是占位的 Player）", auth->playerName());
                check(accountName->text() == QStringLiteral("Steve"),
                      "Z11 底栏左下的名字就是它", accountName->text());
                check(accountType->text() == MainWindowUI::accountTypeOfflineText(),
                      "Z11 第二行是「离线登录」（跟着当前账号来，没有第二个开关）",
                      accountType->text());
            }
        }

        // ---- Z11b 登录失败那一趟：盘上清空了，内存和底栏都得跟着 ----
        // 复现的是"用着账号 A 去登另一个微软账号、登录没成"那一刻：登录那一趟开头的
        // exchangeAuthorizationCode() 已经把活动槽里那份 Minecraft 凭据清掉了（那是
        // 有意的一步，为的是不让"旧 uuid 配新刷新令牌"的错配留在盘上），而
        // m_uuid / m_playerName 是**另一份**，原来谁都没去动它 —— 于是盘上写着"没登录"、
        // 内存和底栏还登着上一个人；接着点启动，loadSavedToken() 会拿 microsoft 槽里那份
        // **新账号**的刷新令牌换出新账号来，一次"失败"的登录悄悄成了另一个人。
        //
        // NOTE: AuthManager 那一半（失败分支里那句 reloadStoredAccount()）要**真发一次
        // 失败的登录**才走得到，离线机器上驱动不了（没有假的 OAuth 端点）。这一段把
        // 那一步手工补上，量的是"那一步做了之后界面是什么样" —— 也就是用户真正看得见
        // 的那一半：底栏不许停在上一个人的名字上（不重画就一直是它）。
        {
            check(AccountStore::activate(uuidA), "Z11b 前提：把当前账号切回正版 Alice");
            MainWindow w13;
            w13.show();
            QApplication::processEvents();
            QLabel *accountType = byObjectName<QLabel>(&w13, MainWindowUI::accountTypeLabelName());
            QLabel *accountName = byObjectName<QLabel>(&w13, MainWindowUI::accountLabelName());
            AuthManager *auth = w13.findChild<AuthManager *>();
            check(accountType != nullptr && accountName != nullptr && auth != nullptr,
                  "Z11b 第十三个窗口里那三块东西都找得到");
            if (accountType != nullptr && accountName != nullptr && auth != nullptr) {
                check(auth->loggedIn() && auth->playerName() == QStringLiteral("Alice") &&
                          accountType->text() == MainWindowUI::accountTypeOnlineText(),
                      "Z11b 开窗时是「正版登录」+ 盘上那个真名",
                      accountType->text());
                // 登录那一趟开头那一步（AuthManager::exchangeAuthorizationCode）。
                TokenStore::clear(TokenStore::minecraftKey());
                QApplication::processEvents();
                check(accountType->text() == MainWindowUI::accountTypeOnlineText() &&
                          accountName->text() == QStringLiteral("Alice"),
                      "Z11b 盘上清空了，界面这会儿还停在原来的样子上（底栏那两行是上一次"
                      "updateBottomBar() 摆的）");
                // 失败分支那两步，顺序照抄 handleNewConnection：先让内存跟着盘走、再发信号。
                auth->reloadStoredAccount();
                check(!auth->loggedIn(), "Z11b 内存里那份身份跟着盘走（退回未登录）");
                dismissNextPrompt();
                QMetaObject::invokeMethod(auth, "loginFailed", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("网络断了。")));
                check(g_modalWasWarning, "Z11b 失败照样弹警告框（挡住操作的错误必须弹窗）",
                      g_modalTitle);
                check(accountType->text() == MainWindowUI::accountTypeNotLoggedInText(),
                      "Z11b 底栏第二行跟着重画成「未登录」（不重画就停在上一个人的身份上）",
                      accountType->text());
            }
        }

        // ---- Z12 收拾自己写的东西 ----
        TokenStore::clear(TokenStore::microsoftKey());
        TokenStore::clear(TokenStore::minecraftKey());
        TokenStore::clear(TokenStore::accountsKey());
        TokenStore::clear(TokenStore::currentUuidKey());
        check(!AccountStore::accounts().isEmpty() == false,
              "Z12 沙盒里的账号列表清干净了（下一个人不会捡到 Bob）");
    }
    return 0;
}


