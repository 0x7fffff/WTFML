/**
 * @file VersionManager.cpp
 * @brief Implements version discovery and download orchestration.
 * @author LoserKugua
 * @date 2026-04-10
 * @note 处理版本 metadata 和下载流程，避免 UI 中直接访问网络。
 *
 * Uses DownloadManager to fetch version manifests, version JSON, and to
 * download required libraries and assets for selected Minecraft versions.
 */

#include "VersionManager.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QUuid>

#include "../util/AppPaths.hpp"
#include "InstanceFiles.hpp"

namespace {

/// wtfml.meta 里那个"从生到死都跟着这个实例"的 id 的字段名。
///
/// NOTE: 刻意**不叫 "id"**，理由和这个文件不叫 wtfml.json 是同一件事（见 .hpp 里
///       displayParts() 那段）：版本 JSON 里的 "id" 有特殊语义，一个文件里出现两个 id
///       早晚会被读错，而那类错误的形态是"静默选错 Java"级别的。换个不可能撞的名字，
///       这个错在结构上就不存在。
const QString kInstanceIdField = QStringLiteral("instanceId");

/// wtfml.meta 里"用户给这个实例起的显示名"的字段名。
///
/// NOTE: 文件里叫 `name`，但 VersionDisplayParts 里那个成员叫 `customName`。
///       这里写、displayParts() 那边读，**两边必须是同一个串** —— 写错一个字不会报错，
///       表现是"改完名界面上没反应"。改的时候两边一起改（那边在 displayParts() 的
///       readString("name")）。
const QString kDisplayNameField = QStringLiteral("name");

/// 一个新的稳定实例 id。只有这一处生成它，免得"生成规则"在改名的路径上被抄第二遍。
QString newInstanceId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

/// 把 from 改名成 to。from 本来就不在时**当成功**（可选文件：json、meta 都可能没有）。
///
/// NOTE: 目标已存在时**拒绝**，不能直接交给 QFile::rename —— 它在 Windows 上走的是
///       MoveFileEx + MOVEFILE_REPLACE_EXISTING，会**静默盖掉**目标，那是丢数据。
///       唯一的例外是只差大小写：那种"目标已存在"其实是同一个文件（Windows 的路径
///       比较本来就不区分大小写），而改显示大小写是合法操作。
bool renameOnto(const QString &from, const QString &to) {
    if (!QFile::exists(from)) {
        return true;
    }
    if (from.compare(to, Qt::CaseInsensitive) != 0 && QFile::exists(to)) {
        return false;
    }
    return QFile::rename(from, to);
}

}  // namespace

VersionManager::VersionManager(QObject *parent)
    : QObject(parent), downloadManager(new DownloadManager(this)) {
    connect(downloadManager, &DownloadManager::versionManifestFetched, this,
            &VersionManager::onVersionManifestFetched);
    connect(downloadManager, &DownloadManager::versionJsonFetched, this,
            &VersionManager::onVersionJsonFetched);
    // NOTE: 这里以前还接了一个 downloadFinished -> onDownloadFinished，把每个文件的
    // 完成都转发成 versionDownloadFinished，于是下载一个版本会在日志里刷出几千行
    // 一模一样的话。现在只认整批结束的信号。
    connect(downloadManager, &DownloadManager::allDownloadsFinished, this,
            &VersionManager::onAllDownloadsFinished);
    connect(downloadManager, &DownloadManager::batchProgress, this,
            [this](int done, int total, int failed, const QString &currentFile) {
                emit versionDownloadStats(done, total, failed, currentFile);
            });
}

VersionManager::~VersionManager() {
    // DownloadManager is automatically deleted as child
}

