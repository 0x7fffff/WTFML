/**
 * @file MainWindowDownloadOrigin.cpp
 * @brief 下载页的"来路"：从实例点到下载页、筛子怎么带过去、以及「← 返回实例设置」与顶部导航那两下。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一片的主语只有一件：`m_downloadReturn`（这份来路，"从哪个实例、哪一页点进来的"）。
 * `openDownloadCategory()` 登记它，`fillDownloadFiltersFromInstance()` 把实例那一栏带过去，
 * `continueOpenDownloadCategory()` 照着它翻页并摆筛子，`returnToInstanceSettingsFromDownload()`
 * 取走它并开回原来那页；`onNavigationApplied()` 里那两下（按「实例」= 返回、重点「下载」
 * = 回主页）量的也是它。分出来之后，"返回键什么时候露着"只在一个文件里翻。
 *
 * 【判据为什么这么绕】
 *
 * 顶上那四颗导航由 `MainWindowUI` **先**接（见 `setupConnections` 里那条连接的注释），
 * 走到这里时选项卡早就翻过了 —— 所以"这一按是不是重点当前页"只能量 `reselected`，
 * 不能量 `currentIndex()`（那个症状只有 mw_check T25.8 量得出来）。注释原样跟着搬。
 *
 * 【判据不在这一层】
 *
 * 目录栏的行序与键在 `MainWindowUI`（`downloadCategoryGameIndex()` 那几家），
 * 筛子的选项来自 `ResourceCatalog`；"实例里装着什么"问 `VersionManager`。
 */

#include "MainWindow.hpp"

#include "..\ui\MainWindowUI.hpp"
#include "..\ui\OverlayHost.hpp"
#include "controllers/MainWindowViewData.hpp"
#include "controllers/NavigationReturn.hpp"
#include "download/ResourceCatalog.hpp"
#include "manage/VersionManager.hpp"

#include <QComboBox>
#include <QListWidget>
#include <QTimer>

using namespace MainWindowViewData;

void MainWindow::openDownloadCategory(const QString &categoryKey,
                                      const NavigationReturn &origin) {
    // 认行用**条目自己的 UserRole**（= 那一栏的键），不是行号：左栏那一列的行序是界面
    // 自己按 downloadCategoryKeys() 摆出来的，这一层不去假设"第几个就是哪一个"
    // （和 MainWindow::refreshDownloadCategory 那边认身份的做法一致）。
    QListWidget *categories = m_ui->getDownloadCategoryList();
    const QStringList keys = MainWindowUI::downloadCategoryKeys();
    int row = -1;
    for (int i = 0; categories != nullptr && i < categories->count(); ++i) {
        if (categories->item(i)->data(Qt::UserRole).toString() == categoryKey) {
            row = i;
            break;
        }
    }
    if (row < 0 || !keys.contains(categoryKey)) {
        // 认不出来就**说出来**：悄悄什么都不做的症状是"按了那颗按钮没反应"，
        // 而这一层是唯一知道"下载页到底有哪几栏"的地方，出错时不说就没人查得到。
        qWarning() << "下载页没有这一栏：" << categoryKey;
        return;
    }

    // ② 这一栏是不是**从某个实例的设置页里点「下载新模组」进来的**（@p origin 有效）。
    //
    // 是的话记下来路，并且把「← 返回实例设置」露出来 —— 从实例设置里点
    // 「下载新模组/资源包…」进来时，左上角保留一个返回键，返回到实例设置里面，
    // 和资源包详情一致。
    // 来路**只记在这一处**：`NavigationReturn` 里已经写清归哪颗根页、哪个实例、哪一页、
    // 文件页滚到哪儿（见那份头文件）；下面 `returnToInstanceSettingsFromDownload()`
    // 按同一份摆回去。
    //
    // 那颗键**整个下载页共用一颗**、摆在左栏顶上（就是左边栏的顶上），翻到别的栏它照旧在那儿 ——
    // 能不能回去和用户此刻在逛哪一栏无关。
    m_downloadReturn = origin;
    m_ui->setDownloadBackVisible(origin.valid());

    // ① 先收掉当前这一页。**只能走 dismiss()**：它发的就是顶上那颗返回箭头的同一发信号
    // （见 OverlayHost::dismiss() 里那段 ★）—— 用户按的那颗按钮可能正开着一条提示/另一页，
    // 只 `closeOverlay()` 的话那一页自己收不到任何信号。
    //
    // NOTE: 这一发只是**尽力**先收（常见情况下它收完就完了）；收干净了没有由下一轮的
    // `continueOpenDownloadCategory()` 判 —— 上面一层压着一条提示时 `dismiss()`
    // 收掉的是**提示**、那一页还在原处。
    m_ui->overlay()->dismiss();

    // ② 摆选项卡与那一栏**排到下一轮事件循环**：这一句这会儿还在按钮的 clicked 处理函数
    // 里，收页的收尾（`pageClosed` 那一发）还没跑完、覆盖层也还盖在内容区上 ——
    // 现在就把选项卡翻过去，翻的是它**底下**那一层，而覆盖层收干净的那一下还没发生。
    // 排到下一轮，那时这一页已经关好了（和 openResourceDetailPage 那边同一个手法）。
    QTimer::singleShot(0, this, [this, row, categoryKey, origin] {
        continueOpenDownloadCategory(row, categoryKey, origin, kDownloadOpenRounds);
    });
}

