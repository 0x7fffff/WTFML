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
        const QColor hostFill(0xfc, 0xfc, 0xfc);   // 页面底（`Part::PageBackground` 出厂那一档）
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
        const QImage shot = captureWidget(scroll);

        // 量：一条线的左边（页面那一层）和滚动条那一条里的底色、把手的左右边界。
        const auto at = [&shot](int x, int y) {
            return shot.pixelColor(qBound(0, x, shot.width() - 1), qBound(0, y, shot.height() - 1));
        };
        // 把手在哪一格**问样式要**（`subControlRect`）—— 那一刻的值是 0，把手在最上面那一段，
        // 不是正中间（第一版按"正中间"取样，量到的是槽，于是"一个墨点都没有"）。
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
        // 把手得是支**灰**（r=g=b），比底下那一层深，而且就是主题那一支（拿混色算一遍对一下）。
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
        check(handleInk.red() == handleInk.green() && handleInk.green() == handleInk.blue() &&
                  handleInk.value() < behind.value() && near(handleInk.red(), expected.red()) &&
                  near(handleInk.green(), expected.green()) && near(handleInk.blue(), expected.blue()),
              "★★ ⑭ 把手是一支中性灰、比它底下那一层深，而且就是主题那一支（混色算得出来）",
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

        Anim::setEnabled(wasEnabled);
    }
    return 0;
}


