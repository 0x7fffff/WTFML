/**
 * @file FolderManager.cpp
 * @brief 实现「游戏文件夹」列表的读取、添加、移除、改名、删除与切换。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 见 FolderManager.hpp 的说明。
 */

#include "FolderManager.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "../settings/LauncherSettings.hpp"
#include "../util/AppPaths.hpp"
#include "util/DirectorySessionLock.hpp"

namespace {

/// 这个目录项是不是一个"指到别处去"的链接（Windows 的 junction / 符号链接，Unix 的符号
/// 链接）。删目录树的时候必须认它，否则会把链接那一头的东西一起删掉。
///
/// NOTE: 判据**不能**只写 `isSymLink()`，也不能用 `canonicalFilePath() !=
///       absoluteFilePath()` —— 实测（2026-09-25，两个探针见 tmp/probe_symlink/）：
///         · `mklink /J` 造出来的 junction，`isSymLink()` 是 **false**（Qt 把它报成
///           普通目录），`canonicalFilePath()` 也**原样返回字面路径**（根本不解链接）；
///         · 只有 `isJunction()` 认得出它。
///       junction 恰恰是普通用户（不需要管理员权限）挂目录时最常用的那一种，所以只判
///       isSymLink() 等于对这一类完全没设防。
bool isLinkToElsewhere(const QFileInfo &info) {
    return info.isJunction() || info.isSymLink();
}

/// 删一个**文件**（文件符号链接也走这里）。
///
/// ★ 2026-10-20：第一次失败时**清掉只读位再试一次**。Qt 6.10 在 Windows 上的
/// `QFile::remove()` 就是一句 `DeleteFile()`（qfilesystemengine_win.cpp），**不会**替你
/// 摘 `FILE_ATTRIBUTE_READONLY` —— 而只读文件恰恰删不掉（ERROR_ACCESS_DENIED）。用户从只读
/// 介质拷来的整合包、被别的启动器/网盘标成只读的 jar，都会卡在这一步，而调用方那句
/// 提示写的是"多半是它正被游戏占着"：方向完全是错的，关掉游戏再删一百次也没用。
///
/// 只读位**只有在第一次真的失败之后才动**：正常文件一次成功，权限一个字节都不碰。
bool removeFileAllowingReadOnly(const QString &path) {
    if (QFile::remove(path)) {
        return true;
    }
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        return true;  // 已经没了（并发删掉的那一种）= 这一步的目的达成
    }
    const QFileDevice::Permissions perms = info.permissions();
    QFile::setPermissions(path, perms | QFileDevice::WriteOwner | QFileDevice::WriteUser);
    return QFile::remove(path);
}

}  // namespace

/// 递归删掉 @p path 这一棵，但**一个链接都不追进去**：链接只把链接本身摘掉，那一头的东西
/// 一个字节都不动。
///
/// NOTE: 为什么不能用 `QDir::removeRecursively()`。实测（2026-09-25 探针）：
///         · 对一个**本身就是 junction** 的目录调用它 → junction 指向的那个真目录里的
///           文件**没了**（目标是活的，链接没了）；
///         · 目录**里面**含一个 junction 时 → 同样跟着进去把目标删了。
///       于是"删掉这一份游戏文件夹"会牵连到游戏文件夹**外面**的东西。把 `saves` 挂到
///       网盘同步目录、把 `mods` 挂到另一块盘，都是很常见的做法 —— 而用户点的是"删掉这
///       一份"，不是"删掉那边那块盘上的东西"（别的程序可能还在用同一份数据）。
///
/// NOTE: 链接本身用 `QDir().rmdir()` 摘。实测它对 junction 有效：只摘掉那个重解析点，
///       目标原封不动。`QFile::remove()` 对目录不行（对文件符号链接才是对的那一个）。
///
/// @return 整棵都删干净了没有。删到一半失败时**返回 false 但仍然尽力删完**（剩下的留着，
///         调用方照实报"没能删干净"，比留一半还不说话强）。
///
/// NOTE: 这个函数**故意留在匿名命名空间外面**（它上面那个 `}  // namespace` 就是为它关的）：
///       删实例那条路（VersionManager::deleteInstance）要用它，而 FolderManager.hpp 里
///       已经把它声明成了一个自由函数。放在匿名命名空间里的话，头文件那条声明会和它
///       变成**两个不同的重载**，调用处直接报 ambiguous。isLinkToElsewhere() 可以留在里面
///       —— 只有它用，而且它在上面、看得见。
bool removeTreeWithoutFollowingLinks(const QString &path) {
    const QFileInfo info(path);
    if (isLinkToElsewhere(info)) {
        return info.isDir() ? QDir().rmdir(path) : removeFileAllowingReadOnly(path);
    }
    if (!info.exists()) {
        return true;  // 已经不在了 = 这一步想要的结果已经达成
    }
    if (!info.isDir()) {
        return removeFileAllowingReadOnly(path);
    }
    const QDir dir(path);
    const QStringList entries =
        dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    bool ok = true;
    for (const QString &entry : entries) {
        // 注意用 && 的写法（不用短路）：一条失败也要接着删剩下的 —— 早退会把剩下的留下，
        // 而"剩一小半"比"整棵都在"更难收拾。所以先把结果收集起来，最后一起回答。
        if (!removeTreeWithoutFollowingLinks(dir.absoluteFilePath(entry))) {
            ok = false;
        }
    }
    if (!ok) {
        // 里面有东西没删掉，这一层（多半）也就空不了，别再多试一次 rmdir 掩盖原因。
        return false;
    }
    return QDir().rmdir(path);
}

