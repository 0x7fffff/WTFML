/**
 * @file AgentController.cpp
 * @brief 智能体页的接线与状态流转（见 .hpp）。
 */

#include "AgentController.hpp"

#include "agent/AgentPaths.hpp"
#include "util/AppPaths.hpp"
#include "agent/AgentProviderAdapters.hpp"
#include "agent/AgentRedaction.hpp"
#include "agent/AgentSecretStore.hpp"
#include "agent/AgentSkill.hpp"
#include "AgentComposer.hpp"
#include "AgentMessageView.hpp"
#include "AgentPage.hpp"
#include "AgentSettingsPage.hpp"
#include "AgentToolCard.hpp"
#include "InlineMessage.hpp"
#include "agent/OpenAiCompatibleClient.hpp"
#include "OverlayHost.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUuid>


namespace {

AgentView::MessageStatus viewStatus(Agent::MessageStatus status) {
    switch (status) {
    case Agent::MessageStatus::Streaming: return AgentView::MessageStatus::Streaming;
    case Agent::MessageStatus::Interrupted: return AgentView::MessageStatus::Interrupted;
    case Agent::MessageStatus::Error: return AgentView::MessageStatus::Error;
    case Agent::MessageStatus::ToolPending: return AgentView::MessageStatus::ToolPending;
    case Agent::MessageStatus::AwaitingApproval: return AgentView::MessageStatus::AwaitingApproval;
    case Agent::MessageStatus::Complete: break;
    }
    return AgentView::MessageStatus::Complete;
}

AgentView::Message viewMessage(const Agent::Message &message) {
    AgentView::Message view;
    view.id = message.id;
    view.user = message.role == Agent::Role::User;
    view.content = message.content;
    view.reasoning = message.reasoning;
    view.status = viewStatus(message.status);
    view.errorText = message.errorText;
    return view;
}

QList<AgentView::Message> viewMessages(const QList<Agent::Message> &messages) {
    QList<AgentView::Message> views;
    views.reserve(messages.size());
    for (const Agent::Message &message : messages) {
        if (message.role == Agent::Role::Tool) continue;  // 工具结果用 tool card 呈现
        views.append(viewMessage(message));
    }
    return views;
}

AgentView::SessionMeta viewMeta(const AgentSessionMeta &meta) {
    AgentView::SessionMeta view;
    view.id = meta.id;
    view.title = meta.title;
    view.updatedAt = meta.updatedAt;
    view.pinned = meta.pinned;
    view.encrypted = meta.encrypted;
    view.corrupt = meta.corrupt;
    return view;
}

QList<AgentView::SessionMeta> viewMetas(const QList<AgentSessionMeta> &metas) {
    QList<AgentView::SessionMeta> views;
    views.reserve(metas.size());
    for (const AgentSessionMeta &meta : metas) views.append(viewMeta(meta));
    return views;
}

AgentView::ProviderDraft viewProfile(const AgentProviderProfile &profile) {
    AgentView::ProviderDraft draft;
    draft.id = profile.id;
    draft.name = profile.name;
    draft.baseUrl = profile.baseUrl;
    draft.model = profile.model;
    draft.contextWindow = profile.contextWindow;
    draft.maxOutput = profile.maxOutput;
    draft.temperature = profile.temperature;
    draft.timeoutSec = profile.timeoutSec;
    draft.deployment = profile.deployment;
    draft.authMode = profile.authMode;
    draft.stream = profile.stream;
    draft.maxTokensField = profile.maxTokensField;
    draft.systemRole = profile.systemRole;
    draft.reasoningEcho = profile.reasoningEcho;
    draft.sendTemperature = profile.sendTemperature;
    draft.sendToolChoice = profile.sendToolChoice;
    draft.localHttpAllowed = profile.localHttpAllowed;
    return draft;
}

AgentProviderProfile coreProfile(const AgentView::ProviderDraft &draft) {
    AgentProviderProfile profile;
    profile.id = draft.id;
    profile.name = draft.name;
    profile.baseUrl = draft.baseUrl;
    profile.model = draft.model;
    profile.contextWindow = draft.contextWindow;
    profile.maxOutput = draft.maxOutput;
    profile.temperature = draft.temperature;
    profile.timeoutSec = draft.timeoutSec;
    profile.deployment = draft.deployment;
    profile.authMode = draft.authMode;
    profile.stream = draft.stream;
    profile.maxTokensField = draft.maxTokensField;
    profile.systemRole = draft.systemRole;
    profile.reasoningEcho = draft.reasoningEcho;
    profile.sendTemperature = draft.sendTemperature;
    profile.sendToolChoice = draft.sendToolChoice;
    profile.localHttpAllowed = draft.localHttpAllowed;
    return profile;
}

}  // namespace

