/**
 * @file AsciiPathAlias.cpp
 * @brief 实现 Java 8 的 ASCII 路径别名（junction/硬链接）。
 * @note 规则与原因见 .hpp；这里只保留实现和不能按直觉修改的约束。
 */

#include "AsciiPathAlias.hpp"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <cstring>

#include "../manage/InstanceFiles.hpp"
#include "../util/AppPaths.hpp"
#include "../util/WriteDirectoryGuard.hpp"

#if defined(Q_OS_WIN)
// NOTE: 放在所有 Qt 头之后 —— windows.h 定义的东西太多（min/max 宏……），先于 Qt 包含
// 容易撞名。和 TokenStore.cpp / LauncherManager.cpp 里那一套同一个写法。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// FSCTL_SET_REPARSE_POINT 在 winioctl.h 里，而 WIN32_LEAN_AND_MEAN 不会把它拉进来。
#include <winioctl.h>
#endif

namespace {

/// 目录联接（junction）那条 reparse 数据的头部。
///
/// 自己声明，不用 `REPARSE_DATA_BUFFER` / `MountPointReparseBuffer`：那两个在 mingw 的
/// 头文件里时有时无（`winnt.h` 只在一部分版本里有），而这个结构**是 Windows 定义的线上
/// 格式**（`IO_REPARSE_TAG_MOUNT_POINT` 那份），写死它比自己赌一个宏存在更可靠。
/// 布局：tag(4) + 长度(2) + 保留(2) + 四个 USHORT + 路径缓冲区。**必须 1 字节对齐**，
/// 否则 USHORT 字段会因为编译器插的填充而整体移位。
#pragma pack(push, 1)
struct MountPointReparseData {
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    USHORT SubstituteNameOffset;
    USHORT SubstituteNameLength;
    USHORT PrintNameOffset;
    USHORT PrintNameLength;
    WCHAR PathBuffer[1];
};
#pragma pack(pop)

/// 句柄那两个"这是不是同一份文件"的字段（卷序列号 + 文件索引）。
struct FileIdentity {
    DWORD volume;
    DWORD indexHigh;
    DWORD indexLow;
};

QString translate(const char *text) {
    return QCoreApplication::translate("AsciiPathAlias", text);
}

/// 删掉别名里那批 jar 的**硬链接**（只删目录项，真实 jar 一个字节都不动），然后把空目录删掉。
void removeAliasJars(const QString &jarsDir) {
    const QStringList names =
        QDir(jarsDir).entryList(QStringList() << QStringLiteral("*.jar"), QDir::Files);
    for (const QString &name : names) {
        QFile::remove(jarsDir + QLatin1Char('/') + name);
    }
    QDir().rmdir(jarsDir);
}

#if defined(Q_OS_WIN)
/// 把 GetLastError() 变成一句人话（拿不到系统文案时退回错误码本身）。
///
/// NOTE: 一定要在**失败之后立刻**调用：中间插一个会改 last error 的调用（哪怕是一次
/// CreateDirectoryW），拿到的就是另一条错误了。
QString lastErrorText() {
    const DWORD code = GetLastError();
    LPWSTR buffer = nullptr;
    const DWORD length =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text;
    if (length > 0 && buffer != nullptr) {
        text = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    if (text.isEmpty()) {
        text = translate("Windows 错误码 %1").arg(code);
    }
    return text;
}

/// 这个目录在不在（联接指向的目标没了时**不算**在：那时 GetFileAttributesW 会失败）。
bool isLiveDir(const QString &path) {
    return QFileInfo(path).isDir();
}

/**
 * @brief 建一个目录联接：`link` → `target`。
 * @return 成功返回 true；否则 errorOut 里是给用户看的一句话。
 *
 * 它的目标存的是 **NT 对象路径**（`\??\D:\...`），不是普通的 `D:\...` —— 这一条写错了的
 * 症状是联接建得出来、但指向一个不存在的路径（"点进去说找不到"），而不是报错。
 *
 * NOTE: 目标用**两种**名字存（SubstituteName 给内核、PrintName 给人看和给
 * `QFileInfo::symLinkTarget()` 读）。两个名字任何一个漏了，行为都还"看起来正常"，
 * 但 `dir` 命令、资源管理器、以及"这个联接还指不指着原来那个实例"这条判据会瞎掉。
 */
bool createJunction(const QString &link, const QString &target, QString *errorOut) {
    const QString targetPath = QDir::toNativeSeparators(QDir(target).absolutePath());
    const QString linkPath = QDir::toNativeSeparators(QDir(link).absolutePath());

    if (targetPath.startsWith(QStringLiteral("\\\\"))) {
        if (errorOut != nullptr) {
            *errorOut = translate("别名不支持网络路径（%1）。").arg(targetPath);
        }
        return false;
    }

    const std::wstring substitute = (QStringLiteral("\\??\\") + targetPath).toStdWString();
    const std::wstring print = targetPath.toStdWString();
    const USHORT substituteBytes = static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
    const USHORT printBytes = static_cast<USHORT>(print.size() * sizeof(wchar_t));
    const int headerBytes = 8;         // tag + 长度 + 保留
    const int mountHeaderBytes = 8;    // 四个 USHORT
    const int pathOffset = headerBytes + mountHeaderBytes;
    const int totalBytes =
        pathOffset + substituteBytes + static_cast<int>(sizeof(wchar_t)) + printBytes +
        static_cast<int>(sizeof(wchar_t));

    QByteArray buffer(totalBytes, '\0');
    auto *header = reinterpret_cast<MountPointReparseData *>(buffer.data());
    header->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    header->ReparseDataLength = static_cast<USHORT>(totalBytes - headerBytes);
    header->Reserved = 0;
    header->SubstituteNameOffset = 0;
    header->SubstituteNameLength = substituteBytes;
    header->PrintNameOffset = static_cast<USHORT>(substituteBytes + sizeof(wchar_t));
    header->PrintNameLength = printBytes;
    std::memcpy(buffer.data() + pathOffset, substitute.data(), substituteBytes);
    std::memcpy(buffer.data() + pathOffset + substituteBytes + sizeof(wchar_t), print.data(),
                printBytes);

    const std::wstring linkWide = linkPath.toStdWString();
    if (!CreateDirectoryW(linkWide.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        if (errorOut != nullptr) {
            *errorOut = translate("建不出别名目录 %1：%2").arg(linkPath, lastErrorText());
        }
        return false;
    }

    HANDLE handle = CreateFileW(linkWide.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (errorOut != nullptr) {
            *errorOut = translate("打不开别名目录 %1：%2").arg(linkPath, lastErrorText());
        }
        return false;
    }
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), totalBytes,
                                    nullptr, 0, &returned, nullptr);
    const QString failure = ok ? QString() : lastErrorText();
    CloseHandle(handle);
    if (!ok) {
        if (errorOut != nullptr) {
            *errorOut = translate("%1 变不成别名（%2）：%3").arg(linkPath, targetPath, failure);
        }
        return false;
    }
    return true;
}

/// 删掉一个目录联接。**只删联接本身**，目标一个字节都不动。
///
/// 这里用的是 `RemoveDirectoryW`，不是 `QDir::removeRecursively()`：后者是"把目录清空
/// 再删"，对联接来说"清空"落在哪一边不是我们能赌的 —— 赌错就是把**用户的实例目录**
/// 删了一部分。所以这里只删这一个目录项。
/// NOTE: **不先判"它在不在"**：联接指向的目标没了时（实例改名、被删，也正是这个函数最常被
/// 调用的时候）`QFileInfo::exists()` 和 `isSymLink()` 都可能给 false，判一下就漏删。
/// 直接删、失败就算了。
void removeJunction(const QString &link) {
    const std::wstring linkWide =
        QDir::toNativeSeparators(QDir(link).absolutePath()).toStdWString();
    RemoveDirectoryW(linkWide.c_str());
}

/// 两个路径指的不是同一个地方（只比字符串，做 cleanPath + 不区分大小写）。
bool samePath(const QString &left, const QString &right) {
    return QDir::cleanPath(left).compare(QDir::cleanPath(right), Qt::CaseInsensitive) == 0;
}

/// 建一个硬链接：`link` 和 `target` 指向同一份数据。跨盘会失败（那是 Windows 的规矩）。
bool createHardlink(const QString &link, const QString &target, QString *errorOut) {
    const std::wstring linkWide = QDir::toNativeSeparators(QDir(link).absolutePath()).toStdWString();
    const std::wstring targetWide =
        QDir::toNativeSeparators(QDir(target).absolutePath()).toStdWString();
    if (CreateHardLinkW(linkWide.c_str(), targetWide.c_str(), nullptr)) {
        return true;
    }
    if (errorOut != nullptr) {
        *errorOut = translate("建不出别名文件 %1（→ %2）：%3")
                        .arg(QDir::toNativeSeparators(link), QDir::toNativeSeparators(target),
                             lastErrorText());
    }
    return false;
}

/// 打开一个文件读它的身份（卷 + 文件索引）；打不开返回 false。
bool fileIdentity(const QString &path, FileIdentity *identityOut) {
    const std::wstring wide =
        QDir::toNativeSeparators(QDir(path).absolutePath()).toStdWString();
    HANDLE handle = CreateFileW(wide.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info;
    std::memset(&info, 0, sizeof(info));
    const BOOL ok = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);
    if (!ok) {
        return false;
    }
    identityOut->volume = info.dwVolumeSerialNumber;
    identityOut->indexHigh = info.nFileIndexHigh;
    identityOut->indexLow = info.nFileIndexLow;
    return true;
}

/**
 * @brief 两个路径是不是**同一份文件**（硬链接的判据只能是这个）。
 * @return 有一个打不开、或者不是同一份，都返回 false。
 *
 * 判据**不能**是"文件在不在"、也不能是"大小一样不一样"：客户端 jar 被重下过一次、
 * 实例被删掉又建了一个同名的，别名那一份还停在旧数据上，症状是**游戏跑的是上一份 jar**，
 * 而屏幕上一点异常都没有（版本号、模组列表全都对不上，用户会以为是我们改坏了版本）。
 */
bool sameFile(const QString &left, const QString &right) {
    FileIdentity a{};
    FileIdentity b{};
    if (!fileIdentity(left, &a) || !fileIdentity(right, &b)) {
        return false;
    }
    return a.volume == b.volume && a.indexHigh == b.indexHigh && a.indexLow == b.indexLow;
}

/// 这个联接指着谁（reparse 数据里那个目标）。
///
/// **必须**先问 `junctionTarget()`。我们建的是 `mklink /J` 那种目录联接
/// （IO_REPARSE_TAG_MOUNT_POINT），而 Qt 6.10.1 把这两种 reparse 分得很清：
/// `isSymLink()` 对目录联接近恒为 false，`symLinkTarget()` 读的也是符号链接那一份，
/// 对目录联接回**空串**。只用 `symLinkTarget()`（`linkUsable()` 同理）时症状不是报错，是
/// **静默**：`liveTargetOfAlias()` 恒回空 ⇒ 每一趟 `ensure()` 都把兄弟目录里**正在跑的那个实例**
/// 的别名当过期删掉，正在跑的那场游戏之后每写一个文件都失败（存档、日志轮转、
/// 还没加载过的类）。
QString targetOfAlias(const QString &link) {
    const QFileInfo info(link);
    QString target = info.symLinkTarget();
    if (target.isEmpty() && info.isJunction()) {
        target = info.junctionTarget();
    }
    return target;
}

/// 这个联结指着的地方对不对、而且目标真的在（= 不用重做）。
///
/// 两条都判：`isDir()` 问"通不通"，`targetOfAlias()` 问"指着的是不是那一份" —— 后者读的
/// 是 reparse 数据里的目标路径，悬空时也读得到（见 `targetOfAlias()` 那段：目录联接走
/// `junctionTarget()`）。
///
/// 读不出目标名字时**不能**只按"通不通"算，还得确认它真是个链接。
/// 只判 `actual.isEmpty() || samePath(...)` 会把**普通目录**也算成可用：`createJunction()`
/// 是"先 `mkpath` 再 `mklink /J`"，中间那一步失败（或者用户/杀软在位置上手建了一个空
/// 目录）留下的正是一个同名普通目录 —— 于是这一套别名被判"可用"，`--gameDir` 指向
/// `cache/ascii/<hash>/game` 这个空壳，游戏把存档、截图、logs 全写进缓存目录，
/// 而实例文件夹里一份都没有；下次清缓存就没了。natives 那一份同理（空的
/// java.library.path ⇒ UnsatisfiedLinkError）。
/// 反过来"每次启动都重做"的风险由另一条守住：目标读得出来时照旧比路径，
/// 只有真链接才会走到这里判 true。
bool linkUsable(const QString &link, const QString &target) {
    if (!isLiveDir(link)) {
        return false;
    }
    const QFileInfo info(link);
    const bool actuallyLinked = info.isJunction() || info.isSymLink();
    const QString actual = targetOfAlias(link);
    if (actual.isEmpty()) {
        return actuallyLinked;
    }
    return samePath(actual, target);
}

/// 这一套别名还指着一个**活着**的实例吗（清过期那几套时的判据）。
///
/// 判据是"**game 那个联结读出来的目标目录**还在不在"，不是"那个联结在不在"：实例被删掉
/// 之后，它的联结在 Windows 上**仍然是一个目录项**——`QFileInfo::isDir()` 那种问法会给
/// true，过期的那几套一套都清不掉。
QString liveTargetOfAlias(const QString &aliasRoot) {
    const QString game = aliasRoot + QStringLiteral("/game");
    const QString target = targetOfAlias(game);
    return QFileInfo(target).isDir() ? target : QString();
}
#else
bool isLiveDir(const QString &path) { return QFileInfo(path).isDir(); }
#endif

}  // namespace

