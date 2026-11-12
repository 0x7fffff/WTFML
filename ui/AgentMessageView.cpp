/**
 * @file AgentMessageView.cpp
 * @brief 消息气泡的渲染（Markdown 全部走 AgentMarkdown 的白名单）。
 */

#include "AgentMessageView.hpp"

#include "AgentMarkdown.hpp"
#include "Theme.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

AgentMessageView::AgentMessageView(const AgentView::Message &message, QWidget *parent)
    : QWidget(parent), m_message(message) {
    setObjectName(QStringLiteral("agentMessageView"));
    auto *layout = new QVBoxLayout(this);
    if (message.user) {
        layout->setContentsMargins(16, 10, 16, 10);
        setMaximumWidth(520);
    } else {
        layout->setContentsMargins(0, 0, 0, 0);
    }
    layout->setSpacing(4);

    m_role = new QLabel(this);
    m_role->setObjectName(QStringLiteral("agentMessageRole"));
    QFont roleFont = m_role->font();
    roleFont.setBold(true);
    m_role->setFont(Theme::textFont(roleFont, Theme::TextRole::Caption));
    m_role->setForegroundRole(QPalette::PlaceholderText);
    m_role->hide();
    layout->addWidget(m_role);

    m_reasoningToggle = new QToolButton(this);
    m_reasoningToggle->setObjectName(QStringLiteral("agentReasoningToggle"));
    m_reasoningToggle->setText(QStringLiteral("思考过程"));
    m_reasoningToggle->setCheckable(true);
    m_reasoningToggle->setChecked(false);
    m_reasoningToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    // 扁平：不留系统 QToolButton 那圈方框（悬停 / 勾上才出底）。
    m_reasoningToggle->setAutoRaise(true);
    layout->addWidget(m_reasoningToggle);
    m_reasoning = new QLabel(this);
    m_reasoning->setObjectName(QStringLiteral("agentReasoningLabel"));
    m_reasoning->setWordWrap(true);
    m_reasoning->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_reasoning->setForegroundRole(QPalette::PlaceholderText);
    m_reasoning->hide();
    layout->addWidget(m_reasoning);
    connect(m_reasoningToggle, &QToolButton::toggled, this, [this](bool checked) {
        m_reasoning->setVisible(checked);
    });

    m_body = new QLabel(this);
    m_body->setObjectName(QStringLiteral("agentMessageBody"));
    m_body->setWordWrap(true);
    m_body->setTextFormat(Qt::RichText);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    m_body->setOpenExternalLinks(false);
    layout->addWidget(m_body);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("agentMessageStatus"));
    m_status->setForegroundRole(QPalette::PlaceholderText);
    footer->addWidget(m_status);
    footer->addStretch(1);
    m_retry = new QPushButton(QStringLiteral("重试"), this);
    m_retry->setObjectName(QStringLiteral("agentRetryButton"));
    m_retry->hide();
    footer->addWidget(m_retry);
    layout->addLayout(footer);
    connect(m_retry, &QPushButton::clicked, this, [this] {
        emit retryRequested(m_message.id);
    });

    refresh();
}

void AgentMessageView::setMessage(const AgentView::Message &message) {
    m_message = message;
    refresh();
}

void AgentMessageView::refresh() {
    const bool user = m_message.user;
    m_role->setText(user ? QStringLiteral("你") : QStringLiteral("助手"));
    setAccessibleName(m_role->text());
    m_body->setText(AgentMarkdown::toHtml(m_message.content));
    const bool hasReasoning = !m_message.reasoning.trimmed().isEmpty();
    m_reasoningToggle->setVisible(hasReasoning);
    m_reasoning->setText(AgentMarkdown::toHtml(m_message.reasoning));
    if (!hasReasoning) m_reasoning->hide();

    QString status;
    switch (m_message.status) {
    case AgentView::MessageStatus::Streaming: status = QStringLiteral("正在生成…"); break;
    case AgentView::MessageStatus::Interrupted: status = QStringLiteral("已中断"); break;
    case AgentView::MessageStatus::Error: status = QStringLiteral("失败"); break;
    case AgentView::MessageStatus::ToolPending: status = QStringLiteral("等待工具…"); break;
    case AgentView::MessageStatus::AwaitingApproval: status = QStringLiteral("等待确认…"); break;
    case AgentView::MessageStatus::Complete: status.clear(); break;
    }
    if (!m_message.errorText.isEmpty()) {
        status = m_message.errorText;
    }
    m_status->setText(status);
    m_status->setVisible(!status.isEmpty());
    const bool canRetry = !user && (m_message.status == AgentView::MessageStatus::Error ||
                                    m_message.status == AgentView::MessageStatus::Interrupted);
    m_retry->setVisible(canRetry);
}

void AgentMessageView::paintEvent(QPaintEvent *) {
    if (!m_message.user) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect());
    // 圆角 16、右下角 4：气泡「指」向说话的一侧。
    QPainterPath path;
    path.addRoundedRect(r, 16, 16);
    QPainterPath corner;
    corner.addRect(QRectF(r.right() - 16, r.bottom() - 16, 16, 16));
    QPainterPath small;
    small.addRoundedRect(QRectF(r.right() - 16, r.bottom() - 16, 16, 16), 4, 4);
    path = path.united(corner).subtracted(corner.subtracted(small));
    p.fillPath(path, Theme::tokens().subtle);
}