QStringList VersionManager::installedVersions(QStringList *warnings) const {
    QStringList versions;

    // Scan for installed versions in .minecraft/versions
    QString versionsPath = AppPaths::versionsDir();
    QDir versionsDir(versionsPath);

    if (versionsDir.exists()) {
        QStringList versionDirs = versionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &versionDir : versionDirs) {
            // 判据是"认得出客户端 jar 在不在"，**不再要求 jar 名和目录名一致** ——
            // 放宽的来龙去脉见 InstanceFiles.hpp 的块注释（一句话：用户自己在资源管理器里
            // 改过目录名的实例，以前会从列表里静默消失，真机上 388 MB / 851 MB 各一个）。
            //
            // 判据没有一路放松到"目录里有任意 .jar 就算已安装"：那样"顶层两个 jar、
            // 认不出哪个是客户端"的目录也会被收进来，点启动时才发现找不着 jar。
            // 宁可在这里说不认识，也不能让列表里躺着一个点了会炸的条目。
            const InstanceFiles::Resolved resolved = InstanceFiles::resolveInstance(versionDir);
            if (resolved.recognized()) {
                versions.append(versionDir);
                continue;
            }
            // 认不出来的目录里**有 jar** 就得说一声：用户看得见磁盘上确实有个 jar，
            // 却看不见它在列表里，不讲清楚就是又一次静默消失。
            // 一个 jar 都没有的目录（下载残骸、纯空目录、误丢进 versions/ 的整合包 zip）
            // 继续静默 —— 那些本来就没人指望它是实例，报了才知道是噪音。
            if (warnings != nullptr && !resolved.jarCandidates.isEmpty()) {
                warnings->append(tr("目录 %1 里有 %2 个 jar（%3），认不出哪个是客户端，已忽略。")
                                     .arg(versionDir)
                                     .arg(resolved.jarCandidates.size())
                                     .arg(resolved.jarCandidates.join(QStringLiteral("、"))));
            }
        }
    }

    // NOTE: 这里以前在没有任何已安装版本时会塞一个 "自定义版本" 进去，好让旧版
    // QListWidget 不至于空着。那是界面层的占位职责，混进数据层之后调用方还得反过来
    // 判 versionId == "自定义版本" 才能把它拒掉（MainWindow 里写了两处）。
    // 现在空就是空：界面用下拉框的 placeholderText 表达"没有可选版本"。
    //
    // NOTE: versions/ 里除了真正的版本目录，还可能有用户随手丢进去的整合包 .zip
    // （真机 4 个、共 935 MB）和纯空目录（真机有个 0 字节的 `26.2hyperland`）。
    // 上面的 QDir::Dirs 过滤 + <id>.jar 判据已经把这两类都正确排除了，jars 为空也就
    // 不会上报 —— 别为了"看起来更宽松"把 .zip 也当版本收进来。
    return versions;
}

QStringList VersionManager::availableVersions() const {
    QStringList versions;
    for (const MinecraftVersion &version : downloadManager->getAvailableVersions()) {
        versions.append(version.id);
    }
    return versions;
}

void VersionManager::fetchAvailableVersions() { downloadManager->fetchVersionManifest(); }

void VersionManager::fetchVersionDetails(const QString &versionId) {
    m_pendingVersionId = versionId;
    if (downloadManager->getAvailableVersions().isEmpty()) {
        // 列表还没拉过：先拉列表，续接在 onVersionManifestFetched 里做。
        // NOTE: 这里以前是临时 connect 一个 lambda、在里面 disconnect(conn)，
        // 而 conn 是局部变量、被按引用捕获 —— 信号在函数返回之后才发，那句
        // disconnect 读的是已经失效的栈内存，实测直接段错误。改成记在成员里。
        m_fetchJsonAfterManifest = versionId;
        downloadManager->fetchVersionManifest();
    } else {
        downloadManager->fetchVersionJson(versionId);
    }
}

bool VersionManager::isDownloading() const { return downloadManager->isBusy(); }

void VersionManager::setUseMirror(bool useMirror) { downloadManager->setUseMirror(useMirror); }

