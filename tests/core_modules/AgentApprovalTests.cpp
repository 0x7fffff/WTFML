/**
 * @file AgentApprovalTests.cpp
 * @brief 审批矩阵与会话放行边界。
 */

#include "Support.hpp"

#include "agent/AgentApprovalPolicy.hpp"

using AgentApprovalPolicy::Decision;

void runAgentApprovalTests() {
    check(AgentApprovalPolicy::decide(QStringLiteral("fs.read"), AgentRisk::ReadOnly, false) ==
              Decision::Auto,
          "审批：自动白名单只读自动");
    check(AgentApprovalPolicy::decide(QStringLiteral("fs.read"), AgentRisk::ReadOnly, true) ==
              Decision::PerAction,
          "审批：需要外发确认时逐次");
    check(AgentApprovalPolicy::decide(QStringLiteral("fs.listNames"), AgentRisk::ReadOnly, false) ==
              Decision::PerAction,
          "审批：完整文件名清单逐次");
    check(AgentApprovalPolicy::decide(QStringLiteral("fs.write"), AgentRisk::Write, false) ==
              Decision::PerAction,
          "审批：写操作逐次");
    check(!AgentApprovalPolicy::allowsSessionApproval(QStringLiteral("fs.write")),
          "审批：文件写不提供会话级放行");
    check(!AgentApprovalPolicy::allowsSessionApproval(QStringLiteral("fs.read")),
          "审批：文件读也不提供会话级放行");
    check(AgentApprovalPolicy::allowsSessionApproval(QStringLiteral("web.search")),
          "审批：只有 web.search/fetch 允许会话级放行（Stage 3）");
    check(AgentApprovalPolicy::allowsSessionApproval(QStringLiteral("web.fetch")),
          "审批：web.fetch 允许会话级放行");
    const QJsonObject request = AgentApprovalPolicy::request(
        QStringLiteral("fs.write"), QStringLiteral("写入文本配置"),
        QStringLiteral("config/a.toml"), QStringLiteral("写入"), QStringLiteral("preview"),
        QStringLiteral("diff"), QStringLiteral("backup"),
        QJsonObject{{QStringLiteral("kind"), QStringLiteral("config-file")}});
    check(!request.contains(QStringLiteral("approved")),
          "审批：持久化请求不存“已批准”布尔");
    check(!request.value(QStringLiteral("sessionAllowAvailable")).toBool(true),
          "审批：写操作的会话放行不可用");
    check(AgentApprovalPolicy::approvalRequestLooksPersisted(request),
          "审批：请求结构可持久化");
}
