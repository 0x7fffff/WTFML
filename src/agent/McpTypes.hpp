/**
 * @file McpTypes.hpp
 * @brief MCP（stdio）的配置、可执行文件身份、确认记录与工具描述（docs/features/AGENT.md §10）。
 *
 * 这一层是纯值类型：不读盘、不起进程。身份计算与确认闸在 `McpTrustStore`，协议在
 * `McpProtocol`，进程与握手在 `McpClient`。
 *
 * 约束：`configHash` 必须覆盖可执行文件身份之外的全部配置字段（命令、参数、工作目录、
 * 环境变量名、超时、启用开关）。任何一项变了，旧确认就失效（D7）。
 */

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

/// 解析后的可执行文件身份。任一关键字段缺失都按"算不出身份"处理，拒绝启动。
struct McpExecutableIdentity {
    QString absolutePath;
    QString sha256;
    qint64 size = -1;
    QString modifiedAt;   ///< UTC ISO
    QString volumeSerial; ///< 十六进制；取不到为空
    QString fileId;       ///< 十六进制；取不到为空

    bool isValid() const { return !absolutePath.isEmpty() && !sha256.isEmpty() && size >= 0; }
    QJsonObject toJson() const;
    static McpExecutableIdentity fromJson(const QJsonObject &object);
    /// 身份是否一致（路径 + 内容 + 大小 + 文件 ID）。mtime 单独比较，避免同秒改写漏判。
    bool sameAs(const McpExecutableIdentity &other) const;
    QString summary() const;
};

struct McpServerConfig {
    QString id;                ///< 稳定 id（uuid），工具名里用得到
    QString name;
    QString command;           ///< 用户填的可执行文件路径（解析成绝对路径后保存）
    QStringList arguments;
    QString workingDirectory;
    /// 环境变量**白名单**（只记名字，值不进配置文件；Stage 2A 只透传这些名字）。
    QStringList environmentNames;
    bool enabled = false;
    int handshakeTimeoutMs = 15000;
    int listTimeoutMs = 15000;
    int callTimeoutMs = 120000;

    bool isValid() const { return !id.isEmpty() && !command.isEmpty(); }
    /// 覆盖除可执行文件身份以外的全部配置字段。
    QString configHash() const;
    QJsonObject toJson() const;
    static McpServerConfig fromJson(const QJsonObject &object);
};

/// 一次确认：serverId + configHash + 可执行文件身份 + 时间。配置或身份变了即失效。
struct McpConfirmation {
    QString serverId;
    QString configHash;
    McpExecutableIdentity identity;
    QString confirmedAt;
    /// 首次启动 / 重新连接 都写一条；只用于展示，判定按 configHash + 身份。
    QString purpose;

    QJsonObject toJson() const;
    static McpConfirmation fromJson(const QJsonObject &object);
};

struct McpToolDescriptor {
    QString serverId;
    QString serverName;
    QString name;          ///< 服务器声明的原始工具名
    QString description;
    QJsonObject inputSchema;

    /// 工具对模型可见的线上名：`mcp__<serverId>__<tool>`，只含 `[A-Za-z0-9_-]`。
    QString wireName() const;
};

namespace McpLimits {
constexpr int kMaxLineBytes = 8 * 1024 * 1024;   ///< 单行上限（§10.2）
constexpr int kMaxPendingRequests = 32;
constexpr int kMaxListPages = 10;
constexpr int kMaxJunkLines = 50;
constexpr int kMaxTools = 200;
constexpr int kMaxResultBytes = 16 * 1024 * 1024;
constexpr int kMaxCwdLength = 1024;
constexpr int kMaxArgumentLength = 1024;
constexpr int kMaxArguments = 32;
constexpr int kMinTimeoutMs = 1000;
constexpr int kMaxTimeoutMs = 300000;
constexpr int kToolNameMaxLength = 64;
}  // namespace McpLimits
