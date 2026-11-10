/**
 * @file SecretProtector.hpp
 * @brief DPAPI 加解密的**唯一**实现：带自描述前缀、失败返回空、绝不降级成明文。
 *
 * 登录凭据（`TokenStore`）和智能体会话/密钥（`AgentSessionCrypto`、
 * `AgentSecretStore`）都要用 DPAPI，但它们的盘上格式各有各的前缀。把
 * `CryptProtectData` / `CryptUnprotectData` 那一段留在这里，是为了避免同一条
 * “失败绝对不能回退成混淆/明文”的判据被抄成两三份、然后其中一份先漂移。
 *
 * 边界：DPAPI 绑定“当前 Windows 用户 + 这台机器”。换用户/换机器解不开是设计代价，
 * 不是 bug。调用方必须把空返回值当成失败，不能拿它当“没有值”。
 */

#pragma once

#include <QByteArray>

namespace SecretProtector {

/// 在 @p blob 前面拼上 @p magic。只做字节拼接，不做校验。
QByteArray withMagic(const QByteArray &blob, const QByteArray &magic);

/// @p stored 是否以 @p magic 开头。
bool hasMagic(const QByteArray &stored, const QByteArray &magic);

/**
 * @brief 用 DPAPI 保护 @p plain，返回 `magic + 密文`。
 * @return 失败（Windows API 报错、空输入）返回空 QByteArray。
 *
 * NOTE: 故意不加 optionalEntropy：熵写在二进制里挡不住能读到二进制的人，却会多出
 * 一个“两处必须一模一样”的失配点。边界就是当前 Windows 用户。
 */
QByteArray protect(const QByteArray &plain, const QByteArray &magic);

/**
 * @brief 解开 `magic + 密文`。
 * @return 不是这个前缀、DPAPI 失败、内容为空 ⇒ 返回空 QByteArray。
 *
 * NOTE: 调用方**不能**在空返回值上尝试明文/旧格式回退 —— 那等于把加密边界拆掉。
 */
QByteArray unprotect(const QByteArray &stored, const QByteArray &magic);

}  // namespace SecretProtector
