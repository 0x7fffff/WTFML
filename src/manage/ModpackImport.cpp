/**
 * @file ModpackImport.cpp
 * @brief ModpackImport 的实现。判据的来龙去脉写在 .hpp 里；这里只有实现。
 * @author 0x7fffff
 * @date 2026-09-22
 */
#include "ModpackImport.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include "InstanceFiles.hpp"

namespace ModpackImport {

QString modrinthIndexName() { return QStringLiteral("modrinth.index.json"); }

QString curseForgeManifestName() { return QStringLiteral("manifest.json"); }

QString fabricLoaderKey() { return QStringLiteral("fabric-loader"); }

QString forgeLoaderKey() { return QStringLiteral("forge"); }

QString neoforgeLoaderKey() { return QStringLiteral("neoforge"); }

QString quiltLoaderKey() { return QStringLiteral("quilt-loader"); }

QStringList loaderDependencyKeys() {
    // 顺序 = 认的顺序。一个包里只会声明其中一个，但表本身得有确定的次序：靠遍历
    // QJsonObject 的键来挑是"实现细节当规则用"，换一次 Qt 就可能挑到另一个。
    return {fabricLoaderKey(), forgeLoaderKey(), neoforgeLoaderKey(), quiltLoaderKey()};
}

QString unsupportedLoaderText(const QString &loaderKey, const QString &loaderVersion) {
    if (loaderKey.isEmpty()) {
        // 纯原版的整合包（依赖表里只有 minecraft）：能装，只是没有加载器要塞。
        return QString();
    }
    if (loaderKey == fabricLoaderKey() || loaderKey == forgeLoaderKey() ||
        loaderKey == neoforgeLoaderKey()) {
        return QString();
    }

    const QString version =
        loaderVersion.isEmpty() ? QString() : QStringLiteral(" %1").arg(loaderVersion);
    if (loaderKey == quiltLoaderKey()) {
        return QObject::tr(
                   "这个整合包要 Quilt%1，而这个启动器现在能装的是 Fabric / Forge / "
                   "NeoForge / OptiFine，没有 Quilt。\n\n"
                   "所以**没有**导入 —— 装出一个进不去的整合包比不装更糟。")
            .arg(version);
    }
    return QObject::tr(
               "这个整合包要「%1」%2，这个启动器不认识这个模组加载器。\n\n"
               "能装的是 Fabric / Forge / NeoForge / OptiFine。所以没有导入。")
        .arg(loaderKey, version);
}

QString unsupportedFormatText(Format format) {
    if (format == Format::CurseForge) {
        return QObject::tr(
            "这是 CurseForge 格式的整合包（压缩包根目录有一份 manifest.json）。\n\n"
            "CurseForge 的清单里**没有文件的下载地址**，得先去问它的接口才换得出来 —— "
            "这个启动器还没做那一步。现在支持的是 Modrinth 格式（根目录有一份 "
            "modrinth.index.json 的那种）。");
    }
    return QObject::tr(
        "这个压缩包里没有整合包清单，认不出它是什么格式。\n\n"
        "Modrinth 格式的整合包，根目录有一份 modrinth.index.json；CurseForge 格式的，"
        "有一份 manifest.json。两个都没有的话，它多半只是一个普通的压缩包"
        "（比如别人打包好的 .minecraft 文件夹）。");
}

bool isSafeRelativePath(const QString &path) {
    if (path.isEmpty()) {
        return false;
    }
    // 整条路径的长度上限（Windows 的 MAX_PATH 是 260，减去实例目录本身那一段留的余量）。
    // NOTE: 这个数**不是** kMaxDirNameLength —— 那个是实例名的规矩，路径段是另一回事。
    if (path.length() > 240) {
        return false;
    }
    // 反斜杠一律拒绝。Modrinth 的清单里用 `/`，出现 `\` 只可能是想借 Windows 的分隔符
    // 绕过逐段判据：`mods\..\..\x.jar` 按 `/` 切开是**一整段**，逐段看起来完全正常。
    if (path.contains(QLatin1Char('\\'))) {
        return false;
    }
    // 绝对路径（`/etc/...`）。盘符（`C:...`）不用单独判：冒号本来就在非法字符里。
    if (path.startsWith(QLatin1Char('/'))) {
        return false;
    }

    const QStringList segments = path.split(QLatin1Char('/'));
    for (const QString &segment : segments) {
        // 逐段的规矩和"实例名"共用一份（含空段 —— `a//b` 会被切出一个空段）。
        if (!InstanceFiles::validatePathSegment(segment).isEmpty()) {
            return false;
        }
    }
    return true;
}

namespace {

/// 这一条地址是不是 Modrinth 自己的 CDN。
/// 判 host、不判整串：清单里同一个文件的 modrinth 地址带着 `%2B` / `%2b` 两种大小写
/// （见那两个真包），拿字符串比很容易在某个包上悄悄失效。
bool isModrinthCdnUrl(const QString &url) {
    return QUrl(url).host() == QLatin1String("cdn.modrinth.com");
}

/// 清单里那条路径的**去重键**。
///
/// 为什么不能直接拿路径当键（2026-09-24 加的）：Windows 的文件系统**不区分大小写**，
/// `mods/X.jar` 和 `mods/x.jar` 是同一个文件，而按区分大小写的键去重会给它们排出两个
/// 作业 —— 两个作业抢着写同一个路径（内容一样就白下一遍，不一样就"谁最后写完谁说了算"）。
/// 别的平台上这两个确实是两个文件，照旧各算一条，所以这一手只在 Windows 上做。
///
/// NOTE: toLower() 不是 Windows 那套精确的大小写折叠（它认的是 UpCaseTable），但对整合包
///       里这种 ASCII 路径足够了 —— 目标是"认得出这是同一个文件"，不是逐字符等价。
QString pathDedupKey(const QString &path) {
#ifdef Q_OS_WIN
    return path.toLower();
#else
    return path;
#endif
}

}  // namespace

QStringList preferredUrlOrder(const QStringList &urls) {
    QStringList ordered;
    ordered.reserve(urls.size());
    for (const QString &url : urls) {
        if (isModrinthCdnUrl(url)) {
            ordered << url;
        }
    }
    // 一条都不匹配时，下面这个循环会把整份**原样**抄一遍 —— 次序不动，和没调过一样。
    for (const QString &url : urls) {
        if (!isModrinthCdnUrl(url)) {
            ordered << url;
        }
    }
    return ordered;
}

QString suggestInstanceName(const QString &packName) {
    QString name = packName.trimmed();

    // 建不出目录的字符换成 `-`：实例名那条路是**拒绝**（报一句"名字里不能有…"），
    // 这里是把名字修好 —— 用户还没输入任何东西，不该先撞一句错误。
    static const QString kIllegal = QStringLiteral("<>:\"/\\|?*");
    for (int i = 0; i < name.size(); ++i) {
        const QChar ch = name.at(i);
        if (ch.unicode() < 0x20 || kIllegal.contains(ch)) {
            name[i] = QLatin1Char('-');
        }
    }

    // 结尾的点和空格：Windows 会静默吃掉（见 InstanceFiles::validatePathSegment 里那段）。
    // 整合包的名字里出现结尾的点是常事（`xxx.1.8.4` 少见，但 `xxx.` 见过），所以这一步
    // 不是多余的。
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
        name.chop(1);
    }
    name = name.trimmed();

