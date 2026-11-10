/**
 * @file AgentController.hpp
 * @brief 智能体 UI 与核心层的唯一接线点：会话、API 配置、流式聊天、停止/重试、加密。
 *
 * Stage 1A 不接任何工具、不启动 MCP、不读文件/日志。所有网络只走 `AgentModelClient`；
 * 所有凭据只走 `AgentSecretStore`；所有会话落盘只走 `AgentSessionStore`。
 */

#pragma once

#include "agent/AgentLoop.hpp"
#include "agent/AgentSessionStore.hpp"
#include "agent/AgentSettings.hpp"
#include "agent/AgentTypes.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

class AgentPage;
class AgentSettingsPage;
class AgentModelClient;
class OverlayHost;
class QTimer;

class AgentController : public QObject {
    Q_OBJECT

public:
    AgentController(AgentPage *page, OverlayHost *overlay, QObject *parent = nullptr);
    ~AgentController() override;

    /// 关窗/析构：取消在飞请求、停节流保存、把会话刷盘。必须有界。
    void shutdown();

    AgentPage *page() const { return m_page; }
    AgentSessionStore *sessionStore() { return &m_store; }
    AgentLoop *loop() { return m_loop; }

    /// 测试专用：换掉真实网络客户端（不转移所有权；调用方负责活得比控制器久）。
    void setModelClientForTest(AgentModelClient *client);
    /// 测试专用：不经过设置页直接摆一份可用的 profile/key。
    void applyProfileForTest(const AgentProviderProfile &profile, const QString &apiKey);
    void cancelForTest() { onStop(); }
    void sendForTest(const QString &text) { onSend(text); }
    QString currentSessionId() const { return m_session.meta.id; }

public slots:
    /// 切到别的选项卡时调用：Stage 1A 选择取消不可见的后台请求，避免悄悄花钱。
    void onPageHidden();

private:
    void wirePage();
    void loadInitialSession();
    void refreshSessions();
    void selectSession(const QString &sessionId);
    void updateHeader();
    void refreshApiState();
    void updateConfiguredState();
    void scheduleSave();
    void flushSave(bool snapshot);
    void upsertAssistant(const Agent::Message &message);
    void removeTrailingFailedAssistant();
    QString currentApiKey() const;
    QString configurationProblem() const;
    void openSettings();
    void onSettingsSave();
    void onSettingsTest();
    void onSettingsDeleteProfile();
    void onNewSession();
    void onSend(const QString &text);
    void onStop();
    void onRetry(const QString &messageId);
    void onRename(const QString &sessionId, const QString &title);
    void onPin(const QString &sessionId, bool pinned);
    void onDelete(const QString &sessionId);
    void onEncryptToggled(bool encrypted);

    AgentPage *m_page = nullptr;
    OverlayHost *m_overlay = nullptr;
    AgentSessionStore m_store;
    AgentSession m_session;
    AgentProviderProfile m_profile;
    QString m_testApiKey;
    AgentModelClient *m_client = nullptr;
    AgentModelClient *m_ownedClient = nullptr;
    AgentLoop *m_loop = nullptr;
    QPointer<AgentSettingsPage> m_settingsPage;
    QTimer *m_saveTimer = nullptr;
    bool m_configured = false;
    bool m_savePending = false;
    bool m_shuttingDown = false;
};
