/**
 * @file GroupListDelegate.hpp
 * @brief 「分组设置…」那张分组清单**每一行行尾那个方框里的上/下移箭头**画在哪、什么形状。
 * @note 【上移/下移这两颗只在这张窗的行尾】
 *
 * 实例树那张树里组行行尾没有箭头（那一行只有一个动作 = 展开/收起，多两颗箭头在那儿
 * 既有歧义、又和"点这一行"的手感打架）；挪动在这张窗里**按行点**。
 *
 * 【形状：一颗在上、一颗在下，装在一个方框里】
 *
 * 箭头都在最右边，上面是上箭头、下面是下箭头，用方框框起来；组与组之间留一点距离，
 * 不然粘在一起分辨不开：
 *
 * [组名 (3)] ┌──┐ ← 行尾，贴着右边缘
 * │ ▲│ ← 上移（方框上半）
 * │ ▼│ ← 下移（方框下半）
 * └──┘
 *
 * ——**上下叠而不是并排**（并排那两颗要占 36 px 宽，一眼看上去像是行尾拖着两个按钮；
 * 叠起来之后行尾只有一格，方框替它们说明"这两颗是一件事"）。方框是**这两格的可见
 * 轮廓**，不是另加的一层装饰：画的就是 moveBoxRect()，认下那一下也认它。
 *
 * 【行与行之间留一条缝】
 *
 * 46 px 的行紧紧码在一起时，两行的字之间只是行内的余量，扫一眼分不出这是两条。
 * 所以**条目矩形比画出来的那一块高** `rowGap()`
 * 那么几像素：字、背景、方框一律按 contentRect() 摆，底下那一条带子永远是视口的底色。
 *
 * NOTE: 这个"高出来"是**灌条目的那一侧**（GroupDialog::rebuildGroupList()）加在
 * `item->setSizeHint()` 上的，这一层不参与行高（见文件末那段）。
 *
 * 【这张表不需要角色位】
 *
 * 实例树那边"这一行该画哪一颗"必须由 fillInstanceTree() 写进一个角色里（空组不画、
 * 虚拟的「未分组」没有位置、还有收着的那些 —— 树上的相邻关系和文件里的下标不是一回
 * 事）。这张表里**别的行一行都没有**，行序就是组序 ⇒ 能不能挪是**算出来的**：
 * 第一行没有上移、最后一行没有下移，中间两颗都有（见 canMoveUp() / canMoveDown()）。
 * 少一个角色位就少一种"撞号"的坑（那段说明在 MainWindowUI.hpp）。
 *
 * 【几何的唯一出处就是下面那几个 static，而且它们吃的都是**条目矩形**】
 *
 * 和 VersionTreeDelegate::clearRect()/refreshRect()、InstanceGroupDelegate::gearRect()
 * 一个规矩：代理按它画、GroupDialog::eventFilter() 按它认下那一下、helpEvent() 按它
 * 决定弹不弹话。三处各算一份的话，错开的形状是"看着点中了箭头、其实点的是那一行"
 * —— 而这一行的双击是**重命名**（会弹一个模态框），比实例树上那个"顺手展开"更烦人。
 *
 * 传进来的矩形一律是**条目矩形**（`option.rect` / `visualItemRect()` /
 * `QAbstractItemView::visualRect()` 给的那一个），不是画出来的那一块 —— 调用方一个
 * 都不用知道"底下还有一条缝"：映射在 contentRect() 里做一次，三个调用点原样传。
 *
 * 【"点这一下被吃掉"靠的是视口上的事件过滤器，不是 editorEvent()】
 *
 * Qt 那边 itemClicked / itemDoubleClicked 是视图自己发的，代理的 editorEvent()
 * 只管"这一下算不算编辑"，压不住它们（这条教训在 ui/RowActivation.hpp 那段）。
 * 所以认下那一下的是 GroupDialog::eventFilter()，它挂在 `m_groupList->viewport()` 上。
 *
 * 【行高不归这一层管】
 *
 * 这张表的行高是 GroupDialog 灌条目时钉的（`item->setSizeHint()`，和版本树那个常数
 * 一样高**再加一条缝**），所以这一层**不给** sizeHint()：给了也不会被问。
 */