bool AsciiPathAlias::hasNonAscii(const QString &text) {
    for (const QChar &character : text) {
        if (character.unicode() > 0x7F) {
            return true;
        }
    }
    return false;
}

QString AsciiPathAlias::jarFileNameInAlias(const QString &fileName) {
    if (!hasNonAscii(fileName)) {
        return fileName;  // 绝大多数 jar 都走这一条：名字一个字节都不动
    }
    // 只换名字，不换内容：硬链接指的仍是同一个文件（判据见 .hpp 的块注释）。
    // 扩展名固定写 `.jar`：进这个函数的只可能是 jar（调用方给的都是顶层的 *.jar）。
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(fileName.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
    return QStringLiteral("jar-") + digest + QStringLiteral(".jar");
}

AsciiPathAlias::Paths AsciiPathAlias::pathsFor(const QString &versionId) {
    // id 里可以是中文、`∞`、空格、括号，拼进路径就不是 ASCII 了；哈希是纯十六进制。
    // 不能用 qHash()：Qt 6 的 qHash(QString) 每次进程启动换一个随机种子（见 .hpp）。
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(versionId.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    Paths paths;
    paths.root = AppPaths::asciiAliasRootDir() + QLatin1Char('/') + digest;
    paths.gameDir = paths.root + QStringLiteral("/game");
    paths.jarsDir = paths.root + QStringLiteral("/jars");
    paths.nativesDir = paths.root + QStringLiteral("/natives");
    return paths;
}

bool AsciiPathAlias::needed(int javaMajorVersion, const QStringList &realPaths) {
    // 0 = 调用方没探到 Java 版本：维持原样。判错的另一边代价不对称 —— "Java 9+ 也换了别名"
    // 只是多一层没用的目录，而"Java 8 该换没换"就是启动失败，可"该换的判据本身依赖
    // javaMajorVersion"，所以这里只能按"不换"处理，让 JVM 报错照旧。
    if (javaMajorVersion <= 0 || javaMajorVersion > 8) {
        return false;
    }
    for (const QString &path : realPaths) {
        if (hasNonAscii(path)) {
            return true;
        }
    }
    return false;
}

bool AsciiPathAlias::isReady(const Paths &paths) {
    // jars/ 里至少要有一个 jar —— "这个目录在"不算数（空目录是建到一半留下的形状，
    // 那上面拼出来的 classpath 是"没有任何来自 versions/ 的 jar"）。
    const bool jarsReady =
        !QDir(paths.jarsDir).entryList(QStringList() << QStringLiteral("*.jar"), QDir::Files)
             .isEmpty();
    return isLiveDir(paths.gameDir) && jarsReady;
}

QString AsciiPathAlias::ensure(const Paths &paths, const QString &versionId,
                               const QString &realNativesPath) {
    const QString realGameDir = AppPaths::versionDir(versionId);
    QString lockProblem;
    if (!WriteDirectoryGuard::acquire(realGameDir, &lockProblem)) return lockProblem;
    const QString rootDir = QDir::toNativeSeparators(paths.root);

    // 别名根**自己**必须是 ASCII 的。它挂在数据目录底下，而数据目录的路径是用户自己选的
    // （`D:\我的游戏\.minecraft` 完全合法）—— 那一路改出来的是**看起来做完了、其实一点
    // 用没有**的别名（每条路径的前缀还是非 ASCII），症状和"根本没建"一模一样，所以宁可在
    // 这里拦住，也不要把一句 `找不到或无法加载主类` 丢给用户。
    if (hasNonAscii(rootDir)) {
        return translate(
                   "这个实例只能用 Java 8 启动，而它所在的路径里有非 ASCII 字符 —— "
                   "Java 8 打不开这种路径（这台机器开着 Windows 的「Beta: 使用 Unicode UTF-8 "
                   "提供全球语言支持」，Java 8 认不出它）。\n\n"
                   "启动器本想换成一套纯 ASCII 的别名绕过去，但游戏目录自己的路径里也有"
                   "非 ASCII 字符，别名救不了：\n%1\n\n"
                   "把游戏目录挪到一个纯英文、数字的路径下（例如 E:\\MC\\.minecraft）就能正常"
                   "启动。")
            .arg(rootDir);
    }
    if (!QDir().mkpath(rootDir)) {
        return translate("建不出别名目录：%1").arg(rootDir);
    }

    // 先清掉过期的那几套（上一次改名/删实例留下的那些**兄弟目录**），再做自己这一套。
    //
    // NOTE: 这里扫的是别名**集合**那一层（`cache/ascii`），不是自己这一套的里面。过期的那几套
    // 是兄弟：实例一改名，实例 id 就变了，哈希跟着变，新根目录是另一个名字，旧的留在
    // 原地指着已经不存在的目录。不扫的话它们会一直攒着（每一套里还有一个指向旧 jar 的
    // 硬链接，那个 jar 被删掉之后数据就只靠它保着了 —— 越攒越占地方）。
    // NOTE: 清理只认我们自己那三个名字、绝不递归（理由见 removeJunction 那段），而且只删
    // "`game` 已经指不到实例"的那种。别的东西（用户自己往这里放的文件）一律不碰，
    // 删完剩下的空壳如果非空就留着（QDir::rmdir 删不掉非空目录）。
    const QString collectionDir = QDir::toNativeSeparators(AppPaths::asciiAliasRootDir());
    const QStringList siblingRoots =
        QDir(collectionDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : siblingRoots) {
        const QString stale = collectionDir + QLatin1Char('/') + name;
        if (samePath(stale, rootDir) || !liveTargetOfAlias(stale).isEmpty()) {
            continue;  // 是自己这一套，或者还有实例在用（见 liveTargetOfAlias 那段）
        }
        removeJunction(stale + QStringLiteral("/game"));
        removeJunction(stale + QStringLiteral("/natives"));
        removeAliasJars(stale + QStringLiteral("/jars"));
        QDir().rmdir(stale);
    }

    QString problem;

    // ① 游戏目录：联接。判据是"通不通、而且指着的是不是那一份"而不是"这个目录在不在" ——
    // 实例被改名、或者同名实例被删了又建，联接可能还在但指错了地方。
    if (!linkUsable(paths.gameDir, realGameDir)) {
        removeJunction(paths.gameDir);
        if (!createJunction(paths.gameDir, realGameDir, &problem)) {
            return problem;
        }
    }

    // ② 那一批 jar：硬链接（省的是那十几 MB 和一次拷贝，见 .hpp）。
    //
    // 链的是**两个目录**顶层的 jar，按名字去重：
    // - `jarOwnerId()` 那个目录：继承了加载器的实例，客户端 jar 在原版那个目录里；
    // - 实例自己那个目录：有的实例把主类放在自己目录里第二个 jar 里（见 .hpp）。
    // 名字撞上的情况不重链（同名的两个 jar 是同一份的可能性远大于它们是两份不同的东西，
    // 而"链了哪一个"这种事**没有地方能看出来** —— 宁可留一条写在这里的说明）。
    if (!QDir().mkpath(paths.jarsDir)) {
        return translate("建不出别名目录：%1").arg(QDir::toNativeSeparators(paths.jarsDir));
    }
    QStringList linkedNames;  // 别名那一侧的名字（可能和真实文件名不一样，见 jarFileNameInAlias）
    for (const QString &ownerId : {InstanceFiles::jarOwnerId(versionId), versionId}) {
        const InstanceFiles::DirListing listing = InstanceFiles::scanDir(ownerId);
        for (const QString &jarName : listing.jarNames) {
            const QString aliasName = jarFileNameInAlias(jarName);
            if (linkedNames.contains(aliasName, Qt::CaseInsensitive)) {
                continue;
            }
            const QString realJar = AppPaths::versionDir(ownerId) + QLatin1Char('/') + jarName;
            if (!QFileInfo::exists(realJar)) {
                continue;
            }
            linkedNames << aliasName;
            const QString aliasJar = paths.jarFor(jarName);
            if (sameFile(aliasJar, realJar)) {
                continue;  // 已经是同一份了
            }
            QFile::remove(aliasJar);
            if (!createHardlink(aliasJar, realJar, &problem)) {
                return problem;
            }
        }
    }
    // 原来链过、现在源目录里已经没有的那些名字（实例里删了个 jar）也要跟着清掉 ——
    // 留着的话 classpath 上会多出一个指向旧数据的 jar（见 sameFile() 那段说的那种症状）。
    // NOTE: 这里比的是**别名那一侧**的名字（linkedNames 里存的也是它）；两个名字混着比的话
    // 非 ASCII 的那种会每次都判成"源没了"，于是每趟启动都白删白建一遍。
    for (const QString &existing :
         QDir(paths.jarsDir).entryList(QStringList() << QStringLiteral("*.jar"), QDir::Files)) {
        if (!linkedNames.contains(existing, Qt::CaseInsensitive)) {
            QFile::remove(paths.jarsDir + QLatin1Char('/') + existing);
        }
    }
    if (linkedNames.isEmpty()) {
        return translate("这个实例的目录里一个 jar 都没有，没法准备别名路径：%1")
            .arg(QDir::toNativeSeparators(paths.jarsDir));
    }

    // ③ natives：联接。不需要 natives 的版本（1.19+）把上一次留下的那份清掉就好。
    if (realNativesPath.isEmpty()) {
        removeJunction(paths.nativesDir);
    } else if (!linkUsable(paths.nativesDir, realNativesPath)) {
        removeJunction(paths.nativesDir);
        if (!createJunction(paths.nativesDir, realNativesPath, &problem)) {
            return problem;
        }
    }

    return QString();
}