    if (name.length() > InstanceFiles::kMaxDirNameLength) {
        name = name.left(InstanceFiles::kMaxDirNameLength).trimmed();
        while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
            name.chop(1);
        }
    }

    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) {
        return QObject::tr("整合包");
    }

    // 剩下的只有"Windows 保留设备名"这一类（`CON`、`COM1`……，见 validatePathSegment）。
    // 那种名字建不出目录来，而这里又不能报错（用户什么都没输），所以换个说法绕开它。
    // NOTE: 判据用**同一份** validateDirName()，不自己抄一遍保留名清单。
    if (!InstanceFiles::validateDirName(name).isEmpty()) {
        name = QObject::tr("整合包 %1").arg(name);
        if (name.length() > InstanceFiles::kMaxDirNameLength) {
            name = name.left(InstanceFiles::kMaxDirNameLength).trimmed();
            // 截完**还要再砍一次结尾的点**（2026-09-24 补的）：上面那处截断带着这一段，
            // 这一处原来漏了 —— 截出来的名字可能以点结尾，那就过不了 validateDirName()，
            // 于是这个函数返回一个"它自己刚拒过一次"的名字。而这里又不许报错（用户什么都没
            // 输），调用方拿它去建目录，Windows 会悄悄把结尾的点吃掉 = 名字跟返回值不一样。
            while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
                name.chop(1);
            }
        }
    }
    return name;
}

