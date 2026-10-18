#include "PresetModInstaller.hpp"
#include "download/DownloadManager.hpp"
#include "download/ResourceStore.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimer>
#include <QRegularExpression>
#include <QJsonDocument>

namespace Presets {
namespace {
bool redirected(const QString &path) {
    const QFileInfo info(path);
    return info.isSymLink() || info.isJunction();
}
}
PresetModInstaller::PresetModInstaller(QObject *parent, ResourceStore *store) : QObject(parent), m_store(store) {}

void PresetModInstaller::start(const QList<ModReference> &references, const QString &root,
    const QString &gameVersion, const QString &loader, const QString &apiKey,
    std::function<bool()> running, std::function<void(Result)> completed) {
    m_root = root;
    m_running = std::move(running);
    m_completed = std::move(completed);
    m_request.gameVersion = gameVersion;
    m_request.loader = loader;
    for (const auto &ref : references) {
        ModResolveInput input;
        input.reference = ref;
        if ((!ref.gameVersion.isEmpty() && ref.gameVersion != gameVersion) ||
            (!ref.loader.isEmpty() && ref.loader.compare(loader, Qt::CaseInsensitive) != 0)) {
            input.queried = true;
            input.queryFailed = true;
            input.queryProblem = tr("预设限定的游戏版本或加载器与目标实例不符，未查询其它版本替代。");
            m_answers.insert(ref.key(), input);
        }
        m_request.references.append(input);
    }
    if (references.isEmpty()) { m_result.ok = true; finish(); return; }
    if (gameVersion.isEmpty() || loader.isEmpty() || loader == QStringLiteral("vanilla")) {
        finish(tr("没有可确认的游戏版本或模组加载器，未下载模组。")); return;
    }
    if (!m_store) m_store = new ResourceStore(this);
    m_store->setCurseForgeApiKey(apiKey);
    connect(m_store, &ResourceStore::versionsFinished, this,
        [this](const QString &key, const QList<ResourceCatalog::VersionEntry> &versions, const QString &problem) {
            if (m_done || key != m_waiting) return;
            auto &answer = m_answers[key];
            answer.queried = true;
            answer.queryFailed = !problem.isEmpty();
            answer.queryProblem = problem;
            answer.versions = versions;
            m_waiting.clear();
            QTimer::singleShot(0, this, [this] { resolveNext(); });
        });
    QTimer::singleShot(600000, this, [this] { if (!m_done) { cancel(); } });
    resolveNext();
}

void PresetModInstaller::resolveNext() {
    if (m_done) return;
    if (m_running && m_running()) { finish(tr("游戏正在运行，已停止应用模组预设。")); return; }
    // Recalculate dependency closure after each answer. Only one platform request is in flight.
    for (auto &input : m_request.references) {
        const auto reference = input.reference;
        if (m_answers.contains(reference.key())) input = m_answers.value(reference.key());
        input.reference = reference; // A dependency lookup must not demote an explicit required root.
    }
    for (auto it = m_answers.cbegin(); it != m_answers.cend(); ++it) {
        if (it->queried && !it->queryFailed) m_request.dependencyAnswers[it.key()] = it->versions;
    }
    m_plan = planModInstall(m_request);
    // Honour exact dependency versions present in the platform answers instead of
    // choosing the newest dependency and then unnecessarily refusing the install.
    for (int pass = 0; pass < 8; ++pass) {
        QHash<QString, QString> pins;
        for (const auto &row : m_plan.downloads()) {
            for (const auto &dependency : row.version.dependencies) {
                if (!dependency.required() || dependency.versionId.isEmpty() || dependency.projectId.isEmpty()) continue;
                const QString key = row.reference.platform + QLatin1Char(':') + dependency.projectId;
                if (pins.contains(key) && pins.value(key) != dependency.versionId) {
                    finish(tr("多个模组要求不同的精确前置版本，未安装：%1").arg(dependency.projectId)); return;
                }
                pins.insert(key, dependency.versionId);
            }
        }
        bool changed = false;
        for (const auto &row : m_plan.downloads())
            if (pins.contains(row.key) && row.version.id != pins.value(row.key)) changed = true;
        if (!changed) break;
        for (auto it = pins.cbegin(); it != pins.cend(); ++it) {
            QList<ResourceCatalog::VersionEntry> matching;
            for (const auto &version : m_answers.value(it.key()).versions)
                if (version.id == it.value()) matching.append(version);
            if (m_request.dependencyAnswers.contains(it.key())) m_request.dependencyAnswers[it.key()] = matching;
            for (auto &input : m_request.references)
                if (input.reference.key() == it.key()) input.versions = matching;
        }
        m_plan = planModInstall(m_request);
        if (pass == 7) { finish(tr("精确前置约束未收敛，未安装模组。")); return; }
    }
    for (const auto &row : m_plan.rows) {
        if (row.key.isEmpty() || m_answers.contains(row.key)) continue;
        if (m_answers.size() >= 256) { finish(tr("模组依赖超过本次安全检查上限（256 项）。")); return; }
        ModResolveInput answer;
        answer.reference = row.reference;
        m_answers.insert(row.key, answer);
        ResourceCatalog::Source source;
        if (!ResourceCatalog::sourceFromKey(row.reference.platform, &source)) {
            m_answers[row.key].queried = true;
            m_answers[row.key].queryFailed = true;
            m_answers[row.key].queryProblem = tr("无法识别的模组平台。");
            QTimer::singleShot(0, this, [this] { resolveNext(); }); return;
        }
        m_waiting = row.key;
        m_store->fetchVersions(row.key, row.reference.projectId, source,
            row.reference.gameVersion.isEmpty() ? m_request.gameVersion : row.reference.gameVersion,
            row.reference.loader.isEmpty() ? m_request.loader : row.reference.loader);
        return;
    }
    for (const auto &row : m_plan.rows) if (row.isFailure()) m_result.lines.append(row.title + QStringLiteral("：") + row.problem);
    m_result.optionalFailures = m_plan.optionalFailures();
    if (!m_plan.ok) { finish(tr("必要模组或前置缺失、未知或冲突；未写入模组文件。")); return; }
    for (const auto &row : m_plan.downloads()) {
        // Never deploy an optional parent whose necessary dependency failed. Until
        // component-level attribution is available, conservatively skip optional rows.
        if (!row.required && m_result.optionalFailures > 0) continue;
        for (const auto &dependency : row.version.dependencies) {
            if (dependency.required() && !dependency.versionId.isEmpty()) {
                bool pinnedMatch = false;
                for (const auto &candidate : m_plan.downloads())
                    if (candidate.reference.platform == row.reference.platform &&
                        candidate.reference.projectId == dependency.projectId &&
                        candidate.version.id == dependency.versionId) pinnedMatch = true;
                if (!pinnedMatch) {
                    finish(tr("模组「%1」要求精确前置版本，当前解析结果不满足，未写入模组。") .arg(row.title)); return;
                }
            }
        }
        m_downloadRows.append(row);
    }
    download();
}

void PresetModInstaller::download() {
    if (!QDir::isAbsolutePath(m_root) || redirected(m_root) ||
        redirected(QDir(m_root).filePath(QStringLiteral("mods")))) {
        finish(tr("实例或模组目录不安全，未写入文件。")); return;
    }
    m_stage = std::make_unique<QTemporaryDir>();
    if (!m_stage->isValid()) { finish(tr("无法创建模组下载暂存目录。")); return; }
    m_download = new DownloadManager(this);
    m_download->setUseMirror(false);
    m_download->setMaxConcurrentDownloads(4);
    connect(m_download, &DownloadManager::allDownloadsFinished, this, [this](bool) {
        if (!m_done) QTimer::singleShot(0, this, [this] { if (!m_done) commit(); });
    });
    m_download->beginBatch();
    const auto rows = m_downloadRows;
    for (int i = 0; i < rows.size(); ++i) {
        const auto &file = rows[i].file;
        const QUrl url(file.url);
        // A platform filename is untrusted; never use it as a path during download.
        if (!url.isValid() || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")) ||
            !url.userInfo().isEmpty() ||
            file.filename != QFileInfo(file.filename).fileName() || file.filename.contains(QLatin1Char('\\')) ||
            file.filename.contains(QLatin1Char(':')) || !file.filename.endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive) ||
            !QRegularExpression(QStringLiteral("^[0-9a-fA-F]{40}$")).match(file.sha1).hasMatch()) continue;
        m_download->downloadFile(file.url, m_stage->filePath(QString::number(i)), file.sha1, file.size);
    }
    m_download->endBatch();
}

