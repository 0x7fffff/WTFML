#include "Support.hpp"
int UiLogRetentionChecks() {
    {
        auto *log = new GameLogWindow(QStringLiteral("行数上限这一节"));
        QPointer<GameLogWindow> guard(log);
        log->show();
        QApplication::processEvents();
        QComboBox *combo = log->getLineLimitCombo();
        QPlainTextEdit *view = log->getLogView();
        QPushButton *more = log->getMoreButton();
        check(combo != nullptr && view != nullptr && more != nullptr && log->clearAction() != nullptr,
              "Z10 顶上那颗行数下拉框、「…」菜单和里面的清空都找得到");
        if (combo != nullptr && view != nullptr && more != nullptr) {
            // ---- (a) 六档，数字写全，默认「全部」 ----
            const QVector<int> choices = GameLogWindow::lineLimitChoices();
            QSet<int> uniqueChoices;
            for (int choice : choices) {
                uniqueChoices.insert(choice);
            }
            check(combo->count() == choices.size() && combo->count() == 6 &&
                      uniqueChoices.size() == choices.size() && choices.contains(0),
                  "Z10 六档两两不同，而且有「全部」那一档（0）",
                  QString::number(combo->count()));
            // NOTE: 第四档是 **5000**（要求里那个「25」是笔误，以 5000 为准）。
            // 这里写死 5000 而不是"随便有个中间值"：这一档就是把那条要求钉死的地方。
            check(choices.contains(500) && choices.contains(1000) && choices.contains(2000) &&
                      choices.contains(5000) && choices.contains(10000),
                  "Z10 500 / 1000 / 2000 / 5000 / 10000 那五档都在",
                  [&choices] {
                      QStringList all;
                      for (int c : choices) {
                          all.append(QString::number(c));
                      }
                      return all.join(QStringLiteral(","));
                  }());
            // 要求：「直接写数字，不要学我」（他打的是 1k / 2k）。数字写全了才算数 ——
            // 而且"1000"和"10000"必须看得出是两档（写成 1k / 10k 也行，但别写成同一个）。
            check(GameLogWindow::lineLimitText(500) == QStringLiteral("500 条") &&
                      GameLogWindow::lineLimitText(1000) == QStringLiteral("1000 条") &&
                      GameLogWindow::lineLimitText(5000) == QStringLiteral("5000 条") &&
                      GameLogWindow::lineLimitText(10000) == QStringLiteral("10000 条") &&
                      GameLogWindow::lineLimitText(0) == QStringLiteral("全部"),
                  "Z10 数字写全（1000 不是 1k），最后一档写「全部」",
                  GameLogWindow::lineLimitText(1000));
            // 越界那一档回默认，而不是回 0 行 —— "看不出来"比"日志凭空没了"好得多。
            check(GameLogWindow::lineLimitOfIndex(-1) == GameLogWindow::defaultLineLimit() &&
                      GameLogWindow::lineLimitOfIndex(combo->count()) ==
                          GameLogWindow::defaultLineLimit(),
                  "Z10 越界的下标回默认那一档（不把上限设成 0 行）");
            check(log->displayLineLimit() == 0 &&
                      combo->currentIndex() >= 0 &&
                      combo->currentData().toInt() == 0,
                  "Z10 打开时是「全部」= 改之前的行为（进来就看得见全部日志）",
                  QStringLiteral("下标 %1").arg(combo->currentIndex()));

            // ---- (b) 滚轮防护：这一颗也装了 ----
            {
                const QString problem = wheelGuardProblem(combo);
                check(problem.isEmpty(), "Z10 这颗下拉框也装了滚轮防护", problem);
            }

            // ---- (c) 选了 500：视图只剩最后 500 行，模型一行没少 ----
            // 一次喂 1200 行（走 appendText，和真游戏输出同一条路）。
            {
                QString chunk;
                for (int i = 1; i <= 1200; ++i) {
                    chunk += QStringLiteral("[12:34:56] [main/INFO]: 第 %1 行\n").arg(i);
                }
                log->appendText(chunk);
                QApplication::processEvents();
                check(log->lineCount() == 1200 && view->blockCount() > 1000,
                      "Z10 前提：1200 行都进了模型和视图",
                      QStringLiteral("模型 %1 / 视图 %2")
                          .arg(log->lineCount())
                          .arg(view->blockCount()));

                combo->setCurrentIndex(combo->findData(500));
                QApplication::processEvents();
                const QString shown = log->logText();
                check(log->displayLineLimit() == 500, "Z10 选中 500 那一档之后上限是 500",
                      QString::number(log->displayLineLimit()));
                check(shown.contains(QStringLiteral("第 1200 行")) &&
                          shown.contains(QStringLiteral("第 701 行")) &&
                          !shown.contains(QStringLiteral("第 700 行")),
                      "Z10 视图只剩**最后** 500 行（第 701..1200 行在、第 700 行没了）",
                      QStringLiteral("视图 %1 行").arg(view->blockCount()));
                check(log->lineCount() == 1200,
                      "Z10 而模型里 1200 行一行没少（少看 ≠ 把日志扔了）",
                      QString::number(log->lineCount()));

                // 换回「全部」：那些行**当场回来**（它们一直在模型里）—— 这条是"上限只作用在
                // 视图那一层"的定义。写成"把模型也一起截了"的话这里只剩 500 行。
                combo->setCurrentIndex(combo->findData(0));
                QApplication::processEvents();
                check(log->displayLineLimit() == 0 &&
                          log->logText().contains(QStringLiteral("第 1 行")) &&
                          log->logText().contains(QStringLiteral("第 1200 行")),
                      "Z10 换回「全部」当场又是一千二百行（模型里那些没被截掉）",
                      QStringLiteral("视图 %1 行").arg(view->blockCount()));

                // 上限调**小**的那一下：模型里那一千二百行照样一行不少。
                combo->setCurrentIndex(combo->findData(500));
                QApplication::processEvents();
                check(log->lineCount() == 1200,
                      "Z10 再调小一次，模型还是 1200 行", QString::number(log->lineCount()));
            }

            // ---- (d) 「…」在标题行最右：正文之上、右对齐 ----
            const QPoint moreAt = more->mapTo(log, QPoint(0, 0));
            const QPoint viewAt = view->mapTo(log, QPoint(0, 0));
            check(moreAt.y() + more->height() <= viewAt.y(),
                  "Z10 「…」在正文之上的标题行里",
                  QStringLiteral("… y=%1 / 正文 y=%2").arg(moreAt.y()).arg(viewAt.y()));
            const int gap = log->width() - (moreAt.x() + more->width());
            check(gap >= 0 && gap < 40,
                  "Z10 「…」右对齐（右缘贴着窗口右边）",
                  QStringLiteral("差 %1 px（窗口宽 %2）").arg(gap).arg(log->width()));
        }
        log->close();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(guard.isNull(), "Z10 收尾：这一节那个窗关掉就销毁（WA_DeleteOnClose）", QString());
    }
    return 0;
}