void VersionManager::downloadVersion(const QString &versionId, const QString &destinationPath) {
    // NOTE: 这两道早退必须在 beginBatch() 之前。beginBatch() 与 endBatch() 之间
    // 不允许有任何提前 return —— 一旦漏掉 endBatch()，m_batchOpen 就永远是 true，
    // allDownloadsFinished 永不发射，UI 会一直停在"下载中"、按钮永久变灰。
    if (!versionJsons.contains(versionId)) {
        qWarning() << "Version JSON not available for" << versionId << ". Fetch details first.";
        // 也必须给 UI 一个终态，否则它会一直等这批下载结束。
        emit versionDownloadFinished(versionId, false, tr("尚未获取到该版本的 version.json。"));
        return;
    }
    if (downloadManager->isBusy()) {
        qWarning() << "A download batch is already running, refusing to start another.";
        emit versionDownloadFinished(versionId, false, tr("已有下载任务正在进行，请等它结束。"));
        return;
    }

    m_currentVersionDownloading = versionId;
    downloadManager->beginBatch();

    const QJsonObject &versionJson = versionJsons[versionId];
    const QString basePath = AppPaths::minecraftDir();

    // 把 version.json 落盘。缺了它，下一次启动时 getVersionJson() 取不到
    // javaVersion / assetIndex，会静默退回错误的 Java 版本和资产索引 ——
    // 26.1 需要 Java 25，退成 Java 21 就是 UnsupportedClassVersionError。
    if (QDir().mkpath(destinationPath)) {
        const QString jsonPath = destinationPath + "/" + versionId + ".json";
        QFile jsonFile(jsonPath);
        if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            jsonFile.write(QJsonDocument(versionJson).toJson(QJsonDocument::Indented));
            jsonFile.close();
            qDebug() << "Saved version JSON to" << jsonPath;
        } else {
            qWarning() << "Failed to save version JSON to" << jsonPath
                       << jsonFile.errorString();
        }
    }

    // Download client jar
    if (versionJson.contains("downloads") && versionJson["downloads"].isObject()) {
        const QJsonObject downloads = versionJson["downloads"].toObject();
        if (downloads.contains("client") && downloads["client"].isObject()) {
            const QJsonObject client = downloads["client"].toObject();
            const QString url = client["url"].toString();
            const QString sha1 = client["sha1"].toString();
            const qint64 size = client["size"].toVariant().toLongLong();

            const QString jarPath = destinationPath + "/" + versionId + ".jar";
            // 带上 size：重新点下载时先比字节数，对得上才算"已经下过"，
            // 省掉一次 41 MB 的全文件哈希。
            // 版本号要一起传：镜像源下客户端 jar 是按版本号取的，不是按 sha1。
            downloadManager->downloadClientJar(versionId, url, jarPath, sha1, size);
        }
    }

    // Download libraries
    // NOTE: 库走 basePath（= versions/<id>），因为 versions/<id>/<id>.jar 和它旁边的
    //       那些文件都是"这个版本自己的"；资源（assets）不一样，是全体共用的，
    //       所以那边的目录由 AppPaths::assetsDir() 说了算，不跟着 basePath 走。
    QList<Library> libraries = downloadManager->extractLibraries(versionJson);
    downloadManager->downloadLibraries(libraries, basePath + "/libraries");

    // Download assets
    AssetIndex assetIndex = downloadManager->extractAssetIndex(versionJson);
    downloadManager->downloadAssets(assetIndex);

    downloadManager->endBatch();
}

QJsonObject VersionManager::getVersionJson(const QString &versionId) const {
    const auto cached = versionJsons.constFind(versionId);
    if (cached != versionJsons.constEnd()) {
        return cached.value();
    }

    // 内存里没有就回读磁盘。下载时已经把 JSON 落盘，所以跨会话启动也能拿到
    // javaVersion 和 assetIndex，不需要先在同一次会话里点一次"下载"。
    //
    // NOTE: 路径走 InstanceFiles 解析，不按 <id>.json 硬拼 —— 目录名被外部改过之后，
    //       里面的 json 还叫老名字，硬拼会读不到，表现就是"实例认出来了、启动却说
    //       版本信息缺失"。认出来和还能启动是两件事。
    const QString jsonPath = InstanceFiles::jsonPath(versionId);
    QFile jsonFile(jsonPath);
    if (!jsonFile.exists() || !jsonFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Version JSON not found on disk:" << jsonPath;
        return QJsonObject();
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
    jsonFile.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Failed to parse" << jsonPath << ":" << parseError.errorString();
        return QJsonObject();
    }

    // 填回缓存，同一次会话里不再重复读盘
    versionJsons[versionId] = doc.object();
    return doc.object();
}

