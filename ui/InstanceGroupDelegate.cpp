#include "InstanceGroupDelegate.hpp"

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

InstanceGroupDelegate::InstanceGroupDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QRect InstanceGroupDelegate::chevronRect(const QRect &row) {
    const int pad = MainWindowUI::versionTreeRowPadding();
    // 边长按这一行的高度收一收。实例树 2026-09-25 之前没有行高常数（就是一行字那么高，
    // 十几像素），2026-09-25 起钉了一个、2026-09-29 起换成它自己那个
    // （instanceTreeRowHeight() = 58）—— 所以这一缩现在**平时不生效**，留着是兜底：
    // 行高哪天再变矮，12 px 的箭头自己跟着缩，而不是上下各冒出去一条被切掉。
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

QRect InstanceGroupDelegate::cardRect(const QRect &row) {
    // 一道空上下各分一半。缺口是**奇数**的时候多出来的那 1 px 落在底下 —— 挑哪边都行，
    // 写死一边是为了"同一个行矩形永远算出同一个卡片矩形"（随机取整会让量像素的断言
    // 两趟之间差一像素）。
    const int gap = MainWindowUI::instanceTreeRowGap();
    if (gap <= 0) {
        return row;
    }
    const int top = gap / 2;
    return row.adjusted(0, top, 0, -(gap - top));
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

void InstanceGroupDelegate::instanceActionGlyphRects(const QRect &row, QRect &gear, QRect &pin,
                                                     QRect &trash) {
    // 一整排的算法就这一份（三个 `instanceXxxGlyphRect()` 和三个 `instanceXxxRect()` 都是
    // 它的薄壳，见 .hpp）：**从行的右边缘往里排**，一颗一颗往左摆。不这么收的话，改一次
    // 间距要同步改六处，而漏改一处只会让某一格错位——屏幕上完全看不出是哪一段写错了。
    const int pad = MainWindowUI::versionTreeRowPadding();
    const int gap = MainWindowUI::instanceRowActionGap();
    // 边长照 `gearGlyphRect()` 那个兜底收一收：行矮的时候 16 px 的字形会上下各冒出去一半。
    // 行高平时是 instanceTreeRowHeight()（58），这一缩不生效，留着是兜底。
    const int size = qMin(MainWindowUI::instanceRowActionSize(), row.height());
    // 竖直居中：和 chevronRect()/gearRect() 同一个式子（`top + (行高 - 边长) / 2`，**不用
    // `row.center().y()`**，理由见 chevronRect() —— 行高是偶数时分母那一半少一像素）。
    const int top = row.top() + (row.height() - size) / 2;
    // ★ 右边缘用 `row.x() + row.width()`，**不是 `row.right()`**：后者是"最后一个像素的 x"
    //   （= left + width - 1），拿它当锚整排会往左挪一个像素 —— 而这一像素正是调用方
    //   （`MainWindow::eventFilter`）和代理对不齐的那种错。
    const int right = row.x() + row.width() - pad;
    // 右起：齿轮 → 上箭头 → 垃圾桶。每一颗都是"前一颗的左边再退一个间隔"。
    gear = QRect(right - size, top, size, size);
    pin = QRect(gear.left() - gap - size, top, size, size);
    trash = QRect(pin.left() - gap - size, top, size, size);
}

QRect InstanceGroupDelegate::instanceGearRect(const QRect &row) {
    QRect gear, pin, trash;
    instanceActionGlyphRects(row, gear, pin, trash);
    // 命中格比画出来的那个字形大一圈（四周各留 instanceRowActionMargin() = 3，和 gearRect()/
    // clearRect() 一个做法）—— 16 px 的点击目标太小了，手一抖就点到那一行上。
    const int m = MainWindowUI::instanceRowActionMargin();
    return gear.adjusted(-m, -m, m, m);
}

QRect InstanceGroupDelegate::instancePinRect(const QRect &row) {
    QRect gear, pin, trash;
    instanceActionGlyphRects(row, gear, pin, trash);
    const int m = MainWindowUI::instanceRowActionMargin();
    return pin.adjusted(-m, -m, m, m);
}

QRect InstanceGroupDelegate::instanceTrashRect(const QRect &row) {
    QRect gear, pin, trash;
    instanceActionGlyphRects(row, gear, pin, trash);
    const int m = MainWindowUI::instanceRowActionMargin();
    return trash.adjusted(-m, -m, m, m);
}

QRect InstanceGroupDelegate::instanceGearGlyphRect(const QRect &row) {
    QRect gear, pin, trash;
    instanceActionGlyphRects(row, gear, pin, trash);
    return gear;
}

QRect InstanceGroupDelegate::instancePinGlyphRect(const QRect &row) {
    QRect gear, pin, trash;
    instanceActionGlyphRects(row, gear, pin, trash);
    return pin;
}

QRect InstanceGroupDelegate::instanceTrashGlyphRect(const QRect &row) {
    QRect gear, pin, trash;
    instanceActionGlyphRects(row, gear, pin, trash);
    return trash;
}

QString InstanceGroupDelegate::rowActionTip(const QModelIndex &index, const QRect &row,
                                            const QPoint &pos) {
    // 组行那一半：这一行**只剩齿轮**一格（上下移那两颗 2026-09-25 搬进分组设置窗了，见 .hpp）。
    if (index.data(MainWindowUI::rowGroupGearRole()).toBool() && gearRect(row).contains(pos)) {
        return MainWindowUI::rowGroupGearTipText();
    }
    // 实例行那一半：三格各说各的话（右起：齿轮 / 上箭头 / 垃圾桶，见 .hpp）。
    // 判据是"**没有** groupIdRole()"，和 paint()/sizeHint() 里"这是不是实例行"是同一条。
    // 三个命中格互不重叠（见 .hpp），所以这就是三条互斥的分支、顺序无所谓 —— 真叠上了的话
    // 先判的那个赢（和 VersionTreeDelegate::rowActionTip() 那段同一个约定）。
    if (!index.data(MainWindowUI::groupIdRole()).isValid()) {
        if (instanceGearRect(row).contains(pos)) {
            return MainWindowUI::rowInstanceSettingsTipText();
        }
        // 那一格（上箭头 / 下箭头**共用的一格**，见 MainWindowUI::rowUnpinRole()）
        // **只在画得出来的行上**才算数 —— 两个角色都为假 = 这一格根本没画（这份
        // .minecraft 里没有置顶组），不然会出现"指着一块空白弹出了「置顶」"。
        // 两个角色互斥（同一个 pinnedGroupId 一个判 `!=`、一个判 `==`），先判哪个都行；
        // 真两个都真了的话先判的那个赢（和上面那条"三条互斥分支"同一个约定）。
        if (instancePinRect(row).contains(pos)) {
            if (index.data(MainWindowUI::rowUnpinRole()).toBool()) {
                return MainWindowUI::rowUnpinTipText();
            }
            if (index.data(MainWindowUI::rowPinRole()).toBool()) {
                return MainWindowUI::rowPinTipText();
            }
        }
        if (instanceTrashRect(row).contains(pos)) {
            return MainWindowUI::rowDeleteInstanceTipText();
        }
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
    // 实例那几行（没有组 id 那个 role 的）：背景/选中/文字/图标**仍然交给基类画**，
    // 这一层只在它画完之后，在行尾补上那三颗按钮（右起：齿轮 / 上箭头 / 垃圾桶，见文件头）。
    if (!index.data(MainWindowUI::groupIdRole()).isValid()) {
        QStyleOptionViewItem opt(option);
        // 悬停那一位**先读走、再抹掉**：它是给"下面我们自己画的那两颗悬停按钮"用的，
        // 不是"这一行被指到了"这件事本身。留着的话样式会给整行铺一层悬停底色 —— 那是
        // 用户没要的另一个改动，而且会把"当前选中的是哪一行"那层底色比下去
        //（这一段和 VersionTreeDelegate::paint() 里那句 `opt.state &= ~QStyle::State_MouseOver`
        // 是同一个理由、同一份措辞）。
        // 读的是**原样传进来**的那个 option：这一位是视图填的，它在 initStyleOption 里
        // 留没留住不该由我们猜（version 树那边也特意读了传进来那一份）。
        const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
        // NOTE: 除 State_MouseOver 之外**别的 state 一个都不许动** —— 这一行还要靠样式
        //       画选中底色和文字/图标（选中那一位就在这个 state 里）。
        opt.state &= ~QStyle::State_MouseOver;
        // ★★ 2026-09-27 修：正文要在**行尾那三颗按钮之前**停下。
        //
        //   原来这里是直接 `QStyledItemDelegate::paint(painter, opt, index)`，正文由样式
        //   按**整格宽度**摆 —— 而实例页这一列是**拉伸**的（表头藏着、只有一列，
        //   stretchLastSection 为真、columnWidth == 视口宽），sizeHint() 里"加了三颗按钮
        //   的宽度"那一段**根本不参与布局**。于是把一个实例改名成一长串之后，名字一路画到
        //   格子右边缘、被行尾常显的那颗齿轮压掉最后两三个字和省略号（悬停时还会多压上
        //   上箭头/垃圾桶两颗）—— 而那正是 sizeHint() 里那段注释说要防的事。
        //
        //   做法：**先自己 initStyleOption**（不能只改传进来那一份：基类 paint 会自己再
        //   initStyleOption 一次，我们写进去的正文会被它覆盖掉），把正文换成**截断过**的
        //   那一份，然后照 VersionTreeDelegate 那一套直接 drawControl —— 背景、选中、
        //   图标、焦点框仍然全归样式画，只是文字短了、自然停在三颗按钮左边。
        //   文字区的宽度**问样式要**（SE_ItemViewItemText），不去猜它给图标和留白各让了
        //   多少 —— 猜的话换一个样式就错一截。
        initStyleOption(&opt, index);
        // 行与行之间那道空（见 cardRect()）：这一行**画**的那一圈比整条行上下各小半个缺口。
        // 摆在这儿（initStyleOption 之后、量文字之前）是因为下面那句 SE_ItemViewItemText
        // 是从 opt.rect 上算的 —— 少了这一句，量出来的宽度还是整条行的，虽然宽度只跟左右有关、
        // 结果一样，但"画在哪一圈"和"按哪一圈量"从此是两件事，读的人得自己核对一遍。
        opt.rect = cardRect(option.rect);
        if (QStyle *style = opt.widget != nullptr ? opt.widget->style() : QApplication::style()) {
            // 停在哪：最左那颗按钮**命中格**的左边（它已经比字形大一圈，天然留出一点空隙）。
            // 三颗无论此刻画不画都算进去 —— 和 sizeHint() 那条规矩一样（跟着悬停变的话，
            // 鼠标一扫过去正文就左右跳一下）。
            const int stopAt = instanceTrashRect(option.rect).left();
            const QRect textArea =
                style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
            const int avail = qMin(textArea.width(), stopAt - textArea.left());
            if (avail > 0) {
                const QFontMetrics metrics(opt.font);
                opt.text = metrics.elidedText(opt.text, Qt::ElideRight, avail);
            }
            style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        }

        // 三颗都用**正文色**（和实例页文件夹行那颗齿轮一个取色口径）：它们是行里的动作，
        // 但这一行的正文就是实例名，动作和正文同色才不显得"行尾挂了三个别的东西"。
        //
        // NOTE: 每颗都不自己缓存 pixmap —— `VersionIcons::` 内部已经按"尺寸 + 颜色"缓存
        //       （见那边 cached()），这一层再存一份只会多一处要同步的地方。
        const QColor ink = option.palette.color(QPalette::WindowText);
        painter->save();
        // 齿轮**常显**（用户追加答复「要，一颗齿轮常显」）：它和"点这一行任何别的地方"
        // 做的是同一件事（进实例设置），常显是给那件事一个看得见的入口。
        const QRect gearGlyph = instanceGearGlyphRect(option.rect);
        painter->drawPixmap(gearGlyph.topLeft(),
                            VersionIcons::gear(gearGlyph.width(), ink));
        // 中间那一格：悬停才画，而且是**一对反义词共用的一格**（见 MainWindowUI::
        // rowUnpinRole() 那段）—— 还没在置顶组里 ⇒ ↑「置顶」；已经在里面 ⇒ ↓「取消置顶」。
        // 两个角色都为假 = 这一格空着，那就不画（这份 .minecraft 里没有置顶组：
        // 那一格点下去没有去处）。
        //
        // NOTE: 上箭头那一半的判据和原来逐字一样（只有 rowPinRole() 为真的行才画 ——
        //       已经在置顶组里的那一条挪不动 ⇒ 不画，因为"挪不动的那侧不画"这条先例在
        //       ui/GroupListDelegate 里；**不是**画一颗灰的）。下箭头那一半是新加的，
        //       它正好补上了原来那一档"什么都不画"里的一半：现在只剩"根本没有置顶组"
        //       这一种什么都不画的情形。
        const bool canPin = index.data(MainWindowUI::rowPinRole()).toBool();
        const bool canUnpin = index.data(MainWindowUI::rowUnpinRole()).toBool();
        if (hovered && (canPin || canUnpin)) {
            const QRect pinGlyph = instancePinGlyphRect(option.rect);
            painter->drawPixmap(pinGlyph.topLeft(),
                                canUnpin ? VersionIcons::arrowDown(pinGlyph.width(), ink)
                                         : VersionIcons::arrowUp(pinGlyph.width(), ink));
        }
        // 垃圾桶：悬停才画。它点下去还要过一次确认框（那一下由 MainWindow::eventFilter 接），
        // 但它一趟操作是**不可逆**的，所以"鼠标没指到这一行时就不摆出来"。
        if (hovered) {
            const QRect trashGlyph = instanceTrashGlyphRect(option.rect);
            painter->drawPixmap(trashGlyph.topLeft(),
                                VersionIcons::trash(trashGlyph.width(), ink));
        }
        painter->restore();
        return;
    }

    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    // 背景/选中/悬停那一层交给样式画，但文字先清掉 —— 位置由下面自己摆（左边留白、
    // 右边要给齿轮和箭头让出两格）。features 清空之后 CE_ItemViewItem 就只画背景那一层。
    opt.text.clear();
    opt.icon = QIcon();
    opt.features = QStyleOptionViewItem::None;
    // 组行同样收掉那道空（见 cardRect()）。这一句改不动屏幕上的东西 —— 组行没有自己的底色，
    // 露出来的那一带和行本身的底色是同一个（悬停/选中那层带子除外，而组行不可选中）；
    // 留着是为了"这张树里每一行都是同一种画法"，将来谁给行加了底色不会只加一半。
    opt.rect = cardRect(option.rect);
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
    const QFontMetrics mainFontMetrics(mainFont);
    painter->save();
    painter->setFont(mainFont);
    painter->setPen(palette.color(QPalette::WindowText));
    // 组名太长时截成省略号（2026-09-26 修；和 VersionTreeDelegate 里那两行同一个理由 ——
    // `drawText(rect, 文字)` 装不下是硬裁，最后一个字被切掉半个）。组名是用户自己起的名字，
    // 比版本号更容易长过这一格。
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                      mainFontMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                 Qt::ElideRight, textRect.width()));
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
    // 行高**每一行都钉成同一个常数**（组行和实例行都算）—— 用户 2026-09-25 原话：
    // 「每个版本啥的上下高一点，像下载页那样」。树上开着 uniformRowHeights（第一行多高所有
    // 行就多高），而平铺那一档的第一行是**实例**、有分组时第一行是**组头** —— 两边都给
    // 同一个数，"一张树多高"才不随"有没有分组"翻个面。
    //
    // ★ 2026-09-29 起这个常数是**实例列表自己那个**（instanceTreeRowHeight() = 58），不再借
    //   版本树的 46：用户原话「实例列表里每一个之间留一点空的距离，每一行高一点，图标大一点」。
    //   版本树那把尺子（46）一个字没动，见 MainWindowUI 里那一段。
    const int height = MainWindowUI::instanceTreeRowHeight();
    if (!isGroup) {
        // 实例行：行尾那三颗按钮占的宽度**无条件**加上 —— 三颗 + 两个间隔 + 左右各一份
        // 命中格的 margin + 右留白。判据里**故意不放悬停**（和 VersionTreeDelegate::
        // sizeHint() 里那一段同一个规矩）：跟着悬停变的话，鼠标一扫过去整行的宽度就跟着
        // 变、正文左右横跳一下 —— 那比"少一个图标"显眼得多。行高照旧是那个常数。
        //
        // ★ 2026-09-27 NOTE：**这一份宽度只是"想要多少"，管不住真正的布局** —— 实例页
        //   只有一列而且它是拉伸的（表头藏着），columnWidth 永远是视口宽，给条目塞一个更
        //   宽的 sizeHint 也没用。所以"正文不许压到齿轮底下"这件事的真正执行处是
        //   paint() 里那一段（它自己按 SE_ItemViewItemText 把正文截断）。这里留着不改：
        //   换了不拉伸的列宽策略时它就是对的，而且两处的"三颗按钮占多宽"必须一致。
        const int actions = 3 * MainWindowUI::instanceRowActionSize() +
                            2 * MainWindowUI::instanceRowActionGap() +
                            2 * MainWindowUI::instanceRowActionMargin() +
                            MainWindowUI::versionTreeRowPadding();
        return QSize(size.width() + actions, height);
    }
    // 宽度按内容加：右边那几格也占地方 —— 展开箭头一律有，齿轮看角色。
    int extra = MainWindowUI::versionTreeArrowSize() + 6;
    if (index.data(MainWindowUI::rowGroupGearRole()).toBool()) {
        extra += MainWindowUI::instanceGroupGearSize() + 6 + 6;
    }
    return QSize(size.width() + extra, height);
}
