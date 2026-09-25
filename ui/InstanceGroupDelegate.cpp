#include "InstanceGroupDelegate.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QHelpEvent>
#include <QPainter>
#include <QStyle>
#include <QToolTip>
#include <QTreeView>

#include "MainWindowUI.hpp"
#include "VersionIcons.hpp"

InstanceGroupDelegate::InstanceGroupDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QRect InstanceGroupDelegate::chevronRect(const QRect &row) {
    const int pad = MainWindowUI::versionTreeRowPadding();
    // 边长按这一行的高度收一收。实例树**没有**像版本树那样钉一个行高常数
    // （versionTreeRowHeight()），行高就是那一行字那么高 —— 一行只有十几像素的时候
    // 12 px 的箭头正好装下，再矮就得跟着缩（行够高时它还是 12，不会变大）。
    const int arrowSize = qMin(MainWindowUI::versionTreeArrowSize(), row.height());
    // 贴着行右边缘（和 VersionTreeDelegate 算那个箭头是同一个式子："右边留出 pad"）。
    //
    // NOTE: 竖直位置走 `top + (行高 - 边长) / 2`，**不用 `row.center().y()`** —— 后者走的是
    //       QRect::center()（= top + (h-1)/2），行高是**偶数**时分母那一半少一像素：行高
    //       和边长一样时（就是上面那一档）那个箭头会往上冒一个像素出去，整屏重绘压到上面
    //       那条行、局部重绘被切掉一条。这个式子在"边长 ≤ 行高"时恒在行内。
    return QRect(row.right() - pad - arrowSize, row.top() + (row.height() - arrowSize) / 2,
                 arrowSize, arrowSize);
}

QRect InstanceGroupDelegate::gearRect(const QRect &row) {
    // 命中格比画出来的那个齿轮大一圈（四周各留 3 px，和 clearRect/refreshRect 一个做法）——
    // 16 px 的点击目标太小了，手一抖就点到那一行上（而那一下是"展开/收起"）。
    const int box = MainWindowUI::instanceGroupGearSize() + 6;
    // 紧挨着箭头左边，中间留 6 px（"文字和箭头之间"也是这个数，看着才是一套的）。
    const int left = chevronRect(row).left() - 6 - box;
    // 竖直居中：和 chevronRect() 同一个式子（理由见那边）。**不按行高收** —— 这一格是
    // 命中格，比一行高是允许的（点得到的那一圈，不是画出来的那一圈，见 .hpp）。
    return QRect(left, row.top() + (row.height() - box) / 2, box, box);
}

QRect InstanceGroupDelegate::gearGlyphRect(const QRect &row) {
    const QRect box = gearRect(row);
    // 画出来的那一圈**必须装在行高里**（MainWindowUI::instanceGroupGearSize() 那段写的正是
    // 这件事）：不缩的话，一行只有十几像素时它上下各冒出去一半，屏幕上就是"齿轮被切掉
    // 上下两条"。缩法是**从命中格里对称地往回收** —— 行够高时收到的正好是那 3 px 的余量
    // （和 `adjusted(3, 3, -3, -3)` 逐像素一样），行矮时再多收一点，收到边长等于行高为止。
    const int size = qMin(MainWindowUI::instanceGroupGearSize(), row.height());
    const int inset = (box.width() - size) / 2;
    return box.adjusted(inset, inset, -inset, -inset);
}

QRect InstanceGroupDelegate::moveDownRect(const QRect &row) {
    // 命中格比画出来的三角大一圈（三角的边长是 versionTreeArrowSize()，四周各留 3 px）——
    // 和齿轮/那个「取消」小叉同一个做法：12 px 的点击目标太小了，手一抖就点到旁边那颗
    // （而"把这一组挪到别处去"点错一下是有后果的）。
    const int box = MainWindowUI::versionTreeArrowSize() + 6;
    // 紧挨着齿轮左边，中间留 6 px（和"齿轮到展开箭头"是同一个数，看着才是一套的）。
    const int left = gearRect(row).left() - 6 - box;
    return QRect(left, row.top() + (row.height() - box) / 2, box, box);
}

QRect InstanceGroupDelegate::moveUpRect(const QRect &row) {
    const QRect down = moveDownRect(row);
    // 上移那颗**紧挨着**下移那颗（中间不留缝）：两颗自己已经各带了 3 px 的手感余量，
    // 中间再有缝的话会空出一格"点下去什么都没发生"的地方 —— 而那一格正是最容易点到的地方。
    return QRect(down.left() - down.width(), down.top(), down.width(), down.height());
}

