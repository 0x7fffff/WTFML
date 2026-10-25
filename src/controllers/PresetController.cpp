/**
 * @file PresetController.cpp
 * @brief 实现 PresetController.hpp。
 *
 * 这一份里只有三类句子：**读盘**（`Presets::loadManifest` / `readTargetSnapshot`）、
 * **把结果翻成页面认识的形状**、以及**把页面发上来的那一下交给 `Presets::` 那一层**。
 * 判据一条都不在这儿 —— 段怎么落、什么时候要确认、备份什么，全在 `src/presets/`。
 */

#include "controllers/PresetController.hpp"

#include "PresetManagePage.hpp"
#include "PresetModEditor.hpp"
#include "PresetOptionsEditor.hpp"
#include "presets/PresetModInstaller.hpp"
#include <QPushButton>
#include <QCryptographicHash>
#include "manage/VersionManager.hpp"
#include "presets/DataPackPreset.hpp"
#include "presets/GenericFiles.hpp"
#include "presets/MalilibKeys.hpp"
#include "presets/ModrinthCompatibility.hpp"
#include "presets/PresetApply.hpp"
#include "presets/PresetCompatibility.hpp"
#include "presets/PresetPlanner.hpp"
#include "presets/PresetStore.hpp"
#include "presets/PresetCatalog.hpp"
#include "download/ResourceCatalog.hpp"
#include "util/AppPaths.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLayout>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>

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
    selection.dataPacks = choice.dataPacks;
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

/// 筛选项那把钥匙。★ `Unset` 在盘上**没有**类别字段（`presetCategoryKey()` 回空串），
/// 而空串在页面上是「全部」那一档 —— 所以旧数据在筛选这一层另起一个不落盘的钥匙。
QString categoryFilterKey(Presets::PresetCategory category) {
    if (category == Presets::PresetCategory::Unset) {
        return QStringLiteral("legacy");
    }
    return Presets::presetCategoryKey(category);
}

