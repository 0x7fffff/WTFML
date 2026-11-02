/**
 * @file MainWindowResourceDependency.cpp
 * @brief 一键装前置时的判据：装进哪个实例、按哪个游戏版本 / 加载器 / 来源去筛、这个实例
 *        已经认过哪些模组。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这四条回答的是同一个问题——"点「装上前置」的那一下，目标到底是什么"。它们被
 * `ResourceInstallController` 那一趟异步检查读，判断结果会直接决定问回来的是谁的前置：
 *
 * - `dependencyInstanceId()`：页面开着的实例优先，否则用下载页顶上那个下拉框；
 * - `downloadCameFromInstance()`：这一条是不是从某个实例追过来的（决定装完回哪儿）；
 * - `dependencyTargetFor()`：游戏版本 / 加载器从**那个实例自己**的 meta 读，不读页面上的
 *   chip；加载器名要归一成小写那一套，否则接口不报错、只是筛不出东西；
 * - `installedModKeys()`：`mods/` 里认出来的那些，"认过没有"与"认出几个"是两件事。
 *
 * 【判据不在这一层】
 *
 * 版本号怎么读是 `VersionManager::instanceGameVersion()` 的事；"这一条是哪个资源"的表
 * 跟着反查住在 `InstanceIconController`；两家的根地址只在
 * `ModpackImport::curseForgeApiBase()` 一处。
 */

#include "MainWindow.hpp"

#include "..\ui\MainWindowUI.hpp"
#include "controllers/InstanceIconController.hpp"
#include "controllers/MainWindowViewData.hpp"
#include "controllers/ResourcePageController.hpp"
#include "download/ModDependencies.hpp"
#include "manage/ModpackImport.hpp"
#include "download/ResourceCatalog.hpp"
#include "manage/VersionManager.hpp"
#include "settings/LauncherSettings.hpp"

using namespace MainWindowViewData;

QString MainWindow::dependencyInstanceId() const {
    // ① 资源详情页**正开着**、而且它记着来路实例 ⇒ 用它。
    // 那一格是"这一版是给哪个实例的"最具体的一句：用户就是站在那个实例的页面上点的
    // 这一版（`InstanceFiles` 那条路，见 `openFileResourcePage()`；`Download` 那条路
    // 递进来的本来就是下拉框里那一个，两份值一样）。
    // NOTE: 判据带上 `open`，是为了不让**关掉那一页之后**残留的旧值去顶替下拉框 ——
    // 用户关掉详情页、回到下载页直接下另一个模组时，目标该是下拉框上那一个。
    // （异步那一趟不受影响：`ResourceInstallController::finishDependencyCheck()` 把实例**显式传下去**了。）
    if (m_resourcePage->state().open && !m_resourcePage->state().instanceId.isEmpty()) {
        return m_resourcePage->state().instanceId;
    }
    // ② 下载页顶上那个「已安装版本」下拉框。
    return selectedVersionId(m_ui->getInstalledVersionCombo());
}

bool MainWindow::downloadCameFromInstance() const {
    // ① 从实例设置点「下载新模组」切到下载页那一条（`m_downloadReturn` —— 就是那
    // 颗「← 返回实例设置」用的同一份来路）。
    if (m_downloadReturn.valid()) {
        return true;
    }
    // ② 资源详情页是从某个实例点进来的（实例设置 → 模组 → 点名字，见
    // `openInstanceFileResource()`）。那一条路不经过下载页，所以上面那一位是空的 ——
    // 而用户站在**那个实例**的模组列表里点进来的这一版，意思就是"装进它"。
    return m_resourcePage->state().open && !m_resourcePage->state().instanceId.isEmpty();
}

