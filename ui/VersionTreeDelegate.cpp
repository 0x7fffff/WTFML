#include "VersionTreeDelegate.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QPainter>
#include <QStyle>
#include <QToolTip>
#include <QTreeView>

#include "MainWindowUI.hpp"
#include "VersionIcons.hpp"

VersionTreeDelegate::VersionTreeDelegate(QObject *parent, bool leafArrow, int iconSize)
    : QStyledItemDelegate(parent), m_leafArrow(leafArrow), m_iconSize(iconSize) {}

int VersionTreeDelegate::rowIconSize() const {
    // 0 = 没传 = 按老规矩。两棵老树走的就是这一条，取到的值和改动前写死的那个常数一样。
    return m_iconSize > 0 ? m_iconSize : MainWindowUI::versionTreeIconSize();
}

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

QRect VersionTreeDelegate::refreshRect(const QRect &row) {
    const int pad = MainWindowUI::versionTreeRowPadding();
    // 画出来的那个圈和**正文前面的图标一样大**（versionTreeIconSize，18 px），命中格比它
    // 大一圈（四周各留 3 px，和 clearRect 同一个做法）—— 18 px 的点击目标太小了，
    // 手一抖就点到那一行上（而那一下是"换分类"）。
    const int box = MainWindowUI::versionTreeIconSize() + 6;
    // 贴着行右边缘。QRect 的 right() 是"最后一个像素的 x"，所以右边留出 pad 那几格之后，
    // 起点要 +1 才让这一格的右边缘正好落在 row.right() - pad 上。
    return QRect(row.right() - pad - box + 1, row.center().y() - box / 2, box, box);
}

QString VersionTreeDelegate::rowActionTip(const QModelIndex &index, const QRect &row,
                                          const QPoint &pos) {
    // 一条一条来：每加一个"行里的按钮"，在这儿加一行（几何仍然在 clearRect/refreshRect 里）。
    // 顺序就是"谁压在谁上面"的顺序 —— 今天两个角色互斥（一行不能同时挂），顺序无所谓，
    // 将来真叠上了，先判的那个赢。
    if (index.data(MainWindowUI::rowRefreshRole()).toBool() && refreshRect(row).contains(pos)) {
        return MainWindowUI::rowRefreshTipText();
    }
    if (index.data(MainWindowUI::rowClearRole()).toBool() && clearRect(row).contains(pos)) {
        return MainWindowUI::rowClearTipText();
    }
    return QString();
}

