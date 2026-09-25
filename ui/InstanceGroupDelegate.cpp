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

QString InstanceGroupDelegate::rowActionTip(const QModelIndex &index, const QRect &row,
                                            const QPoint &pos) {
    // 这一行现在**只剩齿轮**一格（上下移那两颗 2026-09-25 搬进分组设置窗了，见 .hpp）。
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
    // 展开着就画朝下的 V、收着就画朝右的 >。状态只有视图知道（`isExpanded`）——
    // 和 VersionTreeDelegate 一样从 option.widget 上问，问不到就当收着。
    const auto *view = qobject_cast<const QTreeView *>(option.widget);
    const bool expanded = view != nullptr && view->isExpanded(index);

    // 右边那几格从文字的可用宽度里扣掉 —— 不扣的话长组名会从齿轮底下穿过去。
    int textRight = row.right() - pad - arrowSize - 6;
    if (hasGear) {
        textRight -= gearRect(row).width() + 6;
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
    // 宽度按内容加：右边那几格也占地方 —— 展开箭头一律有，齿轮看角色。
    int extra = MainWindowUI::versionTreeArrowSize() + 6;
    if (index.data(MainWindowUI::rowGroupGearRole()).toBool()) {
        extra += MainWindowUI::instanceGroupGearSize() + 6 + 6;
    }
    return QSize(size.width() + extra, height);
}
