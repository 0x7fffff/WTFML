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
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QSet>
#include <QUrl>
#include <QUuid>

#include "../download/LoaderMeta.hpp"
#include "../launcher/VersionInheritance.hpp"
#include "../util/AppPaths.hpp"
#include "../util/ZipUtil.hpp"
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

/// 探 `--help` 那一次最多等多久。它本该打一段帮助就退出（实测两个安装器都在 1 秒内回来），
/// 所以给得短：探不出来就按"宁可信其有"往下走，不能为它耽误真正的安装。
constexpr int kInstallerProbeTimeoutMs = 60 * 1000;

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

/// 把 from 这棵树**覆盖着**拷进 to（to 不存在就建目录）。任何一步失败返回 false。
///
/// 三条规矩都是为"搬进用户真实的游戏目录"这一件事定的：
///
///  * **只覆盖同名文件，绝不删 to 底下任何东西。** to 里可能有用户自己放的东西，也可能
///    有别的版本/加载器的库 —— `<游戏目录>/libraries/` 就是全体版本共用的一份。所以这个
///    函数里没有一个 removeRecursively，删的只能是它马上要盖掉的那一个文件。
///  * 目标已存在时**先删那个文件再拷**：QFile::copy() 目标存在时返回 false（不覆盖），
///    而重装同一个版本正是"目标已存在"的常态。删不掉（被别的进程占着）就当失败报出去，
///    不装作拷过了。
///  * 失败如实返回 false，由调用方报"没搬回来"。半途而废的复制**会被留下**（比如版本
///    目录搬了一半）—— 那种情况下调用方的话里要说清"再装一次能盖好"，而不是说装好了。
///
/// NOTE: 空目录不会在 to 里被建出来（不拷文件就没有 mkpath）。这里用不上：搬的两样东西
///       ——版本目录和 libraries/optifine/ —— 底下的文件一个都不少。
bool copyTreeInto(const QString &from, const QString &to) {
    const QFileInfo source(from);
    if (source.isFile()) {
        const QString parent = QFileInfo(to).absolutePath();
        if (!QDir().mkpath(parent)) {
            qWarning() << "Cannot create directory" << parent;
            return false;
        }
        if (QFile::exists(to) && !QFile::remove(to)) {
            qWarning() << "Cannot overwrite" << to;
            return false;
        }
        if (!QFile::copy(from, to)) {
            qWarning() << "Cannot copy" << from << "to" << to;
            return false;
        }
        return true;
    }
    if (source.isDir()) {
        const QFileInfoList entries =
            QDir(from).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            if (!copyTreeInto(entry.absoluteFilePath(),
                              to + QLatin1Char('/') + entry.fileName())) {
                return false;
            }
        }
    }
    return true;
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
    //
    // 那份临时工作目录（OptiFine 那条路才有）不是 QObject 的子对象，得自己收。正常路径下
    // 它在安装的终态里已经被删过了（finishLoaderInstall / failLoaderInstall），这里只是
    // 兜住"装到一半程序被关掉"：不收的话系统临时目录里会积一份几 MB 的假 .minecraft。
    //
    // NOTE: 这一刻安装器那个子进程可能还活着（它是 QObject 的子对象，要到 ~QObject 才被
    //       收），Windows 上文件被占着就删不掉 —— 那种情况下 remove() 会失败，这里是
    //       析构，没有地方报，也不值得报（QTemporaryDir 下次会自己重试）。
    delete optifineWorkDir;
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

QList<VersionManager::VersionChannel>
VersionManager::groupIntoChannels(const QList<MinecraftVersion> &versions) {
    // 四堆**先全摆出来**（空的那几堆也在）：界面那四条分类是一条都不少的，点开是空的
    // 和"这一条压根不出现"是两回事。
    QList<VersionChannel> channels;
    channels.reserve(VersionCategory::channelCount());
    for (int i = 0; i < VersionCategory::channelCount(); ++i) {
        const VersionCategory::Channel channel = VersionCategory::channelAt(i);
        channels.append(VersionChannel{channel, VersionCategory::channelKey(channel),
                                       VersionCategory::channelText(channel), {}});
    }
    // channelFor() 的返回值是**一个**分类（不是一组标记），所以"一条版本只进一堆"和
    // "一条都不丢"是同一件事 —— 这里不用再去重、也不用再兜"漏掉的那些"。
    for (const MinecraftVersion &version : versions) {
        const int index = VersionCategory::channelIndex(
            VersionCategory::channelFor(version.id, version.type));
        channels[index].versions.append(version);
    }
    return channels;
}

QList<VersionManager::VersionChannel> VersionManager::availableVersionChannels() const {
    return groupIntoChannels(downloadManager->getAvailableVersions());
}

// ---------------------------------------------------------------------------
// 从信号里往回接续：排到下一轮事件循环（用法和理由见 .hpp 里 deferNextStep 的 NOTE）
// ---------------------------------------------------------------------------

void VersionManager::deferNextStep(std::function<void()> step) {
    // context 传 this：这个对象没了的话，排着的这一步会自动作废（Qt 的行为，不用自己判）。
    QMetaObject::invokeMethod(
        this,
        [this, step = std::move(step)] {
            try {
                step();
            } catch (const std::exception &e) {
                recoverFromInternalError(QString::fromUtf8(e.what()));
            } catch (...) {
                // 不是 std::exception 派生的一律叫不出名字，但也不能就这么穿出去。
                recoverFromInternalError(tr("未知异常"));
            }
        },
        Qt::QueuedConnection);
}

void VersionManager::deferLoaderStep(LoaderInstall::Phase expected, std::function<void()> step) {
    deferNextStep([this, expected, step = std::move(step)] {
        if (m_loaderInstall.phase != expected) {
            return;
        }
        step();
    });
}

void VersionManager::deferNamedStep(NamedInstance::Phase expected, std::function<void()> step) {
    deferNextStep([this, expected, step = std::move(step)] {
        // 状态已经被收掉了（用户又点了一次、或者上一步失败了）：这一份续接没人要了。
        // 少了这道守卫，收尾那句会对着一个已经结束的安装再说一遍"装好了"。
        if (m_namedInstance.phase != expected) {
            return;
        }
        step();
    });
}

void VersionManager::recoverFromInternalError(const QString &what) {
    qWarning() << "Internal error while continuing the next step:" << what;

    // 批次可能正开着（异常正好打在 beginBatch() 和 endBatch() 中间 —— 实测那次就是这样）。
    // 先把它放弃掉：不放弃的话 m_batchOpen 永远是 true，之后每一次下载都会被"已有下载任务
    // 正在进行"顶回来，只能重启启动器。abandonBatch() 是**只给这条路用**的：它不发任何信号。
    downloadManager->abandonBatch();

    // 用既有的终态出口把话说出去：界面那边照常弹窗（错误要弹窗），用户知道出了事、
    // 也还能接着用别的功能。
    const QString reason =
        tr("下载过程中出了内部错误（%1），这一步已经停下。重启启动器后再试一次。").arg(what);
    // 整合包导入排在加载器**前面**：导入的第一步就是装加载器，所以这两件事可能同时是活的。
    // 那种时候要收的是整件事 —— 只收掉加载器那半截的话，导入的状态机还停在 InstallingLoader
    // 上，而此后每一次导入都会被"已有任务正在进行"顶回来，只能重启启动器。
    if (m_modpackInstall.active()) {
        failModpackImport(reason);
        return;
    }
    // 排在加载器**前面**：装一个带名字的实例时，父版本是加载器，于是这两件事同时是活的，
    // 而用户等的是他起的那个名字。先收掉它（它会连加载器那半截一起收，见 failNamedInstance），
    // 否则那个状态就永远留在 WaitingLoader 上，之后每次装都"已有任务正在进行"。
    if (m_namedInstance.active()) {
        failNamedInstance(reason);
        return;
    }
    if (m_loaderInstall.active()) {
        failLoaderInstall(reason);
        return;
    }
    emit versionDownloadFinished(m_currentVersionDownloading, false, reason);
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

    // 把 version.json 落盘。缺了它，下一次启动时 getVersionJson() 取不到
    // javaVersion / assetIndex，会静默退回错误的 Java 版本和资产索引 ——
    // 26.1 需要 Java 25，退成 Java 21 就是 UnsupportedClassVersionError。
    // NOTE: 写失败**不**中止这一批（老行为，保留）：这里只写一句日志 —— 版本 json 写不进去
    //       的报错，用户看的是后面那批文件下完之后的收尾话。整合包那条路不一样，它要一个
    //       明确的终态，所以那边看返回值（见 startModpackFiles）。
    writeVersionJson(versionId, destinationPath, versionJson, nullptr);

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
    // NOTE: 库的目标目录走 AppPaths::libraryDir()（= <数据目录>/libraries），是**全体实例
    //       共用**的那一份 —— 和官方启动器、和别的启动器都共用同一份。
    //       这里以前有一句"库走 basePath（= versions/<id>）、库是这个版本自己的"：两句话
    //       都是错的（当时那个 basePath 其实是 minecraftDir()）。照那句话去"修"代码的代价
    //       是给每个实例各下一份库 —— 几百 MB 起步，而且和共用那份不一致。
    //       真正"这个版本自己的"只有 versionDir() 底下那两样（<id>.jar / <id>.json）。
    //       资源（assets）也共用，那边的目录由 AppPaths::assetsDir() 说了算。
    QList<Library> libraries = downloadManager->extractLibraries(versionJson);
    downloadManager->downloadLibraries(libraries, AppPaths::libraryDir());

    // Download assets
    AssetIndex assetIndex = downloadManager->extractAssetIndex(versionJson);
    downloadManager->downloadAssets(assetIndex);

    downloadManager->endBatch();
}

// ---------------------------------------------------------------------------
// 带模组加载器的版本（今天只有 Fabric 接通了）
// ---------------------------------------------------------------------------

