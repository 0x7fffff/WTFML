#include "Support.hpp"
#include <QMenu>
#include <QPointer>
int MwInstanceGroupsChecks(const QDir &root, const QDir &versionsDir) {
    // 直接往沙盒里写 wtfml.folders。产品里写它的是对话框的「确定」；那一步 2026-11-02
    // 起可以驱动了（开页改成非阻塞，见 TGD 那一段），前面这些节仍然直接写文件 ——
    // 它们要摆的是"某个既定状态"，不是"走一遍对话框"。
    {
        const QString groupFile = AppPaths::instanceGroupFilePath();
        QDir().remove(groupFile);
        check(!QFile::exists(groupFile), "T1 前提：沙盒里本来没有 wtfml.folders");

        // 沙盒里有 1.20.1 / 1.18.2 / 26.3 三个实例。26.3 在 S 段被改过名又改回来，
        // 所以它已经带上稳定 id 了，这里不拿它当"按目录名认归属"的例子用。
        // 归属键写的就是**目录名** —— 那是现存老实例在现实里的样子（还没有 instanceId）。
        const QString packId = QStringLiteral("11111111-1111-1111-1111-111111111111");
        const QString modId = QStringLiteral("22222222-2222-2222-2222-222222222222");

        // ---- T1 第一次用这一份 .minecraft：五个默认组种下来，实例各归各位 ----
        // 要求：「把实例分组做个默认（可安装mod，原版，快照版，远古版，
        // 置顶），默认可以删除，组被删掉后版本放到「未分组」这个组里」。
        // 沙盒里三个实例：1.20.1（meta 里写着 loaderName=Forge）、1.18.2、26.3（两个裸的）。
        {
            MainWindow w0;
            w0.show();
            QApplication::processEvents();
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w0, MainWindowUI::instanceListName());
            // 五个默认组都种下去了（下一段断言查文件），但**画出来的只有有人那两个**：
            // 「置顶 / 快照版 / 远古版 / 未分组」里的实例数都是 0 —— 用户 要的
            // 「空白的分组隐藏」。顺序仍然是默认那份的顺序。
            check(tree != nullptr &&
                      groupHeadersOf(tree) ==
                          QStringList({QStringLiteral("可安装mod (1)"),
                                       QStringLiteral("原版 (2)")}),
                  "T1 五个默认组种下去了，但**只有有实例的那两个画出来**（空组隐藏）",
                  groupHeadersOf(tree).join(QLatin1Char('|')));
            check(tree != nullptr && instanceItemsOf(tree).size() == 3,
                  "T1 三个实例一条不少（只是换了摆法）",
                  QString::number(instanceItemsOf(tree).size()));
            check(groupChildrenOf(tree, QStringLiteral("可安装mod (1)")) ==
                      QStringList({QStringLiteral("1.20.1")}),
                  "T1 装了模组加载器的那个进「可安装mod」",
                  groupChildrenOf(tree, QStringLiteral("可安装mod (1)")).join(QLatin1Char('|')));
            check(groupChildrenOf(tree, QStringLiteral("原版 (2)")) ==
                      QStringList({QStringLiteral("1.18.2"), QStringLiteral("26.3")}),
                  "T1 没装加载器的正式版进「原版」",
                  groupChildrenOf(tree, QStringLiteral("原版 (2)")).join(QLatin1Char('|')));

            // 归类是**算**出来的，不往文件里塞归属 —— 用户一动它才算表过态（见产品里
            // isAssigned() 那段）。这条要是红了，说明有人在扫描路径上写盘。
            const QJsonObject root =
                QJsonDocument::fromJson(readBytesAt(AppPaths::instanceGroupFilePath())).object();
            check(root.value(QStringLiteral("members")).toObject().isEmpty(),
                  "T1 自动归类只在界面上，文件里的成员表还是空的");
            const QJsonArray seeded = root.value(QStringLiteral("groups")).toArray();
            QStringList seededNames;
            for (const QJsonValue &value : seeded) {
                seededNames << value.toObject().value(QStringLiteral("name")).toString();
            }
            check(seededNames == QStringList({QStringLiteral("收藏"), QStringLiteral("可安装mod"),
                                              QStringLiteral("原版"), QStringLiteral("快照版"),
                                              QStringLiteral("远古版")}),
                  "T1 五个默认组真的写进了文件（可改名、可删）",
                  seededNames.join(QLatin1Char('|')));
        }

        writeFileAt(groupFile,
                    QByteArrayLiteral(
                        "{\"version\":1,\"groups\":["
                        "{\"id\":\"11111111-1111-1111-1111-111111111111\",\"name\":\"整合包\"},"
                        "{\"id\":\"22222222-2222-2222-2222-222222222222\",\"name\":\"模组\"}],"
                        "\"members\":{"
                        "\"1.20.1\":\"11111111-1111-1111-1111-111111111111\","
                        "\"1.18.2\":\"22222222-2222-2222-2222-222222222222\"}}"));

        // ---- T2 有分组：两个组 + 「未分组」，归属正确的挂在对的组下面 ----
        {
            MainWindow w;
            w.show();
            QApplication::processEvents();
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            check(tree != nullptr && tree->topLevelItemCount() == 3,
                  "T2 三个顶层项：两个组 + 一个虚拟的「未分组」",
                  QString::number(tree != nullptr ? tree->topLevelItemCount() : -1));
            check(groupHeadersOf(tree) ==
                      QStringList({QStringLiteral("整合包 (1)"), QStringLiteral("模组 (1)"),
                                   QStringLiteral("未分组 (1)")}),
                  "T2 组头是「组名 (N)」，顺序照文件里的顺序、未分组排最后",
                  groupHeadersOf(tree).join(QLatin1Char('|')));
            check(instanceItemsOf(tree).size() == 3,
                  "T2 实例一个没多一个没少（分组只改摆法）",
                  QString::number(instanceItemsOf(tree).size()));

            // 归属挂对了没有：每个组头下面挂的必须是那一组里的实例。
            const auto idsUnder = [](QTreeWidgetItem *header) {
                QStringList ids;
                for (int i = 0; i < header->childCount(); ++i) {
                    ids << header->child(i)->data(0, Qt::UserRole).toString();
                }
                return ids;
            };
            check(tree != nullptr && tree->topLevelItemCount() == 3 &&
                      idsUnder(tree->topLevelItem(0)) == QStringList({QStringLiteral("1.20.1")}) &&
                      idsUnder(tree->topLevelItem(1)) == QStringList({QStringLiteral("1.18.2")}) &&
                      idsUnder(tree->topLevelItem(2)) == QStringList({QStringLiteral("26.3")}),
                  "T2 每个实例挂在它自己那一组下面",
                  tree != nullptr && tree->topLevelItem(0) != nullptr
                      ? idsUnder(tree->topLevelItem(0)).join(QLatin1Char('|'))
                      : QString());

            bool headersUnselectable = tree != nullptr;
            for (int i = 0; i < (tree != nullptr ? tree->topLevelItemCount() : 0); ++i) {
                headersUnselectable =
                    headersUnselectable &&
                    !(tree->topLevelItem(i)->flags() & Qt::ItemIsSelectable);
            }
            check(tree != nullptr && headersUnselectable,
                  "T2 三个组头都清掉了 ItemIsSelectable（点它们不会亮）");

            // ---- 点组头：下拉框必须一动不动 ----
            // QTreeWidget::setCurrentItem() 等价于"用户点了这一行"（它发 currentItemChanged，
            // 窗口那条连接就会跑 syncComboToTree）。组头上没有实例 id，所以这里什么都不该发生。
            QComboBox *combo =
                comboByObjectName(&w, MainWindowUI::installedVersionComboName());
            // S 段最后把 26.3 改回了老名字，所以盘上那份"上次启动过的"就是它 —— 开窗
            // 时按它恢复选中，跟下拉框里选中的是谁无关（这条只是给下面那两条做前提）。
            check(combo != nullptr && combo->currentData().toString() == QStringLiteral("26.3"),
                  "T2 前提：开窗时按盘上那份恢复出上次启动过的实例",
                  combo != nullptr ? combo->currentData().toString() : QString());
            const QString comboBefore = combo != nullptr ? combo->currentData().toString() : QString();
            if (tree != nullptr && tree->topLevelItemCount() == 3) {
                // 先点一个**实例**，证明这条路真的通（否则下面那条"没变"可能是因为
                // 压根没接上，而不是因为守卫挡住了）。
                tree->setCurrentItem(tree->topLevelItem(1)->child(0));
                QApplication::processEvents();
                check(combo != nullptr && combo->currentData().toString() ==
                                              QStringLiteral("1.18.2"),
                      "T2 点实例条目：下拉框跟着换到它",
                      combo != nullptr ? combo->currentData().toString() : QString());

                tree->setCurrentItem(tree->topLevelItem(0));
                QApplication::processEvents();
                check(combo != nullptr &&
                          combo->currentData().toString() == QStringLiteral("1.18.2"),
                      "T2 点组头：下拉框一动不动（点一个组的标题不是在挑版本）",
                      combo != nullptr ? combo->currentData().toString() : QString());
                check(comboBefore != QStringLiteral("1.18.2"),
                      "T2 对照：上面那两次点的是两个不同的东西（这条断言防的是测试自己写错）",
                      comboBefore);
            }
        }

        // ---- T3 归属指向一个已经不存在的组 → 当未分组，界面上照样有地方落 ----
        writeFileAt(groupFile,
                    QByteArrayLiteral(
                        "{\"version\":1,\"groups\":[{\"id\":\"g-还在的\",\"name\":\"还在的组\"}],"
                        "\"members\":{\"1.20.1\":\"g-早就没了的\"}}"));
        {
            MainWindow w;
            w.show();
            QApplication::processEvents();
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            check(groupHeadersOf(tree) == QStringList({QStringLiteral("未分组 (3)")}),
                  "T3 悬空的归属当未分组；那个空掉的组（「还在的组」）**不画出来**",
                  groupHeadersOf(tree).join(QLatin1Char('|')));
        }

        // ---- T4 换游戏文件夹：分组跟着换成那一份的 ----
        // 分组文件挂在数据根目录下（不是 versions/ 下），所以换一份 .minecraft 就是
        // 换一套分组；而这件事**不需要任何重置钩子**（store 自己比目录路径）。
        //
        // 关键在验法：必须在**同一个已经开着的窗口**上换目录再让它刷一次。靠"关掉再
        // 开一个窗口"是验不到的 —— 开窗时本来就会按"记住的那一份"重新设一遍 AppPaths，
        // 于是那条路验的其实是 FolderManager 的恢复逻辑（P 段的事），不是这里要验的
        // "没有任何人通知那个 store，它自己就换了一套"。
        {
            const QString otherRoot = root.filePath(QStringLiteral("groups-other-mc"));
            const QDir otherVersions(otherRoot + QStringLiteral("/versions"));
            otherVersions.mkpath(QStringLiteral("."));
            makeInstalledVersion(otherVersions, QStringLiteral("1.7.10"));
            writeFileAt(otherRoot + QStringLiteral("/wtfml.folders"),
                        QByteArrayLiteral(
                            "{\"version\":1,\"groups\":[{\"id\":\"g-别的\",\"name\":\"另一份里的组\"}],"
                            "\"members\":{\"1.7.10\":\"g-别的\"}}"));
            {
                LauncherSettings seed;
                FolderManager folders(&seed);
                check(folders.addFolder(otherRoot), "T4 加上第二份游戏文件夹");
            }

            MainWindow w;
            w.show();
            QApplication::processEvents();
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            check(groupHeadersOf(tree) == QStringList({QStringLiteral("未分组 (3)")}),
                  "T4 前提：这个窗口先把沙盒那份画出来了",
                  groupHeadersOf(tree).join(QLatin1Char('|')));

            AppPaths::setMinecraftDir(otherRoot);
            QMetaObject::invokeMethod(&w, "onVersionsFetched", Qt::DirectConnection,
                                      Q_ARG(QStringList, QStringList({QStringLiteral("1.7.10")})));
            QApplication::processEvents();
            check(groupHeadersOf(tree) == QStringList({QStringLiteral("另一份里的组 (1)")}),
                  "T4 同一个窗口里换了目录，分组自己跟着换了（没有谁去通知那个 store）"
                  "，那一份里「未分组」是空的所以只有一行",
                  groupHeadersOf(tree).join(QLatin1Char('|')));
            check(instanceItemsOf(tree).size() == 1 &&
                      instanceItemsOf(tree).first()->data(0, Qt::UserRole).toString() ==
                          QStringLiteral("1.7.10"),
                  "T4 实例也换成了那一份里的",
                  QString::number(instanceItemsOf(tree).size()));

            AppPaths::setMinecraftDir(QString());  // 回沙盒
            QMetaObject::invokeMethod(&w, "onVersionsFetched", Qt::DirectConnection,
                                      Q_ARG(QStringList, QStringList({QStringLiteral("1.20.1")})));
            QApplication::processEvents();
            check(groupHeadersOf(tree) == QStringList({QStringLiteral("未分组 (3)")}),
                  "T4 切回沙盒，分组又跟着回来了（两个方向都认账）",
                  groupHeadersOf(tree).join(QLatin1Char('|')));

            // 收拾：把第二份从列表里删掉、当前目录退回自带那份。
            {
                LauncherSettings reset;
                FolderManager folders(&reset);
                check(folders.removeFolder(otherRoot), "T4 把第二份删掉");
                check(folders.setCurrentFolder(AppPaths::defaultMinecraftDir()),
                      "T4 当前目录退回自带那份");
            }
            AppPaths::setMinecraftDir(QString());
            QDir(otherRoot).removeRecursively();
            check(!QDir(otherRoot).exists(), "T4 第二份目录删干净了（没攥着文件句柄）");
        }

        // ---- T6 归类的判据：分类跟着实例自己的 meta 走 ----
        //
        // 五个默认组里只有「可安装mod」不按版本分类判（装了加载器就是它），其余三个一一
        // 对应 VersionCategory 的三档。**愚人节版没有对应的默认组**（用户给的那五个里没有
        // 它），它落到「未分组」。
        //
        // NOTE: 三个"分类不对"的实例是**临时造的**，不是改沙盒里那三个已装版本的 meta ——
        // 26.3 在 S 段被改过名又改回来，它的 wtfml.meta 里躺着稳定 id，"改完再改回去"
        // 是一段很容易写漏一行的还原逻辑（漏了就是把 instanceId 抹掉）。造三个目录、
        // 用完删掉，才是没有回头账的写法。
        const QString kTfSnap = QStringLiteral("tf-快照");
        const QString kTfOld = QStringLiteral("tf-远古");
        const QString kApril = QStringLiteral("26w14a");  // 愚人节版的 id，来自那张写死的名单
        {
            makeInstalledVersion(versionsDir, kTfSnap);
            makeVersionMeta(versionsDir, kTfSnap, R"({"type":"snapshot"})");
            makeInstalledVersion(versionsDir, kTfOld);
            makeVersionMeta(versionsDir, kTfOld, R"({"type":"old_beta"})");
            // 愚人节版那条**故意也写一个 snapshot 的 type**：它必须仍然落「未分组」——
            // 愚人节版是按 **id** 判的，和 type 无关（见 VersionCategory.hpp 开头那段）。
            makeInstalledVersion(versionsDir, kApril);
            makeVersionMeta(versionsDir, kApril, R"({"type":"snapshot"})");

            writeFileAt(groupFile,
                        QByteArrayLiteral(
                            "{\"version\":1,\"groups\":["
                            "{\"id\":\"default-pinned\",\"name\":\"置顶\"},"
                            "{\"id\":\"default-moddable\",\"name\":\"可安装mod\"},"
                            "{\"id\":\"default-vanilla\",\"name\":\"原版\"},"
                            "{\"id\":\"default-snapshot\",\"name\":\"快照版\"},"
                            "{\"id\":\"default-old\",\"name\":\"远古版\"}],"
                            "\"members\":{}}"));

            MainWindow w;
            w.show();
            QApplication::processEvents();
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            // 「置顶」那一个默认组一个实例都不会有（用户给的那五个里只有它不自动归类），
            // 所以它**永远画不出来** —— 这一节顺带钉住"空组隐藏"。
            check(groupHeadersOf(tree) ==
                      QStringList({QStringLiteral("可安装mod (1)"), QStringLiteral("原版 (2)"),
                                   QStringLiteral("快照版 (1)"), QStringLiteral("远古版 (1)"),
                                   QStringLiteral("未分组 (1)")}),
                  "T6 六个实例按 meta 里的分类各归各位（空着的「置顶」不画）",
                  groupHeadersOf(tree).join(QLatin1Char('|')));
            check(groupChildrenOf(tree, QStringLiteral("可安装mod (1)")) ==
                      QStringList({QStringLiteral("1.20.1")}),
                  "T6 有加载器的那一条先判（1.20.1 的 meta 里写着 loaderName）",
                  groupChildrenOf(tree, QStringLiteral("可安装mod (1)")).join(QLatin1Char('|')));
            check(groupChildrenOf(tree, QStringLiteral("原版 (2)")) ==
                      QStringList({QStringLiteral("1.18.2"), QStringLiteral("26.3")}),
                  "T6 没写 type 的当正式版（meta 读不到就回正式版）",
                  groupChildrenOf(tree, QStringLiteral("原版 (2)")).join(QLatin1Char('|')));
            check(groupChildrenOf(tree, QStringLiteral("快照版 (1)")) ==
                      QStringList({kTfSnap}),
                  "T6 type=snapshot 的进「快照版」",
                  groupChildrenOf(tree, QStringLiteral("快照版 (1)")).join(QLatin1Char('|')));
            check(groupChildrenOf(tree, QStringLiteral("远古版 (1)")) == QStringList({kTfOld}),
                  "T6 type=old_beta 的进「远古版」",
                  groupChildrenOf(tree, QStringLiteral("远古版 (1)")).join(QLatin1Char('|')));
            check(groupChildrenOf(tree, QStringLiteral("未分组 (1)")) == QStringList({kApril}),
                  "T6 愚人节版按 id 判（type 写的是 snapshot 也不进快照版）→ 未分组",
                  groupChildrenOf(tree, QStringLiteral("未分组 (1)")).join(QLatin1Char('|')));
        }

        // ---- T7 删掉一个默认组：里面的实例落到「未分组」 ----
        //
        // 用户要的就是这条（「组被删掉后版本放到未分组这个组里」）。产品里删组走的是分组
        // 对话框的「确定」（要过 exec() 这个模态循环，offscreen 下驱动不了），所以这里把
        // "删完之后那份文件"直接写进沙盒 —— 那正是对话框点完确定之后盘上的样子。
        //
        // 删的是「可安装mod」和「快照版」这两个**有人在里面**的组：1.20.1 和 tf-快照 于是
        // 落「未分组」，而「原版」「远古版」还在，它们里面的实例一个没动 —— 后半句和前半句
        // 一样重要（"删一个组"不该惊动别的组）。
        {
            writeFileAt(groupFile,
                        QByteArrayLiteral(
                            "{\"version\":1,\"groups\":["
                            "{\"id\":\"default-pinned\",\"name\":\"置顶\"},"
                            "{\"id\":\"default-vanilla\",\"name\":\"原版\"},"
                            "{\"id\":\"default-old\",\"name\":\"远古版\"}],"
                            "\"members\":{}}"));

            {
                MainWindow w;
                w.show();
                QApplication::processEvents();
                QTreeWidget *tree =
                    byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
                check(groupHeadersOf(tree) ==
                          QStringList({QStringLiteral("原版 (2)"), QStringLiteral("远古版 (1)"),
                                       QStringLiteral("未分组 (3)")}),
                      "T7 被删掉的那两个组不再出现（还在但那两个空着的也不画），"
                      "里面的实例落到「未分组」",
                      groupHeadersOf(tree).join(QLatin1Char('|')));
                check(groupChildrenOf(tree, QStringLiteral("未分组 (3)")) ==
                          QStringList({QStringLiteral("1.20.1"), kApril, kTfSnap}),
                      "T7 落进未分组的正好是原本归那两个组的实例",
                      groupChildrenOf(tree, QStringLiteral("未分组 (3)")).join(QLatin1Char('|')));
                check(groupChildrenOf(tree, QStringLiteral("原版 (2)")) ==
                          QStringList({QStringLiteral("1.18.2"), QStringLiteral("26.3")}),
                      "T7 对照：还在的两个组一个实例都没被动",
                      groupChildrenOf(tree, QStringLiteral("原版 (2)")).join(QLatin1Char('|')));
            }

            // 再开一个窗口：它们**不会**被归类拉回去（「可安装mod」这个组已经没了，
            // 1.20.1 认不回来；而 1.18.2 这种"本来就没表过态"的仍然按类型归位）。
            {
                MainWindow w;
                w.show();
                QApplication::processEvents();
                QTreeWidget *tree =
                    byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
                check(groupChildrenOf(tree, QStringLiteral("未分组 (3)")) ==
                          QStringList({QStringLiteral("1.20.1"), kApril, kTfSnap}),
                      "T7 再开一次窗它们还在未分组（没有又自己长回去）",
                      groupChildrenOf(tree, QStringLiteral("未分组 (3)")).join(QLatin1Char('|')));
            }
        }

        // ---- T8 表过态的归属压得住自动归类 ----
        //
        // 两条路，正是 isAssigned() 那个判据存在的全部理由：
        // - 「明确选了未分组」（文件里那个保留值）：1.20.1 装了加载器，按类型该进
        // 「可安装mod」，但用户说"我就想让它待在未分组里" —— 它必须待着；
        // - 「明确选了某个组」：1.18.2 该进「原版」，但用户把它拖进了「远古版」——
        // 它必须在远古版里。
        // 反过来说：没有这一条的话，用户点完「确定」下一次刷新就被拉回去了，表现是
        // "分组点了没用"。
        {
            writeFileAt(
                groupFile,
                QByteArrayLiteral(
                    "{\"version\":1,\"groups\":["
                    "{\"id\":\"default-pinned\",\"name\":\"置顶\"},"
                    "{\"id\":\"default-moddable\",\"name\":\"可安装mod\"},"
                    "{\"id\":\"default-vanilla\",\"name\":\"原版\"},"
                    "{\"id\":\"default-snapshot\",\"name\":\"快照版\"},"
                    "{\"id\":\"default-old\",\"name\":\"远古版\"}],"
                    "\"members\":{\"1.20.1\":\"ungrouped\","
                    "\"1.18.2\":\"default-old\"}}"));

            MainWindow w;
            w.show();
            QApplication::processEvents();
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            check(groupHeadersOf(tree) ==
                      QStringList({QStringLiteral("原版 (1)"), QStringLiteral("快照版 (1)"),
                                   QStringLiteral("远古版 (2)"), QStringLiteral("未分组 (2)")}),
                  "T8 表过态的两条都按用户说的摆，其余三条照旧自动归类"
                  "（空着的「置顶」「可安装mod」不画）",
                  groupHeadersOf(tree).join(QLatin1Char('|')));
            check(groupChildrenOf(tree, QStringLiteral("未分组 (2)")) ==
                      QStringList({QStringLiteral("1.20.1"), kApril}),
                  "T8 明确选了「未分组」的那个不被拉回「可安装mod」",
                  groupChildrenOf(tree, QStringLiteral("未分组 (2)")).join(QLatin1Char('|')));
            check(groupChildrenOf(tree, QStringLiteral("远古版 (2)")) ==
                      QStringList({QStringLiteral("1.18.2"), kTfOld}),
                  "T8 明确归到「远古版」的那个留在远古版（没被按类型拉回原版）",
                  groupChildrenOf(tree, QStringLiteral("远古版 (2)")).join(QLatin1Char('|')));
            // 空组**不画**（要求：「空白的分组隐藏」）。判据用的是
            // groupHeadersOf() 那份**行文字**：没有「可安装mod (0)」这么一行 ——
            // groupChildrenOf() 对不存在的组头也返回空表，拿它判这条会永远绿。
            check(!groupHeadersOf(tree).contains(QStringLiteral("可安装mod (0)")) &&
                      !groupHeadersOf(tree).contains(QStringLiteral("置顶 (0)")) &&
                      groupChildrenOf(tree, QStringLiteral("快照版 (1)")) ==
                          QStringList({kTfSnap}),
                  "T8 空组不画（「可安装mod (0)」「置顶 (0)」都没有那一行）",
                  groupHeadersOf(tree).join(QLatin1Char('|')));

            // 读路径没有副作用：这一趟开窗不该把文件重写一遍。
            const QJsonObject members =
                QJsonDocument::fromJson(readBytesAt(groupFile))
                    .object()
                    .value(QStringLiteral("members"))
                    .toObject();
            check(members.size() == 2 &&
                      members.value(QStringLiteral("1.20.1")).toString() ==
                          QStringLiteral("ungrouped"),
                  "T8 归类只在界面上算，没往文件里塞新条目（保留值原样留着）",
                  QString::number(members.size()));
        }

        // 收拾：三个临时实例和那份分组文件都不能留着 —— 后面的节数实例、以及收尾的
        // 「沙盒回到 3 个版本」都建立在"只有原来那三个、而且没分过组"之上。
        for (const QString &tempId : {kTfSnap, kTfOld, kApril}) {
            QDir(versionsDir.filePath(tempId)).removeRecursively();
            check(!QDir(versionsDir.filePath(tempId)).exists(),
                  QStringLiteral("T9 临时实例 %1 删干净了").arg(tempId));
        }
        QDir().remove(groupFile);
        check(!QFile::exists(groupFile), "T9 沙盒里那份 wtfml.folders 删掉了");

        // ---- T10 组行：点一下展开/收起，而且当场记下来（下次开窗照这样画） ----
        //
        // 要求：「每一个组和版本下载几乎一致，点一下打开/关闭，右侧有个
        // 箭头和齿轮」。这一节验前两条（点一下真的翻、翻完记住），齿轮那一套在 T11～T14。
        //
        // 「展开还是收着」是**组自己的一格**（InstanceGroupStore::Group::expanded，既是
        // 默认也是当前 —— 全文见那边的 NOTE），所以这里验的是"点完那一下盘上是什么"以及
        // "下次开窗照不照它画"。
        writeFileAt(groupFile,
                    QByteArrayLiteral(
                        "{\"version\":1,\"groups\":["
                        "{\"id\":\"g-收着的\",\"name\":\"收着的组\",\"expanded\":false},"
                        "{\"id\":\"g-展开的\",\"name\":\"展开的组\",\"expanded\":true}],"
                        "\"members\":{\"1.20.1\":\"g-收着的\",\"1.18.2\":\"g-展开的\"}}"));
        // 盘上某一组的展开状态（1/0）；没有这一组返回 -1。判据取的是**文件**不是内存：
        // 这一节要验的正是"落盘了没有"（问 store 等于自己验自己）。
        const auto expandedInFile = [&groupFile](const QString &groupId) -> int {
            const QJsonArray array = QJsonDocument::fromJson(readBytesAt(groupFile))
                                         .object()
                                         .value(QStringLiteral("groups"))
                                         .toArray();
            for (const QJsonValue &value : array) {
                const QJsonObject object = value.toObject();
                if (object.value(QStringLiteral("id")).toString() == groupId) {
                    return object.value(QStringLiteral("expanded")).toBool() ? 1 : 0;
                }
            }
            return -1;
        };
        // 点树里的一行（走产品里那条信号路：QTreeWidget::itemClicked → 接线口）。
        //
        // NOTE: **这里不 pump 事件循环**。条目的指针活在"重灌树"之前，而重灌随时可能
        // 插进来（比如版本清单那一发网络回复一到，onVersionsFetched() 就会 refreshInstalled
        // Versions() 重灌这张树，旧条目当场被删）。pump 一下等于给那一发开门，门开之后
        // 手里这两个指针就是悬空的 —— 读它读到的是垃圾（本节曾因此把一条断言读红），
        // 再拿它去 clickTreeRow 更可能撞进"实例行 ⇒ 开设置页"那条嵌套循环，整个测试
        // 卡死在那儿。这条信号是 DirectConnection，处理是同步的，不 pump 也已经是最终态。
        const auto clickTreeRow = [](QTreeWidget *tree, QTreeWidgetItem *item) {
            QMetaObject::invokeMethod(tree, "itemClicked", Qt::DirectConnection,
                                      Q_ARG(QTreeWidgetItem *, item), Q_ARG(int, 0));
        };
        // 按组 id 找回树上那一行。**每一次 pump 之后、再用条目指针之前都要重找** ——
        // 在线版本清单那一发回复一到，`onVersionsFetched()` 就会
        // `refreshInstalledVersions()` → `tree->clear()` 重灌整张树，旧条目当场被删。
        const auto rowOfGroupId = [](QTreeWidget *tree,
                                     const QString &groupId) -> QTreeWidgetItem * {
            if (tree == nullptr) {
                return nullptr;
            }
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                QTreeWidgetItem *item = tree->topLevelItem(i);
                if (item->data(0, MainWindowUI::groupIdRole()).toString() == groupId) {
                    return item;
                }
            }
            return nullptr;
        };
        // 往 viewport 上**真的**发一次鼠标按下。行里那颗齿轮是代理画的（没有控件可点），
        // 产品把它接在视口的事件过滤器上 —— 伪造 itemClicked 正好绕过要验的那一段。
        // 视口坐标 = 条目矩形的坐标系（visualItemRect() 也是视口的坐标）。
        //
        // NOTE: **这里不 pump 事件循环**（和上面 clickTreeRow 同一条教训）。产品处理按下
        // 是同步的（eventFilter 里就把菜单弹出来了），pump 只会给"网络回复 ⇒ 重灌树"
        // 开门；门一开，调用方手里那个条目指针就是野的（实测：读它读到 0 是轻的，
        // 撞上释放过的内存直接 SEGFAULT）。要 pump 的调用方自己 pump，pump 完重找指针。
        const auto pressRowAt = [](QTreeWidget *tree, const QPoint &pos) {
            QWidget *viewport = tree->viewport();
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos),
                              QPointF(viewport->mapToGlobal(pos)), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &press);
        };
        // 切到「实例」页。**量几何之前必须先切**：隐藏着的选项卡里的控件是不排布局的，
        // visualItemRect() 拿到的是一个退化的空矩形（这条 NOTE 在 MainWindowUI 的
        // downloadTabIndex() 那边写着），于是"齿轮在箭头左边"这种相对几何会拿两个空
        // 矩形去比 —— 比出来的结论作不了数。
        const auto showInstanceTab = [](QWidget *w) {
            if (auto *tabs = w->findChild<QTabWidget *>(MainWindowUI::tabWidgetName())) {
                tabs->setCurrentIndex(1);  // 1 = 「实例」页（主页 0 / 下载 2 / 设置 3）
            }
            QApplication::processEvents();
        };
        {
            MainWindow w;
            w.show();
            QApplication::processEvents();
            showInstanceTab(&w);
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            check(tree != nullptr &&
                      groupHeadersOf(tree) ==
                          QStringList({QStringLiteral("收着的组 (1)"),
                                       QStringLiteral("展开的组 (1)"),
                                       QStringLiteral("未分组 (1)")}),
                  "T10 前提：两个组各一个实例，26.3 没分过组所以落在「未分组」",
                  groupHeadersOf(tree).join(QLatin1Char('|')));
            check(expandedInFile(QStringLiteral("g-收着的")) == 0 &&
                      expandedInFile(QStringLiteral("g-展开的")) == 1,
                  "T10 前提：盘上那两格就是 0 / 1（下面读的就是这两格）",
                  QString::number(expandedInFile(QStringLiteral("g-收着的"))));
            QTreeWidgetItem *folded = tree != nullptr ? tree->topLevelItem(0) : nullptr;
            QTreeWidgetItem *opened = tree != nullptr ? tree->topLevelItem(1) : nullptr;
            check(folded != nullptr && !folded->isExpanded(),
                  "T10 文件里写着收着的那一组，画出来就是收着的");
            check(opened != nullptr && opened->isExpanded(),
                  "T10 同一次里另一组照样是展开的（展开/收着是每一组各自一格，"
                  "不是一个全局开关）");

            clickTreeRow(tree, folded);
            check(folded != nullptr && folded->isExpanded(),
                  "T10 点一下收着的组头：它展开了");
            check(expandedInFile(QStringLiteral("g-收着的")) == 1,
                  "T10 而且当场记下来了（盘上那一格变成 1）—— 不用等谁按确定");
            check(expandedInFile(QStringLiteral("g-展开的")) == 1,
                  "T10 只动点的那一组（另一组那一格原样）");

            clickTreeRow(tree, folded);
            check(folded != nullptr && !folded->isExpanded(), "T10 再点一下：收回去");
            check(expandedInFile(QStringLiteral("g-收着的")) == 0, "T10 盘上跟着变回 0");

            // "这一行是组头"的唯一判据就是 rowGroupGearRole（能不能开/关、行尾有没有齿轮
            // 都靠它）。它同时也是"点得快时补不补那一发"的判据（见 RowActivation）。
            //
            // NOTE: 下面开关设置页那一趟会跑嵌套事件循环，期间网络回复可能重灌这张树。
            // 所以每用一次指针之前都从树上现取（行序是文件里那三组），不复用旧指针。
            folded = tree != nullptr ? tree->topLevelItem(0) : nullptr;
            opened = tree != nullptr ? tree->topLevelItem(1) : nullptr;
            check(folded != nullptr &&
                      folded->data(0, MainWindowUI::rowGroupGearRole()).toBool() &&
                      folded->childCount() > 0 &&
                      !folded->child(0)->data(0, MainWindowUI::rowGroupGearRole()).toBool(),
                  "T10 组头挂着 rowGroupGearRole、它底下的实例不挂（点实例那一行不是开关）");
            if (opened != nullptr && opened->childCount() > 0) {
                // 点实例那一行**现在开的是那一页「实例设置」**（要求
                // 「直接点空白区域（和左边图标版本名）进入实例设置」）。那一页现在
                // **不阻塞**（`showPage()`），所以顺序是：先点、再叫助手读页眉 + 按返回。
                // 反过来（先排定时器再点）的话，助手醒过来时页还没开，读到的页眉是空的。
                QString pageTitle;
                clickTreeRow(tree, opened->child(0));
                closeInstanceSettingsPage(&pageTitle);
                check(pageTitle.contains(QStringLiteral("实例设置")),
                      "T10 点实例那一行：开的是实例设置那一页（下面那条「那一组没被翻过来」"
                      "是在这一页关掉之后量的）",
                      pageTitle);
            }
            opened = tree != nullptr ? tree->topLevelItem(1) : nullptr;
            check(opened != nullptr && opened->isExpanded() &&
                      expandedInFile(QStringLiteral("g-展开的")) == 1,
                  "T10 点组里的实例：那一组不会被顺手翻过来（开关只挂在组头上）");
        }
        // 关掉再开一个窗：状态是从文件里读回来的（不是那个窗口的遗产）。
        {
            MainWindow w;
            w.show();
            QApplication::processEvents();
            showInstanceTab(&w);
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            check(tree != nullptr && tree->topLevelItemCount() > 0 &&
                      tree->topLevelItem(0) != nullptr && !tree->topLevelItem(0)->isExpanded(),
                  "T10 关掉再开：收着的那一组还是收着的（这个状态是留得住的那种）");
        }

        // ---- T11 组行行尾那颗齿轮：几何、真的点得中、那一下不会翻展开 ----
        //
        // 齿轮是**代理画的**（行里没有控件），认下那一下的是 MainWindow::eventFilter，
        // 判据两边共用 InstanceGroupDelegate::gearRect()。所以这里往 viewport 上真的发
        // 一次鼠标按下（不是去点某个按钮）—— 走的就是产品里那条路。
        {
            MainWindow w;
            w.show();
            QApplication::processEvents();
            showInstanceTab(&w);
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            QTreeWidgetItem *opened = tree != nullptr ? tree->topLevelItem(1) : nullptr;
            check(opened != nullptr && opened->isExpanded(),
                  "T11 前提：拿展开着的那一组试（翻了没有才看得出来）");
            const QRect rowRect = opened != nullptr ? tree->visualItemRect(opened) : QRect();
            // 量的是**画出来的**那两圈（gearGlyphRect / chevronRect），不是命中格：
            // 命中格刻意比画出来的大一圈（齿轮那个四周各留 3 px 手感余量），行矮的时候
            // 它还会上下冒出去 —— 那是"点得到的那一圈"，本来就允许比行高（见 .hpp）。
            // 屏幕上看得见的是这个，所以"齿轮被上下各切掉一条"这件事得拿它来判。
            const QRect gear = InstanceGroupDelegate::gearGlyphRect(rowRect);
            const QRect chevron = InstanceGroupDelegate::chevronRect(rowRect);
            check(!gear.isEmpty() && !chevron.isEmpty() && rowRect.contains(gear) &&
                      rowRect.contains(chevron) && gear.right() < chevron.left(),
                  "T11 画出来的齿轮和箭头都装在行里（上下沿都没被切），齿轮在箭头左边"
                  "（用户说的「右侧有个箭头和齿轮」）",
                  QStringLiteral("齿轮 %1~%2 / 箭头 %3~%4 / 行 %5~%6 高 %7")
                      .arg(gear.left())
                      .arg(gear.right())
                      .arg(chevron.left())
                      .arg(chevron.right())
                      .arg(rowRect.left())
                      .arg(rowRect.right())
                      .arg(rowRect.height()));

            pressRowAt(tree, gear.center());  // = 画出来那颗齿轮的正中心
            // 按下这一发里产品会开菜单（同步），但重灌树也可能刚好在别的事件里发生 ——
            // 用之前按 id 重找一次，别拿"按下之前"那个指针去读。
            opened = rowOfGroupId(tree, QStringLiteral("g-展开的"));
            QWidget *menu = tree->findChild<QWidget *>(MainWindowUI::groupMenuName());
            check(menu != nullptr, "T11 点那一格齿轮：菜单弹出来了（那一格真的是齿轮）");
            check(opened != nullptr && opened->isExpanded(),
                  "T11 而且那一下被齿轮吃掉了 —— 这一组没被顺手展开/收起",
                  QStringLiteral("isExpanded=%1 / 组行 %2")
                      .arg(opened != nullptr ? (opened->isExpanded() ? 1 : 0) : -1)
                      .arg(tree->topLevelItemCount()));
            const auto actionButton = [menu](int action) -> QPushButton * {
                return menu != nullptr
                           ? menu->findChild<QPushButton *>(
                                 MainWindowUI::groupMenuActionName(action))
                           : nullptr;
            };
            check(actionButton(MainWindowUI::GroupActionMoveInto) != nullptr &&
                      actionButton(MainWindowUI::GroupActionClearMembers) != nullptr &&
                      actionButton(MainWindowUI::GroupActionRename) != nullptr &&
                      actionButton(MainWindowUI::GroupActionToggleExpand) != nullptr &&
                      actionButton(MainWindowUI::GroupActionDelete) != nullptr,
                  "T11 一个真组那一档：五项都在（移入 / 移出 / 重命名 / 展开 / 删除）");
            check(actionButton(MainWindowUI::GroupActionToggleExpand) != nullptr &&
                      actionButton(MainWindowUI::GroupActionToggleExpand)
                          ->text()
                          .endsWith(QStringLiteral("开")),
                  "T11 「默认展开」那一项的字**带着状态**（这一组此刻是展开的）",
                  actionButton(MainWindowUI::GroupActionToggleExpand) != nullptr
                      ? actionButton(MainWindowUI::GroupActionToggleExpand)->text()
                      : QString());

            // 菜单里那一项：翻状态 + 记下来。和点组头那一下走的是同一格（expanded），
            // 所以两条路必须得出同一个结果。
            if (QPushButton *toggle = actionButton(MainWindowUI::GroupActionToggleExpand)) {
                toggle->click();
            }
            QApplication::processEvents();
            // 那块框是 WA_DeleteOnClose，close() 只排了个 deleteLater —— 催一下，
            // 免得下面按 objectName 又捞到它（和 P2 段那几块菜单同一条）。
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            // 上面两发都会让事件循环转一圈（延时那一发要等它转起来才派发），所以条目
            // 指针按 id 重找一次再用。
            QTreeWidgetItem *afterToggle = rowOfGroupId(tree, QStringLiteral("g-展开的"));
            check(afterToggle != nullptr && !afterToggle->isExpanded(),
                  "T11 菜单里那一项点了：这一组当场收起来（不是只改了下次打开的默认值）");
            check(expandedInFile(QStringLiteral("g-展开的")) == 0,
                  "T11 而且记进文件里了（和点组头那一下写的是同一格）");
        }

        // ---- T12 「空白的分组隐藏」之后，往空组里放第一个实例的那条路 ----
        //
        // 这是"空组藏起来"带来的唯一一个真问题：一个实例都没有的组在树上是**看不见的**，
        // 所以点不到它、也没法往里拖东西 —— 它和"把实例放进某个组"这件事之间必须还留着
        // 一条路。那条路就是齿轮菜单第一项弹出的**第二个框**：列的是**全部**分组（含藏着的
        // 那些、含虚拟的「未分组」）。T13、T14 接着这个窗口往下验另外两件事。
        writeFileAt(groupFile,
                    QByteArrayLiteral(
                        "{\"version\":1,\"groups\":["
                        "{\"id\":\"g-有人的\",\"name\":\"有人的组\"},"
                        "{\"id\":\"g-空着的\",\"name\":\"空着的组\"}],"
                        "\"members\":{\"1.20.1\":\"g-有人的\"}}"));
        {
            MainWindow w;
            w.show();
            QApplication::processEvents();
            showInstanceTab(&w);
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            check(groupHeadersOf(tree) == QStringList({QStringLiteral("有人的组 (1)"),
                                                       QStringLiteral("未分组 (2)")}),
                  "T12 前提：一个实例都没有的「空着的组」在树上没有那一行",
                  groupHeadersOf(tree).join(QLatin1Char('|')));

            // 选一个**不在**「有人的组」里的实例（挑目标框里那一格灰不灰由它决定）。
            QComboBox *combo = comboByObjectName(&w, MainWindowUI::installedVersionComboName());
            if (combo != nullptr) {
                const int index = combo->findData(QStringLiteral("1.18.2"));
                if (index >= 0) {
                    combo->setCurrentIndex(index);
                }
            }
            QApplication::processEvents();
            check(combo != nullptr && combo->currentData().toString() == QStringLiteral("1.18.2"),
                  "T12 前提：当前选中「1.18.2」（此刻它在未分组里）",
                  combo != nullptr ? combo->currentData().toString() : QString());

            QMetaObject::invokeMethod(&w, "onGroupActionRequested", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("g-有人的")),
                                      Q_ARG(int, MainWindowUI::GroupActionMoveInto));
            QApplication::processEvents();
            QWidget *picker = tree->findChild<QWidget *>(MainWindowUI::groupMenuName());
            const auto pickRow = [picker](int i) -> QPushButton * {
                return picker != nullptr
                           ? picker->findChild<QPushButton *>(MainWindowUI::groupPickRowName(i))
                           : nullptr;
            };
            check(pickRow(0) != nullptr && pickRow(1) != nullptr && pickRow(2) != nullptr &&
                      pickRow(3) == nullptr,
                  "T12 挑目标那个框：三行 —— 两个组 + 未分组（藏着的那个也在）");
            check(pickRow(0) != nullptr &&
                      pickRow(0)->text() ==
                          MainWindowUI::groupItemText(QStringLiteral("有人的组"), 1) &&
                      pickRow(1) != nullptr &&
                      pickRow(1)->text() ==
                          MainWindowUI::groupItemText(QStringLiteral("空着的组"), 0) &&
                      pickRow(2) != nullptr &&
                      pickRow(2)->text() ==
                          MainWindowUI::groupItemText(QStringLiteral("未分组"), 2),
                  "T12 那个「空着的组 (0)」正是树上画不出来的那一组 —— "
                  "把实例放进一个空组只有这一个入口",
                  pickRow(1) != nullptr ? pickRow(1)->text() : QString());
            check(pickRow(0) != nullptr && pickRow(0)->isEnabled() && pickRow(1) != nullptr &&
                      pickRow(1)->isEnabled() && pickRow(2) != nullptr &&
                      !pickRow(2)->isEnabled(),
                  "T12 选中的实例此刻就在未分组里 → 「未分组」那一格灰着"
                  "（点它等于什么都没做）");

            if (QPushButton *target = pickRow(1)) {
                target->click();
            }
            QApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

            QJsonObject members = QJsonDocument::fromJson(readBytesAt(groupFile))
                                      .object()
                                      .value(QStringLiteral("members"))
                                      .toObject();
            check(members.value(QStringLiteral("1.18.2")).toString() ==
                      QStringLiteral("g-空着的"),
                  "T12 点「空着的组」：那个实例的归属真的写成了它",
                  members.value(QStringLiteral("1.18.2")).toString());
            check(groupHeadersOf(tree) ==
                      QStringList({QStringLiteral("有人的组 (1)"),
                                   QStringLiteral("空着的组 (1)"),
                                   QStringLiteral("未分组 (1)")}),
                  "T12 那个组从「藏着的」变成画出来的了（它有实例了），"
                  "而未分组的数字从 2 掉回 1（搬走了一个）",
                  groupHeadersOf(tree).join(QLatin1Char('|')));

            // ---- T13 「把这一组里的实例都移出分组」：组留着、实例一个不删 ----
            //
            // 这句话的难点全在"写下去的那一条是什么"：移出必须写成**明确选了未分组**那个
            // 保留值，否则那些实例（尤其是按版本类型该自动归位的）下一次刷新又被拉回来 ——
            // 表现就是"点了移出，可它们还在里面"。
            QMetaObject::invokeMethod(&w, "clearGroupMembers", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("g-有人的")),
                                      Q_ARG(QString, QStringLiteral("有人的组")));
            QApplication::processEvents();
            members = QJsonDocument::fromJson(readBytesAt(groupFile))
                          .object()
                          .value(QStringLiteral("members"))
                          .toObject();
            check(members.value(QStringLiteral("1.20.1")).toString() ==
                      QStringLiteral("ungrouped"),
                  "T13 那一条写成的是「明确选了未分组」那个保留值（不是把这一条删掉）",
                  members.value(QStringLiteral("1.20.1")).toString());
            check(members.value(QStringLiteral("1.18.2")).toString() ==
                      QStringLiteral("g-空着的"),
                  "T13 只动这一组里的实例（别的组那一条原样）");
            check(groupHeadersOf(tree) == QStringList({QStringLiteral("空着的组 (1)"),
                                                       QStringLiteral("未分组 (2)")}),
                  "T13 「有人的组」空了所以那一行没了，里面的实例落到「未分组」",
                  groupHeadersOf(tree).join(QLatin1Char('|')));
            QStringList groupNames;
            for (const QJsonValue &value : QJsonDocument::fromJson(readBytesAt(groupFile))
                                               .object()
                                               .value(QStringLiteral("groups"))
                                               .toArray()) {
                groupNames << value.toObject().value(QStringLiteral("name")).toString();
            }
            check(groupNames.contains(QStringLiteral("有人的组")),
                  "T13 组本身还在文件里（移出分组不是删组）", groupNames.join(QLatin1Char('|')));

            // ---- T14 虚拟的「未分组」那一档：只剩「把选中的实例移入分组…」 ----
            //
            // 它不是真的组：不能改名、不能删、不能排序、也没有"默认展开"这一说。留着第一项
            // 是因为**它是把实例放进一个空组的最后一条路**（那一组在树上是藏着的）。
            QTreeWidgetItem *rest = tree != nullptr ? tree->topLevelItem(1) : nullptr;
            check(rest != nullptr && rest->text(0) == QStringLiteral("未分组 (2)") &&
                      rest->data(0, MainWindowUI::rowGroupGearRole()).toBool(),
                  "T14 前提：那一行就是「未分组」，它照样挂着齿轮",
                  rest != nullptr ? rest->text(0) : QString());
