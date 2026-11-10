/**
 * @file AgentSettings.hpp
 * @brief 智能体模型服务的**非密**配置：provider profile、模型、枚举化协议适配项。
 *
 * 盘上落在 QSettings（可被测试重定向到临时 ini）。这里**绝不**存 API key ——
 * 密钥只走 `AgentSecretStore`。profileId 是内部生成的受限 slug，不直接用显示名拼
 * 凭据管理器 target，避免 target 注入。
 */

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

struct AgentProviderProfile {
    QString id = QStringLiteral("default");
    QString name = QStringLiteral("默认模型服务");
    QString baseUrl;
    QString model;
    int contextWindow = 32768;
    int maxOutput = 4096;
    double temperature = 0.7;
    int timeoutSec = 120;
    /// cloud / local。本地只影响外发说明，不改变 URL 校验。
    QString deployment = QStringLiteral("cloud");
    /// bearer / api-key-header / none。none 只用于本地服务。
    QString authMode = QStringLiteral("bearer");
    bool stream = true;
    QString maxTokensField = QStringLiteral("max_tokens");
    QString systemRole = QStringLiteral("system");
    QString reasoningEcho = QStringLiteral("display-only");
    bool sendTemperature = true;
    bool sendToolChoice = true;
    bool localHttpAllowed = false;
    /// 首次使用外发说明的确认记录（providerId + host + policyVersion）。
    bool egressAcknowledged = false;
    int egressPolicyVersion = 0;
    /// credential-manager / dpapi-fallback；只用于界面显示，不代表值本身。
    QString storageBackend = QStringLiteral("credential-manager");

    bool isValid() const { return !id.isEmpty() && !baseUrl.isEmpty() && !model.isEmpty(); }

    QJsonObject toJson() const;
    static AgentProviderProfile fromJson(const QJsonObject &object);
    /// 只校验枚举与数值范围；URL 规则在 AgentProviderAdapters::validateBaseUrl()。
    bool validateEnumFields(QString *problem) const;
};

class AgentSettings {
public:
    /// 全部 profile（盘上没有就返回空表，由调用方决定是否建默认档）。
    static QList<AgentProviderProfile> profiles();
    static bool saveProfile(const AgentProviderProfile &profile, QString *problem = nullptr);
    static bool removeProfile(const QString &profileId, QString *problem = nullptr);
    static AgentProviderProfile profile(const QString &profileId);
    static bool strictEgress();
    static void setStrictEgress(bool strict);
    static QString activeProfileId();
    static void setActiveProfileId(const QString &profileId);

    static QString profilesKey() { return QStringLiteral("Agent/Profiles"); }
    static QString activeKey() { return QStringLiteral("Agent/ActiveProfile"); }
    static QString strictEgressKey() { return QStringLiteral("Agent/StrictEgress"); }
};
