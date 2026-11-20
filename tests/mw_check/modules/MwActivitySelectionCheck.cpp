#include "Support.hpp"
#include "TaskDetailPage.hpp"
int MwActivitySelectionCheck() {

        QTemporaryDir fixture(QDir::current().filePath(QStringLiteral(".activity-check-XXXXXX")));
        if (!fixture.isValid()) return 2;
        int failures = 0;
        const auto expect = [&failures](bool ok, const char *message) {
            check(ok, message);
            if (!ok) ++failures;
        };
        {
            ActivityCenterController updates;
            const auto id = updates.begin(Tasks::TaskKind::Other, QStringLiteral("资源更新检查"));
            updates.finish(id, false, QStringLiteral("A：这一版不在列表里（可能不是给这个实例的，或者作者已经删了它）。\nB：读取失败"), false);
            const auto item = updates.item(updates.tasks()->snapshot(id), false, false, false);
            expect(item.errorSummary == QStringLiteral("更新检查结果可能不完整"), "historical update failures use one concise summary");
            expect(item.detail.contains(QStringLiteral("A：当前文件版本不在兼容版本列表中。")) &&
                   item.detail.contains(QStringLiteral("B：读取失败")), "historical update details preserve the first resource and formalise wording");
        }
        {
            const QString previous = AppPaths::minecraftDir();
            AppPaths::setMinecraftDir(fixture.filePath(QStringLiteral("repair-root")));
            const QString id = QStringLiteral("RepairTest");
            ActivityCenterController repairs;
            const auto task = repairs.begin(Tasks::TaskKind::Import, QStringLiteral("Import"), id);
            repairs.finish(task, false, QStringLiteral("missing"), false);
            auto canRepair = [&] { return repairs.item(repairs.tasks()->snapshot(task), false, false, false)
                .capabilities.repairImport; };
            expect(!canRepair(), "missing report does not expose repair action");
            writeFileAt(InstanceFiles::importMarkerPath(id), "{}");
            writeFileAt(AppPaths::versionDir(id) + QStringLiteral("/wtfml.import-report.json"), "{}");
            expect(canRepair(), "failed import with report and unfinished marker exposes repair action");
            TaskDetailPage details;
            auto item = repairs.item(repairs.tasks()->snapshot(task), false, false, false);
            details.setTask(item);
            auto *button = details.findChild<QPushButton *>(QStringLiteral("taskDetailRepairImport"));
            expect(button && !button->isHidden(), "repair capability creates a visible detail action");
            bool requested = false;
            QObject::connect(&details, &TaskDetailPage::actionRequested, &details,
                [&](const QString &key, TaskDetailPage::Action action) {
                    requested = key == item.key && action == TaskDetailPage::Action::RepairImport;
                });
            if (button) button->click();
            expect(requested, "repair detail button dispatches the original task key and explicit repair action");
            TaskCenterPanel panel(nullptr, [&] { return QList<TaskCenter::Item>{item}; },
                {}, {}, {}, {}, nullptr, true);
            bool panelRequested = false;
            panel.setRowActionHandler([&](const QString &key, TaskCenter::RowAction action) {
                panelRequested = key == item.key && action == TaskCenter::RowAction::RepairImport;
            });
            panel.resize(1000, 500);
            panel.showEmbedded();
            auto *row = panel.findChild<QWidget *>(MainWindowUI::taskCenterPanelRowName(0));
            if (row) {
                const QPointF local(row->rect().center());
                const QPointF global(row->mapToGlobal(local.toPoint()));
                QMouseEvent press(QEvent::MouseButtonPress, local, global,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(row, &press);
            }
            QPushButton *panelButton = nullptr;
            for (auto *candidate : panel.findChildren<QPushButton *>())
                if (candidate->text() == QStringLiteral("补齐文件")) panelButton = candidate;
            expect(panelButton != nullptr, "selected task shows repair action in embedded task center");
            if (panelButton) panelButton->click();
            expect(panelRequested, "embedded repair action dispatches the selected task key");
            const auto unrelated = repairs.begin(Tasks::TaskKind::Download, QStringLiteral("Download"), id);
            repairs.finish(unrelated, false, QStringLiteral("error"), false);
            expect(!repairs.item(repairs.tasks()->snapshot(unrelated), false, false, false).capabilities.repairImport,
                "non-import task never borrows an instance repair action");
            QFile::remove(InstanceFiles::importMarkerPath(id));
            expect(!canRepair(), "finished import no longer exposes stale repair action");
            details.setTask(repairs.item(repairs.tasks()->snapshot(task), false, false, false));
            button = details.findChild<QPushButton *>(QStringLiteral("taskDetailRepairImport"));
            expect(button && button->isHidden(), "repair button hides when unfinished marker disappears");
            AppPaths::setMinecraftDir(previous);
        }
        const QString path = fixture.filePath(QStringLiteral("tasks.json"));
        ActivityCenterController activity;
        activity.tasks()->setHistoryPath(path);
        const auto speedTask = activity.begin(Tasks::TaskKind::Download, QStringLiteral("Speed"));
        activity.setTransferSpeed(speedTask, 1048576);
        expect(activity.item(activity.tasks()->snapshot(speedTask), false, false, false).bytesText.contains("/s"),
               "game download speed remains visible when total byte size is unknown");
        activity.finish(speedTask, true, QString(), false);
        expect(activity.transferSpeed(speedTask) < 0, "finished tasks do not retain a stale live speed");
        const auto failed = activity.begin(Tasks::TaskKind::Crash, QStringLiteral("Game error"), {},
            {{QStringLiteral("登录"), Tasks::TaskStepState::Done, {}},
             {QStringLiteral("检查"), Tasks::TaskStepState::Running, {}},
             {QStringLiteral("启动"), Tasks::TaskStepState::Pending, {}}});
        activity.finish(failed, false, QStringLiteral("crash"), false);
        const auto other = activity.begin(Tasks::TaskKind::Download, QStringLiteral("Other error"));
        activity.finish(other, false, QStringLiteral("network"), false);
        expect(activity.markSeen(QString::number(failed)), "visible failure clears unread indication");
        expect(!activity.item(activity.tasks()->snapshot(failed), false, false, false).attentionAcknowledged,
               "viewport visibility does not resolve attention");
        activity.markAllViewed();
        expect(!activity.item(activity.tasks()->snapshot(failed), false, false, false).attentionAcknowledged,
               "opening the task center does not acknowledge error attention");
        expect(activity.markViewed(QString::number(failed)), "viewing a failed task acknowledges its attention");
        expect(activity.item(activity.tasks()->snapshot(failed), false, false, false).attentionAcknowledged &&
                   !activity.item(activity.tasks()->snapshot(other), false, false, false).attentionAcknowledged,
               "attention acknowledgment is scoped to a single task");
        expect(activity.tasks()->snapshot(failed).state == Tasks::TaskState::Failed,
               "acknowledging an error never changes its failure result");
        expect(activity.tasks()->snapshot(failed).steps.size() == 3 &&
                   activity.tasks()->snapshot(failed).steps.at(0).state == Tasks::TaskStepState::Done &&
                   activity.tasks()->snapshot(failed).steps.at(1).state == Tasks::TaskStepState::Failed &&
                   activity.tasks()->snapshot(failed).steps.at(2).state == Tasks::TaskStepState::Pending,
               "failure retains workflow and marks only the active stage failed");
        const auto active = activity.begin(Tasks::TaskKind::Other, QStringLiteral("Active"));
        expect(!activity.markViewed(QString::number(active)) && !activity.markViewed(QStringLiteral("invalid")),
               "viewing active tasks or invalid keys does not acknowledge attention");
        ActivityCenterController restored;
        restored.tasks()->setHistoryPath(path);
        restored.loadHistory();
        expect(restored.item(restored.tasks()->snapshot(failed), false, false, false).attentionAcknowledged,
               "acknowledged attention survives history reload");
        expect(restored.tasks()->snapshot(active).state == Tasks::TaskState::Interrupted &&
                   restored.markViewed(QString::number(active)), "interrupted tasks can be acknowledged on viewing");
        // C2：「清除已完成」把行删了，**没看过的失败**那一份必须跟着收窄 ——
        // 否则 `markViewed()` 再也没有出口（它要求 `has(id)`），面板里一行不剩、底栏那颗
        // 红点却挂到重启为止。触发它不需要特殊操作：面板停在「进行中」或搜索没命中那一行
        // 时点「清除已完成」即可。顺带钉住"收窄之后要有人重画底栏"（clearFinished 里那一发
        // `activityChanged`）—— 少了它，红点要等账本下一次变动才灭。
        //
        // 位置摆在最后：`clearFinished()` 会把前面那些终态行从账本里删掉，而上面
        // `restored` 那几条读的正是盘上那些行。
        {
            const auto c2 = activity.begin(Tasks::TaskKind::Download, QStringLiteral("C2 error"));
            activity.finish(c2, false, QStringLiteral("boom"), false);
            expect(activity.isUnreadFailure(c2), "C2 前提：那一条失败还没被看过");
            int activityEmissions = 0;
            const QMetaObject::Connection counter = QObject::connect(
                &activity, &ActivityCenterController::activityChanged, &activity,
                [&activityEmissions] { ++activityEmissions; });
            activity.clearFinished();
            expect(!activity.tasks()->has(c2),
                   "C2 「清除已完成」把终态那一条收掉了");
            expect(!activity.isUnreadFailure(c2),
                   "C2 行没了，「没看过的失败」也跟着摘掉（红点的出处不再指着一个不存在的号）");
            expect(!activity.collapsedSummary(QDateTime::currentDateTime(), 10 * 60 * 1000).hasFailure,
                   "C2 收起态摘要里那颗红点真的灭了");
            expect(activityEmissions >= 1,
                   "C2 收窄之后自己再发一发 activityChanged（底栏立刻重画，不等下一次变动）");
            QObject::disconnect(counter);
        }
        // C3：每一类任务的默认能力表 —— **不能取消的必须说清为什么**。
        //
        // 用户看到的是详情页上"没有取消按钮"或者一颗按下去没反应的按钮；那份
        // `cancelBlockedReason` 是唯一一句解释（`TaskCenterPanel` 把它摆在详情页上）。
        // 判据两条，缺一条都会让那句话变成"有时候有、有时候没有"：
        // ① 允许取消的那四类（下载 / 安装 / 导入 / 启动）**不许**带这句话 —— 否则详情页
        //    会一边露着「取消」、一边写着"这一项不支持取消"。启动那一类从进程起来之前就
        //    允许取消（流程在下一个检查点停下，见 `LaunchController::abortStart()`），
        //    起来之后那颗由 `MainWindow::cancelLaunch()` 改成停会话。
        // ② 明确不支持取消的那四类必须带一句非空的人话。
        // 崩溃报告与 `Other` 是兜底/终态档（`Other` 里真正长命的那些 —— 迁移、升级副本
        // —— 由各自的执行器覆盖能力表并自己写原因，见 InstanceDialogController），
        // 默认表对它们不表态。
        {
            const auto reasonOf = [](Tasks::TaskKind kind) {
                return ActivityCenterController::defaultCapabilities(kind).cancelBlockedReason;
            };
            const auto canCancelOf = [](Tasks::TaskKind kind) {
                return ActivityCenterController::defaultCapabilities(kind).canCancel;
            };
            for (Tasks::TaskKind kind : {Tasks::TaskKind::Download, Tasks::TaskKind::Install,
                                         Tasks::TaskKind::Import, Tasks::TaskKind::Launch}) {
                expect(canCancelOf(kind) && reasonOf(kind).isEmpty(),
                       qPrintable(QStringLiteral("C3 允许取消的那一类（%1）不带「为什么不能取消」")
                                      .arg(int(kind))));
            }
            for (Tasks::TaskKind kind : {Tasks::TaskKind::Export, Tasks::TaskKind::Backup,
                                         Tasks::TaskKind::Java, Tasks::TaskKind::Integrity}) {
                expect(!canCancelOf(kind) && !reasonOf(kind).trimmed().isEmpty(),
                       qPrintable(QStringLiteral("C3 不支持取消的那一类（%1）**写清了原因**")
                                      .arg(int(kind))));
            }
        }
        return failures == 0 ? 0 : 1;
    
    return 0;
}

