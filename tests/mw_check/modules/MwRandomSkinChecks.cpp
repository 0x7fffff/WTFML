#include "Support.hpp"
int MwRandomSkinChecks() {
    // ------------------------------------------------------------------
    // 这一节接的是 RandomSkinStore 那条路。**纯函数**那一半（地址怎么拼、列表怎么解、
    // 挑下标越界了怎么办、盘上那一行还算不算数）在 verify 的 ZE 段，一条都不重复。
    //
    // 这里验的是**接线**：MainWindow 有没有把"盘上记的那一行"真的画成右栏那张脸、
    // 那句话对不对、"回来的东西不属于当前身份"时会不会被丢掉。判据只能是屏幕上的像素
    // 和盘上那份记录 —— 中间任何一步接错了，两者都会对不上。
    //
    // 全程离线（一个包都不发）：随机皮肤是**测试预置**的 —— 盘上一行 + 缓存里那张图
    // 本体，于是 RandomSkinStore::fetchRandomSkin() 一次都走不到（它是"盘上还没有记录"
    // 那一档才会走的）。真要验那个端点今天还在不在，那是 mw_check 的一次性探针
    // `random-skin-live`，不是这一节。
    {
        const QString name = QStringLiteral("RandSkin");
        const QString uuid = AccountStore::offlineUuidFor(name);
        const QString recordedPath = RandomSkinStore::chosenPathFor(uuid);
        const QColor tintA(0x30, 0x90, 0xD0);
        const QColor tintB(0xE0, 0x70, 0x20);
        // AF4 的对照：这一节**不许**凭空多出一条取随机皮肤失败的日志（下面 AF5 会故意
        // 造一条"记不下来"的，那是另一句话，不混在一起）。
        const int afFailedBefore = countOccurrences(g_messages, QStringLiteral("取随机皮肤失败"));

        // ---- AF1 前提：此刻**没有当前账号**，所以右栏是写死的那张内置 Steve ----
        // 这一条非有不可：没有它，"AF4 画的是预置那张"可能是恒真的（比如不管什么账号
        // 都画桌面那张）。这里同时把"默认那张头"留下来当对照组。
        QImage defaultAvatar;
        {
            MainWindow wDefault;
            wDefault.show();
            QApplication::processEvents();
            SkinViewer *viewer =
                byObjectName<SkinViewer>(&wDefault, MainWindowUI::skinViewerName());
            QLabel *hint = byObjectName<QLabel>(&wDefault, MainWindowUI::skinHintLabelName());
            QLabel *avatar = byObjectName<QLabel>(&wDefault, MainWindowUI::avatarLabelName());
            check(viewer != nullptr && hint != nullptr && avatar != nullptr,
                  "AF1 没有当前账号时右栏那三样也都在");
            if (viewer != nullptr && hint != nullptr && avatar != nullptr) {
                check(viewer->skin() == SkinModel::defaultSteveSkin() &&
                          hint->text() == MainWindowUI::skinHintNotLoggedInText(),
                      "AF1 没有当前账号 → 还是默认外观 + 「未登录」那句"
                      "（离线那一档**只管离线账号**，没有身份就没有可记的地方）",
                      hint->text());
                defaultAvatar = avatar->pixmap().toImage();
                check(!defaultAvatar.isNull(), "AF1 默认外观那个头也拿到了（AF5 的对照组）");
            }
        }

        // ---- AF2 预置 + 建账号 ----
        seedOfflineRandomSkin(name, kSeededSkinUrl, tintA);
        QString offlineError;
        check(AccountStore::addOffline(name, &offlineError),
              "AF2 建一个离线账号（产品那条路：AccountStore::addOffline）", offlineError);
        check(RandomSkinStore::loadChosenSkinUrl(uuid) == kSeededSkinUrl,
              "AF2 预置那一行读得回来（后面几条验的是它有没有变成屏幕上那张脸）",
              RandomSkinStore::loadChosenSkinUrl(uuid));

        // ---- AF3 开窗：盘上那一行变成屏幕上那张脸 ----
        MainWindow wAF;
        wAF.show();
        QApplication::processEvents();
        SkinViewer *viewer = byObjectName<SkinViewer>(&wAF, MainWindowUI::skinViewerName());
        QLabel *hint = byObjectName<QLabel>(&wAF, MainWindowUI::skinHintLabelName());
        QLabel *avatar = byObjectName<QLabel>(&wAF, MainWindowUI::avatarLabelName());
        check(viewer != nullptr && hint != nullptr && avatar != nullptr,
              "AF3 离线账号的窗口里右栏那三样都按 objectName 找得到");

        if (viewer != nullptr && hint != nullptr && avatar != nullptr) {
            // 缓存命中是**同步**的（fetchTexture 命中就同步发 textureReady），所以这一句
            // 之后已经画好了 —— 不需要等网络，也不该等（这一节压根没有请求在飞）。
            check(viewer->skin().pixelColor(0, 0) == tintA,
                  "AF3 离线账号右栏画的是预置那张（不是内置 Steve、也不是空白）",
                  viewer->skin().pixelColor(0, 0).name());

            // 那句话是**第三句**：skinHintLoadedText 说的是「正版外观」（这张跟正版没关系），
            // skinHintNotLoggedInText 说的是「显示的是默认外观」（这时候恰恰不是）。
            check(hint->text() == MainWindowUI::skinHintOfflineLoadedText(name),
                  "AF3 那句话说的是「离线账号 · 启动器里随机挑的」", hint->text());
            check(hint->text() != MainWindowUI::skinHintLoadedText(name, false) &&
                      hint->text() != MainWindowUI::skinHintNotLoggedInText(),
                  "AF3 而且不是正版那句、也不是未登录那句（三句话两两不同）",
                  hint->text());

            check(!avatar->pixmap().isNull() && avatar->pixmap().toImage() != defaultAvatar,
                  "AF3 底栏头像也跟着换了（不是默认那个头）");

            // ---- AF4 一个包都没发 ----
            // 判据是"那句话不是「正在获取」"，而且这一跑里没有一条「取随机皮肤失败」。
            // 这一条同时是"预置生效了"的证据：真要发了请求，在没网的机器上它会等到
            // 10 秒超时，然后那句话变成"没要到"。
            check(hint->text() != MainWindowUI::skinHintLoadingText(name),
                  "AF4 没有一发请求在路上（预置命中缓存 → 整条路是同步的）", hint->text());
            check(countOccurrences(g_messages, QStringLiteral("取随机皮肤失败")) ==
                      afFailedBefore,
                  "AF4 也没有「取随机皮肤失败」那种日志",
                  QString::number(
                      countOccurrences(g_messages, QStringLiteral("取随机皮肤失败"))));

            // ---- AF5 「记不下来」不影响这一趟显示，但下次会重挑 ----
            // 怎么把"记不下来"摆出来：把那个文件的位置换成一个**目录**（QSaveFile 覆盖不了
            // 一个目录）。这条路真机上会遇到（盘满、目录只读、杀毒锁住），而它的约定是
            // "这一趟照样画得出来，只是下次开启动器会重挑一张" —— 也就是**显示不依赖记账**。
            QFile::remove(recordedPath);
            check(QDir().mkpath(recordedPath), "AF5 把记账那个位置换成一个目录（模拟写不下去）");
            {
                const int before = countOccurrences(g_messages, QStringLiteral("记下随机皮肤失败"));
                // AF6 那张图先塞进缓存，这样这一趟也是同步的（不然会变成"没要到"）。
                QDir().mkpath(AppPaths::skinsDir());
                QImage second(64, 64, QImage::Format_ARGB32_Premultiplied);
                second.fill(tintB);
                check(second.save(PlayerSkinStore::cachePathForUrl(kSeededSkinUrl2)),
                      "AF5 第二张图也按内容寻址塞进缓存了（这一趟同样不发请求）");

                QMetaObject::invokeMethod(&wAF, "onRandomSkinReady", Qt::DirectConnection,
                                          Q_ARG(QString, kSeededSkinUrl2));
                QApplication::processEvents();
                check(viewer->skin().pixelColor(0, 0) == tintB,
                      "AF5 记不下来也**照样换上了**（显示不依赖记账这一步）",
                      viewer->skin().pixelColor(0, 0).name());
                check(countOccurrences(g_messages, QStringLiteral("记下随机皮肤失败")) > before,
                      "AF5 而且日志里留了一行（唯一的迹象，不弹窗）");
            }

            // ---- AF6 记下来了：下次就是它（不然每次开启动器都换一张脸） ----
            QDir().rmdir(recordedPath);
            QMetaObject::invokeMethod(&wAF, "onRandomSkinReady", Qt::DirectConnection,
                                      Q_ARG(QString, kSeededSkinUrl));
            QApplication::processEvents();
            check(viewer->skin().pixelColor(0, 0) == tintA, "AF6 又挑中第一张 → 画回第一张",
                  viewer->skin().pixelColor(0, 0).name());
            check(RandomSkinStore::loadChosenSkinUrl(uuid) == kSeededSkinUrl,
                  "AF6 盘上那一行也跟着改写成了第一张（不写的话下次开窗又变回第二张）",
                  RandomSkinStore::loadChosenSkinUrl(uuid));

            // ---- AF7 身份不再是这个离线账号 → 回来的东西必须丢掉 ----
            // 真机上这一段是"用户在这几百毫秒里换了账号 / 把这个离线账号删了"。丢掉是必须的：
            // 记下去等于把 A 的脸记到 B 名下，而那份记录**永不失效**，再也没人来纠正它。
            //
            // NOTE: 判据只能看**屏幕**，不能看盘上那一行 —— 身份没了以后 activeUuid() 是空串，
            // chosenPathFor("") 也是空，记账那一步本来就会失败，于是"没被写进去"这件事
            // 在两种实现下都成立（照妖镜不亮）。而屏幕会说话：真按下来了，这一句之后
            // 那张脸就变成第二张。
            check(AccountStore::remove(uuid), "AF7 把这个离线账号删掉（当前账号没了）");
            check(!AccountStore::isOffline(AccountStore::activeUuid()),
                  "AF7 当前身份确实不再是那个离线账号");
            QMetaObject::invokeMethod(&wAF, "onRandomSkinReady", Qt::DirectConnection,
                                      Q_ARG(QString, kSeededSkinUrl2));
            QApplication::processEvents();
            check(viewer->skin().pixelColor(0, 0) == tintA,
                  "AF7 身份变了 → 回来的那张被丢掉，屏幕上一个像素都没动",
                  viewer->skin().pixelColor(0, 0).name());
            check(hint->text() == MainWindowUI::skinHintOfflineLoadedText(name),
                  "AF7 那句话也没被顶掉（丢掉是整条路都丢掉，不是只丢一半）", hint->text());
        }

        // ---- AF8 收拾自己写的东西 ----
        // 沙盒是复用的一份（O 节只删 versions/ 和 settings/），这几样留在这儿会跟着
        // 下一次运行，把"盘上本来没有记录"那个前提搅掉。
        QFile::remove(recordedPath);  // AF5 那个目录已经被 rmdir 掉了，这里管的是文件那一趟
        QDir(recordedPath).removeRecursively();
        QFile::remove(PlayerSkinStore::cachePathForUrl(kSeededSkinUrl));
        QFile::remove(PlayerSkinStore::cachePathForUrl(kSeededSkinUrl2));
        check(!QFileInfo::exists(recordedPath), "AF8 记账那一条清掉了");
    }
return 0;
}


