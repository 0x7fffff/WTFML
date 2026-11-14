#include "Support.hpp"
#include "Popups.hpp"
int UiVersionTreeGeometryChecks(QMainWindow &window, QTreeWidget * &versionTree) {


    {
        const int rowHeight = MainWindowUI::versionTreeRowHeight();
        const int iconSize = MainWindowUI::versionTreeIconSize();
        const int arrowSize = MainWindowUI::versionTreeArrowSize();
        const int pad = MainWindowUI::versionTreeRowPadding();

        QTreeWidget probe;
        probe.setColumnCount(1);
        probe.setHeaderHidden(true);
        probe.setUniformRowHeights(true);
        probe.setExpandsOnDoubleClick(false);
        if (versionTree != nullptr) {
            probe.setFont(versionTree->font());  // 字号照产品那棵，量的才是产品那份
        }
        probe.setItemDelegate(new VersionTreeDelegate(&probe));
        auto *channel = new QTreeWidgetItem();
        channel->setText(MainWindowUI::versionTreeKeyColumn(), QStringLiteral("正式版"));
        auto *leaf = new QTreeWidgetItem(channel);
        leaf->setText(MainWindowUI::versionTreeKeyColumn(), QStringLiteral("1.20.1"));
        leaf->setData(MainWindowUI::versionTreeKeyColumn(), MainWindowUI::rowSubTextRole(),
                      QStringLiteral("2023-06-12"));
        auto *emptyChannel = new QTreeWidgetItem();  // 一条版本都没有的分类
        emptyChannel->setText(MainWindowUI::versionTreeKeyColumn(), QStringLiteral("愚人节"));
        // 左边那个系统三角不画 —— 和产品那边摆成同一个形状，不然量出来的行不是产品那个行。
        //
        // NOTE: 这里**不能**用 setChildIndicatorPolicy(DontShowIndicator)：QTreeModel::
        // hasChildren() 是按那个 policy 直接 return false 的，子条目**一条都排不进
        // 布局**（产品那边写成这样过：树上只剩四行空的分类）**。
        // 要去掉那个三角就走 rootIsDecorated(false) —— 它只影响装饰格的绘制。
        probe.setRootIsDecorated(false);
        // 窗口开得比三行高一截：不留竖滚动条，行宽就等于视口宽，下面算右端那格才对得上。
        probe.resize(420, rowHeight * 3 + 60);
        probe.show();
        QApplication::processEvents();
        probe.addTopLevelItem(channel);
        probe.addTopLevelItem(emptyChannel);
        QApplication::processEvents();
        probe.expandItem(channel);
        QApplication::processEvents();

        const QColor bg = probe.viewport()->palette().color(QPalette::Base);
        const QImage shot = captureWidget(probe.viewport());  // 展开着（版本号那些行看得见）
        const int vw = shot.width();
        const auto rowRect = [&](int r) {
            return QRect(0, r * rowHeight, vw, rowHeight);
        };
        const auto inkCount = [&bg](const QImage &img, const QRect &box) {
            int n = 0;
            for (int y = qMax(0, box.top()); y <= qMin(img.height() - 1, box.bottom()); ++y) {
                for (int x = qMax(0, box.left()); x <= qMin(img.width() - 1, box.right()); ++x) {
                    if (img.pixelColor(x, y) != bg) {
                        ++n;
                    }
                }
            }
            return n;
        };
        const auto inkBBox = [&bg](const QImage &img, const QRect &box) {
            QRect bb;
            for (int y = qMax(0, box.top()); y <= qMin(img.height() - 1, box.bottom()); ++y) {
                for (int x = qMax(0, box.left()); x <= qMin(img.width() - 1, box.right()); ++x) {
                    if (img.pixelColor(x, y) != bg) {
                        bb = bb.isNull() ? QRect(x, y, 1, 1) : bb.united(QRect(x, y, 1, 1));
                    }
                }
            }
            return bb;
        };
        // 行尾那格文字（「展开 / 收起」或悬停动词）：代理把它画在 row.right() - pad - trail 起、
        // 宽 trail 的那一列里（体验反馈 8 之后不再画 12×12 的 > / V）。
        const int trail = MainWindowUI::versionTreeTrailWidth();
        const auto arrowBox = [&](int r) {
            return QRect(vw - pad - trail, rowRect(r).top() + 2, trail, rowHeight - 4);
        };

        check(shot.width() > 200 && shot.height() >= rowHeight * 3,
              "小树上三行都画出来了（够宽够高，下面按像素认）",
              QStringLiteral("%1x%2").arg(shot.width()).arg(shot.height()));

        // ① 版本号前面那个草方块。
        // NOTE: 不写死 x —— 子条目左边缩进多少是样式说了算（缩进的宽窄和笔者的
        // 猜测无关），写死就会量到空白上、然后报"没画"。所以这里**按颜色找**：
        // 草是绿的、土是棕的，找得到而且"绿在上、棕在下"才算画对了。
        const QRect leafRow = rowRect(1);
        int greenCount = 0;
        int brownCount = 0;
        qint64 greenYSum = 0;
        qint64 brownYSum = 0;
        int iconLeft = INT_MAX;
        int iconRight = -1;
        const auto isGrass = [](const QColor &c) {
            return c.green() > c.red() && c.green() > c.blue() + 15 && c.green() > 90;
        };
        const auto isDirt = [](const QColor &c) {
            return c.red() > c.green() && c.green() > c.blue() + 5 && c.red() > 90;
        };
        for (int y = leafRow.top(); y <= leafRow.bottom(); ++y) {
            for (int x = leafRow.left(); x <= leafRow.right(); ++x) {
                const QColor c = shot.pixelColor(x, y);
                if (isGrass(c)) {
                    ++greenCount;
                    greenYSum += y;
                    iconLeft = qMin(iconLeft, x);
                    iconRight = qMax(iconRight, x);
                } else if (isDirt(c)) {
                    ++brownCount;
                    brownYSum += y;
                    iconLeft = qMin(iconLeft, x);
                    iconRight = qMax(iconRight, x);
                }
            }
        }
        check(greenCount > 8 && brownCount > 8,
              "版本号前面那个草方块画出来了（一行里找得到绿的、也找得到棕的）",
              QStringLiteral("绿 %1 点 / 棕 %2 点").arg(greenCount).arg(brownCount));
        check(greenCount > 0 && brownCount > 0 &&
                  greenYSum / qMax(1, greenCount) < brownYSum / qMax(1, brownCount),
              "草方块上半截是草、下半截是土（绿的那半边在上面）",
              QStringLiteral("绿 y≈%1 / 棕 y≈%2")
                  .arg(greenYSum / qMax(1, greenCount))
                  .arg(brownYSum / qMax(1, brownCount)));
        const QRect iconBox(iconLeft - 2, leafRow.top(), qMax(1, iconRight - iconLeft + 5),
                            leafRow.height());
        check(!iconBox.isNull() && iconBox.width() <= iconSize + 6,
              "那个方块只有图标那么大（不是横贯一行的一条绿带）",
              QStringLiteral("%1 px 宽，图标 %2").arg(iconBox.width()).arg(iconSize));

        // ② 版本号一行、发布时间一行 —— 分界线就是行的中线：上半截那行是版本号、
        // 下半截那行是日期。用户要的是"在版本号下面小字写发布时间"。
        // 从图标右边一点开始量，免得把草方块自己的像素算成"字"。
        // 找不到那个方块时（扫不到绿/棕）退回"行左端 + 一个图标位"起的文字区 ——
        // 不然下面那几个框会算到图外面去（box.left() 是个越界的数，循环直接跑飞）。
        const int textLeft =
            iconRight >= 0 ? iconBox.right() + 2 : leafRow.left() + pad + iconSize + 8;
        const int textRight = vw - pad - arrowSize - 6;
        const QRect stripAbove(textLeft, leafRow.top() + 2, qMax(1, textRight - textLeft),
                               leafRow.height() / 2 - 3);
        const QRect stripBelow(textLeft, leafRow.center().y() + 1, qMax(1, textRight - textLeft),
                               leafRow.height() / 2 - 3);
        const int inkAbove = inkCount(shot, stripAbove);
        const int inkBelow = inkCount(shot, stripBelow);
        check(inkAbove > 5, "版本号在草方块右边、上半截那行（那一带有字）", QString::number(inkAbove));
        check(inkBelow > 5, "版本号下面那行也有字（发布时间在下半截）", QString::number(inkBelow));
        // 「小字」的那层意思：下面那行比上面那行浅（占位色），不然两行一样黑就分不出主次。
        const auto darkestIn = [&shot, &bg](const QRect &box) {
            int darkest = 255;
            for (int y = qMax(0, box.top()); y <= qMin(shot.height() - 1, box.bottom()); ++y) {
                for (int x = qMax(0, box.left()); x <= qMin(shot.width() - 1, box.right()); ++x) {
                    const QColor c = shot.pixelColor(x, y);
                    if (c != bg) {
                        darkest = qMin(darkest, qGray(c.rgb()));
                    }
                }
            }
            return darkest;
        };
        const int darkestAbove = darkestIn(stripAbove);
        const int darkestBelow = darkestIn(stripBelow);
        check(darkestBelow > darkestAbove,
              "下面那行比上面那行浅（是「小字」，不是第二行正文）",
              QStringLiteral("%1 vs %2").arg(darkestBelow).arg(darkestAbove));
        // 两行左对齐（同一列的版本号，日期不该跑到别处去）。取两行各自最左边那个墨点比。
        const auto leftmostInk = [&shot, &bg](const QRect &box) {
            int leftmost = INT_MAX;
            for (int y = qMax(0, box.top()); y <= qMin(shot.height() - 1, box.bottom()); ++y) {
                for (int x = qMax(0, box.left()); x <= qMin(shot.width() - 1, box.right()); ++x) {
                    if (shot.pixelColor(x, y) != bg) {
                        leftmost = qMin(leftmost, x);
                    }
                }
            }
            return leftmost;
        };
        const int dateLeft = leftmostInk(stripBelow);
        check(qAbs(dateLeft - leftmostInk(stripAbove)) <= 2,
              "下面那行日期和上面那行版本号左对齐（都在方块右边那一列）",
              QStringLiteral("%1 vs %2").arg(dateLeft).arg(leftmostInk(stripAbove)));

        // ③ 行尾不画箭头，写字（tokens-r2 §6）：能展开的分类写「收起 / 展开」，叶子行
        // 只在悬停时写动词（这张图里没有悬停 ⇒ 叶子那一格是空的）。
        check(inkCount(shot, leafRow) > 0, "版本号那种叶子行整行有内容");
        check(inkCount(shot, arrowBox(1)) == 0,
              "版本号那种叶子行不悬停时行尾是空的（不再挂一个朝右的 >）",
              QStringLiteral("墨点 %1").arg(inkCount(shot, arrowBox(1))));
        const QRect bbExpanded = inkBBox(shot, arrowBox(0));
        check(!bbExpanded.isNull() && bbExpanded.width() > arrowSize + 6,
              "展开着的分类行尾是一行字（墨迹比 12 px 的箭头宽得多 —— 是「收起」两个字）",
              QStringLiteral("%1x%2").arg(bbExpanded.width()).arg(bbExpanded.height()));
        probe.collapseItem(channel);
        QApplication::processEvents();
        const QImage collapsedShot = captureWidget(probe.viewport());
        probe.expandItem(channel);
        QApplication::processEvents();
        const QRect bbCollapsed = inkBBox(collapsedShot, arrowBox(0));
        check(!bbCollapsed.isNull() && bbCollapsed.width() > arrowSize + 6,
              "收起之后同一格也是一行字（「展开」）",
              QStringLiteral("%1x%2").arg(bbCollapsed.width()).arg(bbCollapsed.height()));
        check(collapsedShot.copy(arrowBox(0)) != shot.copy(arrowBox(0)),
              "收起和展开那一格写的不是同一行字（「展开」↔「收起」真的换了）");

        // ④ 一个版本都没有的分类行尾什么都不写 —— 写了就骗人说点得开。
        check(inkCount(shot, arrowBox(2)) == 0,
              "一条版本都没有的分类行尾是空的（写了就骗人）",
              QString::number(inkCount(shot, arrowBox(2))));

        // ④b 换成**叶子不写动词**那个代理（安装页那棵加载器树用的就是它）再抓一张：
        // 顶层那行照旧写「收起」（那个开关没管到能展开的那一层）。
        probe.setItemDelegate(new VersionTreeDelegate(&probe, /*leafArrow=*/false));
        QApplication::processEvents();
        const QImage noLeafArrowShot = captureWidget(probe.viewport());
        check(inkCount(noLeafArrowShot, arrowBox(1)) == 0,
              "★ 叶子那一层行尾什么都不写（加载器树那层没有下一页）",
              QStringLiteral("墨点 %1").arg(inkCount(noLeafArrowShot, arrowBox(1))));
        check(inkCount(noLeafArrowShot, arrowBox(0)) > 0,
              "★ 顶层那一条照旧写「展开 / 收起」（它是真的能展开）",
              QStringLiteral("墨点 %1").arg(inkCount(noLeafArrowShot, arrowBox(0))));
        check(inkCount(noLeafArrowShot, leafRow) > 0,
              "叶子那一行别的内容一个没少",
              QString::number(inkCount(noLeafArrowShot, leafRow)));
        // 把代理换回**原来那个**再往下走 —— 下面 ⑤ 那一段量的就是同一个代理。
        probe.setItemDelegate(new VersionTreeDelegate(&probe, /*leafArrow=*/true));
        QApplication::processEvents();

        // ⑤ 不可用的那一条**整行淡下去**："选中一个加载器之后其余几条加载器
        // 变灰、也点不开"。淡那一下是**代理**画的（按条目的 enabled 位切透明度），跟
        // "点不动"是两件事 —— 不淡的话用户看不出来那条点不得，只会觉得"点一下没反应"
        // （"点不开了"正是这个形状）。
        // 判据："同一条内容，禁用之后那格里的墨**轻**了一截" —— 颜色朝背景淡过去，
        // 每个像素离背景的距离都变小，加起来（总墨量）就变小。量"离背景多远"而不是
        // "有多黑"，是为了不假设背景一定是白的。
        //
        // **量的是图标那一格**（iconBox 里是草方块）：草方块的颜色是 VersionIcons
        // 里写死的，跟调色板无关，它变浅只可能是我们在代理里调了透明度。
        // NOTE: 的灵敏度实验：把代理里那两处 setOpacity 摘掉，**整行**的墨量
        // 照样从 141024 掉到 95424 —— 因为禁用之后 Qt 自己会把文字换成"禁用档"
        // 的调色板颜色，那一层不管我们画不画都会变浅。整行那一把量不出我们的
        // 透明度（假绿），所以这里只把整行的数字写进明细里看，不当判据。
        const auto inkWeight = [&bg](const QImage &img, const QRect &box) {
            int total = 0;
            for (int y = qMax(0, box.top()); y <= qMin(img.height() - 1, box.bottom()); ++y) {
                for (int x = qMax(0, box.left()); x <= qMin(img.width() - 1, box.right()); ++x) {
                    const QColor c = img.pixelColor(x, y);
                    total += qAbs(c.red() - bg.red()) + qAbs(c.green() - bg.green()) +
                             qAbs(c.blue() - bg.blue());
                }
            }
            return total;
        };
        const int iconInkEnabled = inkWeight(shot, iconBox);
        const int rowInkEnabled = inkWeight(shot, leafRow);
        leaf->setDisabled(true);
        QApplication::processEvents();
        const QImage disabledShot = captureWidget(probe.viewport());
        const int iconInkDisabled = inkWeight(disabledShot, iconBox);
        const int rowInkDisabled = inkWeight(disabledShot, leafRow);
        leaf->setDisabled(false);
        QApplication::processEvents();
        // 0.5 的透明度 → 每个像素离背景的距离正好减半，所以这里卡 0.8 那一档：
        // 不调透明度时这个比值是 1.0（实验过），调了是 0.5 上下，中间留得开。
        check(iconInkEnabled > 0 && iconInkDisabled * 5 < iconInkEnabled * 4,
              "禁用那一条整行淡下去（点不动这件事得看得出来）",
              QStringLiteral("图标那格 %1 → %2（整行 %3 → %4，整行那把当参考、不当判据）")
                  .arg(iconInkEnabled)
                  .arg(iconInkDisabled)
                  .arg(rowInkEnabled)
                  .arg(rowInkDisabled));

        // ⑥ 行尾那个「取消」小叉（选中那条加载器右边、> 的左边）。要求："选中以后
        // 在那个 > 左侧加一个 x 来取消安装对应版本"。
        // 这一格是**代理自己画的**（VersionIcons::cross，代码画的，仓库里没有图片），
        // 位置由 VersionTreeDelegate::clearRect() 唯一给出 —— 产品那边"这一下点中了
        // 没有"也是按同一份几何判的（MainWindow::eventFilter），两边错开就成了"看着
        // 点中了叉、其实点的是那一行"。所以这里**用产品那一份**算，不自己再推一遍。
        const QRect clearBox = VersionTreeDelegate::clearRect(leafRow);
        check(!clearBox.isNull() && clearBox.right() < arrowBox(1).left() &&
                  clearBox.width() >= arrowSize,
              "那个叉算得出来、正好落在行尾文字左边那一格（和它不重叠）",
              QStringLiteral("%1x%2 @x=%3（箭头 @x=%4）")
                  .arg(clearBox.width())
                  .arg(clearBox.height())
                  .arg(clearBox.x())
                  .arg(arrowBox(1).x()));
        // 没打这个标记的行：那一格**一个墨点都没有**（不然就成了"每一行都带个叉"，
        // 而那个叉的意思是"取消掉这一条"，没有可取消的东西时它不该在）。
        check(inkCount(shot, clearBox) == 0,
              "没被选中的行那一格是空的（那个叉只在选中的那条加载器上出现）",
              QStringLiteral("墨点 %1").arg(inkCount(shot, clearBox)));
        // 打上标记再抓一张：那一格有墨了，而且形状是**叉**（两条对角线 —— 宽高差不多，
        // 而叶子行那个 > 是细高的一条）。判据用"那一格里有墨"+"宽高比"，不写死像素：
        // 叉的大小跟着 arrowSize 走，写死了改一次尺寸就得改断言。
        leaf->setData(0, MainWindowUI::rowClearRole(), true);
        QApplication::processEvents();
        const QImage clearShot = captureWidget(probe.viewport());
        const int clearInk = inkCount(clearShot, clearBox);
        const QRect bbCross = inkBBox(clearShot, clearBox);
        check(clearInk > 0 && !bbCross.isNull(),
              "★ 选中那条加载器右边画出了一个「取消」小叉（那一格有墨了）",
              QStringLiteral("墨点 %1").arg(clearInk));
        check(!bbCross.isNull() && bbCross.height() <= bbCross.width() + 1 &&
                  bbCross.width() >= 5,
              "★ 那一格画的是个叉（宽高差不多、不是细高的一条）",
              QStringLiteral("叉 %1x%2").arg(bbCross.width()).arg(bbCross.height()));
        // 悬停在那**一格**上弹「取消」（和左栏那个刷新圈走的是同一条路：几何都由
        // VersionTreeDelegate 给，文案在 MainWindowUI 里 —— 见 rowActionTip()）。
        // NOTE: 下标要从分类往下点一层（`model()->index(0,0)` 是**顶层第 1 条**，
        // 不是屏幕上第 1 行那个版本号，见下面那条断言里的同一段 NOTE）。
        {
            const QModelIndex leafIndex =
                probe.model()->index(0, 0, probe.model()->index(0, 0));
            check(VersionTreeDelegate::rowActionTip(leafIndex, leafRow, clearBox.center()) ==
                      MainWindowUI::rowClearTipText(),
                  "光标停在「取消」那一格上，弹的就是「取消」",
                  QString("拿到「%1」")
                      .arg(VersionTreeDelegate::rowActionTip(leafIndex, leafRow,
                                                             clearBox.center())));
            // 端到端：真发一个 ToolTip 事件给这棵树的视口（**树**和左栏那份**列表**是两个
            // 不同的宿主控件，代理在两边都得被叫到，所以两边各量一次）。
            // NOTE: 判据是**哨兵换没换**，不是"看不看得见" —— `QToolTip::hideText()`
            // 在这个平台上不生效（见左栏那一块里那段 NOTE），看 isVisible 会拿到上一条。
            const QString sentinel = QStringLiteral("哨兵");
            const auto tipAtTree = [&probe, &sentinel](const QPoint &pos) {
                Popups::showTooltip(sentinel, probe.viewport()->mapToGlobal(pos), probe.viewport());
                QHelpEvent event(QEvent::ToolTip, pos, probe.viewport()->mapToGlobal(pos));
                QApplication::sendEvent(probe.viewport(), &event);
                const QString shown = Popups::tooltipText();  // 代理走自绘那条（107）
                return shown == sentinel ? QString() : shown;
            };
            const QString shown = tipAtTree(clearBox.center());
            check(shown == MainWindowUI::rowClearTipText(),
                  "★ 真发一个悬停事件给这棵树的视口，弹出来的是「取消」",
                  QString("拿到「%1」").arg(shown));
            const QString elsewhere = tipAtTree(QPoint(leafRow.left() + 4, clearBox.center().y()));
            check(elsewhere.isEmpty(),
                  "★ 同一个事件发到那一行的正文上，一个字都不弹（还是那句哨兵）",
                  QString("拿到「%1」").arg(elsewhere));
            Popups::hideTooltip();
        }
        // 叉是**加在**行尾那格左边的：行尾那格（叶子、不悬停）照旧是空的，没被叉压到。
        check(inkCount(clearShot, arrowBox(1)) == 0,
              "★ 加了叉之后行尾那格照旧是空的（叉在它左边那一格里）",
              QStringLiteral("墨点 %1").arg(inkCount(clearShot, arrowBox(1))));
        // 那一格是从**文字**里扣出来的（不是压着版本号画的）：带标记的那一行要宽一点。
        // 量的是代理的 sizeHint —— 用户真会看到的那一列宽度就是它说了算。
        {
            VersionTreeDelegate delegate;
            QStyleOptionViewItem opt;
            opt.font = probe.font();
            // NOTE: 别写成 model()->index(1, 0) —— 那说的是**顶层第 1 条**
            // （=「愚人节」那个空分类），不是屏幕上第 1 行那个版本号。版本号是分类底下的
            // 子条目，得从它爹那儿往下点一层才对得上（踩过：拿错条目之后
            // 这条断言量的是另一条的行宽，怎么改都看不出差别）。
            const QModelIndex channelIndex = probe.model()->index(0, 0);
            const QModelIndex leafIndex = probe.model()->index(0, 0, channelIndex);
            const int widthWithCross = delegate.sizeHint(opt, leafIndex).width();
            leaf->setData(0, MainWindowUI::rowClearRole(), false);
            const int widthPlain = delegate.sizeHint(opt, leafIndex).width();
            check(widthWithCross > widthPlain,
                  "★ 带叉的那一行在算宽度时**多留了一格**（文字不会跑到叉底下）",
                  QStringLiteral("%1 → %2（多留 %3 px）")
                      .arg(widthPlain)
                      .arg(widthWithCross)
                      .arg(widthWithCross - widthPlain));
        }
        leaf->setData(0, MainWindowUI::rowClearRole(), false);
        QApplication::processEvents();

        // 这棵小树是个**独立顶层窗口**，show() 之后它就成了活动窗口 ——
        // 而后面 E2 那组要验"焦点在下拉框上时滚轮照常切换"，成立的前提是
        // **主窗口是活动窗口**（QWidget::hasFocus() 在非活动窗口上恒为 false）。
        // 量完把窗口还回去，不然那组断言会以"hasFocus=0"的形式莫名其妙地红。
        probe.hide();
        window.activateWindow();
        QApplication::processEvents();
    }
 return 0;
}


