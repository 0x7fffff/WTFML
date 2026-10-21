#include "Support.hpp"
void runShutdownTests() {
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "T43.0 前提：拿得到一个临时的数据根目录");
    if (!root.isValid()) {
        return;
    }

    /**
     * 吊住 4 秒再放。
     *
     * 这个数要**够宽**：从"反查那一趟起来"到"窗口被析构"中间要开一页、收一页、摆一批
     * 下载、跑十几条断言 —— 几百毫秒的东西；而 T43.3 那句"它还在跑"必须在窗口拆之前
     * 成立，不然钉子就空转了。也不能太大：那一段的等待直接变成这一节的时长。
     */
    StallingApi api(4000);
    check(api.startAndWait(), "T43.0 假接口（接了不答话）起来了");

    // 还挂在半路的那批下载用**另一台**：上面那一台只管一发连接，放掉之后端口就没了。
    QTcpServer downloadStall;
    check(downloadStall.listen(QHostAddress::LocalHost, 0),
          "T43.0 第二台假接口（只 listen、一个字节都不发）也起来了");

    QSettings sandbox;
    const QVariant foldersBefore = sandbox.value(QStringLiteral("Launcher/Folders"));
    const QVariant currentBefore = sandbox.value(QStringLiteral("Launcher/CurrentFolder"));
    const QVariant modrinthBefore = sandbox.value(QStringLiteral("Launcher/ModrinthApiBase"));
    const auto restoreSettings = [&] {
        sandbox.setValue(QStringLiteral("Launcher/Folders"), foldersBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), currentBefore);
        sandbox.setValue(QStringLiteral("Launcher/ModrinthApiBase"), modrinthBefore);
        sandbox.sync();
        AppPaths::setMinecraftDir(originalDir);
    };
    // 数据根目录**两头都要改**（同 T39/T42：`MainWindow` 构造里会拿盘上那个「当前游戏
    // 文件夹」再把 AppPaths 改一次）。反查那一趟的地址也指到假端口上 —— 它**必须写在
    // 造窗口之前**（`LauncherSettings` 是构造那一刻读的这一份，同 T31 那条 NOTE）。
    sandbox.setValue(QStringLiteral("Launcher/Folders"), QStringList({root.path()}));
    sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), root.path());
    sandbox.setValue(QStringLiteral("Launcher/ModrinthApiBase"), api.base());
    sandbox.sync();
    AppPaths::setMinecraftDir(root.path());

    // 夹具：一个装好的实例 + `mods/` 底下一个**假 jar**（反查那一趟就是拿盘上那几个
    // jar 的 sha1 去发请求的，有一个就够）。
    const QString idV = QStringLiteral("1.97.3");
    const QDir versionsDir(root.path() + QStringLiteral("/versions"));
    makeInstalledVersion(versionsDir, idV);
    makeVersionMeta(versionsDir, idV,
                    QByteArrayLiteral("{\"loaderName\":\"Fabric\",\"loaderVersion\":\"0.15.11\"}"));
    writeFileAt(AppPaths::versionDir(idV) + QStringLiteral("/mods/假模组.jar"),
                QByteArrayLiteral("T43-FAKE-JAR"));

    // ---------- ①② 关窗：没人在跑就一句话都不问；游戏在跑才问，而且默认那一边是「取消」 ----------
    qint64 holderPid = 0;
    {
        MainWindow w;
        w.show();
        QApplication::processEvents();

        // ① 没有游戏也没有服务端：不弹任何东西，窗口直接关。
        g_modalTitle.clear();
        g_modalText.clear();
        w.close();
        QApplication::processEvents();
        check(!w.isVisible() && g_modalTitle.isEmpty(),
              "T43.1 没有游戏也没有服务端在跑：关窗**一句话都不问**，窗口直接关掉",
              g_modalTitle.isEmpty() ? QString() : QStringLiteral("却弹了「%1」").arg(g_modalTitle));

        // ② 游戏在跑：先问一句，**默认那一边是「取消」**（回车 = 别动游戏）。
        w.show();
        QApplication::processEvents();
        LauncherManager *manager = w.findChild<LauncherManager *>();
        check(manager != nullptr, "T43.2 前提：找得到那份 LauncherManager");
        if (manager != nullptr) {
            EffectiveJavaSettings holderSettings;
            // 和 AH 段同一套：这个 exe 的一份替身，argv 里那个词让它睡够四分钟。
            holderSettings.javaPath = QCoreApplication::applicationFilePath();
            const int session =
                manager->startGame(QStringList({QStringLiteral("session-holder")}), holderSettings,
                                   idV, QStringLiteral("1.98.1"));
            pumpUntil([manager] { return !manager->runningGames().isEmpty(); }, 5000);
            check(session > 0 && manager->runningGames().size() == 1,
                  "T43.2 前提：真有一份会话在跑", QString::number(manager->runningGames().size()));
            if (!manager->runningGames().isEmpty()) {
                holderPid = manager->runningGames().first().processId;
            }

            // 默认那一边：回车 = 取消 ⇒ `event->ignore()`，窗口不关、游戏照跑。
            answerNextPrompt(1);
            w.close();
            QApplication::processEvents();
            check(w.isVisible() && manager->runningGames().size() == 1 &&
                      g_modalTitle == QStringLiteral("游戏正在运行"),
                  "★ T43.2 游戏在跑时关窗**先问一句**（标题就是它），默认那一边是「取消」⇒ "
                  "窗口没关、游戏照跑",
                  QStringLiteral("「%1」· 在跑 %2 份")
                      .arg(g_modalTitle)
                      .arg(manager->runningGames().size()));

            // 换成「关掉启动器」：窗口关掉，而游戏**被交出去**（会话表空了）。
            g_modalTitle.clear();
            answerNextPrompt(0);
            w.close();
            QApplication::processEvents();
            check(!w.isVisible() && manager->runningGames().isEmpty(),
                  "★ T43.2 答「关掉启动器」⇒ 窗口关了、会话表也空了（`detachRunningGame()` "
                  "真的被调到）",
                  QStringLiteral("%1 · 在跑 %2 份")
                      .arg(w.isVisible() ? QStringLiteral("窗口还开着")
                                         : QStringLiteral("窗口关了"))
                      .arg(manager->runningGames().size()));
        }
    }  // ← 窗口连它底下那一堆对象一起拆干净
    check(holderPid > 0 && processAlive(holderPid),
          "★★ T43.2 窗口拆干净之后那个游戏进程**还活在系统里**（用户那句「关掉启动器游戏"
          "继续跑」就是这一条）",
          QStringLiteral("PID %1").arg(holderPid));
    killProcess(holderPid);

    // ---------- ③④ 拆窗口那一刻后台还有活：反查那一趟 / 还在飞的下载 ----------
    //
    // 这一段**没有"事后"能断言的**：那一趟被等掉这件事，唯一的证据就是"`delete w` 那句
    // 回来了、后面那几行跑到了"。析构不等那几趟的话，走到 `delete w` 就是 `qFatal`，
    // 整个 mw_check 进程当场 abort、后面所有的节连同总数一起没了。所以下面那句
    // check() 是拿"进程还活着、而且真的等了一段时间"当判据的。
    bool lookupRan = false;
    bool lookupStillRunning = false;
    qint64 closingMs = 0;
    {
        // 这一格**故意摆在堆上**（这一节别处的窗口都是栈上的 `MainWindow w;`）：这一节
        // 要量的正是**析构**，而栈上那个窗口的析构发生在 `}` 上 —— 比"我要掐表的那一句"
        // 晚，掐出来的数就永远是 `close()` 那一发（16 ms）。`delete w;`
        // 就是拆窗口那一下，表掐在它两边。
        auto *w = new MainWindow();
        w->show();
        QApplication::processEvents();

        // 开一页实例设置 ⇒ 里面那一发 `requestInstanceFileIcons()` 就把反查那一趟起上了。
        // 那一页**卡在嵌套事件循环里**，所以"看它一眼、再收页"这两下得排进那个循环里做
        // （同 T20.11 那个 withPage 的手法）。
        QTimer::singleShot(0, [&] {
            OverlayHost *host = currentPageOverlay();
            if (host == nullptr) {
                return;
            }
            for (QThread *thread : w->findChildren<QThread *>()) {
                if (thread != nullptr && thread->isRunning()) {
                    lookupRan = true;
                }
            }
            if (auto *back =
                    host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
                back->click();
            }
        });
        QMetaObject::invokeMethod(w, "openInstanceSettings", Qt::DirectConnection,
                                  Q_ARG(QString, idV), Q_ARG(QString, QStringLiteral("mod")));
        QApplication::processEvents();

        check(lookupRan, "T43.3 前提：开那一页真的起了一趟后台活（假接口就在那儿等着它）");
        for (QThread *thread : w->findChildren<QThread *>()) {
            if (thread != nullptr && thread->isRunning()) {
                lookupStillRunning = true;
            }
        }
        check(lookupStillRunning,
              "★ T43.3 收掉那一页之后那一趟**还在跑**（它卡在假接口上 —— 钉子要的就是"
              "这个现场）");

        // ④ 顺带：关窗那一刻还挂着一批下载（只 listen 不发字节 ⇒ 那一条作业一直吊着）。
        VersionManager *vm = w->findChild<VersionManager *>();
        const QString slowPath = root.path() + QStringLiteral("/t43-slow.jar");
        QFile::remove(slowPath);  // 上一趟留下的残留（存在就会被当成"已下好"跳过）
        if (vm != nullptr) {
            vm->downloads()->downloadFile(
                QStringLiteral("http://127.0.0.1:%1/slow.jar").arg(downloadStall.serverPort()),
                slowPath, QString(), -1, VerifyMode::ExistsOnly);
            QApplication::processEvents();
        }
        check(vm != nullptr && vm->canCancelDownload(),
              "T43.4 前提：关窗那一刻真有一批下载挂在那儿");

        // 这一下窗口里那几条线程**还活着**，析构必须等它们。
        // 表掐在 `delete` 两边 —— **这一句就是拆窗口**（析构里那几条 `wait()` 全在里面）。
        // 析构不等反查和检查更新那两趟的话，走到这一句就是 `qFatal`，整个 mw_check
        // 进程当场 abort。
        QElapsedTimer clock;
        clock.start();
        delete w;
        closingMs = clock.elapsed();
    }
    // 门槛取 1500：`delete` 本身（线程都闲着的那一档）实测是十几毫秒，而反查那一趟卡在
    // 假接口上的 4 秒里，从"起这一趟"到"拆窗口"中间的活只有几百毫秒 ⇒ 真等到了的话这个数
    // 落在 3 秒上下。取 500 的话"它等了一小会儿"也能蒙混过去，1500 只能靠真等出来。
    check(closingMs >= 1500,
          "★★ T43.3/T43.4 窗口连线程带下载一起拆掉，全程没崩 —— 而且**真的等了**那一趟"
          "（不是它碰巧已经跑完了）",
          QStringLiteral("等了 %1 ms").arg(closingMs));

    api.finish();
    restoreSettings();
    check(AppPaths::minecraftDir() == originalDir, "T43.9 数据根目录换回来了");
}