QString sectionKindText(const Presets::PresetSection &section) {
    if (Presets::dataPackHandlesSection(section)) {
        return QStringLiteral("数据包");
    }
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

PresetController::PresetController(VersionManager *versions, QWidget *hostPage, Host host,
                                   QObject *parent)
    : QObject(parent), m_versions(versions), m_hostPage(hostPage), m_host(std::move(host)) {
    m_catalogLoader = new Presets::CatalogLoader(this, m_host.readCatalog);
    // 兼容检查那一套：真网络提供方 + 共享缓存 + 检查器。★ 缓存与 checker 都活到这一层
    // 结束，所以"同一批模组在两个弹窗里连着看两次"第二趟是缓存结论（「共享缓存」）。
    //
    // 根地址问 VersionManager（它认 `Launcher/ModrinthApiBase`）—— 原来这里写死了一份
    // 官方地址，夹具换不掉它：将来任何用 PresetController 的兼容检查测试都会静默打到真
    // Modrinth，而断言是全绿的。
    const QString modrinthRoot =
        m_versions != nullptr ? m_versions->modrinthApiRoot()
                              : ResourceCatalog::modrinthApiRoot();
    m_modProvider = new Presets::PlatformCompatibilityProvider(QUrl(modrinthRoot), this);
    m_modChecker = new Presets::CompatibilityChecker(&m_modCache, this);
    m_modChecker->setProvider(m_modProvider);
    const auto push = [this](const QList<Presets::CompatibilityResult> &results,
                             const Presets::CompatibilitySummary &summary, bool finished) {
        ModCheckUpdate update;
        update.presetId = m_modCheckPresetId;
        update.generation = m_modChecker->generation();
        update.results = results;
        update.summary = summary;
        update.finished = finished;
        emit modCheckUpdated(update);
    };
    connect(m_modChecker, &Presets::CompatibilityChecker::updated, this,
            [push](const QList<Presets::CompatibilityResult> &results,
                   const Presets::CompatibilitySummary &summary) { push(results, summary, false); });
    connect(m_modChecker, &Presets::CompatibilityChecker::finished, this,
            [push](const QList<Presets::CompatibilityResult> &results,
                   const Presets::CompatibilitySummary &summary) { push(results, summary, true); });
}

PresetController::~PresetController() = default;

QString PresetController::presetsRoot() {
    return Presets::presetsRoot();
}

// ---------------------------------------------------------------------------
// 打开
// ---------------------------------------------------------------------------

void PresetController::open() {
    if (m_hostPage == nullptr) {
        return;
    }
    buildPage();
    pushInstances();
    m_page->setBusy(true);
    m_page->showStatus(tr("正在读取预设…"));
    m_catalogLoader->load(presetsRoot(), [this](const Presets::CatalogSnapshot &snapshot) {
        if (!m_page) return;
        if (snapshot.root != presetsRoot()) {
            open();
            return;
        }
        m_page->setBusy(false);
        m_page->showStatus(QString());
        pushCatalog(snapshot, m_currentId);
    });
}

bool PresetController::catalogLoading() const {
    return m_catalogLoader->isLoading();
}

void PresetController::buildPage() {
    if (m_page != nullptr) {
        return;
    }
    m_page = new PresetManagePage(m_hostPage);
    // 页面是实例页第三档的常驻内容（不是弹出来的覆盖页）：塞进宿主里，跟着换页容器一起露/收。
    auto *hostLayout = qobject_cast<QVBoxLayout *>(m_hostPage->layout());
    if (hostLayout != nullptr) {
        hostLayout->addWidget(m_page);
    } else {
        auto *layout = new QVBoxLayout(m_hostPage);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(m_page);
    }

    pushCategories();

    connect(m_page, &PresetManagePage::presetSelected, this, &PresetController::selectPreset);
    connect(m_page, &PresetManagePage::createRequested, this,
            &PresetController::createPreset);
    connect(m_page, &PresetManagePage::duplicateRequested, this,
            &PresetController::duplicatePreset);
    connect(m_page, &PresetManagePage::detailsEdited, this,
            [this](const QString &id, const QString &name, const QString &description) {
                saveDetails(id, name, description);
            });
    connect(m_page, &PresetManagePage::deleteRequested, this, &PresetController::removePreset);
    connect(m_page, &PresetManagePage::editOptionsRequested, this, [this](const QString &id) {
        bool ok = false;
        auto manifest = Presets::loadManifest(presetsRoot(), id, &ok);
        if (!ok || manifest.category != Presets::PresetCategory::GameSettings) return;
        QList<PresetOptionsEditor::Entry> entries;
        QSet<QString> seen;
        QMap<int, Presets::PresetSection> sections;
        QMap<QString, QJsonObject> original;
        for (const auto &section : manifest.sections) {
            if (section.kind != Presets::SectionKind::VanillaOptions && section.kind != Presets::SectionKind::VanillaKeys) continue;
            if (sections.contains(int(section.kind))) {
                m_page->showStatus(tr("这份预设有多段同类设置，请先整理为单段，避免改变覆盖顺序。")); return;
            }
            sections.insert(int(section.kind), section);
            for (const auto &value : section.items) {
                const auto item = value.toObject(); const QString key = item.value("key").toString();
                if (key.isEmpty() || !item.value("value").isString() || seen.contains(key)) {
                    m_page->showStatus(tr("设置段含无效或重复键，未自动改写；请先修复或重新提取。")); return;
                }
                seen.insert(key); original.insert(key, item);
                entries.append({key, item.value("value").toString()});
            }
        }
        const QByteArray before = Presets::manifestToBytes(manifest);
        PresetOptionsEditor editor(entries, m_page);
        if (editor.exec() != QDialog::Accepted) return;
        auto current = Presets::loadManifest(presetsRoot(), id, &ok);
        if (!ok || Presets::manifestToBytes(current) != before) {
            m_page->showStatus(tr("预设在编辑期间发生变化，未覆盖，请重新打开编辑。")); return;
        }
        for (auto it = sections.begin(); it != sections.end(); ++it) it->items = {};
        for (const auto &entry : editor.entries()) {
            const auto kind = entry.key.startsWith(QStringLiteral("key_")) ? Presets::SectionKind::VanillaKeys : Presets::SectionKind::VanillaOptions;
            auto &section = sections[int(kind)]; section.kind = kind;
            if (section.id.isEmpty()) section.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            auto item = original.value(entry.key);
            item.insert("key", entry.key); item.insert("value", entry.value);
            section.items.append(item);
        }
        QList<Presets::PresetSection> output;
        for (const auto &section : current.sections)
            if (section.kind != Presets::SectionKind::VanillaOptions && section.kind != Presets::SectionKind::VanillaKeys) output.append(section);
        for (const auto &section : sections) output.append(section);
        current.sections = output;
        current.updatedAt = QDateTime::currentDateTime();
        if (!Presets::saveManifest(presetsRoot(), current)) { m_page->showStatus(tr("设置保存失败，原预设保留。")); return; }
        refreshList(id);
        m_page->showStatus(tr("游戏设置与按键已保存，其它配置段保持不变。"));
    });
    connect(m_page, &PresetManagePage::editModsRequested, this, [this](const QString &id) {
        bool ok = false;
        auto manifest = Presets::loadManifest(presetsRoot(), id, &ok);
        if (!ok || manifest.category != Presets::PresetCategory::Mods) return;
        QList<PresetModEditor::Entry> entries;
        for (const auto &ref : manifest.modReferences)
            entries.append({ref.platform, ref.projectId, ref.name, ref.gameVersion, ref.loader, ref.required});
        PresetModEditor editor(entries, m_page);
        if (editor.exec() != QDialog::Accepted) return;
        // Re-read to preserve unrelated metadata changed while the editor was open.
        manifest = Presets::loadManifest(presetsRoot(), id, &ok);
        if (!ok || manifest.category != Presets::PresetCategory::Mods) return;
        manifest.modReferences.clear();
        for (const auto &entry : editor.entries()) {
            Presets::ModReference ref;
            ref.platform = entry.platform; ref.projectId = entry.projectId; ref.name = entry.name;
            ref.gameVersion = entry.gameVersion; ref.loader = entry.loader; ref.required = entry.required;
            manifest.modReferences.append(ref);
        }
        manifest.updatedAt = QDateTime::currentDateTime();
        if (!Presets::saveManifest(presetsRoot(), manifest)) { m_page->showStatus(tr("模组清单保存失败，原内容保留。")); return; }
        cancelModCheck();
        refreshList(id);
        m_page->showStatus(tr("模组清单已保存；安装时按目标版本检查并下载。"));
    });
    connect(m_page, &PresetManagePage::defaultRequested, this,
            &PresetController::setDefaultPreset);
    connect(m_page, &PresetManagePage::moveRequested, this, &PresetController::movePreset);
    connect(m_page, &PresetManagePage::captureRequested, this,
            [this](const QString &presetId, const QString &instanceId,
                   const PresetManagePage::CaptureChoice &choice) {
                captureInto(presetId, instanceId, choice);
            });
    connect(m_page, &PresetManagePage::captureAsNewRequested, this,
            [this](const QString &instanceId, const QString &name, const QString &categoryKey,
                   const PresetManagePage::CaptureChoice &choice) {
                captureAsNew(instanceId, name, categoryKey, choice);
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

void PresetController::refreshInstances() {
    pushInstances();
}

QString PresetController::categoryText(Presets::PresetCategory category) {
    switch (category) {
    case Presets::PresetCategory::Unset:
        return QStringLiteral("旧版（未分类）");
    case Presets::PresetCategory::GameSettings:
        return QStringLiteral("游戏设置");
    case Presets::PresetCategory::Mods:
        return QStringLiteral("模组");
    case Presets::PresetCategory::ResourcePacks:
        return QStringLiteral("资源包");
    case Presets::PresetCategory::DataPacks:
        return QStringLiteral("数据包");
    }
    return QStringLiteral("旧版（未分类）");
}

void PresetController::pushCategories() {
    if (m_page == nullptr) {
        return;
    }
    QList<PresetManagePage::CategoryOption> options;

    PresetManagePage::CategoryOption all;
    all.text = QStringLiteral("全部");
    all.filterOnly = true;
    options.append(all);

    const Presets::PresetCategory creatable[] = {
        Presets::PresetCategory::GameSettings,
        Presets::PresetCategory::Mods,
        Presets::PresetCategory::ResourcePacks,
        Presets::PresetCategory::DataPacks,
    };
    for (const Presets::PresetCategory category : creatable) {
        PresetManagePage::CategoryOption option;
        option.key = categoryFilterKey(category);
        option.text = categoryText(category);
        options.append(option);
    }

    // 旧版混合预设（没写类别）：能看、能用、能复制，但不能**新做**出这一类。
    PresetManagePage::CategoryOption legacy;
    legacy.key = categoryFilterKey(Presets::PresetCategory::Unset);
    legacy.text = categoryText(Presets::PresetCategory::Unset);
    legacy.filterOnly = true;
    options.append(legacy);

    m_page->setCategories(options);
}

quint32 PresetController::captureMaskFor(Presets::PresetCategory category) {
    using Bits = PresetManagePage;
    switch (category) {
    case Presets::PresetCategory::Unset:
        return PresetManagePage::captureMaskAll();
    case Presets::PresetCategory::GameSettings:
        return Bits::CaptureVanillaOptions | Bits::CaptureVanillaKeys | Bits::CaptureServers |
               Bits::CaptureConfig | Bits::CaptureDefaultConfigs | Bits::CaptureMalilib |
               Bits::CaptureMalilibValues;
    case Presets::PresetCategory::ResourcePacks:
        return Bits::CaptureResourcePacks | Bits::CaptureShaderPacks;
    case Presets::PresetCategory::DataPacks:
        // 数据包只收世界里的那一坨；模组类仍然没有可提取的段。
        return Bits::CaptureDataPacks;
    case Presets::PresetCategory::Mods:
        return 0;
    }
    return PresetManagePage::captureMaskAll();
}

QString PresetController::captureNoteFor(Presets::PresetCategory category) {
    switch (category) {
    case Presets::PresetCategory::Unset:
        return QStringLiteral("旧版预设没写类别，九样都能收。");
    case Presets::PresetCategory::GameSettings:
        return QStringLiteral("「游戏设置」预设不收资源包 / 光影包。");
    case Presets::PresetCategory::ResourcePacks:
        return QStringLiteral("「资源包」预设只收资源包 / 光影包这一类文件。");
    case Presets::PresetCategory::Mods:
        return QStringLiteral("模组预设请使用“编辑模组清单”添加平台项目，当前不从实例提取本地 JAR。");
    case Presets::PresetCategory::DataPacks:
        return QStringLiteral("这一类只收各个存档的 datapacks/；目标上没有同名存档时先记成「待应用」。");
    }
    return QString();
}

Presets::CaptureSelection PresetController::filterCapture(
    Presets::PresetCategory category, const Presets::CaptureSelection &selection,
    QStringList *skipped) {
    const quint32 mask = captureMaskFor(category);
    Presets::CaptureSelection out = selection;
    auto drop = [mask, skipped](quint32 bit, bool &flag, const char *label) {
        if ((mask & bit) == 0 && flag) {
            flag = false;
            if (skipped != nullptr) {
                skipped->append(QString::fromUtf8(label));
            }
        }
    };
    using Bits = PresetManagePage;
    drop(Bits::CaptureVanillaOptions, out.vanillaOptions, "基础设置");
    drop(Bits::CaptureVanillaKeys, out.vanillaKeys, "标准按键");
    drop(Bits::CaptureServers, out.servers, "服务器列表");
    drop(Bits::CaptureConfig, out.config, "config/");
    drop(Bits::CaptureDefaultConfigs, out.defaultConfigs, "defaultconfigs/");
    drop(Bits::CaptureResourcePacks, out.resourcePacks, "资源包");
    drop(Bits::CaptureShaderPacks, out.shaderPacks, "光影包");
    drop(Bits::CaptureMalilib, out.malilib, "MaLiLib 热键");
    drop(Bits::CaptureMalilibValues, out.malilibValues, "MaLiLib 值");
    drop(Bits::CaptureDataPacks, out.dataPacks, "数据包");
    return out;
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
    const bool wasLoading = m_catalogLoader->isLoading();
    m_catalogLoader->invalidate();
    if (wasLoading) m_page->setBusy(false);
    pushCatalog(Presets::readCatalog(presetsRoot()), selectId);
}

void PresetController::pushCatalog(const Presets::CatalogSnapshot &snapshot, const QString &selectId) {
    const auto &summaries = snapshot.presets;

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
        item.categoryKey = categoryFilterKey(summary.category);
        item.categoryText = categoryText(summary.category);
        items.append(item);
    }

    QList<PresetManagePage::BrokenItem> brokenItems;
    for (const Presets::BrokenPreset &one : snapshot.broken) {
        PresetManagePage::BrokenItem item;
        item.id = one.id;
        item.reason = one.reason;
        brokenItems.append(item);
    }

    QString keep = selectId;
    if (keep.isEmpty()) {
        for (const auto &item : items) {
            if (item.isDefault) { keep = item.id; break; }
        }
    }
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
    // Search/category filtering may hide the requested id. Actions and details
    // must follow the actual visible selection, not an invisible fallback.
    chosen = m_page->currentPresetId();
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
    m_page->showPreset(id, manifest.name, manifest.description, categoryText(manifest.category),
                       sectionItems(manifest));
    if (auto *edit = m_page->findChild<QPushButton *>(QStringLiteral("presetEditModsButton")))
        edit->setVisible(manifest.category == Presets::PresetCategory::Mods);
    if (auto *edit = m_page->findChild<QPushButton *>(QStringLiteral("presetEditOptionsButton")))
        edit->setVisible(manifest.category == Presets::PresetCategory::GameSettings);
    m_page->setCaptureAvailability(captureMaskFor(manifest.category),
                                   captureNoteFor(manifest.category));
}

QList<PresetManagePage::SectionItem> PresetController::sectionItems(
    const Presets::PresetManifest &manifest) const {
    QList<PresetManagePage::SectionItem> items;
    if (!manifest.modReferences.isEmpty()) {
        PresetManagePage::SectionItem item;
        item.title = tr("平台模组清单"); item.kindText = tr("模组");
        item.modeText = tr("按目标版本解析"); item.itemCount = manifest.modReferences.size();
        items.append(item);
    }
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

void PresetController::createPreset(const QString &categoryKey) {
    bool categoryOk = true;
    Presets::PresetCategory category = Presets::presetCategoryFromKey(categoryKey, &categoryOk);
    if (categoryKey.isEmpty()) {
        // 页面上第一项就是它；空串只可能来自"没摆下拉框"的测试。
        category = Presets::PresetCategory::GameSettings;
        categoryOk = true;
    }
    if (!categoryOk || category == Presets::PresetCategory::Unset) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("新建时得先选四类里的一类（跨类的总预设不做）。"));
        }
        return;
    }
    const Presets::PresetManifest created =
        Presets::create(presetsRoot(), newPresetName(), QString(), category);
    if (created.id.isEmpty()) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("没能建出新的一份（预设目录写不进去）。"));
        }
        return;
    }
    log(QStringLiteral("预设：新建了「%1」（%2）。")
            .arg(created.name, categoryText(created.category)));
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
    if (report.dataPackItems > 0) {
        parts.append(QStringLiteral("数据包 %1 个（%2 个存档）")
                         .arg(report.dataPackItems)
                         .arg(report.dataPackWorlds));
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
    // 「不串类别」的第二道闸（第一道在界面上把格子禁掉）：外面要是还是塞进来了
    // 不属于这一类的样，这里静默摘掉并说清楚，绝不让它落进 manifest。
    QStringList skipped;
    request.selection = filterCapture(manifest.category, toSelection(choice), &skipped);
    if (request.selection.isEmpty()) {
        if (m_page != nullptr) {
            m_page->showStatus(
                skipped.isEmpty()
                    ? QStringLiteral("什么都没勾。")
                    : QStringLiteral("这一类预设不收这几样：%1。")
                          .arg(skipped.join(QStringLiteral("、"))));
        }
        return;
    }

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
        QString status = captureSummary(report);
        if (!skipped.isEmpty()) {
            status += QStringLiteral("\n这一类预设不收：%1。")
                          .arg(skipped.join(QStringLiteral("、")));
        }
        m_page->showStatus(status);
    }
}

