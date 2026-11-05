#include "Support.hpp"

#include "UiInstanceFilesTestHelpers.hpp"

// U10T 「结构/原理图」顶上那两排「来源」
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段共用的 `dialog`。分出来以后
// `dialog` 按引用传，其余夹具按显式形参传；生命周期见 docs/TESTING.md
// 「夹具生命周期（ui_check 的大模块）」。
int UiStructureSourcesChecks(const QString &instanceDisplayName, const QString &instanceDirName,
                             const InstanceSettingsDialog::GlobalValues &global) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    const auto itemText = [](InstanceFilesPage *page, int row) { return uiItemText(page, row); };
    const auto itemPath = [](InstanceFilesPage *page, int row) { return uiItemPath(page, row); };
    const auto itemFlag = [](InstanceFilesPage *page, int row, int role) { return uiItemFlag(page, row, role); };
    // ---- U10T 「结构/原理图」顶上那两排「来源」----
    //
    // 要求：「那就像版本树一样根据存档来列，一个存档来一个下拉框，存档类来一个
    // 总下拉，原理图文件夹和存档类并列，没有结构的存档不显示在列表里面，没有装投影图
    // 则不显示投影图下拉」；摆法另有一句钉死：「横着并排，即**上面一排是存档的，下面
    // 一排是投影的**」。
    //
    // 为什么是**两个**来源：结构方块存下来的那些 `.nbt` **跟着存档走**
    // （`saves/<存档>/generated/minecraft/structures/`），而投影模组读的 `.litematic`
    // 在这个实例自己的文件夹里（`schematics/`）。上面那几排说的是"这一页正在看哪几个
    // 目录"，下面那份列表是这几个目录**合起来**的东西（不是"选中谁就只列谁"）。
    //
    // 这一层只量「摆几排、排头写着什么、按选中的那些目录列出什么」。**哪些存档真
    // 有结构 / 这个实例装没装投影模组，是 MainWindow 算好了递进来的**
    // （`structureSourcesFor()` —— 那两句判据在 ui/ 这一层说不出口）："没有结构的
    // 存档根本不进名单"、"没装投影就不摆投影那一排"都在那里。这里量的最后一档是：
    // 调用方递空名单 ⇒ 一个框都不摆，退回到别的七页那种样子。
    {
        QTemporaryDir structureScratch;
        const QString sRoot = structureScratch.path();
        const auto mkfileT = [](const QString &path) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QByteArray(4096, 't'));
            }
        };
        const QString sStructuresA =
            sRoot + QStringLiteral("/saves/存档甲/generated/minecraft/structures");
        const QString sStructuresB =
            sRoot + QStringLiteral("/saves/存档乙/generated/minecraft/structures");
        const QString sSchematics = sRoot + QStringLiteral("/schematics");
        mkfileT(sStructuresA + QStringLiteral("/房子.nbt"));
        mkfileT(sStructuresB + QStringLiteral("/桥.nbt"));
        mkfileT(sSchematics + QStringLiteral("/大厅.litematic"));

        QHash<QString, QString> sDirs;
        sDirs.insert(QStringLiteral("schematic"), sSchematics);
        Dlg structureDlg(instanceDisplayName, instanceDirName, Dlg::Override(), global,
                         QStringList(), true);
        structureDlg.setInstanceDir(sRoot);
        structureDlg.setFileDirs(sDirs);
        structureDlg.resize(900, 520);
        structureDlg.showPage(QStringLiteral("schematic"));
        structureDlg.show();
        InstanceFilesPage *structurePage =
            structureDlg.filesPage(QStringLiteral("schematic"));
        check(structurePage != nullptr && structurePage->list() != nullptr,
              "U10T 前提：找得到「结构/原理图」那一页");
        if (structurePage != nullptr && structurePage->list() != nullptr) {
            // 这一页上"现在列着哪几条" —— 按**路径**报（屏幕上那个名字是二次加工过
            // 的：禁用过的会在尾巴上多长出一截记号）。
            const auto listed = [structurePage]() {
                QStringList out;
                for (int row = 0; row < structurePage->list()->count(); ++row) {
                    QListWidgetItem *item = structurePage->list()->item(row);
                    if (item != nullptr) {
                        out.append(item->data(InstanceFilesPage::pathRole()).toString());
                    }
                }
                return out;
            };
            const auto hasFile = [](const QStringList &paths, const QString &name) {
                for (const QString &path : paths) {
                    if (path == name) {
                        return true;
                    }
                }
                return false;
            };

            // ---- ① 没人摆过「来源」：这一页保持原样 ----
            check(structurePage->structureSourceCombos().isEmpty(),
                  "★ U10T 没摆过「来源」的时候那一页上一个下拉框都没有（别的七页走的"
                  "就是这条早退路 —— 它们谁也走不到那两排上去）");
            check(listed().size() == 1 &&
                      hasFile(listed(), sSchematics + QStringLiteral("/大厅.litematic")),
                  "★ U10T 那时候列的还是 setDirectory() 给的那一个目录",
                  listed().join(QStringLiteral(" / ")));
            InstanceFilesPage *siblingPage = structureDlg.filesPage(QStringLiteral("mod"));
            check(siblingPage != nullptr && siblingPage->structureSourceCombos().isEmpty(),
                  "★ U10T 别的页（模组）上也没有那两排 —— 这一套只给「结构/原理图」用");

            // ---- ② 摆上两排：**上面存档、下面投影** ----
            InstanceFilesPage::StructureSource sWorlds;
            sWorlds.title = InstanceFilesPage::structureWorldTitle();
            sWorlds.labels << QStringLiteral("存档甲") << QStringLiteral("存档乙");
            sWorlds.paths << sStructuresA << sStructuresB;
            InstanceFilesPage::StructureSource sProjections;
            sProjections.title = InstanceFilesPage::structureSchematicTitle();
            sProjections.labels << InstanceFilesPage::structureSchematicText();
            sProjections.paths << sSchematics;
            structurePage->setStructureSources({sWorlds, sProjections});

            const QList<QComboBox *> combos = structurePage->structureSourceCombos();
            check(combos.size() == 2,
                  "★ U10T 摆了两排就是两个下拉框（不是一个，也不是四颗按钮）",
                  QStringLiteral("%1 个").arg(combos.size()));
            if (combos.size() == 2) {
                for (int row = 0; row < 2; ++row) {
                    check(combos.at(row)->objectName() ==
                              InstanceFilesPage::structureComboName(row),
                          "U10T 两个下拉框各挂一个唯一出处的 objectName（验证工程不解析"
                          "文字）",
                          combos.at(row)->objectName());
                }
                auto *title0 = structurePage->findChild<QLabel *>(
                    InstanceFilesPage::structureTitleName(0));
                auto *title1 = structurePage->findChild<QLabel *>(
                    InstanceFilesPage::structureTitleName(1));
                const QString titleText0 =
                    title0 != nullptr ? title0->text() : QStringLiteral("<没有>");
                const QString titleText1 =
                    title1 != nullptr ? title1->text() : QStringLiteral("<没有>");
                check(titleText0 == InstanceFilesPage::structureWorldTitle() &&
                          titleText1 == InstanceFilesPage::structureSchematicTitle(),
                      "★★ U10T **上面那一排是存档的、下面那一排是投影的**（用户点名的那"
                      "句话；两个排头的字都走唯一出处，不在这儿另抄一遍）",
                      QStringLiteral("「%1」「%2」").arg(titleText0, titleText1));
                check(combos.at(0)->count() == 2 &&
                          combos.at(0)->itemText(0) == QStringLiteral("存档甲") &&
                          combos.at(0)->itemText(1) == QStringLiteral("存档乙"),
                      "★ U10T 存档那一排就是递进来的那几个（**没有结构的存档根本不在"
                      "名单里** —— 那一句是 MainWindow 筛的）",
                      QStringLiteral("%1 / %2").arg(combos.at(0)->itemText(0),
                                                   combos.at(0)->itemText(1)));
                check(combos.at(1)->count() == 1 &&
                          combos.at(1)->itemText(0) ==
                              InstanceFilesPage::structureSchematicText(),
                      "★ U10T 投影那一排就是那个文件夹本身（今天只有一个投影来源）",
                      combos.at(1)->itemText(0));

                // ---- ③ 列表 = 两排**现在选中的那几个目录合起来** ----
                check(listed().size() == 2 &&
                          hasFile(listed(), sStructuresA + QStringLiteral("/房子.nbt")) &&
                          hasFile(listed(),
                                  sSchematics + QStringLiteral("/大厅.litematic")),
                      "★★ U10T 列表是两排**合起来**的（存档甲里那份结构 + 投影那一份），"
                      "不是「选中谁就列谁」",
                      listed().join(QStringLiteral(" / ")));

                // ---- ④ 换一个存档：跟着换的只有它那半边 ----
                combos.at(0)->setCurrentIndex(1);
                check(listed().size() == 2 &&
                          hasFile(listed(), sStructuresB + QStringLiteral("/桥.nbt")) &&
                          !hasFile(listed(), sStructuresA + QStringLiteral("/房子.nbt")) &&
                          hasFile(listed(),
                                  sSchematics + QStringLiteral("/大厅.litematic")),
                      "★★ U10T 换一个存档：属于它的那半边当场换掉，另一个来源纹丝不动",
                      listed().join(QStringLiteral(" / ")));
                combos.at(0)->setCurrentIndex(0);
            }

            // ---- ⑤ 一个存档都没有结构：存档那一排摆一行灰字 ----
            InstanceFilesPage::StructureSource sNoWorlds;
            sNoWorlds.title = InstanceFilesPage::structureWorldTitle();
            structurePage->setStructureSources({sNoWorlds, sProjections});
            const QList<QComboBox *> greyCombos = structurePage->structureSourceCombos();
            check(greyCombos.size() == 2 && greyCombos.at(0)->count() == 1 &&
                      greyCombos.at(0)->itemText(0) == InstanceFilesPage::structureNoneText(),
                  "★★ U10T 一个存档都没有结构的时候，存档那一排摆的是那行灰字"
                  "（用户原话：「如果没有任何存档含有结构那么存档总下拉打开显示无可用的"
                  "结构」）",
                  greyCombos.isEmpty() ? QStringLiteral("<没有>")
                                       : greyCombos.at(0)->itemText(0));
            check(!greyCombos.isEmpty() && !greyCombos.at(0)->isEnabled(),
                  "★ U10T 那行灰字是**点不动**的（它不是一个能被选中的值：选中它就等于"
                  "没选，这一排对一个条目都不贡献）");
            check(listed().size() == 1 &&
                      hasFile(listed(), sSchematics + QStringLiteral("/大厅.litematic")),
                  "★ U10T 存档那一排空着的时候，列表里只剩投影那一份（不多一条，也没把"
                  "那个空目录当成一个来源）",
                  listed().join(QStringLiteral(" / ")));

            // ---- ⑥ 两样都没有：调用方递空名单 ⇒ 一个框都不摆 ----
            structurePage->setStructureSources({});
            check(structurePage->structureSourceCombos().isEmpty(),
                  "★★ U10T 两样都没有的时候（调用方递空名单）一个框都不摆 —— 用户原话："
                  "「就直接不显示任何框，只保留没有结构的提示文本」");
            structurePage->setDirectory(sRoot + QStringLiteral("/还没有这个目录"));
            check(structurePage->noticeLabel()->isVisibleTo(structurePage) &&
                      structurePage->noticeLabel()->text() ==
                          Dlg::filesEmptyText(QStringLiteral("schematic")),
                  "★★ U10T 上一条说的「只保留那句提示文本」就是这一句（目录也空着的一"
                  "页，走的是别的七页一字不差的那条路）",
                  structurePage->noticeLabel()->text());
        }
    }
    return 0;
}

