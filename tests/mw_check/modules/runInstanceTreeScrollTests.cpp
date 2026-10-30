#include "Support.hpp"
#include "controllers/MainWindowViewData.hpp"

#include <QEventLoop>
#include <QPointer>
#include <QPixmap>
#include <QScrollBar>
#include <QTimer>
#include <QTreeWidget>

namespace {

/// 跑几轮事件循环：restoreTreeScroll() 里那一发是 `QTimer::singleShot(0)`。
void pumpEvents(int rounds = 3) {
    for (int i = 0; i < rounds; ++i) {
        QEventLoop loop;
        QTimer::singleShot(0, &loop, &QEventLoop::quit);
        loop.exec();
    }
}

/// 一张"装得下、要滚"的实例树。`dontShowOnScreen` 让 show() 照常排版但不上屏：
/// 这一节量的是滚动条，不需要真窗口，也不想在用户眼前闪一下。
QTreeWidget *makeTree(bool dontShowOnScreen) {
    auto *tree = new QTreeWidget;
    if (dontShowOnScreen) {
        tree->setAttribute(Qt::WA_DontShowOnScreen);
    }
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    for (int group = 0; group < 6; ++group) {
        auto *head = new QTreeWidgetItem(tree);
        head->setText(0, QStringLiteral("组 %1").arg(group));
        head->setExpanded(true);
        for (int row = 0; row < 8; ++row) {
            auto *item = new QTreeWidgetItem(head);
            item->setText(0, QStringLiteral("实例 %1-%2").arg(group).arg(row));
        }
    }
    tree->resize(240, 160);
    return tree;
}

QTreeWidgetItem *lastItemOf(QTreeWidget *tree) {
    QTreeWidgetItem *head = tree->topLevelItem(tree->topLevelItemCount() - 1);
    return head->child(head->childCount() - 1);
}

}  // namespace

void runInstanceTreeScrollTests() {
    // =====================================================================
    // T44. 「管理页右侧每次打开都在最底下」
    //
    // 真因不在"选中项挑错了"：`syncTreeToCombo()` 选的是当前实例（多半是最后装的那一个），
    // Qt 会滚到它那儿。开机那一趟这棵树还藏着 —— 这一滚**当场滚不动**（range 还是 0），
    // 被攒到它第一次显示、排完版那一下才补（量法：藏起来选完 value 还是 0，show 之后
    // 立刻变成 max）。所以"选的时候挡一下 scrollTo()"没用，得等排版结束再把位置放回去。
    //
    // 这一节钉三件事：① 上面那个 Qt 行为还在（restore 的前提）；② 放回去真的放回去了，
    // 而且选中项没丢；③ 树露着的那一趟是当场放回，不用等一轮事件循环。
    // =====================================================================
    {
        // ---------- ① 前提：藏起来选最后一条 ⇒ 露出来时被拉到底 ----------
        QTreeWidget *plain = makeTree(true);
        plain->setCurrentItem(lastItemOf(plain));
        check(plain->verticalScrollBar()->value() == 0,
              "★ T44 藏着的树选最后一条时滚动条还动不了（range 是 0 ⇒ 这一滚被攒下了）",
              QString::number(plain->verticalScrollBar()->value()));
        plain->show();
        pumpEvents();
        const int plainMax = plain->verticalScrollBar()->maximum();
        check(plainMax > 0 && plain->verticalScrollBar()->value() == plainMax,
              "★★ T44 一露出来就被补滚到最底 —— 这就是「每次打开都在最底下」那一下"
              "（这条红了说明 Qt 的行为变了、restoreTreeScroll() 的前提要重看）",
              QStringLiteral("%1 / %2").arg(plain->verticalScrollBar()->value()).arg(plainMax));
        plain->hide();
        delete plain;

        // ---------- ② 放回去：重建前的位置还在，选中项也没丢 ----------
        QTreeWidget *held = makeTree(true);
        QTreeWidgetItem *target = lastItemOf(held);
        const int keep = held->verticalScrollBar()->value();
        held->setCurrentItem(target);
        MainWindowViewData::restoreTreeScroll(held, keep);
        held->show();
        const QPixmap firstFrame = held->grab();
        check(!firstFrame.isNull() && held->verticalScrollBar()->value() == keep,
              "T44 first animation snapshot already uses the restored scroll position before event pumping");
        pumpEvents();
        check(held->verticalScrollBar()->value() == keep,
              "★★ T44 重建前记下的滚动位置在排版之后被放回去了（不再是列表底部）",
              QStringLiteral("%1（max %2）")
                  .arg(held->verticalScrollBar()->value())
                  .arg(held->verticalScrollBar()->maximum()));
        check(held->currentItem() == target,
              "★ T44 放回滚动位置**不改选中项**（当前实例还是那一条）",
              held->currentItem() ? held->currentItem()->text(0) : QStringLiteral("<无>"));
        held->hide();
        delete held;

        // ---------- ③ 树露着的那一趟：当场放回 ----------
        QTreeWidget *live = makeTree(true);
        live->show();
        pumpEvents();
        QScrollBar *bar = live->verticalScrollBar();
        const int restoreTo = bar->maximum() / 3;
        bar->setValue(restoreTo);
        live->setCurrentItem(lastItemOf(live));  // 模拟"重灌后又选了一遍当前实例"
        MainWindowViewData::restoreTreeScroll(live, restoreTo);
        check(bar->value() == restoreTo,
              "★★ T44 眼前看得见的那一趟不用等事件循环：调完当场就在原处",
              QStringLiteral("%1（期望 %2）").arg(bar->value()).arg(restoreTo));
        live->hide();
        delete live;
    }
    {
        auto *tree = makeTree(true);
        tree->setCurrentItem(lastItemOf(tree));
        MainWindowViewData::restoreTreeScroll(tree, 0);
        tree->show();
        const int latest = qMin(5, tree->verticalScrollBar()->maximum());
        MainWindowViewData::restoreTreeScroll(tree, latest);
        pumpEvents();
        check(latest > 0 && tree->verticalScrollBar()->value() == latest,
              "T44 newer visible restore cannot be overwritten by an older queued restore");
        tree->hide();
        MainWindowViewData::restoreTreeScroll(tree, 2);
        MainWindowViewData::restoreTreeScroll(tree, 4);
        tree->show();
        pumpEvents();
        check(tree->verticalScrollBar()->value() == 4,
              "T44 reopening a laid-out tree restores the latest request without needing a range change");
        tree->hide();
        tree->setCurrentItem(lastItemOf(tree));
        tree->show();
        tree->grab();
        check(tree->verticalScrollBar()->value() == 4,
              "T44 reopening management without rebuilding preserves the first captured frame");
        pumpEvents();
        tree->clear();
        auto *head = new QTreeWidgetItem(tree);
        for (int i = 0; i < 60; ++i) new QTreeWidgetItem(head);
        head->setExpanded(true);
        tree->setCurrentItem(head->child(59));
        MainWindowViewData::restoreTreeScroll(tree, 3);
        pumpEvents();
        check(tree->verticalScrollBar()->value() == 3,
              "T44 rebuilding a visible tree retains its position after deferred layout");
        tree->hide();
        MainWindowViewData::restoreTreeScroll(tree, 0);
        tree->show();
        QPointer<QTreeWidget> destroyed = tree;
        delete tree;
        pumpEvents();
        check(destroyed.isNull(), "T44 destruction cancels pending restoration safely");
    }

}
