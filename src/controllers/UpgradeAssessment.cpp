#include "UpgradeAssessment.hpp"
#include "InstanceSettingsDialog.hpp"
#include "manage/VersionManager.hpp"
#include "manage/VersionDisplay.hpp"
#include "manage/InstanceIconLookup.hpp"
#include "download/ResourceStore.hpp"
#include "operations/ModVersionDeclaration.hpp"
#include "operations/FabricVersionDeclaration.hpp"
#include "util/ZipUtil.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <memory>
#include <functional>

namespace {
struct Declaration { QString loader; QJsonValue game; QString basis; QJsonValue breaks; QString problem; };
struct Mod { QString path, name, sha1; QList<Declaration> declarations; QString problem, identityProblem; ResourceCatalog::Hit identity; bool identified = false; };
struct Scan { QList<Mod> mods; QString note; };
QJsonValue tomlGame(const QByteArray &bytes) {
    const QString text = QString::fromUtf8(bytes);
    auto blocks = QRegularExpression(QStringLiteral("(?ms)^\\s*\\[\\[dependencies\\.[^\\]]+\\]\\]\\s*(.*?)(?=^\\s*\\[|\\z)")).globalMatch(text);
    QStringList ranges;
    while (blocks.hasNext()) {
        const QString block = blocks.next().captured(1);
        if (!QRegularExpression(QStringLiteral("(?m)^\\s*modId\\s*=\\s*[\"']minecraft[\"']")).match(block).hasMatch()) continue;
        const auto type = QRegularExpression(QStringLiteral("(?m)^\\s*type\\s*=\\s*[\"']([^\"']+)[\"']")).match(block);
        if (type.hasMatch() && type.captured(1) != QStringLiteral("required")) return {};
        const auto range = QRegularExpression(QStringLiteral("(?m)^\\s*versionRange\\s*=\\s*[\"']([^\"']+)[\"']")).match(block);
        if (!range.hasMatch() || (!range.captured(1).startsWith('[') && !range.captured(1).startsWith('('))) return {};
        ranges.append(range.captured(1));
    }
    // Multiple bundled mods may have different requirements; do not pick one silently.
    ranges.removeDuplicates(); return ranges.size() == 1 ? QJsonValue(ranges.first()) : QJsonValue();
}
Scan scan(const QString &root, const QString &sourceId, const QString &cache, const std::shared_ptr<std::atomic_bool> &stop) {
    Scan result;
    const QDir mods(QDir(root).filePath(QStringLiteral("mods")));
    if (QFileInfo(mods.absolutePath()).isSymLink() || QFileInfo(mods.absolutePath()).isJunction()) { result.note = QObject::tr("模组目录是链接，未扫描。"); return result; }
    QStringList paths;
    for (const auto &file : mods.entryInfoList({QStringLiteral("*.jar")}, QDir::Files | QDir::NoSymLinks, QDir::Name)) {
        if (paths.size() >= 10000) { result.note = QObject::tr("模组超过 10000 个，列表已截断。"); break; }
        paths.append(file.absoluteFilePath());
    }
    InstanceIconLookup::Options options; options.cacheDir = cache; options.cacheKey = sourceId; options.cacheOnly = true;
    const auto cached = InstanceIconLookup::lookup(paths, options);
    QHash<QString, InstanceIconLookup::Found> identities;
    for (const auto &entry : cached.found) identities.insert(entry.path, entry);
    QHash<QString, QList<QJsonObject>> installedRecords;
    const QString auditPath = QDir(root).filePath(QStringLiteral("wtfml.preset-mod-install.json"));
    QFile audit(auditPath);
    if (!QFileInfo(auditPath).isSymLink() && audit.open(QIODevice::ReadOnly) && audit.size() <= 8 * 1024 * 1024) {
        const auto object = QJsonDocument::fromJson(audit.readAll()).object();
        if (object.value(QStringLiteral("format")).toString() == QStringLiteral("wtfml-preset-mod-result/1"))
            for (const auto &value : object.value(QStringLiteral("plannedFiles")).toArray()) {
                const auto record = value.toObject(); installedRecords[record.value(QStringLiteral("filename")).toString()].append(record);
            }
    }
    audit.close();
    QString batchProblem;
    const auto metadata = ZipUtil::readMetadataBatch(paths,
        {QStringLiteral("fabric.mod.json"), QStringLiteral("META-INF/neoforge.mods.toml"), QStringLiteral("META-INF/mods.toml")},
        &batchProblem, [stop] { return stop->load(); });
    QElapsedTimer identityClock; identityClock.start();
    for (const auto &path : paths) {
        if (stop->load()) break;
        Mod mod; mod.path = path; mod.name = QFileInfo(path).fileName();
        const QList<QPair<QString, QString>> files{{QStringLiteral("fabric.mod.json"), QStringLiteral("fabric")},
            {QStringLiteral("META-INF/neoforge.mods.toml"), QStringLiteral("neoforge")}, {QStringLiteral("META-INF/mods.toml"), QStringLiteral("forge")}};
        if (!metadata.contains(path)) mod.problem = batchProblem.isEmpty() ? QObject::tr("未检查：本轮未返回声明结果") : batchProblem;
        else mod.problem = metadata.value(path).problem;
        for (const auto &entry : files) {
            const auto record = metadata.value(path);
            if (record.entryProblems.contains(entry.first)) {
                mod.declarations.append({entry.second, {}, entry.first, {}, record.entryProblems.value(entry.first)});
                continue;
            }
            if (!record.entries.contains(entry.first)) continue;
            const QByteArray bytes = record.entries.value(entry.first);
            if (entry.second == QStringLiteral("fabric")) {
                QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse);
                const auto object = document.object();
                if (parse.error != QJsonParseError::NoError || !document.isObject()) {
                    mod.declarations.append({entry.second, {}, entry.first, {}, QObject::tr("Fabric 声明不是有效 JSON 对象")}); continue;
                }
                const auto rule = object.value(QStringLiteral("depends")).toObject().value(QStringLiteral("minecraft"));
                mod.declarations.append({entry.second, rule, entry.first + QStringLiteral(" / depends.minecraft"),
                    object.value(QStringLiteral("breaks")).toObject().value(QStringLiteral("minecraft")), {}});
                if (!object.value(QStringLiteral("name")).toString().isEmpty()) mod.name = object.value(QStringLiteral("name")).toString();
            } else mod.declarations.append({entry.second, tomlGame(bytes), entry.first + QStringLiteral(" / minecraft.versionRange"), {}, {}});
        }
        const auto identity = identities.constFind(path);
        const auto records = installedRecords.value(QFileInfo(path).fileName());
        if ((identity != identities.cend() && !identity->sha1.isEmpty()) || !records.isEmpty()) {
            QFile file(path); QCryptographicHash hash(QCryptographicHash::Sha1); bool good = file.open(QIODevice::ReadOnly);
            while (good && !file.atEnd()) {
                if (stop->load() || identityClock.elapsed() > 120000) { good = false; mod.identityProblem = QObject::tr("平台身份待校验：文件哈希检查超时"); break; }
                const auto data = file.read(1024 * 1024); if (data.isEmpty()) { good = false; break; } hash.addData(data);
            }
            const QString actualHash = QString::fromLatin1(hash.result().toHex());
            if (good) mod.sha1 = actualHash;
            if (good && identity != identities.cend() && actualHash == identity->sha1.toLower()) {
                mod.identity = identity->hit; mod.identified = !mod.identity.projectId.isEmpty();
            }
            if (good && !mod.identified) {
                QSet<QString> projects;
                for (const auto &record : records) if (record.value(QStringLiteral("sha1")).toString().toLower() == actualHash) {
                    const QString key = record.value(QStringLiteral("project")).toString();
                    if (key.startsWith(QStringLiteral("modrinth:")) || key.startsWith(QStringLiteral("curseforge:"))) projects.insert(key);
                }
                if (projects.size() == 1) {
                    const QString key = *projects.cbegin();
                    mod.identity.source = key.startsWith(QStringLiteral("curseforge:")) ? ResourceCatalog::Source::CurseForge : ResourceCatalog::Source::Modrinth;
                    mod.identity.projectId = key.mid(key.indexOf(':') + 1); mod.identified = !mod.identity.projectId.isEmpty();
                }
            }
        }
        result.mods.append(mod);
    }
    result.note += QObject::tr("仅检查顶层启用 JAR；按包内声明判断 Minecraft 范围，加载器版本、Java、嵌套模组及前置尚未完整验证。");
    return result;
}
struct State {
    Scan scan; bool ready = false, started = false; QString target, game, loader;
    QHash<QString, QString> platform; QList<int> pending;
    QHash<QString, int> active; QHash<int, int> attempts;
    QHash<int, QString> fallbackProjects; QSet<int> fallbackTried;
    int index = 0; quint64 generation = 0;
};
QString localState(const Mod &mod, const QString &game, const QString &loader, QString *basis) {
    if (game.isEmpty()) return QObject::tr("未选择目标");
    if (!mod.problem.isEmpty()) { *basis = mod.problem; return mod.problem.startsWith(QObject::tr("未检查")) ? QObject::tr("声明尚未检查") : QObject::tr("声明读取失败"); }
    if (loader.isEmpty()) { *basis = QObject::tr("纯原版不会加载模组"); return QObject::tr("目标不使用模组加载器"); }
    for (const auto &declaration : mod.declarations) if (declaration.loader == loader) {
        if (!declaration.problem.isEmpty()) { *basis = declaration.problem; return QObject::tr("声明读取失败"); }
        *basis = declaration.basis + QStringLiteral(": ") + QString::fromUtf8(QJsonDocument(QJsonArray{declaration.game}).toJson(QJsonDocument::Compact));
        const auto matchRule = [&declaration](const QJsonValue &rule, const QString &target) {
            if (declaration.loader != QStringLiteral("fabric")) return ModVersionDeclaration::matches(rule, target);
            const auto match = FabricVersionDeclaration::matches(rule, target);
            return match == FabricVersionDeclaration::Match::Yes ? ModVersionDeclaration::Match::Yes
                : match == FabricVersionDeclaration::Match::No ? ModVersionDeclaration::Match::No : ModVersionDeclaration::Match::Unknown;
        };
        if (!declaration.breaks.isNull() && !declaration.breaks.isUndefined()) {
            const auto excluded = matchRule(declaration.breaks, game);
            *basis += QStringLiteral("; breaks.minecraft=") + QString::fromUtf8(QJsonDocument(QJsonArray{declaration.breaks}).toJson(QJsonDocument::Compact));
            if (excluded == ModVersionDeclaration::Match::Yes) return QObject::tr("本地明确排除此版本");
            if (excluded == ModVersionDeclaration::Match::Unknown) return QObject::tr("排除范围无法确定");
        }
        if (declaration.game.isNull() || declaration.game.isUndefined()) return QObject::tr("未声明 Minecraft 版本范围");
        const auto match = matchRule(declaration.game, game);
        if (match == ModVersionDeclaration::Match::Yes) return QObject::tr("本地 Minecraft 声明符合");
        if (match == ModVersionDeclaration::Match::No) return QObject::tr("本地声明不支持");
        return QObject::tr("声明范围语法尚不支持");
    }
    *basis = QObject::tr("未找到 %1 的兼容声明文件").arg(loader); return QObject::tr("缺少目标加载器声明");
}
}

