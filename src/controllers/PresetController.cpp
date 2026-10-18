/**
 * @file PresetController.cpp
 * @brief 实现 PresetController.hpp。
 * @author 0x7fffff
 * @date 2026-10-18
 *
 * 这一份里只有三类句子：**读盘**（`Presets::loadManifest` / `readTargetSnapshot`）、
 * **把结果翻成页面认识的形状**、以及**把页面发上来的那一下交给 `Presets::` 那一层**。
 * 判据一条都不在这儿 —— 段怎么落、什么时候要确认、备份什么，全在 `src/presets/`。
 */

#include "controllers/PresetController.hpp"

#include "MainWindowUI.hpp"
#include "OverlayHost.hpp"
#include "PresetManagePage.hpp"
#include "manage/VersionManager.hpp"
#include "presets/GenericFiles.hpp"
#include "presets/MalilibKeys.hpp"
#include "presets/PresetApply.hpp"
#include "presets/PresetPlanner.hpp"
#include "presets/PresetStore.hpp"
#include "util/AppPaths.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSignalBlocker>

namespace {

Presets::CaptureSelection toSelection(const PresetManagePage::CaptureChoice &choice) {
    Presets::CaptureSelection selection;
    selection.vanillaOptions = choice.vanillaOptions;
    selection.vanillaKeys = choice.vanillaKeys;
    selection.servers = choice.servers;
    selection.config = choice.config;
    selection.defaultConfigs = choice.defaultConfigs;
    selection.resourcePacks = choice.resourcePacks;
    selection.shaderPacks = choice.shaderPacks;
    selection.malilib = choice.malilib;
    selection.malilibValues = choice.malilibValues;
    return selection;
}

/// 段种类 → 中文。★ 只有这一处（页面那一层不认识 `SectionKind`）。
QString kindTextOf(Presets::SectionKind kind) {
    switch (kind) {
    case Presets::SectionKind::VanillaOptions:
        return QStringLiteral("游戏设置");
    case Presets::SectionKind::VanillaKeys:
        return QStringLiteral("原版按键");
    case Presets::SectionKind::ServerList:
        return QStringLiteral("服务器列表");
    case Presets::SectionKind::MalilibKeys:
        return QStringLiteral("MaLiLib 热键");
    case Presets::SectionKind::GenericFiles:
        return QStringLiteral("通用文件组");
    case Presets::SectionKind::Custom:
        break;
    }
    return QStringLiteral("自定义");
}

/// 安装页那个下拉框里"什么都不应用"那一项。**只有这一处**：灌数据的是这里，
/// 判"要不要应用"的调用方读的是同一项的 `Qt::UserRole`（空串），文字只在屏幕上出现。
QString installPresetNoneText() {
    return QStringLiteral("不使用预设");
}

QString sectionKindText(const Presets::PresetSection &section) {
    if (section.kind == Presets::SectionKind::GenericFiles) {
        const QString subKey = Presets::fileGroupDirName(Presets::FileGroup::Config);
        if (section.id == Presets::sectionIdFor(section.kind, subKey)) {
            return QStringLiteral("配置文件 config/");
        }
        if (section.id == Presets::sectionIdFor(
                                section.kind,
                                Presets::fileGroupDirName(Presets::FileGroup::DefaultConfigs))) {
            return QStringLiteral("默认配置 defaultconfigs/");
        }
        if (section.id == Presets::sectionIdFor(
                                section.kind,
                                Presets::fileGroupDirName(Presets::FileGroup::ResourcePacks))) {
            return QStringLiteral("资源包");
        }
        if (section.id == Presets::sectionIdFor(
                                section.kind,
                                Presets::fileGroupDirName(Presets::FileGroup::ShaderPacks))) {
            return QStringLiteral("光影包");
        }
        return kindTextOf(section.kind);
    }
    if (section.kind == Presets::SectionKind::MalilibKeys) {
        return kindTextOf(section.kind);
    }
    return kindTextOf(section.kind);
}

}  // namespace

PresetController::PresetController(VersionManager *versions, OverlayHost *overlay,
                                   QWidget *pageParent, Host host, QObject *parent)
    : QObject(parent), m_versions(versions), m_overlay(overlay), m_pageParent(pageParent),
      m_host(std::move(host)) {}

PresetController::~PresetController() = default;

QString PresetController::presetsRoot() {
    return Presets::presetsRoot();
}

// ---------------------------------------------------------------------------
// 打开
// ---------------------------------------------------------------------------

