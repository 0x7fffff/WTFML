/**
 * @file AgentSessionCrypto.hpp
 * @brief 整会话加密：`WTFML-AGENT-SESSION-1\n` + DPAPI(JSON)。
 *
 * 加密只保护**落盘**，不改变发给模型的内容。解密失败（换 Windows 用户/机器、
 * 值损坏）必须报错，**不能**回退明文。
 */

#pragma once

#include <QByteArray>

namespace AgentSessionCrypto {

QByteArray magic();

/// 成功返回 `magic + 密文`；失败返回空（调用方不得写盘）。
QByteArray encrypt(const QByteArray &plain);

/// 成功返回明文；前缀不对/DPAPI 失败返回空。调用方把它当失败，不能当“空会话”。
QByteArray decrypt(const QByteArray &stored);

bool isEncryptedBody(const QByteArray &stored);

}  // namespace AgentSessionCrypto
