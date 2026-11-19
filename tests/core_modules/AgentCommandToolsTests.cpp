/**
 * @file AgentCommandToolsTests.cpp
 * @brief 固定诊断命令：默认关闭、白名单、host 校验、逐次审批、拒绝不执行、真跑一条命令。
 */

#include "Support.hpp"

#include "agent/AgentCommandTools.hpp"
#include "agent/AgentSettings.hpp"
#include "agent/AgentTool.hpp"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QThread>

namespace {

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 15000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    return true;
}

Agent::ToolCall commandCall(const QString &command, const QString &host = QString()) {
    Agent::ToolCall call;
    call.id = QStringLiteral("cmd-call");
    call.name = QStringLiteral("command.diagnose");
    QJsonObject arguments{{QStringLiteral("command"), command}};
    if (!host.isEmpty()) arguments.insert(QStringLiteral("host"), host);
    call.arguments = arguments;
    return call;
}

}  // namespace

void runAgentCommandToolsTests() {
    // ---------- 默认关闭 ----------
    AgentSettings::setDiagnoseCommandsEnabled(false);
    AgentCommandTools tools;
    AgentToolResult result = tools.invoke(commandCall(QStringLiteral("java-version")));
    check(result.status == AgentToolResult::Status::Error,
          "诊断命令：默认关闭时拒绝执行");
    check(result.summary.contains(QStringLiteral("默认关闭")), "诊断命令：拒绝理由说得清");

    AgentSettings::setDiagnoseCommandsEnabled(true);

    // ---------- 白名单与参数 ----------
    result = tools.invoke(commandCall(QStringLiteral("rm-rf")));
    check(result.status == AgentToolResult::Status::Error, "诊断命令：白名单外的命令被拒");
    result = tools.invoke(commandCall(QStringLiteral("ping")));
    check(result.status == AgentToolResult::Status::Error, "诊断命令：缺 host 被拒");
    result = tools.invoke(commandCall(QStringLiteral("ping"), QStringLiteral("8.8.8.8; rm -rf /")));
    check(result.status == AgentToolResult::Status::Error, "诊断命令：host 带元字符被拒");
    result = tools.invoke(commandCall(QStringLiteral("ping"), QStringLiteral("--help")));
    check(result.status == AgentToolResult::Status::Error, "诊断命令：host 以 - 开头被拒");
    Agent::ToolCall extra = commandCall(QStringLiteral("netstat"));
    extra.arguments.insert(QStringLiteral("shell"), QStringLiteral("cmd /c whoami"));
    result = tools.invoke(extra);
    check(result.status == AgentToolResult::Status::Error, "诊断命令：多余参数被拒");

    // ---------- 审批 ----------
    AgentSettings::setDiagnoseCommandsEnabled(true);
    AgentToolResult sinkResult;
    bool sinkCalled = false;
    tools.setCompletionSink([&sinkResult, &sinkCalled](const Agent::ToolCall &,
                                                       const AgentToolResult &value) {
        sinkResult = value;
        sinkCalled = true;
    });
    result = tools.invoke(commandCall(QStringLiteral("where-java")));
    check(result.status == AgentToolResult::Status::ApprovalRequired,
          "诊断命令：每次执行都要审批");
    const QString approvalId = result.approval.value(QStringLiteral("approvalId")).toString();
    const AgentToolResult denied = tools.resolveApproval(approvalId, false);
    check(denied.error.kind == Agent::ErrorKind::ApprovalDenied, "诊断命令：拒绝后不执行");
    check(!sinkCalled, "诊断命令：拒绝后没有结果回调");

    // ---------- 真跑一条（where java） ----------
    // 机器上没有 where / java 时也允许，只要结果以错误结束，不崩、不挂。
    result = tools.invoke(commandCall(QStringLiteral("where-java")));
    if (result.status == AgentToolResult::Status::ApprovalRequired) {
        const AgentToolResult pending =
            tools.resolveApproval(result.approval.value(QStringLiteral("approvalId")).toString(),
                                  true);
        check(pending.status == AgentToolResult::Status::Pending,
              "诊断命令：批准后进入异步执行");
        check(waitFor([&sinkCalled] { return sinkCalled; }), "诊断命令：结果回得来");
        check(sinkResult.data.contains(QStringLiteral("output")) ||
                  sinkResult.data.contains(QStringLiteral("exitCode")) ||
                  sinkResult.status == AgentToolResult::Status::Error,
              "诊断命令：结果里有输出或明确的错误");
        check(sinkResult.data.value(QStringLiteral("untrusted")).toBool(),
              "诊断命令：结果标成不可信");
    } else {
        check(result.status == AgentToolResult::Status::Error,
              "诊断命令：找不到 where 时给出错误");
    }

    // ---------- 超时与清理走同一条路（用一个短超时的命令很难稳定复现，这里只验取消）----------
    tools.cancelPending();
    const AgentToolResult stale =
        tools.resolveApproval(QStringLiteral("not-an-approval"), true);
    check(stale.status == AgentToolResult::Status::Error, "诊断命令：失效审批被拒");

    AgentSettings::setDiagnoseCommandsEnabled(false);
}