bool VersionManager::loaderInstallSupported(LoaderKind kind) {
    // 四个都接通了，所以四个都返回 true（这个函数只说"能不能装"，不说"怎么装"）。
    //
    // 刻意写成**逐个 case 列出来**、末尾 `return false`，而不是一句 `return true`：将来往
    // LoaderKind 里加第五个加载器时，编译器的 -Wswitch 会提醒这里少了一手，而没写完的那个
    // 会落到 false —— 界面那句「还没做」和 installLoaderVersion() 里那道守卫正好接住它，
    // 不会出现"勾了一个没接线的加载器，装出来一个纯原版"。
    switch (kind) {
    case LoaderKind::Fabric:
    case LoaderKind::Forge:
    case LoaderKind::NeoForge:
    case LoaderKind::OptiFine:
        return true;
    }
    return false;
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

int VersionManager::loaderIndexForKind(LoaderKind kind) {
    // 反着查同一张表（loaderKindFromIndex 是正着查）—— 反过来各写一张的话，"那张表
    // 改了、这张没改"会表现成"勾了 Fabric，拉回来的列表填进 Forge 那一条"，不报错。
    for (int index = 0; index < loaderKindCount(); ++index) {
        LoaderKind candidate = LoaderKind::Fabric;
        if (loaderKindFromIndex(index, &candidate) && candidate == kind) {
            return index;
        }
    }
    return -1;  // 枚举里将来多一个、那张表还没跟上时走这里
}

bool VersionManager::isLoaderInstalling() const { return m_loaderInstall.active(); }

// ---------------------------------------------------------------------------
// 整合包导入（Modrinth 格式）
// ---------------------------------------------------------------------------

bool VersionManager::isModpackImporting() const { return m_modpackInstall.active(); }

bool VersionManager::loaderKindForDependencyKey(const QString &key, LoaderKind *out) {
    // 逐条列出来（不是遍历比较）：将来加载器多一个时，编译器会替你盯一眼"这里该跟着改"。
    // Quilt 和没见过的键落到最后那个 return false，是**正常路径的一部分** —— 界面在更早
    // 一步就用 ModpackImport::unsupportedLoaderText() 把原因说清楚了。
    if (key == ModpackImport::fabricLoaderKey()) {
        if (out != nullptr) {
            *out = LoaderKind::Fabric;
        }
        return true;
    }
    if (key == ModpackImport::forgeLoaderKey()) {
        if (out != nullptr) {
            *out = LoaderKind::Forge;
        }
        return true;
    }
    if (key == ModpackImport::neoforgeLoaderKey()) {
        if (out != nullptr) {
            *out = LoaderKind::NeoForge;
        }
        return true;
    }
    return false;
}

void VersionManager::importModpack(const QString &zipPath, const QString &instanceId,
                                   const ModpackImport::Pack &pack,
                                   const QString &installerJavaPath) {
    // 三道闸和 installLoaderVersion() 的入口一样，理由也一样：一次只做一件事。不这么拦的话，
    // 两个状态机同时开着，谁先收到别人那批文件的信号就说不清了。
    if (m_modpackInstall.active() || m_loaderInstall.active() || downloadManager->isBusy()) {
        emit versionDownloadFinished(instanceId, false,
                                     tr("已有下载或者安装任务正在进行，请等它结束再导入。"));
        return;
    }

    // 加载器**先翻译出来再开工**。翻译不了就不开工（界面在更早一步已经用
    // unsupportedLoaderText() 拦过一次，这里是数据层的第二道：数据层不该被绕过去）。
    LoaderKind kind = LoaderKind::Fabric;
    const bool hasLoader = !pack.loaderKey.isEmpty();
    const bool loaderKnown = hasLoader && loaderKindForDependencyKey(pack.loaderKey, &kind);
    if (hasLoader && !loaderKnown) {
        emit versionDownloadFinished(
            instanceId, false,
            ModpackImport::unsupportedLoaderText(pack.loaderKey, pack.loaderVersion));
        return;
    }
    if (hasLoader && !loaderInstallSupported(kind)) {
        // 接口有、实现没有的那天（比如将来加上 Quilt 的键、还没接安装）。这里必须自己发
        // 终态：installLoaderVersion() 在这种情况下只发一句中间话**不**发终态，导入的状态机
        // 会永远停在 InstallingLoader 上，界面按钮再也亮不回来。
        emit versionDownloadFinished(instanceId, false,
                                     tr("%1 在这个启动器里还没接通，整合包没有导入。")
                                         .arg(loaderDisplayName(kind)));
        return;
    }

    m_modpackInstall = ModpackInstall();
    m_modpackInstall.zipPath = zipPath;
    m_modpackInstall.instanceId = instanceId;
    m_modpackInstall.pack = pack;

    const QString loaderText =
        hasLoader ? tr(" + %1 %2").arg(loaderKnown ? loaderDisplayName(kind) : pack.loaderKey,
                                      pack.loaderVersion)
                  : QString();
    emit modpackImportMessage(tr("开始导入整合包「%1」：Minecraft %2%3。")
                                  .arg(pack.name, pack.gameVersion, loaderText));

    if (!hasLoader) {
        // 纯原版的整合包：没有加载器要塞，父版本就是原版那一份。原版也得先装齐 ——
        // 实例那份 json 的 inheritsFrom 指着它，指着一个不存在的版本启动时会报
        // "版本信息缺失"（见继承链那一段的报错）。
        m_modpackInstall.phase = ModpackInstall::Phase::WaitingParentJson;
        emit modpackImportMessage(tr("这个整合包没有模组加载器，先把原版 %1 装好...")
                                      .arg(pack.gameVersion));
        // 走的路和"装加载器时顺手补原版"完全一样（取 json → 下那一批），续接点分别在
        // onVersionJsonFetched 和 onAllDownloadsFinished 里，判据就是上面那个相位。
        fetchVersionDetails(pack.gameVersion);
        return;
    }

    m_modpackInstall.phase = ModpackInstall::Phase::InstallingLoader;
    // 钉死版本（最后一个参数）：整合包说装哪个就装哪个。装加载器那条路自己会把原版补齐
    // （它要基于原版才跑得起来），所以这一步不用再管父版本。
    installLoaderVersion(pack.gameVersion, kind, installerJavaPath, pack.loaderVersion);
}

void VersionManager::startModpackFiles() {
    if (!m_modpackInstall.active()) {
        // 状态机已经被收掉了（导入失败、或者用户又点了别的）：这份续接没人要了。
        return;
    }
    const QString id = m_modpackInstall.instanceId;
    m_modpackInstall.phase = ModpackInstall::Phase::WritingVersionJson;

    // 实例自己那份 json：**只有 id + inheritsFrom 两个字段**。薄成这样是刻意的 —— Fabric
    // 装出来的那份也这么薄：主类、assetIndex、javaVersion、游戏参数全在加载器/原版那一层，
    // getVersionJson() 会顺着继承链合并（真机上那个包是 3 层：整合包 → fabric-loader → 原版）。
    // 自己抄一份完整 json 的坏处是它会变旧账：加载器换了、原版升了，抄来的那份对不上。
    QJsonObject json;
    json[QStringLiteral("id")] = id;
    json[QStringLiteral("inheritsFrom")] = m_modpackInstall.loaderId;
    json[QStringLiteral("type")] = QStringLiteral("release");

    QString error;
    if (!writeVersionJson(id, AppPaths::versionDir(id), json, &error)) {
        failModpackImport(tr("实例的版本文件没能写进 %1：%2\n\n（这份文件是启动器认这个实例的"
                             "凭据，写不进去就等于没导入。）")
                              .arg(AppPaths::versionDir(id), error));
        return;
    }
    // 刚写下的这份是**新的**：在这之前读到的任何缓存（合并结果那张表尤其）都是旧的。
    versionJsons[id] = json;
    mergedVersionJsons.remove(id);

    const QList<ModpackImport::PackFile> files = m_modpackInstall.pack.files;
    m_modpackInstall.phase = ModpackInstall::Phase::DownloadingFiles;
    m_currentVersionDownloading = id;  // 这一批的"名字"（兜底终态那条路要用）
    emit modpackImportMessage(tr("清单里 %1 个文件，开始下载（已经下好的会按 sha1 跳过）...")
                                  .arg(files.size()));

    const QString base = AppPaths::versionDir(id);
    downloadManager->beginBatch();
    // NOTE: beginBatch() 与 endBatch() 之间**不允许**任何提前 return（见 downloadVersion
    //       顶部那段）：下面这个循环里一句早退都没有，就是为了这个。
    for (const ModpackImport::PackFile &file : files) {
        // 先试哪一条由 preferredUrlOrder() 定（**优先 cdn.modrinth.com**，实测差 3~10 倍，
        // 数字在那边）：换完次序后第二条仍然是"取不回来时再试一次"那条。清单里几条地址是
        // 同一份文件的镜像，对调本身是安全的。
        const QStringList urls = ModpackImport::preferredUrlOrder(file.urls);
        downloadManager->downloadFile(urls.value(0), base + QLatin1Char('/') + file.path,
                                      file.sha1, file.size, VerifyMode::SizeAndSha1,
                                      urls.value(1));
    }
    downloadManager->endBatch();
}

void VersionManager::extractModpackOverrides() {
    if (!m_modpackInstall.active()) {
        return;
    }
    const QString id = m_modpackInstall.instanceId;
    m_modpackInstall.phase = ModpackInstall::Phase::ExtractingOverrides;
    emit modpackImportMessage(tr("正在解开整合包里的覆盖文件（config、资源包、光影这些）..."));

    // 两个前缀一起解，顺序 = 优先级：Modrinth 的规矩是 client-overrides **盖过** overrides，
    // "谁在后谁赢"这一条是格式的一部分，不能调换。
    // NOTE: 这一步是同步起 PowerShell 等它（最慢的一段，几百 MB 的包解起来要一会儿），
    //       界面在这期间是卡住的 —— 和既有那几处解压一个形状，不是新问题。
    const QStringList roots = {QStringLiteral("overrides/"),
                               QStringLiteral("client-overrides/")};
    QString error;
    int extracted = 0;
    if (!ZipUtil::extractSubtrees({m_modpackInstall.zipPath}, AppPaths::versionDir(id), roots,
                                  &extracted, &error)) {
        failModpackImport(tr("整合包里的覆盖文件没能解开：%1\n\n（清单里那些文件已经下好在 %2 "
                             "里了，缺的是 config、资源包、光影这些。）")
                              .arg(error, AppPaths::versionDir(id)));
        return;
    }
    m_modpackInstall.extractedEntries = extracted;
    finishModpackImport();
}

void VersionManager::finishModpackImport() {
    // 要用的字段先取出来：reset() 会把它们一起清掉（忘了取的表现是那句话里名字变空的）。
    const QString id = m_modpackInstall.instanceId;
    const ModpackImport::Pack pack = m_modpackInstall.pack;
    const int skipped = pack.filesSkipped;
    const int extracted = m_modpackInstall.extractedEntries;

    // 自检：装完的东西得真的能被列表认出来。判据用**读侧那一份**（InstanceFiles::
    // resolveInstance），不自己再判一遍 —— "装是装了、列表里看不见"这个症状这个项目已经
    // 撞过两次（见 InstanceFiles.hpp 里 recognized() 那段），所以这里宁可多问一句。
    const InstanceFiles::Resolved resolved = InstanceFiles::resolveInstance(id);
    if (!resolved.recognized()) {
        failModpackImport(tr("文件都装好了，但 %1 没被认成一个实例（版本 json 没写对？）——"
                             "列表里看不到它。整个目录在 %2，删掉那个目录再导入一次。")
                              .arg(id, AppPaths::versionDir(id)));
        return;
    }

    // 显示名：realVersion 是"真实的游戏版本"，加载器那两段让「版本名」里多一句
    // "Fabric 0.16.14"（读侧见 VersionDisplay::displayParts）。写不进去只是名字难看
    // 一点，**不算失败** —— 版本本身是好的、能启动，别把用户吓一跳。
    QJsonObject fields;
    fields[QStringLiteral("realVersion")] = pack.gameVersion;
    if (!pack.loaderKey.isEmpty()) {
        LoaderKind kind = LoaderKind::Fabric;
        if (loaderKindForDependencyKey(pack.loaderKey, &kind)) {
            fields[QStringLiteral("loaderName")] = loaderDisplayName(kind);
            fields[QStringLiteral("loaderVersion")] = pack.loaderVersion;
        }
    }
    QString metaError;
    if (!updateVersionMeta(id, fields, &metaError)) {
        qWarning() << "Failed to write wtfml.meta for" << id << ":" << metaError;
        emit modpackImportMessage(tr("装好了，但没能写进 wtfml.meta（%1）——「版本名」里会少一段。")
                                      .arg(metaError));
    }

    // 少装了什么必须说出来（跳过的文件、解开的覆盖文件）—— 用户看不到的那部分最容易
    // 变成"进了游戏才发现少个 mod"。
    QString extra;
    if (skipped > 0) {
        extra = tr("（清单里有 %1 个文件按包里的说明跳过了：只给服务端用、或者没有下载地址）")
                    .arg(skipped);
    }
    if (extracted > 0) {
        extra += tr("（解开了 %1 个覆盖文件）").arg(extracted);
    }

    m_modpackInstall.reset();
    emit versionDownloadFinished(id, true,
                                 tr("整合包「%1」导入好了，实例列表里叫「%2」。%3")
                                     .arg(pack.name, id, extra));
}

void VersionManager::failModpackImport(const QString &reason) {
    // 先把加载器那半截收掉。LoaderInstall::reset() **不发**任何信号正是为了这里：整件事的
    // 终态只能发一个，否则界面会先弹一次"加载器没装成"、再弹一次"整合包没导入"。
    m_loaderInstall.reset();
    // 批次可能正开着（失败正好打在两批之间）：不放弃的话 m_batchOpen 永远是 true，之后每次
    // 下载都被"已有下载任务正在进行"顶回来（同 recoverFromInternalError 里那段）。
    downloadManager->abandonBatch();

    const QString id = m_modpackInstall.instanceId;
    m_modpackInstall.reset();
    emit versionDownloadFinished(id, false, reason);
}

// ---------------------------------------------------------------------------
// 装一个"用户起了名字的实例"（安装页那个「版本名称」框）
// ---------------------------------------------------------------------------

bool VersionManager::isNamedInstanceInstalling() const { return m_namedInstance.active(); }

void VersionManager::installNamedVanilla(const QString &instanceId, const QString &gameVersion) {
    if (m_namedInstance.active() || m_loaderInstall.active() || m_modpackInstall.active() ||
        downloadManager->isBusy()) {
        // NOTE: 忙的时候除了那句话，**也必须发终态信号** —— 界面在点下去那一刻就进了忙态，
        //       只发一条消息的话按钮再也亮不回来（"加载器安装还没接通"那一段踩过同样的坑，
        //       见 startLoaderInstall 里那段注释）。
        emit instanceInstallMessage(tr("已有安装或下载正在进行，请等它结束。"));
        emit versionDownloadFinished(instanceId, false, tr("已有安装或下载正在进行，请等它结束。"));
        return;
    }
    if (instanceId.trimmed().isEmpty() || gameVersion.isEmpty()) {
        // 名字和原版号都该由调用方验过。走到这里说明调用方漏了 —— 那也不能往下走：
        // 一个空名字会变成 `versions/` 本身，那是把整个游戏目录当实例目录用。
        qWarning() << "installNamedVanilla called with an empty name or game version";
        emit versionDownloadFinished(instanceId, false,
                                     tr("实例名或原版版本号是空的，没有开始安装。"));
        return;
    }

    m_namedInstance = NamedInstance();
    m_namedInstance.instanceId = instanceId;
    m_namedInstance.parentId = gameVersion;
    m_namedInstance.phase = NamedInstance::Phase::WaitingParentJson;

    // 名字和原版号一样 = 用户没改名字、装的就是原版本身。收尾时 writeNamedInstanceJson()
    // 会跳过那一份薄 json（写出来就是一份"指着自己"的 json），所以这里不用分开走。
    emit instanceInstallMessage(instanceId == gameVersion
                                    ? tr("正在装原版 %1...").arg(gameVersion)
                                    : tr("正在装 %1（基于原版 %2）...").arg(instanceId, gameVersion));

    // 这一次取 json 是**安装流程自己要的**，不是用户在下这个版本：相位上面已经摆好，
    // onVersionJsonFetched 里那一段会接手（它不发 versionDetailsFetched —— 发了界面会当成
    // "用户点了下载原版"，于是同一个版本下两遍、两个批次互相打架）。
    fetchVersionDetails(gameVersion);
}

void VersionManager::installNamedLoader(const QString &instanceId, const QString &gameVersion,
                                       LoaderKind kind, const QString &pinnedLoaderVersion,
                                       const QString &javaPath) {
    if (m_namedInstance.active() || m_loaderInstall.active() || m_modpackInstall.active() ||
        downloadManager->isBusy()) {
        emit instanceInstallMessage(tr("已有安装或下载正在进行，请等它结束。"));
        emit versionDownloadFinished(instanceId, false, tr("已有安装或下载正在进行，请等它结束。"));
        return;
    }
    if (instanceId.trimmed().isEmpty() || gameVersion.isEmpty()) {
        qWarning() << "installNamedLoader called with an empty name or game version";
        emit versionDownloadFinished(instanceId, false,
                                     tr("实例名或原版版本号是空的，没有开始安装。"));
        return;
    }
    if (!loaderInstallSupported(kind)) {
        // 和 installLoaderVersion() 里那道守卫同一个理由：界面也拦了一道，但数据层不该被
        // 绕过去 —— 绕过去的下场是"点了某个还没接通的加载器，结果装出来一个纯原版"。
        const QString loaderName = loaderDisplayName(kind);
        emit instanceInstallMessage(tr("%1 的安装还没接通。").arg(loaderName));
        emit versionDownloadFinished(instanceId, false,
                                     tr("%1 的安装还没接通，没有安装。").arg(loaderName));
        return;
    }

    m_namedInstance = NamedInstance();
    m_namedInstance.instanceId = instanceId;
    m_namedInstance.phase = NamedInstance::Phase::WaitingLoader;
    // parentId 现在**还不知道**：加载器那个目录名是安装器自己拼的（OptiFine）或者我们按
    // 版本号算的，无论如何都要等装完。收尾的人（finishLoaderInstall）会现取 loaderId 填进来。

    const QString loaderName = loaderDisplayName(kind);
    emit instanceInstallMessage(pinnedLoaderVersion.isEmpty()
                                    ? tr("正在装 %1（%2，基于原版 %3）...")
                                          .arg(instanceId, loaderName, gameVersion)
                                    : tr("正在装 %1（%2 %3，基于原版 %4）...")
                                          .arg(instanceId, loaderName, pinnedLoaderVersion,
                                               gameVersion));

    // 整段交给那条已经验过的路。它自己会把原版补齐（加载器版本要基于原版才跑得起来），
    // 所以这里不用先下原版。收尾在 finishLoaderInstall() 里 —— 那里会先写实例那份薄 json，
    // 再发**唯一**那个终态信号。
    installLoaderVersion(gameVersion, kind, javaPath, pinnedLoaderVersion);
}

bool VersionManager::writeNamedInstanceJson() {
    if (!m_namedInstance.active()) {
        return false;
    }
    const QString id = m_namedInstance.instanceId;
    const QString parentId = m_namedInstance.parentId;
    if (id == parentId) {
        // 用户没改名字：实例就是父版本本身。旁边多写一份"指着自己的"json 只会在实例列表里
        // 多出一个和原版重名的条目（而且那个目录已经存在、是同一份）。
        return true;
    }

    // 和整合包实例、和 Fabric 装出来的那份**同一个形状**（见 startModpackFiles 上面那段）：
    // 只有 id + inheritsFrom。主类、客户端 jar、assetIndex、游戏参数全在父版本那一层，
    // getVersionJson() 会顺着继承链合并。抄一份完整 json 的坏处是它会变旧账。
    QJsonObject json;
    json[QStringLiteral("id")] = id;
    json[QStringLiteral("inheritsFrom")] = parentId;
    json[QStringLiteral("type")] = QStringLiteral("release");

    QString error;
    if (!writeVersionJson(id, AppPaths::versionDir(id), json, &error)) {
        failNamedInstance(tr("实例的版本文件没能写进 %1：%2\n\n（这份文件是启动器认这个实例的"
                             "凭据，写不进去就等于没装成。）")
                              .arg(AppPaths::versionDir(id), error));
        return false;
    }
    // 刚写下的这份是**新的**：在这之前读到的任何缓存（合并结果那张表尤其）都是旧的，
    // 留着会让"刚装好的实例"用回写之前的样子启动（或者压根读不到）。
    versionJsons[id] = json;
    mergedVersionJsons.remove(id);
    return true;
}

void VersionManager::finishNamedInstance(const QString &parentId, const QString &parentMessage) {
    if (!m_namedInstance.active()) {
        // 状态已经被收掉了（用户又点了一次、或者上一步失败了）：这份续接没人要了。
        return;
    }
    const QString id = m_namedInstance.instanceId;
    m_namedInstance.parentId = parentId;

    if (!writeNamedInstanceJson()) {
        // 写不进去时它自己已经发过失败终态了（见上面那个函数）。这里只能退出。
        return;
    }

    m_namedInstance.reset();
    emit versionDownloadFinished(
        id, true,
        id == parentId ? tr("原版 %1 已就绪。%2").arg(parentId, parentMessage)
                       : tr("实例「%1」装好了（基于 %2）。%3").arg(id, parentId, parentMessage));
}

void VersionManager::failNamedInstance(const QString &reason) {
    // 父版本是加载器时，加载器那半截可能还在跑：一起收掉。LoaderInstall::reset() **不发**
    // 任何信号正是为了这里 —— 整件事的终态只能发一个，否则界面会先弹一次"加载器没装成"、
    // 再弹一次"实例没装成"。
    m_loaderInstall.reset();
    // 批次可能正开着（失败正好打在两批之间）：不放弃的话 m_batchOpen 永远是 true，之后每次
    // 下载都被"已有下载任务正在进行"顶回来（同 failModpackImport 里那段）。
    downloadManager->abandonBatch();

    const QString id = m_namedInstance.instanceId;
    m_namedInstance.reset();
    emit versionDownloadFinished(id, false, reason);
}

bool VersionManager::writeVersionJson(const QString &versionId, const QString &destinationPath,
                                      const QJsonObject &json, QString *errorOut) {
    if (!QDir().mkpath(destinationPath)) {
        if (errorOut != nullptr) {
            *errorOut = tr("建不出目录 %1。").arg(destinationPath);
        }
        return false;
    }
    const QString jsonPath =
        destinationPath + QLatin1Char('/') + versionId + QStringLiteral(".json");
    // QSaveFile：先写同目录下的临时文件、commit() 那一下才改名盖上去 —— 断电或者崩溃只会
    // 留下旧内容，不会留下半截 json（半截 json 下次启动时读不到 assetIndex / javaVersion，
    // 症状是"这个版本突然启动不了"，而且看不出是谁弄坏的）。
    QSaveFile file(jsonPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorOut != nullptr) {
            *errorOut = tr("写不进 %1（%2）。").arg(jsonPath, file.errorString());
        }
        qWarning() << "Failed to save version JSON to" << jsonPath << file.errorString();
        return false;
    }
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorOut != nullptr) {
            *errorOut = tr("写 %1 的时候失败了（%2）。").arg(jsonPath, file.errorString());
        }
        qWarning() << "Failed to commit version JSON to" << jsonPath;
        return false;
    }
    qDebug() << "Saved version JSON to" << jsonPath;
    return true;
}

