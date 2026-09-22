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

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QSet>
#include <QUrl>
#include <QUuid>

#include "../download/LoaderMeta.hpp"
#include "../launcher/VersionInheritance.hpp"
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

/// Forge / NeoForge 的安装器那个 Java 进程最多跑多久。**不是**"失败得快一点"，
/// 是"别让界面永远停在忙态"（那个进程不结束，界面就一直禁着按钮）。
///
/// 实测 Forge 47.4.10 在一个空目录上装一次：100 秒上下（下 29 个库 + 把原版的
/// 客户端 jar 打两遍补丁）。慢网、机械盘上几分钟很正常，所以给得宽松 —— 三十分钟
/// 还没结束，那不是慢，那是卡住了（或者弹了个看不见的对话框在等人点）。
constexpr int kInstallerTimeoutMs = 30 * 60 * 1000;

/// 安装器跑着的时候，每隔这么多秒报一次"还在跑"。
///
/// 安装器的输出里绝大部分是 `Patching xxx 1/1` 这种**逐文件**的行，那些不该进日志
/// （进度归进度条，见产品约定）。但一声不吭地等三分钟也不像话 —— 用户会以为卡死了。
/// 折中是：一行都不逐条转，但每 20 秒报一次"还在跑 + 它最后打的那一行"。
constexpr int kInstallerHeartbeatSeconds = 20;

/// 安装器输出的尾巴留多少字节。失败时要报"卡在哪一行"，但没必要把几万行都攥在内存里。
constexpr int kInstallerOutputKeepBytes = 64 * 1024;

/// 心跳和超时都靠这个定时器，间隔固定 1 秒。
constexpr int kInstallerTickMs = 1000;

/// 把安装器的输出切成行（两个通道混在一起，所以 `\r` 和 `\n` 都当分隔符 ——
/// 安装器里那些 "Patching xxx 1/1" 是靠 `\r` 原地刷的）。
QStringList installerOutputLines(const QByteArray &output) {
    return QString::fromLocal8Bit(output).split(
        QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
}

/// 安装器输出的**最后几行**（失败时贴给用户看，是"卡在哪"的唯一线索）。
///
/// 上限是刻意的：装到一半失败时它可能已经打了几千行 "Patching ... 1/1"，全贴出来
/// 只会把真正的那一行淹掉。最后几行才是有信息的那几行。
QString installerOutputTail(const QByteArray &output, int maxLines = 12) {
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
///       已经存在的版本**（用户重装、或者在一个装过 Forge 的版本上换加载器版本），
///       那这个 id 早就被读进缓存了 —— 走缓存就等于拿**安装之前**那份 json 去核对
///       安装之后的产物，核对永远能过，白核。
QJsonObject readVersionJsonAt(const QString &path) {
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

// ---------------------------------------------------------------------------
// 带模组加载器的版本（今天只有 Fabric 接通了）
// ---------------------------------------------------------------------------

bool VersionManager::loaderInstallSupported(LoaderKind kind) {
    // Fabric 是"拉 json + 下库"，Forge/NeoForge 是"另起一个 Java 进程跑它们的安装器"
    // —— 两条完全不同的路，但都接通了，所以这里一样返回 true（这个函数只说"能不能装"）。
    //
    // OptiFine 还没做，理由不是"懒"：它的安装器 main(String[]) **把命令行参数整个丢掉**
    // （实测反汇编：`Utils.getWorkingDirectory()` 只认 `%APPDATA%/.minecraft`），
    // 所以只能给子进程覆盖 APPDATA、再把那个假的 .minecraft 用**目录联接**指到真游戏目录；
    // 而且它在出错时会弹一个 Swing 对话框、**进程因此永不退出**（实测跑满 120 秒被杀）。
    // 这条路要单独做（连接联的删除都得走 rmdir，不能递归删 —— 删错一步就是用户真实的
    // .minecraft），见 README 的待办。
    return kind == LoaderKind::Fabric || kind == LoaderKind::Forge || kind == LoaderKind::NeoForge;
}

QString VersionManager::loaderDisplayName(LoaderKind kind) {
    switch (kind) {
    case LoaderKind::Fabric:
        return QStringLiteral("Fabric");
    case LoaderKind::Forge:
        return QStringLiteral("Forge");
    case LoaderKind::NeoForge:
        return QStringLiteral("NeoForge");
    case LoaderKind::OptiFine:
        return QStringLiteral("OptiFine");
    }
    return QString();
}

int VersionManager::loaderKindCount() { return 4; }

bool VersionManager::loaderKindFromIndex(int index, LoaderKind *out) {
    // 顺序**就是** MainWindowUI::modLoaderOptionNames() 的顺序（那张表里下标 0..3 是四个
    // 加载器）。界面上摆控件的顺序和这里翻译的顺序对不上时不会报错，只会"勾了 Fabric
    // 装出 Forge" —— 所以这张表只有这一处翻译，而且 verify 里按四个名字逐项对过
    // （名字取的是界面那两张表，不是这里抄的第二份字面量）。
    static const LoaderKind kOrder[] = {LoaderKind::Fabric, LoaderKind::Forge,
                                        LoaderKind::NeoForge, LoaderKind::OptiFine};
    constexpr int kCount = static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0]));
    static_assert(kCount == 4, "加载器多了一个/少了一个，界面那两张表也要跟着改");
    if (index < 0 || index >= kCount) {
        return false;
    }
    if (out != nullptr) {
        *out = kOrder[index];
    }
    return true;
}

