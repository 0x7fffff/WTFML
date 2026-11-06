#include "FileAssociations.hpp"

#include <QCoreApplication>
#include <QDir>

#include <windows.h>

namespace FileAssociations {
namespace {
QString normalizedExtension(const QString &extension) {
    QString ext = extension.trimmed().toLower();
    if (!ext.startsWith(QLatin1Char('.'))) ext.prepend(QLatin1Char('.'));
    return ext;
}

QString progId(const QString &extension) {
    return QStringLiteral("WTFML") + normalizedExtension(extension).mid(1);
}

/// 真实根是 HKCU\Software\Classes；测试缝把它换成 HKCU\Software\Classes\WTFMLSelfTest。
QString classesSubKey() {
    const QString override = qEnvironmentVariable("WTFML_ASSOC_CLASSES_ROOT");
    if (!override.isEmpty()) {
        QString path = override;
        const QString prefix = QStringLiteral("HKEY_CURRENT_USER\\");
        if (path.startsWith(prefix, Qt::CaseInsensitive)) path = path.mid(prefix.size());
        return path;
    }
    return QStringLiteral("Software\\Classes");
}

QString shellCommand() {
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return QLatin1Char('"') + exe + QStringLiteral("\" \"%1\"");
}

bool createKey(HKEY root, const QString &subKey, HKEY *out, QString *error) {
    const std::wstring path = subKey.toStdWString();
    LSTATUS status = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE | KEY_READ,
                                     nullptr, out, nullptr);
    if (status != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("RegCreateKeyEx 失败（%1）：%2").arg(status).arg(subKey);
        return false;
    }
    return true;
}

bool setDefaultValue(HKEY key, const QString &value, QString *error) {
    const std::wstring text = value.toStdWString();
    const LSTATUS status = RegSetValueExW(key, nullptr, 0, REG_SZ,
                                          reinterpret_cast<const BYTE *>(text.c_str()),
                                          DWORD((text.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("RegSetValueEx 失败（%1）").arg(status);
        return false;
    }
    return true;
}

bool setNamedValue(HKEY key, const wchar_t *name, const QString &value, QString *error) {
    const std::wstring text = value.toStdWString();
    const LSTATUS status = RegSetValueExW(key, name, 0, REG_SZ,
                                          reinterpret_cast<const BYTE *>(text.c_str()),
                                          DWORD((text.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("RegSetValueEx 失败（%1）").arg(status);
        return false;
    }
    return true;
}

QString defaultValue(HKEY root, const QString &subKey) {
    const std::wstring path = subKey.toStdWString();
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return {};
    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(key, nullptr, nullptr, &type,
                                            reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) return {};
    return QString::fromWCharArray(buffer);
}

bool writeProgId(HKEY classes, const QString &extension, QString *error) {
    const QString id = progId(extension);
    HKEY key = nullptr;
    if (!createKey(classes, id + QStringLiteral("\\shell\\open\\command"), &key, error)) return false;
    const bool ok = setDefaultValue(key, shellCommand(), error);
    RegCloseKey(key);
    if (!ok) return false;
    if (!createKey(classes, id, &key, error)) return false;
    setNamedValue(key, L"FriendlyTypeName", QStringLiteral("WTFML 打开的文件"), error);
    RegCloseKey(key);
    return true;
}

bool setDefault(const QString &extension, QString *error) {
    HKEY classes = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, classesSubKey().toStdWString().c_str(), 0,
                                   KEY_READ | KEY_WRITE, &classes);
    if (status != ERROR_SUCCESS) {
        status = RegCreateKeyExW(HKEY_CURRENT_USER, classesSubKey().toStdWString().c_str(), 0,
                                 nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &classes, nullptr);
    }
    if (status != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("打不开注册表根（%1，%2）。").arg(status).arg(classesSubKey());
        return false;
    }
    const QString ext = normalizedExtension(extension);
    HKEY extKey = nullptr;
    const bool ok = writeProgId(classes, ext, error)
        && createKey(classes, ext, &extKey, error)
        && setDefaultValue(extKey, progId(ext), error);
    if (extKey != nullptr) RegCloseKey(extKey);
    RegCloseKey(classes);
    return ok && isDefaultFor(ext);
}

bool addOpenWith(const QString &extension, QString *error) {
    HKEY classes = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, classesSubKey().toStdWString().c_str(), 0, nullptr, 0,
                        KEY_READ | KEY_WRITE, nullptr, &classes, nullptr) != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("打不开注册表根。");
        return false;
    }
    const QString ext = normalizedExtension(extension);
    bool ok = writeProgId(classes, ext, error);
    HKEY list = nullptr;
    if (ok && createKey(classes, ext + QStringLiteral("\\OpenWithProgids"), &list, error)) {
        ok = setNamedValue(list, progId(ext).toStdWString().c_str(), QString(), error);
    }
    if (list != nullptr) RegCloseKey(list);
    RegCloseKey(classes);
    return ok;
}
}  // namespace

QStringList mcOnlyExtensions() {
    return {QStringLiteral(".nbt"), QStringLiteral(".litematic"), QStringLiteral(".mrpack")};
}

QStringList openableExtensions() {
    QStringList all = mcOnlyExtensions();
    all << QStringLiteral(".jar") << QStringLiteral(".zip");
    return all;
}

bool isOpenable(const QString &extension) {
    return openableExtensions().contains(normalizedExtension(extension));
}

bool isMcOnly(const QString &extension) {
    return mcOnlyExtensions().contains(normalizedExtension(extension));
}

bool registerDefault(const QString &extension, QString *error) {
    if (!isMcOnly(extension)) {
        if (error) *error = QStringLiteral("%1 不是 MC 独有的后缀，不能抢默认。").arg(extension);
        return false;
    }
    return setDefault(extension, error);
}

bool registerOpenWith(const QString &extension, QString *error) {
    if (!isOpenable(extension)) {
        if (error) *error = QStringLiteral("%1 不是能打开的后缀。").arg(extension);
        return false;
    }
    return isMcOnly(extension) ? setDefault(extension, error) : addOpenWith(extension, error);
}

bool isDefaultFor(const QString &extension) {
    const QString ext = normalizedExtension(extension);
    const QString key = classesSubKey() + QLatin1Char('\\') + ext;
    return defaultValue(HKEY_CURRENT_USER, key) == progId(ext);
}

int registerMcDefaults(QStringList *errors) {
    int done = 0;
    for (const QString &ext : mcOnlyExtensions()) {
        QString error;
        if (registerDefault(ext, &error)) {
            ++done;
        } else if (errors != nullptr) {
            errors->append(QStringLiteral("%1：%2").arg(ext, error));
        }
    }
    return done;
}

QString runSelfTest() {
    QStringList problems;
    QStringList errors;
    const int done = registerMcDefaults(&errors);
    if (done != 3) problems << QStringLiteral("registerMcDefaults=%1 %2").arg(done).arg(errors.join(QStringLiteral("; ")));
    for (const QString &ext : mcOnlyExtensions()) {
        if (!isDefaultFor(ext)) problems << QStringLiteral("isDefaultFor 失败：%1").arg(ext);
    }
    QString error;
    if (!registerOpenWith(QStringLiteral(".jar"), &error) || isDefaultFor(QStringLiteral(".jar"))) {
        problems << QStringLiteral(".jar 打开方式登记失败或改了默认：%1").arg(error);
    }
    if (!registerOpenWith(QStringLiteral(".zip"), &error) || isDefaultFor(QStringLiteral(".zip"))) {
        problems << QStringLiteral(".zip 打开方式登记失败或改了默认：%1").arg(error);
    }
    if (registerDefault(QStringLiteral(".zip"), nullptr)) problems << QStringLiteral(".zip 竟被允许设默认");
    return problems.join(QLatin1Char('\n'));
}

}  // namespace FileAssociations


