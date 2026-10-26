/**
 * @file VersionManagerInternals.hpp
 * @brief `VersionManager` 那几个 .cpp 之间共用的、**只在实现层存在**的小工具。
 *
 * 【为什么单独拿一个头文件】
 *
 * `VersionManager` 按职责拆成了四个 .cpp：核心（版本发现 / 下载编排 / 批次收尾）、
 * 加载器安装、整合包导入、实例识别与导出。这些助手原来住在 `VersionManager.cpp` 顶上的
 * 匿名命名空间里 —— 匿名命名空间是**内部链接**，另外那三个 .cpp 看不见。拆文件的时候
 * 它们必须有个共同的家，于是搬到这里。
 *
 * 搬过来、而不是拆开各抄一份：`copyTreeInto()` 上面那三条规矩（只覆盖同名文件、覆盖要
 * 原子、失败如实返回）属于"两处各抄一份的话，只改一处不会报错"的那一类东西。同一个理由
 * 也写在 `copyTreeWithoutFollowingLinks()` 那段注释里。
 *
 * 【两条写法上的讲究】
 *
 * * 函数一律 `inline`：头文件里有定义，非 inline 的函数被两个 .cpp 各包含一次就是重定义
 *   （链接期报 duplicate symbol，不是编译期，报出来还指不到这里）。
 * * `const QString kInstanceIdField` 这种也带 `inline`：命名空间作用域的 `const` 默认是
 *   内部链接，每个 .cpp 各得一份倒也不犯错，但写成一个 `inline` 变量更直观；`constexpr`
 *   自带 inline，不用额外写。
 *
 * NOTE: 这里面的东西**不属于** `VersionManager` 的接口，也不许类外面用 —— 它只解决
 * "同一个类的实现分散在几个文件里"这一个问题。真要被第二处代码复用，那是往 `util/` 搬的信号。
 */

#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QUuid>

#include "../download/LoaderMeta.hpp"
#include "../util/AppPaths.hpp"