AgentController::AgentController(AgentPage *page, OverlayHost *overlay, QObject *parent)
    : QObject(parent),
      m_page(page),
      m_overlay(overlay),
      m_store(),
      m_artifacts(),
      m_backups(),
      m_files(&m_backups) {
    QString problem;
    if (!AgentPaths::ensureDataDir(&problem) && m_page != nullptr) {
        m_page->showNotice(problem);
    }
    m_ownedClient = new OpenAiCompatibleClient(this);
    m_client = m_ownedClient;
    m_loop = new AgentLoop(m_client, this);
    m_toolEngine = new AgentFileToolEngine(
        &m_files, &m_artifacts, [this](QString *problem) { return toolContext(problem); });
    m_toolEngine->setStrictEgress(AgentSettings::strictEgress());
    m_loop->setToolEngine(m_toolEngine);
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(2000);
    connect(m_saveTimer, &QTimer::timeout, this, [this] {
        if (m_savePending) flushSave(false);
    });

    const QList<AgentProviderProfile> profiles = AgentSettings::profiles();
    const QString active = AgentSettings::activeProfileId();
    for (const AgentProviderProfile &profile : profiles) {
        if (profile.id == active) {
            m_profile = profile;
            break;
        }
    }
    if (!m_profile.isValid() && !profiles.isEmpty()) {
        m_profile = profiles.constFirst();
    }
    if (m_profile.id.isEmpty()) {
        m_profile.id = QStringLiteral("default");
        m_profile.name = QStringLiteral("默认模型服务");
        m_profile.baseUrl = QStringLiteral("https://api.openai.com/v1");
        m_profile.model = QStringLiteral("gpt-4o-mini");
    }

    wirePage();
    connect(m_loop, &AgentLoop::stateChanged, this, [this](AgentLoop::State state) {
        const bool busy = state == AgentLoop::State::Preparing ||
                          state == AgentLoop::State::Requesting;
        if (m_page != nullptr) m_page->setStreaming(busy);
        if (busy) refreshApiState();
    });
    connect(m_loop, &AgentLoop::assistantUpdated, this,
            [this](const Agent::Message &message) { upsertAssistant(message); });
    connect(m_loop, &AgentLoop::turnFinished, this, [this](const Agent::Message &message) {
        upsertAssistant(message);
        m_session.pendingApproval = {};
        m_saveTimer->stop();
        flushSave(true);
        if (m_page != nullptr) {
            m_page->setStreaming(false);
            m_page->hideApproval();
            if (!message.errorText.isEmpty()) m_page->showNotice(message.errorText);
        }
        refreshSessions();
    });
    connect(m_loop, &AgentLoop::turnFailed, this, [this](const Agent::Error &error) {
        m_session.pendingApproval = {};
        if (m_page != nullptr) {
            m_page->setStreaming(false);
            m_page->hideApproval();
            m_page->showNotice(error.message);
        }
    });
    connect(m_loop, &AgentLoop::usageUpdated, this, [this](const Agent::Usage &usage) {
        m_session.usage = usage;
    });
    connect(m_loop, &AgentLoop::messageAdded, this, [this](const Agent::Message &message) {
        if (message.role == Agent::Role::Tool) {
            m_session.messages.append(message);
            scheduleSave();
        }
    });
    connect(m_loop, &AgentLoop::toolCallStarted, this, [this](const Agent::ToolCall &call) {
        if (m_page == nullptr) return;
        AgentView::ToolCard card;
        card.id = call.id;
        card.title = call.name;
        card.summary = QStringLiteral("参数已接收");
        card.detail = QString::fromUtf8(
            QJsonDocument(call.arguments).toJson(QJsonDocument::Indented));
        card.status = AgentView::ToolCardStatus::Running;
        m_page->upsertToolCard(card);
    });
    connect(m_loop, &AgentLoop::toolCallFinished, this,
            [this](const Agent::ToolCall &call, const AgentToolResult &result) {
                if (m_page == nullptr) return;
                if (result.data.value(QStringLiteral("autoReadNotice")).toBool(false)) {
                    const QString host = QUrl(m_profile.baseUrl).host();
                    if (m_autoNoticeShownFor != host) {
                        m_autoNoticeShownFor = host;
                        m_page->showNotice(
                            QStringLiteral("本次自动读取的日志/元数据可能发送给当前模型服务：%1")
                                .arg(host));
                    }
                }
                AgentView::ToolCard card;
                card.id = call.id;
                card.title = call.name;
                card.summary = result.summary;
                card.detail = QString::fromUtf8(
                    QJsonDocument(result.data).toJson(QJsonDocument::Indented));
                card.status = result.status == AgentToolResult::Status::Ok
                                  ? AgentView::ToolCardStatus::Done
                                  : AgentView::ToolCardStatus::Error;
                m_page->upsertToolCard(card);
            });
    connect(m_loop, &AgentLoop::approvalRequested, this,
            [this](const QString &approvalId, const Agent::ToolCall &call,
                   const QJsonObject &request) {
                Q_UNUSED(call);
                m_session.pendingApproval = request;
                m_session.pendingApproval.insert(QStringLiteral("approvalId"), approvalId);
                flushSave(true);
                if (m_page != nullptr) m_page->showApproval(m_session.pendingApproval);
            });

    loadInitialSession();
    refreshApiState();
}

AgentController::~AgentController() { shutdown(); }