bool VersionManager::isLoaderInstalling() const { return m_loaderInstall.active(); }

void VersionManager::installLoaderVersion(const QString &gameVersion, LoaderKind kind,
                                         const QString &javaPath) {
    if (m_loaderInstall.active()) {
        qWarning() << "A loader install is already running, refusing to start another.";
        emit loaderInstallMessage(tr("已经有一个加载器正在安装，等它结束再试。"));
        return;
    }
    if (!loaderInstallSupported(kind)) {
        // 界面那边也会拦下来并弹一个"还没做"，这里再兜一道：数据层不该被绕过去，
        // 否则症状是"点了某个还没接通的加载器，结果装出来一个纯原版"。
        emit loaderInstallMessage(
            tr("%1 的安装还没接通，现在只有 Fabric / Forge / NeoForge 能用。")
                .arg(loaderDisplayName(kind)));
        return;
    }
    if (downloadManager->isBusy()) {
        emit loaderInstallMessage(tr("已有下载任务正在进行，请等它结束。"));
        return;
    }

    m_loaderInstall = LoaderInstall();
    m_loaderInstall.kind = kind;
    m_loaderInstall.gameVersion = gameVersion;
    m_loaderInstall.loaderName = loaderDisplayName(kind);
    m_loaderInstall.javaPath = javaPath;

    // 两条路从第一步就分开：Fabric 拉的是"加载器版本列表"，另外两个拉的是
    // "安装器该用哪个版本"，接口完全不是一回事（见 LoaderMeta.hpp 顶部那段）。
    if (kind == LoaderKind::Fabric) {
        m_loaderInstall.phase = LoaderInstall::Phase::FetchingList;
        emit loaderInstallMessage(tr("正在获取 %1 %2 的加载器版本列表...")
                                      .arg(m_loaderInstall.loaderName, gameVersion));
        fetchFabricLoaderList();
        return;
    }

    m_loaderInstall.phase = LoaderInstall::Phase::FetchingInstallerList;
    emit loaderInstallMessage(
        tr("正在获取 %1 %2 该用哪个版本...").arg(m_loaderInstall.loaderName, gameVersion));
    fetchInstallerList();
}

void VersionManager::fetchFabricLoaderList() {
    if (loaderNetwork == nullptr) {
        loaderNetwork = new QNetworkAccessManager(this);
    }
    const QUrl url(LoaderMeta::fabricLoaderListUrl(m_loaderInstall.gameVersion));
    QNetworkRequest request(url);
    // 和 DownloadManager 那几处元数据请求同一个超时：卡死的连接会让界面永远停在
    // "下载中"（按钮是禁用的），而这个请求没有别的兜底。
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onFabricLoaderListReply(reply); });
}

void VersionManager::onFabricLoaderListReply(QNetworkReply *reply) {
    reply->deleteLater();
    // 状态已经换过（上一次失败收了尾、或者用户又点了一次）：这份回复没人要了。
    if (m_loaderInstall.phase != LoaderInstall::Phase::FetchingList) {
        return;
    }

    const QByteArray body = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (reply->error() != QNetworkReply::NoError || !doc.isArray()) {
        failLoaderInstall(tr("拿不到 %1 的加载器列表（%2）。检查网络后重试。")
                              .arg(m_loaderInstall.gameVersion, reply->errorString()));
        return;
    }

    const LoaderMeta::LoaderPick pick = LoaderMeta::pickFabricLoader(doc.array());
    if (!pick.ok()) {
        // 快照、或者比 Fabric 还早的版本：接口会回一个空数组。这不是网络问题，
        // 也不能退回"装原版"——用户要的是带加载器的版本，装出来一个纯原版是骗人。
        failLoaderInstall(tr("Fabric 还没有支持 %1 的加载器（拿到的列表是空的）。")
                              .arg(m_loaderInstall.gameVersion));
        return;
    }

    m_loaderInstall.loaderVersion = pick.version;
    m_loaderInstall.loaderId =
        LoaderMeta::fabricLoaderId(m_loaderInstall.gameVersion, pick.version);
    emit loaderInstallMessage(tr("挑中 %1 %2（%3）。")
                                  .arg(m_loaderInstall.loaderName, pick.version,
                                       pick.stable ? tr("最新的正式版") : tr("列表里最新的那个")));
    m_loaderInstall.phase = LoaderInstall::Phase::FetchingProfile;
    fetchFabricProfile();
}

void VersionManager::fetchFabricProfile() {
    const QUrl url(LoaderMeta::fabricProfileUrl(m_loaderInstall.gameVersion,
                                                m_loaderInstall.loaderVersion));
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onFabricProfileReply(reply); });
}