namespace VersionManagerInternals {

/// wtfml.meta 里那个"从生到死都跟着这个实例"的 id 的字段名。
///
/// NOTE: 刻意**不叫 "id"**，理由和这个文件不叫 wtfml.json 是同一件事（见 .hpp 里
/// displayParts() 那段）：版本 JSON 里的 "id" 有特殊语义，一个文件里出现两个 id
/// 早晚会被读错，而那类错误的形态是"静默选错 Java"级别的。换个不可能撞的名字，
/// 这个错在结构上就不存在。
inline const QString kInstanceIdField = QStringLiteral("instanceId");

/// wtfml.meta 里"用户给这个实例起的显示名"的字段名。
///
/// NOTE: 文件里叫 `name`，但 VersionDisplayParts 里那个成员叫 `customName`。
/// 这里写、displayParts() 那边读，**两边必须是同一个串** —— 写错一个字不会报错，
/// 表现是"改完名界面上没反应"。改的时候两边一起改（那边在 displayParts() 的
/// readString("name")）。
inline const QString kDisplayNameField = QStringLiteral("name");

/// 一个新的稳定实例 id。只有这一处生成它，免得"生成规则"在改名的路径上被抄第二遍。
inline QString newInstanceId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

/// Forge / NeoForge 的安装器那个 Java 进程最多跑多久。**不是**"失败得快一点"，
/// 是"别让界面永远停在忙态"（那个进程不结束，界面就一直禁着按钮）。
constexpr int kInstallerTimeoutMs = 30 * 60 * 1000;

/// 探 `--help` 那一次最多等多久。它本该打一段帮助就退出（实测两个安装器都在 1 秒内回来），
/// 所以给得短：探不出来就按"宁可信其有"往下走，不能为它耽误真正的安装。
constexpr int kInstallerProbeTimeoutMs = 60 * 1000;

/// 安装器跑着的时候，每隔这么多秒报一次"还在跑"。
///
/// 安装器的输出里绝大部分是 `Patching xxx 1/1` 这种**逐文件**的行，那些不该进日志
/// （进度归进度条，见产品约定）。但一声不吭地等三分钟也不像话 —— 用户会以为卡死了。
/// 折中是：一行都不逐条转，但每 20 秒报一次"还在跑 + 它最后打的那一行"。
constexpr int kInstallerHeartbeatSeconds = 20;

/// 安装器连续这么久**一个字节都没往外打**，就当它卡在一条死连接上了：杀掉重起一次。
///
/// 依据：Forge 1.20.1 / 47.4.0 的安装器在 `DOWNLOAD_MOJMAPS`
/// 那一步一声不吭停了 580 秒（最后一行永远是 `Skip: false`），而同一个映射地址用 curl
/// 4.5 秒就拿完 —— Java 那边是一条既不超时也不报错的死连接（`setTransferTimeout` 那套
/// 只管我们自己的 QNetworkReply，管不到安装器内部的 HttpURLConnection）。安装器每下一次
/// 库、每跑一个 processor 都会先打一行，所以静默三分钟是"卡住"而不是"在慢慢算"。
constexpr int kInstallerStallTimeoutMs = 3 * 60 * 1000;

/// 安装器输出的尾巴留多少字节。失败时要报"卡在哪一行"，但没必要把几万行都攥在内存里。
constexpr int kInstallerOutputKeepBytes = 64 * 1024;

/// 心跳和超时都靠这个定时器，间隔固定 1 秒。
constexpr int kInstallerTickMs = 1000;

/// 把安装器的输出切成行（两个通道混在一起，所以 `\r` 和 `\n` 都当分隔符 ——
/// 安装器里那些 "Patching xxx 1/1" 是靠 `\r` 原地刷的）。
inline QStringList installerOutputLines(const QByteArray &output) {
    return QString::fromLocal8Bit(output).split(
        QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
}

/// 安装器输出的**最后几行**（失败时贴给用户看，是"卡在哪"的唯一线索）。
///
/// 上限是刻意的：装到一半失败时它可能已经打了几千行 "Patching ... 1/1"，全贴出来
/// 只会把真正的那一行淹掉。最后几行才是有信息的那几行。
inline QString installerOutputTail(const QByteArray &output, int maxLines = 12) {
    const QStringList lines = installerOutputLines(output);
    const int from = qMax(0, lines.size() - maxLines);
    QStringList tail;
    for (int i = from; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (!line.isEmpty()) {
            tail.append(line);
        }
    }
    return tail.join(QLatin1Char('\n'));
}

/// 直接从盘上读一份版本 json，**不**经过 versionJsons 那份内存缓存。
///
/// NOTE: 安装完之后的核对必须绕开缓存。装 Forge/NeoForge 的版本往往**本来就是一个
/// 已经存在的版本**（用户重装、或者在一个装过 Forge 的版本上换加载器版本），
/// 那这个 id 早就被读进缓存了 —— 走缓存就等于拿**安装之前**那份 json 去核对
/// 安装之后的产物，核对永远能过，白核。
inline QJsonObject readVersionJsonAt(const QString &path) {
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Failed to parse" << path << ":" << parseError.errorString();
        return QJsonObject();
    }
    return doc.object();
}

/// 把 from 改名成 to。from 本来就不在时**当成功**（可选文件：json、meta 都可能没有）。
///
/// NOTE: 目标已存在时**拒绝**，不直接交给 QFile::rename。
///
/// Qt 6.10 里 `QFile::rename` → `QFileSystemEngine::renameFile()` → `::MoveFile`
/// （**不带** REPLACE_EXISTING，qfilesystemengine_win.cpp:1761-1770），目标已存在时
/// 就是失败；带 REPLACE_EXISTING 的是**另一个私有函数**
/// （`renameOverwriteFile()`，同文件 1778-1790，`QSaveFile::commit()` 走的才是它）。
/// 所以**要覆盖别人，只能自己先把它挪开/删掉，不能指望 rename。**
/// 这里的判据是显式拒绝，让"目标已存在"在这一层被说出来，而不是靠 rename 的
/// 副作用。唯一的例外是只差大小写：那种"目标已存在"其实是同一个文件（Windows 的
/// 路径比较本来就不区分大小写），而改显示大小写是合法操作。
inline bool renameOnto(const QString &from, const QString &to) {
    if (!QFile::exists(from)) {
        return true;
    }
    if (from.compare(to, Qt::CaseInsensitive) != 0 && QFile::exists(to)) {
        return false;
    }
    return QFile::rename(from, to);
}

/// 把 @p from **覆盖着**拷到 @p to：先写同目录临时文件、`commit()` 时原子改名。
///
/// 不能写成 `remove(to)` + `copy(from, to)`：两步之间进程被杀掉，用户手上这一份就
/// **没了** —— 而调用方的源目录随后会被删掉（`copyTreeInto` 是"搬回来"），没有第二次
/// 机会。QSaveFile 的 commit 走的是 `renameOverwriteFile()`（带 REPLACE_EXISTING，见
/// 上面 `renameOnto()` 那段注释），目标要么还是旧的那份、要么已经是新的整份。
inline bool copyFileReplacing(const QString &from, const QString &to) {
    QFile input(from);
    if (!input.open(QIODevice::ReadOnly)) {
        return false;
    }
    QSaveFile output(to);
    if (!output.open(QIODevice::WriteOnly)) {
        return false;
    }
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(1024 * 1024);
        if (chunk.isEmpty()) {
            if (input.error() != QFile::NoError) {
                output.cancelWriting();
                return false;
            }
            continue;
        }
        if (output.write(chunk) != chunk.size()) {
            output.cancelWriting();
            return false;
        }
    }
    if (input.error() != QFile::NoError || !output.commit()) {
        return false;
    }
    // 和 `QFile::copy()` 一样把权限带过去（它自己也会拷权限）；失败不影响内容已经落好。
    QFile::setPermissions(to, QFileInfo(from).permissions());
    return true;
}