void AgentController::wirePage() {
    if (m_page == nullptr) return;
    connect(m_page, &AgentPage::newSessionRequested, this, &AgentController::onNewSession);
    connect(m_page, &AgentPage::sessionSelected, this, &AgentController::selectSession);
    connect(m_page, &AgentPage::sendRequested, this, &AgentController::onSend);
    connect(m_page, &AgentPage::stopRequested, this, &AgentController::onStop);
    connect(m_page, &AgentPage::settingsRequested, this, &AgentController::openSettings);
    connect(m_page, &AgentPage::retryRequested, this, &AgentController::onRetry);
    connect(m_page, &AgentPage::sessionRenameRequested, this, &AgentController::onRename);
    connect(m_page, &AgentPage::sessionPinRequested, this, &AgentController::onPin);
    connect(m_page, &AgentPage::sessionDeleteRequested, this, &AgentController::onDelete);
    connect(m_page, &AgentPage::encryptToggled, this, &AgentController::onEncryptToggled);
    connect(m_page, &AgentPage::approvalAllowed, this, [this](const QString &) { onApproval(true); });
    connect(m_page, &AgentPage::approvalDenied, this, [this](const QString &) { onApproval(false); });
    connect(m_page, &AgentPage::instanceChipRequested, this, &AgentController::onInstanceMenuRequested);
    connect(m_page, &AgentPage::instanceChosen, this, &AgentController::onInstanceChosen);
    connect(m_page, &AgentPage::sessionExportRequested, this, &AgentController::onExport);
}

void AgentController::shutdown() {
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    if (m_saveTimer != nullptr) m_saveTimer->stop();
    if (m_loop != nullptr && m_loop->busy()) {
        m_loop->cancel();
    }
    if (!m_session.meta.id.isEmpty()) {
        m_store.save(m_session, false, nullptr);
    }
    if (m_settingsPage != nullptr) {
        m_settingsPage->close();
    }
    if (m_toolEngine != nullptr) {
        m_toolEngine->cancelPending();
        if (m_loop != nullptr) m_loop->setToolEngine(nullptr);
        delete m_toolEngine;
        m_toolEngine = nullptr;
    }
}

void AgentController::setModelClientForTest(AgentModelClient *client) {
    if (client == nullptr || client == m_client) return;
    if (m_loop->busy()) m_loop->cancel();
    if (m_ownedClient != nullptr) {
        m_ownedClient->deleteLater();
        m_ownedClient = nullptr;
    }
    m_client = client;
    m_loop->setClient(client);
}

void AgentController::applyProfileForTest(const AgentProviderProfile &profile,
                                          const QString &apiKey) {
    m_profile = profile;
    m_testApiKey = apiKey;
    refreshApiState();
}

QString AgentController::currentApiKey() const {
    if (!m_testApiKey.isEmpty()) return m_testApiKey;
    if (m_profile.id.isEmpty()) return {};
    return AgentSecretStore::read(m_profile.id).secret;
}

void AgentController::refreshApiState() {
    QString status;
    if (!m_profile.isValid()) {
        m_configured = false;
        status = QStringLiteral("API 未配置");
    } else {
        QString problem;
        const bool valid = AgentProviderAdapters::validateProfile(m_profile, &problem);
        const QString key = currentApiKey();
        const bool keyOk = !key.isEmpty() || m_profile.authMode == QLatin1String("none");
        m_configured = valid && keyOk;
        if (!valid) {
            status = QStringLiteral("配置无效：%1").arg(problem);
        } else if (!keyOk) {
            status = QStringLiteral("API 未配置（缺少 API Key）");
        } else if (m_profile.authMode == QLatin1String("none")) {
            status = QStringLiteral("本地服务（无认证）");
        } else {
            status = QStringLiteral("API 已配置");
        }
    }
    if (m_page != nullptr) {
        m_page->setConfigured(m_configured, status);
        const QString modelLabel =
            m_profile.isValid() ? QStringLiteral("%1 · %2").arg(m_profile.name, m_profile.model)
                                : QStringLiteral("未配置模型");
        m_page->setModelLabel(modelLabel);
    }
}

void AgentController::loadInitialSession() {
    QStringList problems;
    m_store.finalizePendingDeletions(&problems);
    QList<AgentSessionMeta> metas;
    m_store.list(&metas, nullptr);
    if (!problems.isEmpty() && m_page != nullptr) {
        m_page->showNotice(problems.join(QLatin1Char('\n')));
    }
    if (metas.isEmpty()) {
        m_session = {};
        if (m_page != nullptr) {
            m_page->setSessions({});
            m_page->setCurrentSession(QString());
            m_page->showEmptyState(true);
        }
        return;
    }
    if (m_page != nullptr) m_page->setSessions(viewMetas(metas));
    selectSession(metas.constFirst().id);
}

void AgentController::refreshSessions() {
    QList<AgentSessionMeta> metas;
    m_store.list(&metas, nullptr);
    if (m_page != nullptr) m_page->setSessions(viewMetas(metas));
}

void AgentController::selectSession(const QString &sessionId) {
    if (sessionId.isEmpty()) return;
    if (m_loop->busy()) {
        m_loop->cancel();
        flushSave(true);
    }
    AgentSession loaded;
    QString problem;
    if (!m_store.load(sessionId, &loaded, &problem)) {
        if (m_page != nullptr) m_page->showNotice(problem);
        refreshSessions();
        return;
    }
    m_session = loaded;
    if (!m_session.pendingApproval.isEmpty()) {
        // 重启/切回后旧审批不能自动执行，只能作废并重新请求。
        m_session.pendingApproval = {};
        m_store.save(m_session, false, nullptr);
        if (m_page != nullptr) {
            m_page->showNotice(QStringLiteral("上次未完成的审批已作废；需要重新请求并确认。"));
        }
    }
    m_savePending = false;
    m_saveTimer->stop();
    updateHeader();
}

