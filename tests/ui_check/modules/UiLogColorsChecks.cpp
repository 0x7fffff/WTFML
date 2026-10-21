#include "Support.hpp"
int UiLogColorsChecks() {
    {
        // 量法是"每一行那一块里**出现次数最多的颜色**"（见 dominantColor 的说明）：
        // 行里有字，字是另一种色，但底色铺满那一行 —— 多数那一票是它。
        // 夹具**故意把每一行写得很长**（400 个字符，远超视口宽度）：这样视口里那一行
        // 整条都是"文字那一截"，量出来不会掺进窗口底色的票。
        auto *log = new GameLogWindow(QStringLiteral("染底这一节"));
        QPointer<GameLogWindow> guard(log);
        log->show();
        QApplication::processEvents();
        QPlainTextEdit *view = log->getLogView();
        using L = GameLogWindow::LogLevel;
        if (view != nullptr) {
            const QString body = QString(400, QLatin1Char('m'));
            for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
                const L level = GameLogWindow::levelAt(i);
                log->appendText(QStringLiteral("[12:34:56] [main/%1]: %2\n")
                                    .arg(GameLogWindow::levelName(level), body));
            }
            // 末尾那半行（没有换行 = 还没有级别）：它是"不染底"的参照物。
            log->appendText(body);
            QApplication::processEvents();

            const QImage shot = captureWidget(view->viewport());
            const int viewportWidth = view->viewport()->width();
            const int viewportHeight = view->viewport()->height();
            // 行的那一条横带子：`cursorRect()` 是公开的（`blockBoundingGeometry()` 那一对
            // 是 protected），它给的是"光标该画在哪" —— 顶就是这一行的顶、高就是这一行的高。
            // 横向上量**整个视口宽**：这一行比视口宽得多，所以视口里那一段整条都是它。
            QRect rects[6];
            int lineHeight = 0;
            QStringList rectText;
            for (int i = 0; i < 6; ++i) {
                const QRect cursorRect =
                    view->cursorRect(QTextCursor(view->document()->findBlockByNumber(i)));
                rects[i] = QRect(0, cursorRect.top(), viewportWidth, cursorRect.height());
                lineHeight = qMax(lineHeight, rects[i].height());
                rectText << QStringLiteral("%1:(y%2 高%3)")
                                .arg(i)
                                .arg(rects[i].y())
                                .arg(rects[i].height());
            }
            // 前提三条：① 六格都真的排过布局（高都一样、都不是空的）；② 自上而下依次排在
            // 视口里；③ 这一份文本**比视口宽** —— 这一条是"整条横带子都是染色的文字那一截"
            // 的依据，少了它，量出来的可能掺进窗口底色的票（那就不是在量我们画的那一格了）。
            bool premises = lineHeight > 0;
            for (int i = 0; i < 6; ++i) {
                premises = premises && rects[i].height() == lineHeight &&
                           rects[i].bottom() < viewportHeight && rects[i].top() >= 0;
                if (i > 0) {
                    premises = premises && rects[i].top() > rects[i - 1].top();
                }
            }
            const int textWidth = int(view->document()->documentLayout()->documentSize().width());
            check(premises && textWidth > viewportWidth,
                  "Z6 前提：六格都排过布局、自上而下在视口里，而且这一份文本比视口还宽"
                  "（量的是它们自己那一条）",
                  QStringLiteral("视口 %1x%2 文本宽 %3 | %4")
                      .arg(viewportWidth)
                      .arg(viewportHeight)
                      .arg(textWidth)
                      .arg(rectText.join(QStringLiteral(" "))));

            const QColor base = dominantColor(shot, rects[5]);
            QColor got[5];
            QStringList gotText;
            for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
                got[i] = dominantColor(shot, rects[i]);
                gotText << QStringLiteral("%1=%2")
                               .arg(GameLogWindow::levelName(GameLogWindow::levelAt(i)),
                                    got[i].name());
            }
            int worst = -1;
            for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
                const L level = GameLogWindow::levelAt(i);
                const QColor want = GameLogWindow::levelBackground(level);
                if (!want.isValid()) {
                    continue;  // Info 那一档单独判（它该跟那半行一个色）
                }
                const int distance = colorDistance(got[i], want);
                if (distance > worst) {
                    worst = distance;
                }
            }
            // NOTE: 这一条**单独看是空的** —— 它比的是"量出来的"和 `levelBackground()`，
            // 把那个函数改错（比如让 Warn 用 Debug 的色）它会跟着一起变、照样绿
            // （实测过：那样改只有下面那条"两两不同"会红）。它真正挡的是**渲染那条路
            // 没照着格式画**（底色没设上、被窗口底色盖掉）；"颜色本身选对了"由下面
            // 那条两两不同 + INFO 与那半行同色来钉。两条一起才成一个闭环。
            check(worst >= 0 && worst <= 6,
                  "Z6 四档染色的底色**就是**我们定的那个颜色（量的是行上真画出来的像素）",
                  QStringLiteral("%1 | 最远的一档差 %2").arg(gotText.join(QStringLiteral(" "))).arg(worst));
            // 末尾那半行按**上一行那一档**染（ 改，原来是"不染底"）。
            //
            // 这条规则要两个点才算钉住，少一个都能被写死骗过去：上面这个夹具里它上一行是
            // FATAL，断言"那半行的横带子就是 FATAL 那个深红"（写死"半行永远深红"也能过）；
            // 下面那个夹具里它上一行是 INFO，断言"它就是窗口底色"（写死"半行永远白"也能过）
            // —— 两个一起才等于"跟着上一行走"。改这个行为的原因是用户那句「日志框的最后
            // 一行不会被渲染颜色」：最后一行那个位置上站着的往往正是这半行（进程输出按块到，
            // 末尾那条常常还没等到换行，游戏崩了那一行尤其可能永远等不到）。
            check(colorDistance(base, got[int(L::Fatal)]) <= 6,
                  "Z6 末尾那半行跟着**上一行那一档**染（上一行是 FATAL，它就是那个深红）",
                  QStringLiteral("半行=%1 FATAL=%2").arg(base.name(), got[int(L::Fatal)].name()));
            // 五档两两分得开：拿**量出来的**值比（不是拿常量比常量）—— 上面那四条"等于常量"
            // 加上这一条，才同时钉住"颜色对"和"五档真的不一样"。
            // NOTE: 比的是**五档之间**（那半行不参与：它和 INFO 本来就该是一个色，见上一条）。
            int closest = 999;
            QString closestPair;
            for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
                for (int j = i + 1; j < GameLogWindow::levelCount(); ++j) {
                    const int distance = colorDistance(got[i], got[j]);
                    if (distance < closest) {
                        closest = distance;
                        closestPair =
                            QStringLiteral("%1/%2")
                                .arg(GameLogWindow::levelName(GameLogWindow::levelAt(i)),
                                     GameLogWindow::levelName(GameLogWindow::levelAt(j)));
                    }
                }
            }
            check(closest >= 20, "Z6 五档两两看得出来不一样（最近的一对也差得够远）",
                  QStringLiteral("最近的一对：%1，差 %2").arg(closestPair).arg(closest));

            // FATAL 那一档换白墨（深红底上写深灰字读不清）。这一条量的是**文档里的格式**，
            // 不是字形像素：字形是抗锯齿画出来的，没有"那一个确定的像素"可量；底色有，
            // 所以底色走上面那几条像素、墨色走这一条。
            //
            // NOTE: 期望"别的档的墨**不是白的**"而不是"没有设墨" —— QTextBlock::charFormat()
            // 给的是**合并过默认值**的那份格式（没设过也是黑的），所以"没设过"这个
            // 状态在文档里查不出来。这一条要挡的是"给所有档都换了墨"那种错。
            const QTextCursor fatalCursor(view->document()->findBlockByNumber(int(L::Fatal)));
            const QTextCursor errorCursor(view->document()->findBlockByNumber(int(L::Error)));
            check(fatalCursor.charFormat().foreground().color() == QColor(0xff, 0xff, 0xff) &&
                      errorCursor.charFormat().foreground().color() != QColor(0xff, 0xff, 0xff),
                  "Z6 只有 FATAL 那一档换了白墨，别的档还是窗口默认的墨",
                  QStringLiteral("FATAL=%1 ERROR=%2")
                      .arg(fatalCursor.charFormat().foreground().color().name(),
                           errorCursor.charFormat().foreground().color().name()));
            check(!GameLogWindow::levelForeground(L::Info).isValid() &&
                      GameLogWindow::levelForeground(L::Fatal).isValid(),
                  "Z6 「哪一档换墨」也是唯一出处说了算（不换的那几档返回无效色）");
        }
        log->close();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    return 0;
}


