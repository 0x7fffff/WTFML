/**
 * @file AgentToolCard.cpp
 * @brief 工具卡片的渲染。
 */

#include "AgentToolCard.hpp"

#include "Theme.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

AgentToolCard::AgentToolCard(const AgentView::ToolCard &card, QWidget *parent)
    : QWidget(parent), m_card(card) {
    setObjectName(QStringLiteral("agentToolCard"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(4);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("agentToolTitle"));
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(Theme::textFont(titleFont, Theme::TextRole::Caption));
    header->addWidget(m_title);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("agentToolSummary"));
    m_summary->setWordWrap(true);
    m_summary->setForegroundRole(QPalette::PlaceholderText);
    header->addWidget(m_summary, 1);
    m_toggle = new QToolButton(this);
    m_toggle->setObjectName(QStringLiteral("agentToolToggle"));
    m_toggle->setText(QStringLiteral("详情"));
    m_toggle->setCheckable(true);
    header->addWidget(m_toggle);
    layout->addLayout(header);
    m_detail = new QLabel(this);
    m_detail->setObjectName(QStringLiteral("agentToolDetail"));
    m_detail->setWordWrap(true);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detail->hide();
    layout->addWidget(m_detail);
    connect(m_toggle, &QToolButton::toggled, this, [this](bool on) {
        m_detail->setVisible(on && !m_card.detail.isEmpty());
    });
    refresh();
}

void AgentToolCard::setCard(const AgentView::ToolCard &card) {
    m_card = card;
    refresh();
}

void AgentToolCard::refresh() {
    m_title->setText(m_card.title);
    m_summary->setText(m_card.summary);
    QString status;
    switch (m_card.status) {
    case AgentView::ToolCardStatus::Pending: status = QStringLiteral("待执行"); break;
    case AgentView::ToolCardStatus::Running: status = QStringLiteral("执行中…"); break;
    case AgentView::ToolCardStatus::Done: status = QStringLiteral("已完成"); break;
    case AgentView::ToolCardStatus::Error: status = QStringLiteral("失败"); break;
    }
    const QString suffix = status.isEmpty() ? QString() : QStringLiteral(" · ") + status;
    if (!suffix.isEmpty() && !m_summary->text().endsWith(suffix)) {
        m_summary->setText(m_card.summary + suffix);
    }
    m_detail->setText(m_card.detail);
    m_toggle->setVisible(!m_card.detail.isEmpty());
    if (m_card.detail.isEmpty()) m_detail->hide();
}