ModDependencies::Target MainWindow::dependencyTargetFor(const QString &instanceId,
                                                       ResourceCatalog::Source source) {
    ModDependencies::Target target;
    target.instanceId = instanceId;
    if (instanceId.isEmpty()) {
        return target;
    }
    // 游戏版本和加载器都从**这个实例自己**读（不是从页面上那颗 chip）：前置要装进的是
    // 这个实例，而 chip 是用户在挑"看哪个版本"，两者可以不一样。
    const VersionDisplayParts parts = m_versionManager->displayParts(instanceId);
    // `parts.realVersion` 那一格**只有 `wtfml.meta` 里写着 `realVersion` 才有值**，
    // 而**没有任何代码会写它**（见 VersionDisplayParts 那段自己的 NOTE）—— 所以
    // 多数实例递出去的是空串（mw_check 的 `update-target` 探针量的就是这件事）。
    //
    // `instanceGameVersion()` 读的是**同一份 meta 的同一格**（① realVersion），读不出来
    // 再顺继承链上 `clientVersion`（PCL / HMCL 那类把整份 json 合进实例目录的实例唯一
    // 说得出口的地方）和链末端那个目录名 —— **导出整合包那一趟用的就是它**，两处要的
    // 本来就是同一件事（"这个实例是哪个原版"），不该各有一份判据。（它比 ① 严一点：
    // 那个值还得**像**一个真能装出来的版本号，认不出来就回空串 = 退回"不筛"，
    // 也就是这一行今天的样子。）
    target.gameVersion = m_versionManager->instanceGameVersion(instanceId);
    // 加载器名走 instanceLoaderName()（理由同下面那道加载器闸）：PCL 那类合并实例
    // 从 parts.loaderName 读出来是空的，而那两个筛参数空着等于"不筛" —— 问回来的
    // 可能是任意加载器的前置。
    // 名字要**归一成小写那一套**（`loaderGroupKey()`）：`instanceLoaderName()` 回的是
    // 显示名（「Fabric」），而下游（`versionsUrl()` / `versionsRequestKey()`）按小写的词写。
    // 直接递显示名两边一个都对不上 —— 而接口不会报错，只是**筛不出东西**：给 Fabric
    // 实例问回来的前置可能是任意加载器的（装进去就崩）。
    target.loader = loaderGroupKey(instanceLoaderName(parts, m_versionManager, nullptr));
    // 来源跟着**这一条资源是从哪一家搜出来的**走（调用方从条目 / 页面状态里读出来递进来）：
    // 前置也去同一家问 —— 两家的项目号不是一套，拿 Modrinth 的号去 CurseForge 问，
    // 问出来的是**另一个项目**（或者什么都没有），而且不报错。
    target.source = source;
    if (target.source == ResourceCatalog::Source::CurseForge) {
        // 根地址那一处判走 ModpackImport::curseForgeApiBase（**唯一出处**，和
        // ResourceStore::fetchProjects 里面用的是同一条 —— 两边不一致的话，问出去的是
        // 一条地址、解析回来的是另一条的服务，症状是"名字永远取不到"）。
        target.apiBase = ModpackImport::curseForgeApiBase(m_settings->curseForgeApiKey(),
                                                         m_settings->curseForgeApiBaseOverride());
    }
    return target;
}

QSet<QString> MainWindow::installedModKeys(const QString &instanceId, bool *known) const {
    QSet<QString> keys;
    const QString modsDir = instanceFileDirs(instanceId).value(QStringLiteral("mod"));
    if (modsDir.isEmpty()) {
        if (known != nullptr) {
            *known = false;
        }
        return keys;
    }
    // 认出来的那些在那一层（"这一条是哪个资源"那张表跟着反查那一趟住在
    // `InstanceIconController` 里）。
    keys = m_instanceIconController->recognizedResourceKeysUnder(modsDir);
    // `known` 说的是"这个实例的 mods 到底认过没有"，而不是"认出来几个" ——
    // 一个模组都没认出（新实例、全是自己改过的 jar）和"没进过文件管理页"都表现为空表，
    // 而界面上那两句话不一样（见 `ResourceInstallController::startDependencyCheck()`）。
    if (known != nullptr) {
        *known = m_instanceIconController->recognizedInstanceIs(instanceId);
    }
    return keys;
}
