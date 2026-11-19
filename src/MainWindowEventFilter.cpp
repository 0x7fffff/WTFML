/**
 * @file MainWindowEventFilter.cpp
 * @brief 行里那些小按钮的事件路由：视口上的 `eventFilter()`。
 *
 * 【为什么单独一个翻译单元】
 *
 * 界面里有一批按钮不是 QWidget，而是代理**画在行里的一格**（加载器那行的「取消」叉、
 * 组行的齿轮、实例行的齿轮 / 置顶 / 垃圾桶、下载页分类行尾的刷新圈、资源行尾的箭头）。
 * 它们全靠这一支过滤器认"点中了没"：判据用的是代理**画它们时用的同一个矩形函数**，
 * 差一个像素就是"看着点的是垃圾桶、其实点的是那一行"。这一整族判据放在一个文件里，
 * 加一格按钮时不必在别处翻。
 *
 * 【两条反复出现的规矩】
 *
 * 一、**按下就动手**，松开与那几发双击只负责被吃掉 —— 跟普通按钮一样，按下去就该有反应；
 * 二、**双击也要接**：点得快时第二发及以后到的是 `MouseButtonDblClick`，不接就漏给
 * "点中了这一行"那条路（连点垃圾桶的第二下变成进实例设置）。
 *
 * 【判据不在这一层】
 *
 * 该画在哪一格在 `VersionTreeDelegate` / `InstanceGroupDelegate`；点了算"哪一件事"由
 * `MainWindow` 的槽做（`openInstanceSettings()` / `pinInstance()` / `unpinInstance()` /
 * `confirmDeleteInstance()` / `refreshDownloadCategory()` / `openResourceDetailPage()`）。
 */

#include "MainWindow.hpp"

#include "..\ui\InstanceGroupDelegate.hpp"
#include "..\ui\MainWindowUI.hpp"
#include "..\ui\VersionTreeDelegate.hpp"
#include "controllers/InstallPageController.hpp"
#include "download/ResourceCatalog.hpp"