void PresetController::open() {
    if (m_overlay == nullptr) {
        return;
    }
    buildPage();
    pushInstances();
    pushListAndSelect(m_currentId.isEmpty() ? Presets::defaultPresetId(presetsRoot())
                                            : m_currentId);
    m_overlay->execPage(m_page, m_page->titleText(), MainWindowUI::settingsTabIndex());
}

void PresetController::buildPage() {
    if (m_page != nullptr) {
        return;
    }
    m_page = new PresetManagePage(m_pageParent);

    connect(m_page, &PresetManagePage::presetSelected, this, &PresetController::selectPreset);
    connect(m_page, &PresetManagePage::createRequested, this, &PresetController::createPreset);
    connect(m_page, &PresetManagePage::duplicateRequested, this,
            &PresetController::duplicatePreset);
    connect(m_page, &PresetManagePage::detailsEdited, this,
            [this](const QString &id, const QString &name, const QString &description) {
                saveDetails(id, name, description);
            });
    connect(m_page, &PresetManagePage::deleteRequested, this, &PresetController::removePreset);
    connect(m_page, &PresetManagePage::defaultRequested, this,
            &PresetController::setDefaultPreset);
    connect(m_page, &PresetManagePage::moveRequested, this, &PresetController::movePreset);
    connect(m_page, &PresetManagePage::captureRequested, this,
            [this](const QString &presetId, const QString &instanceId,
                   const PresetManagePage::CaptureChoice &choice) {
                captureInto(presetId, instanceId, choice);
            });
    connect(m_page, &PresetManagePage::captureAsNewRequested, this,
            [this](const QString &instanceId, const QString &name,
                   const PresetManagePage::CaptureChoice &choice) {
                captureAsNew(instanceId, name, choice);
            });
    connect(m_page, &PresetManagePage::planRequested, this, &PresetController::previewPlan);
    connect(m_page, &PresetManagePage::applyRequested, this, &PresetController::applyPlan);
}

void PresetController::pushInstances() {
    if (m_page == nullptr) {
        return;
    }
    m_page->setInstances(instanceItems());
}

QString PresetController::instanceRoot(const QString &instanceId) const {
    return AppPaths::versionDir(instanceId);
}

QString PresetController::displayName(const QString &instanceId) const {
    if (m_host.displayName) {
        const QString name = m_host.displayName(instanceId);
        if (!name.isEmpty()) {
            return name;
        }
    }
    return instanceId;
}

void PresetController::log(const QString &message) const {
    if (m_host.log) {
        m_host.log(message);
    }
}

QList<PresetManagePage::InstanceItem> PresetController::instanceItems() const {
    QList<PresetManagePage::InstanceItem> items;
    if (m_versions == nullptr) {
        return items;
    }
    for (const QString &id : m_versions->installedVersions()) {
        PresetManagePage::InstanceItem item;
        item.id = id;
        item.name = displayName(id);
        item.running = m_host.isRunning ? m_host.isRunning(id) : false;
        items.append(item);
    }
    return items;
}

// ---------------------------------------------------------------------------
// 列表
// ---------------------------------------------------------------------------

void PresetController::refreshList(const QString &selectId) {
    if (m_page == nullptr) {
        return;
    }
    pushListAndSelect(selectId);
}