Result parseModrinthIndex(const QByteArray &json) {
    Result result;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.problem = QObject::tr("整合包清单读不懂（%1）。这个文件可能坏了。")
                             .arg(parseError.errorString());
        return result;
    }

    const QJsonObject root = doc.object();

    // 头两条：这份 json 得**看起来就是** modrinth.index.json。`game` 和 `formatVersion`
    // 是 Mojang 无关的格式标记，缺了它们说明拿到的不是清单（比如是个空的同名文件）。
    if (!root.value(QStringLiteral("formatVersion")).isDouble() ||
        !root.value(QStringLiteral("files")).isArray()) {
        result.problem = QObject::tr(
            "压缩包里那份 %1 不是整合包清单（缺少 formatVersion 或者 files）。")
                             .arg(modrinthIndexName());
        return result;
    }

    const QString game = root.value(QStringLiteral("game")).toString();
    if (game != QLatin1String("minecraft")) {
        result.problem =
            game.isEmpty()
                ? QObject::tr("这份 %1 里没写它是哪个游戏的整合包，装不了。")
                      .arg(modrinthIndexName())
                : QObject::tr("这不是 Minecraft 的整合包：清单里写的是「%1」。").arg(game);
        return result;
    }

    // 基于哪个原版。没有这一条就什么也装不了（连"装个纯原版"都不知道装哪个）。
    const QJsonObject dependencies = root.value(QStringLiteral("dependencies")).toObject();
    const QString gameVersion = dependencies.value(QStringLiteral("minecraft")).toString();
    if (gameVersion.isEmpty()) {
        result.problem = QObject::tr(
            "这份整合包的依赖表里没写它基于哪个 Minecraft 版本（dependencies.minecraft），"
            "装不了。");
        return result;
    }

    Pack pack;
    pack.format = Format::Modrinth;
    pack.gameVersion = gameVersion;

    pack.name = root.value(QStringLiteral("name")).toString().trimmed();
    if (pack.name.isEmpty()) {
        // 名字只是"起实例名时的候选"，没有也能装 —— 退化成清单里的版本号，再退化成一句
        // 通用的。**不**因为缺它拒绝整个包。
        pack.name = root.value(QStringLiteral("versionId")).toString().trimmed();
    }
    pack.version = root.value(QStringLiteral("versionId")).toString().trimmed();

    for (const QString &key : loaderDependencyKeys()) {
        if (dependencies.contains(key)) {
            pack.loaderKey = key;
            pack.loaderVersion = dependencies.value(key).toString();
            break;
        }
    }
    // 认不出的加载器键**也得带出去**，不能就这么丢掉。
    // NOTE: 上面那个循环只在那四个键里挑，于是 dependencies 里写着别的加载器
    //       （rift-loader / liteloader / 以后新出的……）时 loaderKey 是空串 —— 而空串在
    //       unsupportedLoaderText() 眼里是"纯原版整合包，放行"，最后装出来一个**进不去**
    //       的包：一堆 mod 躺在 mods/ 里，游戏按原版起。.hpp 里写着"认不出来的键走和
    //       Quilt 同一条路、不返回空串"，当时就是被这一步悄悄绕过去的。
    //       多个认不出的键时取**排序后的第一个**：这种包本身就不合规，报哪个都算报，
    //       但得报确定的那一个 —— 靠 QJsonObject 的遍历顺序挑，换一次 Qt 就可能换一个。
    if (pack.loaderKey.isEmpty()) {
        QStringList unknownKeys;
        for (auto it = dependencies.constBegin(); it != dependencies.constEnd(); ++it) {
            if (it.key() != QLatin1String("minecraft")) {
                unknownKeys.append(it.key());
            }
        }
        unknownKeys.sort();
        if (!unknownKeys.isEmpty()) {
            pack.loaderKey = unknownKeys.first();
            pack.loaderVersion = dependencies.value(pack.loaderKey).toString();
        }
    }

    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    QHash<QString, int> indexesByPath;  // 路径 -> 它在 files 里的下标（去重用）
    for (const QJsonValue &value : files) {
        if (!value.isObject()) {
            continue;  // 不是对象的条目当没有 —— 它连路径都没法说，报也报不清
        }
        const QJsonObject entry = value.toObject();

        const QString path = entry.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            continue;
        }
        // **路径不安全 = 整包拒绝**（不是跳过这一条）。理由写在 .hpp 里。
        if (!isSafeRelativePath(path)) {
            result.problem =
                QObject::tr("这个整合包的文件清单里有一条不能用的路径：\n%1\n\n"
                            "（路径不能跑到实例目录外面去，也不允许某一段以点或空格结尾 —— "
                            "Windows 会把那些字符悄悄吃掉，写出来的名字和清单里写的不一样。）"
                            "所以整个包都没有导入。")
                    .arg(path);
            return result;
        }

        // `env.client` 写着 unsupported = 这个 mod 只给服务端用。装到客户端上轻则没用，
        // 重则一进游戏就崩，所以跳过它，但**记一笔**（装在哪儿、少了什么要说清楚）。
        const QString clientEnv =
            entry.value(QStringLiteral("env")).toObject().value(QStringLiteral("client")).toString();
        if (clientEnv == QLatin1String("unsupported")) {
            ++pack.filesSkipped;
            continue;
        }

        PackFile file;
        file.path = path;
        const QJsonArray downloads = entry.value(QStringLiteral("downloads")).toArray();
        for (const QJsonValue &url : downloads) {
            const QString text = url.toString();
            if (!text.isEmpty()) {
                file.urls.append(text);
            }
        }
        file.sha1 = entry.value(QStringLiteral("hashes"))
                        .toObject()
                        .value(QStringLiteral("sha1"))
                        .toString();
        const QJsonValue size = entry.value(QStringLiteral("fileSize"));
        file.size = size.isDouble() ? static_cast<qint64>(size.toDouble()) : -1;

        if (file.urls.isEmpty()) {
            // 没有下载地址。Modrinth 的规矩是这种文件由压缩包自己带（overrides 里），
            // 所以不是错误，但它**一定**要说出来 —— 万一它并不在 overrides 里，用户得知道
            // 少了什么，而不是开游戏才发现缺 mod。
            ++pack.filesSkipped;
            continue;
        }

        // 同一个路径出现两次时以**最后**一条为准。合法的清单里不会有重复（Modrinth 自己
        // 生成的那份就没有），这里只是别让两条下载写同一个文件。
        // 键要走 pathDedupKey()：Windows 上大小写不同的两条是同一个文件，见那个函数。
        const QString dedupKey = pathDedupKey(path);
        if (indexesByPath.contains(dedupKey)) {
            pack.files[indexesByPath.value(dedupKey)] = file;
            continue;
        }
        indexesByPath.insert(dedupKey, pack.files.size());
        pack.files.append(file);
    }

    if (pack.files.isEmpty() && pack.filesSkipped == 0) {
        // 清单里一条文件都没有。这**不是**错误（理论上可以有一个纯 overrides 的整合包），
        // 但那种包里不该有 files 字段。到这里多半是清单被改坏了，如实说一句，让用户
        // 自己去核对，而不是"装完了，什么都没有"。
        result.problem = QObject::tr(
            "这份整合包的文件清单是空的（一条要下载的文件都没有），没有导入。");
        return result;
    }

    result.ok = true;
    result.pack = pack;
    return result;
}

}  // namespace ModpackImport
