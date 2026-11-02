/**
 * @file UpdateCenterController.hpp
 * @brief 「更新中心」那一页的**全部状态**，以及把它翻成动作的那几条（原来散在 `MainWindow` 里）。
 *
 * 【为什么单独一层】
 *
 * 这一页有三样东西是**成套的**：那一本"忽略 / 固定"的账（`UpdatePolicy::Store`）、页面对象
 * 本身、以及"这一页现在说的是哪个实例"。它们必须同进同退 —— 开页时记 id、动作处理时按 id
 * 认人、关页时清掉；账一动就得把名单重算一遍再把开着的这一页重摆。原来这三样和 `MainWindow`
 * 上其余几百个成员摆在一起，于是"漏了一条"的症状很难认（用户按了「忽略」，屏幕上那一行
 * 还是写着「可更新到 X」，而名单里已经剔掉了）。
 *
 * 【这一层不判、不画、不上网】
 *
 * * 判据在 `src/manage/UpdatePolicy`（"这一条现在要不要提示"）与 `src/manage/UpdateRollback`
 *   （"动手之前先留一份旧的"）；
 * * 页面在 `ui/UpdateCenterPage`（只画不判，`ui/` 不 include `src/`）；
 * * 真正的下载 / 文件操作仍由 `MainWindow` 那一侧的既有路径做（`updateInstanceFiles()`）。
 *
 * 这一层干的只有三件事：把检测的原始结论翻成页面认的那一份（`reportFor()`）、把用户按下去
 * 的翻成一个动作（更新 / 记一笔忽略固定 / 还原）、账动完之后把名单和界面**同一次**摆正
 * （`rebuildOutdatedPaths()`）。
 *
 * NOTE: 这一层对 `MainWindow` 是 `friend`（见 `MainWindow.hpp` 那一串）：它要读/写主窗口上
 * 几样**本来就属于更新流程**的东西（上一趟检测的结果、按实例记的"可更新"名单、覆盖层、
 * 日志）。这些的生命周期比这一页长得多，所以不搬进来。
 */

#pragma once

#include "manage/UpdatePolicy.hpp"
#include "UpdateCenterPage.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

class MainWindow;

class UpdateCenterController final : public QObject {
public:
    explicit UpdateCenterController(MainWindow *owner);

    /// 开「更新中心」那一页（`OverlayHost::showPage()`，收在内容区里）。
    ///
    /// @param instanceId 看哪个实例的结论。**必须传**（不是"当前选中的那个"）：这一页可以从
    ///                   实例设置那一页点进来，也可以从底栏 / 主页绕进来。
    void open(const QString &instanceId);

    /// 把那一页要的那份 `Report` 造出来 —— **唯一翻译点**（和 `integrityReportForPage()`
    /// / `taskCenterItem()` 是同一个路数）。忽略 / 固定那两档在这一处算好，页面自己不再判。
    UpdateCenterPage::Report reportFor(const QString &instanceId) const;

    /// 按**现在**这本账把"这个实例哪几条要更新"重算一遍，写回主窗口那份名单
    /// （`m_instanceOutdatedPaths`），并把开着的这一页重摆一次。忽略 / 固定一动就要走它
    /// —— 名单和界面必须**同一次**动，不然用户会看到"已经忽略了、但计数里还算着它"。
    ///
    /// NOTE: **不碰 `m_instanceUpdateTargets`** —— 那一份留的是**全部**"可更新"的条目
    /// （不管忽略没忽略），「不再忽略」要靠它把版本 id 找回来。
    void rebuildOutdatedPaths(const QString &instanceId);

    /// 那一本账写回盘（`<数据目录>/wtfml.updates`）。写不成**只写日志**：用户按的是一颗
    /// "以后别提示我"，弹一个"存不进去"的框除了让他分心以外没有下一步可做。
    bool savePolicy();

    /// 那一本账的**唯一读者入口**（主窗口那份 `InstanceUpdateCheckController` 收尾时要清
    /// 过期固定，`rebuildOutdatedPaths()` 也要读它）。
    UpdatePolicy::Store &policy() { return m_policy; }

private:
    void onUpdateRequested(const QStringList &paths);
    void onIgnoreToggled(const QString &path, bool ignored);
    void onPinToggled(const QString &path, bool pinned);
    void onUndoRequested();
    /// 账动完之后那一件固定要做的事：这一页正开着、而且说的就是这个实例时**当场重摆**。
    void refreshPageIfOpen(const QString &instanceId);

    MainWindow &m_owner;
    /// 「忽略某一版」「固定某一版」那一本账。**启动时读一次、改了就写**（见 `savePolicy()`）
    /// —— 和那几份会话缓存不一样，这两档说的是"以后"，必须落盘。
    UpdatePolicy::Store m_policy;
    /// 更新中心那一页（懒建一次、之后复用）。
    UpdateCenterPage *m_page = nullptr;
    /// 那一页现在说的是哪个实例的结论（**开那一页时记下来**）。
    ///
    /// 和 `m_integrityReportVersionId` 一个路数：页面是复用的、点动作时它可能已经关了，
    /// 所以"这一按是对哪个实例按的"只能从这儿来 —— 不许去问"现在选中的是哪个实例"。
    QString m_instanceId;
};