QJsonObject VersionManager::readVersionMeta(const QString &versionId, QString *metaPathOut,
                                            bool *unreadableOut) const {
    const QString metaPath = AppPaths::versionMetaPath(versionId);
    if (metaPathOut != nullptr) {
        *metaPathOut = metaPath;
    }
    if (unreadableOut != nullptr) {
        *unreadableOut = false;
    }

    QFile metaFile(metaPath);
    if (!metaFile.exists()) {
        // 绝大多数版本没有这个文件，不是异常，也别打日志——refreshInstalledVersions()
        // 会对着每个已安装版本调一次，打日志就是刷屏。
        return QJsonObject();
    }
    if (!metaFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Version metadata exists but cannot be opened:" << metaPath;
        if (unreadableOut != nullptr) {
            *unreadableOut = true;
        }
        return QJsonObject();
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &parseError);
    metaFile.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // 手写的文件写错格式很常见。整个文件当没有，但不静默——这是用户手写的，
        // 不告诉他写错了他会一直以为生效了。
        qWarning() << "Failed to parse version metadata" << metaPath << ":" << parseError.errorString();
        if (unreadableOut != nullptr) {
            *unreadableOut = true;
        }
        return QJsonObject();
    }
    return doc.object();
}

bool VersionManager::updateVersionMeta(const QString &versionId, const QJsonObject &fields,
                                       QString *errorOut) {
    const QString metaPath = AppPaths::versionMetaPath(versionId);

    // 先把现有的读进来当底，再往上盖要改的键 —— **合并**写，不整体替换。
    // 这个文件是用户可以手写的，里面可能已经有 jvmArgs / nameFormat / realVersion，
    // 整体覆盖会把它们悄悄抹掉。
    bool unreadable = false;
    QJsonObject merged = readVersionMeta(versionId, nullptr, &unreadable);
    if (unreadable) {
        // 往一份读不懂的内容上合并 = 把用户手写的部分丢掉。宁可这次不写。
        if (errorOut != nullptr) {
            *errorOut = tr("%1 的内容不是合法的 JSON，为避免覆盖掉你写的东西，这次没有改动它。")
                            .arg(metaPath);
        }
        return false;
    }

    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        if (it.value().isNull() || it.value().isUndefined()) {
            // 约定：null 表示**删掉**这个键（"改回跟随全局"要的就是"这个键不存在"）。
            merged.remove(it.key());
        } else {
            merged.insert(it.key(), it.value());
        }
    }

    // 目录理论上一定在（扫描是先认 jar 的），但重命名那条路会在目录刚建好时调过来，
    // 建不出来就没什么好写的了。
    if (!QDir().mkpath(AppPaths::versionDir(versionId))) {
        if (errorOut != nullptr) {
            *errorOut = tr("无法创建目录 %1。").arg(AppPaths::versionDir(versionId));
        }
        return false;
    }

    // QSaveFile：先写同目录的临时文件、commit() 时才改名覆盖。断电或崩溃只会留下
    // "旧内容完好"，绝不会留下半截 JSON —— 这个文件坏掉的后果是那个实例的显示名和
    // 参数整体失效，比"这次没写成"糟得多。
    //
    // 缩进输出：这是用户会拿记事本打开看的文件，紧排一行没法读。代价是重写会把
    // 用户原来的排版统一掉，可以接受。
    QSaveFile file(metaPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorOut != nullptr) {
            *errorOut = tr("无法写入 %1。").arg(metaPath);
        }
        return false;
    }
    file.write(QJsonDocument(merged).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorOut != nullptr) {
            *errorOut = tr("写入 %1 失败。").arg(metaPath);
        }
        return false;
    }
    return true;
}

QString VersionManager::instanceId(const QString &versionId) const {
    const QJsonValue value = readVersionMeta(versionId).value(kInstanceIdField);
    // 类型不对就当没有（可能是用户手改坏的）—— 调用方会去生成一个新的覆盖掉它。
    return value.isString() ? value.toString() : QString();
}

