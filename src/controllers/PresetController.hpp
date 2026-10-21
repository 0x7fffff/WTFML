/**
 * @file PresetController.hpp
 * @brief 「实例预设」那一页的控制器：管理页的增删改、从实例提取、以及登录安装前的应用。
 *
 * 【这一层在干什么】
 *
 * `ui/PresetManagePage` 只画外面喂进去的值、把用户按了哪一颗发出去（页面那一层的文件头
 * 写了这条约定）。「读 `manifest.json`、建目录、算计划、备份、真正落盘」全在这儿：
 *
 * · `Presets::` 那一层（`PresetStore` / `PresetCapture` / `PresetApply`）提供判据和盘；
 * · 这一层只负责把它们串成一条用户按得动的流程，以及在页面能念的形状之间来回翻
 * （`SectionKind` 的中文名、`ApplyPlanRow` 的行文本、`PresetApplyRow` 的结果行）。
 *
 * **`ui/` 不 include `src/`**：页面收的是 `PresetManagePage::SectionItem` 这样的 POD，
 * 种类 / 模式的中文全是这一层翻好的。这个方向不许反过来。
 *
 * 【页面只建一次、留着复用】
 *
 * 和 `JavaSettingsController` 同一个理由：页面里的勾选（提取那九样）和下拉框的选择是
 * 用户的上下文，关掉再开不该丢。页面对象归 controller 所有，构造一次之后 `open()` 只刷新。
 *
 * 【页面落在实例页的第三个模式页里】
 * 它不单独占主选项卡，由实例页的模式切换控制。
 */

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <QPointer>

#include "PresetManagePage.hpp"
#include "presets/PresetApply.hpp"
#include "presets/PresetCapture.hpp"
#include "presets/PresetCompatibility.hpp"
#include "presets/PresetTypes.hpp"

class VersionManager;
class QComboBox;
class QWidget;

namespace Presets {
class PlatformCompatibilityProvider;
}

class PresetController final : public QObject {
    Q_OBJECT

public:
    struct Host {
        /// 实例目录名 → 显示名（空串时调用方自己会退回 id）。
        std::function<QString(const QString &)> displayName;
        /// 这个实例现在在不在跑（应用那一趟整趟不写的判据）。
        std::function<bool(const QString &)> isRunning;
        /// 一句进主日志的话。
        std::function<void(const QString &)> log;
        /// 记 / 摘"这一份实例正在搬文件"（唯一出处是 `MainWindow::installingVersionIds()`）。
        /// 模组预设那一趟要联网查版本、下模组、往 `mods/` 里写文件，这期间这一份实例
        /// 不能被启动、不能被删。
        std::function<void(const QString &, bool)> setFileOperationBusy;
        /// 这一份实例**此刻**是不是已经在别的写目录作业手里（复制实例 / 还原 / 迁移 /
        /// 导入整合包）。
        /// 应用预设同样往实例目录里写（模组预设写 `mods/`，普通预设改
        /// `options.txt` / 服务器清单等），和后台拷树撞在一起时两边都不知道对方在写
        /// 所以开跑之前先问一句；别处那几条路都能查到这个名单，预设这一页也要查。
        /// 问的必须**只是搬文件那一份**，不是"不能启动"那个合集：安装向导收尾那一步
        /// 是带着 `m_installingInstanceId` 调进来的（那是它自己的标记）。
        std::function<bool(const QString &)> isFileOperationBusy;
        /// 这一份实例此刻**任何一样**写作业占着它没有（= 不能启动的那个合集：
        /// 后台搬文件 + 正在装 + 正在补全/修复）。只给 `applyPlan()` 用。
        /// 预设页那颗「应用」是用户直接发起的，不会出现"安装向导自己挡
        /// 自己"；而它要防的不只是复制 / 还原 / 导入 —— 正被安装、正被补全文件的
        /// 实例同样不能在这时候往目录里落预设。`installModsAfterInstall()` 继续问窄的
        /// `isFileOperationBusy`：安装向导收尾那一步带着自己的标记进来，宽的会把这一趟
        /// 自己拦掉。
        std::function<bool(const QString &)> isInstanceBusy;
        /// 重灌实例清单（`MainWindow::refreshInstalledVersions()`）。模组预设收尾之后要叫
        /// 一次：这一趟记着"搬文件忙"，而刷新那条路会按 `installingVersionIds()` 把忙的
        /// 实例**整份滤掉** —— 万一这期间别的事（下载收尾、改设置）已经刷过一次，用户看到
        /// 的就是"实例没了"，不重刷它就一直不在列表里。
        std::function<void()> refreshInstances;
    };