void MainWindow::continueOpenDownloadCategory(int row, const QString &categoryKey,
                                             const NavigationReturn &origin, int roundsLeft) {
    // ---------- 收页：收到干净为止，收不干净就**什么都不做**----------
    //
    // 为什么还要再收一次：`openDownloadCategory()` 里那一发 `dismiss()` 收的是**最上面
    // 那一层** —— 提示压在实例设置上面时收掉的是提示，那一页还在原处占着覆盖层那一位。
    //
    // 收不干净时**绝不能切选项卡**：切了就是把那一页孤零零地丢在别的选项卡上（它收着、
    // 看不见），而它占着覆盖层那一位 ⇒ 用户此后既回不去那一页、也开不了新的一页，还会
    // 被一次一次拽到下载页。宁可这一次什么都不做，也不要把他丢进那个状态。
    // （正常路径上收不掉是走不到的：那一页归「实例」那一颗，而它的按钮只在它露着的时候
    // 点得着 —— 见 openInstanceSettings() 里那段。留着这一条是因为"走不到"和"保证了
    // 不会走不到"是两件事。）
    OverlayHost *overlay = m_ui->overlay();
    if (overlay != nullptr && overlay->pageOpen()) {
        if (roundsLeft > 0 && overlay->dismiss()) {
            // 收到一半（这一发收回来的那一层要等收尾才真正落下去）—— 再排一轮。
            QTimer::singleShot(0, this, [this, row, categoryKey, origin, roundsLeft] {
                continueOpenDownloadCategory(row, categoryKey, origin, roundsLeft - 1);
            });
            return;
        }
        qWarning() << "下载页：实例设置那一页没能收掉，这一次不切选项卡";
        return;
    }

    m_ui->getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
    // 从实例设置进来的那一次，顺手做三件事 —— 都是"让屏幕上说的和用户以为的是同一件事"：
    // ① 「已安装版本」那一格选到**那个实例**上：这一栏下来的东西是落进"当前选中的实例"
    // 的（见 ），不替用户选上的话，"从实例 A 的页面点进来、
    // 东西却下进了实例 B"——而屏幕上没有任何东西说过会这样（用户那句「下载新模组就
    // 自动放进当前实例文件夹」说的正是这一件事）；
    // ② 版本筛子填成那个实例的游戏版本；
    // ③ 模组加载器筛子填成那个实例的加载器。
    // ②③ 是「点击下载新模组/...后自动在搜索框里填入对应的版本和加载器」那条要求。
    const bool cameFromInstance = origin.valid();
    if (cameFromInstance) {
        QComboBox *combo = m_ui->getInstalledVersionCombo();
        const int index = combo->findData(origin.instanceId);
        if (index >= 0) {
            combo->setCurrentIndex(index);
            updateStartButtonText();
        }
    }
    QListWidget *categories = m_ui->getDownloadCategoryList();
    if (categories == nullptr) {
        return;
    }
    categories->setCurrentRow(row);
    // 已经在那一行上时 `setCurrentRow()` **不发** currentRowChanged（Qt 只在"变了"
    // 的时候才发），而"第一次进这一页才去搜一次"那一发正挂在它身上 ⇒ 这里补一次。
    // 同一条路、同一个函数，重复调用是安全的（它自己判"树上已经有东西就不动"）。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const bool known = ResourceCatalog::kindFromCategoryKey(categoryKey, &kind);
    if (cameFromInstance && known) {
        fillDownloadFiltersFromInstance(kind, origin.instanceId);
    }
    if (known) {
        ensureResourcePageLoaded(kind);
    }
}

