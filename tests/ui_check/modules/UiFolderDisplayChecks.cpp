#include "Support.hpp"
int UiFolderDisplayChecks(MainWindowUI &ui, QListWidget * &folderList, QTabWidget * &tabs) {
    // ------------------------------------------------------------------
    // 要求：「实例管理里，文件夹允许重命名（一个 customname，不是直接改，毕竟
    // .minecraft 改起来容易出事）……显示的时候和版本下载那里类似，上面写 customname，
    // 下面写路径，分栏做高一点不要像现在那么小，每个版本右侧有个齿轮，点击以后会有个
    // 类似右键菜单的页面（框），里面是刷新 重命名 打开 移除 删除（红色字）……齿轮是按钮」。
    //
    // 这一节只碰 MainWindowUI（数据在 MainWindow 手里，那五个动作**真做起来**在 mw_check
    // 的 P2 段）。这里钉的是**形状**：行里有几行字、多高、齿轮是不是真按钮、菜单里那五项
    // 的字/图标/顺序/可用性，以及"点某一项会把哪一份文件夹、第几项告诉外面"。
    {
        tabs->setCurrentIndex(1);  // 「实例」页：隐藏着的控件不排布局，量不到行高
        QApplication::processEvents();

        // ---- 三个新字形：笔（斜着的）/ 文件夹 / 垃圾桶 ----
        {
            const int side = MainWindowUI::folderMenuIconSize();
            const QColor color(0x33, 0x33, 0x33);
            const auto inkOf = [](const QImage &image) {
                int ink = 0;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        if (qAlpha(image.pixel(x, y)) > 0) {
                            ++ink;
                        }
                    }
                }
                return ink;
            };
            const QImage penImage = VersionIcons::pen(side, color).toImage();
            const QImage folderImage = VersionIcons::folderMark(side, color).toImage();
            const QImage trashImage = VersionIcons::trash(side, color).toImage();
            check(!penImage.isNull() && !folderImage.isNull() && !trashImage.isNull() &&
                      penImage.size() == QSize(side, side) &&
                      folderImage.size() == QSize(side, side) &&
                      trashImage.size() == QSize(side, side),
                  "三个新字形都画得出来，而且都是 side×side（尺寸跟着调用方给的来）",
                  QString("%1x%2").arg(penImage.width()).arg(penImage.height()));
            check(VersionIcons::pen(0, color).isNull() &&
                      VersionIcons::folderMark(0, color).isNull() &&
                      VersionIcons::trash(0, color).isNull(),
                  "边长给 0 时给回空图（不崩，和 refresh / cross 同一个取法）");
            check(inkOf(penImage) > 0 && inkOf(folderImage) > 0 && inkOf(trashImage) > 0,
                  "三支笔都真画了东西（不是三张白图）",
                  QString("%1 / %2 / %3")
                      .arg(inkOf(penImage))
                      .arg(inkOf(folderImage))
                      .arg(inkOf(trashImage)));
            // 两两不同、也不和已有的齿轮/刷新圈/叉撞脸：把别的形状复制一份过来是最可能的
            // 错法，而那种错法"有笔画、尺寸也对"，上面那两条一条都盖不住。
            const QList<QImage> newOnes = {penImage, folderImage, trashImage};
            bool allDistinct = true;
            for (int i = 0; i < newOnes.size(); ++i) {
                for (int j = i + 1; j < newOnes.size(); ++j) {
                    allDistinct = allDistinct && newOnes.at(i) != newOnes.at(j);
                }
                allDistinct = allDistinct && newOnes.at(i) != VersionIcons::gear(side, color).toImage() &&
                              newOnes.at(i) != VersionIcons::refresh(side, color).toImage() &&
                              newOnes.at(i) != VersionIcons::cross(side, color).toImage();
            }
            check(allDistinct, "三个新字形两两不同、也不和已有的齿轮/刷新/叉撞脸（不是抄来的）");
            // 笔是**斜着**的（要求「重命名画一只笔（斜着的）」）。竖着画的话它是一根
            // 又高又窄的杆子 —— 量笔迹的外接矩形就分得开：转 45° 之后它是接近正方形的，
            // 没转的话宽度只剩三成边长。
            int minX = side;
            int maxX = -1;
            int minY = side;
            int maxY = -1;
            for (int y = 0; y < side; ++y) {
                for (int x = 0; x < side; ++x) {
                    if (qAlpha(penImage.pixel(x, y)) == 0) {
                        continue;
                    }
                    minX = qMin(minX, x);
                    maxX = qMax(maxX, x);
                    minY = qMin(minY, y);
                    maxY = qMax(maxY, y);
                }
            }
            const int boxWidth = maxX - minX + 1;
            const int boxHeight = maxY - minY + 1;
            check(boxWidth >= side * 6 / 10 && boxHeight >= side * 6 / 10,
                  "笔是斜着的（笔迹外接矩形接近正方形；竖着画的话宽只有三成边长）",
                  QString("外接框 %1x%2 / 边长 %3").arg(boxWidth).arg(boxHeight).arg(side));
        }

        // ---- 五项的字、提示、图标，以及红字那一档 ----
        {
            QStringList texts;
            QStringList tips;
            bool iconsOk = true;
            for (int a = 0; a < MainWindowUI::folderActionCount(); ++a) {
                texts.append(MainWindowUI::folderActionText(a));
                tips.append(MainWindowUI::folderActionTooltip(a));
                iconsOk = iconsOk &&
                          !MainWindowUI::folderActionIcon(a, MainWindowUI::folderMenuIconSize(),
                                                          Qt::black)
                               .isNull();
            }
            check(texts == QStringList({QStringLiteral("刷新"), QStringLiteral("重命名"),
                                        QStringLiteral("打开"), QStringLiteral("移除"),
                                        QStringLiteral("删除")}),
                  "五项按顺序就是「刷新 重命名 打开 移除 删除」", texts.join(QLatin1Char('/')));
            check(iconsOk, "五项各有一个图标（图标摆在文字前面是 QPushButton 自己的摆法）");
            tips.removeDuplicates();
            check(tips.size() == MainWindowUI::folderActionCount(),
                  "五句提示两两不同（复制粘贴的错法会留下两句一样的）",
                  QString::number(tips.size()));
            // 「删除」的字色是**红**的，而且浅底/深底各有一档：深红落在深色底上只剩一团黑，
            // 而这颗红字正是"这一项会把东西删掉"唯一的提示（不算确认框那一道）。
            QPalette lightPalette;
            lightPalette.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
            QPalette darkPalette;
            darkPalette.setColor(QPalette::Window, QColor(0x20, 0x20, 0x20));
            const QColor lightDanger = MainWindowUI::folderDangerColor(lightPalette);
            const QColor darkDanger = MainWindowUI::folderDangerColor(darkPalette);
            check(lightDanger.hue() <= 10 || lightDanger.hue() >= 350,
                  "浅底那一档是红（色相在红那一头）",
                  QString("%1 色相 %2").arg(lightDanger.name()).arg(lightDanger.hue()));
            check(lightDanger.saturation() > 120 && darkDanger.saturation() > 120,
                  "两档都是**饱和的红**（不是灰扑扑的一团）",
                  QString("饱和度 %1 / %2")
                      .arg(lightDanger.saturation())
                      .arg(darkDanger.saturation()));
            check(lightDanger != darkDanger && lightDanger.lightness() < darkDanger.lightness(),
                  "深色底上用浅红、浅色底上用深红（不这么分的话总有一边看不见）",
                  QString("%1 / %2").arg(lightDanger.name()).arg(darkDanger.name()));
        }

        // ---- 灌两行进去：自带那份（选中）+ 一份起过 customname 的 ----
        const QString defaultPath = QStringLiteral("D:/launcher/.minecraft");
        const QString otherPath = QStringLiteral("E:/MC/我的整合包");
        const QString otherName = QStringLiteral("我的整合包");
        {
            QList<MainWindowUI::FolderRow> rows;
            {
                MainWindowUI::FolderRow row;
                row.path = defaultPath;
                row.title = MainWindowUI::folderAssistantTitle();
                row.subtitle = QDir::toNativeSeparators(defaultPath);
                row.assistant = true;
                row.current = true;
                rows.append(row);
            }
            {
                MainWindowUI::FolderRow row;
                row.path = otherPath;
                row.title = otherName;
                row.subtitle = QDir::toNativeSeparators(otherPath);
                rows.append(row);
            }
            ui.setFolderRows(rows);
            QApplication::processEvents();
            const auto rowWidgetOf = [&folderList](int index) {
                return folderList->viewport()->findChild<QWidget *>(
                    MainWindowUI::folderRowName(index));
            };
            const auto labelOf = [&folderList](int index, const QString &name) {
                return folderList->viewport()->findChild<QLabel *>(name);
            };
            const auto gearOf = [&folderList](int index) {
                return folderList->viewport()->findChild<QPushButton *>(
                    MainWindowUI::folderRowGearName(index));
            };
            check(folderList->count() == 2, "灌两行进去，列表就是两行",
                  QString::number(folderList->count()));
            check(folderList->currentItem() != nullptr &&
                      folderList->currentItem()->data(Qt::UserRole).toString() == defaultPath,
                  "row.current 那一行是选中态（认身份走 UserRole，不认第几行）",
                  folderList->currentItem() != nullptr
                      ? folderList->currentItem()->data(Qt::UserRole).toString()
                      : QString());
            bool rowsOk = true;
            for (int i = 0; i < rows.size(); ++i) {
                QWidget *rowWidget = rowWidgetOf(i);
                QLabel *title = labelOf(i, MainWindowUI::folderRowTitleName(i));
                QLabel *subtitle = labelOf(i, MainWindowUI::folderRowSubtitleName(i));
                QPushButton *gear = gearOf(i);
                check(rowWidget != nullptr && title != nullptr && subtitle != nullptr &&
                          gear != nullptr,
                      QStringLiteral("第 %1 行：行控件 + 上下两行字 + 行尾齿轮都按 objectName "
                                     "找得回来")
                          .arg(i));
                if (rowWidget == nullptr || title == nullptr || subtitle == nullptr ||
                    gear == nullptr) {
                    rowsOk = false;
                    continue;
                }
                check(title->text() == rows.at(i).title && subtitle->text() == rows.at(i).subtitle,
                      QStringLiteral("第 %1 行上面写名字、下面写路径（名字和路径是两行）").arg(i),
                      title->text() + QStringLiteral(" / ") + subtitle->text());
                check(title->text() != subtitle->text() ||
                          rows.at(i).title == rows.at(i).subtitle,
                      QStringLiteral("第 %1 行那两行字不是同一句").arg(i));
                // 路径那行**从中间省略**：这一列是路径，尾巴上那截（最后一级目录名）恰是
                // 唯一认得出"这是哪一份"的信息 —— 几个文件夹前缀一样时（D:/MC/a、D:/MC/b）
                // 两行会长得一模一样。从尾巴省略的话这些行就分不开。这里读的是**标签自己
                // 那个属性**（画字的 paintEvent 读的是同一个成员）—— 谁把 FolderRowWidget
                // 里那句 setElideMode() 删了，它就退回 ElideRight，这条当场红。
                check(subtitle->property("elideMode").toInt() ==
                          static_cast<int>(MainWindowUI::folderRowPathElideMode()),
                      QStringLiteral("第 %1 行路径那行是从中间省略的（两头都比中间那截有用）")
                          .arg(i),
                      QStringLiteral("elideMode=%1")
                          .arg(subtitle->property("elideMode").toInt()));
                // 名字那行照旧从尾巴省略：它是名字，不是路径，两头没有"都有用"这回事。
                // 顺带把"两行的模式真的不一样"钉住 —— 否则上一条在一个全体改成 ElideMiddle
                // 的世界里也是绿的。
                check(title->property("elideMode").toInt() == static_cast<int>(Qt::ElideRight) &&
                          title->property("elideMode") != subtitle->property("elideMode"),
                      QStringLiteral("第 %1 行名字那行还是从尾巴省略（只有路径那行特殊）")
                          .arg(i),
                      QStringLiteral("名字 elideMode=%1")
                          .arg(title->property("elideMode").toInt()));
                // 行高：用户要的「分栏做高一点不要像现在那么小」。两处都量 —— 条目那一格
                // （setItemWidget 摆进去的控件多高全看它）和控件自己的高度。
                check(folderList->item(i) != nullptr &&
                          folderList->item(i)->sizeHint().height() ==
                              MainWindowUI::folderRowHeight(),
                      QStringLiteral("第 %1 行那一格的高度就是 folderRowHeight()（不给的话会被"
                                     "压成一条缝）")
                          .arg(i),
                      folderList->item(i) != nullptr
                          ? QString::number(folderList->item(i)->sizeHint().height())
                          : QString());
                check(rowWidget->height() == MainWindowUI::folderRowHeight(),
                      QStringLiteral("第 %1 行行控件也真的这么高").arg(i),
                      QString::number(rowWidget->height()));
                check(rowWidget->height() >= title->height() + subtitle->height(),
                      QStringLiteral("这个高度装得下上下两行字").arg(i),
                      QString("%1 >= %2 + %3")
                          .arg(rowWidget->height())
                          .arg(title->height())
                          .arg(subtitle->height()));
                // 齿轮是**真按钮**（要求「齿轮是按钮（以免你不理解）」）：不是画出来
                // 再靠事件过滤器认位置 —— 那样就没有 hover / 按下 / disabled 三态。
                check(gear->isEnabled() && !gear->icon().isNull() &&
                          gear->size() == QSize(MainWindowUI::folderRowGearSize(),
                                                MainWindowUI::folderRowGearSize()),
                      QStringLiteral("第 %1 行那颗齿轮是真按钮、装着字形、尺寸也是约定的").arg(i),
                      QString("%1x%2").arg(gear->width()).arg(gear->height()));
                check(gear->cursor().shape() == Qt::PointingHandCursor,
                      QStringLiteral("第 %1 行那颗齿轮的鼠标指针是「手」（可点的样子）").arg(i));
                // 齿轮贴着**行尾**：几行的齿轮竖着对齐，而不是紧跟着路径（路径一行长一行短）。
                check(gear->geometry().right() >= rowWidget->width() - 12,
                      QStringLiteral("第 %1 行的齿轮贴在行尾（不是跟着路径长短跑）").arg(i),
                      QString("%1 / 行宽 %2").arg(gear->geometry().right()).arg(rowWidget->width()));
            }
            check(rowsOk, "V 前提：两行都摆齐了（上面每一条都真的跑过）");

            // ---- 齿轮弹出来的那块：五项、自带那份灰两项、"点了把谁告诉外面" ----
            QList<QPair<QString, int>> calls;
            ui.setFolderActionHandler([&calls](const QString &path, int action) {
                calls.append(qMakePair(path, action));
            });
            const auto menuNow = [&folderList]() -> QWidget * {
                return folderList->findChild<QWidget *>(MainWindowUI::folderMenuName());
            };
            // 「这个字形里有没有**饱和的红**」。判据必须连饱和度一起看：**灰的色相也是 0**
            // （HSV 里无彩色一律落在红那一头），只看色相会把正文色的字形也判成红的。
            // 取图要带上**按钮那一刻的 Mode**：图标现在走 QIconEngine，颜色按
            // Normal / Disabled 现算；灰按钮上 QStyle 问的是 Disabled（灰图标）。
            const auto hasDangerRed = [](const QIcon &icon,
                                        QIcon::Mode mode = QIcon::Normal) {
                const QImage image =
                    icon.pixmap(MainWindowUI::folderMenuIconSize(),
                                MainWindowUI::folderMenuIconSize(), mode)
                        .toImage();
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        const QColor pixel = image.pixelColor(x, y);
                        if (pixel.alpha() > 128 && pixel.saturation() > 120 &&
                            (pixel.hue() <= 10 || pixel.hue() >= 350)) {
                            return true;
                        }
                    }
                }
                return false;
            };
            const auto openMenuOf = [&](int index) -> QWidget * {
                QPushButton *gear = gearOf(index);
                if (gear == nullptr) {
                    return nullptr;
                }
                // 先把上一块收掉：同一颗齿轮上第二次点击是"收回"，不收的话这里会拿到空。
                if (QWidget *stale = menuNow()) {
                    stale->close();
                }
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                gear->click();
                QApplication::processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                return menuNow();
            };

            // 五项各点一遍：回调说的是"哪一份文件夹 + 第几项"。这条正是"点刷新结果把文件夹
            // 删了"那种错法的唯一防线（枚举值和显示顺序漂开就是这么来的）。
            for (int a = 0; a < MainWindowUI::folderActionCount(); ++a) {
                QWidget *menu = openMenuOf(1);
                check(menu != nullptr && menu->isVisible(),
                      QStringLiteral("点第二行那颗齿轮弹出一块框（第 %1 次）").arg(a));
                auto *entry = menu != nullptr
                                  ? menu->findChild<QPushButton *>(
                                        MainWindowUI::folderMenuActionName(a))
                                  : nullptr;
                check(entry != nullptr && entry->text() == MainWindowUI::folderActionText(a),
                      QStringLiteral("菜单里第 %1 项找得到、写的就是那句").arg(a),
                      entry != nullptr ? entry->text() : QString());
                if (entry == nullptr) {
                    continue;
                }
                check(!entry->icon().isNull() &&
                          entry->iconSize().width() == MainWindowUI::folderMenuIconSize(),
                      QStringLiteral("第 %1 项前面带着图标").arg(a));
                check(entry->styleSheet().contains(QStringLiteral("text-align: left")),
                      QStringLiteral("第 %1 项是从左边起排的（像菜单，不是一排居中按钮）").arg(a));
                // 字要**粗**（要求「这个字能不能用加粗的」，问的就是这五项）。
                // 两条一起钉：样式表里写着这条声明（和上面 text-align 那条同一个写法），
                // **以及它真的落到了控件上** —— 只钉样式表字符串的话，哪年换一个不认
                // font-weight 的样式就变成假绿了。
                check(entry->styleSheet().contains(QStringLiteral("font-weight: bold")),
                      QStringLiteral("第 %1 项的样式表里写着加粗").arg(a));
                check(entry->font().bold(),
                      QStringLiteral("第 %1 项的字真的变粗了（不是只写在样式表里）").arg(a),
                      QStringLiteral("bold=%1").arg(entry->font().bold() ? 1 : 0));
                // 垃圾桶图标也是红的，而且**只有**「删除」那一项是红的（用户
                // 原话「删除的垃圾桶也换成红色吧」）。量的是字形里有没有饱和的红 —— 字形
                // 的颜色是我们自己传进去的，不吃调色板，所以这一格真的是"颜色由我们决定"。
                if (a == MainWindowUI::FolderActionDelete) {
                    check(hasDangerRed(entry->icon()),
                          "「删除」那一项的垃圾桶图标是红的（和它的字同一个颜色）");
                } else {
                    check(!hasDangerRed(entry->icon()),
                          QStringLiteral("第 %1 项的图标不是红的（只有「删除」红）").arg(a));
                }
                entry->click();
                QApplication::processEvents();
                check(calls.size() == a + 1 && calls.last().first == otherPath &&
                          calls.last().second == a,
                      QStringLiteral("点第 %1 项，回调说的是「这一份文件夹 + 第 %1 项」").arg(a),
                      QStringLiteral("收到 %1 次").arg(calls.size()));
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
            check(menuNow() == nullptr, "点完一项那块框自己收回去了（不会越点越多）");

            // ---- 那块框自己的留白（用户要收紧的那一圈）----
            //
            // 要求：「点开的那个框在文本左右都有点留白，上下也是，能不能缩小一点点
            // （减少到 80% 的样子）」，紧接着又划了一句「上下不用减那么多，匹配即可」。
            // 于是留白拆成两截，各减各的：
            // 框自己的内边距（kFolderMenuPadding） 10 → 8
            // 一项自己的内边距（folderActionStyle 里那句 QSS） 「6px 8px」→「6px 6px」
            // 左右合计 18 → 14（≈78%，就是"80% 的样子"），上下合计 16 → 14（≈88%，就是
            // "不用减那么多"）—— 两边**都落在 14**，这是"匹配"那两个字要的。
            //
            // NOTE: 两个数都写死在验证工程里（一个是 .cpp 匿名命名空间里的常量、一个在
            // QSS 字符串里，这里够不着），和"实例页那个按钮数钉死成六"同一个取法：
            // 改留白的人必须来这儿说一声。断言同时钉**两个轴相等** —— 只钉"是 8"
            // 的话，哪天有人把上下改成 6、左右留 8，数字变了这条会红，但"上下左右
            // 不匹配"这件事本身没有一条断言在管。
            {
                constexpr int kExpectedBoxPadding = 8;
                constexpr int kExpectedItemPadding = 6;
                QWidget *menu = openMenuOf(1);
                auto *entry = menu != nullptr
                                  ? menu->findChild<QPushButton *>(
                                        MainWindowUI::folderMenuActionName(MainWindowUI::FolderActionRefresh))
                                  : nullptr;
                check(entry != nullptr && entry->x() == kExpectedBoxPadding &&
                          entry->y() == kExpectedBoxPadding,
                      "菜单那块框的内边距收紧到 8，而且**上下与左右是同一个数**",
                      entry != nullptr ? QStringLiteral("x=%1 y=%2")
                                             .arg(entry->x())
                                             .arg(entry->y())
                                       : QString());
                check(entry != nullptr &&
                          entry->styleSheet().contains(QStringLiteral("padding: 6px 6px")),
                      "一项自己的内边距左右与上下相等（6px 6px）—— 用户要的「匹配」",
                      entry != nullptr ? entry->styleSheet() : QString());
                // 上面两条量的是留白"由哪两截拼出来"，这一条量的是用户真正看得见的那件事：
                // **字形到框边有多远**。两截加起来，横向和纵向必须一样宽 —— 用户那句
                // "匹配即可"说的就是这个（横向和纵向都是 14）。两截分开量：框那 8 px 是
                // 上面第一条量的 geometry，这一项那 6 px 只有渲染出来才看得见。
                if (entry != nullptr) {
                    const QImage shot = captureWidget(entry);
                    const auto hasInk = [](QRgb p) { return QColor::fromRgba(p).alpha() > 128; };
                    // 第一列 / 第一行有墨的位置（= 图标画在哪儿）。
                    const auto inkEdge = [&hasInk](const QImage &image, bool vertical) {
                        const int outer = vertical ? image.width() : image.height();
                        const int inner = vertical ? image.height() : image.width();
                        for (int a = 0; a < outer; ++a) {
                            for (int b = 0; b < inner; ++b) {
                                const QRgb p = vertical ? image.pixel(a, b) : image.pixel(b, a);
                                if (hasInk(p)) {
                                    return a;
                                }
                            }
                        }
                        return -1;
                    };
                    const QImage glyph =
                        entry->icon()
                            .pixmap(MainWindowUI::folderMenuIconSize(),
                                    MainWindowUI::folderMenuIconSize())
                            .toImage();
                    const int glyphLeft = inkEdge(glyph, true);
                    const int glyphTop = inkEdge(glyph, false);
                    const int inkLeft = inkEdge(shot, true);
                    const int inkTop = inkEdge(shot, false);
                    check(glyphLeft >= 0 && inkLeft - glyphLeft == kExpectedItemPadding &&
                              inkTop - glyphTop == kExpectedItemPadding,
                          "一项自己的内边距左右与上下相等（量字形落点都往里 6 px）—— 加上框那 8，"
                          "文本到框边就是 14 x 14",
                          QStringLiteral("字形 %1,%2 落点 %3,%4（图标内空白 %5,%6）")
                              .arg(glyphLeft)
                              .arg(glyphTop)
                              .arg(inkLeft)
                              .arg(inkTop)
                              .arg(inkLeft - glyphLeft)
                              .arg(inkTop - glyphTop));
                }
                if (menu != nullptr) {
                    menu->close();
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                }
            }

            // 自带那一份：菜单弹得出来，但「移除」「删除」是灰的（它移不掉也删不掉），
            // 而且灰着要配一句**为什么** —— 灰着不说话，用户只会以为界面坏了。
            {
                QWidget *menu = openMenuOf(0);
                check(menu != nullptr, "自带那一行的齿轮也弹得出菜单");
                if (menu != nullptr) {
                    auto *remove = menu->findChild<QPushButton *>(
                        MainWindowUI::folderMenuActionName(MainWindowUI::FolderActionRemove));
                    auto *removeOther = menu->findChild<QPushButton *>(
                        MainWindowUI::folderMenuActionName(MainWindowUI::FolderActionDelete));
                    auto *open = menu->findChild<QPushButton *>(
                        MainWindowUI::folderMenuActionName(MainWindowUI::FolderActionOpen));
                    check(remove != nullptr && removeOther != nullptr && !remove->isEnabled() &&
                              !removeOther->isEnabled(),
                          "自带那一份的「移除」「删除」是灰的");
                    check(open != nullptr && open->isEnabled(),
                          "同一块菜单里「打开」还是亮的（没把整块一起灰掉）");
                    check(remove != nullptr &&
                              remove->toolTip() !=
                                  MainWindowUI::folderActionTooltip(
                                      MainWindowUI::FolderActionRemove),
                          "灰掉的那一项换了一句说明它**为什么**点不了",
                          remove != nullptr ? remove->toolTip() : QString());
                    // 灰掉那一档的垃圾桶**也不红**：红图标落在点不动的按钮上会被读成
                    // "这一项现在能点" —— 和字那边是同一条规矩（QSS 里那句 `:enabled`）。
                    check(removeOther != nullptr &&
                              !hasDangerRed(removeOther->icon(), QIcon::Disabled),
                          "自带那一份的垃圾桶不是红的（灰着的按钮上画红，会读成能点）");
                    // 再点同一颗齿轮 = **收回**（点一下开、再点一下关）。这是个真行为：
                    // 写成"又叠一块"的话用户点两下会看到两块框压在一起。
                    if (QPushButton *gear = gearOf(0)) {
                        gear->click();
                        QApplication::processEvents();
                        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                        check(menuNow() == nullptr,
                              "同一颗齿轮点第二下是**收回**（不是又叠一块）");
                    }
                }
            }
            // 收尾：把回调摘掉（它捕的是这个块里的局部变量），别留给后面可能加的节
            // 一个悬空引用。
            ui.setFolderActionHandler(nullptr);
        }
    }
    return 0;
}