void AgentController::updateHeader() {
    if (m_page == nullptr) return;
    m_page->setCurrentSession(m_session.meta.id);
    m_page->setTitle(m_session.meta.title);
    m_page->setMessages(viewMessages(m_session.messages));
    m_page->setToolCards(toolCardsFromHistory());
    if (!m_session.pendingApproval.isEmpty()) {
        m_page->showApproval(m_session.pendingApproval);
    } else {
        m_page->hideApproval();
    }
    m_page->setEncryptionChecked(m_session.meta.encrypted);
    m_page->setStreaming(m_loop->busy());
    updateInstanceChip();
}

void AgentController::updateConfiguredState() {
    refreshApiState();
    if (m_page != nullptr) {
        m_page->setConfigured(m_configured, m_configured ? QStringLiteral("API 已配置")
                                                        : QStringLiteral("API 未配置"));
    }
}

void AgentController::scheduleSave() {
    m_savePending = true;
    if (m_saveTimer != nullptr && !m_saveTimer->isActive()) m_saveTimer->start();
}

void AgentController::flushSave(bool snapshot) {
    if (m_session.meta.id.isEmpty()) return;
    m_saveTimer->stop();
    m_savePending = false;
    QString problem;
    if (!m_store.save(m_session, snapshot, &problem)) {
        if (m_page != nullptr) m_page->showNotice(problem);
    }
}

