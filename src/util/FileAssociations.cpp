#include "FileAssociations.hpp"

#include <windows.h>
#include <shlobj.h>

namespace FileAssociations {
namespace {

const QString kClassesRoot = QStringLiteral("Software\\Classes");

QString normalizedExtension(const QString &extension) {
    QString ext = extension.trimmed().toLower();
    if (!ext.startsWith(QLatin1Char('.'))) ext.prepend(QLatin1Char('.'));
    return ext;
}

/// MC 独有、旧版曾设成默认打开方式的那几个。
QStringList mcOnlyExtensions() {
    return {QStringLiteral(".nbt"), QStringLiteral(".litematic"), QStringLiteral(".schematic"),
            QStringLiteral(".schem"), QStringLiteral(".mrpack")};
}

/// 旧版真正写下的 ProgID 是 `WTFMLnbt`（没有点）；issue 里写作 `WTFML.nbt`。
/// 两种都认，清理时两种都查一遍，免得按文档手改过的机器上留下残项。
QStringList progIdCandidates(const QString &extension) {
    const QString ext = normalizedExtension(extension);
    return {QStringLiteral("WTFML") + ext.mid(1), QStringLiteral("WTFML") + ext};
}

QString actualProgId(const QString &extension) {
    return QStringLiteral("WTFML") + normalizedExtension(extension).mid(1);
}

/// 真实根是 HKCU\Software\Classes；测试缝把它换成 WTFML_ASSOC_CLASSES_ROOT 指的测试子键。
QString classesSubKey() {
    const QString override = qEnvironmentVariable("WTFML_ASSOC_CLASSES_ROOT").trimmed();
    if (!override.isEmpty()) {
        QString path = override;
        const QString fullPrefix = QStringLiteral("HKEY_CURRENT_USER\\");
        const QString shortPrefix = QStringLiteral("HKCU\\");
        if (path.startsWith(fullPrefix, Qt::CaseInsensitive)) {
            path = path.mid(fullPrefix.size());
        } else if (path.startsWith(shortPrefix, Qt::CaseInsensitive)) {
            path = path.mid(shortPrefix.size());
        }
        while (path.endsWith(QLatin1Char('\\'))) path.chop(1);
        return path;
    }
    return kClassesRoot;
}

bool isRealClassesRoot(const QString &subKey) {
    return subKey.compare(kClassesRoot, Qt::CaseInsensitive) == 0;
}

QString childKey(const QString &parent, const QString &child) {
    return parent + QLatin1Char('\\') + child;
}

bool createKey(HKEY root, const QString &subKey, HKEY *out, QString *error) {
    const std::wstring path = subKey.toStdWString();
    const LSTATUS status = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE | KEY_READ,
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

bool readDefaultValue(HKEY root, const QString &subKey, QString *out) {
    out->clear();
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.toStdWString().c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(key, nullptr, nullptr, &type,
                                            reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) return false;
    *out = QString::fromWCharArray(buffer);
    return true;
}

bool keyExists(HKEY root, const QString &subKey) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.toStdWString().c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(key);
    return true;
}

bool namedValueExists(HKEY root, const QString &subKey, const QString &name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.toStdWString().c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status = RegQueryValueExW(key, name.toStdWString().c_str(), nullptr, nullptr,
                                            nullptr, nullptr);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

/// 递归删掉一个键（连同键本身）。不存在算成功、不算清理项。
int deleteKeyTree(HKEY root, const QString &subKey, QStringList *problems) {
    const std::wstring path = subKey.toStdWString();
    HKEY key = nullptr;
    LSTATUS status = RegOpenKeyExW(root, path.c_str(), 0, KEY_READ | KEY_WRITE, &key);
    if (status == ERROR_FILE_NOT_FOUND) return 0;
    if (status != ERROR_SUCCESS) {
        if (problems) problems->append(QStringLiteral("打不开 %1（%2）").arg(subKey).arg(status));
        return 0;
    }
    RegCloseKey(key);
    status = RegDeleteTreeW(root, path.c_str());
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        if (problems) problems->append(QStringLiteral("清不掉 %1 的子项（%2）").arg(subKey).arg(status));
        return 0;
    }
    status = RegDeleteKeyW(root, path.c_str());
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        if (problems) problems->append(QStringLiteral("删不掉 %1（%2）").arg(subKey).arg(status));
        return 0;
    }
    return 1;
}

/// 默认值只在等于本程序的 ProgID 时才清。别人的默认打开方式原样不动。
int clearDefaultIfOurs(HKEY root, const QString &subKey, const QString &progId,
                       QStringList *problems) {
    HKEY key = nullptr;
    LSTATUS status = RegOpenKeyExW(root, subKey.toStdWString().c_str(), 0, KEY_READ | KEY_WRITE,
                                   &key);
    if (status == ERROR_FILE_NOT_FOUND) return 0;
    if (status != ERROR_SUCCESS) {
        if (problems) problems->append(QStringLiteral("打不开 %1（%2）").arg(subKey).arg(status));
        return 0;
    }
    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    status = RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
    const QString current = status == ERROR_SUCCESS && type == REG_SZ
                                ? QString::fromWCharArray(buffer)
                                : QString();
    if (current.compare(progId, Qt::CaseInsensitive) != 0) {
        RegCloseKey(key);
        return 0;
    }
    status = RegDeleteValueW(key, nullptr);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        if (problems) problems->append(QStringLiteral("清不掉 %1 的默认值（%2）").arg(subKey).arg(status));
        return 0;
    }
    return 1;
}

