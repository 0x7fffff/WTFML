#include "Support.hpp"
int MwOverridePersistenceChecks(const QDir &root, const QString &settingsDir, const QDir &versionsDir, QSpinBox * &maxSpin, MemorySlider * &maxSlider) {
    // ------------------------------------------------------------------
    // 产品里能**改**覆盖的地方是那个模态对话框，它自己那一半在 ui_check 的 U 段逐条
    // 验过（控件 → Override 值、空路径归一、自动档下两行内存不落值）。这里验 MainWindow
    // 这一半，而这一半里真正会错的是**作用域**：
    // - 一个实例的覆盖写进了全局配置（用户手填的全局 Java 路径、全局内存被某个实例盖掉）；
    // - 失败路径上留下半截写入。
    //
    // NOTE: 覆盖生效之后"这次启动到底用哪套值"由 verify 的 R 段逐条钉住
    // （resolveJavaSettings 六项 × 跟随/覆盖 + 钳位）；这里验的是它**没有副作用**。
    //
    // NOTE: 以前这里写着"`resolveMemoryForLaunch()` 里那条「实例要的自动档 → 结果不写回
    // 全局」证不到，因为让 Java 那一步成功要真的起一个 JVM"。现在证得到了 ——
    // U4～U7 用"空文件 + release 文件"造的假运行时把 Java 那一步走通，全程不起进程，
    // 于是那条分支也跟着被跑到了（U6 那条"别的项一个都没丢"就是它）。
    {
        // U 段每一步都要点启动，而启动那条路的第一道门是**凭据**（onStartButtonClicked
        // 在拿不到令牌时弹窗返回，Java 那一步根本走不到）。 之前这里是在每个
        // 窗口里勾一下主页那个「离线模式」勾选框；那个勾选框去掉了（要求：「不要启动
        // 界面那个离线登录勾选框了，在添加账户那里选择添加离线账号」），于是"这一趟走
        // 离线"只剩一个出处：**当前账号本身是不是离线账号**（MainWindow::offlineIdentity()）。
        //
        // 所以这里照产品那条路摆一次现场（MainWindow::onOfflineAccountRequested 落到的
        // 就是 AccountStore::addOffline），整段都在离线身份下跑 —— 不联网、不要凭据。
        // 收拾的事不用管：Z 段一开头就把这几个键清干净了。
        //
        // NOTE: 从这一句起直到 Z 段把账号清掉，**每一个 `MainWindow w;` 都是在离线身份下
        // 起来的**，构造函数末尾那句 startPlayerSkinLoad() 会走"离线账号要随机皮肤"
        // 那条路。所以外观必须在建账号**之前**预置好（预置 = 盘上记一张 + 缓存里有
        // 图本体 → 那一路变成同步命中，一个包都不发）。否则这里会真去问 MineSkin，
        // 而且回来的东西会写日志、换掉屏幕上的脸 —— 见 seedOfflineRandomSkin 那段。
        seedOfflineRandomSkin(QStringLiteral("mwcheck"), kSeededSkinUrl,
                              QColor(0x30, 0x90, 0xD0));
        {
            QString offlineError;
            check(AccountStore::addOffline(QStringLiteral("mwcheck"), &offlineError),
                  "U 前提：建一个离线账号并把它设成当前身份（启动那条路才不要凭据）",
                  offlineError);
        }

        // 造一个专用实例：真的 version.json（好让启动路径走到 Java 那一步）、一个
        // stub jar（checkPreLaunch 要求有客户端 jar）、一份覆盖。
        const QString id = QStringLiteral("覆盖测试");
        const QDir instanceDir(versionsDir.filePath(id));
        instanceDir.mkpath(QStringLiteral("."));
        writeFileAt(instanceDir.filePath(id + QStringLiteral(".jar")), QByteArrayLiteral("stub"));
        writeFileAt(instanceDir.filePath(id + QStringLiteral(".json")),
                    QByteArrayLiteral("{\"id\":\"覆盖测试\",\"libraries\":[],"
                                      "\"javaVersion\":{\"majorVersion\":17}}"));
        // 覆盖里钉的是一个**不存在**的 Java 路径。实例覆盖了 javaPath 就等于手动档，
        // 手动档下 ensureJavaForVersion() 立刻失败 —— 既不扫本机（几百毫秒）也不下载
        // （要网络），所以这一节能离线、能重复跑。而它验的正是"实例钉的路径挡得住
        // 全局那个「自动」档"：要是 allowAutoSelect 判错了，这里就会去自动挑一个，
        // 弹窗和断言全都不一样。
        const QString metaPath = instanceDir.filePath(QStringLiteral("wtfml.meta"));
        writeFileAt(metaPath,
                    QByteArrayLiteral("{\"javaPath\":\"C:/没有这个目录/java.exe\","
                                      "\"xmx\":2048,\"autoMemory\":true}"));
        // checkPreLaunch（排在 Java 那一步之前）要求这两样存在
        const QDir mcDir(root.filePath(QStringLiteral(".minecraft")));
        const bool hadLibraries = QDir(mcDir.filePath(QStringLiteral("libraries"))).exists();
        const bool hadAssets = QDir(mcDir.filePath(QStringLiteral("assets"))).exists();
        mcDir.mkpath(QStringLiteral("libraries/占位"));
        mcDir.mkpath(QStringLiteral("assets"));

        const QString iniPath = settingsDir + QStringLiteral("/WTFML-check/mw_check.ini");
        QByteArray iniBefore;
        {
            MainWindow w;
            w.show();
            QApplication::processEvents();
            QComboBox *combo = comboByObjectName(&w, MainWindowUI::installedVersionComboName());
            QMetaObject::invokeMethod(&w, "onVersionsFetched", Qt::DirectConnection,
                                      Q_ARG(QStringList, QStringList({id})));
            QApplication::processEvents();
            check(combo != nullptr && combo->findData(id) >= 0,
                  "U1 那个实例进了「已安装版本」下拉框");
            if (combo != nullptr) {
                combo->setCurrentIndex(combo->findData(id));
            }
            QApplication::processEvents();
            check(startButton(&w) != nullptr && combo != nullptr && selectedId(combo) == id,
                  "U1 前提：选中了它（启动按钮指着这个实例）",
                  combo != nullptr ? selectedId(combo) : QString());

            // 基线：全局那份 ini 和实例那份 meta 的字节都记下来，点完之后逐字节比对。
            iniBefore = readBytesAt(iniPath);
            const QByteArray metaBefore = readBytesAt(metaPath);
            check(!iniBefore.isEmpty(), "U1 前提：沙盒那份 ini 里有东西可比",
                  QString::number(iniBefore.size()));

            dismissNextPrompt();
            QMetaObject::invokeMethod(&w, "onStartButtonClicked", Qt::DirectConnection);
            QApplication::processEvents();

            check(!g_modalText.isEmpty(), "U1 实例钉的 Java 用不了 → 拦下并弹窗", g_modalText);
            check(g_modalWasWarning, "U1 而且是警告窗，不是信息窗");
            check(g_modalText.contains(QStringLiteral("实例设置…")) &&
                      g_modalText.contains(QStringLiteral("跟随全局")),
                  "U1 弹的是**实例版**那句（指向「实例设置…」和「跟随全局」）", g_modalText);
            check(!g_modalText.contains(QStringLiteral("请手动指定 Java 路径")),
                  "U1 不是全局那一句（那句说的是「你根本没设过」）", g_modalText);

            // 这一条是整个 U 段的重心：一次失败的启动**不许**在全局留下任何东西。
            check(readBytesAt(iniPath) == iniBefore,
                  "U1 全局配置一个字节都没变（这次启动没往全局写过东西）");
            check(readBytesAt(metaPath) == metaBefore,
                  "U1 实例自己的 meta 也没被动（失败不留半截写入）",
                  QString::fromUtf8(readBytesAt(metaPath)));
        }

        // ---- U2 反方向：实例里的值也不许渗进全局界面 ----
        // 全局设成手动档 + 一个特别的 Xmx（3072），实例那份覆盖里写的是 2048。
        {
            GlobalJavaSettings global;
            global.setAutoMemory(false);
            global.setXmx(3072);
            global.setXms(1024);
            global.save();
        }
        {
            MainWindow w;
            w.show();
            QApplication::processEvents();
            QSpinBox *maxSpin = byObjectName<QSpinBox>(&w, MainWindowUI::memoryMaxSpinName());
            QSlider *maxSlider = byObjectName<QSlider>(&w, MainWindowUI::memoryMaxSliderName());
            QLabel *hint = byObjectName<QLabel>(&w, MainWindowUI::memoryHintName());
            check(maxSpin != nullptr && maxSpin->value() == 3072,
                  "U2 设置页那个框显示的是全局那份（3072），不是某个实例覆盖的 2048",
                  maxSpin != nullptr ? QString::number(maxSpin->value()) : QString());
            check(maxSlider != nullptr && maxSpin != nullptr &&
                      MemorySlider::valueSteps(maxSpin->minimum(), maxSpin->maximum())
                              .value(maxSlider->value(), -1) == 3072,
                  "U2 那根条也停在 3072 那一格上（条和数字框是同一个数的两种说法）",
                  maxSlider != nullptr ? QString::number(maxSlider->value()) : QString());
            // 最小内存那一格撤掉之后，Xms 只剩"说明文字里那一句"看得见 —— 所以这里改成
            // 从说明文字上认它（1024 是全局那份，实例覆盖里写的是别的）。
            check(hint != nullptr && hint->text().contains(QStringLiteral("1024 MB")),
                  "U2 最小内存同理（它现在只在这一句说明文字里露面）",
                  hint != nullptr ? hint->text() : QString());
            GlobalJavaSettings probe;
            check(probe.xmx() == 3072 && !probe.autoMemory(),
                  "U2 盘上那份也没被实例的值改过（另一个读者读回来的）",
                  QStringLiteral("%1 / %2").arg(probe.xmx()).arg(probe.autoMemory()));
        }

        // ---- U4～U7 「实例钉的 Java 和版本要求对不上」那个三岔口 ----
        //
        // 这一段以前**证不到**（U 段开头那条 NOTE 说的就是它）：要让 Java 那一步成功，
        // 机器上得真装着一个匹配的运行时，还得真的起一个 JVM。有"假运行时"这个办法之后
        // 就不用付那个代价了：
        //
        // 一个**空文件**当 java.exe，旁边一个 release 文件写着 JAVA_VERSION。
        // GlobalJavaSettings::probeJavaMajorVersion() 走的是"先读 release、读到就 return"
        // 那条快路径（这正是它在产品里快的原因），所以版本是从文件里读出来的、进程一次
        // 都没起过 —— 而且那条路正是 scanLocalJavaVersions() 认版本用的同一条，
        // 于是假运行时能被真的"扫到"。
        //
        // fake17 一定会在 scanLocalJavaVersions() 里胜出：那个函数先扫 AppPaths::javaDir()
        // （就是这里），原版启动器那份运行时只在"这个主版本还没有"时才补位（first-wins）。
        // 所以「改用匹配的 Java」挑中的必然是 fake17 —— 那一次启动也就完全离线，
        // 而且不依赖这台机器上装了哪些 Java。
        const QString javaDir = root.filePath(QStringLiteral(".minecraft/java"));
        const QString fake8Exe = javaDir + QStringLiteral("/fake8/bin/java.exe");
        const QString fake17Exe = javaDir + QStringLiteral("/fake17/bin/java.exe");
        writeFileAt(fake8Exe, QByteArray());
        writeFileAt(javaDir + QStringLiteral("/fake8/release"),
                    QByteArrayLiteral("JAVA_VERSION=\"1.8.0_301\"\n"));
        writeFileAt(fake17Exe, QByteArray());
        writeFileAt(javaDir + QStringLiteral("/fake17/release"),
                    QByteArrayLiteral("JAVA_VERSION=\"17.0.1\"\n"));
        check(GlobalJavaSettings::probeJavaMajorVersion(fake8Exe) == 8 &&
                  GlobalJavaSettings::probeJavaMajorVersion(fake17Exe) == 17,
              "U4 前提：两个假运行时都认得出主版本（读 release 就够了，一次进程都没起）");

        // 专用实例：version.json 要的是 Java 17，而实例钉的是 fake8 —— 正是"对不上"。
        // meta 里还塞了一个无关的键（当年是 autoMemory， 起读侧已经不认它了，
        // 见 EffectiveJavaSettings.cpp）：它现在的用处是钉住"合并写"——「改用匹配的 Java」
        // 往 meta 里只该改 javaPath 那一项，别的键一个都不许丢。
        const QString remindId = QStringLiteral("版本提醒测试");
        const QDir remindDir(versionsDir.filePath(remindId));
        remindDir.mkpath(QStringLiteral("."));
        writeFileAt(remindDir.filePath(remindId + QStringLiteral(".jar")), QByteArrayLiteral("stub"));
        writeFileAt(remindDir.filePath(remindId + QStringLiteral(".json")),
                    QByteArrayLiteral("{\"id\":\"版本提醒测试\",\"libraries\":[],"
                                      "\"javaVersion\":{\"majorVersion\":17}}"));
        const QString remindMetaPath = remindDir.filePath(QStringLiteral("wtfml.meta"));
        const QByteArray pinnedToFake8 =
            QByteArrayLiteral("{\"javaPath\":\"") + fake8Exe.toUtf8() +
            QByteArrayLiteral("\",\"autoMemory\":true}");
        const auto writeRemindMeta = [&remindMetaPath](const QByteArray &bytes) {
            writeFileAt(remindMetaPath, bytes);
        };

        // 每次启动都从同一份"钉着 fake8"的 meta 开始，并且把全局总闸设成想要的状态。
        const auto setGlobalWarn = [](bool warn) {
            GlobalJavaSettings g;
            g.setWarnVersionMismatch(warn);
            g.save();
        };
        // 开窗 + 选中这个实例。和 U1 那段一样的路子（离线身份在这一段开头就摆好了，
        // 不用再在这里动什么）。
        const auto prepare = [&](MainWindow &w) {
            w.show();
            QApplication::processEvents();
            QMetaObject::invokeMethod(&w, "onVersionsFetched", Qt::DirectConnection,
                                      Q_ARG(QStringList, QStringList({remindId})));
            QApplication::processEvents();
            auto *combo = comboByObjectName(&w, MainWindowUI::installedVersionComboName());
            if (combo != nullptr) {
                combo->setCurrentIndex(combo->findData(remindId));
            }
            QApplication::processEvents();
        };

        // ---- U4 「仍然用这个 Java 启动」：一个字节都不改 ----
        {
            writeRemindMeta(pinnedToFake8);
            setGlobalWarn(true);
            MainWindow w;
            prepare(w);
            const QByteArray iniBeforeLaunch = readBytesAt(iniPath);
            const QByteArray metaBefore = readBytesAt(remindMetaPath);

            answerJavaMismatchDialog(JavaMismatchDialog::continueButtonName(), false);
            QMetaObject::invokeMethod(&w, "onStartButtonClicked", Qt::DirectConnection);
            QApplication::processEvents();

            // 这里原来断言"一个框都没弹"，因为进程起不来那条路以前只写日志。
            // 现在产品会**弹一条「无法启动」**（见 LaunchController 里那份 processError 的
            // 接法）—— 假的 java.exe 是个**空文件**，CreateProcess 当然起不来，所以说的是
            // 「不是有效的 Win32 应用程序」。这一节真正要钉住的是"**不该**落到
            // `Java 运行时缺失` 那条硬错误上"，不是"一个框都不许有"。
            check(!g_modalText.contains(QStringLiteral("Java 运行时"))
                      && !g_modalText.contains(QStringLiteral("请手动指定 Java 路径")),
                  "U4 没有弹「Java 运行时缺失」（走了新那条路，不该再落到硬错误上）",
                  g_modalTitle + QStringLiteral(" / ") + g_modalText);
            check(g_modalText.contains(QStringLiteral("无法启动游戏进程")),
                  "U4 进程起不来那一条现在是**弹出来**的（假 java.exe 是个空文件，这句正是用户该看到的）",
                  g_modalText);
            check(!g_mismatchLabel.isEmpty(), "U4 弹了三岔口那个框", g_mismatchLabel);
            check(g_mismatchLabel.contains(QStringLiteral("版本提醒测试")) &&
                      g_mismatchLabel.contains(QStringLiteral("Java 8")) &&
                      g_mismatchLabel.contains(QStringLiteral("Java 17")),
                  "U4 框里写清了是哪个实例、他钉的是哪一版、这个版本要哪一版");
            check(g_mismatchLabel.contains(fake8Exe), "U4 也露出了那条路径");
            check(readBytesAt(remindMetaPath) == metaBefore,
                  "U4 「仍然用这个 Java 启动」没有改实例设置（一个字节都没动）",
                  QString::fromUtf8(readBytesAt(remindMetaPath)));
            check(readBytesAt(iniPath) == iniBeforeLaunch,
                  "U4 全局配置也没被动（实例钉的那条路径不许写进全局）");
        }

        // ---- U5 「以后不再提醒这个实例」：写进 meta，下次不再问 ----
        {
            writeRemindMeta(pinnedToFake8);
            setGlobalWarn(true);
            MainWindow w;
            prepare(w);

            answerJavaMismatchDialog(JavaMismatchDialog::continueButtonName(),
                                     /*checkDontRemind=*/true);
            QMetaObject::invokeMethod(&w, "onStartButtonClicked", Qt::DirectConnection);
            QApplication::processEvents();
            check(!g_mismatchLabel.isEmpty(), "U5 前提：这一次还是问了");

            const QJsonObject meta =
                QJsonDocument::fromJson(readBytesAt(remindMetaPath)).object();
            const QJsonValue flag = meta.value(QStringLiteral("skipJavaVersionWarning"));
            check(flag.isBool() && flag.toBool(),
                  "U5 「以后不再提醒」写进了这个实例的 meta",
                  QString::fromUtf8(readBytesAt(remindMetaPath)));
            check(meta.value(QStringLiteral("javaPath")).toString() == fake8Exe,
                  "U5 而且没有顺手改掉他钉的那条路径",
                  meta.value(QStringLiteral("javaPath")).toString());

            // 同一个实例、同样的局面，再点一次启动：这次**不许再问**。
            answerJavaMismatchDialog(JavaMismatchDialog::continueButtonName(), false);
            QMetaObject::invokeMethod(&w, "onStartButtonClicked", Qt::DirectConnection);
            QApplication::processEvents();
            // 同上：这一趟依旧会在后面弹「无法启动」（空文件当 java.exe），要钉的是
            // **Java 版本那个框**不再出现。
            check(g_mismatchLabel.isEmpty() && !g_modalText.contains(QStringLiteral("Java 运行时")),
                  "U5 记住之后第二次启动不再弹 Java 版本那个框",
                  g_mismatchLabel + QStringLiteral(" / ") + g_modalText);
        }

        // ---- U6 「改用匹配的 Java 启动」：换掉的是**实例**设置，不是全局 ----
        {
            writeRemindMeta(pinnedToFake8);
            setGlobalWarn(true);
            MainWindow w;
            prepare(w);
            const QByteArray iniBeforeLaunch = readBytesAt(iniPath);

            answerJavaMismatchDialog(JavaMismatchDialog::useMatchingButtonName(), false);
            QMetaObject::invokeMethod(&w, "onStartButtonClicked", Qt::DirectConnection);
            QApplication::processEvents();

            check(!g_mismatchLabel.isEmpty(), "U6 前提：问了");
            // 「挑 Java」那一步成功的判据是**没有 Java 运行时缺失**；后面那条「无法启动」
            // 是假的 java.exe 本身（空文件）起不来，属于这一节故意接受的代价。
            check(!g_modalText.contains(QStringLiteral("Java 运行时")),
                  "U6 挑 Java 那一步没出错",
                  g_modalTitle + QStringLiteral(" / ") + g_modalText);
            const QJsonObject meta =
                QJsonDocument::fromJson(readBytesAt(remindMetaPath)).object();
            const QString now = meta.value(QStringLiteral("javaPath")).toString();
            check(now == fake17Exe,
                  "U6 实例的 javaPath 换成了匹配的那一个（fake17 是本机唯一那个 17）", now);
            check(meta.value(QStringLiteral("autoMemory")).toBool(),
                  "U6 覆盖层里别的项一个都没丢（是合并写，不是整份重写）",
                  QString::fromUtf8(readBytesAt(remindMetaPath)));
            check(readBytesAt(iniPath) == iniBeforeLaunch,
                  "U6 全局配置一个字节都没变 —— 自动挑中的路径写回的是**这个实例**");
            check(!QFile::exists(javaDir + QStringLiteral("/17")),
                  "U6 也没有在 java 目录里新下一个运行时（挑中的是本机已有的那个）");

            // 换完之后版本就对上了：再启动一次不该再问。
            answerJavaMismatchDialog(JavaMismatchDialog::continueButtonName(), false);
            QMetaObject::invokeMethod(&w, "onStartButtonClicked", Qt::DirectConnection);
            QApplication::processEvents();
            check(g_mismatchLabel.isEmpty(), "U6 换过来之后再启动就不问了（版本已经对上）");
        }

        // ---- U7 全局总闸关掉：不问，直接按他钉的那个启动 ----
        // 关掉总闸**不是**"不问但仍然拦下" —— 那种读法会把用户唯一的出路堵死
        // （手动档 + 路径不匹配 = 直接 fail），这个开关就成了纯负作用。
        {
            writeRemindMeta(pinnedToFake8);
            setGlobalWarn(false);
            MainWindow w;
            prepare(w);
            const QByteArray iniBeforeLaunch = readBytesAt(iniPath);
            const QByteArray metaBefore = readBytesAt(remindMetaPath);

            answerJavaMismatchDialog(JavaMismatchDialog::continueButtonName(), false);
            QMetaObject::invokeMethod(&w, "onStartButtonClicked", Qt::DirectConnection);
            QApplication::processEvents();

            check(g_mismatchLabel.isEmpty() && !g_modalText.contains(QStringLiteral("Java 运行时")),
                  "U7 总闸关着就不弹框（连「Java 运行时缺失」那条硬错误都不该有）",
                  g_mismatchLabel + QStringLiteral(" / ") + g_modalText);
            check(readBytesAt(remindMetaPath) == metaBefore,
                  "U7 实例设置没被动（他钉的还是他钉的那个）");
            check(readBytesAt(iniPath) == iniBeforeLaunch, "U7 全局配置也没被动");
        }

        // 收拾：假运行时和专用实例都不能留在沙盒里 —— 后者会被后面 O 节数进"沙盒里有
        // 几个版本"，前者会让**下一次跑**的 scanLocalJavaVersions() 多出两个候选。
        QDir(javaDir + QStringLiteral("/fake8")).removeRecursively();
        QDir(javaDir + QStringLiteral("/fake17")).removeRecursively();
        QDir(remindDir.absolutePath()).removeRecursively();
        check(QDir(javaDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(),
              "U8 两个假运行时删干净了",
              QDir(javaDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).join(QStringLiteral(" | ")));
        check(!remindDir.exists(), "U8 版本提醒测试那个实例的目录也删干净了");

        // 收拾：ini 恢复成 U 之前的样子 —— 后面 O 节虽然只数版本，但留一份动过的全局
        // 配置在沙盒里，迟早会被下一个开窗的人撞上。写回必须在上面那些窗口**都析构之后**
        // （QSettings 是析构时才落盘的，早写会被它盖回去）。
        writeFileAt(iniPath, iniBefore);
        check(readBytesAt(iniPath) == iniBefore, "U8 沙盒那份 ini 恢复原样了");

        QDir(instanceDir.absolutePath()).removeRecursively();
        check(!instanceDir.exists(), "U8 专用实例的目录删干净了");
        if (!hadLibraries) {
            QDir(mcDir.filePath(QStringLiteral("libraries"))).removeRecursively();
        }
        if (!hadAssets) {
            QDir(mcDir.filePath(QStringLiteral("assets"))).removeRecursively();
        }
    }
    return 0;
}