#include <QListWidget>
#include <QMouseEvent>
#include <QTreeWidget>

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    // 安装页那棵树上**唯一一个"行里的按钮"**：选中那条加载器右边的「取消」小叉
    // （谁画的、画在哪一格，见 VersionTreeDelegate::clearRect() 那段 NOTE）。
    //
    // 为什么不走 itemClicked：它只给"点中了哪个条目"、不给"点在条目里的哪儿"，而这一下
    // 点的是行里的一个按钮、**不是这一行** —— 点中叉的时候那一行不该跟着展开/收起。
    // 过滤器能把事件吃掉（返回 true 之后 Qt 那套点击处理整条不走了），两件事就都干净。
    QTreeWidget *tree = m_ui->getLoaderTree();
    if (tree != nullptr && watched == tree->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QTreeWidgetItem *item = nullptr;
        if (mouse->button() == Qt::LeftButton) {
            item = tree->itemAt(mouse->position().toPoint());
        }
        if (item != nullptr &&
            item->data(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowClearRole())
                .toBool() &&
            VersionTreeDelegate::clearRect(tree->visualItemRect(item))
                .contains(mouse->position().toPoint())) {
            if (event->type() == QEvent::MouseButtonPress) {
                // 按下就动手（松开那一下只负责被吃掉）：跟普通按钮一样，按下去就该有反应。
                // 这一下取消的是"装这个加载器"这件事 —— 退回「不装（纯原版）」。
                // NOTE: **钉住的版本号留着**（不清 m_chosenVersion）：那是用户自己挑的
                // 一版，按一下叉就把它忘掉，下次再选中这条加载器还得从头找一遍。
                // 它只在"这一条被选中"的时候才显示，留着不会让人看到不该看的东西。
                m_installPage->selectLoader(MainWindowUI::modLoaderNoneIndex());
            }
            return true;
        }
    }
    // 实例页那张树上**行里的按钮**，头一个：组行行尾那颗齿轮（谁画的、画在哪一格，
    // 见 InstanceGroupDelegate::gearRect()）。判据和上面那个叉一模一样 —— 包括"吃掉
    // 那一下"：不吃的话点齿轮会顺手把这一组展开/收起（那正是"点组行"要干的事）。
    // 另外两颗（上移 / 下移）在下面那一段，判据同族，只有"按得快要不要补那一发"不同。
    //
    // NOTE: 这里**也接双击**。点得快的时候第二发及以后到的是 MouseButtonDblClick
    // （见 ui/RowActivation.hpp 那段），只接 Press/Release 的话"连点两下齿轮"
    // 的第二下会漏给下面那条"点中了这一行"的路。
    QTreeWidget *instanceTree = m_ui->getInstanceList();
    if (instanceTree != nullptr && watched == instanceTree->viewport()
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QTreeWidgetItem *item =
            mouse->button() == Qt::LeftButton ? instanceTree->itemAt(mouse->position().toPoint())
                                              : nullptr;
        if (item != nullptr && item->data(0, MainWindowUI::rowGroupGearRole()).toBool()
            && InstanceGroupDelegate::gearRect(instanceTree->visualItemRect(item))
                   .contains(mouse->position().toPoint())) {
            // 按下就动手（松开、以及那几发双击只负责被吃掉）：跟普通按钮一样，按下去就该
            // 有反应。双击那几发**不能**再弹一遍 —— 弹第二遍时上面那块框已经开着，
            // openGroupMenu() 认到"还是同一组"就把它收回去了，用户看到的是"连点两下齿轮
            // 反而没了"（那一处正是"再点同一颗齿轮 = 收回"，别在门口把它挡掉）。
            if (event->type() == QEvent::MouseButtonPress) {
                openGroupMenuFor(item);
            }
            return true;
        }
    }
    // 同一个实例树上**实例行**悬停那三格（ListRowDelegate：文件夹 / 更多 / 启动）。也接双击：
    // 点得快时第二发是双击事件，不吃掉会漏给"点中这一行"那条路（进实例设置）。
    // 服务端列表（同一个 ListRowDelegate）认同一套三格。
    QTreeWidget *rowTree = instanceTree != nullptr && watched == instanceTree->viewport() ? instanceTree : nullptr;
    if (rowTree == nullptr && m_ui->getServerList() != nullptr && watched == m_ui->getServerList()->viewport()) {
        rowTree = m_ui->getServerList();
    }
    if (rowTree != nullptr
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QTreeWidgetItem *item =
            mouse->button() == Qt::LeftButton ? rowTree->itemAt(mouse->position().toPoint())
                                              : nullptr;
        // 悬停快捷操作三格（文件夹 / 更多 / 启动，几何同 ListRowDelegate::actionRect）：
        // 按下就发 actionTriggered，松开和双击只负责被吃掉，免得漏给"点中这一行 = 进实例设置"。
        auto *rowDelegate = qobject_cast<ListRowDelegate *>(rowTree->itemDelegate());
        if (item != nullptr && rowDelegate != nullptr && !item->data(0, MainWindowUI::groupIdRole()).isValid()) {
            const QRect row = rowTree->visualItemRect(item);
            const QPoint pos = mouse->position().toPoint();
            for (const auto action : {ListRowDelegate::Action::Folder, ListRowDelegate::Action::More,
                                      ListRowDelegate::Action::Launch}) {
                if (!ListRowDelegate::actionRect(row, action).contains(pos)) continue;
                if (event->type() == QEvent::MouseButtonPress)
                    emit rowDelegate->actionTriggered(rowTree->indexAt(pos), action);
                return true;
            }
        }
    }
    // NOTE: 组行行尾那两颗「上移 / 下移」（齿轮左边那两颗实心三角）不在这棵树上 ——
    // 它们在「分组设置…」那张表里，认下那一下的是 `ui/GroupDialog::eventFilter()`
    // （挂在那张表的视口上），"每一发都算"和"Shift = 置顶/置底"那两条手势跟着它。
    // 实例行行尾那一排的接线在**上面**（就在组行齿轮那段后面）—— 它和那套是同一个
    // 位置、同一族判据，别再往下找。
    // 下载页左栏那份分类列表：每一行最右边那个刷新按钮（谁画的、画在哪一格，见
    // VersionTreeDelegate::refreshRect()）。判据和上面那个叉一模一样，只是宿主是个
    // QListWidget（它的 itemAt/visualItemRect 和 QTreeWidget 那对同名同义）。
    QListWidget *categories = m_ui->getDownloadCategoryList();
    if (categories != nullptr && watched == categories->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item =
            mouse->button() == Qt::LeftButton ? categories->itemAt(mouse->position().toPoint())
                                              : nullptr;
        if (item != nullptr &&
            item->data(MainWindowUI::rowRefreshRole()).toBool() &&
            VersionTreeDelegate::refreshRect(categories->visualItemRect(item))
                .contains(mouse->position().toPoint())) {
            if (event->type() == QEvent::MouseButtonPress) {
                // 按下就动手（松开那一下只负责被吃掉）—— 和那个叉一个规矩。
                refreshDownloadCategory(categories->row(item));
            }
            return true;
        }
    }
    // 下载页那四张资源表：**点行身 = 打开这一条资源的详情页**（下载页选中一个模组之后
    // 进入一个独立界面）；点行尾那个箭头仍然是原有规矩（展开 / 收起，
    // 判据是同一个 arrowRect()）—— 于是"展开这一条"这件事从此只挂在箭头上。
    //
    // NOTE: 这一块**只认第一层**（一个资源）。第二层（游戏版本）点开是"问这个资源在这个
    // 游戏版本上有哪些版本可下"、第三层（版本号）点它是"下它"，两条原有的路留着
    // （详情页是**另一条**看整份清单的路，不是把树上那条拆掉）。
    // NOTE: 三发都吃掉（按下 / 松开 / 双击）：`connectRowActivation` 那边给"开关行"补的
    // 那一发双击也会落到"展开"上 —— 而这一下已经算"进这一页"了，不该再翻一次树。
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
        || event->type() == QEvent::MouseButtonDblClick) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        for (ResourceCatalog::Kind treeKind : ResourceCatalog::allKinds()) {
            const QString treeKey = ResourceCatalog::kindKey(treeKind);
            QTreeWidget *tree = m_ui->getDownloadResourceTree(treeKey);
            if (tree == nullptr || watched != tree->viewport()) {
                continue;
            }
            const QPoint pos = mouse->position().toPoint();
            QTreeWidgetItem *item =
                mouse->button() == Qt::LeftButton ? tree->itemAt(pos) : nullptr;
            if (item == nullptr || resourceRowDepth(item) != 0) {
                continue;
            }
            // "这一行是不是我们建的"：占位行（「正在搜索…」那几行）没有这一位。
            const int column = MainWindowUI::versionTreeKeyColumn();
            const QString kindKey =
                item->data(column, MainWindowUI::resourceKindRole()).toString();
            if (kindKey.isEmpty()) {
                continue;
            }
            // 箭头那一格**交回去**（展开 / 收起还是它）：判据和代理画它用的是同一个函数，
            // 两边各算一份的话迟早错开，而错开的形状是"看着点的是箭头、其实进了一页"。
            if (VersionTreeDelegate::arrowRect(tree->visualItemRect(item)).contains(pos)) {
                continue;
            }
            if (event->type() == QEvent::MouseButtonPress) {
                // 按下就动手（松开和那几发双击只负责被吃掉）—— 和实例行上那三颗一个规矩。
                const QString iconKey =
                    item->data(column, MainWindowUI::resourceIconIdRole()).toString();
                // **排到下一轮事件循环**再开页：这一发还在视图的事件处理里（列表那一侧
                // 的状态机悬在半路），开页那一句不该在这里跑（和 `openPage()` 那条
                // "排到下一轮再开"是同一个理由）。这里传的是两个字符串**值**，不是条目
                // 指针 —— 下一轮里树可能已经被重搜清空了。
                //
                // 来路钉死是 `Download`：这条路上这一页**永远**摆「显示
                // 快照版」，哪怕点的是一个模组（不能把下载页的行为带偏）。
                // 实例号从下载页那条来路里读（`m_downloadReturn`）。
                const QString instanceId = m_downloadReturn.instanceId;
                QTimer::singleShot(0, this, [this, iconKey, kindKey, instanceId] {
                    // NOTE: 覆盖层被别的页占着时（典型场面：实例设置那一页**收着**、
                    // 用户切到下载页逛），`openResourceDetailPage()` 开头那条 `isOpen()`
                    // 会把这一下当场吃掉 —— 用户点了行身，屏幕上一个反应都没有。所以
                    // 先让位：静默收掉占着的那一层，再开（让位那一段的唯一出处是
                    // `runWhenOverlayFree()`）。
                    runWhenOverlayFree([this, iconKey, kindKey, instanceId] {
                        openResourceDetailPage(iconKey, kindKey, DetailSource::Download, instanceId);
                    });
                });
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