void VersionManager::adoptInstallerVersion(const QString &version) {
    // 到这一步相位是 FetchingInstallerList：版本号定了、安装器还没下。两条路（版本表挑出来的、
    // 整合包钉死的）都在这个相位上进来，下面那句 deferLoaderStep 的守卫认的就是它。
    // NOTE: 消息**不在这儿发** —— "挑中 xxx"和"正在装指定的 xxx"是两句不同的话，由调用方
    //       在调它之前自己说。
    const bool isForge = m_loaderInstall.kind == LoaderKind::Forge;
    m_loaderInstall.loaderVersion = version;
    m_loaderInstall.loaderId =
        isForge ? LoaderMeta::forgeLoaderId(m_loaderInstall.gameVersion, version)
                : LoaderMeta::neoforgeLoaderId(version);
    m_loaderInstall.installerUrl =
        isForge ? LoaderMeta::forgeInstallerUrl(m_loaderInstall.gameVersion, version)
                : LoaderMeta::neoforgeInstallerUrl(version);
    // 盘上那个 jar 就叫上游那个名字（forge-1.20.1-47.4.10-installer.jar /
    // neoforge-21.1.215-installer.jar），用户去 cache/installers/ 里看一眼能对上。OptiFine
    // 那条路不走这里（它的地址最后两段是 type/patch，不是文件名），由它自己填。
    m_loaderInstall.installerFileName =
        QFileInfo(QUrl(m_loaderInstall.installerUrl).path()).fileName();

    deferLoaderStep(LoaderInstall::Phase::FetchingInstallerList,
                    [this] { startInstallerDownload(); });
}

// ---------------------------------------------------------------------------
// "这个加载器有哪些版本可选"：一次只读的询问（安装页那个可展开的列表用）
// ---------------------------------------------------------------------------
//
// 和上面那条安装路的关系：**这条只是问，什么都不装**。用户在安装页上把四个加载器挨个点开
// 看看，一次安装都还没发生 —— 所以它刻意不复用 m_loaderInstall 那套状态机（用的话点开一个
// 加载器就会把相位弄成"正在装"，isLoaderInstalling() 当场变 true，底栏和按钮全跟着错）。

void VersionManager::requestLoaderVersions(const QString &gameVersion, LoaderKind kind) {
    const QString wanted = gameVersion.trimmed();

    // 上一发还在飞就先掐掉：界面上只有**一处**选择器，用户点开另一个加载器就是"换一份列表"，
    // 两份回复乱序到达会把 Forge 的版本号填进 Fabric 那个列表里。abort 之后 finished()
    // 照样会来一发，回复处理函数靠"这一份还是我手上这份吗"把它丢掉。
    if (m_loaderVersionRequest.reply != nullptr) {
        m_loaderVersionRequest.reply->abort();
        m_loaderVersionRequest.reply = nullptr;
    }
    m_loaderVersionRequest.kind = kind;
    m_loaderVersionRequest.gameVersion = wanted;

    if (wanted.isEmpty()) {
        emit loaderVersionsFetched(wanted, kind, false, QStringList(),
                                   tr("还没选要装哪个版本，先在上面挑一个。"));
        return;
    }
    if (!loaderInstallSupported(kind)) {
        emit loaderVersionsFetched(wanted, kind, false, QStringList(),
                                   tr("%1 的安装还没接通。").arg(loaderDisplayName(kind)));
        return;
    }

    // OptiFine 的列表**必须跟着下载源**走：列出来的版本要能用同一个源装下去。镜像那条路的
    // 地址最后两段是 type/patch（见 LoaderMeta::optifineMirrorPathFromEdition），官方那条路
    // 得先过广告页拿 token —— 两份列表混着用就会"列出来了但下不了"。
    //
    // 另外三个没这个问题（Fabric 只有官方一个源；两份 maven 清单官方就是发布处），一律走官方。
    // NOTE: 两份 maven 清单这里**不换镜像**，和安装那条路（fetchInstallerList，镜像挑不出来
    //       再退官方）不一样。那条教训是实测的：镜像上那份 Forge 版本表是旧文件（1782 B、
    //       最新只到 1.15.2），照着它列会给用户一个"1.16 以后都没有 Forge"的假结论。
    //       列表这件事对"全不全"比对"快不快"敏感得多，所以宁可直接问官方。
    QString url;
    switch (kind) {
    case LoaderKind::Fabric:
        url = LoaderMeta::fabricLoaderListUrl(wanted);
        break;
    case LoaderKind::Forge:
        url = LoaderMeta::forgeMavenMetadataUrl();
        break;
    case LoaderKind::NeoForge:
        url = LoaderMeta::neoforgeMavenMetadataUrl();
        break;
    case LoaderKind::OptiFine:
        url = downloadManager->useMirror() ? LoaderMeta::optifineMirrorListUrl(wanted)
                                           : LoaderMeta::optifineDownloadsPageUrl();
        break;
    }
    fetchLoaderVersions(url);
}

void VersionManager::fetchLoaderVersions(const QString &url) {
    if (loaderNetwork == nullptr) {
        loaderNetwork = new QNetworkAccessManager(this);
    }
    const QUrl target(url);
    QNetworkRequest request(target);
    // 和另外几处元数据请求同一个超时。最大的一份是 Forge 那个 211 KB 的 maven 清单，
    // 30 秒也够（实测挂代理一两秒）。
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetwork->get(request);
    m_loaderVersionRequest.reply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onLoaderVersionsReply(reply); });
}