void MainWindow::fillDownloadFiltersFromInstance(ResourceCatalog::Kind kind,
                                                const QString &instanceId) {
    const QString key = ResourceCatalog::kindKey(kind);
    // 版本那一格**读的是框里的字**（`downloadFilterGameVersion()` 就是这么读的，那一格是
    // 可输入的），所以写也要写进"字"里：`setCurrentText()` 两样都写了（有那一项就选中它、
    // 没有就把字填进编辑框）—— 那个实例的版本号在在线清单里通常有，可万一没有（离线装
    // 的自定义版本），填进去照样是对的，而那正是这一格该显示的东西。
    const QString gameVersion = m_versionManager->instanceGameVersion(instanceId);
    if (!gameVersion.isEmpty()) {
        if (QComboBox *versionCombo = m_ui->getDownloadFilterVersionCombo(key)) {
            versionCombo->setCurrentText(gameVersion);
        }
    }
    // 加载器那一格**只有模组页有**（别的页 getDownloadFilterLoaderCombo 回空指针）。
    //
    // 按**文字**认（「Fabric」/「Forge」/「NeoForge」/「Quilt」）而不是按 `data()`：
    // 两个来源的键**不是一套**（Modrinth 是词、CurseForge 是数字，见 loaderOptions），
    // 而文字两边一模一样 —— 按 data 认的话，用户把来源换成 CurseForge 之后这一格就再也
    // 填不上了（而且不报错、看不出来）。
    QString loaderName;
    if (m_versionManager->instanceModLoader(instanceId, &loaderName) && !loaderName.isEmpty()) {
        if (QComboBox *loaderCombo = m_ui->getDownloadFilterLoaderCombo(key)) {
            const int index = loaderCombo->findText(loaderName);
            if (index >= 0) {
                loaderCombo->setCurrentIndex(index);
            }
        }
    }
}

void MainWindow::returnToInstanceSettingsFromDownload() {
    // 来路整个取走（一次性）：下面无论开得成开不成，这一份都不该留着。
    const NavigationReturn origin = m_downloadReturn;
    m_downloadReturn = NavigationReturn();
    m_ui->setDownloadBackVisible(false);
    if (!origin.valid()) {
        return;  // 正常走不到：那颗键只在有来路时才露面
    }
    // 回去时先把**来路那颗根页**翻回来（主页的快捷入口开出来的那页归主页，其余归实例）。
    // 「返回恢复进入前状态」里"进"的那一半就是这颗根页：不翻的话，`openInstanceSettings()`
    // 会按**当前**选项卡（下载）认家，于是从主页进来的一趟回到实例页 —— 屏幕换了一颗根页，
    // 用户没按过它。
    if (QTabWidget *tabs = m_ui->getTabWidget()) {
        if (origin.rootTab >= 0 && origin.rootTab < tabs->count()) {
            tabs->setCurrentIndex(origin.rootTab);
        }
    }
    // 回来时开在**来路那一页**上（要求「从模组点进去，返回也在模组而非概览，其余同理」），
    // 文件页的滚动位置也一起摆回去 —— 两个值同一个来源（`NavigationReturn`）。
    // 挪到 `m_instanceReturn` 是因为真正摆页的是 `openInstanceSettings()` 那一趟
    // （它读的是一位"开页时塞回去"的来路，见 `InstanceDialogController::open()`）。
    m_instanceReturn = origin;
    openInstanceSettings(origin.instanceId, origin.pageKey);
}

