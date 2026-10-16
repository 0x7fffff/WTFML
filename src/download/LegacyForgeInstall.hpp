/**
 * @file LegacyForgeInstall.hpp
 * @brief 老 Forge 安装器（2019 年以前那种）的"自己装"这条路：从安装器 jar 里那份
 *        `install_profile.json` 算出要落盘的版本 json，以及要替它摆好的那个 universal jar。
 * @author 0x7fffff
 * @date 2026-10-17
 * @note 纯函数：不联网、不读盘、不发信号。进来的两个对象是"安装器 jar 里那份安装说明的字节"
 *       和"原版那份 version.json"，出去的是一份能直接落盘的版本 json。
 *
 * 为什么要有这一层（用户 2026-10-17 报的那句「新电脑上完全装不了」）：
 *
 * 那个年代的 Forge 安装器**没有命令行安装这条路** —— 它的 `--help` 里只有
 * `--extract` / `--installServer` / `--offline`，`--installClient` 一个字都没有。不给参数
 * 直接 `java -jar` 它就弹一个窗口等人点"OK"，本启动器替用户点不了。于是：一台**只有本
 * 启动器**的新电脑上，1.7.10 / 1.8.9 / 1.9.4 / 1.10.2 / 1.11.2 / 1.12.2（推荐的 2847，
 * 实测这份不认 --installClient；同一代的 2860 认）这些版本的 Forge 就**永远装不上** ——
 * 而安装页默认挑的正是"稳定版"那一颗，1.12.2 那一颗恰好就是 2847。这是没做完，不是边界。
 *
 * 好在那个年代的安装器把该说的都写在自己 jar 里的 `install_profile.json` 里了（老 schema：
 * `install` + `versionInfo` 两节，实测 1.7.10 / 1.12.2 两份都是），照着它自己那份说明把
 * 三件事做了就行，**既不用开它的界面、也不用起 JVM**：
 *
 *   1. `versionInfo` 就是它要写进 `versions/<id>/<id>.json` 的那份 json（原样，只改 id
 *      和库表，见下面）—— 里面 mainClass / minecraftArguments / libraries 全是齐的；
 *   2. `install.filePath` 指名了它 jar 里那颗 universal jar（1.12.2-2847 是 4.6 MB 那个
 *      `forge-1.12.2-14.23.5.2847-universal.jar`）—— 它本该被放进
 *      `libraries/net/minecraftforge/forge/<install.path 的后半段>/`，我们照放；
 *   3. 其余十几个库（asm-all / akka / scala 那一串）从 maven 下，走启动器自己那条下载路。
 *
 * 三处**实测**过的细节（2026-10-17 逐条在真数据上量的，别凭印象改回去）：
 *
 *  * **它自己那一颗库不能带 url。** versionInfo 里 `net.minecraftforge:forge:<版本>` 那颗是
 *    写着 `"url": "https://maven.minecraftforge.net/"` 的，而那道 maven 地址下的**普通名字
 *    是 404**（`forge-1.12.2-14.23.5.2847.jar` 404，`-universal.jar` 200 —— 安装器把
 *    universal 摆进 libraries/ 时是按普通名字命名的，那具 4.6 MB 的 jar 因此不在仓库里）。
 *    所以这一颗要写成**只有 name、没有 url**：LibraryRules::artifactFor() 的第 3 种写法
 *    （`localOnly`）正好是"不下、但必须上 classpath"，和 OptiFine 那两个库同一条路。少了
 *    这一条，安装会死在"下 forge-1.12.2-14.23.5.2847.jar 404"上。
 *  * **没有 url 的库默认去 Mojang 拿。** 实测 2847 那份里有 6 颗没有 url：
 *    launchwrapper 1.12 / lzma 0.0.1 / vecmath 1.5.2 / trove4j 3.0.3 / jna 4.4.0 /
 *    jopt-simple 5.0.3。前四颗在 libraries.minecraft.net 上**都是 200**（同一批探的），
 *    后两颗**原版 1.12.2 自己就带着**（见下面那条）—— 1.7.10 那份同理（launchwrapper / lzma
 *    没有 url，原版 1.7.10 带着 guava 17.0 / jopt-simple 4.5 / commons-lang3 3.3.2）。
 *  * **原版已经带上的那些要丢掉。** 不带 url 的库如果原版那份 json 里已经有同名条目，我们
 *    猜的那个 Mojang 地址就是白猜（下重了、而且不是版本错就是多下一次）；直接丢掉，它照样
 *    在 classpath 上（继承链合并的时候原版那一份会带进来）。
 *
 * NOTE: 这份 versionInfo 里的 id 是**上游拼的一个怪名字**（1.12.2-2847 是
 *       `1.12.2-forge1.12.2-14.23.5.2847`，1.7.10 是 `1.7.10-Forge10.13.4.1614-1.7.10`），
 *       每个年代还不一样。我们**不用它**：目录名和 json 里的 id 都由调用方给（本启动器
 *       自己的约定，`LoaderMeta::forgeLoaderId()`），否则那个 id 会一路飘到
 *       `checkInstallerResult()` / wtfml.meta / 版本列表里，和别的加载器长得不一样。
 *       真实启动那一侧不看这个 id —— 父版本看的是 `inheritsFrom`，客户端 jar 看的是 `jar`
 *       （两份 profile 里都是 `"jar": "<原版号>"`，所以我们**不需要**往版本目录里再抄一份
 *       原版客户端 jar）。
 */
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QSet>

