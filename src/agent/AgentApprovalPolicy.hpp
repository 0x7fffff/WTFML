/**
 * @file AgentApprovalPolicy.hpp
 * @brief 审批闸：只读自动；写/删/升级/命令/外部 MCP 逐次确认。
 *
 * Stage 1B 没有 web.search/fetch，因此**不存在任何会话级放行**。审批状态只存“请求”，
 * 不存“已批准”布尔；重启后必须重新确认。
 */

#pragma once

#include "AgentTool.hpp"

#include <QJsonObject>
#include <QString>

namespace AgentApprovalPolicy {

enum class Decision { Auto, PerAction, Blocked };

/// 只有 web.search/fetch 这类联网读取允许“本会话放行”；Stage 1B 一律 false。
bool allowsSessionApproval(const QString &toolName);

Decision decide(const QString &toolName, AgentRisk risk, bool egressNeedsConfirmation);

/// 组装持久化/展示用的审批请求（不含“已批准”字段）。
QJsonObject request(const QString &toolName, const QString &displayName, const QString &target,
                    const QString &summary, const QString &preview, const QString &diff,
                    const QString &backupHint, const QJsonObject &egress);

bool approvalRequestLooksPersisted(const QJsonObject &object);

}  // namespace AgentApprovalPolicy
