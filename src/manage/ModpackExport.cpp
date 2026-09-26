/**
 * @file ModpackExport.cpp
 * @brief 见 .hpp。这里只有三样东西：murmur2 那台小状态机、两份清单的序列化、
 *        以及两张"哪些文件要问接口"的判据表。
 * @author 0x7fffff
 * @date 2026-09-26
 */

#include "ModpackExport.hpp"

#include "ModpackImport.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace ModpackExport {
namespace {

// ---------------------------------------------------------------------------
// murmur2（CurseForge 指纹那一半）
// ---------------------------------------------------------------------------

constexpr quint32 kMurmur2M = 0x5BD1E995u;

/// 那四种被剔掉的字节。**就这四种**：别的空白（0x0B 垂直制表、0x0C 换页）不剔，
/// 非 ASCII 的空白（比如全角空格）也不剔 —— 它们都是普通字节。
inline bool isStrippedByte(quint8 byte) {
    return byte == 0x09 || byte == 0x0A || byte == 0x0D || byte == 0x20;
}

/// 一个 32 位字进来之后该怎么混。顺序是 `乘 → 异或右移24 → 乘`，注意第一步在**外**面
/// 跟 h 的乘法交错（见下面 flushWord：h 先乘再异或这个结果）。
inline quint32 murmur2MixWord(quint32 word) {
    word *= kMurmur2M;
    word ^= word >> 24;
    word *= kMurmur2M;
    return word;
}

/**
 * @brief 流式喂字节的 murmur2。**长度必须事先知道**（初值 = `1 ^ 长度`）。
 *
 * NOTE: 正因为长度要先知道，算一个文件得读**两趟**：第一趟只数非空白字节有几个，
 *       第二趟才喂。代价是多读一遍文件，换来的是 O(1) 内存 —— 一个 200 MB 的 mod
 *       不会让启动器当场涨 200 MB（导出是并行算好几个文件的，那种涨法是乘以线程数的）。
 */
class Murmur2Stream {
public:
    explicit Murmur2Stream(quint32 length) : m_hash(1u ^ length) {}

    void add(const char *data, qsizetype size) {
        for (qsizetype i = 0; i < size; ++i) {
            const quint8 byte = static_cast<quint8>(data[i]);
            if (isStrippedByte(byte)) {
                continue;
            }
            m_tail[m_tailSize++] = byte;
            if (m_tailSize == 4) {
                flushWord();
            }
        }
    }

    quint32 finish() {
        if (m_tailSize > 0) {
            // 末梢那 1~3 个字节按小端拼成一个字，**先异或再乘**（顺序和主循环相反，
            // 这是 murmur2 本来的写法，别"顺手"统一成主循环那个顺序）。
            quint32 word = 0;
            for (qsizetype i = m_tailSize; i-- > 0;) {
                word = (word << 8) | m_tail[i];
            }
            m_hash ^= word;
            m_hash *= kMurmur2M;
            m_tailSize = 0;
        }
        m_hash ^= m_hash >> 13;
        m_hash *= kMurmur2M;
        m_hash ^= m_hash >> 15;
        return m_hash;
    }

private:
    void flushWord() {
        const quint32 word = static_cast<quint32>(m_tail[0]) |
                             (static_cast<quint32>(m_tail[1]) << 8) |
                             (static_cast<quint32>(m_tail[2]) << 16) |
                             (static_cast<quint32>(m_tail[3]) << 24);
        m_hash *= kMurmur2M;
        m_hash ^= murmur2MixWord(word);
        m_tailSize = 0;
    }