/// 把 from 这棵树**覆盖着**拷进 to（to 不存在就建目录）。任何一步失败返回 false。
///
/// 三条规矩都是为"搬进用户真实的游戏目录"这一件事定的：
///
/// * **只覆盖同名文件，绝不删 to 底下任何东西。** to 里可能有用户自己放的东西，也可能
/// 有别的版本/加载器的库 —— `<游戏目录>/libraries/` 就是全体版本共用的一份。所以这个
/// 函数里没有一个 removeRecursively，删的只能是它马上要盖掉的那一个文件。
/// * 目标已存在时**原地覆盖**（QSaveFile 的 commit 带 REPLACE_EXISTING）：重装同一个
/// 版本正是"目标已存在"的常态，而先删再拷的话，删成功、拷失败（盘满 / 被杀掉）就让
/// 用户**少了一个文件**。删不掉（被别的进程占着）同样当失败报出去，不装作拷过了。
/// * 失败如实返回 false，由调用方报"没搬回来"。半途而废的复制**会被留下**（比如版本
/// 目录搬了一半）—— 那种情况下调用方的话里要说清"再装一次能盖好"，而不是说装好了。
///
/// NOTE: 空目录不会在 to 里被建出来（不拷文件就没有 mkpath）。这里用不上：搬的两样东西
/// ——版本目录和 libraries/optifine/ —— 底下的文件一个都不少。
inline bool copyTreeInto(const QString &from, const QString &to) {
    const QFileInfo source(from);
    if (source.isFile()) {
        const QString parent = QFileInfo(to).absolutePath();
        if (!QDir().mkpath(parent)) {
            qWarning() << "Cannot create directory" << parent;
            return false;
        }
        if (!copyFileReplacing(from, to)) {
            qWarning() << "Cannot copy" << from << "to" << to;
            return false;
        }
        return true;
    }
    if (source.isDir()) {
        // 带上 QDir::Hidden：这是一条**搬回来**的路（加载器装失败之后
        // 把版本目录 / libraries/optifine 还原回去），漏掉的条目**没有第二次机会** ——
        // 源目录随后就被删了。不带这一位时，盘上凡是带**隐藏属性位**的东西都会在"还原"
        // 这一步**静默消失**：用户看到的是"装了一次加载器，我放在版本目录里的东西少了
        // 几样"。删得掉的东西，拷贝时更要一个不落。
        // NOTE: Windows 侧的 HiddenAttribute 只由 `FILE_ATTRIBUTE_HIDDEN` 置起来
        // （`qfilesystemmetadata_p.h` 的 fillFromFileAttribute）；点开头的名字在 Windows 上
        // 算不算隐藏要看那个属性位，光看名字不算。
        const QFileInfoList entries = QDir(from).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            if (!copyTreeInto(entry.absoluteFilePath(),
                              to + QLatin1Char('/') + entry.fileName())) {
                return false;
            }
        }
    }
    return true;
}

