// Test-process-only Windows import substitute. No product fault hooks or DLL patching.
#include "auth/TokenStore.hpp"
#include "util/AppPaths.hpp"
#include "support/SettingsIsolation.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QFile>
#include <cstdio>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>

namespace {
bool rejectProtection = false;
using Protect = decltype(&CryptProtectData);
BOOL WINAPI controlledProtect(DATA_BLOB *input, LPCWSTR description, DATA_BLOB *entropy,
    PVOID reserved, CRYPTPROTECT_PROMPTSTRUCT *prompt, DWORD flags, DATA_BLOB *output) {
    if (rejectProtection) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    const auto module = GetModuleHandleW(L"crypt32.dll");
    const auto address = module ? GetProcAddress(module, "CryptProtectData") : nullptr;
    Protect real = nullptr;
    static_assert(sizeof(real) == sizeof(address));
    std::memcpy(&real, &address, sizeof(real));
    if (!real) { SetLastError(ERROR_PROC_NOT_FOUND); return FALSE; }
    return real(input, description, entropy, reserved, prompt, flags, output);
}
}
extern "C" {
Protect __imp_CryptProtectData = &controlledProtect;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("WTFML-token-protection-fault"); app.setApplicationName("isolated");
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, root.filePath("system"));
    requireIsolatedSettings(root.path(), "token_protection_fault");
    app.setProperty("wtfml.testDefaultMinecraftDir", root.filePath("game"));
    AppPaths::setMinecraftDir(root.filePath("game"));
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
        if (!ok) ++failures;
    };
    const QJsonObject original{{"access_token", "synthetic-original-access"}, {"refresh_token", "synthetic-original-refresh"}};
    const auto key = TokenStore::microsoftKey();
    QString problem;
    check(TokenStore::writeJson(key, original, &problem), "real DPAPI saves the initial private fixture");
    QSettings settings;
    QFile file(settings.fileName());
    const auto bytes = [&] { file.close(); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
    const auto before = bytes(); file.close();
    check(!before.isEmpty(), "original settings bytes exist");
    rejectProtection = true;
    check(TokenStore::protect("synthetic-new-credential").isEmpty(), "encryption failure returns no downgraded bytes");
    check(!TokenStore::writeJson(key, {{"access_token", "synthetic-new-access"}}, &problem) && !problem.isEmpty(),
        "credential write fails explicitly when protection fails");
    check(bytes() == before, "failed protection preserves the exact previous settings file");
    file.close();
    rejectProtection = false;
    check(TokenStore::readJson(key) == original, "previous credentials remain decryptable after failed replacement");
    check(TokenStore::writeJson(key, original, &problem) && TokenStore::readJson(key) == original,
        "writes recover when the real protection service is available again");
    return failures ? 1 : 0;
}
