/**
 * @file AgentViewTypes.hpp
 * @brief 智能体界面自己的**值类型**（DTO）：ui/Agent* 只认这些，不 include src/。
 *
 * `AgentController` 是唯一把核心层 `Agent::Message` / `AgentSessionMeta` /
 * `AgentProviderProfile` 翻译成这里的形状的地方。字段只保留界面要画/要收集的那几位，
 * 不把业务对象、网络客户端或存储路径带进 ui/。
 */

#pragma once

#include <QDateTime>
#include <QString>

namespace AgentView {

enum class MessageStatus { Complete, Streaming, Interrupted, Error, ToolPending, AwaitingApproval };

struct Message {
    QString id;
    bool user = false;
    QString content;
    QString reasoning;
    MessageStatus status = MessageStatus::Complete;
    QString errorText;
};

struct SessionMeta {
    QString id;
    QString title;
    QDateTime updatedAt;
    bool pinned = false;
    bool encrypted = false;
    bool corrupt = false;

    bool isValid() const { return !id.isEmpty(); }
};

/// 模式卡片（技能）一行。
struct SkillOption {
    QString id;
    QString name;
    QString description;
};

/// 实例下拉里的一行。
struct InstanceOption {
    QString id;
    QString name;
};

/// 设置页收集的 provider 字段（非密）。密钥永远单独走 AgentSecretEdit。
struct ProviderDraft {
    QString id;
    QString name;
    QString baseUrl;
    QString model;
    int contextWindow = 32768;
    int maxOutput = 4096;
    double temperature = 0.7;
    int timeoutSec = 120;
    QString deployment = QStringLiteral("cloud");
    QString authMode = QStringLiteral("bearer");
    bool stream = true;
    QString maxTokensField = QStringLiteral("max_tokens");
    QString systemRole = QStringLiteral("system");
    QString reasoningEcho = QStringLiteral("display-only");
    bool sendTemperature = true;
    bool sendToolChoice = true;
    bool localHttpAllowed = false;
};

enum class ToolCardStatus { Pending, Running, Done, Error };

struct ToolCard {
    QString id;
    QString title;
    QString summary;
    QString detail;
    ToolCardStatus status = ToolCardStatus::Pending;
};

}  // namespace AgentView
