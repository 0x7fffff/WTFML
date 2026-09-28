/**
 * @file WorldFiles.cpp
 * @brief 实现 WorldFiles.hpp 里那四个函数。
 * @author 0x7fffff
 * @date 2026-09-28
 * @note 见 WorldFiles.hpp 的说明。这里只留实现上的坑。
 */

#include "WorldFiles.hpp"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include "FolderManager.hpp"
#include "InstanceFiles.hpp"

namespace {

/// 一个世界目录里必须有的那一个文件。游戏靠它认"这是不是一个世界"。
constexpr char kLevelDatName[] = "level.dat";

/// `<worldDir>/level.dat` 的完整路径。
QString levelDatPath(const QString &worldDir) {
    return worldDir + QLatin1Char('/') + QLatin1String(kLevelDatName);
}

}  // namespace

namespace WorldFiles {

bool looksLikeWorld(const QString &worldDir) {
    return QFileInfo::exists(levelDatPath(worldDir));
}

QString nameProblem(const QString &rawName) {
    return InstanceFiles::validateDirName(rawName);
}

bool copyWorldInto(const QString &worldDir, const QString &savesDir, const QString &name,
                   QString *problemOut) {
    const auto fail = [problemOut](const QString &reason) {
        if (problemOut != nullptr) {
            *problemOut = reason;
        }
        return false;
    };
    if (problemOut != nullptr) {
        problemOut->clear();
    }

    const QString source = worldDir.trimmed();
    const QString target = savesDir.trimmed();
    if (source.isEmpty()) {
        return fail(QStringLiteral("没有指定要导入哪一个世界。"));
    }
    if (target.isEmpty()) {
        return fail(QStringLiteral("不知道要导入到哪个实例里去。"));
    }

    // ① 名字。**先判名字再查盘**：名字本身就不合法时拼出来的路径不可信，拿它去盘上问
    //    "有没有重名"没有意义（和 InstanceFiles::checkImportTargetName 那一路同一顺序）。
    const QString nameProblemText = nameProblem(name);
    if (!nameProblemText.isEmpty()) {
        return fail(nameProblemText);
    }
    const QString dirName = name.trimmed();

    // ② 源。`QDir(...).exists()` 对**文件**也说 true，所以再走一遍 looksLikeWorld() ——
    //    用户把 `saves/xxx/level.dat`（单个文件）选进来的话，只判 isDir 是拦不住的。
    if (!QFileInfo(source).isDir()) {
        return fail(QStringLiteral("「%1」不是一个目录。").arg(source));
    }
    if (!looksLikeWorld(source)) {
        return fail(QStringLiteral("「%1」里面没有 level.dat，看起来不是一个世界存档"
                                   "（是不是选错了上一层的文件夹？）")
                        .arg(QDir(source).dirName()));
    }

    // ③ 目的地。saves 目录**必须已经存在**：mkpath 出来一个空的 saves/ 也能用，但那说明
    //    这一份实例根本没被游戏跑过，这时候"导入世界"多半是用户点错了实例 —— 宁可让他
    //    自己先跑一次游戏，也不要在这里替他造一个目录出来（造出来的那个目录在游戏那边
    //    和在文件管理页那边都是"凭空多出来的一格"，用户没法解释它哪来的）。
    if (!QFileInfo(target).isDir()) {
        return fail(QStringLiteral("这个实例里还没有 saves 文件夹（先启动一次游戏，"
                                   "或者手动建一个）。"));
    }

    const QString destDir = target + QLatin1Char('/') + dirName;
    if (QFileInfo::exists(destDir)) {
        return fail(QStringLiteral("这个实例里已经有一个叫「%1」的世界了，换个名字再导入。")
                        .arg(dirName));
    }

    if (!copyTreeWithoutFollowingLinks(source, destDir)) {
        // 半成品自己收拾掉（理由见头文件）：世界是几百 MB 一坨，没有"接着拷"这回事。
        if (!removeTreeWithoutFollowingLinks(destDir)) {
            return fail(QStringLiteral("导入「%1」的时候中断了，这一份没有做出来。\n"
                                       "（另外，那份没拷完的 %2 也没能删掉，你需要自己"
                                       "删掉它。）")
                            .arg(dirName, destDir));
        }
        return fail(QStringLiteral("导入「%1」的时候中断了，这一份没有做出来。").arg(dirName));
    }
    return true;
}

bool renameWorld(const QString &worldDir, const QString &newName, QString *problemOut) {
    const auto fail = [problemOut](const QString &reason) {
        if (problemOut != nullptr) {
            *problemOut = reason;
        }
        return false;
    };
    if (problemOut != nullptr) {
        problemOut->clear();
    }

    const QString source = worldDir.trimmed();
    if (source.isEmpty()) {
        return fail(QStringLiteral("没有指定要改哪一个世界。"));
    }
    const QString problem = nameProblem(newName);
    if (!problem.isEmpty()) {
        return fail(problem);
    }
    const QString dirName = newName.trimmed();

    const QFileInfo sourceInfo(source);
    if (!sourceInfo.isDir()) {
        return fail(QStringLiteral("「%1」已经不在盘上了。").arg(source));
    }

    // 只差大小写（`Foo` → `foo`）是**合法**的改名：它改的就是磁盘上那个名字的大小写。
    // 所以恰好和现名相等的那一项不参与"重名"比较（和 VersionManager::renameInstance()
    // 那边同一条规矩）。逐字相等则什么都不用做，直接算成功。
    const QString current = sourceInfo.fileName();
    if (current == dirName) {
        return true;
    }

    const QString parent = sourceInfo.absolutePath();
    const QString destDir = parent + QLatin1Char('/') + dirName;
    if (current.compare(dirName, Qt::CaseInsensitive) != 0 && QFileInfo::exists(destDir)) {
        return fail(QStringLiteral("这个存档文件夹里已经有一个叫「%1」的世界了。").arg(dirName));
    }

    // QDir::rename() 的第二个参数是**目标全路径**（不是"新名字"），写成新名字的话会在
    // 当前工作目录下改出一个同名目录来 —— 这是那个重载最容易看错的一处。
    if (!QDir().rename(source, destDir)) {
        return fail(QStringLiteral("改名失败了（是不是有别的程序正开着这个世界？）"));
    }
    return true;
}

}  // namespace WorldFiles