void VersionManager::onLoaderVersionsReply(QNetworkReply *reply) {
    reply->deleteLater();
    // 这一份还是我手上这份吗？不是就说明调用方已经换了目标（abort 掉的那一份也会走到这里），
    // 这份回复没人要了。
    if (m_loaderVersionRequest.reply != reply) {
        return;
    }
    // 先把两个字段抄下来再 clear：下面几条 emit 都要用它们，而 clear 之后就没得读了。
    const LoaderKind kind = m_loaderVersionRequest.kind;
    const QString gameVersion = m_loaderVersionRequest.gameVersion;
    m_loaderVersionRequest.clear();

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        emit loaderVersionsFetched(
            gameVersion, kind, false, QStringList(),
            tr("拿不到 %1 %2 的版本列表（%3）。检查网络后重试。")
                .arg(loaderDisplayName(kind), gameVersion, reply->errorString()));
        return;
    }

    const LoaderVersionParse parsed = parseLoaderVersions(kind, gameVersion, body);
    if (!parsed.error.isEmpty()) {
        emit loaderVersionsFetched(gameVersion, kind, false, QStringList(), parsed.error);
        return;
    }
    emit loaderVersionsFetched(gameVersion, kind, true, parsed.versions, QString());
}

VersionManager::LoaderVersionParse VersionManager::parseLoaderVersions(LoaderKind kind,
                                                                     const QString &gameVersion,
                                                                     const QByteArray &body) const {
    LoaderVersionParse result;
    LoaderMeta::LoaderVersionList entries;
    switch (kind) {
    case LoaderKind::Fabric: {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (!doc.isArray()) {
            result.error = tr("Fabric 的版本列表读不懂（%1）。").arg(parseError.errorString());
            return result;
        }
        entries = LoaderMeta::listFabricLoaderVersions(doc.array());
        break;
    }
    case LoaderKind::Forge:
        entries = LoaderMeta::listForgeVersions(body, gameVersion);
        break;
    case LoaderKind::NeoForge:
        entries = LoaderMeta::listNeoForgeVersions(body, gameVersion);
        break;
    case LoaderKind::OptiFine:
        if (downloadManager->useMirror()) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
            if (!doc.isArray()) {
                result.error =
                    tr("OptiFine 的版本列表读不懂（%1）。").arg(parseError.errorString());
                return result;
            }
            entries = LoaderMeta::listOptifineVersionsFromMirror(doc.array(), gameVersion);
        } else {
            entries = LoaderMeta::listOptifineVersionsFromDownloadsPage(QString::fromUtf8(body),
                                                                       gameVersion);
        }
        break;
    }

    result.versions = LoaderMeta::loaderVersionStrings(entries);
    if (result.versions.isEmpty()) {
        // 四种加载器里，"这个游戏版本一个能装的都没有"都是**真结论**（不是网络问题）：
        // 比如 Forge 还没支持某个刚发的快照、OptiFine 只发了预览版。所以这里如实说，
        // 而且界面**不该**拿它去弹一个"下载失败"的框。
        result.error = tr("%1 还没有支持 %2 的版本。").arg(loaderDisplayName(kind), gameVersion);
    }
    return result;
}

void VersionManager::installLoaderVersion(const QString &gameVersion, LoaderKind kind,
                                         const QString &javaPath,
                                         const QString &pinnedLoaderVersion) {
    if (m_loaderInstall.active()) {
        qWarning() << "A loader install is already running, refusing to start another.";
        emit loaderInstallMessage(tr("已经有一个加载器正在安装，等它结束再试。"));
        return;
    }
    if (!loaderInstallSupported(kind)) {
        // 界面那边也会拦下来并弹一句"还没做"，这里再兜一道：数据层不该被绕过去，
        // 否则症状是"点了某个还没接通的加载器，结果装出来一个纯原版"。
        emit loaderInstallMessage(
            tr("%1 的安装还没接通。").arg(loaderDisplayName(kind)));
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
    m_loaderInstall.pinnedVersion = pinnedLoaderVersion.trimmed();

    // 第一步就分开了：Fabric 拉的是"加载器版本列表"，Forge/NeoForge 拉的是"安装器该用
    // 哪个版本"，OptiFine 连"版本表"都和前三个不是一回事（镜像一条 JSON、官方一张 203 KB
    // 的网页），接口完全不是一回事（见 LoaderMeta.hpp 顶部那段）。
    if (kind == LoaderKind::Fabric) {
        // 版本被钉死了就别去问版本表了 —— 直接拉那个版本的 profile。
        // NOTE: 这一步对整合包是**必须**的：它的依赖表里写着 `fabric-loader: 0.16.14`，
        //       包里那些模组正是按这个版本配的，换成"最新的正式版"是另一回事（多半也能跑，
        //       但那是猜）。镜像那份旧表挑不出来的情况也因此完全绕开了。
        if (!m_loaderInstall.pinnedVersion.isEmpty()) {
            m_loaderInstall.loaderVersion = m_loaderInstall.pinnedVersion;
            m_loaderInstall.loaderId =
                LoaderMeta::fabricLoaderId(gameVersion, m_loaderInstall.pinnedVersion);
            m_loaderInstall.phase = LoaderInstall::Phase::FetchingProfile;
            emit loaderInstallMessage(tr("正在装指定的 %1 %2（不去问版本列表了）...")
                                          .arg(m_loaderInstall.loaderName,
                                               m_loaderInstall.loaderVersion));
            fetchFabricProfile();
            return;
        }

        m_loaderInstall.phase = LoaderInstall::Phase::FetchingList;
        emit loaderInstallMessage(tr("正在获取 %1 %2 的加载器版本列表...")
                                      .arg(m_loaderInstall.loaderName, gameVersion));
        fetchFabricLoaderList();
        return;
    }

    m_loaderInstall.phase = LoaderInstall::Phase::FetchingInstallerList;
    emit loaderInstallMessage(
        tr("正在获取 %1 %2 该用哪个版本...").arg(m_loaderInstall.loaderName, gameVersion));
    if (kind == LoaderKind::OptiFine) {
        // 钉死了就不用去问版本表 —— 和下面 Forge/NeoForge 那条一个道理，只是它得排在
        // "OptiFine 分支"**里面**（那一段的钉死判断在分支之后，够不着这里）。
        //
        // NOTE: 这一条**以前漏了**：OptiFine 的分支无条件去拉版本表，于是"钉死一个 OptiFine
        //       版本"被无声忽略 —— 用户在安装页上选了 H7，装出来的是版本表里那条最新的 H9。
        //       症状还特别难认：装是装成了、版本目录也在，只是不是他选的那个。
        if (!m_loaderInstall.pinnedVersion.isEmpty()) {
            emit loaderInstallMessage(tr("正在装指定的 %1 %2（不去问版本表了）...")
                                          .arg(m_loaderInstall.loaderName,
                                               m_loaderInstall.pinnedVersion));
            adoptOptifineEdition(m_loaderInstall.pinnedVersion,
                                 LoaderMeta::optifineMirrorPathFromEdition(
                                     m_loaderInstall.pinnedVersion),
                                 downloadManager->mirrorPreferred());
            return;
        }
        fetchOptifineVersionList();
        return;
    }
    // 同上：钉死了就不用去问版本表（那张表在镜像上还是旧的，见 LoaderInstall::listOfficialUrl）。
    // NOTE: 相位先摆好再调 adoptInstallerVersion —— 里面那句 deferLoaderStep 的守卫要的
    //       就是 FetchingInstallerList。
    if (!m_loaderInstall.pinnedVersion.isEmpty()) {
        emit loaderInstallMessage(tr("正在装指定的 %1 %2（不去问版本表了）...")
                                      .arg(m_loaderInstall.loaderName,
                                           m_loaderInstall.pinnedVersion));
        adoptInstallerVersion(m_loaderInstall.pinnedVersion);
        return;
    }
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
    // 这一步在"原版已经装好"时**直接起加载器那个批次**，而这里还在网络回复的槽里。
    deferLoaderStep(LoaderInstall::Phase::FetchingProfile,
                    [this] { startParentOrLoaderFiles(); });
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
    // 版本表本身也走镜像：Forge 那份 1.7 KB 在 files.minecraftforge.net 上，NeoForge 那份
    // 63 KB 在 maven.neoforged.net 上，两个 host 都在 DownloadManager::mirrorUrl() 里认得了。
    // NOTE: 这条路**不是** downloadManager 发的（它只是 GET 一段文本、没有落盘），所以镜像
    //       得在这儿自己换一次 —— downloadFile() 那条路不会替它换。
    //
    // NOTE: 但镜像那份 Forge 版本表**是旧的**（2026-09-22 实测：镜像 1782 B、最新的游戏
    //       版本停在 1.15.2；官方 4111 B、一直到 26.3），拿它挑新版本会挑出一个**假**结论
    //       ——「Forge 还没有支持 26.2 的版本。」所以这里不假定镜像能用：镜像发一发，
    //       挑不出来就换官方那条再发一发（见 retryInstallerListFromOfficial）。镜像哪天
    //       跟上了，这条路自动回到镜像。
    m_loaderInstall.listOfficialUrl =
        m_loaderInstall.kind == LoaderKind::Forge ? LoaderMeta::forgePromotionsUrl()
                                                  : LoaderMeta::neoforgeMavenMetadataUrl();
    const QString url = downloadManager->endpoint(m_loaderInstall.listOfficialUrl);
    m_loaderInstall.listRequestWasMirror = (url != m_loaderInstall.listOfficialUrl);
    m_loaderInstall.listRetriedOfficial = false;
    requestInstallerList(url);
}

void VersionManager::requestInstallerList(const QString &url) {
    // NOTE: 这里必须写成"先落到一个具名变量"，不能写成 QNetworkRequest request(QUrl(url)) ——
    //       那个形状在 C++ 里会被当成函数声明（most vexing parse），报的错还很难认
    //       （"request 不是类类型"）。
    const QUrl target(url);
    QNetworkRequest request(target);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onInstallerListReply(reply); });
}

bool VersionManager::retryInstallerListFromOfficial() {
    // 只在"这一发是镜像发的、而且还没退过"的时候成立。退过一次还是挑不出来，那就是
    // 官方那份里真的没有 —— 不能再往回退，否则镜像、官方来回转。
    if (!m_loaderInstall.listRequestWasMirror || m_loaderInstall.listRetriedOfficial) {
        return false;
    }
    m_loaderInstall.listRetriedOfficial = true;
    m_loaderInstall.listRequestWasMirror = false;
    emit loaderInstallMessage(
        tr("镜像上的 %1 版本表里没有 %2（镜像那份是旧的），改用官方那份再挑一次...")
            .arg(m_loaderInstall.loaderName, m_loaderInstall.gameVersion));
    requestInstallerList(m_loaderInstall.listOfficialUrl);
    return true;
}

void VersionManager::onInstallerListReply(QNetworkReply *reply) {
    reply->deleteLater();
    // 状态已经换过（上一次失败收了尾、或者用户又点了一次）：这份回复没人要了。
    if (m_loaderInstall.phase != LoaderInstall::Phase::FetchingInstallerList) {
        return;
    }

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        if (retryInstallerListFromOfficial()) {
            return;
        }
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
        // 挑不出来有两种可能：镜像那份是旧的（**多数情况**，见 listOfficialUrl 的注释），
        // 或者上游真的还没支持这个游戏版本。先问一次官方那份，两个都没有才是后者。
        if (retryInstallerListFromOfficial()) {
            return;
        }
        // 和 Fabric 那条一样：这不是网络问题，也**不能**退回"装原版"——用户要的是带
        // 加载器的版本，装出来一个纯原版是骗人。
        failLoaderInstall(tr("%1 还没有支持 %2 的版本。")
                              .arg(m_loaderInstall.loaderName, m_loaderInstall.gameVersion));
        return;
    }

    emit loaderInstallMessage(tr("挑中 %1 %2（%3）。")
                                  .arg(m_loaderInstall.loaderName, pick.version,
                                       pick.stable ? tr("官方推荐的正式版")
                                                   : tr("版本表里最新的那个")));
    // 剩下的（loaderId / 安装器地址 / 文件名 / 排下一轮）和"整合包钉死版本"那条路共用一份：
    // 两块地方各写一遍的话，迟早只有一边记得改（比如 installerFileName 那段注释里的坑）。
    adoptInstallerVersion(pick.version);
}

void VersionManager::startInstallerDownload() {
    // 文件名是拉版本表那一步定下来的（installerFileName），这里不再从地址里现切：两条路
    // 的来源本来就不一样——Forge/NeoForge 取地址最后一段（和上游同名），OptiFine 那边地址
    // 最后两段是 type/patch、只能用它在版本表里挑中的那个文件名。版本目录名那边不这么干
    // 是另一回事：那个名字是别的启动器认的，不能改。
    if (m_loaderInstall.installerFileName.isEmpty()) {
        failLoaderInstall(tr("没有拿到 %1 安装器的文件名，安装中止。")
                              .arg(m_loaderInstall.loaderName));
        return;
    }
    m_loaderInstall.installerPath =
        AppPaths::installerCacheDir() + QLatin1Char('/') + m_loaderInstall.installerFileName;

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
    // 安装器 jar 也走镜像（6 MB 上下，是这条路上最大的一笔）。镜像上那份和官方是同一个
    // 文件（见 2026-09-22 的实测）。installerUrl 本身**不改**：盘上那个文件名是从它切的，
    // 两条路上游的文件名一模一样。
    const QString installerUrl =
        downloadManager->useMirror()
            ? downloadManager->endpoint(m_loaderInstall.installerUrl)
            : m_loaderInstall.installerUrl;
    // 第六个参数是官方那条地址：安装器 jar 是这条路上最大的一笔（6 MB 上下），
    // 镜像取不回来时按官方再试一次，比整条安装从头再来划算。
    downloadManager->downloadFile(installerUrl, m_loaderInstall.installerPath,
                                  QString(), -1, VerifyMode::ExistsOnly,
                                  m_loaderInstall.installerUrl);
    downloadManager->endBatch();
}

