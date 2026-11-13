#include "Support.hpp"
int UiDownloadCategoriesChecks(MainWindowUI &ui, QStackedWidget * &gameStack, QWidget * &downloadPage, const std::function<QRect(QWidget *)> &rectInPage) {

    // 左栏从"挑加载器 + 挑版本"换成 PCL 那种**一列分类**：游戏 / 整合包 / 模组 /
    // 数据包 / 资源包 / 光影。点哪个、右栏那份 QStackedWidget 就翻到哪一页。
    //
    // NOTE: 这一节钉的是**结构本身**（条数、对应关系、只有哪一页有内容）；
    // "点一下真的会翻页"是接线的活，钉在 mw_check 那边（那边才搭 MainWindow）。
    // 这里钉住"第 i 个分类 = 第 i 页"这条不变量的前提（两边条数一样、第 0 页就是
    // 「游戏」那页），接线那条断言才有意义。
    {
        QListWidget *categoryList = ui.getDownloadCategoryList();
        QStackedWidget *stack = ui.getDownloadStack();
        const QStringList keys = MainWindowUI::downloadCategoryKeys();
        const QStringList texts = MainWindowUI::downloadCategoryTexts();
        check(categoryList != nullptr &&
                  categoryList->objectName() == MainWindowUI::downloadCategoryListName(),
              "左栏那份分类列表找得回来（按 objectName）",
              categoryList != nullptr ? categoryList->objectName() : QStringLiteral("<null>"));
        check(stack != nullptr && stack->objectName() == MainWindowUI::downloadStackName(),
              "右栏那份堆叠页找得回来（按 objectName）",
              stack != nullptr ? stack->objectName() : QStringLiteral("<null>"));

        // 两张表一一对应：长度一样、各自不重样。跟加载器那两张表同一个道理 ——
        // 哪张表多一条少一条，界面上就会有个分类既没键也对不上字。
        check(keys.size() == texts.size() && keys.size() >= 2 &&
                  QSet<QString>(keys.begin(), keys.end()).size() == keys.size() &&
                  QSet<QString>(texts.begin(), texts.end()).size() == texts.size(),
              "分类的键表和文字表一一对应、各自不重样",
              QString("keys=%1 texts=%2").arg(keys.size()).arg(texts.size()));
        check(!keys.isEmpty() && keys.value(0) == QStringLiteral("game") &&
                  texts.value(0) == QStringLiteral("游戏") &&
                  MainWindowUI::downloadCategoryGameIndex() == 0,
              "第 0 个是「游戏」（唯一现在就有内容的那个分类）",
              QStringLiteral("%1 / %2").arg(keys.value(0), texts.value(0)));

        // 「数据包」那一条的**位置**是用户点名的（要求：「下载那一栏少了个数据包
        // 类型，把它放在模组和资源包中间加上」）—— 所以钉的不是"表里有 datapack 这个键"
        // （那件事 kindFromCategoryKey 那条已经管了），而是它**夹在模组和资源包之间**。
        // 少了这一条，哪天有人顺手把新的一条追加到表尾、或者按字母重排一下，谁也不会发现。
        const int dataPackIndex = keys.indexOf(QStringLiteral("datapack"));
        check(dataPackIndex >= 0 && dataPackIndex == keys.indexOf(QStringLiteral("mod")) + 1 &&
                  keys.indexOf(QStringLiteral("resourcepack")) == dataPackIndex + 1,
              "「数据包」夹在「模组」和「资源包」中间（用户点名要的位置）",
              QStringLiteral("mod=%1 datapack=%2 resourcepack=%3")
                  .arg(keys.indexOf(QStringLiteral("mod")))
                  .arg(dataPackIndex)
                  .arg(keys.indexOf(QStringLiteral("resourcepack"))));
        check(texts.value(dataPackIndex) == QStringLiteral("数据包"),
              "那一格写的字是「数据包」（和键 datapack 一条对一个）",
              dataPackIndex >= 0 && dataPackIndex < texts.size() ? texts.value(dataPackIndex)
                                                                 : QStringLiteral("<越界>"));

        // 条目：**文字**给用户看，**键**进 userData 当身份。不能拿文字当身份 ——
        // 那是产品文案，改一个字的说法就会把认条目的代码全打断。
        check(categoryList != nullptr && categoryList->count() == keys.size(),
              "分类条目数和键表一样多",
              categoryList != nullptr ? QString::number(categoryList->count()) : QString());
        bool itemsMatch = categoryList != nullptr && categoryList->count() == keys.size();
        for (int i = 0; i < keys.size() && categoryList != nullptr; ++i) {
            const QListWidgetItem *item = categoryList->item(i);
            itemsMatch = itemsMatch && item != nullptr && item->text() == texts.value(i) &&
                         item->data(Qt::UserRole).toString() == keys.value(i);
        }
        check(itemsMatch, "每个条目的字和键都对着表（身份在那个键上，不在文字上）");

        // 右栏页数 == 分类数，而且第 0 页就是「游戏」那一页 —— 这两条合起来才是
        // "第 i 个分类 = 第 i 页"。少一条，接线那边 setCurrentIndex(row) 就会翻错页。
        check(stack != nullptr && stack->count() == keys.size(),
              "右栏页数和分类数一样多（第 i 个分类 = 第 i 页）",
              stack != nullptr ? QString::number(stack->count()) : QString());
        check(stack != nullptr && stack->count() > 0 &&
                  stack->widget(MainWindowUI::downloadCategoryGameIndex()) != nullptr &&
                  stack->widget(MainWindowUI::downloadCategoryGameIndex())->objectName() ==
                      MainWindowUI::downloadGamePageName(),
              "第 0 页是「游戏」那一页（就是原来那一摊）");
        // 「游戏」那一页里装的是那两层（版本树 / 安装页）。老的那三件（下拉框、列表、
        // 「下载版本」按钮） 全撤了，换成树 + 安装页 —— 这里钉新东西在，
        // 老东西**不在**（免得哪天又被摆回来，两套并存比哪一套都糟）。
        check(stack != nullptr && stack->count() > 0 &&
                  stack->widget(MainWindowUI::downloadCategoryGameIndex())
                          ->findChild<QTreeWidget *>(MainWindowUI::versionTreeName()) != nullptr &&
                  stack->widget(MainWindowUI::downloadCategoryGameIndex())
                          ->findChild<QWidget *>(MainWindowUI::downloadGameStackName()) != nullptr,
              "「游戏」那一页里有版本树和它那两层堆叠");
        // **树那一层**上没有下拉框、也没有列表：那一层就是一栏版本号，摆别的东西都
        // 说明老控件又回来了（或者有人往树页上加了东西）。
        check(gameStack != nullptr &&
                  gameStack->widget(MainWindowUI::downloadGameTreeIndex())->findChild<QComboBox *>() ==
                      nullptr &&
                  gameStack->widget(MainWindowUI::downloadGameTreeIndex())
                          ->findChild<QListWidget *>() == nullptr,
              "版本树那一层上没有下拉框、也没有列表（那两件已经撤了）");
        check(gameStack != nullptr && stack != nullptr && stack->count() > 0 &&
                  isDescendantOf(gameStack, stack->widget(0)),
              "那两层堆叠也在「游戏」那一页里");

        // 其余几页：**一行筛子 + 一棵三层树**（资源 → 游戏版本 → 版本号）。
        // ① 名称框（它同时是筛子里的「名称」那一格）：
        // 按 objectName 找得回来，提示文字非空、而且是这一页那一句；
        // ② 一棵树（按 objectName 找得回来），形状和上一页那两棵一样（表头藏着、根不装饰、
        // 一列、代理是 VersionTreeDelegate）；
        // ③ 筛子那一行上的东西**一个不多一个不少**：名称框、版本/来源/类型三个下拉框、
        // 「搜索」「清空条件」两颗键（模组页多一个模组加载器下拉框、整合包页多一颗
        // 「导入整合包…」）—— 按名字把这一页上该有的一件件数出来比对，少一件多一件都红。
        // 「入口先留着、别放东西」那条精神还在：这几页的形状是**唯一**的。
        bool pagesOk = stack != nullptr;
        bool searchEditsOk = stack != nullptr;
        bool treesOk = stack != nullptr;
        bool filterRowsOk = stack != nullptr;
        bool filterTextsOk = stack != nullptr;
        bool filterGuardOk = stack != nullptr;
        bool filterMaxWidthOk = stack != nullptr;
        QString filterRowProblem;
        for (int i = 0; i < keys.size() && stack != nullptr; ++i) {
            // 「游戏」和「服务端」都不是资源搜索页（前者是版本树 + 安装页，后者只有一句
            // 说明 + 一颗「新建服务端…」），这一节只量后面那几张资源页。
            if (i == MainWindowUI::downloadCategoryGameIndex() ||
                i == MainWindowUI::downloadCategoryServerIndex()) {
                continue;
            }
            QWidget *page = stack->widget(i);
            if (page == nullptr) {
                pagesOk = false;
                continue;
            }
            const QString key = keys.value(i);

            // ① 名称框 + 版本那个**可编辑**下拉框
            auto *search = page->findChild<QLineEdit *>(MainWindowUI::downloadSearchEditName(key));
            auto *versionCombo = page->findChild<QComboBox *>(
                MainWindowUI::downloadFilterVersionComboName(key));
            const QString hint = MainWindowUI::downloadSearchHint(i);
            // 页上的 QLineEdit **正好两个**：名称框，和版本那个可编辑下拉框内部的文本框
            // （`QComboBox::lineEdit()`）。多出来的那个不是谁漏删的，正是"版本可自行输入"
            // 这条要求本身。判据写成"这两个、而且就是这两个"，多一个少一个都还是要红。
            const QList<QLineEdit *> edits = page->findChildren<QLineEdit *>();
            searchEditsOk = searchEditsOk && search != nullptr && !hint.isEmpty() &&
                            search->placeholderText() == hint && versionCombo != nullptr &&
                            versionCombo->isEditable() && versionCombo->lineEdit() != nullptr &&
                            versionCombo->placeholderText() ==
                                MainWindowUI::downloadFilterVersionPlaceholder() &&
                            // 可输入的框上"回车就把框里的字追进清单"这条**必须**关掉
                            // （Qt 的默认 InsertAtBottom：手打一个版本号按一次回车就多一条，
                            // 而追进去的那一项永远也搜不出东西来）。行为那一半在
                            // mw_check 的 ZI10 里量过（按回车前后项数不变）。
                            versionCombo->insertPolicy() == QComboBox::NoInsert &&
                            edits.size() == 2 && edits.contains(versionCombo->lineEdit());

            // ② 那棵树
            auto *tree = page->findChild<QTreeWidget *>(MainWindowUI::downloadResourceTreeName(key));
            bool treeShapeOk = tree != nullptr && tree->isHeaderHidden() &&
                               !tree->rootIsDecorated() && tree->columnCount() == 1 &&
                               tree->topLevelItemCount() == 0;
            // 代理：**同一个**（版本树/加载器树那一个），而且两个开关都是这一页要的值 ——
            // 图标 32 px（要看清缩略图）、叶子画箭头（第三层点一下是"下它"）。
            // 只验"代理是 VersionTreeDelegate"是不够的：那两个开关接错了同样能过。
            auto *delegate = tree == nullptr ? nullptr : qobject_cast<VersionTreeDelegate *>(
                                                   tree->itemDelegate());
            treeShapeOk = treeShapeOk && delegate != nullptr &&
                          delegate->iconSize() == MainWindowUI::resourceTreeIconSize() &&
                          delegate->rowIconSize() == MainWindowUI::resourceTreeIconSize() &&
                          delegate->leafArrow() && delegate->leafVerb() == QStringLiteral("下载");
            treesOk = treesOk && treeShapeOk;

            // ③ 筛子那一行：该有的那几件按名字数一遍
            QStringList expected;
            expected << MainWindowUI::downloadSearchEditName(key)
                     << MainWindowUI::downloadFilterVersionComboName(key)
                     << MainWindowUI::downloadFilterSourceComboName(key)
                     << MainWindowUI::downloadFilterTypeComboName(key)
                     << MainWindowUI::downloadFilterSearchButtonName(key)
                     << MainWindowUI::downloadFilterResetButtonName(key);
            // NOTE: 「← 返回实例设置」那颗**不在这份名单里**：它长在左栏顶上，成了整个
            // 下载页**共用的一颗**，不属于任何一页（上面那一节钉的就是这一条）。
            // 「模组加载器」那一栏**只有模组页有**（要求）。判据是下标表里那一条，
            // 不是"这个键里有没有 mod 三个字母"那种猜法。
            const bool isModPage = i == MainWindowUI::downloadCategoryModIndex();
            const bool isModpackPage = i == MainWindowUI::downloadCategoryModpackIndex();
            if (isModPage) {
                expected << MainWindowUI::downloadFilterLoaderComboName(key);
            }
            // 「导入整合包…」那颗键的 objectName **不带分类键**（它只有一处），所以这里
            // 直接按那个名字记一笔 —— 记在整合包那一页的预期里，就等于"只有那一页有它"。
            if (isModpackPage) {
                expected << MainWindowUI::downloadFilterImportModpackButtonName();
            }
            QStringList actual;
            for (QWidget *child : page->findChildren<QWidget *>()) {
                // 只数"看得见的那几种控件"里的**带名字的**：下拉框内部那些零件、代理建出来的
                // 临时编辑框、树上的东西都没有 objectName，也就进不来。
                const bool counted = qobject_cast<QPushButton *>(child) != nullptr ||
                                     qobject_cast<QComboBox *>(child) != nullptr ||
                                     qobject_cast<QListWidget *>(child) != nullptr ||
                                     qobject_cast<QLineEdit *>(child) != nullptr;
                if (counted && !child->objectName().isEmpty()) {
                    actual << child->objectName();
                }
            }
            std::sort(expected.begin(), expected.end());
            std::sort(actual.begin(), actual.end());
            if (actual != expected && filterRowProblem.isEmpty()) {
                // 记下**是哪一页**、差在哪儿 —— 只报一句"对不上"的话，得五页一页页翻。
                filterRowProblem = QStringLiteral("%1：该有 [%2] 实际 [%3]")
                                       .arg(key, expected.join(QStringLiteral(",")),
                                            actual.join(QStringLiteral(",")));
            }
            filterRowsOk = filterRowsOk && actual == expected;

            // 两颗键上的字（要求「一个搜索键和一个清空条件键」）
            auto *searchButton = page->findChild<QPushButton *>(
                MainWindowUI::downloadFilterSearchButtonName(key));
            auto *resetButton = page->findChild<QPushButton *>(
                MainWindowUI::downloadFilterResetButtonName(key));
            filterTextsOk = filterTextsOk && searchButton != nullptr && resetButton != nullptr &&
                            searchButton->text() == MainWindowUI::downloadFilterSearchText() &&
                            resetButton->text() == MainWindowUI::downloadFilterResetText() &&
                            searchButton->text() != resetButton->text();

            // 四个下拉框都装了滚轮防护（理由见 wheelGuardProblem() 那段）。
            // 「模组加载器」那一个**只往模组页的名单里加** —— 别的页压根没这个控件，
            // 一起查的话报的是"控件没找到"，那是在查一件不存在的东西。
            QStringList comboNames{
                MainWindowUI::downloadFilterVersionComboName(key),
                MainWindowUI::downloadFilterSourceComboName(key),
                MainWindowUI::downloadFilterTypeComboName(key)};
            if (isModPage) {
                comboNames << MainWindowUI::downloadFilterLoaderComboName(key);
            }
            for (const QString &comboName : comboNames) {
                const QString problem =
                    wheelGuardProblem(page->findChild<QComboBox *>(comboName));
                filterGuardOk = filterGuardOk && problem.isEmpty();
            }
            // 四个框的**宽度上限**也在这儿钉一下（它和上面那两条一起才把这一行的宽度钉住）：
            // 只设 minimumContentsLength 是"有多宽"，去掉 setMaximumWidth 的话整窗 sizeHint
            // 会从 862 涨到 934 —— 那是 E5 那条断言的红，而红在**另一个用例**上，报的话是
            // "窗口 sizeHint 顶过 900"，看不出是这一行干的。这里单独记一笔，红了直接指到这儿。
            for (const QString &comboName : comboNames) {
                QComboBox *combo = page->findChild<QComboBox *>(comboName);
                filterMaxWidthOk = filterMaxWidthOk && combo != nullptr &&
                                   combo->maximumWidth() ==
                                       MainWindowUI::downloadFilterComboMaxWidth() &&
                                   combo->minimumContentsLength() ==
                                       MainWindowUI::downloadFilterMinimumContentsLength();
            }

            pagesOk = pagesOk && search != nullptr && tree != nullptr;
        }
        check(pagesOk, "其余几页每页都有一个搜索框 + 一棵资源树（点进去不是一片空白）");
        check(searchEditsOk,
              "几页的名称框提示文字就是这一页那一句，版本那一格可编辑（能自己打）、而且回车"
              "**不往清单里追项**（InsertPolicy 是 NoInsert）");
        check(treesOk, "那几页那棵树：表头藏着、根不装饰、一列、用同一个代理、图标 32、叶子画箭头");
        check(filterRowsOk,
              "那一行筛子上的控件一个不多一个不少（模组加载器只在模组页、导入口只在整合包页）",
              filterRowProblem);
        check(filterTextsOk, "「搜索」和「清空条件」两颗键都在、字就是那两句、而且不是同一句");
        check(filterGuardOk, "筛子那四个下拉框都装了滚轮防护");
        check(filterMaxWidthOk,
              "★ 那四个下拉框的宽度上下限都设了（去掉上限整窗 sizeHint 会从 862 涨到 934）");
        if (stack && ui.getTabWidget() && downloadPage) {
            const int previousTab = ui.getTabWidget()->currentIndex();
            const int previousPage = stack->currentIndex();
            ui.getTabWidget()->setCurrentWidget(downloadPage);
            stack->setCurrentIndex(MainWindowUI::downloadCategoryModIndex());
            QApplication::processEvents();
            auto *page = stack->currentWidget();
            auto *type = page->findChild<QComboBox *>(MainWindowUI::downloadFilterTypeComboName(QStringLiteral("mod")));
            auto *label = type ? type->parentWidget()->findChild<QLabel *>() : nullptr;
            check(type && label && label->text() == MainWindowUI::downloadFilterTypeLabel() &&
                      qAbs(label->geometry().center().y() - type->geometry().center().y()) <= 1,
                  "筛选类型标签属于同一布局并与下拉框垂直对齐");
            stack->setCurrentIndex(previousPage);
            ui.getTabWidget()->setCurrentIndex(previousTab);
        }

        // 「模组加载器那一栏只存在于模组页」的另一半：**按访问器取**也一样 ——
        // 光有上面那个"页面上数不出来"还不够，MainWindow 灌数据时读的正是这个访问器，
        // 它要是随便回一个别的页的框，用户就会在资源包那一页上看到一个模组加载器的筛子。
        {
            bool loaderOnlyOnMod = true;
            bool typeBoxEverywhere = true;
            QString detail;
            for (int i = 0; i < keys.size(); ++i) {
                // 「游戏」「服务端」都没有筛子（后者连搜索框都没有），跳过它们 ——
                // 一起查的话报的是"控件没找到"，那是在查一件不存在的东西。
                if (i == MainWindowUI::downloadCategoryGameIndex() ||
                    i == MainWindowUI::downloadCategoryServerIndex()) {
                    continue;
                }
                const QString key = keys.value(i);
                const bool isModPage = i == MainWindowUI::downloadCategoryModIndex();
                QComboBox *loaderCombo = ui.getDownloadFilterLoaderCombo(key);
                if ((loaderCombo != nullptr) != isModPage) {
                    loaderOnlyOnMod = false;
                    detail = QStringLiteral("%1：%2")
                                 .arg(key, loaderCombo == nullptr ? QStringLiteral("没有")
                                                                  : QStringLiteral("有"));
                }
                // 「类型」那一栏的**小组**（标签 + 框）每一页都得有：藏不藏是运行时的事，
                // 控件本身一页都不能少（少了 MainWindow 连"要不要藏"都没得判）。
                typeBoxEverywhere =
                    typeBoxEverywhere && ui.getDownloadFilterTypeBox(key) != nullptr &&
                    ui.getDownloadFilterTypeCombo(key) != nullptr;
            }
            check(loaderOnlyOnMod,
                  "★ 模组加载器那一栏按访问器取也只在模组页有（别的页回空）", detail);
            check(typeBoxEverywhere, "「类型」那一栏的小组每一页都在（藏不藏是运行时的事）");
            // 「游戏」那一页一件筛子都没有（它没有搜索框，也没有可筛的东西）
            check(ui.getDownloadFilterVersionCombo(keys.value(MainWindowUI::downloadCategoryGameIndex())) ==
                          nullptr &&
                      ui.getDownloadFilterTypeBox(keys.value(MainWindowUI::downloadCategoryGameIndex())) ==
                          nullptr,
                  "「游戏」那一页上没有筛子（按它的键一件都取不到）");
            // 整合包那一页那颗「导入整合包…」：**有、而且字就是那一句**（和实例页那颗同一个
            // 出处 importModpackText()，一个"导入整合包…"不该有两种说法）。
            QPushButton *importButton = ui.getDownloadFilterImportModpackButton();
            check(importButton != nullptr && importButton->text() == MainWindowUI::importModpackText(),
                  "★ 整合包那一页那颗「导入整合包…」在、字和实例页那颗同一句",
                  importButton == nullptr ? QStringLiteral("没找到")
                                          : importButton->text());
            // 它**不是**实例页那一颗：两颗按钮的 objectName 必须不一样，不然 findChild
            // 找到哪一颗看运气。
            check(importButton != nullptr &&
                      importButton->objectName() != MainWindowUI::importModpackButtonName(),
                  "★ 下载页那颗「导入整合包…」和实例页那颗是两个不同的 objectName",
                  importButton == nullptr ? QStringLiteral("没找到") : importButton->objectName());
        }

        // ---- 「服务端」那一栏 ----
        //
        // 它和「游戏」一样**不是资源搜索页**：没有搜索框、没有三层树、没有筛子，
        // 也没有行尾/页头那颗「刷新」（它不向任何资源站问东西）。那一页只有一句说明
        // + 一颗「新建服务端…」，按下去走的路和实例页那颗完全一样。
        // 这一节钉的是"里头到底摆了哪几件"——接线的活（按下去真的开了那一页）在 mw_check。
        {
            const int serverIndex = MainWindowUI::downloadCategoryServerIndex();
            check(serverIndex >= 0 && serverIndex < keys.size() &&
                      keys.value(serverIndex) == QStringLiteral("server") &&
                      texts.value(serverIndex) == QStringLiteral("服务端"),
                  "服务端位于游戏之后（键 server / 字服务端）",
                  QStringLiteral("index=%1 key=%2 text=%3")
                      .arg(serverIndex)
                      .arg(keys.value(serverIndex), texts.value(serverIndex)));
            QWidget *page = stack->widget(serverIndex);
            check(page != nullptr && page->objectName() == MainWindowUI::downloadServerPageName(),
                  "★ 那一栏有自己的一页（objectName 按约定）",
                  page != nullptr ? page->objectName() : QStringLiteral("<null>"));
            auto *hint = page == nullptr
                             ? nullptr
                             : page->findChild<QLabel *>(MainWindowUI::downloadServerHintName());
            auto *newServer = page == nullptr
                                  ? nullptr
                                  : page->findChild<QPushButton *>(
                                        MainWindowUI::downloadNewServerButtonName());
            check(hint != nullptr,
                  "★ 那一页那句说明逐字就是约定的那一句（装好之后去「管理 → 服务端」）",
                  hint != nullptr ? hint->text() : QStringLiteral("<没找到>"));
            check(newServer != nullptr && newServer->text() == MainWindowUI::newServerText(),
                  "★ 那一页那颗「新建服务端…」在、字和实例页那颗同一句",
                  newServer != nullptr ? newServer->text() : QStringLiteral("<没找到>"));
            check(ui.getDownloadNewServerButton() == newServer,
                  "★ 按访问器取回来的就是那一颗（接线读的正是它）");
            auto *serverVersions = page->findChild<QTreeWidget *>(QStringLiteral("serverVersionTree"));
            auto *serverSearch = page->findChild<QLineEdit *>(QStringLiteral("serverVersionFilter"));
            check(serverIndex == MainWindowUI::downloadCategoryGameIndex() + 1 && serverVersions && serverSearch && newServer->isHidden(),
                  "server category follows game and offers version selection instead of an extra entry button");
            if (serverVersions && serverSearch) {
                const int previousPage = stack->currentIndex();
                stack->setCurrentIndex(MainWindowUI::downloadCategoryServerIndex());
                auto *group = new QTreeWidgetItem(ui.getVersionTree());
                group->setText(0, QStringLiteral("Test servers"));
                auto *leaf = new QTreeWidgetItem(group);
                leaf->setText(0, QStringLiteral("1.21.1-server-probe"));
                leaf->setData(0, Qt::UserRole, leaf->text(0));
                for (int i = 0; i < 3; ++i) QApplication::processEvents();
                const auto found = serverVersions->findItems(leaf->text(0), Qt::MatchExactly | Qt::MatchRecursive);
                check(found.size() == 1, "server versions reuse the game's fetched metadata");
                serverSearch->setText(QStringLiteral("server-probe"));
                check(found.size() == 1 && !found.first()->isHidden(), "server version search keeps the matching version");
                QObject scope;
                int opened = 0;
                QObject::connect(newServer, &QPushButton::clicked, &scope, [&opened] { ++opened; });
                if (!found.isEmpty()) {
                    serverVersions->setCurrentItem(found.first());
                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                    QApplication::sendEvent(serverVersions, &enter);
                }
                check(opened == 1 && newServer->property("selectedVersion").toString() == leaf->text(0),
                      "server activation opens configuration once with the selected game version");
                const int before = serverVersions->property("syncRevision").toInt();
                stack->setCurrentIndex(MainWindowUI::downloadCategoryGameIndex());
                leaf->setText(0,QStringLiteral("updated-server-probe"));
                for (int i=0;i<3;++i) QApplication::processEvents();
                check(serverVersions->property("syncRevision").toInt()==before,"hidden service catalog does not rebuild during game-page updates");
                stack->setCurrentIndex(MainWindowUI::downloadCategoryServerIndex());
                for (int i=0;i<3;++i) QApplication::processEvents();
                check(serverVersions->property("syncRevision").toInt()==before+1,"opening service catalog coalesces pending changes into one rebuild");
                stack->setCurrentIndex(MainWindowUI::downloadCategoryGameIndex());stack->setCurrentIndex(MainWindowUI::downloadCategoryServerIndex());
                for (int i=0;i<3;++i) QApplication::processEvents();
                check(serverVersions->property("syncRevision").toInt()==before+1,"unchanged service catalog is reused on subsequent navigation");
                serverSearch->clear(); delete group;
                for (int i = 0; i < 3; ++i) QApplication::processEvents();
                stack->setCurrentIndex(previousPage);
            }

            // 资源搜索那一套在这一页上**一件都没有** —— 少这一条，哪天有人把服务端
            // 当成第六张资源表塞进那个循环，界面上会长出一整行筛子 + 一棵空树。
            check(ui.getDownloadSearchEdit(QStringLiteral("server")) == nullptr &&
                      ui.getDownloadResourceTree(QStringLiteral("server")) == nullptr &&
                      ui.getDownloadFilterVersionCombo(QStringLiteral("server")) == nullptr &&
                      ui.getDownloadFilterTypeBox(QStringLiteral("server")) == nullptr,
                  "服务端版本选择不使用资源搜索页控件");
            // 页头那句说明也得是这一栏自己的（别掉到「游戏」那句上）。
            check(MainWindowUI::downloadHeaderSubtitle(serverIndex) ==
                      QStringLiteral("新建服务端；安装完成后可在「管理 → 服务端」中启动并查看控制台"),
                  "★ 页头说明在「服务端」那一栏说的是这一页能干什么",
                  MainWindowUI::downloadHeaderSubtitle(serverIndex));
            // 页头那颗「刷新」在服务端那一栏**收起来**（那一页没有"重新问一遍"这回事）。
            QPushButton *headerRefresh = ui.getDownloadHeaderRefreshButton();
            ui.setDownloadHeaderRow(serverIndex);
            const bool visibleOnServer = headerRefresh != nullptr && !headerRefresh->isHidden();
            ui.setDownloadHeaderRow(MainWindowUI::downloadCategoryGameIndex());
            check(headerRefresh != nullptr && visibleOnServer && !headerRefresh->isHidden(),
                  "游戏与服务端版本列表都可刷新",
                  headerRefresh == nullptr
                      ? QStringLiteral("<没找到>")
                      : QStringLiteral("服务端上藏着=%1 游戏上藏着=%2")
                            .arg(visibleOnServer)
                            .arg(headerRefresh->isHidden()));
        }

        // 「游戏」那页**不在**那几页里：它的形状是另外一套（版本树 + 安装页），
        // 所以按它的键去找搜索框/资源树必须都找不到。
        // NOTE: 不能断言"那一页上没有 QLineEdit" —— 它上面**有**一个（安装页那个实例名
        // 输入框），那是有意为之的，不是残留。
        check(MainWindowUI::downloadSearchHint(MainWindowUI::downloadCategoryGameIndex()).isEmpty(),
              "「游戏」那一页没有搜索提示（它那页的搜索框本来就不存在）",
              MainWindowUI::downloadSearchHint(MainWindowUI::downloadCategoryGameIndex()));
        check(ui.getDownloadSearchEdit(keys.value(MainWindowUI::downloadCategoryGameIndex())) ==
                      nullptr &&
                  ui.getDownloadResourceTree(keys.value(MainWindowUI::downloadCategoryGameIndex())) ==
                      nullptr,
              "按「游戏」那个键找不到搜索框、也找不到资源树（那两样只在后面几页上）");

        // **最有价值的一条**：界面上"点的是哪一类"和"去问哪一类"是靠一个字符串对起来的
        // （分类键 == ResourceCatalog::kindKey）。两边各自都能通过自己的断言，却对不上 ——
        // 那种错法是静默的：点「光影」搜出来的是一堆模组。
        {
            const QList<ResourceCatalog::Kind> kinds = ResourceCatalog::allKinds();
            // 分类表里有两栏不是资源搜索页：「游戏」和「服务端」，所以是对 kind 数 + 2。
            bool sameSet = kinds.size() == keys.size() - 2;
            QStringList kindKeys;
            for (ResourceCatalog::Kind kind : kinds) {
                kindKeys.append(ResourceCatalog::kindKey(kind));
            }
            for (int i = 0; i < keys.size(); ++i) {
                if (i == MainWindowUI::downloadCategoryGameIndex() ||
                    i == MainWindowUI::downloadCategoryServerIndex()) {
                    continue;
                }
                ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
                sameSet = sameSet && ResourceCatalog::kindFromCategoryKey(keys.value(i), &kind) &&
                          ResourceCatalog::kindKey(kind) == keys.value(i);
            }
            check(sameSet, "分类键和 ResourceCatalog 的那几类一一对上（点哪一类就问哪一类）",
                  kindKeys.join(QStringLiteral(" / ")));
            // 反过来的那一面：「游戏」那一页**不是**这几类之一（拿它去问就会问错东西）。
            ResourceCatalog::Kind unused = ResourceCatalog::Kind::Mod;
            check(!ResourceCatalog::kindFromCategoryKey(keys.value(0), &unused),
                  "「游戏」不在那几类里（它不走 Modrinth 那条路）");
        }

        // 打开下载页时停在「游戏」（= 原来那个行为，不是停在一个"还没做"的页上）。
        check(categoryList != nullptr &&
                  categoryList->currentRow() == MainWindowUI::downloadCategoryGameIndex(),
              "打开下载页时左栏停在「游戏」",
              categoryList != nullptr ? QString::number(categoryList->currentRow()) : QString());
        check(stack != nullptr && stack->currentIndex() == MainWindowUI::downloadCategoryGameIndex(),
              "右栏一开始显示的就是「游戏」那一页",
              stack != nullptr ? QString::number(stack->currentIndex()) : QString());

        // 几何：分类列表**整个在堆叠页左边**（左栏是"选哪个分类"，右栏是"那个分类的内容"）。
        // UI 7b（设计稿 download-*）：分类改成堆叠页上方一排 chip（左栏那列收起、身份照旧由它管）。
        const QRect catRect = rectInPage(categoryList);
        const QRect stackRect = rectInPage(stack);
        QWidget *chipRow = downloadPage->findChild<QWidget *>(MainWindowUI::downloadCategoryChipsName());
        const QRect chipRect = chipRow != nullptr ? rectInPage(chipRow) : QRect();
        check(!chipRect.isNull() && !stackRect.isNull() && chipRect.bottom() < stackRect.top() &&
                  categoryList != nullptr && !categoryList->isVisibleTo(downloadPage),
              "分类 chip 那一排整个在堆叠页上方，左栏那列收着",
              QString("chips.bottom=%1 stack.top=%2").arg(chipRect.bottom()).arg(stackRect.top()));
        // chip 和列表双向同步：点第 2 颗 = 列表当前行变 2；列表设回游戏 = 第 0 颗亮。
        if (chipRow != nullptr && categoryList != nullptr) {
            auto *chip2 = chipRow->findChild<QPushButton *>(MainWindowUI::downloadCategoryChipName(2));
            auto *chip0 = chipRow->findChild<QPushButton *>(MainWindowUI::downloadCategoryChipName(0));
            if (chip2 != nullptr) chip2->click();
            const int afterClick = categoryList->currentRow();
            categoryList->setCurrentRow(MainWindowUI::downloadCategoryGameIndex());
            check(afterClick == 2 && chip0 != nullptr && chip0->isChecked() && chip2 != nullptr && !chip2->isChecked(),
                  "点 chip 翻分类、列表翻回「游戏」那颗 chip 跟着亮（两边是同一份状态）",
                  QString::number(afterClick));
        }
        // 高度贴着那几条，不是被拉满整条左栏 —— 五个两三个字的条目撑成一整列空白，
        // 看着像坏了（还容易让人以为底下有东西没显示出来）。
        check(!catRect.isNull() && catRect.height() < downloadPage->height() - 40,
              "分类列表的高度贴着那几条，没被拉满整栏",
              QString("list.h=%1 page.h=%2").arg(catRect.height()).arg(downloadPage->height()));
    }
 return 0;
}


