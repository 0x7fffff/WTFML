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
    // UI 7b 起：实例行改用 ListRowDelegate 的 60px 两行版式，悬停在行尾露出
    // 「文件夹 / ⋯ / ▶ 启动」三颗（设计稿 manage-*）；实例设置、收藏、删除收进 ⋯ 菜单，
    // 整行点一下仍进实例设置。几何唯一出处是 `ListRowDelegate::actionRect()`。
    {
        using Action = ListRowDelegate::Action;
        const QRect row(0, 0, 600, ListRowDelegate::rowHeight());
        const QRect folder = ListRowDelegate::actionRect(row, Action::Folder);
        const QRect more = ListRowDelegate::actionRect(row, Action::More);
        const QRect launch = ListRowDelegate::actionRect(row, Action::Launch);

        // ---- a) 右起次序：启动在最右，文件夹最靠里；三格互不重叠、都在行内 ----
        check(launch.left() > more.left() && more.left() > folder.left(),
              "Z13a 右起次序：▶ 启动在最右、⋯ 居中、文件夹最靠里",
              QStringLiteral("%1/%2/%3").arg(folder.left()).arg(more.left()).arg(launch.left()));
        check(!folder.intersects(more) && !more.intersects(launch) && row.contains(folder) &&
                  row.contains(more) && row.contains(launch),
              "Z13b 三个命中格两两不相交，且都装在行高里");

        // ---- b) 两档行高：实例行 60、组头 36 ----
        {
            QTreeWidget probe;
            auto *delegate = new InstanceGroupDelegate(&probe);
            delegate->setInlineRowActions(true);
            probe.setItemDelegate(delegate);
            auto *group = new QTreeWidgetItem(&probe);
            group->setText(0, QStringLiteral("可安装mod (3)"));
            group->setData(0, MainWindowUI::groupIdRole(), QStringLiteral("default-modded"));
            auto *item = new QTreeWidgetItem(group);
            item->setText(0, QStringLiteral("1.20.1"));
            item->setData(0, MainWindowUI::instanceIdRole(), QStringLiteral("1.20.1"));
            QStyleOptionViewItem opt;
            opt.font = probe.font();
            const int groupH = delegate->sizeHint(opt, probe.model()->index(0, 0)).height();
            const int rowH = delegate->sizeHint(opt, probe.model()->index(0, 0, probe.model()->index(0, 0))).height();
            check(groupH == InstanceGroupDelegate::groupRowHeight() && rowH == ListRowDelegate::rowHeight(),
                  "Z13d 组头 36px、实例行 60px（设计稿 manage 的两档）",
                  QStringLiteral("组 %1 / 行 %2").arg(groupH).arg(rowH));
        }

        // ---- c) rowActionTip() 逐格说话 ----
        {
            QStandardItemModel model;
            auto *item = new QStandardItem(QStringLiteral("1.20.1"));
            model.appendRow(item);
            const QModelIndex index = model.index(0, 0);
            check(InstanceGroupDelegate::rowActionTip(index, row, folder.center()) ==
                          MainWindowUI::rowOpenFolderTipText() &&
                      InstanceGroupDelegate::rowActionTip(index, row, more.center()) ==
                          MainWindowUI::rowMoreTipText() &&
                      InstanceGroupDelegate::rowActionTip(index, row, launch.center()) ==
                          MainWindowUI::rowLaunchTipText(),
                  "Z13e 三格各说各的：打开文件夹 / 更多操作 / 启动",
                  InstanceGroupDelegate::rowActionTip(index, row, more.center()));
            check(InstanceGroupDelegate::rowActionTip(index, row, QPoint(row.left() + 2, row.center().y()))
                      .isEmpty(),
                  "Z13e 行里别的地方一个字都不弹（整行不是一个按钮）");
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