bool VersionManager::retryInstallerDownloadAlternate() {
    if (m_loaderInstall.kind != LoaderKind::Forge || m_loaderInstall.retriedAltCoordinate) {
        return false;
    }
    const QString alt = LoaderMeta::forgeInstallerUrlAlternate(m_loaderInstall.gameVersion,
                                                              m_loaderInstall.loaderVersion);
    if (alt.isEmpty() || alt == m_loaderInstall.installerUrl) {
        return false;
    }
    m_loaderInstall.retriedAltCoordinate = true;
    // 文件名是从地址最后一段切的、两种坐标的文件名不一样（`...11.15.1.2318-installer.jar`
    // vs `...11.15.1.2318-1.8.9-installer.jar`），所以这三样都得跟着换 —— 只换地址会让
    // 下载落到上一轮那个名字上，装完核对时对不上。
    m_loaderInstall.installerUrl = alt;
    m_loaderInstall.installerFileName = alt.section(QLatin1Char('/'), -1);
    m_loaderInstall.installerPath = AppPaths::installerCacheDir() + QLatin1Char('/') +
                                    m_loaderInstall.installerFileName;
    emit loaderInstallMessage(
        tr("%1 %2 的安装器不在常规坐标上（老游戏版本是这样），换一种坐标再下一遍...")
            .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion));
    startInstallerDownload();
    return true;
}

bool VersionManager::ensureLauncherProfiles(const QString &minecraftDir, bool *createdOut,
                                           QString *errorOut) const {
    if (createdOut != nullptr) {
        *createdOut = false;
    }
    const QString path =
        minecraftDir + QLatin1Char('/') + LoaderMeta::launcherProfilesFileName();
    if (QFile::exists(path)) {
        // 已经有就**一个字都不动**：这个文件是原版启动器和 PCL 都在写的东西，里面可能
        // 是用户真正的启动器档案。安装器会往里面**加**一条 profile，加完照样是有用的；
        // 我们覆盖它就是把人家的东西弄丢。
        return true;
    }

    // 没有才补一份最小的。它是安装器的硬前置，实测缺了这条：
    //   There is no minecraft launcher profile in "...", you need to run the launcher first!
    //   There was an error during installation      （退出码 1，一个字节的库都不下）
    QDir().mkpath(minecraftDir);
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

VersionManager::InstallerOptions VersionManager::parseInstallerHelp(const QString &helpText) {
    InstallerOptions options;
    options.probed = true;
    // 只做"整段里有没有这个串"的判断：两个安装器的选项名互不包含（实测 1.8.9 那份帮助
    // 545 B、`--installClient` 和 `--mirror` 一个都没有；NeoForge 26.2 那份 2572 B、两个都有），
    // 不需要更细的解析 —— 真要按行解析 joptsimple 的排版反而更容易被下一个版本改坏。
    options.supportsInstallClient = helpText.contains(QStringLiteral("--installClient"));
    options.supportsMirror = helpText.contains(QStringLiteral("--mirror"));
    return options;
}

QStringList VersionManager::forgeInstallerArguments(const QString &installerPath,
                                                    const QString &targetDir, bool useMirror,
                                                    bool supportsMirror) {
    // `--installClient` 后面那个目录**必须**给：不给的话安装器会落到 user.home/.minecraft
    // （这台机器上是 C:\Users\User\.minecraft），装到一个用户根本不知道的地方去。
    QStringList args{QStringLiteral("-jar"), installerPath, QStringLiteral("--installClient"),
                     targetDir};
    // 镜像模式下把安装器**自己**那条下载路也拨到镜像上。它的默认行为是去问 Forge 的镜像
    // 列表、挑一个第三方镜像（本机上是 creeperhost），实测慢一倍：48 个库 61 秒 vs 30 秒。
    // 它只对库生效 —— 原版那几个库（libraries.minecraft.net）它照旧走自己那条路。
    // 探到它不认 `--mirror` 就**不能给**：老安装器（joptsimple）把不认识的选项当致命错误，
    // 当场退出码 1。宁可这一条路慢一点，也不能让整个安装死在"多给了一个参数"上。
    if (useMirror && supportsMirror) {
        args << QStringLiteral("--mirror") << DownloadManager::mirrorMavenRootUrl();
    }
    return args;
}

void VersionManager::runInstaller() {
    const bool optifine = m_loaderInstall.kind == LoaderKind::OptiFine;

    // 往哪个目录装：Forge/NeoForge 直接装进真游戏目录（它们的安装器认 `--installClient`
    // 给的那个目录）；OptiFine 装进**我们自己建的一个临时目录**，因为它的安装器参数整个
    // 丢掉、只从 APPDATA 算（见 LoaderMeta.hpp 里 OptiFine 那一节）。
    //
    // NOTE: 换 APPDATA 这一招**只在 Windows 上成立**。安装器是按 os.name 分支算游戏目录的：
    //       Windows 走 `%APPDATA%/.minecraft`，Linux 走 `user.home/.minecraft` —— 后者改
    //       环境变量根本改不动（Java 是从系统里取的，不是从环境变量里读的），于是它会跑到
    //       用户真实的 ~/.minecraft 里去动手。那种平台上宁可不装，也不能这么干。
    QString targetDir = AppPaths::minecraftDir();
    if (optifine) {
#ifdef Q_OS_WIN
        QString error;
        if (!prepareOptifineWorkDir(&error)) {
            failLoaderInstall(error);
            return;
        }
        targetDir = optifineWorkMinecraftDir();
#else
        failLoaderInstall(tr("OptiFine 的安装器只认 Windows 上的 APPDATA，这个平台上还没接通。"));
        return;
#endif
    }

    bool created = false;
    QString error;
    if (!ensureLauncherProfiles(targetDir, &created, &error)) {
        failLoaderInstall(error);
        return;
    }
    // OptiFine 那一份是补给我们自己的临时目录的（每次装必然是新建），用户没必要知道。
    if (created && !optifine) {
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

    // 这一次用的 Java 和目录存成成员：探完 `--help` 之后还要用同一份起真正的安装（见
    // probeInstallerOptions / startInstallerProcess）。
    m_installerProgram = program;
    m_installerTargetDir = targetDir;
    m_installerOptions = InstallerOptions();

    // 先探一次安装器的命令行开关（只有 Forge/NeoForge 探：OptiFine 那个入口是类名，
    // 压根没有 `--help` 这回事）。
    if (!optifine) {
        probeInstallerOptions();
        return;
    }
    startInstallerProcess();
}

void VersionManager::probeInstallerOptions() {
    m_probeOutput.clear();
    m_installerStartedAt = QDateTime::currentMSecsSinceEpoch();
    // phase 要在 start() **之前**设：进程起不来时 errorOccurred 会回来，那条路要靠
    // phase 认出"这次是探针"（见 onInstallerError）。
    m_loaderInstall.phase = LoaderInstall::Phase::ProbingInstallerOptions;
    emit loaderInstallMessage(tr("先问一下 %1 的安装器认哪些命令行开关...")
                                  .arg(m_loaderInstall.loaderName));

    installerProcess->setProgram(m_installerProgram);
    installerProcess->setArguments({QStringLiteral("-jar"), m_loaderInstall.installerPath,
                                    QStringLiteral("--help")});
    // 环境用**系统的**：探针不该带 OptiFine 那个换过 APPDATA 的环境（那条路本来也不探）。
    installerProcess->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    // 工作目录挑安装器 jar 自己所在的那个目录（cache/installers/）：老安装器起来会往 cwd
    // 里写一个 `<jar 名>.log`，扔在缓存目录里比扔进用户的游戏目录干净。
    installerProcess->setWorkingDirectory(
        QFileInfo(m_loaderInstall.installerPath).absolutePath());
    installerProcess->start();
    installerTimer->start();
}

void VersionManager::onProbeFinished(int exitCode, bool crashed) {
    if (installerTimer != nullptr) {
        installerTimer->stop();
    }
    onInstallerOutput();  // 收尾前把剩下的输出读干净（这一相位下它读进 m_probeOutput）

    const QString help = QString::fromLocal8Bit(m_probeOutput);
    if (crashed || exitCode != 0 || help.trimmed().isEmpty()) {
        // 探不出来（起不来 / 超时被杀 / 一声不吭）：按"宁可信其有"往下走，让真正那一次
        // 安装去撞南墙、报它自己的错 —— 探针是为了少一次莫名其妙的报错，不能反过来变成
        // 新的把能装的版本拦下来的关。
        m_installerOptions = InstallerOptions();
        qWarning() << "Installer option probe inconclusive: exit" << exitCode << "crashed" << crashed
                   << installerOutputTail(m_probeOutput);
    } else {
        m_installerOptions = parseInstallerHelp(help);
        qDebug() << "Installer options:" << m_installerOptions.probed
                 << m_installerOptions.supportsInstallClient << m_installerOptions.supportsMirror;
    }

    if (!m_installerOptions.supportsInstallClient) {
        // 老安装器把"装客户端"做成了图形界面（不给参数就弹窗等人点），命令行里没有这条路。
        // 本启动器没法替用户点那个窗口，所以**明说**，不要拿一段 Java 堆栈糊过去。
        failLoaderInstall(tr("%1 %2 的安装器没有命令行安装这条路（它的帮助里只有 --extract / "
                             "--installServer / --offline）—— 那个年代的安装器装客户端只能靠"
                             "它的图形界面、要人点一下，本启动器替你点不了。\n"
                             "这个版本请用原版启动器或 PCL 装一次 %1：装完本启动器认得出它、"
                             "也能正常启动。1.12.2 及更新的游戏版本本启动器都能自动装"
                             "（1.12.2 / 26.2 实测过）。")
                              .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion,
                                   m_loaderInstall.gameVersion));
        return;
    }

    if (m_installerOptions.probed && !m_installerOptions.supportsMirror &&
        downloadManager->mirrorPreferred()) {
        emit loaderInstallMessage(tr("这个安装器不认 --mirror，它自己下库那一段只好走官方"
                                     "（启动器这边下的库仍然走镜像）。"));
    }
    startInstallerProcess();
}

void VersionManager::startInstallerProcess() {
    const bool optifine = m_loaderInstall.kind == LoaderKind::OptiFine;

    m_installerOutput.clear();
    m_installerTicks = 0;
    m_installerStartedAt = QDateTime::currentMSecsSinceEpoch();

    // phase 要在 start() **之前**设：进程起不来时 errorOccurred 会回来，那条路要靠
    // phase 认出"这次是安装器的事"。
    m_loaderInstall.phase = LoaderInstall::Phase::RunningInstaller;
    if (optifine) {
        emit loaderInstallMessage(
            tr("正在运行 %1 %2 的安装器...（它本身是个图形程序，我们用的是它的无界面入口；"
               "这一步最慢：它要把原版的客户端 jar 抄进新版本目录，再把两个库放好）")
                .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion));
    } else {
        emit loaderInstallMessage(
            tr("正在运行 %1 %2 的安装器...（这一步最慢：它要下十几个库，还要把原版的"
               "客户端 jar 打两遍补丁）")
                .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion));
    }

    // program 和 arguments 都是**常驻**的（和下面那个环境一样）：`start()` 不带参数走的
    // 就是上一次设的那个 program。所以这条路每次都得把它设一遍 —— 漏了它的症状是
    // "安装器起不来（那个 Java 进程没能启动）"，因为 QProcess 拿着一个空 program 去 start。
    // （探 `--help` 那一次也是同一个 QProcess：它的 program/arguments 就是在这儿被覆盖掉的。）
    installerProcess->setProgram(m_installerProgram);

    // 环境**每次都显式设一遍**，不是"只在 OptiFine 时设"：QProcess 的环境是常驻的
    // （set 一次之后每次 start 都用它），OptiFine 那次换过 APPDATA 之后不还原的话，下一次
    // 装 Forge/NeoForge 会把 APPDATA 指到一个**已经删掉的**临时目录上。
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (optifine) {
        // 只有 Windows 能走到这里（上面那道守卫）。
        //
        //  * 入口是 `optifine.Installer`：jar 的 Main-Class 是 `optifine.InstallerFrame`
        //    （Swing 窗口，headless 下当场 HeadlessException），真正的无界面入口是前者，
        //    而且**一个参数都不用给**（版本号它自己从自己那份 jar 里读）。
        //  * -Djava.awt.headless=true 是**必须**的：它出错时会弹一个框等人点，不设这个的话
        //    进程永不退出，只能等我们那个半小时的超时把它杀掉。
        //  * APPDATA 指到临时目录：它只认这一个地方，命令行里给目录它看都不看。
        env.insert(QStringLiteral("APPDATA"), optifineWorkRoot());
        installerProcess->setArguments({QStringLiteral("-Djava.awt.headless=true"),
                                        QStringLiteral("-cp"), m_loaderInstall.installerPath,
                                        QStringLiteral("optifine.Installer")});
    } else {
        // 和原版启动器一样的调用方式。命令行本体在 forgeInstallerArguments() 里（是个纯
        // 函数，`--mirror` 那一条就是靠它被钉住的，见 verify 的 V8）。
        // 传的是 mirrorPreferred() 而不是 useMirror()：镜像正被降级时**不能**再把安装器
        // 拨到它那边去 —— 降级的判据是"这条链子慢到 10 秒给不出 64 KB"，和走的是哪条
        // 路由无关（maven 那一支实测快，但真慢起来也不该硬塞）。
        installerProcess->setArguments(forgeInstallerArguments(
            m_loaderInstall.installerPath, m_installerTargetDir,
            downloadManager->mirrorPreferred(), m_installerOptions.supportsMirror));
    }
    installerProcess->setProcessEnvironment(env);
    installerProcess->setWorkingDirectory(m_installerTargetDir);
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
    //
    // 探 `--help` 那一次是**同一个** QProcess，输出分流到另一个缓冲区（见 m_probeOutput）。
    QByteArray *sink = m_loaderInstall.phase == LoaderInstall::Phase::ProbingInstallerOptions
                           ? &m_probeOutput
                           : &m_installerOutput;
    *sink += installerProcess->readAllStandardOutput();
    *sink += installerProcess->readAllStandardError();
    if (sink->size() > kInstallerOutputKeepBytes) {
        *sink = sink->right(kInstallerOutputKeepBytes);
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
    if (m_loaderInstall.phase == LoaderInstall::Phase::ProbingInstallerOptions) {
        const qint64 probeElapsed = QDateTime::currentMSecsSinceEpoch() - m_installerStartedAt;
        if (probeElapsed >= kInstallerProbeTimeoutMs) {
            // 探针挂住了（它本该打一段帮助就退出）：杀掉它，**不报错** —— finished 那一路会
            // 把结论落成"宁可信其有"然后接着跑真正的安装（见 onProbeFinished）。
            qWarning() << "Installer option probe timed out, going with permissive defaults";
            installerProcess->kill();
        }
        return;
    }
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
    if (m_loaderInstall.phase == LoaderInstall::Phase::ProbingInstallerOptions) {
        // 探针没能起来：**不在这里报错**，按"宁可信其有"往下走 —— 真正那一次安装会撞上
        // 同一个错、报出给用户看的那句话（"安装器起不来，到设置里换一个 Java"）。
        if (static_cast<QProcess::ProcessError>(processError) != QProcess::FailedToStart) {
            return;  // 别的错误都跟着一个 finished 信号，那条路会收尾
        }
        if (installerTimer != nullptr) {
            installerTimer->stop();
        }
        m_installerOptions = InstallerOptions();
        startInstallerProcess();
        return;
    }
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
    if (m_loaderInstall.phase == LoaderInstall::Phase::ProbingInstallerOptions) {
        onProbeFinished(exitCode, crashed);
        return;
    }
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

    // OptiFine 的版本目录**叫什么名字我们算不出来**（安装器按自己那份 jar 里的版本号拼的），
    // 所以它那一套是"先去临时目录里认、核对、搬回来"，出口和下面这段一样：成功就接着补
    // 原版，失败就收尾。核对仍然做，只是搬到了 adoptOptifineProducts() 里 —— 而且那一步
    // 做到"核对通过"之前，用户的游戏目录里一个字节都不会多。
    if (m_loaderInstall.kind == LoaderKind::OptiFine) {
        adoptOptifineProducts();
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

// ---------------------------------------------------------------------------
// OptiFine：跑它自己的安装器（无界面入口 + 换 APPDATA + 对着临时目录装）
// ---------------------------------------------------------------------------
//
// 这一段和上面 Forge/NeoForge 那段**故意长得不一样**，三处实测到的差别（都记在
// LoaderMeta.hpp 里 OptiFine 那一节）直接决定了下面代码的顺序：
//
//  * 它的安装器**不认命令行给的目录**（参数整个丢掉，只从 APPDATA 算）→ 换子进程的
//    APPDATA、让它对着一个临时目录装，装完把产物搬回真游戏目录。真游戏目录从头到尾没被
//    它碰过 —— 这比"用目录联接把假目录指过去"稳得多：那一条路要在用户的真目录上做删除，
//    错一步就是把人家的 .minecraft 删了。
//  * 它**要拿原版的客户端 jar 当输入** → 所以是"先补原版、再跑安装器"（Forge/NeoForge
//    正好相反：先跑安装器、最后才补原版）。
//  * 它的版本目录名**我们算不出来**（安装器按自己那份 jar 里的版本号拼的）→ 装完去盘上认。

void VersionManager::fetchOptifineVersionList() {
    if (loaderNetwork == nullptr) {
        loaderNetwork = new QNetworkAccessManager(this);
    }
    // 两个源是**两份不同的数据**：镜像是一条 JSON 接口（一个游戏版本一页），官方只有那张
    // 203 KB 的下载页可读（每个游戏版本一节）。连"哪一条是正式版"的挑法都不一样，所以
    // 回复也不共用处理，各走各的（见 LoaderMeta.hpp 里那两个纯函数）。
    //
    // NOTE: 走哪条源在**发请求这里决定一次**，然后原样传进回复处理函数，那边**不再重新读**。
    //       以前两边各读一次 useMirror()：这中间用户去设置页换一下「下载源」（安装卡着不动
    //       时他多半会去换），回复回来就会**按另一条源的格式去解** —— HTML 当 JSON 解，
    //       报出来是"版本表读不懂"；JSON 当下载页解，报出来是"还没有支持这个版本的正式版"
    //       （而它明明有，只是我们根本没看）。两句话都在指向错误的方向。
    //       问的是 mirrorPreferred() 而不是 useMirror()：镜像被降级（太慢）那段时间里，
    //       这张表也不该再去问镜像 —— 和 Forge 安装器那条 `--mirror` 是同一个判据。
    const bool fromMirror = downloadManager->mirrorPreferred();
    const QUrl url(fromMirror ? LoaderMeta::optifineMirrorListUrl(m_loaderInstall.gameVersion)
                              : LoaderMeta::optifineDownloadsPageUrl());
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, fromMirror] { onOptifineVersionListReply(reply, fromMirror); });
}

