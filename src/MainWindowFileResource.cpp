/**
 * @file MainWindowFileResource.cpp
 * @brief 从实例文件页点开一个资源：开详情页（来路是 `InstanceFiles`）与"返回时把原页放回去"。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一片的主语是"这一行是哪个资源、点开之后回到哪儿"：`openFileResourcePage()` 按来路摆
 * 那页（只有这一条路才可能露「模组设置」），`openInstanceFileResource()` 先把
 * 实例设置那一页收掉（走 `dismiss()`，和 `openDownloadCategory()` 同一个规矩），再把
 * 三个**字符串**排到下一轮去开新页 —— 下一轮里那一页早就没了，不能等到那时再查
 * "这一条是哪个资源"。
 *
 * 【判据不在这一层】
 *
 * 行的身份钥匙在 `InstanceIconController::fileResourceIconKey()`；种类键在
 * `ResourceCatalog`；页本身的排版在 `ui/ResourceDetailPage`。
 */

#include "MainWindow.hpp"

#include "..\ui\InstanceSettingsDialog.hpp"
#include "..\ui\OverlayHost.hpp"
#include "controllers/InstanceIconController.hpp"
#include "controllers/MainWindowViewData.hpp"
#include "download/ResourceCatalog.hpp"

#include <QDialog>
#include <QTimer>

using namespace MainWindowViewData;

void MainWindow::openFileResourcePage(const QString &versionId, const QString &iconKey,
                                      const QString &kindKey) {
    // 来路是 `InstanceFiles`：只有这一条路上那一页才可能摆「模组设置」（见
    // openResourceDetailPage 里那一句 —— 还要**同时**是个模组）。
    //
    // 收页之后怎么办**全在回调里**（`showPage()` 不等这一页收掉就返回了）。判据还是
    // `QDialog::Rejected`（= 用户把这一页**收掉**了）：返回箭头和 Esc 走的都是它；
    // `Accepted` 专用于「模组设置」，转入当前实例的配置文件管理。
    //
    // 必须和"压根没开出来"分开认：那一档是上面第 ① ② 步认完之后、`m_resourceHits`
    // 里那条已经不在了之类 —— 没开出来就**什么也别做**。不分开的话，用户点一下名字 =
    // 实例设置被收掉、详情页没出来、实例设置又被摆回去，看着就是白闪一下。
    //
    // 「回来时翻回哪颗根页」在这一刻量：这一页摆在哪颗根页上，来路就归哪一颗。
    // 收页那一刻再比一次 —— 中间用户可能已经切走、又从别的根页开了别的事
    // （那种收掉是 `evict()` 的"让位"，**不是**他按的返回，见下面那段）。
    QTabWidget *tabs = m_ui->getTabWidget();
    const int originTab = tabs != nullptr ? tabs->currentIndex() : -1;
    openResourceDetailPage(
        iconKey, kindKey, DetailSource::InstanceFiles, versionId,
        [this, versionId, kindKey, originTab](int result) {
            // 这一发是从 `closeOverlay()` 里发出来的（`pageClosed` 就发在那儿，那一页
            // 还没从"正在收"里走出来）。就在这一发里开别的一页等于在收尾途中又进一层，
            // 所以**排到下一轮**再开（和 `openDownloadCategory()` 那边同一个手法）。
            QTimer::singleShot(0, this, [this, versionId, kindKey, result, originTab] {
                if (result == QDialog::Accepted && !versionId.isEmpty() &&
                    kindKey == QStringLiteral("mod")) {
                    openInstanceSettings(versionId, QStringLiteral("config"));
                    return;
                }
                if (result != QDialog::Rejected || versionId.isEmpty()) {
                    return;
                }
                // ---------- 只在"来路还在"的时候回去 ----------
                //
                // `Rejected` 有两种来路：用户按了返回箭头 / Esc（要回去），以及这一页
                // 被**别处**顶掉（`OverlayHost::evict()` 的"让位"—— 典型：用户切到
                // 另一颗根页，又从那儿开了另一页）。后者这一页不是他收的、他也不在等
                // "回到刚才那一页"；照着回去就会把刚开起来那一页顶掉（或者两发并排
                // 排着、谁后到谁赢）。判据用**根页**：收页这一刻还停在这一页开出来的
                // 那颗根页上 ⇒ 来路还在。那一份"回去滚到哪儿"也一并作废 —— 它属于
                // 这条被放弃的路。
                if (QTabWidget *nowTabs = m_ui->getTabWidget(); nowTabs != nullptr &&
                        originTab >= 0 && nowTabs->currentIndex() != originTab) {
                    m_instanceReturn = NavigationReturn();
                    return;
                }
                // 回来时开在**刚才那一页**上（`kindKey` 就是那一页的键 —— 「这一页是哪
                // 一类资源」和「实例设置里哪一页」现在恰好同名，那个等式在
                // filesDownloadCategoryKey() 一处）。从模组点进去，返回也落在模组那一页
                // 而非概览，其余同理。
                openInstanceSettings(versionId, kindKey);
            });
        });
}

