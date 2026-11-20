#include "Support.hpp"

#include "UiInstanceFilesTestHelpers.hpp"

// U10S：「编辑」/「加入服务器」/「加入选中的世界」那三颗。
//
// 要求：「在服务器列表那里做一个编辑地址和名称的功能，然后世界和服务器都做一个临时占位
// （加入选中的世界（仅限有这个参数的版本）/服务器）先试试看能不能生效」。
//
// 这一层的活是「**哪一颗在、写着什么、没选中时是什么样子、点下去报的是哪一条**」。
// 「这个版本认不认那两条 `--quickPlay*` 参数」由 MainWindow 现算（`instanceSupportsQuickPlay()`），
// 递进来的是一句**空串** —— 那是"整颗收起来"的意思（判据层本身在 verify 的 ZL 节、
// 真读写 `servers.dat` 那条路在 mw_check 的 T20.8 上）。
//
// 这一段原来住在 `UiContentPagesChecks.cpp` 里、跟着那一大段的对话框夹具与临时目录。
// 分出来以后共用的 `dialog` 按引用传，另外三样走显式形参；`mkfile` 用共享的 `uiMkFile()`
// （四份一样的 lambda 收进 `UiInstanceFilesTestHelpers.hpp`）。
int UiQuickPlayChecks(InstanceSettingsDialog &dialog, const QString &instanceDisplayName,
                      const QString &instanceDirName,
                      const InstanceSettingsDialog::GlobalValues &global) {
    using Dlg = InstanceSettingsDialog;
    const auto showPage = [](Dlg &d, const QString &key) { d.showPage(key); };
    const auto mkfile = [](const QString &path) { uiMkFile(path); };
    // ---- U10S 「编辑」/「加入服务器」/「加入选中的世界」那三颗----
    //
    // 要求：「在服务器列表那里做一个编辑地址和名称的功能，然后世界和
    // 服务器都做一个临时占位（加入选中的世界（仅限有这个参数的版本）/服务器）先试试看
    // 能不能生效」。
    //
    // 这一层的活是「**哪一颗在、写着什么、没选中时是什么样子、点下去报的是哪一条**」。
    // 「这个版本认不认那两条 `--quickPlay*` 参数」由 MainWindow 现算
    // （`instanceSupportsQuickPlay()`），递进来的是一句**空串** —— 那是"整颗收起来"的
    // 意思（判据层本身在 verify 的 ZL 节、真读写 `servers.dat` 那条路在 mw_check 的
    // T20.8 上）。
    {
        // ---- ① 那一页现在叫「结构/原理图」----
        const QStringList shortcuts = Dlg::shortcutTexts();
        const QStringList optionalTexts = Dlg::optionalPageTexts();
        check(!shortcuts.contains(QStringLiteral("结构/原理图")),
              "★ U10S 「结构/原理图」不再在快捷方式那一串里重复摆一遍",
              shortcuts.join(QStringLiteral(" / ")));
        check(optionalTexts.size() > 6 &&
                  optionalTexts.at(6) == QStringLiteral("结构/原理图"),
              "★ U10S 勾选表里那条也是同一个名字（两处不许各叫一个）",
              optionalTexts.size() > 6 ? optionalTexts.at(6) : QStringLiteral("<不够长>"));
        // 改的是**叫法**，不是身份：键没动 ⇒ 用户盘上那份 `settingsPages` 不会因此失效
        // （认不出键的会被丢掉，"勾掉的页"就悄悄回来了）。
        check(Dlg::optionalPageKeys().size() > 6 &&
                  Dlg::optionalPageKeys().at(6) == QStringLiteral("schematic"),
              "★ U10S 键还是 `schematic`（改的只是叫什么）");

        // ---- ② 服务器那一页：一颗「编辑」、一颗「加入服务器」----
        showPage(dialog, QStringLiteral("server"));
        QTreeWidget *tree = dialog.serverTree();
        QPushButton *edit = dialog.serverEditButton();
        QPushButton *joinServer = dialog.serverJoinButton();
        check(tree != nullptr && edit != nullptr && joinServer != nullptr,
              "★ U10S 服务器那一页上「编辑」和「加入服务器」两颗都建出来了");
        if (tree != nullptr && edit != nullptr && joinServer != nullptr) {
            check(edit->objectName() == Dlg::serverEditButtonName() &&
                      edit->text() == Dlg::serverEditText(),
                  "★ U10S 「编辑」挂的是唯一出处那个 objectName 和那几个字",
                  QStringLiteral("%1「%2」").arg(edit->objectName(), edit->text()));
            check(joinServer->objectName() == Dlg::serverJoinButtonName(),
                  "★ U10S 「加入服务器」也挂上了唯一出处那个 objectName",
                  joinServer->objectName());
            // 初值：`setServerJoinText()` 还没被喊过 ⇒ 藏着，而且**身上一个字都没有**
            // （那几个字和它一起递进来 —— 一颗"有字但发不出去"的按钮比它不在更坏；
            // 不支持的版本上 MainWindow 递进来的就是空串）。
            check(joinServer->isHidden() && joinServer->text().isEmpty(),
                  "★ U10S 没接过那句话的时候「加入服务器」藏着、身上也没字",
                  QStringLiteral("hidden=%1「%2」")
                      .arg(joinServer->isHidden() ? 1 : 0).arg(joinServer->text()));

            // 自己摆一份确定的表：一条有名字、一条名字是**空串**（屏幕上写成「（无名）」）。
            // 「隐藏服务器地址」打开：屏幕上是遮过的，才分得出"发出去的是完整地址"。
            QSettings().setValue(QStringLiteral("Look/HideServerAddress"), true);
            dialog.setServerList({qMakePair(QStringLiteral("有名字的"),
                                            QStringLiteral("one.example.com")),
                                  qMakePair(QString(), QStringLiteral("two.example.com"))},
                                 QString());
            QSettings().remove(QStringLiteral("Look/HideServerAddress"));
            check(tree->topLevelItemCount() == 2, "U10S 前提：两行摆好了",
                  QStringLiteral("%1 行").arg(tree->topLevelItemCount()));
            check(tree->topLevelItem(1)->text(0) == Dlg::unnamedMark(),
                  "★ U10S 没名字的那一条**屏幕上**写的是「（无名）」"
                  "（而下面那条要说的是：发出去的不是这三个字）",
                  tree->topLevelItem(1)->text(0));

            QString sentName;
            QString sentAddress;
            int editSent = 0;
            int joinSent = 0;
            QObject::connect(&dialog, &Dlg::editServerRequested, &dialog,
                             [&](const QString &name, const QString &address) {
                                 sentName = name;
                                 sentAddress = address;
                                 ++editSent;
                             });
            QObject::connect(&dialog, &Dlg::joinServerRequested, &dialog,
                             [&](const QString &, const QString &address) {
                                 sentAddress = address;
                                 ++joinSent;
                             });

            // 一条都没选中 ⇒ 两颗都灰着，点了什么都不发（"按钮那一道闸"之外信号里还有
            // 第二道 —— 灰掉只是屏幕上说得清，不是唯一的防线）。
            check(!edit->isEnabled() && !joinServer->isEnabled(),
                  "★ U10S 一条都没选中时那两颗都是灰的");
            check(edit->toolTip() == Dlg::serverSelectFirstHint(),
                  "★ U10S 灰着的时候提示换成了「先选中一行」"
                  "（灰按钮上写着「改它的名字」用户只会以为自己点错了地方）",
                  edit->toolTip());
            edit->click();
            joinServer->click();
            check(editSent == 0 && joinSent == 0,
                  "★ U10S 灰着的时候点它们一发都不发",
                  QStringLiteral("编辑 %1 次、加入 %2 次").arg(editSent).arg(joinSent));

            // 「加入服务器」接上那句文字之后：有名的那一条发的是**原名 + 完整地址**
            // （第 1 栏里写的是遮过的 `o-.e-.c-`，发出去的那一串不许是它）。
            dialog.setServerJoinText(Dlg::serverJoinText(), Dlg::serverJoinTip());
            check(!joinServer->isHidden() &&
                      joinServer->text() == Dlg::serverJoinText(),
                  "★ U10S 接过那句话之后「加入服务器」露出来了、字也挂上了",
                  QStringLiteral("hidden=%1「%2」")
                      .arg(joinServer->isHidden() ? 1 : 0).arg(joinServer->text()));
            check(tree->topLevelItem(0)->text(1) == QStringLiteral("o-.e-.c-"),
                  "U10S 前提：开关打开时，地址那一栏在屏幕上是**遮过**的",
                  tree->topLevelItem(0)->text(1));

            tree->setCurrentItem(tree->topLevelItem(0), 0, QItemSelectionModel::ClearAndSelect);
            check(edit->isEnabled() && joinServer->isEnabled(),
                  "★ U10S 选中一行之后两颗都点得动了");
            check(edit->toolTip() == Dlg::serverEditTip(),
                  "★ U10S 有选中时提示换回本来那句");
            edit->click();
            check(editSent == 1 && sentName == QStringLiteral("有名字的") &&
                      sentAddress == QStringLiteral("one.example.com"),
                  "★★ U10S 「编辑」发出去的是**原名 + 完整地址**"
                  "（地址不是第 1 栏里遮过的那一串 —— 拿着遮过的去写盘就把用户的"
                  "服务器改成了一台不存在的机器）",
                  QStringLiteral("「%1」/「%2」").arg(sentName, sentAddress));

            // 没名字的那一条：屏幕上「（无名）」，发出去的必须是**空串**。
            tree->setCurrentItem(tree->topLevelItem(1), 0, QItemSelectionModel::ClearAndSelect);
            sentName = QStringLiteral("<没发>");
            sentAddress = QStringLiteral("<没发>");
            edit->click();
            check(sentName.isEmpty() && sentAddress == QStringLiteral("two.example.com"),
                  "★★ U10S 名字是空的那一条：屏幕上写「（无名）」，发出去的是**空串**"
                  "（发成屏幕上那三个字的话，用户点一下确定就真多出一台叫「（无名）」的"
                  "服务器）",
                  QStringLiteral("屏幕上「%1」，发出去「%2」")
                      .arg(tree->topLevelItem(1)->text(0), sentName));

            joinServer->click();
            check(joinSent == 1 && sentAddress == QStringLiteral("two.example.com"),
                  "★ U10S 点「加入服务器」带的是这一行的**完整地址**",
                  QStringLiteral("发了 %1 次、「%2」").arg(joinSent).arg(sentAddress));

            // 空串 = 这个版本不认那条参数 ⇒ 收起来（MainWindow 走的就是这一句）。
            joinServer->setEnabled(true);
            dialog.setServerJoinText(QString(), QString());
            check(joinServer->isHidden(),
                  "★ U10S 递空串进来 ⇒ 「加入服务器」当场收起来");
        }

        // ---- ③ 存档那一页顶上那颗「加入选中的世界」----
        //
        // 和服务器那一颗同一套规矩（露不露 / 选中才点得动 / 点下去报的是哪一条），
        // 只是判据换成 `--quickPlaySingleplayer`、报出去的是一条**存档路径**。
        {
            QTemporaryDir worlds;
            const QString saveRoot = worlds.path();
            QDir().mkpath(saveRoot + QStringLiteral("/一个世界"));
            mkfile(saveRoot + QStringLiteral("/一个世界/level.dat"));
            QHash<QString, QString> saveDirs;
            saveDirs.insert(QStringLiteral("save"), saveRoot);
            Dlg saveDialog(instanceDisplayName, instanceDirName, Dlg::Override(), global,
                           QStringList(), true);
            saveDialog.setInstanceDir(saveRoot);
            saveDialog.setFileDirs(saveDirs);
            saveDialog.resize(900, 520);
            showPage(saveDialog, QStringLiteral("save"));

            InstanceFilesPage *saves = saveDialog.filesPage(QStringLiteral("save"));
            QPushButton *joinWorld = saves != nullptr ? saves->joinButton() : nullptr;
            QListWidget *saveList = saves != nullptr ? saves->list() : nullptr;
            check(saves != nullptr && joinWorld != nullptr,
                  "★ U10S 存档那一页拿得到、顶上那颗也建出来了");
            if (saves != nullptr && joinWorld != nullptr && saveList != nullptr) {
                check(joinWorld->objectName() == InstanceFilesPage::joinButtonName(),
                      "★ U10S 那颗挂的是唯一出处里的 objectName",
                      joinWorld->objectName());
                // 没接文字 ⇒ 藏着（露不露只看这一句）。身上那个字是存档页的固定叫法
                // 「加入世界」—— `setWorldLayout()` 在搭这一页时就挂好了，不是"接文字"
                // 那一路写进去的（所以它虽然写着字，用户也看不到；这一点和服务器那一颗
                // 的判据不同，别拿"身上有没有字"当可见性）。
                check(joinWorld->isHidden() &&
                          joinWorld->text() == QStringLiteral("加入世界"),
                      "★ U10S 没接文字时它藏着（身上挂着存档页那档的固定叫法「加入世界」）",
                      QStringLiteral("hidden=%1「%2」")
                          .arg(joinWorld->isHidden() ? 1 : 0).arg(joinWorld->text()));

                // 别的几页上它**也是建着的**（同一个控件），但一定藏着 —— 一颗「加入
                // 选中的世界」出现在模组页上，点下去是拿一个 .jar 的名字去开存档。
                for (const QString &key :
                     {QStringLiteral("mod"), QStringLiteral("resourcepack"),
                      QStringLiteral("datapack"), QStringLiteral("shader"),
                      QStringLiteral("schematic")}) {
                    InstanceFilesPage *other = saveDialog.filesPage(key);
                    check(other == nullptr || other->joinButton() == nullptr ||
                              other->joinButton()->isHidden(),
                          QStringLiteral("★ U10S 「%1」那一页上没有那颗「加入选中的世界」")
                              .arg(key));
                }

                saves->setJoinText(InstanceFilesPage::joinText(),
                                   InstanceFilesPage::joinTip());
                // 存档页那一档的字是**固定的「加入世界」**（`setWorldLayout()` 定的，
                // 见上面那条）：`setJoinText()` 递进来的那句只决定"露不露"和悬停提示，
                // 不换按钮上的字（服务器那一页才按递进来的文字写）。
                check(!joinWorld->isHidden() &&
                          joinWorld->text() == QStringLiteral("加入世界"),
                      "★ U10S 递了文字之后它露出来（这个版本认 `--quickPlaySingleplayer`）",
                      QStringLiteral("hidden=%1「%2」")
                          .arg(joinWorld->isHidden() ? 1 : 0).arg(joinWorld->text()));
                check(saveList->count() == 1 && !joinWorld->isEnabled(),
                      "★ U10S 一行都没选中时它是灰的",
                      QStringLiteral("%1 行 / enabled=%2")
                          .arg(saveList->count())
                          .arg(joinWorld->isEnabled() ? 1 : 0));
                check(joinWorld->toolTip() == InstanceFilesPage::joinSelectFirstHint(),
                      "★ U10S 灰着时提示说的是「先选中一个世界」", joinWorld->toolTip());

                QString sentPath;
                int joinCalls = 0;
                const QMetaObject::Connection link =
                    QObject::connect(saves, &InstanceFilesPage::joinRequested, saves,
                                     [&](const QString &path) {
                                         sentPath = path;
                                         ++joinCalls;
                                     });
                joinWorld->click();
                check(joinCalls == 0, "★ U10S 灰着的时候点了什么都不发");

                if (saveList->count() == 1) {
                    saveList->setCurrentRow(0);
                    // 选中那一发是异步排过来的（`itemSelectionChanged`），转一圈再量。
                    QApplication::processEvents();
                    check(joinWorld->isEnabled(),
                          "★ U10S 选中一个世界之后它点得动了");
                    check(joinWorld->toolTip() == InstanceFilesPage::joinTip(),
                          "★ U10S 有选中时提示换回本来那句");
                    joinWorld->click();
                    check(joinCalls == 1 &&
                              QFileInfo(sentPath).fileName() == QStringLiteral("一个世界"),
                          "★ U10S 点它带出去的是**这个世界那一条路径**"
                          "（MainWindow 从路径末尾那个目录名当 quick play 的参数）",
                          QStringLiteral("发了 %1 次、「%2」").arg(joinCalls).arg(sentPath));
                }

                saves->setJoinText(QString(), QString());
                check(joinWorld->isHidden(),
                      "★ U10S 递空串进来 ⇒ 当场收起来（不支持的版本上就是这个样子）");
                QObject::disconnect(link);
            }
        }
    }
    return 0;
}