void VersionManager::onOptifineVersionListReply(QNetworkReply *reply, bool fromMirror) {
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

    LoaderMeta::OptiFinePick pick;
    if (fromMirror) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (!doc.isArray()) {
            failLoaderInstall(tr("OptiFine 的版本表读不懂（%1）。").arg(parseError.errorString()));
            return;
        }
        pick = LoaderMeta::pickOptifineFromMirror(doc.array());
    } else {
        pick = LoaderMeta::pickOptifineFromDownloadsPage(QString::fromUtf8(body),
                                                         m_loaderInstall.gameVersion);
    }

    if (!pick.ok()) {
        // 和另外三条一样：这不是网络问题，也**不能**退回"装原版"。NOTE: 这一条**包含**
        // "只有预览版、还没发正式版"的游戏版本 —— 官方页面上那一节里就没有 main 行，
        // 镜像那边则是全被 preview_ 过滤掉了。
        failLoaderInstall(tr("%1 还没有支持 %2 的正式版（预览版不装）。")
                              .arg(m_loaderInstall.loaderName, m_loaderInstall.gameVersion));
        return;
    }

    // "这个版本叫什么"只有 LoaderMeta::optifineEditionFromFile() 那一处切法，官方/镜像两条路
    // 都得是同一个值（那是要写进 wtfml.meta 的东西）。
    const QString edition =
        LoaderMeta::optifineEditionFromFile(m_loaderInstall.gameVersion, pick.fileName);

    emit loaderInstallMessage(tr("挑中 %1 %2（%3）。")
                                  .arg(m_loaderInstall.loaderName, edition,
                                       fromMirror ? tr("镜像源上最新那条正式版")
                                                  : tr("官方页面上标着正式版的那一条")));

    // 镜像那份数据里带着权威的 type/patch，直接读到的就用它（**不从版本号切**：那两段的切法
    // 和文件名不一样，多一处自己算的就多一处能算错的地方）。用户钉死的版本没有这两个字段，
    // 那时才由版本号切 —— 见下面那个函数的参数说明。
    adoptOptifineEdition(edition, LoaderMeta::OptiFineMirrorPath{pick.type, pick.patch},
                         fromMirror);
}

/**
 * @brief OptiFine：版本定了之后的收尾（填版本号/文件名，然后按下载源决定下一步）。
 * @param edition 版本号（`HD_U_H9`，就是写进 wtfml.meta 的 loaderVersion）。
 * @param mirrorPath 镜像那条路的两个路径段。**镜像列表里直接读到的**就传进来（那是权威的）；
 *        用户钉死的版本没有这两个字段，由 optifineMirrorPathFromEdition() 从版本号切出来 ——
 *        那个切法对着那 11 条真实镜像数据逐条核过，和直接读到的完全一致。
 * @param fromMirror 这一次走的是镜像那条路吗。**由调用方决定好传进来，这里不重新读**
 *        `downloadManager->mirrorPreferred()` —— 版本表已经按某一条源取回来了，
 *        "接着按哪条源走"必须和取表时是同一个答案（重现读的话，用户在两次之间换一下
 *        「下载源」，就会出现"表是按官方页取的、文件却按镜像地址拼"这种半截组合）。
 *
 * 两条路（版本表挑出来的 / 用户钉死的）共用这一段的理由：**挑法和钉死的差别只在"版本号从
 * 哪来"**，后面填文件名、拼地址、走镜像还是广告页，一个字都不该不一样。这里以前是内联在
 * onOptifineVersionListReply() 里的，于是"钉死"那条路根本没有入口 —— 选了 H7 会装成 H9。
 *
 * NOTE: 版本目录名（= loaderId，最终 finished 信号里的 id）这里**故意不填** —— 它是安装器
 *       自己拼的，我们算不出来。装完认出来再填（见 adoptOptifineProducts），在那之前要报
 *       id 的地方走 reportId() 的退化值（原版号）。
 */
void VersionManager::adoptOptifineEdition(const QString &edition,
                                         const LoaderMeta::OptiFineMirrorPath &mirrorPath,
                                         bool fromMirror) {
    m_loaderInstall.loaderVersion = edition;
    m_loaderInstall.installerFileName =
        LoaderMeta::optifineInstallerFileName(m_loaderInstall.gameVersion, edition);

    if (fromMirror) {
        if (!mirrorPath.ok()) {
            failLoaderInstall(tr("%1 的版本号 %2 认不出下载地址，换一个版本试试。")
                                  .arg(m_loaderInstall.loaderName, edition));
            return;
        }
        // 镜像那条路的地址最后两段是 type/patch（`/optifine/1.18.2/HD_U/H9`），302 到
        // maven 上那个真文件。所以 installerFileName 不能用它来切，上面先填好了。
        m_loaderInstall.installerUrl = LoaderMeta::optifineMirrorFileUrl(
            m_loaderInstall.gameVersion, mirrorPath.type, mirrorPath.patch);
        deferLoaderStep(LoaderInstall::Phase::FetchingInstallerList,
                        [this] { ensureParentForOptifine(); });
        return;
    }

    // 官方那条路还差一步：真文件在广告页后面，先把那条带 token 的地址抠出来。
    m_loaderInstall.phase = LoaderInstall::Phase::FetchingInstallerToken;
    emit loaderInstallMessage(tr("正在取 %1 %2 的下载地址...")
                                  .arg(m_loaderInstall.loaderName, edition));
    fetchOptifineDownloadToken();
}

void VersionManager::fetchOptifineDownloadToken() {
    const QUrl url(LoaderMeta::optifineAdPageUrl(m_loaderInstall.installerFileName));
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onOptifineDownloadTokenReply(reply); });
}

void VersionManager::onOptifineDownloadTokenReply(QNetworkReply *reply) {
    reply->deleteLater();
    if (m_loaderInstall.phase != LoaderInstall::Phase::FetchingInstallerToken) {
        return;
    }

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        failLoaderInstall(tr("拿不到 %1 %2 的下载地址（%3）。检查网络后重试。")
                              .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion,
                                   reply->errorString()));
        return;
    }

    const QString url = LoaderMeta::optifineJarUrlFromAdPage(QString::fromUtf8(body),
                                                             m_loaderInstall.installerFileName);
    if (url.isEmpty()) {
        // 抠不出来就**不猜**：宁可报"拿不到下载地址"，也不给用户下一个不知道是什么的文件。
        failLoaderInstall(tr("%1 的广告页上没认出 %2 那条下载地址，没有安装。过一会儿重试；"
                             "一直这样就把下载源换成镜像源。")
                              .arg(m_loaderInstall.loaderName, m_loaderInstall.installerFileName));
        return;
    }

    m_loaderInstall.installerUrl = url;
    deferLoaderStep(LoaderInstall::Phase::FetchingInstallerToken,
                    [this] { ensureParentForOptifine(); });
}

