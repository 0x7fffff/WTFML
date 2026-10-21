/**
 * @file LocalFileReuse.cpp
 * @brief 「从别的游戏文件夹里借一个一模一样的文件」：路径映射、判据、硬链接 / 复制。
 * @note 来龙去脉与"只许看哪几个文件夹"那道闸写在 .hpp 的块注释里。这里只有实现，以及
 * 几处"为什么不能照直觉改"的 NOTE。
 */

#include "LocalFileReuse.hpp"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <memory>

#if defined(Q_OS_WIN)
// NOTE: 放在所有 Qt 头之后 —— windows.h 定义的东西太多（min/max 宏……），先于 Qt 包含
// 容易撞名。和 AsciiPathAlias.cpp / TokenStore.cpp 里那一套同一个写法。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

/// 复制时一块多大。1 MiB 是"吞吐够、取消也够快"的折中：取消判据每个块看一次，一块
/// 在机械盘上最多几十毫秒。
constexpr qint64 kCopyChunkBytes = 1 * 1024 * 1024;

bool samePath(const QString &left, const QString &right) {
    return QDir::cleanPath(left).compare(QDir::cleanPath(right), Qt::CaseInsensitive) == 0;
}

/// 这个路径里有没有 `.` / `..` 段。`relativePathWithin()` 的返回值和
/// `candidatePaths()` 的入参都要过这一道：拼接之后跑到目标目录外面去，等于借了一个
/// 和"只许看这几个文件夹"无关的地方。
bool hasDotSegment(const QString &relativePath) {
    const QStringList segments = relativePath.split(QLatin1Char('/'));
    for (const QString &segment : segments) {
        if (segment == QLatin1String(".") || segment == QLatin1String("..")) {
            return true;
        }
    }
    return false;
}

bool safeRelativePath(const QString &path) {
    return !path.isEmpty() && !QDir::isAbsolutePath(path)
           && !path.contains(QLatin1Char('\\')) && !path.contains(QLatin1Char(':'))
           && !path.startsWith(QLatin1Char('/')) && !hasDotSegment(path);
}

void setError(QString *errorOut, const QString &message) {
    if (errorOut != nullptr) {
        *errorOut = message;
    }
}

/// 复制那一条路：写同目录临时文件，写完（可选地核 SHA-1）再原子改名。
LocalFileReuse::Method copyFile(const QString &source, const QString &destination,
                                const QString &expectedSha1, bool verifyWhileCopying,
                                const std::atomic_bool *cancel, QString *errorOut) {
    if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
        setError(errorOut, QStringLiteral("cancelled"));
        return LocalFileReuse::Method::None;
    }

    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        setError(errorOut, QStringLiteral("cannot open source: %1").arg(in.errorString()));
        return LocalFileReuse::Method::None;
    }

    // 父目录在 DownloadManager 那边一般已经建好了（库 / 资源对象都是先 mkpath 再排队），
    // 但借用这条路可能来自任何调用方，自己再保证一次不亏。
    QDir().mkpath(QFileInfo(destination).absolutePath());

    QSaveFile out(destination);
    if (!out.open(QIODevice::WriteOnly)) {
        setError(errorOut, QStringLiteral("cannot open destination: %1").arg(out.errorString()));
        return LocalFileReuse::Method::None;
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    QByteArray buffer(kCopyChunkBytes, Qt::Uninitialized);
    while (true) {
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
            out.cancelWriting();
            setError(errorOut, QStringLiteral("cancelled"));
            return LocalFileReuse::Method::None;
        }
        const qint64 got = in.read(buffer.data(), buffer.size());
        if (got < 0) {
            out.cancelWriting();
            setError(errorOut, QStringLiteral("read failed: %1").arg(in.errorString()));
            return LocalFileReuse::Method::None;
        }
        if (got == 0) {
            break;
        }
        if (verifyWhileCopying) {
            hash.addData(QByteArrayView(buffer.constData(), got));
        }
        if (out.write(buffer.constData(), got) != got) {
            out.cancelWriting();
            setError(errorOut, QStringLiteral("write failed: %1").arg(out.errorString()));
            return LocalFileReuse::Method::None;
        }
    }

    if (verifyWhileCopying && !expectedSha1.isEmpty()
        && QString::fromLatin1(hash.result().toHex())
               .compare(expectedSha1, Qt::CaseInsensitive)
               != 0) {
        // 来源在"判完它"和"读它"之间被别的东西改了（另一份启动器正在重下、用户手工
        // 覆盖）。这一份不能留：否则下一批的 fileAlreadyValid() 会按大小 / 哈希再判一次，
        // 而它已经是一份谁都不认识的半成品了。
        out.cancelWriting();
        setError(errorOut, QStringLiteral("source changed while copying"));
        return LocalFileReuse::Method::None;
    }

    if (!out.commit()) {
        setError(errorOut, QStringLiteral("commit failed: %1").arg(out.errorString()));
        return LocalFileReuse::Method::None;
    }
    return LocalFileReuse::Method::Copy;
}

}  // namespace