void AgentController::upsertAssistant(const Agent::Message &message) {
    if (message.id.isEmpty()) return;
    bool replaced = false;
    for (Agent::Message &existing : m_session.messages) {
        if (existing.id == message.id) {
            existing = message;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        m_session.messages.append(message);
    }
    if (m_page != nullptr) m_page->upsertMessage(viewMessage(message));
    scheduleSave();
}

void AgentController::removeTrailingFailedAssistant() {
    while (!m_session.messages.isEmpty() &&
           m_session.messages.constLast().role == Agent::Role::Assistant &&
           m_session.messages.constLast().status != Agent::MessageStatus::Complete) {
        const QString id = m_session.messages.constLast().id;
        m_session.messages.removeLast();
        if (m_page != nullptr) m_page->removeMessage(id);
    }
}

QString AgentController::configurationProblem() const {
    QString problem;
    if (!AgentProviderAdapters::validateProfile(m_profile, &problem)) {
        return problem;
    }
    if (currentApiKey().isEmpty() && m_profile.authMode != QLatin1String("none")) {
        return QStringLiteral("没有可用的 API Key，请先在设置里配置。");
    }
    return {};
}

void AgentController::onNewSession() {
    if (m_loop->busy()) {
        m_loop->cancel();
        flushSave(true);
    }
    AgentSession created;
    QString problem;
    if (!m_store.create(&created, &problem)) {
        if (m_page != nullptr) m_page->showNotice(problem);
        return;
    }
    m_session = created;
    refreshSessions();
    updateHeader();
    refreshApiState();
}

void AgentController::onSend(const QString &text) {
    if (m_loop->busy()) return;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;

    refreshApiState();
    const QString problem = configurationProblem();
    if (!problem.isEmpty()) {
        if (m_page != nullptr) m_page->showNotice(problem);
        openSettings();
        return;
    }
    if (m_session.meta.id.isEmpty()) {
        AgentSession created;
        QString createProblem;
        if (!m_store.create(&created, &createProblem)) {
            if (m_page != nullptr) m_page->showNotice(createProblem);
            return;
        }
        m_session = created;
    }

    removeTrailingFailedAssistant();
    const AgentRedaction::Result redacted = AgentRedaction::redact(trimmed);
    if (redacted.containsCredentials()) {
        if (m_page != nullptr) {
            m_page->showNotice(QStringLiteral("检测到疑似凭据（%1），已在本机遮挡后再发送。")
                                   .arg(redacted.kinds.join(QStringLiteral("、"))));
        }
    }
    const QString content = redacted.text;
    if (content.trimmed().isEmpty()) {
        if (m_page != nullptr) m_page->showNotice(QStringLiteral("消息只包含凭据，已取消发送。"));
        return;
    }

    Agent::Message user;
    user.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    user.role = Agent::Role::User;
    user.content = content;
    user.status = Agent::MessageStatus::Complete;
    user.createdAt = QDateTime::currentDateTimeUtc();
    m_session.messages.append(user);
    if (m_session.meta.title.isEmpty() || m_session.meta.title == QStringLiteral("新对话")) {
        m_session.meta.title = AgentSessionTitle::fromMessage(content);
    }
    m_session.meta.provider = QJsonObject{{QStringLiteral("profileId"), m_profile.id},
                                          {QStringLiteral("model"), m_profile.model},
                                          {QStringLiteral("deployment"), m_profile.deployment}};
    if (m_page != nullptr) {
        m_page->upsertMessage(viewMessage(user));
        m_page->setTitle(m_session.meta.title);
        m_page->composer()->clear();
        m_page->showNotice(QString());
    }
    flushSave(true);
    refreshSessions();

    AgentLoop::Config config;
    config.profile = m_profile;
    config.apiKey = currentApiKey();
    config.systemPrompt = AgentSkillRegistry::find(QStringLiteral("general")).systemPrompt;
    m_loop->setConfig(config);
    m_loop->setHistory(m_session.messages);
    m_loop->start();
}

void AgentController::onStop() {
    if (!m_loop->busy()) return;
    m_loop->cancel();
    m_saveTimer->stop();
    flushSave(true);
    if (m_page != nullptr) {
        m_page->setStreaming(false);
        m_page->showNotice(QStringLiteral("已停止生成，已保留收到的部分。"));
    }
    refreshSessions();
}

void AgentController::onRetry(const QString &messageId) {
    if (m_loop->busy()) return;
    if (m_session.messages.isEmpty()) return;
    const Agent::Message last = m_session.messages.constLast();
    if (last.id != messageId || last.role != Agent::Role::Assistant) return;
    removeTrailingFailedAssistant();
    flushSave(false);
    AgentLoop::Config config;
    config.profile = m_profile;
    config.apiKey = currentApiKey();
    config.systemPrompt = AgentSkillRegistry::find(QStringLiteral("general")).systemPrompt;
    m_loop->setConfig(config);
    m_loop->setHistory(m_session.messages);
    m_loop->start();
}

void AgentController::onRename(const QString &sessionId, const QString &title) {
    QString problem;
    if (!m_store.rename(sessionId, title, &problem)) {
        if (m_page != nullptr) m_page->showNotice(problem);
        return;
    }
    if (m_session.meta.id == sessionId) {
        m_session.meta.title = title.simplified().isEmpty() ? QStringLiteral("新对话")
                                                            : title.simplified().left(80);
        if (m_page != nullptr) m_page->setTitle(m_session.meta.title);
    }
    refreshSessions();
}

void AgentController::onPin(const QString &sessionId, bool pinned) {
    QString problem;
    if (!m_store.setPinned(sessionId, pinned, &problem)) {
        if (m_page != nullptr) m_page->showNotice(problem);
        return;
    }
    if (m_session.meta.id == sessionId) m_session.meta.pinned = pinned;
    refreshSessions();
}

void AgentController::onDelete(const QString &sessionId) {
    if (sessionId.isEmpty()) return;
    const int choice = InlineMessage::ask(
        m_overlay, MessageKind::Question, QStringLiteral("删除会话"),
        QStringLiteral("只会删除会话正文、恢复快照和本会话 artifacts；"
                       "文件维护备份会保留。确定删除吗？"),
        QStringList{QStringLiteral("删除"), QStringLiteral("取消")}, 1, 0);
    if (choice != 0) return;

    if (m_session.meta.id == sessionId && m_loop->busy()) {
        m_loop->cancel();
        if (m_toolEngine != nullptr && m_toolEngine->committing()) {
            if (m_page != nullptr) {
                m_page->showNotice(QStringLiteral("文件正在提交，无法安全停止，暂不删除会话。"));
            }
            return;
        }
    }
    QString problem;
    if (!m_store.requestDeletion(sessionId, &problem) ||
        !m_store.completeDeletion(sessionId, &problem, nullptr)) {
        if (m_page != nullptr) m_page->showNotice(problem);
        return;
    }
    if (m_session.meta.id == sessionId) {
        m_session = {};
        m_savePending = false;
        m_saveTimer->stop();
    }
    QList<AgentSessionMeta> metas;
    m_store.list(&metas, nullptr);
    if (metas.isEmpty()) {
        if (m_page != nullptr) {
            m_page->setSessions({});
            m_page->setCurrentSession(QString());
            m_page->clearMessages();
            m_page->setTitle(QStringLiteral("新对话"));
        }
    } else {
        selectSession(metas.constFirst().id);
    }
    refreshSessions();
}

void AgentController::onEncryptToggled(bool encrypted) {
    if (m_session.meta.id.isEmpty()) return;
    QString problem;
    if (!m_store.setEncrypted(m_session.meta.id, encrypted, &problem)) {
        if (m_page != nullptr) {
            m_page->showNotice(problem);
            m_page->setEncryptionChecked(m_session.meta.encrypted);
        }
        return;
    }
    AgentSession reloaded;
    if (m_store.load(m_session.meta.id, &reloaded, &problem)) {
        m_session = reloaded;
    }
    if (m_page != nullptr) {
        m_page->setEncryptionChecked(encrypted);
        m_page->showNotice(encrypted ? QStringLiteral("本会话已加密存储（绑定当前 Windows 用户）")
                                     : QStringLiteral("本会话已改回明文存储"));
    }
    refreshSessions();
}

bool AgentController::deleteSessionForTest(const QString &sessionId) {
    if (sessionId.isEmpty()) return false;
    if (m_loop->busy()) m_loop->cancel();
    if (m_toolEngine != nullptr && m_toolEngine->committing()) return false;
    QString problem;
    if (!m_store.requestDeletion(sessionId, &problem) ||
        !m_store.completeDeletion(sessionId, &problem, nullptr)) {
        return false;
    }
    if (m_session.meta.id == sessionId) {
        m_session = {};
        m_savePending = false;
        m_saveTimer->stop();
    }
    QList<AgentSessionMeta> metas;
    m_store.list(&metas, nullptr);
    if (metas.isEmpty()) {
        if (m_page != nullptr) {
            m_page->setSessions({});
            m_page->setCurrentSession(QString());
            m_page->clearMessages();
        }
    } else {
        selectSession(metas.constFirst().id);
    }
    refreshSessions();
    return true;
}

void AgentController::setInstanceContextProvider(InstanceProvider provider) {
    m_instanceProvider = std::move(provider);
    m_useTestInstance = false;
    updateInstanceChip();
}

void AgentController::setInstanceListProvider(InstanceListProvider provider) {
    m_instanceListProvider = std::move(provider);
}

void AgentController::setInstanceContextForTest(const QString &rootPath,
                                                const AgentInstanceInfo &info) {
    m_testRootPath = rootPath;
    m_testInstance = info;
    m_useTestInstance = true;
    updateInstanceChip();
}

void AgentController::approveToolForTest(bool approved) { onApproval(approved); }

AgentToolContext AgentController::toolContext(QString *problem) {
    AgentToolContext context;
    context.sessionId = m_session.meta.id;
    context.providerHost = QUrl(m_profile.baseUrl).host();
    context.strictEgress = AgentSettings::strictEgress();
    if (context.sessionId.isEmpty()) {
        if (problem != nullptr) *problem = QStringLiteral("先新建会话再使用文件工具。");
        return context;
    }
    const AgentInstanceInfo current = homeInstance();
    if (!m_session.meta.instanceLock.isValid()) {
        if (!current.isValid() || !current.exists) {
            if (problem != nullptr) {
                *problem = QStringLiteral("NeedsInstanceLock：先在主页选择一个实例；不会自动挑一个。");
            }
            return context;
        }
        QString lockProblem;
        if (!AgentInstanceLocks::lock(&m_session, current, &lockProblem)) {
            if (problem != nullptr) *problem = lockProblem;
            return context;
        }
        QString saveProblem;
        if (!m_store.save(m_session, true, &saveProblem)) {
            m_session.meta.instanceLock = {};
            if (problem != nullptr) *problem = QStringLiteral("实例锁定写盘失败：%1").arg(saveProblem);
            return context;
        }
        if (m_page != nullptr) {
            m_page->showNotice(QStringLiteral("本会话已锁定实例：%1").arg(current.name));
        }
        refreshSessions();
        updateInstanceChip();
    }
    if (!AgentInstanceLocks::isUsable(m_session.meta.instanceLock, lockedInstance())) {
        if (problem != nullptr) {
            *problem = QStringLiteral("InstanceUnavailable：锁定实例不在当前游戏目录或已被删除。");
        }
        return context;
    }
    context.instanceId = m_session.meta.instanceLock.id;
    context.instanceName = m_session.meta.instanceLock.name;
    context.gameRootId = m_session.meta.instanceLock.gameRootId;
    context.rootPath = m_useTestInstance ? m_testRootPath : AppPaths::versionDir(context.instanceId);
    if (!QFileInfo::exists(context.rootPath)) {
        if (problem != nullptr) *problem = QStringLiteral("InstanceUnavailable：锁定实例目录不存在。");
    }
    return context;
}

AgentInstanceInfo AgentController::homeInstance() const {
    if (m_useTestInstance) return m_testInstance;
    return m_instanceProvider ? m_instanceProvider() : AgentInstanceInfo{};
}

QList<AgentInstanceInfo> AgentController::instances() const {
    if (!m_useTestInstance && m_instanceListProvider) return m_instanceListProvider();
    // 没接实例清单（测试 / 早期构造）时，可选的只有主页当前那一个。
    const AgentInstanceInfo home = homeInstance();
    return home.isValid() ? QList<AgentInstanceInfo>{home} : QList<AgentInstanceInfo>{};
}

AgentInstanceInfo AgentController::lockedInstance() const {
    const AgentInstanceLock &lock = m_session.meta.instanceLock;
    for (const AgentInstanceInfo &info : instances())
        if (info.id == lock.id) return info;
    return {};
}

void AgentController::updateInstanceChip() {
    if (m_page == nullptr) return;
    const AgentInstanceLock &lock = m_session.meta.instanceLock;
    m_page->setInstanceChip(AgentInstanceLocks::chipText(lock, lock.isValid() ? lockedInstance() : homeInstance()),
                            lock.isValid());
}

void AgentController::onInstanceMenuRequested() {
    if (m_page == nullptr) return;
    QList<AgentView::InstanceOption> options;
    for (const AgentInstanceInfo &info : instances())
        if (info.isValid() && info.exists) options.append({info.id, info.name.isEmpty() ? info.id : info.name});
    m_page->showInstanceMenu(options, m_session.meta.instanceLock.id);
}

void AgentController::onInstanceChosen(const QString &instanceId) {
    if (m_page == nullptr) return;
    if (m_loop->busy() || !m_session.pendingApproval.isEmpty()) {
        m_page->showNotice(QStringLiteral("有待确认或执行中的操作，暂不能切换实例。"));
        return;
    }
    const AgentInstanceLock old = m_session.meta.instanceLock;
    if (instanceId == old.id) return;
    AgentInstanceInfo target;
    if (!instanceId.isEmpty()) {
        for (const AgentInstanceInfo &info : instances())
            if (info.id == instanceId) target = info;
        if (!target.isValid() || !target.exists) {
            m_page->showNotice(QStringLiteral("实例「%1」已不在当前游戏目录。").arg(instanceId));
            return;
        }
    }
    // 已关联过实例、对话里也有了内容：旧实例上的证据和计划不再适用，建议另开会话（D8：切换须显式确认）。
    if (old.isValid() && !m_session.messages.isEmpty() && m_overlay != nullptr) {
        const int choice = InlineMessage::ask(
            m_overlay, MessageKind::Question, QStringLiteral("切换实例"),
            QStringLiteral("本会话的记录属于「%1」，切换后旧计划作废，建议新建会话。").arg(old.name),
            QStringList{QStringLiteral("新建会话"), QStringLiteral("在本会话切换"), QStringLiteral("取消")}, 2, 0);
        if (choice == 0) onNewSession();
        else if (choice != 1) return;
    }
    if (m_session.meta.id.isEmpty()) {
        AgentSession created;
        QString createProblem;
        if (!m_store.create(&created, &createProblem)) {
            m_page->showNotice(createProblem);
            return;
        }
        m_session = created;
    }
    QString problem;
    if (target.isValid()) {
        if (!AgentInstanceLocks::switchTo(&m_session, target, &problem)) {
            m_page->showNotice(problem);
            return;
        }
    } else {
        m_session.meta.instanceLock = {};  // 跟随主页：下一次实例操作时再锁定主页当前实例
    }
    m_session.pendingApproval = {};
    flushSave(true);
    refreshSessions();
    updateHeader();
    m_page->showNotice(target.isValid() ? QStringLiteral("本会话已关联实例：%1").arg(target.name)
                                        : QStringLiteral("本会话改为跟随主页实例"));
}

void AgentController::onExport(const QString &sessionId) {
    if (m_page == nullptr || sessionId.isEmpty()) return;
    AgentSession session = m_session;
    QString problem;
    if (session.meta.id != sessionId && !m_store.load(sessionId, &session, &problem)) {
        InlineMessage::warning(m_overlay, QStringLiteral("导出失败"), problem);
        return;
    }
    QString name = session.meta.title.isEmpty() ? QStringLiteral("新对话") : session.meta.title;
    name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    const QString path = QFileDialog::getSaveFileName(
        m_page->window(), QStringLiteral("导出会话"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)).filePath(name + QStringLiteral(".md")),
        QStringLiteral("Markdown (*.md);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(AgentSessionExport::toMarkdown(session).toUtf8()) < 0 || !file.commit()) {
        InlineMessage::warning(m_overlay, QStringLiteral("导出失败"),
                               QStringLiteral("无法写入 %1：%2。请换一个位置再试。")
                                   .arg(QDir::toNativeSeparators(path), file.errorString()));
        return;
    }
    m_page->showNotice(QStringLiteral("会话已导出：%1").arg(QDir::toNativeSeparators(path)));
}

void AgentController::onApproval(bool approved) {
    if (m_session.meta.id.isEmpty() || m_session.pendingApproval.isEmpty()) return;
    const QString approvalId =
        m_session.pendingApproval.value(QStringLiteral("approvalId")).toString();
    m_session.pendingApproval = {};
    flushSave(true);
    if (m_page != nullptr) m_page->hideApproval();
    m_loop->resolveApproval(approved);
}

QList<AgentView::ToolCard> AgentController::toolCardsFromHistory() const {
    QList<AgentView::ToolCard> cards;
    for (const Agent::Message &message : m_session.messages) {
        if (message.role != Agent::Role::Tool) continue;
        AgentView::ToolCard card;
        card.id = message.toolCallId.isEmpty() ? message.id : message.toolCallId;
        card.title = message.toolName;
        QJsonObject body = QJsonDocument::fromJson(message.content.toUtf8()).object();
        card.summary = body.value(QStringLiteral("summary")).toString();
        const QJsonObject data = body.value(QStringLiteral("data")).toObject();
        card.detail = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented));
        const bool ok = body.value(QStringLiteral("ok")).toBool(false);
        card.status = ok ? AgentView::ToolCardStatus::Done : AgentView::ToolCardStatus::Error;
        cards.append(card);
    }
    return cards;
}

