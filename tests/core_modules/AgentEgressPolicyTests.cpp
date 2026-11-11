/**
 * @file AgentEgressPolicyTests.cpp
 * @brief D11 边界表、严格模式、内容脱敏、凭据文件硬阻断。
 */

#include "Support.hpp"

#include "agent/AgentEgressPolicy.hpp"

using AgentEgressPolicy::Decision;
using AgentEgressPolicy::Kind;
using AgentEgressPolicy::Request;

void runAgentEgressPolicyTests() {
    const auto decision = [](Kind kind, const QString &path = {}, const QString &content = {},
                             bool strict = false) {
        Request request;
        request.kind = kind;
        request.relativePath = path;
        request.content = content;
        request.strictMode = strict;
        return AgentEgressPolicy::evaluate(request);
    };

    check(decision(Kind::Log, QStringLiteral("logs/latest.log"), QStringLiteral("line")).decision ==
              Decision::Auto,
          "外发：日志自动读取（标准模式）");
    check(decision(Kind::CrashReport, QStringLiteral("crash-reports/crash-1.txt")).decision ==
              Decision::Auto,
          "外发：崩溃报告自动读取");
    check(decision(Kind::InstanceMetadata).decision == Decision::Auto,
          "外发：锁定实例元数据自动读取");
    check(decision(Kind::DirectoryMetadata).decision == Decision::Auto,
          "外发：目录元数据自动读取");
    check(decision(Kind::InstanceListSummary).decision == Decision::Auto,
          "外发：全局实例概况自动");
    check(decision(Kind::ConfigFile, QStringLiteral("config/a.toml")).decision == Decision::Confirm,
          "外发：配置文件逐次确认");
    check(decision(Kind::FileNameList, QStringLiteral("mods")).decision == Decision::Confirm,
          "外发：完整文件名清单逐次确认");
    check(decision(Kind::OtherText, QStringLiteral("notes.txt")).decision == Decision::Confirm,
          "外发：其他文本逐次确认");
    check(decision(Kind::InstanceListFull).decision == Decision::Confirm,
          "外发：全局实例完整列表逐次确认");
    check(decision(Kind::CredentialFile, QStringLiteral(".env")).decision == Decision::Block,
          "外发：凭据文件硬阻断");
    check(decision(Kind::CredentialStore).decision == Decision::Block,
          "外发：凭据存储硬阻断");
    check(decision(Kind::Binary, QStringLiteral("mods/a.jar")).decision == Decision::Block,
          "外发：二进制内容硬阻断");

    check(decision(Kind::Log, QStringLiteral("logs/latest.log"), {}, true).decision ==
              Decision::Confirm,
          "外发：严格模式把自动读取改成逐次确认");
    check(decision(Kind::DirectoryMetadata, {}, {}, true).decision == Decision::Confirm,
          "外发：严格模式目录元数据也确认");
    check(decision(Kind::CredentialFile, QStringLiteral(".env"), {}, true).decision ==
              Decision::Block,
          "外发：严格模式仍不放开凭据文件");

    const auto log = decision(Kind::Log, QStringLiteral("logs/latest.log"),
                              QStringLiteral("password=super-secret-123\naccess_token=abcdefghijk"),
                              false);
    check(log.decision == Decision::Auto && log.redactionHits >= 2 &&
              !log.redactedContent.contains(QStringLiteral("super-secret-123")),
          "外发：自动读取日志里的凭据字段只发脱敏片段", log.redactedContent);
    const auto config = decision(Kind::ConfigFile, QStringLiteral("config/a.toml"),
                                 QStringLiteral("api_key = \"sk-abcdefghijklmnop\""));
    check(config.decision == Decision::Confirm &&
              !config.redactedContent.contains(QStringLiteral("sk-abcdefghijklmnop")),
          "外发：配置预览已脱敏");
    check(AgentEgressPolicy::kindForPath(QStringLiteral("config/a.toml")) == Kind::ConfigFile,
          "外发：路径分类 config");
    check(AgentEgressPolicy::kindForPath(QStringLiteral("logs/latest.log")) == Kind::Log,
          "外发：路径分类 log");
    check(AgentEgressPolicy::kindForPath(QStringLiteral(".env")) == Kind::CredentialFile,
          "外发：路径分类凭据文件");
    check(AgentEgressPolicy::kindForPath(QStringLiteral("saves/world/level.dat")) ==
              Kind::OtherText,
          "外发：存档路径归入其他文件");
    check(AgentEgressPolicy::policyVersion() >= 1, "外发：策略版本存在");
}
