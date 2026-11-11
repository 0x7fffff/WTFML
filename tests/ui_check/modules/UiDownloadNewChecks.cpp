#include "Support.hpp"

// U10M：顶上那颗「下载新 X」—— 哪几页有、按下去报的是哪一栏。
//
// 要求：「在实例设置里模组 资源包 数据包 光影包里添加下载新光影，按了以后导向下载页对应
// 类别」。这一节验的是**这一层能验的那一半**：哪几页有这颗按钮、上面的字是什么、按下去
// 报的是哪个**分类键**（"切选项卡、挑那一行"在 MainWindow，端到端那一半在 mw_check 的 T20.12）。
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段的对话框夹具（`dialog` 的当前页
// 状态要沿用它），所以那一个按引用传进来；另外两样按显式形参传，生命周期见
// docs/dev/TESTING.md「夹具生命周期（ui_check 的大模块）」。
int UiDownloadNewChecks(InstanceSettingsDialog &dialog, const QString &instanceDisplayName,
                        const QString &instanceDirName,
                        const InstanceSettingsDialog::GlobalValues &global) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    // ---- U10M 顶上那颗「下载新 X」：哪几页有、按下去报的是哪一栏----
    //
    // 要求：「在实例设置里模组 资源包 数据包 光影包里添加下载新光影，按了以后导向
    // 下载页对应类别」。这一节验的是**这一层能验的那一半**：哪几页有这颗按钮、上面的字
    // 是什么、按下去报的是哪个**分类键**（"切选项卡、挑那一行"在 MainWindow，端到端那
    // 一半在 mw_check 的 T20.12）。
    {
        // ① 纯函数：哪几页有（就是下载页里真有对应一栏的那四页）。
        const QStringList withDownload = {
            QStringLiteral("mod"), QStringLiteral("resourcepack"), QStringLiteral("datapack"),
            QStringLiteral("shader")};
        QStringList wrongPages;
        const QStringList allKeys = Dlg::filePageKeys();
        const QStringList categoryKeys = MainWindowUI::downloadCategoryKeys();
        for (const QString &key : allKeys) {
            const QString text = Dlg::filesDownloadText(key);
            const QString category = Dlg::filesDownloadCategoryKey(key);
            const bool wanted = withDownload.contains(key);
            if (wanted != !text.isEmpty() || wanted != !category.isEmpty()) {
                wrongPages.append(key);
                continue;
            }
            // 报出去的键**必须**是下载页左栏真有的那一栏。写歪一个字母的症状是
            // "按了那颗按钮没反应"（MainWindow 那边认不出这一栏、只说一句 qWarning）
            // —— 而界面本身看不出任何异常。
            if (wanted && !categoryKeys.contains(category)) {
                wrongPages.append(key + QStringLiteral("→") + category);
            }
        }
        check(wrongPages.isEmpty(),
              "★ U10M 只有模组 / 资源包 / 数据包 / 光影那四页有那颗按钮，报出去的是"
              "下载页里真有的那一栏（分类键在 downloadCategoryKeys() 里）",
              wrongPages.isEmpty() ? QStringLiteral("四页都对")
                                   : wrongPages.join(QLatin1Char(' ')));
        // 反过来：没有对应一栏的那几页**一个字都没有**（不是"建了再藏起来"那种有字的）。
        check(Dlg::filesDownloadText(QStringLiteral("schematic")).isEmpty() &&
                  Dlg::filesDownloadText(QStringLiteral("save")).isEmpty() &&
                  Dlg::filesDownloadText(Dlg::configPageKey()).isEmpty() &&
                  Dlg::filesDownloadText(Dlg::serverPageKey()).isEmpty() &&
                  Dlg::filesDownloadCategoryKey(QStringLiteral("schematic")).isEmpty(),
              "★ U10M 原理图 / 存档 / 配置文件 / 服务器列表那四页没有这颗按钮"
              "（原理图在下载页里没有对应的一栏，存档不是「下载」来的）");
        // 「光影包」那一页上的字是「下载新光影」—— 跟着**落地那一栏**的名字走
        // （左栏里这一页仍叫「光影包」，下载页那一栏叫「光影」）。
        check(Dlg::filesDownloadText(QStringLiteral("shader")) == QStringLiteral("下载新光影"),
              "★ U10M 「光影包」那一页上写的是「下载新光影」（用户原话），不是「下载新光影包」",
              Dlg::filesDownloadText(QStringLiteral("shader")));
        // 提示里点名"去哪儿 + 这一页会关掉"：按它是**换个地方**，用户按下之前得知道。
        const QString shaderTip = Dlg::filesDownloadTip(QStringLiteral("shader"));
        check(shaderTip.contains(MainWindowUI::downloadCategoryTexts().at(
                  categoryKeys.indexOf(QStringLiteral("shader")))) &&
                  shaderTip.contains(QStringLiteral("下载")),
              "★ U10M 悬停提示里点名了落到哪一栏（不然用户不知道这一下会离开这一页）",
              shaderTip);

        // ② 界面上：那四页露着、别的页藏着，字和 filesDownloadText() 一致。
        for (const QString &key : allKeys) {
            if (key == Dlg::serverPageKey()) {
                continue;  // 那一页是棵树，不是 InstanceFilesPage。
            }
            showPage(dialog, key);
            InstanceFilesPage *page = dialog.filesPage(key);
            QPushButton *button =
                page != nullptr ? page->downloadButton() : nullptr;
            const QString text = Dlg::filesDownloadText(key);
            if (button == nullptr || page == nullptr) {
                check(false, QStringLiteral("U10M %1 那一页上找不到那颗按钮").arg(key));
                continue;
            }
            check(button->text() == text &&
                      button->isVisibleTo(page) == !text.isEmpty(),
                  QStringLiteral("U10M %1 那一页那颗按钮：字对、露不露也对").arg(key),
                  QStringLiteral("字「%1」/ 露着=%2")
                      .arg(button->text())
                      .arg(button->isVisibleTo(page) ? 1 : 0));
        }

        // ③ 按下去：报的是这一页对应的那一栏，而且**只报一条**（点一下 = 一次）。
        // 四页各点一次，一页一发。
        for (const QString &key : withDownload) {
            showPage(dialog, key);
            InstanceFilesPage *page = dialog.filesPage(key);
            QStringList reported;
            // NOTE: 收完就把这条连接断掉 —— 它捕获的是**这一轮的局部量**，留着的话
            // 下一轮那一次 emit 会往一个已经没了的 QStringList 上写。
            const QMetaObject::Connection conn =
                QObject::connect(&dialog, &Dlg::browseDownloadsRequested, page,
                                 [&reported](const QString &category) {
                                     reported.append(category);
                                 });
            if (page != nullptr && page->downloadButton() != nullptr) {
                page->downloadButton()->click();
            }
            QObject::disconnect(conn);
            check(reported == QStringList{Dlg::filesDownloadCategoryKey(key)},
                  QStringLiteral("★ U10M 点一下「%1」：报的就是这一页对应的那一栏，"
                                 "而且只报一条")
                      .arg(Dlg::filesDownloadText(key)),
                  reported.join(QLatin1Char(' ')));
        }

        // ④ 「用不上」的那一页上它跟着那一排一起收起来（判据写在 .hpp 里：光影包那一页
        // 正在说「这个实例装不了光影」，此时把人送去下载页找光影包是指错路）。
        // 自己起一份：这一档要动"用不用得上"，不该拿上面那份 `dialog` 去翻。
        Dlg plain(instanceDisplayName, instanceDirName, Dlg::Override(), global, QStringList(),
                  true);
        plain.setShaderUsable(false);
        plain.showPage(QStringLiteral("shader"));
        InstanceFilesPage *shaderPage = plain.filesPage(QStringLiteral("shader"));
        check(shaderPage != nullptr && shaderPage->downloadButton() != nullptr &&
                  !shaderPage->downloadButton()->isVisibleTo(shaderPage) &&
                  !shaderPage->refreshButton()->isVisibleTo(shaderPage),
              "★ U10M 「用不上」那一页上这颗按钮跟着那一排一起收起来（和「刷新」"
              "「打开文件夹」同一个待遇）");
        check(Dlg::filesHint(QStringLiteral("mod")).isEmpty() &&
                  Dlg::filesHint(QStringLiteral("resourcepack")).isEmpty() &&
                  Dlg::filesHint(QStringLiteral("datapack")).isEmpty() &&
                  Dlg::filesHint(QStringLiteral("shader")).isEmpty(),
              "★ U10M 批量管理那四页的 filesHint() 一律空串（那四页不再挂底下那行小字）",
              Dlg::filesHint(QStringLiteral("mod")) + QStringLiteral("|") +
                  Dlg::filesHint(QStringLiteral("shader")));
        plain.setShaderUsable(true);
        shaderPage = plain.filesPage(QStringLiteral("shader"));
        check(shaderPage != nullptr && shaderPage->downloadButton() != nullptr &&
                  shaderPage->downloadButton()->isVisibleTo(shaderPage),
              "U10M 那一页回到正常形态之后它又露出来（这一档是可逆的）");
    }
    return 0;
}