void MainWindow::openInstanceFileResource(const QString &versionId, const QString &pageKey,
                                          const QString &path) {
    // 模组以外的几类都是点资源名自动进入下载界面
    // （前提是匹配上了，模组的待会再说）。进的是**那个资源自己的那一页**（下载页里点
    // 一行开的就是它，见 openResourceDetailPage）——「前提是匹配上了」那句话只有这样
    // 才说得通：换成"切到下载页那一栏"就跟匹配不匹配没关系了。
    //
    // ① 这一页是"哪一类资源"：判据就是 filesDownloadCategoryKey() 那张表 —— 页键和下载页
    // 分类键现在恰好同名，可那一处才是"这两件事是一回事"的唯一出处。认不出来就回去，
    // 连覆盖层都不碰：悄悄地少收一页比收错了强。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const QString kindKey = InstanceSettingsDialog::filesDownloadCategoryKey(pageKey);
    if (kindKey.isEmpty() || !ResourceCatalog::kindFromCategoryKey(kindKey, &kind)) {
        return;
    }
    // ② 这一条是哪个资源。认不出来 = 没匹配上 ⇒ **什么都不做**（那一行的名字本来也没
    // 变成链接，正常走不到这儿；测试里点一下量得到的就是"没反应"）。
    // 表在那一层（反查那一趟填的，见 `InstanceIconController`）；这一层只拿那把钥匙。
    QString iconKey;
    if (!m_instanceIconController->fileResourceIconKey(path, &iconKey)) {
        return;
    }
    // ③ 先收掉实例设置那一页 —— 走 `dismiss()`（和 openDownloadCategory() 里同一个规矩）：
    // 它发的就是"用户按了返回箭头"那一发，收页该走的收尾（`pageClosed` 那一串）一条不少；
    // 只 `closeOverlay()` 的话那一页自己一个信号都收不到。
    if (m_ui->overlay() == nullptr || !m_ui->overlay()->isOpen()) {
        // 页已经关掉了（连着点两下、或者返回键和它撞上）。没有那一层要收，
        // 直接开新的就完了。
        openFileResourcePage(versionId, iconKey, kindKey);
        return;
    }
    m_ui->overlay()->dismiss();
    // ④ 摆到下一轮：这一句这会儿还在收页的调用栈里（名字上那个鼠标松开事件），
    // 覆盖层也还盖着 —— 现在就去开新页的话，openResourceDetailPage 开头那条
    // `overlay->isOpen()` 会当场把这次点击吃掉。
    //
    // NOTE: 键与键都在这一层认完了，排到下一轮带过去的只有那三个**字符串**：下一轮
    // 里这一页早就没了，那时候再去查"这一条是哪个资源"就等于查一份已经被清掉的
    // 上下文（和 openDownloadCategory 那边带 `row` 过去是同一个道理）。
    // `versionId` 也在这一层就绑好：它是"回来时把哪一页放回去"的唯一依据，而实例
    // 设置那一页这会儿已经在往下撤了。
    QTimer::singleShot(0, this, [this, versionId, iconKey, kindKey] {
        openFileResourcePage(versionId, iconKey, kindKey);
    });
}