void PresetController::captureAsNew(const QString &instanceId, const QString &name,
                                    const QString &categoryKey,
                                    const PresetManagePage::CaptureChoice &choice) {
    bool categoryOk = true;
    Presets::PresetCategory category = Presets::presetCategoryFromKey(categoryKey, &categoryOk);
    if (categoryKey.isEmpty()) {
        category = Presets::PresetCategory::GameSettings;
        categoryOk = true;
    }
    if (!categoryOk || category == Presets::PresetCategory::Unset) {
        if (m_page != nullptr) {
            m_page->showStatus(QStringLiteral("新建时得先选四类里的一类（跨类的总预设不做）。"));
        }
        return;
    }

    QString wanted = Presets::cleanDisplayName(name);
    if (wanted.isEmpty()) {
        wanted = QStringLiteral("%1 预设").arg(displayName(instanceId));
    }
    const QString root = presetsRoot();
    Presets::PresetManifest created = Presets::create(root, wanted, QString(), category);
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
    QStringList skipped;
    request.selection = filterCapture(category, toSelection(choice), &skipped);
    if (request.selection.isEmpty()) {
        // 这一类什么都收不了（模组 / 数据包首版就是这样）：空壳比"报成功"诚实。
        Presets::remove(root, created.id);
        if (m_page != nullptr) {
            m_page->showStatus(
                skipped.isEmpty()
                    ? QStringLiteral("什么都没勾，新预设没有建。")
                    : QStringLiteral("这一类预设不收这几样：%1；新预设没有建。")
                          .arg(skipped.join(QStringLiteral("、"))));
        }
        return;
    }

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
        QString status = captureSummary(report);
        if (!skipped.isEmpty()) {
            status += QStringLiteral("\n这一类预设不收：%1。")
                          .arg(skipped.join(QStringLiteral("、")));
        }
        m_page->showStatus(status);
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
    Presets::TargetSnapshot snapshot =
        Presets::readTargetSnapshot(instanceRoot(instanceId), &snapshotProblem);
    if (!snapshotProblem.isEmpty()) {
        view.problem = snapshotProblem;
        m_page->showPlan(view);
        return;
    }
    // 数据包从 17w43a 才有：老版本实例上那一段该报"做不了"，不是记成待应用。
    snapshot.supportsDataPacks =
        m_versions != nullptr && m_versions->instanceSupportsDatapacks(instanceId);

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
    if (manifest.category == Presets::PresetCategory::Mods && !manifest.modReferences.isEmpty() &&
        !request.gameRunning && plan.ok) {
        view.canApply = true;
        view.rows.append({tr("平台模组 %1 项；应用时联网检查发行版本、依赖和冲突，必要项无法确认则不安装。")
                          .arg(manifest.modReferences.size()), false});
    }
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
    if (!manifest.modReferences.isEmpty())
        view.summary += tr("；平台模组 %1 项（发行文件及前置待执行时确认）").arg(manifest.modReferences.size());
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
    } else if (row.written == 0 && row.removed == 0 && row.deferred == 0) {
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
        if (row.deferred > 0) {
            bits.append(QStringLiteral("待应用 %1").arg(row.deferred));
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

    if (m_host.isInstanceBusy && m_host.isInstanceBusy(instanceId)) {
        view.problem = tr("这个实例正在安装、补全文件或后台搬文件（复制、还原、导入），"
                          "等它收尾之后再应用预设。");
        m_page->showApplyResult(view);
        return;
    }

    bool ok = false;
    QString reason;
    const Presets::PresetManifest manifest =
        Presets::loadManifest(presetsRoot(), presetId, &ok, &reason);
    if (!ok) {
        view.problem = QStringLiteral("这一份读不出来：%1").arg(reason);
        m_page->showApplyResult(view);
        return;
    }
    if (manifest.category == Presets::PresetCategory::Mods && !manifest.modReferences.isEmpty()) {
        m_page->showStatus(tr("正在检查并安装模组预设，必要前置将一并解析…"));
        installModsAfterInstall({presetId}, instanceId, [this](bool success, const QString &message) {
            if (!m_page) return;
            PresetManagePage::ApplyView report;
            report.ok = success;
            report.lines = message.split(QLatin1Char('\n'));
            if (!success) report.problem = tr("模组预设未全部应用，请查看逐项原因。");
            m_page->showApplyResult(report);
        });
        return;
    }

    Presets::PresetApplyRequest request;
    request.instanceId = instanceId;
    request.instanceRoot = instanceRoot(instanceId);
    request.gameRunning = m_host.isRunning ? m_host.isRunning(instanceId) : false;
    request.replaceConfirmed = replaceConfirmed;
    request.supportsDataPacks =
        m_versions != nullptr && m_versions->instanceSupportsDatapacks(instanceId);

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
    // 没选过才轮到默认项（「不因后台默认项变化而偷偷切换」）。
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

QList<PresetController::InstallCategory> PresetController::installCategories() {
    // 次序 = 安装页弹窗那四个页签的次序。★ 改这里的次序就是改界面 —— 页签和数据同一份，
    // 别处不要再写一遍四类的名字（`categoryText()` 是那四句话的唯一出处）。
    const Presets::PresetCategory order[] = {
        Presets::PresetCategory::GameSettings,
        Presets::PresetCategory::Mods,
        Presets::PresetCategory::ResourcePacks,
        Presets::PresetCategory::DataPacks,
    };
    QList<InstallCategory> out;
    for (const Presets::PresetCategory category : order) {
        out.append({Presets::presetCategoryKey(category), categoryText(category)});
    }
    return out;
}

QList<Presets::PresetSummary> PresetController::presetsInCategory(
    const QString &categoryKey) const {
    bool ok = false;
    const Presets::PresetCategory category = Presets::presetCategoryFromKey(categoryKey, &ok);
    if (!ok || category == Presets::PresetCategory::Unset) {
        return {};
    }
    QList<Presets::PresetSummary> out;
    for (const Presets::PresetSummary &one : availablePresets()) {
        if (one.category == category) {
            out.append(one);
        }
    }
    return out;
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
    view.fingerprint = QString::fromLatin1(QCryptographicHash::hash(Presets::manifestToBytes(manifest), QCryptographicHash::Sha256).toHex());
    if (!manifest.modReferences.isEmpty()) {
        view.lines.append(tr("平台模组 %1 项：安装后按目标版本解析发行文件和必要前置；缺失或查询失败将如实报告，不安装其它版本代替。")
                          .arg(manifest.modReferences.size()));
    }

    QString snapshotProblem;
    Presets::TargetSnapshot snapshot =
        Presets::readTargetSnapshot(instanceRoot(instanceName), &snapshotProblem);
    if (!snapshotProblem.isEmpty()) {
        view.problem = snapshotProblem;
        return view;
    }
    snapshot.supportsDataPacks =
        m_versions != nullptr && m_versions->instanceSupportsDatapacks(instanceName);

    Presets::PlanRequest request;
    request.presetFilesRoot = Presets::filesDir(presetsRoot(), presetId);
    request.sections = manifest.sections;
    request.target = snapshot;
    // 第一趟一律 `false`：这里就是"给用户看的第一趟"，有「覆盖」就如实报出来。
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
        const QString line = QStringLiteral("⚠ 键位冲突：%1 ← %2")
                                 .arg(conflict.keys,
                                      conflict.claims.join(QStringLiteral(" / ")));
        view.lines.append(line);
        view.reasons.append(line);
    }
    // 弹窗状态行那几格：动几项 / 跳过几项 / 哪几段做不了。三个数都是
    // `ApplyPlan` 自己算的，这一层只做加法 —— 别在界面上拿 `lines.size()` 之类的近似。
    view.itemCount = plan.totalCount();
    for (const Presets::ApplyPlanRow &row : plan.rows) {
        view.skippedCount += row.skipped;
        if (row.blocked) {
            ++view.blockedCount;
        }
        // 「缺失、未知、跳过」那几类原因：只在**用得上**的时候才给（正常合并的那些段
        // 不进这张单子，否则展开之后是一屏"合并 18 项"的废话）。
        if (row.risky() || row.skipped > 0 || !row.problem.isEmpty()) {
            view.reasons.append(rowText(row));
        }
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

QString PresetController::presetFingerprint(const QString &presetId) const {
    bool ok = false;
    const auto manifest = Presets::loadManifest(presetsRoot(), presetId, &ok);
    return ok ? QString::fromLatin1(QCryptographicHash::hash(Presets::manifestToBytes(manifest), QCryptographicHash::Sha256).toHex()) : QString();
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
    request.supportsDataPacks =
        m_versions != nullptr && m_versions->instanceSupportsDatapacks(instanceName);

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
                                     "可以在实例页 →「预设」那一档里看计划并确认）。");
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
        for (const auto &row : result.rows) {
            if (row.deferred > 0 || !row.problem.isEmpty())
                out.message += QLatin1Char('\n') + applyRowText(row);
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
    Presets::TargetSnapshot snapshot =
        Presets::readTargetSnapshot(request.instanceRoot, &snapshotProblem);
    if (!snapshotProblem.isEmpty()) {
        out.problem = snapshotProblem;
        return out;
    }
    snapshot.supportsDataPacks = request.supportsDataPacks;

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
        one.deferred = row.deferred;
        one.problem = row.problem;
        out.rows.append(one);
        out.written += row.count;
        out.removed += row.removed;
    }
    return out;
}

// ---------------------------------------------------------------------------
// 安装前的兼容检查
// ---------------------------------------------------------------------------

QList<Presets::ModReference> PresetController::modReferencesFor(const QString &presetId) const {
    if (presetId.trimmed().isEmpty()) {
        return {};
    }
    bool ok = false;
    QString reason;
    const Presets::PresetManifest manifest =
        Presets::loadManifest(presetsRoot(), presetId, &ok, &reason);
    if (!ok) {
        return {};
    }
    return manifest.modReferences;
}

void PresetController::startModCheck(const QString &presetId, const QString &gameVersion,
                                     const QString &loader, bool clientSide) {
    if (m_modChecker == nullptr) {
        return;
    }
    // 先把"这一代查的是哪一份"记下来，再 start()：`start()` 会**同步**发一发
    // `updated`（本地能判的那几条立刻有结论），那一发里必须已经带着正确的 presetId。
    m_modCheckPresetId = presetId;
    Presets::CompatibilityContext context;
    context.gameVersion = gameVersion.trimmed();
    context.loader = loader;
    context.clientSide = clientSide;
    m_modChecker->start(modReferencesFor(presetId), context);
}

void PresetController::cancelModCheck() {
    if (m_modChecker == nullptr) {
        return;
    }
    m_modChecker->cancel();
    m_modCheckPresetId.clear();
}

int PresetController::modCheckGeneration() const {
    return m_modChecker == nullptr ? 0 : m_modChecker->generation();
}

void PresetController::setCurseForgeApiKey(const QString &apiKey) {
    m_presetApiKey = apiKey;
    if (m_modProvider == nullptr) {
        return;
    }
    m_modProvider->setCurseForge(QUrl(QStringLiteral("https://api.curseforge.com/v1")),
                                 apiKey.toUtf8());
}

void PresetController::installModsAfterInstall(const QStringList &presetIds, const QString &instanceId,
    std::function<void(bool, const QString &)> completed) {
    if (m_modInstallTask) { completed(false, tr("另一项模组预设仍在应用，请等待或取消。")); return; }
    if (!m_versions || instanceId.isEmpty()) { completed(false, tr("目标实例不可用。")); return; }
    // 同一道闸在真正开跑之前再问一次（和 `applyPlan()` 那道是**同一个判断**）：从
    // 「应用」按下去到这儿之间可能进过嵌套提示，期间另一趟搬文件已经起来了。
    if (m_host.isFileOperationBusy && m_host.isFileOperationBusy(instanceId)) {
        completed(false, tr("这个实例正在后台被其它作业写入（复制、还原或导入），等它收尾之后再应用模组预设。"));
        return;
    }
    QList<Presets::ModReference> references;
    QJsonArray snapshot;
    QSet<QString> seen;
    for (const auto &id : presetIds) {
        bool ok = false;
        const auto manifest = Presets::loadManifest(presetsRoot(), id, &ok);
        if (!ok) { completed(false, tr("预设已删除或损坏，无法继续应用。")); return; }
        snapshot.append(Presets::manifestToJson(manifest));
        for (const auto &ref : manifest.modReferences) {
            if (seen.contains(ref.key())) {
                for (auto &existing : references) if (existing.key() == ref.key()) {
                    if (existing.gameVersion != ref.gameVersion || existing.loader.compare(ref.loader, Qt::CaseInsensitive) != 0) {
                        completed(false, tr("同一模组存在不同版本/加载器约束，未自动选择其中一项。")); return;
                    }
                    existing.required = existing.required || ref.required;
                }
                continue;
            }
            seen.insert(ref.key()); references.append(ref);
        }
    }
    if (references.isEmpty()) { completed(true, {}); return; }
    QString loader, loaderVersion;
    m_versions->instanceLoaderInfo(instanceId, &loader, &loaderVersion);
    auto *task = new Presets::PresetModInstaller(this);
    task->setSelectionSnapshot(snapshot);
    task->setInstanceIdentityKey(instanceId);
    m_modInstallTask = task;
    // 必须在 `start()` **之前**记：`start()` 在"没有版本 / 加载器对不上"那几条路上
    // 会**同步**回调 completed，先记后清才是对的顺序。
    if (m_host.setFileOperationBusy) m_host.setFileOperationBusy(instanceId, true);
    task->start(references, instanceRoot(instanceId), m_versions->instanceGameVersion(instanceId), loader,
                m_presetApiKey, [this, instanceId] { return m_host.isRunning && m_host.isRunning(instanceId); },
                [this, instanceId, completed](const Presets::PresetModInstaller::Result &result) {
                    m_modInstallTask.clear();
                    if (m_host.setFileOperationBusy) m_host.setFileOperationBusy(instanceId, false);
                    if (m_host.refreshInstances) m_host.refreshInstances();
                    const QString text = result.lines.join(QLatin1Char('\n'));
                    log(text);
                    completed(result.ok, text);
                });
}

bool PresetController::cancelModInstall() {
    if (!m_modInstallTask) return false;
    static_cast<Presets::PresetModInstaller *>(m_modInstallTask.data())->cancel();
    return true;
}
