#include "Support.hpp"
int UiWindowPreferencesChecks(QTabWidget * &tabs) {
    {
        // 这一组三件事：看不看游戏自己的输出（勾选框）、它占多少 CPU（下拉框）、窗口多大
        // （两个数字框）。控件的存在性和"落在右列"已经在 L / E2b 两节验过了，这里验的是
        // **这一组特有的东西**：文案、取值范围、以及那两个数字框的几何。
        //
        // NOTE: 游戏窗口、运行与日志都在「启动」页（游戏页已并入启动），
        // 控件在不在、几何对不对都按那一页的表单量。
        QWidget *settingsPage = tabs->widget(MainWindowUI::settingsTabIndex());
        tabs->setCurrentIndex(MainWindowUI::settingsTabIndex());
        QApplication::processEvents();
        const auto switchSettingsTab = [settingsPage](const QString &key) {
            if (auto *settingsTabs = settingsPage != nullptr
                                         ? settingsPage->findChild<SettingsParts::SlidingTabs *>(
                                               MainWindowUI::settingsTabsName())
                                         : nullptr) {
                const int index = MainWindowUI::settingsCategoryKeys().indexOf(key);
                if (index >= 0) settingsTabs->setCurrentIndex(index);
            }
            QApplication::processEvents();
        };
        switchSettingsTab(QStringLiteral("launch"));
        auto *panelScroll = settingsPage != nullptr
                                ? settingsPage->findChild<QScrollArea *>(
                                      MainWindowUI::settingsPanelName(QStringLiteral("launch")))
                                : nullptr;
        QWidget *form = panelScroll != nullptr ? panelScroll->widget() : nullptr;
        check(form != nullptr, "Z2 前提：找得到「启动」页那张设置表单");
        QWidget *launchForm = form;

        // 「文件关联」那一组已经撤了（issue #21：不再注册系统关联，拖入文件时选实例）。
        {
            int associationRows = 0;
            if (launchForm != nullptr) {
                for (const QLabel *label : launchForm->findChildren<QLabel *>())
                    if (label->text().contains(QStringLiteral("文件关联"))) ++associationRows;
            }
            check(associationRows == 0,
                  "Z2 「启动」页不再有「文件关联」那一组",
                  QString::number(associationRows));
        }

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
                    // 开关行：行标题就是勾选框那句话（合并成一句），且整页只画一次。
                    int rowTitles = 0;
                    for (const QLabel *label : launchForm->findChildren<QLabel *>())
                        if (label->text() == MainWindowUI::showGameLogRowText()) ++rowTitles;
                    check(rowTitles == 1 &&
                              MainWindowUI::showGameLogRowText() == MainWindowUI::showGameLogCheckText(),
                          "Z2 游戏日志开关：行标题与勾选框是同一句，行标题只画一次",
                          QString::number(rowTitles));
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
            // 切到某页并取回那张表单（量完由最后那一次切回「启动」页）。
            const auto formOfCategory = [settingsPage, switchSettingsTab](const QString &key) -> QWidget * {
                switchSettingsTab(key);
                auto *scroll =
                    settingsPage->findChild<QScrollArea *>(MainWindowUI::settingsPanelName(key));
                return scroll != nullptr ? scroll->widget() : nullptr;
            };
            // 一条 = 哪一栏 + 那个下拉框的 objectName。
            const QList<QPair<QString, QString>> combos = {
                {QStringLiteral("launch"), MainWindowUI::processPriorityComboName()},
                {QStringLiteral("launch"), MainWindowUI::gameWindowModeComboName()},
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
                    // 行 = 左标签 + 右侧固定 220 的控件列：下拉铺满这一列，不多不少。
                    check(combo->width() > 0 && combo->width() <= SettingsParts::kControlColumn + 1,
                          QStringLiteral("Z2 「%1」落在右侧 220 控件列里（没被拉去顶满这一栏）")
                              .arg(comboWhere.second),
                          QStringLiteral("w=%1").arg(combo->width()));
                }
            }
            // 切回「启动」页：这一段后面的节假设设置页停在 Z2 开头那一页上。
            formOfCategory(QStringLiteral("launch"));
        }

        // ---- (e) 「窗口大小 · 恢复默认」（UI 问题 #11）----
        // 出厂那对数是原版自己的 854×480（`MainWindowUI::windowDefaultWidth/Height()`）。
        // 置灰的两条判据：已经是默认值；或者那一行因为窗口模式整行灰着（后一条在 mw_check，
        // 那边才有 MainWindow 按 GameWindowMode 灰/亮这一行的那条线）。
        {
            auto *widthSpin = form != nullptr
                                  ? form->findChild<QSpinBox *>(MainWindowUI::gameWindowWidthSpinName())
                                  : nullptr;
            auto *heightSpin = form != nullptr ? form->findChild<QSpinBox *>(
                                                     MainWindowUI::gameWindowHeightSpinName())
                                               : nullptr;
            auto *resetButton =
                form != nullptr
                    ? form->findChild<QPushButton *>(MainWindowUI::gameWindowSizeResetButtonName())
                    : nullptr;
            check(resetButton != nullptr && resetButton->text() == QStringLiteral("恢复默认"),
                  "★ I11 「窗口大小」那一行最右边有「恢复默认」",
                  resetButton != nullptr ? resetButton->text() : QStringLiteral("<null>"));
            check(MainWindowUI::windowDefaultWidth() == 854 && MainWindowUI::windowDefaultHeight() == 480,
                  "I11 出厂默认那一对是 854×480",
                  QString("%1×%2").arg(MainWindowUI::windowDefaultWidth()).arg(MainWindowUI::windowDefaultHeight()));
            if (resetButton != nullptr && widthSpin != nullptr && heightSpin != nullptr) {
                // 刚搭好时两个框停在下限（320×240）：不是默认值，按钮该是亮的。
                check(resetButton->isEnabled(), "★ I11 值不是默认时「恢复默认」可点",
                      QString("%1×%2").arg(widthSpin->value()).arg(heightSpin->value()));
                widthSpin->setValue(MainWindowUI::windowDefaultWidth());
                heightSpin->setValue(MainWindowUI::windowDefaultHeight());
                QApplication::processEvents();
                check(!resetButton->isEnabled(), "★★ I11 已经是 854×480：「恢复默认」置灰");
                heightSpin->setValue(MainWindowUI::windowDefaultHeight() + MainWindowUI::windowSizeStep());
                QApplication::processEvents();
                check(resetButton->isEnabled(), "★ I11 改成 854×496 又变回可点");
                resetButton->click();
                QApplication::processEvents();
                check(widthSpin->value() == MainWindowUI::windowDefaultWidth() &&
                          heightSpin->value() == MainWindowUI::windowDefaultHeight(),
                      "★★ I11 点「恢复默认」：两个框回到 854×480",
                      QString("%1×%2").arg(widthSpin->value()).arg(heightSpin->value()));
                check(!resetButton->isEnabled(), "★ I11 恢复之后按钮自己置灰");
            }
        }
    }
    return 0;
}