void AgentController::showToolCardsFromHistory() {
    if (m_page != nullptr) m_page->setToolCards(toolCardsFromHistory());
}

void AgentController::onPageHidden() {
    if (m_loop->busy()) onStop();
}

void AgentController::openSettings() {
    if (m_overlay == nullptr || m_page == nullptr) return;
    // 覆盖层被别的页占着（典型：在别的选项卡上开了页再切过来，那页收着、仍占着这一位）时 showPage() 直接返回
    // false，按钮就「点了没反应」。用户亲手点的入口按全窗规矩（MainWindow::runWhenOverlayFree）：静默收掉占着的
    // 那一层、排到下一轮再开；这一层只认自己的 overlay，不依赖 MainWindow，所以在这里做同一件事。
    if (m_overlay->isOpen()) {
        if (!m_settingsOpenQueued && m_overlay->evict()) {
            m_settingsOpenQueued = true;
            QTimer::singleShot(0, this, [this] {
                m_settingsOpenQueued = false;
                openSettings();
            });
        }
        return;
    }
    if (m_settingsPage != nullptr) {
        // 已经开着/收着：把值重摆一遍再让它露头。
        m_settingsPage->setProfile(viewProfile(m_profile), AgentSecretStore::hasSecret(m_profile.id),
                                   AgentSecretStore::read(m_profile.id).backend ==
                                           AgentSecretStore::Backend::DpapiFallback
                                       ? QStringLiteral("dpapi-fallback")
                                       : QStringLiteral("credential-manager"));
        m_settingsPage->setStrictEgress(AgentSettings::strictEgress());
        m_overlay->showPage(m_settingsPage, QStringLiteral("API 与模型设置"));
        return;
    }
    auto *page = new AgentSettingsPage(nullptr);
    m_settingsPage = page;
    const AgentSecretStore::ReadResult secret = AgentSecretStore::read(m_profile.id);
    page->setProfile(viewProfile(m_profile), secret.ok,
                     secret.backend == AgentSecretStore::Backend::DpapiFallback
                         ? QStringLiteral("dpapi-fallback")
                         : QStringLiteral("credential-manager"));
    page->setStrictEgress(AgentSettings::strictEgress());
    connect(page, &AgentSettingsPage::saveRequested, this, &AgentController::onSettingsSave);
    connect(page, &AgentSettingsPage::testRequested, this, &AgentController::onSettingsTest);
    connect(page, &AgentSettingsPage::deleteProfileRequested, this,
            &AgentController::onSettingsDeleteProfile);
    connect(page, &AgentSettingsPage::clearKeyRequestedSignal, this, [page] {
        page->setStatus(QStringLiteral("保存后清除；不会再回显旧 Key。"));
    });
    if (!m_overlay->showPage(page, QStringLiteral("API 与模型设置"))) {
        page->deleteLater();
        m_settingsPage = nullptr;
    }
}

