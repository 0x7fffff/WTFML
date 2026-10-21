#include "Support.hpp"
int MwDownloadNavigationChecks(MainWindow &window) {
    // ------------------------------------------------------------------
    // 左栏从"挑加载器 + 挑版本"换成 PCL 那种一列分类（游戏 / 整合包 / 模组 / 资源包 /
    // 光影）。**结构**那几条（条数、文字、只有「游戏」那页有内容）钉在 ui_check 那边；
    // 这里钉的是**接线**：真窗口上点一下，右栏真的跟着翻。
    //
    // 这条值得单独钉，是因为"界面搭对了"和"接上线了"是两件事：QStackedWidget 少一根
    // connect，界面照样搭得漂漂亮亮、ui_check 照样全绿，只是**点了没反应** —— 而
    // "点了没反应"这种毛病用户不会报，他只会觉得这个启动器是坏的。
    //
    // 接线上还多了一件事：切到资源的四页**会自己去问一次"最热门的"**（那四页的第一眼
    // 就是结果，不是空树）。所以这一节要读的是"切过去当下"的状态 —— 见下面那段 NOTE。
    //
    // NOTE: **mw_check 不是离线的**：这一节每跑一次会真的往
    // api.modrinth.com 发四次搜索（每页一次）。断网时也不会红 —— 那一行占位的
    // 「正在获取…」跟回复到没到没关系，它就是"发出去那一刻"的东西；断网的结果
    // 是过了一会儿它换成一行「搜索失败：…」（那句只在树上，不弹窗，见
    // onResourceSearchFinished 里那段 NOTE）。
    {
        auto *categoryList =
            byObjectName<QListWidget>(&window, MainWindowUI::downloadCategoryListName());
        auto *stack = byObjectName<QStackedWidget>(&window, MainWindowUI::downloadStackName());
        // 「游戏」那一页上那个"选了哪个加载器"现在住在那棵树上（那一行第二行的小字），
        // 转一圈回来要比的是它。
        auto *loaderTree = byObjectName<QTreeWidget>(&window, MainWindowUI::loaderTreeName());
        // 「游戏」那一页上现在有状态的是**版本树**（还有它里头的展开/选中）—— 转一圈
        // 分类回来要验的正是"这一页上的东西一个都没被碰过"。
        auto *tree = byObjectName<QTreeWidget>(&window, MainWindowUI::versionTreeName());
        const QStringList keys = MainWindowUI::downloadCategoryKeys();
        const int gameIndex = MainWindowUI::downloadCategoryGameIndex();
        check(categoryList != nullptr && stack != nullptr,
              "分类列表和右栏那份堆叠页都能按 objectName 找回来",
              QString("列表 %1 / 堆叠页 %2")
                  .arg(categoryList != nullptr ? QStringLiteral("有") : QStringLiteral("没有"))
                  .arg(stack != nullptr ? QStringLiteral("有") : QStringLiteral("没有")));

        // NOTE: 跟 Y 段同一个规矩 —— 控件找不齐就只留下上面那一条失败，不再往下戳
        // nullptr。找不齐的典型原因是有人改了 objectName 或把控件挪了家，
        // 那时候后面十几条一起红只会淹没真正的原因。
        if (categoryList != nullptr && stack != nullptr && stack->count() == keys.size() &&
            categoryList->count() == keys.size()) {
            // 开局：停在「游戏」（打开下载页看到的就是原来那一摊）。
            check(categoryList->currentRow() == gameIndex && stack->currentIndex() == gameIndex,
                  "开局停在「游戏」那一页",
                  QString("row=%1 page=%2")
                      .arg(categoryList->currentRow())
                      .arg(stack->currentIndex()));

            // 换一圈分类之前先把「游戏」那页上的选择记下来 —— 转一圈回来要比。
            // 加载器那一侧记的是**每一行第二行的小字**（选中那条写自己钉的版本号、
            // 其余几条写「与 X 不兼容」）：这一串正是用户看得见的"现在选的是哪个"。
            QStringList loaderBefore;
            for (int i = 0; loaderTree != nullptr && i < loaderTree->topLevelItemCount(); ++i) {
                loaderBefore << loaderTree->topLevelItem(i)
                                    ->data(MainWindowUI::loaderTreeKeyColumn(),
                                           MainWindowUI::rowSubTextRole())
                                    .toString();
            }
            // 「游戏」那页上"选中的东西"现在是树里那个条目（离线时树可以是空的，
            // 空树时这一条退化成"两边都是 -1"，仍然成立）。
            const int treeRowBefore = tree != nullptr ? tree->indexOfTopLevelItem(
                                                            tree->currentItem()) : -999;

            // 逐条点一遍：**第 i 个分类 = 第 i 页**这条走动起来也得成立。
            // NOTE: 用 setCurrentRow 而不是伪造鼠标事件 —— Qt 里鼠标点条目最后落到的
            // 就是它（QListWidget 的按下处理走的是同一条路），信号路径完全一样。
            int switched = 0;
            for (int i = 0; i < categoryList->count() && i < stack->count(); ++i) {
                categoryList->setCurrentRow(i);

                // 「切到这一页」的那一下就该看见界面在动了。第 0 页「游戏」是原来那一摊
                // （没有搜索框、没有资源树），其余四页在切过去的那一刻就自己去问一次
                // "最热门的" —— ensureResourcePageLoaded() 就接在切分类那条路上。
                //
                // NOTE: 这一段**必须在下面的 processEvents() 之前**读。setCurrentRow()
                // 是同步一路走到 startResourceSearch() 的，这会儿树里只摆着那一行
                // 灰的「正在获取…」；跑过一轮事件、回复（或者超时）落了地，那行就
                // 换成结果了 —— 那时候再断言就等于在赌网络快不快。
                const QString categoryKey = keys.value(i);
                ResourceCatalog::Kind kind{};
                if (i != gameIndex && ResourceCatalog::kindFromCategoryKey(categoryKey, &kind)) {
                    auto *resTree = byObjectName<QTreeWidget>(
                        &window, MainWindowUI::downloadResourceTreeName(categoryKey));
                    check(resTree != nullptr,
                          QString("「%1」那一页上找得到它自己的资源树")
                              .arg(MainWindowUI::downloadCategoryTexts().value(i)),
                          MainWindowUI::downloadResourceTreeName(categoryKey));
                    if (resTree != nullptr) {
                        // 树得长在**这一页**上，不是别的页那棵。少了这一条，"点哪一页
                        // 动哪棵树"就只是"某棵树动了" —— 接错页也照样绿。
                        check(stack->widget(i) != nullptr &&
                                  stack->widget(i)->isAncestorOf(resTree),
                              QString("「%1」那一页的树就长在那一页上")
                                  .arg(MainWindowUI::downloadCategoryTexts().value(i)));
                        // 切过去的第一眼：一行灰的「正在获取最热门的<类>…」。没有它，
                        // 点这一下到结果回来之间界面**完全没有反应**（请求一秒上下），
                        // 而"点了没反应"是用户唯一会当成启动器坏了的情况。
                        const QString placeholder =
                            resTree->topLevelItemCount() == 1
                                ? resTree->topLevelItem(0)->text(
                                      MainWindowUI::versionTreeKeyColumn())
                                : QString();
                        check(placeholder.contains(QStringLiteral("正在获取最热门的%1")
                                                       .arg(ResourceCatalog::kindLabel(kind))),
                              QString("点「%1」那一下，树上立刻摆出「正在获取最热门的%1…」")
                                  .arg(MainWindowUI::downloadCategoryTexts().value(i)),
                              QString("行数=%1 第一行=「%2」")
                                  .arg(resTree->topLevelItemCount())
                                  .arg(placeholder));
                        // 那一行是**灰的**（禁用位）：它不是一个能点的东西，点它什么都不该
                        // 发生（onResourceItemActivated 开头就是这一条）。将来有人把它改成
                        // 能点，用户点一下就得到一条"正在获取…"的假条目。
                        check(resTree->topLevelItemCount() != 1 ||
                                  resTree->topLevelItem(0)->isDisabled(),
                              QString("「%1」那一页上那行「正在获取…」是灰的（点不动）")
                                  .arg(MainWindowUI::downloadCategoryTexts().value(i)));
                    }
                }

                QApplication::processEvents();
                const bool ok = categoryList->currentRow() == i && stack->currentIndex() == i &&
                                stack->currentWidget() == stack->widget(i);
                switched += ok ? 1 : 0;
                check(ok,
                      QString("点第 %1 个分类「%2」，右栏翻到第 %1 页")
                          .arg(i)
                          .arg(MainWindowUI::downloadCategoryTexts().value(i)),
                      QString("row=%1 page=%2")
                          .arg(categoryList->currentRow())
                          .arg(stack->currentIndex()));
            }
            check(switched == stack->count(), "每一个分类都翻得动（没有点了没反应的那个）",
                  QString("%1/%2").arg(switched).arg(stack->count()));

            // 点一个别的分类：右栏显示的必须是**它自己**那一页，而不是把「游戏」那页留在
            // 原地。第 2 个是「模组」（那一页现在也是真的：一个搜索框 + 一棵三层树）。
            const int modIndex = 2;
            if (modIndex < categoryList->count() && modIndex != gameIndex) {
                categoryList->setCurrentRow(modIndex);
                QApplication::processEvents();
                const QString wantPage = keys.value(modIndex) + QStringLiteral("Page");
                check(stack->currentWidget() != nullptr &&
                          stack->currentWidget()->objectName() == wantPage,
                      "点「模组」翻到的是它自己那一页（不是把「游戏」那页留在原地）",
                      stack->currentWidget() != nullptr ? stack->currentWidget()->objectName()
                                                        : QStringLiteral("<null>"));
            }

            // 转一圈回来，「游戏」那页上的选择**一个都没被碰过**：切分类不是"重新进一次
            // 这一页"。真碰了的话，用户挑好加载器、选好版本、顺手点一下「整合包」看看、
            // 再点回「游戏」，挑的东西就没了 —— 而那种丢失没有任何提示。
            categoryList->setCurrentRow(gameIndex);
            QApplication::processEvents();
            check(stack->currentIndex() == gameIndex, "切回「游戏」翻得回去");
            if (loaderTree != nullptr) {
                QStringList loaderAfter;
                for (int i = 0; i < loaderTree->topLevelItemCount(); ++i) {
                    loaderAfter << loaderTree->topLevelItem(i)
                                       ->data(MainWindowUI::loaderTreeKeyColumn(),
                                              MainWindowUI::rowSubTextRole())
                                       .toString();
                }
                check(loaderAfter == loaderBefore,
                      "转一圈回来，加载器那棵树上选中的还是原来那条（小字一个字都没变）",
                      QString("%1 → %2")
                          .arg(loaderBefore.join(QLatin1Char('/')))
                          .arg(loaderAfter.join(QLatin1Char('/'))));
            }
            if (tree != nullptr) {
                const int treeRowAfter = tree->indexOfTopLevelItem(tree->currentItem());
                check(treeRowAfter == treeRowBefore,
                      "转一圈回来，版本树里选中的那条还在",
                      QString("%1 → %2").arg(treeRowBefore).arg(treeRowAfter));
            }
            // ---- Y2c. 左栏每一条右边那个**刷新按钮** ----
            //
            // 这一下必须走**真的鼠标事件**：产品把它接在视口的事件过滤器上
            // （QEvent::MouseButtonPress/Release，见 MainWindow::eventFilter），伪造
            // itemClicked 正好绕过要验的那一段 —— 和上面那个「取消」小叉同一个规矩。
            //
            // 判据是"树上换了一批新的条目"：startResourceSearch() 会先 clear() 再摆一行
            // 灰的「正在…」，那是**同步**的（就在 sendEvent 那一行里发生）。所以下面
            // 量的时候**不跑 processEvents** —— 跑了之后结果/失败回来，看到的就不是
            // "刚才那一下干了什么"了。为了能把"清过"和"没清过"分开，先往树上挂一行
            // **只有我们知道**的标记：它还在 = 没人动过这棵树。
            {
                const int modKindIndex = 2;  // 和上面那段同样取「模组」（它的键不是 game）
                ResourceCatalog::Kind kind{};
                const QString modKey = keys.value(modKindIndex);
                auto *resTree = byObjectName<QTreeWidget>(
                    &window, MainWindowUI::downloadResourceTreeName(modKey));
                check(resTree != nullptr &&
                          ResourceCatalog::kindFromCategoryKey(modKey, &kind),
                      "找得到「模组」那一页的资源树（下面拿它看「重新问了一遍」）",
                      MainWindowUI::downloadResourceTreeName(modKey));
                if (resTree != nullptr) {
                    // 视口坐标 = visualItemRect() 的坐标系。点在**行尾那一格**的正中心。
                    const auto clickListAt = [](QListWidget *list, const QPoint &pos) {
                        QWidget *viewport = list->viewport();
                        const QPointF local(pos);
                        const QPointF global(viewport->mapToGlobal(pos));
                        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                        QApplication::sendEvent(viewport, &press);
                        QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                        QApplication::sendEvent(viewport, &release);
                    };
                    const QString markerText = QStringLiteral("★刷新测试的标记行★");
                    const auto addMarker = [&markerText](QTreeWidget *tree) {
                        auto *marker = new QTreeWidgetItem(tree);
                        marker->setText(MainWindowUI::versionTreeKeyColumn(), markerText);
                        marker->setDisabled(true);
                    };
                    const auto hasMarker = [&markerText](QTreeWidget *tree) {
                        for (int i = 0; tree != nullptr && i < tree->topLevelItemCount(); ++i) {
                            if (tree->topLevelItem(i)->text(MainWindowUI::versionTreeKeyColumn()) ==
                                markerText) {
                                return true;
                            }
                        }
                        return false;
                    };

                    // 停在「模组」这一页（上面那段已经切过来过一次，这里再钉一次，免得
                    // 后面有人挪了顺序）。
                    categoryList->setCurrentRow(modKindIndex);
                    QApplication::processEvents();
                    const QRect rowRect = categoryList->visualItemRect(
                        categoryList->item(modKindIndex));
                    const QRect refreshBox = VersionTreeDelegate::refreshRect(rowRect);
                    check(rowRect.width() > 0 && rowRect.contains(refreshBox),
                          "「模组」那一行的刷新按钮算得出来、落在行里",
                          QString("行 %1,%2 %3x%4 格子 %5,%6")
                              .arg(rowRect.x())
                              .arg(rowRect.y())
                              .arg(rowRect.width())
                              .arg(rowRect.height())
                              .arg(refreshBox.x())
                              .arg(refreshBox.y()));

                    // ① 点**当前**这一类行尾那一格 → 这一类重新问一遍。
                    addMarker(resTree);
                    check(hasMarker(resTree), "① 前提：标记行挂上去了");
                    clickListAt(categoryList, refreshBox.center());
                    check(!hasMarker(resTree) && resTree->topLevelItemCount() == 1 &&
                              resTree->topLevelItem(0)->text(
                                  MainWindowUI::versionTreeKeyColumn())
                                  .contains(QStringLiteral("正在")),
                          "★ 点「模组」那条右边的刷新 = 这一类**重新问了一遍**"
                          "（树上换成了「正在获取…」那一行占位）",
                          QString("行数=%1 标记还在=%2 第一行=「%3」")
                              .arg(resTree->topLevelItemCount())
                              .arg(hasMarker(resTree) ? 1 : 0)
                              .arg(resTree->topLevelItemCount() > 0
                                       ? resTree->topLevelItem(0)->text(
                                             MainWindowUI::versionTreeKeyColumn())
                                       : QString()));
                    check(categoryList->currentRow() == modKindIndex,
                          "★ 点刷新**没有**把左栏切到别的类去（刷新的还是原来那一页）",
                          QString::number(categoryList->currentRow()));
                    QApplication::processEvents();

                    // ② 反证：点在**同一行里、但不在那一格**（格子左边一点点）——
                    // 什么都不会重新问。没有这一条的话，"把整行都当成刷新"那种把
                    // 过滤器范围写宽了的改法也能全绿（而那会让用户点条目就重搜一次，
                    // 一页烧掉一发限流额度）。
                    addMarker(resTree);
                    clickListAt(categoryList,
                                QPoint(refreshBox.left() - 6, refreshBox.center().y()));
                    check(hasMarker(resTree),
                          "★ 同行里、那一格**左边**那一下什么都不刷新（判据是那一格，不是整行）",
                          QString("标记还在=%1").arg(hasMarker(resTree) ? 1 : 0));
                    // 清掉标记行，把这一页的树还给后面几节（点①那一下本来就会重建它，
                    // 这里只是别留一行假的在里面）。
                    resTree->clear();
                    QApplication::processEvents();

                    // ③ 点**别的**那一类的刷新 → 先翻到它那一页，再问一遍（不然请求飞出去了、
                    // 用户看的还是原来那一页，等于"点了没反应"）。
                    //
                    // NOTE: 这里得先把「资源包」那一页**填上东西**（切过去一趟，它会自己搜一次），
                    // 否则"空页"上"只是切过去"和"切过去 + 重新问一遍"长得一模一样
                    // （两种都是树上只有一行「正在…」），这条断言就验不到"重问"那半截。
                    const int packIndex = 3;  // 「资源包」
                    const QString packKey = keys.value(packIndex);
                    auto *packTree = byObjectName<QTreeWidget>(
                        &window, MainWindowUI::downloadResourceTreeName(packKey));
                    const QRect packRow = categoryList->visualItemRect(
                        categoryList->item(packIndex));
                    if (packTree != nullptr && packRow.width() > 0) {
                        categoryList->setCurrentRow(packIndex);
                        QApplication::processEvents();
                        categoryList->setCurrentRow(modKindIndex);
                        QApplication::processEvents();
                        addMarker(packTree);
                        check(hasMarker(packTree),
                              "③ 前提：标记挂在「资源包」那一页的树上（点之前不在那一页）");
                        clickListAt(categoryList,
                                    VersionTreeDelegate::refreshRect(packRow).center());
                        check(categoryList->currentRow() == packIndex &&
                                  stack->currentIndex() == packIndex,
                              "★ 点「资源包」那条的刷新 = 先翻到「资源包」那一页",
                              QString("row=%1 page=%2")
                                  .arg(categoryList->currentRow())
                                  .arg(stack->currentIndex()));
                        check(!hasMarker(packTree) && packTree->topLevelItemCount() == 1 &&
                                  packTree->topLevelItem(0)
                                      ->text(MainWindowUI::versionTreeKeyColumn())
                                      .contains(QStringLiteral("正在")),
                              "★ 而且**它那一页**的树上换成了「正在获取…」（问的是资源包，"
                              "不是只切过去看了一眼）",
                              QString("行数=%1 标记还在=%2")
                                  .arg(packTree->topLevelItemCount())
                                  .arg(hasMarker(packTree) ? 1 : 0));
                        QApplication::processEvents();
                    } else {
                        check(false, "「资源包」那一页的资源树找得回来（第③段的前提）");
                    }
                }
            }

            // ---- Y2d. 刷新**不动**图标缓存（要求："图标留缓存"）----
            //
            // 这一条钉的是**别动**，不是"走了缓存"：刷新那条路从头到尾不许碰
            // `<数据目录>/cache/icons/` 里的东西（清了它，用户下次进这一页每张图都要
            // 重新下一遍，而他按这个按钮只是想让**列表**重新问一遍）。
            {
                const QString probeId = QStringLiteral("wtfml-refresh-probe");
                const QString cachePath = ResourceStore::iconCachePath(probeId);
                check(!cachePath.isEmpty(), "图标缓存路径算得出来（这条断言的前提）", cachePath);
                if (!cachePath.isEmpty()) {
                    // 那份目录不一定在（这台机器到现在一张图标都没缓存过）—— 先把它建出来，
                    // 不然下面 open() 直接失败，红的是一条跟被测代码毫无关系的断言。
                    QDir().mkpath(QFileInfo(cachePath).absolutePath());
                    QFile probe(cachePath);
                    // 内容是什么不重要（这一条量的是**文件在不在**）：真图标字节要联网
                    // 才拿得到，而这条路必须离线也能跑。
                    const bool written = probe.open(QIODevice::WriteOnly) &&
                                         probe.write("not-a-real-icon") > 0;
                    probe.close();
                    check(written && QFile::exists(cachePath),
                          "前提：往图标缓存里放了一份（下面看点完刷新它还在不在）");
                    // 点一次「游戏」那条的刷新 —— 它那一页**不**走资源搜索，正是"最可能
                    // 顺手把缓存清了"的那一条（要重新拉清单，看着就像该"刷新一下"）。
                    const QRect gameRow = categoryList->visualItemRect(
                        categoryList->item(gameIndex));
                    if (gameRow.width() > 0) {
                        const auto clickOnce = [](QListWidget *list, const QPoint &pos) {
                            QWidget *viewport = list->viewport();
                            const QPointF local(pos);
                            const QPointF global(viewport->mapToGlobal(pos));
                            QMouseEvent press(QEvent::MouseButtonPress, local, global,
                                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                            QApplication::sendEvent(viewport, &press);
                            QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                            QApplication::sendEvent(viewport, &release);
                        };
                        // 「游戏」那一条的刷新走的是 fetchAvailableVersions()（离线发不出去、
                        // 也不会立刻回来），所以"真的重发了一发清单请求"这件事这里量不到 ——
                        // 能钉住的是"那一下没把界面清空、也没走错页"。走错页那条是有效的：
                        // 上面那段结束时停在「资源包」，「游戏」那条的刷新必须把左栏翻回第 0 个。
                        const int treeRowsBefore = tree != nullptr ? tree->topLevelItemCount() : -1;
                        check(categoryList->currentRow() != gameIndex,
                              "前提：点之前不在「游戏」那一页（下面才看得出它翻没翻）",
                              QString::number(categoryList->currentRow()));
                        clickOnce(categoryList,
                                  VersionTreeDelegate::refreshRect(gameRow).center());
                        check(categoryList->currentRow() == gameIndex &&
                                  stack->currentIndex() == gameIndex,
                              "★ 点「游戏」那条的刷新 = 翻回「游戏」那一页（结果得让用户看得见）",
                              QString("row=%1 page=%2")
                                  .arg(categoryList->currentRow())
                                  .arg(stack->currentIndex()));
                        check(tree == nullptr || tree->topLevelItemCount() == treeRowsBefore,
                              "★ 而且版本树一条都没少（老清单留着，等新清单回来顶替 —— "
                              "不是清空重来）",
                              QString("%1 → %2")
                                  .arg(treeRowsBefore)
                                  .arg(tree != nullptr ? tree->topLevelItemCount() : -1));
                        check(QFile::exists(cachePath),
                              "★ 点完刷新，图标缓存里那份还在（刷新**不动**缓存）",
                              cachePath);
                        QApplication::processEvents();
                    }
                    // 收拾：探针文件不能留在沙盒里（后面几节会数沙盒里有什么）。
                    check(QFile::remove(cachePath), "探针那份图标缓存删干净了");
                }
            }

            check(categoryList->currentRow() == gameIndex &&
                      stack->currentIndex() == gameIndex,
                  "Y2 段收尾：停在「游戏」那一页（后面几节拿窗口当开局那个样子用）");
        }
    }
    return 0;
}


