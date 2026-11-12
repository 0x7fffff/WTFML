/**
 * @file AgentMessageView.hpp
 * @brief 一条消息的显示：受控 Markdown 正文、折叠思考、状态、错误与重试。
 *
 * 自己说的话是靠右的一块淡底气泡（最宽 520，圆角 16 / 右下 4），助手的话直接铺在玻璃上
 * （样张 r2 agent .msg.me / .msg.ai）。角色不再单写一行，读屏靠无障碍名。
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

protected:
    void paintEvent(QPaintEvent *event) override;

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
