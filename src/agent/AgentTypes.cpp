/**
 * @file AgentTypes.cpp
 * @brief 枚举与盘上字符串的互译（盘上格式是契约，改名要同步 schema）。
 */

#include "AgentTypes.hpp"

namespace Agent {

QString roleKey(Role role) {
    switch (role) {
    case Role::System: return QStringLiteral("system");
    case Role::User: return QStringLiteral("user");
    case Role::Assistant: return QStringLiteral("assistant");
    case Role::Tool: return QStringLiteral("tool");
    }
    return QStringLiteral("user");
}

Role roleFromKey(const QString &key) {
    if (key == QLatin1String("system")) return Role::System;
    if (key == QLatin1String("assistant")) return Role::Assistant;
    if (key == QLatin1String("tool")) return Role::Tool;
    return Role::User;
}

QString statusKey(MessageStatus status) {
    switch (status) {
    case MessageStatus::Complete: return QStringLiteral("complete");
    case MessageStatus::Streaming: return QStringLiteral("streaming");
    case MessageStatus::Interrupted: return QStringLiteral("interrupted");
    case MessageStatus::Error: return QStringLiteral("error");
    case MessageStatus::ToolPending: return QStringLiteral("tool_pending");
    case MessageStatus::AwaitingApproval: return QStringLiteral("awaiting_approval");
    }
    return QStringLiteral("complete");
}

MessageStatus statusFromKey(const QString &key) {
    if (key == QLatin1String("streaming")) return MessageStatus::Streaming;
    if (key == QLatin1String("interrupted")) return MessageStatus::Interrupted;
    if (key == QLatin1String("error")) return MessageStatus::Error;
    if (key == QLatin1String("tool_pending")) return MessageStatus::ToolPending;
    if (key == QLatin1String("awaiting_approval")) return MessageStatus::AwaitingApproval;
    return MessageStatus::Complete;
}

QString errorKey(ErrorKind kind) {
    switch (kind) {
    case ErrorKind::None: return QStringLiteral("none");
    case ErrorKind::NoConfiguration: return QStringLiteral("no_configuration");
    case ErrorKind::Unauthorized: return QStringLiteral("unauthorized");
    case ErrorKind::RateLimited: return QStringLiteral("rate_limited");
    case ErrorKind::Quota: return QStringLiteral("quota");
    case ErrorKind::Network: return QStringLiteral("network");
    case ErrorKind::Protocol: return QStringLiteral("protocol");
    case ErrorKind::ContextOverflow: return QStringLiteral("context_overflow");
    case ErrorKind::ToolError: return QStringLiteral("tool_error");
    case ErrorKind::EgressApprovalRequired: return QStringLiteral("egress_approval_required");
    case ErrorKind::EgressBlocked: return QStringLiteral("egress_blocked");
    case ErrorKind::ApprovalDenied: return QStringLiteral("approval_denied");
    case ErrorKind::Cancelled: return QStringLiteral("cancelled");
    case ErrorKind::Internal: return QStringLiteral("internal");
    }
    return QStringLiteral("internal");
}

ErrorKind errorFromKey(const QString &key) {
    if (key == QLatin1String("no_configuration")) return ErrorKind::NoConfiguration;
    if (key == QLatin1String("unauthorized")) return ErrorKind::Unauthorized;
    if (key == QLatin1String("rate_limited")) return ErrorKind::RateLimited;
    if (key == QLatin1String("quota")) return ErrorKind::Quota;
    if (key == QLatin1String("network")) return ErrorKind::Network;
    if (key == QLatin1String("protocol")) return ErrorKind::Protocol;
    if (key == QLatin1String("context_overflow")) return ErrorKind::ContextOverflow;
    if (key == QLatin1String("tool_error")) return ErrorKind::ToolError;
    if (key == QLatin1String("egress_approval_required")) return ErrorKind::EgressApprovalRequired;
    if (key == QLatin1String("egress_blocked")) return ErrorKind::EgressBlocked;
    if (key == QLatin1String("approval_denied")) return ErrorKind::ApprovalDenied;
    if (key == QLatin1String("cancelled")) return ErrorKind::Cancelled;
    if (key == QLatin1String("internal")) return ErrorKind::Internal;
    return ErrorKind::None;
}

}  // namespace Agent