void VersionManager::onFabricProfileReply(QNetworkReply *reply) {
    reply->deleteLater();
    if (m_loaderInstall.phase != LoaderInstall::Phase::FetchingProfile) {
        return;
    }

    const QByteArray body = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (reply->error() != QNetworkReply::NoError || !doc.isObject()) {
        failLoaderInstall(tr("拿不到 %1 %2 的 profile（%3）。")
                              .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion,
                                   reply->errorString()));
        return;
    }

    // 这份 json 会**原样**落盘成 versions/<id>/<id>.json —— 它的 id 就是目录名、它的
    // inheritsFrom 决定启动时去哪找主类和客户端 jar。两条规则（自带注释）都在
    // LoaderMeta::checkFabricProfile() 里，那边是纯函数，验证工程拿真 profile 跑过。
    const QJsonObject profile = doc.object();
    const LoaderMeta::ProfileCheck check =
        LoaderMeta::checkFabricProfile(profile, m_loaderInstall.gameVersion);
    if (!check.ok()) {
        switch (check.problem) {
        case LoaderMeta::ProfileProblem::BadId:
            failLoaderInstall(tr("Fabric %1 的 profile 里的版本 id 不能用（%2），没有安装。")
                                  .arg(m_loaderInstall.loaderVersion, check.id));
            return;
        case LoaderMeta::ProfileProblem::WrongParent:
            failLoaderInstall(tr("Fabric %1 的 profile 是基于「%2」的，不是「%3」，没有安装。")
                                  .arg(m_loaderInstall.loaderVersion, check.parent,
                                       m_loaderInstall.gameVersion));
            return;
        case LoaderMeta::ProfileProblem::None:
            break;  // ok() 已经是 true，到不了这里
        }
        failLoaderInstall(tr("Fabric %1 的 profile 没通过检查，没有安装。")
                              .arg(m_loaderInstall.loaderVersion));
        return;
    }
    const QString id = check.id;

    // 落盘由 downloadVersion() 做（它写的正是 versionJsons 里这一份，原样、不合并）。
    versionJsons[id] = profile;
    m_loaderInstall.loaderId = id;
    startParentOrLoaderFiles();
}

void VersionManager::startParentOrLoaderFiles() {
    const QString parent = m_loaderInstall.gameVersion;
    // "原版装过没有"看的是**磁盘上那两份文件**：版本 json（启动路径 getVersionJson →
    // 继承链要读的就是它）和客户端 jar（启动时真正要喂给 JVM 的那个）。两个都要：
    //   - 只有目录、没有 json：老下载残骸。以为"装好了"的话，这个加载器版本启动时
    //     会报"版本信息缺失"。
    //   - 有 json、没有 jar：**我们自己的下载残骸** —— downloadVersion() 是先写 json
    //     再下 jar 的，jar 那一批失败就剩这个形态。以为"装好了"的话，用户拿到的是
    //     一个启动不起来、而且看不出为什么的加载器版本。
    // 判成"没装"正好走上补齐那条路：downloadVersion() 会校验并只补缺的那些文件，
    // 不是重下一遍。
    if (QFile::exists(InstanceFiles::jsonPath(parent)) &&
        QFile::exists(InstanceFiles::jarPath(parent))) {
        emit loaderInstallMessage(
            tr("原版 %1 已经装好了，直接装 %2 自己的文件。").arg(parent, m_loaderInstall.loaderName));
        startLoaderFiles();
        return;
    }

    emit loaderInstallMessage(
        tr("原版 %1 还没装，先把它的文件下全（客户端 jar + 库 + 资源）...").arg(parent));
    m_loaderInstall.phase = LoaderInstall::Phase::WaitingParentJson;
    fetchVersionDetails(parent);
}

void VersionManager::startLoaderFiles() {
    const QString id = m_loaderInstall.loaderId;
    if (id.isEmpty() || !versionJsons.contains(id)) {
        failLoaderInstall(tr("%1 的 profile 没准备好，安装中止。").arg(m_loaderInstall.loaderName));
        return;
    }
    m_loaderInstall.phase = LoaderInstall::Phase::WaitingLoaderFiles;
    emit loaderInstallMessage(tr("开始下载 %1 %2 自己的文件（maven 库）...")
                                  .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion));
    downloadVersion(id, AppPaths::versionDir(id));
}

// ---------------------------------------------------------------------------
// Forge / NeoForge：跑它们自己的安装器
// ---------------------------------------------------------------------------

void VersionManager::fetchInstallerList() {
    if (loaderNetwork == nullptr) {
        loaderNetwork = new QNetworkAccessManager(this);
    }
    // 两个接口一个回 JSON、一个回 XML，但都是"GET 一段文本，从里面挑一个版本号"，
    // 所以共用这一条路，分派放在回复处理里按 kind 做。
    const QUrl url(m_loaderInstall.kind == LoaderKind::Forge ? LoaderMeta::forgePromotionsUrl()
                                                            : LoaderMeta::neoforgeMavenMetadataUrl());
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onInstallerListReply(reply); });
}

