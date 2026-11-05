#include "Support.hpp"

#include "UiInstanceFilesTestHelpers.hpp"

// U10a 中间那几页都是同一个控件，各自落在自己那一格里
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段共用的 `dialog`。分出来以后
// `dialog` 按引用传，其余夹具按显式形参传；生命周期见 docs/TESTING.md
// 「夹具生命周期（ui_check 的大模块）」。
int UiPageIdentityChecks(InstanceSettingsDialog &dialog) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    const auto itemText = [](InstanceFilesPage *page, int row) { return uiItemText(page, row); };
    const auto itemPath = [](InstanceFilesPage *page, int row) { return uiItemPath(page, row); };
    const auto itemFlag = [](InstanceFilesPage *page, int row, int role) { return uiItemFlag(page, row, role); };
    // ---- U10a 中间那几页都是同一个控件，各自落在自己那一格里 ----
    {
        const QStringList keys = Dlg::filePageKeys();
        bool pagesFound = true;
        int count = 0;
        for (const QString &key : keys) {
            // 「服务器列表」是棵表（不是 InstanceFilesPage），跳过。
            if (key == Dlg::serverPageKey()) {
                continue;
            }
            ++count;
            InstanceFilesPage *page = dialog.filesPage(key);
            pagesFound = pagesFound && page != nullptr && page->list() != nullptr &&
                         page->objectName() == Dlg::filesPageName(key) &&
                         dialog.findChild<QWidget *>(Dlg::panelName(key)) != nullptr;
        }
        // 数量**从键表里算**，不写死（ 加了「配置文件」那一页时，写死的
        // `== 6` 正是这一条红的唯一原因）。"页数对不对"由 U0a 那两条接着管 ——
        // 它们量的是 pageCount()，而那个是从同一张键表算出来的。
        const int expected = keys.size() - 1;  // 减去 `server`
        check(pagesFound && count == expected,
              "★ U10a 中间那几页目录列表都搭起来了、都在自己那一格里（filesPageName 与 "
              "panelName 对得上）",
              QStringLiteral("%1 页").arg(count));
        // 而且**「配置文件」那一页和别的几页不是一个形状**：它是一页
        // 两格子（列表 / 编辑器，见 InstanceSettingsDialog::configFilePage() 那段）
        // —— 那张列表不直接是 panel 的孩子，中间还隔着那层堆叠。上面那条用的是
        // findChild（递归），所以它照样找得到；这一条把"隔着的是谁"也钉住。
        QWidget *configPanel = dialog.findChild<QWidget *>(Dlg::panelName(
            QStringLiteral("config")));
        InstanceFilesPage *configFiles = dialog.filesPage(QStringLiteral("config"));
        // 那张列表**不是直接摆在这一页上的**：中间隔着"两格堆叠 → 第 0 格"。
        // 判据走"往上找得到这一页、但它不是这一页的直接孩子"—— 数层数（parentWidget()
        // 连几级）会把中间那层容器的实现细节写死，而那不是这一条要说的事。
        bool nestedOk = false;
        if (configFiles != nullptr && configPanel != nullptr &&
            configFiles->parentWidget() != configPanel) {
            for (QWidget *w = configFiles->parentWidget(); w != nullptr; w = w->parentWidget()) {
                if (w == configPanel) {
                    nestedOk = true;
                    break;
                }
            }
        }
        check(nestedOk,
              "★ U10a 「配置文件」那张列表**嵌在两格堆叠的第 0 格里**（不是直接摆在这一页"
              "上）—— 第 1 格是编辑器，见 configFilePage() 那段");

        // 编辑器那一格：**和列表同在那个堆叠里**，而且刚切过来时没露脸。
        //
        // NOTE: 得先把这一页摆到台面上再问 isVisible()（收着的堆叠页一律是 false，
        // 不摆的话这一条永远绿、什么都没量到 —— 和这一节别处量几何前先 showPage
        // 是同一条规矩）。
        showPage(dialog, QStringLiteral("config"));
        ConfigFileEditorPage *editor = dialog.configFilePage();
        check(editor != nullptr && editor->objectName() == ConfigFileEditorPage::pageName(),
              "★ U10a 那一页第 1 格里有个编辑器（它自己的 objectName 走 pageName()，"
              "里面那个编辑框是 editorName() —— 两个别混）");
        // 两个在同一个堆叠里：从列表往上找，**第一个同时也是编辑器祖先的那一层**
        // 就是那个堆叠（往上数几级是别人的实现细节，不写死）。
        QWidget *sharedParent = nullptr;
        for (QWidget *w = configFiles != nullptr ? configFiles->parentWidget() : nullptr;
             w != nullptr; w = w->parentWidget()) {
            if (editor != nullptr && w->isAncestorOf(editor)) {
                sharedParent = w;
                break;
            }
        }
        check(sharedParent != nullptr && configFiles != nullptr && configFiles->isVisible() &&
                  editor != nullptr && !editor->isVisible(),
              "★ U10a 列表和编辑器**在同一个堆叠的相邻两格里**，而且刚切到这一页时露着的"
              "是**列表**那一格（编辑器要等点开一个文件 —— 一进来就露着的话用户看到的"
              "是一片空白）");
    }
    return 0;
}