void PresetController::pushListAndSelect(const QString &selectId) {
    const QString root = presetsRoot();
    QList<Presets::BrokenPreset> broken;
    const QList<Presets::PresetSummary> summaries = Presets::list(root, &broken);

    QList<PresetManagePage::PresetItem> items;
    for (const Presets::PresetSummary &summary : summaries) {
        PresetManagePage::PresetItem item;
        item.id = summary.id;
        item.name = summary.name;
        item.description = summary.description;
        if (summary.updatedAt.isValid()) {
            item.updatedText =
                QStringLiteral("最后修改：%1")
                    .arg(summary.updatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        }
        item.sectionCount = summary.sectionCount;
        item.isDefault = summary.isDefault;
        items.append(item);
    }

    QList<PresetManagePage::BrokenItem> brokenItems;
    for (const Presets::BrokenPreset &one : broken) {
        PresetManagePage::BrokenItem item;
        item.id = one.id;
        item.reason = one.reason;
        brokenItems.append(item);
    }

    const QString keep = selectId.isEmpty() ? Presets::defaultPresetId(root) : selectId;
    QString chosen;
    for (const PresetManagePage::PresetItem &item : items) {
        if (item.id == keep) {
            chosen = keep;
            break;
        }
    }
    if (chosen.isEmpty() && !items.isEmpty()) {
        chosen = items.first().id;
    }

    m_page->setPresets(items, brokenItems, chosen);
    if (chosen.isEmpty()) {
        m_currentId.clear();
        return;
    }
    selectPreset(chosen);
}

void PresetController::selectPreset(const QString &id) {
    if (m_page == nullptr || id.isEmpty()) {
        return;
    }
    m_currentId = id;
    pushPresetToPage(id);
}

void PresetController::pushPresetToPage(const QString &id) {
    bool ok = false;
    QString reason;
    const Presets::PresetManifest manifest =
        Presets::loadManifest(presetsRoot(), id, &ok, &reason);
    if (!ok) {
        m_page->showStatus(QStringLiteral("这一份读不出来：%1").arg(reason));
        m_page->clearPlan();
        return;
    }
    m_page->showPreset(id, manifest.name, manifest.description, sectionItems(manifest));
}

QList<PresetManagePage::SectionItem> PresetController::sectionItems(
    const Presets::PresetManifest &manifest) const {
    QList<PresetManagePage::SectionItem> items;
    for (const Presets::PresetSection &section : manifest.sections) {
        PresetManagePage::SectionItem item;
        item.title = section.title;
        item.kindText = sectionKindText(section);
        item.modeText = modeText(section.mode);
        item.itemCount = static_cast<int>(section.items.size());
        if (item.title.isEmpty()) {
            item.title = item.kindText;
        }
        items.append(item);
    }
    return items;
}

QString PresetController::modeText(Presets::ApplyMode mode) {
    return Presets::applyPlanVerb(mode);
}

QString PresetController::kindText(const Presets::PresetSection &section) {
    return sectionKindText(section);
}

QString PresetController::newPresetName() const {
    const QString root = presetsRoot();
    QStringList taken;
    for (const Presets::PresetSummary &summary : Presets::list(root)) {
        taken.append(summary.name);
    }
    const QString base = QStringLiteral("新预设");
    if (!taken.contains(base)) {
        return base;
    }
    for (int index = 2; index < 1000; ++index) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(index);
        if (!taken.contains(candidate)) {
            return candidate;
        }
    }
    return base;
}

// ---------------------------------------------------------------------------
// 增删改
// ---------------------------------------------------------------------------

void PresetController::createPreset() {
    const Presets::PresetManifest created =
        Presets::create(presetsRoot(), newPresetName());
    if (created.id.isEmpty()) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("没能建出新的一份（预设目录写不进去）。"));
        }
        return;
    }
    log(QStringLiteral("预设：新建了「%1」。").arg(created.name));
    refreshList(created.id);
    if (m_page != nullptr) {
        m_page->showStatus(QStringLiteral("建好了。在右边改名字和说明，然后从实例提取内容。"));
    }
}

void PresetController::duplicatePreset(const QString &id) {
    const Presets::PresetManifest copy = Presets::duplicate(presetsRoot(), id);
    if (copy.id.isEmpty()) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("复制失败：这一份读不回来，或者新目录建不出来。"));
        }
        return;
    }
    log(QStringLiteral("预设：复制出「%1」。").arg(copy.name));
    refreshList(copy.id);
}

void PresetController::saveDetails(const QString &id, const QString &name,
                                   const QString &description) {
    const QString root = presetsRoot();
    bool ok = false;
    QString reason;
    Presets::PresetManifest manifest = Presets::loadManifest(root, id, &ok, &reason);
    if (!ok) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("这一份读不出来：%1").arg(reason));
        }
        return;
    }
    const QString cleaned = Presets::cleanDisplayName(name);
    if (cleaned.isEmpty()) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("名字不能空着。"));
        }
        return;
    }
    const bool renamed = cleaned != manifest.name;
    if (!renamed && description == manifest.description) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("没有要保存的改动。"));
        }
        return;
    }
    manifest.name = cleaned;
    manifest.description = description.trimmed();
    manifest.updatedAt = QDateTime::currentDateTime();
    if (!Presets::saveManifest(root, manifest)) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("保存失败：那一份写不进去。"));
        }
        return;
    }
    if (renamed) {
        log(QStringLiteral("预设：改名为「%1」。").arg(cleaned));
    }
    refreshList(id);
    if (m_page != nullptr) {
        m_page->showStatus(QStringLiteral("保存了。"));
    }
}

