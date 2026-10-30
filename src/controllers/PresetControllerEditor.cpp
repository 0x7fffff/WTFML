#include "PresetController.hpp"
#include "PresetEditorPage.hpp"
#include "download/ResourceStore.hpp"
#include "manage/VersionManager.hpp"
#include "presets/GenericFiles.hpp"
#include "presets/PresetCapture.hpp"
#include "presets/PresetModTargets.hpp"
#include "presets/PresetWorkspace.hpp"
#include "util/AppPaths.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QLayout>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>

void PresetController::openEditor(const QString &id, bool draft) {
    if (m_editor && !leaveEditor())
        return;
    buildPage();
    QJsonObject state;
    if (!id.isEmpty()) {
        const bool isDraft = draft || id.startsWith("draft:");
        const auto key = id.startsWith("draft:") ? id.mid(6) : id;
        bool ok = false;
        const auto root = isDraft ? presetsRoot() + "/drafts" : presetsRoot();
        auto manifest = Presets::loadManifest(root, key, &ok);
        if (!ok) {
            m_page->showStatus(tr("无法读取预设，原文件保留。"));
            return;
        }
        state = Presets::editorDocument(manifest);
        state["_filesRoot"] = Presets::filesDir(root, key);
    } else {
        state["id"] = Presets::newPresetId();
        state["name"] = QString();
    }
    QSet<QString> editableSections;
    for (const auto &v : state.value("sections").toArray()) {
        const auto section = v.toObject();
        const auto kind = section.value("kind").toString();
        QString key;
        if (kind=="vanillaOptions" || kind=="vanillaKeys") key=kind;
        else if (kind=="genericFiles" && !section.value("items").toArray().isEmpty()) key=kind+"/"+section.value("items").toArray().first().toObject().value("path").toString().section('/',0,0);
        if (key.isEmpty()) continue;
        if (editableSections.contains(key)) {
            m_page->showStatus(tr("此旧预设含多段同类内容，暂不能在统一编辑器中合并编辑。原内容保留，可从实例重新提取一份草稿。"));
            return;
        }
        editableSections.insert(key);
    }
    m_editor = new PresetEditorPage(state, m_hostPage);
    m_hostPage->layout()->addWidget(m_editor);
    m_page->hide();
    m_editor->show();
    m_editor->saveRequested = [this](bool draft) { saveEditor(draft); };
    m_editor->backRequested = [this] { leaveEditor(); };
    m_editor->confirmReset = [this](const QString &text) {
        return m_editorPrompt &&
               m_editorPrompt(tr("放弃修改"), text, {tr("确认"), tr("取消")}) == 0;
    };
}
void PresetController::closeEditor() {
    if (!m_editor)
        return;
    auto *page = m_editor.data();
    m_editor = nullptr;
    m_hostPage->layout()->removeWidget(page);
    page->hide();
    page->deleteLater();
    m_page->show();
    refreshList(m_currentId);
}
void PresetController::saveEditor(bool draft, const std::function<void(bool)> &done) {
    if (!m_editor || m_editorSaving) {
        if (done)
            done(false);
        return;
    }
    m_editorSaving = true;
    m_editor->setEnabled(false);
    m_editor->showProblem(tr("正在保存…"));
    const auto editor = m_editor;
    const auto document = editor->document();
    const auto root = presetsRoot();
    auto *watcher = new QFutureWatcher<Presets::EditorSaveResult>(this);
    connect(watcher, &QFutureWatcher<Presets::EditorSaveResult>::finished, this,
            [this, watcher, editor, draft, done] {
                auto result = watcher->result();
                watcher->deleteLater();
                m_editorSaving = false;
                if (editor) {
                    editor->setEnabled(true);
                    if (result.ok) {
                        editor->markSaved(result.document);
                        editor->showProblem(draft ? tr("草稿已保存") : tr("预设已保存"));
                        m_currentId = draft ? QStringLiteral("draft:")+result.manifest.id : result.manifest.id;
                        refreshList(m_currentId);
                    } else
                        editor->showProblem(result.problem);
                }
                if (done)
                    done(result.ok);
            });
    watcher->setFuture(QtConcurrent::run(
        [root, document, draft] { return Presets::saveEditorDocument(root, document, draft); }));
}
bool PresetController::leaveEditor() {
    if (!m_editor)
        return true;
    if (m_editorSaving || m_editorPrompting)
        return false;
    if (!m_editor->isDirty()) {
        closeEditor();
        return true;
    }
    if (!m_editorPrompt)
        return false;
    m_editorPrompting = true;
    const int choice =
        m_editorPrompt(tr("未保存的修改"), tr("如何处理当前修改？"),
                       {tr("保存为草稿"), tr("保存为预设"), tr("不保存"), tr("取消")});
    m_editorPrompting = false;
    if (choice == 2) {
        closeEditor();
        return true;
    }
    if (choice != 0 && choice != 1)
        return false;
    QEventLoop loop;
    bool saved = false;
    saveEditor(choice == 0, [&](bool ok) {
        saved = ok;
        loop.quit();
    });
    if (m_editorSaving)
        loop.exec();
    if (!saved)
        return false;
    closeEditor();
    return true;
}