void VersionManager::onInstallerListReply(QNetworkReply *reply) {
    reply->deleteLater();
    // 状态已经换过（上一次失败收了尾、或者用户又点了一次）：这份回复没人要了。
    if (m_loaderInstall.phase != LoaderInstall::Phase::FetchingInstallerList) {
        return;
    }

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        failLoaderInstall(tr("拿不到 %1 %2 的版本表（%3）。检查网络后重试。")
                              .arg(m_loaderInstall.loaderName, m_loaderInstall.gameVersion,
                                   reply->errorString()));
        return;
    }

    LoaderMeta::LoaderPick pick;
    if (m_loaderInstall.kind == LoaderKind::Forge) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (!doc.isObject()) {
            failLoaderInstall(tr("Forge 的版本表读不懂（%1）。").arg(parseError.errorString()));
            return;
        }
        pick = LoaderMeta::pickForgeVersion(doc.object(), m_loaderInstall.gameVersion);
    } else {
        pick = LoaderMeta::pickNeoForgeVersion(body, m_loaderInstall.gameVersion);
    }

    if (!pick.ok()) {
        // 和 Fabric 那条一样：这不是网络问题，也**不能**退回"装原版"——用户要的是带
        // 加载器的版本，装出来一个纯原版是骗人。
        failLoaderInstall(tr("%1 还没有支持 %2 的版本。")
                              .arg(m_loaderInstall.loaderName, m_loaderInstall.gameVersion));
        return;
    }

    const bool isForge = m_loaderInstall.kind == LoaderKind::Forge;
    m_loaderInstall.loaderVersion = pick.version;
    m_loaderInstall.loaderId = isForge
                                   ? LoaderMeta::forgeLoaderId(m_loaderInstall.gameVersion,
                                                               pick.version)
                                   : LoaderMeta::neoforgeLoaderId(pick.version);
    m_loaderInstall.installerUrl = isForge
                                       ? LoaderMeta::forgeInstallerUrl(m_loaderInstall.gameVersion,
                                                                       pick.version)
                                       : LoaderMeta::neoforgeInstallerUrl(pick.version);

    emit loaderInstallMessage(tr("挑中 %1 %2（%3）。")
                                  .arg(m_loaderInstall.loaderName, pick.version,
                                       pick.stable ? tr("官方推荐的正式版")
                                                   : tr("版本表里最新的那个")));
    startInstallerDownload();
}

void VersionManager::startInstallerDownload() {
    // 文件名取安装器地址的最后一段，不自己拼 —— 这样盘上那个 jar 和上游叫同一个名字
    // （forge-1.20.1-47.4.10-installer.jar / neoforge-21.1.215-installer.jar），
    // 用户去 cache/installers/ 里看一眼能对上。版本目录名那边不这么干是另一回事：
    // 那个名字是别的启动器认的，不能改。
    const QString fileName = QFileInfo(QUrl(m_loaderInstall.installerUrl).path()).fileName();
    m_loaderInstall.installerPath =
        AppPaths::installerCacheDir() + QLatin1Char('/') + fileName;

    emit loaderInstallMessage(tr("正在下载 %1 %2 的安装器...")
                                  .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion));

    downloadManager->beginBatch();
    // phase 放在 beginBatch() **之后**设：beginBatch() 会替上一批收尾并发一次
    // allDownloadsFinished，那一次不该被当成"安装器下好了"。
    m_loaderInstall.phase = LoaderInstall::Phase::WaitingInstallerJar;
    // 安装器 jar 没有公布 sha1（Forge 只在下载页给别的东西，NeoForge 的 maven-metadata
    // 里也没有），所以这里只能"文件在就算好" —— 第二次装同一个加载器版本时省掉这 6 MB。
    // 校验不了是**接受**的：这个 jar 只被我们当子进程启动，真下坏了会在退出码和产物核对
    // 那两道上现形（见 onInstallerFinished）。
    downloadManager->downloadFile(m_loaderInstall.installerUrl, m_loaderInstall.installerPath,
                                  QString(), -1, VerifyMode::ExistsOnly);
    downloadManager->endBatch();
}

bool VersionManager::ensureLauncherProfiles(bool *createdOut, QString *errorOut) const {
    if (createdOut != nullptr) {
        *createdOut = false;
    }
    const QString path = AppPaths::minecraftDir() + QLatin1Char('/') +
                         LoaderMeta::launcherProfilesFileName();
    if (QFile::exists(path)) {
        // 已经有就**一个字都不动**：这个文件是原版启动器和 PCL 都在写的东西，里面可能
        // 是用户真正的启动器档案。安装器会往里面**加**一条 profile，加完照样是有用的；
        // 我们覆盖它就是把人家的东西弄丢。
        return true;
    }

    // 没有才补一份最小的。它是安装器的硬前置，实测缺了这条：
    //   There is no minecraft launcher profile in "...", you need to run the launcher first!
    //   There was an error during installation      （退出码 1，一个字节的库都不下）
    QDir().mkpath(AppPaths::minecraftDir());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorOut != nullptr) {
            *errorOut = tr("这个游戏目录里还没有 %1（%2 的安装器认它才肯干活），而它建不出来"
                           "（%3）。")
                            .arg(LoaderMeta::launcherProfilesFileName(),
                                 m_loaderInstall.loaderName, file.errorString());
        }
        return false;
    }
    file.write(LoaderMeta::minimalLauncherProfiles());
    if (!file.commit()) {
        if (errorOut != nullptr) {
            *errorOut = tr("这个游戏目录里还没有 %1（%2 的安装器认它才肯干活），而它写不下去"
                           "（%3）。")
                            .arg(LoaderMeta::launcherProfilesFileName(),
                                 m_loaderInstall.loaderName, file.errorString());
        }
        return false;
    }
    if (createdOut != nullptr) {
        *createdOut = true;
    }
    return true;
}

