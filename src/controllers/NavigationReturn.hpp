/**
 * @file NavigationReturn.hpp
 * @brief 「这一页是从哪颗根页上的哪一页进来的」—— 收页之后按它把来路摆回去。
 *
 * 【为什么要有这一份】
 *
 * 覆盖层一次只摆一页（见 `OverlayHost.hpp`），所以"返回"不是从页面栈里弹一层，
 * 而是**把来路那一页重新开出来**：实例设置页每次都是现开的一份
 * （`InstanceDialogController::open()`），关掉就没了 —— 它停在哪儿、文件页滚到哪儿，
 * 都得由外面记着。
 *
 * 哪几件事要"回去时还原"：
 * - 归哪颗根页（主页的快捷入口开出来的那页归**主页**，其余归「实例」）；
 * - 说的是哪个实例；
 * - 停在哪一栏；
 * - 离开那一刻文件页顶上露着的是哪一行（`InstanceFilesPage::ScrollPosition` 的身份那一半）。
 *
 * 这四样原来散在 `MainWindow` 的 `m_downloadFromInstanceId` / `m_downloadFromPageKey` /
 * `FileScrollReturn` 三处，各记各的；"来路"这件事被拆成了三份，加一条新来路就得再想
 * 一遍"回去时该读哪几位"。收成一个值之后，`openDownloadCategory()` / 详情页那一条
 * 共用同一份形状，`valid()` / `hasScroll()` 也只有一个出处。
 *
 * NOTE: 一份只描述**一条**来路。同一时刻覆盖层上只有一页，所以同时最多一条来路活着；
 * 但"逛下载页期间记着的那条"和"离开实例设置去详情页时量下的那条"是**两个位子**
 * （见 `MainWindow::m_downloadReturn` / `m_instanceReturn`）—— 它们不会同时在用，
 * 可生命周期不一样：前一条活到用户按「← 返回实例设置」，后一条是一次性的。
 */

#pragma once

#include <QString>

struct NavigationReturn {
    /// 来路归哪一颗根页（`MainWindowUI::homeTabIndex()` / `instancesTabIndex()`）。
    /// -1 = 没有来路。
    int rootTab = -1;
    /// 来路那一页说的是哪个实例（空 = 没有来路）。**判"有没有来路"只看这一位加 rootTab**。
    QString instanceId;
    /// 那一页停在哪一栏（`InstanceSettingsDialog::filePageKeys()` / 生命周期那几页里的键）。
    QString pageKey;
    /// 离开那一刻文件页顶上露着的那一行（`InstanceFilesPage::identityPath()`）。
    /// 空 = 当时没量到（页不存在 / 列表是空的）—— 回去时落回顶端就是对的。
    QString scrollIdentity;
    /// 上面那一行当时离视口顶端多少像素（这一页按行滚动时恒为 0，见 ScrollPosition）。
    int scrollDelta = 0;

    /// 有来路。
    bool valid() const { return rootTab >= 0 && !instanceId.isEmpty(); }
    /// 带回了滚动位置（`scrollIdentity` 为空时整份作废）。
    bool hasScroll() const { return !scrollIdentity.isEmpty(); }

    /// 从**某个实例的设置页**进来 / 回去。
    static NavigationReturn instanceSettings(int rootTab, const QString &instanceId,
                                            const QString &pageKey) {
        NavigationReturn ret;
        ret.rootTab = rootTab;
        ret.instanceId = instanceId;
        ret.pageKey = pageKey;
        return ret;
    }
};
