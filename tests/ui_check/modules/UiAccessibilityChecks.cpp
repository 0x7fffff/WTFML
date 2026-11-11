#include "Support.hpp"
int UiAccessibilityChecks(QMainWindow &window) {
    // ------------------------------------------------------------------
    // 那一句「完善键盘焦点、Tab 顺序和减少动态效果」，以及最后那条
    // 「每收口一个页面同步检查键盘导航、焦点可见性和减少动态效果」。
    //
    // 为什么是几何判据、而不是把每一条链的控件名抄一遍：抄名字只钉住"今天这一份布置"，
    // 而真会犯的毛病是"新加了一格，它建得比底下那排按钮还晚 ⇒ 焦点链把它排到按钮后面"
    // ——那种只有这条通用规矩抓得住（下面第一条就是这么抓出来的：`InlineMessageBox` 里
    // 输入框 / 列表 / 那个勾都是 `setXxx()` 后插的，建得比按钮行晚）。
    // 名字链那一条留在 W14，因为主题那一栏的顺序是用户点名的。
    {
        // 从 `first` 出发沿焦点链走一圈，收出"按 Tab 真能到"的那些（不显示的、不是
        // TabFocus 的都跳过 —— 和 Qt 自己按 Tab 时挑法一致）。
        const auto tabChain = [](QWidget *first, QWidget *container) {
            QVector<QWidget *> chain;
            QWidget *cursor = first;
            while (cursor != nullptr && chain.size() < 128) {
                cursor = cursor->nextInFocusChain();
                if (cursor == nullptr || cursor == first) {
                    break;
                }
                if (!(cursor->focusPolicy() & Qt::TabFocus) || !cursor->isVisibleTo(container)) {
                    continue;
                }
                chain.append(cursor);
            }
            return chain;
        };
        const auto nameOf = [](QWidget *w) {
            const QString byName = w->objectName();
            return byName.isEmpty() ? QString::fromLatin1(w->metaObject()->className()) : byName;
        };
        // 「往回跳」= 眼睛看到的顺序反了。按 Tab 一格一格走，方向得一直是**往下**；唯一
        // 允许往上走的情形是「换到更右边那一栏」（左栏走到头 → 右边那一页的头，那是正常
        // 的"下一栏"，不是回头）：
        // · 同一"行"里（纵向重叠过半）：只许往右，往左算往回跳；
        // · 不是同一行：往上算往回跳，**除非**同时也往右（换栏）。
        // 为什么不是"只要往上就报"：多栏布局里"左栏最后一行 → 右栏第一行"必然是往上的
        // （右栏从页头下面起，比左栏底矮不了多少也会更高），那是正着走，不是往回跳。
        // 判"回头"的是"往上**且**没往右"。
        // 这一条抓到的两类（都是本节真红过的）：`InlineMessageBox` 里输入框 / 列表 /
        // 那个勾都是 `setXxx()` 后插的，建得比按钮行晚；左栏那几颗齿轮更是
        // `setFolderRows()` 运行时才建。
        // 一次报**全部**（最多列前 4 处）：这一条真正要修的往往是"整页建的时候顺序和摆的
        // 顺序对不上"，只报第一处会让人修一处、跑一趟、再冒一处。
        const auto backwardJump = [&nameOf](QWidget *container, const QVector<QWidget *> &chain) {
            QStringList jumps;
            for (int i = 1; i < chain.size(); ++i) {
                const QPoint a = chain.at(i - 1)->mapTo(container, QPoint(0, 0));
                const QPoint b = chain.at(i)->mapTo(container, QPoint(0, 0));
                const QRect ra(a, chain.at(i - 1)->size());
                const QRect rb(b, chain.at(i)->size());
                const int overlapY = qMin(ra.bottom(), rb.bottom()) - qMax(ra.top(), rb.top());
                const bool sameRow = overlapY > qMin(ra.height(), rb.height()) / 2;
                const bool back = sameRow ? (rb.left() < ra.left())
                                          : (rb.top() < ra.top() && rb.left() <= ra.left());
                if (!back) {
                    continue;
                }
                if (jumps.size() < 4) {
                    jumps << QStringLiteral("第 %1 格 → 第 %2 格：%3(%4,%5) → %6(%7,%8)")
                                 .arg(i - 1)
                                 .arg(i)
                                 .arg(nameOf(chain.at(i - 1)))
                                 .arg(a.x())
                                 .arg(a.y())
                                 .arg(nameOf(chain.at(i)))
                                 .arg(b.x())
                                 .arg(b.y());
                }
            }
            return jumps.join(QStringLiteral("；"));
        };

        // ---- a) 问一句名字那一档：输入框在最上面，按钮在它下面 ----
        {
            InlineMessageBox inputBox(
                MessageKind::Question, QStringLiteral("重命名实例"),
                QStringLiteral("新名字会成为 versions 下面那个目录的名字，也就是这个实例的"
                               "版本号。"),
                QStringList{QStringLiteral("确定"), QStringLiteral("取消")}, 0, -1);
            inputBox.setLineEdit(QStringLiteral("26.3"));
            inputBox.show();
            QApplication::processEvents();
            const QVector<QWidget *> chain = tabChain(&inputBox, &inputBox);
            check(chain.size() >= 3,
                  "U38. 问名字那一档起码有三格能 Tab 到（输入框 + 两颗按钮）",
                  QStringLiteral("只走到 %1 格").arg(chain.size()));
            const QString jump = backwardJump(&inputBox, chain);
            check(jump.isEmpty(),
                  "★★ U38. 问名字那一档按 Tab **不往回跳**（输入框 → 右边那排按钮，从上到"
                  "下、从左到右）",
                  jump);
            inputBox.hide();
            QApplication::processEvents();
        }

        // ---- b) 从一张列表里挑一个那一档：列表在最上面，按钮在它下面 ----
        {
            InlineMessageBox listBox(
                MessageKind::Question, QStringLiteral("挑一个"),
                QStringLiteral("下面这几条里挑一条。"),
                QStringList{QStringLiteral("确定"), QStringLiteral("取消")}, 0, -1);
            listBox.setList(QStringList{QStringLiteral("一"), QStringLiteral("二"),
                                        QStringLiteral("三")},
                            0);
            listBox.show();
            QApplication::processEvents();
            const QVector<QWidget *> chain = tabChain(&listBox, &listBox);
            check(chain.size() >= 3, "U38. 挑一条那一档起码有三格能 Tab 到（列表 + 两颗按钮）",
                  QStringLiteral("只走到 %1 格").arg(chain.size()));
            const QString jump = backwardJump(&listBox, chain);
            check(jump.isEmpty(),
                  "★★ U38. 挑一条那一档按 Tab 也不往回跳（列表 → 右边那排按钮）", jump);
            listBox.hide();
            QApplication::processEvents();
        }

        // ---- c) 带那个勾的一档（EULA 那道闸）：勾在按钮上面 ----
        {
            InlineMessageBox eulaBox(
                MessageKind::Question, QStringLiteral("同意并启动"),
                QStringLiteral("要接着跑就得先应下这一条。"),
                QStringList{QStringLiteral("同意并启动"), QStringLiteral("取消")}, 0, -1);
            eulaBox.setCheckBox(QStringLiteral("我已阅读并同意"), 0);
            eulaBox.show();
            QApplication::processEvents();
            const QVector<QWidget *> chain = tabChain(&eulaBox, &eulaBox);
            const QString jump = backwardJump(&eulaBox, chain);
            check(chain.size() >= 3 && jump.isEmpty(),
                  "★★ U38. 带那个勾的一档按 Tab 也不往回跳（那个勾 → 右边那排按钮）", jump);
            eulaBox.hide();
            QApplication::processEvents();
        }

        // ---- d) 游戏日志窗（用户点名的唯一一扇独立窗）----
        {
            GameLogWindow log(QStringLiteral("⑭ Tab 顺序"));
            log.appendText(QStringLiteral("[12:00:00] [main/INFO]: 一行字\n"));
            log.resize(900, 600);
            log.show();
            QApplication::processEvents();
            const QVector<QWidget *> chain = tabChain(&log, &log);
            check(chain.size() >= 3,
                  "U38. 日志窗里起码有三格能 Tab 到（筛选那几颗 + 下面那两颗）",
                  QStringLiteral("只走到 %1 格").arg(chain.size()));
            const QString jump = backwardJump(&log, chain);
            check(jump.isEmpty(), "★★ U38. 日志窗按 Tab 不往回跳（筛选项在上面、操作在下面）",
                  jump);
            log.hide();
            QApplication::processEvents();
        }

        // ---- e) 主窗口当前那一页（拿一个**真的 MainWindowUI**，不是搭出来的小盒子）----
        // 这一条盯的是"整窗都在这一条规矩底下"：底栏、页表那几颗、当前页里那几十格
        // 一起走一遍。别的选项卡上的那些页这时不显示，链上自然跳过它们。
        if (!window.isVisible()) {
            std::printf("  SKIP U38. 主窗口这会儿没上屏，整窗的 Tab 顺序量不了\n");
            std::fflush(stdout);
        } else {
            const QVector<QWidget *> chain = tabChain(&window, &window);
            // 7a：底栏挪进了左导航轨的底部（左下角），在 Tab 链上仍排在当前页之后。从页面右边
            // 跳回左下角是「换一栏」，不算往回跳 ⇒ 页面那一段和轨底那一段各自量。
            QWidget *footer = window.findChild<QWidget *>(MainWindowUI::bottomBarName());
            QVector<QWidget *> pagePart;
            QVector<QWidget *> footerPart;
            for (QWidget *w : chain)
                (footer != nullptr && footer->isAncestorOf(w) ? footerPart : pagePart).append(w);
            QString jump = backwardJump(&window, pagePart);
            const QString footerJump = backwardJump(&window, footerPart);
            if (!footerJump.isEmpty())
                jump += (jump.isEmpty() ? QString() : QStringLiteral("；")) + footerJump;
            check(chain.size() >= 10,
                  "U38. 主窗口当前这一页有十几格能 Tab 到（量得到东西，不是空链）",
                  QStringLiteral("只走到 %1 格").arg(chain.size()));
            check(jump.isEmpty(),
                  "★★ U38. 整窗按 Tab 不往回跳（页表 → 当前页 → 底栏，一路向下）", jump);
        }

        // ---- f) 实例设置那一扇（左栏页表 + 右栏当前那一页）----
        // 和主窗口**同一个形状**（一列页表 + 一页内容），所以同一套"往上就是往回跳"
        // 的规矩也摆在这儿：这一扇是用户天天开的那一扇，页表左右两半的焦点顺序和主
        // 窗口一样会犯"建的时候顺序和摆的顺序对不上"的毛病。
        {
            using Dlg = InstanceSettingsDialog;
            Dlg::GlobalValues globalValues;
            globalValues.javaPath = QStringLiteral("C:/全局/java.exe");
            globalValues.xmx = 4096;
            globalValues.xms = 1024;
            globalValues.autoMemory = false;
            Dlg dialog(QStringLiteral("我的整合包"), QStringLiteral("我的整合包-1.20.1-Forge"),
                       Dlg::Override(), globalValues, QStringList(), true);
            dialog.resize(980, 700);
            dialog.show();
            QApplication::processEvents();
            const QVector<QWidget *> chain = tabChain(&dialog, &dialog);
            check(chain.size() >= 8,
                  "U38. 实例设置那一扇里能 Tab 到的不止两三格（量得到东西，不是空链）",
                  QStringLiteral("只走到 %1 格").arg(chain.size()));
            const QString jump = backwardJump(&dialog, chain);
            check(jump.isEmpty(),
                  "★★ U38. 实例设置那一扇按 Tab 也不往回跳（左栏页表 → 右边那一页）", jump);
            dialog.hide();
            QApplication::processEvents();
        }
    }
    return 0;
}


