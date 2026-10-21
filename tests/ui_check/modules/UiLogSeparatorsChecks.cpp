#include "Support.hpp"
int UiLogSeparatorsChecks() {
    {
        // 要求：「日志行与行之间加一条细线分隔一下」。
        //
        // 这一节量两件事：**线真的画在「两行之间」那一格**，以及它**只有一条**
        // （`separatorHeight()` —— 两条以上就不再是「细线」了）。
        //
        // 夹具的头两行是**相邻的两条 INFO**：它们的底色一模一样（都不染），那一格底下有没有
        // 线就是唯一分得开它们的东西 —— 那正是这条线存在的理由（相邻两档的底色本来就分得开）。
        // 这个前提本身也是断言，少了它这一节就变成了"在两种不同的颜色之间找一条线"。
        auto *log = new GameLogWindow(QStringLiteral("细线这一节"));
        QPointer<GameLogWindow> guard(log);
        log->show();
        QApplication::processEvents();
        QPlainTextEdit *view = log->getLogView();
        using L = GameLogWindow::LogLevel;
        if (view != nullptr) {
            log->appendText(QStringLiteral("[12:34:56] [main/INFO]: 第一条\n"));
            log->appendText(QStringLiteral("[12:34:56] [main/INFO]: 第二条\n"));
            log->appendText(QStringLiteral("[12:34:56] [main/ERROR]: 第三条\n"));
            log->appendText(QStringLiteral("[12:34:56] [main/ERROR]: 还没换行的那半行"));
            QApplication::processEvents();

            const QImage shot = captureWidget(view->viewport());
            const int viewportWidth = view->viewport()->width();
            const int viewportHeight = view->viewport()->height();
            const QColor separator = GameLogWindow::separatorColor();

            // 这一行（y）里"是那条线"的像素有几个 —— 量的是一段 60 px 宽、**右端没有字**
            // 的地方（夹具那几行都很短）。
            const auto linePixelsAt = [&shot, &separator, viewportWidth](int y) {
                if (y < 0 || y >= shot.height()) {
                    return 0;
                }
                int hits = 0;
                for (int x = viewportWidth - 60; x < viewportWidth; ++x) {
                    if (colorDistance(QColor(shot.pixel(x, y)), separator) <= 6) {
                        ++hits;
                    }
                }
                return hits;
            };
            // 某一格下沿那一带（上下各 2 px，对付一像素的取整偏差）里，**整段都是线**的行有几个。
            // 返回 `separatorHeight()` 才叫"这儿有一条线、而且只有一条"。
            const auto fullLinesUnder = [&linePixelsAt](const QRect &row) {
                int found = 0;
                for (int y = row.bottom() - 2; y <= row.bottom() + 2; ++y) {
                    if (linePixelsAt(y) >= 58) {
                        ++found;
                    }
                }
                return found;
            };

            QRect rows[4];
            QRect body[4];
            bool laid = true;
            for (int i = 0; i < 4; ++i) {
                const QTextCursor cursor(view->document()->findBlockByNumber(i));
                const QRect r = view->cursorRect(cursor);
                rows[i] = QRect(0, r.top(), viewportWidth, r.height());
                // 行体的那一段（去掉最下面那一像素，那里现在是线）：量底色用。
                body[i] = QRect(viewportWidth - 60, r.top(), 60, qMax(0, r.height() - 1));
                laid = laid && r.height() > 0;
                if (i > 0) {
                    laid = laid && rows[i].top() > rows[i - 1].top();
                }
            }
            laid = laid && viewportWidth > 120 && viewportHeight > 0 &&
                   rows[3].bottom() + 1 < viewportHeight;
            check(laid,
                  "Z6c 前提：四格都排过布局、自上而下都在视口里（不然量出来的是别的东西）",
                  QStringLiteral("视口 %1x%2 四格 y=%3/%4/%5/%6")
                      .arg(viewportWidth)
                      .arg(viewportHeight)
                      .arg(rows[0].y())
                      .arg(rows[1].y())
                      .arg(rows[2].y())
                      .arg(rows[3].y()));

            // 那条线的颜色**两头都得看得出来**：白底上（两条 INFO 的底色就是白）、以及
            // Debug 那个最浅的 `#eef2f6` 上。和"两两看得出来"用的是同一个阈值 20。
            check(separator.isValid() &&
                      colorDistance(separator, QColor(0xff, 0xff, 0xff)) >= 20 &&
                      colorDistance(separator, GameLogWindow::levelBackground(L::Debug)) >= 20 &&
                      colorDistance(separator, GameLogWindow::levelBackground(L::Error)) >= 20,
                  "Z6c 那条线的颜色在白底、Debug 底、Error 底上都看得出来（不然等于没画）",
                  QStringLiteral("线=%1 白差=%2 Debug差=%3 Error差=%4")
                      .arg(separator.name())
                      .arg(colorDistance(separator, QColor(0xff, 0xff, 0xff)))
                      .arg(colorDistance(separator, GameLogWindow::levelBackground(L::Debug)))
                      .arg(colorDistance(separator, GameLogWindow::levelBackground(L::Error))));

            // 头两行的底色**确实是同一个**（都是没染的那一档）—— 这是下面那条的前提，
            // 也是"这条线为什么必须有"的证明。
            const QColor firstBody = dominantColor(shot, body[0]);
            const QColor secondBody = dominantColor(shot, body[1]);
            check(colorDistance(firstBody, secondBody) <= 6 && colorDistance(firstBody, separator) >= 20,
                  "Z6c 前提：相邻两条 INFO 的底色本来就是**同一个**（分得开它们的只有那条线）",
                  QStringLiteral("第一行=%1 第二行=%2").arg(firstBody.name(), secondBody.name()));

            const int between01 = fullLinesUnder(rows[0]);
            const int between12 = fullLinesUnder(rows[1]);
            const int between23 = fullLinesUnder(rows[2]);
            const int under3 = fullLinesUnder(rows[3]);
            const int midRow = linePixelsAt(rows[0].top() + rows[0].height() / 2);
            const QString detail =
                QStringLiteral("0|1=%1 1|2=%2 2|3=%3 末尾底下=%4 行中间=%5（线色 %6，粗细 %7）")
                    .arg(between01)
                    .arg(between12)
                    .arg(between23)
                    .arg(under3)
                    .arg(midRow)
                    .arg(separator.name())
                    .arg(GameLogWindow::separatorHeight());

            // ① **这一条就是用户要的那件事**：两条底色相同的行之间真的有一条线。
            check(between01 == GameLogWindow::separatorHeight(),
                  "Z6c 相邻两条 INFO 之间真的有一条细线（底色一样，线是唯一分得开它们的东西）",
                  detail);
            // ② 每一档底下都画 —— 不是只给"不染底"的那几档画。
            check(between12 == GameLogWindow::separatorHeight(),
                  "Z6c ERROR 那一行底下也有一条（每一行都画，不是只给不染底的档画）", detail);
            check(between23 == GameLogWindow::separatorHeight(),
                  "Z6c 染了底的那一行和末尾那半行之间也有", detail);
            // ③ 最后一格底下**没有**：那是整份文本的末尾，不是"两行之间"。
            check(under3 == 0, "Z6c 最后一格底下没有那条线（末尾不是「两行之间」）", detail);
            // ④ 线只占行最下面那一像素 —— 行中间一个线的像素都不许有（"刷整行"那种做法
            // 这一条立刻红）。配合 ① 的「只有一条」，粗细两头就都钉住了。
            check(midRow == 0, "Z6c 线只在行最下面的那一像素上（不是把整行刷成灰的）", detail);
        }
        log->close();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(guard.isNull(), "Z6c 收尾：这一节那个窗关掉就销毁（WA_DeleteOnClose）", QString());
    }
    return 0;
}