    PresetController(VersionManager *versions, QWidget *hostPage, Host host,
                     QObject *parent = nullptr);
    ~PresetController() override;

    /// 刷新管理页（实例页切到「预设」那一档时调用；页面是常驻的，这里只重新灌数据）。
    void open();
    /// 实例清单变了（后台装完一份 / 删掉一份 / 换了游戏文件夹）⇒ 重灌那两个实例下拉框。
    ///
    /// 页面还没建出来（用户从没切到过「预设」那一档）时什么都不做；建过的话
    /// `fillInstances()` 按 id 保留当前选择。**不能只在 `open()` 里灌**：切到别的顶栏页
    /// 之后后台装好一份实例，再切回实例页时模式行还停在「预设」上，`open()` 不会被叫到。
    void refreshInstances();

    /// 预设根目录（验证工程与安装流程都问这一处，别各自拼路径）。
    static QString presetsRoot();

    // ---------- 页面发上来的那几件事（公开出来，安装流程也能调） ----------
    void refreshList(const QString &selectId = QString());
    void selectPreset(const QString &id);
    /// @param categoryKey 页面「新建为」下拉框里那一项（`presetCategoryKey()` 的串）；
    /// 空串 / 认不出 ⇒ 按「游戏设置」建（界面上第一项就是它）。
    void createPreset(const QString &categoryKey);
    void duplicatePreset(const QString &id);
    void saveDetails(const QString &id, const QString &name, const QString &description);
    void removePreset(const QString &id);
    void setDefaultPreset(const QString &id, bool makeDefault);
    void movePreset(const QString &id, int delta);

    void captureInto(const QString &presetId, const QString &instanceId,
                     const PresetManagePage::CaptureChoice &choice);
    void captureAsNew(const QString &instanceId, const QString &name,
                      const QString &categoryKey,
                      const PresetManagePage::CaptureChoice &choice);

    void previewPlan(const QString &presetId, const QString &instanceId);
    void applyPlan(const QString &presetId, const QString &instanceId, bool replaceConfirmed);

    // ---------- 给安装流程的只读入口 ----------
    // / 可用的预设（读得回来的那些，按下标次序）。
    QList<Presets::PresetSummary> availablePresets() const;
    /// 那个唯一的默认项（可能为空 / 可能已经不在盘上，调用方按 `availablePresets()` 核）。
    QString defaultPresetId() const;
    /// 把一份预设算成计划（安装向导的「应用预览」用它，**一个字节都不写**）。
    Presets::PresetApplyResult planOnly(const Presets::PresetManifest &manifest,
                                        const Presets::PresetApplyRequest &request) const;

    // ---------- 安装页 ----------
    /**
     * @brief 把"盘上有哪几份预设"灌进安装页那个下拉框。
     *
     * 第一项固定是「不使用预设」（`Qt::UserRole` 是空串）；其余按 `index.json` 的次序，
     * 默认那一份的名字尾巴上带「（默认）」，且**默认就选中它**。
     *
     * @param preferredId 用户上一次手动选的那一份（空 = 用户没选过）。它在盘上还在 ⇒
     * 选它；不在了 ⇒ 退回默认那一份。★ 这条规则就是「用户手动选择后，
     * 不因后台默认项变化而偷偷切换」的落点 —— 默认项只在"用户没选过"时说话。
     */
    void fillInstallSelector(QComboBox *combo, const QString &preferredId = QString(),
                             bool preferredKnown = false) const;