bool VersionTreeDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                                    const QStyleOptionViewItem &option, const QModelIndex &index) {
    // 只在"光标真落在行里那个按钮那一格上"时说话，别的地方一句都不弹（包括那一行的正文）。
    if (event != nullptr && event->type() == QEvent::ToolTip) {
        const QString tip = rowActionTip(index, option.rect, event->pos());
        if (!tip.isEmpty()) {
            QToolTip::showText(event->globalPos(), tip, view);
            return true;
        }
    }
    // 兜底：条目自己写 `Qt::ToolTipRole` 的那条老路（基类就干这个）。这条对"整条行一句"
    // 仍然有意义（比如将来资源表上"这一行是什么"），所以不能整个吃掉。
    return QStyledItemDelegate::helpEvent(event, view, option, index);
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
    const int iconSize = rowIconSize();

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
    // 行尾那个刷新按钮（rowRefreshRole()）：和上面那个叉一样，它占的那一格也要从文字的
    // 可用宽度里扣掉 —— 不扣的话长条目会从它底下穿过去。
    const bool showRefresh = index.data(MainWindowUI::rowRefreshRole()).toBool();
    const QRect refreshBox = refreshRect(row);
    int textRight = row.right() - pad - arrowSize - 6;
    if (showClear) {
        textRight -= crossBox.width() + 6;
    }
    if (showRefresh) {
        textRight -= refreshBox.width() + 6;
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
    // 行尾那个刷新按钮（见 refreshRect()）。画在**这个** save/restore 里（不是下面画箭头
    // 那一块）：下面那一块对"没有孩子的顶层"会整块 return 掉（分类条目正是那种行），
    // 画在那儿的话这一格根本轮不到。用次要那一档颜色 —— 它是行里的一个动作，不是正文。
    if (showRefresh) {
        // 画的比同格里其他字形大一号（20 而不是 18）：圆形的字形在同样标称尺寸下
        // 看着比方的那个小一圈（正文前面那个草方块就是方的），而这一格是纯图标、
        // 四周还有 2 px 余量，撑不满反倒显得"这一格是空的"。
        const QRect glyph = refreshBox.adjusted(2, 2, -2, -2);
        painter->drawPixmap(glyph.topLeft(),
                            VersionIcons::refresh(glyph.width(),
                                                  palette.color(QPalette::PlaceholderText)));
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
    // 箭头颜色：**真能展开的**那几行用正文色（"这儿点得开"要说清楚），别的用浅一档的
    //（"点进去"那层意思，次要）。
    // NOTE: 判据里那个 `hasChildren` 是 2026-09-23 加的。加之前只看 `isChannel`（顶层），
    //       而下载页那张资源表里"游戏版本"那一层**不是顶层、但真的能展开**（点它才去问
    //       版本列表），照老判据会画成浅色。对原有的两棵树这是空的：它们的非顶层行全是
    //       叶子（没有孩子），取到的颜色和以前一模一样。
    const QColor arrowColor = (isChannel || hasChildren)
                                  ? palette.color(QPalette::WindowText)
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
        // 展开着就画朝下的 V，收着就画朝右的 >。
        // NOTE: 判据里那个 `expanded` 原来写的是 `isChannel && expanded` —— 那是"非顶层行
        //       不可能展开"这个假设的产物（原有的两棵树确实如此）。下载页那张资源表里
        //       "游戏版本"那一层就是非顶层、又要能展开的：照老判据展开之后箭头还朝右，
        //       和"收着"长得一样。改成只看 expanded 对原有的行是空的（叶子永远 expanded
        //       为假，取到的还是 >）。
        painter->drawPixmap(arrowRect.topLeft(),
                            VersionIcons::chevron(expanded, arrowSize, arrowColor));
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
    // 前面那个图标的宽度：子条目一定有（草方块或条目自己给的），顶层条目**真给了才有**。
    //
    // NOTE: 认形态必须和 paint() 那条路**一模一样**（2026-09-24 改的）：那边 QIcon 和
    //       QPixmap 两种都认，这里原来只认 QIcon —— 于是"给的是 QPixmap"的条目画得出
    //       图标、量宽度时却没算它，右边那个箭头（有「取消」小叉时还有小叉）压掉一截文字。
    //       产品代码目前全走 setIcon()（QIcon），所以先踩到的是验证代码。
    const QVariant decoration = index.data(MainWindowUI::rowIconRole());
    bool hasIcon = false;
    if (decoration.canConvert<QIcon>()) {
        hasIcon = !qvariant_cast<QIcon>(decoration).isNull();
    } else if (decoration.canConvert<QPixmap>()) {
        hasIcon = !qvariant_cast<QPixmap>(decoration).isNull();
    }
    if (index.parent().isValid() || hasIcon) {
        width += rowIconSize() + 8;
    }
    width += 2 * MainWindowUI::versionTreeRowPadding() + MainWindowUI::versionTreeArrowSize() + 6;
    // 行尾那个「取消」小叉也占宽度（只有选中的那条加载器有，见 rowClearRole()）。
    if (index.data(MainWindowUI::rowClearRole()).toBool()) {
        width += MainWindowUI::versionTreeArrowSize() + 6 + 6;
    }
    // 行尾那个刷新按钮同理（见 rowRefreshRole()/refreshRect()）：它比那个叉大一号
    //（跟着正文图标那个 18 px 走），间距还是 6。
    if (index.data(MainWindowUI::rowRefreshRole()).toBool()) {
        width += MainWindowUI::versionTreeIconSize() + 6 + 6;
    }
    return QSize(width, height);
}
