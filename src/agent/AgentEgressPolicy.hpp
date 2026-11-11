/**
 * @file AgentEgressPolicy.hpp
 * @brief D11 外发策略：任何进入模型请求的文件/日志/工具结果先判 auto / confirm / block。
 *
 * 标准模式边界（Agent.md §5.6.2）：日志/崩溃报告/锁定实例元数据/目录元数据自动；
 * 配置文件/完整文件名清单/其他文本逐次确认；凭据文件/凭据存储硬阻断；严格模式把
 * 自动整列改成逐次确认。自动读取也必须由 UI 告知“可能发送给当前模型服务”。
 */

#pragma once

#include <QString>

namespace AgentEgressPolicy {

enum class Kind {
    Log,
    CrashReport,
    InstanceMetadata,
    DirectoryMetadata,
    InstanceListSummary,
    InstanceListFull,
    ConfigFile,
    FileNameList,
    OtherText,
    CredentialFile,
    CredentialStore,
    Binary,
    ArtifactRead,
};

enum class Decision { Auto, Confirm, Block };

struct Request {
    Kind kind = Kind::OtherText;
    QString relativePath;
    QString content;
    bool strictMode = false;
    bool localDeployment = false;
};

struct Result {
    Decision decision = Decision::Block;
    QString reason;
    QString redactedContent;
    int redactionHits = 0;
    /// 自动读取第一次使用时要告知用户“可能发送给当前模型服务”。
    bool needsNotice = false;
    bool containsCredentials = false;
};

Kind kindForPath(const QString &relativePath);
QString kindKey(Kind kind);
QString decisionKey(Decision decision);
bool isCredentialFileName(const QString &relativePath);

Result evaluate(const Request &request);

/// 首次使用外发说明的固定版本号（host 或策略变化时重新确认）。
int policyVersion();

}  // namespace AgentEgressPolicy
