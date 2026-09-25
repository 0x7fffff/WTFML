/**
 * @file GroupListDelegate.hpp
 * @brief 「分组设置…」那张分组清单**每一行行尾那两颗「上移 / 下移」箭头**画在哪、什么形状。
 * @author 0x7fffff
 * @date 2026-09-25
 * @note 【为什么这两颗从实例树搬到了这里】
 *
 *       2026-09-25 用户原话：「首先这个点击一下就同时展开和调整了，其次这个箭头不应该在这
 *       里，而应该在 分组设置 里面，这里的删了，去分组设置里面加上，设置那个框下方的上移下移
 *       的按钮就删了」。于是：实例树那张树里组行行尾的两颗**整个撤掉**（那一行只有一个动作
 *       = 展开/收起，多两颗箭头在那儿既有歧义、又和"点这一行"的手感打架），搬进这张窗；
 *       下面那排「上移 / 下移」按钮（moveUpButtonName() / moveDownButtonName()）一并删了，
 *       挪动改成**按行点**。
 *
 *       【这张表不需要角色位】
 *
 *       实例树那边"这一行该画哪一颗"必须由 fillInstanceTree() 写进一个角色里（空组不画、
 *       虚拟的「未分组」没有位置、还有收着的那些 —— 树上的相邻关系和文件里的下标不是一回
 *       事）。这张表里**别的行一行都没有**，行序就是组序 ⇒ 能不能挪是**算出来的**：
 *       第一行没有上移、最后一行没有下移，中间两颗都有（见 canMoveUp() / canMoveDown()）。
 *       少一个角色位就少一种"撞号"的坑（那段说明在 MainWindowUI.hpp）。
 *
 *       【几何的唯一出处就是下面那几个 static】
 *
 *       和 VersionTreeDelegate::clearRect()/refreshRect()、InstanceGroupDelegate::gearRect()
 *       一个规矩：代理按它画、GroupDialog::eventFilter() 按它认下那一下、helpEvent() 按它
 *       决定弹不弹话。三处各算一份的话，错开的形状是"看着点中了箭头、其实点的是那一行"
 *       —— 而这一行的双击是**重命名**（会弹一个模态框），比实例树上那个"顺手展开"更烦人。
 *
 *       【"点这一下被吃掉"靠的是视口上的事件过滤器，不是 editorEvent()】
 *
 *       Qt 那边 itemClicked / itemDoubleClicked 是视图自己发的，代理的 editorEvent()
 *       只管"这一下算不算编辑"，压不住它们（这条教训在 ui/RowActivation.hpp 那段）。
 *       所以认下那一下的是 GroupDialog::eventFilter()，它挂在 `m_groupList->viewport()` 上。
 *
 *       【行高不归这一层管】
 *
 *       这张表的行高是 GroupDialog 灌条目时钉的（`item->setSizeHint()`，和版本树那个常数
 *       一样高），所以这一层**不给** sizeHint()：给了也不会被问。
 */

#pragma once

#include <QStyledItemDelegate>

class QModelIndex;
class QPainter;

/// 「分组设置…」那张清单上挂的代理：普通一行字 + 行尾两颗实心三角。
class GroupListDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit GroupListDelegate(QObject *parent = nullptr);

    /// 行尾那两颗箭头的**命中格**（`row` 是整行的矩形，表坐标）。自左向右是
    /// `[上移] [下移]`，最右边离行尾留出版面那一圈（`versionTreeRowPadding()`）。
    ///
    /// NOTE: 命中格比画出来的三角大一圈（四周各留 3 px），和实例树那两颗、版本树那个
    ///       「取消」小叉同一个做法：12 px 的点击目标太小了。两颗**紧挨着**（不像别处
    ///       留 6 px）：每颗自己已经带了 3 px 余量，中间再有缝就会空出一格"点下去什么都
    ///       没发生"的地方，而那一格正是最容易手抖点到的。
    static QRect moveUpRect(const QRect &row);
    static QRect moveDownRect(const QRect &row);

    /// 那两颗箭头**画出来**的那一圈（命中格四周那 3 px 是手感余量，不画；行矮的时候跟着
    /// 缩，不留半颗在行外 —— 和 InstanceGroupDelegate::gearGlyphRect() 同一条）。
    static QRect moveUpGlyphRect(const QRect &row);
    static QRect moveDownGlyphRect(const QRect &row);

    /// 这一行此刻**画不画**上面/下面那一颗。第一行没有上移、最后一行没有下移
    /// （挪不动的那一侧不画，**不是**画成灰的：一颗都没有的行一眼就说明它在顺序里到头了）。
    ///
    /// NOTE: 它同时是"这一格算不算按钮"的判据（GroupDialog::eventFilter() 读的就是它）
    ///       —— 画和点必须是同一个判据，不然会出现"看着没箭头、点下去却挪了"。
    static bool canMoveUp(const QModelIndex &index);
    static bool canMoveDown(const QModelIndex &index);

    /// 光标落在哪一颗箭头上就弹哪一句话，没落在就是空串。
    /// **带上 Shift 那半句** —— 那是个藏在键盘上的快捷方式，不说没人知道。
    static QString rowActionTip(const QModelIndex &index, const QRect &row, const QPoint &pos);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    /// 悬停提示。基类那个只看 `Qt::ToolTipRole`（整条行一句），这里先问一声 rowActionTip()，
    /// 问不到才交给基类。
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;
};