void VersionManager::runInstaller() {
    bool created = false;
    QString error;
    if (!ensureLauncherProfiles(&created, &error)) {
        failLoaderInstall(error);
        return;
    }
    if (created) {
        emit loaderInstallMessage(
            tr("这个游戏目录里还没有 %1，先补一份最小的（没有它 %2 的安装器拒绝干活；"
               "原版启动器、PCL 都往这个文件里记自己的档案，所以补的是最小的一份、"
               "不动别的东西）。")
                .arg(LoaderMeta::launcherProfilesFileName(), m_loaderInstall.loaderName));
    }

    const QString program = m_loaderInstall.javaPath.isEmpty() ? QStringLiteral("java")
                                                               : m_loaderInstall.javaPath;
    // 只有"看着像路径"的时候才判它在不在盘上。裸名字（`java`）走的是 PATH 查找，而
    // QFileInfo("java").exists() 一定返回 false —— 拿它当判据会把"设置里没指定、但 PATH
    // 上有 java"这种情况误杀，而那正是这条路的默认情形（javaPath 传空就是这么来的）。
    const bool looksLikePath = program.contains(QLatin1Char('/')) ||
                               program.contains(QLatin1Char('\\'));
    if (looksLikePath && !QFileInfo::exists(program)) {
        failLoaderInstall(tr("找不到 Java 可执行文件（%1），%2 的安装器起不来。\n"
                             "到「设置」里指定一个 Java，或者让系统 PATH 上有 java。")
                              .arg(program, m_loaderInstall.loaderName));
        return;
    }

    if (installerProcess == nullptr) {
        installerProcess = new QProcess(this);
        connect(installerProcess, &QProcess::readyReadStandardOutput, this,
                [this] { onInstallerOutput(); });
        connect(installerProcess, &QProcess::readyReadStandardError, this,
                [this] { onInstallerOutput(); });
        connect(installerProcess, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError code) { onInstallerError(static_cast<int>(code)); });
        connect(installerProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus status) {
                    onInstallerFinished(exitCode, status != QProcess::NormalExit);
                });
        installerTimer = new QTimer(this);
        installerTimer->setInterval(kInstallerTickMs);
        connect(installerTimer, &QTimer::timeout, this, &VersionManager::onInstallerTick);
    }

    m_installerOutput.clear();
    m_installerTicks = 0;
    m_installerStartedAt = QDateTime::currentMSecsSinceEpoch();

    // phase 要在 start() **之前**设：进程起不来时 errorOccurred 会回来，那条路要靠
    // phase 认出"这次是安装器的事"。
    m_loaderInstall.phase = LoaderInstall::Phase::RunningInstaller;
    emit loaderInstallMessage(
        tr("正在运行 %1 %2 的安装器...（这一步最慢：它要下十几个库，还要把原版的"
           "客户端 jar 打两遍补丁）")
            .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion));

    // 和原版启动器一样的调用方式。--installClient 那个目录参数**必须**给：不给的话
    // 安装器会落到 user.home/.minecraft（我们这台机器上是 C:\Users\User\.minecraft），
    // 装到一个用户根本不知道的地方去。
    installerProcess->setProgram(program);
    installerProcess->setArguments({QStringLiteral("-jar"), m_loaderInstall.installerPath,
                                    QStringLiteral("--installClient"), AppPaths::minecraftDir()});
    installerProcess->setWorkingDirectory(AppPaths::minecraftDir());
    installerProcess->start();
    installerTimer->start();
}

void VersionManager::onInstallerOutput() {
    if (installerProcess == nullptr) {
        return;
    }
    // 两个通道都读、都往一个缓冲区里放：安装器的诊断信息 stdout 和 stderr 都有，
    // 而我们要的只是"最后那几行说了什么"。混在一起的代价是万一两者交替写、行会交错，
    // 那种情况下报出来的诊断仍然够用。
    m_installerOutput += installerProcess->readAllStandardOutput();
    m_installerOutput += installerProcess->readAllStandardError();
    if (m_installerOutput.size() > kInstallerOutputKeepBytes) {
        m_installerOutput = m_installerOutput.right(kInstallerOutputKeepBytes);
    }
}

QString VersionManager::installerLastOutputLine() const {
    const QStringList lines = installerOutputLines(m_installerOutput);
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString line = lines.at(i).trimmed();
        if (!line.isEmpty()) {
            return line;
        }
    }
    return QString();
}

void VersionManager::onInstallerTick() {
    if (m_loaderInstall.phase != LoaderInstall::Phase::RunningInstaller) {
        return;
    }
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_installerStartedAt;
    if (elapsed >= kInstallerTimeoutMs) {
        abortInstaller(tr("%1 的安装器跑了 %2 分钟还没结束，已经把它停掉了。\n"
                          "它最后打出来的是：%3\n"
                          "多半是网络卡住了，过一会儿重试一次；一直这样就把日志发来。")
                           .arg(m_loaderInstall.loaderName)
                           .arg(elapsed / 60000)
                           .arg(installerLastOutputLine()));
        return;
    }

    if (++m_installerTicks % kInstallerHeartbeatSeconds != 0) {
        return;
    }
    // 心跳，不是进度：说清"还活着"和"它现在在干什么"，但绝不逐行转安装器的输出。
    const QString last = installerLastOutputLine();
    emit loaderInstallMessage(last.isEmpty()
                                  ? tr("%1 的安装器还在跑（%2 秒）...")
                                        .arg(m_loaderInstall.loaderName)
                                        .arg(elapsed / 1000)
                                  : tr("%1 的安装器还在跑（%2 秒）：%3")
                                        .arg(m_loaderInstall.loaderName)
                                        .arg(elapsed / 1000)
                                        .arg(last));
}

