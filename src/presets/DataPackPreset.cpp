/**
 * @file DataPackPreset.cpp
 * @brief 实现 DataPackPreset.hpp（数据包属于世界：有世界就部署，没有就记待应用）。
 * @author 0x7fffff
 * @date 2026-10-18
 */

#include "DataPackPreset.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QSaveFile>
#include <QSet>

#include "GenericFiles.hpp"
#include "manage/FolderManager.hpp"
#include "manage/InstanceFiles.hpp"

namespace Presets {

namespace {

QString dataPackText(const char *source) { return QCoreApplication::translate("Presets", source); }

constexpr auto kSavesDir = "saves";
constexpr auto kDataPacksDir = "datapacks";
constexpr auto kPendingFormatTag = "wtfml-datapacks/1";
constexpr int kPendingSchemaVersion = 1;

/// 一根链接（symbolic link / junction）。★ 两个都要判（见 `InstanceFiles.hpp`）。
bool isLink(const QFileInfo &info) { return info.isJunction() || info.isSymLink(); }

/**
 * @brief 待应用清单 `<实例>/wtfml.datapacks` 的内容（世界 → 包名）。
 *
 * 读不出来 ⇒ `ok = false` 并给一句人话。★ 这份文件**只是我们自己的一份账**：读不动时
 * 宁可整趟不写它，也不拿一份空的盖掉它（那样用户之前记下的待应用包就没了）。
 */
struct PendingList {
    bool ok = true;
    bool exists = false;
    QString problem;
    QMap<QString, QStringList> worlds;  // 世界名 → 包名（有序、已去重）
};

PendingList readPendingList(const QString &instanceRoot) {
    PendingList out;
    const QString path = instanceRoot + QLatin1Char('/') + dataPackPendingFileName();
    const QFileInfo info(path);
    if (!info.exists()) {
        return out;
    }
    out.exists = true;
    if (!info.isFile()) {
        out.ok = false;
        out.problem = dataPackText("「%1」不是一份普通文件，待应用清单读不出来。").arg(path);
        return out;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out.ok = false;
        out.problem = dataPackText("读不动待应用清单「%1」：%2").arg(path, file.errorString());
        return out;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        out.ok = false;
        out.problem = dataPackText("读待应用清单「%1」读到一半出错了：%2")
                          .arg(path, file.errorString());
        return out;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        out.ok = false;
        out.problem = dataPackText("待应用清单「%1」不是一份能读的 JSON，这一趟没有动它。")
                          .arg(path);
        return out;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kPendingFormatTag) ||
        root.value(QStringLiteral("schemaVersion")).toInt() != kPendingSchemaVersion) {
        out.ok = false;
        out.problem = dataPackText("待应用清单「%1」的格式不是这一版认得的那样，这一趟没有动它。")
                          .arg(path);
        return out;
    }
    const QJsonValue worlds = root.value(QStringLiteral("worlds"));
    if (!worlds.isUndefined() && !worlds.isArray()) {
        out.ok = false;
        out.problem = dataPackText("待应用清单「%1」里的 `worlds` 不是数组，这一趟没有动它。")
                          .arg(path);
        return out;
    }
    for (const QJsonValue &value : worlds.toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject one = value.toObject();
        const QString world = one.value(QStringLiteral("name")).toString();
        if (world.isEmpty() || !InstanceFiles::validatePathSegment(world).isEmpty()) {
            continue;  // 坏名字不认，也不连坐别的世界
        }
        QStringList packs;
        for (const QJsonValue &packValue : one.value(QStringLiteral("packs")).toArray()) {
            const QString pack = packValue.toString();
            if (!pack.isEmpty() && InstanceFiles::validatePathSegment(pack).isEmpty()) {
                packs.append(pack);
            }
        }
        packs.removeDuplicates();
        out.worlds.insert(world, packs);
    }
    return out;
}

/// 原子写待应用清单。`ok = false` 时原来那一份还在。
bool writePendingList(const QString &instanceRoot, const QMap<QString, QStringList> &worlds,
                      QString *problem) {
    const QString path = instanceRoot + QLatin1Char('/') + dataPackPendingFileName();
    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(kPendingFormatTag));
    root.insert(QStringLiteral("schemaVersion"), kPendingSchemaVersion);
    QJsonArray array;
    for (auto it = worlds.constBegin(); it != worlds.constEnd(); ++it) {
        if (it.value().isEmpty()) {
            continue;
        }
        QJsonObject one;
        one.insert(QStringLiteral("name"), it.key());
        one.insert(QStringLiteral("packs"), QJsonArray::fromStringList(it.value()));
        array.append(one);
    }
    root.insert(QStringLiteral("worlds"), array);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *problem = dataPackText("写不动待应用清单「%1」：%2").arg(path, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        *problem = dataPackText("写待应用清单「%1」写到一半出错了：%2")
                       .arg(path, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *problem = dataPackText("待应用清单「%1」替换失败：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

/**
 * @brief 删掉一棵树（**一根链接都不跟着删**）。
 *
 * ★ `QDir::removeRecursively()` 会跟着 junction 删到那头去（见 `InstanceFiles.hpp`
 *   那段）。「覆盖」一个同名的数据包目录时，那一棵树里夹着链接的场合虽然怪，但删到
 *   用户别的盘上是不可接受的 —— 所以自己走一遍，碰到链接整项失败、什么都不再删。
 */
bool removeTreeNoLinks(const QString &path, QString *problem) {
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        return true;
    }
    if (isLink(info)) {
        *problem = dataPackText("「%1」是一根链接，没有动它。").arg(path);
        return false;
    }
    if (!info.isDir()) {
        if (!QFile::remove(path)) {
            *problem = dataPackText("删不掉「%1」。").arg(path);
            return false;
        }
        return true;
    }
    QDir dir(path);
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden |
                              QDir::System,
                          QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (!removeTreeNoLinks(entry.absoluteFilePath(), problem)) {
            return false;
        }
    }
    if (!QDir().rmdir(path)) {
        *problem = dataPackText("删不掉目录「%1」。").arg(path);
        return false;
    }
    return true;
}

/// 把一份文件/一棵目录拷到目标上（**先删掉目标上已有的那一份**）。目标上一根链接都不跟。
bool copyOverwrite(const QString &source, const QString &target, QString *problem) {
    const QFileInfo targetInfo(target);
    if (targetInfo.exists() || targetInfo.isSymLink()) {
        if (!removeTreeNoLinks(target, problem)) {
            return false;
        }
    }
    const QString parent = QFileInfo(target).absolutePath();
    if (!parent.isEmpty() && !QDir().mkpath(parent)) {
        *problem = dataPackText("建不了目录「%1」。").arg(parent);
        return false;
    }
    if (!copyTreeWithoutFollowingLinks(source, target)) {
        *problem = dataPackText("拷不进去「%1」（源那一侧也许是一根链接，或者目标建不出来）。")
                       .arg(target);
        return false;
    }
    return true;
}

/// 段里那一项的形状（`saves/<world>/datapacks/<name>`）。
struct ParsedPath {
    QString world;
    QString name;
    bool ok = false;
};

ParsedPath parseDataPackPath(const QString &relativePath) {
    ParsedPath out;
    const QStringList segments = relativePath.split(QLatin1Char('/'));
    if (segments.size() != 4 || segments.at(0) != QLatin1String(kSavesDir) ||
        segments.at(2) != QLatin1String(kDataPacksDir)) {
        return out;
    }
    out.world = segments.at(1);
    out.name = segments.at(3);
    out.ok = true;
    return out;
}

/// 世界目录（`<实例>/saves/<world>`）能不能往下写。链接 ⇒ 不写。
bool worldUsable(const QString &instanceRoot, const QString &world, bool *exists) {
    const QString path =
        instanceRoot + QLatin1Char('/') + QLatin1String(kSavesDir) + QLatin1Char('/') + world;
    const QFileInfo info(path);
    *exists = info.exists() && info.isDir() && !isLink(info);
    return !isLink(info);
}

/// 一个包在目标上的落点（有世界 ⇒ 世界里；没有 ⇒ `datapacks-pending/`）。
QString targetPathFor(const QString &instanceRoot, const QString &world, const QString &name,
                      bool worldExists) {
    if (worldExists) {
        return instanceRoot + QLatin1Char('/') + QLatin1String(kSavesDir) + QLatin1Char('/') +
               world + QLatin1Char('/') + QLatin1String(kDataPacksDir) + QLatin1Char('/') + name;
    }
    return instanceRoot + QLatin1Char('/') + dataPackPendingDirName() + QLatin1Char('/') + world +
           QLatin1Char('/') + name;
}

}  // namespace

QString dataPackSectionId() {
    return sectionIdFor(SectionKind::Custom, QStringLiteral("dataPacks"));
}

bool dataPackHandlesSection(const PresetSection &section) {
    return section.kind == SectionKind::Custom && section.id == dataPackSectionId();
}

QString dataPackPendingFileName() {
    return QStringLiteral("wtfml.datapacks");
}

QString dataPackPendingDirName() {
    return QStringLiteral("datapacks-pending");
}

bool dataPackItemsFromSection(const PresetSection &section, QList<DataPackItem> *out,
                              QString *problemOut) {
    if (problemOut != nullptr) {
        problemOut->clear();
    }
    if (!dataPackHandlesSection(section)) {
        if (problemOut != nullptr) {
            *problemOut = dataPackText("这一段不是数据包那一段。");
        }
        return false;
    }
    QList<DataPackItem> items;
    for (const QJsonValue &value : section.items) {
        if (!value.isObject()) {
            if (problemOut != nullptr) {
                *problemOut = dataPackText("数据包这一段里有不是对象的一项。");
            }
            return false;
        }
        const QJsonObject object = value.toObject();
        const QString relative = object.value(QStringLiteral("path")).toString();
        QString why;
        if (!genericPathIsSafe(relative, &why)) {
            if (problemOut != nullptr) {
                *problemOut = why;
            }
            return false;
        }
        const ParsedPath parsed = parseDataPackPath(relative);
        if (!parsed.ok) {
            if (problemOut != nullptr) {
                *problemOut = dataPackText("「%1」不是 saves/<世界>/datapacks/<包> 这种路径。")
                                  .arg(relative);
            }
            return false;
        }
        const QString world = object.value(QStringLiteral("world")).toString();
        if (!world.isEmpty() && world != parsed.world) {
            if (problemOut != nullptr) {
                *problemOut = dataPackText("「%1」的世界名和它自己的路径对不上。").arg(relative);
            }
            return false;
        }
        DataPackItem item;
        item.world = parsed.world;
        item.name = parsed.name;
        item.relativePath = relative;
        items.append(item);
    }
    if (out != nullptr) {
        *out = items;
    }
    return true;
}

bool sectionsFromDataPacks(const QList<DataPackItem> &items, QList<PresetSection> *out,
                           QString *problemOut) {
    if (problemOut != nullptr) {
        problemOut->clear();
    }
    PresetSection section;
    section.id = dataPackSectionId();
    section.kind = SectionKind::Custom;
    section.title = dataPackText("数据包");
    section.mode = ApplyMode::CopyIfMissing;
    for (const DataPackItem &item : items) {
        const QString relative = QString::fromLatin1(kSavesDir) + QLatin1Char('/') + item.world +
                                 QLatin1Char('/') + QLatin1String(kDataPacksDir) +
                                 QLatin1Char('/') + item.name;
        QString why;
        if (!genericPathIsSafe(relative, &why)) {
            if (problemOut != nullptr) {
                *problemOut = why;
            }
            return false;
        }
        QJsonObject row;
        row.insert(QStringLiteral("world"), item.world);
        row.insert(QStringLiteral("path"), relative);
        section.items.append(row);
    }
    if (section.items.isEmpty()) {
        return true;  // 空段：不写进清单（调用方自己判）
    }
    if (out != nullptr) {
        out->append(section);
    }
    return true;
}

DataPackCaptureReport captureDataPacks(const QString &instanceRoot,
                                       const QString &presetFilesRoot) {
    DataPackCaptureReport report;

    if (instanceRoot.trimmed().isEmpty() || !QFileInfo(instanceRoot).isDir()) {
        report.problem = dataPackText("这个实例的目录不在，没法收数据包。");
        return report;
    }
    if (presetFilesRoot.trimmed().isEmpty()) {
        report.problem = dataPackText("没有指定这份预设的 files 目录。");
        return report;
    }

    const QString savesPath = instanceRoot + QLatin1Char('/') + QLatin1String(kSavesDir);
    const QFileInfo savesInfo(savesPath);
    if (!savesInfo.exists()) {
        report.ok = true;
        report.notes.append(dataPackText("这个实例还没有存档（saves/），数据包这一趟没有可收的。"));
        return report;
    }
    if (isLink(savesInfo)) {
        report.problem = dataPackText("「%1」是一根链接，没有跟着它走。").arg(savesPath);
        return report;
    }
    if (!savesInfo.isDir()) {
        report.problem = dataPackText("「%1」不是一个目录，没有扫它。").arg(savesPath);
        return report;
    }

    const QFileInfoList worldDirs =
        QDir(savesPath).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden |
                                          QDir::System,
                                      QDir::Name);
    QList<DataPackItem> items;
    QSet<QString> worldsWithPacks;
    bool sawAnyDatapacksDir = false;

