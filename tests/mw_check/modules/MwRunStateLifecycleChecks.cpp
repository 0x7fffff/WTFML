#include "Support.hpp"
int MwRunStateLifecycleChecks(const QDir &root, const QDir &versionsDir) {
    // ------------------------------------------------------------------
    // 要求：「左下角没有实例的时候显示（红色的）■（默认的黑色）当前没有
    // 运行中的实例，启动的时候那个符号（也可以画图代替）变成黄色，文本为版本名（没有就
    // 版本号-加载器名称，**不受名字显示设置影响**）启动中... 启动以后符号变成绿色，和原来
    // 一样」。
    //
    // 红那一态（没实例在跑）和绿那一态（有实例在跑）分别由 K 段和下面 ② 钉住；这里钉的是
    // **黄的那一态** —— 它最不容易证，因为"正在启动"是 MainWindow 里一个私有集合
    // （m_launchingSessions）说了算的，只有**它自己那条启动路**能把它摆出来。
    //
    // 所以这一段走的是和 U 段同一条路（离线身份 + 点「启动游戏」），只是那个假 java 换成了
    // **这个 exe 的一份副本**：它被当成 java 起的时候不出声、就那么活着 —— 正是产品里
    // "JVM 起来了、游戏还没吐第一行话"那个窗口。名字走的是盘上那份 wtfml.meta 里的
    // customName（所以这一条同时证了"名字是从实例身上读的"）。
    {
        const QString id = QStringLiteral("状态方块测试");
        const QString customName = QStringLiteral("我的整合包-特别版");
        const QDir stateDir(versionsDir.filePath(id));
        stateDir.mkpath(QStringLiteral("."));
        // 一个真得能走完启动路径的实例：客户端 jar 要有（checkPreLaunch 要）、json 要有
        // 主类（发出去的 java 命令行里得有它 —— 假 java 那一支就靠它认自己是被当 java 起的）。
        writeFileAt(stateDir.filePath(id + QStringLiteral(".jar")), QByteArrayLiteral("stub"));
        writeFileAt(stateDir.filePath(id + QStringLiteral(".json")),
                    QByteArrayLiteral("{\"id\":\"状态方块测试\",\"libraries\":[],"
                                      "\"mainClass\":\"net.minecraft.client.main.Main\","
                                      "\"javaVersion\":{\"majorVersion\":17}}"));

        // 假 java：**这个 exe 的副本**，旁边摆一个 release 文件。
        // NOTE: release 文件非摆不可 —— 没有它，探主版本那一步会真的去跑一次
        // `java.exe -version`（见 GlobalJavaSettings::probeJavaMajorVersion）。
        const QDir fakeJavaDir(root.filePath(QStringLiteral("java/state17")));
        fakeJavaDir.mkpath(QStringLiteral("bin"));
        const QString fakeJava = fakeJavaDir.filePath(QStringLiteral("bin/java.exe"));
        check(QFile::copy(QCoreApplication::applicationFilePath(), fakeJava),
              "AG0 假 java 摆好了（这个 exe 的一份副本）", fakeJava);
        writeFileAt(fakeJavaDir.filePath(QStringLiteral("release")),
                    QByteArrayLiteral("JAVA_VERSION=\"17.0.12\"\n"));
        {
            QJsonObject meta;
            meta.insert(QStringLiteral("javaPath"), fakeJava);
            // 自定义名 —— 「启动中」那句里的名字就是它（不受显示设置影响的写法，
            // 规则本身在 verify 的 I7 段逐条钉过）。
            // NOTE: 盘上这个键叫 **"name"**，不叫 "customName"：VersionDisplayParts 里那个
            // 成员叫 customName，而 VersionManager::displayParts() 读的是 meta 里的
            // "name"（见那边 readString("name") 那行）。写错了不报错 —— 名字静默地
            // 退回目录名，断言那边看着就像"名字规则坏了"。
            meta.insert(QStringLiteral("name"), customName);
            writeFileAt(stateDir.filePath(QStringLiteral("wtfml.meta")),
                        QJsonDocument(meta).toJson(QJsonDocument::Compact));
        }
        const QDir mcDir(root.filePath(QStringLiteral(".minecraft")));
        const bool hadLibraries = QDir(mcDir.filePath(QStringLiteral("libraries"))).exists();
        const bool hadAssets = QDir(mcDir.filePath(QStringLiteral("assets"))).exists();
        mcDir.mkpath(QStringLiteral("libraries/占位"));
        mcDir.mkpath(QStringLiteral("assets"));

        {
            MainWindow w;
            w.show();
            w.resize(1280, 800);  // 7a：窄于 1024 时导航轨折成图标轨，实例状态那格的字收起（见 main.cpp 同一句）
            w.ui()->titleBar()->setCollapsed(false);  // 155 起每次启动导航先收起：宽窗也要显式展开
            QApplication::processEvents();
            QComboBox *combo = comboByObjectName(&w, MainWindowUI::installedVersionComboName());
            QMetaObject::invokeMethod(&w, "onVersionsFetched", Qt::DirectConnection,
                                      Q_ARG(QStringList, QStringList({id})));
            QApplication::processEvents();
            if (combo != nullptr && combo->findData(id) >= 0) {
                combo->setCurrentIndex(combo->findData(id));
                QApplication::processEvents();
            }
            check(combo != nullptr && selectedId(combo) == id,
                  "AG1 前提：选中了那个专用实例",
                  combo != nullptr ? selectedId(combo) : QString());

            QLabel *mark = w.findChild<QLabel *>(MainWindowUI::runStateMarkName());
            QLabel *run = w.findChild<QLabel *>(MainWindowUI::runStateLabelName());
            QLabel *detail = w.findChild<QLabel *>(MainWindowUI::runStateDetailLabelName());
            check(mark != nullptr && run != nullptr && detail != nullptr,
                  "AG1 左下角那三块（方块 / 正文 / 第二行）都找得到");
            if (mark != nullptr && run != nullptr && detail != nullptr && combo != nullptr) {
                dismissNextPrompt();
                QMetaObject::invokeMethod(&w, "onStartButtonClicked", Qt::DirectConnection);
                QApplication::processEvents();
                // 启动成功之后**窗口可能按「启动后」那个设置把自己收起来**（最小化/隐藏，
                // 见 applyLauncherBehaviourAfterLaunch）。这一节量的是那一格里的控件，
                // 所以显式摆回可见 —— 藏着的控件 isVisible() 是 false，那几条断言会全红在
                // 一个跟它们无关的原因上。
                w.show();
                QApplication::processEvents();
                check(g_modalText.isEmpty(),
                      "AG2 那一趟启动没有弹框（假 java 是通的，路走到底了）",
                      g_modalTitle + QStringLiteral(" / ") + g_modalText);
                // 没弹框，上面排的那一发就没处落：它还会盯 20 秒，把下一节弹的第一张框
                // （Y 段的预设弹窗）当成自己的点掉。这里作废它。
                ++g_modalHandlerGeneration;

                // ---- 进程起来后（包括尚未吐出第一行输出时）：左下角只显示「%1 个进程运行中」+ 绿点 ----
                // #52 约定：去掉「启动中」「异常退出」等其余状态，启动/崩溃提示统一进任务中心。
                check(run->text() == MainWindowUI::runStateRunningCountText(1),
                      "AG2 进程启动后左下角直接写「1 个进程运行中」（不再写启动中）",
                      run->text());
                check(!detail->isVisible(),
                      "AG2 停在主页且选中的正是这个在跑的实例时，第二行照旧收起",
                      detail->text());
                const QPalette markPalette = runStateMarkPalette(&w);
                const QColor wantGreen = MainWindowUI::runStateMarkColor(
                    MainWindowUI::RunStateMark::Running, markPalette);
                const QImage shot = mark->pixmap().toImage();
                check(!shot.isNull() &&
                          shot.pixelColor(shot.width() / 2, shot.height() / 2) == wantGreen &&
                          shot.pixelColor(shot.width() / 2, shot.height() / 2) !=
                              MainWindowUI::runStateMarkColor(
                                  MainWindowUI::RunStateMark::Idle, markPalette) &&
                          shot.pixelColor(shot.width() / 2, shot.height() / 2) !=
                              MainWindowUI::runStateMarkColor(
                                  MainWindowUI::RunStateMark::Error, markPalette),
                      "AG2 进程在跑 → 方块画的是**绿**那一档（不再画黄）",
                      QStringLiteral("量到 %1，绿该是 %2")
                          .arg(shot.pixelColor(shot.width() / 2, shot.height() / 2).name())
                          .arg(wantGreen.name()));

                // ---- ③ 会话被**硬停**掉之后：崩溃只进任务中心，运行状态格回到灰「未运行」 ----
                LauncherManager *manager = w.findChild<LauncherManager *>();
                check(manager != nullptr, "AG3 拿得到那个 LauncherManager");
                if (manager != nullptr) {
                    for (const LauncherManager::GameSession &game : manager->runningGames()) {
                        manager->stopGame(game.sessionId);
                    }
                    const bool empty =
                        pumpUntil([manager] { return manager->runningGames().isEmpty(); }, 8000);
                    check(empty, "AG3 那一份会话停掉了（假 java 那一趟收到 kill）");
                    repaintBottomBar(&w);
                    check(run->text() == MainWindowUI::runStateNotRunningText(),
                          "AG3 进程退掉之后运行状态回到「未运行」（异常退出只在任务中心提示）",
                          run->text());
                    check(!detail->isVisible() && detail->text().isEmpty(),
                          "AG3 回到「未运行」后第二行清空收起", detail->text());
                    const QImage back = mark->pixmap().toImage();
                    const QColor backColor = back.isNull()
                                                 ? QColor()
                                                 : back.pixelColor(back.width() / 2, back.height() / 2);
                    check(!back.isNull() && backColor ==
                                                MainWindowUI::runStateMarkColor(
                                                    MainWindowUI::RunStateMark::Idle,
                                                    markPalette) &&
                              runStateIsGrey(backColor),
                          "AG3 方块跟着回到**灰**那一档（未运行）",
                          backColor.name());
                }
            }
        }

        // 收拾：假 java（**进程先停掉了，这里才删得掉**）、专用实例、两个目录。
        QDir(fakeJavaDir.absolutePath()).removeRecursively();
        QDir(stateDir.absolutePath()).removeRecursively();
        check(!fakeJavaDir.exists() && !stateDir.exists(), "AG4 假 java 和专用实例都删干净了");
        if (!hadLibraries) {
            QDir(mcDir.filePath(QStringLiteral("libraries"))).removeRecursively();
        }
        if (!hadAssets) {
            QDir(mcDir.filePath(QStringLiteral("assets"))).removeRecursively();
        }
    }
    return 0;
}


