/**
 * @file AgentEgressPolicy.cpp
 * @brief D11 边界表与内容脱敏（见 .hpp）。
 */

#include "AgentEgressPolicy.hpp"

#include "AgentRedaction.hpp"

#include <QFileInfo>

namespace AgentEgressPolicy {

int policyVersion() { return 1; }

QString kindKey(Kind kind) {
    switch (kind) {
    case Kind::Log: return QStringLiteral("log");
    case Kind::CrashReport: return QStringLiteral("crash-report");
    case Kind::InstanceMetadata: return QStringLiteral("instance-metadata");
    case Kind::DirectoryMetadata: return QStringLiteral("directory-metadata");
    case Kind::InstanceListSummary: return QStringLiteral("instance-list-summary");
    case Kind::InstanceListFull: return QStringLiteral("instance-list-full");
    case Kind::ConfigFile: return QStringLiteral("config-file");
    case Kind::FileNameList: return QStringLiteral("file-name-list");
    case Kind::OtherText: return QStringLiteral("other-text");
    case Kind::CredentialFile: return QStringLiteral("credential-file");
    case Kind::CredentialStore: return QStringLiteral("credential-store");
    case Kind::Binary: return QStringLiteral("binary");
    case Kind::ArtifactRead: return QStringLiteral("artifact-read");
    }
    return QStringLiteral("other-text");
}

QString decisionKey(Decision decision) {
    switch (decision) {
    case Decision::Auto: return QStringLiteral("auto");
    case Decision::Confirm: return QStringLiteral("confirm");
    case Decision::Block: return QStringLiteral("block");
    }
    return QStringLiteral("block");
}

bool isCredentialFileName(const QString &relativePath) {
    const QString name = QFileInfo(relativePath).fileName().toLower();
    if (name == QLatin1String(".env") || name == QLatin1String("launcher_accounts.json") ||
        name == QLatin1String("credentials.json") || name == QLatin1String("tokens.json")) {
        return true;
    }
    const QString suffix = QFileInfo(name).suffix().toLower();
    if (suffix == QLatin1String("pem") || suffix == QLatin1String("key") ||
        suffix == QLatin1String("p12") || suffix == QLatin1String("pfx") ||
        suffix == QLatin1String("jks")) {
        return true;
    }
    return name.startsWith(QLatin1String("credentials")) ||
           name.startsWith(QLatin1String("token")) ||
           name.startsWith(QLatin1String("secret")) ||
           name.startsWith(QLatin1String("auth."));
}

Kind kindForPath(const QString &relativePath) {
    const QString path = relativePath.toLower();
    if (isCredentialFileName(path)) return Kind::CredentialFile;
    if (path.startsWith(QLatin1String("config/")) ||
        path.startsWith(QLatin1String("defaultconfigs/")) ||
        path == QLatin1String("options.txt")) {
        return Kind::ConfigFile;
    }
    if (path.startsWith(QLatin1String("logs/")) || path.endsWith(QLatin1String(".log"))) {
        return Kind::Log;
    }
    if (path.startsWith(QLatin1String("crash-reports/")) ||
        path.contains(QLatin1String("crash-"))) {
        return Kind::CrashReport;
    }
    return Kind::OtherText;
}

Result evaluate(const Request &request) {
    Result result;
    result.containsCredentials = AgentRedaction::containsCredentials(request.content);
    if (request.kind == Kind::CredentialFile || request.kind == Kind::CredentialStore) {
        result.decision = Decision::Block;
        result.reason = QStringLiteral("凭据文件/凭据存储禁止读取和发送。");
        return result;
    }
    if (request.kind == Kind::Binary) {
        result.decision = Decision::Block;
        result.reason = QStringLiteral("二进制文件只允许元数据，不读取/发送内容。");
        return result;
    }
    if (request.kind == Kind::DirectoryMetadata ||
        request.kind == Kind::InstanceMetadata ||
        request.kind == Kind::InstanceListSummary) {
        result.decision = request.strictMode ? Decision::Confirm : Decision::Auto;
        result.needsNotice = !request.strictMode;
        result.reason = request.strictMode ? QStringLiteral("严格模式：元数据也要逐次确认。")
                                           : QStringLiteral("自动读取；可能发送给当前模型服务。");
        return result;
    }
    if (request.kind == Kind::Log || request.kind == Kind::CrashReport ||
        request.kind == Kind::ArtifactRead) {
        result.decision = request.strictMode ? Decision::Confirm : Decision::Auto;
        result.needsNotice = !request.strictMode;
        result.redactedContent = AgentRedaction::redact(request.content).text;
        result.redactionHits = AgentRedaction::redact(request.content).hits;
        result.reason = request.strictMode ? QStringLiteral("严格模式：日志/崩溃/artifact 逐次确认。")
                                           : QStringLiteral("自动读取；已脱敏；可能发送给当前模型服务。");
        return result;
    }
    // 配置文件、完整文件名清单、其他文本、完整实例列表：每次都确认，没有会话级目录放行。
    result.decision = Decision::Confirm;
    result.redactedContent = AgentRedaction::redact(request.content).text;
    result.redactionHits = AgentRedaction::redact(request.content).hits;
    result.reason = QStringLiteral("需要逐次确认；确认后只发送脱敏片段。");
    return result;
}

}  // namespace AgentEgressPolicy
