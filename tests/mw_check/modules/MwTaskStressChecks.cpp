#include "Support.hpp"
int MwTaskStressChecks(MainWindow &window) {
    // ------------------------------------------------------------------
    // 用户点名的压力形状：一次性排一堆下载、几个实例同时在启动、一批文件在改。
    // 这一节直接把账本塞到 30 项（每项 5 步），高速推 40 帧进度，再收掉一半，
    // 然后打开任务中心，量「面板画出来的行」和「账本投影」是不是逐行一致 ——
    // 包括顺序、活动/已完成两栏的切分，以及终态行上不许再长出「取消」。
    {
        auto *abStress = window.findChild<ActivityCenterController *>();
        check(abStress != nullptr, "AB6 找得到活动中心");
        if (abStress != nullptr) {
            // 前面几节留下的已完成先清掉；还活着的项记个数，最后一起收。
            abStress->clearFinished();
            QApplication::processEvents();
            const int baselineActive = abStress->tasks()->active().size();

            QList<Tasks::TaskId> stressIds;
            stressIds.reserve(30);
            for (int i = 0; i < 30; ++i) {
                const Tasks::TaskKind kind = i % 3 == 0 ? Tasks::TaskKind::Download
                                             : i % 3 == 1 ? Tasks::TaskKind::Launch
                                                          : Tasks::TaskKind::Install;
                QList<Tasks::TaskStep> steps;
                for (int s = 0; s < 5; ++s) {
                    steps.append({QStringLiteral("步骤 %1").arg(s + 1),
                                  Tasks::TaskStepState::Pending, {}});
                }
                stressIds.append(abStress->begin(kind, QStringLiteral("压力任务 %1").arg(i),
                                                 QString(), steps));
            }
            check(abStress->tasks()->active().size() == baselineActive + 30,
                  "AB6 30 项全在「正在进行」里（大量下载 / 多实例启动 / 批量改文件）",
                  QString::number(abStress->tasks()->active().size()));

            for (int frame = 0; frame < 40; ++frame) {
                for (const Tasks::TaskId id : stressIds) {
                    (void)abStress->tasks()->setProgress(id, qint64(frame) * 10, 400);
                    (void)abStress->tasks()->setBytes(id, qint64(frame) * 2048, 81920);
                    (void)abStress->tasks()->setStepState(id, qMin(4, frame / 8),
                                                          Tasks::TaskStepState::Running);
                }
            }
            // 收掉 15 项（成功 / 失败 / 取消交替），另外 15 项继续跑 —— 模拟"下完一批、
            // 下一批还在下"、"一个实例已经起来、另一个还在启动"。
            for (int i = 1; i < 30; i += 2) {
                const Tasks::TaskId id = stressIds.at(i);
                if (i % 6 == 1) {
                    abStress->finish(id, true, QString(), false);
                } else if (i % 6 == 3) {
                    abStress->finish(id, false, QStringLiteral("模拟失败"), false);
                } else {
                    abStress->tasks()->cancel(id);
                }
            }
            check(abStress->tasks()->active().size() == baselineActive + 15,
                  "AB6 收掉一半之后还剩 15 项在跑",
                  QString::number(abStress->tasks()->active().size()));

            // 迟到帧不许把终端项拽回来（压力下最容易被漏掉的那一类）。
            const Tasks::TaskId terminal = stressIds.at(1);
            const Tasks::TaskSnapshot terminalBefore = abStress->tasks()->snapshot(terminal);
            const bool lateAccepted =
                abStress->tasks()->setProgress(terminal, 400, 400)
                || abStress->tasks()->succeed(terminal);
            check(!lateAccepted
                      && abStress->tasks()->snapshot(terminal).state == terminalBefore.state,
                  "AB6 已收尾的那一项：迟到帧 / 二次收尾都不生效");

            // 打开面板：画出来的行数 / 顺序必须和账本投影逐行一致。
            QPushButton *ab6Cell =
                window.findChild<QPushButton *>(MainWindowUI::taskSummaryButtonName());
            window.raise();
            window.activateWindow();
            QApplication::processEvents();
            if (ab6Cell != nullptr) {
                ab6Cell->click();
            }
            QApplication::processEvents();
            QApplication::processEvents();
            QWidget *ab6Panel =
                window.findChild<QWidget *>(MainWindowUI::taskCenterPanelName());
            check(ab6Panel != nullptr && ab6Panel->isVisible(), "AB6 点开任务中心面板");
            if (ab6Panel != nullptr) {
                int rows = 0;
                while (ab6Panel->findChild<QWidget *>(
                           MainWindowUI::taskCenterPanelRowName(rows)) != nullptr) {
                    ++rows;
                }
                const QList<TaskCenter::Item> expected = abStress->items(
                    [](const Tasks::TaskSnapshot &) { return false; }, false, {}, nullptr);
                check(rows == expected.size(),
                      "AB6 面板画出来的行数 = 账本投影的行数（30 项一项不少）",
                      QStringLiteral("面板 %1 / 账本 %2").arg(rows).arg(expected.size()));

                bool orderOk = rows == expected.size() && rows > 0;
                int activeSeen = 0;
                int terminalSeen = 0;
                bool terminalSection = false;
                for (int i = 0; orderOk && i < rows; ++i) {
                    QWidget *row = ab6Panel->findChild<QWidget *>(
                        MainWindowUI::taskCenterPanelRowName(i));
                    const QString rowKey =
                        row->property(MainWindowUI::taskCenterPanelRowKeyProperty()).toString();
                    orderOk = rowKey == expected.at(i).key;
                    const bool active = TaskCenter::stateIsActive(expected.at(i).state);
                    if (active) {
                        ++activeSeen;
                        // 「已完成」那一栏开始之后不许再出现活动项。
                        orderOk = orderOk && !terminalSection;
                    } else {
                        ++terminalSeen;
                        terminalSection = true;
                    }
                }
                check(orderOk, "AB6 面板每一行的钥匙和账本投影同一顺序（活动在前、已完成在后）");
                check(activeSeen == baselineActive + 15 && terminalSeen == 15,
                      "AB6 两栏切分正确：活动 15 项、已完成 15 项",
                      QStringLiteral("活动 %1 / 已完成 %2").arg(activeSeen).arg(terminalSeen));

                // 最新收尾的那一件排「已完成」那一栏的最前面（号 29 是最后收的）。
                const QString newestKey = QString::number(stressIds.at(29));
                bool newestFirst = false;
                for (int i = 0; i < expected.size(); ++i) {
                    if (expected.at(i).key == newestKey) {
                        // 「已完成」那一栏的第一行就是它（前面只剩活动项）。
                        newestFirst = i == activeSeen;
                        break;
                    }
                }
                check(newestFirst, "AB6 「已完成」一栏最近收尾的那一件排在最前面");

                // 终态行上不许再长「取消」（能力位 + 活着，两样缺一不可）。
                int invalidCancel = 0;
                for (int i = 0; i < expected.size(); ++i) {
                    auto *cancel = ab6Panel->findChild<QPushButton *>(
                        MainWindowUI::taskCenterPanelRowCancelName(i));
                    if (!TaskCenter::stateIsActive(expected.at(i).state) && cancel != nullptr) {
                        ++invalidCancel;
                    }
                }
                check(invalidCancel == 0,
                      "AB6 15 个已完成行上一颗「取消」都没有",
                      QString::number(invalidCancel));

                if (ab6Panel->isVisible()) {
                    ab6Panel->close();
                }
                QApplication::processEvents();
                QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }

            // 收场：把还在跑的那 15 项也收掉、清空已完成，底栏回到占位话（不给后面几节留活）。
            for (const Tasks::TaskId id : stressIds) {
                if (Tasks::isActiveState(abStress->tasks()->snapshot(id).state)) {
                    abStress->tasks()->cancel(id);
                }
            }
            abStress->clearFinished();
            QApplication::processEvents();
            auto *ab6Status = window.findChild<QLabel *>(MainWindowUI::statusLabelName());
            check(ab6Status != nullptr
                      && ab6Status->text() == MainWindowUI::taskSummaryNoTaskText(),
                  "AB6 收场：账本清空之后底栏中间那格回到占位话",
                  ab6Status == nullptr ? QStringLiteral("<没找到>") : ab6Status->text());
        }
    }
return 0;
}


