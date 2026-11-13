/**
 * @file agent_main.cpp
 * @brief 智能体核心验证入口（Stage 1A A 组）：SSE/适配/会话/加密/凭据/上下文/Loop/技能/Markdown。
 *
 * 不联网、不读真实用户目录；每个用例用临时沙盒。产品入口不设置这些测试属性。
 */

#include "Support.hpp"
#include "support/SettingsIsolation.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

void runAgentSseParserTests();
void runAgentProviderAdapterTests();
void runAgentSessionStoreTests();
void runAgentSessionCryptoTests();
void runAgentCredentialStoreTests();
void runAgentContextTests();
void runAgentLoopTests();
void runAgentSkillsTests();
void runAgentMarkdownTests();
void runAgentPathPolicyTests();
void runAgentEgressPolicyTests();
void runAgentApprovalTests();
void runAgentArtifactTests();
void runAgentInstanceLockTests();
void runAgentBackupLifecycleTests();
void runAgentToolTests();
void runAgentOpenAiClientTests();

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("WTFML-check"));
    app.setApplicationName(QStringLiteral("verify-agent"));
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    QTemporaryDir fixture;
    if (!fixture.isValid()) {
        std::fprintf(stderr, "agent fixture unavailable\n");
        return 2;
    }
    const QString settingsDir = fixture.filePath(QStringLiteral("settings"));
    QDir().mkpath(settingsDir);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                       fixture.filePath(QStringLiteral("system-settings")));
    requireIsolatedSettings(fixture.path(), "verify agent");
    app.setProperty("wtfml.testDefaultMinecraftDir", fixture.filePath(QStringLiteral("minecraft")));
    app.setProperty("wtfml.testAgentDataDir", fixture.filePath(QStringLiteral("agent")));

    runAgentSseParserTests();
    runAgentProviderAdapterTests();
    runAgentSessionStoreTests();
    runAgentSessionCryptoTests();
    runAgentCredentialStoreTests();
    runAgentContextTests();
    runAgentLoopTests();
    runAgentSkillsTests();
    runAgentMarkdownTests();
    runAgentPathPolicyTests();
    runAgentEgressPolicyTests();
    runAgentApprovalTests();
    runAgentArtifactTests();
    runAgentInstanceLockTests();
    runAgentBackupLifecycleTests();
    runAgentToolTests();
    runAgentOpenAiClientTests();

    out() << "agent: " << g_checks - g_failures << "/" << g_checks << "\n";
    return g_failures == 0 ? 0 : 1;
}