void PresetController::removePreset(const QString &id) {
    bool ok = false;
    QString reason;
    const Presets::PresetManifest manifest =
        Presets::loadManifest(presetsRoot(), id, &ok, &reason);
    const QString name = ok ? manifest.name : displayName(id);
    if (!Presets::remove(presetsRoot(), id)) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("删除失败：那一份的目录删不掉。"));
        }
        return;
    }
    log(QStringLiteral("预设：删除了「%1」。").arg(name));
    if (m_currentId == id) {
        m_currentId.clear();
    }
    refreshList(QString());
    if (m_page != nullptr) {
        m_page->showStatus(QStringLiteral("删掉了「%1」。").arg(name));
    }
}

void PresetController::setDefaultPreset(const QString &id, bool makeDefault) {
    if (!Presets::setDefault(presetsRoot(), makeDefault ? id : QString())) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("默认项没能改：那一份已经不在列表里了。"));
        }
        return;
    }
    log(makeDefault ? QStringLiteral("预设：默认项改成了「%1」。").arg(displayName(id))
                    : QStringLiteral("预设：取消了默认项。"));
    refreshList(id);
}

void PresetController::movePreset(const QString &id, int delta) {
    if (!Presets::move(presetsRoot(), id, delta)) {
        return;  // 已经在边上：不报错（按钮那一侧也是按这个判的）
    }
    refreshList(id);
}

// ---------------------------------------------------------------------------
// 提取
// ---------------------------------------------------------------------------

QString PresetController::captureSummary(const Presets::CaptureReport &report) const {
    QStringList parts;
    if (report.optionKeys > 0) {
        parts.append(QStringLiteral("游戏设置 %1 项").arg(report.optionKeys));
    }
    if (report.keybindings > 0) {
        parts.append(QStringLiteral("按键 %1 项").arg(report.keybindings));
    }
    if (report.servers > 0) {
        parts.append(QStringLiteral("服务器 %1 个").arg(report.servers));
    }
    if (report.configItems > 0) {
        parts.append(QStringLiteral("config %1 个").arg(report.configItems));
    }
    if (report.defaultConfigItems > 0) {
        parts.append(QStringLiteral("defaultconfigs %1 个").arg(report.defaultConfigItems));
    }
    if (report.resourcePackItems > 0) {
        parts.append(QStringLiteral("资源包 %1 个").arg(report.resourcePackItems));
    }
    if (report.shaderPackItems > 0) {
        parts.append(QStringLiteral("光影包 %1 个").arg(report.shaderPackItems));
    }
    if (report.malilibHotkeys > 0) {
        parts.append(QStringLiteral("MaLiLib 热键 %1 格").arg(report.malilibHotkeys));
    }
    if (report.malilibValues > 0) {
        parts.append(QStringLiteral("MaLiLib 值 %1 格").arg(report.malilibValues));
    }
    QString text = parts.isEmpty() ? QStringLiteral("什么都没收到。")
                                   : QStringLiteral("收到了 %1。").arg(parts.join(QStringLiteral("，")));
    if (!report.problems.isEmpty()) {
        text += QStringLiteral("\n没收到：%1")
                    .arg(report.problems.join(QStringLiteral("；")));
    }
    return text;
}

void PresetController::captureInto(const QString &presetId, const QString &instanceId,
                                   const PresetManagePage::CaptureChoice &choice) {
    const QString root = presetsRoot();
    bool ok = false;
    QString reason;
    Presets::PresetManifest manifest = Presets::loadManifest(root, presetId, &ok, &reason);
    if (!ok) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("这一份读不出来：%1").arg(reason));
        }
        return;
    }

    Presets::CaptureRequest request;
    request.instanceId = instanceId;
    request.instanceRoot = instanceRoot(instanceId);
    request.presetFilesRoot = Presets::filesDir(root, presetId);
    request.selection = toSelection(choice);

    const Presets::CaptureReport report = Presets::captureFromInstance(request);
    if (!report.ok) {
        if (m_page != nullptr) {
            m_page->showStatus(report.problem.isEmpty()
                                   ? QStringLiteral("什么都没收到。")
                                   : QStringLiteral("提取失败：%1").arg(report.problem));
        }
        return;
    }

    manifest.sections = Presets::mergeCapturedSections(manifest.sections, report.sections);
    manifest.sourceInstanceId = instanceId;
    manifest.updatedAt = QDateTime::currentDateTime();
    if (!Presets::saveManifest(root, manifest)) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("提取到了内容，但这一份预设写不进去。"));
        }
        return;
    }

    log(QStringLiteral("预设「%1」：从 %2 收了 %3 段。")
            .arg(manifest.name, displayName(instanceId))
            .arg(report.sectionCount()));
    refreshList(presetId);
    if (m_page != nullptr) {
        m_page->showStatus(captureSummary(report));
    }
}