void PresetModInstaller::commit() {
    const auto rows = m_downloadRows;
    bool skipOptional = false;
    // Necessary downloads must all verify before any staged file is installed.
    for (int i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        const auto verified = verifyDownloadedFile(m_stage->filePath(QString::number(i)), row.file.size, row.file.sha1);
        if ((!verified.ok || verified.unverified) && row.required) {
            finish(tr("必要模组「%1」未通过下载校验，未写入模组文件。保存文件名或校验信息也可能无效。").arg(row.title)); return;
        }
        if ((!verified.ok || verified.unverified) && !row.required) skipOptional = true;
    }
    if (skipOptional) {
        for (const auto &row : rows) if (!row.required) ++m_result.optionalFailures;
        m_result.lines.append(tr("部分可选文件下载失败；为避免缺少前置，本次可选模组与可选前置一并跳过。"));
    }
    if ((m_running && m_running()) || redirected(m_root) ||
        redirected(QDir(m_root).filePath(QStringLiteral("mods")))) {
        finish(tr("实例状态已变化，未写入模组。")); return;
    }
    QDir mods(QDir(m_root).filePath(QStringLiteral("mods")));
    if (!QDir().mkpath(mods.path())) { finish(tr("无法创建模组目录。")); return; }
    QHash<QString, QString> names;
    for (int i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        if (skipOptional && !row.required) continue;
        if (!QFile::exists(m_stage->filePath(QString::number(i)))) continue;
        const QString name = row.file.filename.toCaseFolded();
        if (names.contains(name) && names.value(name) != row.file.sha1.toLower()) {
            finish(tr("多个模组使用相同文件名但内容不同，未写入：%1").arg(row.file.filename)); return;
        }
        names.insert(name, row.file.sha1.toLower());
        const auto target = mods.filePath(row.file.filename);
        if (QFileInfo(target).isSymLink() ||
            (QFileInfo::exists(target) && !verifyDownloadedFile(target, row.file.size, row.file.sha1).ok)) {
            finish(tr("同名模组已存在且内容不同，未覆盖：%1").arg(row.file.filename)); return;
        }
    }
    for (int i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        if (skipOptional && !row.required) continue;
        const auto path = m_stage->filePath(QString::number(i));
        const auto verified = verifyDownloadedFile(path, row.file.size, row.file.sha1);
        if (!verified.ok || verified.unverified) { ++m_result.optionalFailures; continue; }
        const auto target = mods.filePath(row.file.filename);
        if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
            if (verifyDownloadedFile(target, row.file.size, row.file.sha1).ok) continue;
            finish(tr("同名模组已存在且内容不同，未覆盖：%1").arg(row.file.filename)); return;
        }
        QFile input(path);
        QSaveFile output(target);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
            finish(tr("无法写入模组：%1").arg(row.file.filename)); return;
        }
        while (!input.atEnd()) {
            const auto bytes = input.read(65536);
            if (bytes.isEmpty() || output.write(bytes) != bytes.size()) {
                output.cancelWriting(); finish(tr("模组写入失败：%1").arg(row.file.filename)); return;
            }
        }
        if (!output.commit()) { finish(tr("无法提交模组文件：%1").arg(row.file.filename)); return; }
        ++m_result.installed;
    }
    m_result.ok = m_result.optionalFailures == 0;
    m_result.lines.prepend(tr("模组预设：安装 %1 项，可选项失败/跳过 %2 项。")
                          .arg(m_result.installed).arg(m_result.optionalFailures));
    finish();
}

