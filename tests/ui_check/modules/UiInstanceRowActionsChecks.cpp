#include "Support.hpp"
int UiInstanceRowActionsChecks() {
    // ------------------------------------------------------------------
    // 用户当天原话：「实例页面，实例设置也是放在每一条的右侧，鼠标悬停时右侧显示上箭头和
    // 垃圾桶，分别对应 置顶和删除实例，删除前需要弹窗提示，直接点空白区域（和左边图标版本名）
    // 进入实例设置」，追加答复「要，一颗齿轮常显」。
    //
    // 一行三颗、**右起**：垃圾桶（悬停）/ 上箭头（悬停，且只在还能置顶的行上）/ 齿轮（常显）。
    // 三颗都**不是控件**，是"画出来的一格"：几何的唯一出处是 `InstanceGroupDelegate` 那
    // 六个 public 的 static（代理按它们画、`MainWindow::eventFilter` 按它们认下那一下、
    // `helpEvent()` 按它们弹话）。所以这一节量的是**那六个函数的输出**，不是"屏幕看着像"。
    //
    // NOTE: 真正画图的那一圈（`instanceActionGlyphRects()`）是 private，这一节够不着它；
    // 三个 `...GlyphRect()` 就是它的薄壳（原样返回，见 .hpp），量它们等价于量那一处。
    {
        const auto rt = [](const QRect &r) {
            return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        };
        // 一行宽 300 的实例行。pad/gap/size/margin 全是 MainWindowUI 的常数，所以下面那些
        // 绝对坐标是从行的右边缘往里推出来的，不是别处抄来的魔法数。
        const QRect row(0, 0, 300, MainWindowUI::versionTreeRowHeight());
        const QRect gearGlyph = InstanceGroupDelegate::instanceGearGlyphRect(row);
        const QRect pinGlyph = InstanceGroupDelegate::instancePinGlyphRect(row);
        const QRect trashGlyph = InstanceGroupDelegate::instanceTrashGlyphRect(row);
        const QRect gearHit = InstanceGroupDelegate::instanceGearRect(row);
        const QRect pinHit = InstanceGroupDelegate::instancePinRect(row);
        const QRect trashHit = InstanceGroupDelegate::instanceTrashRect(row);

        // ---- a) 右起次序：齿轮在最右边、垃圾桶在最里面 ----
        // 挡的是"三颗摆反了"：摆反了在屏幕上看着是"垃圾桶离手最近"，而那正是用户特意要
        // 避免的（垃圾桶不可逆，见 MainWindowUI 里"为什么垃圾桶摆最里面"）。
        check(gearHit.left() > pinHit.left() && pinHit.left() > trashHit.left(),
              "Z13a 右起次序：齿轮在最右边、上箭头居中、垃圾桶最靠里",
              QStringLiteral("齿轮 x=%1 / 上箭头 x=%2 / 垃圾桶 x=%3")
                  .arg(gearHit.left())
                  .arg(pinHit.left())
                  .arg(trashHit.left()));

        // ---- b) 三个命中格互不重叠、相邻两个**正好相接** ----
        // 这里把绝对坐标写死在断言里（不是"能重算出来"）：宽度改 1 px、间距改一格，这几条
        // 立刻红；而屏幕上那种错位的形状是"看着点中了垃圾桶、其实点的是那一行"（那一行点
        // 一下是进实例设置），肉眼看不出来。
        check(!gearHit.intersected(pinHit).isValid() &&
                  !pinHit.intersected(trashHit).isValid() &&
                  !gearHit.intersected(trashHit).isValid(),
              "Z13b 三个命中格两两不相交（相邻也不会叠 1 px）",
              QStringLiteral("齿轮 %1 / 上箭头 %2 / 垃圾桶 %3")
                  .arg(rt(gearHit))
                  .arg(rt(pinHit))
                  .arg(rt(trashHit)));
        check(pinHit.right() + 1 == gearHit.left() && trashHit.right() + 1 == pinHit.left(),
              "Z13b 相邻两个命中格**正好相接**（间隔 6、两边各 margin 3 ⇒ 一条缝都不漏）",
              QStringLiteral("齿轮左 %1 / 上箭头右 %2 / 垃圾桶右 %3")
                  .arg(gearHit.left())
                  .arg(pinHit.right())
                  .arg(trashHit.right()));
        const int size = MainWindowUI::instanceRowActionSize();
        const int pad = MainWindowUI::versionTreeRowPadding();
        const int top = row.top() + (row.height() - size) / 2;
        check(gearGlyph == QRect(row.x() + row.width() - pad - size, top, size, size) &&
                  gearGlyph == QRect(278, 15, 16, 16),
              "Z13b 齿轮字形那一圈（右边缘 - pad - size，不是拿 row.right() 当锚）",
              QStringLiteral("实际 %1 / 期望 278,15 16x16").arg(rt(gearGlyph)));
        check(pinGlyph == QRect(256, 15, 16, 16) && trashGlyph == QRect(234, 15, 16, 16),
              "Z13b 上箭头 / 垃圾桶的字形也是那两格（一颗一颗往左退一个 gap + size）",
              QStringLiteral("上箭头 %1 / 垃圾桶 %2").arg(rt(pinGlyph)).arg(rt(trashGlyph)));
        check(gearHit == QRect(275, 12, 22, 22) && pinHit == QRect(253, 12, 22, 22) &&
                  trashHit == QRect(231, 12, 22, 22),
              "Z13b 命中格 = 字形四周各 +margin(3)（齿轮 275..296 / 上箭头 253..274 / "
              "垃圾桶 231..252）",
              QStringLiteral("齿轮 %1 / 上箭头 %2 / 垃圾桶 %3")
                  .arg(rt(gearHit))
                  .arg(rt(pinHit))
                  .arg(rt(trashHit)));

        // ---- c) 字形装在命中格里、也装在行高里 ----
        // 命中格比字形大一圈（那 3 px 是手感余量、不画）；字形本身上下都不许冒出行高 ——
        // 冒出去的那半颗会被相邻两行的整屏重绘压掉（局部重绘更惨，直接切成半颗）。
        check(gearHit.contains(gearGlyph) && pinHit.contains(pinGlyph) &&
                  trashHit.contains(trashGlyph),
              "Z13c 每个字形都装在自己的命中格里（命中格四周那 3 px 是余量、不画）");
        check(row.contains(gearGlyph) && row.contains(pinGlyph) && row.contains(trashGlyph),
              "Z13c 三个字形也都装在行高里（一个像素都不冒出去）",
              QStringLiteral("行 %1 / 齿轮 %2 / 上箭头 %3 / 垃圾桶 %4")
                  .arg(rt(row))
                  .arg(rt(gearGlyph))
                  .arg(rt(pinGlyph))
                  .arg(rt(trashGlyph)));

        // ---- d) sizeHint 只比基类宽那一排按钮 ----
        // 摆一个真 model 来问：派生代理的宽度减掉**同一份 option/index** 下基类
        // （QStyledItemDelegate）的宽度，正好是那一排占的地方。少加/多加一个 gap 就会在
        // 这里露出来 —— 手算一遍是"能重算出来"，量出来才是"真的宽这么多"。
        {
            QTreeWidget probe;
            probe.setItemDelegate(new InstanceGroupDelegate(&probe));
            auto *item = new QTreeWidgetItem(&probe);
            item->setText(0, QStringLiteral("1.20.1"));
            item->setData(0, MainWindowUI::instanceIdRole(), QStringLiteral("1.20.1"));
            QStyleOptionViewItem opt;
            opt.font = probe.font();
            const QModelIndex index = probe.model()->index(0, 0);
            QStyledItemDelegate base;
            const int derivedW = probe.itemDelegate()->sizeHint(opt, index).width();
            const int baseW = base.sizeHint(opt, index).width();
            const int expected = 3 * MainWindowUI::instanceRowActionSize() +
                                 2 * MainWindowUI::instanceRowActionGap() +
                                 2 * MainWindowUI::instanceRowActionMargin() +
                                 MainWindowUI::versionTreeRowPadding();
            check(derivedW - baseW == expected,
                  "Z13d 派生代理比基类宽的正好是那一排（3 颗 + 2 个间隔 + 左右各一份留白）",
                  QStringLiteral("派生 %1 - 基类 %2 = %3（期望 %4）")
                      .arg(derivedW)
                      .arg(baseW)
                      .arg(derivedW - baseW)
                      .arg(expected));
        }

        // ---- e) rowActionTip() 逐格说话 ----
        // 用一个真 model 摆一行（**不挂视图** —— rowActionTip 是纯函数，只要一个 index）。
        // 提示文案全从 MainWindowUI 取，这儿一个字面量都不抄。
        {
            QStandardItemModel model;
            auto *item = new QStandardItem(QStringLiteral("1.20.1"));
            model.appendRow(item);
            const QModelIndex index = model.index(0, 0);
            // 命中三格各自的**中心点**：三句各说各的。
            check(InstanceGroupDelegate::rowActionTip(index, row, gearHit.center()) ==
                      MainWindowUI::rowInstanceSettingsTipText(),
                  "Z13e 齿轮那一格的中心 ⇒ 「实例设置」",
                  InstanceGroupDelegate::rowActionTip(index, row, gearHit.center()));
            check(InstanceGroupDelegate::rowActionTip(index, row, trashHit.center()) ==
                      MainWindowUI::rowDeleteInstanceTipText(),
                  "Z13e 垃圾桶那一格的中心 ⇒ 「删除这个实例」",
                  InstanceGroupDelegate::rowActionTip(index, row, trashHit.center()));
            // 上箭头那一格：`rowPinRole()` 为真（还能置顶）才说话。
            item->setData(true, MainWindowUI::rowPinRole());
            check(InstanceGroupDelegate::rowActionTip(index, row, pinHit.center()) ==
                      MainWindowUI::rowPinTipText(),
                  "Z13e 上箭头那一格的中心（还能置顶时）⇒ 「置顶」",
                  InstanceGroupDelegate::rowActionTip(index, row, pinHit.center()));
            // 行里别的地方（左边正文那一片）：一个字都不说 —— 那一行点一下是进实例设置，
            // 不是一个"按钮"（整行弹话的话，鼠标扫过一列实例名会一路弹个不停）。
            check(InstanceGroupDelegate::rowActionTip(
                      index, row, QPoint(row.left() + 2, row.center().y()))
                      .isEmpty(),
                  "Z13e 行里别的地方（最左边那一片）一个字都不弹（整行不是一个按钮）");
            // 产品上那条"挪不动的那侧不画、也不说话"：`rowPinRole()` 为假的行走同一个点，
            // 不许说出「置顶」—— 不然就是"指着一块空白弹出了置顶"。
            item->setData(false, MainWindowUI::rowPinRole());
            const QString noPinTip =
                InstanceGroupDelegate::rowActionTip(index, row, pinHit.center());
            check(noPinTip != MainWindowUI::rowPinTipText(),
                  "★ Z13e 已经在置顶组里（rowPinRole() 为假）时，上箭头那一格不说「置顶」",
                  QStringLiteral("拿到「%1」").arg(noPinTip));

            // 同一格反过来那一颗：要求「置顶组里的实例悬停时右侧显示
            // 下箭头，即取消置顶」。它和上面那一段是**同一格**（位置/命中格的算法一个字
            // 都没改），换的只是"这一行挂的是哪个角色"和"因此说哪句话"。
            item->setData(true, MainWindowUI::rowUnpinRole());
            check(InstanceGroupDelegate::rowActionTip(index, row, pinHit.center()) ==
                      MainWindowUI::rowUnpinTipText(),
                  "★ Z13e 已经在置顶组里（rowUnpinRole() 为真）时，同一格说的是「取消置顶」"
                  "（不在里面说「置顶」，在里面说「取消置顶」—— 一对反义词共用一格）",
                  InstanceGroupDelegate::rowActionTip(index, row, pinHit.center()));
            // 两个角色都为假 = 这一份 .minecraft 里没有置顶组 ⇒ 那一格是**空的**，
            // 一个字都不该说（"指着一块空白弹出了置顶"是最难解释的一种）。
            item->setData(false, MainWindowUI::rowUnpinRole());
            check(InstanceGroupDelegate::rowActionTip(index, row, pinHit.center()).isEmpty(),
                  "★ Z13e 没有置顶组时（两个角色都假）：那一格一个字都不说（它没画出来）",
                  QStringLiteral("拿到「%1」")
                      .arg(InstanceGroupDelegate::rowActionTip(index, row, pinHit.center())));
            // 组行那一半不许被带坏：挂着 groupIdRole() + rowGroupGearRole() 的行，
            // 齿轮那一格说的是那一组的话，不是「实例设置」。
            QStandardItemModel groupModel;
            auto *groupItem = new QStandardItem(QStringLiteral("整合包 (3)"));
            groupItem->setData(QStringLiteral("default-modded"), MainWindowUI::groupIdRole());
            groupItem->setData(true, MainWindowUI::rowGroupGearRole());
            groupModel.appendRow(groupItem);
            const QModelIndex groupIndex = groupModel.index(0, 0);
            const QPoint groupGearCenter = InstanceGroupDelegate::gearRect(row).center();
            check(InstanceGroupDelegate::rowActionTip(groupIndex, row, groupGearCenter) ==
                      MainWindowUI::rowGroupGearTipText(),
                  "Z13e 组行那一半照旧：齿轮那一格说的是「这一组的更多操作」",
                  InstanceGroupDelegate::rowActionTip(groupIndex, row, groupGearCenter));
        }

        // ---- f) VersionIcons::arrowUp() 这一支 ----
        // 它是这一排中间那颗「置顶」画的字形：一支**有杆的 ↑**（不是 chevronUp() 的 `^`、
        // 也不是 triangleUp() 的实心三角）。挑形状时唯一要想清楚的事就是"一行里三个字形
        // 一眼分得开"，所以下面既要量它自己（尺寸 / 留边 / 有笔画），也要量它和另两支不同。
        {
            const int side = MainWindowUI::instanceRowActionSize();  // 16，界面上那一颗的大小
            const QColor ink(Qt::black);
            const QPixmap arrow = VersionIcons::arrowUp(side, ink);
            check(!arrow.isNull() && arrow.size() == QSize(side, side),
                  "Z13f arrowUp() 给出的是 side×side 的图（尺寸跟着调用方给的来）",
                  QStringLiteral("%1x%2").arg(arrow.width()).arg(arrow.height()));
            const QImage image = arrow.toImage();
            int inkPixels = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (qAlpha(image.pixel(x, y)) > 0) {
                        ++inkPixels;
                    }
                }
            }
            check(inkPixels > 0, "Z13f arrowUp() 不是一张空白（真的画了笔画）",
                  QStringLiteral("有笔画的像素 = %1").arg(inkPixels));
            // 最外一圈一个有色像素都没有：字形碰到画布边缘时，抗锯齿那一圈会被裁掉、
            // 看着像缺了一角（这是仓库的既有规矩，见 VersionIcons.hpp 里它的注释）。
            int borderInk = 0;
            const int last = side - 1;
            for (int i = 0; i < side; ++i) {
                for (const QPoint &point :
                     {QPoint(i, 0), QPoint(i, last), QPoint(0, i), QPoint(last, i)}) {
                    if (qAlpha(image.pixel(point.x(), point.y())) > 0) {
                        ++borderInk;
                    }
                }
            }
            check(borderInk == 0, "Z13f 最外一圈一个有色像素都没有（没被画布裁掉）",
                  QStringLiteral("最外一圈上有 %1 个有色像素").arg(borderInk));
            // 和另两支箭头**不是同一张图** —— 复制粘贴把 chevronUp 抄过来是最可能的错法，
            // 而那种错法"有笔画、尺寸也对"，上面那几条盖不住。
            check(image != VersionIcons::chevronUp(side, ink).toImage() &&
                      image != VersionIcons::triangleUp(side, ink).toImage(),
                  "★ Z13f 它和 chevronUp() / triangleUp() 不是同一张图（不是抄来的）");
            // 颜色真的是调用方给的：换一种颜色画一张，两张图不相等。
            check(image != VersionIcons::arrowUp(side, QColor(Qt::red)).toImage(),
                  "Z13f 换个颜色画出来的是另一张图（颜色由调用方给，不是写死的）");
        }

        // ---- g) VersionIcons::arrowDown() —— 同一格反过来那一颗 ----
        //
        // 要求：「置顶组里的实例悬停时右侧显示下箭头，即取消置顶」。
        // 这一支是 arrowUp() **整条竖直翻过来**（两个方向共用同一组比例，见 .cpp 里
        // arrowPixmap() 那段），所以这里量三件事：它自己成不成图、**朝向对不对**、
        // 以及它和另几支是不是同一张（复制粘贴把 arrowUp 抄过来是最可能的错法）。
        {
            const int side = MainWindowUI::instanceRowActionSize();
            const QColor ink(Qt::black);
            const QImage down = VersionIcons::arrowDown(side, ink).toImage();
            const QImage up = VersionIcons::arrowUp(side, ink).toImage();
            check(!down.isNull() && down.size() == QSize(side, side),
                  "Z13g arrowDown() 给出的是 side×side 的图",
                  QStringLiteral("%1x%2").arg(down.width()).arg(down.height()));
            // 有笔画 + 最外一圈不留色（和 arrowUp() 那两条同一个规矩）。
            const auto inkIn = [](const QImage &image, int y0, int y1) {
                int count = 0;
                for (int y = y0; y < y1; ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        if (qAlpha(image.pixel(x, y)) > 0) {
                            ++count;
                        }
                    }
                }
                return count;
            };
            const int half = side / 2;
            const int topDown = inkIn(down, 0, half);
            const int bottomDown = inkIn(down, half, side);
            const int topUp = inkIn(up, 0, half);
            const int bottomUp = inkIn(up, half, side);
            check(topDown + bottomDown > 0, "Z13g arrowDown() 不是一张空白（真的画了笔画）",
                  QStringLiteral("有笔画的像素 = %1").arg(topDown + bottomDown));
            // 朝向：箭头那一头（宽的那个三角）在哪半边，就是它朝哪边。上箭头"上重下轻"、
            // 下箭头正好反过来 —— 这一条挡的是"两颗画的是同一个方向"（那种错法尺寸对、
            // 有笔画、也和最外一圈那两条不冲突，肉眼在一格 16 px 里未必看得出来）。
            check(topUp > bottomUp && bottomDown > topDown,
                  "★ Z13g arrowUp() 是上重下轻、arrowDown() 是下重上轻（两颗朝向真的相反）",
                  QStringLiteral("上箭头 上%1/下%2，下箭头 上%3/下%4")
                      .arg(topUp)
                      .arg(bottomUp)
                      .arg(topDown)
                      .arg(bottomDown));
            int borderInk = 0;
            const int last = side - 1;
            for (int i = 0; i < side; ++i) {
                for (const QPoint &point :
                     {QPoint(i, 0), QPoint(i, last), QPoint(0, i), QPoint(last, i)}) {
                    if (qAlpha(down.pixel(point.x(), point.y())) > 0) {
                        ++borderInk;
                    }
                }
            }
            check(borderInk == 0, "Z13g 最外一圈一个有色像素都没有（没被画布裁掉）",
                  QStringLiteral("最外一圈上有 %1 个有色像素").arg(borderInk));
            // 和另几支**不是同一张图**：↑ 自己、chevronUp 的 `^`、triangleDown 的实心 ▼
            // 都是"看着像"的候选。
            check(down != up && down != VersionIcons::chevronUp(side, ink).toImage() &&
                      down != VersionIcons::triangleDown(side, ink).toImage(),
                  "★ Z13g 它和 arrowUp() / chevronUp() / triangleDown() 都不是同一张图");
            check(down != VersionIcons::arrowDown(side, QColor(Qt::red)).toImage(),
                  "Z13g 换个颜色画出来的是另一张图（颜色由调用方给，不是写死的）");
            // "同一组比例翻过来"这件事本身：两颗的墨迹**上下边界必须互为镜像**
            // （上箭头的上沿 = 下箭头的下沿翻上去那一行）。它挡的是"哪天有人只改了一边
            // 的比例"—— 那种错法上面几条全绿，屏幕上只是"两颗箭头长得不太一样"。
            const auto inkBounds = [](const QImage &image) {
                QRect box;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        if (qAlpha(image.pixel(x, y)) > 0) {
                            const QRect px(x, y, 1, 1);
                            box = box.isNull() ? px : box.united(px);
                        }
                    }
                }
                return box;
            };
            const QRect upBox = inkBounds(up);
            const QRect downBox = inkBounds(down);
            check(!upBox.isNull() && upBox.left() == downBox.left() &&
                      upBox.right() == downBox.right() &&
                      upBox.top() == last - downBox.bottom() &&
                      upBox.bottom() == last - downBox.top(),
                  "★ Z13g 两颗箭头的墨迹上下互为镜像（同一组比例翻过来，左右边界一模一样）",
                  QStringLiteral("上箭头 %1,%2-%3,%4 / 下箭头 %5,%6-%7,%8")
                      .arg(upBox.left())
                      .arg(upBox.top())
                      .arg(upBox.right())
                      .arg(upBox.bottom())
                      .arg(downBox.left())
                      .arg(downBox.top())
                      .arg(downBox.right())
                      .arg(downBox.bottom()));
        }
    }
    return 0;
}