void PresetController::captureAsNew(const QString &instanceId, const QString &name,
                                    const PresetManagePage::CaptureChoice &choice) {
    QString wanted = Presets::cleanDisplayName(name);
    if (wanted.isEmpty()) {
        wanted = QStringLiteral("%1 预设").arg(displayName(instanceId));
    }
    const QString root = presetsRoot();
    Presets::PresetManifest created = Presets::create(root, wanted);
    if (created.id.isEmpty()) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("没能建出新的一份（预设目录写不进去）。"));
        }
        return;
    }

    Presets::CaptureRequest request;
    request.instanceId = instanceId;
    request.instanceRoot = instanceRoot(instanceId);
    request.presetFilesRoot = Presets::filesDir(root, created.id);
    request.selection = toSelection(choice);

    const Presets::CaptureReport report = Presets::captureFromInstance(request);
    if (!report.ok) {
        // 什么都没收到 ⇒ 这一份空壳留着只会碍眼，撤掉（`create()` 那一步刚建的目录）。
        Presets::remove(root, created.id);
        if (m_page != nullptr) {
            m_page->showStatus(report.problem.isEmpty()
                                   ? QStringLiteral("什么都没收到，新预设没有建。")
                                   : QStringLiteral("提取失败：%1").arg(report.problem));
        }
        return;
    }

    created.sections = report.sections;
    created.sourceInstanceId = instanceId;
    created.updatedAt = QDateTime::currentDateTime();
    if (!Presets::saveManifest(root, created)) {
        Presets::remove(root, created.id);
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("预设目录写不进去，新预设没有建。"));
        }
        return;
    }

    log(QStringLiteral("预设：从 %1 提取出「%2」（%3 段）。")
            .arg(displayName(instanceId), created.name)
            .arg(report.sectionCount()));
    refreshList(created.id);
    if (m_page != nullptr) {
        m_page->showStatus(captureSummary(report));
    }
}

// ---------------------------------------------------------------------------
// 计划 / 应用
// ---------------------------------------------------------------------------

QString PresetController::rowText(const Presets::ApplyPlanRow &row) const {
    QString head = row.title;
    if (head.isEmpty()) {
        head = kindTextOf(row.kind);
    }
    QString text = QStringLiteral("%1  %2").arg(head, Presets::applyPlanRowText(row));
    if (!row.problem.isEmpty()) {
        text += QStringLiteral("（%1）").arg(row.problem);
    }
    return text;
}

void PresetController::previewPlan(const QString &presetId, const QString &instanceId) {
    if (m_page == nullptr) {
        return;
    }
    PresetManagePage::PlanView view;

    bool ok = false;
    QString reason;
    const Presets::PresetManifest manifest =
        Presets::loadManifest(presetsRoot(), presetId, &ok, &reason);
    if (!ok) {
        view.problem = QStringLiteral("这一份读不出来：%1").arg(reason);
        m_page->showPlan(view);
        return;
    }

    QString snapshotProblem;
    const Presets::TargetSnapshot snapshot =
        Presets::readTargetSnapshot(instanceRoot(instanceId), &snapshotProblem);
    if (!snapshotProblem.isEmpty()) {
        view.problem = snapshotProblem;
        m_page->showPlan(view);
        return;
    }

    Presets::PlanRequest request;
    request.presetFilesRoot = Presets::filesDir(presetsRoot(), presetId);
    request.sections = manifest.sections;
    request.target = snapshot;
    request.replaceConfirmed = false;
    request.gameRunning = m_host.isRunning ? m_host.isRunning(instanceId) : false;

    const Presets::ApplyPlan plan = Presets::planPresetApplication(request);

    view.ok = plan.ok;
    view.problem = plan.problem;
    view.blockedReason = plan.blockedReason;
    view.needsConfirm = plan.needsConfirm();
    view.replaceConfirmed = false;
    view.canApply = plan.canApply() && plan.writesAnything();
    for (const Presets::ApplyPlanRow &row : plan.rows) {
        PresetManagePage::PlanLine line;
        line.text = rowText(row);
        line.risky = row.risky();
        view.rows.append(line);
    }
    for (const Presets::MalilibConflict &conflict : plan.keyConflicts) {
        view.keyConflicts.append(
            QStringLiteral("%1 ← %2").arg(conflict.keys, conflict.claims.join(QStringLiteral(" / "))));
    }
    if (plan.ok) {
        QString summary = QStringLiteral("%1 段，会动 %2 项").arg(plan.rows.size()).arg(plan.totalCount());
        if (plan.blockedRowCount() > 0) {
            summary += QStringLiteral("，%1 段做不了").arg(plan.blockedRowCount());
        }
        if (plan.riskyRowCount() > 0) {
            summary += QStringLiteral("，%1 段要留意").arg(plan.riskyRowCount());
        }
        view.summary = summary;
    }
    m_page->showPlan(view);
}

