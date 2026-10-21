#include "Support.hpp"
int UiLogTogglesChecks() {
    {
        // 「开着的那颗染成这一档的颜色、关掉之后变回原来那个灰」——这是用户提的那件事。
        //
        // 关态那个灰**是量出来的**：改成自己画之前，原生 Fusion 给这一排五颗
        // 关着时画的就是 `#ababab`。钉成常量写在这里，是因为用户的原话就是「按下关闭以后
        // 变成现在的灰色」—— 哪天这个数字变了，先问一句"是不是又换了画法"，别顺手改断言。
        const QColor kOffGray(0xab, 0xab, 0xab);

        auto *log = new GameLogWindow(QStringLiteral("开关染色这一节"));
        QPointer<GameLogWindow> guard(log);
        log->show();
        QApplication::processEvents();
        using L = GameLogWindow::LogLevel;

        // 开态那一张必须在**点之前**量：五颗开局全是勾着的（默认就是"全看"）。
        QToolButton *buttons[GameLogWindow::levelCount()] = {};
        QImage onShot[GameLogWindow::levelCount()];
        QColor onColor[GameLogWindow::levelCount()];
        bool allThere = true;
        int buttonHeight = 0;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            buttons[i] = log->getFilterButton(GameLogWindow::levelAt(i));
            if (buttons[i] == nullptr) {
                allThere = false;
                continue;
            }
            onShot[i] = captureWidget(buttons[i]);
            // 量的是**按钮整块**里出现次数最多的那个色：里面还有边（1 px）和字，底色铺满中间。
            onColor[i] = dominantColor(onShot[i], QRect(QPoint(0, 0), onShot[i].size()));
            buttonHeight = qMax(buttonHeight, onShot[i].height());
        }
        check(allThere && buttonHeight > 0, "Z7 前提：五颗开关都建出来了、都排过布局",
              QStringLiteral("高 %1 px").arg(buttonHeight));

        // 一颗一颗来：点掉量关态、再点回来量"回没回到原来的色"，顺手记两态下的尺寸。
        QImage offShot[GameLogWindow::levelCount()];
        QColor offColor[GameLogWindow::levelCount()];
        QColor backColor[GameLogWindow::levelCount()];
        QSize onSize[GameLogWindow::levelCount()];
        bool sizeJumped = false;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            if (buttons[i] == nullptr) {
                continue;
            }
            onSize[i] = onShot[i].size();
            buttons[i]->click();   // 真点（不是 setChecked）：用户能碰到的就是这一下
            QApplication::processEvents();
            offShot[i] = captureWidget(buttons[i]);
            offColor[i] = dominantColor(offShot[i], QRect(QPoint(0, 0), offShot[i].size()));
            if (offShot[i].size() != onSize[i]) {
                sizeJumped = true;
            }
            buttons[i]->click();
            QApplication::processEvents();
            const QImage back = captureWidget(buttons[i]);
            backColor[i] = dominantColor(back, QRect(QPoint(0, 0), back.size()));
        }

        QStringList onText, offText;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            onText << QStringLiteral("%1=%2").arg(GameLogWindow::levelName(GameLogWindow::levelAt(i)),
                                                  onColor[i].name());
            offText << QStringLiteral("%1=%2").arg(GameLogWindow::levelName(GameLogWindow::levelAt(i)),
                                                  offColor[i].name());
        }

        // ① 关掉之后**五颗都是同一个灰**，而且就是改之前那个灰。
        int worstOff = -1;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            worstOff = qMax(worstOff, colorDistance(offColor[i], kOffGray));
        }
        check(worstOff >= 0 && worstOff <= 6,
              "Z7 关掉之后变回**原来那个灰**（五颗一律 #ababab，改之前量的就是它）",
              QStringLiteral("%1 | 最远的一颗差 %2")
                  .arg(offText.join(QStringLiteral(" ")))
                  .arg(worstOff));

        // ② 开着的那颗：有底色的四档**就是正文那一行的颜色**（这件事的全部意义在这儿）。
        int worstOn = -1;
        QString onDetail;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            const L level = GameLogWindow::levelAt(i);
            const QColor want = GameLogWindow::filterButtonBackground(level, /*on=*/true);
            const int distance = colorDistance(onColor[i], want);
            if (distance > worstOn) {
                worstOn = distance;
                onDetail = QStringLiteral("%1 量到 %2 该是 %3")
                               .arg(GameLogWindow::levelName(level), onColor[i].name(),
                                    want.name());
            }
        }
        // NOTE: 和 Z6 那条一样，**这一条单独看也是空的** —— 它比的是"量出来的"和
        // `filterButtonBackground()`，把那个函数改错（比如让按钮用比正文深一点的色）
        // 它会跟着一起变、照样绿（实测过：那样改只有下面那条"唯一出处"会红）。
        // 它挡的是**渲染那条路没照着我们给的色画**（底色没铺上、被别的东西盖掉）；
        // "按钮的颜色就是正文那一行的颜色"由下面那条跨出处的比较来钉。
        check(worstOn >= 0 && worstOn <= 6,
              "Z7 开着那颗填的就是这一档的颜色（和正文那一行同一个色）",
              QStringLiteral("%1 | 最远 %2").arg(onText.join(QStringLiteral(" ")), onDetail));
        check(GameLogWindow::filterButtonBackground(L::Warn, true) ==
                      GameLogWindow::levelBackground(L::Warn) &&
                  !GameLogWindow::levelBackground(L::Info).isValid() &&
                  GameLogWindow::filterButtonBackground(L::Info, true).isValid(),
              "Z7 「开着填什么色」也是唯一出处说了算（四档借正文的色，Info 借正文区的底色）",
              QStringLiteral("WARN=%1 INFO=%2")
                  .arg(GameLogWindow::filterButtonBackground(L::Warn, true).name(),
                       GameLogWindow::filterButtonBackground(L::Info, true).name()));

        // ②b Info 那颗**开着比关着亮** —— 这是用户那句话本身（「info 的按钮再亮一点」）。
        // 别的档不适用这一条：Fatal 那个深红比关态那个灰还暗，而那是对的（它要的是"醒目"
        // 不是"亮"）。所以这条只管 Info 那一档，判据用三通道之和（比"亮"这个词具体）。
        {
            const auto luminance = [](const QColor &c) { return c.red() + c.green() + c.blue(); };
            const int onLum = luminance(GameLogWindow::filterButtonBackground(L::Info, true));
            const int offLum = luminance(GameLogWindow::filterButtonBackground(L::Info, false));
            check(onLum >= offLum + 60,
                  "Z7 Info 那颗开着的时候比关着**亮**（用户要的「再亮一点」）",
                  QStringLiteral("开=%1(和 %2) 关=%3(和 %4)")
                      .arg(GameLogWindow::filterButtonBackground(L::Info, true).name())
                      .arg(onLum)
                      .arg(GameLogWindow::filterButtonBackground(L::Info, false).name())
                      .arg(offLum));
        }

        // ③ 每一颗"开"和"关"都看得出来不一样 —— **Info 那颗也要**：它没有颜色可借，
        // 要是做成开=关，那颗按钮就成了一颗按不动的按钮（用户点了没反应的那种）。
        int closest = 999;
        QString closestName;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            const int distance = colorDistance(onColor[i], offColor[i]);
            if (distance < closest) {
                closest = distance;
                closestName = GameLogWindow::levelName(GameLogWindow::levelAt(i));
            }
        }
        check(closest >= 20, "Z7 每一颗开/关都看得出来不一样（Info 那颗也算）",
              QStringLiteral("最接近的一颗：%1，差 %2").arg(closestName).arg(closest));
        // 五颗的"开"色两两分得开：两档用同一个色的话，一眼就分不出来谁是谁（比的是量出来的值）。
        int pairClosest = 999;
        QString pairName;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            for (int j = i + 1; j < GameLogWindow::levelCount(); ++j) {
                const int distance = colorDistance(onColor[i], onColor[j]);
                if (distance < pairClosest) {
                    pairClosest = distance;
                    pairName = QStringLiteral("%1/%2")
                                   .arg(GameLogWindow::levelName(GameLogWindow::levelAt(i)),
                                        GameLogWindow::levelName(GameLogWindow::levelAt(j)));
                }
            }
        }
        check(pairClosest >= 20, "Z7 五颗开着时的颜色两两分得开",
              QStringLiteral("最近的一对：%1，差 %2").arg(pairName).arg(pairClosest));

        // ④ 再点一下要回到原来那个色（只管"关"那一下的写法会在这里露馅）。
        int worstBack = -1;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            worstBack = qMax(worstBack, colorDistance(backColor[i], onColor[i]));
        }
        check(worstBack >= 0 && worstBack <= 6, "Z7 再点一下回到原来那个色（一去一回不掉色）",
              QStringLiteral("最远的一颗差 %1").arg(worstBack));

        // ⑤ 点一下按钮不能自己变大变小。**这一条盯的是"按钮上的字"**：数字数的是模型
        // （关掉一档数字不变小），所以开关那一行文字不随状态变、宽度也就不变；哪天有人
        // 给关掉的按钮换个写法（"隐藏""+3"之类），宽度一跳整排就跟着挪。
        // NOTE: 量的是**控件自己的尺寸**（不是像素），两态下要一模一样。
        check(!sizeJumped,
              "Z7 点一下按钮自己不变大变小（开关上的字不随开关状态变，整排不会挪）",
              QStringLiteral("%1x%2").arg(onSize[0].width()).arg(onSize[0].height()));

        // ⑥ 字和底得**分得开**：FATAL 那颗是白字（深红底上黑字读不清），其余四颗是黑字
        // （它们的底都是浅的）。字形是抗锯齿画的，没有"确定的那一个像素"，所以数的是
        // **一块里有几个接近白 / 接近黑的点**。
        //
        // NOTE: 原来是"数白点，除了 FATAL 一颗都不许有"。INFO 那颗的底改成纯白之后
        // 那条就不成立了 —— 它的底**就是**白，白点分不出是字还是底（量出来 1677 个，
        // 全是底）。改成反过来数黑点：黑字那四颗一定有，FATAL 那颗一个都不该有。
        const auto pixelsNear = [](const QImage &image, bool white) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    const bool near = white ? c.red() >= 0xf0 && c.green() >= 0xf0 && c.blue() >= 0xf0
                                            : c.red() <= 0x40 && c.green() <= 0x40 && c.blue() <= 0x40;
                    if (near) {
                        ++count;
                    }
                }
            }
            return count;
        };
        QStringList inkText;
        int fatalWhite = 0;
        int fatalBlack = 0;
        bool othersBlack = true;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            const L level = GameLogWindow::levelAt(i);
            const int white = pixelsNear(onShot[i], /*white=*/true);
            const int black = pixelsNear(onShot[i], /*white=*/false);
            inkText << QStringLiteral("%1=白%2/黑%3")
                           .arg(GameLogWindow::levelName(level))
                           .arg(white)
                           .arg(black);
            if (level == L::Fatal) {
                fatalWhite = white;
                fatalBlack = black;
            } else if (black <= 0) {
                othersBlack = false;
            }
        }
        check(fatalWhite > 0 && fatalBlack == 0,
              "Z7 FATAL 那颗的字是**白**的（深红底上黑字读不清；反过来一个黑点都不该有）",
              inkText.join(QStringLiteral(" ")));
        check(othersBlack,
              "Z7 其余四颗的字是**黑**的（含 INFO：它的底现在是白的，更得是黑字）",
              inkText.join(QStringLiteral(" ")));

        log->close();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    return 0;
}


