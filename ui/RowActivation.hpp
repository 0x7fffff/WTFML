/**
 * @file RowActivation.hpp
 * @brief 树里"这一行被用户激活了"的唯一接线口：**一次手势只调一次处理**。
 * @note 【为什么不能 itemClicked 和 itemActivated 都接】
 *
 * 三棵树（下载页那四张资源表、游戏页的版本树、安装页的加载器树）若把
 * itemClicked（鼠标）和 itemActivated（键盘）两条一起接，Windows 上
 * **双击恰好各触发一次**，于是处理函数被调两次 ——
 * - qabstractitemview.cpp 的 mouseReleaseEvent：`click = (index == d->pressedIndex
 * && index.isValid() && !releaseFromDoubleClick)`，第一发松开发 clicked（:1979），
 * 双击那一发松开时 releaseFromDoubleClick 是 true，clicked **不再发**（所以是
 * 两次、不是三次）；
 * - qtreeview.cpp 的 mouseDoubleClickEvent：发 doubleClicked，紧接着
 * `if (!style()->styleHint(SH_ItemView_ActivateItemOnSingleClick)) emit activated`
 * （:2037 那一段）—— Windows/Fusion 上那个 style hint 是 false，所以 activated 照发。
 * 后果按行分三种：**展开/收起翻两次 = 净效果没动**（用户看到的正是"双击没反应"）、
 * **"下它"那条路下两遍**（第二发撞上"已有下载任务正在进行"，界面弹一句假的"下载失败"，
 * 而真的整合包待导入记录已经被那一发吃掉了）、**openInstallPage 跑两遍**（第二遍把四个
 * 在飞的加载器版本请求 abort 掉重发）。
 *
 * 【另一条同等重要的判据：Qt 自带的双击展开必须关掉】
 * QTreeView 的 expandsOnDoubleClick 默认是 **true**，双击时它自己也会翻一次展开状态
 * （qtreeview.cpp:62 那段）。而这三棵树里的展开/收起**都是处理函数自己做的**
 * （"点一下展开、再点一下收起"是用户要的），两只手一起翻就是"翻两次 = 没动"。
 * 所以接线口顺带 setExpandsOnDoubleClick(false)。
 *
 * 【点得快的时候"每一下都要算"】
 * 上面那条"一次手势只调一次"对**动作**（进安装页、下它）是对的：双击不该办两遍。
 * 但对**开关**（展开/收起）就反了 —— 用户连着点六下想看它翻六次，而 Windows 上
 * **第二下（以及后面每一下点得够快的）到的是 MouseButtonDblClick**：它不是 Press，
 * 视图那边 `clicked` 只在"press + release 都到手"时才发（见上面 qabstractitemview
 * 那两行），于是**那些点击全被丢掉了**。所以开关行要额外补这一发：判据由调用方给的
 * isToggleRow() 说（它比谁都
 * 清楚哪一行的激活是"开/关"、哪一行是"做事"），补完就是"一下点击 = 一次激活"。
 *
 * 【键盘为什么自己接】
 * itemActivated 是**鼠标双击和键盘回车共用的**一个信号，没法只接其中一半。既然鼠标
 * 那一半必须靠 itemClicked（它天然只有一发），键盘那一半就自己在这个过滤器里做 ——
 * 判据照抄 Qt 那两条（qabstractitemview.cpp:2546 的 Key_Enter/Key_Return），
 * 连"不算 auto-repeat"这种细节也一并接过来。空格**不是**激活键（Qt 那边它是
 * type-ahead 搜索），所以这里也不认它。
 *
 * 【展开动画期间，开关行自己收这一下】
 * `QTreeView::setAnimated(true)` 一开，展开/收起那 200 ms 里视图处于 AnimatingState。
 * Qt 6.10 的 `QTreeView::mousePressEvent` 会先问
 * `QTreeViewPrivate::expandOrCollapseItemAtPos()`，而它开头是：
 *
 *     if ((state != NoState && state != EditingState) || !viewport->rect().contains(pos))
 *         return true;   // ← 交回去了**却没办任何事**
 *
 * 返回值 true ⇒ 连 `QAbstractItemView::mousePressEvent` 都不调 ⇒ `pressedIndex` 不落，
 * 松开时 `clicked` 也发不出来。用户在那 200 ms 里点的**任何一行**都被吞掉（症状就是
 * "点了一下没反应"）。开关行（组头）尤其明显：刚展开就想收起来，这一下必然落在动画里。
 *
 * 所以开关行不再等 `itemClicked`：视口上的**按下 + 松开**由这里自己认、自己算。
 * 按下记住那一行（`QPersistentModelIndex`，重灌树时自动作废），松开时若还落在同一行
 * 就补一发并吃掉两发 —— 视图根本看不到这次手势，动画状态也就左右不了它。点得快时那
 * 几发双击事件同样**吃掉**（只补那一发）：视图的双击处理在 `pressedIndex` 没指着这一行
 * 时会把它当"一次按下"补上，紧接着的松开又发一次 clicked，两下相抵。动作行
 * （点实例进设置等等）**不接管**：那里两件事（选中 + 进设置）绑在一起，交给视图。
 *
 * NOTE: 回车的处理**故意不吞事件**：视图自己还会发一发没人接的 activated，而回车照
 * 原样往上传播，父窗口上有默认按钮的场合行为不变。
 */
