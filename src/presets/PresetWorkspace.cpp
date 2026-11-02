#include "PresetWorkspace.hpp"
#include "util/WriteDirectoryGuard.hpp"
#include "DataPackPreset.hpp"
#include "GenericFiles.hpp"
#include "download/DownloadCheckpoint.hpp"
#include "manage/FolderManager.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
namespace Presets {
QString editorSectionPart(const PresetSection &section) {
    if (section.kind == SectionKind::VanillaOptions || section.kind == SectionKind::VanillaKeys ||
        section.kind == SectionKind::ServerList)
        return "gameSettings";
    if (dataPackHandlesSection(section))
        return "dataPacks";
    if (section.kind == SectionKind::GenericFiles) {
        FileGroup group;
        if (!section.items.isEmpty() &&
            fileGroupOfPath(section.items.first().toObject().value("path").toString(), &group) &&
            (group == FileGroup::ResourcePacks || group == FileGroup::ShaderPacks))
            return "resourcePacks";
    }
    return {};
}
QJsonObject editorDocument(const PresetManifest &manifest) {
    auto state = manifest.editorState;
    if (state.isEmpty()) {
        QJsonArray sections, mods;
        for (const auto &s : manifest.sections)
            sections.append(sectionToJson(s));
        for (const auto &m : manifest.modReferences)
            mods.append(modReferenceToJson(m));
        state["sections"] = sections;
        state["modReferences"] = mods;
    }
    state["id"] = manifest.id;
    state["name"] = manifest.name;
    return state;
}
namespace {
bool copyFile(const QString &source, const QString &target, QString *problem) {
    QFile input(source);
    if (QFileInfo(source).isSymLink() || !input.open(QIODevice::ReadOnly)) {
        *problem = QStringLiteral("无法读取文件：%1").arg(source);
        return false;
    }
    if (!DownloadCheckpoint::safePath(target)) {
        *problem = QStringLiteral("资源目标路径不安全。");
        return false;
    }
    if (QFileInfo::exists(target)) {
        if (QFileInfo(source).canonicalFilePath() == QFileInfo(target).canonicalFilePath())
            return true;
        QFile existing(target);
        QCryptographicHash a(QCryptographicHash::Sha256), b(QCryptographicHash::Sha256);
        if (existing.open(QIODevice::ReadOnly) && a.addData(&input) && b.addData(&existing) &&
            a.result() == b.result())
            return true;
        *problem = QStringLiteral("已有资源文件与待保存内容不一致，原文件保留。");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        *problem = QStringLiteral("无法创建预设文件目录。");
        return false;
    }
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly)) {
        *problem = output.errorString();
        return false;
    }
    while (!input.atEnd()) {
        auto bytes = input.read(1024 * 1024);
        if (bytes.isEmpty() && input.error() != QFileDevice::NoError) {
            *problem = input.errorString();
            return false;
        }
        if (output.write(bytes) != bytes.size()) {
            *problem = output.errorString();
            return false;
        }
    }
    if (!output.commit()) {
        *problem = output.errorString();
        return false;
    }
    return true;
}
} // namespace
EditorSaveResult saveEditorDocument(const QString &root, const QJsonObject &input, bool draft) {
    EditorSaveResult result;
    auto state = input;
    const auto id = state.value("id").toString();
    if (!isValidPresetId(id)) {
        result.problem = QStringLiteral("预设标识无效。");
        return result;
    }
    QString name = cleanDisplayName(state.value("name").toString());
    if (name.isEmpty()) {
        if (!draft) {
            result.problem = QStringLiteral("请填写预设名称。");
            return result;
        }
        name = QStringLiteral("未命名预设");
    }
    state["name"] = name;
    PresetManifest manifest;
    manifest.id = id;
    manifest.name = name;
    manifest.createdAt = QDateTime::currentDateTime();
    manifest.updatedAt = manifest.createdAt;
    const auto destinationRoot = draft ? root + "/drafts" : root;
    if (!DownloadCheckpoint::safePath(presetDir(destinationRoot, id))) {
        result.problem = QStringLiteral("预设目录包含不安全的路径。");
        return result;
    }
    if(!WriteDirectoryGuard::acquire(presetDir(destinationRoot,id),&result.problem))return result;
    bool oldOk = false;
    auto old = loadManifest(root, id, &oldOk);
    if (oldOk)
        manifest.createdAt = old.createdAt;
    const auto enabled = state.value("enabledParts").toObject();
    auto on = [&](const QString &part) {
        return !enabled.contains(part) || enabled.value(part).toBool();
    };
    QJsonArray fullSections;
    QSet<QString> activeParts;
    QJsonArray normalized;
    for (const auto &value : state.value("sections").toArray()) {
        auto section = value.toObject();
        const auto kind = section.value("kind").toString();
        if (kind == "genericFiles") {
            QMap<QString, QJsonArray> groups;
            for (const auto &item : section.value("items").toArray())
                groups[item.toObject().value("path").toString().section('/', 0, 0)].append(item);
            for (auto it = groups.begin(); it != groups.end(); ++it) {
                auto copy = section;
                copy["id"] = sectionIdFor(SectionKind::GenericFiles, it.key());
                copy["items"] = it.value();
                normalized.append(copy);
            }
        } else {
            if (kind == "vanillaOptions" || kind == "vanillaKeys")
                section["id"] = sectionIdFor(sectionKindFromKey(kind));
            if (section.value("id") == "dataPacks")
                section["id"] = dataPackSectionId();
            normalized.append(section);
        }
    }
    for (const auto &value : normalized) {
        if (!value.isObject()) {
            result.problem = QStringLiteral("预设内容无效。");
            return result;
        }
        auto json = value.toObject();
        bool valid = false;
        auto section = sectionFromJson(json, &valid);
        if (!valid) {
            result.problem = QStringLiteral("预设条目格式无效。");
            return result;
        }
        const auto part = editorSectionPart(section);
        // Legacy mod configuration is kept in editor storage but never newly applied.
        if (!draft && (section.kind == SectionKind::VanillaOptions ||
                       section.kind == SectionKind::VanillaKeys)) {
            QSet<QString> keys;
            for (const auto &v : section.items) {
                const auto object = v.toObject();
                const auto key = object.value("key").toString(),
                           val = object.value("value").toString();
                if (key.isEmpty() || key.contains(':') || key.contains('\n') ||
                    key.contains('\r') || val.contains('\n') || val.contains('\r') ||
                    keys.contains(key) || !object.value("value").isString() ||
                    ((section.kind == SectionKind::VanillaKeys) != key.startsWith("key_"))) {
                    result.problem = QStringLiteral(
                        "设置键不能为空或重复；按键请放在按键页，数值中不能含换行。");
                    return result;
                }
                keys.insert(key);
            }
        }
        if (!part.isEmpty() && !section.items.isEmpty() && on(part))
            activeParts.insert(part);
        fullSections.append(json);
        if (!part.isEmpty() && on(part))
            manifest.sections.append(section);
    }
    QList<ModReference> mods;
    QSet<QString> identities;
    for (const auto &v : state.value("modReferences").toArray()) {
        auto ref = modReferenceFromJson(v.toObject());
        const auto key = ref.platform + "/" + ref.projectId;
        if (!draft && on("mods") &&
            (ref.platform.isEmpty() || ref.projectId.trimmed().isEmpty() ||
             identities.contains(key))) {
            result.problem = QStringLiteral("请选择模组来源并填写唯一的项目 ID。");
            return result;
        }
        identities.insert(key);
        mods.append(ref);
    }
    if (!mods.isEmpty() && on("mods")) {
        manifest.modReferences = mods;
        activeParts.insert("mods");
    }
    if (!draft && activeParts.isEmpty()) {
        result.problem = QStringLiteral("至少添加并启用一项内容后才能保存为预设。");
        return result;
    }
    manifest.category = activeParts.size() == 1 ? presetCategoryFromKey(*activeParts.begin())
                                                : PresetCategory::Unset;
    // Validate before copying any payload; imports use content-addressed file names.
    QJsonArray storedSections;
    for (const auto &v : fullSections) {
        auto section = v.toObject();
        QJsonArray items;
        for (const auto &entry : section.value("items").toArray()) {
            auto item = entry.toObject();
            QString relative = item.value("path").toString();
            const auto source = item.value("_source").toString();
            if (!source.isEmpty()) {
                QFile file(source);
                if (QFileInfo(source).isSymLink() || !file.open(QIODevice::ReadOnly)) {
                    result.problem = QStringLiteral("无法读取资源文件：%1").arg(source);
                    return result;
                }
                QCryptographicHash hash(QCryptographicHash::Sha256);
                if (!hash.addData(&file)) {
                    result.problem = QStringLiteral("资源文件校验失败。");
                    return result;
                }
                relative = QFileInfo(relative).path() + "/" +
                           QString::fromLatin1(hash.result().toHex().left(12)) + "-" +
                           QFileInfo(relative).fileName();
                if (!isSafeRelativePath(relative)) {
                    result.problem = QStringLiteral("资源文件路径无效。");
                    return result;
                }
                if (!copyFile(source, filesDir(destinationRoot, id) + "/" + relative,
                              &result.problem))
                    return result;
                item["path"] = relative;
                if (item.contains("world"))
                    item["name"] = QFileInfo(relative).fileName();
                item.remove("_source");
            } else if (!relative.isEmpty()) {
                if (!isSafeRelativePath(relative)) {
                    result.problem = QStringLiteral("资源文件路径无效。");
                    return result;
                }
                QString origin = state.value("_filesRoot").toString(filesDir(root, id));
                const auto base = QDir(root).absolutePath() + "/";
                const auto candidate = QDir(origin).absolutePath();
                if (!candidate.startsWith(base, Qt::CaseInsensitive) ||
                    !DownloadCheckpoint::safePath(origin)) {
                    result.problem = QStringLiteral("预设资源来源目录无效。");
                    return result;
                }
                if (origin != filesDir(destinationRoot, id)) {
                    const auto from = origin + "/" + relative,
                               to = filesDir(destinationRoot, id) + "/" + relative;
                    if (QFileInfo(from).isDir()) {
                        if (QFileInfo(from).isSymLink() ||
                            !copyTreeWithoutFollowingLinks(from, to, {}, &result.problem))
                            return result;
                    } else if (!copyFile(from, to, &result.problem))
                        return result;
                }
            }
            items.append(item);
        }
        section["items"] = items;
        storedSections.append(section);
    }
    state["sections"] = storedSections;
    state["_filesRoot"] = filesDir(destinationRoot, id);
    // Rebuild runtime payload after imported file paths have been normalized.
    manifest.sections.clear();
    for (const auto &v : storedSections) {
        auto section = sectionFromJson(v.toObject());
        const auto part = editorSectionPart(section);
        if (!part.isEmpty() && on(part))
            manifest.sections.append(section);
    }
    const QString icon = state.value("icon").toString();
    if (!icon.isEmpty() && QFileInfo::exists(icon)) {
        QFile file(icon);
        if (!file.open(QIODevice::ReadOnly)) {
            result.problem = QStringLiteral("无法读取图标。");
            return result;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&file);
        auto target = presetDir(destinationRoot, id) + "/icon-" +
                      QString::fromLatin1(hash.result().toHex().left(12)) + "." +
                      QFileInfo(icon).suffix();
        if (!copyFile(icon, target, &result.problem))
            return result;
        state["icon"] = target;
    }
    if (draft) {
        manifest.sections.clear();
        manifest.modReferences.clear();
        manifest.category = PresetCategory::Unset;
    }
    QJsonObject components;
    QStringList createdComponents;
    // 这一趟失败时把已经落下的子项收掉：父清单还是旧的那一份，这些新 id 没有任何人引用。
    // 只收**刚由这一趟创建**的 id（`discardInternalComponent` 不查全局引用）。
    const auto discardCreated = [&root, &createdComponents] {
        for (const QString &childId : createdComponents) {
            discardInternalComponent(root, childId);
        }
    };
    if (!draft && activeParts.size() > 1) {
        for (const auto &part : activeParts) {
            auto child = manifest;
            child.id = newPresetId();
            createdComponents.append(child.id);
            child.name = name;
            child.category = presetCategoryFromKey(part);
            child.internalComponent = true;
            child.editorState = {};
            child.sections.clear();
            child.modReferences.clear();
            for (const auto &section : manifest.sections)
                if (editorSectionPart(section) == part)
                    child.sections.append(section);
            if (part == "mods")
                child.modReferences = manifest.modReferences;
            // Each publish creates immutable components so failed saves cannot mutate a previous
            // publish.
            for (const auto &section : child.sections)
                for (const auto &v : section.items) {
                    const auto relative = v.toObject().value("path").toString();
                    if (relative.isEmpty())
                        continue;
                    const auto from = filesDir(destinationRoot, id) + "/" + relative,
                               to = filesDir(root, child.id) + "/" + relative;
                    if (QFileInfo(from).isDir()) {
                        if (!copyTreeWithoutFollowingLinks(from, to, {}, &result.problem)) {
                            discardCreated();
                            return result;
                        }
                    } else if (!copyFile(from, to, &result.problem)) {
                        discardCreated();
                        return result;
                    }
                }
            if (!saveManifest(root, child)) {
                result.problem = QStringLiteral("综合预设保存失败，原预设保留。");
                discardCreated();
                return result;
            }
            components[part] = child.id;
        }
    }
    state["components"] = components;
    manifest.editorState = state;
    if (!saveManifest(destinationRoot, manifest)) {
        result.problem = QStringLiteral("保存失败，原预设保留。");
        discardCreated();
        return result;
    }
    bool indexOk = false;
    auto index = loadIndex(destinationRoot, &indexOk);
    if (indexOk && !index.contains(id) && !saveIndex(destinationRoot, appendToIndex(index, id))) {
        result.problem = QStringLiteral("预设已保存，但列表顺序未能写入。");
        if (!draft) sweepOrphanComponents(destinationRoot);
        return result;
    }
    if (!draft && input.value("_filesRoot").toString() == filesDir(root + "/drafts", id) &&
        QFileInfo::exists(manifestPath(root + "/drafts", id)))
        remove(root + "/drafts", id);
    // 发布成功：上一版的子项这时可能已经没人引用了（这一趟换了新子项，或者从综合预设
    // 退回了单段）。核对拿不准时 sweep 一份都不动。
    if (!draft) sweepOrphanComponents(destinationRoot);
    result.ok = true;
    result.manifest = manifest;
    result.document = state;
    return result;
}
} // namespace Presets
