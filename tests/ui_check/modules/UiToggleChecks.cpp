#include "Support.hpp"

#include "UiInstanceFilesTestHelpers.hpp"

// U10d 三条开关：能不能启停 / 能不能删 / 只列文件夹
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段共用的 `dialog`（当前页状态
// 要沿用）。分出来以后 `dialog` 按引用传，其余夹具按显式形参传；生命周期见
// docs/dev/TESTING.md「夹具生命周期（ui_check 的大模块）」。
int UiToggleChecks(InstanceSettingsDialog &dialog, const QString &modsDir) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    const auto itemText = [](InstanceFilesPage *page, int row) { return uiItemText(page, row); };
    const auto itemPath = [](InstanceFilesPage *page, int row) { return uiItemPath(page, row); };
    // ---- U10d 三条开关：能不能启停 / 能不能删 / 只列文件夹 ----
    {
        // 「这一页有没有那种按钮」= **逐行扫一遍**，不是只看第 0 行：资源包那一页
        // 第一条是个文件夹（DirsFirst），而文件夹永远不给「禁用」。
        const auto rowsWith = [&dialog, &showPage](const QString &key, bool wantToggle) {
            showPage(dialog, key);
            InstanceFilesPage *page = dialog.filesPage(key);
            if (page == nullptr) {
                return 0;
            }
            int found = 0;
            for (int row = 0; row < page->list()->count(); ++row) {
                const auto button = wantToggle ? uiToggleAt(page, row) : uiDeleteAt(page, row);
                found += button != nullptr ? 1 : 0;
            }
            return found;
        };
        const auto hasToggle = [&rowsWith](const QString &key) {
            return rowsWith(key, true) > 0;
        };
        const auto hasDelete = [&rowsWith](const QString &key) {
            return rowsWith(key, false) > 0;
        };
        check(hasToggle(QStringLiteral("mod")) && !hasToggle(QStringLiteral("resourcepack")) &&
                  hasToggle(QStringLiteral("shader")),
              "★ U10d 文件改名开关仅用于模组和光影；资源包改用 options.txt 启用顺序管理");
        check(!hasToggle(QStringLiteral("datapack")) &&
                  !hasToggle(QStringLiteral("schematic")) && !hasToggle(QStringLiteral("save")),
              "★ U10d 数据包 / 原理图 / 存档**没有**「启用·禁用」（改了名也不会生效，"
              "给了反而是骗人）");
        check(hasDelete(QStringLiteral("mod")) && hasDelete(QStringLiteral("datapack")) &&
                  hasDelete(QStringLiteral("schematic")) &&
                  !hasDelete(QStringLiteral("save")),
              "★ U10d 存档那一页**没有删除**（那是用户手里唯一那份，启动器没有副本）");

        // 存档只列文件夹：saves/我的世界/level.dat 不该占一行。
        showPage(dialog, QStringLiteral("save"));
        InstanceFilesPage *saves = dialog.filesPage(QStringLiteral("save"));
        check(saves != nullptr && saves->list()->count() == 1 &&
                  itemText(saves, 0) == QStringLiteral("我的世界"),
              "★ U10d 存档那一页只列文件夹（一个文件夹就是一个存档，level.dat 不是存档）",
              QStringLiteral("%1 条 / %2")
                  .arg(saves != nullptr ? saves->list()->count() : -1)
                  .arg(itemText(saves, 0)));
        // 资源包那一页反过来：**文件夹也算一个资源包**，要列出来。
        showPage(dialog, QStringLiteral("resourcepack"));
        InstanceFilesPage *packs = dialog.filesPage(QStringLiteral("resourcepack"));
        check(packs != nullptr && packs->list()->count() == 2 &&
                  itemText(packs, 0) == QStringLiteral("文件夹包"),
              "★ U10d 资源包那一页文件夹和 .zip 都列（文件夹也是一种资源包）",
              QStringLiteral("%1 条 / 第一条 %2")
                  .arg(packs != nullptr ? packs->list()->count() : -1)
                  .arg(itemText(packs, 0)));
        // ---- ★★ ：「模组」那一页**一个文件夹都不列**（用户点名）----
        //
        // 要求：「游戏模组文件夹里套的文件夹也会被列出来，而禁用对其的作用只是改个
        // 名」。模组目录底下那些子目录（用户盘上的 `mods/.connector`）游戏根本不把它们
        // 当模组看，列出来还给开关就是骗人 —— 所以这一页的 Mode 是 `FilesOnly`，
        // 而资源包 / 数据包 / 光影那三页照旧列目录（**目录本身就是一份资源**）。
        const QString nestDir = modsDir + QStringLiteral("/mcef-libraries");
        check(QFileInfo(nestDir).isDir(),
              "★★ U10d 前提：模组目录里真的套着文件夹（那一页有得可漏）");
        showPage(dialog, QStringLiteral("mod"));
        InstanceFilesPage *mods = dialog.filesPage(QStringLiteral("mod"));
        QString modPaths;
        for (int row = 0; mods != nullptr && row < mods->list()->count(); ++row) {
            modPaths += itemPath(mods, row) + QStringLiteral(" | ");
        }
        check(mods != nullptr && mods->list()->count() == 3 &&
                  !modPaths.contains(QStringLiteral("mcef-libraries")) &&
                  !modPaths.contains(QStringLiteral(".connector")),
              "★★ U10d 「模组」那一页**一个文件夹都不列**（用户原话「游戏模组文件夹里套的"
              "文件夹也会被列出来」）—— 三个 .jar 照旧一条不少",
              QStringLiteral("%1 条：%2")
                  .arg(mods != nullptr ? mods->list()->count() : -1)
                  .arg(modPaths));
        // 反过来钉一次「不是把这一页筛空了」：其它三页的目录还在。
        check(packs != nullptr && packs->list()->count() == 2, "U10d 资源包那一页没被连累");
    }
    return 0;
}

