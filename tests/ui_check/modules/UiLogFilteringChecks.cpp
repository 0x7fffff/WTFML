#include "Support.hpp"
int UiLogFilteringChecks() {
    {
        auto *log = new GameLogWindow(QStringLiteral("筛选这一节"));
        QPointer<GameLogWindow> guard(log);
        log->show();
        QApplication::processEvents();
        QPlainTextEdit *view = log->getLogView();
        using L = GameLogWindow::LogLevel;
        if (view != nullptr) {
            // ---- (a) 五档各来一条，看计数 ----
            log->appendText(QStringLiteral("[12:34:56] [main/DEBUG]: d\n"
                                           "[12:34:56] [main/INFO]: i\n"
                                           "[12:34:56] [main/WARN]: w\n"
                                           "[12:34:56] [main/ERROR]: e\n"
                                           "[12:34:56] [main/FATAL]: f\n"));
            check(log->lineCount() == 5, "Z5 五行都进了模型", QString::number(log->lineCount()));
            int sum = 0;
            QStringList perLevel;
            for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
                const L level = GameLogWindow::levelAt(i);
                sum += log->lineCountFor(level);
                perLevel << QStringLiteral("%1=%2")
                                .arg(GameLogWindow::levelName(level))
                                .arg(log->lineCountFor(level));
            }
            check(sum == log->lineCount(),
                  "Z5 这五个显式级别各是一条记录，所以记录数正好等于物理行数",
                  QStringLiteral("%1 合计 %2 / 总 %3")
                      .arg(perLevel.join(QStringLiteral(" ")))
                      .arg(sum)
                      .arg(log->lineCount()));
            QStringList countWrong;
            for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
                const L level = GameLogWindow::levelAt(i);
                if (log->lineCountFor(level) != 1) {
                    countWrong << QStringLiteral("%1=%2")
                                      .arg(GameLogWindow::levelName(level))
                                      .arg(log->lineCountFor(level));
                }
            }
            check(countWrong.isEmpty(), "Z5 五档各记了一条（每行都按自己的级别进了账）",
                  countWrong.join(QStringLiteral(", ")));

            // ---- (b) 按钮上的字：级别名 + 数字（数字是唯一的出处算出来的）----
            QStringList textWrong;
            for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
                const L level = GameLogWindow::levelAt(i);
                QToolButton *button = log->getFilterButton(level);
                const QString want =
                    GameLogWindow::filterButtonText(level, log->lineCountFor(level));
                if (button == nullptr || button->text() != want) {
                    textWrong << QStringLiteral("%1：%2（该是 %3）")
                                     .arg(GameLogWindow::levelName(level),
                                          button != nullptr ? button->text()
                                                            : QStringLiteral("<没这颗按钮>"),
                                          want);
                }
            }
            check(textWrong.isEmpty(), "Z5 五颗按钮上写的就是「级别 数字」",
                  textWrong.join(QStringLiteral("; ")));

            // ---- (c) 关掉一档：视图里没了，账上还在 ----
            // 这是这一节最要命的一条：数字要是跟着"显示了几条"算，用户关掉 ERROR 之后
            // 明明刚才看到「ERROR 5」，一关就变成 0 了 —— 那等于把刚发生的事抹掉。
            log->setLevelVisible(L::Error, false);
            check(!log->logText().contains(QStringLiteral("[main/ERROR]")) &&
                      log->logText().contains(QStringLiteral("[main/WARN]")) &&
                      log->logText().contains(QStringLiteral("[main/INFO]")),
                  "Z5 关掉 ERROR：那一条不显示了，别的几条还在");
            check(log->lineCountFor(L::Error) == 1 && log->lineCount() == 5,
                  "Z5 但**账没变**：数字说的是「有多少条记录」，不是「显示了几条」",
                  QStringLiteral("ERROR=%1 总=%2")
                      .arg(log->lineCountFor(L::Error))
                      .arg(log->lineCount()));
            check(log->getFilterButton(L::Error) != nullptr &&
                      log->getFilterButton(L::Error)->isChecked() == false,
                  "Z5 那一颗按钮的勾跟着状态走了");
            // 关掉之后**新来的**那一档也不许摆出来：只重画一次是不够的 —— 用户关掉 ERROR
            // 之后游戏还在接着报错，那些新的也得挡住（这条是"筛选只管重画那一下"那个漏法
            // 的看门人：把 appendText 里那个 if 拆掉，这里就红）。
            log->appendText(QStringLiteral("[12:34:56] [main/ERROR]: 关掉之后又来的一条\n"));
            check(!log->logText().contains(QStringLiteral("关掉之后又来的一条")),
                  "Z5 关掉之后**新来的**那一档也不摆出来", log->logText().right(80));
            check(log->lineCountFor(L::Error) == 2 && log->lineCount() == 6,
                  "Z5 而新来的那条照样进账（数字不因为被筛掉就不算）",
                  QStringLiteral("ERROR=%1 总=%2")
                      .arg(log->lineCountFor(L::Error))
                      .arg(log->lineCount()));

            // ---- (d) 五档全关：一句"都被筛掉了"，而不是假装"还没有输出" ----
            for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
                log->setLevelVisible(GameLogWindow::levelAt(i), false);
            }
            check(log->logText().isEmpty(), "Z5 五档全关 → 视图里一行不剩",
                  QString::number(log->logText().size()));
            check(view->placeholderText() == GameLogWindow::filteredOutPlaceholderText(),
                  "Z5 空的原因不一样，说的那句话也不一样（都被筛掉了 ≠ 还没有输出）",
                  view->placeholderText());
            check(log->lineCount() == 6, "Z5 但六行都还在模型里（关掉显示不是丢掉）",
                  QString::number(log->lineCount()));

            // NOTE: 这里原来是点那颗「显示全部」按钮 —— 它 换成了行数下拉框
            // （用户点名要的，见 Z10），于是"一键把五档全勾回来"没有了。回退的路只剩
            // 占位那句话指的那一条，所以先钉住**那句指引本身还在**（不然这一档就真成了
            // 死胡同：五档全关之后界面上没有任何一句话说得出怎么办），再自己勾回来。
            check(GameLogWindow::filteredOutPlaceholderText().contains(QStringLiteral("勾")),
                  "Z5 五档全关之后那句占位得说清怎么回去（那颗「显示全部」已经没有了）",
                  GameLogWindow::filteredOutPlaceholderText());
            for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
                log->setLevelVisible(GameLogWindow::levelAt(i), true);
            }
            bool allBack = true;
            for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
                allBack = allBack && log->levelVisible(GameLogWindow::levelAt(i));
            }
            check(allBack && log->logText().contains(QStringLiteral("[main/ERROR]")) &&
                      log->logText().contains(QStringLiteral("[main/FATAL]")),
                  "Z5 把那五档勾回来、字也回来了（免得空窗看着像坏了）");
            check(view->placeholderText() == GameLogWindow::emptyPlaceholderText(),
                  "Z5 全回来之后占位那句话也回到「还没有输出」", view->placeholderText());

            // 一条错误后面的崩栈是同一条记录的续行：正文和底色都保留，但顶部数字不能把
            // 一次异常膨胀成几十次 ERROR。
            log->appendText(QStringLiteral("    at net.minecraft.client.Main.main(Main.java:42)\n"));
            check(log->lineCount() == 7 && log->lineCountFor(L::Error) == 2,
                  "Z5 ERROR 的崩栈续行留在模型里，但不重复增加错误记录数",
                  QStringLiteral("物理行=%1 ERROR记录=%2")
                      .arg(log->lineCount())
                      .arg(log->lineCountFor(L::Error)));

            // ---- (e) 半行：进程输出是按块来的，一个块可能断在行中间 ----
            log->getClearButton()->click();
            check(log->lineCount() == 0 && log->lineCountFor(L::Info) == 0 &&
                      log->logText().isEmpty(),
                  "Z5 清空把模型、账、正文一起清了（只清正文的话按钮上会留着数字）",
                  QStringLiteral("总=%1").arg(log->lineCount()));
            check(view->placeholderText() == GameLogWindow::emptyPlaceholderText(),
                  "Z5 清空之后那句占位也回到「还没有输出」", view->placeholderText());

            log->appendText(QStringLiteral("第一行\n第二"));
            check(log->lineCount() == 1 && log->logText() == QStringLiteral("第一行\n第二"),
                  "Z5 没等到换行的那半行先原样摆着、还不算一条",
                  QStringLiteral("行数=%1 正文=%2")
                      .arg(log->lineCount())
                      .arg(log->logText()));
            log->appendText(QStringLiteral("行\n"));
            check(log->lineCount() == 2 && log->logText() == QStringLiteral("第一行\n第二行\n"),
                  "Z5 补齐之后合成**一条**，不是劈成两条",
                  log->logText());
            check(view->document()->blockCount() == 3,
                  "Z5 两行 + 末尾那一格空的（结构没多长出格子来）",
                  QString::number(view->document()->blockCount()));

            // 断在级别正中间：这时候还不能认，认了就是猜。
            log->appendText(QStringLiteral("[12:34:56] [main/ER"));
            check(log->lineCount() == 2 && log->lineCountFor(L::Error) == 0,
                  "Z5 级别被切断时先攒着不认",
                  QStringLiteral("行数=%1 ERROR=%2")
                      .arg(log->lineCount())
                      .arg(log->lineCountFor(L::Error)));
            log->appendText(QStringLiteral("ROR]: boom\n"));
            check(log->lineCount() == 3 && log->lineCountFor(L::Error) == 1 &&
                      log->logText().endsWith(QStringLiteral("[12:34:56] [main/ERROR]: boom\n")),
                  "Z5 补齐之后认出来了 —— 原来那半行是个 ERROR",
                  QStringLiteral("ERROR=%1").arg(log->lineCountFor(L::Error)));

            // CRLF：那个 \r 是行尾符，不是内容。
            log->appendText(QStringLiteral("aaa\r\n"));
            check(log->logText().endsWith(QStringLiteral("aaa\n")) &&
                      !log->logText().contains(QLatin1Char('\r')),
                  "Z5 CRLF 的那个回车不算内容（不然复制出来每行末尾都躲一个 \\r）");

            // ---- (f) 一直不给换行的那种输出，不许把内存养大 ----
            log->getClearButton()->click();
            log->appendText(QString(5000, QLatin1Char('x')));
            check(log->lineCount() == 0 && log->logText().size() == 5000,
                  "Z5 前提：没到闸（5000 < 8192）之前那半行照样看得见",
                  QStringLiteral("行数=%1 正文字数=%2")
                      .arg(log->lineCount())
                      .arg(log->logText().size()));
            log->appendText(QString(4000, QLatin1Char('y')));
            // 9000 个字 + 末尾那个换行 = 9001：收下这一条之后末尾照规矩又起了一格空的
            // （那一格是光标行），所以 toPlainText() 末尾多一个 \n。原来那半行是没有
            // 换行的，被"收下"之后就有了 —— 这是有意的：既然宣布它是一条了，下
            // 一块输出就该另起一行。
            check(log->lineCount() == 1 && log->logText().size() == 9001,
                  "Z5 攒过头就当成一整条收下（用 \\r 刷进度条的模组也不至于把这个窗撑爆）",
                  QStringLiteral("行数=%1 正文字数=%2")
                      .arg(log->lineCount())
                      .arg(log->logText().size()));

            // ---- (g) 行数上限：满了两万行之后丢的是**最老的**那条 ----
            log->getClearButton()->click();
            {
                QString bulk;
                // 前六条是 ERROR，后面全是 INFO：于是"丢的是最老的"和"丢的是最新的"
                // 在账上是**两个不同的数**（0 比 6）—— 不然这条断言两种实现都是绿的。
                const int extra = 20;
                const int total = GameLogWindow::maxLogBlocks() + extra;
                bulk.reserve(total * 34);
                for (int i = 0; i < total; ++i) {
                    bulk += i < 6 ? QStringLiteral("[12:34:56] [main/ERROR]: e%1\n").arg(i)
                                  : QStringLiteral("[12:34:56] [main/INFO]: i%1\n").arg(i);
                }
                QElapsedTimer timer;
                timer.start();
                log->appendText(bulk);
                const qint64 tookMs = timer.elapsed();
                check(log->lineCount() == GameLogWindow::maxLogBlocks(),
                      "Z5 满了两万行之后模型就停在上限上",
                      QStringLiteral("行数=%1 上限=%2 上这一次账花了 %3 ms")
                          .arg(log->lineCount())
                          .arg(GameLogWindow::maxLogBlocks())
                          .arg(tookMs));
                check(log->lineCountFor(L::Error) == 0 && log->lineCountFor(L::Info) ==
                              GameLogWindow::maxLogBlocks(),
                      "Z5 丢的是**最老的**那几条（最新的留着）—— 前六条 ERROR 全滚掉了",
                      QStringLiteral("ERROR=%1 INFO=%2")
                          .arg(log->lineCountFor(L::Error))
                          .arg(log->lineCountFor(L::Info)));
                check(log->logText().startsWith(QStringLiteral("[12:34:56] [main/INFO]: i20\n")),
                      "Z5 留下的是第 21 条往后（前面那 20 条真没了）",
                      log->logText().left(60));
                // 顺带钉住"视图和模型同步丢"：视图里最早那一格就是模型里最早那一条。
                check(view->document()->blockCount() ==
                          GameLogWindow::maxLogBlocks() + 1,
                      "Z5 视图那边同步丢（模型 + 末尾那一格空的）",
                      QStringLiteral("块数=%1").arg(view->document()->blockCount()));
            }
        }
        log->close();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    return 0;
}


