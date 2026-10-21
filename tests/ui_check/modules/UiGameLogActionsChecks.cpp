#include "Support.hpp"
int UiGameLogActionsChecks(QFrame * &bar) {
    {
        // 这个窗是"给我一行字我就写上去"的一块控件，不认识"会话"是什么（见它的文件头），
        // 所以在这里验它最合适：给它几行字，看它写没写上去、跟不跟到底、两颗钮管不管用。
        //
        // NOTE: 建在**堆**上，因为它自己带 WA_DeleteOnClose —— 关一下就析构，栈上的对象
        // 那么干就是 double free。
        auto *log = new GameLogWindow(QStringLiteral("我的整合包"));
        QPointer<GameLogWindow> guard(log);
        log->show();
        QApplication::processEvents();

        // ---- (a) 标题带实例名：同时开着好几份时靠它认哪个是哪个 ----
        check(GameLogWindow::windowTitleText(QStringLiteral("我的整合包"))
                      .contains(QStringLiteral("我的整合包")),
              "Z3 标题里带实例名", GameLogWindow::windowTitleText(QStringLiteral("我的整合包")));
        // 实例名是空的（会话快照没拿到）时不留一个光秃秃的破折号。
        check(GameLogWindow::windowTitleText(QString()) == QStringLiteral("游戏日志"),
              "Z3 没有实例名时标题就是「游戏日志」",
              GameLogWindow::windowTitleText(QString()));

        QPlainTextEdit *view = log->getLogView();
        check(view != nullptr && view->objectName() == GameLogWindow::logViewName(),
              "Z3 那块文本控件按名字找得回来",
              view != nullptr ? view->objectName() : QStringLiteral("<null>"));
        check(log->getHintLabel() != nullptr &&
                  log->getHintLabel()->text() == GameLogWindow::hintText() &&
                  !GameLogWindow::hintText().isEmpty(),
              "Z3 顶上那句说明在，而且有话说");
        check(log->getHintLabel() != nullptr && log->getClearButton() != nullptr &&
                  log->getCloseButton() != nullptr &&
                  log->getClearButton()->objectName() == GameLogWindow::clearButtonName() &&
                  log->getCloseButton()->objectName() == GameLogWindow::closeButtonName(),
              "Z3 清空/关闭两颗钮都在");

        // ---- 底下右角那两颗「导出…」 ----
        {
            QPushButton *exportLog = log->getExportLogButton();
            QPushButton *exportReport = log->getExportReportButton();
            check(exportLog != nullptr && exportReport != nullptr,
                  "Z3c 底下那两颗「导出…」都在");
            if (exportLog != nullptr && exportReport != nullptr) {
                check(exportLog->objectName() == GameLogWindow::exportLogButtonName() &&
                          exportReport->objectName() == GameLogWindow::exportReportButtonName(),
                      "Z3c 两颗都按各自的名字找得回来");
                check(exportLog->text() == GameLogWindow::exportLogButtonText() &&
                          exportReport->text() == GameLogWindow::exportReportButtonText(),
                      "Z3c 两颗钮上的字就是那两句",
                      exportLog->text() + QStringLiteral(" / ") + exportReport->text());
                // 两颗钮干的是两件事（一份是全量日志、一份是"头部 + 错误摘录 + 日志"），
                // 提示文字一模一样的话用户点哪个都心虚。
                check(!exportLog->toolTip().isEmpty() && !exportReport->toolTip().isEmpty() &&
                          exportLog->toolTip() != exportReport->toolTip(),
                      "Z3c 两颗的提示文字是**两句话**（说的不是同一件事）",
                      exportLog->toolTip() + QStringLiteral(" | ") + exportReport->toolTip());
                // 它们得有父容器 —— 摆在一个真的布局里才会跟着窗一起缩放（漏了 addWidget
                // 的话控件是"悬"在那儿的，测试里也找得到，但屏幕上位置是错的）。
                check(exportLog->parentWidget() != nullptr &&
                          exportLog->parentWidget() == exportReport->parentWidget(),
                      "Z3c 两颗在同一行里");
                // 「右下角」= 它们右边缘在那颗「关闭」的左边、且整个按钮组靠右。
                QPushButton *close = log->getCloseButton();
                if (close != nullptr) {
                    check(exportLog->x() < close->x() && exportReport->x() < close->x(),
                          "Z3c 两颗排在「关闭」左边（「关闭」是最右下那颗）",
                          QStringLiteral("导出日志 x=%1 / 报告 x=%2 / 关闭 x=%3")
                              .arg(exportLog->x())
                              .arg(exportReport->x())
                              .arg(close->x()));
                    check(exportLog->parentWidget() != nullptr &&
                              exportLog->parentWidget()->width() > 0 &&
                              exportReport->x() + exportReport->width() >
                                  exportLog->parentWidget()->width() / 2,
                          "Z3c 它们靠右半截（是右下角那一组，不是摊在最左边）",
                          QStringLiteral("报告右边缘 %1 / 父容器宽 %2")
                              .arg(exportReport->x() + exportReport->width())
                              .arg(exportLog->parentWidget() == nullptr
                                       ? 0
                                       : exportLog->parentWidget()->width()));
                }
            }
        }

        if (view != nullptr) {
            // ---- (b) 只读、等宽、有上限 ----
            check(view->isReadOnly(), "Z3 那块文本是只读的（是看输出的窗，不是输入框）");
            check(view->placeholderText() == GameLogWindow::emptyPlaceholderText() &&
                      !GameLogWindow::emptyPlaceholderText().isEmpty(),
                  "Z3 空的时候有一句占位", view->placeholderText());
            // 等宽：日志里有缩进和对齐的表格，比例字体读起来是散的。
            //
            // NOTE: 这里量的是"字体是不是 Qt 认的那个系统等宽字体"，**不是**
            // QFontInfo::fixedPitch()。后者是在问"这台机器上那个字体真的是等宽的
            // 吗"，而 offscreen 平台根本没有真字体库（Qt 只回一个叫 monospace 的
            // 占位族名、量出来 fixedPitch() 是 false）—— 那是**环境**的属性，不是
            // 我们的决定。按"颜色/字体由谁决定"来挑断言：挑字体这件事是我们干的，
            // 挑得对不对是环境的事。
            const QString fixedFamily =
                QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
            check(view->font().family() == fixedFamily,
                  "Z3 用的是 Qt 认的那个系统等宽字体",
                  QString("%1 vs %2").arg(view->font().family(), fixedFamily));
            check(view->font().family() != QApplication::font().family(),
                  "Z3 而且真的换掉了默认字体（不然上面那条是白验）",
                  QString("%1 vs %2").arg(view->font().family(), QApplication::font().family()));
            // 字号**比系统那个等宽字体大一号**（ 用户说的「字号稍微大一点」）。
            //
            // NOTE: 比的是**系统那一份**，不是 `GameLogWindow::logFont()` 自己 —— 跟自己比
            // 是空的（那个函数改回原样，这条跟着一起变）。另外点大小和像素大小二选一
            // （字体表给的可能是任一种，另一头是 -1），所以分两支量。
            {
                const QFont systemFixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
                const QFont logFont = view->font();
                bool bigger = false;
                QString fontDetail;
                if (systemFixed.pointSizeF() > 0) {
                    bigger = logFont.pointSizeF() > systemFixed.pointSizeF();
                    fontDetail = QStringLiteral("点 %1 vs 系统 %2")
                                     .arg(logFont.pointSizeF())
                                     .arg(systemFixed.pointSizeF());
                } else {
                    bigger = logFont.pixelSize() > systemFixed.pixelSize();
                    fontDetail = QStringLiteral("像素 %1 vs 系统 %2")
                                     .arg(logFont.pixelSize())
                                     .arg(systemFixed.pixelSize());
                }
                check(bigger && GameLogWindow::logFontPointDelta() > 0,
                      "Z3 正文字号比系统那个等宽字体**大一档**（用户说的「稍微大一点」）",
                      fontDetail);
            }
            // 行数上限**必须有**：挂了一百个模组的实例能吐几十万行，不封顶的话开一晚上
            // 就把内存吃干净了。上限是"超了丢最老的"，所以只要它 > 0 就是个真的闸。
            //
            // NOTE: 起视图的上限允许**比 maxLogBlocks() 多一格**：多出来的那一格
            // 是"还没等到换行的那半行"（它不在模型里）。两个上限必须对齐 —— 视图比模型
            // 能多留，模型里最早那一行才会被它自己同步丢掉；视图比模型少留的话，跑满
            // 两万行之后切一次筛选，最早那一行会从模型里又冒出来。
            check(GameLogWindow::maxLogBlocks() > 0 &&
                      view->maximumBlockCount() >= GameLogWindow::maxLogBlocks() &&
                      view->maximumBlockCount() <= GameLogWindow::maxLogBlocks() + 1,
                  "Z3 文本有行数上限，而且真的装上了（最多比模型多留一格给那半行）",
                  QString("maximumBlockCount=%1 模型上限=%2")
                      .arg(view->maximumBlockCount())
                      .arg(GameLogWindow::maxLogBlocks()));
            // 说明行比正文小一号、颜色是 PlaceholderText（和设置页那些说明同一套写法）。
            if (log->getHintLabel() != nullptr) {
                check(log->getHintLabel()->font().pointSize() < log->font().pointSize(),
                      "Z3 顶上那句说明比正文小一号",
                      QString("%1 vs %2")
                          .arg(log->getHintLabel()->font().pointSize())
                          .arg(log->font().pointSize()));
                check(log->getHintLabel()->palette().color(QPalette::WindowText) ==
                          log->getHintLabel()->palette().color(QPalette::PlaceholderText),
                      "Z3 那句说明的颜色是 PlaceholderText（灰色注释那一档）");
            }

            // ---- (c) 写进去的就是原样那几行 ----
            log->appendText(QStringLiteral("第一行\n"));
            check(log->logText() == QStringLiteral("第一行\n"),
                  "Z3 一行原样写进去（游戏自己的换行不动它）", log->logText().left(80));
            log->appendText(QString());
            check(log->logText() == QStringLiteral("第一行\n"),
                  "Z3 空串不会往文本里塞东西（也不炸）", log->logText().left(80));

            // ---- (d) 跟到底，但**用户往上翻的时候不跟** ----
            // 这一条是这类窗最招人烦的地方：正在往上翻看崩栈，新输出一来就被顶走。
            for (int i = 0; i < 200; ++i) {
                log->appendText(QStringLiteral("第 %1 行\n").arg(i));
            }
            QApplication::processEvents();
            QScrollBar *bar = view->verticalScrollBar();
            check(bar->maximum() > 0,
                  "Z3 前提：攒够内容之后滚动条真的有范围了（不然下面两条量不出东西）",
                  QString("max=%1").arg(bar->maximum()));

            // 在底下：新的一行要把它带到底
            bar->setValue(bar->maximum());
            log->appendText(QStringLiteral("跟着滚的那一行\n"));
            check(bar->value() == bar->maximum(),
                  "Z3 本来就在最底下时，新输出跟着滚到底",
                  QString("%1 vs %2").arg(bar->value()).arg(bar->maximum()));

            // 往上翻：位置不许动
            bar->setValue(0);
            const int before = bar->value();
            log->appendText(QStringLiteral("用户在读上面的时候来的一行\n"));
            check(bar->value() == before,
                  "Z3 用户在往上翻时不跟（不许把正在读的东西顶走）",
                  QString("%1 -> %2").arg(before).arg(bar->value()));
            check(log->logText().contains(QStringLiteral("用户在读上面")),
                  "Z3 那行字照样写进去了，只是没把视图带下去");

            // ---- (e) 两颗钮 ----
            log->getClearButton()->click();
            check(log->logText().isEmpty(), "Z3 「清空」把这一块文本清空了",
                  QString::number(log->logText().size()));
            check(bar->value() == 0, "Z3 清空之后回到顶部（不至于停在一个空范围中间）",
                  QString::number(bar->value()));

            // ---- (f) 关闭：发 finished + 真的析构 ----
            bool finishedFired = false;
            QObject::connect(log, &QDialog::finished, log,
                             [&finishedFired]() { finishedFired = true; });
            log->getCloseButton()->click();
            QApplication::processEvents();
            check(finishedFired,
                  "Z3 「关闭」发 finished（调用方正是靠它记住「这一份用户不想再看日志了」）");
            // WA_DeleteOnClose 的销毁走 deleteLater，所以得把 DeferredDelete 那批事件放出去。
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            check(guard.isNull(),
                  "Z3 关掉之后真的析构了（这些窗会留一整份文本，藏起来不销毁就是白占内存）");
        } else {
            delete log;  // 上面已经红好几条了，这里只负责别漏内存
        }
    }
    return 0;
}


