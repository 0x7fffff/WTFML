#include "VersionTreeDelegate.hpp"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QTreeView>

#include "MainWindowUI.hpp"
#include "VersionIcons.hpp"

VersionTreeDelegate::VersionTreeDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void VersionTreeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    // 背景/选中/悬停那一层交给样式画，但**文字和图标先清掉** —— 这一行是"两行字 + 一个小图标"，
    // 样式按"一行文字一个图标"摆出来的那个位置不能用（版本号会在行正中间，下面那行小字
    // 根本没地方放）。features 清空之后 CE_ItemViewItem 就只画背景那一层了。
    opt.text.clear();
    opt.icon = QIcon();
    opt.features = QStyleOptionViewItem::None;
    if (QStyle *style = opt.widget != nullptr ? opt.widget->style() : QApplication::style()) {
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    }

    const QRect row = option.rect;
    const QPalette &palette = option.palette;
    // 顶层那四条是**分类**（一行字 + 右边那个能转的箭头），底下的子条目是**版本号**
    // （草方块 + 版本号 + 发布时间）。判据就是"有没有爹"。
    const bool isChannel = !index.parent().isValid();
    const auto *view = qobject_cast<const QTreeView *>(option.widget);
    const bool expanded = view != nullptr && view->isExpanded(index);
    const bool hasChildren =
        view != nullptr && view->model() != nullptr && view->model()->rowCount(index) > 0;

    const int pad = MainWindowUI::versionTreeRowPadding();
    const int arrowSize = MainWindowUI::versionTreeArrowSize();
    const int iconSize = MainWindowUI::versionTreeIconSize();

    int textLeft = row.left() + pad;
    QRect iconRect;
    if (!isChannel) {
        iconRect = QRect(row.left() + pad, row.center().y() - iconSize / 2, iconSize, iconSize);
        textLeft = iconRect.right() + 1 + 8;
    }
    // 右边那个箭头自己占一格，文字不许压过去。
    const int textRight = row.right() - pad - arrowSize - 6;
    const QRect textRect(textLeft, row.top(), qMax(0, textRight - textLeft), row.height());
    const QString mainText = index.data(Qt::DisplayRole).toString();

    painter->save();
    if (isChannel) {
        QFont font = option.font;
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(palette.color(QPalette::WindowText));
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, mainText);
    } else {
        const QString date = index.data(MainWindowUI::versionTreeDateRole()).toString();
        QFont dateFont = option.font;
        dateFont.setPointSize(
            qMax(6, option.font.pointSize() + MainWindowUI::versionDateFontDelta()));
        const QFontMetrics mainMetrics(option.font);
        const QFontMetrics dateMetrics(dateFont);
        // 两行合成一块，整块在行里居中（行高是常数，所以"上面一行字下面一行字"看着才稳）。
        const int gap = 2;
        const int blockHeight = mainMetrics.height() + (date.isEmpty() ? 0 : gap + dateMetrics.height());
        int y = row.center().y() - blockHeight / 2;
        painter->setFont(option.font);
        painter->setPen(palette.color(QPalette::WindowText));
        painter->drawText(QRect(textRect.left(), y, textRect.width(), mainMetrics.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, mainText);
        y += mainMetrics.height() + gap;
        if (!date.isEmpty()) {
            painter->setFont(dateFont);
            painter->setPen(palette.color(QPalette::PlaceholderText));
            painter->drawText(QRect(textRect.left(), y, textRect.width(), dateMetrics.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, date);
        }
        painter->drawPixmap(iconRect.topLeft(), VersionIcons::grassBlock(iconSize));
    }
    painter->restore();

    // 最右边那个 >：分类点上它会展开、展开之后变成朝下的 V；版本号那些是**叶子**，
    // 一直朝右（"点进去"那层意思）。一个版本都没有的分类**连箭头都不画** ——
    // 画了就骗人说点得开。
    if (isChannel && !hasChildren) {
        return;
    }
    const QColor arrowColor = isChannel ? palette.color(QPalette::WindowText)
                                        : palette.color(QPalette::PlaceholderText);
    const QRect arrowRect(row.right() - pad - arrowSize, row.center().y() - arrowSize / 2,
                          arrowSize, arrowSize);
    painter->drawPixmap(arrowRect.topLeft(),
                        VersionIcons::chevron(isChannel && expanded, arrowSize, arrowColor));
}

QSize VersionTreeDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const {
    // 行高是个**常数**，不走"按内容算"：树上开着 uniformRowHeights（900 多条，让它按
    // 第一行算一次就够），而第一行是分类（一行字）、底下是版本号（两行字）——
    // 两边高度不一样的话，两行那种会被按一行的行高裁掉半行。
    const int height = MainWindowUI::versionTreeRowHeight();
    const QFontMetrics metrics(option.font);
    int width = metrics.horizontalAdvance(index.data(Qt::DisplayRole).toString());
    if (index.parent().isValid()) {
        // 版本号那一行还要放得下下面那行小字和前面那个草方块。
        QFont dateFont = option.font;
        dateFont.setPointSize(
            qMax(6, option.font.pointSize() + MainWindowUI::versionDateFontDelta()));
        width = qMax(width, QFontMetrics(dateFont).horizontalAdvance(
                                index.data(MainWindowUI::versionTreeDateRole()).toString()));
        width += MainWindowUI::versionTreeIconSize() + 8;
    }
    width += 2 * MainWindowUI::versionTreeRowPadding() + MainWindowUI::versionTreeArrowSize() + 6;
    return QSize(width, height);
}
