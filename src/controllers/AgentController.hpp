/**
 * @file AgentController.hpp
 * @brief 智能体 UI 与核心层的唯一接线点：会话、API 配置、流式聊天、停止/重试、加密、实例关联、导出。
 *
 * 文件工具只作用于本会话锁定的实例（Stage 1B，写入逐次确认）；不启动 MCP。所有网络只走 `AgentModelClient`；
 * 所有凭据只走 `AgentSecretStore`；所有会话落盘只走 `AgentSessionStore`。
 */

#pragma once

#include "agent/AgentArtifactStore.hpp"
#include "agent/AgentBackupIndex.hpp"
#include "agent/AgentFileToolEngine.hpp"
#include "agent/AgentFileTools.hpp"
#include "agent/AgentInstanceLock.hpp"
#include "agent/AgentLoop.hpp"
#include "agent/AgentSessionStore.hpp"
#include "agent/AgentSettings.hpp"
#include "agent/AgentTypes.hpp"

#include "AgentViewTypes.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

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
    AgentFileToolEngine *toolEngine() { return m_toolEngine; }

    /// 主窗口提供“当前主页选中的实例 + 游戏根身份”；首次实例工具调用时锁定。
    using InstanceProvider = std::function<AgentInstanceInfo()>;
    void setInstanceContextProvider(InstanceProvider provider);
    /// 当前游戏目录下的全部实例（「实例」下拉的选项；锁定的实例是否还在也按它判）。
    using InstanceListProvider = std::function<QList<AgentInstanceInfo>()>;
    void setInstanceListProvider(InstanceListProvider provider);

    /// 测试专用：固定实例根目录与实例信息，不经过主页。
    void setInstanceContextForTest(const QString &rootPath, const AgentInstanceInfo &info);
    /// 测试专用：走审批卡
    void approveToolForTest(bool approved);
    /// 测试专用：取消在飞请求后直接删除当前会话（跳过确认弹层）。
    bool deleteSessionForTest(const QString &sessionId);

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
    /// 切回智能体页：「跟随主页」的胶囊按主页此刻的实例重算（主页实例可能在别的页换过）。
    void onPageShown();

private:
    void wirePage();
    void loadInitialSession();
    void refreshSessions();
    void selectSession(const QString &sessionId);
    void discardBlankCurrent(const QString &nextId);
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
    void onInstanceMenuRequested();
    void onInstanceChosen(const QString &instanceId);
    void onExport(const QString &sessionId);
    AgentInstanceInfo homeInstance() const;
    QList<AgentInstanceInfo> instances() const;
    AgentInstanceInfo lockedInstance() const;
    void onApproval(bool approved);
    AgentToolContext toolContext(QString *problem);
    void updateInstanceChip();
    void showToolCardsFromHistory();
    QList<AgentView::ToolCard> toolCardsFromHistory() const;

    AgentPage *m_page = nullptr;
    OverlayHost *m_overlay = nullptr;
    AgentSessionStore m_store;
    AgentArtifactStore m_artifacts;
    AgentBackupIndex m_backups;
    AgentFileTools m_files;
    AgentFileToolEngine *m_toolEngine = nullptr;
    AgentSession m_session;
    InstanceProvider m_instanceProvider;
    InstanceListProvider m_instanceListProvider;
    AgentInstanceInfo m_testInstance;
    QString m_testRootPath;
    bool m_useTestInstance = false;
    AgentProviderProfile m_profile;
    QString m_testApiKey;
    AgentModelClient *m_client = nullptr;
    AgentModelClient *m_ownedClient = nullptr;
    AgentLoop *m_loop = nullptr;
    QPointer<AgentSettingsPage> m_settingsPage;
    QTimer *m_saveTimer = nullptr;
    bool m_configured = false;
    bool m_savePending = false;
    bool m_settingsOpenQueued = false;  // 让位后排了下一轮重开 API 设置页
    bool m_shuttingDown = false;
    QString m_autoNoticeShownFor;
};
