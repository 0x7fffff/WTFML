#include "Support.hpp"
#include "UiInstanceFilesTestHelpers.hpp"

// U10N：行尾那颗「详情」⇒ 报出这一条是哪个文件（模组 / 资源包 / 数据包 / 光影包）。
//
// 要求（上半句）：「除了模组以外的几个都是点击模组名称自动进入下载界面（前提是匹配上了，
// **模组的待会再说**）」；（同一天的下半句）「对于模组，点进去和资源包差不多」⇒ 模组也接上了。
//
// 这一层验的是**这一层能验的那一半**：哪几页有那一格、那一格在"认得出 / 认不出"两种行上
// 分别什么样、点一下报的是什么。"收页 + 开那个资源自己的那一页"那一半在 MainWindow
// （端到端在 mw_check 的 T24）。
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段的对话框夹具。分出来以后
// 要的三样（两个名字 + 全局那份值）都是**显式形参**，生命周期见 docs/dev/TESTING.md
// 「夹具生命周期（ui_check 的大模块）」。
int UiDetailEntryChecks(const QString &instanceDisplayName, const QString &instanceDirName,
                        const InstanceSettingsDialog::GlobalValues &global) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    // ---- U10N 行尾那颗「详情」⇒ 报出这一条是哪个文件（ / ）----
    //
    // 要求（ 上半句）：「除了模组以外的几个都是点击模组名称自动进入下载界面
    // （前提是匹配上了，**模组的待会再说**）」；（同一天的下半句）「对于模组，点进去和
    // 资源包差不多」⇒ **模组也接上了**。
    //
    // 这一层验的是**这一层能验的那一半**：哪几页有那一格、那一格在"认得出 / 认不出"
    // 两种行上分别什么样、点一下报的是什么。"收页 + 开那个资源自己的那一页"那一半在
    // MainWindow（端到端在 mw_check 的 T24）。
    {
        // ① 纯函数：哪几页接这件事。★ 起**模组也在表里** —— 那两张表（接哪几页 /
        // 顶上那颗「下载新 X」）从这一天起是同一张脸，可它们仍然是**两件事**，
        // 别合并（见 .hpp 里那条 NOTE）。
        check(Dlg::filesNameOpensResource(QStringLiteral("mod")) &&
                  Dlg::filesNameOpensResource(QStringLiteral("resourcepack")) &&
                  Dlg::filesNameOpensResource(QStringLiteral("datapack")) &&
                  Dlg::filesNameOpensResource(QStringLiteral("shader")),
              "★★ U10N 模组 / 资源包 / 数据包 / 光影包**四页**都有那颗「详情」"
              "（判据仍是 filesNameOpensResource()）");
        check(!Dlg::filesNameOpensResource(QStringLiteral("schematic")) &&
                  !Dlg::filesNameOpensResource(QStringLiteral("save")) &&
                  !Dlg::filesNameOpensResource(Dlg::configPageKey()) &&
                  !Dlg::filesNameOpensResource(Dlg::serverPageKey()) &&
                  !Dlg::filesNameOpensResource(QStringLiteral("shortcut")),
              "★★ U10N 而原理图 / 存档 / 配置文件 / 服务器列表 / 快捷方式上**没有那一颗**"
              "（前两个在下载页里没有「它自己的那一页」可去，配置文件根本不是个资源）");
        // 没认出来时那一句是**空串**：空串就是"这一行不变成链接"的唯一判据
        // （见 setResourceForPath），所以这一条钉的是"别把空标题也当认出来了"。
        check(Dlg::filesResourceTip(QString()).isEmpty() &&
                  !Dlg::filesResourceTip(QStringLiteral("材质包甲")).isEmpty(),
              "★ U10N 没认出来（标题空）⇒ 一个字的提示都没有；认出来了才有那一句");

        // ③ ★★ ：**原来这一格是钉 `filesDetailHint()` 那句话的**
        // （「行尾那颗 ⓘ 能进它自己的那一页，单击选中一条，按住 Ctrl 能多选，
        // Ctrl+A 全选」—— 它当年存在的意义是"说出去的话和代码是一件事"：
        // 名字上那一发退了场，旧句子留着会让用户照着一个不存在的动线点）。
        // 现在**那句话连同 `filesDetailHint()` 一起删了**（用户："把这个页面下方那段
        // 叙述删了来让框拉到底充分利用空间"），那四页底下**一个 QLabel 都不建**。
        // ⇒ 这一格没有可钉的东西了，**它钉的两件事搬到了 U10j 和 U10M**：
        // / U10j：那四页**找不到** `filesHintName(key)` 那个控件、其余页找得到；
        // / U10M：那四页 `filesHint()` 一律回空串。
        // 留在这一格的教训，别再犯一遍：删一个界面元素的时候，**先问一句"哪条断言
        // 是钉它的"** —— 断言不比产品少写更值钱，钉着一个已删的东西的断言只会红，
        // 然后被人顺手注释掉（`wtfml-comment-vs-code`：字面声称的那一侧就是洞）。

        // ② 界面上：认出来的那一条是链接的样子，没认出来的那一条不是。
        QTemporaryDir scratch2;
        const QString root2 = scratch2.path();
        const auto mkfile2 = [](const QString &path) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QByteArray(4096, 'y'));
            }
        };
        mkfile2(root2 + QStringLiteral("/resourcepacks/材质包甲.zip"));
        mkfile2(root2 + QStringLiteral("/resourcepacks/没认出来的.zip"));
        mkfile2(root2 + QStringLiteral("/mods/模组甲.jar"));
        QHash<QString, QString> dirs2;
        dirs2.insert(QStringLiteral("mod"), root2 + QStringLiteral("/mods"));
        dirs2.insert(QStringLiteral("resourcepack"), root2 + QStringLiteral("/resourcepacks"));
        dirs2.insert(QStringLiteral("datapack"), root2 + QStringLiteral("/datapacks"));
        dirs2.insert(QStringLiteral("shader"), root2 + QStringLiteral("/shaderpacks"));
        const QString packPath = root2 + QStringLiteral("/resourcepacks/材质包甲.zip");
        const QString unknownPath = root2 + QStringLiteral("/resourcepacks/没认出来的.zip");
        const QString modPath = root2 + QStringLiteral("/mods/模组甲.jar");

        Dlg linked(instanceDisplayName, instanceDirName, Dlg::Override(), global,
                   QStringList(), true);
        linked.setInstanceDir(root2);
        linked.setFileDirs(dirs2);
        linked.showPage(QStringLiteral("resourcepack"));
        linked.show();

        const auto rowForPath = [](Dlg &d, const QString &key, const QString &path) {
            InstanceFilesPage *page = d.filesPage(key);
            if (page == nullptr || page->list() == nullptr) {
                return -1;
            }
            for (int row = 0; row < page->list()->count(); ++row) {
                QListWidgetItem *item = page->list()->item(row);
                if (item != nullptr &&
                    item->data(InstanceFilesPage::pathRole()).toString() == path) {
                    return row;
                }
            }
            return -1;
        };
        // 名字那一格：条目上的名字 + 那一行挂的悬停提示（行首图标、名字两格都挂整条路径）。
        struct NameCell {
            QString text;
            QString tip;
            QString toolTip() const { return tip; }
        };
        const auto nameLabelFor = [&rowForPath](Dlg &d, const QString &key,
                                                const QString &path) -> std::shared_ptr<NameCell> {
            const int row = rowForPath(d, key, path);
            InstanceFilesPage *page = d.filesPage(key);
            QListWidgetItem *item = row < 0 || page == nullptr ? nullptr : page->list()->item(row);
            if (item == nullptr) return nullptr;
            return std::make_shared<NameCell>(
                NameCell{item->data(Qt::AccessibleTextRole).toString(), item->toolTip()});
        };
        const auto detailButtonFor = [&rowForPath](Dlg &d, const QString &key,
                                                   const QString &path) -> UiRowCellPtr {
            const int row = rowForPath(d, key, path);
            return row < 0 ? nullptr : uiDetailAt(d.filesPage(key), row);
        };

        // 认出来的那一条，报"点得动"的方式就是**给一个标题**（这一层不知道资源长什么样，
        // 名字是 MainWindow 从反查那一趟带回来的）。
        linked.setFileResource(packPath, QStringLiteral("材质包甲"));
        const auto packLabel = nameLabelFor(linked, QStringLiteral("resourcepack"), packPath);
        const auto unknownLabel =
            nameLabelFor(linked, QStringLiteral("resourcepack"), unknownPath);
        check(packLabel != nullptr && unknownLabel != nullptr,
              "U10N 前提：资源包那一页上那两个文件都列出来了");

        // 名字那一格**两种行都不再是链接**（用户 点名把这一件事挪到那颗按钮上，单击改成
        // 选中）。行是代理画的：名字那一格没有链接色、没有手形光标可言（代理只画正文墨），
        // 剩下的只是那条悬停提示 —— 完整路径（名字是省略过的）。
        const QString plainTip = QDir::toNativeSeparators(unknownPath);
        check(packLabel != nullptr && unknownLabel != nullptr &&
                  packLabel->toolTip() != plainTip &&
                  packLabel->toolTip().contains(QDir::toNativeSeparators(packPath)) &&
                  unknownLabel->toolTip() == plainTip,
              "★★ U10N 名字那一格**不再是链接**（认得出认不出都一样，只挂完整路径那条提示）"
              "—— 它现在只负责「选中这一行」，详情在那颗按钮上",
              packLabel != nullptr ? packLabel->toolTip() : QString());

        // ---- 那颗「详情」：认出来了才点得动 ----
        const auto packDetail = detailButtonFor(linked, QStringLiteral("resourcepack"), packPath);
        const auto unknownDetail =
            detailButtonFor(linked, QStringLiteral("resourcepack"), unknownPath);
        check(packDetail != nullptr && unknownDetail != nullptr,
              "★★ U10N 资源包那一页**每一行尾巴上都有一颗「详情」**（认得出认不出都有"
              "—— 一列下来那三格要对得齐）");
        check(packDetail != nullptr && packDetail->isEnabled() &&
                  packDetail->toolTip() == Dlg::filesResourceTip(QStringLiteral("材质包甲")),
              "★★ U10N 认出来的那一条：那颗「详情」**点得动**，提示就是这一条资源"
              "（没这几样的话用户根本不知道它能点 —— 「看着像一行普通文字」和「点不动」"
              "在屏幕上一模一样）",
              packDetail != nullptr ? packDetail->toolTip() : QString());
        check(unknownDetail != nullptr && !unknownDetail->isEnabled() &&
                  !unknownDetail->toolTip().contains(
                      Dlg::filesResourceTip(QStringLiteral("材质包甲"))),
              "★★ U10N 而**没认出来的那一条那颗按钮是灰的**（看得见、按不动 —— 和"
              "「这一行没有这一格」不一样，一列下来三格要对得齐）",
              unknownDetail != nullptr ? unknownDetail->toolTip() : QString());

        // 行尾那几格的**次序**：**`[详情][开关][删除]`**。
        // 资源包这一页的那颗「启用 / 禁用」**整个退场**了 —— 改名当启停
        // 对资源包已经不成立（`fileKeyCanToggle()` 只留给模组和光影，资源包改走
        // `options.txt` 的启用顺序页，见 U10d），所以这一页的行尾只剩
        // **`[详情][删除]`**。这条断言问的次序还在（详情最左、删除最右），多钉一个
        // "开关这一格**压根没有**"（`uiToggleAt()` 回 nullptr）—— 只钉前两格的话，
        // 开关哪天被顺手加回来照样绿。
        InstanceFilesPage *packPage = linked.filesPage(QStringLiteral("resourcepack"));
        const int packRow = rowForPath(linked, QStringLiteral("resourcepack"), packPath);
        const auto packToggle = packRow < 0 ? nullptr : uiToggleAt(packPage, packRow);
        const auto packDelete = packRow < 0 ? nullptr : uiDeleteAt(packPage, packRow);
        check(packDetail != nullptr && packDelete != nullptr && packToggle == nullptr &&
                  packDetail->geometry().right() < packDelete->geometry().left(),
              "★★ U10N 资源包那一页行尾是 **[详情][删除]**：改名开关退场（资源包走"
              "options.txt 的启用顺序页），详情在最左、删除在最右",
              QStringLiteral("详情 x=%1 开关=%2 删除 x=%3")
                  .arg(packDetail != nullptr ? packDetail->geometry().x() : -1)
                  .arg(packToggle != nullptr ? QStringLiteral("有") : QStringLiteral("无"))
                  .arg(packDelete != nullptr ? packDelete->geometry().x() : -1));

        // ③ 点一下：报的**就是这一条文件的路径**，而且只报一次。
        QStringList reported;
        const QMetaObject::Connection conn = QObject::connect(
            &linked, &Dlg::fileResourceRequested, &linked,
            [&reported](const QString &pageKey, const QString &path) {
                reported.append(pageKey + QStringLiteral(" ") + path);
            });
        if (packDetail != nullptr) {
            packDetail->click();
        }
        check(reported == QStringList{QStringLiteral("resourcepack ") + packPath},
              "★★ U10N 点那颗「详情」：报的**就是这一页 + 这一条文件的路径**（只报一次）"
              " —— 报错页或者报错文件，症状都是「点 A 开出 B」",
              reported.join(QStringLiteral(" | ")));

        // 点**没认出来**的那一颗：一个字节都不该报（它本来就是 enable=false）。
        reported.clear();
        if (unknownDetail != nullptr) {
            unknownDetail->click();
        }
        check(reported.isEmpty(),
              "★★ U10N 点那颗灰的：什么都不报（正确界面就是「按下去没反应」）",
              reported.join(QStringLiteral(" | ")));

        // ④ 模组那一页： 下半句之后它也**接上了** —— 和资源包那一页逐字同一条
        // 路（同一个 setFileResource()、同一个 filesResourceTip()）。判据仍然只有
        // filesNameOpensResource() 一处。
        const QString modTitle = QStringLiteral("模组甲");
        linked.showPage(QStringLiteral("mod"));
        linked.setFileResource(modPath, modTitle);
        const auto modDetail = detailButtonFor(linked, QStringLiteral("mod"), modPath);
        check(modDetail != nullptr && modDetail->isEnabled() &&
                  modDetail->toolTip() == Dlg::filesResourceTip(modTitle),
              "★★ U10N 模组那一页的「详情」**也认得出**（原话："
              "「对于模组，点进去和资源包差不多」）",
              modDetail != nullptr ? modDetail->toolTip() : QString());
        reported.clear();
        if (modDetail != nullptr) {
            modDetail->click();
        }
        check(reported == QStringList{QStringLiteral("mod ") + modPath},
              "★★ U10N 点模组那一颗：报的是「mod + 完整路径」（和资源包那一页一字不差）",
              reported.join(QStringLiteral(" | ")));

        // ⑤ 同一把路径换了内容（大小 / 时间变了 ⇒ 那一趟会拿一个**空标题**过来）：
        // 上一趟记住的资源得作废 —— 那一格要从"点得动"**退回"点不动"**，不然点进去
        // 是另一个资源。
        linked.setFileResource(packPath, QString());
        const auto unlinked =
            detailButtonFor(linked, QStringLiteral("resourcepack"), packPath);
        check(unlinked != nullptr && !unlinked->isEnabled() &&
                  !unlinked->toolTip().contains(
                      Dlg::filesResourceTip(QStringLiteral("材质包甲"))),
              "★★ U10N 后来改回「没认出来」时那一颗要**退回灰的**（空标题不是"
              "「没话说」，是「把上一趟那个作废」—— 留着的话点进去是另一个资源）");
        QObject::disconnect(conn);
    }
    return 0;
}