#include "launcher/LibraryRules.hpp"

namespace LegacyForgeInstall {

/// 没有 url 的那些库往哪个仓库猜。原版那份 json 里的库全在这个地址上，实测上面那几颗
/// 没有 url 的（launchwrapper / lzma / vecmath / trove4j）在这里也是 200。
inline const QString &mojangLibraryBase() {
    static const QString base = QStringLiteral("https://libraries.minecraft.net/");
    return base;
}

/// 认不出来是哪一种。调用方按这个分类拼话（这里不 tr()：这一层是纯函数，仓库里
/// LoaderMeta::ProfileProblem 那套也是这个形状）。
enum class Problem {
    None,
    NotJson,        ///< 那段字节根本不是 json（jar 坏了 / 读到别的条目上去了）
    ModernProfile,  ///< 没有 install / versionInfo 这两节 —— 2019 年以后那种新格式
    MissingInstall, ///< install 那一节缺字段（filePath / path）
    UnknownPath,    ///< `install.path` 拼不出安全的 maven 路径（名字里有脏字符）
    NoParent,       ///< 认不出它基于哪个原版（inheritsFrom 和 install.minecraft 都是空的）
    NoLibraries,    ///< 一条库都没有 —— 这种 profile 不可信，别照着它写 json
};

/// planFromInstallProfile() 的结果。
struct Plan {
    bool ok = false;
    Problem problem = Problem::None;
    /// 认不出来时的细节（哪个坐标、哪一节），拼进上面那句里。可能为空。
    QString detail;

    /// 它基于哪个原版。调用方要用它去下原版那一批文件，所以单独给一份（json 里也有）。
    QString inheritsFrom;
    /// 要落盘的那一份版本 json：id 已经改成调用方给的那个，libraries 已经补好 url、
    /// 该丢的丢了。**直接写盘**即可。
    QJsonObject versionJson;

    /// 它 jar 里那颗 universal jar 的条目名（`install.filePath`）。
    QString universalEntry;
    /// 那颗 universal jar 该落到 `libraries/` 下的相对路径（`install.path` 换算来的）。
    QString universalLibraryPath;

