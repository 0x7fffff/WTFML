#include "Support.hpp"

// U10f：空目录 / 用不上 —— 两句不同的话。
//
// 「不存在的目录」和「空目录」应当说同一句话；「这一页用不上」（没装光影 / 没装结构 mod）
// 另有一句，而且装了之后要能回来（这一档是可逆的）。
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段的对话框夹具与临时目录。
// 分出来以后要的四样都走显式形参，生命周期见 docs/TESTING.md
// 「夹具生命周期（ui_check 的大模块）」。
int UiEmptyPageChecks(const QString &instanceDisplayName, const QString &instanceDirName,
                      const InstanceSettingsDialog::GlobalValues &global, const QString &root) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    {
        Dlg other(instanceDisplayName, instanceDirName, Dlg::Override(), global, QStringList(),
                  true);
        QHash<QString, QString> emptyDirs;
        // 这个目录故意建都不建：不存在的目录和空目录应当是同一句话。
        emptyDirs.insert(QStringLiteral("datapack"), root + QStringLiteral("/还没有"));
        other.setFileDirs(emptyDirs);
        other.showPage(QStringLiteral("datapack"));
        InstanceFilesPage *page = other.filesPage(QStringLiteral("datapack"));
        // 「显不显示」一律问 **isVisibleTo(这一页自己)**。两个更直白的写法在这儿都是假的：
        // * `isHidden()` 量的是"这个控件自己有没有被显式藏起来"，父控件被藏起来不算
        // （「打开文件夹」那颗就是这样 —— 它自己从没被藏过，藏的是它爹那一排）。
        // * `isVisible()` 恒假：这一页这会儿收在堆叠里，父链上就断了。
        // isVisibleTo() 正好只看到"这一页为止"那一段。
        check(page != nullptr && !page->list()->isVisibleTo(page) &&
                  page->noticeLabel()->isVisibleTo(page) &&
                  page->noticeLabel()->text() == Dlg::filesEmptyText(QStringLiteral("datapack")),
              "★ U10f 目录是空的（或者压根还没建）时列表收起来、换成一句话",
              page != nullptr ? page->noticeLabel()->text() : QString());
        check(page != nullptr && page->refreshButton()->isVisibleTo(page) &&
                  page->openFolderButton()->isVisibleTo(page),
              "U10f 空目录里那两颗按钮还在（用户就是要在空目录上点「打开文件夹」）");
        check(page != nullptr && page->searchEdit() != nullptr &&
                  page->stateCombo() != nullptr &&
                  page->stateButtonAt(InstanceFilesPage::StateFilter::All) != nullptr &&
                  !page->searchEdit()->isVisibleTo(page) &&
                  !page->stateButtonAt(InstanceFilesPage::StateFilter::All)->isVisibleTo(page),
              "★ U10f 空目录里那两排筛选也收起来了（没东西可筛）");

        // 「用不上」那一档：列表**和那排按钮一起**收起来。
        other.setShaderUsable(false);
        other.showPage(QStringLiteral("shader"));
        InstanceFilesPage *shader = other.filesPage(QStringLiteral("shader"));
        check(shader != nullptr && !shader->list()->isVisibleTo(shader) &&
                  !shader->refreshButton()->isVisibleTo(shader) &&
                  !shader->openFolderButton()->isVisibleTo(shader) &&
                  shader->searchEdit() != nullptr &&
                  !shader->searchEdit()->isVisibleTo(shader) &&
                  shader->stateButtonAt(InstanceFilesPage::StateFilter::All) != nullptr &&
                  !shader->stateButtonAt(InstanceFilesPage::StateFilter::All)
                       ->isVisibleTo(shader) &&
                  shader->noticeLabel()->text() == Dlg::shaderUnavailableText(),
              "★ U10f 装不了光影的实例：「光影包」那一页整页换成一句解释"
              "（列表、那排按钮、以及那两排筛选一起收起来）",
              shader != nullptr ? shader->noticeLabel()->text() : QString());
        // 取舍写在 `rebuildNavigationTree()` 的注释里 ——
        // 「保留用户明确关闭页面的选择；可用性不足在既有页面里解释，**不吞掉导航入口**」：
        // 只在树上认"用户自己勾掉的"（`m_pageOverrides`），不看 `m_pageAvailable`。
        // 于是"点得进去"这件事仍然成立：点进去那一页整幅是一句解释 + 那两颗按钮，
        // 而不是"树上少一条、用户不知道这一页去哪了"。
        //
        // NOTE: 量的是**树上那棵导航**（`navigationTree()` —— 界面导航的唯一出处），
        // 不再是 `pageList()` 那一列旧的、`hide()` 着的列表：它的内容早就不跟
        // 页面表走了（还是老的四组十三行），拿它量两个方向都会量错。
        const auto leftHasKey = [&other](const QString &key) {
            QTreeWidget *tree = other.navigationTree();
            if (tree == nullptr) {
                return false;
            }
            for (QTreeWidgetItemIterator it(tree); *it; ++it) {
                if ((*it)->data(0, Qt::UserRole).toString() == key) {
                    return true;
                }
            }
            return false;
        };
        check(leftHasKey(QStringLiteral("shader")) &&
                  !other.shownEntryKeys().contains(QStringLiteral("shader")),
              "★★ U10f 用不上的那一条**留在树上**（点得进去、由那一页自己解释）—— 而"
              "shownEntryKeys() 里已经没它了（「能用」这份账仍然只算可用的页）",
              QStringLiteral("树上有 %1 / shownEntryKeys 里 %2")
                  .arg(leftHasKey(QStringLiteral("shader")) ? QStringLiteral("有")
                                                            : QStringLiteral("<没有>"),
                       other.shownEntryKeys().contains(QStringLiteral("shader"))
                           ? QStringLiteral("有")
                           : QStringLiteral("<没有>")));
        // 反过来：这一条钉的是"树只认用户自己勾掉的" —— 可用性变了，树上一个条目都不动。
        const int shaderRowsBefore = leftHasKey(QStringLiteral("shader")) ? 1 : 0;
        other.setShaderUsable(true);
        check(shader != nullptr && shader->refreshButton()->isVisibleTo(shader) &&
                  shader->noticeLabel()->text() != Dlg::shaderUnavailableText() &&
                  (leftHasKey(QStringLiteral("shader")) ? 1 : 0) == shaderRowsBefore &&
                  other.shownEntryKeys().contains(QStringLiteral("shader")),
              "★ U10f 装了 Iris / OptiFine 之后那一页回到正常（页里的解释收掉、树上那条"
              "一直都在 —— 可用性不进树）");
        // 原理图那一档是同一个形状（谁来定"用得上"是 MainWindow 的事 —— 它去 mods
        // 里找 litematica；这个对话框自己一个字都不猜）。
        other.setSchematicUsable(false);
        other.showPage(QStringLiteral("schematic"));
        InstanceFilesPage *schematic = other.filesPage(QStringLiteral("schematic"));
        // 量的是**那排按钮**不是列表：这个实例那几页的目录压根没喂进来，列表本来就是空的
        // （空目录也是收起来）—— 只看列表分不出"用不上"和"还没有"。
        // NOTE: 左栏里那一条**还在树上**（上面 `leftHasKey` 那一对钉的就是这件事 ——
        // 树不跟可用性走），这里量的是**页里**那一半（整页换解释 + 收掉那排按钮）。
        check(schematic != nullptr &&
                  !schematic->refreshButton()->isVisibleTo(schematic) &&
                  schematic->noticeLabel()->text() == Dlg::schematicUnavailableText(),
              "★ U10f 没装 litematica 时「原理图」那一页走同一套（整页换解释 + 那排按钮"
              "一起收起来）",
              schematic != nullptr ? schematic->noticeLabel()->text() : QString());
        other.setSchematicUsable(true);
        check(schematic != nullptr && schematic->refreshButton()->isVisibleTo(schematic) &&
                  schematic->noticeLabel()->text() != Dlg::schematicUnavailableText(),
              "★ U10f 装了之后那一页回来（这一档是可逆的）");
    }
    return 0;
}
