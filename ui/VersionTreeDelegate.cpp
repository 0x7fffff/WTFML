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
#include "MotdText.hpp"
#include "Theme.hpp"
#include "VersionIcons.hpp"

VersionTreeDelegate::VersionTreeDelegate(QObject *parent, bool leafArrow, int iconSize,
                                         bool navRow)
    : QStyledItemDelegate(parent), m_leafArrow(leafArrow), m_iconSize(iconSize), m_navRow(navRow) {
}

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

QRect VersionTreeDelegate::arrowRect(const QRect &row) {
    const int pad = MainWindowUI::versionTreeRowPadding();
    const int arrowSize = MainWindowUI::versionTreeArrowSize();
    // 命中格比画出来的那个箭头四周各大 3 px（和 clearRect / refreshRect 一个做法）。
    const int box = arrowSize + 6;
    // 命中格**以画出来的那一格为中心**：画出来的是贴右边缘留 pad 的那一块
    //（`row.right() - pad - arrowSize` 起、宽 arrowSize），中心就在它中间。
    const int centerX = row.right() - pad - arrowSize / 2;
    return QRect(centerX - box / 2, row.center().y() - box / 2, box, box);
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
    // 悬停那一位**先读走、再抹掉**：它是给"这一行上我们自己画的那个按钮"用的（见下面那段
    // showRefresh），本来就不是"这一行被指到了"这件事本身。
    // ★ 2026-09-29 更正：抹掉它**本来就不是**为了挡悬停底色 —— 那一层在这个仓库里一直是
    //   **我们自己画**才有的（就是下面那段 fillRect）。核过 Qt 源码：
    //   `QCommonStyle::drawPrimitive(PE_PanelItemViewItem)` 只认 `backgroundBrush` 和
    //   `State_Selected`，**从不看 `State_MouseOver`**；Fusion 没有覆写它，而我们的
    //   `Theme::createStyle()` 也只重写了 `PE_IndicatorArrowDown` 一个原语。
    //   （旧注释写的是"留着的话样式会给整行铺一层悬停底色、而那是用户没要的"：前半句按
    //   源码不成立，后半句从 2026-09-29 起也反了 —— 用户原话「把所有悬停在版本/按钮上都
    //   加一个颜色加深的效果」。）
    //   这一位仍然要抹掉：别让**将来**某个样式插一层我们没要的底色，把我们画的那层盖掉。
    // 读的是**原样传进来**的那个 option，不是 initStyleOption 之后的那一份：这一位是视图
    // 填的，它在 initStyleOption 里留没留住不该由我们猜（这条路上我们自己说了算）。
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
    // ★ 选中那一块由**样式**画成"这一行自己的底色混一档主题色 + 左边一根 3 px 指示条"
    //   （用户 2026-09-29 原话：「选中的时候那个蓝色的换成深灰色」；2026-10-12 换成现在
    //   这一档 —— 用户答复「完全照 §9.3（和侧栏导航同款）」
    //   —— 两条原语覆写在 `Theme::createStyle()` 那一份代理样式里，见 `ui/Theme.hpp` 的
    //   `selectionFill()` 那段）。这一位在这儿用来做三件事：左边那一截分支区补涂、
    //   分支区那截上**把指示条补回来**，以及下面那一份**局部调色板**里换墨。
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    opt.state &= ~QStyle::State_MouseOver;
    // 侧栏导航的选中底由下面那支自己画成「内缩圆角块」。这里先把 Selected 从交给
    // 平台样式的副本里拿掉，否则 Fusion 会先铺一块顶到四边的灰底，圆角外面会露出
    // 四个灰色直角。普通数据列表仍然保留平台样式负责的那块选中底。
    if (m_navRow) {
        opt.state &= ~QStyle::State_Selected;
    }
    // ★ 悬停那一行的加深底色（用户原话见上）。
    //
    // 这一层**画在 `CE_ItemViewItem` 之前**是有意的：选中那一行是样式紧接着用**不透明的**
    // `Highlight` 铺满整格的，所以"选中"天然压得住"悬停"，两者不需要互斥判断（下面那句
    // 挪到之后去画的话，悬停就会反压选中 —— 那才是错的）。
    // 底色走 `Theme::rowHoverFill()`（唯一出处在 ui/Theme.hpp 里那段说明）：它从**这一层
    // 自己的底色**出发往主题色混一档，于是浅色主题下和白底按钮的 `:hover` 逐字节同一个色，
    // 换成深色预设也不会糊成一片近白（"按硬编码白去算"的写法在 `Base = #323232` 上会炸）。
    // ★ 不可用的那几行不染（判据和下面 `rowOpacity` 那条逐字一样）：那一档整行淡到 0.5，
    //   再铺一层底色反而像是"点得动"。在这儿**内联**写一遍是因为 `rowEnabled` 要到下面才
    //   算得出来，而这一层必须赶在样式画底色之前。
    const bool hoverable = hovered && (index.flags() & Qt::ItemIsEnabled) != 0;
    if (hoverable) {
        const QColor hoverFill = Theme::rowHoverFill(option.palette.color(QPalette::Base));
        if (m_navRow) {
            const QRectF hoverRect = QRectF(option.rect.adjusted(3, 2, -3, -2));
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(hoverFill);
            painter->drawRoundedRect(hoverRect, 7.0, 7.0);
            painter->restore();
        } else {
            painter->fillRect(option.rect, hoverFill);
        }
        // 有子条目的那几棵树上（版本树、资源表：分类 → 版本号），条目左边留着一截**分支区**，
        // 而**选中**那一层是连这一截一起涂的 —— `QTreeView::drawRow()` 先按
        // `QRect(position, y, indentation, height)` 铺一遍选中色，**然后**才把画笔交给代理
        // （长注释在 ui/InstanceGroupDelegate.cpp 里那段 ★★ 2026-09-29）。
        // 悬停**不补这一截**的话，同一行上"选中带"和"悬停带"的左边就差着一个缩进（默认
        // 20 px）—— 看着像是悬停没铺到头。所以照着选中的形状补上。
        // ★ 只在第 0 列补：这一截长在**这一格自己的左边**，别的列那一截是上一列的地盘。
        //   （今天挂在这份代理上的每一张表都是单列 —— 服务器表是多列，但代理只挂在第 0 列。
        //   留着这个条件是因为"从 0 开始涂"这件事只在第 0 列上成立。）
        // ★ 宽度取 `option.rect.left()`（= 这一格从哪儿开始），不是写死的缩进：横向滚动时
        //   它和视图那边同时变小（两边同以 `columnViewportPosition(0)` 为原点），整截滚出屏
        //   时它 ≤ 0、这一句自己什么都不画；写死的话往右滚一下左边就留一根擦不掉的条。
        if (index.column() == 0 && option.rect.left() > 0) {
            painter->fillRect(
                QRect(0, option.rect.top(), option.rect.left(), option.rect.height()), hoverFill);
        }
    }
    if (QStyle *style = opt.widget != nullptr ? opt.widget->style() : QApplication::style()) {
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    }

    // ★ 选中那一行的**分支区那一截**：`QTreeView::drawRow()` 在把画笔交给代理之前自己先用
    //   **平台的选中色（蓝）**把 `QRect(0, y, 缩进, 行高)` 铺一遍（长注释在
    //   ui/InstanceGroupDelegate.cpp 里那段 ★★ 2026-09-29）。样式那一层已经把
    //   `PE_PanelItemViewRow` 也接过去画成我们这一档了，但"drawRow 到底走不走那条原语"是
    //   **视图**说了算的事 —— 这一句是保底，两边同一个色，重复涂一遍看不出区别。
    //   NOTE: 实例树那一边**故意不补**（它选中时的带子只有卡片那一圈，左边那根竖条正是
    //   用户 2026-09-29 点名要删掉的东西，见那条 ★★）。两张表的选中带形状本来就不一样。
    //
    //   ★ 指示条得在**这一句之后**补一次：样式那条 `PE_PanelItemViewItem` 早在 3 行之前
    //     就把 0..3 那一列画成主题色了，而这一句正好从 0 铺过来、会把它整根盖掉。
    //     （`option.rect.left() == 0` 的行不补 —— 那时候这一句根本不画，样式画的那根还在。）
    if (selected && !m_navRow && index.column() == 0 && option.rect.left() > 0) {
        const QColor base = option.palette.color(QPalette::Base);
        painter->fillRect(QRect(0, option.rect.top(), option.rect.left(), option.rect.height()),
                          Theme::selectionFill(base));
        Theme::paintSelectionRail(painter, option.rect);
    }

    // ★★ 侧栏那一列（`m_navRow`）：选中那一行的底和墨和别处**是同一套**，只是形状不同 ——
    //    这里是"内缩圆角块 + 一块跟着圆角走的指示条"，别的表是"整条行 + 左边那根直条"
    //    （用户 2026-10-12 那份改版文档点名侧栏是圆角块那一款）。
    //
    //    平台样式那一层的 Selected 已在上面拿掉：这一档要的是内缩圆角块；如果先让
    //    Fusion 铺满灰底，再在上面盖圆角块，四个圆角外仍会漏出灰色直角。
    //    ★ 换色**只能**这么做：调色板里那一档 `Highlight` 被 `WtfmlProxyStyle::`
    //      `selectedItemOption()` 拿去当"要不要接管"的判据了（见 Theme.hpp 里那段：
    //      换过 Highlight 的表它就不接管），而这一层要的恰恰是"接管了之后再改一层色"。
    if (m_navRow && selected) {
        const QRectF selectedRect = QRectF(option.rect.adjusted(3, 2, -3, -2));
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(Theme::selectionFill(option.palette.color(QPalette::Base)));
        painter->drawRoundedRect(selectedRect, 7.0, 7.0);

        // 指示条也收在圆角块里面，上下留一点呼吸，不再像一根贯穿两行之间的分隔线。
        const QRectF barRect(selectedRect.left(), selectedRect.top() + 6.0,
                             Theme::selectionBarThickness(),
                             qMax(0.0, selectedRect.height() - 12.0));
        painter->setBrush(Theme::selectionBar());
        painter->drawRoundedRect(barRect, Theme::selectionBarThickness() / 2.0,
                                 Theme::selectionBarThickness() / 2.0);
        painter->restore();
    }

    const QRect row = option.rect;
    // ★ 选中那一行的**墨**：底已经不是调色板里那一档蓝了（是我们自己画的那一块），而这一行上
    //   手画的每一笔（正文、第二行小字、图标、右边的箭头）都从下面这份 palette 取色 ——
    //   不换的话深色主题下就是白字压浅底（等于没字）。换哪几档只有一处出处
    //   （`selectionPalette()`），换的是**一份局部拷贝**：控件的调色板、别处的取色一点都不
    //   受影响。
    //   ★ 侧栏那一列**不用再单独换一遍**（2026-10-12 合成一份之前它有自己那一支墨）：
    //     现在两条路的底是同一个混法，墨自然也是同一支 —— 合成之前那句话留下的第二处
    //     取色点正是"以后改一处漏一处"的形状。
    QPalette palette = Theme::selectionPalette(option.palette, selected);
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
    // ★ 2026-09-25：这个按钮**只在鼠标悬停到这一行时才画**（用户原话：「下载界面只有鼠标
    //    悬停在按钮上时才显示对应的刷新按钮」）。扣宽度那一条**没有**跟着挂 hovered ——
    //    宽度一直留着，鼠标挪进来的时候正文不会左右跳一下（那比少一个图标显眼得多）。
    // NOTE: hovered 是**视图**填的，而视图**默认不填**：量过（探针在 tmp/probe_hover）——
    //       干净的 QListWidget 视口 WA_Hover=0 / mouseTracking=0，真鼠标移动一个字都不改
    //       State_MouseOver；给那份列表开 setMouseTracking(true) 之后才会改（真平台的
    //       windows 与 offscreen 各量一遍，同一个结果）。所以这一段搭在 MainWindowUI 里
    //       建左栏那份列表时那句 setMouseTracking 上 —— 那句被删掉的话，症状是"刷新按钮
    //       再也不出现了"，不是报错、也不是断言先红。
    //
    // ★ 2026-09-27：上面那句话（"扣宽度没跟着挂 hovered"）在代码里**曾经是相反的** ——
    //   扣宽度用的就是 `showRefresh`，而它带着 `&& hovered`。于是鼠标一进这一行：图标
    //   冒出来（对的）+ 文字宽度当场变窄 ⇒ 长条目的省略号位置左右跳一下，正是上面那句
    //   "那比少一个图标显眼得多"要避免的。两个判据拆开：**这一行有没有这一格**（一直扣）、
    //   **这一刻画不画它**（跟着 hovered）。
    const bool hasRefresh = index.data(MainWindowUI::rowRefreshRole()).toBool();
    const bool showRefresh = hasRefresh && hovered;
    const QRect refreshBox = refreshRect(row);
    int textRight = row.right() - pad - arrowSize - 6;
    if (showClear) {
        textRight -= crossBox.width() + 6;
    }
    if (hasRefresh) {
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
    // ★ 服务器那一行的 MOTD（rowMotdRawRole()）：**照游戏里的样子画**（用户 2026-09-27
    //   原话「motd按照游戏里显示的样式展现」）—— 带颜色、带字形、还是两行。
    //
    // NOTE: **在这里现解析**，不把解析结果塞进 QVariant：`QVector<MotdPiece>` 要做成
    //       元类型得先登记（跨线程的排队连接还要 `qRegisterMetaType`），而这一串是**画**的
    //       时候才用得着的东西 —— 现解析一遍的代价是几十个字符的扫描，比那条路省事得多。
    //
    // NOTE: 和 `rowSubTextRole()` 是**二选一**：两个角色都非空时只画 MOTD 那条
    //       （否则同一格上会叠两段字）。`rowSubTextRole()` 那份留着 —— 它有两个用处：
    //       悬停提示，以及"只有洗过的那一行、没有原串"的老路（缓存里没有 motdRaw 时）。
    const QString motdRaw = index.data(MainWindowUI::rowMotdRawRole()).toString();
    const QVector<MotdPiece> motdPieces =
        motdRaw.isEmpty() ? QVector<MotdPiece>() : MotdText::parse(motdRaw, 2);
    const int motdLines = motdPieces.isEmpty() ? 0 : (motdPieces.last().line + 1);
    QFont subFont = option.font;
    subFont.setPointSize(qMax(6, option.font.pointSize() + MainWindowUI::versionDateFontDelta()));
    const QFontMetrics mainMetrics(option.font);
    const QFontMetrics subMetrics(subFont);
    // 两行合成一块，整块在行里居中（行高是常数，所以"上面一行字下面一行字"看着才稳）。
    // NOTE: 底下那一块可能是**两行**（MOTD），所以高度按行数算 —— 不按行数算的话，
    //       两行的 MOTD 会被画到行外去（行高 64 是服务器那一页逐条设的，够装）。
    const int gap = 2;
    const int subLines = motdLines > 0 ? motdLines : (subText.isEmpty() ? 0 : 1);
    const int blockHeight =
        mainMetrics.height() + (subLines == 0 ? 0 : gap + subLines * subMetrics.height());
    int y = row.center().y() - blockHeight / 2;

    QFont mainFont = option.font;
    if (isChannel) {
        // 顶层那几条（分类 / 加载器）是**粗体**：它们和底下的版本号不是一回事。
        mainFont.setBold(true);
    }
    painter->setFont(mainFont);
    painter->setPen(palette.color(QPalette::WindowText));
    // ★ 两行都要**先截成省略号再画**（2026-09-26 修）。`drawText(rect, 文字)` 在装不下时
    //   是**硬裁**：最后那个字被切掉半个、右边那一小截贴在图标上，看着像"字画坏了"，
    //   而不是"这里还有更多"。版本名和加载器名都能长过这一格（自定义名更长），
    //   资源表那几张也一样 —— 四张表都用这一个代理。
    //   同一份写法在 GroupListDelegate 里（那是 2026-09-25 加的新代理，一开始就带截断），
    //   这里当时漏了。宽度的算法一个字没动，只多这一步。
    painter->drawText(QRect(textRect.left(), y, textRect.width(), mainMetrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      mainMetrics.elidedText(mainText, Qt::ElideRight, textRect.width()));
    y += mainMetrics.height() + gap;
    if (motdLines > 0) {
        // 底下那一块按游戏那样画：一段一段接着画，每段自己的颜色和字形；换行就回到左边。
        //
        // ★ 颜色要**按我们的底色重新算一次对比度**（MotdText::readableOn）：游戏那边
        //   底色是黑的，服务器按黑底配的 `§f` 白字 / `§e` 黄字在我们的浅色底上几乎看不见
        //   —— 白字画在白底上等于没画，而这一格本来就是"要看服务器写了什么"。
        //   底色取的是**这一行真正铺的那一层**：选中时是 Highlight、平时是 Base。读的是
        //   传进来那个 `option`（上面只抹掉了 MouseOver 那一位，选中那一位原样还在）。
        const QColor rowBg = (option.state & QStyle::State_Selected)
                                 ? palette.color(QPalette::Highlight)
                                 : palette.color(QPalette::Base);
        int line = motdPieces.first().line;
        int lineY = y;
        int x = textRect.left();
        // ★ 2026-09-27：这一行装不下了要**只跳过这一行剩下的段**，下一行照画。
        //   原来两处都是 `break`，跳的是**整个循环** —— 于是第一行比那一格宽时（这一列是
        //   拉伸的，还得给地址/状态两列让位，第一行写长的服务器很常见）第二行**整行不画**，
        //   而 subLines / blockHeight 是按两行算的 ⇒ 正文整体上移、底下空出一条。
        //   原来那段注释逐字写着「**不是** break 整个循环：第二行（如果还有）照画」，
        //   代码干的正好是它说没干的那件事。
        bool skipRestOfLine = false;
        for (const MotdPiece &piece : motdPieces) {
            if (piece.line != line) {
                line = piece.line;
                lineY += subMetrics.height();
                x = textRect.left();
                skipRestOfLine = false;  // 新的一行：从头照画
            }
            if (skipRestOfLine) {
                continue;
            }
            const int avail = textRect.left() + textRect.width() - x;
            if (avail <= 0) {
                // 这一行已经画满了（正好填满时会走到这儿，x 落在右边缘上）。
                // 同一件事：只作废这一行剩下的段。
                skipRestOfLine = true;
                continue;
            }
            QFont pieceFont = subFont;
            pieceFont.setBold(piece.bold);
            pieceFont.setItalic(piece.italic);
            pieceFont.setUnderline(piece.underline);
            pieceFont.setStrikeOut(piece.strike);
            const QFontMetrics pieceMetrics(pieceFont);
            // 装不下就从**这一段**的尾巴上省略（和上面那两行同一个做法：`drawText` 装不下
            // 是硬裁，最后那个字会被切掉半个）。省略了就是这一行的结尾 —— 后面那些段
            // 再画就要压到省略号上了，作废掉。第二行（如果还有）照画，见上面那一句。
            const QString shown = pieceMetrics.elidedText(piece.text, Qt::ElideRight, avail);
            painter->setFont(pieceFont);
            painter->setPen(MotdText::readableOn(MotdText::pieceColor(piece), rowBg));
            painter->drawText(QRect(x, lineY, avail, subMetrics.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, shown);
            if (shown != piece.text) {
                skipRestOfLine = true;
                continue;
            }
            x += pieceMetrics.horizontalAdvance(shown);
        }
    } else if (!subText.isEmpty()) {
        painter->setFont(subFont);
        painter->setPen(palette.color(QPalette::PlaceholderText));
        painter->drawText(QRect(textRect.left(), y, textRect.width(), subMetrics.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          subMetrics.elidedText(subText, Qt::ElideRight, textRect.width()));
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
                                                  (index.flags() & Qt::ItemIsEnabled) ? Theme::tokens().primary : Theme::tokens().disabled));
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
    // 箭头那一格：**几何只有 arrowRect() 一处** —— 它给的是命中格（比画出来的大一圈），
    // 画的是它居中那一块。下载页那四张表按同一个函数认"这一下点的是不是箭头"
    // （见 MainWindow::eventFilter 里那一段），两处就是同一处。
    const QRect arrowBox = arrowRect(row);
    const QRect arrowGlyph(arrowBox.center().x() - arrowSize / 2,
                           row.center().y() - arrowSize / 2, arrowSize, arrowSize);
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
        painter->drawPixmap(arrowGlyph.topLeft(),
                            VersionIcons::chevron(expanded, arrowSize, arrowColor));
    }
    painter->restore();
}

QSize VersionTreeDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const {
    // 行高是个**常数**，不走"按内容算"：树上开着 uniformRowHeights（900 多条，让它按
    // 第一行算一次就够），而第一行是分类（一行字）、底下是版本号（两行字）——
    // 两边高度不一样的话，两行那种会被按一行的行高裁掉半行。
    //
    // ★ 2026-09-27：条目自己给了 `Qt::SizeHintRole` 的**按它算高度**。服务器列表那一页要的
    //   就是这一条 —— 它的行里画的是**两行** MOTD（照游戏里那个样子），46 装不下，那一页
    //   逐条 `setSizeHint(QSize(0, serverRowHeight()))`。在这之前这里写死一个常数，
    //   `SizeHintRole` 被**静默无视**（Qt 的规矩是代理说了算，模型那一格只是"建议"）——
    //   症状是"设了行高、屏幕上一点没变"，不报任何东西。
    //   宽度**照旧自己算**（它要按两行字、图标、行尾那几个角色逐项加起来），只在条目自己
    //   给了更宽的要求时才让路。
    const QSize hint = index.data(Qt::SizeHintRole).toSize();
    const int height =
        hint.height() > 0 ? hint.height() : MainWindowUI::versionTreeRowHeight();
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
    // MOTD（rowMotdRawRole()）量的是**最长的那一行**：`§` 码不是字，不能拿原串直接量
    //（量出来比画出来的宽上一大截，那一格会白白多留一块）。
    const QString motdRaw = index.data(MainWindowUI::rowMotdRawRole()).toString();
    if (!motdRaw.isEmpty()) {
        int line = -1;
        int lineWidth = 0;
        int widest = 0;
        // ★ 2026-10-11：`subFont` 是循环不变量，度量构造一次就够（先前每片 MOTD 都新建一个
        //   QFontMetrics）。
        const QFontMetrics subMetrics(subFont);
        for (const MotdPiece &piece : MotdText::parse(motdRaw, 2)) {
            if (piece.line != line) {
                line = piece.line;
                lineWidth = 0;
            }
            lineWidth += subMetrics.horizontalAdvance(piece.text);
            widest = qMax(widest, lineWidth);
        }
        width = qMax(width, widest);
    }
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
    // NOTE: 判据里**没有** hovered —— 那一格是"这一行有这么一个按钮"占的地方，不是"这会儿
    //       画不画"。跟着悬停变的话，鼠标一扫过去整行的宽度就跟着变（paint 那段有说明）。
    if (index.data(MainWindowUI::rowRefreshRole()).toBool()) {
        width += MainWindowUI::versionTreeIconSize() + 6 + 6;
    }
    return QSize(width, height);
}
