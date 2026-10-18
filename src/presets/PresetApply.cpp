/**
 * @file PresetApply.cpp
 * @brief 实现 PresetApply.hpp。取舍都写在头文件里，这里只留实现上的几个坑。
 * @author 0x7fffff
 * @date 2026-10-18
 */

#include "PresetApply.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include "GenericFiles.hpp"
#include "PresetPlanner.hpp"
#include "PresetStore.hpp"
#include "ServerListPreset.hpp"
#include "VanillaOptions.hpp"
#include "manage/FolderManager.hpp"
#include "manage/ServerListFile.hpp"

namespace Presets {

namespace {

QString applyText(const char *source) { return QCoreApplication::translate("Presets", source); }

/// `options.txt` 的读取上限（同 `PresetCapture.cpp`）：真机上几十 KB，超过这个数不猜。
constexpr qint64 kOptionsMaxBytes = 32LL * 1024 * 1024;

/// 读一整份文件。`false` 时 `problem` 是一句人话。
bool readWholeFile(const QString &path, QByteArray *out, QString *problem) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *problem = applyText("读不动「%1」：%2").arg(path, file.errorString());
        return false;
    }
    *out = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        *problem = applyText("读「%1」读到一半出错了：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

/// 原子写一整份文件（`QSaveFile`）。`false` 时原来那一份**还在**。
bool writeWholeFile(const QString &path, const QByteArray &bytes, QString *problem) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *problem = applyText("写不动「%1」：%2").arg(path, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        *problem = applyText("写「%1」写到一半出错了：%2").arg(path, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *problem = applyText("「%1」替换失败：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

/**
 * @brief 造一个这一趟专用的备份目录（`<presets 根>/backups/<实例>/<时间>`）。
 *
 * ★ 同一秒里撞上同名目录时往后加 `-2` / `-3`：覆盖掉上一份备份等于这一趟没备份。
 */
QString makeBackupDir(const QString &presetsRoot, const QString &instanceId) {
    const QString base = presetsRoot + QStringLiteral("/backups/") +
                         (instanceId.isEmpty() ? QStringLiteral("instance") : instanceId) +
                         QLatin1Char('/') +
                         QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString candidate = base;
    for (int n = 2; QFileInfo::exists(candidate) && n < 100; ++n) {
        candidate = base + QStringLiteral("-%1").arg(n);
    }
    if (!QDir().mkpath(candidate)) {
        return QString();
    }
    return candidate;
}

/**
 * @brief 把 @p relative 那一份（文件或整棵目录）从实例里拷进备份目录。
 *
 * @return 成功、或者目标上本来就没有这一份 ⇒ true。**备份失败必须让整趟停下** —— 这是
 *         调用方在写任何东西之前最后一道"还能回到原样"的保证。
 */
bool backupOne(const QString &instanceRoot, const QString &backupDir, const QString &relative,
               QString *problem) {
    if (relative.isEmpty()) {
        return true;
    }
    const QString source = instanceRoot + QLatin1Char('/') + relative;
    if (!QFileInfo::exists(source)) {
        return true;
    }
    const QString target = backupDir + QLatin1Char('/') + relative;
    const QString parent = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(parent)) {
        *problem = applyText("建不了备份目录「%1」。").arg(parent);
        return false;
    }
    if (!copyTreeWithoutFollowingLinks(source, target)) {
        *problem = applyText("备份「%1」失败（这一趟什么都没写）。").arg(relative);
        return false;
    }
    return true;
}

/// 段在界面上的默认标题（段自己没带标题时用）。★ 中文名只应该出现在 `src/` 和 `ui/`
/// 其中一处；这里沿用的是 `PresetPlanner` 报告里那份叫法（`applyPlanUnit()` 等），
/// 管理页拿到的是 `PresetApplyRow`，标题为空时按 `kind` 现翻一次。
QString fallbackSectionTitle(SectionKind kind) {
    switch (kind) {
    case SectionKind::VanillaOptions:
        return applyText("游戏设置");
    case SectionKind::VanillaKeys:
        return applyText("原版按键");
    case SectionKind::ServerList:
        return applyText("服务器列表");
    case SectionKind::MalilibKeys:
        return applyText("MaLiLib 热键");
    case SectionKind::GenericFiles:
        return applyText("配置文件");
    case SectionKind::Custom:
        break;
    }
    return applyText("未知内容");
}

/// 这一段归哪一类（报告里那一行用）。
PresetApplyRow rowFromPlan(const ApplyPlanRow &planRow) {
    PresetApplyRow row;
    row.sectionId = planRow.sectionId;
    row.kind = planRow.kind;
    row.mode = planRow.mode;
    row.title = planRow.title.isEmpty() ? fallbackSectionTitle(planRow.kind) : planRow.title;
    row.blocked = planRow.blocked;
    row.ok = !planRow.blocked && planRow.mode == ApplyMode::Skip;
    row.skipped = planRow.mode == ApplyMode::Skip;
    row.written = planRow.count;
    row.removed = planRow.removed;
    row.skippedItems = planRow.skipped;
    row.problem = planRow.problem;
    return row;
}

}  // namespace

TargetSnapshot readTargetSnapshot(const QString &instanceRoot, QString *problemOut) {
    if (problemOut != nullptr) {
        problemOut->clear();
    }
    TargetSnapshot snapshot;
    snapshot.instanceRoot = instanceRoot;
    if (instanceRoot.trimmed().isEmpty()) {
        if (problemOut != nullptr) {
            *problemOut = applyText("没有指定目标实例。");
        }
        return snapshot;
    }

    // ---------- options.txt ----------
    const QString optionsPath = instanceRoot + QStringLiteral("/options.txt");
    const QFileInfo optionsInfo(optionsPath);
    if (optionsInfo.exists()) {
        if (!optionsInfo.isFile()) {
            if (problemOut != nullptr) {
                *problemOut = applyText("「%1」不是一份普通文件，不敢拿预设去改它。")
                                  .arg(optionsPath);
            }
            return snapshot;
        }
        if (optionsInfo.size() > kOptionsMaxBytes) {
            if (problemOut != nullptr) {
                *problemOut = applyText("「%1」大到不像是 options.txt（%2 字节），"
                                        "不敢拿预设去改它。")
                                  .arg(optionsPath)
                                  .arg(optionsInfo.size());
            }
            return snapshot;
        }
        QByteArray bytes;
        QString why;
        if (!readWholeFile(optionsPath, &bytes, &why)) {
            if (problemOut != nullptr) {
                *problemOut = why;
            }
            return snapshot;
        }
        snapshot.options = parseOptions(bytes);
    }

    // ---------- servers.dat ----------
    const QString serversPath = instanceRoot + QStringLiteral("/servers.dat");
    NbtTree::Document document;
    bool gzipped = false;
    QString why;
    const ServerListFile::FileState state =
        ServerListFile::loadDocument(serversPath, &document, &gzipped, &why);
    if (state == ServerListFile::FileState::Unreadable) {
        // ★ 读不动和"没有这一份"是两件事（见 `PresetPlanner::TargetSnapshot` 那段）：
        //   前者会让服务器列表这一段这一趟做不了，后者是最正常的新实例。
        snapshot.serversProblem = why;
    } else if (state == ServerListFile::FileState::Ok) {
        if (!serverEntriesOf(&document, &snapshot.servers, &snapshot.serversUntouchable, &why)) {
            snapshot.serversProblem = why;
        }
    }
    return snapshot;
}

PresetApplyResult applyPresetToInstance(const QString &presetsRoot, const PresetManifest &manifest,
                                        const PresetApplyRequest &request) {
    PresetApplyResult result;

    if (!isValidPresetId(manifest.id)) {
        result.problem = applyText("这份预设的 id 不合法，没法应用。");
        return result;
    }
    if (request.instanceRoot.trimmed().isEmpty() ||
        !QFileInfo(request.instanceRoot).isDir()) {
        result.problem = applyText("这个实例的目录不在，没法把预设落到它上面。");
        return result;
    }

    // ------------------------------------------------------------------
    // 1. 目标快照
    // ------------------------------------------------------------------
    QString snapshotProblem;
    const TargetSnapshot snapshot = readTargetSnapshot(request.instanceRoot, &snapshotProblem);
    if (!snapshotProblem.isEmpty()) {
        result.problem = snapshotProblem;
        return result;
    }

    // ------------------------------------------------------------------
    // 2. 计划（只读）
    // ------------------------------------------------------------------
    PlanRequest planRequest;
    planRequest.presetFilesRoot = filesDir(presetsRoot, manifest.id);
    planRequest.sections = manifest.sections;
    planRequest.target = snapshot;
    planRequest.replaceConfirmed = request.replaceConfirmed;
    planRequest.gameRunning = request.gameRunning;

    const ApplyPlan plan = planPresetApplication(planRequest);
    if (!plan.ok) {
        result.problem = plan.problem;
        return result;
    }
    result.keyConflicts = plan.keyConflicts;

    if (!plan.blockedReason.isEmpty()) {
        // 游戏在跑 —— 整趟不许开始（连备份都不做）。
        result.blockedReason = plan.blockedReason;
        result.rows.reserve(plan.rows.size());
        for (const ApplyPlanRow &one : plan.rows) {
            result.rows.append(rowFromPlan(one));
        }
        return result;
    }

    if (plan.needsConfirm() && !request.replaceConfirmed) {
        // 第一趟：「覆盖」还没点头。**一个字节都不写**，把"哪几段在等你确认"报回去。
        result.needsConfirm = true;
        result.rows.reserve(plan.rows.size());
        for (const ApplyPlanRow &one : plan.rows) {
            result.rows.append(rowFromPlan(one));
        }
        return result;
    }

    // ------------------------------------------------------------------
    // 3. 备份：把"这一趟会改写或删掉的、已经存在的"那几样先收走
    // ------------------------------------------------------------------
    //
    // ★ 这一趟算的是"哪些会被动"，和下面第 4 步用的是**同一套判据**（适配器自己的
    //   计划函数）。不重算写出来的那一份，只重算"目标上现在有没有"。
    QSet<QString> backupPaths;
    const QString optionsPath = request.instanceRoot + QStringLiteral("/options.txt");
    const QString serversPath = request.instanceRoot + QStringLiteral("/servers.dat");

    // ---------- options.txt：在内存里叠加一遍，看最后的字节变没变 ----------
    {
        OptionsFile running = snapshot.options;
        const QByteArray original = running.toBytes();
        for (const PresetSection &section : manifest.sections) {
            if (!optionsHandlesSection(section.kind)) {
                continue;
            }
            bool readable = false;
            const OptionsFile source = optionsFromSections({section}, &readable);
            if (!readable) {
                continue;  // 这一段下面会被报成 blocked，不备份
            }
            QSet<QString> selected;
            const QStringList keys = source.keys();
            for (const QString &key : keys) {
                selected.insert(key);
            }
            const OptionsApplyResult applied =
                applyOptions(section.kind, running, source, selected, section.mode, true);
            if (!applied.report.ok) {
                continue;
            }
            running = parseOptions(applied.bytes);
        }
        if (running.toBytes() != original && QFileInfo::exists(optionsPath)) {
            backupPaths.insert(QStringLiteral("options.txt"));
        }
    }

    // ---------- servers.dat ----------
    for (const PresetSection &section : manifest.sections) {
        if (!serverListHandlesSection(section.kind) || section.mode == ApplyMode::Skip) {
            continue;
        }
        QList<NbtUtil::ServerEntry> source;
        QString why;
        if (!serverListFromSection(section, &source, &why)) {
            continue;
        }
        const ServerListReport planned = planServerListApply(
            section.mode, snapshot.servers, snapshot.serversUntouchable, source, true);
        if (planned.ok && (planned.written > 0 || planned.removed > 0) &&
            QFileInfo::exists(serversPath)) {
            backupPaths.insert(QStringLiteral("servers.dat"));
        }
    }

    // ---------- 通用文件组：这一趟会被换成预设那一份的同名项 ----------
    const QString presetFilesRoot = filesDir(presetsRoot, manifest.id);
    for (const PresetSection &section : manifest.sections) {
        if (!genericFilesHandlesSection(section.kind) || section.mode == ApplyMode::Skip) {
            continue;
        }
        QList<GenericItem> items;
        QString why;
        if (!fileGroupFromSection(section, &items, &why)) {
            continue;
        }
        FileGroup group = FileGroup::Config;
        const bool idSays = fileGroupFromSectionId(section.id, &group);
        if (!idSays || items.isEmpty()) {
            continue;
        }
        const GenericPlan planned =
            planFileGroup(presetFilesRoot, items, request.instanceRoot, section.mode);
        if (!planned.ok) {
            continue;
        }
        for (int i = 0; i < items.size() && i < planned.items.size(); ++i) {
            if (planned.items.at(i).action != GenericAction::Replace) {
                continue;
            }
            const QString relative = items.at(i).relativePath;
            if (QFileInfo::exists(request.instanceRoot + QLatin1Char('/') + relative)) {
                backupPaths.insert(relative);
            }
        }
    }

    // ---------- MaLiLib：这一趟内容真的会变的那些文件 ----------
    for (const PresetSection &section : manifest.sections) {
        if (!malilibHandlesSection(section.kind) || section.mode == ApplyMode::Skip) {
            continue;
        }
        QList<MalilibEntry> entries;
        QString why;
        if (!malilibEntriesFromSections({section}, &entries, &why)) {
            continue;
        }
        QStringList modIds;
        for (const MalilibEntry &entry : entries) {
            if (!modIds.contains(entry.modId)) {
                modIds.append(entry.modId);
            }
        }
        const MalilibTarget target = malilibReadTarget(request.instanceRoot, modIds);
        const MalilibApplyResult applied = applyMalilibEntries(entries, target, section.mode, true);
        if (!applied.report.ok) {
            continue;
        }
        for (const MalilibFileResult &file : applied.files) {
            if (!file.changed) {
                continue;
            }
            const QString relative = QStringLiteral("config/%1.json").arg(file.modId);
            if (QFileInfo::exists(request.instanceRoot + QLatin1Char('/') + relative)) {
                backupPaths.insert(relative);
            }
        }
    }

    if (!backupPaths.isEmpty()) {
        const QString instanceId = request.instanceId.isEmpty()
                                       ? QFileInfo(request.instanceRoot).fileName()
                                       : request.instanceId;
        const QString backupDir = makeBackupDir(presetsRoot, instanceId);
        if (backupDir.isEmpty()) {
            result.problem = applyText("建不了备份目录，这一趟什么都没写（不敢在没有备份的情况下动"
                                       "这个实例）。");
            return result;
        }
        QStringList ordered = backupPaths.values();
        ordered.sort();
        for (const QString &relative : ordered) {
            QString why;
            if (!backupOne(request.instanceRoot, backupDir, relative, &why)) {
                result.problem = why;
                return result;
            }
        }
        result.backupDir = backupDir;
    }

    // ------------------------------------------------------------------
    // 4. 执行
    // ------------------------------------------------------------------
    //
    // ★ 一行一段，次序照 manifest。`options.txt` 那两段只在内存里叠，最后写一次。
    OptionsFile optionsTarget = snapshot.options;
    QList<int> optionsRowIndexes;
    bool optionsTouched = false;

    for (const PresetSection &section : manifest.sections) {
        PresetApplyRow row;
        row.sectionId = section.id;
        row.kind = section.kind;
        row.mode = section.mode;
        row.title = section.title.isEmpty() ? fallbackSectionTitle(section.kind) : section.title;

        if (section.mode == ApplyMode::Skip) {
            row.ok = true;
            row.skipped = true;
            result.rows.append(row);
            continue;
        }

        if (optionsHandlesSection(section.kind)) {
            bool readable = false;
            const OptionsFile source = optionsFromSections({section}, &readable);
            if (!readable) {
                row.blocked = true;
                row.problem = applyText("这一段读不回来（载荷不是这一层认得的那样），这一趟不动它。");
                result.rows.append(row);
                continue;
            }
            QSet<QString> selected;
            const QStringList keys = source.keys();
            for (const QString &key : keys) {
                selected.insert(key);
            }
            const OptionsApplyResult applied = applyOptions(
                section.kind, optionsTarget, source, selected, section.mode,
                request.replaceConfirmed);
            if (!applied.report.ok) {
                row.blocked = true;
                row.problem = applied.report.problem;
                result.rows.append(row);
                continue;
            }
            optionsTarget = parseOptions(applied.bytes);
            optionsTouched = true;
            optionsRowIndexes.append(static_cast<int>(result.rows.size()));
            row.ok = true;
            row.written = applied.report.written;
            row.added = applied.report.added;
            row.removed = applied.report.removed;
            row.skippedItems = applied.report.skipped;
            for (const OptionsConflict &conflict : applied.report.conflicts) {
                row.details.append(applyText("按键「%1」被 %2 个动作抢着用。")
                                       .arg(conflict.binding)
                                       .arg(conflict.claimedBy()));
            }
            result.rows.append(row);
            continue;
        }

        if (serverListHandlesSection(section.kind)) {
            QList<NbtUtil::ServerEntry> source;
            QString why;
            if (!serverListFromSection(section, &source, &why)) {
                row.blocked = true;
                row.problem = why;
                result.rows.append(row);
                continue;
            }
            const ServerListReport applied = applyServerListToInstance(
                serversPath, section.mode, source, request.replaceConfirmed);
            row.ok = applied.ok;
            row.blocked = !applied.ok;
            row.problem = applied.problem;
            row.written = applied.written;
            row.added = applied.added;
            row.removed = applied.removed;
            row.skippedItems = applied.skipped;
            if (applied.skipped > 0) {
                row.details.append(applyText("这几台已经在列表里了：%1")
                                       .arg(applied.skippedAddresses.join(QStringLiteral("、"))));
            }
            result.rows.append(row);
            continue;
        }

        if (genericFilesHandlesSection(section.kind)) {
            QList<GenericItem> items;
            QString why;
            if (!fileGroupFromSection(section, &items, &why)) {
                row.blocked = true;
                row.problem = why;
                result.rows.append(row);
                continue;
            }
            FileGroup group = FileGroup::Config;
            if (!fileGroupFromSectionId(section.id, &group)) {
                if (items.isEmpty()) {
                    row.blocked = true;
                    row.problem = applyText("认不出这一段是哪一组文件，这一趟不动它。");
                    result.rows.append(row);
                    continue;
                }
                // 路径自己说了算（`fileGroupOfPath()` 已经有那一口；这里按第一项推）。
                if (!fileGroupOfPath(items.at(0).relativePath, &group)) {
                    row.blocked = true;
                    row.problem = applyText("这一段里的路径不属于那四个文件组，这一趟不动它。");
                    result.rows.append(row);
                    continue;
                }
            }
            const GenericReport applied =
                applyFileGroup(presetFilesRoot, request.instanceRoot, group, section.mode);
            row.ok = applied.ok;
            row.blocked = !applied.ok;
            row.problem = applied.problem;
            row.written = applied.replaced + applied.copied;
            row.skippedItems = applied.skipped;
            for (const QString &one : applied.refused) {
                row.details.append(one);
            }
            if (applied.needsChoice > 0) {
                row.details.append(applyText("这一档下这一组里有 %1 项做不了主，没有动。")
                                       .arg(applied.needsChoice));
            }
            result.rows.append(row);
            continue;
        }

        if (malilibHandlesSection(section.kind)) {
            QList<MalilibEntry> entries;
            QString why;
            if (!malilibEntriesFromSections({section}, &entries, &why)) {
                row.blocked = true;
                row.problem = why;
                result.rows.append(row);
                continue;
            }
            const MalilibApplyReport applied = applyMalilibToInstance(
                request.instanceRoot, entries, section.mode, request.replaceConfirmed);
            row.ok = applied.ok;
            row.blocked = !applied.ok;
            row.problem = applied.problem;
            row.written = applied.written;
            row.added = applied.added;
            row.removed = applied.removed;
            row.skippedItems = applied.skipped;
            row.details.append(applied.details);
            if (applied.typeMismatch > 0) {
                row.details.append(applyText("有 %1 格因为类型对不上被拒绝转换。")
                                       .arg(applied.typeMismatch));
            }
            result.rows.append(row);
            continue;
        }

        row.blocked = true;
        row.problem = applyText("这一段没有任何适配器接手，这一趟不动它。");
        result.rows.append(row);
    }

    // ---------- options.txt：最后写一次 ----------
    if (optionsTouched) {
        const QByteArray bytes = optionsTarget.toBytes();
        if (bytes != snapshot.options.toBytes()) {
            QString why;
            if (!writeWholeFile(optionsPath, bytes, &why)) {
                for (int index : optionsRowIndexes) {
                    PresetApplyRow &row = result.rows[index];
                    row.ok = false;
                    row.blocked = true;
                    row.problem = why;
                }
            }
        } else {
            // ★ 字节一个都没变（同一份预设连着落两趟就是这个样子）：适配器报的"N 项"是
            //   "这一段管着几个键"，不是"盘上改了几个键"。在这儿归零，让
            //   `wroteAnything()` 在"什么都没变"的那一趟说实话 —— 界面和日志都拿它判
            //   "这一趟办完了没有"，而"写了 2 项"在一份没动的文件上是假话。
            for (int index : optionsRowIndexes) {
                PresetApplyRow &row = result.rows[index];
                row.written = 0;
                row.added = 0;
                row.removed = 0;
            }
        }
    }

    for (const PresetApplyRow &row : result.rows) {
        result.written += row.written;
        result.removed += row.removed;
    }
    result.ok = true;
    return result;
}

}  // namespace Presets
