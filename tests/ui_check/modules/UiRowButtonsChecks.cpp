#include "Support.hpp"

#include "UiInstanceFilesTestHelpers.hpp"

// U10e 行尾那两颗按下去报的是哪一条
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段共用的 `dialog`。分出来以后
// `dialog` 按引用传，其余夹具按显式形参传；生命周期见 docs/dev/TESTING.md
// 「夹具生命周期（ui_check 的大模块）」。
int UiRowButtonsChecks(InstanceSettingsDialog &dialog, const QString &modsDir) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    const auto itemText = [](InstanceFilesPage *page, int row) { return uiItemText(page, row); };
    const auto itemPath = [](InstanceFilesPage *page, int row) { return uiItemPath(page, row); };
    const auto itemFlag = [](InstanceFilesPage *page, int row, int role) { return uiItemFlag(page, row, role); };
    // ---- U10e 行尾那两颗按下去报的是哪一条 ----
    {
        showPage(dialog, QStringLiteral("mod"));
        InstanceFilesPage *page = dialog.filesPage(QStringLiteral("mod"));
        QString lastPath;
        int toggles = 0;
        int deletes = 0;
        QObject::connect(page, &InstanceFilesPage::toggleRequested, page,
                         [&lastPath, &toggles](const QString &path) {
                             lastPath = path;
                             ++toggles;
                         });
        QObject::connect(page, &InstanceFilesPage::deleteRequested, page,
                         [&lastPath, &deletes](const QString &path) {
                             lastPath = path;
                             ++deletes;
                         });
        if (page != nullptr && page->toggleButtonAt(2) != nullptr) {
            page->toggleButtonAt(2)->click();
        }
        const bool toggleOk = toggles == 1 && lastPath.endsWith(QStringLiteral(".disabled"));
        if (page != nullptr && page->deleteButtonAt(0) != nullptr) {
            page->deleteButtonAt(0)->click();
        }
        check(toggleOk && deletes == 1 &&
                  lastPath == modsDir + QStringLiteral("/iris-1.6.5.jar"),
              "★ U10e 「启用/禁用」报的是**这一条现在那个名字**（禁用着的带 .disabled）、"
              "「删除」报的是这一条的绝对路径；两件事各自成一条信号",
              QStringLiteral("改名 %1 次 / 删除 %2 次，最后一条 %3")
                  .arg(toggles)
                  .arg(deletes)
                  .arg(lastPath));

        // 「打开文件夹」那一颗也报同一个目录。
        lastPath.clear();
        int folderOpens = 0;
        QObject::connect(page, &InstanceFilesPage::openFolderRequested, page,
                         [&lastPath, &folderOpens](const QString &path) {
                             lastPath = path;
                             ++folderOpens;
                         });
        if (page != nullptr && page->openFolderButton() != nullptr) {
            page->openFolderButton()->click();
        }
        check(folderOpens == 1 && lastPath == modsDir,
              "U10e 那一页的「打开文件夹」开的是这一页看的那个目录", lastPath);
    }
    return 0;
}
