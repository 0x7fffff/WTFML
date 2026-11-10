/**
 * @file AgentFileToolEngine.hpp
 * @brief Stage 1B 文件工具引擎：实例锁 → D11 外发 → 审批 → 受控读写。
 *
 * 给 AgentLoop 用。`ContextProvider` 由控制器提供：它负责在第一次实例操作时锁定实例、
 * 返回锁定实例根目录；引擎自己**不接受模型传入的实例 id**。
 */

#pragma once

#include "AgentArtifactStore.hpp"
#include "AgentEgressPolicy.hpp"
#include "AgentFileTools.hpp"
#include "AgentTool.hpp"

#include <QHash>
#include <QString>

#include <functional>

struct AgentToolContext {
    QString sessionId;
    QString instanceId;
    QString instanceName;
    QString gameRootId;
    QString rootPath;
    QString providerHost;
    bool strictEgress = false;
};

class AgentFileToolEngine : public AgentToolEngine {
public:
    using ContextProvider = std::function<AgentToolContext(QString *problem)>;

    AgentFileToolEngine(AgentFileTools *files, AgentArtifactStore *artifacts,
                        ContextProvider provider);

    QJsonArray schemas() const override;
    AgentToolResult invoke(const Agent::ToolCall &call) override;
    AgentToolResult resolveApproval(const QString &approvalId, bool approved) override;
    void cancelPending() override;
    bool committing() const override { return m_committing; }

    void setStrictEgress(bool strict) { m_strictEgress = strict; }
    bool strictEgress() const { return m_strictEgress; }

private:
    struct Pending {
        enum class Kind { DirectoryMetadata, FileNames, Read, Write };
        Kind kind = Kind::Read;
        Agent::ToolCall call;
        AgentToolContext context;
        QJsonObject egress;
        QString content;
        QString summary;
        AgentDirMetadata directory;
        QList<AgentFileNameEntry> names;
        bool namesTruncated = false;
        AgentFileReadResult read;
        AgentWritePlan writePlan;
    };

    AgentToolResult handleList(const Agent::ToolCall &call, const AgentToolContext &context);
    AgentToolResult handleListNames(const Agent::ToolCall &call, const AgentToolContext &context);
    AgentToolResult handleRead(const Agent::ToolCall &call, const AgentToolContext &context);
    AgentToolResult handleWrite(const Agent::ToolCall &call, const AgentToolContext &context);
    AgentToolResult handleArtifactRead(const Agent::ToolCall &call, const AgentToolContext &context);
    AgentToolResult approvalRequired(const Pending &pending, const QString &summary,
                                     const QString &preview, const QString &diff,
                                     const QString &backupHint, const QJsonObject &egress);
    QJsonObject egressJson(const AgentEgressPolicy::Result &result,
                           const AgentEgressPolicy::Kind kind) const;
    bool autoNotice(const AgentEgressPolicy::Result &result) const;

    AgentFileTools *m_files = nullptr;
    AgentArtifactStore *m_artifacts = nullptr;
    ContextProvider m_provider;
    QHash<QString, Pending> m_pending;
    bool m_strictEgress = false;
    bool m_committing = false;
};
