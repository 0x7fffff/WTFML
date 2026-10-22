#include "Support.hpp"
int UiWindowPreferencesChecks(QTabWidget * &tabs) {
    {
        // 这一组三件事：看不看游戏自己的输出（勾选框）、它占多少 CPU（下拉框）、窗口多大
        // （两个数字框）。控件的存在性和"落在右列"已经在 L / E2b 两节验过了，这里验的是
        // **这一组特有的东西**：文案、取值范围、以及那两个数字框的几何。
        //
        // NOTE: 「游戏日志」那一勾与「进程优先级」那颗下拉框在搭完页后会被
        // `moveSetting(…, "运行与日志")` 按职责搬进**「启动」栏**（启动器行为/进程/日志
        // 归启动，见 MainWindowUI::setupSettingsPage 里那段），只有窗口尺寸那两格留在
        // 「游戏」栏。所以下面勾选框与行标题在「启动」栏那张表里找，尺寸/窗口模式才在
        // 「游戏」栏找 —— 找错栏的症状就是"控件明明在、这里却报 nullptr"。
        QWidget *settingsPage = tabs->widget(MainWindowUI::settingsTabIndex());
        tabs->setCurrentIndex(MainWindowUI::settingsTabIndex());
        QApplication::processEvents();
        // 先把左栏切到「游戏」栏
        // （几何断言要的是"这一页真的摆出来了"，切过去之前量到的是藏起来那一页的旧值），
        // 再按那一栏的键取表单。整页唯一的那张 settingsForm 已经没有了。
        if (auto *categoryList = settingsPage != nullptr
                                     ? settingsPage->findChild<QListWidget *>(
                                           MainWindowUI::settingsCategoryListName())
                                     : nullptr) {
            const int gameRow =
                MainWindowUI::settingsCategoryKeys().indexOf(QStringLiteral("game"));
            if (gameRow >= 0) {
                categoryList->setCurrentRow(gameRow);
            }
        }
        QApplication::processEvents();
        auto *panelScroll = settingsPage != nullptr
                                ? settingsPage->findChild<QScrollArea *>(
                                      MainWindowUI::settingsPanelName(QStringLiteral("game")))
                                : nullptr;
        QWidget *form = panelScroll != nullptr ? panelScroll->widget() : nullptr;
        check(form != nullptr, "Z2 前提：找得到「游戏」栏那张设置表单");
        // 日志那两格搬去了「启动」栏，按那一栏的键另取一张表（见本节开头的 NOTE）。
        auto *launchScroll =
            settingsPage != nullptr
                ? settingsPage->findChild<QScrollArea *>(
                      MainWindowUI::settingsPanelName(QStringLiteral("launch")))
                : nullptr;
        QWidget *launchForm = launchScroll != nullptr ? launchScroll->widget() : nullptr;

        // ---- (a) 「显示游戏日志」：这句话是要求，别被改写 ----
        {
            auto *checkBox = launchForm != nullptr
                                 ? launchForm->findChild<QCheckBox *>(
                                       MainWindowUI::showGameLogCheckName())
                                 : nullptr;
            check(checkBox != nullptr, "Z2 找得到「显示游戏日志」那个勾选框");
            if (checkBox != nullptr) {
                check(checkBox->text() == MainWindowUI::showGameLogCheckText(),
                      "Z2 勾选框上的字就是那一句（用户原话：显示游戏日志）", checkBox->text());
                // 界面层不许自己默认打开：默认值是产品逻辑，由 MainWindow 从盘上灌。
                check(!checkBox->isChecked(),
                      "Z2 刚搭好时未勾选（值由 MainWindow 灌，界面不自己猜）");
                if (launchForm != nullptr) {
                    bool hasRowTitle = false;
                    const QList<QLabel *> labelsInForm = launchForm->findChildren<QLabel *>();
                    for (const QLabel *label : labelsInForm) {
                        if (label->text() == MainWindowUI::showGameLogRowText()) {
                            hasRowTitle = true;
                        }
                    }
                    check(hasRowTitle,
                          "Z2 行标题「游戏日志」真画在表单上（不是把勾选框那句话抄一遍）",
                          MainWindowUI::showGameLogRowText());
                    check(MainWindowUI::showGameLogRowText() !=
                              MainWindowUI::showGameLogCheckText(),
                          "Z2 行标题与勾选框文字是两句不同的话（同屏出现两遍那个已经改掉）",
                          MainWindowUI::showGameLogRowText() + QStringLiteral(" / ") +
                              MainWindowUI::showGameLogCheckText());
                }
            }
        }

        // ---- (b) 两个数字框的范围：和 LauncherSettings 的钳位**必须一样** ----
        // 这两份数是各写一遍的（ui/ 不许 include src/），所以这条等号是唯一盯着它们的地方。
        // 不一致的症状是"用户填了 320，读回来变成 256"那种"填了不生效"。
        {
            auto *widthSpin = form != nullptr
                                  ? form->findChild<QSpinBox *>(MainWindowUI::gameWindowWidthSpinName())
                                  : nullptr;
            auto *heightSpin = form != nullptr ? form->findChild<QSpinBox *>(
                                                     MainWindowUI::gameWindowHeightSpinName())
                                               : nullptr;
            check(widthSpin != nullptr && heightSpin != nullptr, "Z2 找得到那两个数字框");
            if (widthSpin != nullptr && heightSpin != nullptr) {
                check(widthSpin->minimum() == LauncherSettings::gameWindowMinWidth() &&
                          widthSpin->maximum() == LauncherSettings::gameWindowMaxWidth(),
                      "Z2 宽度范围 == LauncherSettings 的钳位（320…7680）",
                      QString("%1…%2 vs %3…%4")
                          .arg(widthSpin->minimum())
                          .arg(widthSpin->maximum())
                          .arg(LauncherSettings::gameWindowMinWidth())
                          .arg(LauncherSettings::gameWindowMaxWidth()));
                check(heightSpin->minimum() == LauncherSettings::gameWindowMinHeight() &&
                          heightSpin->maximum() == LauncherSettings::gameWindowMaxHeight(),
                      "Z2 高度范围 == LauncherSettings 的钳位（240…4320）",
                      QString("%1…%2 vs %3…%4")
                          .arg(heightSpin->minimum())
                          .arg(heightSpin->maximum())
                          .arg(LauncherSettings::gameWindowMinHeight())
                          .arg(LauncherSettings::gameWindowMaxHeight()));
                check(widthSpin->singleStep() == MainWindowUI::windowSizeStep() &&
                          heightSpin->singleStep() == MainWindowUI::windowSizeStep(),
                      "Z2 步长是 windowSizeStep()（16：拖一下就走得到常见值）",
                      QString::number(widthSpin->singleStep()));
                // 刚搭好时停在**下限**，和上面那两个内存框同一个规矩：界面只搭壳，值由
                // MainWindow 从盘上灌（它才知道存哪儿）。原版那对 854×480 是
                // LauncherSettings 的初值，在 verify 的 ZD1 里验 —— 界面层自己默认成
                // 854 的话，"MainWindow 忘了灌值"就会显示一个看起来合理的假值。
                check(widthSpin->value() == widthSpin->minimum() &&
                          heightSpin->value() == heightSpin->minimum(),
                      "Z2 刚搭好时两个框停在下限（值由 MainWindow 灌，界面不自己猜）",
                      QString("%1×%2").arg(widthSpin->value()).arg(heightSpin->value()));
            }
        }

        // ---- (b2) 「窗口模式」那一格----
        //
        // 三档的文案和代号都是**MainWindow**灌的（界面层不认识 GameWindowMode 这个枚举
        // —— ui/ 不许 include src/，见 InstanceSettingsDialog::WindowModeChoice 那条路），
        // 所以这里只钉壳：那一格在、是一句话就能看懂的那个行标题、而且**自己一条都不加**。
        // "加了自己的默认档"这件事的症状很具体：盘上存着「全屏」，开窗时界面显示「小窗」，
        // 而用户没动过任何东西 —— 下一次落盘就把他的档位改掉了。
        {
            auto *modeCombo = form != nullptr
                                  ? form->findChild<QComboBox *>(
                                        MainWindowUI::gameWindowModeComboName())
                                  : nullptr;
            check(modeCombo != nullptr, "Z2 「窗口模式」的下拉框按名字找得回来");
            if (modeCombo != nullptr) {
                check(modeCombo->count() == 0,
                      "Z2 刚搭好时那一格是空的（三档由 MainWindow 灌，界面不自己猜一档）",
                      QString::number(modeCombo->count()));
                check(!modeCombo->isEditable(),
                      "Z2 那一格不可编辑（档位只有三档，让用户打进去的是第四个值）");
            }
            // 行标题得在表单上（同上面「游戏日志」那一条的判据）。
            bool hasModeRowTitle = false;
            if (form != nullptr) {
                const QList<QLabel *> labels = form->findChildren<QLabel *>();
                for (QLabel *label : labels) {
                    if (label->text() == QStringLiteral("窗口模式")) {
                        hasModeRowTitle = true;
                    }
                }
            }
            check(hasModeRowTitle, "Z2 行标题「窗口模式」真画在表单上");
        }

        // ---- (c) 那一行的几何：两个框等宽、并排、整行靠左不撑满右列 ----
        {
            auto *widthSpin = form != nullptr
                                  ? form->findChild<QSpinBox *>(MainWindowUI::gameWindowWidthSpinName())
                                  : nullptr;
            auto *heightSpin = form != nullptr ? form->findChild<QSpinBox *>(
                                                     MainWindowUI::gameWindowHeightSpinName())
                                               : nullptr;
            auto *timesLabel = form != nullptr
                                   ? form->findChild<QLabel *>(MainWindowUI::gameWindowSizeTimesLabelName())
                                   : nullptr;
            QWidget *sizeRow = widthSpin != nullptr ? widthSpin->parentWidget() : nullptr;
            check(timesLabel != nullptr, "Z2 中间那个「×」也按名字找得回来");
            if (widthSpin != nullptr && heightSpin != nullptr && sizeRow != nullptr) {
                check(timesLabel != nullptr && timesLabel->text() == QStringLiteral("×"),
                      "Z2 中间那个符号是「×」（不是字母 x）",
                      timesLabel != nullptr ? timesLabel->text() : QStringLiteral("<null>"));
                check(widthSpin->parentWidget() == sizeRow &&
                          heightSpin->parentWidget() == sizeRow,
                      "Z2 两个框在同一个容器里（不是各自占一整格）");
                check(qAbs(widthSpin->width() - heightSpin->width()) <= 1,
                      "Z2 两个框一样宽（一宽一窄看着就是坏的）",
                      QString("%1 vs %2").arg(widthSpin->width()).arg(heightSpin->width()));
                check(widthSpin->x() < timesLabel->x() && timesLabel->x() < heightSpin->x(),
                      "Z2 从左到右是「宽 × 高」",
                      QString("w.x=%1 times.x=%2 h.x=%3")
                          .arg(widthSpin->x())
                          .arg(timesLabel->x())
                          .arg(heightSpin->x()));
                // 「只占自己需要的那点宽度」：和 Z 节那条导入按钮同一个判据。右列有 7 份宽
                // （900px 的窗口下 600 多 px），两个数字框被拉到那个宽度就是坏的。
                check(sizeRow->width() <= sizeRow->sizeHint().width() + 2,
                      "Z2 整行只占自己 sizeHint 那么宽（没被拉去顶满右列）",
                      QString("w=%1 sizeHint=%2")
                          .arg(sizeRow->width())
                          .arg(sizeRow->sizeHint().width()));
            }
        }

        // ---- (d) 设置页每一个下拉框都"按 sizeHint 靠左摆" ----
        //
        // 这一条是上面那两条的**另一面**。四颗都带 `Qt::AlignLeft`：不带的话它们吃到
        // 拉伸 ⇒ 「进程优先级」（项就"正常（默认）"几个字）被拉成整行那么宽、右边一大片
        // 空白，而「窗口模式」只有它自己那么宽 —— 同一个控件在同一页上两种宽度。
        //
        // NOTE: 这四颗分在三个栏里，所以要**切栏**才量得到真几何（没摆出来的那一栏，
        // 里面控件的宽度还是旧的）。量完切回「游戏」栏 —— 这一段后面的节假设
        // 设置页停在 Z2 开头那一栏上。
        if (settingsPage != nullptr) {
            // 切到某栏并取回那张表单（量完由最后那一次切回「游戏」栏）。
            const auto formOfCategory = [settingsPage](const QString &key) -> QWidget * {
                if (auto *list = settingsPage->findChild<QListWidget *>(
                        MainWindowUI::settingsCategoryListName())) {
                    const int row = MainWindowUI::settingsCategoryKeys().indexOf(key);
                    if (row >= 0) {
                        list->setCurrentRow(row);
                    }
                }
                QApplication::processEvents();
                auto *scroll =
                    settingsPage->findChild<QScrollArea *>(MainWindowUI::settingsPanelName(key));
                return scroll != nullptr ? scroll->widget() : nullptr;
            };
            // 一条 = 哪一栏 + 那个下拉框的 objectName。
            const QList<QPair<QString, QString>> combos = {
                {QStringLiteral("launch"), MainWindowUI::processPriorityComboName()},
                {QStringLiteral("game"), MainWindowUI::gameWindowModeComboName()},
                {QStringLiteral("launch"), MainWindowUI::launcherBehaviourComboName()},
                {QStringLiteral("download"), MainWindowUI::downloadSourceComboName()},
                {QStringLiteral("download"), MainWindowUI::downloadConcurrencyComboName()},
            };
            for (const auto &comboWhere : combos) {
                QWidget *panel = formOfCategory(comboWhere.first);
                auto *combo = panel != nullptr
                                  ? panel->findChild<QComboBox *>(comboWhere.second)
                                  : nullptr;
                check(combo != nullptr,
                      QStringLiteral("Z2 「%1」那一颗下拉框找得到").arg(comboWhere.second));
                if (combo != nullptr) {
                    check(combo->width() <= combo->sizeHint().width() + 2,
                          QStringLiteral("Z2 「%1」按 sizeHint 靠左摆（没被拉去顶满这一栏）")
                              .arg(comboWhere.second),
                          QStringLiteral("w=%1 sizeHint=%2")
                              .arg(combo->width())
                              .arg(combo->sizeHint().width()));
                }
            }
            // 切回「游戏」栏：这一段后面的节假设设置页停在 Z2 开头那一栏上。
            formOfCategory(QStringLiteral("game"));
        }
    }
    return 0;
}


