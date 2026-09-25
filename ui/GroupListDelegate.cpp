/**
 * @file GroupListDelegate.cpp
 * @brief Implements the group-settings list row painter.
 * @author 0x7fffff
 * @date 2026-09-25
 * @note 形状：一行字（左）+ 行尾两颗实心三角（右）。能不能挪是**算出来的**（见 .hpp），
 *       所以这一层除了几何什么都不用从条目上读。
 */

#include "GroupListDelegate.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QHelpEvent>
#include <QModelIndex>
#include <QPainter>
#include <QStyle>
#include <QToolTip>

#include "MainWindowUI.hpp"
#include "VersionIcons.hpp"

GroupListDelegate::GroupListDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QRect GroupListDelegate::moveDownRect(const QRect &row) {
    const int box = MainWindowUI::versionTreeArrowSize() + 6;
    // 贴着行右边缘（右边留出版面那一圈），下移那一颗在最外面 —— 上移在左、下移在右，
    // 和实例树上那两颗的次序一样（换了个宿主，手势不该跟着变）。
    return QRect(row.right() - MainWindowUI::versionTreeRowPadding() - box,
                 row.top() + (row.height() - box) / 2, box, box);
}

QRect GroupListDelegate::moveUpRect(const QRect &row) {
    const QRect down = moveDownRect(row);
    // 紧挨着下移那颗（中间不留缝），理由见 .hpp 那段。
    return QRect(down.left() - down.width(), down.top(), down.width(), down.height());
}

QRect GroupListDelegate::moveUpGlyphRect(const QRect &row) {
    const QRect box = moveUpRect(row);
    // 画出来的那一圈按行高收一收（和 InstanceGroupDelegate::*GlyphRect() 同一个式子）：
    // 命中格可以比一行高，画出来的不行 —— 冒出去的那半颗会压到相邻那一行上。
    const int size = qMin(MainWindowUI::versionTreeArrowSize(), row.height());
    const int inset = (box.width() - size) / 2;
    return box.adjusted(inset, inset, -inset, -inset);
}

QRect GroupListDelegate::moveDownGlyphRect(const QRect &row) {
    const QRect box = moveDownRect(row);
    const int size = qMin(MainWindowUI::versionTreeArrowSize(), row.height());
    const int inset = (box.width() - size) / 2;
    return box.adjusted(inset, inset, -inset, -inset);
}

bool GroupListDelegate::canMoveUp(const QModelIndex &index) {
    return index.isValid() && index.row() > 0;
}

bool GroupListDelegate::canMoveDown(const QModelIndex &index) {
    if (!index.isValid()) {
        return false;
    }
    const QAbstractItemModel *model = index.model();
    // 行序就是组序（这张表里只有分组），所以"下面还有没有一行"就是全部判据。
    return model != nullptr && index.row() < model->rowCount(index.parent()) - 1;
}

QString GroupListDelegate::rowActionTip(const QModelIndex &index, const QRect &row,
                                       const QPoint &pos) {
    // 先判上移、再判下移（和画它们、认它们那一格同一个顺序）。今天两格互不重叠，
    // 顺序无所谓；将来真叠上了，先判的那个赢（和 VersionTreeDelegate::rowActionTip() 同一条）。
    if (canMoveUp(index) && moveUpRect(row).contains(pos)) {
        return QStringLiteral("上移这一组（按住 Shift 直接放到最上面）");
    }
    if (canMoveDown(index) && moveDownRect(row).contains(pos)) {
        return QStringLiteral("下移这一组（按住 Shift 直接放到最下面）");
    }
    return QString();
}

bool GroupListDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                                  const QStyleOptionViewItem &option, const QModelIndex &index) {
    if (event != nullptr && event->type() == QEvent::ToolTip) {
        const QString tip = rowActionTip(index, option.rect, event->pos());
        if (!tip.isEmpty()) {
            QToolTip::showText(event->globalPos(), tip, view);
            return true;
        }
    }
    // 兜底：条目自己写 `Qt::ToolTipRole` 的老路（基类就干这个）。
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

void GroupListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    const QString text = opt.text;
    // 背景/选中/悬停那一层交给样式画，但**文字和图标先清掉** —— 位置由下面自己摆
    // （右边要给那两颗箭头让出两格，长组名不能从箭头底下穿过去）。
    opt.text.clear();
    opt.icon = QIcon();
    if (QStyle *style = opt.widget != nullptr ? opt.widget->style() : QApplication::style()) {
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    }

    const QRect row = option.rect;
    const QPalette &palette = option.palette;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const int pad = MainWindowUI::versionTreeRowPadding();
    // 右边那两格**无条件**扣掉（不管这一行此刻画几颗）：只扣一颗的话，第一行和别的行
    // 的文字右边界就不一样了 —— 组名一长，看着就是每行各自截断在不同的地方。
    const int textLeft = row.left() + pad;
    const int textRight = moveUpRect(row).left() - 6;
    const QRect textRect(textLeft, row.top(), qMax(0, textRight - textLeft), row.height());

    // 字色跟基类画的时候一样：没选中是正文色、选中是高亮前景色。**不能一直用正文色** ——
    // 这一行没换代理之前是基类画的，选中那一档一直是样式挑的那个高亮前景色（这张表没有
    // 自己的 QSS，选中底色是 Fusion 那个蓝，深色字压上去就看不清了）。
    const QColor ink = palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    painter->save();
    painter->setPen(ink);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                      option.fontMetrics.elidedText(text, Qt::ElideRight, textRect.width()));

    // 行尾那两颗实心三角：**只有这一行真挪得动的那一侧才画**（不是画成灰的）——
    // 判据和 eventFilter() 认那一下读的是同一对函数，画和点永远不会错开。
    //
    // NOTE: 选中那一行改用高亮前景色：这两个三角用的是次要那一档灰（PlaceholderText），
    //       压在选中底色上基本看不见 —— 用户会以为"箭头没了"。字色那一档怎么走，它就
    //       怎么走（两档之外不引入第三种颜色）。
    const QColor arrowInk =
        palette.color(selected ? QPalette::HighlightedText : QPalette::PlaceholderText);
    const int size = MainWindowUI::versionTreeArrowSize();
    if (canMoveUp(index)) {
        const QRect glyph = moveUpGlyphRect(row);
        painter->drawPixmap(glyph.topLeft(),
                            VersionIcons::triangleUp(qMin(size, glyph.width()), arrowInk));
    }
    if (canMoveDown(index)) {
        const QRect glyph = moveDownGlyphRect(row);
        painter->drawPixmap(glyph.topLeft(),
                            VersionIcons::triangleDown(qMin(size, glyph.width()), arrowInk));
    }
    painter->restore();
}
