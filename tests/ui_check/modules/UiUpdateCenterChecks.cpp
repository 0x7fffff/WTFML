#include "Support.hpp"
int UiUpdateCenterChecks() {
    // ------------------------------------------------------------------
    // 界面的那一半分三处验：判据层（忽略 / 固定那本账）在 verify 的 ZT / ZU 两节、
    // **开页那一半**（谁把哪一份结论递进来、点下去走哪条路）在 mw_check 的 T32，
    // **这一页长什么样**在这里。
    //
    // 盯的是三件在纯数据上看不出来的事：
    // ① 两组的**标题带条数**、而且能动手的那一组在上面（用户点进来就是来看它的）；
    // ② "能不能勾"和"底下那颗按钮"是同一件事（勾了才点得动、勾了几条就写几条）；
    // ③ 已经"别再提示我"的那几条**勾不上**（用户刚说完别动它，紧接着被批量更新一起换掉，
    // 是这一页最不该发生的事）—— 而它们自己那颗「更新」照旧在。
    {
        const QString outDate = QStringLiteral("D:/mc/versions/1.20.1/mods/jei.jar");
        const QString pinnedPath = QStringLiteral("D:/mc/versions/1.20.1/mods/sodium.jar");
        const QString cleanPath = QStringLiteral("D:/mc/versions/1.20.1/shaderpacks/光影.zip");
        const QString unknownPath = QStringLiteral("D:/mc/versions/1.20.1/mods/自己改的.jar");

        UpdateCenterPage::Report report;
        report.instanceName = QStringLiteral("1.20.1-我的整合包");
        report.summary = QStringLiteral("查了 41 个文件，2 个有新版本。");
        report.note = QStringLiteral("有 1 个文件读不动，那几条没算进来。");

        UpdateCenterPage::Row outdated;
        outdated.path = outDate;
        outdated.name = QStringLiteral("JEI 物品管理器");
        outdated.fileName = QStringLiteral("jei.jar");
        outdated.currentText = QStringLiteral("15.2.0");
        outdated.targetText = QStringLiteral("15.3.1");
        outdated.state = UpdateCenterPage::State::Outdated;
        outdated.targetVersionId = QStringLiteral("ver-1531");
        outdated.currentSha1 = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        report.rows.append(outdated);

        // 已固定的那一条：状态还是"有新版本"，但用户说过"别动它" ⇒ 勾不上。
        UpdateCenterPage::Row pinned;
        pinned.path = pinnedPath;
        pinned.name = QStringLiteral("Sodium");
        pinned.fileName = QStringLiteral("sodium.jar");
        pinned.currentText = QStringLiteral("0.5.8");
        pinned.targetText = QStringLiteral("0.6.0");
        pinned.state = UpdateCenterPage::State::Outdated;
        pinned.suppress = UpdateCenterPage::Suppress::Pinned;
        pinned.targetVersionId = QStringLiteral("ver-060");
        pinned.currentSha1 = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        report.rows.append(pinned);

        UpdateCenterPage::Row clean;
        clean.path = cleanPath;
        clean.name = QStringLiteral("光影包");
        clean.fileName = QStringLiteral("光影.zip");
        clean.currentText = QStringLiteral("2.1");
        clean.state = UpdateCenterPage::State::UpToDate;
        report.rows.append(clean);

        UpdateCenterPage::Row unknown;
        unknown.path = unknownPath;
        unknown.name = QStringLiteral("自己改的.jar");
        unknown.fileName = QStringLiteral("自己改的.jar");
        unknown.state = UpdateCenterPage::State::Unknown;
        unknown.note = QStringLiteral("两边的表里都没有这个 sha1。");
        report.rows.append(unknown);

        report.undoFileCount = 3;
        report.undoText = QStringLiteral("撤销上一次更新（3 个文件）");

        UpdateCenterPage page;
        page.resize(760, 560);
        page.show();
        QApplication::processEvents();
        page.setReport(report);
        QApplication::processEvents();
        // **必须自己把延迟删除派发掉**（这一句不是多余的）。
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents();

        const auto labelBy = [&page](const QString &name) {
            return page.findChild<QLabel *>(name);
        };
        const auto buttonBy = [&page](const QString &name) {
            return page.findChild<QPushButton *>(name);
        };
        const auto rowBy = [&page](int index) {
            return page.findChild<QWidget *>(MainWindowUI::updateCenterRowName(index));
        };

        check(page.objectName() == MainWindowUI::updateCenterPageName()
                  && page.windowTitle() == UpdateCenterPage::windowTitle(),
              "★ U33.1 这一页认得出自己，页眉那句话就是 windowTitle()（唯一出处）",
              page.objectName() + QLatin1Char('/') + page.windowTitle());

        QLabel *summary = labelBy(MainWindowUI::updateCenterSummaryName());
        check(summary != nullptr
                  && summary->text()
                         == QStringLiteral("「%1」%2").arg(report.instanceName, report.summary),
              "★ U33.2 顶上那句总述 =「实例名 + 一句话结论」，一个字不差",
              summary != nullptr ? summary->text() : QStringLiteral("没画"));
        QLabel *note = labelBy(MainWindowUI::updateCenterNoteName());
        check(note != nullptr && note->isVisible() && note->text() == report.note,
              "U33.2 反查那一趟自己的毛病（几个文件读不动）**单独一句**摆在下面"
              "（和「这一趟根本没有结论」不是一回事）",
              note != nullptr ? note->text() : QStringLiteral("没画"));
        QLabel *error = labelBy(MainWindowUI::updateCenterErrorName());
        check(error != nullptr && !error->isVisible(),
              "U33.2 有结论的时候「没有结论」那一块**收着**（不是摆一块空的出来）");

        // ---------- 两组：标题带条数、能动手的在上 ----------
        const QStringList expectedTitles{
            MainWindowUI::updateCenterOutdatedGroupTitleText(2),
            MainWindowUI::updateCenterRestGroupTitleText(2)};
        QStringList actualTitles;
        for (int g = 0; g < 2; ++g) {
            QLabel *title = labelBy(MainWindowUI::updateCenterGroupName(g));
            actualTitles.append(title != nullptr ? title->text() : QStringLiteral("(没画)"));
        }
        check(actualTitles == expectedTitles,
              "★★ U33.3 两组**一个不少**、顺序是「有新版本」在上（用户点进来就是来看它的）、"
              "「已是最新或说不清」在下，两个标题都带条数",
              actualTitles.join(QStringLiteral(" / ")));
        check(rowBy(0) != nullptr && rowBy(1) != nullptr && rowBy(2) != nullptr
                  && rowBy(3) != nullptr && rowBy(4) == nullptr,
              "★★ U33.3 四行按顺序摆在两组里（下标连着，不是每组从头数）");

        // ---------- 一行里那几格 ----------
        QLabel *row0Name = labelBy(MainWindowUI::updateCenterRowNameLabelName(0));
        check(row0Name != nullptr
                  && row0Name->text()
                         == MainWindowUI::updateCenterRowLabelText(outdated.name,
                                                                  outdated.fileName),
              "★ U33.4 第一行写的是**名字**（不是文件名）", row0Name != nullptr ? row0Name->text()
                                                                             : QStringLiteral("没画"));
        QLabel *row0File = labelBy(MainWindowUI::updateCenterRowFileNameLabelName(0));
        check(row0File != nullptr && row0File->text() == outdated.fileName
                  && row0File->isVisible(),
              "U33.4 名字下面那行小字写的是盘上那个文件的名字（用户要拿它去盘上找）",
              row0File != nullptr ? row0File->text() : QStringLiteral("没画"));
        QLabel *row0State = labelBy(MainWindowUI::updateCenterRowStateName(0));
        check(row0State != nullptr
                  && row0State->text()
                         == MainWindowUI::updateCenterRowStateText(
                             int(outdated.state), int(outdated.suppress), outdated.targetText),
              "★★ U33.4 状态那一格说的是**要换成哪一版**（这一句是「它现在算不算更新」的"
              "唯一说法）",
              row0State != nullptr ? row0State->text() : QStringLiteral("没画"));

        // 「说不清」那一档：右栏就那三个字，缘由在鼠标提示里（那一格说不下）。
        QLabel *row3State = labelBy(MainWindowUI::updateCenterRowStateName(3));
        check(row3State != nullptr
                  && row3State->text() == MainWindowUI::updateCenterRowStateText(
                         int(UpdateCenterPage::State::Unknown), 0, QString())
                  && row3State->text() == ModDependencies::updateUnknownText()
                  && row3State->toolTip() == unknown.note,
              "★★ U33.4 「说不清」那一行右边就写那三个字、缘由挂在 tooltip 上"
              "（写成「没有问题」是这一页最坏的一种错）",
              row3State != nullptr ? row3State->text() : QStringLiteral("没画"));

        // ---------- 勾选框：勾得上的、勾不上的 ----------
        auto *check0 = page.findChild<QCheckBox *>(MainWindowUI::updateCenterRowCheckName(0));
        auto *check1 = page.findChild<QCheckBox *>(MainWindowUI::updateCenterRowCheckName(1));
        auto *check2 = page.findChild<QCheckBox *>(MainWindowUI::updateCenterRowCheckName(2));
        check(check0 != nullptr && check0->isEnabled() && !check0->isChecked(),
              "★ U33.5 有更新、没被按住的那一条**勾得上**（刚开页时一律不勾）");
        check(check1 != nullptr && !check1->isEnabled() && !check1->isChecked(),
              "★★ U33.5 已固定的那一条**勾不上**（用户刚说完「别动它」，紧接着被批量更新一起"
              "换掉是这一页最不该发生的事）");
        check(check2 != nullptr && !check2->isEnabled(),
              "★★ U33.5 「已是最新」那条一样勾不上（勾了也没有下一件事可做）");
        // 勾不上的那几行**占着同一格宽度**：不保留尺寸的话两组左边参差不齐。
        check(check1 != nullptr
                  && check1->sizePolicy().retainSizeWhenHidden(),
              "★ U33.5 勾不上 / 藏起来的那一格**保留尺寸**（不保留的话两组左边参差不齐）");

        // ---------- 底下那颗按钮：勾了几条就写几条、一条没勾点不动 ----------
        QPushButton *updateSelected =
            buttonBy(MainWindowUI::updateCenterUpdateSelectedName());
        check(updateSelected != nullptr && !updateSelected->isEnabled()
                  && updateSelected->text() == MainWindowUI::updateCenterUpdateSelectedText(0),
              "★★ U33.6 一条都没勾 ⇒ 底下那颗**点不动**，而且不写「更新选中的 0 项」"
              "（那句话没有意义）",
              updateSelected != nullptr ? updateSelected->text() : QStringLiteral("没画"));
        QStringList requested;
        QObject::connect(&page, &UpdateCenterPage::updateRequested, &page,
                         [&requested](const QStringList &paths) { requested = paths; });
        if (check0 != nullptr) {
            check0->setChecked(true);
            QApplication::processEvents();
        }
        check(updateSelected != nullptr && updateSelected->isEnabled()
                  && updateSelected->text() == MainWindowUI::updateCenterUpdateSelectedText(1),
              "★★ U33.6 勾上一条 ⇒ 那颗按钮变成「更新选中的 1 项」而且能点",
              updateSelected != nullptr ? updateSelected->text() : QStringLiteral("没画"));
        check(page.checkedPaths() == QStringList{outDate},
              "★ U33.6 勾中的那几条按**列表顺序**报出来（更新那一批下完之后任务里那几步、"
              "日志里那几行都照这个顺序念）",
              page.checkedPaths().join(QStringLiteral(" / ")));
        if (updateSelected != nullptr) {
            updateSelected->click();
        }
        check(requested == QStringList{outDate},
              "★★ U33.6 点下去发的是**勾中的那几条**的路径", requested.join(QStringLiteral(" / ")));

        // ---------- 行尾那三颗 chip ----------
        const auto chipText = [&page](const QString &name) {
            auto *button = page.findChild<QPushButton *>(name);
            return button != nullptr ? button->text() : QStringLiteral("(没画)");
        };
        check(chipText(MainWindowUI::updateCenterRowUpdateName(0)) ==
                      MainWindowUI::updateCenterRowUpdateText()
                  && chipText(MainWindowUI::updateCenterRowIgnoreName(0)) ==
                      MainWindowUI::updateCenterRowIgnoreText()
                  && chipText(MainWindowUI::updateCenterRowPinName(0)) ==
                      MainWindowUI::updateCenterRowPinText(),
              "★ U33.7 有更新那一行末尾是那三颗：更新 / 忽略这一版 / 固定这一版",
              chipText(MainWindowUI::updateCenterRowIgnoreName(0)));
        check(chipText(MainWindowUI::updateCenterRowPinName(1)) ==
                  MainWindowUI::updateCenterRowPinUndoText(),
              "★★ U33.7 已固定的那一条那颗写的是「不再固定」（同一个位置换一句话 —— "
              "写「固定这一版」的话用户按下去会以为自己在固定、其实是在取消）",
              chipText(MainWindowUI::updateCenterRowPinName(1)));
        check(page.findChild<QPushButton *>(MainWindowUI::updateCenterRowUpdateName(2)) == nullptr
                  && page.findChild<QPushButton *>(MainWindowUI::updateCenterRowUpdateName(3)) ==
                         nullptr,
              "★ U33.7 另外两组（已是最新 / 说不清）的行尾**一颗 chip 都没有**"
              "（那几句本来就是说明，没有下一步可做）");

        // 点一颗 chip：各发各的信号，参数是那一行的路径。
        QString toggledPath;
        bool toggledValue = false;
        QObject::connect(&page, &UpdateCenterPage::ignoreToggled, &page,
                         [&toggledPath, &toggledValue](const QString &path, bool ignored) {
                             toggledPath = path;
                             toggledValue = ignored;
                         });
        // 固定那一档是**另一个信号**（`pinToggled`），得单独接一条 —— 不接的话点下去什么都
        // 收不到，而屏幕上那颗按钮看着一点毛病都没有（下面那条断言就是当年这么红的）。
        QObject::connect(&page, &UpdateCenterPage::pinToggled, &page,
                         [&toggledPath, &toggledValue](const QString &path, bool pinned) {
                             toggledPath = path;
                             toggledValue = pinned;
                         });
        if (auto *ignore = page.findChild<QPushButton *>(
                MainWindowUI::updateCenterRowIgnoreName(0))) {
            ignore->click();
        }
        check(toggledPath == outDate && toggledValue,
              "★ U33.7 「忽略这一版」发的是（那一行的路径, true）",
              toggledPath + QStringLiteral(" / ") + QString::number(toggledValue ? 1 : 0));
        // 已固定的那一行那颗是"取消"：发的是 false。
        toggledPath.clear();
        toggledValue = true;
        if (auto *pin = page.findChild<QPushButton *>(MainWindowUI::updateCenterRowPinName(1))) {
            pin->click();
        }
        check(toggledPath == pinnedPath && !toggledValue,
              "★★ U33.7 「不再固定」发的是（那一行的路径, false）—— 「点完之后该是什么样」"
              "由这一页说，MainWindow 不再自己猜一遍");

        // ---------- 一行那颗「更新」是"逐项"那一档 ----------
        requested.clear();
        if (auto *one = page.findChild<QPushButton *>(
                MainWindowUI::updateCenterRowUpdateName(0))) {
            one->click();
        }
        check(requested == QStringList{outDate},
              "★★ U33.7 一行上那颗「更新」发的是**就这一条**（逐项那一档）",
              requested.join(QStringLiteral(" / ")));

        // ---------- 「撤销上一次更新」 ----------
        QPushButton *undo = buttonBy(MainWindowUI::updateCenterUndoName());
        check(undo != nullptr && undo->isVisible() && undo->text() == report.undoText,
              "★ U33.8 有回滚点的时候底下露出那颗「撤销上一次更新（N 个文件）」",
              undo != nullptr ? undo->text() : QStringLiteral("没画"));
        int undoClicks = 0;
        QObject::connect(&page, &UpdateCenterPage::undoRequested, &page,
                         [&undoClicks] { ++undoClicks; });
        if (undo != nullptr) {
            undo->click();
        }
        check(undoClicks == 1, "★ U33.8 点一下正好发一条 undoRequested", QString::number(undoClicks));

        // ---------- 没有回滚点 / 还没查过 ----------
        {
            UpdateCenterPage fresh;
            fresh.resize(760, 560);
            fresh.show();
            QApplication::processEvents();
            UpdateCenterPage::Report none;
            none.instanceName = QStringLiteral("1.20.1-我的整合包");
            none.error = QStringLiteral("这个实例还没查过更新。");
            fresh.setReport(none);
            QApplication::processEvents();

            auto *freshUndo = fresh.findChild<QPushButton *>(
                MainWindowUI::updateCenterUndoName());
            check(freshUndo != nullptr && !freshUndo->isVisible(),
                  "★★ U33.9 没有回滚点的时候那颗「撤销上一次更新」**整颗收起来**"
                  "（给一颗点了什么都不做的按钮，比不给还糟）",
                  freshUndo != nullptr ? QStringLiteral("露着") : QStringLiteral("没画"));
            auto *freshError = fresh.findChild<QLabel *>(
                MainWindowUI::updateCenterErrorName());
            check(freshError != nullptr && freshError->isVisible()
                      && freshError->text().contains(MainWindowUI::updateCenterEmptyTitleText())
                      && freshError->text().contains(none.error)
                      && freshError->text().contains(MainWindowUI::updateCenterEmptyHintText()),
                  "★★ U33.9 没查过的时候那一整块写的是「一句大话 + 为什么 + 下一步去哪儿」"
                  "（只说「没有结果」等于把用户扔在原地）",
                  freshError != nullptr ? freshError->text() : QStringLiteral("没画"));
            auto *freshRow0 =
                fresh.findChild<QWidget *>(MainWindowUI::updateCenterRowName(0));
            check(freshRow0 == nullptr,
                  "★ U33.9 没有结论时列表**整个是空的**（不是摆一堆空行）");
            auto *freshUpdate = fresh.findChild<QPushButton *>(
                MainWindowUI::updateCenterUpdateSelectedName());
            check(freshUpdate != nullptr && !freshUpdate->isEnabled(),
                  "★ U33.9 底下那颗主按钮照旧点不动");
            fresh.close();
            QApplication::processEvents();
        }

        page.close();
        QApplication::processEvents();
    }
    return 0;
}


