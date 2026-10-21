#include "Support.hpp"
int UiTaskCenterChecks(QMainWindow &window, MainWindowUI &ui, QTabWidget * &tabs) {

    {
        // 这一节量的是任务中心这颗入口按钮：中间那格写哪句话由**任务账本**
        // 决定（`Tasks::collapsedSummary()`，纯规则在 verify 的 AL/AM 两节量；"同步等待
        // 压在上面"那条在 mw_check 量）。界面这一头量的是这颗按钮**本身**：它是任务中心
        // 的入口，点一下往上弹那块面板。
        QPushButton *cell = ui.getTaskSummaryButton();
        check(cell != nullptr && cell->isVisible() && cell->isEnabled(),
              "I2 中间那格是一颗**亮着的**、能点的按钮（不再整格藏起来）");

        // 「正在启动 <显示名> ...」这句话仍然只有**一个**出处：startGame() 之前那次
        // enterSyncWait() 和进程起来之后的 updateBottomBar() 都用它，两处各写一遍
        // 就会出现"文字在启动的那一瞬间跳一下"（ui_shot 那张 02-launching.png 也引它）。
        check(MainWindowUI::launchingStatusText(QStringLiteral("26.2")) ==
                  QStringLiteral("正在启动 26.2 ..."),
              "启动中那句文案：前缀 + 空格 + 显示名 + 空格 + 三个点",
              MainWindowUI::launchingStatusText(QStringLiteral("26.2")));
        check(MainWindowUI::launchingStatusText(QStringLiteral("26.2")) !=
                  MainWindowUI::launchingStatusText(QStringLiteral("1.20.1")),
              "显示名不一样，那句话就不一样（不是写死的常量）");

        if (cell != nullptr) {
            // 喂一份假内容：一件在跑 + 一件已完成（真数据是 MainWindow 的事，那几条在
            // mw_check 量 —— ui/ 不许 include src/，见 ui/TaskCenterPanel.hpp 文件头）。
            int clearCalls = 0;
            ui.setTaskCenterContent(
                []() {
                    QList<TaskCenter::Item> items;
                    TaskCenter::Item running;
                    running.key = QStringLiteral("7");
                    running.title = QStringLiteral("正在下载 26.2");
                    running.detail = QStringLiteral("已下载 3 / 9 个文件");
                    running.percent = -1;  // 测不出百分比 ⇒ 不确定态
                    running.state = TaskCenter::State::Running;
                    TaskCenter::Step step;
                    step.name = QStringLiteral("校验文件");
                    step.state = TaskCenter::StepState::Done;
                    running.steps << step;
                    items << running;
                    TaskCenter::Item done;
                    done.key = QStringLiteral("6");
                    done.title = QStringLiteral("安装 Fabric");
                    done.state = TaskCenter::State::Succeeded;
                    done.finishedAt = QDateTime::currentDateTime();
                    items << done;
                    return items;
                },
                [&clearCalls]() { ++clearCalls; });
            check(TaskCenter::terminalMark(TaskCenter::State::Succeeded) == QStringLiteral("✓")
                      && TaskCenter::terminalMark(TaskCenter::State::Failed) == QStringLiteral("✗")
                      && TaskCenter::terminalMark(TaskCenter::State::Cancelled) == QStringLiteral("—")
                      && TaskCenter::terminalMark(TaskCenter::State::Interrupted) == QStringLiteral("!"),
                  "I2 四种终态各有自己的记号（取消/中断不冒充成功）");
            // 收尾时还停在「正在跑」的那一步：面板行 / 详情页 / 复制文本三处都走
            // `stepForTask()`。取消不能被画成 ●（任务已经结束、里面还有东西在跑）。
            {
                TaskCenter::Step runningStep;
                runningStep.name = QStringLiteral("复制文件");
                runningStep.state = TaskCenter::StepState::Running;
                const TaskCenter::Step cancelledStep =
                    TaskCenter::stepForTask(TaskCenter::State::Cancelled, runningStep);
                const TaskCenter::Step interruptedStep =
                    TaskCenter::stepForTask(TaskCenter::State::Interrupted, runningStep);
                const TaskCenter::Step succeededStep =
                    TaskCenter::stepForTask(TaskCenter::State::Succeeded, runningStep);
                const TaskCenter::Step activeStep =
                    TaskCenter::stepForTask(TaskCenter::State::Running, runningStep);
                check(cancelledStep.state == TaskCenter::StepState::Pending &&
                          cancelledStep.note == QStringLiteral("已取消") &&
                          interruptedStep.state == TaskCenter::StepState::Failed &&
                          interruptedStep.note == QStringLiteral("已中断") &&
                          succeededStep.state == TaskCenter::StepState::Done &&
                          activeStep.state == TaskCenter::StepState::Running,
                      "I2 终态任务里停在 Running 的那一步跟着收尾改写；活动中的任务照旧",
                      QStringLiteral("取消 %1/%2 · 中断 %3/%4 · 成功 %5 · 运行中 %6")
                          .arg(int(cancelledStep.state)).arg(cancelledStep.note)
                          .arg(int(interruptedStep.state)).arg(interruptedStep.note)
                          .arg(int(succeededStep.state)).arg(int(activeStep.state)));
            }
            check(MainWindowUI::taskSummaryText(QStringLiteral("任务"),
                                                TaskCenter::State::Cancelled)
                          .endsWith(MainWindowUI::taskSummaryCancelledSuffix())
                      && MainWindowUI::taskSummaryText(QStringLiteral("任务"),
                                                       TaskCenter::State::Interrupted)
                             .endsWith(MainWindowUI::taskSummaryInterruptedSuffix()),
                  "I2 底栏能分别写出已取消和已中断");
            cell->click();
            QApplication::processEvents();
            QWidget *panel = window.findChild<QWidget *>(MainWindowUI::taskCenterPanelName());
            check(panel != nullptr && panel->isVisible(),
                  "I2 点一下中间那格，打开了完整任务中心工作区");
            if (panel != nullptr) {
                check(!panel->isWindow() && ui.overlay() != nullptr
                          && ui.overlay()->pageOpen(),
                      "I2 任务中心是主窗口内容区中的完整页面，不是独立 Popup");
                QTabBar *topTabs = ui.getTabWidget() != nullptr ? ui.getTabWidget()->tabBar()
                                                                 : nullptr;
                check(topTabs != nullptr
                          && panel->mapToGlobal(QPoint(0, 0)).y()
                                 > topTabs->mapToGlobal(topTabs->rect().bottomLeft()).y(),
                      "I2 任务中心从顶端四个主选项卡下方开始，不覆盖导航按钮");
                QTabWidget *tabs = ui.getTabWidget();
                const int ownerTab = tabs != nullptr ? tabs->currentIndex() : -1;
                const int otherTab = tabs != nullptr && tabs->count() > 1
                                         ? (ownerTab + 1) % tabs->count()
                                         : -1;
                if (tabs != nullptr && otherTab >= 0) {
                    tabs->setCurrentIndex(otherTab);
                    check(!panel->isVisible(),
                          "I2 切到其它主页面时任务中心停放，不盖住新页面");
                    tabs->setCurrentIndex(ownerTab);
                    check(panel->isVisible() && ui.overlay()->graphicsEffect() == nullptr,
                          "I2 切回时任务中心立即恢复，不用灰色覆盖层淡入");
                    QWidget *curtain =
                        tabs->findChild<QWidget *>(QStringLiteral("wtfmlPageCurtain"));
                    check(curtain == nullptr || !curtain->isVisible(),
                          "I2 任务中心停放期间切主页面不显示灰色换页幕布");
                }
                check(panel->findChild<QWidget *>(QStringLiteral("taskCenterRunningPane")) !=
                              nullptr
                          && panel->findChild<QWidget *>(QStringLiteral("taskCenterTaskList")) !=
                                 nullptr
                          && panel->findChild<QWidget *>(QStringLiteral("taskCenterDetailPane")) !=
                                 nullptr,
                      "I2 任务中心具备运行中实例 / 任务列表 / 上下文详情三列结构");
                check(panel->findChild<QPushButton *>(
                              QStringLiteral("taskCenterRunningCollapse")) != nullptr,
                      "I2 运行中实例列提供收起按钮");
                // 行按**行号**找得回来（名字由 taskCenterPanelRowName() 给），两栏接着数：
                // 第 0 行是「当前工作」那件、第 1 行是「已完成」那件。
                QLabel *title0 =
                    panel->findChild<QLabel *>(MainWindowUI::taskCenterPanelRowTitleName(0));
                QLabel *title1 =
                    panel->findChild<QLabel *>(MainWindowUI::taskCenterPanelRowTitleName(1));
                check(title0 != nullptr && title0->text() == QStringLiteral("正在下载 26.2") &&
                          title1 != nullptr && title1->text() == QStringLiteral("安装 Fabric"),
                      "I2 两栏各画了一行（在跑的那件在前、已完成的那件在后）",
                      (title0 != nullptr ? title0->text() : QStringLiteral("<没找到>")) +
                          QStringLiteral(" / ") +
                          (title1 != nullptr ? title1->text() : QStringLiteral("<没找到>")));
                // 认一行靠那一行的 key（**不是行号**）：属性名走唯一出处。
                QWidget *rowWidget0 =
                    panel->findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(0));
                check(rowWidget0 != nullptr &&
                          rowWidget0->property(MainWindowUI::taskCenterPanelRowKeyProperty())
                                  .toString() == QStringLiteral("7"),
                      "I2 每一行挂着自己是谁（taskCenterPanelRowKeyProperty() = 任务号）",
                      rowWidget0 != nullptr
                          ? rowWidget0->property(MainWindowUI::taskCenterPanelRowKeyProperty())
                                .toString()
                          : QStringLiteral("<没找到>"));
                // 百分比测不出时**不摆那根条**（不编一个假百分比出来）。
                // 这一行里那点信息由下面那句小字说（"已下载 3 / 9 个文件"）。
                // NOTE: 不确定态（range 0..0）是**底栏那一整格**的写法（"有东西在跑但说不清
                // 到哪了"），不是面板里每一行的 —— 一行一行都摆一根空转的条，面板
                // 一去几条就是几根在那儿空转。
                auto *bar0 =
                    panel->findChild<QProgressBar *>(MainWindowUI::taskCenterPanelRowBarName(0));
                auto *detail0 =
                    panel->findChild<QLabel *>(MainWindowUI::taskCenterPanelRowDetailName(0));
                check(bar0 == nullptr, "I2 测不出百分比那一行**不摆**那根条（不编一个假百分比出来）",
                      bar0 != nullptr
                          ? QStringLiteral("还是摆了 %1..%2").arg(bar0->minimum()).arg(bar0->maximum())
                          : QStringLiteral("<没找到>"));
                check(detail0 != nullptr &&
                          detail0->text() == QStringLiteral("已下载 3 / 9 个文件"),
                      "I2 那一行的进度改由小字那句说（「已下载 3 / 9 个文件」）",
                      detail0 != nullptr ? detail0->text() : QStringLiteral("<没找到>"));
                // 「清除已完成」那颗按钮：有处理器才出现，点了要回一声。
                // 它挂在**页标题行**（面板把 `headerActions()` 交给
                // `OverlayHost::setPageHeaderWidget()`），打开这一页时已不在面板子树里。
                auto *clear = window.findChild<QPushButton *>(
                    MainWindowUI::taskCenterPanelClearButtonName());
                check(clear != nullptr,
                      "I2 有一件已完成 ⇒ 面板标题行里有那颗「清除已完成」");
                if (clear != nullptr) {
                    clear->click();
                    QApplication::processEvents();
                    check(clearCalls == 1, "I2 点「清除已完成」把话带回了调用方（一次）",
                          QStringLiteral("回来 %1 次").arg(clearCalls));
                }
                // 行尾那颗「取消」（行内快捷动作）：
                // 只有**能力位说可以**、而且外面挂了处理器时才长。上面那件在跑的那一行
                // 没设 `capabilities.cancel` ⇒ 那一行行尾什么都不长；挂上处理器也不长。
                auto *cancel0 = panel->findChild<QPushButton *>(
                    MainWindowUI::taskCenterPanelRowCancelName(0));
                check(cancel0 == nullptr,
                      "I2 能力位说不能取消的行，行尾**不长**那颗「取消」",
                      cancel0 != nullptr ? cancel0->text() : QStringLiteral("<没找到>"));
                if (tabs != nullptr && otherTab >= 0 && ui.titleBar() != nullptr) {
                    auto *otherNav = ui.titleBar()->navButton(otherTab);
                    auto *ownerNav = ui.titleBar()->navButton(ownerTab);
                    if (otherNav != nullptr && ownerNav != nullptr) {
                        otherNav->click();
                        QApplication::processEvents();
                        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                        check(tabs->currentIndex() == otherTab && ui.overlay() != nullptr
                                  && !ui.overlay()->isOpen()
                                  && window.findChild<QWidget *>(
                                         MainWindowUI::taskCenterPanelName()) == nullptr,
                              "I2 点顶上别的选项卡 = 任务中心被收起（不再占着覆盖层那一位）");
                        ownerNav->click();
                        QApplication::processEvents();
                        check(ui.overlay() != nullptr && !ui.overlay()->isOpen()
                                  && window.findChild<QWidget *>(
                                         MainWindowUI::taskCenterPanelName()) == nullptr,
                              "I2 再点回原来那一页，任务中心不会自己又摊开");
                    }
                }
                ui.overlay()->dismiss();
                QApplication::processEvents();
                QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
            // 收尾：那个假 provider 捕的是这一节里的局部量，用完就卸掉（留着的话，后面
            // 哪一节再点开面板就是拿着悬空引用在跑）。
            ui.setTaskCenterContent(nullptr, nullptr);
            check(window.findChild<QWidget *>(MainWindowUI::taskCenterPanelName()) == nullptr,
                  "I2 关闭之后那块面板被删掉了（不留着一块看不见的面板）");
        }
    }
 return 0;
}


