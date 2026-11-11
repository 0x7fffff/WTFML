/**
 * @file AgentApprovalCard.hpp
 * @brief 写/外发审批卡：目标、摘要、预览/diff、备份提示、允许/拒绝。
 *
 * Stage 1B **不提供**“本会话允许此目录”；sessionAllowAvailable 恒为 false。
 */

#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

class AgentApprovalCard : public QWidget {
    Q_OBJECT

public:
    explicit AgentApprovalCard(QWidget *parent = nullptr);

    void setRequest(const QJsonObject &request);
    QString approvalId() const { return m_approvalId; }

signals:
    void allowed(const QString &approvalId);
    void denied(const QString &approvalId);

private:
    QString m_approvalId;
    QLabel *m_title = nullptr;
    QLabel *m_target = nullptr;
    QLabel *m_summary = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QLabel *m_backup = nullptr;
    QLabel *m_egress = nullptr;
    QPushButton *m_allow = nullptr;
    QPushButton *m_deny = nullptr;
    QPushButton *m_sessionAllow = nullptr;
};
