#include "Support.hpp"
int UiScrollBarsChecks(QApplication &app, QFrame * &bar) {
    // ---------- ⑭ 滚动条：嵌在页面边上的一条 ----------
    // 要求：「每一次切页面的时候滚动条都是瞬间出现的，把滚顶条变成一个嵌在页面边上的条，
    // 不要直接一个白色条」。
    //
    // 这一段量三件事：**它有多粗**（`Theme::scrollBarThickness()`）、**它自己画不画底**
    // （不画 ⇒ 露出来的是它底下那一层）、**把手是不是嵌在中间那一条**（左右各留
    // `Theme::scrollBarHandleMargin()`）。硬要求还是那一条：动效不许动几何。
    {
        const bool wasEnabled = Anim::enabled();   // 这个工程平时是关着的，收尾要摆回去
        check(Theme::scrollBarThickness() > 0 && Theme::scrollBarHandleMargin() > 0 &&
                  Theme::scrollBarThickness() > 2 * Theme::scrollBarHandleMargin() &&
                  Theme::scrollBarHandleRadius() ==
                      (Theme::scrollBarThickness() - 2 * Theme::scrollBarHandleMargin()) / 2 &&
                  Theme::scrollBarMinHandleLength() > 0,
              "⑭ 滚动条那几个数自洽（把手宽 = 粗 - 2*留白，圆角 = 半宽 = 胶囊）",
              QStringLiteral("粗 %1 / 留白 %2 / 把手宽 %3 / 圆角 %4 / 最短 %5")
                  .arg(Theme::scrollBarThickness())
                  .arg(Theme::scrollBarHandleMargin())
                  .arg(Theme::scrollBarThickness() - 2 * Theme::scrollBarHandleMargin())
                  .arg(Theme::scrollBarHandleRadius())
                  .arg(Theme::scrollBarMinHandleLength()));

        // 样式表里那两段（竖的、横的）都在，而且是**同一份参数**。
        const QString sheet = Theme::widgetStyleSheet();
        const bool hasVertical = sheet.contains(QStringLiteral("QScrollBar::handle:vertical"));
        const bool hasHorizontal = sheet.contains(QStringLiteral("QScrollBar::handle:horizontal"));
        const bool noArrows =
            sheet.contains(QStringLiteral("QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical"
                                          " { height: 0px; }"));
        check(hasVertical && hasHorizontal && noArrows,
              "⑭ 竖的、横的两段都在，两头那对小箭头被拿掉了（点槽翻页那两条留着）",
              QStringLiteral("竖 %1 / 横 %2 / 箭头拿掉了 %3")
                  .arg(hasVertical ? QStringLiteral("在") : QStringLiteral("不在"),
                       hasHorizontal ? QStringLiteral("在") : QStringLiteral("不在"),
                       noArrows ? QStringLiteral("是") : QStringLiteral("不是")));

        // —— 真搭一个会滚的区，量它画出来是什么样 ——
        QWidget host;
        host.resize(240, 160);
        host.setAutoFillBackground(true);
        QPalette hostPalette = host.palette();
        // 页面底取**当前预设那一支**（`tokens().page`）：把手色也是那一支里挑的
        // （`scrollHandleColor()`），两样同一个底才量得出"把手和它底下那层分得开"。
        const QColor hostFill = Theme::tokens().page;
        hostPalette.setColor(QPalette::Window, hostFill);
        host.setPalette(hostPalette);

        auto *scroll = new QScrollArea(&host);
        scroll->setObjectName(QStringLiteral("wtfml_scrollbar_probe"));
        scroll->setGeometry(0, 0, 240, 160);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *content = new QWidget(scroll);
        content->setObjectName(QStringLiteral("wtfml_scroll_content_probe"));
        content->setMinimumHeight(600);
        scroll->setWidget(content);
        host.show();
        QApplication::processEvents();

        QScrollBar *bar = scroll->verticalScrollBar();
        const QRect barRect(bar->mapTo(scroll, QPoint(0, 0)), bar->size());
        const QImage shot = captureWidgetOn(scroll, hostFill);  // 磨砂下视口不铺底，垫上页面那一层再量

        // 量：一条线的左边（页面那一层）和滚动条那一条里的底色、把手的左右边界。
        const auto at = [&shot](int x, int y) {
            return shot.pixelColor(qBound(0, x, shot.width() - 1), qBound(0, y, shot.height() - 1));
        };
        // 把手在哪一格**问样式要**（`subControlRect`）—— 那一刻的值是 0，把手在最上面那一段，
        // 不是正中间（按"正中间"取样量到的是槽，会得出"一个墨点都没有"）。
        const auto subRect = [](QScrollBar *widget, QStyle::SubControl which) {
            QStyleOptionSlider option;
            option.initFrom(widget);
            option.orientation = widget->orientation();
            option.minimum = widget->minimum();
            option.maximum = widget->maximum();
            option.sliderPosition = widget->sliderPosition();
            option.sliderValue = widget->value();
            option.pageStep = widget->pageStep();
            return widget->style()->subControlRect(QStyle::CC_ScrollBar, &option, which, widget);
        };
        const QRect grooveRect = subRect(bar, QStyle::SC_ScrollBarGroove);
        const QRect sliderRect = subRect(bar, QStyle::SC_ScrollBarSlider);
        const int probeY = barRect.top() + sliderRect.center().y();
        int inked = 0;
        int firstInk = -1;
        int lastInk = -1;
        for (int x = barRect.left(); x <= barRect.right() && x < shot.width(); ++x) {
            const QColor color = at(x, probeY);
            if (color != at(barRect.left() - 1, probeY)) {
                ++inked;
                if (firstInk < 0) {
                    firstInk = x;
                }
                lastInk = x;
            }
        }
        const QColor behind = at(barRect.left() - 1, probeY);
        const QColor barFill = at(barRect.left() + 1, barRect.top() + sliderRect.bottom() + 5);
        std::printf("[探针] 滚动条宽 %d（sizeHint %d）/ 条 %d,%d %dx%d / 槽 %d,%d %dx%d / "
                    "把手 %d,%d %dx%d / 它左边那一格 %s / 槽那一格 %s / 墨 %d..%d（%d px）\n",
                    bar->width(), bar->sizeHint().width(), barRect.x(), barRect.y(),
                    barRect.width(), barRect.height(), grooveRect.x(), grooveRect.y(),
                    grooveRect.width(), grooveRect.height(), sliderRect.x(), sliderRect.y(),
                    sliderRect.width(), sliderRect.height(), qPrintable(behind.name()),
                    qPrintable(barFill.name()), firstInk, lastInk, inked);
        std::printf("[探针] 把手上的色 %s / 悬停档 %s / 拖动档 %s\n",
                    qPrintable(Theme::scrollHandleColor().name(QColor::HexArgb)),
                    qPrintable(Theme::scrollHandleHoverColor().name(QColor::HexArgb)),
                    qPrintable(Theme::scrollHandlePressedColor().name(QColor::HexArgb)));

        check(bar->width() == Theme::scrollBarThickness(),
              "⑭ 真滚动条就占 `scrollBarThickness()` 那么宽",
              QStringLiteral("控件宽 %1 / 常数 %2").arg(bar->width()).arg(Theme::scrollBarThickness()));
        check(inked == Theme::scrollBarThickness() - 2 * Theme::scrollBarHandleMargin(),
              "★★ ⑭ 把手是嵌在中间那一条：左右各留 `scrollBarHandleMargin()`，就是那个胶囊",
              QStringLiteral("墨迹 %1 px（%2..%3）/ 期望 %4 px")
                  .arg(inked)
                  .arg(firstInk)
                  .arg(lastInk)
                  .arg(Theme::scrollBarThickness() - 2 * Theme::scrollBarHandleMargin()));
        check(firstInk == barRect.left() + Theme::scrollBarHandleMargin() &&
                  lastInk == barRect.right() - Theme::scrollBarHandleMargin(),
              "★★ ⑭ 留白是两边**一样**的（不是只往一边推）",
              QStringLiteral("左边留 %1 px / 右边留 %2 px（都该是 %3）")
                  .arg(firstInk - barRect.left())
                  .arg(barRect.right() - lastInk)
                  .arg(Theme::scrollBarHandleMargin()));
        check(barFill == behind,
              "★★ ⑭ 滚动条自己不画底 —— 露出来的就是它下面那一层（不再是那根白条）",
              QStringLiteral("槽上量到 %1 / 它左边那一格 %2")
                  .arg(barFill.name(), behind.name()));
        // 槽**就是整条**：Fusion 出厂那一条两头各留 ~15 px 给小箭头，槽只有中间那一截；
        // 这里量到的槽 = `0,0 宽x整高` ⇒ 那两个三角连"位置"都没有了。
        check(grooveRect == QRect(0, 0, bar->width(), bar->height()),
              "★★ ⑭ 两头那对小箭头连位置都没了（槽 = 整条，不是中间那一截）",
              QStringLiteral("槽 %1,%2 %3x%4 / 条 %5x%6")
                  .arg(grooveRect.x())
                  .arg(grooveRect.y())
                  .arg(grooveRect.width())
                  .arg(grooveRect.height())
                  .arg(bar->width())
                  .arg(bar->height()));
        // 把手得是**主题那一支**（`scrollHandleColor()`：认得出的预设是自家 `thumb`），
        // 拿混色算一遍对一下；并且和底下那一层明度差得开（看得见才叫把手）。
        const QColor handleInk = at((firstInk + lastInk) / 2, probeY);
        const QColor want = Theme::scrollHandleColor();
        const auto blend = [](const QColor &over, const QColor &under) {
            const int a = over.alpha();
            return QColor((over.red() * a + under.red() * (255 - a)) / 255,
                          (over.green() * a + under.green() * (255 - a)) / 255,
                          (over.blue() * a + under.blue() * (255 - a)) / 255);
        };
        const QColor expected = blend(want, behind);
        const auto near = [](int a, int b) { return qAbs(a - b) <= 2; };
        check(qAbs(handleInk.value() - behind.value()) >= 20 &&
                  near(handleInk.red(), expected.red()) &&
                  near(handleInk.green(), expected.green()) && near(handleInk.blue(), expected.blue()),
              "★★ ⑭ 把手是主题那一支、和它底下那一层分得开（混色算得出来）",
              QStringLiteral("把手 %1（明度 %2）/ 期望 %3 / 底 %4（明度 %5）")
                  .arg(handleInk.name())
                  .arg(handleInk.value())
                  .arg(expected.name())
                  .arg(behind.name())
                  .arg(behind.value()));

        // —— 冒出来那一下：淡进来，而且**一个像素都不许漏到条外面去** ——
        Anim::setEnabled(true);
        Anim::installScrollBarAnimation(app);

        QWidget host2;
        host2.resize(240, 160);
        host2.setAutoFillBackground(true);
        host2.setPalette(hostPalette);
        auto *scroll2 = new QScrollArea(&host2);
        scroll2->setObjectName(QStringLiteral("wtfml_scrollbar_probe2"));
        scroll2->setGeometry(0, 0, 240, 160);
        scroll2->setFrameShape(QFrame::NoFrame);
        scroll2->setWidgetResizable(true);
        scroll2->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *content2 = new QWidget(scroll2);
        content2->setMinimumHeight(600);
        scroll2->setWidget(content2);
        host2.show();
        QApplication::processEvents();

        QScrollBar *bar2 = scroll2->verticalScrollBar();
        // 这两个基准点必须在**露头 / 布完局之后**取：`show()` 本身会挪动顶层窗口（窗口管理器
        // 给的落点），那不是动效干的。这条量的是"淡入这一段里有没有东西动过"。
        const QRect host2Before = host2.geometry();
        check(bar2->graphicsEffect() != nullptr &&
                  bar2->graphicsEffect()->objectName() == QLatin1String("wtfmlAnimEffect"),
              "★★ ⑭ 滚动条一露面就挂上了那一层淡入（不再是硬切出来的）",
              QStringLiteral("效果 = %1")
                  .arg(bar2->graphicsEffect() != nullptr ? bar2->graphicsEffect()->objectName()
                                                         : QStringLiteral("没有")));
        const QRect bar2Before = bar2->geometry();
        check(bar2->width() == Theme::scrollBarThickness(),
              "⑭ 探针二身上那条也一样宽");
        QEventLoop fadeDone;
        QTimer::singleShot(Anim::scrollBarMs() + 90, &fadeDone, &QEventLoop::quit);
        fadeDone.exec();
        QApplication::processEvents();
        check(bar2->graphicsEffect() == nullptr,
              "⑭ 淡完把那一层效果摘掉了（不留常驻的离屏渲染开销）");
        check(bar2->geometry() == bar2Before && host2.geometry() == host2Before,
              "★★ ⑭ 淡入那一段里滚动条和它那一块页面的几何一个像素都没动"
              "（还是那一条：不许有元素超出）",
              QStringLiteral("滚动条 %1,%2 %3x%4 → %5,%6 %7x%8")
                  .arg(bar2Before.x())
                  .arg(bar2Before.y())
                  .arg(bar2Before.width())
                  .arg(bar2Before.height())
                  .arg(bar2->geometry().x())
                  .arg(bar2->geometry().y())
                  .arg(bar2->geometry().width())
                  .arg(bar2->geometry().height()));

        // —— 用户点名的那一幕：**切过去之后滚动条冒出来**（内容变长 ⇒ 它自己出现） ——
        // 上面那一发是"整个窗口露头"，这一发才是"换了一页更长的内容，条子冒出来"。
        QWidget host3;
        host3.resize(240, 160);
        host3.setAutoFillBackground(true);
        host3.setPalette(hostPalette);
        auto *scroll3 = new QScrollArea(&host3);
        scroll3->setObjectName(QStringLiteral("wtfml_scrollbar_probe3"));
        scroll3->setGeometry(0, 0, 240, 160);
        scroll3->setFrameShape(QFrame::NoFrame);
        scroll3->setWidgetResizable(true);
        scroll3->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *content3 = new QWidget(scroll3);
        content3->setObjectName(QStringLiteral("wtfml_scroll_content_probe3"));
        content3->setMinimumHeight(80);   // 一开始装得下 ⇒ 这一条条子根本不在
        scroll3->setWidget(content3);
        host3.show();
        QApplication::processEvents();
        QScrollBar *bar3 = scroll3->verticalScrollBar();
        check(!bar3->isVisible(),
              "⑭ 内容装得下的时候这条滚动条不在（下面那一条才有意义）");
        content3->setMinimumHeight(600);   // ≈ 切过去换了一页更长的
        QApplication::processEvents();
        QApplication::processEvents();
        check(bar3->isVisible() && bar3->graphicsEffect() != nullptr &&
                  bar3->graphicsEffect()->objectName() == QLatin1String("wtfmlAnimEffect"),
              "★★ ⑭ 内容一变长它就冒出来 —— 冒出来那一下是**淡进来**的，不是硬切"
              "（用户点名的那一幕）",
              QStringLiteral("可见 %1 / 效果 %2")
                  .arg(bar3->isVisible() ? QStringLiteral("是") : QStringLiteral("不是"),
                       bar3->graphicsEffect() != nullptr ? bar3->graphicsEffect()->objectName()
                                                         : QStringLiteral("没有")));

        // —— 用户 点名的那一幕：**上下拖窗口**的时候条子来回出现／消失，
        // 里面换行的说明文字跟着重排。`Theme::GutterScrollArea` 右边常留一格死区
        // （条子常驻、装得下时把手透明）⇒ 视口宽度不随条子变。
        QWidget host4;
        host4.resize(240, 160);
        host4.setAutoFillBackground(true);
        host4.setPalette(hostPalette);
        auto *gutterScroll = new Theme::GutterScrollArea(&host4);
        gutterScroll->setObjectName(QStringLiteral("wtfml_scrollbar_probe4"));
        gutterScroll->setGeometry(0, 0, 240, 160);
        gutterScroll->setFrameShape(QFrame::NoFrame);
        gutterScroll->setWidgetResizable(true);
        gutterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *gutterContent = new QWidget(gutterScroll);
        gutterContent->setMinimumHeight(80);   // 一开始装得下 ⇒ 条子是"空的"
        gutterScroll->setWidget(gutterContent);
        host4.show();
        QApplication::processEvents();
        if (Anim::enabled()) {
            QEventLoop fadeDone;
            QTimer::singleShot(Anim::scrollBarMs() + 120, &fadeDone, &QEventLoop::quit);
            fadeDone.exec();
            QApplication::processEvents();
        }
        QScrollBar *gutterBar = gutterScroll->verticalScrollBar();
        const QPoint gutterBarOrigin = gutterBar->mapTo(gutterScroll, QPoint(0, 0));
        // 把手的格子**问样式要**（`subControlRect`）。装得下时它铺满整条，变长之后在最上面。
        const auto gutterSliderCenter = [&](const QImage &image) {
            const QRect slider = subRect(gutterBar, QStyle::SC_ScrollBarSlider);
            return image.pixelColor(gutterBarOrigin + slider.center());
        };
        const int gutterViewportWidth = gutterScroll->viewport()->width();
        const bool gutterIdleAtFirst = gutterBar->property("wtfmlIdle").toBool();
        const QImage gutterIdleImage = captureWidget(gutterScroll);
        const QColor gutterIdlePixel = gutterSliderCenter(gutterIdleImage);
        const QColor gutterBackgroundPixel = gutterIdleImage.pixelColor(
            gutterBarOrigin + QPoint(-4, gutterBar->height() / 2));
        gutterContent->setMinimumHeight(600);   // 内容变长 ⇒ 条子本来会冒出来（文字本来会重排）
        QApplication::processEvents();
        QApplication::processEvents();
        check(gutterIdleAtFirst && !gutterBar->property("wtfmlIdle").toBool(),
              "⑭ 死区那一格：条子常驻，装得下时它是「空的」（把手透明），内容变长后才真的画出来",
              QStringLiteral("前 idle=%1 / 后 idle=%2")
                  .arg(gutterIdleAtFirst ? QStringLiteral("1") : QStringLiteral("0"),
                       gutterBar->property("wtfmlIdle").toBool() ? QStringLiteral("1")
                                                                 : QStringLiteral("0")));
        check(gutterScroll->viewport()->width() == gutterViewportWidth,
              "★★ ⑭ 条子冒出来之后**视口宽度一个像素都不变** —— 右边那一格是常留的死区，"
              "里面换行的文字不会跟着重排（用户原话：「拖窗口上下时文字因为滚动条出现消失重排列」）",
              QStringLiteral("前 %1 / 后 %2")
                  .arg(gutterViewportWidth)
                  .arg(gutterScroll->viewport()->width()));
        check(gutterBar->width() == Theme::scrollBarThickness() &&
                  gutterScroll->viewport()->width() + Theme::scrollBarThickness()
                      == gutterScroll->width(),
              "★ ⑭ 死区那一格就是 `scrollBarThickness()` 那么宽，条子落进去、不多不少",
              QStringLiteral("条宽 %1 / 视口 %2 / 总宽 %3 / 常数 %4")
                  .arg(gutterBar->width())
                  .arg(gutterScroll->viewport()->width())
                  .arg(gutterScroll->width())
                  .arg(Theme::scrollBarThickness()));

        // 那条 `[wtfmlIdle="true"]` 的 QSS 到底有没有生效 —— 只看字符串会假绿
        // （这一仓有过 `::drop-down` 里多写一句 `border: none` 把整个箭头吃掉的先例）。
        // 量法：装得下时**把手那一格和它旁边的页面底一个色**（透明），内容变长之后
        // 同一格变成把手那一档色。
        {
            const QImage busyImage = captureWidget(gutterScroll);
            const QColor busyPixel = gutterSliderCenter(busyImage);
            check(gutterIdlePixel == gutterBackgroundPixel && busyPixel != gutterIdlePixel,
                  "★★ ⑭ 装得下时那一条**真的什么都不画**（把手透明、就是一条留白），"
                  "内容变长之后同一格才有把手的颜色",
                  QStringLiteral("空着 %1 / 有内容 %2 / 页面底 %3")
                      .arg(gutterIdlePixel.name(QColor::HexArgb),
                           busyPixel.name(QColor::HexArgb),
                           gutterBackgroundPixel.name(QColor::HexArgb)));
        }

        // —— 换主题时**藏着的**滚动条不白跑，露头那一刻补上 ——
        //
        // 这一条钉的是 2026-10-31 改掉的那笔账：换一次主题原来把每一根滚动条都重挂一遍
        // （控件级 `setStyleSheet()` ⇒ 重新解析 + polish），探针拆出来是 15–26 ms，而其中
        // 大多数根本不在屏幕上。现在只重挂露着的，藏着的等 Show 事件补
        // （`ScrollBarStyler` + `styleScrollBar()` 身上的代数标记）。
        {
            const Theme::Spec specBefore = Theme::current();
            QWidget hiddenHost;
            hiddenHost.resize(240, 160);
            hiddenHost.setAutoFillBackground(true);
            auto *hiddenScroll = new QScrollArea(&hiddenHost);
            hiddenScroll->setGeometry(0, 0, 240, 160);
            hiddenScroll->setFrameShape(QFrame::NoFrame);
            hiddenScroll->setWidgetResizable(true);
            hiddenScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            auto *hiddenContent = new QWidget(hiddenScroll);
            hiddenContent->setMinimumHeight(600);
            hiddenScroll->setWidget(hiddenContent);
            QScrollBar *hiddenBar = hiddenScroll->verticalScrollBar();
            check(hiddenBar->styleSheet().isEmpty(),
                  "⑭ 还没露头的滚动条身上没有控件级样式（下面两条的前提）");

            Theme::Spec dark = specBefore;
            dark.presetId = QStringLiteral("dark");
            dark.accent = Theme::tokens(dark.presetId).primary;
            Theme::setSpec(dark);
            Theme::apply(app);
            check(hiddenBar->styleSheet().isEmpty(),
                  "★ ⑭ 换主题不给藏着的滚动条白跑一趟（它身上还是没样式）",
                  hiddenBar->styleSheet());

            hiddenHost.show();
            QApplication::processEvents();
            QApplication::processEvents();
            check(!hiddenBar->styleSheet().isEmpty(),
                  "★★ ⑭ 藏着的滚动条一露头就把新主题的样式补上（不是停在老颜色上）",
                  hiddenBar->styleSheet());

            Theme::setSpec(specBefore);
            Theme::apply(app);
        }

        Anim::setEnabled(wasEnabled);
    }

    // ---------- ⑮ 方向键与 PageUp/PageDown 滚动页面（不在输入/列表等控件上才滚） ----------
    {
        QWidget host;
        host.resize(500, 360);
        auto *layout = new QVBoxLayout(&host);
        layout->setContentsMargins(0, 0, 0, 0);

        auto *headerBtn = new QPushButton(QStringLiteral("页眉按钮"), &host);
        layout->addWidget(headerBtn);

        auto *tabs = new QTabWidget(&host);
        tabs->setObjectName(QStringLiteral("mainTabWidget"));
        auto *tabScroll = new Theme::GutterScrollArea(tabs);
        tabScroll->setWidgetResizable(true);
        auto *tabBody = new QWidget(tabScroll);
        auto *tabBodyLayout = new QVBoxLayout(tabBody);
        auto *edit = new QLineEdit(tabBody);
        auto *list = new QListWidget(tabBody);
        for (int i = 0; i < 6; ++i) {
            list->addItem(QStringLiteral("条目 %1").arg(i));
        }
        list->setCurrentRow(0);
        list->setFixedHeight(120);
        auto *spacer = new QWidget(tabBody);
        spacer->setFixedHeight(1200);
        tabBodyLayout->addWidget(edit);
        tabBodyLayout->addWidget(list);
        tabBodyLayout->addWidget(spacer);
        tabScroll->setWidget(tabBody);
        tabs->addTab(tabScroll, QStringLiteral("Tab"));
        layout->addWidget(tabs, 1);

        OverlayHost overlay(tabs, &host);
        host.show();
        QApplication::processEvents();

        QScrollBar *tabBar = tabScroll->verticalScrollBar();
        check(tabBar != nullptr && tabBar->maximum() > 0, "⑮ 前提：页面滚动条有范围");

        // 1) 无焦点 / 焦点在滚动区外部按钮上：方向键和 PageUp/PageDown 滚页
        headerBtn->setFocus();
        QKeyEvent downFromBtn(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(headerBtn, &downFromBtn);
        const int afterDown = tabBar->value();
        check(afterDown > 0 && downFromBtn.isAccepted(),
              "⑮ 焦点在滚动区外按钮上时，按 Down 滚动页面一步",
              QString::number(afterDown));

        QKeyEvent pageDown(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
        QApplication::sendEvent(&host, &pageDown);
        const int afterPageDown = tabBar->value();
        check(afterPageDown > afterDown,
              "⑮ 按 PageDown 向下滚一页",
              QStringLiteral("%1 -> %2").arg(afterDown).arg(afterPageDown));

        QKeyEvent pageUp(QEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier);
        QApplication::sendEvent(&host, &pageUp);
        const int afterPageUp = tabBar->value();
        check(afterPageUp < afterPageDown,
              "⑮ 按 PageUp 向上滚一页",
              QStringLiteral("%1 -> %2").arg(afterPageDown).arg(afterPageUp));

        tabBar->setValue(0);

        // 2) 焦点在输入框（QLineEdit）里时：方向键不滚动页面
        edit->setFocus();
        QKeyEvent downInEdit(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(edit, &downInEdit);
        check(tabBar->value() == 0,
              "⑮ 焦点在输入框里时按 Down 不滚动页面",
              QString::number(tabBar->value()));

        // 3) 焦点在列表（QListWidget）里时：方向键归列表，不滚动页面
        list->setFocus();
        QKeyEvent downInList(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(list, &downInList);
        check(tabBar->value() == 0 && list->currentRow() == 1,
              "⑮ 焦点在列表里时按 Down 只移动列表选中项，不滚动外层页面",
              QStringLiteral("scroll=%1 row=%2").arg(tabBar->value()).arg(list->currentRow()));

        // 4) 浮层打开时，按方向键只滚最上层浮层，不滚底下被盖住的页面
        QWidget *overlayContent = overlay.openPage(QStringLiteral("测试浮层"));
        auto *tallOverlayWidget = new QWidget;
        tallOverlayWidget->setFixedHeight(1200);
        overlayContent->layout()->addWidget(tallOverlayWidget);
        for (int i = 0; i < 3; ++i) {
            QApplication::processEvents();
        }
        auto *overlayScroll = overlay.findChild<QScrollArea *>(OverlayHost::overlayPageScrollName());
        auto *backBtn = overlay.findChild<QPushButton *>(OverlayHost::overlayBackButtonName());
        QScrollBar *overlayBar = overlayScroll != nullptr ? overlayScroll->verticalScrollBar() : nullptr;
        check(overlayBar != nullptr && overlayBar->maximum() > 0 && backBtn != nullptr,
              "⑮ 浮层前提：浮层滚动条有范围且页眉返回钮在");
        if (backBtn != nullptr && overlayBar != nullptr) {
            backBtn->setFocus();
            QKeyEvent downInOverlay(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
            QApplication::sendEvent(backBtn, &downInOverlay);
            check(overlayBar->value() > 0 && tabBar->value() == 0,
                  "⑮ 浮层打开且焦点在返回钮上时，按 Down 只滚浮层不滚底页",
                  QStringLiteral("overlay=%1 tab=%2").arg(overlayBar->value()).arg(tabBar->value()));
        }
        overlay.closeOverlay();
        QApplication::processEvents();
        delete tallOverlayWidget;
    }
    return 0;
}


