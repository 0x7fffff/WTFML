/**
 * @file TokenStore.cpp
 * @brief 凭据落盘的加解密与读写（见 .hpp 里那段说明）。
 * @author 0x7fffff
 * @date 2026-09-22
 */

#include "TokenStore.hpp"

#include <QCryptographicHash>
#include <QDebug>
#include <QJsonDocument>
#include <QSettings>

#if defined(Q_OS_WIN)
// NOTE: 放在所有 Qt 头之后 —— windows.h 定义的东西太多，先于 Qt 包含容易撞名。
// NOMINMAX 是给 std::min/std::max 让路（windows.h 会定义 min/max 宏）。
// 先判一下：libstdc++ 的 os_defines.h 已经定义过它了，直接再来一次会有重定义告警。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#endif

namespace {
// 旧格式（XOR 混淆）的密钥。只为了还能把老值读出来 —— 新写的一律走 DPAPI。
static constexpr const char kEncryptionKey[] = "WTFMLSecretKey2026";

// ───────────────────────── 令牌落盘的加解密 ─────────────────────────
//
// 盘上那份令牌是这个应用最值钱的数据 —— refresh_token 拿到手就等于拿到账号的长期访问权。
// 所以这里**要真加密**。旧的 obfuscate() 只是"密钥写在二进制里的混淆"：谁捡到那个 ini、
// 再反汇编一下把密钥读出来，令牌明文就还原了。
//
// Windows 上的正解是 DPAPI（CryptProtectData）：它用**当前登录用户**的凭据派生密钥，密文只有
// 同一个用户在这台机器上解得开。好处是密钥不归我们保管 —— 没有可泄露的密钥，也不用让用户
// 再记一个密码。代价也说明白：换用户、换机器就解不开（重登一次即可），这是设计的代价，不是 bug。
//
// NOTE: 故意**不加** optionalEntropy。熵是写死在二进制里的，挡不住任何能读到二进制的对手，
//       却会多出一个"两边必须一模一样"的失配点。真正的边界是"同一个 Windows 用户"。
//
// 盘上的格式是自描述的，为的是兼容已经存在的旧值：
//     以 kTokenMagic 开头 → 后面是 DPAPI 密文
//     其它               → 旧的 XOR 混淆（读得出来；下次写盘就自动升级成新格式）
//
// NOTE: 这一层**只管"当前用户 + 这台机器"这一条边界**。多账号列表里那些副本用的是同一个
//       格式、同一个 protect()，所以它们的强度完全一样 —— 换账号不需要用户重新输密码。

static constexpr char kTokenMagic[] = "WTFML-PROTECTED-1\n";
static constexpr int kTokenMagicLen = int(sizeof(kTokenMagic)) - 1;

/// 旧的混淆，只为了还能读出来。加密不再走它。
QByteArray legacyXor(const QByteArray &data) {
    const QByteArray key =
        QCryptographicHash::hash(QByteArray(kEncryptionKey), QCryptographicHash::Sha256);
    QByteArray result;
    result.reserve(data.size());
    for (int i = 0; i < data.size(); ++i) {
        result.append(data.at(i) ^ key.at(i % key.size()));
    }
    return result;
}

#if defined(Q_OS_WIN)
/// 调 DPAPI 的公共部分：出参 blob 用 LocalFree 还回去是微软的约定，忘了就是内存泄漏。
/// @return 成功时是密文/明文；失败返回空 QByteArray（调用方自己决定怎么退）。
QByteArray dpapiCall(const QByteArray &input, bool protect) {
    if (input.isEmpty()) {
        return QByteArray();
    }

    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(input.constData()));
    in.cbData = static_cast<DWORD>(input.size());

    DATA_BLOB out{};
    // 描述串只有 Windows 自己看得到（凭据管理器里），随手写一个方便辨认。
    const BOOL ok = protect ? CryptProtectData(&in, L"WTFML 登录令牌", nullptr, nullptr, nullptr,
                                               CRYPTPROTECT_UI_FORBIDDEN, &out)
                            : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                                                 CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (ok == FALSE) {
        // 解不开的常见原因不是"坏了"，是"不是这个用户/这台机器加的密"（或者值被改过）。
        qWarning() << "DPAPI" << (protect ? "加密" : "解密") << "失败，错误码"
                   << int(GetLastError());
        return QByteArray();
    }

    const QByteArray result(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return result;
}
#endif
}  // namespace

QByteArray TokenStore::protect(const QByteArray &plain) {
#if defined(Q_OS_WIN)
    const QByteArray blob = dpapiCall(plain, true);
    if (!blob.isEmpty()) {
        return QByteArray(kTokenMagic, kTokenMagicLen) + blob;
    }
    // DPAPI 极少失败（凭据存储坏了、内存不够）。真失败就退回混淆，**但要让人看见** ——
    // 悄悄降级成弱保护比直接失败更糟。
    qWarning() << "DPAPI 加密失败，这次的令牌只能用旧的混淆写盘。";
#else
    // 本工程目前只发 Windows（PCL 目录、注册表这些都是 Windows 的东西）。
    // 其它平台暂时沿用旧的混淆，见上面的说明。
#endif
    return legacyXor(plain);
}

QByteArray TokenStore::unprotect(const QByteArray &stored) {
    if (stored.isEmpty()) {
        return QByteArray();
    }
#if defined(Q_OS_WIN)
    if (stored.startsWith(QByteArray(kTokenMagic, kTokenMagicLen))) {
        return dpapiCall(stored.mid(kTokenMagicLen), false);
    }
#endif
    return legacyXor(stored);
}

bool TokenStore::writeJson(const QString &key, const QJsonObject &json, QString *errorOut) {
    QSettings settings;
    const QByteArray rawData = QJsonDocument(json).toJson(QJsonDocument::Compact);
    settings.setValue(key, protect(rawData.toBase64()));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("凭据写盘失败（%1）：%2")
                            .arg(key, settings.fileName());
        }
        qWarning() << "凭据写盘失败：" << key << settings.fileName();
        return false;
    }
    return true;
}

QJsonObject TokenStore::readJson(const QString &key) {
    QSettings settings;
    const QByteArray encrypted = settings.value(key).toByteArray();
    if (encrypted.isEmpty()) {
        return QJsonObject();
    }

    const QByteArray rawData = QByteArray::fromBase64(unprotect(encrypted));
    const QJsonDocument document = QJsonDocument::fromJson(rawData);
    return document.isObject() ? document.object() : QJsonObject();
}

bool TokenStore::hasValue(const QString &key) {
    QSettings settings;
    return !settings.value(key).toByteArray().isEmpty();
}

void TokenStore::clear(const QString &key) {
    QSettings settings;
    settings.remove(key);
    settings.sync();
}
