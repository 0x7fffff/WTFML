#include "Support.hpp"

#include "UiInstanceFilesTestHelpers.hpp"

// U10O 存档页那三样新东西（顶上两颗 / 统计小字 / 不跟 junction 走）
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段共用的 `dialog`（当前页状态
// 要沿用）。分出来以后 `dialog` 按引用传，其余夹具按显式形参传；生命周期见
// docs/dev/TESTING.md「夹具生命周期（ui_check 的大模块）」。
int UiMediumPagesChecks(InstanceSettingsDialog &dialog, const QString &root,
                              const QString &savesDir) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    const auto mkfile = [](const QString &path) { uiMkFile(path); };
    // ---- U10O 「存档」那一页的三样新东西----
    //
    // 要求：「在实例设置里面存档那一栏每个世界都显示存档大小和最后游玩时间，右边
    // 画一支笔意为编辑，和基岩版一样，顶端加个导入世界按钮和批量编辑按钮（这个先做
    // 占位）」。这里量的全是**显示**：那一行上有没有那支笔、那行小字写的是什么、
    // 顶上那两颗露不露。真正动手的两条路（选框、拷盘）在 mw_check / verify 里走。
    //
    // 逃生口 `WTFML_UC_SKIP_U10O=1`：整段跳过。理由不是"这一段不对"，而是它
    // 后面那一发事件循环会把一个**跟这一段无关的**堆损坏戳爆（见下面
    // 「不跟 junction 走」那一节里那一段 NOTES，以及文件头）—— 而这一爆就带走了
    // 后面一千五百多条判据（⑬/⑭ 全线在内）。**默认必须是跑**，只在"这一台机器
    // 上今天就是过不去"时用它换一份后半段的结论。
    if (!qEnvironmentVariableIsSet("WTFML_UC_SKIP_U10O")) {
        showPage(dialog, QStringLiteral("save"));
        InstanceFilesPage *saves = dialog.filesPage(QStringLiteral("save"));
        check(saves != nullptr, "U10O 存档那一页拿得到");
        if (saves != nullptr) {
            // 那支笔只在**存档**这一页露 —— 判据是 setCanEdit()，而对话框只为
            // "save" 那一页调它。别的几页要是也露出来，用户会在一个 .jar 上看见
            // 一支"编辑"笔，点下去是"重命名这个世界"。
            QToolButton *pen = saves->editButtonAt(0);
            check(pen != nullptr, "★ U10O 存档那一行行尾有那支笔（「编辑」）");
            if (pen != nullptr) {
                check(pen->objectName() == InstanceFilesPage::editButtonName(),
                      "U10O 那支笔挂的是那个唯一出处里的 objectName", pen->objectName());
                check(pen->text() == InstanceFilesPage::editText(),
                      "U10O 笔上那几个字是「编辑」", pen->text());
                check(!pen->toolTip().isEmpty(), "U10O 而且有 tooltip 说清点下去会怎样");
                check(pen->isVisible(), "U10O 那一页开着的时候它是看得见的");
                // 一支"画出来的笔"：图标不许是空的（空图标 + 空白字 = 行尾一小块真空）。
                check(!pen->icon().isNull(), "U10O 笔的图标真的画出来了（不是一张空图）");
            }
            for (const QString &key : {QStringLiteral("mod"), QStringLiteral("resourcepack"),
                                       QStringLiteral("datapack"), QStringLiteral("shader"),
                                       QStringLiteral("schematic")}) {
                InstanceFilesPage *other = dialog.filesPage(key);
                check(other == nullptr || other->editButtonAt(0) == nullptr,
                      QStringLiteral("★ U10O 「%1」那一页**没有**那支笔（那儿的每一行不是"
                                     "世界，给它一支笔就是骗人）")
                          .arg(key));
            }

            // ---- 顶上那两颗 ----
            check(saves->importButton() != nullptr && saves->batchButton() != nullptr,
                  "★ U10O 存档页顶上那两颗按钮都建出来了");
            if (saves->importButton() != nullptr && saves->batchButton() != nullptr) {
                check(saves->importButton()->text() ==
                          InstanceSettingsDialog::importWorldText(),
                      "U10O 「导入世界」那几个字来自唯一出处",
                      saves->importButton()->text());
                check(saves->batchButton()->isHidden(),
                      "U10O 未实现的批量编辑不占用存档工具栏");
                check(saves->importButton()->isVisible(),
                      "U10O 存档工具栏保留真实导入入口");
                // 「批量编辑」今天是个**占位**：它的 tooltip 必须自己先说清"还没做"
                // —— 只靠点下去弹的那句话的话，用户要先把鼠标移过去点一下才知道。
                check(!saves->batchButton()->isVisibleTo(saves),
                      "U10O 占位批量编辑刷新后仍不显示");
                // 导入那一颗是**真的**，tooltip 里不该出现"还没做"。
                check(!saves->importButton()->toolTip().contains(QStringLiteral("还没做")),
                      "★ U10O 而「导入世界」是真的，tooltip 里没有那三个字",
                      saves->importButton()->toolTip());
            }
            for (const QString &key : {QStringLiteral("mod"), QStringLiteral("resourcepack"),
                                       QStringLiteral("datapack"), QStringLiteral("shader"),
                                       QStringLiteral("schematic")}) {
                InstanceFilesPage *other = dialog.filesPage(key);
                check(other == nullptr || other->importButton() == nullptr ||
                          !other->importButton()->isVisible(),
                      QStringLiteral("★ U10O 「%1」那一页顶上**没有**这两颗（导入世界是存档"
                                     "那一页的事）")
                          .arg(key));
            }

            // ---- 那行小字：先「正在统计…」，算完换成真数 ----
            //
            // 规矩和别的那几页一样：**先把行摆出来**，一个目录的字节数（要递归整个
            // saves/，几百个世界的话是几秒）排到事件循环上一趟一趟算（见 .cpp 里
            // fillDirectoryStats 那段）。所以这里量的是两态：**刚刷出来**那一刻必须是
            // 「正在统计…」，泵一次事件循环之后必须变成"大小 / 时间"。
            //
            // NOTE: 显式调 refresh() 而不是靠对话框构造时那一趟 —— 构造那一趟排的活儿
            // 可能已经被本段前面那些 processEvents 跑掉了，那时第一态就量不到，
            // 这一条会变成一条**看起来**偶尔红的断言。
            const auto metaTextOf = [&](InstanceFilesPage *page, int row) {
                QListWidgetItem *item = page->list()->item(row);
                QWidget *rowWidget = item != nullptr ? page->list()->itemWidget(item) : nullptr;
                QLabel *meta = rowWidget != nullptr
                                   ? rowWidget->findChild<QLabel *>(
                                         InstanceFilesPage::metaLabelName())
                                   : nullptr;
                return meta != nullptr ? meta->text() : QString();
            };

            check(metaTextOf(saves, 0) == InstanceFilesPage::statsPendingText(),
                  "★ U10O 刚刷出来那一刻那行小字是「正在统计…」（不是空白，也不是 0 B）",
                  metaTextOf(saves, 0));
            // 一个目录一趟，队列空了才停 —— 这里只有一个世界，一次事件循环就够。
            for (int i = 0; i < 8; ++i) {
                QCoreApplication::processEvents();
            }
            const QString filled = metaTextOf(saves, 0);
            check(filled != InstanceFilesPage::statsPendingText(),
                  "★ U10O 泵过事件循环之后它换成真数了（不然就永远停在「正在统计…」）",
                  filled);
            // 那个世界里只有一个 level.dat（4 KB，见上面 mkfile 那个夹具）。
            check(filled.contains(InstanceFilesPage::formatSize(4096)),
                  "★ U10O 大小算的是**目录里所有东西加起来**（4 KB 那份 level.dat）",
                  filled);
            check(filled.contains(QStringLiteral(" / ")),
                  "U10O 大小和最后游玩时间是同一行里的两截（斜线分隔）", filled);
            // 「最后游玩时间」取的是 level.dat 的修改时刻（游戏每存一次档就写它一次），
            // 所以那一截必须长得像一个日期 —— 拿目录自己的 mtime 那种错法给出的也是
            // 日期，这一条只证明"有日期"，取值来源在 verify 那边（见 ZJ 节）。
            const QRegularExpression stamp(QStringLiteral(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2})"));
            check(filled.contains(stamp),
                  "★ U10O 后面那一截是一个「yyyy-MM-dd HH:mm」的时刻（最后游玩时间）",
                  filled);

            // ---- ★ 不跟 junction 走 ----
            //
            // 这一条量的是一条**判据的洞**：那个数的判据原来是 `isSymLink()` 一条，而
            // `mklink /J` 造出来的目录 junction 在 Qt 里 `isSymLink()` 是 **false**
            // （只有 `isJunction()` 认得出它 —— Qt 那边是 `qfilesystemmetadata_p.h:364`
            // 分两个 tag：IO_REPARSE_TAG_SYMLINK → LinkType、IO_REPARSE_TAG_MOUNT_POINT
            // → 只有 JunctionType）。**而且**光"看见链接就 continue"也拦不住：QDirIterator
            // 拒绝下潜只看 `isSymLink()` 一条（Qt 6.10.1 `qdirlisting.cpp:384`），junction
            // 照进不误、里面的文件一条条都是独立结果。所以这一条必须拿**真 junction**
            // 来量（和 mw_check 里删除那一段同一个理由）。
            //
            // 摆法：在这个世界里挂一个 junction，指向另一个装了同样大小（4 KB）一份文件的
            // 目录。**跟过去的话这个数是 8.0 KB，不跟就是 4.0 KB** —— 两个渲染出来的
            // 字面量互不包含（`formatSize()` 那三行），所以判据不含糊。
            const QString worldDir = savesDir + QStringLiteral("/我的世界");
            const QString elsewhere = root + QStringLiteral("/别处的世界");
            mkfile(elsewhere + QStringLiteral("/level.dat"));
            const QString linkPath = worldDir + QStringLiteral("/那边");
            const bool madeJunction =
                QProcess::execute(QStringLiteral("cmd"),
                                  {QStringLiteral("/c"), QStringLiteral("mklink"),
                                   QStringLiteral("/J"), QDir::toNativeSeparators(linkPath),
                                   QDir::toNativeSeparators(elsewhere)}) == 0 &&
                QFileInfo(linkPath).isJunction();
            check(madeJunction, "★ U10O 先造出一个真 junction（mklink /J，不要管理员权限）",
                  QStringLiteral("isSymLink=%1 isJunction=%2")
                      .arg(QFileInfo(linkPath).isSymLink() ? 1 : 0)
                      .arg(QFileInfo(linkPath).isJunction() ? 1 : 0));
            // 逃生口 `WTFML_SKIP_JUNCTION=1`：跳过下面这一段（refresh + 泵 + 取小字）。
            //
            // 当天稍后又量了一轮：崩点在 `InstanceSettingsDialog` 的析构里，栈全是
            // Qt6Widgets / Qt6Gui、一行我们的代码都没有，而且一崩就是连着几十趟、
            // 一绿也是连着好几趟。详情见文件头那一段。
            // 所以这个逃生口治不了它（崩点在它前面），留着是为了别的事 —— 同样
            // **别拿它当常态**，默认必须是**跑**。
            if (madeJunction && !qEnvironmentVariableIsSet("WTFML_SKIP_JUNCTION")) {
                // 顺带把"junction 不是符号链接"这条前提**现量一遍**：这条断言红了，
                // 说明 Qt 那边的分类变了，上面那两处判据得跟着改。
                check(!QFileInfo(linkPath).isSymLink(),
                      "★ U10O junction **不是**符号链接（isSymLink() 是 false）—— 只判"
                      "isSymLink() 的那条老判据在 Windows 上就是这么漏的",
                      QStringLiteral("isSymLink=%1")
                          .arg(QFileInfo(linkPath).isSymLink() ? 1 : 0));
                saves->refresh();
                for (int i = 0; i < 8; ++i) {
                    QCoreApplication::processEvents();
                }
                const QString withLink = metaTextOf(saves, 0);
                check(withLink.contains(InstanceFilesPage::formatSize(4096)) &&
                          !withLink.contains(InstanceFilesPage::formatSize(8192)),
                      "★ U10O junction 那一头**不算进来**（世界里那一份 4 KB 照旧，"
                      "不是 8 KB）",
                      withLink);
            }
        }
    }
    return 0;
}