QString VersionManager::ensureInstanceId(const QString &versionId) {
    const QString existing = instanceId(versionId);
    if (!existing.isEmpty()) {
        return existing;
    }

    // QUuid 的 WithoutBraces 形式（36 字符带横线）。用它而不是自增整数：分组归属
    // 存在另一个文件里，自增 id 在"删了一个组再加一个"时会和残留的归属撞上，
    // 而 UUID 不会。
    const QString generated = newInstanceId();

    QJsonObject fields;
    fields.insert(kInstanceIdField, generated);
    QString error;
    if (!updateVersionMeta(versionId, fields, &error)) {
        // 走到这里说明磁盘有问题（目录不可写之类）。返回空串让调用方知道"没拿到"，
        // 而不是返回一个**没落盘**的 id —— 那个 id 下次会话就对不上了，属于静默错。
        qWarning() << "Failed to persist instance id for" << versionId << ":" << error;
        return QString();
    }
    return generated;
}

bool VersionManager::renameInstance(const QString &oldId, const QString &newName,
                                    QString *errorOut) {
    const auto fail = [errorOut](const QString &reason) {
        if (errorOut != nullptr) {
            *errorOut = reason;
        }
        return false;
    };

    const QString newId = newName.trimmed();

    // ---------- 第 1 步：全部前置检查，磁盘一个字节都不动 ----------
    //
    // "正在跑"不在这里判，见 .hpp 里的 NOTE —— 那是 MainWindow 的闸口，必须更早。
    const QString problem = InstanceFiles::validateRenameTarget(newId, oldId);
    if (!problem.isEmpty()) {
        return fail(problem);
    }

    const QString oldDir = AppPaths::versionDir(oldId);
    if (!QDir(oldDir).exists()) {
        return fail(tr("找不到实例目录 %1。").arg(QDir::toNativeSeparators(oldDir)));
    }
    // NOTE: 这里**不判** `QDir(newDir).exists()`。目标已经存在的情况
    //       validateRenameTarget() 里按重名挡过了，而它挡不了、这里也不该挡的正是
    //       "只差大小写"那一种 —— 那种目标就是它自己，判存在会把唯一合法的改名拒掉。
    const QString newDir = AppPaths::versionDir(newId);

    // 改名要回滚时得把显示名放回去，所以先把老值记下来（可能是"压根没有"）。
    const QJsonValue oldDisplayName = readVersionMeta(oldId).value(kDisplayNameField);

    // ---------- 第 2 步：写 meta ----------
    //
    // 排在改名字前面：它可能失败，而它失败时我们还什么都没破坏；反过来先改目录名，
    // 写完 meta 却失败，就留下一个改完了名、显示名还指着老名字的实例。
    // 顺手把稳定 id 定下来（没有才生成）—— 这一句正是"被启动器碰过一次"的那一次，
    // 从今往后这个实例的分组归属不再靠目录名认人。两件事合成一次原子写。
    QJsonObject fields;
    fields.insert(kDisplayNameField, newId);
    if (instanceId(oldId).isEmpty()) {
        fields.insert(kInstanceIdField, newInstanceId());
    }
    QString error;
    if (!updateVersionMeta(oldId, fields, &error)) {
        return fail(error);
    }

    // ---------- 第 3~5 步：jar、json、目录，从里往外改 ----------
    const QString oldJarPath = oldDir + QLatin1Char('/') + InstanceFiles::jarName(oldId);
    const QString newJarPath = oldDir + QLatin1Char('/') + newId + QStringLiteral(".jar");
    const QString oldJsonPath = oldDir + QLatin1Char('/') + InstanceFiles::jsonName(oldId);
    const QString newJsonPath = oldDir + QLatin1Char('/') + newId + QStringLiteral(".json");

    // 只回滚**真做成了**的那几步。两个标志必须在回滚 lambda 之前声明，好让它捕获。
    bool jarRenamed = false;
    bool jsonRenamed = false;

    /// 逆序回滚，然后把"磁盘现在到底怎么样了"告诉用户。
    ///
    /// 回滚本身也可能失败（文件被别的程序开着，正是刚才那一步失败的原因）。那时
    /// 绝不能只说一句"失败了" —— 用户需要知道**哪些东西还停在半路上**，否则他
    /// 在资源管理器里看到的是一个自相矛盾的目录，而且不知道该不该再点一次。
    const auto rollbackAndFail = [&](const QString &reason) {
        QStringList stuck;
        if (jsonRenamed && !QFile::rename(newJsonPath, oldJsonPath)) {
            stuck.append(QDir::toNativeSeparators(newJsonPath));
        }
        if (jarRenamed && !QFile::rename(newJarPath, oldJarPath)) {
            stuck.append(QDir::toNativeSeparators(newJarPath));
        }
        // 显示名跟着回滚：原来是什么就写回什么，原来没有这个键就删掉它。
        // 稳定 id **留着** —— 改名失败不改变"它是哪个实例"，而 id 正是用来扛这个的。
        QJsonObject restore;
        restore.insert(kDisplayNameField, oldDisplayName.isString()
                                              ? QJsonValue(oldDisplayName.toString())
                                              : QJsonValue(QJsonValue::Null));
        QString metaError;
        if (!updateVersionMeta(oldId, restore, &metaError)) {
            stuck.append(QDir::toNativeSeparators(AppPaths::versionMetaPath(oldId)));
        }

        if (stuck.isEmpty()) {
            return fail(tr("%1\n\n磁盘上的东西已经全部改回去了，可以直接重试。").arg(reason));
        }
        return fail(tr("%1\n\n自动回滚也没能做完，下面这些还停在半路上，需要你手动看一眼：\n%2")
                        .arg(reason, stuck.join(QStringLiteral("\n"))));
    };

    if (!renameOnto(oldJarPath, newJarPath)) {
        return rollbackAndFail(tr("无法把 %1 改名成 %2（可能有别的程序正开着它）。")
                                   .arg(QDir::toNativeSeparators(oldJarPath),
                                        QDir::toNativeSeparators(newJarPath)));
    }
    jarRenamed = true;

    if (!renameOnto(oldJsonPath, newJsonPath)) {
        return rollbackAndFail(tr("无法把 %1 改名成 %2（可能有别的程序正开着它）。")
                                   .arg(QDir::toNativeSeparators(oldJsonPath),
                                        QDir::toNativeSeparators(newJsonPath)));
    }
    jsonRenamed = true;

    if (!QDir().rename(oldDir, newDir)) {
        return rollbackAndFail(tr("无法把目录 %1 改名成 %2（可能有别的程序正开着它）。")
                                   .arg(QDir::toNativeSeparators(oldDir),
                                        QDir::toNativeSeparators(newDir)));
    }

    return true;
}

