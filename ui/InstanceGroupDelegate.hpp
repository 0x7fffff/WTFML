/**
 * @file InstanceGroupDelegate.hpp
 * @brief 实例页那张树里**分组头**那一行长什么样：粗体组名 +（右边）一颗齿轮 + 一个箭头。
 * @author 0x7fffff
 * @date 2026-09-25
 * @note 【为什么单独一个代理，不复用 VersionTreeDelegate】
 *
 *       VersionTreeDelegate 是"两行字 + 前面一个图标 + 右边一个箭头（带两个可选装饰）"那
 *       一族的画法，它的判据里"顶层 = 分类"（粗体、无图标、画箭头）是**按那四棵树**立的。
 *       而实例页这张树上顶层有两种东西：有分组时顶层是**组头**、没分组时顶层是**实例**
 *       （平铺，见 fillInstanceTree）—— 照那套判据画，平铺那一档每个实例都会变成粗体
 *       加箭头。所以这一层反过来判：**只有挂着 groupIdRole() 的行**才按"组行"画，
 *       其余（实例那几行）原地交给基类，一个像素都不动。
 *
 *       这一层只管"画"：要知道的都在条目上（`DisplayRole` = 「组名 (N)」、
 *       `groupIdRole()` 说明它是组行、`rowGroupGearRole()` 说明这一行有齿轮）。
 *       业务判断（这一组里几个实例、能不能改名）不在这儿。
 *
 *       【行里的按钮：几何的唯一出处就是下面那两个 static】
 *
 *       和 VersionTreeDelegate 的 clearRect()/refreshRect() 一个规矩：代理按它画、
 *       `MainWindow::eventFilter` 按它认下那一下、`helpEvent()` 按它决定弹不弹话。
 *       三处各算一份的话，错开的形状是"看着点中了齿轮、其实点的是那一行"（那一行点一下
 *       是展开/收起）。
 */

#pragma once

#include <QStyledItemDelegate>

class QPainter;

/// 实例页那张树上挂的代理：只接管分组头那几行，实例行原样走基类。
class InstanceGroupDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit InstanceGroupDelegate(QObject *parent = nullptr);

    /// 行尾那颗**齿轮**的**命中格**占哪一格（`row` 是整行的矩形，视口坐标）。只有条目上
    /// `rowGroupGearRole()` 为真的行才画它、也只有那种行才有意义。
    ///
    /// NOTE: 形状和 `VersionTreeDelegate::clearRect()`（那个「取消」小叉）**逐字一样**：
    ///       命中格比画出来的字形大一圈（四周各留 3 px），位置是"紧挨着右边那个箭头
    ///       左边、中间留 6 px"。两处的差别只有边长来源（那边是 `versionTreeArrowSize()`，
    ///       这边是 `instanceGroupGearSize()`）。
    /// NOTE: 这一格**允许比一行高**（实例树的行高就是那一行字那么高）：它是"点得到的那一
    ///       圈"，不是画出来的那一圈。冒出去的那几点点不到别的东西 —— 认下那一下的是
    ///       `MainWindow::eventFilter`，而它先要求 `itemAt()` 落在这条行里。
    static QRect gearRect(const QRect &row);

    /// 那颗齿轮**画出来**的那一圈（比 gearRect() 小：那个是命中格，四周各留 3 px 余量）。
    ///
    /// NOTE: 它一定装在行高里面 —— 边长取 `min(instanceGroupGearSize(), 行高)`。实例树没有
    ///       像版本树那样钉一个行高常数（就是一行字那么高，十几像素），16 px 的齿轮不缩的话
    ///       会上下各冒出去一半：整屏重绘时压到上/下相邻那两条上，局部重绘时被切成半颗。
    static QRect gearGlyphRect(const QRect &row);

    /// 最右边那个**箭头**占哪一格（收起时画 `>`、展开时画朝下的 V）。分组头一定画它 ——
    /// "点一下打开/关闭"就是这个箭头在说的那件事。**它同时也是画出来的那一圈**（12 px，
    /// 竖着恒在行内，理由见 .cpp）。
    static QRect chevronRect(const QRect &row);

    /// 光标落在**行里那颗齿轮**上就说那句话（`rowGroupGearTipText()`），没落在就是空串。
    /// 和 VersionTreeDelegate::rowActionTip() 一个路数（那儿是上一族的第二处几何读者）。
    static QString rowActionTip(const QModelIndex &index, const QRect &row, const QPoint &pos);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /// 悬停提示。基类那个只看 `Qt::ToolTipRole`（整条行一句），这里先问一声 rowActionTip()，
    /// 问不到才交给基类。
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;
};