void AgentController::onSettingsSave() {
    AgentSettingsPage *page = m_settingsPage;
    if (page == nullptr) return;
    AgentProviderProfile profile = coreProfile(page->profile());
    profile.id = m_profile.id.isEmpty() ? QStringLiteral("default") : m_profile.id;
    QString problem;
    if (!AgentProviderAdapters::validateProfile(profile, &problem) ||
        !profile.validateEnumFields(&problem)) {
        page->setStatus(problem, true);
        return;
    }

    const QString newKey = page->apiKey();
    if (!newKey.isEmpty()) {
        AgentSecretStore::Backend backend = AgentSecretStore::Backend::Unavailable;
        QString keyProblem;
        if (!AgentSecretStore::write(profile.id, newKey, &backend, &keyProblem)) {
            // 两条路都失败：保留旧 key，也不保存这次改动，避免出现“配置变了但 key 没变”的错觉。
            page->setStatus(keyProblem, true);
            return;
        }
        profile.storageBackend = AgentSecretStore::backendKey(backend);
        m_testApiKey.clear();
    } else if (page->clearKeyRequested()) {
        QString keyProblem;
        if (!AgentSecretStore::clear(profile.id, &keyProblem)) {
            page->setStatus(keyProblem, true);
            return;
        }
        m_testApiKey.clear();
    }

    if (!AgentSettings::saveProfile(profile, &problem)) {
        page->setStatus(problem, true);
        return;
    }
    AgentSettings::setActiveProfileId(profile.id);
    AgentSettings::setStrictEgress(page->strictEgress());
    if (m_toolEngine != nullptr) m_toolEngine->setStrictEgress(page->strictEgress());
    m_profile = profile;
    refreshApiState();
    refreshSessions();
    page->setStatus(QStringLiteral("已保存。"), false);
    page->accept();
}

