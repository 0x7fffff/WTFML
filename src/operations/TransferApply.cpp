/**
 * @file TransferApply.cpp
 * @brief TransferApply.hpp 的实现：按计划搬文件，只增不删、不跟链接。
 * @author 0x7fffff
 * @date 2026-10-18
 */

#include "operations/TransferApply.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include "manage/InstanceFiles.hpp"
#include "tools/FileHash.hpp"

namespace TransferApply {

namespace {

constexpr int kFailureListLimit = 5;

void noteFailure(Result *result, const QString &path) {
    ++result->failedFiles;
    if (result->failures.size() < kFailureListLimit) result->failures << path;
}

/// 一个相对路径得是"普通的相对路径"：不带分隔符换向、不带 `..`、不是绝对路径。
/// 表是这一层写死的常量，但这条路将来会接用户选的东西（例如自定义目录），所以闸在这儿。
bool safeRelativePath(const QString &relativePath) {
    if (relativePath.isEmpty()) return false;
    if (QDir::isAbsolutePath(relativePath)) return false;
    const QString unified = QString(relativePath).replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList segments = unified.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : segments) {
        if (segment == QStringLiteral("..") || segment == QStringLiteral(".")) return false;
    }
    return !segments.isEmpty();
}

/// 拷一个文件；目标已存在时**跳过**（不覆盖）。
void copyOneFile(const QString &sourceFile, const QString &targetFile, Result *result) {
    const QFileInfo info(sourceFile);
    if (info.isJunction() || info.isSymLink()) {
        noteFailure(result, sourceFile);  // 链接不跟
        return;
    }
    if (QFileInfo::exists(targetFile)) {
        ++result->skippedFiles;
        return;
    }
    if (!QDir().mkpath(QFileInfo(targetFile).absolutePath())) {
        noteFailure(result, targetFile);
        return;
    }
    if (!QFile::copy(sourceFile, targetFile)) {
        noteFailure(result, sourceFile);
        return;
    }
    ++result->copiedFiles;
    result->copiedBytes += info.size();
}

/// 把一棵目录**合并**进目标：只拷目标里没有的文件，目录先建出来。
void mergeTree(const QString &sourceDir, const QString &targetDir, Result *result) {
    if (!QDir().mkpath(targetDir)) {
        noteFailure(result, targetDir);
        return;
    }
    const QDir source(sourceDir);
    const QFileInfoList files =
        source.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
    for (const QFileInfo &file : files) {
        copyOneFile(file.absoluteFilePath(),
                    targetDir + QLatin1Char('/') + file.fileName(), result);
    }
    const QFileInfoList subs =
        source.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
    for (const QFileInfo &sub : subs) {
        if (sub.isJunction() || sub.isSymLink()) {
            noteFailure(result, sub.absoluteFilePath());
            continue;
        }
        mergeTree(sub.absoluteFilePath(), targetDir + QLatin1Char('/') + sub.fileName(), result);
    }
}

// ---------- 校验（§9.9 bullet 3 的最后一步）----------

constexpr int kVerifyListLimit = 5;

/// 对**一个文件**：源里有的，目标里在不在、大小一不一样。
/// 源里的链接不跟 —— `apply()` 本来也不拷它们，所以它们不算"该在目标里"。
void checkOneFile(const QString &sourceFile, const QString &targetFile, VerifySummary *summary) {
    const QFileInfo info(sourceFile);
    if (info.isJunction() || info.isSymLink()) return;
    ++summary->checkedFiles;
    const QFileInfo targetInfo(targetFile);
    if (!targetInfo.exists() || !targetInfo.isFile()) {
        ++summary->missingFiles;
        if (summary->missing.size() < kVerifyListLimit) summary->missing << targetFile;
        return;
    }
    if (targetInfo.size() != info.size()) {
        ++summary->sizeMismatchFiles;
        if (summary->mismatched.size() < kVerifyListLimit) summary->mismatched << targetFile;
    }
}

void checkTree(const QString &sourceDir, const QString &targetDir, VerifySummary *summary) {
    const QDir source(sourceDir);
    const QFileInfoList files =
        source.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
    for (const QFileInfo &file : files) {
        checkOneFile(file.absoluteFilePath(),
                     targetDir + QLatin1Char('/') + file.fileName(), summary);
    }
    const QFileInfoList subs =
        source.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
    for (const QFileInfo &sub : subs) {
        if (sub.isJunction() || sub.isSymLink()) continue;
        checkTree(sub.absoluteFilePath(), targetDir + QLatin1Char('/') + sub.fileName(), summary);
    }
}

}  // namespace

QString Result::text() const {
    if (!ok) return problem;
    QStringList parts;
    parts << QStringLiteral("复制了 %1 个文件（%2）")
                 .arg(copiedFiles)
                 .arg(FileHash::fileSizeText(copiedBytes));
    if (skippedFiles > 0) {
        parts << QStringLiteral("跳过 %1 个（目标里已经有同名的，没有覆盖）").arg(skippedFiles);
    }
    if (failedFiles > 0) {
        parts << QStringLiteral("失败 %1 个").arg(failedFiles);
    }
    QString text = parts.join(QStringLiteral("，")) + QStringLiteral("。");
    if (!failures.isEmpty()) {
        text += QStringLiteral("\n\n没搬成的：");
        for (const QString &failure : failures) {
            text += QStringLiteral("\n· %1").arg(QDir::toNativeSeparators(failure));
        }
        if (failedFiles > failures.size()) {
            text += QStringLiteral("\n· …还有 %1 个").arg(failedFiles - failures.size());
        }
    }
    return text;
}

