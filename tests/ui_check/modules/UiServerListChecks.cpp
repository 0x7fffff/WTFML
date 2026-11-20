#include "Support.hpp"

#include "UiInstanceFilesTestHelpers.hpp"

// U10g 服务器列表：三种状态
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段共用的 `dialog`。分出来以后
// `dialog` 按引用传，其余夹具按显式形参传；生命周期见 docs/dev/TESTING.md
// 「夹具生命周期（ui_check 的大模块）」。
int UiServerListChecks(InstanceSettingsDialog &dialog) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    const auto itemText = [](InstanceFilesPage *page, int row) { return uiItemText(page, row); };
    const auto itemPath = [](InstanceFilesPage *page, int row) { return uiItemPath(page, row); };
    const auto itemFlag = [](InstanceFilesPage *page, int row, int role) { return uiItemFlag(page, row, role); };
    // ---- U10g 服务器列表：三种状态 ----
    {
        showPage(dialog, QStringLiteral("server"));
        auto *tree = dialog.serverTree();
        auto *notice = dialog.serverNotice();
        check(tree != nullptr && notice != nullptr, "U10g 服务器那一页的表格和那句话都在");

        dialog.setServerList({qMakePair(QStringLiteral("我的服"),
                                        QStringLiteral("mc.example.com")),
                              qMakePair(QString(), QStringLiteral("1.2.3.4:25565"))},
                             QString());
        // NOTE: 地址那一格默认显示原文，开关打开后才是遮过的（见 U10L-8）。
        check(!tree->isHidden() && tree->topLevelItemCount() == 2 &&
                  tree->topLevelItem(0)->text(0) == QStringLiteral("我的服") &&
                  tree->topLevelItem(0)->text(1) == QStringLiteral("mc.example.com"),
              "U10g 读到几条就摆几行，名字和地址各占一列");
        check(tree->topLevelItem(1)->text(0) == Dlg::unnamedMark(),
              "★ U10g 名字那一格是空串时顶上一个记号（不是留一栏空白）",
              tree->topLevelItem(1)->text(0));

        dialog.setServerList({}, QString());
        check(tree->isHidden() && !notice->isHidden() &&
                  notice->text() == Dlg::serverEmptyText(),
              "★ U10g 读到了但是一条都没有 ⇒ 「还没加过服务器」", notice->text());

        dialog.setServerList({}, QStringLiteral("servers.dat 读不动：它不是 NBT。"));
        check(tree->isHidden() && notice->text() ==
                                      QStringLiteral("servers.dat 读不动：它不是 NBT。"),
              "★ U10g 读不动时说的是**为什么**（和「还没加过服务器」是两句不同的话 —— "
              "说成一句用户会照着错的那句去修）",
              notice->text());
    }
    return 0;
}
