/**
 * @file ModpackImport.cpp
 * @brief ModpackImport 的实现。判据的来龙去脉写在 .hpp 里；这里只有实现。
 * @note 两种整合包：Modrinth（`modrinth.index.json`，清单里带地址）与
 *       CurseForge（`manifest.json`，**清单里只有号**，要先拿去接口换文件）。
 *       后者在这里分两段：`parseCurseForgeManifest` 读号，`parseCurseForgeFileInfo`
 *       读接口的答复 —— 中间那一发网络请求在 VersionManager 里（见 resolveCurseForgeFiles）。
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
#include <QSet>
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
    // 两句话都得说清"哪一条路认得什么"（2026-09-25 修）：原来这两句都写着"能装的是
    // Fabric / Forge / NeoForge / OptiFine"，那说的是**安装页**那条路（那边四个加载器
    // 都能装，见 VersionManager::loaderInstallSupported）。可这里判的是**整合包导入**
    // 这条路，它只认三个加载器键（fabric-loader / forge / neoforge）—— 于是
    // `dependencies` 里写着 "optifine" 的包会被这里拒绝，而拒绝的话里却说 OptiFine 能装：
    // 用户按那句话去安装页一看确实能装，就会以为"这个包应该也行，是启动器抽了"。
    if (loaderKey == quiltLoaderKey()) {
        return QObject::tr(
                   "这个整合包要 Quilt%1，而整合包导入这条路只认得 fabric-loader / forge / "
                   "neoforge 三个加载器键，没有 Quilt。\n\n"
                   "所以**没有**导入 —— 装出一个进不去的整合包比不装更糟。")
            .arg(version);
    }
    return QObject::tr(
               "这个整合包要「%1」%2，而整合包导入这条路只认得 fabric-loader / forge / "
               "neoforge 三个加载器键。\n\n"
               "所以没有导入。（安装页里能直接装 Fabric / Forge / NeoForge / OptiFine "
               "四个加载器，那是另一条路 —— OptiFine 在整合包的依赖表里没有对应的键，"
               "所以这样写的包不会按 OptiFine 装上。）")
        .arg(loaderKey, version);
}

QString unsupportedFormatText(Format format) {
    // NOTE: 参数现在**用不上**了（CurseForge 那一支已经能装，两条路都不该走到这个函数）。
    //       留着形状是为了调用点不用改，也留着"将来多一种格式时往哪儿加"的位置。
    Q_UNUSED(format);
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
        // fileSize 越界（或者干脆不是数）= **当"不知道字节数"**（-1），不强转。
        // NOTE: 这一步原来是裸的 `static_cast<qint64>(size.toDouble())`，而越界的 double 转
        //       qint64 是**未定义行为**（x86 上给出 0x8000…，一个负数）。清单是外部输入，
        //       写个 1e30 就能撞上，而那个负数会一路走进"这份文件下完了没有"的判断里 ——
        //       变成一个永远下不完的作业。0 也留着（0 字节的文件确实存在，但那份字节数由
        //       sha1 那一侧兜），这里只挡"明显不是字节数"的。
        // 2^63 这个字面量在 double 里表示得**正好**，`<` 它就一定塞得进 qint64。
        constexpr double kQint64UpperBound = 9223372036854775808.0;
        const QJsonValue size = entry.value(QStringLiteral("fileSize"));
        file.size = -1;
        if (size.isDouble() && size.toDouble() >= 0.0 && size.toDouble() < kQint64UpperBound) {
            file.size = static_cast<qint64>(size.toDouble());
        }

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

// ---------------------------------------------------------------------------
// CurseForge
// ---------------------------------------------------------------------------

namespace {

/// `modLoaders[].id` 拆开之后的形状（喂给 curseForgeLoaderKey 那一对出参）。
struct CurseForgeLoaderId {
    bool known = false;
    QString key;
    QString version;
};

CurseForgeLoaderId splitCurseForgeLoaderId(const QString &rawId) {
    CurseForgeLoaderId out;
    const QString id = rawId.trimmed();
    if (id.isEmpty()) {
        return out;
    }

    // 拆在**第一个** `-` 上（不是最后一个）：`neoForge-20.4.1-beta` 也得把后缀留在版本里。
    const int dash = id.indexOf(QLatin1Char('-'));
    const QString name = (dash < 0 ? id : id.left(dash)).trimmed();
    out.version = (dash < 0) ? QString() : id.mid(dash + 1).trimmed();

    // 名字那一半按小写认：真清单里三种写法都见过（`forge` / `neoForge` / `NeoForge`）。
    const QString lowered = name.toLower();
    if (lowered == QLatin1String("forge")) {
        out.key = forgeLoaderKey();
        out.known = true;
    } else if (lowered == QLatin1String("neoforge")) {
        out.key = neoforgeLoaderKey();
        out.known = true;
    } else if (lowered == QLatin1String("fabric")) {
        out.key = fabricLoaderKey();
        out.known = true;
    } else if (lowered == QLatin1String("quilt")) {
        out.key = quiltLoaderKey();
        out.known = true;
    } else {
        // 认不出来**也给 key**（原样那个前缀）：空串在下游是"纯原版包，放行"，
        // 于是 `liteloader-1.12.2` 会被当成纯原版装出来 —— 一堆 mod 躺在 mods/ 里，
        // 游戏按原版起。见 .hpp 里那一条。
        out.key = name;
    }
    return out;
}

/// `hashes` 里 `algo == 1` 的那一条（CurseForge 的 SHA-1；`2` 是 MD5）。
///
/// 找不到回空串 —— 空 sha1 在下载那一段是"不核 sha1、只核字节数"，不是错误。
/// NOTE: **不能拿 `hashes[0]`**：数组的次序没有承诺，而且实际回来时它常常是 md5
///       （那样下载完每一条都会被判成"校验不过"，而字节数是对的）。
QString sha1FromCurseForgeHashes(const QJsonArray &hashes) {
    for (const QJsonValue &value : hashes) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("algo")).toInt(-1) == 1) {
            return entry.value(QStringLiteral("value")).toString();
        }
    }
    return QString();
}

/// `{"fileIds":[…]}` / `{"modIds":[…]}` 那一类请求体。键名和"号是数字不是字符串"
/// 这两件事写错时接口回 400，而 400 的正文里只有一句 `{"error":"…"}` —— 那看着和
/// "这个包坏了"一模一样（见 .hpp 里为什么把请求体做成纯函数）。
QByteArray idListRequestBody(const QString &key, const QList<int> &ids) {
    QJsonArray array;
    for (const int id : ids) {
        array.append(id);
    }
    QJsonObject body;
    body[key] = array;
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

}  // namespace

bool curseForgeLoaderKey(const QString &rawId, QString *keyOut, QString *versionOut) {
    const CurseForgeLoaderId parsed = splitCurseForgeLoaderId(rawId);
    if (keyOut != nullptr) {
        *keyOut = parsed.key;
    }
    if (versionOut != nullptr) {
        *versionOut = parsed.version;
    }
    return parsed.known;
}

Result parseCurseForgeManifest(const QByteArray &json) {
    Result result;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.problem = QObject::tr("整合包清单读不懂（%1）。这个文件可能坏了。")
                             .arg(parseError.errorString());
        return result;
    }

    const QJsonObject root = doc.object();

    // 头两条：这份 json 得**看起来就是** manifest.json。
    // `manifestVersion` + `files` 是那份清单的骨架，缺了它们说明拿到的不是清单。
    if (!root.value(QStringLiteral("manifestVersion")).isDouble() ||
        !root.value(QStringLiteral("files")).isArray()) {
        result.problem = QObject::tr(
            "压缩包里那份 %1 不是整合包清单（缺少 manifestVersion 或者 files）。")
                             .arg(curseForgeManifestName());
        return result;
    }

    // `manifestType` 只在**写着别的**时才拒。CurseForge 自己生成的包一定写着
    // "minecraftModpack"，但手改过的、以及一些很老的包缺这一条 —— 缺它不是
    // "这不是整合包"的证据（上面那两条骨架才是），为它拒一个能装的包不值得。
    const QString manifestType = root.value(QStringLiteral("manifestType")).toString();
    if (!manifestType.isEmpty() && manifestType != QLatin1String("minecraftModpack")) {
        result.problem =
            QObject::tr("这不是 Minecraft 的整合包：清单里写的类型是「%1」。").arg(manifestType);
        return result;
    }

    // 基于哪个原版。没有这一条就什么也装不了（连"装个纯原版"都不知道装哪个）。
    const QJsonObject minecraft = root.value(QStringLiteral("minecraft")).toObject();
    const QString gameVersion = minecraft.value(QStringLiteral("version")).toString().trimmed();
    if (gameVersion.isEmpty()) {
        result.problem = QObject::tr(
            "这份整合包的清单里没写它基于哪个 Minecraft 版本（minecraft.version），装不了。");
        return result;
    }

    Pack pack;
    pack.format = Format::CurseForge;
    pack.gameVersion = gameVersion;

    pack.name = root.value(QStringLiteral("name")).toString().trimmed();
    if (pack.name.isEmpty()) {
        // 名字只是"起实例名时的候选"，缺了不拒整个包（和 Modrinth 那一条同一个取舍）。
        pack.name = root.value(QStringLiteral("version")).toString().trimmed();
    }
    pack.version = root.value(QStringLiteral("version")).toString().trimmed();

    // 加载器：`modLoaders` 里挑 `primary` 那一条。CurseForge 允许多条（真有包写了不止一条），
    // 挑不出来时取**第一条**；一条都没有 = 纯原版整合包，不是错误。
    const QJsonArray loaders = minecraft.value(QStringLiteral("modLoaders")).toArray();
    QString loaderRaw;
    for (const QJsonValue &value : loaders) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) {
            continue;
        }
        if (entry.value(QStringLiteral("primary")).toBool()) {
            loaderRaw = id;
            break;  // primary 优先，且它就该是唯一的那条
        }
        if (loaderRaw.isEmpty()) {
            loaderRaw = id;
        }
    }
    if (!loaderRaw.isEmpty()) {
        curseForgeLoaderKey(loaderRaw, &pack.loaderKey, &pack.loaderVersion);
    }

    // 覆盖文件那一层目录名。**这是外部输入**（它会拼成一条路径前缀），所以过一遍
    // isSafeRelativePath()：不安全就退回默认的 `overrides` —— 少解几个 config 是小事，
    // 让它拼出一条能写到实例目录外面的前缀是大事。
    const QString overrides = root.value(QStringLiteral("overrides")).toString().trimmed();
    if (!overrides.isEmpty() && isSafeRelativePath(overrides)) {
        pack.overridesDir = overrides;
    }
    // CurseForge 没有 `client-overrides` 这个规矩（空串 = 不解这一层，见 extractModpackOverrides）。
    pack.clientOverridesDir.clear();

    // 文件清单。**这里一个地址都没有** —— 只有两个号，全都要去接口换（见 .hpp 顶上那段）。
    QSet<int> seenFileIds;
    for (const QJsonValue &value : root.value(QStringLiteral("files")).toArray()) {
        if (!value.isObject()) {
            continue;  // 不是对象的条目当没有：它连号都给不出，报也报不清
        }
        const QJsonObject entry = value.toObject();
        const QJsonValue projectId = entry.value(QStringLiteral("projectID"));
        const QJsonValue fileId = entry.value(QStringLiteral("fileID"));
        if (!projectId.isDouble() || !fileId.isDouble()) {
            continue;
        }
        PackRef ref;
        ref.projectId = projectId.toInt();
        ref.fileId = fileId.toInt();
        if (ref.projectId <= 0 || ref.fileId <= 0) {
            continue;
        }
        // 同一个文件列两遍 = 同一个文件下两遍（两条作业写同一个路径）。真清单里不会有，
        // 但这一手很便宜，而踩上的症状是"两个线程抢着写同一个 jar"。
        if (seenFileIds.contains(ref.fileId)) {
            continue;
        }
        seenFileIds.insert(ref.fileId);
        // `required: false` 的文件**照样下**：主流启动器都这么做，而"这个包作者认为可选的
        // 那个 mod"对用户来说通常正是包的一部分（少装它进游戏就报错）。
        pack.pendingRefs.append(ref);
    }

    if (pack.pendingRefs.isEmpty()) {
        // 清单在，但一条文件都没有。CF 的包**一定**有 files（连纯 overrides 的包也得写个
        // 空数组？没有 —— 那种包根本生成不出来），所以到这里多半是清单被改坏了。
        result.problem = QObject::tr(
            "这份整合包的文件清单是空的（一条要下载的文件都没有），没有导入。");
        return result;
    }

    result.ok = true;
    result.pack = pack;
    return result;
}

QByteArray curseForgeFilesRequestBody(const QList<PackRef> &refs) {
    QList<int> ids;
    ids.reserve(refs.size());
    for (const PackRef &ref : refs) {
        ids.append(ref.fileId);
    }
    return idListRequestBody(QStringLiteral("fileIds"), ids);
}

QByteArray curseForgeModsRequestBody(const QList<int> &projectIds) {
    return idListRequestBody(QStringLiteral("modIds"), projectIds);
}

CurseForgeFiles parseCurseForgeFileInfo(const QByteArray &json, const QList<PackRef> &asked) {
    CurseForgeFiles out;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        out.problem =
            QObject::tr("CurseForge 的接口回了一句读不懂的话（%1）。").arg(parseError.errorString());
        return out;
    }

    const QJsonValue data = doc.object().value(QStringLiteral("data"));
    if (!data.isArray()) {
        // 出错时那边回的是 `{"error":"…"}`（没有 data）—— 这一档和 400 是同一个形状，
        // 得让用户看见接口说了什么，而不是我们猜。
        const QString error =
            doc.object().value(QStringLiteral("error")).toString().trimmed();
        out.problem = error.isEmpty()
                          ? QObject::tr("CurseForge 的接口回的话里没有文件表。")
                          : QObject::tr("CurseForge 的接口拒绝了这一问：%1").arg(error);
        return out;
    }

    // 正文里的条目先按 fileId 归档。**不能按位置对上**：批量回来的次序没有承诺，
    // 而且查不到的号根本不会回来 —— 按位置取会静默错位，把 A 的文件名安到 B 上
    // （症状是"装完有几个 mod 是错的"，比少装可怕得多）。
    QHash<int, QJsonObject> byFileId;
    for (const QJsonValue &value : data.toArray()) {
        const QJsonObject entry = value.toObject();
        const int id = entry.value(QStringLiteral("id")).toInt(0);
        if (id > 0) {
            byFileId.insert(id, entry);
        }
    }

    QList<int> projectIds;
    QSet<int> seenProjects;
    for (const PackRef &ref : asked) {
        const auto it = byFileId.constFind(ref.fileId);
        if (it == byFileId.constEnd()) {
            ++out.missing;  // 接口里没有这一条：号写错了 / 已删 / 没过审
            continue;
        }
        const QJsonObject entry = it.value();

        // 文件名要拼进路径（`mods/<名字>`），所以**先过安全性**：带 `/`、`\`、`..` 的
        // 当场算"查不到"（记 missing），绝不放行到写盘那一步。名字不安全的文件
        // **整个包不拒绝** —— 和 Modrinth 那份清单里写死路径的情形不同：那边路径是
        // 作者写在清单里的，这边是一个我们控制不了的文件名。
        const QString fileName = entry.value(QStringLiteral("fileName")).toString();
        if (fileName.isEmpty() || !isSafeRelativePath(fileName)) {
            ++out.missing;
            continue;
        }

        // 作者关了第三方分发：接口给的就是一条 null。**不代下**（理由写在 .hpp 里那条
        // NOTE：CDN 上确实躺着，但"技术上能下"不是"该下"）。
        const QJsonValue url = entry.value(QStringLiteral("downloadUrl"));
        if (!url.isString() || url.toString().isEmpty()) {
            ++out.notDistributable;
            continue;
        }

        CurseForgeFile file;
        file.file.urls.append(url.toString());
        file.file.sha1 = sha1FromCurseForgeHashes(entry.value(QStringLiteral("hashes")).toArray());
        // `fileLength` 越界（或者干脆不是数）= 当"不知道字节数"（-1）。和 Modrinth 那边
        // 同一个取舍：越界的 double 转 qint64 是未定义行为，而负数会变成一个永远下不完的作业。
        constexpr double kQint64UpperBound = 9223372036854775808.0;
        const QJsonValue size = entry.value(QStringLiteral("fileLength"));
        file.file.size = -1;
        if (size.isDouble() && size.toDouble() >= 0.0 && size.toDouble() < kQint64UpperBound) {
            file.file.size = static_cast<qint64>(size.toDouble());
        }
        // `path` 里此刻只有一个文件名 —— 目录要拿 modId 去问 classId，见 placeCurseForgeFiles()。
        file.file.path = fileName;
        file.modId = entry.value(QStringLiteral("modId")).toInt(0);
        out.resolved.append(file);

        if (file.modId > 0 && !seenProjects.contains(file.modId)) {
            seenProjects.insert(file.modId);
            projectIds.append(file.modId);
        }
    }
    out.projectIds = projectIds;
    return out;
}

QString curseForgeFilePath(int classId) {
    // 类目号来自 CurseForge 自己（`classId`，九条 `isClass=true` 里我们用得上的那几条）：
    // 6 模组 / 12 资源包 / 6552 光影 / 6945 数据包 / 17 存档 / 4546 Customization /
    // 4471 整合包。
    switch (classId) {
    case 12:    // Resource Packs
    case 4546:  // Customization（披风、皮肤那一类贴图包，本质就是资源包）
        return QStringLiteral("resourcepacks/");
    case 6552:  // Shaders
        return QStringLiteral("shaderpacks/");
    case 6945:  // Data Packs —— 和下载页那一类的落点**必须一致**（见
                // ResourceCatalog::folderForKind）：同一个东西两条路放进两个目录，
                // 症状是"从下载页下的能生效、从整合包里来的不生效"。
        // 注意原版只认存档里那份（`<存档>/datapacks/`），这里的产物要用户自己搬进存档 ——
        // 和 ResourceCatalog 那边同一句话，别在这儿说成"装好了"。
        return QStringLiteral("datapacks/");
    case 17:    // Worlds（存档）
        return QStringLiteral("saves/");
    case 4471:  // Modpacks —— 嵌套的整合包，**不代装**
        return QString();
    default:
        // 6（Mods）和"认不出来"合在一起，理由写在 .hpp 里。
        return QStringLiteral("mods/");
    }
}

CurseForgePlacement placeCurseForgeFiles(const QList<CurseForgeFile> &resolved,
                                         const QHash<int, int> &classIdByProject) {
    CurseForgePlacement out;

    QHash<QString, int> indexesByPath;  // 去重键 -> 它在 out.files 里的下标
    for (const CurseForgeFile &entry : resolved) {
        // 拿不到类目号（那一发没问到这一项）就按"认不出来"落 mods/ —— 不该让文件消失。
        const int classId = classIdByProject.value(entry.modId, 0);
        const QString dir = curseForgeFilePath(classId);
        if (dir.isEmpty()) {
            ++out.skipped;  // 嵌套的整合包
            continue;
        }

        PackFile file = entry.file;
        file.path = dir + entry.file.path;
        // 拼完**再判一次**：文件名单判过一次，但加上目录之后整条可能超长
        // （isSafeRelativePath 的长度上限是按整条路径算的）。
        if (!isSafeRelativePath(file.path)) {
            ++out.missing;
            continue;
        }

        // 同一个路径出现两次时以**最后**一条为准（和 Modrinth 那一份同一个取舍）：
        // 文件名是外部输入，两个项目给出同名文件是可能的，而两条同时写一个路径
        // 就是数据竞争。键走 pathDedupKey()（Windows 上大小写不同的两条是同一个文件）。
        const QString dedupKey = pathDedupKey(file.path);
        if (indexesByPath.contains(dedupKey)) {
            out.files[indexesByPath.value(dedupKey)] = file;
            continue;
        }
        indexesByPath.insert(dedupKey, out.files.size());
        out.files.append(file);
    }
    return out;
}

QHash<int, int> parseCurseForgeClassIds(const QByteArray &json, QString *problem) {
    const auto fail = [problem](const QString &why) {
        if (problem != nullptr) {
            *problem = why;
        }
        return QHash<int, int>();
    };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail(QObject::tr("CurseForge 的接口回了一句读不懂的话（%1）。")
                        .arg(parseError.errorString()));
    }
    const QJsonValue data = doc.object().value(QStringLiteral("data"));
    if (!data.isArray()) {
        return fail(QObject::tr("CurseForge 的接口回的话里没有项目表。"));
    }

    QHash<int, int> out;
    for (const QJsonValue &value : data.toArray()) {
        const QJsonObject entry = value.toObject();
        const int id = entry.value(QStringLiteral("id")).toInt(0);
        // `classId` 缺了就**不放进表里**（等于"认不出来"，落 mods/）——
        // 填一个 0 进去会静默落 mods/，而"表里根本没有"也是落 mods/，两件事一样，但
        // 少了这一条后面就分不出"接口说它是 0"和"接口没说"。
        if (id <= 0 || !entry.contains(QStringLiteral("classId"))) {
            continue;
        }
        out.insert(id, entry.value(QStringLiteral("classId")).toInt(0));
    }
    return out;
}

QString curseForgeApiBase(const QString &apiKey, const QString &override) {
    // 验证工程的口子（`Launcher/CurseForgeApiBase`）：产品**从不写这个键**，
    // 但填了它就得最优先 —— 那是"把接口指到本机的假服务器上"的唯一办法。
    QString base = override.trimmed();
    if (base.isEmpty()) {
        // ★ 没填 key 时走的那一台（2026-09-26 换过一次，原因是**上一台整个死了**）：
        //   · 旧的那台 `api.curse.tools` —— 2026-09-25 实测 200，2026-09-26 再测**整站 403**
        //     （Cloudflare 的拦截页，不带 UA、带浏览器 UA、带全套浏览器头都一样）。
        //     它的症状是"CurseForge 那一栏什么都搜不出来 / 整合包导入说换不到文件"，
        //     而那句话里只会看到 HTTP 403。
        //   · 现在这台 `mod.mcimirror.top/curseforge/v1` —— 实测 200，而且是**照着官方那份
        //     做的镜像**（`{"data":[…]}`、`pagination`、`downloadUrl`、`hashes`、
        //     `latestFilesIndexes` 一层不差），所以上层一个字都不用改。同一趟实测过的还有：
        //     搜索（`searchFilter=jei` 377 条）、版本列表（`/mods/{id}/files` 带 gameVersion +
        //     modLoaderType）、单文件（`/mods/{id}/files/{id}`）、两个批量 POST
        //     （`/mods/files`、`/mods`）与分类表（`/categories`）—— 这条路用到的每一发都问过。
        //   · ★ **结尾那个 `/v1` 一个字符都不能少**。这个函数的约定是"返回到 `/v1` 为止的
        //     那一段"，调用点只往后接 `/mods/search` / `/mods/files` / `/mods/{id}/files` ——
        //     少这一段就变成 `/curseforge/mods/search`，**每一发都是 404**。
        //     2026-09-26 实测：`/curseforge/mods/search?…` → 404、`/curseforge/v1/mods/search?…`
        //     → 200。它坑人的地方在于**症状和"服务挂了"长得一模一样**（CurseForge 那一栏搜不出
        //     东西、整合包导入说换不到文件），而这条路的每一发都是拿假服务器验的 ——
        //     本机那个假接口的 base 就是 `…:/v1`，所以少一段这种事**只有真网络那一跑能看见**
        //     （mw_check 的 `cf-resolve-live` 当场报 404）。ZG5 有一条断言钉着 `/v1` 这个尾巴。
        //   · **它也可能哪天就没了**（上一台就是这么没的）：换的时候只改下面这一行。
        base = apiKey.trimmed().isEmpty()
                   ? QStringLiteral("https://mod.mcimirror.top/curseforge/v1")
                   : QStringLiteral("https://api.curseforge.com/v1");
    }
    // 结尾那个 `/` 去掉：拼 URL 的地方一律自己带上它（`%1/mods/%2/files/%3`），
    // 两种写法都收下，免得用户在设置里多打或者少打一个斜杠就 404。
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return base;
}

}  // namespace ModpackImport