QString PresetController::applyRowText(const Presets::PresetApplyRow &row) const {
    QString head = row.title;
    if (head.isEmpty()) {
        head = kindTextOf(row.kind);
    }
    QString state;
    if (row.blocked) {
        state = QStringLiteral("做不了%1").arg(row.problem.isEmpty()
                                                  ? QString()
                                                  : QStringLiteral("（%1）").arg(row.problem));
    } else if (row.skipped) {
        state = QStringLiteral("跳过");
    } else if (!row.ok) {
        state = QStringLiteral("没做成%1").arg(row.problem.isEmpty()
                                                   ? QString()
                                                   : QStringLiteral("（%1）").arg(row.problem));
    } else if (row.written == 0 && row.removed == 0) {
        state = QStringLiteral("没有变化");
    } else {
        QStringList bits;
        if (row.written > 0) {
            bits.append(QStringLiteral("写入 %1").arg(row.written));
        }
        if (row.added > 0) {
            bits.append(QStringLiteral("新增 %1").arg(row.added));
        }
        if (row.removed > 0) {
            bits.append(QStringLiteral("删掉 %1").arg(row.removed));
        }
        if (row.skippedItems > 0) {
            bits.append(QStringLiteral("跳过 %1").arg(row.skippedItems));
        }
        state = bits.join(QStringLiteral("，"));
    }
    return QStringLiteral("%1  %2").arg(head, state);
}

void PresetController::applyPlan(const QString &presetId, const QString &instanceId,
                                 bool replaceConfirmed) {
    if (m_page == nullptr) {
        return;
    }
    PresetManagePage::ApplyView view;

    bool ok = false;
    QString reason;
    const Presets::PresetManifest manifest =
        Presets::loadManifest(presetsRoot(), presetId, &ok, &reason);
    if (!ok) {
        view.problem = QStringLiteral("这一份读不出来：%1").arg(reason);
        m_page->showApplyResult(view);
        return;
    }

    Presets::PresetApplyRequest request;
    request.instanceId = instanceId;
    request.instanceRoot = instanceRoot(instanceId);
    request.gameRunning = m_host.isRunning ? m_host.isRunning(instanceId) : false;
    request.replaceConfirmed = replaceConfirmed;

    const Presets::PresetApplyResult result =
        Presets::applyPresetToInstance(presetsRoot(), manifest, request);

    view.ok = result.ok;
    view.needsConfirm = result.needsConfirm;
    view.problem = !result.blockedReason.isEmpty() ? result.blockedReason : result.problem;
    view.backupDir = result.backupDir;
    for (const Presets::PresetApplyRow &row : result.rows) {
        view.lines.append(applyRowText(row));
    }
    for (const Presets::MalilibConflict &conflict : result.keyConflicts) {
        view.lines.append(QStringLiteral("⚠ 键位冲突：%1 ← %2")
                              .arg(conflict.keys,
                                   conflict.claims.join(QStringLiteral(" / "))));
    }
    m_page->showApplyResult(view);

    if (result.needsConfirm) {
        return;  // 盘上一个字节都没动，日志里也不写"应用了"
    }
    if (!result.blockedReason.isEmpty()) {
        log(QStringLiteral("预设：%1 现在不能应用（%2）。")
                .arg(displayName(instanceId), result.blockedReason));
        return;
    }
    if (!result.ok) {
        log(QStringLiteral("预设：应用到 %1 没有完成%2。")
                .arg(displayName(instanceId),
                     result.problem.isEmpty() ? QString()
                                               : QStringLiteral("：%1").arg(result.problem)));
        return;
    }
    QString line = QStringLiteral("预设「%1」：应用到 %2，写入 %3 项")
                       .arg(manifest.name, displayName(instanceId))
                       .arg(result.written);
    if (result.removed > 0) {
        line += QStringLiteral("、删掉 %1 项").arg(result.removed);
    }
    if (!result.backupDir.isEmpty()) {
        line += QStringLiteral("；备份在 %1").arg(QDir::toNativeSeparators(result.backupDir));
    }
    log(line);
}

