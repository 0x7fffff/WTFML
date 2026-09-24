/**
 * @file RowActivation.hpp
 * @brief 树里"这一行被用户激活了"的唯一接线口：**一次手势只调一次处理**。
 * @author 0x7fffff
 * @date 2026-09-24
 * @note 【为什么不能 itemClicked 和 itemActivated 都接】
 *
 *       2026-09-24 修的。三棵树（下载页那四张资源表、游戏页的版本树、安装页的加载器树）
 *       原来都是两条一起接：itemClicked 给鼠标、itemActivated 给键盘。而 Windows 上
 *       **双击恰好各触发一次**，于是处理函数被调两次 ——
 *         - qabstractitemview.cpp 的 mouseReleaseEvent：`click = (index == d->pressedIndex
 *           && index.isValid() && !releaseFromDoubleClick)`，第一发松开发 clicked（:1979），
 *           双击那一发松开时 releaseFromDoubleClick 是 true，clicked **不再发**（所以是
 *           两次、不是三次）；
 *         - qtreeview.cpp 的 mouseDoubleClickEvent：发 doubleClicked，紧接着
 *           `if (!style()->styleHint(SH_ItemView_ActivateItemOnSingleClick)) emit activated`
 *           （:2037 那一段）—— Windows/Fusion 上那个 style hint 是 false，所以 activated 照发。
 *       后果按行分三种：**展开/收起翻两次 = 净效果没动**（用户看到的正是"双击没反应"）、
 *       **"下它"那条路下两遍**（第二发撞上"已有下载任务正在进行"，界面弹一句假的"下载失败"，
 *       而真的整合包待导入记录已经被那一发吃掉了）、**openInstallPage 跑两遍**（第二遍把四个
 *       在飞的加载器版本请求 abort 掉重发）。
 *
 *       【另一条同等重要的判据：Qt 自带的双击展开必须关掉】
 *       QTreeView 的 expandsOnDoubleClick 默认是 **true**，双击时它自己也会翻一次展开状态
 *       （qtreeview.cpp:2050-2062 那段）。而这三棵树里的展开/收起**都是处理函数自己做的**
 *       （"点一下展开、再点一下收起"是用户要的），两只手一起翻就是"翻两次 = 没动"。
 *       所以接线口顺带 setExpandsOnDoubleClick(false)。
 *
 *       【键盘为什么改成自己接】
 *       itemActivated 是**鼠标双击和键盘回车共用的**一个信号，没法只接其中一半。既然鼠标
 *       那一半必须靠 itemClicked（它天然只有一发），键盘那一半就自己在这个过滤器里做 ——
 *       判据照抄 Qt 那两条（qabstractitemview.cpp:2546 的 Key_Enter/Key_Return），
 *       连"不算 auto-repeat"这种细节也一并接过来。空格**不是**激活键（Qt 那边它是
 *       type-ahead 搜索），所以这里也不认它。
 *
 *       NOTE: 回车的处理**故意不吞事件**：视图自己还会发一发没人接的 activated，而回车照
 *             原样往上传播（和接线之前完全一样），父窗口上有默认按钮的场合行为不变。
 */
#pragma once

#include <QKeyEvent>
#include <QObject>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <functional>
#include <utility>

/// 挂在树上、把"一行被激活"汇总成一次调用的继电器。**不要直接 new** ——
/// 入口是下面的 connectRowActivation()（两件事必须一起做）。
class RowActivationRelay : public QObject {
public:
    RowActivationRelay(QTreeWidget *tree, std::function<void(QTreeWidgetItem *)> handler)
        : QObject(tree), m_tree(tree), m_handler(std::move(handler)) {
        // 鼠标：**只**接 itemClicked。它一次手势只发一发（双击的第二发被 Qt 自己压掉，
        // 见文件头），所以双击天然等于"点了一下"。
        QObject::connect(tree, &QTreeWidget::itemClicked, this,
                         [this](QTreeWidgetItem *item, int) { fire(item); });
        tree->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            // auto-repeat（按住回车不放）不算"又激活了一次"：不然展开/收起会被翻得飞快。
            if (!key->isAutoRepeat()
                && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
                fire(m_tree->currentItem());
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void fire(QTreeWidgetItem *item) {
        if (item != nullptr && m_handler) {
            m_handler(item);
        }
    }

    QTreeWidget *m_tree;
    std::function<void(QTreeWidgetItem *)> m_handler;
};

/**
 * @brief 把"这一行被激活了"接到 @p handler 上，一次手势只调一次。
 * @param tree    要接线的树（空指针什么都不做，调用方不必先判空）。
 * @param handler 收到的是被激活的那一行（鼠标单击 / 双击的第一下 / 键盘回车）。
 *
 * NOTE: 顺带 setExpandsOnDoubleClick(false) —— 理由见文件头。也就是说：**展开/收起这件事
 *       从此完全由 handler 负责**，Qt 不再插手。这三棵树的 handler 本来就是自己 setExpanded()
 *       的，接线之后不要再指望双击能替它们展开。
 */
inline void connectRowActivation(QTreeWidget *tree,
                                 std::function<void(QTreeWidgetItem *)> handler) {
    if (tree == nullptr || !handler) {
        return;
    }
    tree->setExpandsOnDoubleClick(false);
    // 挂在树上：树没了它跟着走（没有一个"记得 delete"的地方，也就不会漏）。
    new RowActivationRelay(tree, std::move(handler));
}