QString VersionManager::jvmArgsField() { return QStringLiteral("jvmArgs"); }

QJsonObject VersionManager::versionMeta(const QString &versionId) const {
    // readVersionMeta() 已经把所有"读不到"的形态（文件不在 / 打不开 / 解析不了）
    // 收口成空对象了，这里不再重复判断 —— 那些分支的措辞和告警都只该有一处。
    return readVersionMeta(versionId);
}

QString VersionManager::skipJavaVersionWarningField() {
    return QStringLiteral("skipJavaVersionWarning");
}

bool VersionManager::skipJavaVersionWarning(const QString &versionId) const {
    QString metaPath;
    const QJsonObject meta = readVersionMeta(versionId, &metaPath);
    if (meta.isEmpty()) {
        return false;
    }

    const QJsonValue value = meta.value(skipJavaVersionWarningField());
    if (value.isUndefined() || value.isNull()) {
        // 没写过这个键 = 没表过态 = 提醒。
        return false;
    }
    if (!value.isBool()) {
        // 写成字符串/数字：按"没表态"处理，但要说一声（见头文件里那条 NOTE）。
        qWarning() << "Version metadata field skipJavaVersionWarning is not a bool in" << metaPath
                   << "— treated as \"warn\"";
        return false;
    }
    return value.toBool();
}

