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
 */

#pragma once

#include <QStyledItemDelegate>

class QPainter;

/// 版本树那棵树上挂的代理。
class VersionTreeDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit VersionTreeDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