void VersionManager::onInstallerError(int processError) {
    if (m_loaderInstall.phase != LoaderInstall::Phase::RunningInstaller) {
        return;
    }
    // FailedToStart 是唯一**不会**跟着一个 finished 信号的错误（进程压根没起来），
    // 所以要在这里收尾。别的（Crashed / Timedout）都会走到 finished。
    if (static_cast<QProcess::ProcessError>(processError) != QProcess::FailedToStart) {
        return;
    }
    abortInstaller(tr("%1 的安装器起不来（那个 Java 进程没能启动）。\n"
                      "到「设置」里换一个 Java 试试。")
                       .arg(m_loaderInstall.loaderName));
}

void VersionManager::abortInstaller(const QString &reason) {
    if (installerTimer != nullptr) {
        installerTimer->stop();
    }
    // 先把 phase 挪开**再**杀：kill() 会让那个进程的 finished 回来，而它一进
    // onInstallerFinished 就会看到 phase 还是 RunningInstaller —— 于是又收一次尾
    // （两个完成信号，UI 那边第二个会把刚恢复的界面再按一次）。挪开之后那条被守卫挡掉。
    m_loaderInstall.phase = LoaderInstall::Phase::Idle;
    if (installerProcess != nullptr && installerProcess->state() != QProcess::NotRunning) {
        installerProcess->kill();
        installerProcess->waitForFinished(5000);
    }
    failLoaderInstall(reason);
}

void VersionManager::onInstallerFinished(int exitCode, bool crashed) {
    // 超时那一路已经收过尾了（见 abortInstaller）：这里只认还走在 RunningInstaller 上的。
    if (m_loaderInstall.phase != LoaderInstall::Phase::RunningInstaller) {
        return;
    }
    if (installerTimer != nullptr) {
        installerTimer->stop();
    }
    onInstallerOutput();  // 收尾前把剩下的输出读干净，失败报告才有东西可报

    const QString name = m_loaderInstall.loaderName;
    const QString version = m_loaderInstall.loaderVersion;

    if (crashed) {
        failLoaderInstall(tr("%1 %2 的安装器被强行结束了（不是它自己退出的）。\n"
                             "它最后打出来的是：\n%3")
                              .arg(name, version, installerOutputTail(m_installerOutput)));
        return;
    }
    if (exitCode != 0) {
        failLoaderInstall(tr("%1 %2 的安装器报了错（退出码 %3），没有装成。\n"
                             "它最后打出来的是：\n%4")
                              .arg(name, version)
                              .arg(exitCode)
                              .arg(installerOutputTail(m_installerOutput)));
        return;
    }

    // 退出码 0 **还不算**装好：核对它真的把那份 json 放在我们算出来的目录里、而且那份
    // json 认的父版本就是我们打算装的那个。Forge 哪天改了目录名约定的话，这两条里至少
    // 有一条会亮 —— 报出来的是"装是装了、但不在预期的地方"，而不是静默把 wtfml.meta
    // 写到一个不存在的 id 上。
    const QString id = m_loaderInstall.loaderId;
    const QJsonObject produced = readVersionJsonAt(InstanceFiles::jsonPath(id));
    if (produced.isEmpty()) {
        failLoaderInstall(tr("%1 %2 的安装器跑完了（退出码 0），但 %3 里没有出现 %4，"
                             "认不出它装到哪去了。\n"
                             "它最后打出来的是：\n%5")
                              .arg(name, version, AppPaths::versionDir(id),
                                   id + QStringLiteral(".json"),
                                   installerOutputTail(m_installerOutput)));
        return;
    }
    const LoaderMeta::ProfileCheck check =
        LoaderMeta::checkInstallerResult(produced, m_loaderInstall.gameVersion, id);
    if (!check.ok()) {
        if (check.problem == LoaderMeta::ProfileProblem::BadId) {
            failLoaderInstall(tr("%1 %2 装出来的版本 json 里的 id 是「%3」，和我们算的「%4」"
                                 "对不上，没有记成装好。")
                                  .arg(name, version, check.id, id));
            return;
        }
        failLoaderInstall(tr("%1 %2 装出来的版本是基于「%3」的，不是我们选的「%4」，"
                             "没有记成装好。")
                              .arg(name, version, check.parent, m_loaderInstall.gameVersion));
        return;
    }

    emit loaderInstallMessage(tr("%1 %2 装好了（%3）。").arg(name, version, id));
    startParentCompletion();
}

void VersionManager::startParentCompletion() {
    // 安装器**不下资源文件**（assets）：它自己那份 json 是给启动器读的，资源是启动器的事。
    // 少了那一半，用户拿到的是一个能装模组、但一进游戏就崩的版本。
    //
    // Forge 还会连原版的 json 都不写（只在 versions/<原版>/ 留下一份 sha1 与官方一致的
    // 客户端 jar），NeoForge 会留 json + jar 两份。所以这里**不看原版装没装**，一律走
    // 一遍 downloadVersion()：它写 json、校验客户端 jar 与库（已有就跳过，不是重下）、
    // 再把资源下全。NeoForge 那种"已经齐了"的情况下它也只是校验 + 补资源。
    const QString parent = m_loaderInstall.gameVersion;
    m_loaderInstall.phase = LoaderInstall::Phase::WaitingParentJson;
    emit loaderInstallMessage(tr("%1 装好了，接着把原版 %2 的文件补齐（安装器不下资源文件）...")
                                  .arg(m_loaderInstall.loaderName, parent));
    fetchVersionDetails(parent);
}

