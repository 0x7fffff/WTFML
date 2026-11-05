#include "Support.hpp"

#include "UiInstanceFilesTestHelpers.hpp"

// U10M：拨一下启用/禁用之后，列表**不许跳回顶端**。
//
// 要求：「选中了模组并修改了启用/禁用状态以后整个表会弹回顶端，修掉」。根子是那两个"重列"
// 的地方都是 `m_list->clear()`（`refresh()` 一次、`rebuildRows()` 再一次）—— 那一下把滚动条
// 连人一起送回了 0，用户翻了半天的那一屏就没了。修法见
// `InstanceFilesPage::rememberScrollAnchor()`（记"顶上那一条是谁 + 它露了多少"，重列完再按
// 身份把它摆回去）。
//
// 这一节量的就是那件事，分四层：这个目录真的撑出了滚动条 → 滚动条的值没掉回 0 → 顶上还是
// 原来那一条 → 用户选中的那一条**没有被踢出选择**（改名之后按**身份路径**认得回来）。
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段的夹具。分出来以后要的三样
// （两个名字 + 全局那份值）都是显式形参；`mkfile` 用共享的 `uiMkFile()`。
int UiScrollAnchorChecks(const QString &instanceDisplayName, const QString &instanceDirName,
                         const InstanceSettingsDialog::GlobalValues &global) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    const auto mkfile = [](const QString &path) { uiMkFile(path); };
    // ---------- U10M 拨一下启用/禁用之后，列表**不许跳回顶端**----------
    //
    // 要求：「选中了模组并修改了启用/禁用状态以后整个表会弹回顶端，修掉」。
    // 根子是那两个"重列"的地方都是 `m_list->clear()`（`refresh()` 一次、
    // `rebuildRows()` 再一次）—— 那一下把滚动条连人一起送回了 0，用户翻了半天的那
    // 一屏就没了。修法见 `InstanceFilesPage::rememberScrollAnchor()`（记"顶上那一条
    // 是谁 + 它露了多少"，重列完再按身份把它摆回去）。
    //
    // 这一节量的就是那件事，分四层：这个目录真的撑出了滚动条 → 滚动条的值没掉回 0 →
    // 顶上还是原来那一条 → 用户选中的那一条**没有被踢出选择**（改名之后按**身份路径**
    // 认得回来，见 `InstanceFilesPage` 文件头那一段）。
    {
        QTemporaryDir rows;
        const QString rowsRoot = rows.path();
        const QString manyMods = rowsRoot + QStringLiteral("/mods");
        // 六十条：不够一屏的话"跳回顶端"这件事在屏幕上根本看不出来。
        for (int i = 0; i < 60; ++i) {
            mkfile(manyMods +
                   QStringLiteral("/mod-%1.jar").arg(i, 2, 10, QLatin1Char('0')));
        }
        QHash<QString, QString> rowsDirs;
        rowsDirs.insert(QStringLiteral("mod"), manyMods);
        Dlg rowsDialog(instanceDisplayName, instanceDirName, Dlg::Override(), global,
                       QStringList(), true);
        rowsDialog.setInstanceDir(rowsRoot);
        rowsDialog.setFileDirs(rowsDirs);
        rowsDialog.resize(900, 520);
        rowsDialog.show();
        showPage(rowsDialog, QStringLiteral("mod"));

        InstanceFilesPage *many = rowsDialog.filesPage(QStringLiteral("mod"));
        QListWidget *rowsList = many != nullptr ? many->list() : nullptr;
        QScrollBar *bar = rowsList != nullptr ? rowsList->verticalScrollBar() : nullptr;
        check(rowsList != nullptr && bar != nullptr && rowsList->count() == 60,
              "U10M 前提：摆好六十条（够撑出滚动条）",
              rowsList != nullptr ? QStringLiteral("%1 条").arg(rowsList->count())
                                  : QStringLiteral("<没这一页>"));
        if (rowsList != nullptr && bar != nullptr && rowsList->count() == 60) {
            QApplication::processEvents();
            check(bar->maximum() > 0,
                  "★ U10M 前提：这一列**真的滚得动**（不然下面几条量的是个死东西）",
                  QStringLiteral("0..%1").arg(bar->maximum()));

            // 顶上看不见第一条的那个位置（中间偏后）—— "跳回顶端"才看得出来。
            bar->setValue(bar->maximum() / 2);
            QApplication::processEvents();
            const int beforeValue = bar->value();
            // 「现在顶上看不见的那一条是谁」——锚点记的就是它，判据也就照它写。
            const auto topIdentity = [rowsList]() {
                for (int row = 0; row < rowsList->count(); ++row) {
                    QListWidgetItem *item = rowsList->item(row);
                    if (item != nullptr && rowsList->visualItemRect(item).bottom() > 0) {
                        return InstanceFilesPage::identityPath(
                            item->data(InstanceFilesPage::pathRole()).toString());
                    }
                }
                return QString();
            };
            const QString topBefore = topIdentity();
            check(beforeValue > 0 && !topBefore.isEmpty(),
                  "U10M 前提：这一列已经滚到中间了（顶上那条看得见、第一条看不见）",
                  QStringLiteral("value=%1 顶上=%2").arg(beforeValue).arg(topBefore));

            // 用户手里选着一条 —— 就是马上要拨开关的那一条（第 40 条，在第 27 行
            // 上下那一片里，离顶端足够远）。
            const QString target = manyMods + QStringLiteral("/mod-40.jar");
            int targetRow = -1;
            for (int row = 0; row < rowsList->count(); ++row) {
                if (rowsList->item(row)->data(InstanceFilesPage::pathRole()).toString() ==
                    target) {
                    targetRow = row;
                }
            }
            check(targetRow >= 0, "U10M 前提：要拨的那一条（mod-40.jar）在表里",
                  QStringLiteral("第 %1 行").arg(targetRow));
            if (targetRow >= 0) {
                rowsList->item(targetRow)->setSelected(true);
                QApplication::processEvents();

                // 拨开关那件事**在盘上就是改个名**（`X.jar` → `X.jar.disabled`），
                // 然后回头喊 `refresh()` —— 产品那一路（见 InstanceSettingsDialog /
                // MainWindow::toggleInstanceFile）一字不差地这么走。
                //
                // 和 U10R ⑤ 同一个理由 —— 这一发必须**从这一页自己
                // 走出去**（行尾开关 → `requestToggle()` 先记一笔"刚发出去的开关"），
                // 不能绕开它手工改名。绕开的那一路产品里根本不存在，而"改名之后
                // 还选着"那一笔账正是 `requestToggle()` 记下的（删除不许被当成改名，
                // 见 `m_pendingIdentityMigration`）。
                const QString renamed = target + QStringLiteral(".disabled");
                const QMetaObject::Connection simConn = QObject::connect(
                    many, &InstanceFilesPage::toggleRequested, many,
                    [many](const QString &path) {
                        const QString suffix = InstanceFilesPage::disabledSuffix();
                        const QString dest =
                            path.endsWith(suffix)
                                ? path.left(path.size() - suffix.size())
                                : path + suffix;
                        check(QFile::rename(path, dest), "U10M 前提：盘上改好名了", path);
                        many->refresh();
                    });
                QAbstractButton *rowSwitch = many->toggleButtonAt(targetRow);
                check(rowSwitch != nullptr, "U10M 前提：那一行行尾的开关在");
                if (rowSwitch != nullptr) {
                    rowSwitch->click();
                }
                // 还原锚点那一发排在 0 毫秒的定时器里（见 rebuildRows() 末尾那段）。
                QEventLoop settle;
                QTimer::singleShot(50, &settle, &QEventLoop::quit);
                settle.exec();
                QApplication::processEvents();

                check(rowsList->count() == 60,
                      "U10M 拨完开关之后那一条还在表里（六十条一条不少）",
                      QStringLiteral("%1 条").arg(rowsList->count()));
                check(bar->value() == beforeValue,
                      "★★ U10M 滚动条**停在原地**（不是被那一发 clear() 送回 0）",
                      QStringLiteral("拨之前 %1 → 拨之后 %2（0 就是用户报的那个毛病）")
                          .arg(beforeValue)
                          .arg(bar->value()));
                check(topIdentity() == topBefore,
                      "★★ U10M 顶上那一条**还是原来那一条**（不是整条列表从头再来一遍）",
                      QStringLiteral("%1 → %2").arg(topBefore, topIdentity()));
                // 改名之后按**身份路径**认得回来 ⇒ 那一条还选着（用户的手还在上面）。
                int stillSelected = -1;
                for (int row = 0; row < rowsList->count(); ++row) {
                    if (InstanceFilesPage::identityPath(rowsList->item(row)
                                                            ->data(InstanceFilesPage::
                                                                       pathRole())
                                                            .toString()) ==
                            InstanceFilesPage::identityPath(target) &&
                        rowsList->item(row)->isSelected()) {
                        stillSelected = row;
                    }
                }
                check(stillSelected >= 0,
                      "★★ U10M 而且**拨过开关的那一条还选着**（盘上还是同一个文件，"
                      "只是名字尾巴上多了 `.disabled`）",
                      QStringLiteral("选中的是第 %1 行").arg(stillSelected));
                // 名字还是原来那个（`.disabled` 不在界面上露出来 —— 它是个记号，
                // 不是文件名的一部分），开关也翻过去了。
                QListWidgetItem *toggled = rowsList->item(targetRow);
                check(toggled != nullptr &&
                          toggled->data(InstanceFilesPage::pathRole()).toString() == renamed,
                      "U10M 那一行的路径换成了盘上那个新名字",
                      toggled != nullptr
                          ? toggled->data(InstanceFilesPage::pathRole()).toString()
                          : QString());
                QObject::disconnect(simConn);
            }

            // ---- U10M-2 从详情页退回来：位置**跨过那一页本身**活下来----
            //
            // 要求：「点进模组详情以后退出来保留刚才的滚动位置」。这一趟和上面那条
            // 不是一回事：实例设置这个对话框是**每次现开的栈上对象**
            // （`MainWindow::openInstanceSettingsNow()`），那一页整套是重建出来的 ——
            // 页面自己记着的那一份位置跟着一起没了。修法是把那一对数字提到页面**外面**
            // 存（`InstanceFilesPage::ScrollPosition`）：离开那一刻量下来
            // （`fileResourceRequested` 那一发），回来时在 `showPage()` **之前**塞回去
            // （`MainWindow::m_instanceReturn` → `setScrollPosition()`）。
            //
            // 这里仿的正是那一趟：**另开一个对话框**（= 另一批 InstanceFilesPage），把
            // 老那一页现量出来的位置塞给新的那一页，再看它列完之后停在哪儿。
            {
                bar->setValue(bar->maximum() / 2);
                QApplication::processEvents();
                const InstanceFilesPage::ScrollPosition carried = many->scrollPosition();
                check(!carried.identity.isEmpty() && bar->value() > 0,
                      "U10M-2 前提：离开那一刻量得出一份位置（最上面那一行是谁）",
                      QStringLiteral("顶上是 %1（value=%2）").arg(carried.identity).arg(bar->value()));

                Dlg returnDialog(instanceDisplayName, instanceDirName, Dlg::Override(), global,
                                 QStringList(), true);
                returnDialog.setInstanceDir(rowsRoot);
                returnDialog.setFileDirs(rowsDirs);
                returnDialog.resize(900, 520);
                returnDialog.show();
                InstanceFilesPage *reopened = returnDialog.filesPage(QStringLiteral("mod"));
                check(reopened != nullptr && reopened->scrollPosition().identity.isEmpty(),
                      "U10M-2 刚开出来那一页还没列过盘 ⇒ 它**自己**那份位置是空的"
                      "（所以下面塞进去的那一份才作数）");
                if (reopened != nullptr) {
                    // 塞进去的时机必须是**列盘之前**（产品那一句就摆在 `showPage()`
                    // 前面）—— 摆到后面会被 `rememberScrollAnchor()` 在第一次
                    // `refresh()` 里现量一份（空表上它是空操作，可位置已经被换成
                    // 新量的那一份）盖掉。
                    reopened->setScrollPosition(carried);
                    showPage(returnDialog, QStringLiteral("mod"));
                    QEventLoop settleBack;
                    QTimer::singleShot(50, &settleBack, &QEventLoop::quit);
                    settleBack.exec();
                    QApplication::processEvents();

                    QListWidget *backList = reopened->list();
                    const auto topOf = [backList]() {
                        for (int row = 0; row < backList->count(); ++row) {
                            QListWidgetItem *item = backList->item(row);
                            if (item != nullptr && backList->visualItemRect(item).bottom() > 0) {
                                return InstanceFilesPage::identityPath(
                                    item->data(InstanceFilesPage::pathRole()).toString());
                            }
                        }
                        return QString();
                    };
                    check(backList->count() == 60 && topOf() == carried.identity &&
                              backList->verticalScrollBar()->value() > 0,
                          "★★ U10M-2 退回来之后**顶上还是离开前那一条**（不是回到顶端）—— "
                          "换了一个页面对象也认得回来，靠的是「那一行是谁」，不是「滚动条"
                          "停在几」",
                          QStringLiteral("顶上 %1（该 %2）")
                              .arg(topOf(), carried.identity));
                }
            }
        }
    }
    return 0;
}