/// 把 @p from 这一棵原样拷到 @p to（@p to 必须还不存在），一个链接都不追进去。
///
/// NOTE: 目录必须**先建再进**：`QFile::copy()` 不会替你造父目录，mkdir 和拷文件分两步
///       是必须的（先拷文件再建目录这条最常见的写法在这里会失败）。
///
/// NOTE: 顺序上**先把目标目录建出来**再往里灌。中途失败时盘上会留下一个"拷了一半"的
///       目录 —— 这是故意的：调用方（复制实例、导入世界）拿到的 from/to 都是它自己算
///       出来的，它比这里更清楚该不该收拾，而"留一半"比"悄悄删掉用户别的东西"安全。
bool copyTreeWithoutFollowingLinks(const QString &from, const QString &to,
                                   const std::function<bool()> &cancelled) {
    if (cancelled && cancelled()) return false;
    const QFileInfo source(from);
    if (isLinkToElsewhere(source)) {
        // 链接照拷的话，会在新位置复制出**那一头**的一整棵 —— 用户挂到别的盘上的东西
        // 会被无声无息地抄一份进来（可能几百 GB）。所以这里直接判失败，不是"跳过"。
        qWarning() << "copyTreeWithoutFollowingLinks: refusing to follow a link:" << from;
        return false;
    }
    if (source.isDir()) {
        if (QFileInfo::exists(to)) {
            qWarning() << "copyTreeWithoutFollowingLinks: target already exists:" << to;
            return false;
        }
        if (!QDir().mkpath(to)) {
            qWarning() << "copyTreeWithoutFollowingLinks: cannot create directory" << to;
            return false;
        }
        const QFileInfoList entries = QDir(from).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            if (!copyTreeWithoutFollowingLinks(entry.absoluteFilePath(),
                                               to + QLatin1Char('/') + entry.fileName(), cancelled)) {
                return false;
            }
        }
        return true;
    }
    if (!source.isFile()) {
        // 既不是目录也不是文件：设备文件、坏掉的链接、或者这一瞬间被别的东西拿走了。
        // 报失败而不是跳过 —— 拷出来的一份"少了点东西"是最难发现的那种坏。
        qWarning() << "copyTreeWithoutFollowingLinks: cannot copy a non-file entry:" << from;
        return false;
    }
    if (QFileInfo::exists(to)) {
        // QFile::copy() 遇到已存在的目标**直接返回 false**（Qt 不肯覆盖），所以这一句
        // 其实只是把"为什么会失败"说清楚一点，真正挡住覆盖的是 copy() 自己。
        qWarning() << "copyTreeWithoutFollowingLinks: target already exists:" << to;
        return false;
    }
    if (cancelled) {
        QFile input(from), output(to);
        if (!input.open(QIODevice::ReadOnly) ||
            !output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
        while (!input.atEnd()) {
            if (cancelled()) return false;
            const auto bytes = input.read(1024 * 1024);
            if (bytes.isEmpty() || output.write(bytes) != bytes.size()) return false;
        }
        if (input.error() != QFile::NoError || !output.flush() || cancelled()) return false;
        output.setPermissions(input.permissions());
        return true;
    }
    if (!QFile::copy(from, to)) {
        qWarning() << "copyTreeWithoutFollowingLinks: cannot copy" << from << "to" << to;
        return false;
    }
    return true;
}

