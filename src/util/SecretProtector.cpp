/**
 * @file SecretProtector.cpp
 * @brief Windows DPAPI 封装（见 .hpp 里的边界说明）。
 */

#include "SecretProtector.hpp"

#include <QDebug>

#if defined(Q_OS_WIN)
// NOTE: windows.h 放在所有 Qt 头之后；NOMINMAX 给 std::min/std::max 让路。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#endif

namespace SecretProtector {

QByteArray withMagic(const QByteArray &blob, const QByteArray &magic) {
    return magic + blob;
}

bool hasMagic(const QByteArray &stored, const QByteArray &magic) {
    return !magic.isEmpty() && stored.startsWith(magic);
}

#if defined(Q_OS_WIN)
namespace {
/// 调 DPAPI 的公共部分。out 用 LocalFree 还回去是微软的约定。
QByteArray dpapiCall(const QByteArray &input, bool protect, const wchar_t *description) {
    if (input.isEmpty()) {
        return {};
    }

    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(input.constData()));
    in.cbData = static_cast<DWORD>(input.size());

    DATA_BLOB out{};
    const BOOL ok = protect
                        ? CryptProtectData(&in, description, nullptr, nullptr, nullptr,
                                           CRYPTPROTECT_UI_FORBIDDEN, &out)
                        : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                                             CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (ok == FALSE) {
        qWarning() << "DPAPI" << (protect ? "加密" : "解密") << "失败，错误码"
                   << int(GetLastError());
        return {};
    }

    const QByteArray result(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return result;
}
}  // namespace
#endif

QByteArray protect(const QByteArray &plain, const QByteArray &magic) {
    if (plain.isEmpty() || magic.isEmpty()) {
        return {};
    }
#if defined(Q_OS_WIN)
    const QByteArray blob = dpapiCall(plain, true, L"WTFML 敏感数据");
    if (blob.isEmpty()) {
        return {};
    }
    return withMagic(blob, magic);
#else
    // Windows 是发布目标。别的平台上不假装有加密：返回空 = 拒绝保存。
    return {};
#endif
}

QByteArray unprotect(const QByteArray &stored, const QByteArray &magic) {
    if (!hasMagic(stored, magic)) {
        return {};
    }
#if defined(Q_OS_WIN)
    return dpapiCall(stored.mid(magic.size()), false, nullptr);
#else
    return {};
#endif
}

}  // namespace SecretProtector