void UpgradeAssessment::attach(InstanceSettingsDialog *dialog, VersionManager *versions, ResourceStore *store,
                               const QString &sourceId, const QString &root, const QString &identityCache) {
    const auto state = std::make_shared<State>();
    const auto stop = std::make_shared<std::atomic_bool>(false);
    QObject::connect(dialog, &QObject::destroyed, [stop] { stop->store(true); });
    const auto render = [dialog, state] {
        QList<QStringList> rows;
        if (state->ready) for (const auto &mod : state->scan.mods) {
            QString basis; const QString local = localState(mod, state->game, state->loader, &basis);
            const QString platform = state->platform.value(mod.path, mod.identified ? QObject::tr("平台项目已识别，等待查询")
                : mod.identityProblem.isEmpty() ? QObject::tr("未识别平台项目（无有效识别记录）") : mod.identityProblem);
            const QString filename = QFileInfo(mod.path).fileName();
            const QString title = mod.name == filename ? mod.name : mod.name + QStringLiteral("（%1）").arg(filename);
            rows.append({title, platform + QLatin1Char('\n') + local, basis, filename,
                local == QObject::tr("本地 Minecraft 声明符合") ? QStringLiteral("keep")
                : (local == QObject::tr("本地声明不支持") || local == QObject::tr("本地明确排除此版本")
                   || local == QObject::tr("目标不使用模组加载器")) ? QStringLiteral("disable") : QStringLiteral("review"),
                local == QObject::tr("本地 Minecraft 声明符合") && platform.startsWith(QObject::tr("平台有目标发行版"))
                    ? QStringLiteral("compatible") : QString()});
        }
        const QString assumption = state->target.startsWith(QStringLiteral("@download:"))
            ? (dialog->property("modernUpgradeFlow").toBool()
                ? QObject::tr("按所选加载器 %1 检查。 ") : QObject::tr("目标核心未准备，暂按当前加载器 %1 评估；安装时仍须确认。 "))
                .arg(state->loader.isEmpty() ? QObject::tr("原版 / 未识别") : state->loader) : QString();
        dialog->setUpgradeModChecks(rows, state->ready ? assumption + QObject::tr("%1 个启用模组。%2").arg(rows.size()).arg(state->scan.note) : QObject::tr("正在后台读取本地声明…"));
    };
    const auto advance = std::make_shared<std::function<void()>>();
    *advance = [dialog, store, state, render] {
        // Qt network I/O is asynchronous: four simultaneous requests, not four GUI-blocking threads.
        while (state->active.size() < 4 && state->index < state->pending.size()) {
            const int index = state->pending.at(state->index++);
            const auto &mod = state->scan.mods.at(index);
            const int attempt = ++state->attempts[index];
            const QString key = QStringLiteral("upgrade-assess:%1:%2:%3:%4").arg(quintptr(dialog)).arg(state->generation).arg(index).arg(attempt);
            state->active.insert(key, index);
            state->platform[mod.path] = attempt > 1 ? QObject::tr("正在重试平台查询") : QObject::tr("正在查询平台");
            const QString fallback = state->fallbackProjects.value(index);
            store->fetchVersions(key, fallback.isEmpty() ? mod.identity.projectId : fallback,
                fallback.isEmpty() ? mod.identity.source : ResourceCatalog::Source::Modrinth, state->game, state->loader);
        }
        dialog->setProperty("upgradeAssessmentQuerying", !state->active.isEmpty() || state->index < state->pending.size());
        render();
    };
    QObject::connect(store, &ResourceStore::versionsFinished, dialog,
        [store, state, render, advance](const QString &key, const QList<ResourceCatalog::VersionEntry> &entries, const QString &problem) {
            if (!state->active.contains(key)) return;
            const int index = state->active.take(key);
            const auto &mod = state->scan.mods.at(index);
            bool matching = false, incomplete = false;
            for (const auto &entry : entries) {
                if (entry.gameVersions.isEmpty() || entry.loaders.isEmpty()) { incomplete = true; continue; }
                if (entry.gameVersions.contains(state->game) && entry.loaders.contains(state->loader, Qt::CaseInsensitive)) matching = true;
            }
            state->platform[mod.path] = !problem.isEmpty() ? QObject::tr("平台查询失败：%1").arg(problem)
                : matching ? QObject::tr("平台有目标发行版") : incomplete ? QObject::tr("平台发行元数据不完整，无法确定") : QObject::tr("平台未找到目标发行版");
            if (problem.isEmpty() && state->fallbackProjects.contains(index)) state->platform[mod.path] += QObject::tr("（Modrinth 后备）");
            // Retry one transient failure; permanent authentication/permission errors are not retried.
            if (!problem.isEmpty()) {
                qWarning().noquote() << "[InstanceUpdate]" << mod.identity.projectId << problem;
                if (mod.identity.source == ResourceCatalog::Source::CurseForge && !mod.sha1.isEmpty() && !state->fallbackTried.contains(index)) {
                    state->fallbackTried.insert(index);
                    const QString lookupKey = key + QStringLiteral(":hash-fallback");
                    state->active.insert(lookupKey, index);
                    state->platform[mod.path] = QObject::tr("原平台查询失败，正在通过文件哈希核验 Modrinth 后备项目");
                    render(); store->fetchModrinthFiles(lookupKey, {mod.sha1}); (*advance)(); return;
                }
                const bool permanent = problem.contains(QStringLiteral("403")) || problem.contains(QStringLiteral("401"))
                    || problem.contains(QStringLiteral("404")) || problem.contains(QStringLiteral("429"));
                if (!permanent && state->attempts.value(index) < 2) state->pending.append(index);
            }
            render(); (*advance)();
        });
    QObject::connect(store, &ResourceStore::modrinthFilesFinished, dialog,
        [state, render, advance](const QString &key, const QHash<QString, QString> &projects,
            const QHash<QString, ResourceCatalog::VersionEntry> &, const QString &problem) {
            if (!key.endsWith(QStringLiteral(":hash-fallback")) || !state->active.contains(key)) return;
            const int index = state->active.take(key);
            const auto &mod = state->scan.mods.at(index);
            const QString project = projects.value(mod.sha1);
            if (problem.isEmpty() && !project.isEmpty()) {
                state->fallbackProjects.insert(index, project); state->attempts[index] = 0; state->pending.append(index);
            } else {
                state->platform[mod.path] = QObject::tr("原平台查询失败；Modrinth 后备身份%1，仍需确认")
                    .arg(problem.isEmpty() ? QObject::tr("未匹配") : QObject::tr("查询失败：%1").arg(problem));
                if (!problem.isEmpty()) qWarning().noquote() << "[InstanceUpdate] hash fallback:" << problem;
            }
            render(); (*advance)();
        });
    const auto select = [dialog, store, state, versions, sourceId, render, advance](const QString &key, const QString &target) {
        const QString selectedLoader = dialog->property("upgradeTargetLoader").toString();
        if (key != QStringLiteral("upgrade") || (target == state->target && state->ready
            && (!dialog->property("modernUpgradeFlow").toBool() || selectedLoader == state->loader))) return;
        state->target = target; state->platform.clear(); state->pending.clear(); state->attempts.clear(); state->index = 0; state->active.clear(); ++state->generation;
        state->fallbackProjects.clear(); state->fallbackTried.clear();
        dialog->setProperty("upgradeAssessmentQuerying", false);
        store->abortAll();
        state->game.clear(); state->loader.clear();
        if (target.startsWith(QStringLiteral("@download:"))) {
            state->game = target.mid(10);
            state->loader = dialog->property("modernUpgradeFlow").toBool() ? selectedLoader
                : versions->displayParts(sourceId).loaderName.toLower();
        }
        else if (!target.isEmpty()) {
            const auto parts = versions->displayParts(target);
            state->game = parts.realVersion.isEmpty() ? parts.id : parts.realVersion;
            state->loader = parts.loaderName.toLower();
        }
        render();
        if (!state->ready || state->game.isEmpty() || state->loader.isEmpty()) return;
        for (int i = 0; i < state->scan.mods.size(); ++i) if (state->scan.mods.at(i).identified) state->pending.append(i);
        dialog->setProperty("upgradeAssessmentQuerying", !state->pending.isEmpty());
        const auto generation = state->generation;
        QTimer::singleShot(300, dialog, [state, advance, generation] { if (generation == state->generation) (*advance)(); });
        QTimer::singleShot(180000, dialog, [dialog, store, state, render, generation] {
            if (generation != state->generation || !dialog->property("upgradeAssessmentQuerying").toBool()) return;
            for (const int i : state->active) state->platform[state->scan.mods.at(i).path] = QObject::tr("平台查询超时，不能视为无适配版");
            state->active.clear();
            for (int i = state->index; i < state->pending.size(); ++i) state->platform[state->scan.mods.at(state->pending.at(i)).path] = QObject::tr("平台查询超时，不能视为无适配版");
            state->pending.clear(); store->abortAll(); render();
            dialog->setProperty("upgradeAssessmentQuerying", false);
        });
    };
    QObject::connect(dialog, &InstanceSettingsDialog::stopUpgradeQueryRequested, dialog, [dialog, store, state, render] {
        for (const int i : state->active) state->platform[state->scan.mods.at(i).path] = QObject::tr("平台查询已停止，结果未确认");
        state->active.clear(); ++state->generation;
        for (int i = state->index; i < state->pending.size(); ++i)
            state->platform[state->scan.mods.at(state->pending.at(i)).path] = QObject::tr("平台查询已停止，结果未确认");
        state->pending.clear(); store->abortAll(); dialog->setProperty("upgradeAssessmentQuerying", false); render();
    });
    QObject::connect(dialog, &InstanceSettingsDialog::transferTargetChosen, dialog, select);
    auto *refresh = dialog->findChild<QPushButton *>(QStringLiteral("instanceUpgradeRecheck"));
    const auto startScan = [dialog, store, state, root, sourceId, identityCache, stop, select, render, refresh] {
        if (state->started && !state->ready) return;
        state->started = true; state->ready = false; state->active.clear(); ++state->generation; store->abortAll();
        dialog->setProperty("upgradeAssessmentReading", true); render();
        dialog->setProperty("upgradeAssessmentQuerying", false);
        if (refresh) refresh->setEnabled(false);
        auto *watcher = new QFutureWatcher<Scan>(dialog);
        QObject::connect(watcher, &QFutureWatcher<Scan>::finished, dialog, [watcher, dialog, state, select, refresh] {
            state->scan = watcher->result(); state->ready = true; state->target = QStringLiteral("@invalid");
            dialog->setProperty("upgradeAssessmentReading", false);
            if (refresh) refresh->setEnabled(true);
            select(QStringLiteral("upgrade"), dialog->transferTarget(QStringLiteral("upgrade"))); watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([root, sourceId, identityCache, stop] { return scan(root, sourceId, identityCache, stop); }));
    };
    if (refresh) QObject::connect(refresh, &QPushButton::clicked, dialog, startScan);
    QObject::connect(dialog, &InstanceSettingsDialog::transferTargetChosen, dialog,
        [state, startScan](const QString &key, const QString &) {
            if (key != QStringLiteral("upgrade") || state->started) return;
            startScan();
        });
}
