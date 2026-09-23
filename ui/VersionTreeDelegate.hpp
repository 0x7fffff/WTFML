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
    explicit VersionTreeDelegate(QObject *parent = nullptr, bool leafArrow = true);

    /// 叶子那一层的箭头画不画（见构造函数）。验证工程用它钉住**产品那边真的传了 false**：
    /// 只验"传 false 就不画箭头"是不够的，那条路自己也能通过。
    bool leafArrow() const { return m_leafArrow; }

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

private:
    bool m_leafArrow = true;
};