void VersionManager::ensureParentForOptifine() {
    // 判据和 startParentOrLoaderFiles() 完全一样（版本 json + 客户端 jar 两份都在盘上），
    // 但**理由和方向都不一样**：那边是"原版在就直接下加载器自己的文件"，这边是"原版不在就
    // 先补 —— 安装器要拿那份客户端 jar 当输入，它不在的话安装器起不来"。
    const QString parent = m_loaderInstall.gameVersion;
    if (QFile::exists(InstanceFiles::jsonPath(parent)) &&
        QFile::exists(InstanceFiles::jarPath(parent))) {
        emit loaderInstallMessage(tr("原版 %1 已经装好了，直接跑 %2 的安装器。")
                                      .arg(parent, m_loaderInstall.loaderName));
        startInstallerDownload();
        return;
    }

    emit loaderInstallMessage(tr("%1 的安装器要拿原版 %2 的客户端 jar 当输入，先把原版装好...")
                                  .arg(m_loaderInstall.loaderName, parent));
    m_loaderInstall.phase = LoaderInstall::Phase::WaitingParentJson;
    fetchVersionDetails(parent);
}

QString VersionManager::optifineWorkRoot() const {
    return optifineWorkDir != nullptr ? optifineWorkDir->path() : QString();
}

QString VersionManager::optifineWorkMinecraftDir() const {
    // 安装器算的是 `%APPDATA%/.minecraft`（实测：Utils.getWorkingDirectory()），所以 APPDATA
    // 指的是**外面那层**，它真正往里装的是下面这层 .minecraft。
    return optifineWorkRoot() + QStringLiteral("/.minecraft");
}

void VersionManager::cleanupOptifineWorkDir() {
    if (optifineWorkDir == nullptr) {
        return;
    }
    // 递归删。那底下全是我们自己刚建的东西（见 .hpp 里那个成员的 NOTE），所以这是安全的
    // —— 换来的正是"真游戏目录一个字节都不删"。
    //
    // NOTE: 删不掉（比如子进程还占着文件）也不报错、不当失败：最坏的结果是系统临时目录里
    //       多留一份几 MB 的假 .minecraft，不值得为这个去打扰用户。
    if (!optifineWorkDir->remove()) {
        qWarning() << "Could not remove the OptiFine work dir" << optifineWorkDir->path();
    }
    delete optifineWorkDir;
    optifineWorkDir = nullptr;
}

bool VersionManager::prepareOptifineWorkDir(QString *errorOut) {
    // 先清一次：正常路径下不该有残留（两条终态都会清），这里是保险 —— 万一将来某条新加的
    // 失败路径漏了清，下一次安装不该装进一个脏目录里。
    cleanupOptifineWorkDir();

    optifineWorkDir =
        new QTemporaryDir(QDir::tempPath() + QStringLiteral("/wtfml-optifine-XXXXXX"));
    if (!optifineWorkDir->isValid()) {
        if (errorOut != nullptr) {
            *errorOut = tr("建不出临时工作目录（%1）。%2 的安装器只认 APPDATA 指的那个目录，"
                           "没它装不了。")
                            .arg(optifineWorkDir->errorString(), m_loaderInstall.loaderName);
        }
        return false;
    }

    // 搬运安装器要的两样**输入**：原版的版本 json 和客户端 jar。它把那份 jar 原样抄进新
    // 版本目录当"这个版本的客户端 jar"（不往里写补丁），所以它得先在盘上、还得叫那个名字
    // —— 安装器是按 `versions/<原版>/<原版>.jar` 这个约定去找的。
    const QString parent = m_loaderInstall.gameVersion;
    const QString destDir = optifineWorkMinecraftDir() + QStringLiteral("/versions/") + parent;
    if (!QDir().mkpath(destDir)) {
        if (errorOut != nullptr) {
            *errorOut = tr("建不出临时目录里的 versions/%1（%2）。").arg(parent, destDir);
        }
        return false;
    }

    const QString jsonFrom = InstanceFiles::jsonPath(parent);
    const QString jarFrom = InstanceFiles::jarPath(parent);
    if (!QFile::exists(jsonFrom) || !QFile::exists(jarFrom)) {
        if (errorOut != nullptr) {
            *errorOut = tr("原版 %1 的文件不全（%2 / %3 这两份要拷进临时目录给安装器用），"
                           "没有安装。")
                            .arg(parent, jsonFrom, jarFrom);
        }
        return false;
    }
    if (!QFile::copy(jsonFrom, destDir + QLatin1Char('/') + parent + QStringLiteral(".json")) ||
        !QFile::copy(jarFrom, destDir + QLatin1Char('/') + parent + QStringLiteral(".jar"))) {
        if (errorOut != nullptr) {
            *errorOut = tr("把原版 %1 的版本 json / 客户端 jar 拷进临时目录时失败了，没有安装。")
                            .arg(parent);
        }
        return false;
    }
    return true;
}

void VersionManager::adoptOptifineProducts() {
    const QString name = m_loaderInstall.loaderName;
    const QString version = m_loaderInstall.loaderVersion;
    const QString prefix = m_loaderInstall.gameVersion + QStringLiteral("-OptiFine_");

    // 1. 认它装到哪去了。那个临时目录是全新的，我们只往里放了一份**原版**，所以
    //    `versions/` 底下除了原版之外多出来的**那一个**目录就是它装的。多了、少了都不猜
    //    —— 猜错就是把 wtfml.meta 写到一个不存在的 id 上。
    const QString versionsRoot = optifineWorkMinecraftDir() + QStringLiteral("/versions");
    QString id;
    const QFileInfoList entries =
        QDir(versionsRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName() == m_loaderInstall.gameVersion) {
            continue;  // 我们自己放进去的原版
        }
        if (!id.isEmpty()) {
            failLoaderInstall(tr("%1 的安装器在临时目录里装出了不止一个版本（%2、%3），"
                                 "认不出该用哪一个，没有安装。\n"
                                 "它最后打出来的是：\n%4")
                                  .arg(name, id, entry.fileName(),
                                       installerOutputTail(m_installerOutput)));
            return;
        }
        id = entry.fileName();
    }
    if (id.isEmpty()) {
        failLoaderInstall(tr("%1 %2 的安装器跑完了（退出码 0），但临时目录里没有多出任何"
                             "版本目录，认不出它装到哪去了。\n"
                             "它最后打出来的是：\n%3")
                              .arg(name, version, installerOutputTail(m_installerOutput)));
        return;
    }

    // 2. 名字得能当目录名（它来自安装器，我们拿它拼路径），而且得是 `<原版>-OptiFine_<版本>`
    //    这个形状 —— 那是它自己拼目录名的约定，对不上就说明装的不是我们要的那个原版。
    if (!LoaderMeta::isSafeVersionId(id) || !id.startsWith(prefix)) {
        failLoaderInstall(tr("%1 %2 的安装器装出来一个「%3」，不是「%4…」那个形状，"
                             "没有承认它装好了。")
                              .arg(name, version, id, prefix));
        return;
    }

    // 3. 核对那份 json。**从临时目录读**（不经过内存缓存 —— 走缓存就等于拿一份跟这次安装
    //    无关的东西去核对）。NOTE: checkInstallerResult() 的 id 那一半在这里是**退化的**
    //    （id 就是我们刚从盘上认出来的那个，必然相等），真正在查的是 inheritsFrom 是不是
    //    我们选的那个原版、以及 id 能当目录名 —— 上面那两步已经把这两条查过了。
    const QJsonObject produced = readVersionJsonAt(optifineWorkMinecraftDir() +
                                                   QStringLiteral("/versions/") + id +
                                                   QLatin1Char('/') + id +
                                                   QStringLiteral(".json"));
    if (produced.isEmpty()) {
        failLoaderInstall(tr("%1 %2 的安装器装出了 %3 这个目录，但里面没有 %4.json，没有安装。")
                              .arg(name, version, id, id));
        return;
    }
    const LoaderMeta::ProfileCheck check =
        LoaderMeta::checkInstallerResult(produced, m_loaderInstall.gameVersion, id);
    if (!check.ok()) {
        if (check.problem == LoaderMeta::ProfileProblem::BadId) {
            failLoaderInstall(tr("%1 %2 装出来的版本 json 里的 id 是「%3」，和目录名「%4」"
                                 "对不上，没有安装。")
                                  .arg(name, version, check.id, id));
            return;
        }
        failLoaderInstall(tr("%1 %2 装出来的版本是基于「%3」的，不是我们选的「%4」，没有安装。")
                              .arg(name, version, check.parent, m_loaderInstall.gameVersion));
        return;
    }

    // 4. 搬回真游戏目录。**先核对、后搬**：上面任何一条不过，用户的游戏目录里一个字节都
    //    不会多出来 —— 这正是"对着临时目录装"换来的东西。
    //
    //    两样都要搬，而且**先搬库、后搬版本目录**：
    //     * 那份 json 里的两个库既没有 `url` 也没有 `downloads`，只能从本机那份
    //       `libraries/` 里满足。只搬版本目录会得到一个启动就 NoClassDefFound 的版本。
    //     * 顺序是刻意的：版本目录一出现，这个版本在启动器的版本列表里就"存在"了。库先搬
    //       的话，中途失败留下的是一份谁都看不见的库；反过来留下的会是一个半成品版本。
    const QString tmpLibs = optifineWorkMinecraftDir() + QStringLiteral("/libraries/optifine");
    if (!QDir(tmpLibs).exists()) {
        failLoaderInstall(tr("%1 %2 装出来的版本目录里少了它的库（临时目录里没有 %3），"
                             "这样的版本启动时会报找不到类，没有安装。")
                              .arg(name, version, tmpLibs));
        return;
    }
    if (!copyTreeInto(tmpLibs, AppPaths::libraryDir() + QStringLiteral("/optifine"))) {
        failLoaderInstall(tr("%1 %2 的库（libraries/optifine/）没能从临时目录搬回游戏目录"
                             "（%3），没有安装。")
                              .arg(name, version, AppPaths::libraryDir() + QStringLiteral("/optifine")));
        return;
    }
    if (!copyTreeInto(QDir(versionsRoot).absoluteFilePath(id), AppPaths::versionDir(id))) {
        failLoaderInstall(tr("%1 %2 的库搬回来了，但版本目录没能搬回 %3，游戏目录里可能留下了"
                             "半个版本 —— 再装一次就能盖好。")
                              .arg(name, version, AppPaths::versionDir(id)));
        return;
    }

    // 5. 版本号以**盘上那个目录名**为准：目录名是安装器拼的（我们算不出来），而 wtfml.meta
    //    里记的东西必须和真实装出来的那个版本一致。挑版本时报的那个（从安装器文件名切的）
    //    正常情况下和它一样。
    m_loaderInstall.loaderId = id;
    m_loaderInstall.loaderVersion = id.mid(prefix.size());

    emit loaderInstallMessage(
        tr("%1 %2 装好了（%3）。").arg(name, m_loaderInstall.loaderVersion, id));
    startParentCompletion();
}

