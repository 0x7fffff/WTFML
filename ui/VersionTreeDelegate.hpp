/**
 * @file VersionTreeDelegate.hpp
 * @brief 树上每一行长什么样：前面一个小图标 + 正文 + 下面那行小字 + 最右边的 >/V。
 * @author 0x7fffff
 * @date 2026-09-22
 * @note 为什么是代理（delegate）而不是"给每个条目塞一个自定义控件"：
 *       树里是 900 多条（正式版 103 + 快照 700+ + …），`setItemWidget` 那条路要给
 *       每一条都建一个真的 QWidget，光建起来就够卡；代理是**画**出来的，条目还是
 *       QTreeWidgetItem，`UserRole` 里那个身份、展开状态、点击那条路全都原样不变。
 *
 *       这一层只管"画"：要知道的都在条目上（`DisplayRole` = 正文、`rowSubTextRole()` =
 *       第二行小字、`rowIconRole()` = 前面那个图标），一个业务判断都不做。
 *
 *       2026-09-23 起**两棵树共用它**（版本树 + 安装页那棵加载器树，用户要的"和上一页
 *       相同的模式"）：条目给了 `rowIconRole()` 就画它（没给时"版本号前面是草方块"
 *       那条老规矩照旧），第二行小字两种行**都**看 `rowSubTextRole()`（空串就不画）。
 *       两棵树只有**一处**不一样：叶子那一层右边那个箭头画不画（见构造函数那个开关）。
 *
 *       2026-09-23 又多了第三处用户：下载页那四张资源表（模组/整合包/资源包/光影）。
 *       它和上面两棵树的形状**不一样**（三层：资源 → 游戏版本 → 版本号），于是代理多了
 *       一个新开关：**图标多大**（见构造函数第三个参数）。上面两棵树不传，取到的还是原来
 *       那个 18 px，画出来的像素一个都不变。
 */

#pragma once

#include <QStyledItemDelegate>

class QPainter;

/// 版本树那棵树上挂的代理。
class VersionTreeDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    /// @param parent 视图（QTreeWidget / QTreeView）。
    /// @param leafArrow **叶子那一层**右边画不画那个箭头（顶层那几条分类/加载器的箭头
    ///        不受它管 —— 那些是真的能展开，照画 `>` / V）。
    ///        · 版本树：`true` —— 叶子是**版本号**，点它进安装页，那个 `>` 就是"点进去"；
    ///        · 安装页那棵加载器树：`false` —— 叶子是那两档和具体版本号，点它们是**选中**、
    ///          底下没有下一页，右边挂个 `>` 等于说"点进去还有东西"（用户 2026-09-23
    ///          原话："模组加载器的具体版本右侧就不用 > 了（毕竟这个层次没有下拉框了）"）。
    /// @param iconSize 前面那个图标画多大（像素，正方形）。**0 = 按老规矩**
    ///        （`MainWindowUI::versionTreeIconSize()`，18 px）—— 版本树和加载器树都用这个
    ///        默认值，所以传 0 和以前完全一样。下载页那几张表要的是"能看清的缩略图"
    ///        （用户原话「会有图片啥的效仿 pcl 的布局放着」），会显式传一个大一点的数。
    explicit VersionTreeDelegate(QObject *parent = nullptr, bool leafArrow = true,
                                 int iconSize = 0);

    /// 叶子那一层的箭头画不画（见构造函数）。验证工程用它钉住**产品那边真的传了 false**：
    /// 只验"传 false 就不画箭头"是不够的，那条路自己也能通过。
    bool leafArrow() const { return m_leafArrow; }

    /// 这一行前面那个图标实际画多大（0 表示"用默认那个常数"，见构造函数）。
    int iconSize() const { return m_iconSize; }

    /// 这一行该按多大的图标留位置（**唯一出处**：paint 和 sizeHint 都读它，不然"画出来了"
    /// 和"留了几格"会各算各的，症状是图标压着文字或者右边多出一块空）。
    int rowIconSize() const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /// 行尾那个「取消」小叉**占哪一格**（`row` 是整行的矩形，视口坐标）。只有条目上
    /// `rowClearRole()` 为真的行才画它、也只有那种行才有意义。
    ///
    /// NOTE: 这是**唯一出处** —— paint() 按它画、MainWindow 那边按它判断"这一下点中的
    ///       是不是那个叉"（见 MainWindow::eventFilter）。两边各算一份的话迟早错开，
    ///       而错开的形状是"看着点中了叉、其实点的是那一行"（那一行点一下是展开/收起，
    ///       于是用户以为取消了、其实只是把那一列收了起来）。
    static QRect clearRect(const QRect &row);

    /// 行尾那个**刷新按钮**占哪一格（`row` 是整行的矩形，视口坐标）。只有条目上
    /// `rowRefreshRole()` 为真的行才画它、也只有那种行才有意义。
    ///
    /// 现在是下载页左栏那五个分类条目在用（用户原话："每一项的右边放一个刷新按钮"）。
    /// 和 clearRect() 一样，**这一格也是唯一出处** —— paint() 按它画、MainWindow 那边
    /// 按它判断"这一下点中的是不是那个按钮"（见 MainWindow::eventFilter）。
    ///
    /// NOTE: 定位是**贴着行右边缘**（不像 clearRect 那样挂在箭头左边）—— 用这个角色的
    ///       行（分类条目）本来就没有孩子、代理不给它画箭头，那一格是空的。
    ///       代价是它和 clearRect()（那个「取消」小叉，挂在箭头左边）**占的是同一块地方**：
    ///       一行同时挂这两个角色的话，两个字形会叠在一起。今天界面上没有任何一行同时挂着，
    ///       ui_check 里有一条断言把这条互斥钉住（真要有那么一行，那里先红）。
    static QRect refreshRect(const QRect &row);

private:
    bool m_leafArrow = true;
    int m_iconSize = 0;
};