void MainWindow::onNavigationApplied(int sourceIndex, int index, bool reselected) {
    // 顶上那四颗导航，这一层只管**下载页那两件**（要求「在这个情况下点击
    // 顶部下载按钮就刷新页面回到主页」+「如果点击了下载新模组按钮后再点击实例按钮，功能
    // 就和返回一样，如果点两下那就是先返回再回到实例主页」）。翻页、以及"点的是当前这一
    // 颗就把上面那层收掉"那两件，仍旧是 MainWindowUI 自己接的那一条在管（那边**先接**，
    // 所以它先跑完，见 setupConnections 里那条连接的注释）—— 这里不重复做，也不去改它。
    //
    // ---------- 「实例」那一颗 = 按了「← 返回实例设置」（要求见上） ----------
    //
    // 这**不是**那一颗返回键的第二条接线（那颗键仍旧只有一处出口，见 setupConnections
    // 里那段）—— 这里只是"同一件事从另一颗键上够得着"。两条都走
    // `returnToInstanceSettingsFromDownload()`，所以"取哪一份来路、翻回哪颗根页、开在哪一页"
    // 一个字都不用写两遍。
    //
    // 判据四条：
    // ① 点的是「实例」那一颗；
    // ② **刚才是从下载页按的**（`sourceIndex`）。少了这一条，"从实例进来 →
    // 切去主页逛了一圈 → 点实例"也会被当成"回实例设置"，可那一下要的是切到实例
    // 列表（他手上那一页早就切走了）。这一位**只有 MainWindowUI 说得出口**：翻页那一下
    // 是它先跑的，这边醒过来时 `currentIndex()` 已经是新的一颗了（见那个访问器的注释）。
    // ③ 这一趟**确实是从某个实例点进来的** —— 也就是那颗返回键正露着。
    // ④ 这一层**没有别的页面占着**（典型：用户先从下载页点开了一个资源的详情页）。
    // `openInstanceSettings()` 自己会让位（静默收掉占着的那一层、
    // 排到下一轮再开，见 `runWhenOverlayFree()`）—— 所以这一条判据是
    // "这一按到底算不算'回实例设置'"，不是"那一页开不出来"：占着的时候按「实例」
    // 就照常翻页（和按「主页」「设置」一样），让位那一套交给上面那一发。
    // 留着这一条的理由是**顺序**：来路（`m_downloadReturn`）要排在那一次
    // 开页之前清掉，而"让位"是异步的 —— 两下里撞在一起会变成"返回键没了、页也没开"。
    //
    // 第二下为什么自动就成了"回到实例主页"：上面那一发把这一页开起来之后，当前选项卡
    // **就是**来路那颗根页（`NavigationReturn::rootTab`，通常是「实例」），于是再按一下
    // 正中 MainWindowUI 那条的 ② 档（点的是当前这一颗 ⇒ `overlayHost->dismiss()` = 收掉
    // 这一页，露出底下的实例列表）。那一下这一份已经被上面取走了 ⇒ 这里不接手，
    // 两下没人打架。
    const bool overlayFree = m_ui->overlay() == nullptr || !m_ui->overlay()->isOpen();
    if (index == MainWindowUI::instancesTabIndex()
        && sourceIndex == MainWindowUI::downloadTabIndex()
        && m_downloadReturn.valid() && overlayFree) {
        returnToInstanceSettingsFromDownload();
        return;
    }

    // 判据三条，缺一不可：
    // ① 点的**就是**「下载」那一颗；
    // ② **这一按是重点当前页**（`reselected`）—— 也就是"这一按没有翻页"。
    // 少了这一条，"从实例进来、切去主页逛了一圈、再点下载"也会被当成"回主页" —— 可那
    // 一下要的是**切回来**（原来那一栏、连筛子带实例都在，见 MainWindowUI 搭导航
    // 那段①），顺手清掉就成了"我明明没点返回，回实例设置那颗键却没了"；
    // ③ 这一趟**确实是从某个实例点进来的**（那一位非空 = 「← 返回实例设置」正露着）。
    // 要求里这一条是跟着"点了下载新模组"来的；平白从顶部点进下载页时，左栏挑的
    // 哪一栏、框里填的什么都是他自己摆的，那一发不该替他重置。
    //
    // ② 的**量法**必须用 `reselected`，不能写成
    // `tabs->currentIndex() == index`：上面那条 MainWindowUI 的槽先跑，它自己就会把
    // 选项卡翻过来，于是"切去了主页再点下载"走到这里时 `currentIndex()` 早就是下载页了
    // ⇒ 判据永远为真，等于没有这一条（那个症状照样发生，只有 `mw_check` T25.8 量得出来）。
    if (index != MainWindowUI::downloadTabIndex()
        || !reselected
        || !m_downloadReturn.valid()) {
        return;
    }

    // 来路那一栏先认下来：下面头一件事就是把它整个取走。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const bool known = ResourceCatalog::kindFromCategoryKey(m_downloadReturn.pageKey, &kind);

    // ① 把"从实例进来"这件事整个收起来。返回键露不露只看这一份（见 setDownloadBackVisible
    // 的调用点），所以先取走再收起来。
    m_downloadReturn = NavigationReturn();
    m_ui->setDownloadBackVisible(false);

    // ② 那一栏的筛子是**我们**灌进去的（fillDownloadFiltersFromInstance），回主页就该一并
    // 还回去 —— 留着的话，用户下一次点进那一栏看到的是"我没填过"的条件。
    //
    // 清完**还得把那一页的搜索结果扔掉**：筛子是新条件、树上那批是按旧条件搜出来的，
    // 两下里对不上 —— 正是上面筛子那一段点名的"界面上筛了、请求里没筛"。这里**不发
    // 新请求**：清空之后，下一次进那一页时 ensureResourcePageLoaded() 自然会按新的
    // 筛子搜一次（既不替用户白等一下，也不白烧一次限流额度）。
    if (known) {
        resetDownloadFilters(kind);
        if (QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind))) {
            tree->clear();
        }
    }

    // ③ 左栏回到第一栏 —— 那**就是**下载页的首页（「游戏」：在线版本清单，见
    // downloadCategoryGameIndex()）。
    //
    // 摆的是**下标**，不是"现在选中的那一栏"：这件事跟用户此刻在哪一栏无关，起点是
    // 确定的。（切过去会触发 currentRowChanged → 右栏翻页；第一栏是「游戏」，那一页
    // 不走资源搜索，所以这一下不会多发任何请求。）
    if (QListWidget *categories = m_ui->getDownloadCategoryList()) {
        categories->setCurrentRow(MainWindowUI::downloadCategoryGameIndex());
    }
}
