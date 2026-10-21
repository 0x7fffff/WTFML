/**
 * @file ServerResourcePack.cpp
 * @brief ServerResourcePack.hpp 的实现（读条目表、算 SHA-1；不装、不下载、不填地址）。
 */

#include "server/ServerResourcePack.hpp"

#include <QFileInfo>

#include "tools/FileHash.hpp"
#include "util/ZipUtil.hpp"

namespace ServerResourcePack {

namespace {

const QString kPackMeta = QStringLiteral("pack.mcmeta");
const QChar kSlash = QLatin1Char('/');

/// 条目名归一到"能拿来比"的形状：`\` 当 `/`（"一键压缩"那类工具会写反斜杠）。
/// **大小写不动**：`pack.mcmeta` 游戏是按原样找的。
QString normalized(const QString &entryName) {
    QString name = entryName;
    name.replace(QLatin1Char('\\'), kSlash);
    return name;
}

bool isDirectoryEntry(const QString &entry) { return entry.endsWith(kSlash); }

/// 最外层有没有这一段（`assets` 或者 `assets/…` 这两种写法都算）。
bool hasRootFolder(const QStringList &entries, const QString &folder) {
    const QString prefix = folder + kSlash;
    for (const QString &entry : entries) {
        if (entry == folder || entry == prefix || entry.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

}  // namespace

EntriesCheck checkEntries(const QStringList &entries) {
    EntriesCheck result;
    QStringList names;
    names.reserve(entries.size());
    for (const QString &entry : entries) {
        names.append(normalized(entry));
    }
    if (names.isEmpty()) {
        result.problem = QStringLiteral("这个压缩包是空的 —— 里面一个条目都没有。");
        return result;
    }

    bool nestedMeta = false;
    for (const QString &name : names) {
        if (name == kPackMeta) {
            result.hasPackMeta = true;
        } else if (name.endsWith(kSlash + kPackMeta)) {
            nestedMeta = true;
        }
    }
    result.hasAssets = hasRootFolder(names, QStringLiteral("assets"));
    const bool hasData = hasRootFolder(names, QStringLiteral("data"));
    result.looksLikeDataPack = !result.hasAssets && hasData;

    if (!result.hasPackMeta && nestedMeta) {
        // 最常见的一种错：把整个文件夹一起压了。游戏只认最外层那一份。
        result.problem =
            QStringLiteral("pack.mcmeta 被套在一个文件夹里 —— 游戏只认压缩包**最外层**的"
                           "那一份。把包里的东西重新压到最外层再试。");
        return result;
    }
    if (!result.hasPackMeta) {
        if (result.looksLikeDataPack) {
            result.problem = QStringLiteral("这里面是 data/ 而不是 assets/ —— 它像是**数据包**，"
                                            "不是资源包。服务端资源包要选改了客户端贴图/声音"
                                            "的那一份。");
        } else {
            result.problem = QStringLiteral("里面没有 pack.mcmeta —— 这不像是资源包。"
                                            "资源包解开来最外层一定有一份 pack.mcmeta。");
        }
        return result;
    }
    if (!result.hasAssets) {
        // 有 pack.mcmeta 却没有 assets/：仍然是个合法的资源包（只改语言文件之类也算），
        // 不拦 —— 但这一句留着，界面上那行提示要用。
        // 不设 problem：这不是错误。
    }
    result.ok = true;
    return result;
}

Inspect inspect(const QString &zipPath) {
    Inspect result;
    const QFileInfo info(zipPath);
    if (!info.exists() || !info.isFile()) {
        result.problem = QStringLiteral("找不到这个文件 —— 它可能已经被移走或者改名了。");
        return result;
    }

    // ① 条目表（一个 PowerShell 进程，只读名字）。
    QStringList entries;
    QString error;
    const ZipUtil::EntryStatus status =
        ZipUtil::listEntries(zipPath, &entries, &error, QStringLiteral("读这个资源包"));
    if (status == ZipUtil::EntryStatus::NotAnArchive) {
        result.problem = QStringLiteral("这不是一个 zip 压缩包（或者文件坏了）—— "
                                        "资源包要选 .zip 那一份。");
        return result;
    }
    if (status != ZipUtil::EntryStatus::Ok) {
        result.problem = error.isEmpty() ? QStringLiteral("读不出这个压缩包的条目表。") : error;
        return result;
    }

    // ② 条目表像不像资源包。
    result.entries = checkEntries(entries);
    if (!result.entries.ok) {
        result.problem = result.entries.problem;
        return result;
    }
    for (const QString &entry : entries) {
        if (!isDirectoryEntry(normalized(entry))) {
            ++result.fileCount;
        }
    }

    // ③ 最后才算 SHA-1（要把整份读完）。
    const FileHash::Result hash =
        FileHash::digestOfFile(zipPath, FileHash::Algorithm::Sha1);
    if (!hash.ok) {
        result.problem = QStringLiteral("算不出这个文件的 SHA-1：%1").arg(hash.error);
        return result;
    }
    result.ok = true;
    result.sha1 = hash.hex;
    result.bytes = hash.bytes;
    return result;
}

QString fileCountText(const Inspect &inspect) {
    if (inspect.fileCount <= 0) {
        return QStringLiteral("包里没有文件");
    }
    return QStringLiteral("%1 个文件").arg(inspect.fileCount);
}

}  // namespace ServerResourcePack