#pragma once

#include <QStyledItemDelegate>

class QModelIndex;
class QPainter;

/// 「分组设置…」那张清单上挂的代理：普通一行字 + 行尾一个装了两颗三角的方框。
class GroupListDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit GroupListDelegate(QObject *parent = nullptr);

    /// 行与行之间空出来的那几像素。**加在条目行高上的就是这个数**：
    /// `GroupDialog::rebuildGroupList()` 里
    /// `setSizeHint(versionTreeRowHeight() + rowGap())`。
    static int rowGap();

    /// 条目矩形 → 真正画出来的那一块（上面一行字、行尾一个方框）。底下让出 `rowGap()`
    /// 那么高一条不画东西的带子 —— 相邻两行的底色（选中/悬停那一层）因此裂开一条缝。
    ///
    /// NOTE: 条目比 `versionTreeRowHeight()` 还矮的时候这一块就是整条条目（不让成负数），
    /// 所以任何矩形进来都算得出东西来（验证工程里那几个手摆的矩形就是走这一档）。
    static QRect contentRect(const QRect &itemRect);

    /// 行尾那个方框：**上下两格的可见轮廓**。宽 `versionTreeArrowSize() + 6`、高是它的两倍，
    /// 贴着行右边（右边留出版面那一圈 `versionTreeRowPadding()`），竖着在画出来的那一块里
    /// 居中。
    static QRect moveBoxRect(const QRect &itemRect);

    /// 方框的**上半 / 下半** = 两颗箭头的命中格（各占一格，合起来正好把方框铺满、不重不漏）。
    /// 上面那颗是上移、下面那颗是下移。
    ///
    /// NOTE: 命中格比画出来的三角大一圈（四周各留 3 px），和实例树那颗齿轮、版本树那个
    /// 「取消」小叉同一个做法：12 px 的点击目标太小了。两格**中间不留缝**：每颗自己
    /// 已经带了 3 px 余量，中间再有缝就会空出一格"点下去什么都没发生"的地方，而那一格
    /// 正是最容易手抖点到的。
    static QRect moveUpRect(const QRect &itemRect);
    static QRect moveDownRect(const QRect &itemRect);

    /// 那两颗箭头**画出来**的那一圈（命中格那 3 px 是手感余量，不画；格子小的时候跟着缩，
    /// 不留半颗在格子外 —— 和 InstanceGroupDelegate::gearGlyphRect() 同一条）。
    static QRect moveUpGlyphRect(const QRect &itemRect);
    static QRect moveDownGlyphRect(const QRect &itemRect);

    /// 这一行此刻**画不画**上面/下面那一颗。第一行没有上移、最后一行没有下移
    /// （挪不动的那一侧不画，**不是**画成灰的：一颗都没有的行一眼就说明它在顺序里到头了）。
    ///
    /// NOTE: 它同时是"这一格算不算按钮"的判据（GroupDialog::eventFilter() 读的就是它）
    /// —— 画和点必须是同一个判据，不然会出现"看着没箭头、点下去却挪了"。
    static bool canMoveUp(const QModelIndex &index);
    static bool canMoveDown(const QModelIndex &index);

    /// 这一行此刻**画不画那个方框**：两颗都挪不动的行（表里只剩一条分组）整块不画 ——
    /// 一个空框看着像坏了。**框和里面的三角是同一个判据**：认下那一下的 eventFilter()
    /// 也先问这一句，不然会出现"看着什么都没有、点下去却吃了一发"。
    static bool showsMoveBox(const QModelIndex &index);

    /// 光标落在哪一颗箭头上就弹哪一句话，没落在就是空串。
    /// **带上 Shift 那半句** —— 那是个藏在键盘上的快捷方式，不说没人知道。
    static QString rowActionTip(const QModelIndex &index, const QRect &itemRect, const QPoint &pos);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    /// 悬停提示。基类那个只看 `Qt::ToolTipRole`（整条行一句），这里先问一声 rowActionTip()，
    /// 问不到才交给基类。
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

private:
    /// 一个命中格 → 里面那颗三角**画出来**的那一圈（对称往回收，见上面那两条）。
    static QRect glyphRect(const QRect &cell);
};
