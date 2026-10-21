#include "Support.hpp"
int MwRapidRowActivationChecks(MainWindow &window, QTreeWidget * &versionTree, QStackedWidget * &gameStack, QLineEdit * &installNameEdit, QLabel * &installTargetLabel, QPushButton * &installBackBtn) {
    // ------------------------------------------------------------------
    // 要求：「还有下载版本那里如果一直点那个展开关闭会有几次点击失效」。
    // 真因见 ui/RowActivation.hpp 文件头：Windows 上**点得快的第二发（以及后面每一发）到的
    // 是 MouseButtonDblClick**，而视图的 itemClicked 只在"按下 + 松开都到手"时才发（双击
    // 那一发的松开被 releaseFromDoubleClick 挡掉）⇒ 那几发点击**全丢**。修法是给接线口一个
    // "这一行是开/关"的判据，开关行把那几发补上；**动作行（版本号、加载器版本号）不给**，
    // 保持一次手势一发（双击不该把安装页开两遍 / 下载两遍）。
    //
    // NOTE: 这一节必须发**真的鼠标事件**（发到 viewport 上）。本文件里到处在用的那一招
    // —— 伪造 itemClicked 信号 —— 正好绕过要验的那一段：补发那一路走的是 viewport
    // 上的事件过滤器，根本不经过信号。
    {
        check(versionTree != nullptr && versionTree->topLevelItemCount() > 0,
              "版本树上真有分类条目（下面按它翻）",
              versionTree != nullptr ? QStringLiteral("%1 条").arg(versionTree->topLevelItemCount())
                                     : QStringLiteral("没有版本树"));

        const auto sendMouseTo = [](QTreeWidget *tree, QEvent::Type type, const QPoint &pos,
                                    Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(pos), QPointF(tree->viewport()->mapToGlobal(pos)),
                              Qt::LeftButton, buttons, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &event);
        };
        // 一发普通点击：按下 + 松开（视图两个都到手 → 发 itemClicked）。
        const auto clickRow = [&](QTreeWidget *tree, QTreeWidgetItem *item) {
            const QPoint pos = tree->visualItemRect(item).center();
            sendMouseTo(tree, QEvent::MouseButtonPress, pos, Qt::LeftButton);
            sendMouseTo(tree, QEvent::MouseButtonRelease, pos, Qt::NoButton);
            QApplication::processEvents();
        };
        // **点得快的那一发**：到的是 MouseButtonDblClick。视图自己的双击处理
        // （qtreeview.cpp:2007 → qabstractitemview.cpp:2012）在这一发上**不发** clicked
        // （松开时 releaseFromDoubleClick 把它挡掉）—— 所以修之前这一发是白白丢掉的。
        // NOTE: 真机上"第二下以及后面点得够快的每一下"都是这个事件，所以下面凡是快点击
        // 都紧跟在一次 clickRow 之后发（那样 pressedIndex 已经指着这一行，和真机一致）。
        const auto fastClickRow = [&](QTreeWidget *tree, QTreeWidgetItem *item) {
            const QPoint pos = tree->visualItemRect(item).center();
            sendMouseTo(tree, QEvent::MouseButtonDblClick, pos, Qt::LeftButton);
            sendMouseTo(tree, QEvent::MouseButtonRelease, pos, Qt::NoButton);
            QApplication::processEvents();
        };
        // 连着点 n 下：第一下是普通的，后面每一下都是"点得快"的那一发。
        const auto burstRow = [&](QTreeWidget *tree, QTreeWidgetItem *item, int n) {
            for (int i = 0; i < n; ++i) {
                if (i == 0) {
                    clickRow(tree, item);
                } else {
                    fastClickRow(tree, item);
                }
            }
        };

        if (versionTree != nullptr && versionTree->topLevelItemCount() > 0) {
            QTreeWidgetItem *top = versionTree->topLevelItem(0);
            const bool wasExpanded = top->isExpanded();
            const int stackBefore = gameStack != nullptr ? gameStack->currentIndex() : -1;

            // NOTE: 每一小段都**先自己把状态摆好**再点。上面的断言一旦红了，后面的起点就
            // 不是它以为的那个了 —— 那种"因为上一条错了所以这条看起来是对的"最坑。
            top->setExpanded(true);
            QApplication::processEvents();
            clickRow(versionTree, top);
            check(!top->isExpanded(), "Y7 点一下分类：翻一次（收起）");
            clickRow(versionTree, top);
            check(top->isExpanded(), "Y7 再点一下分类：又翻一次（展开）");

            // 这两条就是"一直点会有几次失效"：点得快的那一发到的是双击事件，
            // 补发之前它是**完全丢掉**的（展开状态一下都不动）。两条各换一个起点，
            // 这样"补发一下都没走"和"补发走反了"都跑不掉。
            top->setExpanded(false);
            QApplication::processEvents();
            fastClickRow(versionTree, top);
            check(top->isExpanded(), "Y7 点得快的那一发也算（收起 → 展开）—— 以前这一发被 Qt 压掉");
            top->setExpanded(true);
            QApplication::processEvents();
            fastClickRow(versionTree, top);
            check(!top->isExpanded(), "Y7 再快一发也算（展开 → 收起）");

            // 连着点六下 = 翻六次 ⇒ **回到原样**（展开）。修之前后面五下全丢，只剩第一下，
            // 它会停在收起 —— 偶数下正是为了让"少算的那几下"露出来（奇数下会假装对）。
            top->setExpanded(true);
            QApplication::processEvents();
            burstRow(versionTree, top, 6);
            check(top->isExpanded(), "Y7 连着点六下 = 翻六次（一下都没丢，回到原样）");
            check(gameStack == nullptr
                      || gameStack->currentIndex() == MainWindowUI::downloadGameTreeIndex(),
                  "Y7 全程没进安装页（分类行只是开关）",
                  gameStack != nullptr ? QString::number(gameStack->currentIndex()) : QString());

            // 动作行（版本号）：**不给补发** —— 双击只算一次手势（不然安装页会开两遍，
            // 第二遍还会把四个在飞的加载器版本请求 abort 掉重发）。清单没拉到的机器上
            // 自己插一条假的，走的是和真版本号完全相同的信号路（和 Y6 段一个做法）。
            top->setExpanded(true);
            QApplication::processEvents();
            QTreeWidgetItem *child = top->child(0);
            const bool synthetic = child == nullptr;
            if (synthetic) {
                child = new QTreeWidgetItem(top);
                child->setText(MainWindowUI::versionTreeKeyColumn(), QStringLiteral("1.21.4"));
                child->setData(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole,
                               QStringLiteral("1.21.4"));
                QApplication::processEvents();
            }
            const QString pickedId =
                child->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole).toString();
            // 普通点一下：进安装页，进的就是这一份。
            clickRow(versionTree, child);
            check(gameStack != nullptr
                      && gameStack->currentIndex() == MainWindowUI::downloadGameInstallIndex(),
                  "Y7 普通点一下**版本号**：进安装页",
                  gameStack != nullptr ? QString::number(gameStack->currentIndex()) : QString());
            check(installTargetLabel != nullptr
                      && installTargetLabel->text().contains(pickedId),
                  "Y7 进的是那一份（点的是哪一个，看得见）", pickedId);

            // 动作行**不给补发**：紧接着的那一发（真·双击的第三下，到的是双击事件）不许
            // 再把安装页开一遍。看得见的代价是**名称框里刚改的名字会被那第二遍抹掉**
            // （openInstallPage() 会把名字重置成版本号）—— 这里就拿它当证人。
            // NOTE: 真机上那第二下确实会落到树上（Windows 在按下时就把鼠标捕获给了这个
            // 控件，切页不会把它抢走 —— 老账里"openInstallPage 跑两遍"就是这么来的），
            // 所以下面直接发给 viewport 是真机的样子，不是硬凑的场景。
            const QString sentinel = QStringLiteral("手改的名字");
            if (installNameEdit != nullptr) {
                installNameEdit->setText(sentinel);
                QApplication::processEvents();
                check(installNameEdit->text() == sentinel,
                      "Y7 先在名称框里留个记号（下面看它会不会被第二遍抹掉）",
                      installNameEdit->text());
                fastClickRow(versionTree, child);
                check(installNameEdit->text() == sentinel,
                      "Y7 双击的第二下没有再开一遍安装页（刚改的名字还在）",
                      installNameEdit->text());
            }
            if (installBackBtn != nullptr) {
                installBackBtn->click();
                QApplication::processEvents();
            }
            if (synthetic) {
                top->removeChild(child);
                delete child;
            }

            // 收拾成进来时那个样子（后面几节还拿这棵树当开局）。
            top->setExpanded(wasExpanded);
            if (stackBefore >= 0 && gameStack != nullptr) {
                gameStack->setCurrentIndex(stackBefore);
            }
            QApplication::processEvents();
        }

        // 加载器那棵树：顶层那几条（除「不装」）也是**开关行**。这一节不假设树上一定有
        // 可点的加载器（换了台"列表没取到"的机器，四条全是灰占位）—— 没有就只印一句。
        if (auto *loaderTree = byObjectName<QTreeWidget>(&window, MainWindowUI::loaderTreeName())) {
            QTreeWidgetItem *toggle = nullptr;
            for (int i = 0; i < loaderTree->topLevelItemCount(); ++i) {
                QTreeWidgetItem *candidate = loaderTree->topLevelItem(i);
                if (candidate == nullptr || candidate->isDisabled()) {
                    continue;
                }
                const int candidateId =
                    candidate->data(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole).toInt();
                if (candidateId == MainWindowUI::modLoaderNoneIndex()) {
                    continue;  // 「不装」点它是选中，算做事 —— 不是开关
                }
                if (candidateId < 0) {
                    continue;  // 「预设」那一行点它开的是弹窗（P1），不是开关
                }
                if (candidate->data(MainWindowUI::loaderTreeKeyColumn(),
                                    MainWindowUI::rowClearRole())
                        .toBool()) {
                    continue;  // 选中的那条行尾有个「取消」小叉，别点到它身上
                }
                toggle = candidate;
                break;
            }
            if (toggle == nullptr) {
                std::printf("  ----  加载器树上没有可点的加载器条目（列表没取到）—— 这一段跳过\n");
            } else {
                const bool wasExpanded = toggle->isExpanded();
                toggle->setExpanded(true);
                QApplication::processEvents();
                clickRow(loaderTree, toggle);
                check(!toggle->isExpanded(), "Y7 加载器树：点一下翻一次（收起）");
                toggle->setExpanded(false);
                QApplication::processEvents();
                fastClickRow(loaderTree, toggle);
                check(toggle->isExpanded(),
                      "Y7 加载器树：点得快的那一发也算（收起 → 展开）");
                toggle->setExpanded(true);
                QApplication::processEvents();
                burstRow(loaderTree, toggle, 6);
                check(toggle->isExpanded(), "Y7 加载器树：连着点六下 = 翻六次（回到原样）");
                toggle->setExpanded(wasExpanded);
                QApplication::processEvents();
            }
        }
    }
    return 0;
}