    for (const QFileInfo &worldDir : worldDirs) {
        if (isLink(worldDir)) {
            report.problems.append(dataPackText("存档「%1」是一根链接，没有跟着它走。")
                                       .arg(worldDir.fileName()));
            continue;
        }
        const QString world = worldDir.fileName();
        if (!InstanceFiles::validatePathSegment(world).isEmpty()) {
            report.problems.append(
                dataPackText("存档名「%1」不能当目录名，这一趟没有碰它。").arg(world));
            continue;
        }
        const QString packsPath = worldDir.absoluteFilePath() + QLatin1Char('/') +
                                  QLatin1String(kDataPacksDir);
        const QFileInfo packsInfo(packsPath);
        if (!packsInfo.exists()) {
            continue;
        }
        sawAnyDatapacksDir = true;
        if (isLink(packsInfo)) {
            report.problems.append(dataPackText("存档「%1」的 datapacks/ 是一根链接，没有动它。")
                                       .arg(world));
            continue;
        }
        if (!packsInfo.isDir()) {
            report.problems.append(dataPackText("存档「%1」的 datapacks 不是一个目录，没有动它。")
                                       .arg(world));
            continue;
        }

        const QFileInfoList entries =
            QDir(packsPath).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot |
                                              QDir::Hidden | QDir::System,
                                          QDir::Name);
        for (const QFileInfo &entry : entries) {
            const QString name = entry.fileName();
            if (isLink(entry)) {
                report.problems.append(dataPackText("「%1」里的「%2」是一根链接，没有收它。")
                                           .arg(world, name));
                continue;
            }
            if (!InstanceFiles::validatePathSegment(name).isEmpty()) {
                report.problems.append(dataPackText("「%1」里有一项名字不能当文件名（%2），没有收它。")
                                           .arg(world, name));
                continue;
            }
            const QString relative = QString::fromLatin1(kSavesDir) + QLatin1Char('/') + world +
                                     QLatin1Char('/') + QLatin1String(kDataPacksDir) +
                                     QLatin1Char('/') + name;
            const QString target = presetFilesRoot + QLatin1Char('/') + relative;
            QString why;
            if (!copyOverwrite(entry.absoluteFilePath(), target, &why)) {
                report.problems.append(
                    dataPackText("「%1」没收进来：%2").arg(relative, why));
                continue;
            }
            DataPackItem item;
            item.world = world;
            item.name = name;
            item.relativePath = relative;
            items.append(item);
            worldsWithPacks.insert(world);
        }
    }

    if (items.isEmpty()) {
        report.ok = true;
        report.notes.append(sawAnyDatapacksDir
                                ? dataPackText("存档里的 datapacks/ 都是空的。")
                                : dataPackText("saves/ 底下没有哪个存档带 datapacks/。"));
        return report;
    }

    QList<PresetSection> made;
    QString why;
    if (!sectionsFromDataPacks(items, &made, &why)) {
        report.problem = why;
        return report;
    }
    report.sections = made;
    report.worlds = static_cast<int>(worldsWithPacks.size());
    report.packs = static_cast<int>(items.size());
    report.notes.append(dataPackText("收了 %1 个存档里的 %2 个数据包。")
                            .arg(report.worlds)
                            .arg(report.packs));
    report.ok = true;
    return report;
}