    quint32 m_hash;
    quint8 m_tail[4] = {};
    qsizetype m_tailSize = 0;
};

/// 读文件的那一趟：`countOnly` 为真就只数非空白字节，否则喂给状态机。
/// 两趟走的是同一个函数，省得"数"和"喂"两边的过滤规则哪天走岔。
bool scanFile(const QString &path, bool countOnly, Murmur2Stream *stream, qint64 *countOut,
              QString *errorOut) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut != nullptr) {
            *errorOut = QObject::tr("打不开这个文件：%1").arg(file.errorString());
        }
        return false;
    }
    QByteArray buffer;
    buffer.resize(64 * 1024);
    qint64 count = 0;
    for (;;) {
        const qint64 read = file.read(buffer.data(), buffer.size());
        if (read < 0) {
            if (errorOut != nullptr) {
                *errorOut = QObject::tr("读这个文件的时候出错了：%1").arg(file.errorString());
            }
            return false;
        }
        if (read == 0) {
            break;
        }
        if (countOnly) {
            const char *data = buffer.constData();
            for (qint64 i = 0; i < read; ++i) {
                if (!isStrippedByte(static_cast<quint8>(data[i]))) {
                    ++count;
                }
            }
        } else if (stream != nullptr) {
            stream->add(buffer.constData(), read);
        }
    }
    if (countOut != nullptr) {
        *countOut = count;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 清单序列化
// ---------------------------------------------------------------------------

/// `hashes` 那一段。sha1 与 sha512 **两个都必须有**（规范原文是 MUST）——
/// 缺哪个都不该写成一条，理由见 writableAsIndexFile()。
QJsonObject hashesOf(const MatchedFile &file) {
    QJsonObject hashes;
    if (!file.sha1.isEmpty()) {
        hashes.insert(QStringLiteral("sha1"), file.sha1);
    }
    if (!file.sha512.isEmpty()) {
        hashes.insert(QStringLiteral("sha512"), file.sha512);
    }
    return hashes;
}

}  // namespace

QString modrinthDependencyKey(const QString &loaderKey) {
    const QString key = loaderKey.trimmed();
    if (key.isEmpty()) {
        return QString();  // 纯原版：依赖表里只有 minecraft 那一行
    }
    if (key == ModpackImport::fabricLoaderKey() || key == ModpackImport::forgeLoaderKey() ||
        key == ModpackImport::neoforgeLoaderKey() || key == ModpackImport::quiltLoaderKey()) {
        return key;
    }
    // 认不出来的加载器（OptiFine、rift、以后新出的……）：**不编一个键出来**。
    // 编了的话导入方会拿它去装一个不存在的东西；返回空串让调用方照实说
    // "这个实例的加载器两种规范都表达不了"。
    return QString();
}

QString curseForgeLoaderId(const QString &loaderKey, const QString &loaderVersion,
                           const QString &gameVersion) {
    const QString key = loaderKey.trimmed();
    const QString version = loaderVersion.trimmed();
    if (key.isEmpty() || version.isEmpty()) {
        return QString();
    }
    QString prefix;
    if (key == ModpackImport::forgeLoaderKey()) {
        prefix = QStringLiteral("forge");
    } else if (key == ModpackImport::neoforgeLoaderKey()) {
        prefix = QStringLiteral("neoforge");
    } else if (key == ModpackImport::quiltLoaderKey()) {
        prefix = QStringLiteral("quilt");
    } else if (key == ModpackImport::fabricLoaderKey()) {
        // 只有这一种写法是官方明文确认过的：`fabric-x.y.z`（CurseForge 的拒稿文案里
        // 逐字写着 "Invalid Fabric mod loader id"，要求就是 `fabric-<版本>`）。
        prefix = QStringLiteral("fabric");
    } else {
        return QString();
    }
    // NeoForge 的 1.20.1 那一代版本号本身就是 `1.20.1-47.1.106` 这个形状，所以那一个
    // 游戏版本上要把它拼回来。别的版本加了反而是错的（CurseForge 那边认得的是
    // `neoforge-21.1.210`）—— 这套是照 Prism 的做法，不是官方文档里的（官方只
    // 明文写了 fabric 那一种）。
    if (key == ModpackImport::neoforgeLoaderKey() && gameVersion.trimmed() == QLatin1String("1.20.1")) {
        return prefix + QLatin1Char('-') + gameVersion.trimmed() + QLatin1Char('-') + version;
    }
    return prefix + QLatin1Char('-') + version;
}

