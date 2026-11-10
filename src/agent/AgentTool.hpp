/**
 * @file AgentTool.hpp
 * @brief 工具接口/风险等级/注册表/执行引擎抽象（Stage 1B 文件工具）。
 *
 * 工具结果一律视为不可信数据；参数 JSON Schema 只支持所需子集，未知字段默认拒绝。
 * 写入/外发确认由引擎返回 ApprovalRequired，AgentLoop 暂停等待用户决定。
 */

#pragma once

#include "AgentTypes.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

enum class AgentRisk { ReadOnly, NetworkRead, Write, Destructive, Command, ExternalMcp };

struct AgentToolSpec {
    QString name;
    QString displayName;
    QString description;
    AgentRisk risk = AgentRisk::ExternalMcp;
    QJsonObject schema;
};

struct AgentToolResult {
    enum class Status { Ok, Error, ApprovalRequired, Blocked, Cancelled };
    Status status = Status::Error;
    QString summary;
    QJsonObject data;
    QJsonObject approval;
    Agent::Error error;

    bool ok() const { return status == Status::Ok; }
};

class AgentToolEngine {
public:
    virtual ~AgentToolEngine() = default;
    virtual QJsonArray schemas() const = 0;
    virtual AgentToolResult invoke(const Agent::ToolCall &call) = 0;
    virtual AgentToolResult resolveApproval(const QString &approvalId, bool approved) = 0;
    virtual void cancelPending() = 0;
    /// 文件提交阶段：取消只能登记“提交后停止”，不能强杀。
    virtual bool committing() const { return false; }
};

class AgentToolRegistry {
public:
    static QList<AgentToolSpec> builtins();
    static AgentToolSpec find(const QString &name);
    static QString riskKey(AgentRisk risk);

    /// JSON Schema 子集：type/required/properties/enum/maxLength/maximum/additionalProperties。
    static bool validateArguments(const QJsonObject &schema, const QJsonObject &arguments,
                                  QString *problem);
};
