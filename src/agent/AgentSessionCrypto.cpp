/**
 * @file AgentSessionCrypto.cpp
 * @brief 整会话 DPAPI 封装（格式见 .hpp）。
 */

#include "AgentSessionCrypto.hpp"

#include "util/SecretProtector.hpp"

namespace AgentSessionCrypto {

QByteArray magic() {
    static const QByteArray value("WTFML-AGENT-SESSION-1\n");
    return value;
}

QByteArray encrypt(const QByteArray &plain) {
    return SecretProtector::protect(plain, magic());
}

QByteArray decrypt(const QByteArray &stored) {
    return SecretProtector::unprotect(stored, magic());
}

bool isEncryptedBody(const QByteArray &stored) {
    return SecretProtector::hasMagic(stored, magic());
}

}  // namespace AgentSessionCrypto