pressRowAt(tree, InstanceGroupDelegate::gearGlyphRect(tree->visualItemRect(rest)).center());
            QWidget *restMenu = tree->findChild<QWidget *>(MainWindowUI::groupMenuName());
            const auto restAction = [restMenu](int action) -> QPushButton * {
                return restMenu != nullptr
                           ? restMenu->findChild<QPushButton *>(
                                 MainWindowUI::groupMenuActionName(action))
                           : nullptr;
            };
            check(restAction(MainWindowUI::GroupActionMoveInto) != nullptr,
                  "T14 那一档有第一项（「把选中的实例移入分组…」）");
            check(restAction(MainWindowUI::GroupActionClearMembers) == nullptr &&
                      restAction(MainWindowUI::GroupActionRename) == nullptr &&
                      restAction(MainWindowUI::GroupActionToggleExpand) == nullptr &&
                      restAction(MainWindowUI::GroupActionDelete) == nullptr,
                  "T14 别的四项**根本不显示**（不是灰着：给未分组改名这件事没有"
                  "「为什么不能」可说）");
            // 就是这一项把上面那条路接通：点它弹的正是挑目标那个框，而且三行一个不少。
            if (QPushButton *moveInto = restAction(MainWindowUI::GroupActionMoveInto)) {
                moveInto->click();
            }
            QApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QWidget *restPicker = tree->findChild<QWidget *>(MainWindowUI::groupMenuName());
            check(restPicker != nullptr &&
                      restPicker->findChild<QPushButton *>(MainWindowUI::groupPickRowName(0)) !=
                          nullptr &&
                      restPicker->findChild<QPushButton *>(MainWindowUI::groupPickRowName(2)) !=
                          nullptr &&
                      restPicker->findChild<QPushButton *>(MainWindowUI::groupPickRowName(3)) ==
                          nullptr,
                  "T14 点那一项弹的是挑目标那个框，三个组一个不少"
                  "（第 0 行就是那个在树上藏着的空组）");
        }

        // ---- T16 那两颗「上移 / 下移」**不在这棵树里**（ 傍晚搬走的） ----
        //
        // 要求：「首先这个点击一下就同时展开和调整了，其次这个箭头不应该在这里，而应该在
        // 分组设置 里面，这里的删了，去分组设置里面加上，设置那个框下方的上移下移的按钮就
        // 删了」。
        {
            // 第四个实例：三个真组 + 一个「未分组」同时在树上，才排得出"顺序变没变"这件事
            // —— 少了谁，"第 0 行是哪个组"就分不出是没动还是动了。
            const QString kSortTemp = QStringLiteral("tf-排序");
            makeInstalledVersion(versionsDir, kSortTemp);
            makeVersionMeta(versionsDir, kSortTemp, R"({"type":"snapshot"})");

            const QString kSortA = QStringLiteral("g-排序一");
            const QString kSortEmpty = QStringLiteral("g-空组");
            const QString kSortB = QStringLiteral("g-排序二");
            const QString kSortC = QStringLiteral("g-排序三");
            // 文件里的顺序：一 / **空组** / 二 / 三（空组夹在中间是故意的：它藏在树上不占行，
            // 而它所在的**位置**是"顺序动没动"最容易被看漏的那一格）。「排序一」显式写
            // expanded:true —— 下面那一下点完要能从**文件**上看出它翻了（缺这一格的话读出来
            // 恒是 0，翻了也看不出来）。
            writeFileAt(groupFile,
                        QByteArray(R"({"version":1,"groups":[)"
                                   R"({"id":"g-排序一","name":"排序一","expanded":true},)"
                                   R"({"id":"g-空组","name":"空组"},)"
                                   R"({"id":"g-排序二","name":"排序二"},)"
                                   R"({"id":"g-排序三","name":"排序三"}],)"
                                   R"("members":{"1.20.1":"g-排序一","1.18.2":"g-排序二",)"
                                   R"("tf-排序":"g-排序三"}})"));

            // 盘上组的顺序（读的是**文件**：这一节验的正是"盘上动了没有"，问 store 等于自己
            // 验自己）。
            const auto groupOrderInFile = [&groupFile]() -> QStringList {
                const QJsonArray array = QJsonDocument::fromJson(readBytesAt(groupFile))
                                             .object()
                                             .value(QStringLiteral("groups"))
                                             .toArray();
                QStringList ids;
                for (const QJsonValue &value : array) {
                    ids.append(value.toObject().value(QStringLiteral("id")).toString());
                }
                return ids;
            };
            // 这张树每重灌一次（`refreshInstalledVersions()` → `tree->clear()`）就发一次
            // modelReset。下面那一下点击的**两发之间不能有事件循环**：中间一转，
            // 在线版本清单那一发网络回复就可能到，它一到就重灌整张树，视图手里的
            // pressedIndex 当场作废、`itemClicked` 发不出来（症状就是"点了一下没翻"）。
            // 量这三个数是为了红的时候一眼分出"产品没翻"和"点击被重灌拆开了"。
            int treeResets = 0;
            // 往 viewport 上真的发一次鼠标事件。@param type Press / Release。
            // NOTE: 修饰键必须**挂在这一发事件上**（产品是从 `mouse->modifiers()` 取 Shift
            // 的）—— 走 QApplication::sendEvent 时合成事件里带的就是它。
            // NOTE: **这里不 pump**（和上面 pressRowAt、T17 的 clickRowAt 同一条）：两发
            // 要落在同一个事件循环回合里，pump 等于把"按下、松开"拆开交给网络回复。
            const auto sendRowMouse = [](QTreeWidget *tree, QEvent::Type type, const QPoint &pos,
                                         Qt::KeyboardModifiers mods) {
                QWidget *viewport = tree->viewport();
                QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)),
                                  Qt::LeftButton, Qt::LeftButton, mods);
                QApplication::sendEvent(viewport, &event);
            };

            MainWindow w;
            w.show();
            QApplication::processEvents();
            showInstanceTab(&w);
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            // 点这一行到底有没有走到"激活"那一层：QTreeWidget::itemClicked 是产品那条
            // 信号路的入口（RowActivation 接的就是它）。红的时候这个数能分出"视图没发"
            // 和"发了、产品没翻"。
            int itemClickedHits = 0;
            if (tree != nullptr) {
                QObject::connect(tree, &QTreeWidget::itemClicked, tree,
                                 [&itemClickedHits](QTreeWidgetItem *, int) {
                                     ++itemClickedHits;
                                 });
                QObject::connect(tree->model(), &QAbstractItemModel::modelReset, tree,
                                 [&treeResets]() { ++treeResets; });
            }
            check(tree != nullptr &&
                      groupHeadersOf(tree) ==
                          QStringList({QStringLiteral("排序一 (1)"), QStringLiteral("排序二 (1)"),
                                       QStringLiteral("排序三 (1)"),
                                       QStringLiteral("未分组 (1)")}),
                  "T16 前提：三个真组 + 未分组，那个空组藏在中间（不画）",
                  groupHeadersOf(tree).join(QLatin1Char('|')));
            check(tree != nullptr && tree->topLevelItemCount() == 4,
                  "T16 前提：树上一共四行（藏起来的空组不占行）",
                  QString::number(tree != nullptr ? tree->topLevelItemCount() : -1));

            if (tree != nullptr) {
                tree->scrollToTop();
                QApplication::processEvents();
            }
            QTreeWidgetItem *rowA = tree != nullptr ? tree->topLevelItem(0) : nullptr;
            QTreeWidgetItem *rowB = tree != nullptr ? tree->topLevelItem(1) : nullptr;
            QTreeWidgetItem *rowC = tree != nullptr ? tree->topLevelItem(2) : nullptr;
            QTreeWidgetItem *rowRest = tree != nullptr ? tree->topLevelItem(3) : nullptr;

            // 那两颗三角原来在条目上留了一格"这一行画哪一颗"的掩码（`Qt::UserRole + 12`，
            // 见 MainWindowUI::rowGroupGearRole() 下面那段 NOTE）。搬走之后那一格就该整个
            // 空着 —— 断言它空着，是因为"能不能挪"这件事现在**根本没有角色位**：那张表里
            // 行序就是组序、一个别的行都没有，算得出来（见 ui/GroupListDelegate 文件头）。
            // 留着旧值不会有任何症状，只会让下一个加角色的人以为这一格有主了。
            const auto noMoveRole = [](QTreeWidgetItem *item) {
                return item != nullptr && !item->data(0, Qt::UserRole + 12).isValid();
            };
            // 这个号码**被实例行拿去用了** —— MainWindowUI::rowPinRole() 也是
            // `Qt::UserRole + 12`，装的是"这一条还能不能置顶"（能不能画那颗上箭头，真假
            // 由 T17 验）。号码复用是故意的：那个掩码搬走之后一个使用者都没有了。所以这条
            // 断言现在分两半：**组行**那一格必须整个空着、**实例行**那一格必须有值。
            check(noMoveRole(rowA) && noMoveRole(rowB) && noMoveRole(rowC) && noMoveRole(rowRest) &&
                      rowA != nullptr && rowA->childCount() > 0 &&
                      rowA->child(0)->data(0, MainWindowUI::rowPinRole()).isValid(),
                  "★ 组行上不再存「这一行画哪一颗箭头」那个角色位了（那一格在组行上整个空着）"
                  "—— 能不能挪是「分组设置…」那张表按行序算出来的，这棵树上没有位置这一说"
                  "（实例那几行用的是同一个号码，装的是「还能不能置顶」）",
                  QStringLiteral("一=%1 二=%2 三=%3 未分组=%4 实例那一格=%5")
                      .arg(noMoveRole(rowA) ? 0 : 1)
                      .arg(noMoveRole(rowB) ? 0 : 1)
                      .arg(noMoveRole(rowC) ? 0 : 1)
                      .arg(noMoveRole(rowRest) ? 0 : 1)
                      .arg(rowA != nullptr && rowA->childCount() > 0
                               ? (rowA->child(0)->data(0, MainWindowUI::rowPinRole()).isValid()
                                      ? QStringLiteral("有")
                                      : QStringLiteral("空"))
                               : QStringLiteral("没那一行")));

            // 行高：组行和实例行都跟版本下载那棵树一样高（要求「每个版本啥的上下高一点，
            // 像下载页那样」）—— 这一条是端到端的：真树、真布局，不是代理那个 sizeHint。
            // 起这一页有自己的常数（instanceTreeRowHeight()，要求「实例列表里
            // 每一个之间留一点空的距离，每一行高一点，图标大一点」），版本树那个 46 一个字没动。
            check(rowA != nullptr && rowA->childCount() > 0 &&
                      tree->visualItemRect(rowA).height() ==
                          InstanceGroupDelegate::groupRowHeight() &&
                      tree->visualItemRect(rowA->child(0)).height() ==
                          ListRowDelegate::rowHeight(),
                  "T16 真树里组头 36、实例行 60（UI 7b 设计稿 manage 的两档）",
                  QStringLiteral("组 %1 / 实例 %2 / 常数 %3")
                      .arg(rowA != nullptr ? tree->visualItemRect(rowA).height() : -1)
                      .arg(rowA != nullptr && rowA->childCount() > 0
                               ? tree->visualItemRect(rowA->child(0)).height()
                               : -1)
                      .arg(ListRowDelegate::rowHeight()));

            check(groupOrderInFile() == QStringList({kSortA, kSortEmpty, kSortB, kSortC}) &&
                      expandedInFile(kSortA) == 1,
                  "T16 前提：盘上的顺序是一 / 空组 / 二 / 三，而且「排序一」写着展开",
                  QStringLiteral("%1（排序一 expanded=%2）")
                      .arg(groupOrderInFile().join(QLatin1Char('|')))
                      .arg(expandedInFile(kSortA)));

            // 当年那两颗三角的命中格：齿轮命中格**左边**、中间留 6 px 的那两块（边长
            // versionTreeArrowSize() + 6，从右往左排 下移 / 上移）。这一节要的就是这个坐标
            // 本身 —— 它现在什么都不是，点上去只该办这一行本来那件事。
            const int oldArrowBox = MainWindowUI::versionTreeArrowSize() + 6;
            const QRect firstRow =
                (tree != nullptr && rowA != nullptr) ? tree->visualItemRect(rowA) : QRect();
            const QPoint oldArrowPos(
                InstanceGroupDelegate::gearRect(firstRow).left() - 6 - oldArrowBox / 2,
                firstRow.center().y());
            check(firstRow.width() > 0 && firstRow.contains(oldArrowPos) &&
                      !InstanceGroupDelegate::gearRect(firstRow).contains(oldArrowPos) &&
                      oldArrowPos.x() > firstRow.left() + 8,
                  "T16 前提：要点的那个点落在那一行里、在齿轮**左边**（这一格不是齿轮那一格，也不是行首），也不是行首给文字留的那一截",
                  QStringLiteral("点 (%1,%2) / 行 %3,%4 %5x%6 / 齿轮命中格左 %7")
                      .arg(oldArrowPos.x())
                      .arg(oldArrowPos.y())
                      .arg(firstRow.left())
                      .arg(firstRow.top())
                      .arg(firstRow.width())
                      .arg(firstRow.height())
                      .arg(InstanceGroupDelegate::gearRect(firstRow).left()));

            // 发那一发：按下 + 松开。组行是**开关行**，这一对手势由 RowActivation 那个
            // 继电器自己认（展开动画那 200 ms 里视图会把按下整个吞掉，见 RowActivation.hpp），
            // 所以这里不经过 itemClicked、也不依赖视图的点击状态。两发之间**不 pump**。
            const int resetsBeforeClick = treeResets;
            sendRowMouse(tree, QEvent::MouseButtonPress, oldArrowPos, Qt::NoModifier);
            const int resetsBetweenPressAndRelease = treeResets;
            sendRowMouse(tree, QEvent::MouseButtonRelease, oldArrowPos, Qt::NoModifier);
            QApplication::processEvents();
            check(groupOrderInFile() == QStringList({kSortA, kSortEmpty, kSortB, kSortC}),
                  "★ 点那一格（那里不画任何东西）：盘上的组序一个字节都没动（那一格现在归这一行"
                  "自己，没有「挪一格」这件事了）",
                  groupOrderInFile().join(QLatin1Char('|')));
            QTreeWidgetItem *afterOldArrow = rowOfGroupId(tree, kSortA);
            const bool clickLanded = afterOldArrow != nullptr &&
                                     tree->itemAt(oldArrowPos) == afterOldArrow &&
                                     tree->visualItemRect(afterOldArrow) == firstRow;
            check(afterOldArrow != nullptr && !afterOldArrow->isExpanded() &&
                      expandedInFile(kSortA) == 0,
                  "★ 而且它办的是这一行本来那件事：这一组收起来了（用户报的"
                  "「点击一下就同时展开和调整了」—— 那两件事不能挤在同一个格子上，这"
                  "一格只剩这一件）",
                  QStringLiteral("树上 isExpanded=%1 / 文件 expanded=%2 / 重灌 %3→%4→%5 / "
                                 "视图 clicked %6 次（组行走开关手势，正常不经过它）/ 点还落在这一行 %7")
                      .arg(afterOldArrow != nullptr ? (afterOldArrow->isExpanded() ? 1 : 0) : -1)
                      .arg(expandedInFile(kSortA))
                      .arg(resetsBeforeClick)
                      .arg(resetsBetweenPressAndRelease)
                      .arg(treeResets)
                      .arg(itemClickedHits)
                      .arg(clickLanded ? 1 : 0));

            // ---- 另一半：「分组设置…」里摆好的顺序落盘、再长到树上 ----
            //
            // 上面那条钉的是"这里没有了"，这一条钉"那儿真的接上了" —— 中间那一段是产品里
            // 真跑的那条路：对话框只改自己手里那份工作副本，「确定」之后由 applyGroupList()
            // 落盘，写成功发 groupsChanged 重灌整张树（这里按名字调的就是那一个槽，见
            // MainWindow.hpp 里那段 NOTE）。
            using Group = GroupDialog::Group;
            QList<Group> dialogGroups;
            // 「排序一」写成收着 = 和盘上刚点出来那一下对齐（下面那条"它自己那一格没被谁
            // 顺手拨回来"才作数）。
            dialogGroups.append({kSortA, QStringLiteral("排序一"), false});
            dialogGroups.append({kSortEmpty, QStringLiteral("空组"), true});
            dialogGroups.append({kSortB, QStringLiteral("排序二"), true});
            dialogGroups.append({kSortC, QStringLiteral("排序三"), true});
            QHash<QString, int> dialogCounts;
            dialogCounts.insert(kSortA, 1);
            dialogCounts.insert(kSortB, 1);
            dialogCounts.insert(kSortC, 1);
            GroupDialog groupDialog(dialogGroups, dialogCounts, dialogGroups, dialogCounts);
            check(groupDialog.moveGroupUp(kSortC) && groupDialog.moveGroupToTop(kSortC),
                  "T16 那张窗里：先把「排序三」往上挪一格、再直接提到最顶上");
            groupDialog.setGroupExpanded(kSortB, false);
            QStringList dialogOrder;
            for (const Group &group : groupDialog.groups()) {
                dialogOrder.append(group.id);
            }
            check(dialogOrder == QStringList({kSortC, kSortA, kSortEmpty, kSortB}),
                  "T16 前提：那张窗里现在是 三 / 一 / 空组 / 二",
                  dialogOrder.join(QLatin1Char('|')));

            const bool invoked =
                QMetaObject::invokeMethod(&w, "applyGroupList", Qt::DirectConnection,
                                          Q_ARG(const GroupDialog *, &groupDialog));
            check(invoked, "T16 按名字调得动 applyGroupList()（那一半是给验证工程留的口）");
            check(groupOrderInFile() == QStringList({kSortC, kSortA, kSortEmpty, kSortB}),
                  "★ 那张窗里摆好的顺序落到了盘上（一 / 空组 / 二 / 三 → 三 / 一 / 空组 / 二，"
                  "含「直接提到最顶上」那一步）",
                  groupOrderInFile().join(QLatin1Char('|')));
            check(expandedInFile(kSortB) == 0 && expandedInFile(kSortC) == 1 &&
                      expandedInFile(kSortA) == 0,
                  "★ 「默认展开」那一格也跟着落了盘（排序二 收着、排序三 展开着、排序一 还是"
                  "上面那一下点出来的收着 —— 那一格是组自己的状态，和顺序一起比、一起写）",
                  QStringLiteral("一=%1 二=%2 三=%3")
                      .arg(expandedInFile(kSortA))
                      .arg(expandedInFile(kSortB))
                      .arg(expandedInFile(kSortC)));

            showInstanceTab(&w);
            check(groupHeadersOf(tree) ==
                      QStringList({QStringLiteral("排序三 (1)"), QStringLiteral("排序一 (1)"),
                                   QStringLiteral("排序二 (1)"), QStringLiteral("未分组 (1)")}),
                  "★ 屏幕上那四行真的照新顺序排了（写盘之后由 groupsChanged 重灌的这张树；"
                  "那个空组还是藏在里面不占行）",
                  groupHeadersOf(tree).join(QLatin1Char('|')));
            QTreeWidgetItem *rowAfterDialog = rowOfGroupId(tree, kSortA);
            check(rowAfterDialog != nullptr && !rowAfterDialog->isExpanded(),
                  "★ 「排序一」在树上还是收着的（顺序变了，它自己那一格没被谁顺手拨回来）",
                  QStringLiteral("isExpanded=%1")
                      .arg(rowAfterDialog != nullptr ? (rowAfterDialog->isExpanded() ? 1 : 0)
                                                     : -1));

            // 收拾：这个临时实例和那份分组文件都不能留着（T9/T15 同一条要求）。
            QDir(versionsDir.filePath(kSortTemp)).removeRecursively();
            check(!QDir(versionsDir.filePath(kSortTemp)).exists(),
                  QStringLiteral("T16 临时实例 %1 删干净了").arg(kSortTemp));
        }

        // ---- T17 实例行行尾那三颗按钮：齿轮=实例设置 / 上箭头=置顶 / 垃圾桶=删除实例 ----
        //
        // 要求：「实例页面，实例设置也是放在每一条的右侧，鼠标悬停时右侧
        // 显示上箭头和垃圾桶，分别对应 置顶和删除实例，删除前需要弹窗提示，直接点空白区域
        // （和左边图标版本名）进入实例设置，为每个版本预设一个图标（默认下载时就带，原版
        // 草块，forge铁砧等，现有的没有就补上）」。
        //
        // 四件事各占一段，验的都是**产品那条路**：那三颗是代理画出来的（行里一个控件都没有），
        // 认下那一下的是 MainWindow::eventFilter()，所以这里往视口上**真发鼠标事件**，不是去
        // 点某个按钮 —— 判定用的格子也是产品那一个出处（InstanceGroupDelegate::instanceXxxRect）。
        //
        // ① 每一条前面那个图标是"这个版本该有的那一个"，含"meta 里没写 type 时回实例自己的
        // json 读"那条路（在那之前它对每个实例都退化成草方块，而用户这一轮要的正是图标）；
        // ② 点行尾那颗**齿轮**、以及点这一行的**别的地方**，开的都是这个实例的设置页；
        // ③ 点**上箭头** = 把它挪进置顶组（哪一组当家见 InstanceGroupStore::pinnedGroupId）；
        // ④ 点**垃圾桶** = 先问一句，答「删除」才真的删，答「取消」一个字节都不动。
        {
            const QString kPinGroup = QStringLiteral("g-置顶组");
            const QString kPlainGroup = QStringLiteral("g-普通组");

            // 给①用的样本：一个**只有版本 json、没有 wtfml.meta** 的实例，json 里写着
            // type=snapshot。沙盒另外三个实例验不到这一条 —— 1.18.2 / 26.3 连 json 都没有
            // （读出来就是正式版），1.20.1 的 type 写在 meta 里（走的是前面那条路）。
            // 它同时是④要删掉的那一个（删完顺手就完成清理了）。
            const QString kSnapTemp = QStringLiteral("tf-快照");
            makeInstalledVersion(versionsDir, kSnapTemp);
            writeFileAt(versionsDir.filePath(kSnapTemp + QLatin1Char('/') + kSnapTemp +
                                             QStringLiteral(".json")),
                        QByteArrayLiteral("{\"id\":\"tf-快照\",\"type\":\"snapshot\"}"));

            // 一份**有置顶组**的分组文件：上箭头有没有去处全看它（那三条出口见
            // InstanceGroupStore::pinnedGroupId()）。置顶组刻意是**空的** —— 空组在树上不占行，
            // 于是"置顶之后它才出现"本身就成了看得见的一条断言。两个老实例放进「普通组」，
            // 26.3 留着不分（落在「未分组」里，正好是"还没置顶过"的那一条）。
            writeFileAt(groupFile,
                        QByteArray(R"({"version":1,"groups":[)"
                                   R"({"id":"g-普通组","name":"普通组","expanded":true},)"
                                   R"({"id":"g-置顶组","name":"置顶组","expanded":true,)"
                                   R"("pinned":true}],)"
                                   R"("members":{"1.20.1":"g-普通组","1.18.2":"g-普通组"}})"));
            check(QJsonDocument::fromJson(readBytesAt(groupFile))
                          .object()
                          .value(QStringLiteral("groups"))
                          .toArray()
                          .size() == 2,
                  "T17 前提：盘上这份分组文件里有两个组（置顶组那一格就是上箭头的去处）");

            // 按版本号找回树上那一行。**每次改完之后都要重找** —— 写盘成功会发
            // groupsChanged，那条路是**重灌整张树**（tree->clear()），旧指针已经是野的了。
            const auto rowOfInstanceId = [](QTreeWidget *tree,
                                            const QString &id) -> QTreeWidgetItem * {
                const QList<QTreeWidgetItem *> all = instanceItemsOf(tree);
                for (QTreeWidgetItem *item : all) {
                    if (item->data(0, MainWindowUI::instanceIdRole()).toString() == id) {
                        return item;
                    }
                }
                return nullptr;
            };
            // 同一个实例在树上**画了几行**。 用户把置顶改成「两边都显示」之后，
            // 置顶着的那一条会画两行（老家一行 + 置顶组里一行**同名的**），于是"找得到
            // 一条"这件事本身不再够用：那两条"多出来的是同一条、不是两个不同的实例"
            // 的断言得靠这个数。**数的是行、不是组**：它在哪两组里由调用方自己看 parent。
            const auto rowsOfInstanceId = [](QTreeWidget *tree, const QString &id) {
                int count = 0;
                const QList<QTreeWidgetItem *> all = instanceItemsOf(tree);
                for (QTreeWidgetItem *item : all) {
                    if (item->data(0, MainWindowUI::instanceIdRole()).toString() == id) {
                        ++count;
                    }
                }
                return count;
            };
            // 那一行**现在被摆在哪个组下面**（空串 = 未分组）。摆在哪一组不再等于它的
            // 归属（老家那一条和置顶组里那一条都摆着，归属在盘上），所以这里读的是树本身。
            const auto parentGroupOfRow = [](QTreeWidgetItem *item) -> QString {
                return item != nullptr && item->parent() != nullptr
                           ? item->parent()->data(0, MainWindowUI::groupIdRole()).toString()
                           : QString();
            };
            // 往视口上真发一次"按下 + 松开"。两发都要发：产品那条"这一行被激活了"走的是
            // QTreeWidget::itemClicked，而它只在按下的项和松开的项是同一个时才发（见
            // ui/RowActivation.hpp 文件头那段）。行尾那三颗**按下就动手**，松开那一发只是
            // 被 eventFilter 吃掉（不会漏给"点中了这一行"）。
            const auto clickRowAt = [](QTreeWidget *tree, const QPoint &pos) {
                QWidget *viewport = tree->viewport();
                const auto send = [viewport, pos](QEvent::Type type) {
                    QMouseEvent event(type, QPointF(pos), QPointF(viewport->mapToGlobal(pos)),
                                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(viewport, &event);
                };
                send(QEvent::MouseButtonPress);
                send(QEvent::MouseButtonRelease);
                QApplication::processEvents();
            };
            // UI 7b：实例设置 / 收藏 / 删除收进行尾「⋯」菜单。点 ⋯ 那一格（真发鼠标），在弹出的
            // 菜单里按文字挑一项触发；找不到那一项 = 那一格本来就没有这个动作。
            const auto pickRowMenu = [&clickRowAt](QTreeWidget *tree, const QRect &row, const QStringList &texts) {
                clickRowAt(tree, ListRowDelegate::actionRect(row, ListRowDelegate::Action::More).center());
                QPointer<QMenu> menu = tree->findChild<QMenu *>(MainWindowUI::instanceRowMenuName());
                if (menu.isNull()) return false;
                menu->setObjectName(QString());
                QAction *hit = nullptr;
                for (QAction *action : menu->actions()) {
                    if (texts.contains(action->text())) hit = action;
                }
                if (hit != nullptr) hit->trigger();
                if (!menu.isNull()) menu->close();
                QApplication::processEvents();
                return hit != nullptr;
            };
            const QStringList pinTexts{MainWindowUI::rowPinTipText(), MainWindowUI::rowUnpinTipText()};
            // 盘上那份成员表（读的是**文件**：这一节验的正是"落盘了没有"，问 store 等于自己
            // 验自己）。**按值数、不按键找**：键是 instanceKey() 给的 —— 有稳定 id 的实例
            // 按 id 存、没有的按目录名存，而"哪个实例已经有稳定 id 了"取决于它之前有没有被
            // 写过设置（S2 那一段就会给 26.3 写一个）。这一节要验的是"归属挪了没有"，
            // 不该在键的形态上翻车。
            const auto membersInFile = [&groupFile]() -> QJsonObject {
                return QJsonDocument::fromJson(readBytesAt(groupFile))
                    .object()
                    .value(QStringLiteral("members"))
                    .toObject();
            };
            const auto membersPointingAt = [&membersInFile](const QString &groupId) {
                int count = 0;
                const QJsonObject members = membersInFile();
                for (auto it = members.constBegin(); it != members.constEnd(); ++it) {
                    if (it.value().toString() == groupId) {
                        ++count;
                    }
                }
                return count;
            };
            // 盘上那份「置顶前的归属」（用户 那句要的那一格）。读法同上：读的是
            // **文件** —— 这一节验的正是"落盘了没有"。
            const auto pinOriginInFile = [&groupFile]() -> QJsonObject {
                return QJsonDocument::fromJson(readBytesAt(groupFile))
                    .object()
                    .value(QStringLiteral("pinOrigin"))
                    .toObject();
            };
            // 那一格里值**等于某个组 id**、以及值是 **null** 的条数。按值数、不按键找，
            // 理由和上面那份成员表逐字相同（键是 instanceKey() 给的）。
            const auto originCount = [&pinOriginInFile](const QString &groupId) {
                int count = 0;
                const QJsonObject origins = pinOriginInFile();
                for (auto it = origins.constBegin(); it != origins.constEnd(); ++it) {
                    if (it.value().toString() == groupId) {
                        ++count;
                    }
                }
                return count;
            };
            const auto originNullCount = [&pinOriginInFile]() {
                int count = 0;
                const QJsonObject origins = pinOriginInFile();
                for (auto it = origins.constBegin(); it != origins.constEnd(); ++it) {
                    if (it.value().isNull()) {
                        ++count;
                    }
                }
                return count;
            };

            MainWindow w;
            w.show();
            QApplication::processEvents();
            showInstanceTab(&w);
            QTreeWidget *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
            QComboBox *combo = comboByObjectName(&w, MainWindowUI::installedVersionComboName());
            check(tree != nullptr && groupHeadersOf(tree) ==
                                         QStringList({QStringLiteral("普通组 (2)"),
                                                      QStringLiteral("未分组 (2)")}),
                  "T17 前提：普通组装着两个老实例、未分组里是 26.3 和新摆的那个样本"
                  "（那个空的置顶组不占行）",
                  groupHeadersOf(tree).join(QLatin1Char('|')));

            // ---- ① 每一条前面的图标 ----
            //
            // 两条路各验一个：**装了加载器的**给加载器标志（用户点名的「forge铁砧」），
            // **没装的**按版本分类给（原版草方块 / 快照版覆雪草块）。判据一律拿
            // VersionIcons 那两个函数现算出来比，不在断言里另画一张。
            // 边长取**实例列表自己那一档**（instanceTreeIconSize()， 起 26）。
            // 拿版本树的 18 来量的话，`QIcon::pixmap()` 会把手里那张缩一遍，缩出来的像素和
            // `VersionIcons` 现画的那张**不相等**（草方块那块 16×16 贴图是最近邻放的）——
            // 底下每一条都会红，而屏幕上什么都没错。
            const int markSize = MainWindowUI::instanceTreeIconSize();
            const auto rowIcon = [markSize](QTreeWidgetItem *item) -> QImage {
                return item != nullptr ? item->icon(0).pixmap(markSize, markSize).toImage()
                                       : QImage();
            };
            check(tree->iconSize() == QSize(markSize, markSize),
                  "T17 视图那一档图标边长和现算 pixmap 时递进去的是同一个数"
                  "（两处对不上就是图标被重新采样一遍：屏幕上只是略糊，而下面那些"
                  "「行首那张图 == 现画的那张」的断言全红）",
                  QStringLiteral("视图 %1x%2 / 常数 %3")
                      .arg(tree->iconSize().width())
                      .arg(tree->iconSize().height())
                      .arg(markSize));
            // 「Forge」在第几档：**走产品那张表**（显示名那一条的唯一出处），不写死下标。
            const int forgeIndex = MainWindowUI::modLoaderOptionTexts().indexOf(
                QStringLiteral("Forge"));
            const QImage grassImage = VersionIcons::grassBlock(markSize).toImage();
            check(forgeIndex >= 0 && !grassImage.isNull(),
                  "T17 前提：能问出 Forge 是第几档、也画得出草方块", QString::number(forgeIndex));

            QTreeWidgetItem *forgeRow = rowOfInstanceId(tree, QStringLiteral("1.20.1"));
            check(rowIcon(forgeRow) == VersionIcons::loaderMark(forgeIndex, markSize).toImage() &&
                      rowIcon(forgeRow) != grassImage,
                  "★ 装了 Forge 的那一条：行前面是铁砧（wtfml.meta 里的 loaderName 说了算，"
                  "不是草方块）",
                  QStringLiteral("是铁砧=%1 是草方块=%2")
                      .arg(rowIcon(forgeRow) ==
                                   VersionIcons::loaderMark(forgeIndex, markSize).toImage()
                               ? 1
                               : 0)
                      .arg(rowIcon(forgeRow) == grassImage ? 1 : 0));

            QTreeWidgetItem *plainRow = rowOfInstanceId(tree, QStringLiteral("1.18.2"));
            check(rowIcon(plainRow) == grassImage,
                  "★ 没装加载器、也没有版本 json 的那一条：草方块（读不到 type 就当正式版，"
                  "不是「一个方块都不画」的那种空白）");

            // 这一条钉的是 versionChannelOf() 那条 json 回退：meta 里一个字都没写 type，
            // 分类只能回实例自己的 json 里读。**两条一起看才作数** —— 光"等于快照版那个
            // 图标"在"它其实一直是草方块、而快照版那个图标恰好是草方块"的时候也会绿，
            // 所以同时要求它**不等于**草方块。
            const int snapIndex = VersionCategory::channelIndex(
                VersionCategory::channelFor(kSnapTemp, QStringLiteral("snapshot")));
            check(rowIcon(rowOfInstanceId(tree, kSnapTemp)) ==
                          VersionIcons::versionChannelMark(snapIndex, markSize).toImage() &&
                      rowIcon(rowOfInstanceId(tree, kSnapTemp)) != grassImage,
                  "★ 只有版本 json、meta 里没写 type 的那一条：图标按 json 里的 type 来"
                  "（快照版那一张），不是草方块 —— 这一条挡的是「每个实例都退化成正式版」"
                  "那个老 bug 回来",
                  QStringLiteral("快照下标=%1").arg(snapIndex));

            // ---- ② 点齿轮 / 点这一行别的地方 = 进这个实例的设置页 ----
            //
            // 两处都验：行尾那颗**齿轮**是"常显的那个入口"，行里别的地方（这里点的是版本名
            // 左边那一截空白）是要求里那条"直接点空白区域（和左边图标版本名）"。
            {
                QTreeWidgetItem *target = rowOfInstanceId(tree, QStringLiteral("1.20.1"));
                const QRect row = target != nullptr ? tree->visualItemRect(target) : QRect();
                check(row.width() > 0, "T17 前提：这一行在树上");
                QString pageTitle;
                pickRowMenu(tree, row, {MainWindowUI::rowInstanceSettingsTipText()});
                closeInstanceSettingsPage(&pageTitle);
                check(pageTitle.contains(QStringLiteral("我的整合包")),
                      "★ 点行尾那颗齿轮：开的是**这一行**那个实例的设置页（页眉上写着它）",
                      pageTitle);
                check(g_modalTitle.isEmpty(),
                      "T17 而且只有这一页，没有别的框跟着冒出来", g_modalTitle);
            }
            {
                QTreeWidgetItem *target = rowOfInstanceId(tree, kSnapTemp);
                const QRect row = target != nullptr ? tree->visualItemRect(target) : QRect();
                // 行里靠左那一截（版本名/图标那一片）—— 和行尾三颗按钮离得远远的。
                const QPoint bodyPos(row.left() + row.width() / 3, row.center().y());
                check(row.width() > 0 && row.contains(bodyPos) &&
                          !ListRowDelegate::actionRect(row, ListRowDelegate::Action::Folder).contains(bodyPos) &&
                          !ListRowDelegate::actionRect(row, ListRowDelegate::Action::More).contains(bodyPos) &&
                          !ListRowDelegate::actionRect(row, ListRowDelegate::Action::Launch).contains(bodyPos),
                      "T17 前提：要点的那个点在行里，而且不在行尾那三颗按钮的任何一格上",
                      QStringLiteral("点 (%1,%2) / 行 %3,%4 %5x%6")
                          .arg(bodyPos.x())
                          .arg(bodyPos.y())
                          .arg(row.left())
                          .arg(row.top())
                          .arg(row.width())
                          .arg(row.height()));
                QString pageTitle;
                // 这里**走信号**（和 T10 的 clickTreeRow 同一个理由），不再伪造鼠标两发。
                //
                // QTreeView 在 AnimatingState 里会把**任何一行**的按下整条吞掉
                // （`expandOrCollapseItemAtPos()` 开头那条 `state != NoState` —— 见
                // ui/RowActivation.hpp 文件头）。树一直开着展开动画，测试进程又没有真实
                // 鼠标消息泵，`QApplication::sendEvent()` 造出的两发事件可能落在这个窗口里：
                // 行内坐标、命中项都验过是对的，但视图根本没进 `QAbstractItemView::
                // mousePressEvent`，于是 `itemClicked` 一发都不发（这一条在本机 100% 复现，
                // 与产品逻辑无关）。行内坐标和"不在三颗按钮上"仍由上面那条前提钉住；
                // 信号路才是"点了这一行之后产品做了什么"的判据。
                clickTreeRow(tree, target);
                closeInstanceSettingsPage(&pageTitle);
                check(pageTitle.contains(kSnapTemp),
                      "★ 点这一行的别的地方（左边图标 / 版本名那一片）：也是进这个实例的设置"
                      "（用户原话「直接点空白区域（和左边图标版本名）进入实例设置」）",
                      pageTitle);
            }

            // ---- ③ 上箭头 = 置顶：挪进置顶组 ----
            {
                QTreeWidgetItem *target = rowOfInstanceId(tree, QStringLiteral("26.3"));
                const QRect row = target != nullptr ? tree->visualItemRect(target) : QRect();
                check(target != nullptr &&
                          target->data(0, MainWindowUI::rowPinRole()).toBool() &&
                          membersPointingAt(kPinGroup) == 0 &&
                          membersInFile().size() == 2,
                      "T17 前提：26.3 还没置顶过（它在未分组里、那一格的上箭头该画着）",
                      QStringLiteral("成员 %1 条、指到置顶组的 %2 条")
                          .arg(membersInFile().size())
                          .arg(membersPointingAt(kPinGroup)));
                check(pickRowMenu(tree, row, pinTexts), "T17 ⋯ 菜单里有「收藏」那一项");
                check(membersPointingAt(kPinGroup) == 1 && membersInFile().size() == 3,
                      "★ 点一下上箭头：这一条**落盘进了置顶组**（用户原话「置顶以后进选了这个"
                      "的组」），而且只多出这一条",
                      QStringLiteral("成员 %1 条、指到置顶组的 %2 条")
                          .arg(membersInFile().size())
                          .arg(membersPointingAt(kPinGroup)));
                check(groupHeadersOf(tree) == QStringList({QStringLiteral("普通组 (2)"),
                                                           QStringLiteral("置顶组 (1)"),
                                                           QStringLiteral("未分组 (2)")}),
                      "★ 树上真的跟着动了（原来那个空的置顶组现在有了一条、它才出现）—— "
                      "而未分组那个数**没变**：按「两边都显示」，置顶"
                      "不是搬家 —— 老家留一份，置顶组里多一条同名的（26.3 的老家还在未分组里）",
                      groupHeadersOf(tree).join(QLatin1Char('|')));
                check(rowsOfInstanceId(tree, QStringLiteral("26.3")) == 2,
                      "★ 置顶之后**同一个实例在树上占两行**（用户原话「就是在被设为置顶组的"
                      "那个组里再放一个同名的」）；上面那个 (1) 不是「只画一行」的证据",
                      QStringLiteral("画了 %1 行").arg(rowsOfInstanceId(tree, QStringLiteral("26.3"))));
                QTreeWidgetItem *moved = rowOfInstanceId(tree, QStringLiteral("26.3"));
                // 那两行分别落在哪两组：一行在「置顶组」、另一行**还在未分组**（老家）。
                // 逐个 parent 看，不靠"树是按组序深度优先、取第一个就是置顶组那条"——
                // 那种写法在别人挪了组序之后会静默指错行。
                QTreeWidgetItem *movedInPin = nullptr;
                QTreeWidgetItem *movedAtHome = nullptr;
                for (QTreeWidgetItem *item : instanceItemsOf(tree)) {
                    if (item->data(0, MainWindowUI::instanceIdRole()).toString() !=
                        QStringLiteral("26.3")) {
                        continue;
                    }
                    if (parentGroupOfRow(item) == kPinGroup) {
                        movedInPin = item;
                    } else {
                        movedAtHome = item;
                    }
                }
                check(movedInPin != nullptr && movedAtHome != nullptr &&
                          parentGroupOfRow(movedAtHome).isEmpty(),
                      "★ 那两行各自的落点：一行在「置顶组」里、另一行还在**未分组**（老家）"
                      "—— 「原组留一份」那半句",
                      QStringLiteral("置顶组里 %1 行 / 未分组里 %2 行")
                          .arg(movedInPin != nullptr ? 1 : 0)
                          .arg(movedAtHome != nullptr ? 1 : 0));
                // 下面几条（那一格摆的是哪颗箭头、挪出了置顶组没有）说的是**置顶组里那一条**
                // —— 树上第一个找到的 26.3 就是它（组序里置顶组在未分组前面），但这里显式
                // 用 movedInPin，别让那件事变成隐含前提。
                moved = movedInPin != nullptr ? movedInPin : moved;
                check(moved != nullptr && moved->parent() != nullptr &&
                          moved->parent()->data(0, MainWindowUI::groupIdRole()).toString() ==
                              kPinGroup,
                      "T17 它挪进了「置顶组」那一行下面");
                check(moved != nullptr &&
                          !moved->data(0, MainWindowUI::rowPinRole()).toBool(),
                      "★ 已经在置顶组里的那一条，那一格不再摆上箭头（挪不动的那侧不画，"
                      "不是画一颗灰的）");
                check(membersPointingAt(kPlainGroup) == 2,
                      "T17 只动了点的那一条，别的归属一个字节都没碰",
                      QStringLiteral("还指到普通组的 %1 条").arg(membersPointingAt(kPlainGroup)));
                // 「置顶前的归属」：26.3 置顶之前**文件里一条归属都没有**（它显示在未分组
                // 底下是显示层按版本类型算出来的，不是用户选的）⇒ 这一格记成 **null**
                // （见 InstanceGroupStore 文件头那段）。
                check(pinOriginInFile().size() == 1 && originNullCount() == 1,
                      "★ 置顶这一下顺手记下了「置顶之前它在哪一组」：26.3 置顶前没有归属，"
                      "记成 null ⇒ 取消置顶时把这一条**删掉**，让它按版本类型重新归类、落回"
                      "置顶前显示的那一组",
                      QStringLiteral("记录 %1 条 / 其中 null %2 条")
                          .arg(pinOriginInFile().size())
                          .arg(originNullCount()));

                // ---- ③b 同一格反过来那一颗：下箭头 = 取消置顶 ----
                //
                // 要求：「置顶组里的实例悬停时右侧显示下箭头，即取消置顶」。
                // 于是那一格是**一对反义词共用的一格**（见 MainWindowUI::rowUnpinRole()）：
                // 不在里面 ⇒ ↑，已经在里面 ⇒ ↓。两个角色**互斥**，上面那条"不再摆上箭头"
                // 现在守的是这前半句。
                const QRect movedRow = moved != nullptr ? tree->visualItemRect(moved) : QRect();
                check(moved != nullptr &&
                          moved->data(0, MainWindowUI::rowUnpinRole()).toBool() &&
                          !moved->data(0, MainWindowUI::rowPinRole()).toBool(),
                      "★ 已经在置顶组里的那一条：那一格挂的是**取消置顶**（悬停画下箭头），"
                      "两个角色互斥（上面那条老断言守的就是它的另一半）");
                pickRowMenu(tree, movedRow, pinTexts);
                check(membersPointingAt(kPinGroup) == 0 && membersInFile().size() == 2,
                      "★ 点一下下箭头：这一条**挪出了置顶组**（落盘）—— 而它是「置顶前没表过"
                      "态」那一种，所以文件里那一条归属被**删掉**了（不是写成「明确选了未"
                      "分组」）：显示层按版本类型再算一遍，落回它置顶前显示的那一组。"
                      "写成保留值等于替用户说了一句他没说过的话 —— 那个实例从此再也不会"
                      "被自动归类（见 InstanceGroupStore 文件头那段）",
                      QStringLiteral("指到置顶组的 %1 条 / 成员 %2 条")
                          .arg(membersPointingAt(kPinGroup))
                          .arg(membersInFile().size()));
                check(pinOriginInFile().isEmpty(),
                      "★ 那条记录用完就删（留着的话下一次置顶会把旧账当成「置顶前」再读一遍）",
                      QStringLiteral("记录 %1 条").arg(pinOriginInFile().size()));
                check(groupHeadersOf(tree) == QStringList({QStringLiteral("普通组 (2)"),
                                                           QStringLiteral("未分组 (2)")}),
                      "★ 树上真的跟着动了（置顶组空回去了、不占行；它落回未分组）",
                      groupHeadersOf(tree).join(QLatin1Char('|')));
                check(rowsOfInstanceId(tree, QStringLiteral("26.3")) == 1,
                      "★ 取消置顶之后**那一条只剩一行**了（置顶组里那条同名的没了；老家那一行"
                      "本来就一直在，所以它是「少了一行」而不是「换了一行」）",
                      QStringLiteral("画了 %1 行").arg(rowsOfInstanceId(tree, QStringLiteral("26.3"))));
                QTreeWidgetItem *back = rowOfInstanceId(tree, QStringLiteral("26.3"));
                check(back != nullptr && back->parent() != nullptr &&
                          back->parent()->data(0, MainWindowUI::groupIdRole()).toString().isEmpty(),
                      "T17 它落在了「未分组」那一档里");
                check(back != nullptr &&
                          back->data(0, MainWindowUI::rowPinRole()).toBool() &&
                          !back->data(0, MainWindowUI::rowUnpinRole()).toBool(),
                      "T17 挪出来之后那一格换回上箭头（互斥，不是两颗一起挂着）");
                // 再点一次（这回它又是上箭头了）：来回一趟回到原处 —— 这两颗**必须落在
                // 同一格上**，不然上一节的取舍（"一行里塞不下第四颗"）就白讲了。
                const QRect backRow = back != nullptr ? tree->visualItemRect(back) : QRect();
                pickRowMenu(tree, backRow, pinTexts);
                check(membersPointingAt(kPinGroup) == 1 && membersInFile().size() == 3,
                      "★ 再点一次那颗刚换回上箭头的：又回到置顶组里（同一格上的一对反义词）",
                      QStringLiteral("指到置顶组的 %1 条").arg(membersPointingAt(kPinGroup)));
            }

            // ---- ③c 「置顶前的归属」：从某一个**组**里置顶的，取消置顶回到那个组 ----
            //
            // 要求：「加一个记录置顶前这玩意属于哪一组的，用户直接塞的取消
            // 置顶才归进未分类」—— 前半截（记下来、并且回到那里）就是这一段。上一段验的是
            // "置顶前没有归属"那一种（记 null、取消置顶时把那一条删掉），这一段验的是
            // "置顶前待在某个组里"那一种（记那个组 id、取消置顶写回去）。
            {
                const QString kFromPlain = QStringLiteral("1.18.2");
                QTreeWidgetItem *plain = rowOfInstanceId(tree, kFromPlain);
                const QRect plainRow = plain != nullptr ? tree->visualItemRect(plain) : QRect();
                check(plain != nullptr && plain->data(0, MainWindowUI::rowPinRole()).toBool() &&
                          membersPointingAt(kPlainGroup) == 2 && originCount(kPlainGroup) == 0,
                      "T17 前提：1.18.2 在「普通组」里、那一格摆着上箭头，而且盘上没有任何"
                      "「记着普通组」的记录",
                      QStringLiteral("成员 %1 条 / 指到普通组 %2 条 / 记录 %3 条")
                          .arg(membersInFile().size())
                          .arg(membersPointingAt(kPlainGroup))
                          .arg(originCount(kPlainGroup)));
                pickRowMenu(tree, plainRow, pinTexts);
                check(membersPointingAt(kPinGroup) == 2 && membersPointingAt(kPlainGroup) == 1 &&
                          originCount(kPlainGroup) == 1,
                      "★ 置顶「普通组」里的那一条：它进了置顶组，而且**记下了「置顶之前它属于"
                      "普通组」**（记录里那个值就是那一组的 id）",
                      QStringLiteral("指到置顶组 %1 条 / 指到普通组 %2 条 / 记着普通组 %3 条")
                          .arg(membersPointingAt(kPinGroup))
                          .arg(membersPointingAt(kPlainGroup))
                          .arg(originCount(kPlainGroup)));
                QTreeWidgetItem *inPin = rowOfInstanceId(tree, kFromPlain);
                const QRect inPinRow = inPin != nullptr ? tree->visualItemRect(inPin) : QRect();
                check(inPin != nullptr && inPin->data(0, MainWindowUI::rowUnpinRole()).toBool(),
                      "T17 前提：它现在在置顶组里（那一格挂的是下箭头）");
                pickRowMenu(tree, inPinRow, pinTexts);
                check(membersPointingAt(kPlainGroup) == 2 && membersPointingAt(kPinGroup) == 1 &&
                          membersInFile().size() == 3 && originCount(kPlainGroup) == 0,
                      "★ 点一下下箭头：它**回到了置顶之前待的那一组**"
                      "—— 归属的条数一条不多一条不少（写回去的是记录里那个组 id，不是未分组"
                      "那条保留值）",
                      QStringLiteral("指到普通组 %1 条 / 指到置顶组 %2 条 / 成员 %3 条")
                          .arg(membersPointingAt(kPlainGroup))
                          .arg(membersPointingAt(kPinGroup))
                          .arg(membersInFile().size()));
                QTreeWidgetItem *backPlain = rowOfInstanceId(tree, kFromPlain);
                check(backPlain != nullptr && backPlain->parent() != nullptr &&
                          backPlain->parent()->data(0, MainWindowUI::groupIdRole()).toString() ==
                              kPlainGroup,
                      "T17 树上也回到「普通组」那一行下面了（不是丢在未分组里）");
                check(groupHeadersOf(tree) == QStringList({QStringLiteral("普通组 (2)"),
                                                           QStringLiteral("置顶组 (1)"),
                                                           QStringLiteral("未分组 (2)")}),
                      "T17 组头也跟着回来了（置顶组里只剩 26.3 那一条同名的，1.18.2 回到"
                      "「普通组」）—— 未分组那个数是 2：26.3 的老家还在里面（它置着顶，"
                      "所以它在置顶组里也有一条）",
                      groupHeadersOf(tree).join(QLatin1Char('|')));
                check(rowsOfInstanceId(tree, kFromPlain) == 1 &&
                          parentGroupOfRow(rowOfInstanceId(tree, kFromPlain)) == kPlainGroup,
                      "T17 1.18.2 那一条也只剩一行、而且落在「普通组」下面（置顶组里那条"
                      "同名的跟着记录一起收掉了）");
            }

            // ---- ③d 用户**自己**塞进置顶组的 ⇒ 取消置顶落未分类 ----
            //
            // 要求那一句的后半截：「用户直接塞的取消置顶才归进未分类」。这一档盘上
            // **没有记录**（「移入分组」不是置顶，它没有「置顶前」这么一回事；这一版之前
            // 就在置顶组里的老数据同理），所以只能落未分组 —— 猜不出它本来该在哪一组，
            // 而猜错的表现是"取消置顶之后它跳去了一个我没选过的组"。
            //
            // 摆这个状态走的是**产品那条路**：「移入分组」那个动作（组齿轮菜单里那一项 +
            // 挑目标那个框）。T12 已经验过那个框的形状，这里只借它把用户能碰到的那个状态
            // 摆出来 —— 自己往文件里塞一条"没有记录、但人在置顶组里"的归属，验的就不是
            // 用户真会碰到的那条路了。
            {
                // 「移入分组」读的是**下拉框**里选中的那个实例（见它的实现），先在那儿选它。
                check(combo != nullptr && combo->findData(kSnapTemp) >= 0,
                      "T17 前提：那个样本还在下拉框里",
                      QStringLiteral("index=%1")
                          .arg(combo != nullptr ? combo->findData(kSnapTemp) : -1));
                if (combo != nullptr) {
                    combo->setCurrentIndex(combo->findData(kSnapTemp));
                }
                QApplication::processEvents();
                check(combo != nullptr && combo->currentData().toString() == kSnapTemp,
                      "T17 前提：当前选中那个样本（此刻它在未分组里）",
                      combo != nullptr ? combo->currentData().toString() : QString());

                QMetaObject::invokeMethod(&w, "onGroupActionRequested", Qt::DirectConnection,
                                          Q_ARG(QString, kPinGroup),
                                          Q_ARG(int, MainWindowUI::GroupActionMoveInto));
                QApplication::processEvents();
                QWidget *picker = tree->findChild<QWidget *>(MainWindowUI::groupMenuName());
                QPushButton *pinTarget =
                    picker != nullptr
                        ? picker->findChild<QPushButton *>(MainWindowUI::groupPickRowName(1))
                        : nullptr;
                check(pinTarget != nullptr && pinTarget->isEnabled(),
                      "T17 前提：挑目标那个框里第 1 行就是「置顶组」（组序 = 清单里的顺序），"
                      "而且点得动",
                      pinTarget != nullptr ? pinTarget->text() : QString());
                if (pinTarget != nullptr) {
                    pinTarget->click();
                }
                QApplication::processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                check(membersPointingAt(kPinGroup) == 2 && pinOriginInFile().size() == 1 &&
                          originNullCount() == 1,
                      "★ 用户自己塞进置顶组的那一条**没有记录**（盘上还是只有 26.3 那条 "
                      "null）—— 记的是「置顶前」，而它不是置顶过来的",
                      QStringLiteral("指到置顶组 %1 条 / 记录 %2 条")
                          .arg(membersPointingAt(kPinGroup))
                          .arg(pinOriginInFile().size()));
                // 「移入分组 → 置顶组」落盘之后也是"在置顶组里"（members 那一格指着置顶组
                // 就是置顶，见 InstanceGroupStore::isPinned()），于是它同样按「两边都显示」
                // 画两行：置顶组里一条同名的 + 老家那条（用户自己塞的没有记录 ⇒ 老家 =
                // 未分组）。这不是"多画了一行"的 bug，是"这个动作在数据层等价于置顶"在界面上
                // 的形状 —— 下面 ④ 删完那个样本之后的组头期望也建立在它上面。
                check(rowsOfInstanceId(tree, kSnapTemp) == 2 &&
                          parentGroupOfRow(rowOfInstanceId(tree, kSnapTemp)) == kPinGroup,
                      "T17 自己塞进置顶组的那一条也画两行（置顶组里一条 + 未分组里老家那条）"
                      "—— 「移入分组 → 置顶组」和「置顶」在数据层是同一句话",
                      QStringLiteral("画了 %1 行 / 第一条落在 %2")
                          .arg(rowsOfInstanceId(tree, kSnapTemp))
                          .arg(parentGroupOfRow(rowOfInstanceId(tree, kSnapTemp))));
                QTreeWidgetItem *stuffed = rowOfInstanceId(tree, kSnapTemp);
                const QRect stuffedRow =
                    stuffed != nullptr ? tree->visualItemRect(stuffed) : QRect();
                check(stuffed != nullptr &&
                          stuffed->data(0, MainWindowUI::rowUnpinRole()).toBool(),
                      "T17 前提：它在置顶组里（那一格挂的是下箭头）");
                pickRowMenu(tree, stuffedRow, pinTexts);
                check(membersPointingAt(kPinGroup) == 1 && membersInFile().size() == 4,
                      "★ 点一下下箭头：没有记录 ⇒ 落**未分类**，而且写的是「明确选了未分组」"
                      "那条**保留值**（不是把这一条删掉）—— 于是它下一次刷新不会被自动归类"
                      "拉回别的组去（条数：三条老归属 + 这一条保留值）",
                      QStringLiteral("指到置顶组 %1 条 / 成员 %2 条")
                          .arg(membersPointingAt(kPinGroup))
                          .arg(membersInFile().size()));
                QTreeWidgetItem *unstuffed = rowOfInstanceId(tree, kSnapTemp);
                check(unstuffed != nullptr && unstuffed->parent() != nullptr &&
                          unstuffed->parent()->data(0, MainWindowUI::groupIdRole())
                              .toString()
                              .isEmpty(),
                      "T17 它落在「未分组」那一档里");
                check(pinOriginInFile().size() == 1 && originNullCount() == 1,
                      "T17 别的记录一个字节都没碰（26.3 那条还在）",
                      QStringLiteral("记录 %1 条").arg(pinOriginInFile().size()));
            }

            // ---- ④ 垃圾桶 = 删除这个实例：先问一句 ----
            //
            // 要求：「删除前需要弹窗提示」。两半都验：答「取消」一个字节都不动、
            // 答「删除」连目录一起没。
            {
                QTreeWidgetItem *target = rowOfInstanceId(tree, kSnapTemp);
                const QRect row = target != nullptr ? tree->visualItemRect(target) : QRect();
                check(row.width() > 0, "T17 前提：这一行在树上");

                dismissNextPrompt();  // 最后一颗 = 「取消」
                pickRowMenu(tree, row, {MainWindowUI::rowDeleteInstanceTipText()});
                check(g_modalTitle == QStringLiteral("删除实例"),
                      "★ 点垃圾桶：先弹一句才动手（用户原话「删除前需要弹窗提示」）", g_modalTitle);
                check(g_modalText.contains(kSnapTemp) &&
                          g_modalText.contains(
                              QDir::toNativeSeparators(versionsDir.filePath(kSnapTemp))),
                      "T17 那句话里写着删的是哪个实例、以及那个目录的**完整路径**"
                      "（用户要看得清删的是什么、它长在哪）",
                      g_modalText);
                check(QDir(versionsDir.filePath(kSnapTemp)).exists() &&
                          rowOfInstanceId(tree, kSnapTemp) != nullptr,
                      "★ 答「取消」：盘上那个目录、树上那一条，都一个字节没动");

                answerNextPrompt(0);  // 第一颗 = 「删除」
                pickRowMenu(tree, row, {MainWindowUI::rowDeleteInstanceTipText()});
                check(!QDir(versionsDir.filePath(kSnapTemp)).exists(),
                      "★ 答「删除」：整个目录真的没了（jar / json / 它自己的设置一起）");
                check(rowOfInstanceId(tree, kSnapTemp) == nullptr,
                      "T17 那一条从树上也没了");
                check(combo == nullptr || combo->findData(kSnapTemp) < 0,
                      "T17 下拉框里也没有它了");
                check(groupHeadersOf(tree) == QStringList({QStringLiteral("普通组 (2)"),
                                                           QStringLiteral("置顶组 (1)"),
                                                           QStringLiteral("未分组 (1)")}),
                      "T17 那个样本原本在「未分组」里 —— 删完它未分组里还剩 26.3 的**老家**"
                      "那一行（26.3 置着顶、在置顶组里也有一条），所以那一档没空、照样画着"
                      "；树上少的是那两行同名里的**样本**那一份",
                      groupHeadersOf(tree).join(QLatin1Char('|')));
                check(rowsOfInstanceId(tree, kSnapTemp) == 0 &&
                          rowsOfInstanceId(tree, QStringLiteral("26.3")) == 2,
                      "T17 删掉的是那一个实例的**两行**都一起没了，而 26.3 的两行原样还在",
                      QStringLiteral("样本 %1 行 / 26.3 %2 行")
                          .arg(rowsOfInstanceId(tree, kSnapTemp))
                          .arg(rowsOfInstanceId(tree, QStringLiteral("26.3"))));
            }

            // 收拾：这个临时实例的目录（④那一半正常跑完时它已经没了，这一句是兜底 —— 上面
            // 红了的话别把后面那几节连累掉）。分组文件由 T15 那段统一删。
            QDir(versionsDir.filePath(kSnapTemp)).removeRecursively();
            check(!QDir(versionsDir.filePath(kSnapTemp)).exists(),
                  QStringLiteral("T17 临时实例 %1 删干净了").arg(kSnapTemp));
        }

        // ---- TGD. 「分组设置…」那一页改成了非阻塞开页（2026-11-02） ----
        //
        // 以前这一段没法写：点那颗按钮会卡在 `execPage()` 的嵌套事件循环里，offscreen 下
        // 驱动不了「确定 / 取消」（T16 那一半因此只能直接调 applyGroupList()）。改成
        // `OverlayHost::showPage()` 之后点完就返回，于是能真的从界面走到盘上：
        // 开页 → 在页上改 → 点「确定 / 取消」 → 收页钩子把工作副本落盘（或丢掉）。
        {
            const QString groupFile = AppPaths::instanceGroupFilePath();
            QDir().remove(groupFile);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

            MainWindow w;
            w.show();
            QApplication::processEvents();
            showInstanceTab(&w);
            auto *button = w.findChild<QPushButton *>(MainWindowUI::groupInstanceButtonName());
            check(button != nullptr, "TGD 前提：找得到「分组设置…」那颗按钮");
            if (button == nullptr) {
                return 0;  // 找不到就没有下文了；上面的 check 已经把这件红记下了
            }

            // 点一下、把这一页摆出来。**这一发不该卡住** —— 卡住的话下面这一行压根走不到
            // （offscreen 下那个嵌套循环没有东西会来收它）。
            const auto openGroupPage = [&]() -> QPair<OverlayHost *, GroupDialog *> {
                button->click();
                QApplication::processEvents();
                return {currentPageOverlay(), w.findChild<GroupDialog *>()};
            };
            const auto groupsInFile = [&groupFile]() -> QStringList {
                QStringList names;
                const QJsonArray groups = QJsonDocument::fromJson(readBytesAt(groupFile))
                                              .object()
                                              .value(QStringLiteral("groups"))
                                              .toArray();
                for (const QJsonValue &value : groups) {
                    names << value.toObject().value(QStringLiteral("name")).toString();
                }
                return names;
            };

            const QString confirmedName = QStringLiteral("TGD-确定落盘");
            {
                const auto page = openGroupPage();
                OverlayHost *host = page.first;
                GroupDialog *dialog = page.second;
                check(host != nullptr && host->pageOpen(),
                      "TGD 点完那颗按钮这一页就摆上来了（点完就返回，没有卡在嵌套循环里）");
                check(dialog != nullptr, "TGD 覆盖层里摆着的那一个是 GroupDialog");
                if (dialog != nullptr) {
                    check(dialog->createGroup(confirmedName), "TGD 在页上新建一个组");
                    auto *confirm =
                        dialog->findChild<QPushButton *>(GroupDialog::confirmButtonName());
                    check(confirm != nullptr, "TGD 找得到「确定」那一颗");
                    if (confirm != nullptr) {
                        confirm->click();
                    }
                }
                check(host == nullptr || !host->pageOpen(),
                      "TGD 点「确定」之后这一页收干净了（pageClosed 那条钩子跑过）");
                check(groupsInFile().contains(confirmedName),
                      "★★ TGD 「确定」之后那个组真的写进了 wtfml.folders",
                      groupsInFile().join(QLatin1Char('|')));
            }

            // 取消那一档：页上改的那一份**只在对话框自己手里**，磁盘一个字节都不许动。
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            {
                const QString droppedName = QStringLiteral("TGD-取消不落盘");
                const auto page = openGroupPage();
                OverlayHost *host = page.first;
                GroupDialog *dialog = page.second;
                check(host != nullptr && host->pageOpen(), "TGD 取消那一档：这一页也摆上来了");
                check(dialog != nullptr, "TGD 取消那一档：找到的是**新**摆上来的那一个");
                if (dialog != nullptr) {
                    check(dialog->createGroup(droppedName), "TGD 取消那一档：在页上新建一个组");
                    auto *cancel =
                        dialog->findChild<QPushButton *>(GroupDialog::cancelButtonName());
                    check(cancel != nullptr, "TGD 找得到「取消」那一颗");
                    if (cancel != nullptr) {
                        cancel->click();
                    }
                }
                check(host == nullptr || !host->pageOpen(), "TGD 点「取消」之后这一页也收干净了");
                check(!groupsInFile().contains(droppedName),
                      "★★ TGD 「取消」之后页上那个组一个字节都没落盘",
                      groupsInFile().join(QLatin1Char('|')));
            }
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }

        // 这一节把分组文件改得面目全非（两个自定义组 + 几条归属），下一节开始前清干净 ——
        // 和 T9 一样的要求：后面的节数实例、以及收尾那句「沙盒回到 3 个版本」都建立在
        // "只有原来那三个、而且没分过组"之上。
        QDir().remove(groupFile);
        check(!QFile::exists(groupFile), "T15 这一节自己那份 wtfml.folders 删掉了");

    }
    return 0;
}


