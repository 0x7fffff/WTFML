/**
 * @file AgentWorkflowEngine.hpp
 * @brief 工作流自己的两个工具：`workflow.submitPlan`（计划卡）与 `workflow.finish`（完成闸）。
 *
 * 计划必须结构化提交：字段缺失、工具越权、只读模式里出现写步骤、写步骤缺验证步，全部本地拒绝。
 * 批准计划不等于批准写操作：每个写步骤仍由对应的工具引擎单独弹审批（D5）。
 */

#pragma once

#include "AgentTool.hpp"
#include "AgentWorkflow.hpp"

#include <QHash>
#include <QJsonObject>
#include <QString>

class AgentWorkflowEngine : public AgentToolEngine {
public:
    explicit AgentWorkflowEngine(AgentWorkflow *workflow);

    static QList<AgentToolSpec> specs();
    static bool isWorkflowTool(const QString &internalName);

    QJsonArray schemas() const override;
    AgentToolResult invoke(const Agent::ToolCall &call) override;
    AgentToolResult resolveApproval(const QString &approvalId, bool approved) override;
    void cancelPending() override;
    bool handles(const QString &internalName) const override;

    /// 控制器切技能时同步；引擎不拥有这一层。
    void setWorkflow(AgentWorkflow *workflow) { m_workflow = workflow; }

private:
    struct Pending {
        QString goal;
        QList<AgentPlanStep> steps;
    };

    AgentToolResult handleSubmitPlan(const Agent::ToolCall &call);
    AgentToolResult handleFinish(const Agent::ToolCall &call);
    AgentToolResult errorResult(Agent::ErrorKind kind, const QString &message) const;

    AgentWorkflow *m_workflow = nullptr;
    QHash<QString, Pending> m_pending;
};
