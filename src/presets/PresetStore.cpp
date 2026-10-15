/**
 * @file PresetStore.cpp
 * @brief 实例预设落盘那一层的实现（形状与取舍见头文件与 `PresetTypes.hpp`）。
 * @author 0x7fffff
 * @date 2026-10-14
 *
 * 这一份里三处最容易写歪的地方，各自都有长注释：
 *   · `isValidPresetId()` —— 唯一的目录逃逸闸（§6 P1 的验收之一）；
 *   · `list()` —— "一份坏了不许连坐别的"（§6 P1 的另一条验收）落成的那个形状；
 *   · `remove()` / `create()` 里那两句"下标读不回来就**不写回去**" —— 写回去等于把用户
 *     排好的顺序抹平，而那一下屏幕上什么都不会说。
 */

#include "PresetStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

#include "manage/FolderManager.hpp"
#include "util/AppPaths.hpp"

namespace Presets {
namespace {

const char *kIndexFileName = "index.json";
const char *kManifestFileName = "manifest.json";
const char *kFilesDirName = "files";

/// 显示名最长这么多个字。★ 界面那一侧还会再按宽度截一次（那是摆不下的问题），这一刀管的是
/// "用户粘进来一整篇"：名字会进 `manifest.json`，而它还会被写进任务标题和错误报告。
constexpr int kDisplayNameLimit = 80;

/// 说明那一栏的上限。比名字宽松得多 —— 说明本来就是一段话。
constexpr int kDescriptionLimit = 400;

/// 写盘：临时文件 + 原子改名（和任务账本 / 启动历史同一套）。
bool writeBytesAtomic(const QString &path, const QByteArray &bytes) {
    const QFileInfo info(path);
    if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath())) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

/// 说明那一栏怎么收：只去首尾空白，**不折行**（它是给人看的一段话，换行是有意义的）。
QString cleanDescription(const QString &raw) {
    QString cleaned = raw.trimmed();
    if (cleaned.size() > kDescriptionLimit) {
        cleaned.truncate(kDescriptionLimit);
    }
    return cleaned;
}

/// Windows 上那几个保留的设备名（`CON` / `NUL` / `COM1`……）。★ 它不属于"穿越"，属于
/// "写下去就不见了"：拿它当文件名，`QFile::open()` 那句**会成功**，写进黑洞里。
///
/// ★ 判的是**基名**（第一个点之前那一截）：Windows 上 `nul.txt` 一样是那个空设备
///   （跟后缀无关），而 `console.txt` 是正常文件 —— 差的就是这个点。
bool isReservedDeviceName(const QString &part) {
    QString base = part;
    const int dot = base.indexOf(QLatin1Char('.'));
    if (dot >= 0) {
        base.truncate(dot);
    }
    // Windows 还会把结尾的空格和点吃掉（`nul ` 也是 nul）。
    while (!base.isEmpty() && (base.endsWith(QLatin1Char(' ')) || base.endsWith(QLatin1Char('.')))) {
        base.chop(1);
    }
    const QString upper = base.toUpper();
    if (upper == QLatin1String("CON") || upper == QLatin1String("PRN")
        || upper == QLatin1String("AUX") || upper == QLatin1String("NUL")) {
        return true;
    }
    if (upper.size() == 4 && (upper.startsWith(QLatin1String("COM"))
                              || upper.startsWith(QLatin1String("LPT")))) {
        const QChar digit = upper.at(3);
        return digit >= QLatin1Char('1') && digit <= QLatin1Char('9');
    }
    return false;
}

/// 以 `id` 为目录名的那一份，路径拼出来在 root 里面吗（最后一道自检，见 `isValidPresetId`）。
bool pathIsInsideRoot(const QString &root, const QString &path) {
    const QString canonicalRoot = QDir(root).absolutePath();
    const QString canonicalPath = QDir(path).absolutePath();
    return canonicalPath == canonicalRoot
           || canonicalPath.startsWith(canonicalRoot + QLatin1Char('/'));
}

/// 一句"哪个文件、怎么了"。用户的每一句抱怨都该说得出文件，所以路径摆在最前面。
QString faultAt(const QString &path, const QString &why) {
    return path + QStringLiteral("：") + why;
}

}  // namespace