bool looksLikeResolvableResource(const QString &relativePath) {
    const QString path = relativePath.trimmed().toLower();
    if (path.isEmpty()) {
        return false;
    }
    // 扩展名那一半。`.disabled` / `.old` 也认：那两种是"改了后缀的同一个文件"，
    // 字节没变，照样能反查出来（见 .hpp 里 MatchedFile::path 那段）。
    static const QStringList kExtensions = {QStringLiteral("jar"),   QStringLiteral("litemod"),
                                            QStringLiteral("zip"),   QStringLiteral("rar"),
                                            QStringLiteral("disabled"), QStringLiteral("old")};
    const QString suffix = QFileInfo(path).suffix();
    if (!kExtensions.contains(suffix)) {
        return false;
    }
    // 路径那另一半：**必须待在某个资源目录底下**。为什么要有这一条：导出时每个候选
    // 文件都要读一遍（CurseForge 那边还得读两遍）再往接口发一遍，而 config/ 里成千上万个
    // 小文件没有一个是能反查出来的 —— 不问，直接进 overrides 就行。代价是慢，收益是零。
    static const QStringList kContainers = {
        QStringLiteral("mods"),        QStringLiteral("coremods"), QStringLiteral("resourcepacks"),
        QStringLiteral("texturepacks"), QStringLiteral("shaderpacks"), QStringLiteral("datapacks"),
        QStringLiteral("openloader")};
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {  // 最后一段是文件名，不算
        if (kContainers.contains(parts.at(i))) {
            return true;
        }
    }
    return false;
}

bool writableAsIndexFile(const MatchedFile &file) {
    if (file.path.trimmed().isEmpty()) {
        return false;  // 没路径的条目导入方会直接跳过（等于没这条）
    }
    if (file.sha1.trimmed().isEmpty() || file.sha512.trimmed().isEmpty()) {
        // 规范原文是 MUST 两条都有。缺了**不写**：清单里的 sha1 是安装时唯一的完整性
        // 判据（DownloadManager::fileAlreadyValid 在 sha1 空着时直接点头），少了它
        // 半截文件也算"下好了" —— 那种包比"文件躺在 overrides 里"坏得多。
        return false;
    }
    for (const QString &url : file.urls) {
        if (!url.trimmed().isEmpty()) {
            return true;
        }
    }
    return false;  // 没有地址 = 导入方不知道该从哪下
}