FolderManager::FolderManager(LauncherSettings *settings, QObject *parent)
    : QObject(parent), m_settings(settings) {
    // 盘上那串路径是上一次运行写下的：用户可能已经把目录删了、挪了、或者换了盘符。
    // 一律重新归一化一遍，免得界面里同一个目录以两种写法各占一行。
    const QStringList stored = m_settings != nullptr ? m_settings->folders() : QStringList();
    for (const QString &entry : stored) {
        const QString path = normalized(entry);
        // 等于自带那份的要丢掉：启动器被拷到 D:\MC\ 之后，原先"用户添加的
        // D:\MC\.minecraft"就和自带那份撞上了，留着就是列表里两条一模一样的路径。
        if (path.isEmpty() || path == defaultFolder() || m_userFolders.contains(path)) {
            continue;
        }
        m_userFolders.append(path);
    }

    // 名字表：盘上那份是 `路径 + 制表符 + 名字`（见 LauncherSettings::folderNames()）。
    // 路径照旧重新归一化一遍，归一化之后认不出来的（那一项已经被挪走、或者盘上的
    // 路径写法变了）就丢掉 —— 名字是挂在具体某个目录上的，认不出挂给谁了留着没用。
    const QStringList storedNames =
        m_settings != nullptr ? m_settings->folderNames() : QStringList();
    for (const QString &entry : storedNames) {
        // ★ 按**最后一个**制表符切，不是第一个：名字那一半由 validateDisplayName()
        //   挡着制表符（进不去），而路径那一半在 NTFS 上是**可以**带制表符的。
        //   按第一个切的话，`D:\MC\foo<TAB>bar` 这种目录配的名字会被读成
        //   路径 `D:\MC\foo` + 名字 `bar<TAB>…` —— 名字没了、还可能认给别的目录。
        const int tab = entry.lastIndexOf(QLatin1Char('\t'));
        if (tab <= 0) {
            continue;  // 连分隔符都没有（手改的）：这一项读不出是给谁起的名字
        }
        const QString path = normalized(entry.left(tab));
        const QString name = entry.mid(tab + 1);
        if (path.isEmpty() || name.isEmpty()) {
            continue;
        }
        m_names.insert(path, name);
    }

    m_current = normalized(m_settings != nullptr ? m_settings->currentFolder() : QString());
    if (m_current.isEmpty() || !QFileInfo(m_current).isDir()) {
        // 当前那一份不在盘上（U 盘今天没插、目录被删或被挪走）：这次先用自带那份。
        // **刻意不把盘上那个值改掉** —— 它可能只是今天没挂上，插回去还该接着用它。
        // 代价是这次会话里它不再生效，右边那句"当前文件夹"会照实说是哪一份。
        if (!m_current.isEmpty()) {
            qWarning() << "FolderManager: 记着的游戏文件夹不在盘上，这次先用自带那份："
                       << m_current;
        }
        m_current = defaultFolder();
    }
}