/// `OpenWithProgids` 里只删名字是本程序 ProgID 的那一个值。
int removeOpenWithValue(HKEY root, const QString &subKey, const QString &progId,
                        QStringList *problems) {
    HKEY key = nullptr;
    LSTATUS status = RegOpenKeyExW(root, subKey.toStdWString().c_str(), 0, KEY_READ | KEY_WRITE,
                                   &key);
    if (status == ERROR_FILE_NOT_FOUND) return 0;
    if (status != ERROR_SUCCESS) {
        if (problems) problems->append(QStringLiteral("打不开 %1（%2）").arg(subKey).arg(status));
        return 0;
    }
    const std::wstring name = progId.toStdWString();
    status = RegQueryValueExW(key, name.c_str(), nullptr, nullptr, nullptr, nullptr);
    if (status == ERROR_FILE_NOT_FOUND) {
        RegCloseKey(key);
        return 0;
    }
    if (status != ERROR_SUCCESS) {
        if (problems) problems->append(QStringLiteral("读不了 %1\\%2（%3）").arg(subKey, progId).arg(status));
        RegCloseKey(key);
        return 0;
    }
    status = RegDeleteValueW(key, name.c_str());
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        if (problems) problems->append(QStringLiteral("清不掉 %1\\%2（%3）").arg(subKey, progId).arg(status));
        return 0;
    }
    return 1;
}

}  // namespace

QStringList openableExtensions() {
    QStringList all = mcOnlyExtensions();
    all << QStringLiteral(".jar") << QStringLiteral(".zip");
    return all;
}

bool isOpenable(const QString &extension) {
    return openableExtensions().contains(normalizedExtension(extension));
}

int cleanupLegacyAssociations(QStringList *problems) {
    QStringList localProblems;
    int removed = 0;
    const QString classes = classesSubKey();
    for (const QString &ext : openableExtensions()) {
        for (const QString &id : progIdCandidates(ext)) {
            removed += deleteKeyTree(HKEY_CURRENT_USER, childKey(classes, id), &localProblems);
        }
        for (const QString &id : progIdCandidates(ext)) {
            removed += clearDefaultIfOurs(HKEY_CURRENT_USER, childKey(classes, ext), id,
                                          &localProblems);
        }
        const QString openWith = childKey(childKey(classes, ext), QStringLiteral("OpenWithProgids"));
        for (const QString &id : progIdCandidates(ext)) {
            removed += removeOpenWithValue(HKEY_CURRENT_USER, openWith, id, &localProblems);
        }
    }
    if (removed > 0) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
    if (problems != nullptr) *problems = localProblems;
    return removed;
}