#pragma once

#include <QKeyEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPersistentModelIndex>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>

#include <functional>
#include <utility>

/// 挂在树上、把"一行被激活"汇总成一次调用的继电器。**不要直接 new** ——
/// 入口是下面的 connectRowActivation()（三件事必须一起做）。
class RowActivationRelay : public QObject {
public:
    using Handler = std::function<void(QTreeWidgetItem *)>;
    /// "这一行的激活是开/关（展开、收起）而不是做事"的判据。空 = 一行都不算（不补那一发）。
    using ToggleRowPredicate = std::function<bool(QTreeWidgetItem *)>;

    RowActivationRelay(QTreeWidget *tree, Handler handler, ToggleRowPredicate isToggleRow)
        : QObject(tree), m_tree(tree), m_handler(std::move(handler)),
          m_isToggleRow(std::move(isToggleRow)) {
        // 鼠标：**只**接 itemClicked。它一次手势只发一发（双击的第二发被 Qt 自己压掉，
        // 见文件头），所以双击天然等于"点了一下"。
        QObject::connect(tree, &QTreeWidget::itemClicked, this,
                         [this](QTreeWidgetItem *item, int) { fire(item); });
        tree->installEventFilter(this);
        // 开关行那"被压掉的第二发"得自己补上：双击事件进的是**viewport**（鼠标事件按
        // 光标位置投递给最上面那个孩子，也就是视口），挂在树上收不到 —— 所以两处都挂。
        m_viewport = tree->viewport();
        if (m_viewport != nullptr) {
            m_viewport->installEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == m_tree && event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            // auto-repeat（按住回车不放）不算"又激活了一次"：不然展开/收起会被翻得飞快。
            if (!key->isAutoRepeat()
                && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
                fire(m_tree->currentItem());
            }
        } else if (watched == m_viewport && event->type() == QEvent::MouseButtonPress) {
            // 开关行（组头）整条是"开/关"，不等视图发 clicked —— 展开/收起动画那 200 ms 里
            // 视图会把手势整个吞掉（文件头那条 Qt 源码）。这里先记住按的是哪一行，
            // 且**不放行**：视图没收到按下，松开时也就不会选别人、不会自己发 clicked。
            auto *mouse = static_cast<QMouseEvent *>(event);
            m_togglePress = QPersistentModelIndex();
            if (mouse->button() == Qt::LeftButton) {
                QTreeWidgetItem *item = m_tree->itemAt(mouse->position().toPoint());
                if (isToggleRow(item)) {
                    m_togglePress = QPersistentModelIndex(m_tree->indexFromItem(item));
                    // 按下本来会把焦点交给视图；这一发被我们收走了，自己补上 —— 不然
                    // "点一下分类、再按回车"会打在一个没有焦点的控件上（没反应）。
                    m_tree->setFocus(Qt::MouseFocusReason);
                    return true;
                }
            }
        } else if (watched == m_viewport && event->type() == QEvent::MouseButtonRelease) {
            if (m_togglePress.isValid()) {
                const QPersistentModelIndex pressed = m_togglePress;
                m_togglePress = QPersistentModelIndex();
                auto *mouse = static_cast<QMouseEvent *>(event);
                // 松开时还落在**同一行**才算一下点击；拖开了就当没按过（与 Qt 的 clicked 同判据）。
                QTreeWidgetItem *item = m_tree->itemFromIndex(QModelIndex(pressed));
                if (item != nullptr && mouse->button() == Qt::LeftButton
                    && m_tree->itemAt(mouse->position().toPoint()) == item) {
                    fire(item);
                }
                return true;
            }
        } else if (watched == m_viewport && event->type() == QEvent::MouseButtonDblClick) {
            // 第二发（点得快的时候是后面每一发）走的是双击事件，而 itemClicked 不发它 ⇒
            // 补一发。**只补开关行**：动作行（进安装页、"下它"）仍然一次手势只办一次，
            // 双击不该办两遍（见文件头）。
            //
            // NOTE: 开关行这一发**吃掉**（不交给视图）。视图的 `mouseDoubleClickEvent` 在
            // `pressedIndex != index` 时会把它**当成一次按下**补一遍（qabstractitemview.cpp
            // 那段 fallback），于是紧跟着的那一发松开又能发一次 clicked —— 合起来正好翻
            // 两次、净效果没动（"点得快的那一下没反应"，见下面 Y7 那种场景）。按下那一半
            // 已经被我们收走了，这里也就没有"视图自己那次按下"可言。
            auto *mouse = static_cast<QMouseEvent *>(event);
            QTreeWidgetItem *item = m_tree->itemAt(mouse->position().toPoint());
            if (mouse->button() == Qt::LeftButton && isToggleRow(item)) {
                fire(item);
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    bool isToggleRow(QTreeWidgetItem *item) const {
        return item != nullptr && m_isToggleRow && m_isToggleRow(item);
    }

    void fire(QTreeWidgetItem *item) {
        if (item != nullptr && m_handler) {
            m_handler(item);
        }
    }

    QTreeWidget *m_tree;
    QWidget *m_viewport = nullptr;
    /// 上一次按下落在开关行上的那一行；松开时补一发。模型重灌时它会自动失效。
    QPersistentModelIndex m_togglePress;
    Handler m_handler;
    ToggleRowPredicate m_isToggleRow;
};

/**
 * @brief 把"这一行被激活了"接到 @p handler 上。动作行一次手势只调一次；开关行每一下点击都算。
 * @param tree 要接线的树（空指针什么都不做，调用方不必先判空）。
 * @param handler 收到的是被激活的那一行（鼠标单击 / 每一发快点击 / 键盘回车）。
 * @param isToggleRow 这一行的激活是不是"开/关"（展开、收起）。缺省 = 一行都不算，
 * 点得快时后面几发会被 Qt 压掉。
 *
 * NOTE: 顺带 setExpandsOnDoubleClick(false) —— 理由见文件头。也就是说：**展开/收起这件事
 * 从此完全由 handler 负责**，Qt 不再插手。这三棵树的 handler 本来就是自己 setExpanded()
 * 的，不要指望双击能替它们展开。
 */
inline void connectRowActivation(QTreeWidget *tree,
                                 std::function<void(QTreeWidgetItem *)> handler,
                                 RowActivationRelay::ToggleRowPredicate isToggleRow = {}) {
    if (tree == nullptr || !handler) {
        return;
    }
    tree->setExpandsOnDoubleClick(false);
    // 挂在树上：树没了它跟着走（没有一个"记得 delete"的地方，也就不会漏）。
    new RowActivationRelay(tree, std::move(handler), std::move(isToggleRow));
}