QString FolderManager::normalized(const QString &path) {
    if (path.isEmpty()) {
        return QString();
    }
    const QFileInfo info(path);
    if (info.exists()) {
        // ★ 这一格**本身就是个链接**时不许解（2026-09-26 修）。canonicalFilePath() 会把
        //   符号链接解析成它指向的那个目录 —— 于是用户添加进来的 `D:\MC\link` 在列表里
        //   变成 `E:\real`，而"删除这一份游戏文件夹"随后调的就是
        //   `removeTreeWithoutFollowingLinks(E:\real)`：链接本身还在，**另一块盘上的东西
        //   没了**。上面 isLinkToElsewhere() 那个 guard 判的正是 `isJunction() ||
        //   isSymLink()`，可它**在更早的这一步就被解掉了**，所以它一辈子也看不到符号链接。
        //
        //   NOTE: junction 不受影响 —— 实测（探针 linkprobe，2026-09-26，和 2026-09-25
        //         那次结论一致）`canonicalFilePath()` 对 junction **原样返回字面路径**，
        //         根本不解释链接，所以它在下面那条路上活得好好的。这个不对称正是"自测
        //         全绿"的原因：能造的那一类（junction）防住了，防不住的那一类没进过测试。
        //
        //   只把**父目录**归一（盘符大小写、`..` 这些照旧收拾掉），最后那一段原样留着 ——
        //   列表里那条还是用户亲手加进来的那一条，删的时候也只是把链接摘掉。
        if (isLinkToElsewhere(info)) {
            const QFileInfo parent(info.absolutePath());
            const QString parentPath = parent.canonicalFilePath();
            const QString base = parentPath.isEmpty() ? info.absolutePath() : parentPath;
            return QDir::cleanPath(base + QLatin1Char('/') + info.fileName());
        }
        // 目录真在的时候用 canonicalFilePath：它顺带统一盘符大小写、解掉符号链接和
        // `..`，否则 `D:/mc/.minecraft` 和 `d:\mc\.minecraft` 会被当成两个文件夹。
        const QString canonical = info.canonicalFilePath();
        if (!canonical.isEmpty()) {
            return QDir::cleanPath(canonical);
        }
    }
    // 目录还不存在（新装的启动器、还没下过东西）：只能按字面归一到绝对路径。
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString FolderManager::defaultFolder() { return normalized(AppPaths::defaultMinecraftDir()); }

bool FolderManager::isDefaultFolder(const QString &path) {
    const QString target = normalized(path);
    return !target.isEmpty() && target == defaultFolder();
}

QString FolderManager::officialLauncherFolder() {
    // `%APPDATA%` 不存在 = 不是一台有用户配置的 Windows（服务账户 / 精简环境）。这时候
    // 返回空串，上层按"没有这一份"处理 —— 不要退到 QStandardPaths，它给的可能是另一个
    // 位置（甚至我们的沙盒），那就不是在说"官方启动器"了。
    const QString appData = qEnvironmentVariable("APPDATA");
    if (appData.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(appData + QStringLiteral("/.minecraft"));
}

QStringList FolderManager::reuseSourceFolders() const {
    QStringList roots;
    const auto appendUnique = [&roots](const QString &path) {
        if (path.isEmpty() || !QFileInfo(path).isDir()) {
            return;
        }
        const QString clean = QDir::cleanPath(path);
        for (const QString &existing : roots) {
            if (QDir::cleanPath(existing).compare(clean, Qt::CaseInsensitive) == 0) {
                return;
            }
        }
        roots.append(clean);
    };
    // 顺序有讲究：自带那份 + 用户添加的在前（用户自己挑的目录更可能是他常玩的那一份），
    // 官方那一份垫后。
    for (const QString &folder : folders()) {
        appendUnique(folder);
    }
    appendUnique(officialLauncherFolder());
    return roots;
}

QStringList FolderManager::folders() const {
    QStringList all;
    all.append(defaultFolder());
    all.append(m_userFolders);
    return all;
}

QString FolderManager::currentFolder() const { return m_current; }

bool FolderManager::addFolder(const QString &path) {
    const QString target = normalized(path);
    if (target.isEmpty() || !QFileInfo(target).isDir()) {
        return false;
    }
    // 自带那份、以及已经在列表里的：不重复追加，但它确实"现在在列表里"，所以算成功。
    if (target == defaultFolder() || m_userFolders.contains(target)) {
        return true;
    }
    m_userFolders.append(target);
    if (m_settings != nullptr) {
        m_settings->setFolders(m_userFolders);
    }
    emit foldersChanged();
    return true;
}

bool FolderManager::removeFolder(const QString &path) {
    const QString target = normalized(path);
    // 自带那份移不掉：它是 AppPaths 的默认值，也是"别的目录都不在盘上"时的唯一退路。
    if (target.isEmpty() || target == defaultFolder()) {
        return false;
    }
    if (!m_userFolders.contains(target)) {
        return false;
    }
    forgetFolder(target);
    return true;
}

void FolderManager::forgetFolder(const QString &target) {
    m_userFolders.removeOne(target);
    // 名字跟着一起清：移除之后界面上已经没有这一条了，留着它只是让盘上多一项
    // 认不出挂给谁的记录（重新加回来是"新加了一份文件夹"，该用默认显示名）。
    m_names.remove(target);
    if (m_settings != nullptr) {
        m_settings->setFolders(m_userFolders);
    }
    storeNames();
    emit foldersChanged();

    // 移除掉的正好是当前在用的那一份 → 退回自带那份。不退回的话启动器会指着一个
    // 已经不在列表里的目录（界面上没有它，用户想再选回来都没地方点）。
    if (target == m_current) {
        m_current = defaultFolder();
        if (m_settings != nullptr) {
            m_settings->setCurrentFolder(QString());
        }
        emit currentFolderChanged(m_current);
    }
}

QString FolderManager::validateDisplayName(const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return QString();  // 空名字 = 清掉名字，合法
    }
    // 换行和制表符进不去：前者进不了界面上一行那么高的标签，后者是**落盘的字段分隔符**
    // （见 LauncherSettings::folderNames()），放进去等于把一个名字劈成两半。
    for (const QChar ch : trimmed) {
        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QLatin1Char('\t')) {
            return tr("名字里不能有换行。");
        }
    }
    if (trimmed.length() > maxDisplayNameLength()) {
        return tr("名字最多 %1 个字。").arg(maxDisplayNameLength());
    }
    return QString();
}

