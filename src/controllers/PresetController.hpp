/**
 * @file PresetController.hpp
 * @brief 「实例预设」那一页的控制器：管理页的增删改、从实例提取、以及登录安装前的应用。
 * @author 0x7fffff
 * @date 2026-10-18
 *
 * 【这一层在干什么】
 *
 * `ui/PresetManagePage` 只画外面喂进去的值、把用户按了哪一颗发出去（页面那一层的文件头
 * 写了这条约定）。「读 `manifest.json`、建目录、算计划、备份、真正落盘」全在这儿：
 *
 *   · `Presets::` 那一层（`PresetStore` / `PresetCapture` / `PresetApply`）提供判据和盘；
 *   · 这一层只负责把它们串成一条用户按得动的流程，以及在页面能念的形状之间来回翻
 *     （`SectionKind` 的中文名、`ApplyPlanRow` 的行文本、`PresetApplyRow` 的结果行）。
 *
 * ★ **`ui/` 不 include `src/`**：页面收的是 `PresetManagePage::SectionItem` 这样的 POD，
 *   种类 / 模式的中文全是这一层翻好的。这个方向不许反过来。
 *
 * 【页面只建一次、留着复用】
 *
 * 和 `JavaSettingsController` 同一个理由：页面里的勾选（提取那九样）和下拉框的选择是
 * 用户的上下文，关掉再开不该丢。页面对象归 controller 所有，构造一次之后 `open()` 只刷新。
 */

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "PresetManagePage.hpp"
#include "presets/PresetApply.hpp"
#include "presets/PresetCapture.hpp"
#include "presets/PresetTypes.hpp"

class OverlayHost;
class VersionManager;
class QComboBox;
class QWidget;

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
    };

    PresetController(VersionManager *versions, OverlayHost *overlay, QWidget *pageParent,
                     Host host, QObject *parent = nullptr);
    ~PresetController() override;

    /// 打开管理页（从设置页那一栏进来）。
    void open();

    /// 预设根目录（验证工程与安装流程都问这一处，别各自拼路径）。
    static QString presetsRoot();

    // ---------- 页面发上来的那几件事（公开出来，安装流程也能调） ----------
    void refreshList(const QString &selectId = QString());
    void selectPreset(const QString &id);
    void createPreset();
    void duplicatePreset(const QString &id);
    void saveDetails(const QString &id, const QString &name, const QString &description);
    void removePreset(const QString &id);
    void setDefaultPreset(const QString &id, bool makeDefault);
    void movePreset(const QString &id, int delta);

    void captureInto(const QString &presetId, const QString &instanceId,
                     const PresetManagePage::CaptureChoice &choice);
    void captureAsNew(const QString &instanceId, const QString &name,
                      const PresetManagePage::CaptureChoice &choice);

    void previewPlan(const QString &presetId, const QString &instanceId);
    void applyPlan(const QString &presetId, const QString &instanceId, bool replaceConfirmed);

    // ---------- 给安装流程的只读入口（§7 / §8.3） ----------
    /// 可用的预设（读得回来的那些，按下标次序）。
    QList<Presets::PresetSummary> availablePresets() const;
    /// 那个唯一的默认项（可能为空 / 可能已经不在盘上，调用方按 `availablePresets()` 核）。
    QString defaultPresetId() const;
    /// 把一份预设算成计划（安装向导的「应用预览」用它，**一个字节都不写**）。
    Presets::PresetApplyResult planOnly(const Presets::PresetManifest &manifest,
                                        const Presets::PresetApplyRequest &request) const;

    // ---------- 安装页（§7） ----------
    /**
     * @brief 把"盘上有哪几份预设"灌进安装页那个下拉框。
     *
     * 第一项固定是「不使用预设」（`Qt::UserRole` 是空串）；其余按 `index.json` 的次序，
     * 默认那一份的名字尾巴上带「（默认）」，且**默认就选中它**。
     *
     * @param preferredId 用户上一次手动选的那一份（空 = 用户没选过）。它在盘上还在 ⇒
     *        选它；不在了 ⇒ 退回默认那一份。★ 这条规则就是 §7 那句「用户手动选择后，
     *        不因后台默认项变化而偷偷切换」的落点 —— 默认项只在"用户没选过"时说话。
     */
    void fillInstallSelector(QComboBox *combo, const QString &preferredId = QString(),
                             bool preferredKnown = false) const;

    /// 安装前的计划预览（`lines` 每一项是"段名 + 那一行计划"，`summary` 是一句话）。
    struct InstallPreview {
        bool ok = false;
        bool needsConfirm = false;
        QString problem;
        QString blockedReason;
        QString summary;
        QStringList lines;
        QString presetName;
    };

    /**
     * @brief 算"装好「instanceName」之后把 presetId 落上去会做什么"。
     *
     * 目标目录此刻**还没有**（实例还没装出来）：`readTargetSnapshot()` 对"没有
     * `options.txt`"是当正常新实例的，所以这就是如实的那一份计划 —— 别拿去和"装完之后
     * 再算一遍"比：那两份在通用文件组那几段上本来就该不一样（缺了才拷）。
     */
    InstallPreview previewForInstall(const QString &presetId, const QString &instanceName) const;

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
     *        盘上一个字节都不会动之前仍然过一遍 `PresetPlanner` 的那道闸（见 PresetApply）。
     */
    InstallApply applyAfterInstall(const QString &presetId, const QString &instanceName,
                                   bool replaceConfirmed);

private:
    QString instanceRoot(const QString &instanceId) const;
    QString displayName(const QString &instanceId) const;
    void log(const QString &message) const;

    void buildPage();
    void pushInstances();
    void pushPresetToPage(const QString &id);
    void pushListAndSelect(const QString &selectId);
    QList<PresetManagePage::SectionItem> sectionItems(const Presets::PresetManifest &manifest) const;
    QList<PresetManagePage::InstanceItem> instanceItems() const;
    QString newPresetName() const;
    QString captureSummary(const Presets::CaptureReport &report) const;
    QString rowText(const Presets::ApplyPlanRow &row) const;
    QString applyRowText(const Presets::PresetApplyRow &row) const;
    static QString kindText(const Presets::PresetSection &section);
    static QString modeText(Presets::ApplyMode mode);

    VersionManager *m_versions = nullptr;
    OverlayHost *m_overlay = nullptr;
    QWidget *m_pageParent = nullptr;
    Host m_host;
    PresetManagePage *m_page = nullptr;
    /// 正在显示的那一份（应用/提取都带着它出门）。
    QString m_currentId;
};