namespace LocalFileReuse {

QString relativePathWithin(const QString &path, const QString &root) {
    if (path.isEmpty() || root.isEmpty()) {
        return QString();
    }
    const QString target = QDir::cleanPath(path);
    QString base = QDir::cleanPath(root);
    if (base.isEmpty() || target.size() <= base.size()) {
        return QString();
    }
    // 卷根（`D:/` → cleanPath 之后是 `D:`）后面紧跟的就是分隔符，不能再多吃一个。
    const bool baseEndsWithSeparator = base.endsWith(QLatin1Char('/'));
    if (!baseEndsWithSeparator) {
        if (target.at(base.size()) != QLatin1Char('/')) {
            return QString();
        }
        if (target.left(base.size()).compare(base, Qt::CaseInsensitive) != 0) {
            return QString();
        }
    } else if (target.left(base.size()).compare(base, Qt::CaseInsensitive) != 0) {
        return QString();
    }
    const int skip = baseEndsWithSeparator ? base.size() : base.size() + 1;
    const QString relative = target.mid(skip);
    if (relative.isEmpty() || hasDotSegment(relative)) {
        return QString();
    }
    return relative;
}

QStringList candidatePaths(const QString &relativePath, const QStringList &sourceRoots,
                           const QString &currentRoot) {
    QStringList candidates;
    if (!safeRelativePath(relativePath)) {
        return candidates;
    }

    const auto appendUnique = [&candidates](const QString &path) {
        const QString clean = QDir::cleanPath(path);
        for (const QString &existing : candidates) {
            if (samePath(existing, clean)) {
                return;
            }
        }
        candidates.append(clean);
    };

    for (const QString &root : sourceRoots) {
        if (root.isEmpty() || (!currentRoot.isEmpty() && samePath(root, currentRoot))) {
            continue;
        }
        appendUnique(root + QLatin1Char('/') + relativePath);
    }

    // 版本 jar 的"正统位置"：`versions/<实例名>/<owner>.jar`。实例改过名 / 是从别的启动器
    // 拷进来的那种，jar 名字还是原版的，而别的文件夹里那份往往老老实实躺在原版目录下。
    if (relativePath.startsWith(QLatin1String("versions/"))) {
        const QStringList parts = relativePath.split(QLatin1Char('/'));
        if (parts.size() == 3 && parts.at(2).endsWith(QLatin1String(".jar"), Qt::CaseInsensitive)) {
            const QString jarName = parts.at(2);
            const QString owner = jarName.left(jarName.size() - 4);
            if (!owner.isEmpty() && owner != parts.at(1)) {
                for (const QString &root : sourceRoots) {
                    if (root.isEmpty()
                        || (!currentRoot.isEmpty() && samePath(root, currentRoot))) {
                        continue;
                    }
                    appendUnique(root + QStringLiteral("/versions/") + owner + QLatin1Char('/')
                                 + jarName);
                }
            }
        }
    }

    return candidates;
}

bool sourceMatches(const QString &path, const QString &expectedSha1, qint64 expectedSize,
                   Check check, const std::atomic_bool *cancel) {
    // 没有 SHA-1 的文件一律不借：那种文件（例如加载器安装器）跨文件夹"同名"不代表
    // "同内容"，而它又没有别的判据能说明这一点。
    if (expectedSha1.isEmpty() || (cancel && cancel->load(std::memory_order_relaxed))) {
        return false;
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.size() == 0) {
        return false;
    }
    if (check == Check::Exists) {
        // 资源对象：文件名就是 SHA-1，判据和盘上那一份完全一样（见 fileAlreadyValid()）。
        return true;
    }
    if (expectedSize > 0 && info.size() != expectedSize) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha1);
    QByteArray buffer(kCopyChunkBytes, Qt::Uninitialized);
    while (!file.atEnd()) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            return false;
        }
        const qint64 got = file.read(buffer.data(), buffer.size());
        if (got <= 0) {
            return false;
        }
        hash.addData(QByteArrayView(buffer.constData(), got));
    }
    return (!cancel || !cancel->load(std::memory_order_relaxed))
           && QString::fromLatin1(hash.result().toHex()).compare(expectedSha1, Qt::CaseInsensitive) == 0;
}

