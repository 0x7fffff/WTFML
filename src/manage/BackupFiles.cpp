/**
 * @file BackupFiles.cpp
 * @brief 世界与实例备份的实现（coding plan §4.4）。设计与为什么这么设计见头文件。
 * @author 0x7fffff
 * @date 2026-10-13
 */

#include "manage/BackupFiles.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QUuid>

#include <algorithm>

#include "manage/FolderManager.hpp"
#include "util/AppPaths.hpp"
#include "util/ZipUtil.hpp"

namespace BackupFiles {

namespace {

// 盘上那两个字面量。写成一眼能读懂、**和界面无关**的词（`kindKey` 出现在清单里，
// 别的工具照着这个字段判"这是哪种备份"，所以它不能跟着界面上那句中文走）。
const char *kFormatTag = "wtfml-backup/1";
const char *kKindWorld = "world";
const char *kKindInstance = "instance";

/// 链接目录**一个都不进去**。和 `ModpackExport` 那份是同一件事、同一个理由：
/// `QDir::NoSymLinks` 挡不住 junction（本仓实测过），而这里恰是被别人的链接牵着走的
/// 那种地方 —— 一个指回祖先的 junction 能让枚举转不完，一个指向实例外面的 junction 会把
/// 别人的东西打进包（还原时再写回去）。
bool isLinkToElsewhere(const QFileInfo &info) {
    return info.isJunction() || info.isSymLink();
}

/// 一份文件的 sha1（小写十六进制）。读不动时回空串 —— **调用方必须把空串当失败**，
/// 不能当成"这个文件的哈希是空的"（那样这一份备份就永远对得了账了）。
///
/// NOTE: 走 `addData(QIODevice*)` 是**流式**的，不是 `readAll()`。一个存档的 region 文件
///       几十 MB、一个实例快照里有几百 MB 的 jar —— 整份读到内存里只是为了让哈希函数看
///       一遍，是不必要的峰值。
QString sha1OfFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file)) {
        return QString();
    }
    return QString::fromLatin1(hash.result().toHex());
}

/// 文件名里那个词能不能当文件名用（Windows 不认 `\/:*?"<>|` 和控制字符，
/// 结尾的点和空格也会被悄悄吃掉 —— 吃掉之后两次备份可能撞成同一个名字）。
QString safeLabel(const QString &label) {
    QString out;
    out.reserve(label.size());
    for (const QChar ch : label) {
        const bool bad = ch == QLatin1Char('/') || ch == QLatin1Char('\\') || ch == QLatin1Char(':')
                         || ch == QLatin1Char('*') || ch == QLatin1Char('?') || ch == QLatin1Char('"')
                         || ch == QLatin1Char('<') || ch == QLatin1Char('>') || ch == QLatin1Char('|')
                         || ch < QChar(0x20);
        out.append(bad ? QLatin1Char('-') : ch);
    }
    out = out.trimmed();
    while (!out.isEmpty() && (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))) {
        out.chop(1);
    }
    if (out.isEmpty()) {
        out = QStringLiteral("backup");
    }
    if (out.size() > 80) {
        out = out.left(80);
    }
    return out;
}

QString kindKey(Kind kind) {
    return QString::fromLatin1(kind == Kind::Instance ? kKindInstance : kKindWorld);
}

/// 字符串数组那一小段（范围 / 排除表），空的项一律不记 —— 空串进清单只会让读回来的人
/// 多一条"这一条是什么意思"的疑问。
QStringList stringListOf(const QJsonValue &value) {
    QStringList out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        const QString text = item.toString();
        if (!text.isEmpty()) {
            out.append(text);
        }
    }
    return out;
}

}  // namespace

bool Manifest::isValid() const {
    if (root.isEmpty() || !createdAt.isValid()) {
        return false;
    }
    // 前缀只能是**一个目录名**。还原时落点是 `targetParentDir + "/" + root` 拼出来的，
    // 里面带上 `/`、`..` 或者盘符就能把文件写到别的地方去 —— 我们自己写的包不会这样，
    // 但"拿一个别人的包来还原"是用户能做的事，所以读回来的那一刻就得挡住。
    if (root.contains(QLatin1Char('/')) || root.contains(QLatin1Char('\\'))) {
        return false;
    }
    if (root == QLatin1String(".") || root == QLatin1String("..")) {
        return false;
    }
    if (QDir::isAbsolutePath(root)) {
        return false;
    }
    return true;
}