/// NOTE: `copyTreeWithoutFollowingLinks()`（"把 from 原样拷成还不存在的 to"）在
/// `src/manage/FolderManager.cpp`：复制实例和导入世界这两条路都要"造一份新的出来"，
/// 所以它是公用函数而不是本文件的局部函数（两处各抄一份的话，下面这三条规矩里任何一条
/// 只改了一处，另一处**不会报错**、只会静默地按另一套规矩做）。
///
/// 两个函数的规矩**条条相反**，因为要办的事相反：copyTreeInto 是"把东西搬回用户真实
/// 的游戏目录"（目标常态是已经有一份，要的是覆盖），那一个是"多出一份"（目标必须是
/// 一片空白，要的是**绝不**碰已有东西）：to 已存在就失败而不是合并、带
/// `QDir::Hidden | QDir::System`、空目录也建出来。改其中任何一个之前，**先去读另一个**。

/// 挑中某个加载器版本之后，日志里那句括号里的说明（"这是怎么挑出来的"）。
///
/// 为什么要分开说这三句而不是一句"挑中 X"：用户手上那一格可能是**最新档**（连 alpha/beta
/// 一起算）也可能是**稳定档**，两档挑出来的版本号不一样，日志里不写清楚就分不出"它给我挑的
/// 是哪个档"——装在盘上的东西可是不一样的（`versions/<id>/wtfml.meta` 里的 loaderVersion）。
///
/// @param stable 上游自己标着"正式版/推荐"的那一个吗（Fabric 是接口的 stable 字段、
/// Forge 是 promotions 的 recommended、NeoForge 是没有 `-beta` 后缀、OptiFine 是
/// 官方页那一行）。**稳定档也可能挑出一条 stable=false 的**：上游这份清单里一条正式版
/// 都没有时，那两档都会退回"最新的那条"（界面上那一行也是灰的，见
/// MainWindow::fillLoaderVersionItems），这时候话要说成"退回去了"。
inline QString pickChannelDetail(LoaderMeta::LoaderChannel channel, bool stable) {
    if (channel == LoaderMeta::LoaderChannel::Newest) {
        return QObject::tr("最新那一档，连测试版一起算");
    }
    if (stable) {
        return QObject::tr("稳定那一档，上游自己标着正式版的那个");
    }
    return QObject::tr("稳定那一档，可上游一条正式版都没有，退回了最新的那个");
}

/// CurseForge 那台接口没成时给用户的那条**出路**。
///
/// 镜像是别人搭的一台服务：它限流、哪天停掉，都不是用户能修的，所以这句话必须点出
/// 「填一份自己的 key 就改走官方」。**两处用户**（导入那条路的 resolveCurseForgeFiles、
/// 导出那条路的 lookupCurseForgeFingerprints）—— 各写一份的话，改了一处另一处就变成
/// 一句把用户引到死路上的话。
inline QString curseForgeKeyHint(bool hasKey) {
    if (hasKey) {
        return QObject::tr("设置里那份 CurseForge API key 可能不对、或者已经失效了，可以换一份。");
    }
    return QObject::tr("网络本身没问题的话，多半是那台公共接口的事了 —— 可以在设置页「下载」"
                       "栏里填一份自己的 CurseForge API key（免费申请），填了就改走官方接口。"
                       "现在这一发走的是别人搭的一台社区镜像，它可能限流、也可能哪天就停了。");
}

/// 实例 `mods/` 下的 jar **文件名**（不含路径）。首启资源预取认消费者只看这一层：
/// 读 jar 里的 `mcmod.info` 要为一次检查解几百个包，不值得。
inline QStringList modJarNames(const QString &versionId) {
    QStringList names;
    const QDir modsDir(AppPaths::versionDir(versionId) + QStringLiteral("/mods"));
    if (!modsDir.exists()) {
        return names;
    }
    for (const QFileInfo &info : modsDir.entryInfoList(QDir::Files)) {
        if (info.suffix().compare(QStringLiteral("jar"), Qt::CaseInsensitive) == 0) {
            names.append(info.fileName());
        }
    }
    return names;
}

}  // namespace VersionManagerInternals
