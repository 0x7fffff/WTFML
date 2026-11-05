#include "Support.hpp"

#include "UiInstanceFilesTestHelpers.hpp"

// U10b2 常用目录那一排分成两段（在盘的在外、其余收在「更多目录…」里）
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段共用的 `dialog`（当前页状态
// 要沿用）。分出来以后 `dialog` 按引用传，其余夹具按显式形参传；生命周期见
// docs/TESTING.md「夹具生命周期（ui_check 的大模块）」。
int UiDirectoryButtonsChecks(InstanceSettingsDialog &dialog) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    // ---- U10b2：那一排分成两段（在盘的在外、其余收在「更多目录…」里）----
    //
    // 要求原话：「仅在**目录存在**或**相关模组可识别**时优先展示，其他项
    // 收在'更多目录'」。**判据不在这儿**（哪几路目录在盘上、哪个实例装了 ReplayMod 都
    // 只有 MainWindow 知道，见 MainWindow::instanceCommonFolderKeys()）—— 这一层只认
    // 递进来的那张表，所以这一条量的是"递进去一张只有三路的表之后，五颗分别在哪一段"。
    {
        showPage(dialog, Dlg::overviewCategoryKey());
        const QList<QWidget *> buttons = dialog.shortcutButtons();
        const QStringList keys = Dlg::shortcutPathKeys();
        // ① 默认这一档（没人喊过 setFolderPresentKeys）：五颗全在外面，**没有**
        // 「更多目录…」—— 不摆一颗按下去什么都不变的按钮。
        check(dialog.shortcutMoreButton() == nullptr &&
                  dialog.shortcutHiddenButtons().isEmpty() && !dialog.shortcutMoreExpanded(),
              "★ U10b2 没人说过哪几路「现在有东西」时：五颗全摆在外面、也不摆"
              "「更多目录…」（一个都没收起来 ⇒ 那颗开关没有存在的理由）");

        // ② 只在盘上三路（版本目录 / 游戏目录 / 配置）⇒ 外面三颗 + 第四格那颗开关，其余收着。
        // 收着的是 `setVisible(false)`（**还在表里**、下标一个没挪），不是删控件。
        const QStringList present = {QStringLiteral("dir"), QStringLiteral("game"),
                                     QStringLiteral("config")};
        dialog.setFolderPresentKeys(present);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QApplication::processEvents();
        auto *more = dialog.shortcutMoreButton();
        const QList<QWidget *> hidden = dialog.shortcutHiddenButtons();
        check(more != nullptr && more->text() == Dlg::shortcutMoreText() &&
                  !dialog.shortcutMoreExpanded(),
              "★ U10b2 真有东西的只有三路 ⇒ 第四格是「更多目录…」（收着的时候写的就是这"
              "四个字）");
        check(hidden.size() == keys.size() - present.size(),
              "★ U10b2 另外那几路收在「更多目录」里（不是删掉 —— 它们照旧在表里）",
              QStringLiteral("收着 %1 颗，应 %2 颗")
                  .arg(hidden.size())
                  .arg(keys.size() - present.size()));
        // 该在外面的 / 该收着的，按**位置**（表序）认：外面那三颗是 at(0)/at(2)/at(4)。
        const QList<int> outside = {0, 2, 4};
        bool seenOk = buttons.size() == keys.size();
        for (int i = 0; i < buttons.size(); ++i) {
            seenOk = seenOk &&
                     (buttons.at(i)->isVisibleTo(&dialog) == outside.contains(i));
        }
        check(seenOk,
              "★ U10b2 该在外面的看得见、该收着的真看不见（按 isVisibleTo 数的，不是"
              "「有没有这个控件」）");
        check(more != nullptr && buttons.value(0) != nullptr &&
                  more->y() > buttons.at(0)->y() && more->x() == buttons.at(0)->x(),
              "★ U10b2 那颗开关接着外面那三颗往下排（第四格 —— 不是另起一块、也不是"
              "甩到行尾）");

        // ③ 点一下 → 五颗全露出来（开关自己改叫「收起更多目录」）。
        if (more != nullptr) {
            more->click();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            QApplication::processEvents();
            bool allSeen = true;
            for (QWidget *button : buttons) {
                allSeen = allSeen && button->isVisibleTo(&dialog);
            }
            check(dialog.shortcutMoreExpanded() && more->text() == Dlg::shortcutMoreLessText() &&
                      dialog.shortcutHiddenButtons().isEmpty() && allSeen,
                  "★ U10b2 点一下「更多目录…」⇒ 五颗全露出来，那颗开关改口叫"
                  "「收起更多目录」");
            // 再点一下 → 收回去（这一档必须是**可逆**的，否则用户点开就关不掉了）。
            more->click();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            QApplication::processEvents();
            bool backOk = buttons.size() == keys.size();
            for (int i = 0; i < buttons.size(); ++i) {
                backOk = backOk &&
                         (buttons.at(i)->isVisibleTo(&dialog) == outside.contains(i));
            }
            check(!dialog.shortcutMoreExpanded() && more->text() == Dlg::shortcutMoreText() &&
                      backOk,
                  "★ U10b2 再点一下收回去，原样（「收起更多目录」是可逆的）");
        }

        // ④ 摆回"全都在"：后面那几节（U10c…U12 / U0j）还按五颗全在外面量。
        // NOTE: 这一发会把「更多目录…」那颗**连控件一起删掉**，所以 `more` 从那以后
        // 就是野指针了 —— 上面那几条一个都不能挪到这后面来。
        dialog.setFolderPresentKeys(keys);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QApplication::processEvents();
        check(dialog.shortcutMoreButton() == nullptr &&
                  dialog.shortcutHiddenButtons().isEmpty(),
              "★ U10b2 摆回「全都在」⇒「更多目录…」那一颗整颗消失（不是留着不画）");
    }
    return 0;
}
