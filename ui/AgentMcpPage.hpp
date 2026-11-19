/**
 * @file AgentMcpPage.hpp
 * @brief MCP 服务器设置页：服务器列表、状态、连接/断开/编辑/删除与"本机用户权限"提示。
 *
 * 纯 QWidget：只认值、信号和槽；不 include `src/`、不起进程、不读盘。确认闸的文案由
 * 控制器组织，这里只负责把用户的操作发出去。
 */

#pragma once

#include "AgentViewTypes.hpp"

#include <QDialog>
#include <QList>

class QLabel;
class QVBoxLayout;

class AgentMcpPage : public QDialog {
    Q_OBJECT

public:
    explicit AgentMcpPage(QWidget *parent = nullptr);

    void setServers(const QList<AgentView::McpServerRow> &servers);
    void setNote(const QString &text);
    void setLog(const QString &text);
    QList<AgentView::McpServerRow> servers() const { return m_rows; }

    /// 编辑对话框：确定返回 true，并把结果写回 @p draft。
    static bool editDraft(QWidget *parent, const QString &title, AgentView::McpServerDraft *draft);

signals:
    void addRequested();
    void editRequested(const QString &serverId);
    void removeRequested(const QString &serverId);
    void connectRequested(const QString &serverId);
    void disconnectRequested(const QString &serverId);
    void refreshRequested();

private:
    void rebuild();

    QVBoxLayout *m_listLayout = nullptr;
    QLabel *m_note = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_log = nullptr;
    QList<AgentView::McpServerRow> m_rows;
};