    /// 安装页弹窗里的**一类**（次序 = 页签次序，`key` 是 `presetCategoryKey()` 的串）。
    struct InstallCategory {
        QString key;
        QString title;
    };
    /// 四类（游戏设置 / 模组 / 资源包 / 数据包）。★ 「旧版（混合）」`Unset` **不在里面**：
    /// "不静默拆分类别"在安装页这一侧同样是"不猜它属于哪一类"。
    static QList<InstallCategory> installCategories();

    /// 某一类的可用预设（按下标次序，只含读得回来的那些）。认不出的 key 回空表。
    QList<Presets::PresetSummary> presetsInCategory(const QString &categoryKey) const;

    /// 安装前的计划预览（`lines` 每一项是"段名 + 那一行计划"，`summary` 是一句话）。
    struct InstallPreview {
        bool ok = false;
        bool needsConfirm = false;
        QString problem;
        QString blockedReason;
        QString summary;
        QStringList lines;
        QString presetName;
        QString fingerprint;
        /// 这一趟真会动的项数（`ApplyPlan::totalCount()`）。状态行那句「会动 12 项」用它。
        int itemCount = 0;
        /// 预设里有、这一趟不动的项数（各段 `skipped` 之和）。
        int skippedCount = 0;
        /// 这一份里**做不了的段落数**（不认识的种类 / 那一段读不回来 / 目标读不动）。
        int blockedCount = 0;
        /// 「查看计划与原因」展开之后那几行：只看得出问题的段（做不了 / 要确认 / 会删 /
        /// 有冲突 / 要人选 / 跳过了东西）。
        QStringList reasons;
    };

    /**
     * @brief 算"装好「instanceName」之后把 presetId 落上去会做什么"。
     *
     * 目标目录此刻**还没有**（实例还没装出来）：`readTargetSnapshot()` 对"没有
     * `options.txt`"是当正常新实例的，所以这就是如实的那一份计划 —— 别拿去和"装完之后
     * 再算一遍"比：那两份在通用文件组那几段上本来就该不一样（缺了才拷）。
     */
    InstallPreview previewForInstall(const QString &presetId, const QString &instanceName) const;
    QString presetFingerprint(const QString &presetId) const;

    /// 安装收尾那一步的结果。
    struct InstallApply {
        bool ok = false;
        bool needsConfirm = false;
        QString message;  ///< 给日志/提示的**一句人话**（成功是一段小结，失败是原因）
        QString backupDir;
        int written = 0;
        int removed = 0;
    };

    /**
     * @brief 把一份预设落到**刚装好的**实例上（安装流程的最后一步）。
     *
     * @param replaceConfirmed 安装前那次预览里用户已经看过「覆盖」那几行并点了同意 ⇒ true。
     * 盘上一个字节都不会动之前仍然过一遍 `PresetPlanner` 的那道闸（见 PresetApply）。
     */
    InstallApply applyAfterInstall(const QString &presetId, const QString &instanceName,
                                   bool replaceConfirmed);

    // ---------- 安装前的兼容检查 ----------
    /**
     * @brief 一次兼容检查的进度快照。
     *
     * `presetId` + `generation` 一起给出来，是因为这两样任一变了，界面手上那份就过期了
     * （用户换了一份 / 又开了一次检查）。`results` 的次序 = 引用首次出现的次序。
     */
    struct ModCheckUpdate {
        QString presetId;
        int generation = 0;
        QList<Presets::CompatibilityResult> results;
        Presets::CompatibilitySummary summary;
        /// 这一代是不是已经全部有结论（`updated` 与 `finished` 共用一个载荷）。
        bool finished = false;
    };