QString manifestEntryName() {
    return QStringLiteral("wtfml-backup.json");
}

QString formatTag() {
    return QString::fromLatin1(kFormatTag);
}

QByteArray manifestToJson(const Manifest &manifest) {
    QJsonObject root;
    root.insert(QStringLiteral("format"), formatTag());
    root.insert(QStringLiteral("kind"), kindKey(manifest.kind));
    root.insert(QStringLiteral("root"), manifest.root);
    root.insert(QStringLiteral("label"), manifest.label);
    // §4.4 点名的四样：来源实例 id、时间、游戏版本、包含范围。校验值在下面 `files` 里。
    root.insert(QStringLiteral("sourceInstanceId"), manifest.sourceInstanceId);
    root.insert(QStringLiteral("sourceInstanceName"), manifest.sourceInstanceName);
    root.insert(QStringLiteral("gameVersion"), manifest.gameVersion);
    root.insert(QStringLiteral("createdAt"), manifest.createdAt.toString(Qt::ISODate));
    root.insert(QStringLiteral("scope"), QJsonArray::fromStringList(manifest.scope));
    if (!manifest.worldName.isEmpty()) {
        root.insert(QStringLiteral("worldName"), manifest.worldName);
    }
    // `complete` / `excluded` 只有实例快照有意义：世界备份无从"排除"起，写一个
    // 恒为 false 的字段只会让读的人以为它是个开关。
    if (manifest.kind == Kind::Instance) {
        root.insert(QStringLiteral("complete"), manifest.complete);
        root.insert(QStringLiteral("excluded"), QJsonArray::fromStringList(manifest.excluded));
    }
    if (manifest.safety) {
        root.insert(QStringLiteral("safety"), true);
    }
    root.insert(QStringLiteral("fileCount"), manifest.fileCount());
    root.insert(QStringLiteral("totalBytes"), manifest.totalBytes);

    QJsonArray files;
    for (const FileRecord &record : manifest.files) {
        QJsonObject item;
        item.insert(QStringLiteral("path"), record.path);
        item.insert(QStringLiteral("size"), record.size);
        item.insert(QStringLiteral("sha1"), record.sha1);
        files.append(item);
    }
    root.insert(QStringLiteral("files"), files);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

Manifest manifestFromJson(const QByteArray &bytes, bool *ok) {
    if (ok != nullptr) {
        *ok = false;
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return Manifest();
    }
    const QJsonObject root = doc.object();
    // ★ 第一道判据是那个**写死的**格式标记，而不是"有没有哪个字段"。以后改格式时，
    //   旧包会在这里被挡住（"不是我们这一版做的"），而不是半信半疑地往里解。
    if (root.value(QStringLiteral("format")).toString() != formatTag()) {
        return Manifest();
    }

    Manifest manifest;
    const QString kind = root.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String(kKindWorld)) {
        manifest.kind = Kind::World;
    } else if (kind == QLatin1String(kKindInstance)) {
        manifest.kind = Kind::Instance;
    } else {
        return Manifest();  // 认不出的档 → 整份不认（宁可说"读不动"，也不猜）
    }

    manifest.root = root.value(QStringLiteral("root")).toString();
    manifest.label = root.value(QStringLiteral("label")).toString();
    manifest.sourceInstanceId = root.value(QStringLiteral("sourceInstanceId")).toString();
    manifest.sourceInstanceName = root.value(QStringLiteral("sourceInstanceName")).toString();
    manifest.gameVersion = root.value(QStringLiteral("gameVersion")).toString();
    manifest.worldName = root.value(QStringLiteral("worldName")).toString();
    manifest.complete = root.value(QStringLiteral("complete")).toBool(false);
    manifest.safety = root.value(QStringLiteral("safety")).toBool(false);
    manifest.createdAt = QDateTime::fromString(root.value(QStringLiteral("createdAt")).toString(),
                                               Qt::ISODate);
    manifest.scope = stringListOf(root.value(QStringLiteral("scope")));
    manifest.excluded = stringListOf(root.value(QStringLiteral("excluded")));

    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    manifest.files.reserve(files.size());
    for (const QJsonValue &value : files) {
        const QJsonObject item = value.toObject();
        FileRecord record;
        record.path = item.value(QStringLiteral("path")).toString();
        if (record.path.isEmpty()) {
            continue;  // 没名字的条目落不了地，直接当它不存在
        }
        record.size = item.value(QStringLiteral("size")).toInteger(0);
        record.sha1 = item.value(QStringLiteral("sha1")).toString();
        manifest.files.append(record);
    }

    // `totalBytes` 以数出来的为准：清单是"记录"，不是"凭据" —— 有人手改过这个数的话，
    // 界面上的"共 N MB"不该跟着错。`files` 空的时候保留清单里那个数（这时它是唯一的说法）。
    if (!manifest.files.isEmpty()) {
        qint64 total = 0;
        for (const FileRecord &record : manifest.files) {
            total += record.size;
        }
        manifest.totalBytes = total;
    } else {
        manifest.totalBytes = root.value(QStringLiteral("totalBytes")).toInteger(0);
    }

    if (!manifest.isValid()) {
        return Manifest();
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return manifest;
}

QString backupRoot() {
    return AppPaths::backupDir();
}

QString backupDir(Kind kind) {
    return backupRoot() + (kind == Kind::Instance ? QStringLiteral("/instances")
                                                  : QStringLiteral("/worlds"));
}

QString backupFilePrefix(const QString &label) {
    return safeLabel(label) + QLatin1Char('-');
}

QString backupFileName(const QString &label, const QDateTime &at, bool safety) {
    QString name = backupFilePrefix(label);
    if (safety) {
        // 「恢复前」夹在中间而不是放在尾巴上：这一份是**自动**做出来的，摆前面一点是为了
        // 在按名字排序的列表里一眼看得见（它的时间戳和用户点的那一份几乎一样，只看时间
        // 排不出先后）。
        name += QStringLiteral("恢复前-");
    }
    name += at.toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
    name += QStringLiteral(".zip");
    return name;
}

QList<FileRecord> collectFiles(const QString &rootDir, const QStringList &excludeTop,
                               QString *problemOut) {
    if (problemOut != nullptr) {
        problemOut->clear();
    }
    if (!QFileInfo(rootDir).isDir()) {
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("找不到这个目录：%1").arg(QDir::toNativeSeparators(rootDir));
        }
        return QList<FileRecord>();
    }

    QList<FileRecord> files;
    struct Frame {
        QString abs;
        QString prefix;  ///< 相对根的那一截，带尾斜杠；根自己是空串
    };
    QList<Frame> stack;
    const QString cleanRoot = QDir::cleanPath(rootDir);
    stack.append({cleanRoot, QString()});
    QSet<QString> seenDirs;  // 同一个目录（大小写/`.` 这类差别）只走一遍

    while (!stack.isEmpty()) {
        const Frame frame = stack.takeLast();
        const QString key = frame.abs.toLower();
        if (seenDirs.contains(key)) {
            continue;
        }
        seenDirs.insert(key);

        const QDir dir(frame.abs);
        // 和 `ModpackExport::collectInstanceFiles()` 同一组 flags：隐藏文件**要收**
        // （存档里 `.gitignore`、加载器写的状态文件都带点，"名字带不带点"不该决定
        // 它进不进备份），目录**不跟链接**（见 isLinkToElsewhere）。
        const QFileInfoList plainFiles =
            dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
        for (const QFileInfo &entry : plainFiles) {
            FileRecord record;
            record.path = frame.prefix + entry.fileName();
            record.size = entry.size();
            record.sha1 = sha1OfFile(entry.absoluteFilePath());
            if (record.sha1.isEmpty()) {
                // 读不动就是读不动 —— 不能记一条没有校验值的记录假装它在，那样还原之后的
                // 对账反而会**放过**这个文件（"空校验值 = 不查"）。
                if (problemOut != nullptr) {
                    *problemOut = QObject::tr("读不动这个文件：%1")
                                      .arg(QDir::toNativeSeparators(entry.absoluteFilePath()));
                }
                return QList<FileRecord>();
            }
            files.append(record);
        }

        const QFileInfoList subdirs = dir.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Hidden, QDir::Name);
        for (const QFileInfo &entry : subdirs) {
            if (isLinkToElsewhere(entry)) {
                continue;
            }
            const QString name = entry.fileName();
            // 排除**只看顶层**：`assets` / `libraries` 是实例根下面那两份，别处的同名目录
            // （某个模组自己 data 里的 assets）是**要**备份的 —— 按名字全局排除会把它们
            // 悄悄漏掉，而"少了一个模组的资源"是那种几年之后才会有人发现的事。
            if (frame.prefix.isEmpty() && excludeTop.contains(name, Qt::CaseInsensitive)) {
                continue;
            }
            stack.append({entry.absoluteFilePath(), frame.prefix + name + QLatin1Char('/')});
        }
    }

    // 按路径排序：清单要**可复现**（同一份内容打两次包，清单逐字一样）——
    // 目录枚举顺序是文件系统的自由，不排的话连"两次录屏逐帧比"这种事都做不了。
    std::sort(files.begin(), files.end(),
              [](const FileRecord &a, const FileRecord &b) { return a.path < b.path; });
    return files;
}

