#include "Support.hpp"
int MwInstanceRenameChecks(const QDir &versionsDir, MainWindow &window) {
    // ------------------------------------------------------------------
    // （第二轮）：要求「重命名进实例设置」。外面那颗「重命名…」按钮删掉
    // 了，那一格搬进了实例设置页（`InstanceSettingsDialog::instanceNameEditName()`，
    // 初值是**目录名**）。
    //
    // （第三轮）：要求「实例设置里面的那个取消和确定删了，修改了的设置
    // 实时写入」。于是这一节的形状又变了一次 —— 从前是"改那一格 → 点确定 → 改名"，
    // 现在是**改名那一格自己带一发**（回车 / 失焦 ⇒ `editingFinished` ⇒
    // `renameRequested` ⇒ MainWindow 那边 applyRename()）。页面上已经**没有「确定」
    // 可点**了，唯一的出口是左上角那颗返回箭头（= reject）。这一节验的就是这条新路：
    // 改完字的那一下磁盘上**当场**就变了，而"关掉这一页"不等于"放弃"。
    //
    // 这一节同时钉住用户给的那条方向 ——「最终不出现独立的窗口」：整件事里**顶窗一个都不
    // 许多**（那一页是盖在内容区上的一层，见 OverlayHost::execPage）。S 段验的是"改完名
    // 要跟着动的状态"，这里验的是"这一下发生在哪儿、用户看不看得见"。
    //
    // NOTE: 这一节和 S 段里那条 `applyRename()` 不是重复：这里走的是**产品那条路**，
    // 而那条路里"改名之后把对话框手上那八条路径整份重算"是产品的一处讲究（写在
    // MainWindow::openInstanceSettings() 里 refreshInstanceDirs 那一段）—— 不重算的
    // 话它们还指着那个已经不在了的老目录，用户接着点「打开文件夹」就会把老目录
    // 重新建出来。这一节顺带把那个顺序也验了（改名之后写的必须是新目录）。
    {
        const QString btnOldId = QStringLiteral("26.3");
        const QString btnNewId = QStringLiteral("26.3-按钮改的");
        // 沙盒里那个实例在 S 段被改过名又改回来了（目录名/稳定 id 都在），这里接着用它。
        rememberLastVersion(btnOldId);

        MainWindow w;
        w.show();
        QApplication::processEvents();
        QComboBox *combo = comboByObjectName(&w, MainWindowUI::installedVersionComboName());
        check(combo != nullptr && combo->currentData().toString() == btnOldId,
              "S2 前提：下拉框停在这个实例上（改名作用于当前选中项）",
              combo != nullptr ? combo->currentData().toString() : QString());

        // 摆出来之前先数一遍顶窗。
        const int windowsBefore = topLevelWindowCount();
        check(windowsBefore >= 1, "S2 前提：主窗口自己是一个顶窗（下面那条才有得比）",
              QString::number(windowsBefore));

        bool sawPage = false;
        QString pageTitle = QStringLiteral("（没读到那一页）");
        QString prefilled;
        // 三样"这一下当场发生了什么"的证据，都在那一发之后**立刻**读（不等后面任何一句）：
        // - 目录改名了没有（`renamedOnTheSpot`）；
        // - 页眉上那行字跟着换了没有；
        // - 那个条上还有没有「确定 / 取消」。
        bool renamedOnTheSpot = false;
        QString titleAfterRename;
        bool nameRevertedOnEmpty = false;
        QString anyConfirmButton;
        // 用户报的那条 bug 直接量在这儿：覆盖层**整页那一层**的滚动区还滚不滚得动。
        // 滚得动 = 整页（含左栏那两条导航列）能被推着走 —— 那正是"滚动的时候左边也跟着
        // 滚了"。修法是把滚动区挪到**每一页自己**身上（见 InstanceSettingsDialog::
        // addPage()），于是这一层再也滚不动（内容全装得下）。量不到 = -1（下面会红）。
        int outerScrollMax = -1;
        QTimer::singleShot(0, [&] {
            OverlayHost *host = currentOverlay();
            if (host == nullptr) {
                recordPrompt(nullptr);  // 没摆出那一页：下面那条断言会红，这里别崩
                return;
            }
            if (auto *title = host->findChild<QLabel *>(OverlayHost::overlayTitleLabelName())) {
                sawPage = true;
                pageTitle = title->text();
            }
            const auto titleNow = [host] {
                auto *label = host->findChild<QLabel *>(OverlayHost::overlayTitleLabelName());
                return label != nullptr ? label->text() : QString();
            };
            auto *pageScroll =
                host->findChild<QScrollArea *>(OverlayHost::overlayPageScrollName());
            if (pageScroll != nullptr && pageScroll->verticalScrollBar() != nullptr) {
                outerScrollMax = pageScroll->verticalScrollBar()->maximum();
            }
            // 要求要删掉的那两颗按钮：整页上**一颗都不该有**（文案扫，不按 objectName ——
            // 它们连名字常量都删了，扫文案才是"用户看得见的那两颗没了"）。
            for (QPushButton *button : host->findChildren<QPushButton *>()) {
                // An owned native wizard is a separate window, not a page footer.
                if (button->window() != host->window()) continue;
                bool insideSelectionBar = false;
                for (QObject *o = button; o != nullptr; o = o->parent()) {
                    if (o->objectName() == InstanceFilesPage::selectionBarName()) {
                        insideSelectionBar = true;
                        break;
                    }
                }
                if (insideSelectionBar) {
                    continue;
                }
                const QString text = button->text().remove(QLatin1Char('&'));
                if (text == QStringLiteral("确定") || text == QStringLiteral("取消")) {
                    anyConfirmButton = text;
                }
            }
            // 「实例名」那一格的初值必须是**目录名**：拿显示名当初值的话，用户点进设置页
            // 什么都不改、离开那一格（失焦就会发 editingFinished），实例就被改成一个奇怪的
            // 名字（那个坑写在 InstanceSettingsDialog.hpp 里 instanceId 那一段）。
            auto *edit =
                host->findChild<QLineEdit *>(InstanceSettingsDialog::instanceNameEditName());
            if (edit != nullptr) {
                prefilled = edit->text();
                edit->setText(btnNewId);
                // 敲一下回车 / 点到别处 —— 产品里那一格只认这一发（`editingFinished`）。
                // `setText()` 是程序化改值，它**不发**这一发；句柄这边只能自己补上。
                QMetaObject::invokeMethod(edit, "editingFinished");
                renamedOnTheSpot = QDir(versionsDir.filePath(btnNewId)).exists() &&
                                   !QDir(versionsDir.filePath(btnOldId)).exists();
                titleAfterRename = titleNow();

                // 负例：把那一格清空再提交 —— 空名字不是"改成没有名字"，等于没改，
                // 而且那一格要**自己退回**当前名字（不能留在"空着"的样子上：用户接着动
                // 任何一个开关，写盘那边用的还是 targetId，界面上却写着空）。
                edit->setText(QString());
                QMetaObject::invokeMethod(edit, "editingFinished");
                nameRevertedOnEmpty = edit->text() == btnNewId;
            }
            // 收页：这一页没有「确定」了，返回箭头就是唯一出口（= reject，见 execPage）。
            if (auto *back =
                    host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
                back->click();
                return;
            }
            host->closeOverlay();  // 兜底：至少别让测试卡在这一层上
        });
        // 这一句会**卡在嵌套事件循环里**，直到上面那一发把返回箭头点掉（见 OverlayHost::execPage）。
        QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                  Q_ARG(QString, btnOldId));
        QApplication::processEvents();

        check(sawPage && pageTitle.contains(btnOldId),
              "S2 实例设置页开出来了（页眉上写的就是这个实例）", pageTitle);
        check(anyConfirmButton.isEmpty(),
              "★ S2 这一页上**没有「确定」也没有「取消」**（用户原话「那个取消和确定删了」）——"
              "整页唯一的出口是左上角那颗返回箭头",
              anyConfirmButton.isEmpty() ? QStringLiteral("（一颗都没有）") : anyConfirmButton);
        check(topLevelWindowCount() == windowsBefore,
              "S2 整件事里顶窗一个都没多（用户要的「不出现独立的窗口」）",
              QString("%1 -> %2").arg(windowsBefore).arg(topLevelWindowCount()));
        check(outerScrollMax == 0,
              "★ S2 覆盖层**整页那一层**的滚动区滚不动了（范围 0）—— 从前整页（连带左栏"
              "那两条导航列）套在这一层里，它就是用户报的「滚动的时候左边也跟着滚了」的"
              "来路；现在滚动区落在每一页自己身上，这一层再推也推不动",
              QString::number(outerScrollMax));
        check(prefilled == btnOldId,
              "★ S2 「实例名」那一格的初值是**目录名**（不是显示名 —— 那一格什么都不改就"
              "离开这一格，实例不该被改名）",
              prefilled);

        // 这一节的重点：磁盘上是**那一格提交的那一下**就变的，中间一个按钮都没点。
        check(renamedOnTheSpot,
              "★ S2 那一格一发 editingFinished（= 用户敲回车 / 点到别处），磁盘上**当场**就"
              "改名了 —— 没有任何「确定」参与这件事（实时写盘）");
        check(!QDir(versionsDir.filePath(btnOldId)).exists() &&
                  QFile::exists(versionsDir.filePath(btnNewId + QLatin1Char('/') + btnNewId +
                                                     QStringLiteral(".jar"))),
              "★ S2 而且老目录没了、新目录里那份 jar 还在（改的是那一半，不是复制一份）");
        // NOTE: 比的是"**以新名字结尾**"而不是"不含老名字"：老名字 `26.3` 是新名字
        // `26.3-按钮改的` 的前缀，那一条恒真 —— 恒真的断言等于没断言。
        check(titleAfterRename.endsWith(btnNewId) && titleAfterRename != pageTitle,
              "★ S2 页眉上那行字跟着换成新名字了（不换的话最高处还写着老名字、底下那一格"
              "却是新的 —— OverlayHost::setPageTitle 就是为这一下加的）",
              QStringLiteral("%1 -> %2").arg(pageTitle, titleAfterRename));
        check(nameRevertedOnEmpty,
              "★ S2 负例：把那一格清空再提交（回车）—— 空名字不是「改成没有名字」，等于没改，"
              "那一格自己退回当前名字（界面上不能留着一个空字符串）");
        check(QFile::exists(versionsDir.filePath(btnNewId + QLatin1Char('/') + btnNewId +
                                                  QStringLiteral(".jar"))),
              "S2 负例：清空那一下磁盘上什么都没动");
        check(combo != nullptr && combo->currentData().toString() == btnNewId,
              "S2 下拉框跟着挪到新名字上",
              combo != nullptr ? combo->currentData().toString() : QString());
        // 返回箭头是"关掉"，不是"撤销"：上面那些改动**留在盘上**（那个负例里清空的那一下
        // 没有写，是因为它本来就等于没改，不是因为收页时回滚了）。
        check(currentPageOverlay() == nullptr,
              "S2 返回箭头把这一页收掉了（收掉 ≠ 放弃 —— 该写的早就写完了）");

        // ---- S2b 别的设置也实时写盘；而且**值没变一个字都不写** ----
        //
        // 改名那一格走的是自己那条路（renameRequested），别的每一项走的是
        // `settingsEdited()` → `MainWindow::applyInstanceSettings()`。这一节验的就是后
        // 一条：在页面上改一格 ⇒ 磁盘上**当场**变了，中间一个按钮都不点。
        //
        // 去重那一半同样重要：`settingsEdited()` 是按**值签名**发的（见 dialog 那边
        // notifyEdited()），而 applyInstanceSettings() 收到之后会把**整份**设置重写一遍 ——
        // 不判"值到底变没变"的话，用户在这一页上随便碰一下（比如点那颗"打开文件夹"）
        // 都会重写一次 wtfml.meta。
        {
            const QString metaPath = versionsDir.filePath(btnNewId + QStringLiteral("/wtfml.meta"));
            const QByteArray metaBefore = readBytesAt(metaPath);
            int edited = 0;
            int editedAtOpen = -1;
            int editedAfterClick = -1;
            bool remindWasChecked = false;
            bool fieldWrittenOnTheSpot = false;
            int editedAfterNoOp = -1;
            QByteArray metaAfterNoOp;
            QTimer::singleShot(0, [&] {
                OverlayHost *host = currentOverlay();
                if (host == nullptr) {
                    recordPrompt(nullptr);  // 没摆出那一页：下面那条断言会红，这里别崩
                    return;
                }
                auto *page = host->findChild<InstanceSettingsDialog *>();
                if (page == nullptr) {
                    return;
                }
                QObject::connect(page, &InstanceSettingsDialog::settingsEdited, page,
                                 [&edited] { ++edited; });
                editedAtOpen = edited;
                // 「启动前提醒 Java 版本对不上」那一格：默认勾着（= 提醒），点一下变成不提醒。
                // 挑它当扳机是因为它直接落在 wtfml.meta 的 skipJavaVersionWarning 上 ——
                // 不用先过"跟随全局 / 自定义"那一档，是这一页上最干净的一颗。
                if (auto *remind = page->findChild<QCheckBox *>(
                        InstanceSettingsDialog::warnJavaMismatchCheckName())) {
                    remindWasChecked = remind->isChecked();
                    remind->click();
                    editedAfterClick = edited;
                    const QJsonObject meta =
                        QJsonDocument::fromJson(readBytesAt(metaPath)).object();
                    fieldWrittenOnTheSpot =
                        meta.value(VersionManager::skipJavaVersionWarningField()).toBool();
                }
                // 去重：对一格**一个字节都没改**的输入框补一发 editingFinished。那一发照常
                // 走到 notifyEdited()，但值签名和上一发一模一样 ⇒ 一条线都不该再响。
                editedAfterNoOp = edited;
                if (auto *noop =
                        page->findChild<QLineEdit *>(InstanceSettingsDialog::javaPathEditName())) {
                    QMetaObject::invokeMethod(noop, "editingFinished");
                }
                metaAfterNoOp = readBytesAt(metaPath);
                // 收页（返回箭头，见上）。
                if (auto *back =
                        host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
                    back->click();
                    return;
                }
                host->closeOverlay();  // 兜底：至少别让测试卡在这一层上
            });
            QMetaObject::invokeMethod(&w, "openInstanceSettings", Qt::DirectConnection,
                                      Q_ARG(QString, btnNewId));
            QApplication::processEvents();

            check(remindWasChecked, "S2b 前提：那一格本来勾着（= 提醒，缺省的那一侧）");
            check(editedAtOpen == 0,
                  "S2b 前提：开这一页本身一发都不发（值没变就不会平白写一次盘）",
                  QString::number(editedAtOpen));
            check(editedAfterClick - editedAtOpen == 1,
                  "★ S2b 在页面上改一格 ⇒ **恰好**一发 settingsEdited()（没有任何按钮参与）",
                  QStringLiteral("%1 -> %2").arg(editedAtOpen).arg(editedAfterClick));
            check(fieldWrittenOnTheSpot,
                  "★ S2b 而且**那一瞬间** wtfml.meta 里就有 skipJavaVersionWarning 了（关掉提醒"
                  "之后它写着 true）—— 不是「攒到收页时统一处理」的",
                  QString::fromUtf8(readBytesAt(metaPath)));
            check(editedAfterNoOp == editedAfterClick,
                  "★ S2b 负例：对一格**没改过**的输入框再发一发 editingFinished —— 一条线都不响"
                  "（值签名没变 ⇒ 不发 settingsEdited ⇒ 不白写一遍盘）",
                  QStringLiteral("%1 -> %2").arg(editedAfterClick).arg(editedAfterNoOp));
            check(metaAfterNoOp == readBytesAt(metaPath),
                  "S2b 负例：那一发之后文件一个字节都没被重写");
            check(metaBefore != readBytesAt(metaPath),
                  "S2b 这一节确实动过盘（上面那两条「没动」才有意义）");
        }

        // 改回去。后面那几节和收尾清理都指望沙盒恢复原样（和 S 段同一个收法）。
        bool back = false;
        QMetaObject::invokeMethod(&w, "applyRename", Qt::DirectConnection,
                                  Q_RETURN_ARG(bool, back), Q_ARG(QString, btnNewId),
                                  Q_ARG(QString, btnOldId));
        check(back, "S2 还能再改回原来的名字（这一节也要把沙盒还原）");
    }
    return 0;
}


