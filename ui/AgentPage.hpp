/**
 * @file AgentPage.hpp
 * @brief 智能体页：左侧会话栏 + 右侧聊天区（DeepSeek 式双栏，Stage 1A 无工具卡）。
 *
 * 纯 QWidget：只认值、信号和槽；不 include `src/`，不读盘、不联网。
 */

#pragma once

#include "AgentViewTypes.hpp"
#include "ThemeBackground.hpp"

#include <QHash>
#include <QList>

class QListWidgetItem;
class QPoint;

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;
class AgentComposer;
class AgentMessageView;

class AgentPage : public ThemeBackground {
    Q_OBJECT

public:
    explicit AgentPage(QWidget *parent = nullptr);

    static int sidebarWidth();
    static int sidebarNarrowWidth();
    static QString relativeTime(const QDateTime &when, const QDateTime &now);

    void setSessions(const QList<AgentView::SessionMeta> &sessions);
    void setCurrentSession(const QString &sessionId);
    QString currentSessionId() const { return m_currentSessionId; }

    void setTitle(const QString &title);
    void setMessages(const QList<AgentView::Message> &messages);
    void upsertMessage(const AgentView::Message &message);
    void removeMessage(const QString &messageId);
    void clearMessages();

    void setStreaming(bool streaming);
    void setConfigured(bool configured, const QString &statusText = QString());
    void setModelOptions(const QStringList &labels, const QString &current);
    void setInstanceChip(const QString &text, bool locked);
    void setEncryptionVisible(bool visible);
    void setEncryptionChecked(bool checked);
    void showNotice(const QString &text);
    void showEmptyState(bool empty);

    AgentComposer *composer() const { return m_composer; }
    QListWidget *sessionList() const { return m_sessionList; }

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    void newSessionRequested();
    void sessionSelected(const QString &sessionId);
    void sessionRenameRequested(const QString &sessionId, const QString &title);
    void sessionPinRequested(const QString &sessionId, bool pinned);
    void sessionDeleteRequested(const QString &sessionId);
    void sendRequested(const QString &text);
    void stopRequested();
    void settingsRequested();
    void modelChanged(const QString &label);
    void retryRequested(const QString &messageId);
    void encryptToggled(bool encrypted);
    void textChanged();

private:
    void rebuildSessionRow(QListWidgetItem *item, const AgentView::SessionMeta &meta);
    void updateSendEnabled();
    void scrollToLatest();
    AgentMessageView *viewFor(const QString &messageId);
    void showSessionMenu(const QPoint &position);

    QWidget *m_sidebar = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_sessionList = nullptr;
    QLabel *m_apiStatus = nullptr;
    QLabel *m_title = nullptr;
    QPushButton *m_newSession = nullptr;
    QPushButton *m_settingsButton = nullptr;
    QPushButton *m_instanceChip = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QCheckBox *m_encryptCheck = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_messageHost = nullptr;
    QVBoxLayout *m_messageLayout = nullptr;
    QLabel *m_emptyState = nullptr;
    QLabel *m_notice = nullptr;
    QComboBox *m_modeCombo = nullptr;
    AgentComposer *m_composer = nullptr;
    QPushButton *m_send = nullptr;
    QPushButton *m_stop = nullptr;
    QHash<QString, AgentMessageView *> m_views;
    QHash<QString, AgentView::SessionMeta> m_metaById;
    QString m_currentSessionId;
    bool m_streaming = false;
    bool m_configured = false;
};
