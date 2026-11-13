#include "Support.hpp"
#include "ActivityPopover.hpp"
#include <QVariantAnimation>
#include "TaskCenterPanel.hpp"
#include <QMouseEvent>
#include <QThread>
int UiUiRefreshCheck(QApplication &app) {

        const int fontId = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc")));
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) app.setFont(QFont(families.first(), 9));
        app.setStyle(Theme::createStyle());
        Theme::setSpec(Theme::defaultSpec());
        Theme::apply(app);
        Anim::setEnabled(false);
        QMainWindow preview;
        MainWindowUI ui(&preview);
        preview.resize(1000, 650);
        preview.show();
        app.processEvents();
        int failures = 0;
        const auto expect = [&failures](bool ok, const char *message) {
            printf("%s %s\n", ok ? "PASS" : "FAIL", message);
            if (!ok) ++failures;
        };
        expect(ui.getTabWidget()->tabText(MainWindowUI::instancesTabIndex()) == QStringLiteral("管理"), "management navigation label");
        auto *launchSettings = preview.findChild<QWidget *>(MainWindowUI::settingsPanelName(QStringLiteral("launch")));
        auto *warningSettings = preview.findChild<QWidget *>(MainWindowUI::settingsPanelName(QStringLiteral("warn")));
        expect(launchSettings && launchSettings->isAncestorOf(preview.findChild<QWidget *>(MainWindowUI::showGameLogCheckName())) &&
                   launchSettings->isAncestorOf(preview.findChild<QWidget *>(MainWindowUI::processPriorityComboName())),
               "game logs and process priority belong to launch settings");
        expect(warningSettings && warningSettings->isAncestorOf(preview.findChild<QWidget *>(MainWindowUI::warnMissingDependenciesCheckName())),
               "dependency warning belongs to safety settings");
        // 7a：实例带只剩「最近 / 收藏」两颗胶囊页签（设计稿 r2 .seg：28 高），「当前」是上面那块启动区。
        for (const QString &name : {QStringLiteral("homeQuickRecentTab"), QStringLiteral("homeQuickFavoriteTab")}) {
            auto *tab = preview.findChild<QPushButton *>(name);
            expect(tab && tab->minimumSizeHint().width() >= 48 && tab->sizeHint().height() >= 28,
                   "home strip tabs keep a usable click target");
        }
        auto *currentTab = preview.findChild<QPushButton *>(QStringLiteral("homeQuickCurrentTab"));
        expect(currentTab && currentTab->isHidden(), "the current instance is the launch block, not a strip tab");
        // 「全部实例 ›」改成「管理」：纯导航，点了去管理页（不在主页里展开列表）。
        if (auto *manage = preview.findChild<QPushButton *>(MainWindowUI::homeInstanceSelectButtonName())) {
            expect(manage->text() == QStringLiteral("管理") && manage->toolTip().isEmpty(),
                   "strip header link reads 管理 with no tooltip");
            manage->click();
            app.processEvents();
            expect(ui.getTabWidget()->currentIndex() == MainWindowUI::instancesTabIndex(),
                   "管理 navigates to the instances page");
            ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
            app.processEvents();
        } else expect(false, "strip header link exists");
        MainWindowUI::HomePickerInstance instance;
        instance.id = QStringLiteral("custom-instance");
        instance.title = QStringLiteral("Adventure");
        instance.folderPath = QStringLiteral("D:/Games/Minecraft");
        instance.loaderKey = QStringLiteral("fabric");
        instance.favorite = true;
        instance.recent = true;
        instance.recentRank = 0;
        instance.current = true;
        instance.searchKeywords = QStringLiteral("1.21.1 0.16.9 我的整合包");
        {
            // 弹层锚在启动区那块「名字 + 版本」上：先摆一张当前实例卡。
            MainWindowUI::HomeQuickItem current;
            current.key = instance.id;
            current.name = instance.title;
            ui.setHomeQuickRows({current}, {}, {});
            app.processEvents();
        }
        ui.openHomeInstancePicker({}, {instance}, {}, {});
        app.processEvents();
        auto *search = preview.findChild<QLineEdit *>(QStringLiteral("homePickerSearch"));
        auto *list = preview.findChild<QListWidget *>(QStringLiteral("homePickerInstanceList"));
        if (search && list) {
            // 不搜的时候分两段：「最近」+「全部 · N」（两行段头 + 两行实例）。
            expect(list->count() == 4 && list->item(0)->text() == QStringLiteral("最近") &&
                       list->item(2)->text() == QStringLiteral("全部 · 1"),
                   "picker lists 最近 then 全部 · N");
            for (const QString &query : {QStringLiteral("1.21 fabric 收藏"), QStringLiteral("0.16.9"),
                                        QStringLiteral("games recent"), QStringLiteral("我的整合包")}) {
                search->setText(query);
                expect(list->count() == 1, "expanded search keywords match");
            }
            search->setText(QStringLiteral("1.21 forge"));
            expect(list->count() == 0, "all search terms must match");
        } else expect(false, "home search controls exist");
        expect(preview.findChild<QPushButton *>(QStringLiteral("homePickerNew")) &&
                   preview.findChild<QPushButton *>(QStringLiteral("homePickerManage")),
               "picker footer has 新建实例 and 管理全部实例");
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
        expect(!ui.homeInstancePickerOpen(), "switching away resets hidden home picker");
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        expect(!ui.homeInstancePickerOpen(), "returning home does not restore stale picker");
        ui.openHomeInstancePicker({}, {instance}, {}, {});
        app.processEvents();
        if (auto *picker = preview.findChild<QWidget *>(MainWindowUI::homeInstancePickerName())) {
            QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(picker, &esc);
            app.processEvents();
        }
        expect(!ui.homeInstancePickerOpen(), "escape closes home picker");
        ui.closeHomeInstancePicker();
        auto *button = ui.getToolboxButton();
        // 7a：工具箱是左导航轨底部的一项，和导航项一样铺满轨宽（不再按字宽收缩）。
        expect(button->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding &&
                   button->parentWidget() != nullptr &&
                   button->width() * 10 >= button->parentWidget()->width() * 8,
               "toolbox trigger fills the rail footer width like a nav item");
        // 点「工具箱」直接切到工具箱选项卡并高亮，没有弹出框；已经在工具箱页上再点 = 回目录（交给 handler）。
        QStringList toolboxKeys;
        ui.setToolboxToolHandler([&toolboxKeys](const QString &key) { toolboxKeys << key; });
        expect(!button->isChecked(), "toolbox trigger idle before opening");
        button->click();
        expect(ui.getTabWidget()->currentIndex() == MainWindowUI::toolboxTabIndex() && button->isChecked() &&
                   toolboxKeys == QStringList{QString()},
               "toolbox button switches straight to the toolbox tab, no popup");
        button->click();
        expect(toolboxKeys == QStringList({QString(), QStringLiteral("catalog")}),
               "second click on the toolbox tab goes back to the catalog");
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        expect(!button->isChecked(), "toolbox trigger highlight follows the current tab");
        AccountSkinDialog account;
        account.setStatusText(QStringLiteral("当前账号：Player（离线登录）"));
        account.setAccounts({{QStringLiteral("offline-player"), QStringLiteral("Player"), {}, true, true},
                             {QStringLiteral("other-player"), QStringLiteral("Adventure"), {}, false, true}});
        // （要求「那个账号页的宽度恢复一下，高度窄一点」）：这一页是**内嵌页**，
        // 摆进内容区之后宽高都由内容区说了算 ⇒ 这里量的是**它自己那两道约束**：
        // 宽**一道都不许有**（这一页铺满内容区 ⇒ 谁给它加一句 `setMaximumWidth` 这一条
        // 当场红）、高要有盖（否则它一路长到窗口底）。
        account.resize(900, 900);
        account.show();
        app.processEvents();
        expect(account.width() == 900, "account page width is not capped");
        auto *accountsPane = account.findChild<QWidget *>(QStringLiteral("accountManagementPane"));
        expect(accountsPane && accountsPane->width() <= 340,
               "account management pane stays compact instead of consuming half the page");
        auto *status = account.findChild<QLabel *>(AccountSkinDialog::statusLabelName());
        auto *pageTitle = account.findChild<QLabel *>(QStringLiteral("accountPageTitle"));
        expect(pageTitle && status && status->alignment().testFlag(Qt::AlignLeft) &&
                   status->mapTo(&account, QPoint()).y() > pageTitle->mapTo(&account, QPoint()).y(),
               "account page has a big title with the identity as its one-line subtitle (design .page-head)");
        auto *viewer = account.findChild<SkinViewer *>(AccountSkinDialog::viewerName());
        expect(viewer && viewer->width() == 208 && viewer->height() == 312,
               "account character preview has bounded dimensions");
        auto *login = account.findChild<QPushButton *>(AccountSkinDialog::loginButtonName());
        auto *body = account.findChild<QWidget *>(QStringLiteral("accountBody"));
        expect(body && body->height() < 520 && login &&
               login->mapTo(&account, QPoint()).y() < 280,  // 大标题 + 副标题之下：小节名、账号列表、「登录新账号…」
               "account controls stay top aligned without distributing surplus height into gaps");
        expect(login && login->grab().toImage().pixelColor(8, 8) == Theme::tokens().primary,
               "default login button keeps solid primary background");
        if (app.arguments().contains(QStringLiteral("--ui-refresh-preview")))
            account.grab().save(QStringLiteral("build/account-layout-preview.png"));
        account.resize(720, 700);
        app.processEvents();
        expect(accountsPane && viewer &&
               viewer->mapTo(&account, QPoint()).y() > accountsPane->geometry().bottom(),
               "narrow account page stacks sections instead of clipping horizontal controls");
        account.hide();
        preview.resize(1200, 760);
        app.processEvents();
        auto *accountHost = ui.overlay();
        auto *accountArea = accountHost->openPage(AccountSkinDialog::windowTitle());
        expect(accountArea != nullptr, "account page opens in the real overlay shell");
        if (accountArea) {
            account.setParent(accountArea, Qt::Widget);
            accountArea->layout()->addWidget(&account);
            account.show();
            app.processEvents();
            auto *header = account.findChild<QWidget *>(QStringLiteral("accountHeaderBar"));
            // 设计稿：面包屑下面一行大标题（身份副标题 + 右侧「刷新外观」），留在本页顶上。
            expect(header && header->parentWidget() == &account && header->isVisible() && header->y() <= 16,
                   "embedded account page keeps its title row at the top of the page");
            expect(body && header && body->y() <= header->geometry().bottom() + 16,
                   "embedded account content follows the title row directly");
            if (app.arguments().contains(QStringLiteral("--ui-refresh-preview")))
                preview.grab().save(QStringLiteral("build/account-embedded-preview.png"));
            accountHost->closeOverlay();
            expect(header && header->parentWidget() == &account,
                   "closing account page returns header ownership safely");
        }
        {
            class MountProbe final : public QDialog {
            public:
                int parentChanges = 0;
                bool event(QEvent *event) override {
                    if (event->type() == QEvent::ParentChange) ++parentChanges;
                    return QDialog::event(event);
                }
            } page;
            class EnabledProbe final : public QObject {
            public:
                int changes = 0;
                bool eventFilter(QObject *, QEvent *event) override {
                    if (event->type() == QEvent::EnabledChange) ++changes;
                    return false;
                }
            } enabledProbe;
            QWidget *covered = ui.getTabWidget()->currentWidget();
            covered->installEventFilter(&enabledProbe);
            expect(accountHost->showPage(&page, QStringLiteral("Mount probe")),
                   "nonmodal page mounts successfully");
            expect(page.parentChanges == 1 && !page.isWindow()
                       && page.parentWidget()->layout()->indexOf(&page) >= 0,
                   "mounting reparents the dialog once directly into its final layout");
            const int changesAfterOpen = enabledProbe.changes;
            accountHost->setPromptBlockedWidget(nullptr);
            accountHost->setPromptBlockedWidget(button->parentWidget());
            expect(!covered->isEnabled() && enabledProbe.changes == changesAfterOpen,
                   "retargeting the same covered page does not enable and disable its subtree again");
            accountHost->closeOverlay();
            expect(covered->isEnabled(), "closing the page restores underlying keyboard access");
            covered->removeEventFilter(&enabledProbe);
            InstanceSettingsDialog embedded(QStringLiteral("Embedded"), QStringLiteral("Embedded"),
                                            {}, {}, {}, true, {}, accountHost->pageContainer(), true);
            expect(!embedded.isWindow() && embedded.parentWidget() == accountHost->pageContainer(),
                   "instance settings are constructed as a child of the final page container");
            expect(accountHost->showPage(&embedded, QStringLiteral("Embedded instance")),
                   "preconstructed instance settings mount without changing parents");
            embedded.reject();
            expect(!accountHost->isOpen(), "embedded instance settings retain normal close semantics");
        }
        QList<TaskCenter::RunningItem> runningItems;
        TaskCenterPanel running(nullptr, {}, {}, [&runningItems]() { return runningItems; }, {}, {}, nullptr, true);
        running.resize(1000, 500);
        running.showEmbedded();
        app.processEvents();
        auto *detailPane = running.findChild<QWidget *>(QStringLiteral("taskCenterDetailPane"));
        expect(detailPane && detailPane->width() == 270,
               "task center detail pane scales with the workspace (27% of 1000)");
        // 没有实例在跑 ⇒ 左边那一栏默认收起（要求：「左侧没有实例运行时默认不展开」）。
        auto *runningToggle = running.findChild<QPushButton *>(
            QStringLiteral("taskCenterRunningCollapse"));
        auto *runningPane = running.findChild<QWidget *>(QStringLiteral("taskCenterRunningPane"));
        expect(runningToggle && runningPane && !runningToggle->isChecked()
                   && runningPane->width() == 36,
               "running pane starts collapsed when nothing is running");
        running.resize(1400, 500);
        app.processEvents();
        expect(detailPane && detailPane->width() == 378,
               "wider workspace gives the detail pane more room (27% of 1400)");
        running.resize(700, 500);
        app.processEvents();
        running.resize(1000, 500);
        app.processEvents();
        expect(detailPane && detailPane->width() == 270, "detail width is restored after compact layout");
        TaskCenter::RunningItem game;
        game.key = QStringLiteral("session-1");
        game.title = QStringLiteral("Live instance");
        runningItems.append(game);
        running.requestRefresh();
        running.requestRefresh();
        app.processEvents();
        expect(running.findChildren<QWidget *>(QStringLiteral("taskCenterRunningRow")).size() == 1,
               "running instance appears without reopening panel");
        // 有实例跑起来了 ⇒ 同一句判据的另一头：自动展开（用户没亲手点过）。
        expect(runningToggle && runningToggle->isChecked() && runningPane->width() == 220,
               "running pane auto-expands when an instance starts");
        runningItems.clear();
        running.requestRefresh();
        app.processEvents();
        expect(running.findChildren<QWidget *>(QStringLiteral("taskCenterRunningRow")).isEmpty(),
               "finished instance disappears without reopening panel");
        expect(runningToggle && !runningToggle->isChecked() && runningPane->width() == 36,
               "running pane auto-collapses when the last instance exits");
        running.hide();
        runningItems.append(game);
        running.requestRefresh();
        app.processEvents();
        running.show();
        app.processEvents();
        expect(running.findChildren<QWidget *>(QStringLiteral("taskCenterRunningRow")).size() == 1,
               "parked task center resyncs when shown");
        {
            QList<TaskCenter::Item> longTasks;
            TaskCenter::Item longItem;
            longItem.key = QStringLiteral("long");
            longItem.title = QString(90, QLatin1Char('W')) + QStringLiteral(".jar");
            longItem.state = TaskCenter::State::Running;
            longItem.capabilities.cancel = true;
            longTasks << longItem;
            TaskCenterPanel longPanel(nullptr, [&longTasks] { return longTasks; }, {},
                                      {}, {}, {}, nullptr, true);
            longPanel.setRowActionHandler(
                [](const QString &, TaskCenter::RowAction) {});
            longPanel.resize(1000, 500);
            longPanel.showEmbedded();
            app.processEvents();
            auto *longRow =
                longPanel.findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(0));
            auto *longCancel = longPanel.findChild<QPushButton *>(
                MainWindowUI::taskCenterPanelRowCancelName(0));
            auto *list = longPanel.findChild<QScrollArea *>(QStringLiteral("taskCenterTaskList"));
            expect(longRow && longCancel && list && list->viewport(),
                   "long task name still shows the row and its cancel button");
            if (longRow && longCancel && list && list->viewport()) {
                expect(longRow->width() <= list->viewport()->width(),
                       "a long file name does not widen the row past the scroll viewport");
                const QPoint corner =
                    longCancel->mapTo(&longPanel, QPoint(longCancel->width(), longCancel->height()));
                expect(corner.x() <= longPanel.width() && corner.y() <= longPanel.height(),
                       "the cancel button stays inside the panel with a long file name");
            }
            longPanel.hide();
        }
        {
            QList<TaskCenter::Item> tasks;
            TaskCenter::Item error;
            error.key = QStringLiteral("error"); error.title = QStringLiteral("游戏错误"); error.state = TaskCenter::State::Failed;
            TaskCenter::Item choice;
            choice.key = QStringLiteral("choice"); choice.title = QStringLiteral("等待选择"); choice.state = TaskCenter::State::WaitingUser;
            choice.percent = 70;
            choice.steps = {{QStringLiteral("检查启动文件"), TaskCenter::StepState::Done, {}},
                            {QStringLiteral("登录"), TaskCenter::StepState::Running, QStringLiteral("正在校验")},
                            {QStringLiteral("等待启动"), TaskCenter::StepState::Pending, {}},
                            {QStringLiteral("完成"), TaskCenter::StepState::Pending, {}}};
            TaskCenter::Item interrupted;
            interrupted.key = QStringLiteral("interrupted"); interrupted.title = QStringLiteral("中断"); interrupted.state = TaskCenter::State::Interrupted;
            tasks = {error, choice, interrupted};
            TaskCenterPanel panel(nullptr, [&tasks] { return tasks; }, {}, {}, {}, {}, nullptr, true);
            panel.setRowActionHandler([&tasks](const QString &key, TaskCenter::RowAction action) {
                if (action != TaskCenter::RowAction::Viewed) return;
                for (auto &item : tasks)
                    if (item.key == key && (item.state == TaskCenter::State::Failed || item.state == TaskCenter::State::Interrupted))
                        item.attentionAcknowledged = true;
            });
            panel.resize(1000, 500); panel.showEmbedded(); app.processEvents();
            const auto row = [&panel](const QString &key) -> QWidget * {
                for (auto *widget : panel.findChildren<QWidget *>())
                    if (widget->property(MainWindowUI::taskCenterPanelRowKeyProperty()).toString() == key) return widget;
                return nullptr;
            };
            const auto click = [](QWidget *widget) {
                if (!widget) return;
                const QPointF p(widget->rect().center());
                QMouseEvent event(QEvent::MouseButtonPress, p, QPointF(widget->mapToGlobal(p.toPoint())),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(widget, &event);
            };
            const auto attention = [&panel](int count) {
                for (auto *label : panel.findChildren<QLabel *>())
                    if (label->text() == QStringLiteral("需要处理：%1").arg(count)) return true;
                return false;
            };
            expect(attention(3), "unselected task details show outstanding attention count");
            click(row(error.key));
            expect(row(error.key) && row(error.key)->property("taskCenterSelected").toBool() && tasks[0].attentionAcknowledged,
                   "selecting an error highlights the row and acknowledges its attention");
            auto *detail = panel.findChild<QLabel *>(MainWindowUI::taskDetailTitleName());
            expect(detail && detail->text() == error.title && detail->isVisible(), "selected task details are visible immediately");
            click(row(choice.key));
            expect(row(choice.key)->property("taskCenterSelected").toBool() && !row(error.key)->property("taskCenterSelected").toBool(),
                   "task selection is exclusive");
            expect(!tasks[1].attentionAcknowledged, "viewing a choice does not resolve it");
            expect(panel.findChild<QWidget *>(QStringLiteral("taskCenterFlowStep_3")) &&
                       panel.findChild<TaskProgressBar *>(QStringLiteral("taskCenterFlowProgress")),
                   "details retain completed, current and pending workflow stages with active stage progress");
            click(row(choice.key));
            expect(attention(2) && !row(choice.key)->property("taskCenterSelected").toBool(),
                   "clicking the selected task again clears selection and restores overview");
            click(row(choice.key));
            auto *scroll = panel.findChild<QScrollArea *>(QStringLiteral("taskCenterTaskList"));
            click(scroll->viewport());
            expect(attention(2) && !row(choice.key)->property("taskCenterSelected").toBool(),
                   "blank click clears selection and restores updated overview counts");
            tasks[1].state = TaskCenter::State::Succeeded;
            tasks[1].percent = 100;
            for (auto &step : tasks[1].steps) step.state = TaskCenter::StepState::Done;
            click(row(choice.key));
            panel.refresh();
            expect(panel.findChild<QWidget *>(QStringLiteral("taskCenterFlowStep_3")) &&
                       !panel.findChild<TaskProgressBar *>(QStringLiteral("taskCenterFlowProgress")),
                   "workflow stages remain visible after progress completes");
            auto *completedBar = panel.findChild<TaskProgressBar *>(QStringLiteral("taskCenterDetailProgress"));
            expect(completedBar && completedBar->value() == 100, "completed task retains its measured overall progress");
            click(scroll->viewport());
            panel.refresh();
            expect(attention(1), "choice attention clears only after the task leaves WaitingUser");
            click(row(interrupted.key)); panel.refresh();
            expect(row(interrupted.key)->property("taskCenterSelected").toBool(), "selection survives refresh by task key");
            tasks.removeLast(); panel.refresh();
            expect(attention(0), "removing the selected task restores the unselected overview");
        }
        {
            // 失败行落在滚动区**下面**（用户没滚到）时，不许被标成"看过了"。
            // 旧实现只看 `m_scroll` 整体可不可见，一开面板就把所有失败行整批标掉 ——
            // 底栏那颗"没看过的失败"红点当场熄灭，用户却一行都没看见（而且是落盘的）。
            // 新判据是**逐行和视口相交**：滚下去露脸了才算看过。
            QList<TaskCenter::Item> tasks;
            for (int i = 0; i < 14; ++i) {
                TaskCenter::Item done;
                done.key = QStringLiteral("done-%1").arg(i);
                done.title = QStringLiteral("Completed task %1").arg(i);
                done.state = TaskCenter::State::Succeeded;
                tasks.append(done);
            }
            TaskCenter::Item failed;
            failed.key = QStringLiteral("failed-bottom");
            failed.title = QStringLiteral("Failed below the fold");
            failed.state = TaskCenter::State::Failed;
            failed.unreadFailure = true;
            tasks.append(failed);
            bool acknowledged = false;
            TaskCenterPanel panel(nullptr, [&tasks] { return tasks; }, {}, {}, {}, {}, nullptr, true);
            panel.setRowActionHandler([&acknowledged](const QString &key, TaskCenter::RowAction action) {
                if (action == TaskCenter::RowAction::Seen && key == QStringLiteral("failed-bottom")) {
                    acknowledged = true;
                }
            });
            panel.resize(1000, 420);
            panel.showEmbedded();
            app.processEvents();
            expect(!acknowledged, "a failed row below the fold is not silently marked as viewed");
            auto *list = panel.findChild<QScrollArea *>(QStringLiteral("taskCenterTaskList"));
            expect(list && list->verticalScrollBar()->maximum() > 0,
                   "the test really pushes the failed row out of the viewport");
            if (list) {
                list->verticalScrollBar()->setValue(list->verticalScrollBar()->maximum());
                app.processEvents();
                expect(acknowledged, "scrolling a failure into view marks unread only, not attention");
            }
            panel.hide();
        }
        {
            QList<TaskCenter::Item> tasks;
            for (const auto state : {TaskCenter::State::Running, TaskCenter::State::WaitingUser,
                                     TaskCenter::State::Failed, TaskCenter::State::Succeeded}) {
                TaskCenter::Item item;
                item.key = QString::number(tasks.size());
                item.title = QStringLiteral("任务-%1").arg(tasks.size());
                item.state = state;
                tasks.append(item);
            }
            TaskCenterPanel panel(nullptr, [&] { return tasks; }, [&] {
                tasks.removeIf([](const auto &item) { return !TaskCenter::stateIsActive(item.state); });
            }, {}, {}, {}, nullptr, true);
            panel.resize(1000, 500); panel.showEmbedded();
            app.processEvents();
            auto *filter = panel.findChild<QComboBox *>(QStringLiteral("taskCenterFilter"));
            auto *search = panel.findChild<QLineEdit *>(QStringLiteral("taskCenterSearch"));
            auto *clear = panel.findChild<QPushButton *>(MainWindowUI::taskCenterPanelClearButtonName());
            auto *scroll = panel.findChild<QScrollArea *>(QStringLiteral("taskCenterTaskList"));
            expect(filter && search && clear && scroll && !scroll->isAncestorOf(clear),
                   "task filtering and clear action stay outside the scrolling history");
            const auto rows = [&]() {
                int count = 0;
                for (auto *widget : panel.findChildren<QWidget *>())
                    if (widget->property(MainWindowUI::taskCenterPanelRowKeyProperty()).isValid()) ++count;
                return count;
            };
            const auto settle = [&]() { for (int pass = 0; pass < 5; ++pass) app.processEvents(); };
            if (filter && search && clear) {
                filter->setCurrentIndex(1); settle();
                expect(rows() == 2, "active filter includes running and waiting for a decision");
                filter->setCurrentIndex(2); settle();
                expect(rows() == 2, "finished filter includes both success and failure");
                filter->setCurrentIndex(3); settle();
                expect(rows() == 2, "attention filter includes unseen failures and decisions");
                auto *errorRow = panel.findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(1));
                if (errorRow) {
                    const QPointF p(errorRow->rect().center());
                    QMouseEvent press(QEvent::MouseButtonPress, p, QPointF(errorRow->mapToGlobal(p.toPoint())),
                                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(errorRow, &press);
                }
                tasks[2].attentionAcknowledged = true;
                panel.refresh(); settle();
                auto *detail = panel.findChild<QLabel *>(MainWindowUI::taskDetailTitleName());
                expect(detail && detail->text() == tasks[2].title,
                       "acknowledging an error does not close its readable detail under attention filtering");
                search->setText(QStringLiteral("任务-1")); settle();
                expect(rows() == 1, "search combines with the state filter");
                search->clear(); filter->setCurrentIndex(0); settle();
                clear->click(); settle();
                expect(tasks.size() == 2 && rows() == 2 && !clear->isEnabled(),
                       "clear removes finished records but never cancels active work");
            }
            auto *host = ui.overlay();
            QWidget *page = host->openPage(QStringLiteral("任务工具栏测试"));
            expect(page != nullptr, "header actions can be attached to an open page");
            if (page) {
                host->setPageHeaderWidget(panel.headerActions());
                expect(panel.headerActions()->parentWidget() != &panel, "task toolbar occupies the page title row");
                host->closeOverlay();
                expect(panel.headerActions()->parentWidget() == &panel, "closing the page returns toolbar ownership safely");
            }
        }
        Anim::setEnabled(true);
        {
            QWidget canvas;
            canvas.resize(240, 160);
            canvas.show();
            app.processEvents();
            Anim::pageAppear(&canvas);
            auto *cover = canvas.findChild<QWidget *>(QStringLiteral("wtfmlPageCurtain"));
            expect(cover && cover->testAttribute(Qt::WA_OpaquePaintEvent),
                   "page curtain writes every pixel (underlay + content) so the covered page is not repainted each frame");
            QScrollBar bar(Qt::Vertical, &canvas);
            bar.resize(16, 120);
            bar.show();
            app.processEvents();
            Anim::scrollBarAppear(&bar);
            expect(bar.graphicsEffect() != nullptr, "scrollbar fade starts normally");
            // 模拟主线程错过整段动画，不应在恢复后重新补播全部帧。
            QThread::msleep(Anim::scrollBarMs() + 40);
            QElapsedTimer deadline;
            deadline.start();
            while (bar.graphicsEffect() && deadline.elapsed() < 100) {
                app.processEvents(QEventLoop::AllEvents, 5);
                QThread::msleep(1);
            }
            expect(bar.graphicsEffect() == nullptr,
                   "delayed animation catches up to elapsed time instead of replaying missed frames");
        }

        {
            QWidget canvas;
            canvas.resize(360,220);
            QFrame content(&canvas);
            content.setGeometry(canvas.rect());
            content.setStyleSheet(QStringLiteral("background: #aa1414; border: none;"));
            canvas.show();
            app.processEvents();
            bool coveredDuringChange=false;
            Anim::layoutTransition(&canvas,[&] {
                auto *cover=canvas.findChild<QWidget *>(QStringLiteral("wtfmlLayoutCurtain"),Qt::FindDirectChildrenOnly);
                coveredDuringChange=cover&&!cover->isHidden();
                content.raise();
                content.setStyleSheet(QStringLiteral("background: #2040a0; border: none;"));
                // QWidget/QSS geometry changes can finish on the next event turn.
                QTimer::singleShot(0,&content,[&] { content.setStyleSheet(QStringLiteral("background: #14a050; border: none;")); });
            },true);
            expect(coveredDuringChange,"layout transition covers the old frame before changing the live layout");
            auto *topCover=canvas.findChild<QWidget *>("wtfmlLayoutCurtain",Qt::FindDirectChildrenOnly);
            expect(canvas.findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly).last()==topCover,
                   "opening a raised page cannot cover its own transition snapshot");
            QEventLoop ready;
            QTimer::singleShot(25,&ready,&QEventLoop::quit);
            ready.exec();
            auto *cover=canvas.findChild<QWidget *>(QStringLiteral("wtfmlLayoutCurtain"),Qt::FindDirectChildrenOnly);
            auto animations=cover?cover->findChildren<QVariantAnimation *>():QList<QVariantAnimation *>();
            if(!animations.isEmpty())animations.first()->setCurrentTime(animations.first()->duration()-1);
            const auto pixel=cover?cover->grab().toImage().pixelColor(180,110):QColor();
            expect(pixel.isValid()&&qAbs(pixel.red()-20)<3&&qAbs(pixel.green()-160)<3&&qAbs(pixel.blue()-80)<3,
                   "transition destination includes next-turn polish instead of flashing to a stale snapshot");
            Anim::cancelLayoutTransition(&canvas);
        }
        const auto settle = [] {
            QEventLoop loop;
            QTimer::singleShot(400, &loop, &QEventLoop::quit);
            loop.exec();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        };
        auto *home = ui.getTabWidget()->widget(MainWindowUI::homeTabIndex());
        const auto curtainCount = [&preview] {
            int count = 0;
            for (auto *curtain : preview.findChildren<QWidget *>(QStringLiteral("wtfmlLayoutCurtain")))
                if (!curtain->isHidden()) ++count;
            return count;
        };
        MainWindowUI::HomeQuickItem quick;
        quick.key = QStringLiteral("preview"); quick.name = QStringLiteral("Preview");
        quick.detail = QStringLiteral("Minecraft 1.21.1\nLast played today");
        ui.setHomeQuickRows({quick}, {quick}, {quick});
        settle();
        auto *launchCard = home->findChild<QWidget *>(QStringLiteral("homeCurrentInstanceCard"));
        const int normalHeight = launchCard ? launchCard->height() : 0;
        // 实例切换是浮在页上的毛玻璃弹层：开着时启动区一像素都不动，也不起换页那层幕布。
        ui.openHomeInstancePicker({}, {instance}, {}, {});
        settle();
        expect(ui.homeInstancePickerOpen(), "the name block opens the instance popover");
        expect(launchCard && launchCard->height() == normalHeight && curtainCount() == 0,
               "the popover floats over the page; the launch card keeps its height");
        if (auto *anchor = home->findChild<QWidget *>(MainWindowUI::homeCurrentPickerName())) {
            expect(anchor->property("wtfmlOpen").toBool(), "the name block keeps its pressed look while the popover is open");
        }
        {
            // 点弹层外面（同一个窗里）= 收。
            QWidget *target = ui.getTabWidget();
            const QPointF local(2, 2);
            QMouseEvent press(QEvent::MouseButtonPress, local, target->mapToGlobal(local.toPoint()),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(target, &press);
            settle();
            expect(!ui.homeInstancePickerOpen(), "an outside press closes the instance popover");
        }
        // 实例带：点卡片 = 设为当前（不展开别的面板）。
        {
            auto *favSection = home->findChild<QWidget *>(QStringLiteral("homeFavoriteSection"));
            auto *card = favSection ? favSection->findChild<QPushButton *>(QStringLiteral("homeQuickCard_preview"))
                                    : nullptr;
            expect(card != nullptr, "favorite page exposes the preview quick card");
            expect(preview.findChild<QWidget *>(QStringLiteral("homeQuickPopup")) == nullptr,
                   "the strip has no per-card popup any more");
        }
        settle();
        ui.openHomeInstancePicker({}, {instance}, {}, {});
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
        settle();
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        expect(!ui.homeInstancePickerOpen() && !home->findChild<QWidget *>(QStringLiteral("wtfmlLayoutCurtain")),
               "switching tabs closes the picker without restoring stale content");
        ui.getTaskSummaryButton()->click();
        ActivityPopover::openAllIn(ui.getTaskSummaryButton()->window());
        expect(preview.findChild<QWidget *>(MainWindowUI::taskCenterPanelName()) && curtainCount() == 1,
               "task center open starts a bounded content transition");
        auto *curtain = preview.findChild<QWidget *>(QStringLiteral("wtfmlLayoutCurtain"));
        expect(curtain && curtain->testAttribute(Qt::WA_TransparentForMouseEvents) &&
                   curtain->geometry() == ui.getTabWidget()->geometry(),
               "task center transition excludes top navigation and bottom bar and does not intercept input");
        expect(curtain && curtain->parentWidget()->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly).last()==curtain,
               "task center opening snapshot is stacked above the page overlay");
        ui.getTaskSummaryButton()->click();
        settle();
        expect(!preview.findChild<QWidget *>(MainWindowUI::taskCenterPanelName()) && curtainCount() == 0,
               "task center closes cleanly during opening animation");
        ui.getTaskSummaryButton()->click();
        ActivityPopover::openAllIn(ui.getTaskSummaryButton()->window());
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
        expect(curtainCount() == 0, "switching tabs cancels task center animation immediately");
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        ui.getTaskSummaryButton()->click();
        ActivityPopover::openAllIn(ui.getTaskSummaryButton()->window());
        preview.resize(1020, 670);
        settle();
        expect(curtainCount() == 0, "window resizing leaves no stale transition snapshot");
        // 页开着时点任务中心：必须是**提示层叠在页上**，不能挤掉页。
        {
            QDialog page;
            page.setObjectName(QStringLiteral("taskCenterOverlayProbePage"));
            auto *pageLayout = new QVBoxLayout(&page);
            auto *edit = new QLineEdit(QStringLiteral("unsaved-instance-state"), &page);
            pageLayout->addWidget(edit);
            auto *host = ui.overlay();
            expect(host->showPage(&page, QStringLiteral("测试页")),
                   "probe page opens on the overlay");
            QApplication::processEvents();
            ui.getTaskSummaryButton()->click();
            ActivityPopover::openAllIn(ui.getTaskSummaryButton()->window());
            QApplication::processEvents();
            expect(host->promptOpen(),
                   "task center stacks as a prompt layer over the page");
            expect(curtainCount() == 1, "task center over instance page uses the full content opening transition");
            auto *workspace = host->findChild<QWidget *>(QStringLiteral("taskCenterPromptWorkspace"));
            auto *toolbar = workspace ? workspace->findChild<QWidget *>(QStringLiteral("taskCenterToolbar")) : nullptr;
            expect(toolbar && toolbar->isVisible() && workspace->findChild<QComboBox *>(QStringLiteral("taskCenterFilter")),
                   "over-page task center retains filtering and header actions");
            expect(!host->graphicsEffect() && !host->promptCardWidget()->graphicsEffect(),
                   "full-size task center avoids a second prompt opacity effect");
            if (auto *close = host->findChild<QPushButton *>(QStringLiteral("taskCenterPromptClose"))) {
                close->click();
                QElapsedTimer closeWait2;
                closeWait2.start();
                while (!host->pageOpen() && closeWait2.elapsed() < 1000) QApplication::processEvents();
                expect(host->pageOpen() && page.isVisible(),
                       "the close button returns to the same page");
            } else {
                expect(false, "task center prompt has a close button");
            }
            ui.getTaskSummaryButton()->click();
            ActivityPopover::openAllIn(ui.getTaskSummaryButton()->window());
            QApplication::processEvents();
            expect(host->promptOpen(), "task center reopens as a prompt layer");
            ui.getTaskSummaryButton()->click();
            {
                QElapsedTimer closeWait;
                closeWait.start();
                while (!host->pageOpen() && closeWait.elapsed() < 1000) QApplication::processEvents();
            }
            QApplication::processEvents();
            expect(host->pageOpen(), "prompt dismissed returns to page mode");
            expect(page.isVisible(), "prompt dismissed makes the page visible again");
            expect(edit->text() == QStringLiteral("unsaved-instance-state"), "task center closing preserves the original page edits");
            settle();
            expect(!host->findChild<QWidget *>(QStringLiteral("taskCenterPromptWorkspace")),
                   "closed task workspace and toolbar are released together");
            ui.getTaskSummaryButton()->click();
            ActivityPopover::openAllIn(ui.getTaskSummaryButton()->window());
            ui.getTabWidget()->setCurrentIndex(MainWindowUI::settingsTabIndex());
            QApplication::processEvents();
            expect(!host->promptOpen() && curtainCount() == 0,
                   "switching tabs clears an over-page task center without a stale transition");
            ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
            QApplication::processEvents();
            expect(host->pageOpen() && edit->text() == QStringLiteral("unsaved-instance-state"),
                   "returning to the owner tab restores the same underlying page");
            page.reject();
            QApplication::processEvents();
        }

        Anim::setEnabled(false);
        {
            QWidget host;
            host.resize(1100, 680);
            host.show();
            QList<TaskCenter::Item> tasks;
            for (int i = 0; i < 120; ++i) {
                TaskCenter::Item item;
                item.key = QString::number(i);
                item.title = QStringLiteral("Task %1").arg(i);
                item.state = i < 5 ? TaskCenter::State::Running : TaskCenter::State::Succeeded;
                item.percent = i < 5 ? 10 : 100;
                item.capabilities.cancel = i < 5;
                tasks.append(item);
            }
            int originalActions = 0, replacementActions = 0;
            TaskCenterPanel panel(&host, [&] { return tasks; }, {}, {}, {}, {}, &host, true);
            panel.setGeometry(host.rect());
            panel.setRowActivatedHandler([](const QString &) {});
            panel.setRowActionHandler([&](const QString &, TaskCenter::RowAction action) {
                if (action == TaskCenter::RowAction::Cancel) ++originalActions;
            });
            panel.showEmbedded();
            app.processEvents();
            QPointer<QWidget> stable = panel.findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(80));
            QPointer<QWidget> changed = panel.findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(0));
            QScrollBar *scrollBar = nullptr;
            for (auto *area : panel.findChildren<QScrollArea *>())
                if (stable && area->widget() == stable->parentWidget()) scrollBar = area->verticalScrollBar();
            expect(stable && changed && scrollBar && scrollBar->maximum() > 500,
                   "task refresh fixture has real scrollable rows");
            if (scrollBar) scrollBar->setValue(450);
            for (int frame = 0; frame < 20; ++frame) {
                tasks[0].percent = 20 + frame;
                panel.refresh();
                app.processEvents();
            }
            expect(stable && stable == panel.findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(80)),
                   "progress ticks preserve unchanged completed rows");
            auto *bar = panel.findChild<QProgressBar *>(MainWindowUI::taskCenterPanelRowBarName(0));
            expect(changed.isNull() && bar && bar->value() == 39,
                   "changed task updates progress without stale widgets");
            expect(scrollBar && scrollBar->value() == 450, "task progress preserves scroll position");
            if (stable) {
                const QPoint local = stable->rect().center();
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(local),
                                  QPointF(stable->mapToGlobal(local)), Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                app.sendEvent(stable, &press);
                tasks[80].bytesText = QStringLiteral("detail-only-fresh-bytes");
                panel.refresh();
                bool fresh = false;
                for (auto *label : panel.findChildren<QLabel *>())
                    fresh |= label->text() == tasks[80].bytesText;
                expect(stable && fresh, "reused row still shows latest selected task details");
            }
            QPointer<QWidget> beforeHandler = panel.findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(0));
            panel.setRowActionHandler([&](const QString &, TaskCenter::RowAction action) {
                if (action == TaskCenter::RowAction::Cancel) ++replacementActions;
            });
            panel.refresh();
            if (auto *cancel = panel.findChild<QPushButton *>(MainWindowUI::taskCenterPanelRowCancelName(0))) cancel->click();
            expect(beforeHandler.isNull() && originalActions == 0 && replacementActions == 1,
                   "replacing callbacks invalidates cached buttons");
            QPointer<QWidget> beforePalette = panel.findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(0));
            if (beforePalette) {
                QPalette next = beforePalette->parentWidget()->palette();
                next.setColor(QPalette::PlaceholderText, QColor(80, 60, 120));
                beforePalette->parentWidget()->setPalette(next);
                panel.refresh();
                expect(beforePalette.isNull(), "theme palette changes invalidate row presentation");
            }
            stable = panel.findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(80));
            expect(!stable.isNull(), "task still exists before removal");
            tasks.removeAt(80);
            panel.refresh();
            expect(stable.isNull(), "removed task releases cached row");
        }
        {
            QTemporaryDir files;
            QFile fixture(files.filePath(QStringLiteral("visible-row.jar")));
            expect(fixture.open(QIODevice::WriteOnly), "file row painting fixture opens");
            fixture.write("fixture");
            fixture.close();
            InstanceFilesPage page;
            page.setBatchMode(true);
            page.setMode(InstanceFilesPage::Mode::FilesOnly);
            page.resize(700, 350);
            page.setDirectory(files.path());
            page.refresh();
            page.show();
            app.processEvents();
            QListWidget *list = page.list();
            expect(list && list->count() == 1, "file row painting fixture displayed");
            if (list && list->count() == 1) {
                QWidget *row = list->itemWidget(list->item(0));
                // 7a：行本身透明（不 autoFill、只在选中时铺选中那一层），底由视口铺 —— 半透明的
                // Base 叠两层，行就比列表空白处白一截。量法：整张列表垫在页面底上抓，行上那点
                // 对比列表空白处那点。
                expect(row && !row->autoFillBackground(), "embedded file row does not stack a second base layer");
                if (row) {
                    // 行左边距的竖直正中：选中底是圆角块，角上那几个像素是抗锯齿过渡，不能拿来比。
                    const QPoint rowSpot = row->mapTo(list, QPoint(5, row->height() / 2));
                    const QPoint emptySpot(rowSpot.x(), row->mapTo(list, QPoint(0, row->height())).y() + 20);
                    const auto shot = [list]() { return captureWidgetOn(list, Theme::tokens().page); };
                    const auto near = [](const QColor &a, const QColor &b) {
                        return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) +
                                   qAbs(a.blue() - b.blue()) <= 4;
                    };
                    list->item(0)->setSelected(true);
                    app.processEvents();
                    {
                        const QImage image = shot();
                        const QColor got = image.pixelColor(rowSpot);
                        const QColor want = overOpaque(Theme::selectionFill(list->palette().color(QPalette::Base)),
                                                       image.pixelColor(emptySpot));
                        expect(near(got, want), "embedded row shows exactly one selected layer over the list base");
                    }
                    const auto buttons = row->findChildren<QToolButton *>();
                    bool matchingBackground = !buttons.isEmpty();
                    for (QToolButton *button : buttons) {
                        matchingBackground &= button->palette().color(QPalette::Window) ==
                            Theme::selectionFill(list->palette().color(QPalette::Base));
                        matchingBackground &= button->palette().color(QPalette::Button) ==
                            Theme::selectionFill(list->palette().color(QPalette::Base));
                    }
                    expect(matchingBackground, "row buttons inherit selected background instead of white patches");
                    list->clearSelection();
                    app.processEvents();
                    {
                        const QImage image = shot();
                        expect(near(image.pixelColor(rowSpot), image.pixelColor(emptySpot)),
                               "embedded row restores background after selection clears");
                    }
                    list->item(0)->setSelected(true);
                    const QPoint point = list->visualItemRect(list->item(0)).center();
                    const QPoint global = list->viewport()->mapToGlobal(point);
                    QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), QPointF(global),
                                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(list->viewport(), &press);
                    expect(list->selectedItems().size() == 1,
                           "selected row stays selected until release so dragging remains possible");
                    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), QPointF(global),
                                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(list->viewport(), &release);
                    expect(list->selectedItems().isEmpty() && page.selectionBar()->isHidden(),
                           "clicking the only selected row clears selection and closes action panel");
                }
            }
        }
        printf("UI refresh check: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
    
 return 0;
}