// ---------------------------------------------------------------------------
// 落点
// ---------------------------------------------------------------------------

QString presetsRoot() {
    // ★ 启动器自带的数据根目录（不是 `minecraftDir()`）：预设是用户的习惯，换一个游戏
    //   文件夹之后它不该跟着不见 —— 理由和 `Tasks::defaultHistoryPath()` 逐字同源，
    //   见头文件那一段。
    return AppPaths::defaultMinecraftDir() + QStringLiteral("/presets");
}

QString indexPath(const QString &root) {
    return root + QLatin1Char('/') + QLatin1String(kIndexFileName);
}

QString presetDir(const QString &root, const QString &id) {
    return root + QLatin1Char('/') + id;
}

QString manifestPath(const QString &root, const QString &id) {
    return presetDir(root, id) + QLatin1Char('/') + QLatin1String(kManifestFileName);
}

QString filesDir(const QString &root, const QString &id) {
    return presetDir(root, id) + QLatin1Char('/') + QLatin1String(kFilesDirName);
}

// ---------------------------------------------------------------------------
// 名字与 id
// ---------------------------------------------------------------------------

QString newPresetId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool isValidPresetId(const QString &id) {
    // ★★ 这一条是**唯一**的目录逃逸闸，所以判据是"必须是一个 UUID"而不是"里面没有斜杠"。
    //    "没有斜杠"那种黑名单挡不住 `..`、挡不住 `C:`、挡不住以点开头、也挡不住 Windows
    //    那几个设备名 —— 而这几样正好都是"拼出来的路径不在 root 里面"的路子。
    //
    // ★ 连带的好处：我们自己在 `newPresetId()` 里造出来的 id 永远是 UUID，所以"用户手打
    //   一个 id 进来"这种事在这一层根本不存在（它只会走显示名那一路，而显示名不进路径）。
    if (id.isEmpty() || id.size() > 64) {
        return false;
    }
    const QUuid parsed = QUuid::fromString(id);
    if (parsed.isNull()) {
        return false;
    }
    // ★ 要的是**逐字**等于规范写法（小写、不带花括号）：`QUuid::fromString` 连
    //   `{...}` 和大小写混着写都认，认下来就等于"同一个 id 有两种写法" —— 而目录名是
    //   按字符串比的，两种写法在盘上就是两个目录。
    return parsed.toString(QUuid::WithoutBraces) == id;
}

QString cleanDisplayName(const QString &raw) {
    // `simplified()` 一举三得：去首尾空白、把中间连续的空格折成一个、把换行/制表符也折成
    // 空格 —— 后面那一条是必需的，名字会被写进任务标题（那边的行高是按一行算的）。
    QString cleaned = raw.simplified();
    if (cleaned.size() > kDisplayNameLimit) {
        cleaned.truncate(kDisplayNameLimit);
    }
    return cleaned;
}

