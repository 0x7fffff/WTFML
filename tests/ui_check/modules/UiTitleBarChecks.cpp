#include "Support.hpp"
int UiTitleBarChecks(QMainWindow &window, MainWindowUI &ui) {
    {
        // 要求：「能把整个外边框设计成完全自定义的吗，就是不用默认的那个顶上一条白色的
        // 那个，而是用pcl的那种自己设计顶上」。
        //
        // 这一节管的是**顶上摆什么**（ui/TitleBar）。"系统那条白条到底去掉了没有"是 Win32
        // 那一层的事（ui/WindowChrome），offscreen 里没有真 HWND、验不了 —— 那份是拿
        // build/tools/chromeprobe2 对着真窗口量的（客户区铺满整窗、缩放边、HTCAPTION、
        // 双击不最大化）。这里只验**接线与摆位**：挂件挂对了、条通栏贴在最上面、
        // 中间那一片是空的（拖窗区）、六颗按钮（窗口两颗 + 四个功能四颗）的格子与颜色、
        // 点下去发哪一条信号。
        TitleBar *strip = ui.titleBar();
        WindowChrome *chrome = ui.chrome();
        check(strip != nullptr && chrome != nullptr, "Z11 顶上那一条、外框挂件都搭出来了");
        if (strip == nullptr || chrome == nullptr) {
            std::printf("      （条或挂件没搭出来，这一节剩下的断言跳过）\n");
        } else {
            check(chrome->strip() == strip, "Z11 外框认的那一条就是布局里那一条（不是两份）");
            // 外框这件事在**窗口标志**上留下的唯一痕迹。去掉系统标题栏是"客户区铺满整窗"
            // 的前提，理由见 WindowChrome.hpp 里"为什么偏偏用 FramelessWindowHint"那一段。
            check((window.windowFlags() & Qt::FramelessWindowHint) != 0,
                  "Z11 主窗口带上了 FramelessWindowHint（系统那条标题栏去掉了）");
            check((window.windowFlags() & Qt::WindowMaximizeButtonHint) == 0,
                  "Z11 主窗口没有最大化（右边因此只有最小化/关闭两颗窗口按钮，不是三颗）");

            // ---------- 摆位：通栏贴顶，选项卡紧接在它下面 ----------
            check(strip->height() == TitleBar::barHeight(),
                  "Z11 条高就是 TitleBar::barHeight()（唯一出处）",
                  QStringLiteral("实测 %1 期望 %2").arg(strip->height()).arg(TitleBar::barHeight()));
            check(strip->geometry().top() == 0 && strip->x() == 0 &&
                      strip->width() == window.centralWidget()->width(),
                  "Z11 条通栏贴在最上面（左右也顶到边）",
                  QStringLiteral("条 %1,%2 %3x%4 / 窗口 %5")
                      .arg(strip->x()).arg(strip->y()).arg(strip->width()).arg(strip->height())
                      .arg(window.width()));
            QTabWidget *tabsIn = ui.getTabWidget();
            check(tabsIn != nullptr && tabsIn->geometry().top() == TitleBar::barHeight(),
                  "Z11 标签页那一块紧接在条下面（覆盖层开出来也盖不到条）",
                  QStringLiteral("标签页 top=%1 条高=%2")
                      .arg(tabsIn != nullptr ? tabsIn->geometry().top() : -1)
                      .arg(TitleBar::barHeight()));

            // ---------- 那五个字母 + 四个功能搬到条上 ----------
            // 要求：「现在把顶上标题换成五个字母，然后把四个选项卡放到最顶上去，通过顶端
            // 按钮打开四个功能」。
            //
            // 做法是**"条上摆四颗按钮 + 把标签栏藏起来"**，不是 `QTabWidget::setTabBar()`
            // （理由见 TitleBar.hpp 文件头）。"藏起来"这件事有个能算的后果：Qt 的
            // `initStyleOption()` 里那句 `if (d->tabs->isVisibleTo(this))` 一判到标签栏
            // 不可见，`tabBarSize` 就归零、内容页于是铺满整个 QTabWidget —— 所以下面那条
            // "当前页占的正好是整个 tabWidget"既是结果、也是判据（它是覆盖层变大到整页的原因）。
            check(tabsIn != nullptr && !tabsIn->tabBar()->isVisible(),
                  "★ Z11 顶上那条标签栏藏起来了（四个功能搬到条上，不是把它搬出去）");
            check(TitleBar::defaultBrandText() == QStringLiteral("WTFML"),
                  "★ Z11 条上默认写的是那五个字母", TitleBar::defaultBrandText());
            check(strip->titleText() == TitleBar::defaultBrandText(),
                  "Z11 没改过时条上那行字就是默认那五个字母（不跟窗口标题走）",
                  strip->titleText());
            const int tabCount = tabsIn != nullptr ? tabsIn->count() : -1;
            check(tabCount == 4 && strip->navCount() == tabCount,
                  "★ Z11 四个功能 = 四颗导航按钮（颗数跟标签页数走）",
                  QStringLiteral("标签页 %1 ／ 按钮 %2").arg(tabCount).arg(strip->navCount()));
            if (tabsIn != nullptr && strip->navCount() == tabCount && tabCount > 0) {
                // 标签只有一个出处：`QTabWidget::tabText()`。按钮上写的就是它。
                QStringList navLabels;
                QStringList tabLabels;
                for (int i = 0; i < tabCount; ++i) {
                    QAbstractButton *button = strip->navButton(i);
                    navLabels.append(button != nullptr ? button->text() : QStringLiteral("<null>"));
                    tabLabels.append(tabsIn->tabText(i));
                }
                check(navLabels == tabLabels,
                      "★ Z11 四颗按钮的字就是四个选项卡的字（唯一出处 tabText()）",
                      navLabels.join(QStringLiteral(" / ")));

                {
                    const qreal basePoints = QApplication::font().pointSizeF();
                    bool allSame = strip->navCount() > 0;
                    bool bigger = true;
                    QString fontText;
                    for (int i = 0; i < strip->navCount(); ++i) {
                        const QFont f = strip->navButton(i) != nullptr ? strip->navButton(i)->font()
                                                                       : QFont();
                        fontText += QStringLiteral("%1 ").arg(f.pointSizeF());
                        allSame = allSame && f.pointSizeF() == strip->navButton(0)->font().pointSizeF();
                        bigger = bigger && f.pointSizeF() > basePoints;
                    }
                    check(basePoints > 0 && allSame && bigger
                              && strip->navButton(0)->font().pointSizeF()
                                     == basePoints + TitleBar::navFontPointDelta(),
                          "★ Z11 四颗导航的字比正文大一档（「上面四个按钮的文本也做大点」）、四颗一个样",
                          QStringLiteral("正文 %1 / 四颗 %2 / 增量 %3")
                              .arg(basePoints)
                              .arg(fontText)
                              .arg(TitleBar::navFontPointDelta()));
                }

                // 字大了之后还得**放得下**：那一格是 `drawText(rect(), AlignCenter, …)`，
                // 一行字宽过格子就会被两头切掉（不报错、也不提示）。所以这里量最宽的那一个
                // 标签 —— 而且**加粗那一档也要量**（当前那一颗是 `setBold(true)` 画的，
                // 同一行字会再宽一点）。留白多少写进注释里，往后谁调大了能一眼看出还剩多少。
                {
                    int worst = 0;
                    int worstWide = 0;
                    QString worstText;
                    for (int i = 0; i < tabCount; ++i) {
                        QAbstractButton *button = strip->navButton(i);
                        if (button == nullptr) {
                            continue;
                        }
                        QFontMetrics fm(button->font());
                        QFont boldFont = button->font();
                        boldFont.setBold(true);
                        const QFontMetrics boldFm(boldFont);
                        const int plain = fm.horizontalAdvance(button->text());
                        const int wide = boldFm.horizontalAdvance(button->text());
                        if (wide > worstWide) {
                            worstWide = wide;
                            worst = plain;
                            worstText = button->text();
                        }
                    }
                    check(!worstText.isEmpty() && worstWide <= TitleBar::navItemWidth(),
                          "★ Z11 变大之后标签仍然放得下那一格（含加粗的当前那一颗）",
                          QStringLiteral("最宽「%1」普通 %2 / 加粗 %3 / 格宽 %4")
                              .arg(worstText)
                              .arg(worst)
                              .arg(worstWide)
                              .arg(TitleBar::navItemWidth()));
                }

                // 几何：四颗都压在 buttonInset() 那条不变式上（它们的高也是 buttonSize()），
                // 宽一律 navItemWidth()，从左到右一个挨一个、不叠、也不压到窗口那两颗上面。
                const QSize navCell(TitleBar::navItemWidth(), TitleBar::buttonSize());
                bool cellOk = true;
                bool insetOk = true;
                bool orderOk = true;
                QString geoText;
                for (int i = 0; i < tabCount; ++i) {
                    QAbstractButton *button = strip->navButton(i);
                    if (button == nullptr) {
                        cellOk = false;
                        insetOk = false;
                        orderOk = false;
                        break;
                    }
                    cellOk = cellOk && button->size() == navCell;
                    insetOk = insetOk && button->y() == TitleBar::buttonInset();
                    // 同一组里一颗挨一颗：每一颗的左边缘 = 上一颗的右边缘 + 1。
                    orderOk = orderOk && (i == 0 || button->x() == strip->navButton(i - 1)->geometry().right() + 1);
                    geoText += QStringLiteral("%1@%2 ").arg(button->text()).arg(button->x());
                }
                check(cellOk && insetOk,
                      "★ Z11 四颗导航按钮都是 navItemWidth() x buttonSize()、上留 buttonInset()",
                      geoText);
                check(orderOk, "Z11 四颗从左到右一个挨一个（不叠、不漏缝）", geoText);

                // **那四颗的正中 == 整条的正中**（要求：「上面的四个居中」）。
                // 这一条量的是**结果**、不是 TitleBar.cpp 里那个数（`kNavGroupLeftMargin`
                // 的推导就在它旁边）：布局是 [左留白][stretchA][导航组][stretchB][两颗窗口
                // 按钮]，两段 stretch 一样长 ⇒ "居中"这件事完全由左留白定，而左留白只跟
                // buttonSize() 有关、**和导航组自己多宽无关**。所以这条同时钉住了
                // "以后加一颗按钮（或者改 navItemWidth()）不许把居中弄丢"。
                // 容差 1 px：条宽是奇数时两边不可能各占整数。
                const int rowLeft = strip->navButton(0)->x();
                const int rowRight = strip->navButton(tabCount - 1)->geometry().right() + 1;
                const int rowCenter = (rowLeft + rowRight) / 2;
                check(qAbs(rowCenter - strip->width() / 2) <= 1,
                      "★ Z11 四颗导航按钮在条上居中（那一组的正中就是整条的正中）",
                      QStringLiteral("组 %1..%2 正中 %3 ／ 条宽 %4 正中 %5")
                          .arg(rowLeft)
                          .arg(rowRight)
                          .arg(rowCenter)
                          .arg(strip->width())
                          .arg(strip->width() / 2));
                check(strip->navButton(tabCount - 1)->geometry().right() <
                          strip->minimizeButton()->geometry().left(),
                      "★ Z11 最右那颗导航按钮不压到最小化那颗上（拖窗区那一格还留着）",
                      QStringLiteral("末颗右 %1 ／ 最小化左 %2")
                          .arg(strip->navButton(tabCount - 1)->geometry().right())
                          .arg(strip->minimizeButton()->geometry().left()));

                // ---------- 点一颗 = 翻到那一页 ----------
                // 两条连着验：条上发的是**哪一颗的号**，以及主窗口上确实接成了"翻页"
                // （`currentChanged` 反过来又把"当前这一页"标回条上 —— 那一份也得对）。
                int lastNav = -1;
                const QMetaObject::Connection navSpy =
                    QObject::connect(strip, &TitleBar::navRequested, strip,
                                     [&lastNav](int index) { lastNav = index; });
                const int before = tabsIn->currentIndex();
                const int target = (before + 1) % tabCount;  // 换一页（别点当前那一页，那样看不出来）
                if (QAbstractButton *button = strip->navButton(target)) {
                    button->click();
                    QApplication::processEvents();
                }
                check(lastNav == target, "Z11 点第几颗就发 navRequested(几)",
                      QStringLiteral("发了 %1 期望 %2").arg(lastNav).arg(target));
                check(tabsIn->currentIndex() == target, "★ Z11 点了真的翻到那一页",
                      QStringLiteral("当前页 %1 期望 %2").arg(tabsIn->currentIndex()).arg(target));
                check(strip->currentNav() == target && strip->currentNav() != before,
                      "★ Z11 翻页之后「当前这一颗」跟着走（currentChanged → setCurrentNav 接上了）",
                      QStringLiteral("标着 %1").arg(strip->currentNav()));
                QObject::disconnect(navSpy);
                // 当前那一颗画得跟别的不一样（加粗 + 底下一条横线）：只比"有一颗是当前、
                // 其余都不是"这一条 —— 那两种画法各有像素断言在下面。
                int currentCount = 0;
                for (int i = 0; i < tabCount; ++i) {
                    auto *button = qobject_cast<TitleBarButton *>(strip->navButton(i));
                    currentCount += (button != nullptr && button->isCurrent()) ? 1 : 0;
                }
                check(currentCount == 1, "★ Z11 四颗里正好一颗是「当前」",
                      QStringLiteral("%1 颗").arg(currentCount));

                // ---------- 当前那一颗**画得不一样**（按像素量） ----------
                // 那两种画法（加粗 + 底下一条横线）里，**横线**是能按像素断言的：它用的是
                // 当前那一档的墨色（正文色），别的几颗那一行什么都不该有。
                // NOTE: 量的必须是**"颜色由我们自己决定"的那一格** —— 这儿正是（`setInkColor()`
                // 给的就是这两个色）。整行去比会被别的东西带偏。
                const QColor strongInk = strip->palette().color(QPalette::WindowText);
                const int other = (target + 1) % tabCount;
                auto bottomInkCount = [&strongInk](QWidget *button) {
                    const QImage shot = captureWidget(button);
                    int hits = 0;
                    for (int y = qMax(0, shot.height() - 2); y < shot.height(); ++y) {
                        for (int x = 0; x < shot.width(); ++x) {
                            if (shot.pixelColor(x, y) == strongInk) {
                                ++hits;
                            }
                        }
                    }
                    return hits;
                };
                const int currentInk = bottomInkCount(strip->navButton(target));
                const int otherInk = bottomInkCount(strip->navButton(other));
                check(currentInk > 0 && otherInk == 0,
                      "★ Z11 底下那条横线只画在「当前」那一颗上（其余几颗底下一笔都没有）",
                      QStringLiteral("当前 %1 个像素 / 别的 %2 个").arg(currentInk).arg(otherInk));
            }

            check(TitleBar::buttonInset() >= WindowChrome::resizeBorder(),
                  "★ Z11 按钮上面留的空白 ≥ 系统那圈缩放边（否则按按钮上沿=缩放窗口）",
                  QStringLiteral("buttonInset=%1 缩放边=%2")
                      .arg(TitleBar::buttonInset())
                      .arg(WindowChrome::resizeBorder()));
            // NOTE: 那句说明里的三个数是**现算的**（从前写死成「44 = 8 + 28 + 8」）——
            // 条高 从 44 抬到 52 那一趟，断言本身是绿的、只有这句话在说谎。
            check(TitleBar::barHeight() == TitleBar::buttonInset() * 2 + TitleBar::buttonSize(),
                  QStringLiteral("Z11 条高 = 上留白 + 按钮 + 下留白（%1 = %2 + %3 + %2）")
                      .arg(TitleBar::barHeight()).arg(TitleBar::buttonInset())
                      .arg(TitleBar::buttonSize()),
                  QStringLiteral("%1 = %2 + %3 + %2")
                      .arg(TitleBar::barHeight()).arg(TitleBar::buttonInset())
                      .arg(TitleBar::buttonSize()));

            // ---------- 条上那两颗按钮的格子 ----------
            QAbstractButton *minBtn = strip->minimizeButton();
            QAbstractButton *closeBtn = strip->closeButton();
            check(minBtn != nullptr && closeBtn != nullptr, "Z11 最小化与关闭两颗都找得到");
            if (minBtn != nullptr && closeBtn != nullptr) {
                const QSize cell(TitleBar::buttonSize(), TitleBar::buttonSize());
                check(minBtn->size() == cell && closeBtn->size() == cell,
                      "Z11 两颗都是 buttonSize() 见方",
                      QStringLiteral("%1x%2 / %3x%4").arg(minBtn->width()).arg(minBtn->height())
                          .arg(closeBtn->width()).arg(closeBtn->height()));
                check(minBtn->y() == TitleBar::buttonInset() && closeBtn->y() == TitleBar::buttonInset(),
                      "Z11 两颗按钮上下居中（上留 buttonInset()）",
                      QStringLiteral("y=%1,%2").arg(minBtn->y()).arg(closeBtn->y()));
                check(closeBtn->geometry().right() == strip->width() - 1 - TitleBar::windowButtonsRightMargin()
                          && TitleBar::windowButtonsRightMargin() > 0
                          && TitleBar::windowButtonsRightMargin() < TitleBar::buttonSize(),
                      "★ Z11 关闭那颗离条的最右边空出一格（「稍稍往左边靠一点」）",
                      QStringLiteral("右边缘 %1 / 条宽 %2 / 右边距 %3")
                          .arg(closeBtn->geometry().right()).arg(strip->width())
                          .arg(TitleBar::windowButtonsRightMargin()));
                check(minBtn->geometry().left() == closeBtn->geometry().left() - TitleBar::buttonSize(),
                      "Z11 最小化紧挨在关闭左边");
                check(minBtn->focusPolicy() == Qt::NoFocus && closeBtn->focusPolicy() == Qt::NoFocus,
                      "Z11 两颗按钮不抢键盘焦点");

                // 条上那几片空白**必须是空的**：`WindowChrome` 判"这一手是不是拖窗"用的
                // 就是 `strip->childAt()`。往后谁往条上加一个铺满的空白 label（比如"顺手用
                // QLabel 显示标题"），这几条会当场红。
                // NOTE: 那四颗导航按钮**搬到正中间**之后（用户：「上面的四个
                // 居中」），"正中"从此**不是**空的 —— 空的是它左右那两片。所以这里改成
                // 量那两片，并且反过来把"正中归按钮"也钉住（居中的命中测试后果）。
                const QList<QWidget *> direct =
                    strip->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
                // 起条上多了那四颗导航按钮 ⇒ 是 **6 颗**（2 窗口 + 4 导航），
                // 一颗都不许多。这里刻意写死这个数、不用 `2 + navCount()`：写死的意思是
                // "加子控件这件事必须有人来改这一行"。
                check(direct.size() == 6,
                      "★ Z11 条上只有六颗按钮（2 窗口 + 4 导航），没有别的子控件",
                      QStringLiteral("%1 个：%2").arg(direct.size()).arg(
                          [&direct] {
                              QStringList names;
                              for (QWidget *w : direct) {
                                  names.append(w->objectName());
                              }
                              return names.join(QStringLiteral(","));
                          }()));
                // 正中归导航按钮（居中的必然结果 —— 这一格从此是 HTCLIENT，不是拖窗区）。
                QWidget *atCenter = strip->childAt(strip->width() / 2, strip->height() / 2);
                bool centerIsNav = false;
                for (int i = 0; i < strip->navCount(); ++i) {
                    centerIsNav = centerIsNav || atCenter == strip->navButton(i);
                }
                check(centerIsNav,
                      "★ Z11 条的正中现在是导航按钮那一颗（居中之后那一片归按钮，不归拖窗）",
                      atCenter != nullptr ? atCenter->objectName() : QStringLiteral("<空>"));
                // 那两片空白：名字与导航之间、导航与最小化之间。**坐标是算的**，不是写死的
                // 数字 —— 居中的位置一变，这里跟着变。
                QAbstractButton *firstNav = strip->navButton(0);
                QAbstractButton *lastNav = strip->navButton(strip->navCount() - 1);
                const int gapLeftX =
                    firstNav != nullptr ? (TitleBar::buttonInset() + firstNav->x()) / 2 : -1;
                const int gapRightX = lastNav != nullptr
                                          ? (lastNav->geometry().right() + minBtn->geometry().left()) / 2
                                          : -1;
                check(strip->childAt(gapLeftX, strip->height() / 2) == nullptr &&
                          strip->childAt(gapRightX, strip->height() / 2) == nullptr &&
                          strip->childAt(TitleBar::buttonInset() + 6, strip->height() / 2) == nullptr,
                      "★ Z11 导航组左右那两片还是空的（居中没把拖窗区挤掉）",
                      QStringLiteral("%1 / %2 / 14").arg(gapLeftX).arg(gapRightX));
                // 反过来：按钮自己那一片**必须**问得出来（问不出来就成了"点按钮变成拖窗"）。
                check(strip->childAt(closeBtn->geometry().center()) == closeBtn &&
                          strip->childAt(minBtn->geometry().center()) == minBtn,
                      "Z11 两颗按钮自己那一片问得出来（点它们不会变成拖窗）");
            }

            // ---------- 点下去发哪一条信号 ----------
            // NOTE: 这一组用**单独摆出来的一条**（自己的父控件），不去点主窗口上那两颗 ——
            // 主窗口那颗「×」在 `MainWindowUI` 里是接在 `parent->close()` 上的，点一下
            // 窗口真会关，后面那些像素断言就没得量了。"只发信号、自己不动窗"这件事
            // 要在一条**没接线**的条上验，而"接上了没有"在主窗口那条上验（这一节最后一条）。
            QWidget soloHost;
            soloHost.resize(600, 400);
            soloHost.show();
            TitleBar solo(&soloHost);
            solo.resize(600, TitleBar::barHeight());
            int minimizeHits = 0;
            int closeHits = 0;
            QObject::connect(&solo, &TitleBar::minimizeRequested, &solo, [&minimizeHits] { ++minimizeHits; });
            QObject::connect(&solo, &TitleBar::closeRequested, &solo, [&closeHits] { ++closeHits; });
            if (solo.minimizeButton() != nullptr) {
                solo.minimizeButton()->click();
                QApplication::processEvents();
            }
            check(minimizeHits == 1 && closeHits == 0, "Z11 点「—」只发最小化那一条",
                  QStringLiteral("最小化 %1 / 关闭 %2").arg(minimizeHits).arg(closeHits));
            if (solo.closeButton() != nullptr) {
                solo.closeButton()->click();
                QApplication::processEvents();
            }
            check(minimizeHits == 1 && closeHits == 1, "Z11 点「×」只发关闭那一条",
                  QStringLiteral("最小化 %1 / 关闭 %2").arg(minimizeHits).arg(closeHits));
            // 条**自己不动窗**（不 call window()->close()）：这是"它能单独摆出来点"的前提，
            // 也是"点关闭之前要不要先问一句"这类事只能改在 MainWindow 那一侧的原因。
            check(soloHost.isVisible(), "★ Z11 条自己不动窗（点了「×」父控件照旧在）");

            // ---------- 颜色：量渲染出来的多数色 ----------
            // 改了设计：条底色**不再是"窗口底色压暗一档"**，而是**主题色那一支**
            // （要求：「使用 #56eb6c 作为默认主题颜色（顶栏用）」）。所以原来那条
            // "上下两条框同色"的断言**作废了** —— 底下那条留着中性色（它装的是启动/日志
            // 那一排控件），现在量的是"各自都铺着它该铺的那一支"。
            // NOTE: 比的是 `.rgb()`，不是 `QColor` 直接比 —— `darker()` 给的是 Hsv spec，
            // 而量出来的是 Rgb（这条坑本文件里踩过一次，见下面最小化那颗的注释）。
            QFrame *bottom = ui.getBottomBar();
            const QColor stripFill = dominantColor(captureWidget(strip), strip->rect());
            const QColor bottomFill =
                bottom != nullptr ? dominantColor(captureWidget(bottom), bottom->rect()) : QColor();
            const QColor accentPure = Theme::accentShade(Theme::AccentShade::Pure);
            check(stripFill.isValid() && stripFill.rgb() == accentPure.rgb(),
                  "★★ Z11 顶上那一条铺的就是**主题色**（用户点名的那一支绿）",
                  QStringLiteral("条=%1 主题色=%2").arg(stripFill.name(), accentPure.name()));
            const QColor windowFill = window.palette().color(QPalette::Window);
            check(bottomFill.isValid() &&
                      bottomFill.rgb() == windowFill.darker(105).rgb() &&
                      bottomFill.rgb() != stripFill.rgb(),
                  "★ Z11 底下那条还是中性色（窗口底色压暗一档）—— 上下两条**故意不一样了**",
                  QStringLiteral("底栏=%1 该是 %2 / 条=%3")
                      .arg(bottomFill.name(), windowFill.darker(105).name(), stripFill.name()));

            // 悬停：关闭那颗红底白叉、最小化那颗只是压暗一档。
            // NOTE: 悬停那一位**默认没人填**（Qt 不会自己产生 HoverEnter），所以
            // `WA_Hover` 与 `mouseTracking` 两条得一起在 —— 少哪条都是"悬停色再也
            // 不出现"，而这件事从代码上看不出来。这里把"属性在位"与"收到事件之后
            // 真的变色"两件都钉住，并且悬停是**自己发事件**触发的（offscreen 没有鼠标）。
            if (closeBtn != nullptr && minBtn != nullptr) {
                auto *closeWidget = static_cast<TitleBarButton *>(closeBtn);
                auto *minWidget = static_cast<TitleBarButton *>(minBtn);
                check(closeWidget != nullptr && closeWidget->testAttribute(Qt::WA_Hover) &&
                          closeWidget->hasMouseTracking() && minWidget != nullptr &&
                          minWidget->testAttribute(Qt::WA_Hover) && minWidget->hasMouseTracking(),
                      "★ Z11 两颗按钮的 WA_Hover 与 mouseTracking 都开着（少哪条悬停色都不出现）");

                const QColor danger = MainWindowUI::folderDangerColor(strip->palette());
                // 不悬停：底是全透（条底色透上来），所以图里只有那几笔字形。
                // 条上那个墨色现在是**白的**（顶栏是主题色那一支绿，要求
                // 「顺便那个最小化和 x 都换成白的」）—— 所以这里比的是"画出来的就是条上
                // 那个墨色"，而"是不是白的"另有下面那一条明说，不再靠"白 0 个像素"。
                const QImage plainShot = captureWidget(closeBtn);
                int plainWhite = 0;
                int plainInk = 0;
                const QColor ink = closeBtn->palette().color(QPalette::WindowText);
                for (int y = 0; y < plainShot.height(); ++y) {
                    for (int x = 0; x < plainShot.width(); ++x) {
                        const QColor c = plainShot.pixelColor(x, y);
                        if (c == QColor(Qt::white)) {
                            ++plainWhite;
                        }
                        if (c == ink) {
                            ++plainInk;
                        }
                    }
                }
                check(plainInk > 0 && plainWhite == (ink == QColor(Qt::white) ? plainInk : 0),
                      "Z11 不悬停时那颗「×」就是条上那个墨色的几笔、底是透的",
                      QStringLiteral("墨色 %1 个像素 / 白 %2 个").arg(plainInk).arg(plainWhite));
                check(ink == QColor(Qt::white) && minBtn->palette().color(QPalette::WindowText) ==
                                                      QColor(Qt::white),
                      "★★ Z11 两颗窗口按钮**平时**就是白墨（绿条上只有白读得出来；红底那一套"
                      "是悬停时的，见下）",
                      QStringLiteral("× %1 / — %2").arg(ink.name(),
                                                        minBtn->palette().color(QPalette::WindowText).name()));

                QHoverEvent enter(QEvent::HoverEnter, QPointF(2, 2), QPointF(2, 2),
                                  QPointF(2, 2));
                QApplication::sendEvent(closeBtn, &enter);
                QApplication::processEvents();
                check(closeWidget->isHovered(), "Z11 悬停事件到了（isHovered() 为真）");
                const QImage hoverShot = captureWidget(closeBtn);
                const QColor hoverFill = dominantColor(hoverShot, closeBtn->rect());
                check(hoverFill == danger,
                      "★ Z11 悬停底色就是全仓那一处危险操作的红",
                      QStringLiteral("量到 %1 期望 %2").arg(hoverFill.name(), danger.name()));
                int hoverWhite = 0;
                for (int y = 0; y < hoverShot.height(); ++y) {
                    for (int x = 0; x < hoverShot.width(); ++x) {
                        if (hoverShot.pixelColor(x, y) == QColor(Qt::white)) {
                            ++hoverWhite;
                        }
                    }
                }
                check(hoverWhite > 0, "★ Z11 悬停时那几笔转白（红底上换白墨才看得清）",
                      QStringLiteral("白 %1 个像素（不悬停时是 0）").arg(hoverWhite));
                QHoverEvent leave(QEvent::HoverLeave, QPointF(2, 2), QPointF(2, 2),
                                  QPointF(2, 2));
                QApplication::sendEvent(closeBtn, &leave);
                QApplication::processEvents();
                check(!closeWidget->isHovered(), "Z11 移开之后回到不悬停态");

                QHoverEvent minEnter(QEvent::HoverEnter, QPointF(2, 2), QPointF(2, 2),
                                     QPointF(2, 2));
                QApplication::sendEvent(minBtn, &minEnter);
                QApplication::processEvents();
                const QColor minHoverFill = dominantColor(captureWidget(minBtn), minBtn->rect());
                // NOTE: 这里比的是 `.rgb()`，**不是 QColor 直接比** —— `QColor::darker()` 返回的
                // 颜色 spec 是 **Hsv**，而量出来的那个是 Rgb；`operator==` 连 spec 一起比，
                // 于是两个 `name()` 打印出来一模一样、`==` 却是假（ 踩过：
                // FAIL 那一行写成「量到 #c6c6c6 期望 #c6c6c6」，看着像见了鬼）。
                const QColor expectMinHover = stripFill.darker(115);
                check(minHoverFill.rgb() != danger.rgb() &&
                          minHoverFill.rgb() == expectMinHover.rgb(),
                      "★ Z11 最小化那颗悬停只是压暗一档（不是那个红）",
                      QStringLiteral("量到 %1 期望 %2（危险红是 %3）")
                          .arg(minHoverFill.name(), expectMinHover.name(), danger.name()));
                QHoverEvent minLeave(QEvent::HoverLeave, QPointF(2, 2), QPointF(2, 2),
                                     QPointF(2, 2));
                QApplication::sendEvent(minBtn, &minLeave);
                QApplication::processEvents();

                // 导航那四颗的悬停**和最小化那一颗同一档**（条底色再压暗一档、不是那个红）。
                // NOTE: 这一条是照着一段真踩过的坑写的 —— `setNavItems()` 里原来写的是
                // `barColor(palette())...`，而调色板里的 Window **已经是条底色**
                // （构造函数把压暗过的那一档写进去了），于是那是**第二次压暗**、
                // 量出来比方框里该有的深一档。和"两颗按钮的悬停色"是同一个坑的两种形状。
                if (strip->navCount() > 0) {
                    // 挑一颗**不是当前**的（当前那颗多了加粗和底下一条横线，底色不好当多数票）。
                    const int plainNav =
                        strip->currentNav() == 0 ? 1 % strip->navCount() : 0;
                    if (QAbstractButton *navBtn = strip->navButton(plainNav)) {
                        QHoverEvent navEnter(QEvent::HoverEnter, QPointF(2, 2), QPointF(2, 2),
                                             QPointF(2, 2));
                        QApplication::sendEvent(navBtn, &navEnter);
                        QApplication::processEvents();
                        const QColor navHoverFill = dominantColor(captureWidget(navBtn), navBtn->rect());
                        check(navHoverFill.rgb() != danger.rgb() &&
                                  navHoverFill.rgb() == expectMinHover.rgb(),
                              "★ Z11 导航按钮的悬停色和最小化那颗同一档（条底色再压暗一档）",
                              QStringLiteral("量到 %1 期望 %2").arg(navHoverFill.name(), expectMinHover.name()));
                        QHoverEvent navLeave(QEvent::HoverLeave, QPointF(2, 2), QPointF(2, 2),
                                             QPointF(2, 2));
                        QApplication::sendEvent(navBtn, &navLeave);
                        QApplication::processEvents();
                    }
                }
            }

            // ---------- 条上那行字 ----------
            // 那五个字母是**写死的品牌名**，条上写的和窗口标题是**两件事**：窗口标题还得在
            // Alt-Tab / 任务栏里读得出来（`MainWindowUI::setupUi` 里那句 setWindowTitle），
            // 条上只写那五个字母。
            check(!strip->titleText().isEmpty() &&
                      strip->titleText() == TitleBar::defaultBrandText(),
                  "★ Z11 条上那行字就是默认那个名字（不跟窗口标题走）", strip->titleText());
            // 名字再长也不许把条顶宽（条宽 = 整窗宽，字是 paintEvent 里省略号截的）。
            // 这一条是 E5 那条的邻居："折行 QLabel 会把整窗 sizeHint 顶大"那件事不许从这条上回来。
            const int hintBefore = strip->sizeHint().width();
            const QString longTitle = QString(300, QLatin1Char('W'));
            window.setWindowTitle(longTitle);
            QApplication::processEvents();
            check(strip->sizeHint().width() == hintBefore && window.sizeHint().width() <= 900,
                  "★ Z11 300 字的名字也顶不大条、更顶不大窗口（字是画的，不是控件）",
                  QStringLiteral("条 sizeHint %1 → %2 / 窗口 %3")
                      .arg(hintBefore).arg(strip->sizeHint().width()).arg(window.sizeHint().width()));
            // 窗口标题换成 300 个字之后**条上那行字一个字都不动** —— 这条和上面那条一起，
            // 把"条上的字是从窗口标题来的"这条路彻底堵死（那正是这一轮改掉的东西）。
            check(strip->titleText() == QStringLiteral("WTFML"),
                  "★ Z11 窗口标题换成长长一串，条上照旧是那五个字母", strip->titleText());
            window.setWindowTitle(QStringLiteral("WTF Minecraft Launcher"));
            QApplication::processEvents();

            // ---------- 条上那行字可以在设置页里改（ 加的） ----------
            // 要求：「左上角五个字母默认是这五个，在设置里允许自定义」。
            // 这里量的是**条这一侧**：换过之后画出来的**真的是新的那串**（不是"titleText()
            // 报了个新值、paintEvent 那支笔还在写旧值" —— 那个默认值原来是个静态函数直接
            // 调用的，改成能换之后这条是最容易漏的一处）。
            // 「设置页那一格 → 条」那条接线在 mw_check 的 AG 节。
            const QImage defaultBrandShot = captureWidget(strip);
            strip->setBrandText(QStringLiteral("自定义的名字"));
            QApplication::processEvents();
            check(strip->titleText() == QStringLiteral("自定义的名字"),
                  "★ Z11 setBrandText() 换掉了条上那行字", strip->titleText());
            const QImage customBrandShot = captureWidget(strip);
            check(customBrandShot != defaultBrandShot,
                  "★ Z11 换过之后条**真的重画了**（画的是新的那串，不是报了个新值、笔还写旧的）");
            // 清空 = 回到默认那五个字母（不是"条上什么都不写"）：空串是"我没改过"的表达，
            // 盘上那份也是这么存的（LauncherSettings::titleText() 空 = 默认档）。
            strip->setBrandText(QString());
            QApplication::processEvents();
            check(strip->titleText() == TitleBar::defaultBrandText(),
                  "★ Z11 清空 = 回到默认那五个字母", strip->titleText());
            check(captureWidget(strip) == defaultBrandShot,
                  "Z11 回到默认之后画出来的和一开始一模一样");
            // 两头的空白去掉：名字是从条的最左边开始画的，" WTFML " 看着就是没对齐。
            strip->setBrandText(QStringLiteral("  空格  "));
            QApplication::processEvents();
            check(strip->titleText() == QStringLiteral("空格"),
                  "Z11 两头的空白被去掉了", QStringLiteral("[%1]").arg(strip->titleText()));
            strip->setBrandText(QString());
            QApplication::processEvents();
            check(strip->titleText() == TitleBar::defaultBrandText(),
                  "Z11 收尾：条上回到默认那行字（后面几节量的是干净状态）", strip->titleText());

            // ---------- 外框那两条消息的**契约**（不用真 HWND 也问得出来） ----------
            // `WindowChrome::handleNativeEvent()` 是公开的，喂一发**假的** MSG 就能问它认不认。
            // 这里验的是"契约"；真的 Win32 行为（客户区铺满整窗、那一圈缩放边、
            // 条上判成 HTCAPTION、双击条不最大化）是探针 build/tools/chromeprobe2 对着真 HWND
            // 量的，offscreen 里量不了。
            {
                qintptr ncResult = -1;
                const bool ncClaimed = chromeHandlesNcCalcsize(chrome, &ncResult);
                check(ncClaimed && ncResult == 0,
                      "★ Z11 WM_NCCALCSIZE 那一问回 0（客户区铺满整窗 ⇒ 系统那条标题栏被盖住）",
                      QStringLiteral("认领=%1 结果=%2").arg(ncClaimed).arg(ncResult));

                // 只认那两条：别的原生消息一个字都不碰（碰了就会把系统的行为改掉）。
                qintptr otherResult = -1;
                check(!chromeHandlesPlainMessage(chrome, chromeLeftButtonDownMessage(), &otherResult),
                      "Z11 别的原生消息一个字都不碰（只认 WM_NCCALCSIZE 与 WM_NCHITTEST）");

                // NOTE: WM_NCHITTEST 那条**故意不喂假的** —— 它要的是一个真 HWND 才能谈"点在
                // 哪儿"（GetWindowRect 对空句柄直接失败），拿假 lParam 问出来的只会是
                // "没认领"，那是验了等于没验。
            }

            // ---------- 例外那一扇：游戏日志窗照旧是系统的框 ----------
            // 要求是「最终目标是不出现独立的窗口（**日志除外**）」—— 日志窗是唯一允许
            // 独立的那一扇，`WindowChrome` 只挂在主窗口上，它不该被卷进来。
            {
                GameLogWindow log(QStringLiteral("例外那一扇"));
                check((log.windowFlags() & Qt::FramelessWindowHint) == 0,
                      "★ Z11 「游戏日志」窗照旧带系统标题栏（用户点名的例外没被外框卷进来）");
            }

            // ---------- 最后一条：主窗口上那颗「×」**真的接上了** ----------
            // `MainWindowUI::setupUi` 把 `closeRequested` 接到了 `parent->close()`，所以点它
            // 窗口就得关。放在最末（这一下之后窗口就藏了，后面没得量了）。
            // 和上面那条"条自己不动窗"合起来才是完整的一件事：**条只管发信号，接不接、接去哪儿
            // 由 MainWindowUI 决定**。
            check(window.isVisible(), "Z11 前提：主窗口这会儿还开着");
            if (closeBtn != nullptr) {
                closeBtn->click();
                QApplication::processEvents();
            }
            check(!window.isVisible(), "★ Z11 主窗口上那颗「×」接在窗口上（点它窗口真的关）");
        }
    }
    return 0;
}