// ---------------------------------------------------------------------------
// 给安装流程的只读入口
// ---------------------------------------------------------------------------

QList<Presets::PresetSummary> PresetController::availablePresets() const {
    return Presets::list(presetsRoot());
}

QString PresetController::defaultPresetId() const {
    return Presets::defaultPresetId(presetsRoot());
}

void PresetController::fillInstallSelector(QComboBox *combo, const QString &preferredId,
                                           bool preferredKnown) const {
    if (combo == nullptr) {
        return;
    }
    // 这一趟是**程序在灌数据**：不挡信号的话，每加一项都会触发一发 currentIndexChanged，
    // 那一发会把"用户自己选的"这个状态点着 —— 然后下一次进来就不再跟着默认项走了。
    const QSignalBlocker blocker(combo);
    combo->clear();
    // 第一项是"什么都不应用"。它也是 `Qt::UserRole` 为空串的那一项，调用方按 data 判。
    combo->addItem(installPresetNoneText(), QString());

    const QList<Presets::PresetSummary> presets = availablePresets();
    const QString defaultId = defaultPresetId();
    for (const Presets::PresetSummary &one : presets) {
        const QString label = one.id == defaultId
                                  ? QStringLiteral("%1（默认）").arg(one.name)
                                  : one.name;
        combo->addItem(label, one.id);
    }

    // 选哪一项：**用户选过就听用户的**（哪怕他选的是"不使用预设"，那也是 selection）；
    // 没选过才轮到默认项（§7「不因后台默认项变化而偷偷切换」）。
    int index = -1;
    if (preferredKnown) {
        if (preferredId.isEmpty()) {
            index = 0;
        } else {
            index = combo->findData(preferredId);
        }
    }
    if (index < 0 && !defaultId.isEmpty()) {
        index = combo->findData(defaultId);
    }
    if (index < 0) {
        index = 0;
    }
    combo->setCurrentIndex(index);
}

PresetController::InstallPreview PresetController::previewForInstall(
    const QString &presetId, const QString &instanceName) const {
    InstallPreview view;
    if (!Presets::isValidPresetId(presetId)) {
        view.problem = QStringLiteral("没有选中有效的预设。");
        return view;
    }

    bool ok = false;
    QString reason;
    const Presets::PresetManifest manifest =
        Presets::loadManifest(presetsRoot(), presetId, &ok, &reason);
    if (!ok) {
        view.problem = QStringLiteral("这一份预设读不出来：%1").arg(reason);
        return view;
    }
    view.presetName = manifest.name;

    QString snapshotProblem;
    const Presets::TargetSnapshot snapshot =
        Presets::readTargetSnapshot(instanceRoot(instanceName), &snapshotProblem);
    if (!snapshotProblem.isEmpty()) {
        view.problem = snapshotProblem;
        return view;
    }

    Presets::PlanRequest request;
    request.presetFilesRoot = Presets::filesDir(presetsRoot(), presetId);
    request.sections = manifest.sections;
    request.target = snapshot;
    // ★ 第一趟一律 `false`：这里就是"给用户看的第一趟"，有「覆盖」就如实报出来。
    request.replaceConfirmed = false;
    request.gameRunning = m_host.isRunning ? m_host.isRunning(instanceName) : false;

    const Presets::ApplyPlan plan = Presets::planPresetApplication(request);
    view.ok = plan.ok;
    view.problem = plan.problem;
    view.blockedReason = plan.blockedReason;
    view.needsConfirm = plan.needsConfirm();
    for (const Presets::ApplyPlanRow &row : plan.rows) {
        view.lines.append(rowText(row));
    }
    for (const Presets::MalilibConflict &conflict : plan.keyConflicts) {
        view.lines.append(QStringLiteral("⚠ 键位冲突：%1 ← %2")
                              .arg(conflict.keys,
                                   conflict.claims.join(QStringLiteral(" / "))));
    }
    // 这一段和 `previewPlan()` 里那句小结**同口径**：界面里两处说"会动几项"不该有两种说法。
    if (plan.ok) {
        QString summary = QStringLiteral("%1 段，会动 %2 项")
                              .arg(plan.rows.size())
                              .arg(plan.totalCount());
        if (plan.blockedRowCount() > 0) {
            summary += QStringLiteral("，%1 段做不了").arg(plan.blockedRowCount());
        }
        if (plan.riskyRowCount() > 0) {
            summary += QStringLiteral("，%1 段要留意").arg(plan.riskyRowCount());
        }
        view.summary = summary;
    }
    return view;
}