WriteResult writeBackup(const Manifest &manifest, const QString &sourceDir,
                        const QString &targetZip) {
    WriteResult result;
    result.zipPath = targetZip;
    result.manifest = manifest;

    if (manifest.root.isEmpty()) {
        result.problem = QObject::tr("这一份没说清自己是什么（缺少顶层前缀），没有打包。");
        return result;
    }

    QString problem;
    result.manifest.files = collectFiles(sourceDir, manifest.excluded, &problem);
    if (!problem.isEmpty()) {
        result.problem = problem;
        result.manifest.files.clear();
        return result;
    }
    if (result.manifest.files.isEmpty()) {
        result.problem = QObject::tr("这个目录是空的，没什么可备份的：%1")
                             .arg(QDir::toNativeSeparators(sourceDir));
        return result;
    }

    qint64 total = 0;
    for (const FileRecord &record : result.manifest.files) {
        total += record.size;
    }
    result.manifest.totalBytes = total;
    if (!result.manifest.createdAt.isValid()) {
        result.manifest.createdAt = QDateTime::currentDateTime();
    }

    // 清单先落一份**临时文件**：`ZipUtil::createZip` 的条目表只能指向盘上真实的文件
    // （整张表走临时文件递进 PowerShell，理由见 ZipUtil.hpp 里 createZip 那段 ——
    //  Windows 一条命令行只有 32767 个字符）。
    //
    // ★★ 这里**不能用 `QTemporaryFile`**（2026-10-13 verify ZQ3 撞出来的，产品级的错）：
    //    它建出来的文件在**那个对象还活着的时候别的进程读不了** —— 连 `close()` 过之后
    //    也一样，.NET 那边拿到的是「The process cannot access the file ... because it is
    //    being used by another process」。而打包这件事**正是**"另一个进程（PowerShell）
    //    去读这个文件"，所以只要走 QTemporaryFile，这一趟必然整份失败：清单加不进去 ⇒
    //    zip 一个条目都没有 ⇒ 用户在「立即备份」上看到的是一句和自己的盘毫无关系的话。
    //    改成普通 QFile + 一个不会撞的名字（和 `ZipUtil::createZip` 递条目表那张临时文件
    //    同一个写法），写进一个作用域里让它在打包之前就关掉，成功失败都删掉那一份。
    const QString manifestPath = QDir::tempPath() + QStringLiteral("/wtfml-backup-")
                                 + QUuid::createUuid().toString(QUuid::WithoutBraces)
                                 + QStringLiteral(".json");
    const QByteArray manifestBytes = manifestToJson(result.manifest);
    {
        QFile manifestFile(manifestPath);
        if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.problem = QObject::tr("没能写出这份备份的清单（临时文件建不出来）：%1")
                                 .arg(QDir::toNativeSeparators(manifestPath));
            return result;
        }
        // 短写（盘刚好在半路满）也算失败：半截 JSON 装进包里，读回来时是"清单读不动"，
        // 而那句话说不到这个真因上。
        if (manifestFile.write(manifestBytes) != manifestBytes.size()) {
            result.problem = QObject::tr("没能写出这份备份的清单（临时文件写不完整）：%1")
                                 .arg(QDir::toNativeSeparators(manifestPath));
            manifestFile.close();
            QFile::remove(manifestPath);
            return result;
        }
    }

    QList<ZipUtil::ZipSource> entries;
    ZipUtil::ZipSource manifestEntry;
    manifestEntry.entryName = manifestEntryName();
    manifestEntry.sourcePath = manifestPath;
    entries.append(manifestEntry);

    const QDir source(sourceDir);
    for (const FileRecord &record : result.manifest.files) {
        ZipUtil::ZipSource entry;
        entry.entryName = result.manifest.root + QLatin1Char('/') + record.path;
        entry.sourcePath = source.filePath(record.path);
        entries.append(entry);
    }

    QString error;
    const bool made = ZipUtil::createZip(targetZip, entries, &error);
    // 成功失败都一样删：它躺在系统临时目录里，里面是这个实例 / 世界都有哪些文件。
    QFile::remove(manifestPath);
    if (!made) {
        result.problem = error.isEmpty() ? QObject::tr("打包没有成功。") : error;
        return result;
    }

    result.ok = true;
    return result;
}

