#include "Support.hpp"
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
        for (const QString &name : {QStringLiteral("homeQuickCurrentTab"), QStringLiteral("homeQuickRecentTab"),
                                    QStringLiteral("homeQuickFavoriteTab")}) {
            auto *tab = preview.findChild<QPushButton *>(name);
            expect(tab && tab->minimumWidth() >= 64, "home quick tabs have wider click targets");
        }
        expect(preview.findChild<QPushButton *>(MainWindowUI::homeInstanceSelectButtonName())->minimumWidth() >= 80,
               "home list button is wider than quick tabs");
        MainWindowUI::HomePickerInstance instance;
        instance.id = QStringLiteral("custom-instance");
        instance.title = QStringLiteral("Adventure");
        instance.folderPath = QStringLiteral("D:/Games/Minecraft");
        instance.loaderKey = QStringLiteral("fabric");
        instance.favorite = true;
        instance.recent = true;
        instance.searchKeywords = QStringLiteral("1.21.1 0.16.9 我的整合包");
        ui.openHomeInstancePicker({}, {instance}, {}, {});
        auto *search = preview.findChild<QLineEdit *>(QStringLiteral("homePickerSearch"));
        auto *list = preview.findChild<QListWidget *>(QStringLiteral("homePickerInstanceList"));
        if (search && list) {
            for (const QString &query : {QStringLiteral("1.21 fabric 收藏"), QStringLiteral("0.16.9"),
                                        QStringLiteral("games recent"), QStringLiteral("我的整合包")}) {
                search->setText(query);
                expect(list->count() == 1, "expanded search keywords match");
            }
            search->setText(QStringLiteral("1.21 forge"));
            expect(list->count() == 0, "all search terms must match");
        } else expect(false, "home search controls exist");
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
        expect(!ui.homeInstancePickerOpen(), "switching away resets hidden home picker");
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        expect(!ui.homeInstancePickerOpen(), "returning home does not restore stale picker");
        ui.openHomeInstancePicker({}, {instance}, {}, {});
        auto *escape = preview.findChild<QShortcut *>(QStringLiteral("homePickerCloseShortcut"));
        expect(escape && escape->key() == QKeySequence(Qt::Key_Escape)
                   && escape->context() == Qt::WidgetWithChildrenShortcut,
               "home picker escape is scoped to home controls");
        if (escape) {
            QMetaObject::invokeMethod(escape, "activated", Qt::DirectConnection);
        }
        expect(!ui.homeInstancePickerOpen(), "escape closes home picker");
        ui.closeHomeInstancePicker();
        auto *button = ui.getToolboxButton();
        // 已改为内容自适应，不能再要求旧的固定 112 px。
        expect(button->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed &&
                   button->minimumWidth() != button->maximumWidth() &&
                   button->width() == button->sizeHint().width(),
               "toolbox trigger follows its content width without horizontal stretching");
        button->click();
        auto *panel = preview.findChild<QFrame *>(MainWindowUI::toolboxPanelName());
        expect(panel && panel->isVisible(), "toolbox opens on first click");
        if (panel) {
            const QPoint global = button->mapToGlobal(button->rect().center());
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(panel->mapFromGlobal(global)), QPointF(global),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(panel, &press);
            expect(!panel->isVisible(), "popup forwards same-anchor press to close");
        }
        button->click();
        panel = preview.findChild<QFrame *>(MainWindowUI::toolboxPanelName());
        expect(panel && panel->isVisible(), "toolbox reopens before deferred deletion");
        button->click();
        expect(!panel || !panel->isVisible(), "toolbox closes on next button click");
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
        expect(status && status->alignment().testFlag(Qt::AlignRight), "account identity is right aligned");
        auto *viewer = account.findChild<SkinViewer *>(AccountSkinDialog::viewerName());
        expect(viewer && viewer->width() == 208 && viewer->height() == 312,
               "account character preview has bounded dimensions");
        auto *login = account.findChild<QPushButton *>(AccountSkinDialog::loginButtonName());
        auto *body = account.findChild<QWidget *>(QStringLiteral("accountBody"));
        expect(body && body->height() < 520 && login &&
               login->mapTo(&account, QPoint()).y() < 120,
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
            // 工具栏在宿主中，不再属于 account 的子树。
            if (!header) header = preview.findChild<QWidget *>(QStringLiteral("accountHeaderBar"));
            expect(header && header->parentWidget() != &account && header->isVisible(),
                   "account identity and refresh occupy the existing page header");
            expect(body && body->y() <= 16, "embedded account content starts without a redundant identity row");
            if (app.arguments().contains(QStringLiteral("--ui-refresh-preview")))
                preview.grab().save(QStringLiteral("build/account-embedded-preview.png"));
            accountHost->closeOverlay();
            expect(header && header->parentWidget() == &account,
                   "closing account page returns header ownership safely");
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
        ui.openHomeInstancePicker({}, {instance}, {}, {});
        expect(ui.homeInstancePickerOpen() && curtainCount() == 1, "home picker changes state immediately and starts transition");
        ui.closeHomeInstancePicker();
        expect(!ui.homeInstancePickerOpen() && curtainCount() == 1, "rapid reversal replaces the previous transition");
        settle();
        expect(curtainCount() == 0, "home picker transition removes its snapshot on completion");
        ui.openHomeInstancePicker({}, {instance}, {}, {});
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
        settle();
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        expect(!ui.homeInstancePickerOpen() && !home->findChild<QWidget *>(QStringLiteral("wtfmlLayoutCurtain")),
               "switching tabs cancels picker animation without restoring stale content");
        ui.getTaskSummaryButton()->click();
        expect(preview.findChild<QWidget *>(MainWindowUI::taskCenterPanelName()) && curtainCount() == 1,
               "task center open starts a bounded content transition");
        auto *curtain = preview.findChild<QWidget *>(QStringLiteral("wtfmlLayoutCurtain"));
        expect(curtain && curtain->testAttribute(Qt::WA_TransparentForMouseEvents) &&
                   curtain->geometry() == ui.getTabWidget()->geometry(),
               "task center transition excludes top navigation and bottom bar and does not intercept input");
        ui.getTaskSummaryButton()->click();
        settle();
        expect(!preview.findChild<QWidget *>(MainWindowUI::taskCenterPanelName()) && curtainCount() == 0,
               "task center closes cleanly during opening animation");
        ui.getTaskSummaryButton()->click();
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::instancesTabIndex());
        expect(curtainCount() == 0, "switching tabs cancels task center animation immediately");
        ui.getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        ui.getTaskSummaryButton()->click();
        preview.resize(1020, 670);
        settle();
        expect(curtainCount() == 0, "window resizing leaves no stale transition snapshot");
        Anim::setEnabled(false);
        printf("UI refresh check: %d failure(s)\n", failures);
        return failures == 0 ? 0 : 1;
    
 return 0;
}

