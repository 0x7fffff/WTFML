#include "Support.hpp"
int UiInstanceSnapshotsChecks() {
    // ---------- 实例概览那一排上那两颗「快照」 ----------
    {
        using Dlg = InstanceSettingsDialog;
        Dlg dialog(QStringLiteral("我的整合包"), QStringLiteral("我的整合包-1.20.1-Forge"),
                   Dlg::Override(), Dlg::GlobalValues(), QStringList(), true);
        dialog.resize(980, 640);
        dialog.show();
        QApplication::processEvents();

        const auto byName = [&dialog](const QString &name) {
            return dialog.findChild<QPushButton *>(name);
        };
        auto *create = byName(Dlg::createSnapshotButtonName());
        auto *restore = byName(Dlg::restoreSnapshotButtonName());
        auto *copy = byName(Dlg::copyButtonName());
        auto *remove = byName(Dlg::deleteButtonName());

        check(create != nullptr && restore != nullptr,
              "★★ U30.2 实例概览那一排上两颗「快照」都在（创建快照… / 从快照恢复…）");
        if (create != nullptr && restore != nullptr) {
            check(create->objectName() == Dlg::createSnapshotButtonName() &&
                      restore->objectName() == Dlg::restoreSnapshotButtonName(),
                  "U30.2 objectName 挂的就是唯一出处里那两个");
            check(create->text() == Dlg::createSnapshotText() &&
                      restore->text() == Dlg::restoreSnapshotText() &&
                      create->text() == QStringLiteral("创建快照…") &&
                      restore->text() == QStringLiteral("从快照恢复…"),
                  "U30.2 那四个字（连省略号）就是唯一出处里写的",
                  create->text() + QStringLiteral(" / ") + restore->text());
            check(!create->isVisible() && !restore->isVisible(),
                  "★★ U30.2 概览那一页上**不再**摆快照那两颗（动作都搬进各自那页）");
            dialog.showPage(QStringLiteral("snapshots"));
            QApplication::processEvents();
            check(create->isVisible() && restore->isVisible(),
                  "U30.2 切到「快照」那一页，两颗都看得见");
            // 省略号是**故意的**：按下去之后还会再问一句（创建那颗问"要不要连着能重新
            // 下回来的那部分一起打包"）/ 还要挑一份（恢复那颗）。这套界面里省略号就是
            // "后面还有一步"的意思，两颗都得带着。
            check(create->text().endsWith(QChar(0x2026)) &&
                      restore->text().endsWith(QChar(0x2026)),
                  "★ U30.2 两颗都带省略号（按下去之后还会再问一句 / 还要挑一份）");
            check(create->toolTip().contains(QStringLiteral("后台")) &&
                      create->toolTip().contains(QStringLiteral("cache/backups")) &&
                      restore->toolTip().contains(QStringLiteral("覆盖")),
                  "★★ U30.2 两句 tooltip 各说各的：创建那颗说清「打在后台跑、包落在哪儿」，"
                  "恢复那颗**自己先说清会覆盖现在这些文件**（别等点下去才知道）",
                  create->toolTip());

            // 格子底下那两行小字：创建那颗必须点名"默认不打包什么"，恢复那颗必须点名
            // "还原之前会自动留一份" —— 这两句正是用户唯一需要先知道的事。
            const auto hintOf = [](QWidget *button) {
                QWidget *cell = button->parentWidget();
                QLabel *hint = cell != nullptr ? cell->findChild<QLabel *>() : nullptr;
                return hint != nullptr ? hint->text() : QString();
            };
            check(hintOf(create).contains(QStringLiteral("默认不打包")) &&
                      hintOf(create).contains(QStringLiteral("资源文件")) &&
                      hintOf(restore).contains(QStringLiteral("自动给现在这一份留一份")) &&
                      hintOf(restore).contains(QStringLiteral("核对")),
                  "★★ U30.2 两行小字都点到了正地方（默认不打包能重新下回来的那部分 / "
                  "还原之前自动留一份、还原之后逐文件核对）",
                  hintOf(restore));

            // ---- 颜色：创建是"干这件事"（主题色），恢复是"删了就拿不回来"那一档 ----
            const auto kindOf = [](QPushButton *button) {
                const QVariant value = button->property("wtfmlButtonKind");
                return value.isValid() ? value.toString()
                                       : Theme::buttonKindKey(Theme::ButtonKind::Accent);
            };
            check(kindOf(create) == Theme::buttonKindKey(Theme::ButtonKind::Accent) &&
                      kindOf(restore) == Theme::buttonKindKey(Theme::ButtonKind::Danger),
                  "★★ U30.2 「创建快照…」是主题色那一档（创建 = 干这件事），"
                  "「从快照恢复…」和「删除实例」同一档（它把现在这一份整个换掉）",
                  kindOf(create) + QStringLiteral(" / ") + kindOf(restore));
            auto *integrity = byName(Dlg::integrityCheckButtonName());
            auto *updates = byName(Dlg::checkUpdatesButtonName());
            check(remove != nullptr && kindOf(remove) == kindOf(restore) &&
                      integrity != nullptr && updates != nullptr &&
                      kindOf(integrity) == Theme::buttonKindKey(Theme::ButtonKind::Neutral) &&
                      kindOf(updates) == Theme::buttonKindKey(Theme::ButtonKind::Neutral),
                  "★★ U30.2 而这一排上原来那颗红的「删除实例」照旧红；"
                  "「检查并修复」「检查更新」那些是**白的那一档**（`Neutral` —— "
                  "它们只读不写，按下去出现的是个结果（不挂档就会跟着默认"
                  "吃了绿的）—— 加了一颗红的**没有**把别的一起带下来（全红等于没有红）",
                  (remove != nullptr ? kindOf(remove) : QStringLiteral("<没找到>"))
                      + QStringLiteral(" / ")
                      + (integrity != nullptr ? kindOf(integrity)
                                              : QStringLiteral("<没找到>"))
                      + QStringLiteral(" / ")
                      + (updates != nullptr ? kindOf(updates) : QStringLiteral("<没找到>")));

            // ---- 「检查并修复」旁边的「更新中心」也得挂上档 ----
            //
            // 概览里这三颗（检查更新 / 更新中心 / 检查并修复）只调了
            // `prepareActionButton()`、**没挂档** ⇒ 会跟着默认吃了绿的那一档。
            // 这一条钉的就是"别再有第四颗漏挂"：挑法一样，**认名字不认位置**。
            auto *updateCenter = byName(Dlg::updateCenterButtonName());
            check(updateCenter != nullptr &&
                      kindOf(updateCenter) ==
                          Theme::buttonKindKey(Theme::ButtonKind::Neutral),
                  "★★ U30.2 概览里那三颗「检查…」（检查更新 / 更新中心 / 检查并修复）"
                  "**三颗都挂了档**、都是白的那一档 —— 它们只读不写（判据见 Theme.hpp 的"
                  "`ButtonKind` 那段），漏挂一颗就会跟着默认变成绿的",
                  updateCenter != nullptr ? kindOf(updateCenter)
                                          : QStringLiteral("<没找到>"));

            // ---- 四颗按钮**不在同一条线上** ----
            //
            // 复制 / 删除在「实例操作」页、快照那两颗在「快照」页 —— **不在同一页上就没有
            // 一条线可比**，所以每一页各自比一遍（量法不变：同一行比 x、换行比 y）。
            const auto pointOf = [](QWidget *w) {
                if (w == nullptr) {
                    return QPoint(-1, -1);
                }
                const QPoint p = w->mapToGlobal(QPoint(0, 0));
                return QPoint(p.x(), p.y());
            };
            const auto before = [](const QPoint &a, const QPoint &b) {
                return a.y() < b.y() || (a.y() == b.y() && a.x() < b.x());
            };
            dialog.showPage(QStringLiteral("operationsmenu"));
            QApplication::processEvents();
            check(copy != nullptr && remove != nullptr && copy->isVisible() &&
                      remove->isVisible() && before(pointOf(copy), pointOf(remove)),
                  "★ U30.2 实例操作页上「复制实例」排在「删除实例」**前面**"
                  "（左边多出一个、右边少掉一个）",
                  QStringLiteral("(%1,%2) (%3,%4)")
                      .arg(pointOf(copy).x()).arg(pointOf(copy).y())
                      .arg(pointOf(remove).x()).arg(pointOf(remove).y()));
            dialog.showPage(QStringLiteral("snapshots"));
            QApplication::processEvents();
            check(create != nullptr && restore != nullptr && create->isVisible() &&
                      restore->isVisible() && before(pointOf(create), pointOf(restore)),
                  "★ U30.2 快照页上「创建快照…」排在「从快照恢复…」**前面**"
                  "（留一份 / 换回去）",
                  QStringLiteral("(%1,%2) (%3,%4)")
                      .arg(pointOf(create).x()).arg(pointOf(create).y())
                      .arg(pointOf(restore).x()).arg(pointOf(restore).y()));

            // ---- 点一下：各发各的信号（这一层只发信号，干活在 MainWindow）----
            int createClicks = 0;
            int restoreClicks = 0;
            QObject::connect(&dialog, &Dlg::createSnapshotRequested,
                             [&createClicks] { ++createClicks; });
            QObject::connect(&dialog, &Dlg::restoreSnapshotRequested,
                             [&restoreClicks] { ++restoreClicks; });
            create->click();
            restore->click();
            QApplication::processEvents();
            check(createClicks == 1 && restoreClicks == 1,
                  "★★ U30.2 两颗各发各的信号（串线的话按「创建快照…」会弹一个「要拿以前的"
                  "快照把现在这一份换掉吗」）",
                  QStringLiteral("创建 %1 / 恢复 %2").arg(createClicks).arg(restoreClicks));
        }

        dialog.close();
        QApplication::processEvents();
    }
    return 0;
}


