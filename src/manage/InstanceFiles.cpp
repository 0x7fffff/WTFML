/**
 * @file InstanceFiles.cpp
 * @brief Implements the instance-directory criterion and jar/json name resolution.
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 判据的来龙去脉写在 .hpp 的块注释里；这里只有实现，几处 NOTE 说的是"为什么不能
 *       照直觉改"。
 */

#include "InstanceFiles.hpp"

#include <QDir>
#include <QLatin1Char>

#include "../util/AppPaths.hpp"

namespace InstanceFiles {
namespace {

/// 在清单里找一个名字，**不区分大小写**，返回它在磁盘上的**真实**拼写（找不到返回空串）。
///
/// 为什么不区分大小写：Windows 的文件系统本来就不区分。用户把目录从 `MyPack` 改成 `mypack`
/// 之后，里面的 `MyPack.jar` 是**同一个文件**，用区分大小写的比较会认不出来。返回值必须是
/// 磁盘上那个真实拼写 —— 它要拿去拼路径。
QString findNamed(const QStringList &names, const QString &wanted) {
    for (const QString &name : names) {
        if (name.compare(wanted, Qt::CaseInsensitive) == 0) {
            return name;
        }
    }
    return QString();
}

}  // namespace

Resolved resolve(const DirListing &listing) {
    Resolved out;
    out.jarCandidates = listing.jarNames;

    // ① 精确匹配（<目录名>.jar）优先。绝大多数实例走这条，而且它不依赖扫描顺序。
    // ② 否则顶层恰好一个 jar 就是它 —— 这一条正是本轮放松判据要救的那类实例：
    //    用户自己在资源管理器里改了目录名，没改里面的 jar 名。
    // ③ 其余（一个都没有 / 两个以上且没有精确匹配）都不算实例。
    out.jarName = findNamed(listing.jarNames, listing.dirName + QStringLiteral(".jar"));
    if (out.jarName.isEmpty() && listing.jarNames.size() == 1) {
        out.jarName = listing.jarNames.first();
    }

    // json 按同样的 ①/② 解析，但**解析不到不算认不出来**：
    //   1. installedVersions() 的语义始终是"客户端 jar 在不在"（半个版本、只有 json 的下载
    //      残骸都不是实例），把 json 纳入判据会把"下到一半"的目录从"看得见、点了会告诉你
    //      为什么"变成"看不见" —— 正是本轮要消灭的那种静默。
    //   2. json 缺席有它自己现成的错误路径：启动时会弹"版本信息缺失，请先重新下载"。
    out.jsonName = findNamed(listing.jsonNames, listing.dirName + QStringLiteral(".json"));
    if (out.jsonName.isEmpty() && listing.jsonNames.size() == 1) {
        out.jsonName = listing.jsonNames.first();
    }

    return out;
}

DirListing scanDir(const QString &versionId) {
    DirListing out;
    out.dirName = versionId;

    const QDir dir(AppPaths::versionDir(versionId));
    if (!dir.exists()) {
        return out;
    }

    // NOTE: 必须**非递归** —— QDir::Files 不能配上 QDir::Subdirectories。
    //       实例目录底下还有 libraries/：下载时把整个版本目录当目的地，库 jar 全写在
    //       versions/<id>/libraries/ 里，真机上一个整合包随手就是几百个。递归扫会把它们
    //       统统当成客户端 jar 的候选，于是"顶层恰好一个 jar"这条判据立刻失效、
    //       所有实例反而认不出来了。别顺手加上 Subdirectories。
    out.jarNames = dir.entryList(QStringList() << QStringLiteral("*.jar"), QDir::Files);
    out.jsonNames = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    return out;
}

Resolved resolveInstance(const QString &versionId) { return resolve(scanDir(versionId)); }

QString jarName(const QString &versionId) {
    const Resolved resolved = resolveInstance(versionId);
    // 退回老名字。见 .hpp 的 NOTE —— 这不是"错误处理"，是让只想拿个名字去看看在不在的
    // 调用方在正常目录上逐字节不变。
    return resolved.recognized() ? resolved.jarName : versionId + QStringLiteral(".jar");
}

QString jsonName(const QString &versionId) {
    const Resolved resolved = resolveInstance(versionId);
    return resolved.jsonName.isEmpty() ? versionId + QStringLiteral(".json") : resolved.jsonName;
}

QString jarPath(const QString &versionId) {
    return AppPaths::versionDir(versionId) + QLatin1Char('/') + jarName(versionId);
}

QString jsonPath(const QString &versionId) {
    return AppPaths::versionDir(versionId) + QLatin1Char('/') + jsonName(versionId);
}

}  // namespace InstanceFiles
