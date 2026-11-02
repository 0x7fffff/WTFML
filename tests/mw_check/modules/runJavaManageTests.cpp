#include "Support.hpp"
#include "controllers/JavaSettingsController.hpp"
#include "controllers/JavaRuntimeController.hpp"
#include <QSemaphore>
#include <atomic>
#include <memory>
void runJavaManageTests() {
    {
        struct ScanState { QSemaphore started, release; std::atomic<int> calls{0}, done{0}; };
        const auto state = std::make_shared<ScanState>();
        QMainWindow window;
        MainWindowUI ui(&window);
        GlobalJavaSettings settings;
        settings.setJavaPath(QString());
        JavaRuntimeController runtime;
        JavaSettingsController::Host host;
        host.scanJava = [state](const QString &) {
            ++state->calls;
            state->started.release();
            state->release.tryAcquire(1, 3000);
            ++state->done;
            return QMap<int, QString>{{17, QStringLiteral("fixture-java")}};
        };
        int finished = 0;
        bool heartbeat = false;
        auto controller = std::make_unique<JavaSettingsController>(&ui, &settings, &runtime, &window, host);
        QObject::connect(controller.get(), &JavaSettingsController::discoveryFinished, &window, [&] { ++finished; });
        controller->refreshList();
        check(controller->discoveryPending() && state->started.tryAcquire(1, 1000),
              "T26.async 首次扫描已排到后台，刷新立即返回");
        QTimer::singleShot(0, &window, [&] { heartbeat = true; });
        QApplication::processEvents();
        check(heartbeat && finished == 0, "T26.async 扫描尚未返回时界面事件仍可处理");
        controller->refreshList();
        controller->refreshList();
        check(state->calls == 1, "T26.async 重复刷新不重复扫描");
        controller->resetDiscovery();
        state->release.release();
        const auto waitForWorker = [&](int count) {
            QElapsedTimer timer; timer.start();
            while (state->done < count && timer.elapsed() < 3000) {
                QApplication::processEvents(); QThread::msleep(1);
            }
            return state->done >= count;
        };
        check(waitForWorker(1), "T26.async 作废扫描正常退出");
        QApplication::processEvents();
        check(finished == 0 && !controller->discoveryPending(), "T26.async 换目录作废后旧回调不发布结果");
        state->release.release();
        controller->refreshList();
        QElapsedTimer wait;
        wait.start();
        while (controller->discoveryPending() && wait.elapsed() < 3000) {
            QApplication::processEvents(); QThread::msleep(1);
        }
        check(finished == 1 && state->calls == 2, "T26.async 重置后的新扫描可以发布并缓存");
        state->started.tryAcquire(); // 清掉第二轮通知。
        controller->resetDiscovery();
        controller->refreshList();
        check(state->started.tryAcquire(1, 1000), "T26.async 析构验收的扫描正在后台运行");
        controller.reset();
        state->release.release();
        check(waitForWorker(3), "T26.async 控制器销毁后工人可安全结束");
        QApplication::processEvents();
        check(finished == 1, "T26.async 销毁后不向旧页面发布回调");
    }
    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir root;
    check(root.isValid(), "T26.0 临时数据根目录建出来了");
    if (!root.isValid()) {
        return;
    }

    QSettings sandbox;
    const QVariant foldersBefore = sandbox.value(QStringLiteral("Launcher/Folders"));
    const QVariant currentBefore = sandbox.value(QStringLiteral("Launcher/CurrentFolder"));
    const QVariant javaPathBefore = sandbox.value(QStringLiteral("Java/Path"));
    const QVariant javaAutoBefore = sandbox.value(QStringLiteral("Java/AutoSelect"));
    const auto restoreSettings = [&] {
        sandbox.setValue(QStringLiteral("Launcher/Folders"), foldersBefore);
        sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), currentBefore);
        sandbox.setValue(QStringLiteral("Java/Path"), javaPathBefore);
        sandbox.setValue(QStringLiteral("Java/AutoSelect"), javaAutoBefore);
        sandbox.sync();
        AppPaths::setMinecraftDir(originalDir);
    };
    sandbox.setValue(QStringLiteral("Launcher/Folders"), QStringList({root.path()}));
    sandbox.setValue(QStringLiteral("Launcher/CurrentFolder"), root.path());
    // 那两个 Java 键**删掉**（不是写个假值）：这一节要从"从没配过的用户"那一档出发 ——
    // 「自动」那个默认值唯一量得到的时刻就是它从没被写过的时候。
    sandbox.remove(QStringLiteral("Java/Path"));
    sandbox.remove(QStringLiteral("Java/AutoSelect"));
    sandbox.sync();
    AppPaths::setMinecraftDir(root.path());

    // 造一份"装在游戏文件夹里"的运行时。
    //
    // **不摆 java.exe 的真身**：认主版本走的是它旁边那个 release 文件
    // （`JAVA_VERSION="17.0.12"`，见 GlobalJavaSettings::probeJavaMajorVersion），
    // 一个字节都不会去跑它 —— 所以这一步既不需要这台机器上真装着 Java，也不会冷启 JVM。
    const auto plantRuntime = [&](int major) {
        const QString dir = root.path() + QStringLiteral("/java/") + QString::number(major);
        QDir().mkpath(dir + QStringLiteral("/bin"));
        QFile exe(dir + QStringLiteral("/bin/java.exe"));
        if (exe.open(QIODevice::WriteOnly)) {
            exe.write("fake");
            exe.close();
        }
        QFile release(dir + QStringLiteral("/release"));
        if (release.open(QIODevice::WriteOnly | QIODevice::Text)) {
            release.write(QByteArrayLiteral("JAVA_VERSION=\"") + QByteArray::number(major) +
                          QByteArrayLiteral(".0.12\"\n"));
            release.close();
        }
    };
    // 路径按 `scanLocalJavaVersions()` 那种拼法拼（`dir.absoluteFilePath(subdir +
    // "/bin/java.exe")`）—— 换个拼法（比如手写反斜杠）量出来的是"字符串不一样"，不是产品的事。
    const auto sandboxJava = [&](int major) {
        return QDir(root.path()).absoluteFilePath(
            QStringLiteral("java/%1/bin/java.exe").arg(major));
    };
    plantRuntime(17);
    const QString java17 = sandboxJava(17);

    {
        MainWindow w;
        w.show();
        QApplication::processEvents();

        MainWindowUI *ui = w.ui();
        OverlayHost *overlay = ui != nullptr ? ui->overlay() : nullptr;
        QTabWidget *tabs = ui != nullptr ? ui->getTabWidget() : nullptr;
        QPushButton *javaManage =
            byObjectName<QPushButton>(&w, MainWindowUI::javaManageButtonName());
        QLabel *javaLabel = byObjectName<QLabel>(&w, MainWindowUI::javaPathLabelName());
        check(overlay != nullptr && tabs != nullptr && javaManage != nullptr &&
                  javaLabel != nullptr,
              "T26.1 覆盖层 / 选项卡 / 那颗按钮 / 设置页那行路径都在");
        if (overlay == nullptr || tabs == nullptr || javaManage == nullptr ||
            javaLabel == nullptr) {
            restoreSettings();
            return;
        }
        // 那句「Java 管理…」是**设置页那一层**的字面量（ui/MainWindowUI.cpp），ui/ 那边
        // 没有把它抽成函数，所以这儿照写一遍 —— 它断了就是"用户点名的那句话被人改了"，
        // 正是这一条要拦住的。
        check(javaManage->text() == QStringLiteral("Java 管理…"),
              "T26.1 那颗按钮上写的就是用户点名的那五个字", javaManage->text());
        // 「Java 版本」下拉框**不许**回来：留着它就有两处各自声称"这台机器用哪份
        // Java"，而那两处迟早对不上（见 MainWindowUI.hpp 那段）。
        check(w.findChild<QComboBox *>(QStringLiteral("javaVersionCombo")) == nullptr,
              "T26.1 老的那个下拉框一个都不剩（按它原来的 objectName 找）");

        // 这一页是**整页**，而且开页是**非阻塞**的（`OverlayHost::showPage()`）：点完那颗
        // 按钮 `openJavaManage()` 就返回了。所以量它得自己把事件转起来 —— 下面排的是一发
        // **反复重排**的定时器：哪一轮真看到那一页了再量，量完点页眉上那颗返回箭头收页。
        //
        // NOTE: **不能**用 `QTimer::singleShot(0, …)`：`openJavaManage()` 在切选项卡和
        // 摆页之前还有一句 `enterSyncWait()`，那一句自己会
        // `processEvents(ExcludeUserInputEvents)` —— 排出去的那一发会**在页面摆上
        // 去之前**就跑掉（那时候量到的是"没有这一页"）。
        bool probed = false;
        int ticks = 0;
        QTimer probe;
        probe.setInterval(10);
        QObject::connect(&probe, &QTimer::timeout, [&] {
            if (probed) {
                return;
            }
            ++ticks;
            OverlayHost *host = currentPageOverlay();
            auto *dialog = host != nullptr ? host->findChild<JavaManageDialog *>() : nullptr;
            if (dialog == nullptr) {
                // 兜底：这一页没开出来、屏幕上换成了别的（最可能是"先关掉现在这一页"那条
                // 提示），别把这一跑挂死在那儿。30 下 = 300 ms，够那一页摆上来了。
                if (ticks > 30) {
                    probe.stop();
                    if (OverlayHost *open = currentOverlay(); open != nullptr) {
                        open->closeOverlay();
                    }
                }
                return;
            }
            probed = true;
            probe.stop();

            // 开页后的扫描现在在后台线程跑。先等这一趟把结果灌回来再量四行；等待期间
            // Qt 事件循环照常工作，正是要验证的「界面不冻结」。
            QElapsedTimer initialScanWait;
            initialScanWait.start();
            auto *initialDetect =
                byObjectName<QPushButton>(dialog, JavaManageDialog::detectButtonName());
            while (initialDetect != nullptr && !initialDetect->isEnabled() &&
                   initialScanWait.elapsed() < 5000) {
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(2);
            }

            // ---------- 页眉 + 左上角那颗返回键 ----------
            check(host->pageTitle() == dialog->titleText() && !dialog->titleText().isEmpty(),
                  "T26.2 页眉上写着「Java 管理」", host->pageTitle());
            auto *back = host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName());
            check(back != nullptr && !back->isHidden(),
                  "★ T26.2 左上角那颗返回键露着 —— 这一页**自己不画**它，用户要的「左上角放一个"
                  "返回键」就是 OverlayHost 白送的那一颗（自己再摆一颗，屏幕上就有两个）");
            check(tabs->currentIndex() == MainWindowUI::settingsTabIndex(),
                  "T26.2 而且选项卡停在设置页上（这一页是从那儿开出来的，切走了它就整个收着）",
                  QStringLiteral("停在第 %1 颗").arg(tabs->currentIndex()));
            if (back != nullptr && host->width() > 0 && host->height() > 0) {
                const QPoint at = back->mapTo(host, QPoint(0, 0));
                check(at.x() < host->width() / 2 && at.y() < host->height() / 2,
                      "T26.2 而且它真在**左上角**（用户原话）",
                      QStringLiteral("(%1, %2)，内容区 %3x%4")
                          .arg(at.x())
                          .arg(at.y())
                          .arg(host->width())
                          .arg(host->height()));
            }

            // ---------- 顶上那一条「自动」 + 四行 ----------
            const QList<int> majors = JavaManageDialog::managedMajors();
            check(majors == QList<int>{25, 21, 17, 11, 8},
                  "T26.3 Java 管理包含 11，从新到旧排列",
                  QStringLiteral("%1 个").arg(majors.size()));
            auto *autoLabel = byObjectName<QLabel>(dialog, JavaManageDialog::autoLabelName());
            auto *autoUse =
                byObjectName<QPushButton>(dialog, JavaManageDialog::autoUseButtonName());
            check(autoLabel != nullptr && autoUse != nullptr,
                  "T26.3 顶上那一行「自动（按版本挑选）」和它那颗「选用」都在");
            check(autoLabel != nullptr &&
                      autoLabel->text() ==
                          JavaManageDialog::autoRowText() + JavaManageDialog::usedMarkText(),
                  "T26.3 这一趟是从「自动」出发的（沙盒里那两个键被删了 ⇒ 读出来就是默认那"
                  "一档），所以「（当前）」记号挂在它身上",
                  autoLabel == nullptr ? QString() : autoLabel->text());

            // 四行各自的三个控件。按**主版本号**去取（不是按下标）——行序将来再怎么挪，
            // 量到的还是那一行。
            QList<QLabel *> rowLabel;
            QList<QPushButton *> rowButton;
            QList<QPushButton *> rowUse;
            bool rowsFound = true;
            for (int major : majors) {
                QLabel *label =
                    byObjectName<QLabel>(dialog, JavaManageDialog::rowLabelName(major));
                QPushButton *button =
                    byObjectName<QPushButton>(dialog, JavaManageDialog::rowButtonName(major));
                QPushButton *use =
                    byObjectName<QPushButton>(dialog, JavaManageDialog::useButtonName(major));
                rowsFound = rowsFound && label != nullptr && button != nullptr && use != nullptr;
                rowLabel.append(label);
                rowButton.append(button);
                rowUse.append(use);
            }
            check(rowsFound,
                  "T26.3 四行各自的「版本：路径」+「选用」+「下载 / 重新下载」都找得到"
                  "（按 objectName 认 —— 那颗按钮的文字跟着装没装变，拿文字当身份会认错）");

            int onScreenMissing = 0;
            for (int i = 0; i < majors.size(); ++i) {
                QLabel *label = rowLabel.at(i);
                QPushButton *button = rowButton.at(i);
                QPushButton *use = rowUse.at(i);
                if (label == nullptr || button == nullptr || use == nullptr) {
                    continue;
                }
                // 装了没有**不能只看按钮文字**（那一格正是我们要验的东西）。所以反过来：
                // 先拿"这一行写的是不是「缺失」"判它装没装，再拿按钮文字 + 「选用」露不露
                // 去核对同一个判断 —— 三个来源对上了，这一行才是自洽的。
                const int major = majors.at(i);
                const bool missing =
                    label->text() == JavaManageDialog::rowText(major, QString());
                if (missing) {
                    ++onScreenMissing;
                }
                check(label->text().startsWith(JavaManageDialog::rowLabelText(major)) &&
                          button->text() == JavaManageDialog::rowButtonText(!missing) &&
                          use->isHidden() == missing,
                      QStringLiteral("T26.3 Java %1 那一行自洽：缺失 ⇒「下载」+ 没有「选用」；"
                                     "装了 ⇒「重新下载」+ 露着「选用」")
                          .arg(major),
                      label->text() + QStringLiteral(" / ") + button->text());
            }

            // 下面几条只看 Java 17 那一行：那一份是**这一节自己摆进沙盒的**
            // （`plantRuntime(17)`），扫盘一定认得出它 —— 另外三行装没装看这台机器，
            // 这一行不看这台机器上的任何东西。
            const int i17 = majors.indexOf(17);
            check(i17 >= 0 && rowLabel.at(i17) != nullptr &&
                      rowLabel.at(i17)->text() == JavaManageDialog::rowText(17, java17),
                  "★★ T26.4 沙盒里那份 Java 17 **认出来了**，而且路径是**原样写出来**的"
                  "（用户原话「Java21: C:\\...」）",
                  i17 < 0 || rowLabel.at(i17) == nullptr ? QString()
                                                         : rowLabel.at(i17)->text());
            check(i17 >= 0 && rowButton.at(i17) != nullptr &&
                      rowButton.at(i17)->text() == JavaManageDialog::rowButtonText(true),
                  "T26.4 装了的那一行，按钮写的是「重新下载」（**不是**「已安装」—— 这一页是"
                  "来**修**东西的：路径在、可那份打不开了，正是用户来这儿的原因）",
                  i17 < 0 || rowButton.at(i17) == nullptr ? QString()
                                                          : rowButton.at(i17)->text());

            // ---------- 「下载所有缺少的 Java」 ----------
            auto *downloadAll =
                byObjectName<QPushButton>(dialog, JavaManageDialog::downloadAllButtonName());
            check(downloadAll != nullptr &&
                      downloadAll->text() == JavaManageDialog::downloadAllText(),
                  "T26.4 「下载所有缺少的 Java」那颗键在，文字就是用户点名的那句",
                  downloadAll == nullptr ? QStringLiteral("<没有这颗键>")
                                         : downloadAll->text());
            check(downloadAll != nullptr && downloadAll->isEnabled() == (onScreenMissing > 0),
                  "T26.4 它亮不亮 = **屏幕上有没有写着「缺失」的行**（两边各算一遍：一边数"
                  "界面上的字，一边读这一页自己的判据 hasMissing()）",
                  QStringLiteral("屏幕上缺 %1 行，按钮 %2")
                      .arg(onScreenMissing)
                      .arg(downloadAll != nullptr && downloadAll->isEnabled()
                               ? QStringLiteral("亮着")
                               : QStringLiteral("灰着")));

            // ---------- 「检测已有 Java」：真的**重新扫盘** ----------
            auto *detect = byObjectName<QPushButton>(dialog, JavaManageDialog::detectButtonName());
            check(detect != nullptr && detect->text() == JavaManageDialog::detectText(),
                  "T26.5 「检测已有 Java」那颗键在，文字就是用户点名的那句",
                  detect == nullptr ? QStringLiteral("<没有这颗键>") : detect->text());

            // 判据只能是**先把盘上改掉、再按这颗键**。只按一下再看"和按之前一样"是假绿 ——
            // 一个什么都不做的实现也长那样。这一按只扫盘：不发请求、不弹窗、不写盘。
            for (int major : majors) {
                if (major != 17) plantRuntime(major);
            }
            if (detect != nullptr) {
                detect->click();
                QElapsedTimer detectionWait;
                detectionWait.start();
                while (!detect->isEnabled() && detectionWait.elapsed() < 5000) {
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(2);
                }
            }
            bool allReFound = true;
            QString reFoundDetail;
            for (int major : majors) {
                const int i = majors.indexOf(major);
                const bool ok =
                    i >= 0 && rowLabel.at(i) != nullptr && rowButton.at(i) != nullptr &&
                    rowUse.at(i) != nullptr &&
                    rowLabel.at(i)->text() == JavaManageDialog::rowText(major, sandboxJava(major)) &&
                    rowButton.at(i)->text() == JavaManageDialog::rowButtonText(true) &&
                    !rowUse.at(i)->isHidden();
                if (!ok) {
                    allReFound = false;
                    reFoundDetail = QStringLiteral("Java %1 那一行还是：%2 / %3")
                                        .arg(major)
                                        .arg(i >= 0 && rowLabel.at(i) != nullptr
                                                 ? rowLabel.at(i)->text()
                                                 : QStringLiteral("<没有那一行>"))
                                        .arg(i >= 0 && rowButton.at(i) != nullptr
                                                 ? rowButton.at(i)->text()
                                                 : QString());
                }
            }
            check(allReFound,
                  "★★ T26.5 补齐管理列表的运行时后再按「检测已有 Java」，各行当场变成"
                  "「重新下载」+ 露出「选用」（这一颗键真的重新扫了盘，不是拿开页那一刻那份"
                  "陈旧的扫描结果重画一遍）",
                  reFoundDetail);
            check(!dialog->hasMissing() && downloadAll != nullptr && !downloadAll->isEnabled(),
                  "★★ T26.5 管理列表各版本齐了 ⇒「下载所有缺少的 Java」变灰（一份都不缺时留着它可点，"
                  "就是让用户白点一下）");

            // ---------- 「选用」：这一页也是「选哪份 Java」那把钥匙 ----------
            //
            // 那把钥匙是**全局手动指定 Java 的唯一入口**（见 JavaManageDialog.hpp 那段）
            // —— 这里验它真写进了盘。
            check(autoUse != nullptr && !autoUse->isEnabled(),
                  "T26.6 前提：现在在「自动」档上 ⇒ 它那颗「选用」是**灰的**（已经在用的那一"
                  "档，再点一次什么都不会变 —— 灰而不是藏：这一档确实存在、也确实选得动）");
            const int i21 = majors.indexOf(21);
            const QString java21 = sandboxJava(21);
            if (i21 >= 0 && rowUse.at(i21) != nullptr) {
                rowUse.at(i21)->click();  // 同步：只写设置，不联网
            }
            {
                // 换一个对象读回来（不是读界面、也不是读 MainWindow 里那一份）——
                // 这一条量的是**盘上**记了什么。
                GlobalJavaSettings reread;
                check(!reread.autoSelectJava() && reread.javaPath() == java21,
                      "★★ T26.6 点「Java 21」那一行的「选用」⇒ 盘上记的是「手动指定 + 那条"
                      "路径」（★ 这一条会 point 出改了内存忘了 save() 的那种 bug）",
                      QStringLiteral("AutoSelect=%1 path=%2")
                          .arg(reread.autoSelectJava() ? 1 : 0)
                          .arg(reread.javaPath()));
            }
            check(i21 >= 0 && rowLabel.at(i21) != nullptr &&
                      rowLabel.at(i21)->text() == JavaManageDialog::rowText(21, java21) +
                                                      JavaManageDialog::usedMarkText(),
                  "T26.6 那一行当场挂上「（当前）」记号",
                  i21 < 0 || rowLabel.at(i21) == nullptr ? QString()
                                                         : rowLabel.at(i21)->text());
            check(i21 >= 0 && rowUse.at(i21) != nullptr && !rowUse.at(i21)->isEnabled(),
                  "T26.6 而且已经在用的那一颗「选用」变灰（再点一次什么都不会变）");
            check(autoLabel != nullptr && autoLabel->text() == JavaManageDialog::autoRowText(),
                  "T26.6 「自动」那一行上的记号**摘掉了**（当前那一档只有一条）",
                  autoLabel == nullptr ? QString() : autoLabel->text());
            check(autoUse != nullptr && autoUse->isEnabled(), "T26.6 而它那颗「选用」又能点了");
            check(javaLabel->text() == QStringLiteral("手动指定：%1").arg(java21),
                  "★★ T26.6 设置页那行字跟着变成「手动指定：…」（这一页钉的是**全局**那一档，"
                  "不是某一个实例）",
                  javaLabel->text());

            // ---------- 再点回「自动」 ----------
            if (autoUse != nullptr) {
                autoUse->click();
            }
            check(i21 >= 0 && rowLabel.at(i21) != nullptr &&
                      rowLabel.at(i21)->text() == JavaManageDialog::rowText(21, java21),
                  "T26.7 点「自动」那一行的「选用」⇒ 记号从 Java 21 挪回上面那条",
                  i21 < 0 || rowLabel.at(i21) == nullptr ? QString()
                                                         : rowLabel.at(i21)->text());
            {
                GlobalJavaSettings reread;
                check(reread.autoSelectJava() && reread.javaPath() == java21,
                      "★★ T26.7 盘上记的是「自动」，而**上次挑中的那条路径原样留着**"
                      "（那是缓存、不是用户钉的 —— 清掉的话每次启动都要重扫一遍本机 Java）",
                      QStringLiteral("AutoSelect=%1 path=%2")
                          .arg(reread.autoSelectJava() ? 1 : 0)
                          .arg(reread.javaPath()));
            }
            check(javaLabel->text() == QStringLiteral("自动：上次用的是 %1").arg(java21),
                  "T26.7 设置页那行字也跟着变回来", javaLabel->text());
            check(autoLabel != nullptr &&
                      autoLabel->text() ==
                          JavaManageDialog::autoRowText() + JavaManageDialog::usedMarkText(),
                  "T26.7 「（当前）」记号回到「自动」那一条上");

            // ---------- 「手动指定 java.exe…」：扫描认不出来的那一份 ----------
            //
            // 要的是一份**在扫描目录之外**的 Java。沙盒里 java/<主版本> 那几份都是扫描
            // 认得的，所以另起一棵树；主版本故意选 25 —— 沙盒里也有一份扫描认得的 25，
            // 正好量"手动那条要盖过扫描那条"。
            const QString externalRoot = root.path() + QStringLiteral("/tools/jdk-25");
            QDir().mkpath(externalRoot + QStringLiteral("/bin"));
            QFile externalExe(externalRoot + QStringLiteral("/bin/java.exe"));
            if (externalExe.open(QIODevice::WriteOnly)) {
                externalExe.write("fake");
                externalExe.close();
            }
            // 认主版本只读 release（见 GlobalJavaSettings::probeJavaMajorVersion）：
            // 一个字节都不会去跑那个假 exe，所以这一条不依赖这台机器真装着 Java。
            QFile externalRelease(externalRoot + QStringLiteral("/release"));
            if (externalRelease.open(QIODevice::WriteOnly | QIODevice::Text)) {
                externalRelease.write("JAVA_VERSION=\"25.0.1\"\n");
                externalRelease.close();
            }
            const QString externalJava = externalRoot + QStringLiteral("/bin/java.exe");
            auto *controller = w.findChild<JavaSettingsController *>();
            const int i25 = majors.indexOf(25);
            check(controller != nullptr && i25 >= 0 && rowLabel.at(i25) != nullptr,
                  "T26.8 前提：主窗口里那个 Java 控制器和 Java 25 那一行都在");
            if (controller != nullptr && i25 >= 0 && rowLabel.at(i25) != nullptr) {
                controller->setManualJava(externalJava);
                const bool pinnedToExternal = pumpUntil(
                    [&] {
                        return javaLabel->text() ==
                               QStringLiteral("手动指定：%1").arg(externalJava);
                    },
                    3000);
                check(pinnedToExternal,
                      "★★ T26.8 手动挑一份扫描不认识的 java.exe ⇒ 设置页那行当场变成"
                      "「手动指定：…」（选文件那个模态框测试驱动不了，这里直接驱动"
                      "setManualJava —— 框后面的逻辑一条不少）",
                      javaLabel->text());
                {
                    GlobalJavaSettings reread;
                    check(!reread.autoSelectJava() && reread.javaPath() == externalJava,
                          "★★ T26.8 盘上记的是「手动指定 + 那条路径」（再换一个对象读回来 —"
                          "— 这一条会 point 出改了内存忘了 save() 的那种 bug）",
                          QStringLiteral("AutoSelect=%1 path=%2")
                              .arg(reread.autoSelectJava() ? 1 : 0)
                              .arg(reread.javaPath()));
                }
                check(rowLabel.at(i25)->text() ==
                          JavaManageDialog::rowText(25, externalJava) +
                              JavaManageDialog::usedMarkText(),
                      "★ T26.8 手动那份盖在扫描结果上面（同一主版本里扫描认得的那条不能把它"
                      "挤掉），并且挂着「（当前）」记号",
                      rowLabel.at(i25)->text());

                // 选一条不存在的路径：弹一句、一个字节都不写。
                const QString pathBefore = GlobalJavaSettings().javaPath();
                bool warned = false;
                QTimer dismissWarning;
                dismissWarning.setInterval(10);
                QObject::connect(&dismissWarning, &QTimer::timeout, [&] {
                    if (InlineMessageBox *box = currentPrompt()) {
                        warned = true;
                        box->reject();
                    }
                });
                dismissWarning.start();
                controller->setManualJava(root.path() + QStringLiteral("/nope/bin/java.exe"));
                const bool sawWarning = pumpUntil([&] { return warned; }, 3000);
                dismissWarning.stop();
                GlobalJavaSettings afterBadPick;
                check(sawWarning && !afterBadPick.autoSelectJava() &&
                          afterBadPick.javaPath() == pathBefore,
                      "★ T26.8 选了一条不存在的路径 ⇒ 弹一句「没选到 java.exe」，盘上仍是"
                      "刚才那条（不许半途改一半）",
                      QStringLiteral("弹了=%1 path=%2")
                          .arg(warned ? 1 : 0)
                          .arg(afterBadPick.javaPath()));
            }

            // ---------- 收页：点左上角那颗返回箭头（就是上面验过的那一颗） ----------
            if (back != nullptr) {
                back->click();
                QApplication::processEvents();
            } else {
                host->dismiss();
            }
        });
        probe.start();
        javaManage->click();  // 非阻塞：点完就回来了，这一页由覆盖层自己收
        // 开页不再卡在嵌套循环里，所以在这里把事件转起来：等探针真看到那一页（它自己
        // 会量完并点返回箭头收页），再等这一层收干净。
        const bool sawPage = pumpUntil([&] { return probed; }, 5000);
        pumpUntil([&] { return !overlay->isOpen(); }, 5000);
        probe.stop();
        // 这一条是上面一整段的**前提**：没开出来的话里面一条都没跑 —— 那种"全绿"是假的。
        check(sawPage, "T26.2 那颗按钮真的把「Java 管理」那一页开出来了"
                       "（★ 这条要是红了，上面里面所有东西一条都没量到）");
        check(!overlay->isOpen(), "T26.9 点完那颗返回箭头，这一页收干净了");
    }

    restoreSettings();
    check(AppPaths::minecraftDir() == originalDir, "T26.9 数据根目录换回来了");
}