    /// 那一份预设里的模组引用（读不出来 / 没有引用 ⇒ 空表）。
    QList<Presets::ModReference> modReferencesFor(const QString &presetId) const;

    /**
     * @brief 开始查「这一份模组预设在这个版本 / 加载器下能不能拿到」。
     *
     * 弹窗**先出来**、这里再查：本地能判的立刻有结论，剩下的按
     * 并发上限排队；网络失败 / 超时只算「暂无法确认」，**不计入缺失**。
     * 在途期间再调一次就是新一代，旧回调作废（generation token）。
     */
    void startModCheck(const QString &presetId, const QString &gameVersion, const QString &loader,
                       bool clientSide = true);
    /// 作废当前这一代（关弹窗时调；不改变已经出来的结论）。
    void cancelModCheck();
    int modCheckGeneration() const;

    /// CurseForge 那一侧用的 key（设置里用户自己填的那一个）。空 = 那一侧一律「暂无法确认」。
    void setCurseForgeApiKey(const QString &apiKey);
    void installModsAfterInstall(const QStringList &presetIds, const QString &instanceId,
                                 std::function<void(bool, const QString &)> completed);
    bool cancelModInstall();
    bool isInstallingMods() const { return !m_modInstallTask.isNull(); }

signals:
    /// 兼容检查每出一条 / 收尾都会发（`finished` 那一发也在这条信号上）。
    void modCheckUpdated(const PresetController::ModCheckUpdate &update);

private:
    QString instanceRoot(const QString &instanceId) const;
    QString displayName(const QString &instanceId) const;
    void log(const QString &message) const;

    void buildPage();
    void pushCategories();
    void pushInstances();
    void pushPresetToPage(const QString &id);
    void pushListAndSelect(const QString &selectId);
    /// 这一类的预设允许收哪几样（`PresetManagePage::CaptureBit` 那九位）。
    static quint32 captureMaskFor(Presets::PresetCategory category);
    /// 掩码对应的"为什么这一类不收那几样"（挂到界面上被禁掉那几格的工具提示里）。
    static QString captureNoteFor(Presets::PresetCategory category);
    /// 把页面勾的那几样按类别闸一遍；被闸掉的样名收进 @p skipped（可为空）。
    static Presets::CaptureSelection filterCapture(Presets::PresetCategory category,
                                                   const Presets::CaptureSelection &selection,
                                                   QStringList *skipped);
    /// 类别 → 界面上的中文。★ 只有这一处（`ui/` 不认识 `PresetCategory`）。
    static QString categoryText(Presets::PresetCategory category);
    QList<PresetManagePage::SectionItem> sectionItems(const Presets::PresetManifest &manifest) const;
    QList<PresetManagePage::InstanceItem> instanceItems() const;
    QString newPresetName() const;
    QString captureSummary(const Presets::CaptureReport &report) const;
    QString rowText(const Presets::ApplyPlanRow &row) const;
    QString applyRowText(const Presets::PresetApplyRow &row) const;
    static QString kindText(const Presets::PresetSection &section);
    static QString modeText(Presets::ApplyMode mode);

    VersionManager *m_versions = nullptr;
    /// 页面落进的那个宿主要器（实例页第三页，`getInstancePresetPage()`）。
    QWidget *m_hostPage = nullptr;
    Host m_host;
    PresetManagePage *m_page = nullptr;
    /// 正在显示的那一份（应用/提取都带着它出门）。
    QString m_currentId;
    /// 兼容检查那一套（缓存跨弹窗共享；checker / provider 都归这一层所有）。
    Presets::CompatibilityCache m_modCache;
    Presets::CompatibilityChecker *m_modChecker = nullptr;
    Presets::PlatformCompatibilityProvider *m_modProvider = nullptr;
    /// 这一代查的是哪一份（信号里原样带回，界面按它判"还是不是我现在选的那一份"）。
    QString m_modCheckPresetId;
    QPointer<QObject> m_modInstallTask;
    QString m_presetApiKey;
};
