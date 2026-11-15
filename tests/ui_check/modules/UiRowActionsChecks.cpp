#include "Support.hpp"
#include "UiInstanceFilesTestHelpers.hpp"

// U10R：第四批 —— 行尾次序 / 详情悬停才露 / 右键进详情 / 两颗开关的显隐 / 拨开关不掉出选择。
//
// 要求：「详情和启用/禁用换个顺序，且详情只在悬停时显现，右键点击时也可以进入详情页，
// 此时保留上一页的选择；修改禁用/启用状态时改成没有禁用的时候不显示启用，但是没有启用
// （全是禁用）的时候保持显示禁用，此外修复点击开关的时候会取消选中的问题」。
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段的对话框夹具。分出来以后
// 要的三样（两个名字 + 全局那份值）都是**显式形参**，生命周期见 docs/dev/TESTING.md
// 「夹具生命周期（ui_check 的大模块）」。
int UiRowActionsChecks(const QString &instanceDisplayName, const QString &instanceDirName,
                       const InstanceSettingsDialog::GlobalValues &global) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    // ---- U10R 第四批：行尾次序 / 详情悬停才露 / 右键进详情 / 两颗开关的显隐 /
    // 拨开关不掉出选择----
    //
    // 要求：「详情和启用/禁用换个顺序，且详情只在悬停时显现，右键点击时也可以进入
    // 详情页，此时保留上一页的选择；修改禁用/启用状态时改成没有禁用的时候不显示启用，
    // 但是没有启用（全是禁用）的时候保持显示禁用，此外修复点击开关的时候会取消选中的
    // 问题」。
    {
        QTemporaryDir rScratch;
        const QString rRoot = rScratch.path();
        const auto mkfileR = [](const QString &path) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QByteArray(1024, 'r'));
            }
        };
        // 三条：两条启用着、一条禁用着（和 U10Q 那一节的形状一样 —— 这一节要的正是
        // "两种状态都摆得出来"）。
        mkfileR(rRoot + QStringLiteral("/mods/甲.jar"));
        mkfileR(rRoot + QStringLiteral("/mods/乙.jar"));
        mkfileR(rRoot + QStringLiteral("/mods/丙.jar") + InstanceFilesPage::disabledSuffix());
        const QString modA = rRoot + QStringLiteral("/mods/甲.jar");
        const QString modB = rRoot + QStringLiteral("/mods/乙.jar");
        const QString modC =
            rRoot + QStringLiteral("/mods/丙.jar") + InstanceFilesPage::disabledSuffix();
        QHash<QString, QString> rDirs;
        rDirs.insert(QStringLiteral("mod"), rRoot + QStringLiteral("/mods"));

        Dlg fourth(instanceDisplayName, instanceDirName, Dlg::Override(), global, QStringList(),
                   true);
        fourth.setInstanceDir(rRoot);
        fourth.setFileDirs(rDirs);
        fourth.showPage(QStringLiteral("mod"));
        fourth.show();

        InstanceFilesPage *page = fourth.filesPage(QStringLiteral("mod"));
        check(page != nullptr && page->list() != nullptr && page->batchMode(),
              "U10R 前提：模组那一页是**开了批量**的那一页");
        if (page != nullptr && page->list() != nullptr) {
            QListWidget *list = page->list();
            QWidget *bar = page->selectionBar();
            const auto rowOf = [list](const QString &path) {
                for (int row = 0; row < list->count(); ++row) {
                    QListWidgetItem *item = list->item(row);
                    if (item != nullptr &&
                        item->data(InstanceFilesPage::pathRole()).toString() == path) {
                        return row;
                    }
                }
                return -1;
            };
            const auto selectOnly = [list, &rowOf](const QString &path) {
                list->clearSelection();
                const int row = rowOf(path);
                if (row >= 0 && list->item(row) != nullptr) {
                    list->item(row)->setSelected(true);
                }
            };
            const auto barButton = [page](InstanceFilesPage::SelectionAction action) {
                return page->selectionButtonAt(static_cast<int>(action));
            };

            const int rowA = rowOf(modA);
            check(rowA >= 0 && rowOf(modB) >= 0 && rowOf(modC) >= 0 && list->count() == 3,
                  "U10R 前提：三条都列出来了（两条启用、一条禁用）",
                  QString::number(list->count()));

            // ---- ① 行尾三格的次序：`[详情][开关][删除]` ----
            // 行是代理画的：三格的位置由 rowGeometry() 给（paint 和点击命中认同一份）。
            const auto detail = uiDetailAt(page, rowA);
            const auto toggle = uiToggleAt(page, rowA);
            const auto remove = uiDeleteAt(page, rowA);
            check(detail != nullptr && toggle != nullptr && remove != nullptr &&
                      detail->geometry().right() < toggle->geometry().left() &&
                      toggle->geometry().right() < remove->geometry().left(),
                  "★★ U10R 行尾三格是 **[详情][开关][删除]**（用户原话「详情和启用/禁用"
                  "换个顺序」）",
                  QStringLiteral("详情 x=%1 开关 x=%2 删除 x=%3")
                      .arg(detail != nullptr ? detail->geometry().x() : -1)
                      .arg(toggle != nullptr ? toggle->geometry().x() : -1)
                      .arg(remove != nullptr ? remove->geometry().x() : -1));

            // ---- ② 「详情」只在鼠标悬停这一行时露着 ----
            page->hoverRow(-1);
            check(detail != nullptr && !detail->isShown(),
                  "★★ U10R 一进来那一格是**藏着的**（用户原话「详情只在悬停时显现」）");
            const int toggleXBefore = toggle != nullptr ? toggle->geometry().x() : -1;
            check(detail != nullptr && toggleXBefore >= 0 &&
                      detail->geometry().right() < toggleXBefore,
                  "★★ U10R 而且它藏起来时**占位留着**（不画，但那一格的位置还在）—— 不留的话"
                  "鼠标一进一出整行都要重排，而这一格正好夹在名字和开关中间，抖起来最扎眼");
            // 合成一发 MouseMove 给视口（光标压到这一行上，和真鼠标同一条路）。
            page->hoverRow(rowA);
            check(detail != nullptr && detail->isShown(),
                  "★★ U10R 光标移到这一行上 ⇒ 那一格**露出来**（悬停才露的那一半）");
            check(toggle != nullptr && toggleXBefore >= 0 && toggle->geometry().x() == toggleXBefore,
                  "★★ U10R 露出这一格**没有把别的挤走**（开关的 x 一动不动 —— 这就是"
                  "「占位留着」在屏幕上的意思）",
                  QStringLiteral("露出前 x=%1 / 露出后 x=%2")
                      .arg(toggleXBefore)
                      .arg(toggle != nullptr ? toggle->geometry().x() : -1));
            page->hoverRow(-1);
            check(detail != nullptr && !detail->isShown(),
                  "★★ U10R 光标移出列表 ⇒ 又**收回去**（是「悬停才露」，不是「露过一次"
                  "就一直留着」）");

            // ---- ③ 右键点一行 = 开这一条的详情页，而且那一发**被吃掉** ----
            //
            // 「认得出资源」的那一条才发（判据 = `m_resourceTips`，和那颗 ⓘ 的可用性
            // 逐字相同）。
            page->setResourceForPath(modA, QStringLiteral("甲的资源"));
            QStringList rightClicked;
            const QMetaObject::Connection rcConn =
                QObject::connect(page, &InstanceFilesPage::detailRequested, page,
                                 [&rightClicked](const QString &path) {
                                     rightClicked.append(path);
                                 });
            // 先选着**另一条**：右键之后这一份选择必须一条都不动（用户点名「此时保留
            // 上一页的选择」）。
            selectOnly(modB);
            check(list->selectedItems().size() == 1,
                  "U10R 前提：右键之前先选着「乙」那一条");
            QWidget *viewport = list->viewport();
            const QPoint pointA = page->rowGeometry(rowA).name.center();
            const QPointF localAt(pointA);
            const QPointF globalAt = viewport->mapToGlobal(pointA);
            QMouseEvent rightPress(QEvent::MouseButtonPress, localAt, globalAt,
                                   Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            // `ignore()` 那一句**不能省**：`QEvent` 生出来时 accepted 位**默认就是真**，
            // 直接断言 `isAccepted()` 是假绿。先把它摆成"还没人接受"，才问得出"产品到底
            // accept 了没有"。而这一位正是真机上"要不要往上冒"的开关：`QApplication::notify`
            // 那个循环判的是 `res && isAccepted()`，不 accept 的话这一发会继续冒到
            // viewport 上、视图顺手就把选中态改掉（见 .hpp 里 eventFilter 那段）。
            rightPress.ignore();
            QApplication::sendEvent(viewport, &rightPress);
            check(rightClicked == QStringList{modA},
                  "★★ U10R 右键点一行 ⇒ 发的是**这一条**的 detailRequested（用户原话"
                  "「右键点击时也可以进入详情页」）",
                  rightClicked.join(QStringLiteral(" | ")));
            check(rightPress.isAccepted(),
                  "★★ U10R 那一发被**吃掉**了（产品把 accepted 位翻回真）—— 不 accept 的"
                  "话它会冒到视图上，视图于是自己改选中态，「保留上一页的选择」就不成立");
            check(list->selectedItems().size() == 1 &&
                      list->selectedItems().first()->data(InstanceFilesPage::pathRole())
                              .toString() == modB,
                  "★★ U10R 右键之后**选中的还是「乙」那一条**（用户点名「此时保留上一页"
                  "的选择」）",
                  QStringLiteral("选中 %1 条").arg(list->selectedItems().size()));
            // 认不出来的那一条：右键**一个字节都不发**（灰的那颗和这一发是同一条判据）。
            rightClicked.clear();
            const int rowB = rowOf(modB);
            const QPoint pointB = rowB < 0 ? QPoint(-1, -1) : page->rowGeometry(rowB).name.center();
            QMouseEvent rightPressUnknown(QEvent::MouseButtonPress, QPointF(pointB),
                                          QPointF(viewport->mapToGlobal(pointB)),
                                          Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            rightPressUnknown.ignore();
            QApplication::sendEvent(viewport, &rightPressUnknown);
            check(rightClicked.isEmpty(),
                  "★★ U10R 右键点那条**认不出来的** ⇒ 什么都不发（正确界面就是「按下去"
                  "没反应」）",
                  rightClicked.join(QStringLiteral(" | ")));
            QObject::disconnect(rcConn);

            // ---- ④ 启用和禁用直接可见，分别按选中项的状态置灰 ----
            const auto enableButton = [&barButton] {
                return barButton(InstanceFilesPage::SelectionAction::Enable);
            };
            const auto disableButton = [&barButton] {
                return barButton(InstanceFilesPage::SelectionAction::Disable);
            };
            const auto stateSays = [&](const QString &name, QPushButton *button) {
                return QStringLiteral("%1 在面板里=%2 可用于本次=%3")
                    .arg(name)
                    .arg(button != nullptr && button->isVisibleTo(bar) ? 1 : 0)
                    .arg(button != nullptr && button->isEnabled() ? 1 : 0);
            };
            const auto bothButtonsSaid = [&] {
                return stateSays(QStringLiteral("启用"), enableButton()) +
                       QStringLiteral(" / ") +
                       stateSays(QStringLiteral("禁用"), disableButton());
            };
            const auto visibleInPanel = [&] {
                return enableButton() != nullptr && disableButton() != nullptr &&
                       enableButton()->isVisibleTo(bar) && disableButton()->isVisibleTo(bar);
            };
            selectOnly(modA);  // 只选那条**启用着**的
            check(bar != nullptr && bar->isVisible() && visibleInPanel() &&
                      enableButton() != nullptr && !enableButton()->isEnabled() &&
                      disableButton() != nullptr && disableButton()->isEnabled(),
                  "★★ U10R 选中的里面**一条禁用着的都没有** ⇒ 「启用」灰、"
                  "「禁用」亮（两颗按钮直接显示）",
                  bothButtonsSaid());
            selectOnly(modC);  // 只选那条**禁用着**的（全是禁用）
            check(visibleInPanel() && enableButton() != nullptr &&
                      enableButton()->isEnabled() && disableButton() != nullptr &&
                      !disableButton()->isEnabled(),
                  "★★ U10R 选中的**全是禁用着的** ⇒ 「禁用」灰、"
                  "「启用」亮（同一套规则反过来）",
                  bothButtonsSaid());
            selectOnly(modB);
            if (rowOf(modC) >= 0) {
                list->item(rowOf(modC))->setSelected(true);  // 一条启用 + 一条禁用
            }
            check(visibleInPanel() && enableButton() != nullptr &&
                      enableButton()->isEnabled() && disableButton() != nullptr &&
                      disableButton()->isEnabled(),
                  "U10R 选中里两种都有 ⇒ 启用与禁用都可用（这一档谁都不该灰）",
                  bothButtonsSaid());
            list->clearSelection();

            // ---- ⑤ 点一下开关（改名）之后，这一行**还选着** ----
            //
            // 「点开关」在真机上是这条路：行尾那颗 `ToggleSwitch` 按下去 → 这一页的
            // `requestToggle()`（先记一笔"这是**我刚发出去的开关**"）→ `toggleRequested`
            // → MainWindow 把 `X.jar` 改成 `X.jar.disabled`，再回头喊一声
            // refreshFilePages()（见 toggleInstanceFile）。
            //
            // 这一档必须**走这一页**，不能直接 `QFile::rename` + `refresh()`：产品只把
            // "这一页刚发出去的开关"解释成改名（`m_pendingIdentityMigration`）—— 否则
            // 一条**被删掉**的文件也会把同名的 `.disabled` 那一行重新选上（见 ⑥ 那一档）。
            const auto simulateDiskToggle = [](const QString &path) {
                const QString suffix = InstanceFilesPage::disabledSuffix();
                const QString target = path.endsWith(suffix)
                                           ? path.left(path.size() - suffix.size())
                                           : path + suffix;
                return QFile::rename(path, target);
            };
            const QMetaObject::Connection simConn = QObject::connect(
                page, &InstanceFilesPage::toggleRequested, page,
                [page, &simulateDiskToggle](const QString &path) {
                    check(simulateDiskToggle(path),
                          "U10R 前提：盘上改名必须成功（= 点一下开关）", path);
                    page->refresh();
                });
            selectOnly(modA);
            check(list->selectedItems().size() == 1, "U10R 前提：先选中「甲」那一条");
            const QString modARenamed = modA + InstanceFilesPage::disabledSuffix();
            const auto rowSwitch = uiToggleAt(page, rowOf(modA));
            check(rowSwitch != nullptr, "U10R 前提：行尾那颗开关在（按得下去才谈得上这条路）");
            if (rowSwitch != nullptr) {
                rowSwitch->click();
            }
            check(QFile::exists(modARenamed) && !QFile::exists(modA),
                  "U10R 前提：点完开关盘上真改成了「甲.jar.disabled」",
                  QFile::exists(modARenamed) ? QStringLiteral("在") : QStringLiteral("没有"));
            const int renamedRow = rowOf(modARenamed);
            check(renamedRow >= 0 && list->selectedItems().size() == 1 &&
                      list->item(renamedRow) != nullptr && list->item(renamedRow)->isSelected(),
                  "★★ U10R 改名（= 点一下开关）之后那一行**还在选中里**（账按 "
                  "`identityPath()` 对：盘上还是同一个文件，用户手里选的就是「这一条」）",
                  QStringLiteral("选中 %1 条").arg(list->selectedItems().size()));
            check(bar != nullptr && bar->isVisible() && page->selectionCountLabel() != nullptr &&
                      page->selectionCountLabel()->text() ==
                          InstanceFilesPage::selectionCountText(1),
                  "★★ U10R 那条操作条**也没有跟着消失**（唯一选中的一条掉出选择时，"
                  "整条就一起收起来了）",
                  page->selectionCountLabel() != nullptr ? page->selectionCountLabel()->text()
                                                         : QString());
            // 而账上记的是**盘上真那个名字**（发给 MainWindow 的就是它）—— 借「删除」
            // 那一颗读一眼那份账（它恒可用，而且只发信号、一个字节都不动盘）。
            QStringList account;
            const QMetaObject::Connection accConn =
                QObject::connect(page, &InstanceFilesPage::batchDeleteRequested, page,
                                 [&account](const QStringList &paths) { account = paths; });
            if (barButton(InstanceFilesPage::SelectionAction::Delete) != nullptr) {
                barButton(InstanceFilesPage::SelectionAction::Delete)->click();
            }
            QObject::disconnect(accConn);
            check(account == QStringList{modARenamed},
                  "★★ U10R 账上记的是**改过名之后那个名字**（`X.jar.disabled`）—— 报出去的"
                  "路径必须是盘上真那一个",
                  account.join(QStringLiteral(" | ")));
            // 再反过来（启用）：同一条规矩的另一面，同样走**真那条路**。
            const auto rowSwitchBack = uiToggleAt(page, rowOf(modARenamed));
            check(rowSwitchBack != nullptr, "U10R 前提：改名之后那条的行尾开关也在");
            if (rowSwitchBack != nullptr) {
                rowSwitchBack->click();
            }
            check(QFile::exists(modA) && !QFile::exists(modARenamed),
                  "U10R 前提：再点一下开关，盘上改回「甲.jar」",
                  QFile::exists(modA) ? QStringLiteral("在") : QStringLiteral("没有"));
            check(rowOf(modA) >= 0 && list->selectedItems().size() == 1 &&
                      list->item(rowOf(modA)) != nullptr &&
                      list->item(rowOf(modA))->isSelected(),
                  "★★ U10R 改回来（启用）也一样：那一行照旧选着");
            QObject::disconnect(simConn);

            // ---- ⑥ 「同名的一对」不许被身份一起选中----
            //
            // `X.jar` 和 `X.jar.disabled` **同时在场**时两行的身份一样（`identityPath()`
            // 把后缀抹掉算）。还选择那一趟要是只按身份比，用户只点了一条、重列之后两条
            // 都选上 —— 紧接着一次「删除选中项」就把**用户从来没选过**的那一份也删了
            // （不可逆）。这一档钉的是"重列之后选中集只许不变或变小，不许自己变大"。
            const QString twinOn = rRoot + QStringLiteral("/mods/戊.jar");
            const QString twinOff = twinOn + InstanceFilesPage::disabledSuffix();
            mkfileR(twinOn);
            mkfileR(twinOff);
            page->refresh();
            check(rowOf(twinOn) >= 0 && rowOf(twinOff) >= 0,
                  "U10R 前提：同名的一对（`戊.jar` 与 `戊.jar.disabled`）两条都列出来了",
                  QString::number(list->count()));
            selectOnly(twinOn);
            check(list->selectedItems().size() == 1,
                  "U10R 前提：只点了**启用着的那一条**");
            // 再列一遍 —— 搜索框敲一个字、任一批量操作跑完之后都会走到这里。
            page->refresh();
            check(list->selectedItems().size() == 1 &&
                      list->item(rowOf(twinOn)) != nullptr &&
                      list->item(rowOf(twinOn))->isSelected() &&
                      list->item(rowOf(twinOff)) != nullptr &&
                      !list->item(rowOf(twinOff))->isSelected(),
                  "★★ U10R 同名的一对同时在场时，重列**不许把用户没点过的那一条也选上**"
                  "（身份抹掉 `.disabled` 算，只按身份还选择会把选中集悄悄放大 ⇒ 随后的"
                  "「删除选中项」多删一份）",
                  QStringLiteral("选中 %1 条").arg(list->selectedItems().size()));
        }
    }
    return 0;
}
