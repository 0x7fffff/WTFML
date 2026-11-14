#include "Support.hpp"
#include "Popups.hpp"
int UiDownloadTypographyChecks(QMainWindow &window, MainWindowUI &ui) {

    {
        QListWidget *categoryList = ui.getDownloadCategoryList();
        QTreeWidget *versionTreeForFont = ui.getVersionTree();
        // 一行文字、没动过字号的普通列表 —— "做大点"要跟它比才有意义（跟常数比等于自己说自己）。
        QListWidget plain;
        plain.addItem(QStringLiteral("参照"));
        plain.resize(200, 60);
        plain.show();
        QApplication::processEvents();

        check(categoryList != nullptr &&
                  qobject_cast<VersionTreeDelegate *>(categoryList->itemDelegate()) != nullptr,
              "分类列表换上了和那两棵树同一个代理（不是默认代理）",
              categoryList != nullptr && categoryList->itemDelegate() != nullptr
                  ? categoryList->itemDelegate()->metaObject()->className()
                  : QStringLiteral("<null>"));

        // ① 做大
        const int rowHeight = categoryList == nullptr ? -1 : categoryList->sizeHintForRow(0);
        check(categoryList != nullptr && rowHeight == MainWindowUI::versionTreeRowHeight(),
              "每一项的行高就是 versionTreeRowHeight()（和版本树那几个分类一般高）",
              QString("row=%1 常数=%2")
                  .arg(rowHeight)
                  .arg(MainWindowUI::versionTreeRowHeight()));
        check(rowHeight > plain.sizeHintForRow(0),
              "行高**确实**比没动过的列表大一截（这就是「做大点」）",
              QString("分类=%1 普通=%2").arg(rowHeight).arg(plain.sizeHintForRow(0)));
        check(categoryList != nullptr && versionTreeForFont != nullptr &&
                  categoryList->font().pixelSize() == 14 &&
                  categoryList->font().pixelSize() == versionTreeForFont->font().pixelSize(),
              "分类和版本列表共用 14 px 正文字号",
              categoryList == nullptr
                  ? QString()
                  : QString("分类=%1 普通=%2 版本树=%3")
                        .arg(categoryList->font().pixelSize())
                        .arg(plain.font().pixelSize())
                        .arg(versionTreeForFont == nullptr
                                 ? -1
                                 : versionTreeForFont->font().pixelSize()));

        // ② 每一条都挂着那个标记（少一条就是"这一类没有刷新"）
        // 「服务端」那一栏**故意没有**这一格 —— 它不向任何资源站问东西，
        // 「刷新」在那儿没有可做的事。所以判据是"除它以外一条不落"。
        const int serverRefreshIndex = MainWindowUI::downloadCategoryServerIndex();
        int marked = 0;
        for (int i = 0; categoryList != nullptr && i < categoryList->count(); ++i) {
            if (categoryList->item(i) != nullptr &&
                categoryList->item(i)->data(MainWindowUI::rowRefreshRole()).toBool()) {
                ++marked;
            }
        }
        check(categoryList != nullptr && marked == categoryList->count() &&
                  marked == MainWindowUI::downloadCategoryKeys().size() &&
                  serverRefreshIndex >= 0 && serverRefreshIndex < categoryList->count() &&
                  categoryList->item(serverRefreshIndex) != nullptr &&
                  categoryList->item(serverRefreshIndex)
                       ->data(MainWindowUI::rowRefreshRole()).toBool(),
              "所有下载分类都有刷新入口，包括服务端版本列表",
              QString("挂上的=%1 条目=%2")
                  .arg(marked)
                  .arg(categoryList == nullptr ? -1 : categoryList->count()));

        // ③ 那一格的几何（拿一个虚拟的宽行算，不依赖列表现在有多宽）
        {
            const QRect row(0, 0, 300, MainWindowUI::versionTreeRowHeight());
            const QRect box = VersionTreeDelegate::refreshRect(row);
            const int boxSide = MainWindowUI::versionTreeIconSize() + 6;
            check(row.contains(box) && box.width() == boxSide && box.height() == boxSide,
                  "刷新那一格整个在行内，边长 = 正文图标 + 6（点击目标比画出来的字形大一圈）",
                  QString("box=%1,%2 %3x%4")
                      .arg(box.x())
                      .arg(box.y())
                      .arg(box.width())
                      .arg(box.height()));
            // 竖向居中：比的是**上下两条空隙**，不是 box.center() == row.center()。
            // NOTE: 允许差 1 px，因为 refreshRect 是按 `row.center().y() - box/2` 摆的，
            // 而 QRect::center() 是整数除法（46 高的行给回 22，不是 23），24 高的格子
            // 跟上去就上下各差 1 px。clearRect()（那个小叉）用的是同一个写法、同样差 1 px
            // —— 一行 46 px 里的 1 px，看不出来。这条断言盯的是"别摆歪"（比如贴到行顶），
            // 不是"必须像素级对称"。
            const int topGap = box.top() - row.top();
            const int bottomGap = row.bottom() - box.bottom();
            // 比的是"格子的中心"离"行的中心"有多远 —— 两个 center() 用的是同一套整数除法
            // （QRect::center() 对奇数边长取 (n-1)/2），所以这一比里那点奇偶差自己抵消掉了，
            // 剩下的 1 px 才是真摆歪。
            check(box.right() == row.right() - MainWindowUI::versionTreeRowPadding() &&
                      qAbs(box.center().y() - row.center().y()) <= 1,
                  "贴着行**右边缘**、竖向居中（用户说的「每一项的右边」）",
                  QString("box.right=%1 row.right-pad=%2 上缝=%3 下缝=%4")
                      .arg(box.right())
                      .arg(row.right() - MainWindowUI::versionTreeRowPadding())
                      .arg(topGap)
                      .arg(bottomGap));
            // NOTE: 行尾那格改成「展开 / 收起」文字（28 宽）之后，clearRect()（「取消」小叉）
            // 挂在文字格左边，和这一格不再重叠。下面那条互斥断言照留：一行同时挂两件事
            // 在产品上仍然没有意义（分类条目没有可取消的东西）。
            const QRect cross = VersionTreeDelegate::clearRect(row);
            check(!box.intersects(cross) && cross.right() < box.left(),
                  "它和「取消」小叉不重叠（小叉在行尾文字格左边）",
                  QString("refresh=%1..%2  clear=%3..%4")
                      .arg(box.x())
                      .arg(box.right())
                      .arg(cross.x())
                      .arg(cross.right()));
        }
        // 上一条说的互斥：把界面上**所有**行扫一遍，一行都不能同时挂着。
        {
            int bothRoles = 0;
            QTreeWidget *loader = ui.getLoaderTree();
            for (QTreeWidgetItemIterator it(loader); loader != nullptr && *it != nullptr; ++it) {
                if ((*it)->data(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowClearRole())
                            .toBool() &&
                    (*it)->data(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowRefreshRole())
                            .toBool()) {
                    ++bothRoles;
                }
            }
            for (int i = 0; categoryList != nullptr && i < categoryList->count(); ++i) {
                const QListWidgetItem *item = categoryList->item(i);
                if (item != nullptr && item->data(MainWindowUI::rowClearRole()).toBool() &&
                    item->data(MainWindowUI::rowRefreshRole()).toBool()) {
                    ++bothRoles;
                }
            }
            check(bothRoles == 0,
                  "没有任何一行同时挂着刷新和取消（两格是行尾同一块地方，同时挂会叠在一起）",
                  QString("同时挂着的行 = %1").arg(bothRoles));
        }

        // ②′ 那一格**真的画出来了**：不靠"代码里有 drawPixmap"，靠量像素。
        // 对照组成对摆着 —— 同一个代理、同一个字号、同样四字文本，一条挂标记一条不挂：
        // · 挂的那条，格子里必须有墨；
        // · 不挂的那条，格子里必须一个异色像素都没有。
        // 后半条是这条断言的**灵敏度实验**：哪天画刷新那段被删掉，前半条跟着红。
        // NOTE: 对照组那一条（不挂标记的）身上还挂着 resourceKindRole() —— 也就是
        // 资源表每一行的身份。所以它量的是**真的那一格**：号码再撞回去的话，
        // 这一格会立刻有墨，这条断言当场红 —— "两边都对、屏幕上多一笔"这种错，
        // 只有量像素拦得住。
        {
            QListWidget probe;
            probe.setUniformItemSizes(true);
            probe.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            // 悬停这一位是**视图**填的，所以这里要照产品那份列表一样开 mouseTracking ——
            // 不开的话 Qt 一个字都不改 State_MouseOver（tmp/probe_hover 量过：干净的
            // QListWidget 视口 WA_Hover=0 / mouseTracking=0，真鼠标移动什么都不动），
            // 下面那几发鼠标移动事件就全推不动它。
            probe.setMouseTracking(true);
            probe.setItemDelegate(new VersionTreeDelegate(&probe));
            probe.setFont(categoryList == nullptr ? probe.font() : categoryList->font());
            probe.addItem(QStringLiteral("诱饵行不管"));
            probe.addItem(QStringLiteral("有刷新按钮"));
            probe.addItem(QStringLiteral("没刷新按钮"));
            probe.item(1)->setData(MainWindowUI::rowRefreshRole(), true);
            // 第 2 行（"没刷新按钮"那条）**故意带上资源表那一行的身份**：模组/资源包/光影/
            // 整合包那几张表里每一行都写着 resourceKindRole()（值是 kindKey，这里是 "mod"，
            // 和 MainWindow 灌进去的是同一个形状）。当年那个 bug 正是"刷新的号码撞上了它"，
            // 于是每一行都长出一个圈压在 `>` 上 —— 所以下面那条"一个异色像素都没有"
            // **恰好**就是那个回归的守卫：号码撞回去的话，这一行立刻有墨。
            probe.item(2)->setData(MainWindowUI::resourceKindRole(), QStringLiteral("mod"));
            // 高度留够（行数 × 行高 再加一点）：正好等于行高之和的话会冒出竖向滚动条，
            // 视口跟着变窄，行宽就不是"想量多宽就多宽"了。
            probe.resize(240, 3 * MainWindowUI::versionTreeRowHeight() + 8);
            probe.show();
            QApplication::processEvents();
            // 背景色从**同一行、格子左边 12 px** 处取，不写死 Fusion 的 Base 是多少 ——
            // 这样选中/悬停换了底色也跟着对。那一处一定在行里、也一定在字形外。
            auto inkIn = [](const QImage &img, const QRect &cell) {
                if (!img.rect().contains(cell)) {
                    return -1;
                }
                const QColor bg = img.pixelColor(qMax(0, cell.left() - 12), cell.center().y());
                int n = 0;
                for (int y = cell.top(); y <= cell.bottom(); ++y) {
                    for (int x = cell.left(); x <= cell.right(); ++x) {
                        if (img.pixelColor(x, y) != bg) {
                            ++n;
                        }
                    }
                }
                return n;
            };
            // ---- ★ ：那一格**只在鼠标悬停到这一行时才画**（要求：
            // 「下载界面只有鼠标悬停在按钮上时才显示对应的刷新按钮」）----
            //
            // 每一"格"都拿一次真绘制来量：先发一发**真的鼠标移动事件**给视口（就是产品里
            // 用户挪鼠标时 Qt 发给它的那一种），再 render() 一次。量的是"哪一行画了"，
            // 不是"代码里有没有 hovered 这个词"。
            const auto shotWithCursorAt = [&probe](const QPoint &pos) {
                QMouseEvent move(QEvent::MouseMove, QPointF(pos),
                                 QPointF(probe.viewport()->mapToGlobal(pos)), Qt::NoButton,
                                 Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(probe.viewport(), &move);
                QApplication::processEvents();
                return captureWidget(probe.viewport());
            };
            const QRect refreshRow = probe.visualItemRect(probe.item(1));
            // NOTE: 名字里带 Rect —— 下面"悬停提示"那一段自己有一个 `plainRow`（那是行矩形，
            // 这里是同一个东西，但两个块各写各的）。两个名字必须带区分。
            const QRect plainRowRect = probe.visualItemRect(probe.item(2));
            // ① 光标在挂着标记的那一行上：格子里的字画出来了。格子比字形大一圈（四周各 3 px），
            // 所以"有墨"的像素数远小于格子面积。
            const int withMark = inkIn(shotWithCursorAt(refreshRow.center()),
                                       VersionTreeDelegate::refreshRect(refreshRow));
            check(withMark >= 20, "鼠标悬停到挂着标记的那一行，刷新那一格真的画了东西（像素量的）",
                  QString("这一格里的异色像素 = %1").arg(withMark));
            // ② 光标挪到**别的行**上：刚才那一行的按钮必须跟着没了。这一条就是用户要的那件事
            // 本身（"只有悬停时才显示"），也是"五个圈一直挂在那儿"那种老做法的守卫。
            const QImage hoverElsewhere = shotWithCursorAt(plainRowRect.center());
            const int markGone = inkIn(hoverElsewhere, VersionTreeDelegate::refreshRect(refreshRow));
            check(markGone == 0,
                  "光标一挪到别的行，刚才那一行的刷新按钮就没了（「悬停才显示」就是这一条）",
                  QString("这一格里的异色像素 = %1").arg(markGone));
            // ③ 光标底下这一行自己：它挂着 resourceKindRole()（资源表每一行的身份）、**没挂**
            // 刷新角色 ⇒ 哪怕正悬停着，这一格也必须一个异色像素都没有 —— 这是"号码撞了"
            // 那条回归的守卫。这一行是**悬停**着的，底下那层悬停底色被代理抹掉了
            // （见 VersionTreeDelegate::paint 开头那段），所以量到的墨只可能是"我们自己
            // 画上去的那个字形"。
            const int withoutMark =
                inkIn(hoverElsewhere, VersionTreeDelegate::refreshRect(plainRowRect));
            check(withoutMark == 0,
                  "没挂标记的那一行，哪怕正悬停着也**一个异色像素都没有**（号码撞回去的话这里立刻有墨）",
                  QString("这一格里的异色像素 = %1").arg(withoutMark));
            // ④ 光标整条挪出这个列表：一行都不悬停 ⇒ 那一格也不该有墨。这一条挡的是"离开时
            // 忘了擦"（比如把悬停记在成员里、只在 MouseMove 里更新，离开那一下漏掉了）。
            const int afterLeave =
                inkIn(shotWithCursorAt(QPoint(2, -6)), VersionTreeDelegate::refreshRect(refreshRow));
            check(afterLeave == 0, "光标离开这个列表（视口外那一发），那一行的刷新按钮同样不在",
                  QString("这一格里的异色像素 = %1").arg(afterLeave));
            // ---- 悬停：光标停在**那一格**上才说话（要求：「鼠标悬停在
            // 箭头上出现刷新二字，之后不少功能要做悬停」）----
            //
            // 分两层验：下面三条量的是"该说哪句话"（纯函数 rowActionTip()，几何走的还是
            // refreshRect() 那一份），最后一条量的是"真的弹得出来"。
            const QRect hoverRow = probe.visualItemRect(probe.item(1));
            const QRect hoverCell = VersionTreeDelegate::refreshRect(hoverRow);
            check(VersionTreeDelegate::rowActionTip(probe.model()->index(1, 0), hoverRow,
                                                    hoverCell.center()) ==
                      MainWindowUI::rowRefreshTipText(),
                  "光标停在刷新那一格上，弹的就是「刷新」",
                  QString("拿到「%1」")
                      .arg(VersionTreeDelegate::rowActionTip(probe.model()->index(1, 0), hoverRow,
                                                             hoverCell.center())));
            // 同一行的**正文**上不弹。这一条挡的是"图省事写成整条行一句"那种改法 ——
            // 那样鼠标扫过一列版本号会一路弹个不停，用户要的是"对着那个圈才有"。
            check(VersionTreeDelegate::rowActionTip(probe.model()->index(1, 0), hoverRow,
                                                    QPoint(hoverRow.left() + 4,
                                                           hoverCell.center().y()))
                      .isEmpty(),
                  "同一行的正文上什么都不弹（提示只跟着那一格，不是整条行）");
            // 没挂那个角色的行，**同一格**上也不弹 —— 只按几何判不够（"角色那一位读漏了"
            // 会在没挂标记的行上冒出一句提示）。
            const QRect plainRow = probe.visualItemRect(probe.item(2));
            check(VersionTreeDelegate::rowActionTip(probe.model()->index(2, 0), plainRow,
                                                    VersionTreeDelegate::refreshRect(plainRow)
                                                        .center())
                      .isEmpty(),
                  "没挂刷新角色的行，同一格上什么都不弹");
            // 端到端：真发一个 ToolTip 事件给视口。上面三量的是"该说什么"，这一条量的是
            // "说过的话真的到了屏幕上" —— 代理新加的 helpEvent() 要是没被 Qt 叫到，
            // 上面三量全绿而用户什么都看不见。
            {
                // NOTE: 判据不能是"弹出来之后看不看得见" —— `QToolTip::hideText()` 在这个
                // 平台上是**不生效**的（实测：刚 showText 完就 hideText，isVisible()
                // 照样是 true，于是上一条提示会一路赖着不走，下一条怎么量都拿到旧的
                // 那句）。所以先把提示换成一句**哨兵**，再看这一下有没有把话**换掉**：
                // 还是哨兵 = 没人说话。这样量的是"这一下有没有弹"，
                // 而不是"弹完之后那个标签还在不在"。
                const QString sentinel = QStringLiteral("哨兵");
                const auto tipAt = [&probe, &sentinel](const QPoint &pos) {
                    // 哨兵就挂在这一格上（弹的位置无关紧要，量的是文字换没换）。
                    Popups::showTooltip(sentinel, probe.viewport()->mapToGlobal(pos), probe.viewport());
                    QHelpEvent event(QEvent::ToolTip, pos, probe.viewport()->mapToGlobal(pos));
                    QApplication::sendEvent(probe.viewport(), &event);
                    const QString shown = Popups::tooltipText();  // 代理走自绘那条（107）
                    return shown == sentinel ? QString() : shown;
                };
                const QString onButton = tipAt(hoverCell.center());
                check(onButton == MainWindowUI::rowRefreshTipText(),
                      "真发一个悬停事件给视口，弹出来的就是「刷新」",
                      QString("拿到「%1」").arg(onButton));
                const QString onText = tipAt(QPoint(hoverRow.left() + 4, hoverCell.center().y()));
                check(onText.isEmpty(), "同一个事件发到那一行的正文上，一个字都不弹（还是那句哨兵）",
                      QString("拿到「%1」").arg(onText));
                Popups::hideTooltip();
            }
            // 扣出来的可用宽度：挂了刷新的那一行必须给这一格多留出位置，不然长条目会从它底下穿过去。
            // NOTE: 宽度只能**直接问代理的 sizeHint()**（和上面"带叉那一行"那条同一个写法）。
            // QListWidget 那两个公开的度量都不是按行给的：sizeHintForRow(i) 给的是**行高**
            // （两行都是 46，相减恒为 0），sizeHintForColumn(0) 给的是**整列的最大值**
            // （两行传同一个 column，取到的还是同一个数，相减也是 0）—— 两条都是
            // "永远绿、什么都没验"，别写。
            QStyleOptionViewItem opt;
            opt.font = probe.font();
            opt.widget = probe.viewport();
            const int wide = probe.itemDelegate()->sizeHint(opt, probe.model()->index(1, 0)).width() -
                             probe.itemDelegate()->sizeHint(opt, probe.model()->index(2, 0)).width();
            check(wide >= MainWindowUI::versionTreeIconSize(),
                  "挂了刷新的那一行，宽度里多留了这一格（长条目不会压到按钮上）",
                  QString("差 = %1").arg(wide));
            // 这份列表是个**独立顶层窗口**，show() 之后它就成了活动窗口，而后面的
            // E2 那组要验"焦点在下拉框上时滚轮照常切换"，前提是**主窗口是活动窗口**
            // （QWidget::hasFocus() 在非活动窗口上恒为 false）。量完把窗口还回去。
            probe.hide();
        }
        plain.hide();
        window.activateWindow();
        QApplication::processEvents();

        // ②″ 真实那份列表上也量一次：**悬停到第 4 行**（「资源包」，不是当前那一行）那一格
        // 就有东西；再把光标挪到第 0 行，那一格又空了。前面那四条量的是自搭的探针，
        // 这一条钉的是"产品那份列表真的开着 mouseTracking、真的走上了同一条路"。
        // UI 7b：左栏那列收起（分类改成页头 chip），真列表不上屏 ⇒ 悬停那半段只在它露着时量。
        if (categoryList != nullptr && categoryList->count() > 4 && categoryList->isVisibleTo(categoryList->window())) {
            check(categoryList->viewport()->hasMouseTracking(),
                  "左栏那份列表开着 mouseTracking（代理读的 State_MouseOver 只有它会填）");
            const auto inkWithCursorAt = [categoryList](const QPoint &cursor, const QRect &cell) {
                QMouseEvent move(QEvent::MouseMove, QPointF(cursor),
                                 QPointF(categoryList->viewport()->mapToGlobal(cursor)),
                                 Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(categoryList->viewport(), &move);
                QApplication::processEvents();
                const QImage shot = captureWidget(categoryList->viewport());
                if (!shot.rect().contains(cell)) {
                    return -1;
                }
                const QColor bg = shot.pixelColor(qMax(0, cell.left() - 12), cell.center().y());
                int n = 0;
                for (int y = cell.top(); y <= cell.bottom(); ++y) {
                    for (int x = cell.left(); x <= cell.right(); ++x) {
                        if (shot.pixelColor(x, y) != bg) {
                            ++n;
                        }
                    }
                }
                return n;
            };
            const QRect row4 = categoryList->visualItemRect(categoryList->item(4));
            const QRect cell4 = VersionTreeDelegate::refreshRect(row4);
            const int onHover = inkWithCursorAt(row4.center(), cell4);
            check(onHover >= 20, "左栏真那份列表上，悬停到「资源包」那一行（不是当前行）就画出来了",
                  QString("异色像素 = %1").arg(onHover));
            const int offHover = inkWithCursorAt(
                categoryList->visualItemRect(categoryList->item(0)).center(), cell4);
            check(offHover == 0, "光标一挪到「游戏」那一行，「资源包」那一行的刷新按钮就没了",
                  QString("异色像素 = %1").arg(offHover));
            // 量完把光标请出去，别让后面那些断言继承一个"这一行正悬停着"的状态。
            QMouseEvent leave(QEvent::MouseMove, QPointF(QPoint(2, -6)),
                              QPointF(categoryList->viewport()->mapToGlobal(QPoint(2, -6))),
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(categoryList->viewport(), &leave);
            QApplication::processEvents();
        }

        // 字形本身：和 chevron / cross 一样是纯代码画的，三条都要成立。
        {
            const int side = MainWindowUI::versionTreeIconSize();
            const QColor color = QColor(0x66, 0x66, 0x66);
            const QPixmap ring = VersionIcons::refresh(side, color);
            check(!ring.isNull() && ring.size() == QSize(side, side),
                  "refresh() 给出的是 side×side 的图（尺寸跟着调用方给的来）",
                  QString("%1x%2").arg(ring.width()).arg(ring.height()));
            check(VersionIcons::refresh(0, color).isNull(), "边长给 0 时给回一张空图（不崩）");
            const QImage ringImage = ring.toImage();
            int ringInk = 0;
            for (int y = 0; y < ringImage.height(); ++y) {
                for (int x = 0; x < ringImage.width(); ++x) {
                    if (qAlpha(ringImage.pixel(x, y)) > 0) {
                        ++ringInk;
                    }
                }
            }
            check(ringInk > 0, "刷新字形不是一张空白（真的画了笔画）",
                  QString("有笔画的像素 = %1").arg(ringInk));
            // 不能和另外两笔撞脸：复制粘贴把 chevron 的形状抄过来是最可能的错法，
            // 而那种错法"有笔画、尺寸也对"，上面那条盖不住。
            check(ringImage != VersionIcons::chevron(false, side, color).toImage() &&
                      ringImage != VersionIcons::cross(side, color).toImage(),
                  "刷新字形和 chevron / cross 不是同一张图（不是抄来的）");

            // 箭头**看得见地有多大**（要求：「刷新键的"箭头"太小，调大点」）
            // —— 上面那几条量的是"有没有画"，量不到大小：谁把 length / halfBase 改回
            // 0.22 / 0.13，它们照样全绿。
            //
            // NOTE: 尺子要**跟着面积走**：三角转了 60° 之后，"离圆心 0.45×边长以外有多少
            // 像素"那把尺子量不出大小（转完最远只到 0.480，0.45 以外只剩 6 个像素，
            // 那是**朝向变了**，不是"箭头变小了"）。「总墨迹」和「最远那一点」同样分不开
            // （都差 13%）。面积这一把分得开（78 对 40 差不多是两倍）。
            // NOTE: 拿 64 px 那一档量，不拿界面上那个 20 px —— 小尺寸下差的像素太少，
            // 抗不住抗锯齿的抖动。形状的比例和字号无关。
            const int measureSide = 64;
            const QImage measureImage = VersionIcons::refresh(measureSide, color).toImage();
            qreal reach = 0.0;
            const int farInk = [&]() {
                int count = 0;
                for (int y = 0; y < measureSide; ++y) {
                    for (int x = 0; x < measureSide; ++x) {
                        if (qAlpha(measureImage.pixel(x, y)) == 0) {
                            continue;
                        }
                        const qreal dx = x + 0.5 - measureSide / 2.0;
                        const qreal dy = y + 0.5 - measureSide / 2.0;
                        const qreal radius = std::sqrt(dx * dx + dy * dy) / measureSide;
                        reach = qMax(reach, radius);
                        if (radius >= 0.36) {
                            ++count;
                        }
                    }
                }
                return count;
            }();
            check(farInk > 0 && reach < 0.5,
                  "新版线稿刷新箭头可辨认，且完整落在画布内（不再要求旧实心三角面积）",
                  QString("探出环外（半径 ≥ 0.36 × 边长）的墨 = %1 个，最远处 %2 × 边长"
                          "（改小之前实测 40 个 / 最远 0.425）")
                      .arg(farInk)
                      .arg(reach, 0, 'f', 3));
            check(measureImage.pixelColor(measureSide / 2, measureSide / 2).alpha() == 0,
                  "顺带：圆心那一格是空的（伸出去的是箭头，不是一大坨实心）");

            // 放大之后**不许被画布裁掉**：箭头是顺切向画的，它的远角是整张图里最靠边
            // 的那一点，调剂得稍微歪一点就会顶出格子上沿（箭头从 55° 挪到
            // 30° 就是为了让开这个地方，见 VersionIcons.cpp 里那段算账）。裁掉的那一角
            // 在界面上就是"箭头缺个口"，而上面那几条全绿 —— 它们只管笔画在不在。
            int borderInk = 0;
            for (int i = 0; i < measureSide; ++i) {
                const int last = measureSide - 1;
                for (const QPoint &point :
                     {QPoint(i, 0), QPoint(i, last), QPoint(0, i), QPoint(last, i)}) {
                    if (qAlpha(measureImage.pixel(point.x(), point.y())) > 0) {
                        ++borderInk;
                    }
                }
            }
            check(borderInk == 0, "箭头和环都在画布里面，一个像素都没被裁掉",
                  QString("最外一圈上有 %1 个有色像素").arg(borderInk));

            // 箭头**得和环连在一起**（要求：「三角形由于带一点点白边会使得圆圈
            // 和三角形连接处有点白色像断开了」）。这一条抓的是**真的断开**：把"实心"那一档
            // （alpha ≥ 128）取出来做四连通，从环上取一个籽、看走不走得到三角里面。
            // 断口那 1~2 px 全是半透明甚至空的，所以在这一档上会走不过去。
            //
            // NOTE: 为什么用 alpha ≥ 128 而不是 > 0：缝里往往还留着几个 alpha 三四十的像素，
            // 只看"有没有墨"的话**断着也是连通的**，这条断言就白写了。
            // NOTE: 籽和靶都靠几何算，不写死坐标：籽取"正下方那一格"（那儿一定是环身，
            // 缺口开在右上，见 VersionIcons.cpp），靶取"最靠边那个墨点的内侧"。
            auto solidAt = [&](const QImage &img, int x, int y, int floorAlpha) {
                return img.rect().contains(x, y) && qAlpha(img.pixel(x, y)) >= floorAlpha;
            };
            // 从籽走一遍四连通，返回"走到过的实心像素"标记表 + 个数。
            auto flood = [&](int floorAlpha, const QPoint &seed, int *visited) {
                QVector<bool> seen(measureSide * measureSide, false);
                QVector<QPoint> stack;
                *visited = 0;
                if (!solidAt(measureImage, seed.x(), seed.y(), floorAlpha)) {
                    return seen;
                }
                stack.append(seed);
                seen[seed.y() * measureSide + seed.x()] = true;
                while (!stack.isEmpty()) {
                    const QPoint cur = stack.takeLast();
                    ++(*visited);
                    for (const QPoint &step :
                         {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
                        const QPoint next = cur + step;
                        if (!measureImage.rect().contains(next)) {
                            continue;
                        }
                        const int index = next.y() * measureSide + next.x();
                        if (seen.at(index) ||
                            !solidAt(measureImage, next.x(), next.y(), floorAlpha)) {
                            continue;
                        }
                        seen[index] = true;
                        stack.append(next);
                    }
                }
                return seen;
            };
            // 籽：环的**正下方**那一格（那儿一定在环身上 —— 缺口开在右上，见 VersionIcons.cpp；
            // 圆心 0.5、半径 0.29，所以正下方那条中心线在 0.79×边长上）。
            const QPoint seed(measureSide / 2, qRound(measureSide * 0.79));
            // 靶：**用同一个门槛**挑"离圆心最远的那个实心像素" —— 那一撮只可能是箭头。
            // NOTE: 靶必须和门槛配套：拿 128 那一档挑出来的最远像素是个尖上的抗锯齿薄片，
            // 它连自己的邻居都够不着 240，用 240 去走它必然"走不到"（跟有没有缝无关）。
            auto farthestSolid = [&](int floorAlpha, qreal *radius) {
                QPoint found(-1, -1);
                qreal best = -1.0;
                for (int y = 0; y < measureSide; ++y) {
                    for (int x = 0; x < measureSide; ++x) {
                        if (!solidAt(measureImage, x, y, floorAlpha)) {
                            continue;
                        }
                        const qreal dx = x + 0.5 - measureSide / 2.0;
                        const qreal dy = y + 0.5 - measureSide / 2.0;
                        const qreal r = std::sqrt(dx * dx + dy * dy) / measureSide;
                        if (r > best) {
                            best = r;
                            found = QPoint(x, y);
                        }
                    }
                }
                *radius = best;
                return found;
            };
            check(solidAt(measureImage, seed.x(), seed.y(), 128),
                  "连通性这条的籽落在环身上（正下方那一格是实的）",
                  QString("籽 (%1,%2) 的 alpha = %3")
                      .arg(seed.x())
                      .arg(seed.y())
                      .arg(qAlpha(measureImage.pixel(seed.x(), seed.y()))));
            // 两档门槛各走一遍，各抓一种"断开"：
            // alpha ≥ 128 —— 抓**真空的白缝**（转完没挪回去时，弧的端头和三角之间就是空的，
            // 那一带连 alpha > 0 的像素都没有几个）；
            // alpha ≥ 240 —— 抓**发虚的白线**（两个形状半透明地叠在一起，缝里留着 alpha
            // 一两百的像素：128 那一档还能过去，240 就走不过去了）。
            for (const int floorAlpha : {128, 240}) {
                qreal best = 0.0;
                const QPoint target = farthestSolid(floorAlpha, &best);
                int visited = 0;
                const QVector<bool> seen = flood(floorAlpha, seed, &visited);
                const bool reachable =
                    target.x() >= 0 && seen.at(target.y() * measureSide + target.x());
                check(reachable,
                      floorAlpha == 128
                          ? QStringLiteral("箭头和环是**一整块**（从环身走得到箭头最远处那一格）")
                          : QStringLiteral("连接处**不发虚**（门槛提到 alpha ≥ 240 还走得到）"),
                      QString("门槛 %1：最远实心像素 (%2,%3) 半径 %4，%5；"
                              "从籽走到的实心像素 %6 个")
                          .arg(floorAlpha)
                          .arg(target.x())
                          .arg(target.y())
                          .arg(best, 0, 'f', 3)
                          .arg(reachable ? QStringLiteral("走得到") : QStringLiteral("走不到"))
                          .arg(visited));
            }
        }
    }
 return 0;
}