bool isSafeRelativePath(const QString &path) {
    if (path.isEmpty()) {
        return false;
    }
    QString normalized = path;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // 结尾那一个斜杠是"这是个目录"的写法，无害，先摘掉；摘完是空的说明它整个就是斜杠。
    while (normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    if (normalized.isEmpty() || normalized.startsWith(QLatin1Char('/'))) {
        return false;
    }
    const QStringList parts = normalized.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        // 空的那一段 = 连着两个斜杠：同一个路径有两种写法，别放进来。
        if (part.isEmpty() || part == QLatin1String("..")) {
            return false;
        }
        if (part == QLatin1String(".")) {
            continue;
        }
        // 盘符（`C:`）/ NTFS 备用数据流（`name:stream`）/ 任何带冒号的怪名字。
        if (part.contains(QLatin1Char(':'))) {
            return false;
        }
        if (isReservedDeviceName(part)) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 下标
// ---------------------------------------------------------------------------

PresetIndex appendToIndex(PresetIndex index, const QString &id) {
    if (id.isEmpty() || index.order.contains(id)) {
        return index;
    }
    index.order.append(id);
    return index;
}

PresetIndex dropFromIndex(PresetIndex index, const QString &id) {
    index.order.removeAll(id);
    if (index.defaultPresetId == id) {
        index.defaultPresetId.clear();
    }
    return index;
}

PresetIndex moveInIndex(PresetIndex index, const QString &id, int delta) {
    const int from = index.order.indexOf(id);
    const int to = from + delta;
    if (from < 0 || delta == 0 || to < 0 || to >= index.order.size()) {
        return index;
    }
    index.order.move(from, to);
    return index;
}

PresetIndex loadIndex(const QString &root, bool *ok) {
    if (ok != nullptr) {
        // 文件不在**不算错**：一次都没做过预设的新用户就是这样。
        *ok = true;
    }
    QFile file(indexPath(root));
    if (!file.open(QIODevice::ReadOnly)) {
        return PresetIndex();
    }
    const QByteArray bytes = file.readAll();
    file.close();

    bool parsed = false;
    PresetIndex index = indexFromJson(bytes, &parsed);
    if (!parsed) {
        // ★ 读不回来时回的是**空下标 + ok=false**：调用方那一下不许把它写回去。写回去等于
        //   把用户排好的顺序抹平，而屏幕上什么都不会说（列表还是那么几份，只是次序变了）。
        if (ok != nullptr) {
            *ok = false;
        }
        return PresetIndex();
    }
    // 表里那些不像 id 的条目当场摘掉：留着它们只会让列表那一侧多出几行"查无此物"的报告。
    QStringList kept;
    for (const QString &id : index.order) {
        if (isValidPresetId(id)) {
            kept.append(id);
        }
    }
    index.order = kept;
    if (!index.defaultPresetId.isEmpty() && !isValidPresetId(index.defaultPresetId)) {
        index.defaultPresetId.clear();
    }
    return index;
}

bool saveIndex(const QString &root, const PresetIndex &index) {
    PresetIndex normalized = index;
    normalized.schemaVersion = kSchemaVersion;
    // 顺序表里只留合法 id（写侧同样要过一遍：`appendToIndex()` 进得来，`loadIndex()` 也可能
    // 是被别处拼出来直接喂进来的）。
    QStringList kept;
    for (const QString &id : normalized.order) {
        if (isValidPresetId(id) && !kept.contains(id)) {
            kept.append(id);
        }
    }
    normalized.order = kept;
    if (!isValidPresetId(normalized.defaultPresetId)) {
        normalized.defaultPresetId.clear();
    }
    if (!QDir().mkpath(root)) {
        return false;
    }
    return writeBytesAtomic(indexPath(root), indexToBytes(normalized));
}

// ---------------------------------------------------------------------------
// 单份
// ---------------------------------------------------------------------------

PresetManifest loadManifest(const QString &root, const QString &id, bool *ok, QString *reason) {
    const auto fail = [&](const QString &why) {
        if (ok != nullptr) {
            *ok = false;
        }
        if (reason != nullptr) {
            *reason = why;
        }
        return PresetManifest();
    };
    if (ok != nullptr) {
        *ok = true;
    }
    if (reason != nullptr) {
        reason->clear();
    }
    const QString dir = presetDir(root, id);
    if (!isValidPresetId(id)) {
        return fail(faultAt(dir, QStringLiteral("这个 id 不是一份预设（预设目录名只能是 UUID）")));
    }
    if (!pathIsInsideRoot(root, dir)) {
        return fail(faultAt(dir, QStringLiteral("拼出来的路径跑到预设目录外面去了")));
    }
    const QString path = manifestPath(root, id);
    if (!QFileInfo::exists(path)) {
        if (!QFileInfo::exists(dir)) {
            return fail(faultAt(dir, QStringLiteral("已经不在了")));
        }
        return fail(faultAt(path, QStringLiteral("不存在（那个目录里没有这一份）")));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(faultAt(path, QStringLiteral("打不开")));
    }
    const QByteArray bytes = file.readAll();
    file.close();

    bool parsed = false;
    const PresetManifest manifest = manifestFromJson(bytes, &parsed);
    if (!parsed) {
        return fail(faultAt(path, QStringLiteral("读不回来（不是合法 JSON、不是我们写的格式、"
                                                "版本比这个启动器新、或者缺了 id / 名字）")));
    }
    // ★ 记的 id 和目录名对不上 ⇒ 这一份被人动过（拷串了、或者手改过）。这种时候**宁可
    //   把它报成坏的**，也不要拿着"目录 A 里的内容属于 B"去覆盖用户的实例。
    if (manifest.id != id) {
        return fail(faultAt(path, QStringLiteral("记的 id 是「%1」，和目录名对不上").arg(manifest.id)));
    }
    return manifest;
}

bool saveManifest(const QString &root, const PresetManifest &manifest) {
    if (!isValidPresetId(manifest.id)) {
        return false;
    }
    if (cleanDisplayName(manifest.name).isEmpty()) {
        return false;
    }
    const QString dir = presetDir(root, manifest.id);
    if (!pathIsInsideRoot(root, dir)) {
        return false;
    }
    if (!QDir().mkpath(dir)) {
        return false;
    }
    // schema 版本由这一层钉死（调用方手里那一份可能是个 0）。理由：读的时候 `<= 0` 是要被
    // 挡住的，写一个 0 下去等于当场把这份预设写成"坏的那一档"。
    PresetManifest normalized = manifest;
    normalized.schemaVersion = kSchemaVersion;
    return writeBytesAtomic(manifestPath(root, manifest.id), manifestToBytes(normalized));
}

// ---------------------------------------------------------------------------
// 列表
// ---------------------------------------------------------------------------

QList<PresetSummary> list(const QString &root, QList<BrokenPreset> *broken) {
    const PresetIndex index = loadIndex(root, nullptr);
    QList<PresetSummary> summaries;

    const auto addOne = [&](const QString &id) {
        bool ok = false;
        QString reason;
        const PresetManifest manifest = loadManifest(root, id, &ok, &reason);
        if (!ok) {
            // ★ 坏的那一份**只进 broken**（不进 summary 列表）：这是 §6 P1 那条验收落成的
            //   形状 —— 一份读不回来，别的照旧列出来、照旧能用。
            if (broken != nullptr) {
                broken->append(BrokenPreset{id, reason});
            }
            return;
        }
        PresetSummary summary;
        summary.id = id;
        summary.name = manifest.name;
        summary.description = manifest.description;
        summary.updatedAt = manifest.updatedAt;
        summary.sectionCount = manifest.sections.size();
        summary.isDefault = (id == index.defaultPresetId);
        summaries.append(summary);
    };

    QStringList seen;
    for (const QString &id : index.order) {
        seen.append(id);
        addOne(id);
    }

    // 盘上有、下标里没有的（用户自己拷进来一份、或者下标被写坏过）→ **追加在末尾**。
    // 不丢东西是这一条的全部理由：那些目录里躺着用户真正的预设。
    QStringList extras;
    const QDir dir(root);
    const QStringList entries =
        dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QString &entry : entries) {
        if (!isValidPresetId(entry) || seen.contains(entry)) {
            continue;
        }
        extras.append(entry);
    }
    // 读盘那一趟的次序要确定（`entryList` 的次序是文件系统给的，两台机器上不一样）。
    extras.sort();
    for (const QString &id : extras) {
        addOne(id);
    }
    return summaries;
}

QString defaultPresetId(const QString &root) {
    return loadIndex(root, nullptr).defaultPresetId;
}

// ---------------------------------------------------------------------------
// 增删改
// ---------------------------------------------------------------------------

PresetManifest create(const QString &root, const QString &name, const QString &description) {
    const QString cleaned = cleanDisplayName(name);
    if (cleaned.isEmpty()) {
        // 名字是这一份身份里给用户看的那一半，空的建不了（界面上"新建"那一趟总会先问到名字）。
        return PresetManifest();
    }
    PresetManifest manifest;
    manifest.id = newPresetId();
    manifest.name = cleaned;
    manifest.description = cleanDescription(description);
    manifest.createdAt = QDateTime::currentDateTime();
    manifest.updatedAt = manifest.createdAt;

    if (!QDir().mkpath(filesDir(root, manifest.id))) {
        return PresetManifest();
    }
    if (!saveManifest(root, manifest)) {
        return PresetManifest();
    }

    bool indexReadable = true;
    PresetIndex index = loadIndex(root, &indexReadable);
    if (indexReadable) {
        // ★ 下标读不回来时**不去覆盖它**（那会把用户的顺序抹平）；代价是刚建的这一份这一趟
        //   进不了顺序表 —— 而 `list()` 会把"盘上有、下标里没有"的那一份追加在末尾列出来，
        //   所以它从用户的角度依然是"建出来了"。
        if (saveIndex(root, appendToIndex(index, manifest.id))) {
            return manifest;
        }
    }
    return manifest;
}

PresetManifest duplicate(const QString &root, const QString &id, const QString &newName) {
    bool ok = false;
    QString reason;
    const PresetManifest source = loadManifest(root, id, &ok, &reason);
    if (!ok) {
        return PresetManifest();
    }
    PresetManifest copy = source;
    copy.id = newPresetId();
    const QString cleaned = cleanDisplayName(newName);
    copy.name = cleaned.isEmpty() ? cleanDisplayName(source.name + QStringLiteral(" 副本")) : cleaned;
    if (copy.name.isEmpty()) {
        return PresetManifest();
    }
    copy.sections = source.sections;
    copy.createdAt = QDateTime::currentDateTime();
    copy.updatedAt = copy.createdAt;

    const QString newDir = presetDir(root, copy.id);
    if (!QDir().mkpath(newDir)) {
        return PresetManifest();
    }
    const QString fromFiles = filesDir(root, id);
    if (QFileInfo::exists(fromFiles)) {
        if (!copyTreeWithoutFollowingLinks(fromFiles, filesDir(root, copy.id))) {
            // 拷了一半：把这一份的新目录整棵收掉，别在预设列表里留一个半成品。
            removeTreeWithoutFollowingLinks(newDir);
            return PresetManifest();
        }
    } else if (!QDir().mkpath(filesDir(root, copy.id))) {
        return PresetManifest();
    }
    if (!saveManifest(root, copy)) {
        removeTreeWithoutFollowingLinks(newDir);
        return PresetManifest();
    }

    bool indexReadable = true;
    const PresetIndex index = loadIndex(root, &indexReadable);
    if (indexReadable) {
        saveIndex(root, appendToIndex(index, copy.id));
    }
    return copy;
}

bool rename(const QString &root, const QString &id, const QString &newName) {
    const QString cleaned = cleanDisplayName(newName);
    if (cleaned.isEmpty()) {
        return false;
    }
    bool ok = false;
    QString reason;
    PresetManifest manifest = loadManifest(root, id, &ok, &reason);
    if (!ok) {
        return false;
    }
    if (manifest.name == cleaned) {
        // 名字没变：算成功，但**不动** `updatedAt`（"改了一次"这个动作没发生过）。
        return true;
    }
    manifest.name = cleaned;
    manifest.updatedAt = QDateTime::currentDateTime();
    return saveManifest(root, manifest);
}

bool remove(const QString &root, const QString &id) {
    if (!isValidPresetId(id)) {
        return false;
    }
    const QString dir = presetDir(root, id);
    if (!pathIsInsideRoot(root, dir)) {
        return false;
    }
    // ★ 目录那一趟**不追链接**：用户把 `files/` 挂到另一块盘上是常见做法，追进去删的是
    //   那一头的东西（这条在 `FolderManager.cpp` 里有一整段实测结论）。
    if (QFileInfo::exists(dir) && !removeTreeWithoutFollowingLinks(dir)) {
        return false;
    }
    bool indexReadable = true;
    const PresetIndex index = loadIndex(root, &indexReadable);
    if (!indexReadable) {
        // 下标本来就坏着：不拿这一趟去覆盖它。目录已经摘掉了，从用户的角度这一份没了。
        return true;
    }
    return saveIndex(root, dropFromIndex(index, id));
}

bool setDefault(const QString &root, const QString &id) {
    bool indexReadable = true;
    PresetIndex index = loadIndex(root, &indexReadable);
    if (!indexReadable) {
        return false;
    }
    if (id.isEmpty()) {
        index.defaultPresetId.clear();
        return saveIndex(root, index);
    }
    if (!index.contains(id)) {
        return false;
    }
    index.defaultPresetId = id;
    return saveIndex(root, index);
}

bool move(const QString &root, const QString &id, int delta) {
    bool indexReadable = true;
    const PresetIndex index = loadIndex(root, &indexReadable);
    if (!indexReadable) {
        return false;
    }
    const PresetIndex moved = moveInIndex(index, id, delta);
    if (moved.order == index.order) {
        return false;
    }
    return saveIndex(root, moved);
}

}  // namespace Presets
