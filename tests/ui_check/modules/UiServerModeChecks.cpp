#include "Support.hpp"
int UiServerModeChecks(QMainWindow &window, MainWindowUI &ui, QTabWidget * &tabs) {
    // ---------- U34 实例页顶部模式切换 + 服务端那半页（M4-1）----------
    //
    // 落到界面上的那一份：实例页正文面顶上多一行**模式切换**（客户端实例 | 服务端），
    // 下面那一整块跟着换页（`instanceModeStack`）—— 客户端那一页就是原来那张「文件夹 +
    // 实例树」，服务端那一页是一棵空的树 + 一句「还没有服务端」+ 一颗「新建服务端…」。
    // 这一节量的是**看得见的那一份**：两颗 chip 在不在 / 勾没勾对、换页换没换对、服务端
    // 那半页那三样控件在不在、那句空状态是不是约定的那一句；再加「新建服务端」那一页自身
    // 的形状（下拉框里那几档、按档露/收那三行、说明与「创建」能不能点）。
    //
    // NOTE: 接线（切模式真的重扫了盘、创建真的落了盘、双击真的开了目录）在 mw_check 的 T38；
    // 判据层（认档、洗名字、读写 `.wtfml-server`）在 verify 的 ZW / ZX 段。这一节
    // 只量"这一行长什么样"，不重验那两半。
    {
        window.show();
        window.resize(980, 780);
        if (tabs != nullptr) {
            tabs->setCurrentIndex(MainWindowUI::instancesTabIndex());
        }
        QApplication::processEvents();

        QPushButton *clientChip =
            ui.getInstanceModeButton(MainWindowUI::InstanceMode::Client);
        QPushButton *serverChip =
            ui.getInstanceModeButton(MainWindowUI::InstanceMode::Server);
        QPushButton *presetChip =
            ui.getInstanceModeButton(MainWindowUI::InstanceMode::Preset);
        QStackedWidget *modeStack = ui.getInstanceModeStack();
        QTreeWidget *serverTree = ui.getServerList();
        QLabel *serverEmpty = ui.getServerEmptyLabel();
        QPushButton *newServer = ui.getNewServerButton();
        QWidget *presetHost = ui.getInstancePresetPage();
        const bool modeThere = clientChip != nullptr && serverChip != nullptr &&
                               presetChip != nullptr && modeStack != nullptr &&
                               serverTree != nullptr && serverEmpty != nullptr &&
                               newServer != nullptr && presetHost != nullptr;
        check(modeThere,
              "U34.0 前提：模式行那三颗 chip / 换页容器 / 服务端那棵树 / 空状态那句 / "
              "「新建服务端…」/ 预设那一页的宿主都按 objectName 找得到");
        if (modeThere) {
            // ---------- ① 三颗 chip：文字、能亮/能灭、初始停在「客户端」 ----------
            check(clientChip->isCheckable() && serverChip->isCheckable() &&
                      presetChip->isCheckable() &&
                      clientChip->text() == MainWindowUI::instanceModeText(
                                                MainWindowUI::InstanceMode::Client) &&
                      serverChip->text() == MainWindowUI::instanceModeText(
                                                MainWindowUI::InstanceMode::Server) &&
                      presetChip->text() == MainWindowUI::instanceModeText(
                                                MainWindowUI::InstanceMode::Preset),
                  "★ U34.1 三颗模式 chip 的文字逐字是约定的「客户端 / 服务端 / 预设」，"
                  "而且都是「能亮/能灭」的那一档（它们要的是「正停在哪一边」这个状态）",
                  QStringLiteral("%1 / %2 / %3")
                      .arg(clientChip->text(), serverChip->text(), presetChip->text()));
            check(!clientChip->toolTip().isEmpty() && !serverChip->toolTip().isEmpty() &&
                      !presetChip->toolTip().isEmpty(),
                  "★ U34.1 三颗各自挂着一句说明（悬停能看出这半页各自管什么）");
            check(modeStack->count() == 3 && modeStack->currentIndex() == 0 &&
                      clientChip->isChecked() && !serverChip->isChecked() &&
                      !presetChip->isChecked(),
                  "★★ U34.2 起手停在「客户端」：换页容器的第 0 页 + 左边那颗勾着、"
                  "另外两颗没勾",
                  QStringLiteral("页数=%1 停在第%2页").arg(modeStack->count())
                      .arg(modeStack->currentIndex()));

            // ---------- ② 换页：setInstanceMode 是「停在哪一页 + 谁勾着」的唯一出处 ----------
            ui.setInstanceMode(MainWindowUI::InstanceMode::Server);
            QApplication::processEvents();
            check(modeStack->currentIndex() == 1 && serverChip->isChecked() &&
                      !clientChip->isChecked() && !presetChip->isChecked(),
                  "★★ U34.2 切到服务端：换页容器停到第 1 页，而且**勾跟着换**（三颗是一组"
                  "互斥的 chip，不是各自能按的按钮）",
                  QStringLiteral("停在第%1页 服务端勾=%2 客户端勾=%3 预设勾=%4")
                      .arg(modeStack->currentIndex())
                      .arg(serverChip->isChecked() ? 1 : 0)
                      .arg(clientChip->isChecked() ? 1 : 0)
                      .arg(presetChip->isChecked() ? 1 : 0));
            ui.setInstanceMode(MainWindowUI::InstanceMode::Preset);
            QApplication::processEvents();
            check(modeStack->currentIndex() == 2 && presetChip->isChecked() &&
                      !clientChip->isChecked() && !serverChip->isChecked() &&
                      modeStack->widget(2) == presetHost,
                  "★★ U34.2 切到预设：停在第 2 页（那一页正是 `instancePresetPage` 宿主），"
                  "勾也换过去");
            ui.setInstanceMode(MainWindowUI::InstanceMode::Client);
            QApplication::processEvents();
            check(modeStack->currentIndex() == 0 && clientChip->isChecked() &&
                      !serverChip->isChecked() && !presetChip->isChecked(),
                  "★ U34.2 切回客户端：又回到第 0 页、勾也换回来");

            // ---------- ②b 路径行与「导入整合包…」按三个标签显隐（issue #18）----------
            {
                QLabel *pathLine = ui.getInstanceFolderLabel();
                QPushButton *importButton = ui.getImportModpackButton();
                ui.setInstanceFolderText(QStringLiteral("C:\\MC（2 个实例）"));
                ui.setServerFolderText(QStringLiteral("C:\\MC\\Servers（1 个服务端）"));
                ui.setPresetSubtitle(QStringLiteral("C:\\Presets（3 份预设）"));
                struct Expect {
                    MainWindowUI::InstanceMode mode;
                    const char *text;
                    bool importVisible;
                } const expects[] = {
                    {MainWindowUI::InstanceMode::Client, "C:\\MC（2 个实例）", true},
                    {MainWindowUI::InstanceMode::Server, "C:\\MC\\Servers（1 个服务端）", true},
                    {MainWindowUI::InstanceMode::Preset, "C:\\Presets（3 份预设）", false},
                    {MainWindowUI::InstanceMode::Client, "C:\\MC（2 个实例）", true},
                };
                for (const Expect &e : expects) {
                    ui.setInstanceMode(e.mode);
                    QApplication::processEvents();
                    const QString want = QString::fromUtf8(e.text);
                    check(pathLine != nullptr && importButton != nullptr &&
                              !pathLine->isHidden() && pathLine->text() == want &&
                              pathLine->toolTip() == want &&
                              !importButton->isHidden() == e.importVisible,
                          "★★ U34.2b 三个标签下路径行都在、写法统一（无「当前文件夹：」前缀），"
                          "「导入整合包…」只在预设标签收起",
                          QStringLiteral("模式=%1 路径行=%2 导入按钮显示=%3")
                              .arg(static_cast<int>(e.mode))
                              .arg(pathLine != nullptr ? pathLine->text() : QStringLiteral("<无>"))
                              .arg(importButton != nullptr && !importButton->isHidden() ? 1 : 0));
                }
            }

            // ---------- ③ 那一行摆在哪：和换页容器同栏、在它上面 ----------
            //
            // 次序按**布局里那一位**比（不比屏幕坐标）：藏起来的控件不参加布局，算出来的
            // 位置是个残留值。模式行和换页容器**同一栏**，行在前。
            QWidget *modeRow = window.findChild<QWidget *>(MainWindowUI::instanceModeRowName());
            auto *modeColumn = modeRow != nullptr
                                   ? qobject_cast<QBoxLayout *>(modeRow->parentWidget()->layout())
                                   : nullptr;
            const int rowAt = modeColumn != nullptr ? modeColumn->indexOf(modeRow) : -1;
            const int stackAt = modeColumn != nullptr ? modeColumn->indexOf(modeStack) : -1;
            check(modeRow != nullptr && modeRow->parentWidget() == modeStack->parentWidget() &&
                      rowAt >= 0 && rowAt < stackAt,
                  "★★ U34.3 模式行和换页容器**同一栏、在它上面**（这一行是「下面那一整块"
                  "看哪一边」的开关，不是另开一块面板）",
                  QStringLiteral("同一栏=%1 次序 %2 < %3")
                      .arg(modeRow != nullptr && modeRow->parentWidget() == modeStack->parentWidget()
                               ? 1
                               : 0)
                      .arg(rowAt)
                      .arg(stackAt));

            // ---------- ④ 服务端那半页那三样 ----------
            check(serverEmpty->text() == MainWindowUI::serverEmptyText(),
                  "★ U34.4 服务端那半页的空状态逐字就是约定的那一句", serverEmpty->text());
            check(!serverTree->rootIsDecorated() && serverTree->selectionMode() ==
                      QAbstractItemView::SingleSelection,
                  "★ U34.4 服务端那棵树不画根展开箭头、单选（和实例树同一档观感）");
            check(newServer->text() == MainWindowUI::newServerText(),
                  "★ U34.4 「新建服务端…」那颗的文字逐字就是约定的那一句", newServer->text());
        }

        // ---------- ⑤ 「新建服务端」那一页自身的形状 ----------
        //
        // 这一页**不联网、不碰盘**（见它的文件头）：挑档、填名字这些事都在它自己身上，
        // 建目录 / 下载是 `accept()` 之后 `MainWindow` 的活。所以这里能离线把它驱动一遍。
        {
            NewServerPage page;
            page.resize(720, 520);
            page.show();
            QApplication::processEvents();

            QList<NewServerPage::FlavorOption> flavors;
            {
                NewServerPage::FlavorOption a;
                a.token = QStringLiteral("fabric");
                a.name = QStringLiteral("Fabric");
                a.note = QStringLiteral("装了 Fabric 的服务端");
                a.needsLoader = true;
                a.needsInstaller = true;
                a.needsLoaderGame = true;
                flavors.append(a);
                NewServerPage::FlavorOption b;
                b.token = QStringLiteral("paper");
                b.name = QStringLiteral("Paper");
                b.note = QStringLiteral("Paper 服务端");
                b.needsBuild = true;
                b.needsLoaderGame = true;
                flavors.append(b);
            }
            page.setFlavors(flavors, QStringLiteral("fabric"));
            QApplication::processEvents();

            auto *flavorList =
                page.findChild<QListWidget *>(MainWindowUI::newServerFlavorListName());
            auto *loaderEdit = page.findChild<QLineEdit *>(MainWindowUI::newServerLoaderEditName());
            auto *installerEdit =
                page.findChild<QLineEdit *>(MainWindowUI::newServerInstallerEditName());
            auto *buildEdit = page.findChild<QLineEdit *>(MainWindowUI::newServerBuildEditName());
            auto *memorySpin = page.findChild<QSpinBox *>(MainWindowUI::newServerMemorySpinName());
            auto *summary = page.findChild<QLabel *>(MainWindowUI::newServerSummaryLabelName());
            auto *createBtn =
                page.findChild<QPushButton *>(MainWindowUI::newServerCreateButtonName());
            const bool pageThere = flavorList != nullptr && loaderEdit != nullptr &&
                                   installerEdit != nullptr && buildEdit != nullptr &&
                                   memorySpin != nullptr && summary != nullptr &&
                                   createBtn != nullptr;
            check(pageThere, "U34.5 前提：「新建服务端」那一页那几格都按 objectName 找得到");
            if (pageThere) {
                check(flavorList->count() == 2 && flavorList->item(0)->text() == QStringLiteral("Fabric") &&
                          flavorList->item(1)->text() == QStringLiteral("Paper") &&
                          flavorList->item(0)->data(Qt::UserRole).toString() == QStringLiteral("fabric"),
                      "★ U34.5 端类别那一排逐条摆的就是喂进来的那几档（认的是 token，不是文字）");
                // 停在 Fabric（要加载器 + 安装器，不要构建号）⇒ 那两行露着、构建号收着。
                check(loaderEdit->isVisible() && installerEdit->isVisible() &&
                          !buildEdit->isVisible(),
                      "★★ U34.6 停在 Fabric：加载器/安装器两行露着、构建号那一行**收着**"
                      "（填到哪一格才知道下一步填什么）",
                      QStringLiteral("loader=%1 installer=%2 build=%3")
                          .arg(loaderEdit->isVisible() ? 1 : 0)
                          .arg(installerEdit->isVisible() ? 1 : 0)
                          .arg(buildEdit->isVisible() ? 1 : 0));
                loaderEdit->setText("0.16.14"); installerEdit->setText("1.0.1");
                page.hide();
                check(page.loaderVersion() == QStringLiteral("0.16.14") && page.installerVersion() == QStringLiteral("1.0.1"),
                    "U34 closing the whole wizard preserves applicable loader/installer values for installation");
                page.show(); QApplication::processEvents();
                // 换成 Paper（只要构建号）⇒ 反过来，而且**收着的那一行读回去是空串**
                // （旧的字留在框里，读回去就成了"这一档也有加载器版本"）。
                page.setFlavors(flavors, QStringLiteral("paper"));
                QApplication::processEvents();
                check(!loaderEdit->isVisible() && !installerEdit->isVisible() &&
                          buildEdit->isVisible(),
                      "★★ U34.6 换成 Paper：构建号那一行露出来、加载器/安装器两行收起来",
                      QStringLiteral("loader=%1 installer=%2 build=%3")
                          .arg(loaderEdit->isVisible() ? 1 : 0)
                          .arg(installerEdit->isVisible() ? 1 : 0)
                          .arg(buildEdit->isVisible() ? 1 : 0));
                check(page.loaderVersion().isEmpty() && page.installerVersion().isEmpty() &&
                          page.buildId().isEmpty(),
                      "★ U34.6 没填的那几格读回去是空串（收着的行不算数）");
                buildEdit->setText("232"); page.hide();
                check(page.buildId() == QStringLiteral("232") && page.loaderVersion().isEmpty() && page.installerVersion().isEmpty(),
                    "U34 closed Paper wizard retains its build while excluding hidden loader fields");
                page.show(); buildEdit->clear(); QApplication::processEvents();
                // 说明与「创建」：有问题时点名问题并把创建挡住，没问题时放行。
                page.setSummary(QString(), QStringLiteral("先填个游戏版本"));
                check(summary->text() == QStringLiteral("先填个游戏版本") && !createBtn->isEnabled(),
                      "★★ U34.7 填法不成立时：把那句问题摆出来，「创建」**点不动**"
                      "（点下去只会建一个半残的服务端）");
                page.setSummary(QStringLiteral("要下 paper.jar"), QString());
                check(summary->text() == QStringLiteral("要下 paper.jar") && createBtn->isEnabled(),
                      "★ U34.7 填法成立时：摆计划说明，「创建」放行");
                check(memorySpin->value() == 2048 && memorySpin->minimum() == 512,
                      "★ U34.7 内存那一格起手 2048 MB、下界 512",
                      QString::number(memorySpin->value()));
            }
            page.close();
            QApplication::processEvents();
        }

        // ---------- U35 「新建服务端」那一页的「目录」那一行（M4-2）----------
        //
        // 用户 定的那一条：「创建」= 建目录 + 当趟把东西下下来。落盘那件事在
        // mw_check 的 T40（真建、真下、真取消）；这一节只量**建之前用户看得见的那一格**：
        // 名字会变成盘上哪个目录。它是**算出来的**（服务端根只有一份，一个服务端 = 根下面
        // 一个以名字命名的目录），所以这一格**只读** —— 页面上不许有一个能改它的输入框。
        {
            NewServerPage page;
            page.resize(720, 520);
            page.show();
            QApplication::processEvents();

            auto *dirLabel = page.findChild<QLabel *>(MainWindowUI::newServerTargetDirName());
            const bool dirThere = dirLabel != nullptr;
            check(dirThere,
                  "U35.0 前提：「目录」那一行按 objectName 找得到（而且是一个字，不是一个输入框）");
            if (dirThere) {
                // 只读：是 `QLabel`、字能框选复制（要能粘出去），但**改不了**（没有 editable）。
                const Qt::TextInteractionFlags flags = dirLabel->textInteractionFlags();
                check(flags.testFlag(Qt::TextSelectableByMouse) &&
                          !flags.testFlag(Qt::TextEditable) &&
                          page.findChild<QLineEdit *>(MainWindowUI::newServerTargetDirName()) ==
                              nullptr,
                      "★★ U35.1 那一格是**只读的**：字能框选（好粘出去），但页面上没有一个能"
                      "改它的输入框 —— 服务端根只有一份，这一格是算出来的，不是选出来的");

                // 没起名 ⇒ 什么都不摆（翻译点算出来的目录名还空着）。
                page.setTargetDir(QString());
                check(dirLabel->text().isEmpty(),
                      "★ U35.2 名字还空着时那一格**什么都不摆**（没有目录可算）",
                      QStringLiteral("「%1」").arg(dirLabel->text()));

                // 起了名 ⇒ 逐字就是翻译点喂进来的那一个，悬停也就是它。
                const QString wantDir =
                    QDir::toNativeSeparators(QStringLiteral("D:/Sandbox/Servers/我的服务端"));
                page.setTargetDir(wantDir);
                QApplication::processEvents();
                check(dirLabel->text() == wantDir && dirLabel->toolTip() == wantDir,
                      "★★ U35.3 喂进来什么就摆什么（逐字），悬停那一句也就是这个目录 —— "
                      "用户在建之前就看得见名字会变成盘上哪儿",
                      QStringLiteral("摆的是「%1」").arg(dirLabel->text()));

                // 空的那一趟把悬停换成"先起名"那句人话（不是留着一个空 tooltip）。
                page.setTargetDir(QString());
                QApplication::processEvents();
                check(!dirLabel->toolTip().isEmpty(),
                      "★ U35.3 名字洗空了那一趟，悬停给的是「先起名」那句话（不留一个空提示）",
                      dirLabel->toolTip());

                // 长路径不许把这一页撑宽：那一格挂的是 Ignored（路径可能很长）。
                page.setTargetDir(QStringLiteral("/x"));
                QApplication::processEvents();
                const int shortHint = page.sizeHint().width();
                const QString longDir = QStringLiteral("D:/")
                                        + QString(400, QLatin1Char('n'))
                                        + QStringLiteral("/我的服务端");
                page.setTargetDir(longDir);
                QApplication::processEvents();
                const int longHint = page.sizeHint().width();
                check(dirLabel->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored &&
                          longHint == shortHint,
                      "★★ U35.4 400 个字的长路径**不许**把这一页撑宽：那一格挂的是 "
                      "`QSizePolicy::Ignored` ⇒ 页面的 sizeHint 一个字都不变（路径可能很长，"
                      "同摘要那一句的规矩；屏幕上真怎么样另有一处钉着）",
                      QStringLiteral("短路径 hint=%1 / 长路径 hint=%2（那一格自己的 sizeHint 宽 %3）")
                          .arg(shortHint)
                          .arg(longHint)
                          .arg(dirLabel->sizeHint().width()));
            }
            page.close();
            QApplication::processEvents();
        }

        // ---------- U36 服务端控制台那一页 + EULA 那道闸（M4-3）----------
        //
        // M4-3 在界面上长出两样东西：
        //
        // ① **控制台那一页** —— 服务端跑起来之后唯一能看见"它现在在说什么"的地方。这一节
        // 把它自己驱动一遍（离线、不需要任何进程）：灌日志 / 读回来 / 敲一行 / 看信号 /
        // 看那三颗按钮亮不亮。它**一行判据都没有**（状态怎么说、能不能停是 `MainWindow`
        // 翻好的，见它的文件头），所以量得到的正好是"这些 bool 摆到屏幕上了没有"。
        //
        // ② **EULA 那道闸长什么样** —— 用户 定的那一条：小提示 + 一个勾选框 +
        // 一行**能点开原文**的链接。那一问的产品侧那一半（`MainWindow::ensureServerEula()`）
        // 量得住的就这么两条："点了那一行链接要有人开浏览器"和"**没勾那个框就不许走到
        // 下一步**"。前者在这一层是 `linkActivated` 那一发信号，后者是"「同意并启动」
        // 在勾上之前点不动" —— 两条都是 `InlineMessageBox` 自己的行为，所以在这儿量。
        {
            ServerConsolePage page;
            page.resize(880, 620);

            auto *title = page.findChild<QLabel *>(MainWindowUI::serverConsoleTitleName());
            auto *status = page.findChild<QLabel *>(MainWindowUI::serverConsoleStatusName());
            auto *log = page.findChild<QPlainTextEdit *>(MainWindowUI::serverConsoleLogName());
            auto *edit = page.findChild<QLineEdit *>(MainWindowUI::serverConsoleCommandEditName());
            auto *send = page.findChild<QPushButton *>(MainWindowUI::serverConsoleSendButtonName());
            auto *stop = page.findChild<QPushButton *>(MainWindowUI::serverConsoleStopButtonName());
            auto *kill =
                page.findChild<QPushButton *>(MainWindowUI::serverConsoleForceKillButtonName());
            const bool pageThere = title != nullptr && status != nullptr && log != nullptr &&
                                   edit != nullptr && send != nullptr && stop != nullptr &&
                                   kill != nullptr;
            // 「哪一颗挂了哪一档」的取法：没挂属性 = `Accent`（同 U10L 那一节 —— 属性名
            // 那一对是有唯一出处的，但 `QObject::property()` 只吃字面量）。
            const auto kindOf = [](QPushButton *button) {
                const QVariant value = button->property("wtfmlButtonKind");
                return value.isValid() ? value.toString()
                                       : Theme::buttonKindKey(Theme::ButtonKind::Accent);
            };
            check(pageThere,
                  "U36.0 前提：控制台那一页那七样（名字 / 状态 / 日志区 / 输入框 / 发送 / "
                  "停止 / 强制结束）都按 objectName 找得到");
            if (pageThere) {
                check(page.objectName() == MainWindowUI::serverConsolePageName() &&
                          ServerConsolePage::windowTitle() == QStringLiteral("服务端控制台"),
                      "★ U36.1 这一页认得出自己（页眉上那行字是 `windowTitle()` 那一个出处）",
                      page.objectName());
                // 日志区：只读、不折行、等宽（服务端那些行是对着列的）。
                // NOTE: 等宽这一条**比字体族**，不比 `QFont::fixedPitch()` —— 那一面旗是
                // 字体后端自己报的，offscreen 那个桩字体库一律报 0（会假红）。产品那
                // 一边问的就是 `QFontDatabase::systemFont(FixedFont)`，这里问同一个出处。
                const QString fixedFamily =
                    QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
                check(log->isReadOnly() &&
                          log->lineWrapMode() == QPlainTextEdit::NoWrap &&
                          log->font().family() == fixedFamily,
                      "★★ U36.1 日志区是**只读 + 不折行 + 等宽字体**（折了之后时间戳和正文"
                      "错开，反而更难读；只读是因为这地方不该能改）",
                      QStringLiteral("只读=%1 不折行=%2 字体=%3（要的是 %4）")
                          .arg(log->isReadOnly() ? 1 : 0)
                          .arg(log->lineWrapMode() == QPlainTextEdit::NoWrap ? 1 : 0)
                          .arg(log->font().family(), fixedFamily));

                // ---------- 起手：三颗全灰着（"什么都没在跑"那一档）----------
                check(!stop->isEnabled() && !kill->isEnabled() && !edit->isEnabled() &&
                          !send->isEnabled(),
                      "★★ U36.2 刚建出来的时候三颗全是灰的：没在跑的时候「停止 / 强制结束」"
                      "不该能按，输入框也不该能用（写进去没人读）");
                check(kindOf(kill) == Theme::buttonKindKey(Theme::ButtonKind::Danger) &&
                          kindOf(stop) == Theme::buttonKindKey(Theme::ButtonKind::Neutral),
                      "★ U36.2 「强制结束」是**危险那一档**（可能丢存档）、「停止服务端」是"
                      "一般那一档（推荐走它）",
                      QStringLiteral("kill=%1 stop=%2").arg(kindOf(kill), kindOf(stop)));
                // 「发送」是这一页的主操作 ⇒ 主题色那一档（**不挂属性**就是 Accent）。
                check(kindOf(send) == Theme::buttonKindKey(Theme::ButtonKind::Accent) &&
                          kindOf(send) != kindOf(kill),
                      "★ U36.2 「发送」是主题色那一档（它不是危险那一档，也没被染成一般档）",
                      kindOf(send));

                // ---------- 灌日志 / 读回来 / 上限 ----------
                page.setLog(QStringList{QStringLiteral("第一行"), QStringLiteral("第二行")});
                check(page.logLines() ==
                          QStringList{QStringLiteral("第一行"), QStringLiteral("第二行")},
                      "★ U36.3 `setLog()` 灌进去什么就读回来什么（开页时那一趟历史靠它）",
                      page.logLines().join(QStringLiteral(" | ")));
                page.appendLine(QStringLiteral("第三行"));
                check(page.logLines().size() == 3 &&
                          page.logLines().last() == QStringLiteral("第三行"),
                      "★ U36.3 `appendLine()` 续在末尾（服务端刚吐出来的那一行）");
                page.setLog(QStringList());
                check(page.logLines().isEmpty(), "★ U36.3 空表 = 清空（不是留一段老日志）");

                // 上限：这一条是"跑了几天的服务端不把启动器拖死"的落点。
                const int keep = 3;
                page.setMaxLines(keep);
                check(log->document()->maximumBlockCount() == keep,
                      "★★ U36.3 `setMaxLines()` 真的钉到了文档上（那一层的上限就是它喂的 "
                      "`ServerProcess::maxLogLines()`）",
                      QString::number(log->document()->maximumBlockCount()));
                for (int i = 0; i < keep + 4; ++i) {
                    page.appendLine(QStringLiteral("行%1").arg(i));
                }
                const QStringList capped = page.logLines();
                check(capped.size() <= keep && !capped.isEmpty() &&
                          capped.last() == QStringLiteral("行%1").arg(keep + 3),
                      "★★ U36.3 超出去的老行**自己就掉了**（`setMaximumBlockCount` 那一套，"
                      "不是我们另写一段「留后 N 行」）",
                      QStringLiteral("%1 行，最后一行「%2」")
                          .arg(capped.size())
                          .arg(capped.isEmpty() ? QString() : capped.last()));

                // ---------- 状态 / 名字：喂什么摆什么 ----------
                page.setServerName(QStringLiteral("我的服务端"));
                page.setStatusText(ServerModel::statusName(ServerModel::Status::Running));
                check(title->text() == QStringLiteral("我的服务端") &&
                          status->text() == ServerModel::statusName(ServerModel::Status::Running),
                      "★ U36.4 名字和状态逐字就是喂进来的（状态那句话的**唯一出处**是 "
                      "`ServerModel::statusName()`）",
                      QStringLiteral("%1 / %2").arg(title->text(), status->text()));

                // ---------- 那三颗："能不能按"完全由外面说了算 ----------
                page.setStopAvailable(true);
                page.setForceKillAvailable(false);
                page.setCommandAvailable(true);
                check(stop->isEnabled() && !kill->isEnabled() && edit->isEnabled() &&
                          send->isEnabled(),
                      "★★ U36.4 「停止」亮着、「强制结束」还灰着、输入框能用了 —— 宽限时间"
                      "没到你就不该能把它打死");
                page.setForceKillAvailable(true);
                check(kill->isEnabled(), "★ U36.4 宽限时间到了 ⇒ 「强制结束」这才亮起来");

                // ---------- 敲一行：信号 + 清空 + 空行不发 ----------
                int submitted = 0;
                QString lastCommand;
                QObject::connect(&page, &ServerConsolePage::commandSubmitted, &page,
                                 [&submitted, &lastCommand](const QString &command) {
                                     ++submitted;
                                     lastCommand = command;
                                 });
                edit->setText(QStringLiteral("  say 大家好  "));
                send->click();
                check(submitted == 1 && lastCommand == QStringLiteral("say 大家好"),
                      "★★ U36.5 按「发送」发出去的是**去掉两头空白**的那一行，一个字不加"
                      "（换行由 `ServerProcess::sendCommand()` 补，这一页不碰它）",
                      lastCommand);
                check(edit->text().isEmpty(),
                      "★ U36.5 发完把输入框清干净（同一条命令连按两下会重复执行）");
                // 回车 = 发送（同一件事）。
                edit->setText(QStringLiteral("op 我"));
                QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                QApplication::sendEvent(edit, &enter);
                check(submitted == 2 && lastCommand == QStringLiteral("op 我"),
                      "★ U36.5 在那一格按回车 == 按「发送」（同一个槽）", lastCommand);
                // 空行 / 只有空白：什么都不发。
                edit->setText(QStringLiteral("   "));
                send->click();
                check(submitted == 2, "★★ U36.5 空行**不发**（服务端收到一个空命令只会回一句"
                                      "没用的报错）",
                      QString::number(submitted));

                // ---------- 底下那两颗：各发各的信号 ----------
                int stops = 0;
                int kills = 0;
                QObject::connect(&page, &ServerConsolePage::stopRequested, &page,
                                 [&stops] { ++stops; });
                QObject::connect(&page, &ServerConsolePage::forceKillRequested, &page,
                                 [&kills] { ++kills; });
                stop->click();
                kill->click();
                check(stops == 1 && kills == 1,
                      "★★ U36.6 「停止服务端」和「强制结束」发的是**两条**信号（一个是好好说、"
                      "一个是打死，收尾完全不一样）",
                      QStringLiteral("stop=%1 kill=%2").arg(stops).arg(kills));

                page.close();
                QApplication::processEvents();
            }

            // ---------- EULA 那道闸的形状（`InlineMessageBox` 那一层）----------
            {
                InlineMessageBox box(MessageKind::Question, ServerRun::eulaPromptTitle(),
                                     ServerRun::eulaPromptText(),
                                     QStringList{QStringLiteral("同意并启动"),
                                                 QStringLiteral("先不启动")},
                                     -1, -1);
                box.setCheckBox(ServerRun::eulaCheckBoxText(), 0);
                box.setLink(ServerRun::eulaLinkText(), ServerRun::eulaUrl());

                auto *checkBox = box.findChild<QCheckBox *>(InlineMessageBox::checkBoxName());
                auto *link = box.findChild<QLabel *>(InlineMessageBox::linkName());
                QPushButton *agree = box.buttonAt(0);
                auto *decline = box.buttonAt(1);
                const bool there = checkBox != nullptr && link != nullptr && agree != nullptr &&
                                   decline != nullptr;
                check(there,
                      "★ U36.7 前提：那个勾选框 / 那一行链接 / 两颗按钮都在（按 objectName 认）");
                if (there) {
                    check(!checkBox->isChecked() && !box.checkBoxChecked(),
                          "★★ U36.7 那个框**起手是空的**（「替你同意」是这一条路唯一禁止的事）");
                    check(!agree->isEnabled() && decline->isEnabled(),
                          "★★★ U36.7 没勾之前「同意并启动」**点不动**（那个勾是唯一的技术"
                          "落点，不是摆着好看的）");
                    // 颜色这一条是**位置的函数**（`InlineMessageBox` 那套：默认那一颗 = 主题色、
                    // 危险那一颗 = 红、其余 = 一般档）。这一份 `defaultIndex` 故意是 −1 ⇒
                    // 两颗都落"一般那档"：**主题色 = "回车就能办"的那个记号**，而这一格上
                    // 恰恰不许有那样一颗（同意一份协议不能是回车的手滑）。红也不是 ——
                    // "同意"不是破坏性操作。
                    const QString neutral = Theme::buttonKindKey(Theme::ButtonKind::Neutral);
                    const QString danger = Theme::buttonKindKey(Theme::ButtonKind::Danger);
                    check(kindOf(agree) == neutral && kindOf(decline) == neutral &&
                              kindOf(agree) != danger,
                          "★ U36.7 两颗都**不染主题色**（这一格上不许存在「回车就办」的默认那"
                          "一颗 —— 染了主题色就是承认它默认）、也**都不是危险那一档**",
                          QStringLiteral("%1 / %2（都不能是 %3）")
                              .arg(kindOf(agree), kindOf(decline), danger));
                    checkBox->setChecked(true);
                    check(agree->isEnabled() && box.checkBoxChecked(),
                          "★★ U36.7 勾上之后那一颗才亮 —— 勾了又去掉的话它跟着灭",
                          QStringLiteral("%1").arg(agree->isEnabled() ? 1 : 0));
                    checkBox->setChecked(false);
                    check(!agree->isEnabled(), "★ U36.7 去掉勾 ⇒ 又点不动了");

                    // 那一行链接：文字里得有"打开"两个字，而且**它自己不开浏览器**
                    // （点了只发信号，谁去开由调用方决定 —— 见 `setLink()` 那段）。
                    check(link->text().contains(QStringLiteral("打开")) &&
                              link->text().contains(ServerRun::eulaUrl()) &&
                              box.linkUrl() == ServerRun::eulaUrl() &&
                              !link->openExternalLinks(),
                          "★★ U36.8 那一行是**可点的链接**：文字里有「打开」、href 就是 "
                          "`eulaUrl()`（`https://aka.ms/MinecraftEULA`）、而且这一层**自己"
                          "不开浏览器**（谁去开、开不了怎么办由调用方接手）",
                          box.linkUrl());
                    check(link->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse),
                          "★ U36.8 而且它真的能点（不是一行看着像链接的灰字）");
                    QString activatedUrl;
                    QObject::connect(&box, &InlineMessageBox::linkActivated, &box,
                                     [&activatedUrl](const QString &url) { activatedUrl = url; });
                    emit link->linkActivated(ServerRun::eulaUrl());
                    check(activatedUrl == ServerRun::eulaUrl(),
                          "★★ U36.8 点那一行会发 `linkActivated(url)`（`MainWindow` 就是"
                          "接在这一条上开浏览器的）",
                          activatedUrl);
                    // 默认那一颗 = 没有：这一格上一颗都不许被回车打中。
                    check(box.clickedIndex() == -1 && !ServerRun::eulaCheckBoxText().isEmpty() &&
                              !ServerRun::eulaPromptText().isEmpty(),
                          "★★ U36.9 用户什么都没点 ⇒ `clickedIndex()` 停在 −1（关掉 / 按 Esc "
                          "一律按「没同意」算）");
                    box.close();
                    QApplication::processEvents();
                }
            }
        }
    }
    return 0;
}