void VersionManager::finishLoaderInstall(bool success, const QString &message) {
    // 先把要用的字段取出来：reset() 会把它们清掉，忘了取的表现是消息里那个名字变空的
    // （不崩，但话就说不清了）。
    const QString loaderName = m_loaderInstall.loaderName;
    const QString loaderVersion = m_loaderInstall.loaderVersion;
    const QString loaderId = m_loaderInstall.reportId();

    if (success) {
        writeLoaderMeta();
        // 装出去的这份 json 是**新的**：在这之前读到的任何缓存（合并结果那张表尤其）
        // 都是旧的，留着会让"刚装好的版本"用回装之前的样子启动。
        versionJsons.remove(m_loaderInstall.loaderId);
        mergedVersionJsons.remove(m_loaderInstall.loaderId);
    }

    // 收尾：既结束这次安装，也结束这次点击 —— 它是**唯一**的终态。
    m_loaderInstall.reset();
    emit versionDownloadFinished(
        loaderId, success,
        success ? tr("%1 %2 已就绪。%3").arg(loaderName, loaderVersion, message)
                : tr("%1 %2 没有装成。%3").arg(loaderName, loaderVersion, message));
}

void VersionManager::writeLoaderMeta() {
    QJsonObject fields;
    fields[QStringLiteral("realVersion")] = m_loaderInstall.gameVersion;
    fields[QStringLiteral("loaderName")] = m_loaderInstall.loaderName;
    fields[QStringLiteral("loaderVersion")] = m_loaderInstall.loaderVersion;

    QString error;
    if (!updateVersionMeta(m_loaderInstall.loaderId, fields, &error)) {
        // NOTE: 版本本身已经装好了、能启动 —— 写不进 meta 只是"显示名少一段"，
        //       所以这不是失败，不该把它算进失败里。但也不能不说（用户会发现名字不对）。
        qWarning() << "Failed to write wtfml.meta for" << m_loaderInstall.loaderId << ":" << error;
        emit loaderInstallMessage(tr("装好了，但没能写进 wtfml.meta（%1）——「版本名」里会少一段「%2 %3」。")
                                      .arg(error, m_loaderInstall.loaderName,
                                           m_loaderInstall.loaderVersion));
    }
}

void VersionManager::failLoaderInstall(const QString &reason) {
    // 那份 profile 还在内存缓存里、磁盘上却没有对应的目录：留着的话 getVersionJson()
    // 会给出一个"磁盘上并不存在"的合并结果（它先查缓存）。清掉它。
    if (!m_loaderInstall.loaderId.isEmpty()) {
        versionJsons.remove(m_loaderInstall.loaderId);
    }
    const QString reportId = m_loaderInstall.reportId();
    qWarning() << "Loader install failed for" << reportId << ":" << reason;
    m_loaderInstall.reset();
    emit versionDownloadFinished(reportId, false, reason);
}