void AgentController::onSettingsTest() {
    AgentSettingsPage *page = m_settingsPage;
    if (page == nullptr) return;
    AgentProviderProfile profile = coreProfile(page->profile());
    profile.id = m_profile.id.isEmpty() ? QStringLiteral("default") : m_profile.id;
    QString problem;
    if (!AgentProviderAdapters::validateProfile(profile, &problem)) {
        page->setTestResult(problem, false);
        return;
    }
    QString key = page->apiKey();
    if (key.isEmpty()) {
        key = AgentSecretStore::read(profile.id).secret;
    }
    if (key.isEmpty() && profile.authMode != QLatin1String("none")) {
        page->setTestResult(QStringLiteral("没有可用的 API Key；先填写或保存后再测试。"), false);
        return;
    }
    const QString requestId = QStringLiteral("test-") +
                              QUuid::createUuid().toString(QUuid::WithoutBraces);
    page->setTestResult(QStringLiteral("正在测试连接…"), true);
    connect(m_client, &AgentModelClient::connectionTested, page,
            [page, requestId](const QString &id, bool ok, int status, const QString &summary) {
                if (id != requestId) return;
                Q_UNUSED(status);
                page->setTestResult(summary, ok);
            },
            Qt::SingleShotConnection);
    Agent::ModelRequest request;
    request.requestId = requestId;
    request.baseUrl = profile.baseUrl;
    request.model = profile.model;
    request.apiKey = key;
    request.authMode = profile.authMode;
    request.localHttpAllowed = profile.localHttpAllowed;
    request.timeoutSec = profile.timeoutSec;
    m_client->testConnection(request);
}

void AgentController::onSettingsDeleteProfile() {
    if (m_overlay == nullptr) return;
    const int choice = InlineMessage::ask(
        m_overlay, MessageKind::Question, QStringLiteral("删除模型配置"),
        QStringLiteral("会删除这份 provider 配置和它保存的 API Key（凭据管理器/DPAPI 后备）。"
                       "会话本身保留。确定吗？"),
        QStringList{QStringLiteral("删除"), QStringLiteral("取消")}, 1, 0);
    if (choice != 0) return;
    QString problem;
    if (!AgentSecretStore::clear(m_profile.id, &problem) ||
        !AgentSettings::removeProfile(m_profile.id, &problem)) {
        if (m_settingsPage != nullptr) m_settingsPage->setStatus(problem, true);
        return;
    }
    m_profile = AgentProviderProfile{};
    m_testApiKey.clear();
    refreshApiState();
    if (m_settingsPage != nullptr) {
        const AgentSecretStore::ReadResult secret = AgentSecretStore::read(m_profile.id);
        m_settingsPage->setProfile(viewProfile(m_profile), secret.ok, QString());
        m_settingsPage->setStatus(QStringLiteral("配置已删除。"), false);
        m_settingsPage->accept();
    }
}
