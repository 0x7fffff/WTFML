#include "VersionTreeDelegate.hpp"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QTreeView>

#include "MainWindowUI.hpp"
#include "VersionIcons.hpp"

VersionTreeDelegate::VersionTreeDelegate(QObject *parent, bool leafArrow)
    : QStyledItemDelegate(parent), m_leafArrow(leafArrow) {}

QRect VersionTreeDelegate::clearRect(const QRect &row) {
    const int pad = MainWindowUI::versionTreeRowPadding();
    const int arrowSize = MainWindowUI::versionTreeArrowSize();
    // 命中格比画出来的那个叉大一圈（x 的边长是 arrowSize，格子里四周各留 3 px）——
    // 12 px 的点击目标太小了，手一抖就点到旁边的箭头或者那一行上。
    const int box = arrowSize + 6;
    // 它紧挨着箭头左边，中间留 6 px（"文字和箭头之间"也是这个数，看着才是一套的）。
    const int left = row.right() - pad - arrowSize - 6 - box;
    return QRect(left, row.center().y() - box / 2, box, box);
}

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
    // 不可用的那几条（安装页上"选中了一个加载器之后其余几条"）**整行淡下去**：这一行
    // 的字和图标都是这儿自己画的，不处理的话"点得动"和"点了没反应"在屏幕上一模一样
    // —— 2026-09-23 用户报的"点不开了"正是这个形状，别再制造一次。
    //
    // 判据用 index.flags() 而不是 option.state：Qt 到底有没有替我们把 State_Enabled
    // 填进去要看版本和样式，而**这一位是我们自己 setDisabled() 写下去的**，
    // 读它就是读我们写的那一位。禁用是整行的事（图标、两行字、右边的箭头一起淡），
    // 所以按透明度来，而不是挨个换调色板里那一档颜色。
    const bool rowEnabled = (index.flags() & Qt::ItemIsEnabled) != 0;
    // 0.5：一眼看得出"这条现在点不得"，但字还读得清（那句话本身就是信息 ——
    // 「与 Forge 不兼容」正是给用户看的）。下面两处画东西的地方各用一次（第二处是
    // 右边的箭头，它画在 save/restore 之外）。
    const qreal rowOpacity = rowEnabled ? 1.0 : 0.5;
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

    // 前面那个小图标：条目自己给了 `Qt::DecorationRole` 就画它（加载器那棵树五条都是
    // 这么给的 —— 各自那个标志），没给就按版本树那条老规矩来（**版本号**前面是草方块、
    // 分类前面什么都没有）。给成空的 QIcon 等于没给。
    QPixmap mark;
    const QVariant decoration = index.data(MainWindowUI::rowIconRole());
    if (decoration.canConvert<QIcon>()) {
        mark = qvariant_cast<QIcon>(decoration).pixmap(iconSize, iconSize);
    } else if (decoration.canConvert<QPixmap>()) {
        mark = qvariant_cast<QPixmap>(decoration).scaled(iconSize, iconSize, Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation);
    }
    if (mark.isNull() && !isChannel) {
        mark = VersionIcons::grassBlock(iconSize);
    }

    int textLeft = row.left() + pad;
    QRect iconRect;
    if (!mark.isNull()) {
        iconRect = QRect(row.left() + pad, row.center().y() - iconSize / 2, iconSize, iconSize);
        textLeft = iconRect.right() + 1 + 8;
    }
    // 右边那个箭头自己占一格，文字不许压过去；选中的那条加载器右边还多一个「取消」小叉
    // （rowClearRole()，见下），那一格也要从文字的可用宽度里扣掉。
    const bool showClear = index.data(MainWindowUI::rowClearRole()).toBool();
    const QRect crossBox = clearRect(row);
    int textRight = row.right() - pad - arrowSize - 6;
    if (showClear) {
        textRight -= crossBox.width() + 6;
    }
    const QRect textRect(textLeft, row.top(), qMax(0, textRight - textLeft), row.height());
    const QString mainText = index.data(Qt::DisplayRole).toString();

    painter->save();
    painter->setOpacity(rowOpacity);
    // 第二行小字（rowSubTextRole）：版本树上是发布时间，加载器那棵树上是「与 X 不兼容」
    // 或者选中的版本号。两种行**都**看这个角色 —— 分类以前不画第二行只是因为没人给它
    // 塞过值（空串就是不画）。
    const QString subText = index.data(MainWindowUI::rowSubTextRole()).toString();
    QFont subFont = option.font;
    subFont.setPointSize(qMax(6, option.font.pointSize() + MainWindowUI::versionDateFontDelta()));
    const QFontMetrics mainMetrics(option.font);
    const QFontMetrics subMetrics(subFont);
    // 两行合成一块，整块在行里居中（行高是常数，所以"上面一行字下面一行字"看着才稳）。
    const int gap = 2;
    const int blockHeight =
        mainMetrics.height() + (subText.isEmpty() ? 0 : gap + subMetrics.height());
    int y = row.center().y() - blockHeight / 2;

    QFont mainFont = option.font;
    if (isChannel) {
        // 顶层那几条（分类 / 加载器）是**粗体**：它们和底下的版本号不是一回事。
        mainFont.setBold(true);
    }
    painter->setFont(mainFont);
    painter->setPen(palette.color(QPalette::WindowText));
    painter->drawText(QRect(textRect.left(), y, textRect.width(), mainMetrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter, mainText);
    y += mainMetrics.height() + gap;
    if (!subText.isEmpty()) {
        painter->setFont(subFont);
        painter->setPen(palette.color(QPalette::PlaceholderText));
        painter->drawText(QRect(textRect.left(), y, textRect.width(), subMetrics.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, subText);
    }
    if (!mark.isNull()) {
        painter->drawPixmap(iconRect.topLeft(), mark);
    }
    painter->restore();

    // 最右边那个 >：分类点上它会展开、展开之后变成朝下的 V；版本号那些是**叶子**，
    // 一直朝右（"点进去"那层意思）。一个版本都没有的分类**连箭头都不画** ——
    // 画了就骗人说点得开。
    if (isChannel && !hasChildren) {
        return;
    }
    // 叶子那一层的箭头：版本树上是"点进去"（点一个版本号进安装页），安装页那棵加载器树上
    // **不画** —— 那一层没有下一页，两档和具体版本号都是"选中它"（见构造函数的 @param）。
    // 注意这一位只管**那一笔箭头**：同一个 save/restore 里还画着那个「取消」小叉，两个
    // 功能各管各的（把整个块一起 return 掉的话，"叶子不画箭头"会顺手把叉也吃掉 ——
    // ui_check 里恰好有一条拿叶子行验叉的断言，一跑就红）。
    const bool drawArrow = isChannel || m_leafArrow;
    const QColor arrowColor = isChannel ? palette.color(QPalette::WindowText)
                                        : palette.color(QPalette::PlaceholderText);
    const QRect arrowRect(row.right() - pad - arrowSize, row.center().y() - arrowSize / 2,
                          arrowSize, arrowSize);
    painter->save();
    painter->setOpacity(rowOpacity);
    // 选中的那条加载器：箭头**左边**再画一个「取消」小叉（点它 = 不装这个加载器了，
    // 退回纯原版 —— 用户原话："选中以后在那个 > 左侧加一个 x 来取消安装对应版本"）。
    // 它比正文浅一档：那是个次要操作，别和版本号抢眼睛。
    if (showClear) {
        const QRect glyph = crossBox.adjusted(3, 3, -3, -3);
        painter->drawPixmap(glyph.topLeft(),
                            VersionIcons::cross(glyph.width(),
                                                palette.color(QPalette::PlaceholderText)));
    }
    if (drawArrow) {
        painter->drawPixmap(arrowRect.topLeft(),
                            VersionIcons::chevron(isChannel && expanded, arrowSize, arrowColor));
    }
    painter->restore();
}

QSize VersionTreeDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const {
    // 行高是个**常数**，不走"按内容算"：树上开着 uniformRowHeights（900 多条，让它按
    // 第一行算一次就够），而第一行是分类（一行字）、底下是版本号（两行字）——
    // 两边高度不一样的话，两行那种会被按一行的行高裁掉半行。
    const int height = MainWindowUI::versionTreeRowHeight();
    // 顶层那几条是粗体画的，宽度也得按粗体量（不然"加载器名 + 右边那个箭头"会差几个像素）。
    QFont mainFont = option.font;
    if (!index.parent().isValid()) {
        mainFont.setBold(true);
    }
    int width = QFontMetrics(mainFont).horizontalAdvance(index.data(Qt::DisplayRole).toString());
    // 第二行小字：两棵树都可能有（版本树上是发布时间，加载器那棵树上是"与 X 不兼容"
    // 或者选中的版本号），有就按它量。
    QFont subFont = option.font;
    subFont.setPointSize(qMax(6, option.font.pointSize() + MainWindowUI::versionDateFontDelta()));
    width = qMax(width, QFontMetrics(subFont).horizontalAdvance(
                            index.data(MainWindowUI::rowSubTextRole()).toString()));
    // 前面那个图标的宽度：子条目一定有（草方块或条目自己给的），顶层条目**给了才有**。
    const QVariant decoration = index.data(MainWindowUI::rowIconRole());
    if (index.parent().isValid() || decoration.canConvert<QIcon>()) {
        width += MainWindowUI::versionTreeIconSize() + 8;
    }
    width += 2 * MainWindowUI::versionTreeRowPadding() + MainWindowUI::versionTreeArrowSize() + 6;
    // 行尾那个「取消」小叉也占宽度（只有选中的那条加载器有，见 rowClearRole()）。
    if (index.data(MainWindowUI::rowClearRole()).toBool()) {
        width += MainWindowUI::versionTreeArrowSize() + 6 + 6;
    }
    return QSize(width, height);
}
