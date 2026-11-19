/**
 * @file McpProtocol.hpp
 * @brief MCP stdio 的 JSON-RPC 2.0 帧编解码与工具名映射（纯函数，docs/features/AGENT.md §10.2）。
 *
 * 一行一条 JSON；不合法的一行不抛异常、不回半个状态，交给调用方按"垃圾行"处理。
 * 这里不做重连、不管进程，只负责把字节翻成结构、把结构翻成字节。
 */

#pragma once

#include "McpTypes.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace McpProtocol {

/// 协议版本：客户端采用自己支持的最高版本，服务器回什么以服务器为准（不支持则报错）。
QString supportedProtocolVersion();

struct Frame {
    bool valid = false;
    QString problem;
    bool hasId = false;
    QString id;          ///< 数字 id 也按字符串存（回包时原样写回）
    bool idWasNumber = false;
    QString method;      ///< 空 = 响应
    QJsonObject params;
    QJsonObject result;
    QJsonObject error;
    bool hasError = false;
};

/// 解析一行。空行/超过 8 MB/不是 JSON 对象/缺 jsonrpc 都算 invalid。
Frame parseLine(const QByteArray &line, int maxBytes = McpLimits::kMaxLineBytes);

QByteArray encodeRequest(const QString &id, const QString &method, const QJsonObject &params);
QByteArray encodeNotification(const QString &method, const QJsonObject &params);
QByteArray encodeResult(const QString &id, const QJsonObject &result, bool numericId = false);
QByteArray encodeError(const QString &id, int code, const QString &message,
                       bool numericId = false);

/// JSON-RPC 标准错误码。
namespace ErrorCode {
constexpr int kParse = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternal = -32603;
}  // namespace ErrorCode

/// 服务器反向请求：`ping` 直接回空结果，其它已知不支持的方法回 -32601。
QByteArray replyToServerRequest(const Frame &frame);

}  // namespace McpProtocol