QRect InstanceGroupDelegate::moveUpGlyphRect(const QRect &row) {
    const QRect box = moveUpRect(row);
    const int size = qMin(MainWindowUI::versionTreeArrowSize(), row.height());
    const int inset = (box.width() - size) / 2;
    return box.adjusted(inset, inset, -inset, -inset);
}

QRect InstanceGroupDelegate::moveDownGlyphRect(const QRect &row) {
    const QRect box = moveDownRect(row);
    const int size = qMin(MainWindowUI::versionTreeArrowSize(), row.height());
    const int inset = (box.width() - size) / 2;
    return box.adjusted(inset, inset, -inset, -inset);
}

QString InstanceGroupDelegate::rowActionTip(const QModelIndex &index, const QRect &row,
                                            const QPoint &pos) {
    // 三格一条一条来。顺序就是"谁压在谁上面"的顺序 —— 今天三格互不重叠，顺序无所谓；
    // 将来真叠上了，先判的那个赢（和 VersionTreeDelegate::rowActionTip() 同一条规矩）。
    const int moveMask = index.data(MainWindowUI::rowGroupMoveRole()).toInt();
    if ((moveMask & MainWindowUI::groupMoveUpFlag()) != 0 && moveUpRect(row).contains(pos)) {
        return MainWindowUI::rowGroupMoveUpTipText();
    }
    if ((moveMask & MainWindowUI::groupMoveDownFlag()) != 0 && moveDownRect(row).contains(pos)) {
        return MainWindowUI::rowGroupMoveDownTipText();
    }
    if (index.data(MainWindowUI::rowGroupGearRole()).toBool() && gearRect(row).contains(pos)) {
        return MainWindowUI::rowGroupGearTipText();
    }
    return QString();
}