Manifest readManifest(const QString &zipPath, bool *ok, QString *problemOut) {
    if (ok != nullptr) {
        *ok = false;
    }
    if (problemOut != nullptr) {
        problemOut->clear();
    }

    QByteArray bytes;
    QString error;
    const ZipUtil::EntryStatus status =
        ZipUtil::readEntry(zipPath, manifestEntryName(), &bytes, &error,
                           QObject::tr("读这份备份的清单"));
    // ★ 「不是我们做的包」和「读不动」在这个 switch 里是**两句不同的话**：
    //   前者是用户拿错了文件（或者那个包是别的启动器做的），后者是这个文件本身坏了。
    //   合成一句的话，用户会去怀疑一个其实好好的文件。
    switch (status) {
    case ZipUtil::EntryStatus::NotFound:
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("这个压缩包里没有备份清单（%1）—— 它多半不是本启动器"
                                      "做出来的备份。")
                              .arg(manifestEntryName());
        }
        return Manifest();
    case ZipUtil::EntryStatus::NotAnArchive:
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("打不开这个文件：它不是一个压缩包，或者已经损坏了。");
        }
        return Manifest();
    case ZipUtil::EntryStatus::Failed:
        if (problemOut != nullptr) {
            *problemOut = error.isEmpty() ? QObject::tr("读这份备份的清单时出错了。") : error;
        }
        return Manifest();
    case ZipUtil::EntryStatus::Ok:
        break;
    }

    bool parsed = false;
    const Manifest manifest = manifestFromJson(bytes, &parsed);
    if (!parsed) {
        if (problemOut != nullptr) {
            *problemOut = QObject::tr("这份备份的清单读不动（格式对不上，可能是别的工具做的包）。");
        }
        return Manifest();
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return manifest;
}