Method materialize(const QString &source, const QString &destination,
                   const QString &expectedSha1, bool verifyWhileCopying,
                   const std::atomic_bool *cancel, bool allowHardLink, QString *errorOut) {
    if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
        setError(errorOut, QStringLiteral("cancelled"));
        return Method::None;
    }
    QDir().mkpath(QFileInfo(destination).absolutePath());

#if defined(Q_OS_WIN)
    if (allowHardLink) {
        const std::wstring linkWide =
            QDir::toNativeSeparators(QFileInfo(destination).absoluteFilePath()).toStdWString();
        const std::wstring targetWide =
            QDir::toNativeSeparators(QFileInfo(source).absoluteFilePath()).toStdWString();
        if (CreateHardLinkW(linkWide.c_str(), targetWide.c_str(), nullptr)) {
            return Method::HardLink;
        }
        // 失败有两种常见形状，都不用在这里报错：
        // · ERROR_NOT_SAME_DEVICE —— 跨卷，复制那条路接住；
        // · ERROR_ALREADY_EXISTS —— 目标已经在了（另一个作业刚写完，或者上一个是
        // 0 字节的残骸），QSaveFile 会原子改名覆盖它。
        // 真正两种都不行时，下面那条路会把原因写进 errorOut。
    }
#endif

    return copyFile(source, destination, expectedSha1, verifyWhileCopying, cancel, errorOut);
}

Method reuseFrom(const QString &destinationPath, const QString &relativePath,
                 const QStringList &sourceRoots, const QString &currentRoot,
                 const QString &expectedSha1, qint64 expectedSize, Check check,
                 const std::atomic_bool *cancel, bool allowHardLink, QString *errorOut) {
    const QStringList candidates = candidatePaths(relativePath, sourceRoots, currentRoot);
    if (candidates.isEmpty()) {
        setError(errorOut, QStringLiteral("no source folder"));
        return Method::None;
    }

    QString lastError;
    bool matched = false;
    for (const QString &candidate : candidates) {
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
            setError(errorOut, QStringLiteral("cancelled"));
            return Method::None;
        }
        if (!sourceMatches(candidate, expectedSha1, expectedSize, check, cancel)) {
            continue;
        }
        matched = true;
        const Method method =
            materialize(candidate, destinationPath, expectedSha1, check == Check::SizeAndSha1,
                        cancel, allowHardLink, &lastError);
        if (method != Method::None) {
            return method;
        }
    }

    setError(errorOut, matched ? lastError
                               : QStringLiteral("no matching source file"));
    return Method::None;
}

}  // namespace LocalFileReuse