QByteArray buildModrinthIndex(const Spec &spec) {
    QJsonArray files;
    for (const MatchedFile &file : spec.files) {
        if (!writableAsIndexFile(file)) {
            // 这一条本该被调用方拦下（走 writableAsIndexFile 那个分岔口去 overrides）。
            // 真漏到这里就跳过不写：写一条坏的会让导入方少一个文件，
            // 而"少一个文件"是最难被发现的那种坏 —— 包能装完，游戏能启动，
            // 只是某个 mod 不见了。
            continue;
        }
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), file.path);
        entry.insert(QStringLiteral("hashes"), hashesOf(file));
        QJsonArray downloads;
        for (const QString &url : file.urls) {
            if (!url.trimmed().isEmpty()) {
                downloads.append(url);
            }
        }
        entry.insert(QStringLiteral("downloads"), downloads);
        if (file.size >= 0) {
            entry.insert(QStringLiteral("fileSize"), static_cast<double>(file.size));
        }
        if (!file.envClient.isEmpty() && !file.envServer.isEmpty()) {
            QJsonObject env;
            env.insert(QStringLiteral("client"), file.envClient);
            env.insert(QStringLiteral("server"), file.envServer);
            entry.insert(QStringLiteral("env"), env);
        }
        files.append(entry);
    }

    QJsonObject dependencies;
    dependencies.insert(QStringLiteral("minecraft"), spec.gameVersion);
    const QString loaderKey = modrinthDependencyKey(spec.loaderKey);
    if (!loaderKey.isEmpty() && !spec.loaderVersion.trimmed().isEmpty()) {
        dependencies.insert(loaderKey, spec.loaderVersion.trimmed());
    }

    QJsonObject root;
    // 头两条是"这份 json 就是 modrinth.index.json"的标记，我们的解析器也按它们判
    // （见 parseModrinthIndex 开头那两条）。`game` 只可能是 minecraft。
    root.insert(QStringLiteral("formatVersion"), 1);
    root.insert(QStringLiteral("game"), QStringLiteral("minecraft"));
    root.insert(QStringLiteral("versionId"), spec.version);
    root.insert(QStringLiteral("name"), spec.name);
    if (!spec.summary.trimmed().isEmpty()) {
        root.insert(QStringLiteral("summary"), spec.summary.trimmed());
    }
    root.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("dependencies"), dependencies);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray buildCurseForgeManifest(const Spec &spec) {
    QJsonArray files;
    for (const MatchedRef &ref : spec.refs) {
        if (ref.projectId <= 0 || ref.fileId <= 0) {
            continue;  // 两个号缺一个就换不出文件来，那一条对导入方是纯噪音
        }
        QJsonObject entry;
        entry.insert(QStringLiteral("projectID"), ref.projectId);
        entry.insert(QStringLiteral("fileID"), ref.fileId);
        // `required` 恒为 true：我们列出来的都是这个实例**正在用**的文件。
        // "可选文件"那一套（导入时让用户勾）我们没做，写了反而给出一个假的选择。
        entry.insert(QStringLiteral("required"), true);
        files.append(entry);
    }

    QJsonObject minecraft;
    minecraft.insert(QStringLiteral("version"), spec.gameVersion);
    const QString loaderId =
        curseForgeLoaderId(spec.loaderKey, spec.loaderVersion, spec.gameVersion);
    if (!loaderId.isEmpty()) {
        QJsonObject loader;
        loader.insert(QStringLiteral("id"), loaderId);
        loader.insert(QStringLiteral("primary"), true);
        minecraft.insert(QStringLiteral("modLoaders"), QJsonArray{loader});
    }

    QJsonObject root;
    root.insert(QStringLiteral("manifestType"), QStringLiteral("minecraftModpack"));
    root.insert(QStringLiteral("manifestVersion"), 1);
    root.insert(QStringLiteral("name"), spec.name);
    root.insert(QStringLiteral("version"), spec.version);
    if (!spec.author.trimmed().isEmpty()) {
        root.insert(QStringLiteral("author"), spec.author.trimmed());
    }
    root.insert(QStringLiteral("minecraft"), minecraft);
    root.insert(QStringLiteral("files"), files);
    // 这一格**必须明写**：规范只说过"有个叫 overrides 的目录"，没定过缺省值是什么
    // （各家实现都自己写死）。所以我们也不赌，写出来。
    root.insert(QStringLiteral("overrides"), spec.overridesDir);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

quint32 curseForgeFingerprintOfBytes(const QByteArray &bytes) {
    quint32 count = 0;
    for (const char byte : bytes) {
        if (!isStrippedByte(static_cast<quint8>(byte))) {
            ++count;
        }
    }
    Murmur2Stream stream(count);
    stream.add(bytes.constData(), bytes.size());
    return stream.finish();
}

bool curseForgeFingerprint(const QString &path, quint32 *out, QString *errorOut) {
    if (out == nullptr) {
        // 出参没给就没什么可做的了 —— 这是调用方的编程错误，不当成"文件读不了"。
        if (errorOut != nullptr) {
            *errorOut = QObject::tr("内部错误：没有给出放指纹的地方。");
        }
        return false;
    }
    qint64 count = 0;
    if (!scanFile(path, true, nullptr, &count, errorOut)) {
        return false;
    }
    // 长度要先知道，所以这里才开第二趟。上限按 32 位截断：murmur2 的初值就是
    // `1 ^ (uint32)长度`，超 4 GB 的文件在算法里本来也是这么截的。
    Murmur2Stream stream(static_cast<quint32>(count));
    if (!scanFile(path, false, &stream, nullptr, errorOut)) {
        return false;
    }
    *out = stream.finish();
    return true;
}

}  // namespace ModpackExport