QString FolderManager::displayName(const QString &path) const {
    const QString target = normalized(path);
    if (target.isEmpty()) {
        return QString();
    }
    return m_names.value(target);
}

bool FolderManager::setDisplayName(const QString &path, const QString &name) {
    const QString target = normalized(path);
    if (target.isEmpty() || !folders().contains(target)) {
        return false;
    }
    const QString wanted = name.trimmed();
    const bool changed = wanted.isEmpty() ? m_names.remove(target) > 0
                                          : m_names.value(target) != wanted;
    if (!wanted.isEmpty()) {
        m_names.insert(target, wanted);
    }
    if (changed) {
        storeNames();
        emit foldersChanged();
    }
    return true;
}

bool FolderManager::deleteFolder(const QString &path, QString *error) {
    const QString target = normalized(path);
    // 自带那份删不掉，理由和"移不掉"是同一条（AppPaths 的默认值 + 唯一退路），
    // 而且它多半正是启动器此刻在用的那一份 —— 日志、设置、缓存都写在里面。
    if (target.isEmpty() || target == defaultFolder()) {
        if (error != nullptr) {
            *error = tr("启动器自带的游戏文件夹删不掉。");
        }
        return false;
    }
    if (!m_userFolders.contains(target)) {
        if (error != nullptr) {
            *error = tr("这一份已经不在列表里了。");
        }
        return false;
    }
    QString lockProblem;
    if (!DirectorySessionLock::acquire(target, &lockProblem)) {
        if (error) *error = lockProblem;
        return false;
    }
    // 盘上那个目录不存在（U 盘没插、已经被手工删掉）不算失败：列表里那一条照样要清掉。
    //
    // NOTE: 走的是 removeTreeWithoutFollowingLinks() 而**不是** QDir::removeRecursively()
    //       —— 后者会追进 junction / 符号链接，把链接那一头的东西也删掉（实测见上面那个
    //       函数的说明）。这一份自己就是个链接时，这里只摘掉链接、那边一个字节不动。
    if (QFileInfo(target).exists() && !removeTreeWithoutFollowingLinks(target)) {
        if (error != nullptr) {
            // 失败原因照实说：删到一半失败时目录是**半截**的（前面那些文件已经没了），
            // 而用户需要知道现在盘上到底是什么状态。
            *error = tr("「%1」没能删干净，可能是里面的文件正被别的程序占用"
                                 "（比如游戏还开着）。")
                         .arg(QDir::toNativeSeparators(target));
        }
        return false;
    }
    forgetFolder(target);
    return true;
}

void FolderManager::storeNames() {
    if (m_settings == nullptr) {
        return;
    }
    QStringList entries;
    // 顺序按 folders() 走（自带那份没有名字、不进这一项）：盘上那份表读起来就是
    // 界面上从上到下那个顺序，排查的时候对得上。
    for (const QString &path : folders()) {
        const QString name = m_names.value(path);
        if (!name.isEmpty()) {
            entries.append(path + QLatin1Char('\t') + name);
        }
    }
    m_settings->setFolderNames(entries);
}

bool FolderManager::setCurrentFolder(const QString &path) {
    const QString target = normalized(path);
    if (target.isEmpty() || !QFileInfo(target).isDir() || !folders().contains(target)) {
        return false;
    }
    QString lockProblem;
    if (!DirectorySessionLock::acquire(target, &lockProblem)) {
        emit directoryAccessDenied(lockProblem);
        return false;
    }
    if (target == m_current) {
        // 值没变，但盘上记的可能是那个"不在盘上、这次先不用"的旧目录（见构造函数）。
        // 顺手把盘上写回当前这一份，免得每次开窗都警告一遍。不发信号：界面没必要重灌。
        if (m_settings != nullptr) {
            m_settings->setCurrentFolder(m_current == defaultFolder() ? QString() : m_current);
        }
        return true;
    }
    m_current = target;
    if (m_settings != nullptr) {
        m_settings->setCurrentFolder(target);
    }
    emit currentFolderChanged(m_current);
    return true;
}
