/**
 * @file AgentApprovalPolicy.cpp
 * @brief 审批矩阵（见 .hpp）。
 */

#include "AgentApprovalPolicy.hpp"

namespace AgentApprovalPolicy {

bool allowsSessionApproval(const QString &toolName) {
    return toolName == QStringLiteral("web.search") || toolName == QStringLiteral("web.fetch");
}

Decision decide(const QString &toolName, AgentRisk risk, bool egressNeedsConfirmation) {
    if (risk == AgentRisk::Write || risk == AgentRisk::Destructive ||
        risk == AgentRisk::Command || risk == AgentRisk::ExternalMcp) {
        return Decision::PerAction;
    }
    if (egressNeedsConfirmation) return Decision::PerAction;
    if (toolName == QStringLiteral("fs.listNames")) return Decision::PerAction;
    return Decision::Auto;
}

QJsonObject request(const QString &toolName, const QString &displayName, const QString &target,
                    const QString &summary, const QString &preview, const QString &diff,
                    const QString &backupHint, const QJsonObject &egress) {
    return QJsonObject{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("tool"), toolName},
        {QStringLiteral("displayName"), displayName},
        {QStringLiteral("target"), target},
        {QStringLiteral("summary"), summary},
        {QStringLiteral("preview"), preview},
        {QStringLiteral("diff"), diff},
        {QStringLiteral("backupHint"), backupHint},
        {QStringLiteral("egress"), egress},
        {QStringLiteral("sessionAllowAvailable"), allowsSessionApproval(toolName)},
    };
}

bool approvalRequestLooksPersisted(const QJsonObject &object) {
    return object.value(QStringLiteral("schema")).toInt() > 0 &&
           !object.value(QStringLiteral("tool")).toString().isEmpty() &&
           !object.contains(QStringLiteral("approved"));
}

}  // namespace AgentApprovalPolicy