QJsonObject VersionManager::rawVersionJson(const QString &versionId) const {
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

QJsonObject VersionManager::getVersionJson(const QString &versionId) const {
    const auto cached = mergedVersionJsons.constFind(versionId);
    if (cached != mergedVersionJsons.constEnd()) {
        return cached.value();
    }

    QJsonObject merged = rawVersionJson(versionId);
    if (merged.isEmpty()) {
        // 取不到就不缓存（同 rawVersionJson 的理由：这次取不到可能只是"还没下载完"，
        // 缓存一个空对象会让之后下载好了也还是空的）。
        return merged;
    }

    // 顺着 inheritsFrom 往上合。三道闸：
    //   - 访问过的 id 集合：挡环（手写的 json 可以 A 继承 B、B 继承 A）；
    //   - 深度上限：挡一条长得离谱的链（光有集合挡不住它）；
    //   - 父版本取不到：停在这儿，把手上这份"合了一部分"的交出去 —— 问题由
    //     inheritanceProblem() 去说，这一层的职责只是尽量给出能用的东西。
    //
    // NOTE: 每一轮都是 `merge(当前结果, 上一层父版本)`，这是在**已经合过**的东西上再合
    //       一层。这样做是对的（父在前、子覆盖，一层层往外套）而且不会重复拼接 ——
    //       靠的是 merge() 把 inheritsFrom 丢掉、下一层的 id 从**父版本自己那份**里读。
    QSet<QString> seen;
    seen.insert(versionId);
    QString parentId = VersionInheritance::parentIdOf(merged);
    for (int depth = 0; depth < VersionInheritance::maxDepth() && !parentId.isEmpty(); ++depth) {
        if (seen.contains(parentId)) {
            qWarning() << "Inheritance cycle in version JSON:" << versionId << "-> ... ->"
                       << parentId;
            break;
        }
        seen.insert(parentId);

        const QJsonObject parent = rawVersionJson(parentId);
        if (parent.isEmpty()) {
            qWarning() << "Version" << versionId << "inherits from" << parentId
                       << "which is not installed — cannot merge";
            break;
        }
        merged = VersionInheritance::merge(merged, parent);
        parentId = VersionInheritance::parentIdOf(parent);
    }

    mergedVersionJsons[versionId] = merged;
    return merged;
}

QString VersionManager::inheritanceProblem(const QString &versionId) const {
    QJsonObject json = rawVersionJson(versionId);
    if (json.isEmpty()) {
        // 连自己那份都没有：这是"版本信息缺失"那条路的事，不是继承的问题。
        return QString();
    }

    QSet<QString> seen;
    seen.insert(versionId);
    for (int depth = 0; depth < VersionInheritance::maxDepth(); ++depth) {
        const QString parentId = VersionInheritance::parentIdOf(json);
        if (parentId.isEmpty()) {
            return QString();
        }
        if (seen.contains(parentId)) {
            return tr("版本「%1」的继承链接成环了（指回 %2），没法确定它到底基于什么。\n"
                      "这个版本的 json 是被改坏过的，建议删掉重装。")
                .arg(versionId, parentId);
        }
        seen.insert(parentId);

        json = rawVersionJson(parentId);
        if (json.isEmpty()) {
            // 这句话必须点明"要去装原版"，而不是套用那句"请重新下载该版本" —— 用户照
            // 后者做，下载的还是这个加载器版本（它的 json 本来就不含原版那份东西），
            // 于是问题原样复现。
            return tr("版本「%1」基于「%2」，但本机没有安装「%2」，启动不了。\n"
                      "请先安装原版「%2」，再启动这个版本。")
                .arg(versionId, parentId);
        }
    }

    return tr("版本「%1」的继承链太长了（超过 %2 层），没法确定它到底基于什么。")
        .arg(versionId)
        .arg(VersionInheritance::maxDepth());
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

        // 加载器安装里"顺便把原版装好"那一步：这次取 json 是**安装流程自己要的**，不是
        // 用户在界面上下载这个版本。不能再发 versionDetailsFetched —— 发出去界面会当成
        // "用户点了下载原版"，于是同一个版本下两遍、两个批次互相打架。这里直接接手。
        if (m_loaderInstall.phase == LoaderInstall::Phase::WaitingParentJson &&
            versionId == m_loaderInstall.gameVersion) {
            m_loaderInstall.phase = LoaderInstall::Phase::WaitingParentFiles;
            downloadVersion(versionId, AppPaths::versionDir(versionId));
            return;
        }

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

    // 安装流程在等原版那份 json：这次失败就是安装失败，而不是"某个人请求的版本没取到"。
    if (m_loaderInstall.phase == LoaderInstall::Phase::WaitingParentJson &&
        pending == m_loaderInstall.gameVersion) {
        failLoaderInstall(tr("拿不到原版 %1 的版本信息，%2 没有安装。").arg(
            pending, m_loaderInstall.loaderName));
        return;
    }

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

    // 加载器安装的几段在这里接续（一次点击 = 好几件事，见 .hpp 里 installLoaderVersion 的
    // NOTE）。这一段**必须排在下面那条信号之前**：用户点的是"安装 Forge"，中间那批原版
    // 文件不该单独报一次"下载完成"（日志里会多一句莫名其妙的原版、按钮也会中途解禁）。
    if (m_loaderInstall.active()) {
        if (m_loaderInstall.phase == LoaderInstall::Phase::WaitingInstallerJar) {
            if (!success) {
                failLoaderInstall(tr("%1 的安装器没下下来，%2 没有安装。检查网络后重试。\n%3")
                                      .arg(m_loaderInstall.loaderName,
                                           m_loaderInstall.gameVersion, message));
                return;
            }
            // 这一批结束了，但事情远没完：安装器那个 Java 进程现在才开始跑。
            runInstaller();
            return;
        }

        if (m_loaderInstall.phase == LoaderInstall::Phase::WaitingParentFiles) {
            if (!success) {
                failLoaderInstall(tr("原版 %1 的文件没下全，%2 没有安装。先把原版装好再试。\n%3")
                                      .arg(m_loaderInstall.gameVersion,
                                           m_loaderInstall.loaderName, message));
                return;
            }
            if (m_loaderInstall.kind == LoaderKind::Fabric) {
                emit loaderInstallMessage(tr("原版 %1 就绪，接着装 %2 自己的文件...")
                                              .arg(m_loaderInstall.gameVersion,
                                                   m_loaderInstall.loaderName));
                startLoaderFiles();
                return;
            }
            // Forge / NeoForge：加载器自己那份 json 和库**安装器已经放好了**，这一批只是
            // 把原版补齐，补完就结束了 —— 没有"加载器自己那几个库"要再下一遍。
            finishLoaderInstall(true, message);
            return;
        }

        if (m_loaderInstall.phase == LoaderInstall::Phase::WaitingLoaderFiles) {
            finishLoaderInstall(success, message);
            return;
        }
    }

    // 一批只发一次。改动前这里由每个文件的 downloadFinished 触发，
    // 下载一个版本会在日志里刷出几千行一模一样的话。
    emit versionDownloadFinished(m_currentVersionDownloading, success, message);
}