namespace {
struct EditorCapture {
    Presets::CaptureReport report;
    QMap<QString, QString> mods;
};
} // namespace
void PresetController::captureForEditor(const QString &instanceId) {
    if (instanceId.isEmpty() || !leaveEditor())
        return;
    if ((m_host.isRunning && m_host.isRunning(instanceId)) ||
        (m_host.isInstanceBusy && m_host.isInstanceBusy(instanceId))) {
        if (m_page)
            m_page->showStatus(tr("请先停止实例及其文件操作。"));
        return;
    }
    const auto id = Presets::newPresetId();
    const auto root = presetsRoot() + "/drafts";
    Presets::CaptureRequest request;
    request.instanceId = instanceId;
    request.instanceRoot = instanceRoot(instanceId);
    request.presetFilesRoot = Presets::filesDir(root, id);
    request.selection.config = false;
    request.selection.defaultConfigs = false;
    request.selection.malilib = false;
    request.selection.resourcePacks = true;
    request.selection.shaderPacks = true;
    request.selection.dataPacks = true;
    auto *watcher = new QFutureWatcher<EditorCapture>(this);
    if (m_page) {
        m_page->setBusy(true);
        m_page->showStatus(tr("正在提取实例设置与资源…"));
    }
    connect(
        watcher, &QFutureWatcher<EditorCapture>::finished, this,
        [this, watcher, id, root, instanceId] {
            auto capture = watcher->result();
            watcher->deleteLater();
            if (!capture.report.ok && capture.mods.isEmpty()) {
                if (m_page) {
                    m_page->setBusy(false);
                    m_page->showStatus(capture.report.problems.join('\n'));
                }
                return;
            }
            auto *store = new ResourceStore(this);
            if (m_versions)
                store->setModrinthApiBaseOverride(m_versions->modrinthApiRoot());
            struct State {
                QMap<QString, QString> projects;
                QStringList pending;
                bool done = false;
            };
            auto state = std::make_shared<State>();
            state->pending = capture.mods.keys();
            auto finish = [this, store, state, capture, id, root, instanceId] {
                if (state->done)
                    return;
                state->done = true;
                store->abortAll();
                store->deleteLater();
                if (m_page)
                    m_page->setBusy(false);
                Presets::PresetManifest draft;
                draft.id = id;
                draft.name = displayName(instanceId);
                draft.sections = capture.report.sections;
                draft.sourceInstanceId = instanceId;
                draft.createdAt = QDateTime::currentDateTime();
                draft.updatedAt = draft.createdAt;
                auto document = Presets::editorDocument(draft);
                QJsonArray refs;
                int unresolved = 0;
                for (auto it = capture.mods.begin(); it != capture.mods.end(); ++it) {
                    const auto project = state->projects.value(it.key());
                    if (project.isEmpty())
                        ++unresolved;
                    refs.append(QJsonObject{
                        {"platform", project.isEmpty() ? QString() : QStringLiteral("modrinth")},
                        {"projectId", project},
                        {"name", it.value()},
                        {"required", true}});
                }
                document["modReferences"] = refs;
                draft.editorState = document;
                if (!Presets::saveManifest(root, draft)) {
                    if (m_page)
                        m_page->showStatus(tr("无法保存提取的草稿。"));
                    return;
                }
                openEditor(id, true);
                if (m_editor) {
                    QStringList notes = capture.report.problems;
                    if (unresolved)
                        notes.prepend(
                            tr("%1 个模组未识别来源，请在模组页补充，或在概览中暂时禁用模组部分。")
                                .arg(unresolved));
                    m_editor->showProblem(notes.join('\n'));
                }
            };
            auto next = [store, state, finish] {
                if (state->pending.isEmpty()) {
                    finish();
                    return;
                }
                QStringList chunk;
                while (!state->pending.isEmpty() && chunk.size() < 100)
                    chunk.append(state->pending.takeFirst());
                store->fetchModrinthFiles(QStringLiteral("preset-capture"), chunk);
            };
            connect(store, &ResourceStore::modrinthFilesFinished, store,
                    [state, next](const QString &, const QHash<QString, QString> &projects,
                                  const QHash<QString, ResourceCatalog::VersionEntry> &,
                                  const QString &) {
                        for (auto it = projects.begin(); it != projects.end(); ++it)
                            state->projects.insert(it.key(), it.value());
                        if (!state->done)
                            next();
                    });
            QTimer::singleShot(45000, store, finish);
            if (m_page && !capture.mods.isEmpty())
                m_page->showStatus(tr("正在识别模组来源…"));
            next();
        });
    watcher->setFuture(QtConcurrent::run([request] {
        EditorCapture result;
        result.report = Presets::captureFromInstance(request);
        const auto entries =
            QDir(request.instanceRoot + "/mods")
                .entryInfoList({"*.jar"}, QDir::Files | QDir::NoSymLinks, QDir::Name);
        for (const auto &entry : entries) {
            QFile file(entry.filePath());
            if (!file.open(QIODevice::ReadOnly))
                continue;
            QCryptographicHash hash(QCryptographicHash::Sha1);
            if (hash.addData(&file))
                result.mods.insert(QString::fromLatin1(hash.result().toHex()),
                                   entry.completeBaseName());
        }
        return result;
    }));
}