    /// 因为"原版自己就带着"而丢掉的库有几条（判据用，也进日志）。
    int droppedFromParent = 0;
    /// 没有 url、按 mojangLibraryBase() 猜的有几条（判据用）。
    int assumedMojang = 0;
};

/// 这一条库的下载地址（`downloads.artifact.url`）有没有。有的话就别去动它 —— 那是安装器
/// 自己写明的地址，比我们猜的准。
inline bool artifactUrlPresent(const QJsonObject &libObj) {
    return !libObj.value(QStringLiteral("downloads"))
                .toObject()
                .value(QStringLiteral("artifact"))
                .toObject()
                .value(QStringLiteral("url"))
                .toString()
                .isEmpty();
}

/**
 * @brief 照老 Forge 安装器自己那份安装说明，算出该落盘的版本 json。
 * @param profileJson 安装器 jar 里 `install_profile.json` 的**原始字节**。
 * @param parentJson 原版那份 version.json（用来认"哪些库原版已经带上了"）。拿不到时传空对象
 *        —— 那种情况下一条都不丢，靠"没有 url 的按 Mojang 猜"兜底（实测那几颗在 Mojang
 *        上都有，只是原版已经带上、会下重）。
 * @param loaderId 调用方给这个版本的 id（本启动器的约定，见文件头那条 NOTE）。
 * @return 见 Plan。`ok == false` 时**不要**用 versionJson（它是空的）。
 */
inline Plan planFromInstallProfile(const QByteArray &profileJson, const QJsonObject &parentJson,
                                   const QString &loaderId) {
    Plan plan;

    const QJsonDocument doc = QJsonDocument::fromJson(profileJson);
    if (!doc.isObject()) {
        plan.problem = Problem::NotJson;
        return plan;
    }
    const QJsonObject root = doc.object();
    const QJsonObject install = root.value(QStringLiteral("install")).toObject();
    const QJsonObject versionInfo = root.value(QStringLiteral("versionInfo")).toObject();
    if (install.isEmpty() || versionInfo.isEmpty()) {
        // 1.12.2 的 2860 及以后那种格式：`_comment_/spec/profile/processors`，那份的
        // `--installClient` 是认的（实测），本来就该走安装器那条路。走到这儿只可能是
        // 探针没探出来（起不来 / 超时），不是"我们要自己装"。
        plan.problem = Problem::ModernProfile;
        return plan;
    }

    const QString coordinate = install.value(QStringLiteral("path")).toString();
    plan.universalEntry = install.value(QStringLiteral("filePath")).toString();
    if (coordinate.isEmpty() || plan.universalEntry.isEmpty()) {
        plan.problem = Problem::MissingInstall;
        plan.detail = coordinate.isEmpty() ? QStringLiteral("path") : QStringLiteral("filePath");
        return plan;
    }

    // 名字进过一次白名单才敢当路径用（同一个理由：这段字符串是下载来的数据）。
    plan.universalLibraryPath = LibraryRules::mavenPathForName(coordinate);
    if (plan.universalLibraryPath.isEmpty()) {
        plan.problem = Problem::UnknownPath;
        plan.detail = coordinate;
        return plan;
    }

    // 它基于哪个原版：两份实测的 profile 里 `inheritsFrom` 和 `install.minecraft` 是同一个
    // 值（1.12.2 / 1.7.10），但一个是"给启动器看的"、一个是"给安装器自己看的"，谁也说不准
    // 哪个年代哪一份会缺一个，所以两个都认。
    QString inheritsFrom = versionInfo.value(QStringLiteral("inheritsFrom")).toString();
    if (inheritsFrom.isEmpty()) {
        inheritsFrom = install.value(QStringLiteral("minecraft")).toString();
    }
    if (inheritsFrom.isEmpty()) {
        plan.problem = Problem::NoParent;
        return plan;
    }
    plan.inheritsFrom = inheritsFrom;

    QSet<QString> parentNames;
    for (const QJsonValue &value : parentJson.value(QStringLiteral("libraries")).toArray()) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
            parentNames.insert(name);
        }
    }

    QJsonArray libraries;
    bool universalListed = false;
    // 库表**按它给的顺序**走（实测两份 profile 里 universal jar 都是第一条；顺序留着不动，
    // 真的安装器写出来就是这一份，没必要替它重排）。
    for (const QJsonValue &value : versionInfo.value(QStringLiteral("libraries")).toArray()) {
        const QJsonObject libObj = value.toObject();
        const QString name = libObj.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            continue;
        }
        // NOTE: 这里**故意不看** `clientreq` / `serverreq`（1.6~1.7 那个年代的两个键：安装器
        //       用它说自己这条库是给客户端还是给服务端的）。看起来该照它办 —— 2847 那份里
        //       `org.jline:jline:3.5.1` 明明写着 `clientreq: false` —— 但真装出来的客户端
        //       classpath 上**有 jline**（本机那份 1.12.2-Forge_14.23.5.2860 的 json 里，
        //       jline 是从 forge maven 下来的，和 profile 里那颗一模一样）⇒ 安装器自己不看
        //       这个键，我们也不看。少一个库的症状是游戏起来时一个 NoClassDefFoundError，
        //       而"多带一个库"顶多是重复 —— 和 LibraryRules 里 `"rules": []` 那条判据同一个
        //       方向。（这条是 V6i 那两条断言逼出来的：第一版按 clientreq 丢，19 条变成 18 条。）
        if (name == coordinate) {
            // 见文件头那条 NOTE：**只留 name**（`localOnly` ⇒ 不下、但进 classpath），
            // jar 由调用方从安装器里取出来摆好。
            universalListed = true;
            QJsonObject entry;
            entry[QStringLiteral("name")] = name;
            libraries.append(entry);
            continue;
        }
        if (parentNames.contains(name)) {
            // 原版那一份里已经有同名条目：丢掉它（继承链合并的时候照样进 classpath），
            // 免得我们去猜的那个地址下重一遍、下错一遍。
            plan.droppedFromParent++;
            continue;
        }
        QJsonObject entry = libObj;
        if (entry.value(QStringLiteral("url")).toString().isEmpty() && !artifactUrlPresent(entry)) {
            entry[QStringLiteral("url")] = mojangLibraryBase();
            plan.assumedMojang++;
        }
        libraries.append(entry);
    }

    if (!universalListed) {
        // 有的 profile 只在 install.path 里说那颗 jar、不把它列进 libraries（实测这两份
        // 都列了，但这条路是"照它说的办"，少列一条我们补上比漏掉一个 classpath 项强）。
        QJsonObject entry;
        entry[QStringLiteral("name")] = coordinate;
        libraries.insert(0, entry);
    }
    if (libraries.isEmpty()) {
        plan.problem = Problem::NoLibraries;
        return plan;
    }

    QJsonObject out = versionInfo;
    out[QStringLiteral("libraries")] = libraries;
    out[QStringLiteral("id")] = loaderId;
    plan.versionJson = out;
    plan.ok = true;
    return plan;
}

} // namespace LegacyForgeInstall