void PresetModInstaller::cancel() {
    if (m_done) return;
    m_result.cancelled = true;
    finish(tr("模组预设应用已取消或超时；基础实例保留。"));
}
void PresetModInstaller::finish(const QString &problem) {
    if (m_done) return;
    m_done = true;
    if (!problem.isEmpty()) { m_result.ok = false; m_result.lines.prepend(problem); }
    if (!problem.isEmpty() && m_result.installed > 0)
        m_result.lines.append(tr("失败前已写入 %1 个模组；基础实例和已写入文件保留。").arg(m_result.installed));
    if (m_download) m_download->cancelBatch();
    // Local audit record only: no launch-time enforcement or automatic reapplication.
    if (QDir::isAbsolutePath(m_root) && QDir(m_root).exists() && !redirected(m_root)) {
        const QString path = QDir(m_root).filePath(QStringLiteral("wtfml.preset-mod-install.json"));
        QJsonArray references, files, lines;
        for (const auto &input : m_request.references) references.append(modReferenceToJson(input.reference));
        for (const auto &row : m_downloadRows) files.append(QJsonObject{
            {"project", row.key}, {"versionId", row.version.id}, {"filename", row.file.filename}, {"sha1", row.file.sha1}});
        for (const auto &line : m_result.lines) lines.append(line);
        const QByteArray bytes = QJsonDocument(QJsonObject{{"format", "wtfml-preset-mod-result/1"},
            {"finishedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"gameVersion", m_request.gameVersion},
            {"loader", m_request.loader}, {"selection", m_selectionSnapshot}, {"references", references},
            {"plannedFiles", files}, {"success", m_result.ok}, {"cancelled", m_result.cancelled},
            {"installed", m_result.installed}, {"optionalFailures", m_result.optionalFailures}, {"lines", lines}}).toJson();
        QSaveFile record(path);
        if (QFileInfo(path).isSymLink() || !record.open(QIODevice::WriteOnly) ||
            record.write(bytes) != bytes.size() || !record.commit()) {
            m_result.ok = false;
            m_result.lines.append(tr("预设执行记录保存失败；请保留本次任务报告。"));
        }
    }
    if (m_completed) m_completed(m_result);
    deleteLater();
}
}