QString runSelfTest() {
    QStringList problems;
    const QString configuredRoot = qEnvironmentVariable("WTFML_ASSOC_CLASSES_ROOT").trimmed();
    const QString rootKey = classesSubKey();
    if (configuredRoot.isEmpty()) {
        return QStringLiteral("WTFML_ASSOC_CLASSES_ROOT 未设置：自检不碰真实注册表。");
    }
    if (isRealClassesRoot(rootKey)) {
        return QStringLiteral("WTFML_ASSOC_CLASSES_ROOT 指向真实 HKCU\\Software\\Classes：拒绝自检。");
    }
    if (!rootKey.contains(QStringLiteral("WTFML"), Qt::CaseInsensitive)) {
        return QStringLiteral("WTFML_ASSOC_CLASSES_ROOT 不含 WTFML：测试根不像专用子键，拒绝自检。");
    }

    const auto expect = [&problems](bool ok, const QString &what) {
        if (!ok) problems.append(what);
    };
    const auto seedDefault = [&](const QString &subKey, const QString &value) {
        HKEY key = nullptr;
        QString error;
        const bool opened = createKey(HKEY_CURRENT_USER, subKey, &key, &error);
        const bool written = opened && setDefaultValue(key, value, &error);
        if (key != nullptr) RegCloseKey(key);
        expect(opened && written, QStringLiteral("造测试数据：%1（%2）").arg(subKey, error));
        return opened && written;
    };
    const auto seedNamed = [&](const QString &subKey, const QString &name, const QString &value) {
        HKEY key = nullptr;
        QString error;
        const bool opened = createKey(HKEY_CURRENT_USER, subKey, &key, &error);
        const bool written = opened && setNamedValue(key, name.toStdWString().c_str(), value, &error);
        if (key != nullptr) RegCloseKey(key);
        expect(opened && written, QStringLiteral("造测试数据：%1\\%2（%3）").arg(subKey, name, error));
        return opened && written;
    };

    // 从干净的测试子键开始：上一次自检若中途失败，残留不能被当成"这次造出来的"。
    deleteKeyTree(HKEY_CURRENT_USER, rootKey, nullptr);
    expect(!keyExists(HKEY_CURRENT_USER, rootKey),
           QStringLiteral("测试子键删不掉：%1").arg(rootKey));
    if (problems.isEmpty()) {
        // 1) 造出旧版注册的样子：ProgID 键、默认值、OpenWithProgids 候选。
        for (const QString &ext : openableExtensions()) {
            const QString id = actualProgId(ext);
            const QString progIdKey = childKey(rootKey, id);
            seedDefault(childKey(progIdKey, QStringLiteral("shell\\open\\command")),
                        QStringLiteral("\"C:\\Old\\WTFMinecraftLauncher.exe\" \"%1\""));
            seedNamed(progIdKey, QStringLiteral("FriendlyTypeName"), QStringLiteral("WTFML 打开的文件"));
            if (mcOnlyExtensions().contains(ext)) {
                seedDefault(childKey(rootKey, ext), id);
            } else {
                seedNamed(childKey(childKey(rootKey, ext), QStringLiteral("OpenWithProgids")), id,
                          QString());
            }
        }
        // issue 里写的 ProgID 形状（带点）：旧版代码实际没这么写，但文档这么写过。
        seedDefault(childKey(childKey(rootKey, QStringLiteral("WTFML.nbt")),
                             QStringLiteral("shell\\open\\command")),
                    QStringLiteral("\"C:\\Old\\WTFMinecraftLauncher.exe\" \"%1\""));

        // 2) 别人的关联：默认打开方式是 7-Zip，候选里也有它。
        seedDefault(childKey(rootKey, QStringLiteral(".zip")), QStringLiteral("7-Zip.zip"));
        seedNamed(childKey(childKey(rootKey, QStringLiteral(".zip")), QStringLiteral("OpenWithProgids")),
                  QStringLiteral("7-Zip.zip"), QString());

        // 3) 清理。
        QStringList errors;
        const int removed = cleanupLegacyAssociations(&errors);
        expect(removed == 15, QStringLiteral("清理项数=%1（应为 15）").arg(removed));
        expect(errors.isEmpty(), QStringLiteral("清理报错：%1").arg(errors.join(QStringLiteral("; "))));

        // 4) 本程序的都清干净了，别人的原样还在。
        for (const QString &ext : openableExtensions()) {
            const QString id = actualProgId(ext);
            expect(!keyExists(HKEY_CURRENT_USER, childKey(rootKey, id)),
                   QStringLiteral("ProgID 键还在：%1").arg(id));
            expect(keyExists(HKEY_CURRENT_USER, childKey(rootKey, ext)),
                   QStringLiteral("`%1` 键被整个删掉了（只该清默认值）").arg(ext));
            QString current;
            const bool hasDefault = readDefaultValue(HKEY_CURRENT_USER, childKey(rootKey, ext), &current);
            if (mcOnlyExtensions().contains(ext)) {
                expect(!hasDefault || current.compare(id, Qt::CaseInsensitive) != 0,
                       QStringLiteral("%1 的默认值还指着本程序：%2").arg(ext, current));
            } else {
                const QString openWith =
                    childKey(childKey(rootKey, ext), QStringLiteral("OpenWithProgids"));
                for (const QString &candidate : progIdCandidates(ext)) {
                    expect(!namedValueExists(HKEY_CURRENT_USER, openWith, candidate),
                           QStringLiteral("%1\\OpenWithProgids 里还有 %2").arg(ext, candidate));
                }
            }
        }
        expect(!keyExists(HKEY_CURRENT_USER, childKey(childKey(rootKey, QStringLiteral("WTFML.nbt")),
                                                      QStringLiteral("shell"))),
               QStringLiteral("带点的 ProgID 键还在：WTFML.nbt"));

        QString zipDefault;
        expect(readDefaultValue(HKEY_CURRENT_USER, childKey(rootKey, QStringLiteral(".zip")), &zipDefault)
                   && zipDefault == QStringLiteral("7-Zip.zip"),
               QStringLiteral(".zip 的别人的默认值被动了：%1").arg(zipDefault));
        expect(namedValueExists(HKEY_CURRENT_USER,
                                childKey(childKey(rootKey, QStringLiteral(".zip")),
                                         QStringLiteral("OpenWithProgids")),
                                QStringLiteral("7-Zip.zip")),
               QStringLiteral(".zip\\OpenWithProgids 里别人的值被删了"));

        // 5) 重复跑：没有可清的项，返回 0，什么都不再动。
        QStringList againErrors;
        const int again = cleanupLegacyAssociations(&againErrors);
        expect(again == 0, QStringLiteral("重复清理还动了 %1 项").arg(again));
        expect(againErrors.isEmpty(),
               QStringLiteral("重复清理报错：%1").arg(againErrors.join(QStringLiteral("; "))));
        QString zipDefaultAgain;
        expect(readDefaultValue(HKEY_CURRENT_USER, childKey(rootKey, QStringLiteral(".zip")),
                                &zipDefaultAgain)
                   && zipDefaultAgain == QStringLiteral("7-Zip.zip"),
               QStringLiteral("重复清理动了 .zip 的默认值：%1").arg(zipDefaultAgain));
    }

    // 6) 收尾：删掉测试子键。
    deleteKeyTree(HKEY_CURRENT_USER, rootKey, nullptr);
    expect(!keyExists(HKEY_CURRENT_USER, rootKey),
           QStringLiteral("收尾：测试子键没删掉：%1").arg(rootKey));
    return problems.join(QLatin1Char('\n'));
}

}  // namespace FileAssociations