PresetController::InstallApply PresetController::applyAfterInstall(const QString &presetId,
                                                                   const QString &instanceName,
                                                                   bool replaceConfirmed) {
    InstallApply out;
    bool ok = false;
    QString reason;
    const Presets::PresetManifest manifest =
        Presets::loadManifest(presetsRoot(), presetId, &ok, &reason);
    if (!ok) {
        out.message = QStringLiteral("预设读不出来：%1").arg(reason);
        log(QStringLiteral("预设：%1").arg(out.message));
        return out;
    }

    Presets::PresetApplyRequest request;
    request.instanceId = instanceName;
    request.instanceRoot = instanceRoot(instanceName);
    request.gameRunning = m_host.isRunning ? m_host.isRunning(instanceName) : false;
    request.replaceConfirmed = replaceConfirmed;

    const Presets::PresetApplyResult result =
        Presets::applyPresetToInstance(presetsRoot(), manifest, request);
    out.needsConfirm = result.needsConfirm;
    out.backupDir = result.backupDir;
    out.written = result.written;
    out.removed = result.removed;

    int failed = 0;
    for (const Presets::PresetApplyRow &row : result.rows) {
        if (!row.ok && !row.skipped && !row.blocked) {
            ++failed;
        }
        if (row.blocked) {
            ++failed;
        }
    }

    if (result.needsConfirm) {
        out.message = QStringLiteral("预设里有一段是「覆盖」，需要先确认计划才能应用（"
                                     "可以在设置 →「实例预设…」里看计划并确认）。");
    } else if (!result.blockedReason.isEmpty()) {
        out.message = result.blockedReason;
    } else if (!result.ok) {
        out.message = result.problem.isEmpty()
                          ? QStringLiteral("这一趟没有走完。")
                          : QStringLiteral("这一趟没有走完：%1").arg(result.problem);
    } else {
        out.ok = failed == 0;
        out.message = QStringLiteral("实例预设「%1」已应用：写入 %2 项")
                          .arg(manifest.name)
                          .arg(result.written);
        if (result.removed > 0) {
            out.message += QStringLiteral("、删掉 %1 项").arg(result.removed);
        }
        if (failed > 0) {
            out.message += QStringLiteral("，%1 段没做成（管理页里可以看详情）").arg(failed);
        }
        if (!result.backupDir.isEmpty()) {
            out.message += QStringLiteral("；备份在 %1")
                               .arg(QDir::toNativeSeparators(result.backupDir));
        }
    }

    if (out.ok) {
        log(QStringLiteral("预设：%1").arg(out.message));
    } else {
        log(QStringLiteral("预设：「%1」没有应用到 %2：%3")
                .arg(manifest.name, displayName(instanceName), out.message));
    }
    return out;
}

Presets::PresetApplyResult PresetController::planOnly(
    const Presets::PresetManifest &manifest, const Presets::PresetApplyRequest &request) const {
    Presets::PresetApplyResult out;

    QString snapshotProblem;
    const Presets::TargetSnapshot snapshot =
        Presets::readTargetSnapshot(request.instanceRoot, &snapshotProblem);
    if (!snapshotProblem.isEmpty()) {
        out.problem = snapshotProblem;
        return out;
    }

    Presets::PlanRequest planRequest;
    planRequest.presetFilesRoot = Presets::filesDir(presetsRoot(), manifest.id);
    planRequest.sections = manifest.sections;
    planRequest.target = snapshot;
    planRequest.replaceConfirmed = request.replaceConfirmed;
    planRequest.gameRunning = request.gameRunning;

    const Presets::ApplyPlan plan = Presets::planPresetApplication(planRequest);
    out.ok = plan.ok;
    out.problem = plan.problem;
    out.blockedReason = plan.blockedReason;
    out.needsConfirm = plan.needsConfirm();
    out.keyConflicts = plan.keyConflicts;
    for (const Presets::ApplyPlanRow &row : plan.rows) {
        Presets::PresetApplyRow one;
        one.sectionId = row.sectionId;
        one.kind = row.kind;
        one.mode = row.mode;
        one.title = row.title;
        one.blocked = row.blocked;
        one.skipped = row.mode == Presets::ApplyMode::Skip;
        one.ok = !row.blocked;
        one.written = row.count;
        one.removed = row.removed;
        one.skippedItems = row.skipped;
        one.problem = row.problem;
        out.rows.append(one);
        out.written += row.count;
        out.removed += row.removed;
    }
    return out;
}
