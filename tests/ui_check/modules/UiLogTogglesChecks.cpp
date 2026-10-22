#include "Support.hpp"
int UiLogTogglesChecks() {
    {
        // 「开着的那颗染成这一档的颜色、关掉之后变回原来那个灰」—— 这一节钉的就是这个。
        auto *log = new GameLogWindow(QStringLiteral("开关染色这一节"));
        QPointer<GameLogWindow> guard(log);
        log->show();
        QApplication::processEvents();
        using L = GameLogWindow::LogLevel;

        // 关态那个灰**是算出来的**：`GameLogWindow::switchOffColor()`（从窗口底色往窗口
        // 墨色混 `kSwitchOffPercent`；浅色下约 `#aa`，深色下自动变深灰）。量的时候拿
        // **这一窗自己的调色板** —— 日志窗的颜色不跟应用调色板走（见 .cpp 里那段）。
        const QColor kOffGray = GameLogWindow::switchOffColor(log->palette());

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

        // ① 关掉之后**五颗都是同一个灰**，而且就是那个固定的灰。
        int worstOff = -1;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            worstOff = qMax(worstOff, colorDistance(offColor[i], kOffGray));
        }
        check(worstOff >= 0 && worstOff <= 6,
              "Z7 关掉之后变回**那个中性灰**（五颗一律 `switchOffColor()` 算出来的那支）",
              QStringLiteral("%1 | 最远的一颗差 %2")
                  .arg(offText.join(QStringLiteral(" ")))
                  .arg(worstOff));

        // ② 开着的那颗：有底色的四档**就是正文那一行的颜色**（这件事的全部意义在这儿）。
        int worstOn = -1;
        QString onDetail;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            const L level = GameLogWindow::levelAt(i);
            const QColor want =
                GameLogWindow::filterButtonBackground(level, /*on=*/true, log->palette());
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
        check(GameLogWindow::filterButtonBackground(L::Warn, true, log->palette()) ==
                      GameLogWindow::levelBackground(L::Warn, log->palette()) &&
                  !GameLogWindow::levelBackground(L::Info, log->palette()).isValid() &&
                  GameLogWindow::filterButtonBackground(L::Info, true, log->palette()).isValid(),
              "Z7 「开着填什么色」也是唯一出处说了算（三档借正文的色，Info 借正文区的底色）",
              QStringLiteral("WARN=%1 INFO=%2")
                  .arg(GameLogWindow::filterButtonBackground(L::Warn, true, log->palette()).name(),
                       GameLogWindow::filterButtonBackground(L::Info, true, log->palette()).name()));

        // ②b Info 那颗**开着比关着亮** —— 这是用户那句话本身（「info 的按钮再亮一点」）。
        // 别的档不适用这一条：Fatal 那个深红比关态那个灰还暗，而那是对的（它要的是"醒目"
        // 不是"亮"）。所以这条只管 Info 那一档，判据用三通道之和（比"亮"这个词具体）。
        {
            const auto luminance = [](const QColor &c) { return c.red() + c.green() + c.blue(); };
            const int onLum =
                luminance(GameLogWindow::filterButtonBackground(L::Info, true, log->palette()));
            const int offLum =
                luminance(GameLogWindow::filterButtonBackground(L::Info, false, log->palette()));
            check(onLum >= offLum + 60,
                  "Z7 Info 那颗开着的时候比关着**亮**（用户要的「再亮一点」）",
                  QStringLiteral("开=%1(和 %2) 关=%3(和 %4)")
                      .arg(GameLogWindow::filterButtonBackground(L::Info, true, log->palette())
                               .name())
                      .arg(onLum)
                      .arg(GameLogWindow::filterButtonBackground(L::Info, false, log->palette())
                               .name())
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

        // ⑥ 字和底得**分得开**：浅色那一套里五颗的字都是那支深灰墨（`#202020`）——
        // FATAL 那颗的底是浅红（`#edb5bd`），深灰压上去读得清；白字是**深色那一套**
        // （深红 `#d32f2f` 底）才换的。字形是抗锯齿画的，没有"确定的那一个像素"，所以数的是
        // **一块里有几个接近白 / 接近黑的点**。
        //
        // NOTE: 这里数黑点：INFO 那颗的底**接近**白，但白点分不出是字还是底（那支墨是
        // `#202020`，黑点一定来自字）；五颗都该有黑点、也都不该出现白点。
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
        bool allBlack = true;
        bool noWhiteInk = true;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            const L level = GameLogWindow::levelAt(i);
            const int white = pixelsNear(onShot[i], /*white=*/true);
            const int black = pixelsNear(onShot[i], /*white=*/false);
            inkText << QStringLiteral("%1=白%2/黑%3")
                           .arg(GameLogWindow::levelName(level))
                           .arg(white)
                           .arg(black);
            allBlack = allBlack && black > 0;
            // INFO 那一颗的底**就是**近白（正文区的底色），白点分不出是字还是底 ——
            // 别的几颗的底都是浅彩色，出现一片白就是"字换成白墨了"。
            const QColor onBg = GameLogWindow::filterButtonBackground(level, true, log->palette());
            const bool nearWhiteBase = onBg.red() >= 0xf0 && onBg.green() >= 0xf0 &&
                                       onBg.blue() >= 0xf0;
            if (!nearWhiteBase && white > 0) {
                noWhiteInk = false;
            }
        }
        check(allBlack && noWhiteInk,
              "Z7 五颗的字都铺着那支深灰墨（浅色那一套；浅彩色底上一个白点都不该有）",
              inkText.join(QStringLiteral(" ")));

        log->close();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    return 0;
}


