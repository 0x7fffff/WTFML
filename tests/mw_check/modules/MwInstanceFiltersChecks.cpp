#include "Support.hpp"
int MwInstanceFiltersChecks() {
    // ------------------------------------------------------------------
    // 判据：「搜索实例名、版本号、加载器；筛选原版/模组实例、可更新、
    // 最近运行；搜索不改变分组归属；清空搜索恢复原展开状态」。
    //
    // 这一节验的是**接线**：搜索框里敲几个字、四颗 chip 点一下，树上剩下的到底是哪几个
    // 实例、组头长什么样。判据层那一半（怎么分词、哪六个串参与比对、"与/或"关系）在
    // verify 的 ZS 段逐格念过了，不在这儿重复。
    //
    // 只有界面这一层说得准的两条钉在下面：
    // * 筛着的时候命中的那些组**摆成展开的**（不展开 = 命中条目躲在收着的组里，
    // 用户看着像"搜不到"）；
    // * 清空之后各组回到**盘上记着的**那个样子 —— 因为"搜索"整件事一个字节都不许写盘。
    {
        const QString groupFile = AppPaths::instanceGroupFilePath();
        QDir().remove(groupFile);
        // 「最近运行」那一档的输入是启动账本（`wtfml.history`）。这一节自己写一条，
        // 收尾删掉 —— 别的节没有一个指望沙盒里有这本账。
        const QString historyFile = LaunchHistory::historyPath();
        QDir().remove(historyFile);

        // 沙盒里那三个实例：1.20.1（wtfml.meta 里写着 loaderName=Forge）、1.18.2、26.3。
        //
        // 分组只写**两个自定义组**，名字故意不叫「原版」/「可安装mod」：默认那几档的名字里
        // 带着"模组"两个字，搜索词会撞上它们，而这一节要的是两格干净的组头。
        // 「甲组」**收着**（expanded=false）—— "清空搜索之后回到收着"那一条全靠它；
        // 「乙组」开着，当对照（本来就展开的那组清空之后不该被翻成收着的）。
        const QString collapsedGroup = QStringLiteral("tf-甲组");
        const QString expandedGroup = QStringLiteral("tf-乙组");
        writeFileAt(groupFile,
                    QJsonDocument(QJsonObject{
                                      {QStringLiteral("version"), 1},
                                      {QStringLiteral("groups"),
                                       QJsonArray{
                                           QJsonObject{{QStringLiteral("id"), collapsedGroup},
                                                       {QStringLiteral("name"),
                                                        QStringLiteral("甲组")},
                                                       {QStringLiteral("expanded"), false}},
                                           QJsonObject{{QStringLiteral("id"), expandedGroup},
                                                       {QStringLiteral("name"),
                                                        QStringLiteral("乙组")},
                                                       {QStringLiteral("expanded"), true}}}},
                                      {QStringLiteral("members"),
                                       QJsonObject{{QStringLiteral("1.18.2"), collapsedGroup},
                                                   {QStringLiteral("1.20.1"), expandedGroup}}}})
                        .toJson(QJsonDocument::Compact));

        MainWindow w;
        w.show();
        QApplication::processEvents();

        auto *tree = byObjectName<QTreeWidget>(&w, MainWindowUI::instanceListName());
        auto *search = byObjectName<QLineEdit>(&w, MainWindowUI::instanceSearchName());
        auto *noMatch = byObjectName<QLabel>(&w, MainWindowUI::instanceNoMatchLabelName());
        const auto chip = [&w](MainWindowUI::InstanceFilterButton which) {
            return byObjectName<QPushButton>(&w, MainWindowUI::instanceFilterButtonName(which));
        };
        QPushButton *chipVanilla = chip(MainWindowUI::InstanceFilterButton::Vanilla);
        QPushButton *chipModded = chip(MainWindowUI::InstanceFilterButton::Modded);
        QPushButton *chipUpdatable = chip(MainWindowUI::InstanceFilterButton::Updatable);
        QPushButton *chipRecent = chip(MainWindowUI::InstanceFilterButton::Recent);
        auto *folderLabel = byObjectName<QLabel>(&w, MainWindowUI::instanceFolderLabelName());
        check(tree != nullptr && search != nullptr && noMatch != nullptr &&
                  chipVanilla != nullptr && chipModded != nullptr && chipUpdatable != nullptr &&
                  chipRecent != nullptr,
              "T37.0 前提：搜索框、四颗 chip、还有那句「没有符合筛选条件的实例」都摆在这一页上");
        // 上面任何一格是空的，下面就没什么可驱动的了：树那几样（实例条目、组头）本来
        // 就吃得下空指针，搜索框和那四颗 chip 吃不下 —— 所以整段套在这一个条件里，
        // 红了就是上面那一条，不会在这里崩掉（**不是 return**：这一节后面还有别的节）。
        const bool wired = tree != nullptr && search != nullptr && noMatch != nullptr &&
                           chipVanilla != nullptr && chipModded != nullptr &&
                           chipUpdatable != nullptr && chipRecent != nullptr;
        if (wired) {

        // ---------- 读树的三只眼睛 ----------
        // 剩下的实例 id（**排过序**：底下那几行的顺序跟着目录列举顺序走，那是文件系统的
        // 事，不是产品语义）。
        const auto shownIds = [tree] {
            QStringList ids;
            for (QTreeWidgetItem *item : instanceItemsOf(tree)) {
                ids << item->data(0, MainWindowUI::instanceIdRole()).toString();
            }
            ids.sort();
            return ids;
        };
        const auto headersNow = [tree] { return groupHeadersOf(tree).join(QLatin1Char('|')); };
        const auto groupExpandedAt = [tree](int index) {
            QTreeWidgetItem *item = tree->topLevelItem(index);
            return item != nullptr && item->isExpanded();
        };
        // 「那一句话现在露着没有」问的是**它自己有没有被藏起来**（这一页不是当前那一页，
        // isVisible() 会答"没露"）—— 和 T36 读主页那张卡同一套，用 isVisibleTo。
        const auto noMatchShown = [noMatch] {
            return noMatch->isVisibleTo(noMatch->parentWidget());
        };
        const auto typeText = [search](const QString &text) {
            search->setText(text);
            QApplication::processEvents();
        };

        // ---------- ① 不筛的时候：原样 ----------
        check(instanceItemsOf(tree).size() == 3 && headersNow() == QStringLiteral("甲组 (1)|乙组 (1)|未分组 (1)"),
              "T37.0 前提：三个实例、三个组头都在（自定义组在前、「未分组」垫底）",
              QStringLiteral("%1 条 / %2").arg(instanceItemsOf(tree).size()).arg(headersNow()));
        check(!groupExpandedAt(0) && groupExpandedAt(1),
              "T37.0 前提：甲组收着、乙组开着（盘上那份 wtfml.folders 就是这么写的）");
        check(!noMatchShown(), "T37.0 一句话都不筛的时候，「没有符合筛选条件的实例」是收着的");
        const QByteArray foldersAfterOpen = readBytesAt(groupFile);

        // ---------- ② 搜索按版本号：剩下的组头只剩命中的那些 ----------
        typeText(QStringLiteral("1.18"));
        check(shownIds() == QStringList{QStringLiteral("1.18.2")},
              "★ T37.1 搜「1.18」⇒ 树上只剩 1.18.2 一条（「搜索版本号」）",
              shownIds().join(QLatin1Char('|')));
        check(headersNow() == QStringLiteral("甲组 (1)"),
              "★ T37.1 一条都没命中的组**整组不画** —— 不是留一个空组头让用户点开看",
              headersNow());
        check(groupExpandedAt(0),
              "★ T37.1 命中的那一条本来就是收在甲组里的 ⇒ 筛着的时候那一组**摆成展开的**"
              "（不展开的话用户看着像「搜不到」）");
        check(tree->topLevelItem(0) != nullptr &&
                  tree->topLevelItem(0)->data(0, MainWindowUI::groupIdRole()).toString() ==
                      collapsedGroup,
              "★ T37.1 而且它挂的还是**原来那一组**（「搜索不改变分组归属」）");
        check(folderLabel != nullptr &&
                  folderLabel->text().contains(QStringLiteral("（3 个实例，筛出 1 个）")),
              "★ T37.1 顶上那行说明**两个数都要**报（「3 个实例，筛出 1 个」）—— 只报筛出来"
              "那个数，用户会以为自己另外两份实例没了",
              folderLabel != nullptr ? folderLabel->text() : QStringLiteral("<没有那一行>"));

        // ---------- ③ 清空搜索：回到盘上记着的那个样子 ----------
        typeText(QString());
        check(instanceItemsOf(tree).size() == 3 &&
                  headersNow() == QStringLiteral("甲组 (1)|乙组 (1)|未分组 (1)"),
              "★ T37.2 清空搜索：三条都回来、组头照旧",
              QStringLiteral("%1 条 / %2").arg(instanceItemsOf(tree).size()).arg(headersNow()));
        check(!groupExpandedAt(0) && groupExpandedAt(1),
              "★ T37.2 而且**甲组回到收着的那个样子**（「清空搜索恢复原展开状态」）—— "
              "筛着的时候那一下展开是临时的，不落盘");
        check(readBytesAt(groupFile) == foldersAfterOpen,
              "★★ T37.2 这一路走下来 wtfml.folders **一个字节都没被重写**"
              "（搜一搜、点两下不是编辑分组）");
        check(folderLabel != nullptr &&
                  folderLabel->text().contains(QStringLiteral("（3 个实例）")) &&
                  !folderLabel->text().contains(QStringLiteral("筛出")),
              "T37.2 清空之后那行说明回到底数（「（3 个实例）」、不再提筛出几个）",
              folderLabel != nullptr ? folderLabel->text() : QStringLiteral("<没有那一行>"));

        // ---------- ④ 搜索：词与词是「与」，词与串是「或」 ----------
        typeText(QStringLiteral("1.18 1.18.2"));
        check(shownIds() == QStringList{QStringLiteral("1.18.2")},
              "★ T37.3 两个词都命中同一条 ⇒ 留下（真机上名字长成「我的整合包-1.18.2-Forge」，"
              "用户手里那几句本来就不在同一段里）",
              shownIds().join(QLatin1Char('|')));
        typeText(QStringLiteral("1.18.2 26.3"));
        check(instanceItemsOf(tree).isEmpty(),
              "★ T37.3 两个词要求的实例不一样 ⇒ **一条都不剩**（词与词之间是「与」）");
        check(noMatchShown() && noMatch->text() == MainWindowUI::instanceNoMatchText(),
              "★ T37.3 一条都没筛出来 ⇒ 当场把那句话摆出来（不是留一张空树让用户猜）",
              noMatchShown() ? noMatch->text() : QStringLiteral("（没露）"));
        typeText(QString());
        check(!noMatchShown() && instanceItemsOf(tree).size() == 3,
              "T37.3 清空之后那句话收回去、三条也回来了");

        // ---------- ⑤ 搜索按加载器（大小写随便） ----------
        typeText(QStringLiteral("forge"));
        check(shownIds() == QStringList{QStringLiteral("1.20.1")},
              "★ T37.4 搜加载器名也搜得到（点名的「加载器」；大小写用户不会去打对）",
              shownIds().join(QLatin1Char('|')));
        typeText(QString());

        // ---------- ⑥ 「原版」/「模组」是一对互斥的档 ----------
        chipModded->click();
        QApplication::processEvents();
        check(chipModded->isChecked() && !chipVanilla->isChecked(),
              "★ T37.5 点「模组」：这两颗**同时只能亮一颗**（一对反义词，两个都亮着读不出意思）");
        check(shownIds() == QStringList{QStringLiteral("1.20.1")},
              "★ T37.5 「模组」= 检测到加载器的那些",
              shownIds().join(QLatin1Char('|')));
        chipVanilla->click();
        QApplication::processEvents();
        check(chipVanilla->isChecked() && !chipModded->isChecked(),
              "★ T37.5 点「原版」：上一颗当场灭掉");
        check(shownIds() == QStringList({QStringLiteral("1.18.2"), QStringLiteral("26.3")}),
              "★ T37.5 「原版」= 没检测到加载器的那些（判据是加载器名，不是 mods 目录里有没有"
              "文件 —— 装过又删干净的实例仍然带着 Forge）",
              shownIds().join(QLatin1Char('|')));
        chipVanilla->click();
        QApplication::processEvents();
        check(!chipVanilla->isChecked() && instanceItemsOf(tree).size() == 3,
              "T37.5 同一颗再点一下 = 取消（两颗都灭 ⇒ 回到不筛）",
              QString::number(instanceItemsOf(tree).size()));

        // ---------- ⑦ 「可更新」认的是那一趟检查的结果 ----------
        chipUpdatable->click();
        QApplication::processEvents();
        check(instanceItemsOf(tree).isEmpty() && noMatchShown(),
              "★ T37.6 一个实例都**没查过更新**（`m_instanceOutdatedPaths` 里一个键都没有）"
              "⇒ 这一档一条都筛不出来，还老实说一句「没有符合筛选条件的实例」",
              QStringLiteral("%1 条").arg(instanceItemsOf(tree).size()));
        chipUpdatable->click();  // 取消
        QApplication::processEvents();
        w.seedOutdatedPathsForTest(QStringLiteral("1.18.2"),
                                   QStringList{QStringLiteral("C:/tf/假模组.jar.disabled")});
        chipUpdatable->click();
        QApplication::processEvents();
        check(shownIds() == QStringList{QStringLiteral("1.18.2")},
              "★ T37.6 摆上「1.18.2 查出来有一条能更多」⇒ 这一档只剩它"
              "（键在不在 = 查过没有；没查过的不许算进「有更新」）",
              shownIds().join(QLatin1Char('|')));
        typeText(QStringLiteral("26.3"));
        check(instanceItemsOf(tree).isEmpty(),
              "★ T37.6 搜索词和 chip 之间也是「与」（26.3 命中了搜索词，但它没有查出来可更新的"
              "东西）");
        typeText(QString());
        check(shownIds() == QStringList{QStringLiteral("1.18.2")},
              "T37.6 清掉搜索词：chip 那一档照旧只剩 1.18.2",
              shownIds().join(QLatin1Char('|')));
        chipUpdatable->click();
        QApplication::processEvents();
        check(instanceItemsOf(tree).size() == 3, "T37.6 取消「可更新」⇒ 三条都回来");

        // ---------- ⑧ 「最近运行」认的是启动账本 ----------
        w.seedLaunchRecordForTest(QStringLiteral("26.3"), QDateTime::currentMSecsSinceEpoch());
        chipRecent->click();
        QApplication::processEvents();
        check(shownIds() == QStringList{QStringLiteral("26.3")},
              "★ T37.7 「最近运行」= 启动账本（`wtfml.history`）上留下过记录的那些"
              "（账本只留最近二十来条，所以更早玩过的会从这一档里掉出去 —— 口径写在它的 tooltip 里）",
              shownIds().join(QLatin1Char('|')));
        chipRecent->click();
        QApplication::processEvents();
        check(instanceItemsOf(tree).size() == 3, "T37.7 取消 ⇒ 三条都回来");

        }  // if (wired)

        // ---------- 收尾：这一节自己用的那两个文件删干净 ----------
        // 和 T15/T17 末了那一句同一个要求：后面的节数实例、数分组都建立在"没分过组、没有账"
        // 之上。落在这儿的那个 `w` 是这一节自己的窗，删文件不影响它（它只读、不写）。
        QDir().remove(groupFile);
        QDir().remove(historyFile);
        check(!QFile::exists(groupFile) && !QFile::exists(historyFile),
              "T37.9 这一节自己那份 wtfml.folders 和那条启动记录都删干净了");
    }
    return 0;
}