QStringList VersionManager::versionJvmArgs(const QString &versionId) const {
    QString metaPath;
    const QJsonObject meta = readVersionMeta(versionId, &metaPath);
    if (meta.isEmpty()) {
        return QStringList();
    }

    const QJsonValue value = meta.value(jvmArgsField());
    if (value.isUndefined() || value.isNull()) {
        return QStringList();
    }
    // 用数组而不是 PCL 那种空格分隔的字符串：JVM 参数本身可能含空格
    // （`-Dfoo=bar baz` 这种），切成数组无歧义。
    if (!value.isArray()) {
        qWarning() << "Version metadata field jvmArgs is not an array in" << metaPath
                   << "— ignored";
        return QStringList();
    }

    // 元素类型不对就跳过那一个、并说一声 —— 和 readString 对类型错误的态度一致：
    // 这是用户手写的文件，写错了必须告诉他，否则表现成"明明写了却不生效"。
    QStringList args;
    const QJsonArray entries = value.toArray();
    for (const QJsonValue &entry : entries) {
        if (!entry.isString()) {
            qWarning() << "Version metadata jvmArgs has a non-string entry in" << metaPath
                       << "— skipped";
            continue;
        }
        const QString arg = entry.toString();
        if (!arg.isEmpty()) {
            args.append(arg);
        }
    }
    return args;
}

VersionDisplayParts VersionManager::displayParts(const QString &versionId) const {
    // id 永远用参数填，不从文件里取：元数据文件是可以被用户手改的，而 id 是真正
    // 进 --version 和文件路径的那个值，必须由调用方（本地扫描 / 在线清单）决定。
    VersionDisplayParts parts;
    parts.id = versionId;

    QString metaPath;
    const QJsonObject meta = readVersionMeta(versionId, &metaPath);
    if (meta.isEmpty()) {
        return parts;
    }

    // 逐字段读。类型不对时给一句告警：QJsonValue::toString() 对数字/对象会安静地
    // 返回空串，手写 JSON 时把 "name" 写成数字是最容易犯的错，不提示的话表现成
    // "明明写了却不生效"。
    const auto readString = [&meta, &metaPath](const char *key) -> QString {
        const QJsonValue value = meta.value(QLatin1String(key));
        if (value.isUndefined() || value.isNull()) {
            return QString();
        }
        if (!value.isString()) {
            qWarning() << "Version metadata field" << key << "is not a string in" << metaPath
                       << "— ignored";
            return QString();
        }
        return value.toString();
    };

    parts.customName = readString("name");
    parts.realVersion = readString("realVersion");
    parts.loaderName = readString("loaderName");
    parts.loaderVersion = readString("loaderVersion");

    // 预留：按实例覆盖全局的显示格式。现在没有任何代码会写它，但读的这一侧先接好，
    // 以后做"分开实例管理"时只要往 wtfml.meta 里写 "nameFormat" 就能生效。
    //
    // 语义是**整份替换**：没写的字段取 VersionNameOptions 的 struct 默认值，而不是取
    // 全局设置里的值。理由是这个类在数据层，看不见 LauncherSettings，为了一个还没人写的
    // 字段去建立这条依赖不划算；而且全局那四个开关今天恒等于默认值，两条语义在当前
    // 完全等价。TODO: 以后真做"逐字段覆盖全局"时，得把全局 options 作为参数传进来。
    // 四个开关全缺省时不构造 override，免得一个空对象把全局设置顶掉。
    const QJsonValue nameFormat = meta.value(QLatin1String("nameFormat"));
    if (nameFormat.isObject()) {
        const QJsonObject formatObj = nameFormat.toObject();
        VersionNameOptions overrideOptions;
        bool anySet = false;
        const auto readFlag = [&formatObj, &anySet](const char *key, bool &target) {
            const QJsonValue value = formatObj.value(QLatin1String(key));
            if (value.isBool()) {
                target = value.toBool();
                anySet = true;
            }
        };
        readFlag("showCustomName", overrideOptions.showCustomName);
        readFlag("showRealVersion", overrideOptions.showRealVersion);
        readFlag("showLoaderName", overrideOptions.showLoaderName);
        readFlag("showLoaderVersion", overrideOptions.showLoaderVersion);
        if (anySet) {
            parts.nameFormatOverride = overrideOptions;
        }
    } else if (!nameFormat.isUndefined() && !nameFormat.isNull()) {
        qWarning() << "Version metadata field nameFormat is not an object in" << metaPath
                   << "— ignored";
    }

    // NOTE: 这里刻意**不缓存**。getVersionJson() 能缓存是因为版本 JSON 一旦下载就不再变，
    // 而这个文件会被"重命名"功能改写——缓存就会变成"改了名不生效"的 bug。
    // 开销不是不缓存的理由：调用方 refreshInstalledVersions() 每个已安装版本都要过这里
    // 一遍，而它一次会话会被调很多次且**没有上界** —— 构造函数、版本清单到达、下载完成，
    // 以及每勾一下「版本名格式」的四个勾选框（onVersionNameOptionChanged）都会重跑。
    // 每次每个版本一个 exists() 加（有文件时才有的）几百字节读取；真机上实例数可以到
    // 61 个，也仍然是可忽略的量级 —— 所以别再拿"数量少"当论据，真正的理由是上面那条。
    return parts;
}