DataPackPlan planDataPacks(const QString &instanceRoot, const QList<DataPackItem> &items,
                           ApplyMode mode) {
    DataPackPlan plan;
    if (instanceRoot.trimmed().isEmpty()) {
        plan.problem = dataPackText("没有指定目标实例，算不出数据包这一趟要做什么。");
        return plan;
    }

    for (const DataPackItem &raw : items) {
        DataPackPlanItem item;
        item.world = raw.world;
        item.name = raw.name;
        item.relativePath = raw.relativePath;

        QString why;
        if (!genericPathIsSafe(item.relativePath, &why)) {
            plan.problem = why;
            return plan;
        }
        const ParsedPath parsed = parseDataPackPath(item.relativePath);
        if (!parsed.ok || parsed.world != item.world || parsed.name != item.name) {
            plan.problem = dataPackText("预设里的数据包项「%1」形状不对。").arg(item.relativePath);
            return plan;
        }

        bool exists = false;
        if (!worldUsable(instanceRoot, item.world, &exists)) {
            // 世界那一格是一根链接：不往里写。当成"没有这个世界"会悄悄记成待应用，
            // 但用户明明有那个存档 —— 所以整段做不了，理由说清楚。
            plan.problem = dataPackText("目标上的存档「%1」是一根链接，没有往里写。").arg(item.world);
            return plan;
        }
        const QFileInfo worldInfo(instanceRoot + QLatin1Char('/') + QLatin1String(kSavesDir) +
                                  QLatin1Char('/') + item.world);
        if (!exists && worldInfo.exists()) {
            plan.problem = dataPackText("目标上的「saves/%1」不是个目录，没有往里写。")
                               .arg(item.world);
            return plan;
        }
        item.worldExists = exists;
        const QString target = targetPathFor(instanceRoot, item.world, item.name, exists);
        const QFileInfo targetInfo(target);
        item.targetExists = targetInfo.exists() || targetInfo.isSymLink();

        switch (mode) {
        case ApplyMode::Skip:
            ++plan.skipped;
            break;
        case ApplyMode::Merge:
        case ApplyMode::Append:
            ++plan.needsChoice;
            break;
        case ApplyMode::CopyIfMissing:
            if (item.targetExists) {
                ++plan.skipped;
            } else if (item.worldExists) {
                ++plan.deploy;
            } else {
                ++plan.deferred;
            }
            break;
        case ApplyMode::Replace:
            if (item.worldExists) {
                ++plan.deploy;
            } else {
                ++plan.deferred;
            }
            break;
        }
        plan.items.append(item);
    }
    plan.ok = true;
    return plan;
}

