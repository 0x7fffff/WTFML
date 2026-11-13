/**
 * @file AgentEgressPolicyTests.cpp
 * @brief D11 边界表、严格模式、内容脱敏、凭据文件硬阻断。
 */

#include "Support.hpp"

#include "agent/AgentEgressPolicy.hpp"
#include "agent/AgentRedaction.hpp"

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

    // 键名变体。以前这里只认写死的完整字段名，而 `\b` 在下划线/连字符中间不成立，
    // 带前后缀的名字一个都不命中。
    const auto valueRedacted = [](const QString &line, const QString &secret) {
        const AgentRedaction::Result result = AgentRedaction::redact(line);
        return result.hits >= 1 && !result.text.contains(secret);
    };
    check(valueRedacted(QStringLiteral("hypixel_api_key=abcd1234efgh5678"),
                        QStringLiteral("abcd1234efgh5678")),
          "脱敏：hypixel_api_key 这类带前缀的键名也遮");
    check(valueRedacted(QStringLiteral("DB_PASSWORD: p@ssw0rdvalue"),
                        QStringLiteral("p@ssw0rdvalue")),
          "脱敏：DB_PASSWORD 也遮");
    check(valueRedacted(QStringLiteral("REDIS_PASSWORD=cachevalue1234"),
                        QStringLiteral("cachevalue1234")),
          "脱敏：REDIS_PASSWORD 也遮");
    check(valueRedacted(QStringLiteral("API_SECRET=shhhsecretvalue"),
                        QStringLiteral("shhhsecretvalue")),
          "脱敏：API_SECRET 也遮");
    check(valueRedacted(QStringLiteral("JWT_SECRET=opaquestuff99"),
                        QStringLiteral("opaquestuff99")),
          "脱敏：JWT_SECRET 也遮");
    check(valueRedacted(QStringLiteral("x-api-key: gateway12345"),
                        QStringLiteral("gateway12345")),
          "脱敏：x-api-key 这类连字符写法也遮");
    check(valueRedacted(QStringLiteral("apiKey=camelcasevalue1"),
                        QStringLiteral("camelcasevalue1")),
          "脱敏：apiKey 驼峰写法也遮");
    check(valueRedacted(QStringLiteral("PASSWORD=ALLCAPSVALUE1"),
                        QStringLiteral("ALLCAPSVALUE1")),
          "脱敏：全大写的 PASSWORD 不被切成两个词根而漏掉");
    check(valueRedacted(QStringLiteral("client_secret=oldnamesecure"),
                        QStringLiteral("oldnamesecure")),
          "脱敏：原先认识的 client_secret 仍然认识");
    check(!AgentRedaction::redact(QStringLiteral("version=1.21.10-snapshot"))
               .text.contains(QStringLiteral("<redacted>")),
          "脱敏：普通字段不动");
    check(!AgentRedaction::redact(QStringLiteral("player_username=awa123456"))
               .text.contains(QStringLiteral("<redacted>")),
          "脱敏：username 不算凭据字段");
    check(AgentRedaction::redact(QStringLiteral("password = true")).text.contains(
              QStringLiteral("true")),
          "脱敏：短值不遮，避免吃掉说明文字");
    check(AgentRedaction::redact(QStringLiteral("DB_PASSWORD: p@ssw0rdvalue"))
              .text.contains(QStringLiteral("DB_PASSWORD")),
          "脱敏：遮值不遮字段名");
}