Result apply(const QString &sourceDir, const QString &targetDir,
             const QList<TransferPlanner::Item> &items, const QStringList &keys,
             const ProgressCallback &progress) {
    Result result;

    const QString source = sourceDir.trimmed();
    const QString target = targetDir.trimmed();
    if (source.isEmpty() || target.isEmpty()) {
        result.problem = QStringLiteral("没有给全源实例和目标实例的目录。");
        return result;
    }
    if (!QDir(source).exists()) {
        result.problem = QStringLiteral("找不到源实例目录：%1")
                             .arg(QDir::toNativeSeparators(source));
        return result;
    }
    if (!QDir(target).exists()) {
        result.problem = QStringLiteral("找不到目标实例目录：%1")
                             .arg(QDir::toNativeSeparators(target));
        return result;
    }
    // ② 源和目标不能是同一份、也不能互相嵌套。判据和「复制实例」那道闸是同一个函数
    //   （它解 junction / 8.3 短名 —— 光比字面路径挡不住"拿一个指着源实例的 junction
    //   当目标"）。双向各问一次：往源里面搬 = 自己抄自己，往源外面搬进它的祖先也一样危险。
    if (InstanceFiles::pathIsSameOrInside(source, target)
        || InstanceFiles::pathIsSameOrInside(target, source)) {
        result.problem = QStringLiteral("源实例和目标实例是同一份（或互相嵌套），什么都没有搬。");
        return result;
    }

    for (const TransferPlanner::Item &item : items) {
        if (!keys.contains(item.key)) continue;
        if (!item.exists) continue;
        if (!safeRelativePath(item.relativePath)) {
            noteFailure(&result, item.relativePath);
            continue;
        }
        const QString sourcePath = source + QLatin1Char('/') + item.relativePath;
        const QString targetPath = target + QLatin1Char('/') + item.relativePath;
        const int failedBefore = result.failedFiles;
        if (item.isDirectory && QFileInfo(sourcePath).isDir()) {
            mergeTree(sourcePath, targetPath, &result);
        } else {
            copyOneFile(sourcePath, targetPath, &result);
        }
        // 逐项报告（§9.9 bullet 5）：一块搬完就报一次，**跳过不算失败**。
        if (progress) progress(item.key, result.failedFiles == failedBefore);
    }

    result.ok = true;
    return result;
}

QString VerifySummary::text() const {
    if (!ok) return problem;
    QString head = QStringLiteral("校验：看过 %1 个文件，缺 %2 个").arg(checkedFiles).arg(missingFiles);
    if (sizeMismatchFiles > 0) {
        head += QStringLiteral("；另有 %1 个目标里原有的同名文件大小不同（按规矩没有覆盖）")
                    .arg(sizeMismatchFiles);
    }
    head += QStringLiteral("。");
    if (!missing.isEmpty()) {
        head += QStringLiteral("\n\n没在目标里找到的：");
        for (const QString &path : missing) {
            head += QStringLiteral("\n· %1").arg(QDir::toNativeSeparators(path));
        }
        if (missingFiles > missing.size()) {
            head += QStringLiteral("\n· …还有 %1 个").arg(missingFiles - missing.size());
        }
    }
    if (!mismatched.isEmpty()) {
        head += QStringLiteral("\n\n大小不同的（原来是目标里就有的）：");
        for (const QString &path : mismatched) {
            head += QStringLiteral("\n· %1").arg(QDir::toNativeSeparators(path));
        }
        if (sizeMismatchFiles > mismatched.size()) {
            head += QStringLiteral("\n· …还有 %1 个").arg(sizeMismatchFiles - mismatched.size());
        }
    }
    return head;
}

VerifySummary verify(const QString &sourceDir, const QString &targetDir,
                     const QList<TransferPlanner::Item> &items, const QStringList &keys) {
    VerifySummary summary;

    const QString source = sourceDir.trimmed();
    const QString target = targetDir.trimmed();
    if (source.isEmpty() || target.isEmpty()) {
        summary.problem = QStringLiteral("没有给全源实例和目标实例的目录。");
        return summary;
    }
    if (!QDir(source).exists()) {
        summary.problem = QStringLiteral("找不到源实例目录：%1")
                              .arg(QDir::toNativeSeparators(source));
        return summary;
    }
    if (!QDir(target).exists()) {
        summary.problem = QStringLiteral("找不到目标实例目录：%1")
                              .arg(QDir::toNativeSeparators(target));
        return summary;
    }

    for (const TransferPlanner::Item &item : items) {
        if (!keys.contains(item.key)) continue;
        if (!item.exists) continue;
        if (!safeRelativePath(item.relativePath)) continue;   // apply() 里已经记过这一条失败
        const QString sourcePath = source + QLatin1Char('/') + item.relativePath;
        const QString targetPath = target + QLatin1Char('/') + item.relativePath;
        if (item.isDirectory && QFileInfo(sourcePath).isDir()) {
            checkTree(sourcePath, targetPath, &summary);
        } else if (QFileInfo(sourcePath).isFile()) {
            checkOneFile(sourcePath, targetPath, &summary);
        }
    }

    summary.ok = true;
    return summary;
}

}  // namespace TransferApply