void VersionManager::onVersionManifestFetched(const QList<MinecraftVersion> &versions) {
    QStringList versionIds;
    for (const MinecraftVersion &version : versions) {
        versionIds.append(version.id);
    }
    emit versionsFetched(versionIds);

    if (m_fetchJsonAfterManifest.isEmpty()) {
        return;
    }
    const QString pending = m_fetchJsonAfterManifest;
    m_fetchJsonAfterManifest.clear();
    // 列表里没有这个版本（或者列表压根没拉到）时，fetchVersionJson 会立刻回一个
    // 空 JSON，由 onVersionJsonFetched 转成终态信号 —— 请求方总能拿到结果。
    downloadManager->fetchVersionJson(pending);
}

void VersionManager::onVersionJsonFetched(const QJsonObject &versionJson) {
    if (versionJson.contains("id")) {
        QString versionId = versionJson["id"].toString();
        versionJsons[versionId] = versionJson;
        m_pendingVersionId.clear();
        emit versionDetailsFetched(versionId, versionJson);
        return;
    }

    // 拿不到版本 JSON（manifest 里没有这个版本，或者网络失败）：这里必须回一个终态。
    // 静默丢弃会让 UI 一直停在"正在获取版本信息"，而按钮已经被禁用 ——
    // 那就成了死界面。
    if (m_pendingVersionId.isEmpty()) {
        qDebug() << "Ignored a version JSON reply that nobody asked for.";
        return;
    }
    const QString pending = m_pendingVersionId;
    m_pendingVersionId.clear();
    qWarning() << "Failed to fetch version JSON for" << pending;
    emit versionDetailsFetched(pending, QJsonObject());
}

void VersionManager::onAllDownloadsFinished(bool success) {
    const DownloadStats stats = downloadManager->stats();

    QString message;
    if (success) {
        message = stats.skipped > 0
                      ? tr("%1 个文件已全部就绪（其中 %2 个本来就存在，已跳过）。")
                            .arg(stats.total)
                            .arg(stats.skipped)
                      : tr("%1 个文件已全部就绪。").arg(stats.total);
    } else {
        // 失败清单只列前 10 项，剩下的折成一行 —— 5000 个 asset 全列出来没人看。
        const QStringList shown = stats.failedPaths.mid(0, 10);
        QString list = shown.join(QLatin1Char('\n'));
        if (stats.failedPaths.size() > shown.size()) {
            list += QLatin1Char('\n') +
                    tr("……还有 %1 个").arg(stats.failedPaths.size() - shown.size());
        }
        message = tr("%1 个文件下载失败：").arg(stats.failed) + QLatin1Char('\n') + list;
    }

    // 一批只发一次。改动前这里由每个文件的 downloadFinished 触发，
    // 下载一个版本会在日志里刷出几千行一模一样的话。
    emit versionDownloadFinished(m_currentVersionDownloading, success, message);
}
