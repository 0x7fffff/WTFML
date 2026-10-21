#include "Support.hpp"
int UiIntegrityReportChecks() {
    // ------------------------------------------------------------------
    // 界面的那一半分三处验：判据层在 verify 的 ZN 段、开页那一半在 mw_check 的 T33、
    // **这一页长什么样**在这里。它是**整页**（顶上四颗导航和底栏不盖，见 OverlayHost），
    // 里面认的是自己那套 `Report`（`ui/` 不 include `src/`，见那一页的文件头）。
    {
        IntegrityReportPage::Report dirty;
        dirty.instanceName = QStringLiteral("1.20.1-我的整合包");
        dirty.summary = QStringLiteral("3120 个文件里 2 个要修：缺 1 个、坏 1 个");
        {
            IntegrityReportPage::Group client;
            client.name = QStringLiteral("客户端");
            client.corrupt = 1;
            client.rows.append({QStringLiteral("客户端 jar"),
                                QStringLiteral("校验不过（内容与 sha1 对不上）"),
                                QStringLiteral("D:/mc/versions/1.20.1/1.20.1.jar")});
            dirty.groups.append(client);

            IntegrityReportPage::Group libs;
            libs.name = QStringLiteral("库");
            libs.missing = 1;
            libs.rows.append(
                {QStringLiteral("com.example:thing:1.0"), QStringLiteral("缺少文件"),
                 QStringLiteral("D:/mc/libraries/com/example/thing/1.0/thing-1.0.jar")});
            dirty.groups.append(libs);

            IntegrityReportPage::Group index;
            index.name = QStringLiteral("资源索引");
            dirty.groups.append(index);

            IntegrityReportPage::Group objects;
            objects.name = QStringLiteral("资源对象");
            dirty.groups.append(objects);
        }

        IntegrityReportPage page;
        page.resize(620, 520);
        page.show();
        QApplication::processEvents();
        page.setReport(dirty);
        QApplication::processEvents();

        const auto labelBy = [&page](const QString &name) {
            return page.findChild<QLabel *>(name);
        };
        const auto buttonBy = [&page](const QString &name) {
            return page.findChild<QPushButton *>(name);
        };
        // 一组那一块里摆着的标签（有问题的组：逐条那几行；干净的组：一句"没有要修的"）。
        const auto bodyLabels = [&page](int g) {
            QWidget *body = page.findChild<QWidget *>(MainWindowUI::integrityReportGroupBodyName(g));
            return body != nullptr ? body->findChildren<QLabel *>() : QList<QLabel *>();
        };

        check(page.objectName() == MainWindowUI::integrityReportPageName()
                  && page.windowTitle() == IntegrityReportPage::windowTitle(),
              "★ U28.1 这一页认得出自己，页眉那句话就是 windowTitle()（唯一出处）",
              page.objectName() + QLatin1Char('/') + page.windowTitle());

        QLabel *summary = labelBy(MainWindowUI::integrityReportSummaryName());
        check(summary != nullptr
                  && summary->text()
                         == QStringLiteral("「%1」%2").arg(dirty.instanceName, dirty.summary),
              "★ U28.2 顶上那句总述 =「实例名 + 一句话结论」，一个字不差",
              summary != nullptr ? summary->text() : QStringLiteral("没画"));

        QLabel *errorLabel = labelBy(MainWindowUI::integrityReportErrorName());
        check(errorLabel != nullptr && !errorLabel->isVisible(),
              "U28.3 没出错的时候那一块**收着**（不是摆一句空的出来）");

        const QStringList expectedTitles{QStringLiteral("客户端（1 个要修）"),
                                         QStringLiteral("库（1 个要修）"),
                                         QStringLiteral("资源索引（0 个要修）"),
                                         QStringLiteral("资源对象（0 个要修）")};
        QStringList actualTitles;
        for (int g = 0; g < 4; ++g) {
            QLabel *title = labelBy(MainWindowUI::integrityReportGroupName(g));
            actualTitles.append(title != nullptr ? title->text() : QStringLiteral("(没画)"));
        }
        check(actualTitles == expectedTitles,
              "★★ U28.4 四组**一个不少**、顺序是客户端 → 库 → 资源索引 → 资源对象，"
              "标题带「N 个要修」（0 个那两组也留着 —— 省掉就分不清「没问题」和「没查」）",
              actualTitles.join(QStringLiteral(" / ")));

        // 逐条那两行：`名字：那句话` + 换行缩进的路径（走唯一出处拼，不在这儿另写格式）。
        QLabel *row0 = labelBy(MainWindowUI::integrityReportRowName(0));
        QLabel *row1 = labelBy(MainWindowUI::integrityReportRowName(1));
        check(row0 != nullptr
                  && row0->text()
                         == MainWindowUI::integrityReportRowText(dirty.groups.at(0).rows.at(0).label,
                                                                 dirty.groups.at(0).rows.at(0).detail,
                                                                 dirty.groups.at(0).rows.at(0).path),
              "★ U28.5 第一条问题就是那一行「名字：那句话 / 缩进的路径」",
              row0 != nullptr ? row0->text() : QStringLiteral("没画"));
        check(row1 != nullptr && row1->text().contains(QStringLiteral("缺少文件"))
                  && row1->text().contains(QStringLiteral("thing-1.0.jar")),
              "U28.5 第二条问题（缺的那个库）也在，路径完整", row1 != nullptr ? row1->text() : QString());

        // 干净的那两组：各自摆一句"没有要修的"（不是空一块）。
        const QList<QLabel *> cleanIndex = bodyLabels(2);
        const QList<QLabel *> cleanObjects = bodyLabels(3);
        check(cleanIndex.size() == 1
                  && cleanIndex.at(0)->text() == MainWindowUI::integrityReportAllGoodText(),
              "★ U28.6 干净的那一组摆一句「这一组没有要修的文件。」（不是一块空白）",
              cleanIndex.isEmpty() ? QStringLiteral("没画") : cleanIndex.at(0)->text());
        check(cleanObjects.size() == 1
                  && cleanObjects.at(0)->text() == MainWindowUI::integrityReportAllGoodText(),
              "U28.6 另一组干净的也一样");

        // 按钮：两颗。（「补齐…」只在**真有要修的**时候露。）
        QPushButton *copy = buttonBy(MainWindowUI::integrityReportCopyName());
        QPushButton *repair = buttonBy(MainWindowUI::integrityReportRepairName());
        QPushButton *closeBtn = buttonBy(MainWindowUI::integrityReportCloseName());
        check(copy != nullptr && copy->isVisible()
                  && copy->text() == MainWindowUI::integrityReportCopyText(),
              "U28.7 「复制报告」永远在（报告就是这一页上那些字）",
              copy != nullptr ? copy->text() : QStringLiteral("没画"));
        check(repair != nullptr && repair->isVisible()
                  && repair->text() == MainWindowUI::integrityReportRepairText(),
              "★ U28.7 有要修的文件时「补齐缺失或损坏文件」露出来",
              repair != nullptr ? repair->text() : QStringLiteral("没画"));
        check(repair != nullptr
                  && repair->property("wtfmlButtonKind").toString()
                         == Theme::buttonKindKey(Theme::ButtonKind::Primary),
              "★ U28.7 它是这一页**唯一的主动作**（主题色实底 + 白字）",
              repair != nullptr ? repair->property("wtfmlButtonKind").toString()
                                : QStringLiteral("没画"));
        check(closeBtn != nullptr
                  && closeBtn->property("wtfmlButtonKind").toString()
                         == Theme::buttonKindKey(Theme::ButtonKind::Neutral),
              "U28.7 「只查看」是一般性的那一档（它不是动作，是这一页的出路之一）");

        // 两颗动作按钮**只报"点了哪一个"**，真正的活全在 MainWindow 那边。
        int copyClicks = 0;
        int repairClicks = 0;
        QObject::connect(&page, &IntegrityReportPage::actionRequested,
                         [&copyClicks, &repairClicks](IntegrityReportPage::Action action) {
                             if (action == IntegrityReportPage::Action::CopyReport) {
                                 ++copyClicks;
                             } else if (action == IntegrityReportPage::Action::Repair) {
                                 ++repairClicks;
                             }
                         });
        if (copy != nullptr) {
            copy->click();
        }
        check(copyClicks == 1 && repairClicks == 0,
              "U28.8 点「复制报告」只发 CopyReport（不顺手把补文件也点起来）");
        if (repair != nullptr) {
            repair->click();
        }
        check(repairClicks == 1, "U28.8 点「补齐缺失或损坏文件」发 Repair");
        if (closeBtn != nullptr) {
            closeBtn->click();
        }
        check(page.result() == QDialog::Rejected,
              "★ U28.8 「只查看」= reject()（和顶上那颗「← 返回」是同一条路）",
              QString::number(page.result()));

        {
            // 上一段点了「只查看」= reject() —— 那一下把这一页**藏起来**了（`isVisible()` 从
            // 那一刻起恒假）。这一段的断言全在可见性上，所以先把页摆回去。
            page.show();
            QApplication::processEvents();
            IntegrityReportPage::Report clean;
            clean.instanceName = QStringLiteral("1.20.1");
            clean.summary = QStringLiteral("3120 个文件都完好");
            for (const QString &name : {QStringLiteral("客户端"), QStringLiteral("库"),
                                        QStringLiteral("资源索引"),
                                        QStringLiteral("资源对象")}) {
                IntegrityReportPage::Group group;
                group.name = name;
                clean.groups.append(group);
            }
            page.setReport(clean);
            QApplication::processEvents();
            QPushButton *copy2 = buttonBy(MainWindowUI::integrityReportCopyName());
            QPushButton *repair2 = buttonBy(MainWindowUI::integrityReportRepairName());
            check(copy2 != nullptr && copy2->isVisible() && repair2 != nullptr
                      && !repair2->isVisible(),
                  "★ U28.9 全都完好的时候「补齐缺失或损坏文件」**收着**（摆一颗点了什么都不做的"
                  "按钮比不给还糟）");
            check(labelBy(MainWindowUI::integrityReportRowName(0)) == nullptr,
                  "U28.9 也没有任何一条问题行");
            bool allGoodFour = true;
            for (int g = 0; g < 4; ++g) {
                const QList<QLabel *> labels = bodyLabels(g);
                allGoodFour = allGoodFour && labels.size() == 1
                              && labels.at(0)->text() == MainWindowUI::integrityReportAllGoodText();
            }
            check(allGoodFour, "U28.9 四组都摆着那句「没有要修的」");
        }

        // ---------- 压根没跑起来那一份：只有那句错 + 「复制报告」 ----------
        {
            IntegrityReportPage::Report failed;
            failed.instanceName = QStringLiteral("1.20.1");
            failed.error = QStringLiteral("读不出这个实例的 version.json，没法知道它该有哪些文件。");
            failed.summary = failed.error;
            page.setReport(failed);
            QApplication::processEvents();
            QLabel *err = labelBy(MainWindowUI::integrityReportErrorName());
            QPushButton *copy3 = buttonBy(MainWindowUI::integrityReportCopyName());
            QPushButton *repair3 = buttonBy(MainWindowUI::integrityReportRepairName());
            check(err != nullptr && err->isVisible()
                      && err->text().startsWith(MainWindowUI::integrityReportErrorTitleText())
                      && err->text().contains(failed.error),
                  "★ U28.10 「体检没跑起来」时那块错露出来，标题 + 正文都在",
                  err != nullptr ? err->text() : QStringLiteral("没画"));
            check(copy3 != nullptr && copy3->isVisible() && repair3 != nullptr
                      && !repair3->isVisible(),
                  "U28.10 那一趟没有任何「要修的文件」（清单都没列出来）⇒ 只有「复制报告」");
            bool noGroupsDrawn = true;
            for (int g = 0; g < 4; ++g) {
                noGroupsDrawn = noGroupsDrawn
                                && labelBy(MainWindowUI::integrityReportGroupName(g)) == nullptr;
            }
            check(noGroupsDrawn,
                  "★ U28.10 那一趟**不画四个空组**（清单都没列出来，画「0 个要修」是骗人）");
        }
    }
    return 0;
}


