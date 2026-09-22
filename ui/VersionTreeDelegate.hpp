/**
 * @file VersionTreeDelegate.hpp
 * @brief 版本树里每一行长什么样：草方块图标 + 版本号 + 下面那行发布时间 + 最右边的 >/V。
 * @author 0x7fffff
 * @date 2026-09-22
 * @note 为什么是代理（delegate）而不是"给每个条目塞一个自定义控件"：
 *       树里是 900 多条（正式版 103 + 快照 700+ + …），`setItemWidget` 那条路要给
 *       每一条都建一个真的 QWidget，光建起来就够卡；代理是**画**出来的，条目还是
 *       QTreeWidgetItem，`UserRole` 里那个身份、展开状态、点击那条路全都原样不变。
 *
 *       这一层只管"画"：要知道的都在条目上（`DisplayRole` = 版本号/分类名、
 *       `versionTreeDateRole()` = 发布时间），一个业务判断都不做。
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