DataPackApplyReport applyDataPacks(const QString &presetFilesRoot, const QString &instanceRoot,
                                   const PresetSection &section, ApplyMode mode,
                                   bool replaceConfirmed) {
    DataPackApplyReport report;

    QList<DataPackItem> items;
    QString why;
    if (!dataPackItemsFromSection(section, &items, &why)) {
        report.problem = why;
        return report;
    }

    if (instanceRoot.trimmed().isEmpty() || !QFileInfo(instanceRoot).isDir()) {
        report.problem = dataPackText("目标实例的目录不在，没法把数据包落下去。");
        return report;
    }
    if (presetFilesRoot.trimmed().isEmpty()) {
        report.problem = dataPackText("没有给出预设那一侧的文件根（files/），拷不到数据包。");
        return report;
    }

    if (mode == ApplyMode::Skip) {
        report.ok = true;
        report.skipped = static_cast<int>(items.size());
        return report;
    }
    if (mode == ApplyMode::Merge || mode == ApplyMode::Append) {
        // 数据包是游戏自己读的整份东西：我们不解释它里面的内容，"合并"无从谈起。
        // 这一档一个字节都不写，只把"要你选"的数报回去（和通用文件组那一档同一条）。
        report.ok = true;
        report.needsChoice = static_cast<int>(items.size());
        report.details.append(dataPackText("这一档下数据包做不了主（要做的是整份拷进去），"
                                           "没有动任何文件；请把这一段改成「覆盖」或「缺了才拷」。"));
        return report;
    }
    if (mode == ApplyMode::Replace && !replaceConfirmed) {
        report.problem = dataPackText("「覆盖」还没有确认过，这一趟没有动数据包。");
        return report;
    }

    const DataPackPlan plan = planDataPacks(instanceRoot, items, mode);
    if (!plan.ok) {
        report.problem = plan.problem;
        return report;
    }

    // 待应用清单：动手之前先读一遍。读不动 ⇒ 整段不动（不拿一份空的盖掉用户的账）。
    PendingList pending;
    bool hasDeferred = false;
    for (const DataPackPlanItem &item : plan.items) {
        if (!item.worldExists && !(mode == ApplyMode::CopyIfMissing && item.targetExists)) {
            hasDeferred = true;
            break;
        }
    }
    if (hasDeferred) {
        pending = readPendingList(instanceRoot);
        if (!pending.ok) {
            report.problem = pending.problem;
            return report;
        }
    }

    int deployed = 0;
    int deferred = 0;
    int skipped = 0;

    for (const DataPackPlanItem &item : plan.items) {
        const QString source = presetFilesRoot + QLatin1Char('/') + item.relativePath;
        const QFileInfo sourceInfo(source);
        if (!sourceInfo.exists()) {
            report.problem = dataPackText("预设里那一份「%1」不在（files/ 底下找不到）。")
                                 .arg(item.relativePath);
            // 已经落下去的那些留在盘上（和通用文件组那一条一致）。
            return report;
        }
        if (isLink(sourceInfo)) {
            report.problem = dataPackText("预设里那一份「%1」是一根链接，没有跟着它走。")
                                 .arg(item.relativePath);
            return report;
        }

        const QString target = targetPathFor(instanceRoot, item.world, item.name, item.worldExists);
        const QFileInfo targetInfo(target);
        const bool targetExists = targetInfo.exists() || targetInfo.isSymLink();

        if (mode == ApplyMode::CopyIfMissing && targetExists) {
            ++skipped;
            continue;
        }
        if (targetExists && isLink(targetInfo)) {
            report.problem =
                dataPackText("目标上的「%1」是一根链接，没有动它。").arg(target);
            return report;
        }
        if (mode == ApplyMode::Replace && targetExists) {
            QString removeWhy;
            if (!removeTreeNoLinks(target, &removeWhy)) {
                report.problem = removeWhy;
                return report;
            }
        }

        QString copyWhy;
        if (!copyOverwrite(source, target, &copyWhy)) {
            report.problem = copyWhy;
            return report;
        }

        if (item.worldExists) {
            ++deployed;
        } else {
            ++deferred;
            QStringList &packs = pending.worlds[item.world];
            if (!packs.contains(item.name)) {
                packs.append(item.name);
                packs.sort();
            }
        }
    }

    if (deferred > 0) {
        QString writeWhy;
        if (!writePendingList(instanceRoot, pending.worlds, &writeWhy)) {
            report.problem = writeWhy;
            return report;
        }
        report.pendingFile = instanceRoot + QLatin1Char('/') + dataPackPendingFileName();
    }

    report.ok = true;
    report.deployed = deployed;
    report.deferred = deferred;
    report.skipped = skipped;

    if (deployed > 0) {
        report.details.append(dataPackText("已把 %1 个数据包部署进对应的存档（datapacks/）。"
                                           "游戏里开没开由存档自己决定，可以在游戏内用 "
                                           "/datapack list 查看。")
                               .arg(deployed));
    }
    if (deferred > 0) {
        report.details.append(dataPackText("目标上没有这些存档，%1 个数据包记为「待应用」"
                                           "（%2/<存档名>/，清单是 %3）；这一趟没有新建世界。")
                               .arg(deferred)
                               .arg(dataPackPendingDirName(), dataPackPendingFileName()));
    }
    if (skipped > 0) {
        report.details.append(dataPackText("目标上已经有 %1 个同名数据包，这一档没有动它们。")
                                  .arg(skipped));
    }
    return report;
}

QStringList pendingDataPacks(const QString &instanceRoot, const QString &world) {
    if (instanceRoot.trimmed().isEmpty() || world.trimmed().isEmpty()) {
        return QStringList();
    }
    const PendingList pending = readPendingList(instanceRoot);
    if (!pending.ok) {
        return QStringList();
    }
    return pending.worlds.value(world);
}

}  // namespace Presets
