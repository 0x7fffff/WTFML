/**
 * @file AgentMessageView.hpp
 * @brief 一条消息的显示：角色/状态、受控 Markdown 正文、折叠思考、错误与重试。
 *
 * 纯界面：只认 `AgentView::Message` 值，不读盘、不联网。
 */

#pragma once

#include "AgentViewTypes.hpp"

#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

class AgentMessageView : public QWidget {
    Q_OBJECT

public:
    explicit AgentMessageView(const AgentView::Message &message, QWidget *parent = nullptr);

    QString messageId() const { return m_message.id; }
    void setMessage(const AgentView::Message &message);
    AgentView::Message message() const { return m_message; }

signals:
    void retryRequested(const QString &messageId);

private:
    void refresh();

    AgentView::Message m_message;
    QLabel *m_role = nullptr;
    QLabel *m_body = nullptr;
    QLabel *m_reasoning = nullptr;
    QToolButton *m_reasoningToggle = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_retry = nullptr;
};