void VersionManager::finishLoaderInstall(bool success, const QString &message) {
    // 那份临时工作目录（OptiFine 那条路才有）到这里就没人要了：产物已经搬回真游戏目录，
    // 剩下的只是一份几 MB 的假 .minecraft。成功和失败两条终态都清，不留垃圾。
    cleanupOptifineWorkDir();

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

    // 这一次安装是**整合包导入的第一步**（不是用户单独要装加载器）：这里不发终态信号 ——
    // 发了界面会先弹一句"Fabric 已就绪"、按钮中途解禁，而导入那边还有上百个模组要下。
    // 成功就接着走导入的下一步，失败就把整件事按失败收掉（那句原因正是加载器这边给的）。
    if (m_modpackInstall.active() &&
        m_modpackInstall.phase == ModpackInstall::Phase::InstallingLoader) {
        m_loaderInstall.reset();
        if (!success) {
            failModpackImport(tr("%1 没装成，整合包没有导入。%2").arg(loaderName, message));
            return;
        }
        m_modpackInstall.loaderId = loaderId;
        emit modpackImportMessage(tr("%1 %2 装好了，接着落实例自己的文件...")
                                      .arg(loaderName, loaderVersion));
        // 这一步是从"加载器那批文件下完"的信号栈里回来的（allDownloadsFinished 一路同步
        // 到这儿）：排到下一轮，让它从干净栈上开始（同 2026-09-22 那次崩溃的改法）。
        deferNextStep([this] { startModpackFiles(); });
        return;
    }

    // 这一次加载器安装是"装一个**带名字的实例**"的第一步：加载器装完不算完，还得写实例
    // 自己那份薄 json，而终态信号报的该是**用户起的那个名字**（他等的是那个）。
    //
    // NOTE: 这一支和上面整合包那一支是**互斥**的（两条入口都会在对方 active 时拒绝开局），
    //       所以谁先谁后不影响行为；放在后面只是因为整合包那条路多一步。
    if (m_namedInstance.active() && m_namedInstance.phase == NamedInstance::Phase::WaitingLoader) {
        m_loaderInstall.reset();
        if (!success) {
            failNamedInstance(tr("%1 %2 没有装成，实例没有装成。%3")
                                  .arg(loaderName, loaderVersion, message));
            return;
        }
        // 父版本到这一刻才知道是谁（加载器那个目录名是安装器自己拼的）。
        m_namedInstance.parentId = loaderId;
        finishNamedInstance(loaderId, tr("%1 %2 已就绪。%3").arg(loaderName, loaderVersion, message));
        return;
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
    // 同上：不管是哪一步失败的，那个临时目录都该消失。
    cleanupOptifineWorkDir();

    // 那份 profile 还在内存缓存里、磁盘上却没有对应的目录：留着的话 getVersionJson()
    // 会给出一个"磁盘上并不存在"的合并结果（它先查缓存）。清掉它。
    if (!m_loaderInstall.loaderId.isEmpty()) {
        versionJsons.remove(m_loaderInstall.loaderId);
    }
    const QString reportId = m_loaderInstall.reportId();
    qWarning() << "Loader install failed for" << reportId << ":" << reason;

    // 这一次加载器安装是"装一个带名字的实例"的第一步：终态信号报**用户起的那个名字**
    // （他等的是那个），而且必须把那个状态一起收掉 —— 不收的话 isNamedInstanceInstalling()
    // 永远是 true，界面就一直卡在忙态上、之后每次安装都被"已有任务正在进行"顶回来。
    //
    // NOTE: loaderName 必须在 reset() **之前**取出来（reset 会把它清掉，症状是那句话里
    //       加载器的名字变成空的 —— 同 finishLoaderInstall 开头那段）。
    if (m_namedInstance.active() && m_namedInstance.phase == NamedInstance::Phase::WaitingLoader) {
        const QString instanceId = m_namedInstance.instanceId;
        const QString instanceLoaderName = m_loaderInstall.loaderName;
        m_namedInstance.reset();
        m_loaderInstall.reset();
        emit versionDownloadFinished(
            instanceId, false,
            tr("实例「%1」没有装成（%2 没装成）。%3").arg(instanceId, instanceLoaderName, reason));
        return;
    }

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

    // 只缓存**链正常走完**的那一份。
    //
    // NOTE: 上面两种 break（环、父版本没装）都会留下一份"合了一半"的 json。以前不管走没走完
    //       都往缓存里塞，于是：启动前 MainWindow 先 getVersionJson() 拿到半份、再问
    //       inheritanceProblem()，此时用户把缺的那个父版本装上 —— 同一个会话里第二次
    //       getVersionJson() 直接命中半份缓存（inheritanceProblem() 是重新读盘、这时已经不抱怨了），
    //       启动器就拿着缺 assets/librarys 的半份 json 去启动，且不报任何错。
    //       环和深度超限同理：留在盘上的东西没变过，重读一次盘比用错的强。
    if (parentId.isEmpty()) {
        mergedVersionJsons[versionId] = merged;
    }
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
            // OptiFine 那次是"**装之前**先把原版下全"（它的安装器要拿原版的客户端 jar 当
            // 输入），所以它用另一个 phase 记着：那一批结束时该起安装器，而不是收尾。
            // 另外三个都是"装完再补原版"，走 WaitingParentFiles。
            //
            // NOTE: 判据是 kind **加上 loaderId 还空着**，不是只看 kind。OptiFine 的 loaderId
            //       是装完从盘上认出来的（见 adoptOptifineProducts），所以"还空着"正好等于
            //       "安装器还没跑过"。装完那一次 startParentCompletion() 也要走这条路，那时
            //       loaderId 已经有值 —— 再判成 WaitingParentBefore 的话，那一批下完会回头
            //       **再跑一次安装器**，装完又补原版、补完又装：死循环，而且每一圈都往 %TEMP%
            //       里再开一个临时目录、再起一个 java（实测就是这么转起来的）。
            const bool beforeOptifineInstaller = m_loaderInstall.kind == LoaderKind::OptiFine &&
                                                 m_loaderInstall.loaderId.isEmpty();
            const LoaderInstall::Phase parentPhase =
                beforeOptifineInstaller ? LoaderInstall::Phase::WaitingParentBefore
                                        : LoaderInstall::Phase::WaitingParentFiles;
            m_loaderInstall.phase = parentPhase;
            // 这批原版文件是从"刚收到原版那份 json"的这条栈上起的（上面那条信号链一路都是
            // 同步的）：排到下一轮再起。相位上面已经定好了，守卫要认的就是它。
            deferLoaderStep(parentPhase, [this, versionId] {
                downloadVersion(versionId, AppPaths::versionDir(versionId));
            });
            return;
        }

        // 纯原版的整合包那一条：这次取 json 是**导入流程自己要的**，不是用户在下载这个
        // 版本。和上面加载器那段同一个理由 —— 发 versionDetailsFetched 出去，界面会当成
        // "用户点了下载原版"，于是同一个版本下两遍、两个批次互相打架。
        if (m_modpackInstall.active() &&
            m_modpackInstall.phase == ModpackInstall::Phase::WaitingParentJson &&
            versionId == m_modpackInstall.pack.gameVersion) {
            // 纯原版包的父版本就是原版那一份 —— 实例 json 的 inheritsFrom 指它。
            m_modpackInstall.loaderId = versionId;
            m_modpackInstall.phase = ModpackInstall::Phase::WaitingParentFiles;
            deferNextStep([this, versionId] {
                downloadVersion(versionId, AppPaths::versionDir(versionId));
            });
            return;
        }

        // "装一个带名字的实例"里**纯原版**那一条：父版本就是原版自己，这里接手去下它的
        // 文件。理由同上（不能发 versionDetailsFetched）。
        //
        // NOTE: 父版本是**加载器**的那一条永远走不到这里 —— 那整段在 m_loaderInstall 里跑，
        //       上面的加载器分支先认领了（那时 m_namedInstance.parentId 还空着，是装完才填的）。
        if (m_namedInstance.active() &&
            m_namedInstance.phase == NamedInstance::Phase::WaitingParentJson &&
            versionId == m_namedInstance.parentId) {
            m_namedInstance.phase = NamedInstance::Phase::WaitingParentFiles;
            deferNamedStep(NamedInstance::Phase::WaitingParentFiles, [this, versionId] {
                downloadVersion(versionId, AppPaths::versionDir(versionId));
            });
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

    // 整合包导入在等原版那份 json（纯原版包那一条）：这次失败就是导入失败。
    if (m_modpackInstall.active() &&
        m_modpackInstall.phase == ModpackInstall::Phase::WaitingParentJson &&
        pending == m_modpackInstall.pack.gameVersion) {
        failModpackImport(tr("拿不到原版 %1 的版本信息，整合包没有导入（检查网络后重试）。")
                              .arg(pending));
        return;
    }

    // 安装流程在等原版那份 json：这次失败就是安装失败，而不是"某个人请求的版本没取到"。
    if (m_loaderInstall.phase == LoaderInstall::Phase::WaitingParentJson &&
        pending == m_loaderInstall.gameVersion) {
        failLoaderInstall(tr("拿不到原版 %1 的版本信息，%2 没有安装。").arg(
            pending, m_loaderInstall.loaderName));
        return;
    }

    // 带名字的实例（纯原版那一条）在等原版那份 json：这次失败就是安装失败。
    // 和上面加载器那一段不会撞车：installNamedVanilla 的入口就拒掉了 m_loaderInstall 活着
    // 的情况，两者不会同时是活的。父版本是加载器的那一条也走不到这里 —— 那次是加载器在等
    // json，由上面那一小段转给 failLoaderInstall()。
    if (m_namedInstance.active() &&
        m_namedInstance.phase == NamedInstance::Phase::WaitingParentJson &&
        pending == m_namedInstance.parentId) {
        failNamedInstance(tr("拿不到原版 %1 的版本信息，实例没有装成（检查网络后重试）。")
                              .arg(pending));
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
                // Forge 有两种 maven 坐标，老的游戏版本只有第二种（实测表见
                // LoaderMeta::forgeInstallerUrlAlternate）。常规那种 404 时换一次再下，
                // 换过还不行才按失败报。
                if (retryInstallerDownloadAlternate()) {
                    return;
                }
                failLoaderInstall(tr("%1 的安装器没下下来，%2 没有安装。检查网络后重试。\n%3")
                                      .arg(m_loaderInstall.loaderName,
                                           m_loaderInstall.gameVersion, message));
                return;
            }
            // 这一批结束了，但事情远没完：安装器那个 Java 进程现在才开始跑。
            // 这一句本来就在 allDownloadsFinished 的发射里，起进程也一并排到下一轮。
            deferLoaderStep(LoaderInstall::Phase::WaitingInstallerJar, [this] { runInstaller(); });
            return;
        }

        // OptiFine 独有的一段：这一批是"装之前先把原版下全"那一次，下完才轮到安装器
        // （它的安装器要拿原版的客户端 jar 当输入）。Forge/NeoForge 那边原版是**最后**
        // 补的，所以它们永远走不到这里。
        if (m_loaderInstall.phase == LoaderInstall::Phase::WaitingParentBefore) {
            if (!success) {
                failLoaderInstall(tr("原版 %1 的文件没下全，%2 没有安装。先把原版装好再试。\n%3")
                                      .arg(m_loaderInstall.gameVersion,
                                           m_loaderInstall.loaderName, message));
                return;
            }
            emit loaderInstallMessage(tr("原版 %1 就绪，接着跑 %2 的安装器...")
                                          .arg(m_loaderInstall.gameVersion,
                                               m_loaderInstall.loaderName));
            deferLoaderStep(LoaderInstall::Phase::WaitingParentBefore,
                            [this] { startInstallerDownload(); });
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
                // NOTE: 2026-09-22 那次崩溃就在这一句上（取证见 build/tools/crashdump/）：
                //       这里直接 startLoaderFiles() 的话，加载器那个批次是在**上一批的
                //       信号栈里**起来的。排到下一轮，让它从干净栈上开始。
                deferLoaderStep(LoaderInstall::Phase::WaitingParentFiles,
                                [this] { startLoaderFiles(); });
                return;
            }
            // Forge / NeoForge / OptiFine：加载器自己那份 json 和库**安装器已经放好了**
            // （OptiFine 那两份是我们从临时目录里搬回来的），这一批只是把原版补齐，补完就
            // 结束了 —— 没有"加载器自己那几个库"要再下一遍。
            finishLoaderInstall(true, message);
            return;
        }

        if (m_loaderInstall.phase == LoaderInstall::Phase::WaitingLoaderFiles) {
            finishLoaderInstall(success, message);
            return;
        }
    }

    // 整合包导入的两段批次（和加载器同一个理由：一次点击 = 好几件事，中间那批文件不该
    // 单独报一次"下载完成"）。
    // NOTE: 排在加载器那一段**之后** —— 带加载器的整合包在装加载器时，导入的相位是
    //       InstallingLoader，那一段由上面的加载器分支收尾、再由 finishLoaderInstall()
    //       接着往下走。这里管的是另外两段：纯原版包的原版批次、清单里那批文件。
    if (m_modpackInstall.active()) {
        if (m_modpackInstall.phase == ModpackInstall::Phase::WaitingParentFiles) {
            if (!success) {
                failModpackImport(tr("原版 %1 的文件没下全，整合包没有导入。先把原版装好再试。\n%2")
                                      .arg(m_modpackInstall.pack.gameVersion, message));
                return;
            }
            emit modpackImportMessage(tr("原版 %1 就绪，接着下整合包自己那些文件...")
                                          .arg(m_modpackInstall.pack.gameVersion));
            // 这一批是从"上一批下完"的信号栈里起来的：排到下一轮（理由同上）。
            deferNextStep([this] { startModpackFiles(); });
            return;
        }
        if (m_modpackInstall.phase == ModpackInstall::Phase::DownloadingFiles) {
            if (!success) {
                // 半成品目录**刻意留着**（几百 MB 已经下好的文件在里面，删是不逆的）：
                // 失败那句话得说清它在哪、以及想重试要先删掉它（重名会拦下第二次导入）。
                failModpackImport(
                    tr("整合包里有 %1 个文件没下下来，没有装完。\n%2\n\n已经下好的那些留在 %3 "
                       "里了 —— 想重试的话，先把那个目录删掉，再导入一次。")
                        .arg(stats.failed)
                        .arg(message, AppPaths::versionDir(m_modpackInstall.instanceId)));
                return;
            }
            // 解压是同步起 PowerShell 等它（最慢的一段）：排在下一轮干净栈上跑 ——
            // 这一步也是从批次的信号栈里回来的。
            deferNextStep([this] { extractModpackOverrides(); });
            return;
        }
    }

    // 带名字的实例（纯原版那一条）下完了：写实例那份薄 json、发终态。
    // 排在加载器那一段之后 —— 父版本是加载器时这一段压根不参与（那整件事在
    // m_loaderInstall 里跑，收尾在 finishLoaderInstall()）。这里只管纯原版。
    if (m_namedInstance.active() &&
        m_namedInstance.phase == NamedInstance::Phase::WaitingParentFiles) {
        if (!success) {
            failNamedInstance(tr("原版文件没下全，实例没有装成。\n%1").arg(message));
            return;
        }
        // 这一批是从"上一批下完"的信号栈里起来的：排到下一轮（理由同上面几处）。
        const QString parentId = m_namedInstance.parentId;
        deferNamedStep(NamedInstance::Phase::WaitingParentFiles,
                       [this, parentId, message] { finishNamedInstance(parentId, message); });
        return;
    }

    // 一批只发一次。改动前这里由每个文件的 downloadFinished 触发，
    // 下载一个版本会在日志里刷出几千行一模一样的话。
    emit versionDownloadFinished(m_currentVersionDownloading, success, message);
}