bool InstanceGroupDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                                      const QStyleOptionViewItem &option, const QModelIndex &index) {
    // 只在"光标真落在那颗齿轮那一格上"时说话（和 VersionTreeDelegate 那个箭头/叉一个规矩），
    // 别的地方一句都不弹。
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

void InstanceGroupDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const {
    // 实例那几行（没有组 id 那个 role 的）**原样交给基类** —— 这一层只管分组头，
    // 见文件头那条"为什么不复用 VersionTreeDelegate"。
    if (!index.data(MainWindowUI::groupIdRole()).isValid()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    // 背景/选中/悬停那一层交给样式画，但文字先清掉 —— 位置由下面自己摆（左边留白、
    // 右边要给齿轮和箭头让出两格）。features 清空之后 CE_ItemViewItem 就只画背景那一层。
    opt.text.clear();
    opt.icon = QIcon();
    opt.features = QStyleOptionViewItem::None;
    if (QStyle *style = opt.widget != nullptr ? opt.widget->style() : QApplication::style()) {
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    }

    const QRect row = option.rect;
    const QPalette &palette = option.palette;
    const int pad = MainWindowUI::versionTreeRowPadding();
    const int arrowSize = MainWindowUI::versionTreeArrowSize();
    const bool hasGear = index.data(MainWindowUI::rowGroupGearRole()).toBool();
    // 行尾那两颗上/下箭头画哪一颗（位掩码，见 MainWindowUI::rowGroupMoveRole()）。谁写进去的、
    // 为什么不是"在文件里排第几"，都在那个角色那段。
    const int moveMask = index.data(MainWindowUI::rowGroupMoveRole()).toInt();
    const bool canMoveUp = (moveMask & MainWindowUI::groupMoveUpFlag()) != 0;
    const bool canMoveDown = (moveMask & MainWindowUI::groupMoveDownFlag()) != 0;
    // 展开着就画朝下的 V、收着就画朝右的 >。状态只有视图知道（`isExpanded`）——
    // 和 VersionTreeDelegate 一样从 option.widget 上问，问不到就当收着。
    const auto *view = qobject_cast<const QTreeView *>(option.widget);
    const bool expanded = view != nullptr && view->isExpanded(index);

    // 右边那几格从文字的可用宽度里扣掉 —— 不扣的话长组名会从齿轮底下穿过去。
    //
    // NOTE: 上/下那两颗**按"两颗都占着"扣**，不管这一行此刻画的是几颗：只画一颗的行
    //       （最上面那一组没有上移）要是只扣一颗，同一张树上每一行的文字右边界就不一样了
    //       —— 组名一长，看着就是每行各自截断在不同的地方。
    int textRight = row.right() - pad - arrowSize - 6;
    if (hasGear) {
        textRight -= gearRect(row).width() + 6;
        textRight -= moveDownRect(row).width() * 2;
    }
    const int textLeft = row.left() + pad;
    const QRect textRect(textLeft, row.top(), qMax(0, textRight - textLeft), row.height());

    // 组名是**粗体**：它是一堆实例的标题，和底下的实例名不是一回事（版本树那几条分类
    // 也是这么画的）。
    QFont mainFont = option.font;
    mainFont.setBold(true);
    painter->save();
    painter->setFont(mainFont);
    painter->setPen(palette.color(QPalette::WindowText));
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(Qt::DisplayRole).toString());
    // 行尾那两颗上/下箭头（齿轮左边）：只有这一行真挪得动的那一侧才画 —— 挪不动就没有
    // 那一颗（不是画成灰的）。理由：这两颗是"这一组在顺序里还能不能动"，一颗都没有的行
    // 一眼就说明它已经到头了；画成灰的还得先学会"灰 = 点不动"（而它旁边那颗齿轮的颜色
    // 本来就是灰的那一档，两处灰的含义还不一样）。
    if (canMoveUp) {
        const QRect glyph = moveUpGlyphRect(row);
        painter->drawPixmap(glyph.topLeft(),
                            VersionIcons::triangleUp(glyph.width(),
                                                     palette.color(QPalette::PlaceholderText)));
    }
    if (canMoveDown) {
        const QRect glyph = moveDownGlyphRect(row);
        painter->drawPixmap(glyph.topLeft(),
                            VersionIcons::triangleDown(glyph.width(),
                                                       palette.color(QPalette::PlaceholderText)));
    }
    // 行尾那颗齿轮：画的是 gearGlyphRect()（比命中格小一圈、还要装进行高里），用次要那一档
    // 颜色 —— 它是行里的一个动作，不是正文。
    if (hasGear) {
        const QRect glyph = gearGlyphRect(row);
        painter->drawPixmap(glyph.topLeft(),
                            VersionIcons::gear(glyph.width(),
                                               palette.color(QPalette::PlaceholderText)));
    }
    // 最右边那个箭头：分组头**一定**画（"点一下打开/关闭"就是它）。用正文色 ——
    // 这一行真的能展开，和版本树那几条分类同一条规矩。
    painter->drawPixmap(chevronRect(row).topLeft(),
                        VersionIcons::chevron(expanded, arrowSize,
                                              palette.color(QPalette::WindowText)));
    painter->restore();
}

QSize InstanceGroupDelegate::sizeHint(const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const {
    const bool isGroup = index.data(MainWindowUI::groupIdRole()).isValid();
    QStyleOptionViewItem opt(option);
    if (isGroup) {
        // 正文是粗体画的，宽度也得按粗体量（不然"组名 + 右边那两格"会差几个像素）。
        QFont bold = option.font;
        bold.setBold(true);
        opt.font = bold;
    }
    const QSize size = QStyledItemDelegate::sizeHint(opt, index);
    // 行高**每一行都钉成版本树那个常数**（实例行也一样）—— 用户 2026-09-25 原话：
    // 「每个版本啥的上下高一点，像下载页那样」。树上开着 uniformRowHeights（第一行多高所有
    // 行就多高），而平铺那一档的第一行是**实例**、有分组时第一行是**组头** —— 两边都给
    // 同一个数，"一张树多高"才不随"有没有分组"翻个面。
    const int height = MainWindowUI::versionTreeRowHeight();
    if (!isGroup) {
        return QSize(size.width(), height);
    }
    // 宽度按内容加：右边那几格也占地方 —— 展开箭头一律有，齿轮和那两颗上/下箭头看角色。
    // NOTE: 上/下那两颗**按两颗都占着**加（和 paint() 里扣文字宽度同一个口径），不然
    //       最上面那一组的行宽会比别的组窄一截，树的横向滚动条会跟着一会儿有一会儿没有。
    int extra = MainWindowUI::versionTreeArrowSize() + 6;
    if (index.data(MainWindowUI::rowGroupGearRole()).toBool()) {
        extra += MainWindowUI::instanceGroupGearSize() + 6 + 6;
        extra += (MainWindowUI::versionTreeArrowSize() + 6) * 2;
    }
    return QSize(size.width() + extra, height);
}
