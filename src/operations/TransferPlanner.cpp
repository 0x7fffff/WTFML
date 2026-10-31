/**
 * @file TransferPlanner.cpp
 * @brief TransferPlanner.hpp 的实现：只读盘、只算，给迁移 / 升级副本一份范围表。
 */

#include "operations/TransferPlanner.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include "TextMarks.hpp"
#include "tools/FileHash.hpp"

namespace TransferPlanner {

namespace {

/// 一块内容在盘上占多少（只数文件、不读内容）。
struct TreeSize {
    int files = 0;
    qint64 bytes = 0;
};

TreeSize scanTree(const QString &root) {
    TreeSize total;
    QStringList stack;
    stack.append(root);
    while (!stack.isEmpty()) {
        const QDir dir(stack.takeLast());
        const QFileInfoList files =
            dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
        for (const QFileInfo &file : files) {
            ++total.files;
            total.bytes += file.size();
        }
        const QFileInfoList subs =
            dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
        for (const QFileInfo &sub : subs) {
            if (sub.isJunction() || sub.isSymLink()) continue;
            stack.append(sub.absoluteFilePath());
        }
    }
    return total;
}

TreeSize measure(const QString &absolutePath, bool isDirectory) {
    const QFileInfo info(absolutePath);
    if (!info.exists()) return TreeSize();
    if (isDirectory && info.isDir()) return scanTree(absolutePath);
    // 不是目录的（`options.txt` / `servers.dat`）按单文件算；万一它真是个目录，也照样数。
    if (info.isDir()) return scanTree(absolutePath);
    TreeSize one;
    one.files = 1;
    one.bytes = info.size();
    return one;
}

QString joinSize(int files, qint64 bytes) {
    return QStringLiteral("%1 个文件 / %2").arg(files).arg(FileHash::fileSizeText(bytes));
}

}  // namespace

QString compatibilityName(Compatibility compatibility) {
    switch (compatibility) {
    case Compatibility::Same: return QStringLiteral("一致");
    case Compatibility::GameVersionDiffers: return QStringLiteral("游戏版本不同");
    case Compatibility::LoaderDiffers: return QStringLiteral("加载器不同");
    case Compatibility::BothDiffer: return QStringLiteral("版本与加载器都不同");
    case Compatibility::Unknown: return QStringLiteral("认不出来");
    }
    return QString();
}

QString compatibilityText(Compatibility compatibility, const QString &sourceGameVersion,
                          const QString &sourceLoaderName, const QString &targetGameVersion,
                          const QString &targetLoaderName) {
    const auto loaderWord = [](const QString &name) {
        return name.isEmpty() ? QStringLiteral("原版") : name;
    };
    switch (compatibility) {
    case Compatibility::Same:
        return QStringLiteral("目标版本（%1 / %2）与源一致：模组与配置可以原样搬过去。")
            .arg(sourceGameVersion, loaderWord(sourceLoaderName));
    case Compatibility::GameVersionDiffers:
        return QStringLiteral("目标版本是 %1，源是 %2 —— 模组与部分配置可能不兼容，"
                              "所以「模组」默认不勾选。")
            .arg(targetGameVersion, sourceGameVersion);
    case Compatibility::LoaderDiffers:
        return QStringLiteral("目标加载器是 %1，源是 %2（游戏版本都是 %3）—— "
                              "模组与加载器相关配置可能不兼容，所以「模组」默认不勾选。")
            .arg(loaderWord(targetLoaderName), loaderWord(sourceLoaderName), sourceGameVersion);
    case Compatibility::BothDiffer:
        return QStringLiteral("目标版本是 %1 / %2，源是 %3 / %4 —— 模组与部分配置可能不兼容，"
                              "所以「模组」默认不勾选。")
            .arg(targetGameVersion, loaderWord(targetLoaderName), sourceGameVersion,
                 loaderWord(sourceLoaderName));
    case Compatibility::Unknown:
        return QStringLiteral("有一边的游戏版本或加载器读不出来 —— 不做猜测："
                              "「模组」默认不勾选，请自己判断要不要原样搬过去。");
    }
    return QString();
}

bool neverDefaultSelected(const QString &key) {
    return key == QStringLiteral("screenshots") || key == QStringLiteral("logs")
           || key == QStringLiteral("crashreports");
}

QStringList Plan::defaultSelectedKeys() const {
    QStringList keys;
    for (const Item &item : items) {
        if (item.exists && item.defaultSelected) keys << item.key;
    }
    return keys;
}

int Plan::selectedFileCount(const QStringList &keys) const {
    int files = 0;
    for (const Item &item : items) {
        if (item.exists && keys.contains(item.key)) files += item.fileCount;
    }
    return files;
}

qint64 Plan::selectedBytes(const QStringList &keys) const {
    qint64 bytes = 0;
    for (const Item &item : items) {
        if (item.exists && keys.contains(item.key)) bytes += item.bytes;
    }
    return bytes;
}

QString Plan::headline() const {
    if (!ok) return problem;
    const int count = defaultSelectedKeys().size();
    const QString prefix =
        upgrade ? QStringLiteral("升级副本计划：%1").arg(compatibilityName(compatibility))
                : QStringLiteral("迁移计划：%1").arg(compatibilityName(compatibility));
    const qint64 bytes = selectedBytes(defaultSelectedKeys());
    const int files = selectedFileCount(defaultSelectedKeys());
    return QStringLiteral("%1 / 默认搬 %2 项（%3）").arg(prefix).arg(count).arg(joinSize(files, bytes));
}

QString Plan::detailText() const {
    if (!ok) return problem;
    QStringList lines;
    lines << headline();
    lines << compatibilitySentence;
    lines << QString();
    lines << (upgrade ? QStringLiteral("以下是这次升级副本里要搬的内容：")
                      : QStringLiteral("以下是这次迁移里要搬的内容："));
    for (const Item &item : items) {
        if (!item.exists) {
            lines << QStringLiteral("• %1：这一份实例里没有").arg(item.label);
            continue;
        }
        // 勾选框记号：`☑` / `☐` 默认界面字体里没有（首次排版约 280 ms 的字体回退），
        // 换成同一份字体里有的 `■` / `□`（见 `ui/TextMarks.hpp` 头注释）。
        lines << QStringLiteral("%1 %2（%3）")
                     .arg(item.defaultSelected ? TextMarks::checked() : TextMarks::unchecked(),
                          item.label, joinSize(item.fileCount, item.bytes));
        if (!item.note.isEmpty()) lines << QStringLiteral("    %1").arg(item.note);
    }
    lines << QString();
    lines << QStringLiteral("日志、截图、崩溃报告只是记录，默认不搬（想搬就手动勾上）。");
    lines << QStringLiteral("迁移只增加文件、不删除、不覆盖目标里已有的同名文件；"
                            "两边的游戏都必须先关掉。");
    return lines.join(QLatin1Char('\n'));
}

QString Plan::confirmText(const QStringList &keys) const {
    const int files = selectedFileCount(keys);
    const qint64 bytes = selectedBytes(keys);
    QString text = upgrade
                       ? QStringLiteral("将创建一份升级副本，并把选中的 %1 项（%2）搬进去。")
                             .arg(QString::number(keys.size()), joinSize(files, bytes))
                       : QStringLiteral("将把选中的 %1 项（%2）搬进目标实例。")
                             .arg(QString::number(keys.size()), joinSize(files, bytes));
    text += QStringLiteral("\n\n只增加文件，不删除、不覆盖目标里已有的同名文件。");
    if (compatibility != Compatibility::Same) {
        text += QStringLiteral("\n\n兼容性结论：%1").arg(compatibilityName(compatibility));
    }
    return text;
}

Plan plan(const Inputs &inputs) {
    Plan result;
    result.upgrade = inputs.upgrade;

    const QString sourceDir = inputs.sourceDir.trimmed();
    if (sourceDir.isEmpty() || !QDir(sourceDir).exists()) {
        result.problem = QStringLiteral("找不到源实例目录：%1").arg(
            sourceDir.isEmpty() ? QStringLiteral("（没有给）") : QDir::toNativeSeparators(sourceDir));
        return result;
    }

    const bool sourceVersionKnown = !inputs.sourceGameVersion.trimmed().isEmpty();
    const bool targetVersionKnown = !inputs.targetGameVersion.trimmed().isEmpty();
    // 「原版」是一个**读得出来**的结论（加载器那一格空着且版本 json 里没有 inheritsFrom），
    // 所以这里不把空加载器当成"认不出来"；只有版本那一格空着才进入 Unknown。
    if (!sourceVersionKnown || !targetVersionKnown) {
        result.compatibility = Compatibility::Unknown;
    } else {
        const bool sameGame =
            inputs.sourceGameVersion.compare(inputs.targetGameVersion, Qt::CaseInsensitive) == 0;
        const bool sameLoader =
            inputs.sourceLoaderName.compare(inputs.targetLoaderName, Qt::CaseInsensitive) == 0;
        if (sameGame && sameLoader) {
            result.compatibility = Compatibility::Same;
        } else if (!sameGame && !sameLoader) {
            result.compatibility = Compatibility::BothDiffer;
        } else if (!sameGame) {
            result.compatibility = Compatibility::GameVersionDiffers;
        } else {
            result.compatibility = Compatibility::LoaderDiffers;
        }
    }
    result.compatibilitySentence = compatibilityText(
        result.compatibility, inputs.sourceGameVersion, inputs.sourceLoaderName,
        inputs.targetGameVersion, inputs.targetLoaderName);
    // 加载器小版本不同：结论仍是 Same（模组默认勾选），但把事实说出来。
    if (result.compatibility == Compatibility::Same && !inputs.sourceLoaderVersion.isEmpty()
        && !inputs.targetLoaderVersion.isEmpty()
        && inputs.sourceLoaderVersion != inputs.targetLoaderVersion) {
        result.compatibilitySentence +=
            QStringLiteral("加载器小版本不同（源 %1 / 目标 %2），一般没问题，起不来时先看它。")
                .arg(inputs.sourceLoaderVersion, inputs.targetLoaderVersion);
    }

    struct Row {
        const char *key;
        const char *label;
        const char *relativePath;
        bool isDirectory;
    };
    // 顺序就是界面上的顺序：先把"真的在玩的东西"摆前面，记录类垫底。
    static const Row rows[] = {
        {"saves", "存档", "saves", true},
        {"mods", "模组", "mods", true},
        {"config", "模组配置", "config", true},
        {"defaultconfigs", "默认配置", "defaultconfigs", true},
        {"resourcepacks", "资源包", "resourcepacks", true},
        {"shaderpacks", "光影包", "shaderpacks", true},
        {"options", "游戏设置", "options.txt", false},
        {"servers", "服务器列表", "servers.dat", false},
        {"structure", "结构文件", "structures", true},
        {"screenshots", "截图", "screenshots", true},
        {"logs", "日志", "logs", true},
        {"crashreports", "崩溃报告", "crash-reports", true},
    };

    for (const Row &row : rows) {
        Item item;
        item.key = QString::fromLatin1(row.key);
        item.label = QString::fromUtf8(row.label);
        item.relativePath = QString::fromLatin1(row.relativePath);
        item.isDirectory = row.isDirectory;
        const QString absolutePath = sourceDir + QLatin1Char('/') + item.relativePath;
        item.exists = QFileInfo::exists(absolutePath);
        if (item.exists) {
            const TreeSize size = measure(absolutePath, item.isDirectory);
            item.fileCount = size.files;
            item.bytes = size.bytes;
        }
        // 默认勾选：记录类永不默认；模组只看兼容结论；其余有就勾。
        if (!item.exists || neverDefaultSelected(item.key)) {
            item.defaultSelected = false;
        } else if (item.key == QStringLiteral("mods")) {
            item.defaultSelected = result.compatibility == Compatibility::Same;
        } else {
            item.defaultSelected = true;
        }
        if (item.key == QStringLiteral("mods") && item.exists
            && result.compatibility != Compatibility::Same) {
            item.note = QStringLiteral("目标版本 / 加载器与源不一致，模组可能起不来；"
                                       "确认要原样搬过去再自己勾上。");
        } else if (neverDefaultSelected(item.key) && item.exists) {
            item.note = QStringLiteral("只是记录，默认不搬。");
        } else if (result.compatibility == Compatibility::Unknown && item.exists
                   && item.key != QStringLiteral("saves")) {
            item.note = QStringLiteral("认不出目标版本：这一项是否兼容不确定，"
                                       "要不要搬请自己判断。");
        }
        result.items.append(item);
    }

    result.ok = true;
    return result;
}

}  // namespace TransferPlanner
