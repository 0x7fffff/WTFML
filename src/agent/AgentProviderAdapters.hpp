/**
 * @file AgentProviderAdapters.hpp
 * @brief OpenAI-compatible 基础兼容子集的**枚举化适配**：URL 校验、认证头、请求字段。
 *
 * 这里不联网、不读配置；给 profile + 消息，产出请求 JSON。所有适配项只允许枚举值，
 * 不接受任意 header/URL 模板/脚本（Agent.md §5.1、D6）。
 */

#pragma once

#include "AgentSettings.hpp"
#include "AgentTypes.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace AgentProviderAdapters {

/// https，或显式允许的 loopback http；拒绝 userinfo、片段、查询串。
bool validateBaseUrl(const QString &baseUrl, bool localHttpAllowed, bool *isLoopback = nullptr,
                     QString *problem = nullptr);
bool validateProfile(const AgentProviderProfile &profile, QString *problem = nullptr);

QString chatCompletionsUrl(const AgentProviderProfile &profile);
QString modelsUrl(const AgentProviderProfile &profile);

QByteArray authHeaderName(const AgentProviderProfile &profile);
QByteArray authHeaderValue(const AgentProviderProfile &profile, const QString &apiKey);

/// 把内部消息翻成 OpenAI chat/completions 的 `messages`。
QJsonArray buildMessages(const AgentProviderProfile &profile, const QList<Agent::Message> &messages,
                         const QString &systemPrompt);

/// 完整的请求体（含 stream / max_tokens / temperature / tools / tool_choice）。
QJsonObject buildRequest(const AgentProviderProfile &profile, const QList<Agent::Message> &messages,
                         const QString &systemPrompt, const QJsonArray &tools = {});

}  // namespace AgentProviderAdapters
