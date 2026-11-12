/**
 * @file AgentToolCard.hpp
 * @brief 工具调用卡片：工具显示名、摘要、展开详情、状态（待执行/执行中/结束/失败）。
 *
 * 纯界面；不执行工具、不做审批。
 */

#pragma once

#include "AgentViewTypes.hpp"

#include <QWidget>

class QLabel;
class QToolButton;

class AgentToolCard : public QWidget {
    Q_OBJECT

public:
    explicit AgentToolCard(const AgentView::ToolCard &card, QWidget *parent = nullptr);

    QString cardId() const { return m_card.id; }
    void setCard(const AgentView::ToolCard &card);
    AgentView::ToolCard card() const { return m_card; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void refresh();

    AgentView::ToolCard m_card;
    QLabel *m_title = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_detail = nullptr;
    QToolButton *m_toggle = nullptr;
};