RestoreResult restoreBackup(const QString &zipPath, const QString &targetParentDir) {
    RestoreResult result;

    bool readable = false;
    QString problem;
    const Manifest manifest = readManifest(zipPath, &readable, &problem);
    if (!readable) {
        result.stage = RestoreResult::Stage::ReadManifest;
        result.problem = problem;
        return result;
    }

    const QString target = QDir(targetParentDir).filePath(manifest.root);
    result.targetPath = target;

    // ① 现在那儿已经有一份 → **先给它做一份安全快照**，再整个挪走。
    //
    //    ★ 顺序不能反，而且安全快照**做不成就不往下走**：先删掉旧的、再回来告诉用户
    //      "刚才想给你留一份但是没留成"，是这一整套里唯一不可接受的结果。
    const QFileInfo existing(target);
    if (existing.exists()) {
        if (existing.isDir()) {
            Manifest safety;
            safety.kind = manifest.kind;
            safety.root = manifest.root;
            safety.label = manifest.label;
            safety.sourceInstanceId = manifest.sourceInstanceId;
            safety.sourceInstanceName = manifest.sourceInstanceName;
            safety.gameVersion = manifest.gameVersion;
            safety.worldName = manifest.worldName;
            safety.complete = manifest.complete;
            safety.excluded = manifest.excluded;
            safety.safety = true;
            safety.createdAt = QDateTime::currentDateTime();
            safety.scope = QStringList{
                QObject::tr("还原之前自动给「现在这一份」做的安全快照")};

            const QString dir = backupDir(safety.kind);
            QDir().mkpath(dir);
            const QString safetyZip =
                QDir(dir).filePath(backupFileName(safety.label, safety.createdAt, /*safety=*/true));

            const WriteResult snapshot = writeBackup(safety, target, safetyZip);
            if (!snapshot.ok) {
                result.stage = RestoreResult::Stage::SafetySnapshot;
                result.problem = QObject::tr("没能在还原之前给「现在这一份」做安全快照：%1"
                                             "\n为了不让你丢东西，这个目录**没有被改动**。")
                                     .arg(snapshot.problem);
                return result;
            }
            result.safetyZip = safetyZip;
        } else {
            // 同名的**文件**（不是目录）：没有"快照"这一说，直接挪开。备份里那是一条
            // 记录不了的东西 —— 走到这儿多半是用户自己在那儿放过一个同名文件。
            if (!QFile::remove(target)) {
                result.stage = RestoreResult::Stage::RemoveOld;
                result.problem = QObject::tr("没能挪开原来的那个同名文件：%1")
                                     .arg(QDir::toNativeSeparators(target));
                return result;
            }
        }
        if (existing.isDir() && !removeTreeWithoutFollowingLinks(target)) {
            // 半截删掉了，也没法回退。**如实说**（安全快照在 `safetyZip` 里，用户手上
            // 有东西可救），而不是假装没事继续往下解压。
            result.stage = RestoreResult::Stage::RemoveOld;
            result.problem = QObject::tr("没能把「现在这一份」整个挪开（删到一半失败了）：%1"
                                         "\n还原前的安全快照在：%2")
                                 .arg(QDir::toNativeSeparators(target),
                                      QDir::toNativeSeparators(result.safetyZip));
            return result;
        }
    }

    // ② 解压。走 `extractSubtrees()` 是**故意的**：它把前缀剥掉，而且**不属于那个前缀的
    //    条目一个都不碰** —— zip 根上躺着的那份清单因此不会落到用户的世界目录里。
    //
    //    ★ 落点必须是**那一层前缀那一份**（`target`），不是 `targetParentDir`：前缀是剥掉的，
    //      所以它得由我们这边补回来。传 `targetParentDir` 的话（2026-10-13 verify ZQ4 撞到
    //      的，产品级的错）`基地周目/region/x` 会被解成 `<父目录>/region/x` —— 也就是
    //      **把存档的文件倒在 saves/ 这一层**，而上面那一步刚把 `saves/基地周目` 删掉。
    QString error;
    if (!ZipUtil::extractSubtrees(QStringList{zipPath}, target,
                                  QStringList{manifest.root + QLatin1Char('/')}, nullptr, &error)) {
        result.stage = RestoreResult::Stage::Extract;
        QString message = QObject::tr("解压没能完成：%1").arg(error);
        if (!result.safetyZip.isEmpty()) {
            message += QObject::tr("\n还原之前的「现在这一份」在：%1")
                           .arg(QDir::toNativeSeparators(result.safetyZip));
        }
        result.problem = message;
        return result;
    }

    // ③ 逐文件对账。**"文件在那儿"和"文件是对的"是两件事** —— 解压到一半被打断、盘写满，
    //    留下的都是"在，但不对"的文件，只有这一趟认得出。
    result.checked = manifest.fileCount();
    result.mismatched = verifyTree(target, manifest.files);
    result.ok = result.mismatched == 0;
    if (!result.ok) {
        result.stage = RestoreResult::Stage::Verify;
        QString message = QObject::tr("备份解开了，但解出来的文件和清单里记的对不上"
                                      "（%1 / %2 个）。")
                              .arg(result.mismatched)
                              .arg(result.checked);
        if (!result.safetyZip.isEmpty()) {
            message += QObject::tr("\n还原之前的「现在这一份」在：%1")
                           .arg(QDir::toNativeSeparators(result.safetyZip));
        }
        result.problem = message;
    }
    return result;
}

int verifyTree(const QString &dir, const QList<FileRecord> &files) {
    int mismatched = 0;
    const QDir root(dir);
    for (const FileRecord &record : files) {
        const QFileInfo info(root.filePath(record.path));
        if (!info.isFile()) {
            ++mismatched;  // 缺了
            continue;
        }
        if (info.size() != record.size) {
            ++mismatched;  // 长短就不对，不用算哈希了
            continue;
        }
        if (record.sha1.isEmpty()) {
            continue;  // 清单里本来就没记校验值（手改过的清单）—— 查不了，不算它错
        }
        if (sha1OfFile(info.absoluteFilePath()) != record.sha1) {
            ++mismatched;  // 长度一样但内容不一样
        }
    }
    return mismatched;
}

QString scopeLine(const QString &what, int fileCount) {
    return QObject::tr("%1（%2 个文件）").arg(what).arg(fileCount);
}

}  // namespace BackupFiles
