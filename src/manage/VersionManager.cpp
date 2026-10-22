/**
 * @file VersionManager.cpp
 * @brief 实现版本发现、元数据获取与下载编排。
 * @note 网络和下载统一走 DownloadManager；UI 不直接访问网络。
 */

#include "VersionManager.hpp"
#include "util/WriteDirectoryGuard.hpp"
#include "../download/DownloadRetryPolicy.hpp"
#include "../download/InstallerFailure.hpp"
#include "../settings/GlobalJavaSettings.hpp"

#include "FolderManager.hpp"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

#include "../download/LegacyForgeInstall.hpp"
#include "../download/LoaderMeta.hpp"
#include "../download/ResourceCatalog.hpp"
#include "../launcher/VersionArguments.hpp"
#include "../launcher/VersionInheritance.hpp"
#include "../net/HttpClient.hpp"
#include "../util/AppPaths.hpp"
#include "../util/ZipUtil.hpp"
#include "FolderManager.hpp"
#include "InstanceFiles.hpp"

namespace {

/// wtfml.meta 里那个"从生到死都跟着这个实例"的 id 的字段名。
///
/// NOTE: 刻意**不叫 "id"**，理由和这个文件不叫 wtfml.json 是同一件事（见 .hpp 里
/// displayParts() 那段）：版本 JSON 里的 "id" 有特殊语义，一个文件里出现两个 id
/// 早晚会被读错，而那类错误的形态是"静默选错 Java"级别的。换个不可能撞的名字，
/// 这个错在结构上就不存在。
const QString kInstanceIdField = QStringLiteral("instanceId");

/// wtfml.meta 里"用户给这个实例起的显示名"的字段名。
///
/// NOTE: 文件里叫 `name`，但 VersionDisplayParts 里那个成员叫 `customName`。
/// 这里写、displayParts() 那边读，**两边必须是同一个串** —— 写错一个字不会报错，
/// 表现是"改完名界面上没反应"。改的时候两边一起改（那边在 displayParts() 的
/// readString("name")）。
const QString kDisplayNameField = QStringLiteral("name");

/// 一个新的稳定实例 id。只有这一处生成它，免得"生成规则"在改名的路径上被抄第二遍。
QString newInstanceId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

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
/// 已经存在的版本**（用户重装、或者在一个装过 Forge 的版本上换加载器版本），
/// 那这个 id 早就被读进缓存了 —— 走缓存就等于拿**安装之前**那份 json 去核对
/// 安装之后的产物，核对永远能过，白核。
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
bool renameOnto(const QString &from, const QString &to) {
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
bool copyFileReplacing(const QString &from, const QString &to) {
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
bool copyTreeInto(const QString &from, const QString &to) {
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
QString pickChannelDetail(LoaderMeta::LoaderChannel channel, bool stable) {
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
QString curseForgeKeyHint(bool hasKey) {
    if (hasKey) {
        return QObject::tr("设置里那份 CurseForge API key 可能不对、或者已经失效了，可以换一份。");
    }
    return QObject::tr("网络本身没问题的话，多半是那台公共接口的事了 —— 可以在设置页「下载」"
                       "栏里填一份自己的 CurseForge API key（免费申请），填了就改走官方接口。"
                       "现在这一发走的是别人搭的一台社区镜像，它可能限流、也可能哪天就停了。");
}

}  // namespace

VersionManager::VersionManager(QObject *parent)
    : QObject(parent), downloadManager(new DownloadManager(this)) {
    // `loaderVersionsFetched` 那一个信号带的是一份 LoaderVersionList（不是 QStringList）——
    // Q_DECLARE_METATYPE 在 LoaderMeta.hpp 里，这里把它登记进 Qt 的元类型表：不是为了让当下
    // 这条直连能用（直连不看元类型表），而是**排队的连接和测试里的 QSignalSpy 要认它** ——
    // 少了这一句，那两种用法会在运行期回一句"Type not registered"然后收不到东西。
    qRegisterMetaType<LoaderMeta::LoaderVersionList>("LoaderMeta::LoaderVersionList");
    connect(downloadManager, &DownloadManager::versionManifestFetched, this,
            &VersionManager::onVersionManifestFetched);
    connect(downloadManager, &DownloadManager::versionJsonFetched, this,
            &VersionManager::onVersionJsonFetched);
    // NOTE: 只认整批结束的信号。每个文件的完成都转发成 versionDownloadFinished 的话，
    // 下载一个版本会在日志里刷出几千行一模一样的话。
    connect(downloadManager, &DownloadManager::allDownloadsFinished, this,
            &VersionManager::onAllDownloadsFinished);
    // 单个文件的结果（更新任务里那一步"哪个资源成了/没成"）。★ **只管资源那一批**：
    // 上面那句 NOTE 说的"每完成一个文件转发一次"会刷屏，这里转发的前提是"这一批下的是
    // 用户在下载页/更新中心点名的那几个资源"（`m_resourceDownload.running`）——那种一批
    // 就是几个到几十个文件，正好是逐条报得完、也正需要逐条报的东西。
    connect(downloadManager, &DownloadManager::downloadFinished, this,
            [this](const QString &fileName, bool success, const QString &errorMessage) {
                if (m_resourceDownload.running) {
                    emit resourceDownloadFileFinished(m_resourceDownload.label, fileName, success,
                                                      errorMessage);
                }
            });
    connect(downloadManager, &DownloadManager::batchProgress, this,
            [this](int done, int total, int failed, const QString &currentFile) {
                emit versionDownloadStats(done, total, failed, currentFile);
            });
}

VersionManager::~VersionManager() {
    // 工作线程只持有路径与结果值；退出前等它结束，避免程序退出后仍在写实例。
    // 先通知外部解压进程退出，避免析构时一直等完整解压超时。
    if (m_modpackOverridesCancelled) m_modpackOverridesCancelled->store(true);
    m_modpackOverridesWork.cancel();
    m_modpackOverridesWork.waitForFinished();
    // DownloadManager is automatically deleted as child
    if (installerProcess != nullptr && installerProcess->state() != QProcess::NotRunning) {
        if (installerTimer != nullptr) {
            installerTimer->stop();
        }
        installerProcess->kill();
        installerProcess->waitForFinished(5000);
    }
    // 那份临时工作目录（OptiFine 那条路才有）不是 QObject 的子对象，得自己收。正常路径下
    // 它在安装的终态里已经被删过了（finishLoaderInstall / failLoaderInstall），这里只是
    // 兜住"装到一半程序被关掉"：不收的话系统临时目录里会积一份几 MB 的假 .minecraft。
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
            // 判据是"认得出客户端 jar 在不在"，**不要求 jar 名和目录名一致** ——
            // 放宽的来龙去脉见 InstanceFiles.hpp 的块注释（一句话：用户自己在资源管理器里
            // 改过目录名的实例不能被静默丢掉）。
            //
            // 判据没有一路放松到"目录里有任意 .jar 就算已安装"：那样"顶层两个 jar、
            // 认不出哪个是客户端"的目录也会被收进来，点启动时才发现找不着 jar。
            // 宁可在这里说不认识，也不能让列表里躺着一个点了会炸的条目。
            const InstanceFiles::Resolved resolved = InstanceFiles::resolveInstance(versionDir);
            if (resolved.recognized()) {
                // **半成品导入不算装好了**。整合包导入第一步就把那份薄 json
                // （`id` + `inheritsFrom`）落盘，失败时目录刻意留着（已下的几百 MB 在里面）。
                // `recognized()` 只看"jar 在不在或有没有 inheritsFrom"，于是这种目录满足它 ——
                // 列进启动列表就能被启动（缺文件，崩在缺库上），而且启动时那一步
                // `updateVersionMeta()` 会写出 wtfml.meta，`isUnfinishedImport()` 从此再也
                // 认不出它，"接着上次的导入"那条路就断了。
                // 它该由 ImportController 在同一名字上认出来（`ImportNameVerdict::OkResume`）。
                if (InstanceFiles::isUnfinishedImport(versionDir)) {
                    if (warnings != nullptr) {
                        warnings->append(
                            tr("目录 %1 是上一次没导完的整合包（没有顶层 jar），先不列在启动列表里；"
                               "重新导入同一个整合包、用同一个名字就能接着下。")
                                .arg(versionDir));
                    }
                    continue;
                }
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

    // NOTE: 没有已安装版本时这里返回的就是空列表。占位是界面层的职责：界面用下拉框的
    // placeholderText 表达"没有可选版本"；数据层塞一个假版本的话，每个调用方都得反过来
    // 判 versionId == "自定义版本" 才能把它拒掉。
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

QList<VersionManager::LatestVersion> VersionManager::latestVersions(
    const QList<MinecraftVersion> &versions) {
    // 一条一趟扫完就地收工：清单几百条，两个坑（正式版、快照）都填上就不用再往下看了。
    // NOTE: 不能"边扫边 append" —— 清单是新到旧交错排的（快照常常排在正式版前面，比如
    // 26.4-snapshot-1 就压在 26.3 上面），那样出来的顺序会随清单变。这里先各自记下
    // 来，最后按**固定顺序**（正式版、快照）append，界面上那两行的位置才是稳的。
    bool gotRelease = false;
    bool gotSnapshot = false;
    LatestVersion release;
    LatestVersion snapshot;
    for (const MinecraftVersion &version : versions) {
        const VersionCategory::Channel channel =
            VersionCategory::channelFor(version.id, version.type);
        if (channel == VersionCategory::Channel::Release) {
            if (gotRelease) {
                continue;
            }
            release.channel = channel;
            release.version = version;
            gotRelease = true;
        } else if (channel == VersionCategory::Channel::Snapshot) {
            if (gotSnapshot) {
                continue;
            }
            snapshot.channel = channel;
            snapshot.version = version;
            gotSnapshot = true;
        }
        if (gotRelease && gotSnapshot) {
            break;
        }
    }

    QList<LatestVersion> latest;
    if (gotRelease) {
        latest.append(release);
    }
    if (gotSnapshot) {
        latest.append(snapshot);
    }
    return latest;
}

QList<VersionManager::LatestVersion> VersionManager::availableLatestVersions() const {
    return latestVersions(downloadManager->getAvailableVersions());
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

    abandonOwnBatch();

    // 用既有的终态出口把话说出去：界面那边照常弹窗（错误要弹窗），用户知道出了事、
    // 也还能接着用别的功能。
    const QString reason =
        tr("下载过程中出了内部错误（%1），这一步已经停下。重启启动器后再试一次。").arg(what);
    // 内部错误之后不给「重试」：那一条路重下几个文件修不好（这次是代码里的毛病）。
    m_failedBatch.reset();
    unwindDownloadBatch(reason);
}

void VersionManager::unwindDownloadBatch(const QString &reason) {
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
    // 资源下载那一路（下载页那五类里挑一个装）**也在这条收尾路上**：它不属于上面任何
    // 一套安装流程，`m_resourceDownload.running` 一旦留在 true 上，之后
    // 每一次资源下载都会被"已有下载任务正在进行"顶回来，只能重启启动器 —— 和上面那三个
    // 状态机是同一个病，只是它没有"相位"可以看，更容易被漏掉。
    if (m_resourceDownload.running) {
        const QString label = m_resourceDownload.label;
        m_resourceDownload.reset();
        emit resourceDownloadFinished(label, false, reason);
        return;
    }
    emit versionDownloadFinished(m_currentVersionDownloading, false, reason);
}

bool VersionManager::canCancelDownload() const { return downloadManager->isBusy(); }

bool VersionManager::cancelActiveDownload() {
    if (!downloadManager->isBusy()) {
        return false;
    }
    // 先把批次掐掉，再收尾。顺序不能换：unwindDownloadBatch() 里那几段终态一发出去，界面
    // 就会立刻解禁并且允许用户再点一次下载 —— 那一刻批次必须已经是"没在忙"。
    cancelOwnBatch();
    m_failedBatch.reset();
    unwindDownloadBatch(tr("已取消。已经下好的文件留在原处，一个都没删。"));
    return true;
}

bool VersionManager::canRetryDownload() const { return m_failedBatch.valid; }

QString VersionManager::retryBatchLabel() const {
    return m_failedBatch.valid ? m_failedBatch.label : QString();
}

bool VersionManager::retryDownload() {
    if (!m_failedBatch.valid || m_failedBatch.jobs.isEmpty()) {
        return false;
    }
    if (downloadManager->isBusy()) {
        // 一次一批（和 downloadVersion / repairVersionFiles / downloadResourceFiles 是同一条
        // 规矩）。硬叠上去的话两份进度会互相盖，而"取消"那一刻也说不清在取消谁。
        qWarning() << "A download batch is already running, refusing to retry.";
        return false;
    }
    const FailedBatch batch = m_failedBatch;
    m_failedBatch.reset();

    // 把"这一批叫什么"照原样摆回去。收尾那一段（`onAllDownloadsFinished()` 的末尾）就是靠
    // 这两个成员决定发哪个终态的 —— 这两句是"重试之后终态还能报对"的全部前提。不摆的话，
    // 一次模组补下会被报成「版本 <上一个实例> 的文件已全部就绪」。
    if (batch.resource) {
        m_resourceDownload.running = true;
        m_resourceDownload.label = batch.label;
    }
    m_currentVersionDownloading = batch.id;

    beginOwnBatch();
    for (const DownloadManager::FailedJob &job : batch.jobs) {
        // 校验档一律用 SizeAndSha1：失败的那几个在盘上根本不存在，这个档位只影响
        // fileAlreadyValid() 那一次问话，而那一次必然回 false。真正的判据（sha1 / 字节数）
        // 在收尾那一步，和档位无关（见 onDownloadReplyFinished）。
        downloadManager->downloadFile(job.url, job.destinationPath, job.expectedSha1,
                                       job.expectedSize, VerifyMode::SizeAndSha1,
                                       job.officialUrl);
    }
    endOwnBatch();
    qInfo() << "Retrying" << batch.jobs.size() << "failed download(s).";
    return true;
}

void VersionManager::fetchAvailableVersions() { downloadManager->fetchVersionManifest(); }

void VersionManager::fetchVersionDetails(const QString &versionId) {
    m_pendingVersionId = versionId;
    if (downloadManager->getAvailableVersions().isEmpty()) {
        m_fetchJsonAfterManifest = versionId;
        downloadManager->fetchVersionManifest();
    } else {
        downloadManager->fetchVersionJson(versionId);
    }
}

bool VersionManager::isDownloading() const { return downloadManager->isBusy(); }

QString VersionManager::busyVersionId() const {
    // 门控的理由见头里那条 NOTE（那个成员收尾后不清空）。★ 门控**用的是"这一批是不是我开的"**
    // 而不是 `isDownloading()`：下载器是共用的，服务端安装那四段也在这上面开批次 ——
    // 那一段时间里 isDownloading() 是真的，可这一格在写的是**上一次下过的那个版本号**，
    // 照它答的话就是"用户在装服务端，1.20.1 那个实例忽然点不动了"。
    return m_ownBatchOpen ? m_currentVersionDownloading : QString();
}

void VersionManager::setUseMirror(bool useMirror) { downloadManager->setUseMirror(useMirror); }

void VersionManager::setDownloadConcurrency(int count) {
    downloadManager->setMaxConcurrentDownloads(count);
}

void VersionManager::setDownloadSpeedLimit(qint64 bytesPerSecond) {
    downloadManager->setSpeedLimitBytesPerSecond(bytesPerSecond);
}

void VersionManager::setCurseForgeApiKey(const QString &apiKey) {
    m_curseForgeApiKey = apiKey.trimmed();
}

void VersionManager::setCurseForgeApiBaseOverride(const QString &base) {
    m_curseForgeApiBaseOverride = base.trimmed();
}

void VersionManager::setModrinthApiBaseOverride(const QString &base) {
    m_modrinthApiBaseOverride = base.trimmed();
}

QString VersionManager::modrinthApiRoot() const {
    // 官方那一份的唯一出处仍是 ResourceCatalog（见 `ResourceCatalog::modrinthApiRoot()`）。
    return m_modrinthApiBaseOverride.isEmpty() ? ResourceCatalog::modrinthApiRoot()
                                               : m_modrinthApiBaseOverride;
}

void VersionManager::setVersionManifestUrlOverride(const QString &url) {
    downloadManager->setVersionManifestUrlOverride(url);
}

QString VersionManager::resolveCurseForgeFiles(ModpackImport::Pack *pack) {
    return resolveCurseForgeFiles(pack, curseForgeResolveSettings());
}

QString VersionManager::resolveCurseForgeFiles(ModpackImport::Pack *pack,
                                               const CurseForgeResolveSettings &settings) {
    if (pack == nullptr || pack->format != ModpackImport::Format::CurseForge ||
        pack->pendingRefs.isEmpty()) {
        // 不是 CF 的包、或者已经换过一次（pendingRefs 空了）：调用点可以对所有格式无脑调一次。
        return QString();
    }
    const QString base =
        ModpackImport::curseForgeApiBase(settings.apiKey, settings.apiBaseOverride);
    const bool hasKey = !settings.apiKey.isEmpty();
    QList<QPair<QByteArray, QByteArray>> headers;
    if (hasKey) {
        headers.append({QByteArrayLiteral("x-api-key"), settings.apiKey.toUtf8()});
    }

    /// 这一发没成时那句话（**必须给出一条用户自己能走的路**：镜像是别人的服务）。
    const auto failureText = [hasKey](const HttpClient::Reply &reply) {
        const QString hint = curseForgeKeyHint(hasKey);
        if (!reply.networkError.isEmpty()) {
            return tr("问 CurseForge 要文件清单时没连上：%1\n\n%2").arg(reply.networkError, hint);
        }
        if (reply.status == HttpClient::kStatusTooManyRequests) {
            return HttpClient::throttledText(tr("问 CurseForge 要文件清单"));
        }
        const QString apiError =
            reply.json.value(QStringLiteral("error")).toString().trimmed();
        return tr("CurseForge 的接口回了 HTTP %1%2\n\n%3")
            .arg(reply.status)
            .arg(apiError.isEmpty() ? QString() : tr("（%1）").arg(apiError), hint);
    };

    // 一发问多少条。接口那一侧对一批的条数有上限（官方文档是 1000，镜像更保守），
    // 我们自己定 100：正文小、重来代价小，而一个大包（几百条）也就三四发。
    constexpr int kChunk = 100;

    // 这一发**不借 `loaderNetworkManager()`**：那个 QNAM 归主线程（`new` 在
    // VersionManager 身上），而导入整合包这条路的预览现在会整段放到工作线程上跑
    // （见 ImportController 里 `runOffUiThread()` 那两处）。跨线程用同一个 QNAM 是
    // 数据竞争，Qt 的文档也明说一个 QNAM 只能在一个线程里用。这里自己开一个：
    // resolve 一次只发两三类请求（每类一两发），不依赖连接池复用；应用级代理
    // （QNetworkProxy::setApplicationProxy）对新开的 QNAM 一样生效。
    QNetworkAccessManager network;
    // ---- 第一发：号 → 文件（地址、字节数、sha1 都在这一发的答复里）----
    const QList<ModpackImport::PackRef> refs = pack->pendingRefs;
    ModpackImport::CurseForgeFiles files;
    for (int offset = 0; offset < refs.size(); offset += kChunk) {
        const QList<ModpackImport::PackRef> chunk = refs.mid(offset, kChunk);
        const HttpClient::Reply reply = HttpClient::send(
            &network, QUrl(base + QStringLiteral("/mods/files")),
            HttpClient::Verb::Post, ModpackImport::curseForgeFilesRequestBody(chunk),
            QByteArrayLiteral("application/json"), QByteArray(), HttpClient::kDefaultTimeoutMs,
            headers);
        if (!reply.ok()) {
            // **整件事停下**：一条地址都没换回来的话，"导入"就是装一个没有 mod 的实例，
            // 而用户看到的是"导入好了"。这里是"会挡住用户操作"的那一类，如实报。
            return failureText(reply);
        }
        const ModpackImport::CurseForgeFiles part = ModpackImport::parseCurseForgeFileInfo(reply.raw, chunk);
        if (!part.problem.isEmpty()) {
            return part.problem;
        }
        files.resolved += part.resolved;
        files.projectIds += part.projectIds;
        files.notDistributable += part.notDistributable;
        files.missing += part.missing;
    }

    // ---- 第二发：这些项目是哪一类（决定文件落到哪个子目录）----
    //
    // 去重：同一个项目可能在不同的分块里各出现一次（一个 mod 有两个文件），问两遍白花时间。
    QList<int> projectIds;
    {
        QSet<int> seen;
        for (const int projectId : files.projectIds) {
            if (!seen.contains(projectId)) {
                seen.insert(projectId);
                projectIds.append(projectId);
            }
        }
    }
    QHash<int, int> classIds;
    QString classIdProblem;
    for (int offset = 0; offset < projectIds.size(); offset += kChunk) {
        const QList<int> chunk = projectIds.mid(offset, kChunk);
        const HttpClient::Reply reply = HttpClient::send(
            &network, QUrl(base + QStringLiteral("/mods")), HttpClient::Verb::Post,
            ModpackImport::curseForgeModsRequestBody(chunk),
            QByteArrayLiteral("application/json"), QByteArray(), HttpClient::kDefaultTimeoutMs,
            headers);
        if (!reply.ok()) {
            // NOTE: 这一发没成**不拦整件事** —— 缺的只是"落到哪个目录"（退成 mods/，
            // 见 placeCurseForgeFiles），文件本身下得下来。停在这里等于因为一个
            // "放哪儿"的问题把一个能装的包拒了。
            classIdProblem = failureText(reply);
            break;
        }
        QString problem;
        const QHash<int, int> part = ModpackImport::parseCurseForgeClassIds(reply.raw, &problem);
        if (!problem.isEmpty()) {
            classIdProblem = problem;
            break;
        }
        for (auto it = part.constBegin(); it != part.constEnd(); ++it) {
            classIds.insert(it.key(), it.value());
        }
    }

    // ---- 落地：补上目录，写回 pack ----
    const ModpackImport::CurseForgePlacement placement =
        ModpackImport::placeCurseForgeFiles(files.resolved, classIds);
    pack->files = placement.files;
    pack->pendingRefs.clear();
    pack->filesNotDistributable = files.notDistributable;
    // 两类"没换到"合在一起：接口说没有的 + 拼不出安全路径的（多半是路径太长）。
    pack->filesMissing = files.missing + placement.missing;
    pack->filesSkipped += placement.skipped;

    if (!classIdProblem.isEmpty()) {
        emit modpackImportMessage(
            tr("%1\n\n有 %2 个文件没能确认该放进哪个目录，都按模组放进 mods/ 了。")
                .arg(classIdProblem)
                .arg(placement.files.size()));
    }
    return QString();
}

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

    const QJsonObject &versionJson = versionJsons[versionId];

    // 把 version.json 落盘。缺了它，下一次启动时 getVersionJson() 取不到
    // javaVersion / assetIndex，会静默退回错误的 Java 版本和资产索引 ——
    // 26.1 需要 Java 25，退成 Java 21 就是 UnsupportedClassVersionError。
    // 保存失败必须在开批次前终止，避免下载成功却留下不能启动的实例。
    QString writeProblem;
    if (!writeVersionJson(versionId, destinationPath, versionJson, &writeProblem)) {
        emit versionDownloadFinished(versionId, false, writeProblem);
        return;
    }

    m_currentVersionDownloading = versionId;
    beginOwnBatch();

    registerVersionFiles(versionJson, versionId, destinationPath + "/" + versionId + ".jar");

    endOwnBatch();
}

void VersionManager::registerVersionFiles(const QJsonObject &versionJson,
                                          const QString &jarOwnerId, const QString &jarPath) {
    // Download client jar
    if (versionJson.contains("downloads") && versionJson["downloads"].isObject()) {
        const QJsonObject downloads = versionJson["downloads"].toObject();
        if (downloads.contains("client") && downloads["client"].isObject()) {
            const QJsonObject client = downloads["client"].toObject();
            const QString url = client["url"].toString();
            const QString sha1 = client["sha1"].toString();
            const qint64 size = client["size"].toVariant().toLongLong();

            // 带上 size：重新点下载时先比字节数，对得上才算"已经下过"，
            // 省掉一次 41 MB 的全文件哈希。
            // 版本号要一起传：镜像源下客户端 jar 是按版本号取的，不是按 sha1。
            downloadManager->downloadClientJar(jarOwnerId, url, jarPath, sha1, size);
        }
    }

    // Download libraries
    // NOTE: 库的目标目录走 AppPaths::libraryDir()（= <数据目录>/libraries），是**全体实例
    // 共用**的那一份 —— 和官方启动器、和别的启动器都共用同一份。
    // 库是**全体版本共用**的一份，落在 AppPaths::libraryDir()：给每个实例各下一份库
    // 要多花几百 MB，而且和共用那份不一致。真正"这个版本自己的"只有 versionDir()
    // 底下那两样（<id>.jar / <id>.json）。资源（assets）也共用，那边的目录由
    // AppPaths::assetsDir() 说了算。
    QList<Library> libraries = downloadManager->extractLibraries(versionJson);
    downloadManager->downloadLibraries(libraries, AppPaths::libraryDir());

    // Download assets
    AssetIndex assetIndex = downloadManager->extractAssetIndex(versionJson);
    downloadManager->downloadAssets(assetIndex);
}

void VersionManager::repairVersionFiles(const QString &versionId,
                                        const QStringList &flaggedPaths) {
    // 两道早退都在 beginBatch() 之前，而且都要发终态（理由见 downloadVersion() 顶部那段 NOTE）。
    const QJsonObject versionJson = getVersionJson(versionId);
    if (versionJson.isEmpty()) {
        qWarning() << "No version json on disk for" << versionId << ". Cannot repair.";
        emit versionDownloadFinished(versionId, false,
                                     tr("读不出这个实例的 version.json，没法校验它要哪些文件。"));
        return;
    }
    if (downloadManager->isBusy()) {
        qWarning() << "A download batch is already running, refusing to start another.";
        emit versionDownloadFinished(versionId, false, tr("已有下载任务正在进行，请等它结束。"));
        return;
    }
    // 这一批会覆盖 `versions/<id>/<id>.jar`，还会动**全体版本共用**的 `libraries/`
    // 与 `assets/`。和 `downloadVersion()` / 资源下载那几个入口同一条：先取所属数据根的
    // 生命周期锁，拿不到就发终态、一个字节都不写（不半截开始）。
    QString lockProblem;
    if (!WriteDirectoryGuard::acquire(AppPaths::minecraftDir(), &lockProblem)) {
        emit versionDownloadFinished(versionId, false, lockProblem);
        return;
    }

    m_currentVersionDownloading = versionId;
    beginOwnBatch();

    // 体检点过名的那几条：这一批不问盘上那份，直接重下（见 .hpp 的 @param 和
    // DownloadManager::setFlaggedPaths()）。**必须在 beginBatch() 之后**——它会清掉上一批
    // 留下的那张表。体检没跑过 / 体检的不是这个实例时这里是一张空表，等于没点名任何文件。
    downloadManager->setFlaggedPaths(flaggedPaths);

    // 不写 json（见头文件里的 ②）；jar 的主人可能是父版本（见 ③）。
    registerVersionFiles(versionJson, InstanceFiles::jarOwnerId(versionId),
                         InstanceFiles::jarPath(versionId));

    endOwnBatch();
}

QList<VersionIntegrity::ExpectedFile> VersionManager::integrityPlan(const QString &versionId,
                                                                   QString *error) const {
    if (error != nullptr) {
        error->clear();
    }
    // 和 repairVersionFiles() 同一条：**盘上那份**（走继承链合并）才是权威，不是 versionJsons
    // 那张内存表 —— 体检一个早就装好的实例恰恰最常发生在重开启动器之后，那时候表是空的。
    const QJsonObject versionJson = getVersionJson(versionId);
    if (versionJson.isEmpty()) {
        if (error != nullptr) {
            *error = tr("读不出这个实例的 version.json，没法知道它该有哪些文件。");
        }
        return {};
    }

    // 两个解析结果由调用方递进去（VersionIntegrity::plan 不自己 new 一个 DownloadManager）：
    // 「哪些库、哪个资源索引」这件事只有一个出处 —— 就是装配侧用的这两个函数。
    return VersionIntegrity::plan(versionJson, versionId,
                                  downloadManager->extractLibraries(versionJson),
                                  downloadManager->extractAssetIndex(versionJson));
}

VersionIntegrity::Report VersionManager::checkInstanceIntegrity(const QString &versionId) const {
    VersionIntegrity::Report report;

    // 清单那一半搬去了 integrityPlan()（界面那条后台路要单独取它）。这一条同步路照旧：
    // 取清单 → 逐条比 → 只收不好的那些。
    QString error;
    const QList<VersionIntegrity::ExpectedFile> files = integrityPlan(versionId, &error);
    if (!error.isEmpty()) {
        report.error = error;
        return report;
    }

    report.checked = static_cast<int>(files.size());
    for (const VersionIntegrity::ExpectedFile &file : files) {
        if (VersionIntegrity::Verdict verdict = VersionIntegrity::check(file); !verdict.ok()) {
            report.problems.append(verdict);
        }
    }
    return report;
}

// ---------------------------------------------------------------------------
// 下载页那几类资源：把一个文件下到实例里（模组 / 数据包 / 资源包 / 光影）
// ---------------------------------------------------------------------------

bool VersionManager::downloadResourceFiles(const QString &destinationDir,
                                           const QString &busyInstanceId, const QString &label,
                                           const QList<ResourceFile> &files) {
    // 下面这几道早退**必须在 beginBatch() 之前**（理由见 downloadVersion() 顶部那段 NOTE：
    // beginBatch() 与 endBatch() 之间提前 return，m_batchOpen 就永远是 true）。
    // 每一道都要发终态信号 —— 界面那边点一下就在等一个结果，不发就等于"点了没反应"。
    if (destinationDir.isEmpty()) {
        emit resourceDownloadFinished(label, false, tr("没有说清要下到哪儿去。"));
        return false;
    }
    if (downloadManager->isBusy()) {
        qWarning() << "A download batch is already running, refusing to start another.";
        emit resourceDownloadFinished(label, false, tr("已有下载任务正在进行，请等它结束。"));
        return false;
    }
    // 那三套安装状态机在**它们的批次之间**是不忙的（比如正在拉版本表、跑安装器），
    // 光看 isBusy() 拦不住那一刻 —— 那时候插进来一批模组，DownloadManager 那份唯一的批次
    // 状态就会被两件事同时用（它们的接续点在 allDownloadsFinished 上，谁先收到就说不清了）。
    // 和 importModpack() / installLoaderVersion() 入口那三道闸同一个道理。
    if (m_loaderInstall.active() || m_modpackInstall.active() || m_namedInstance.active()) {
        emit resourceDownloadFinished(label, false,
                                      tr("已有下载或者安装任务正在进行，请等它结束再装。"));
        return false;
    }
    if (files.isEmpty()) {
        // 正常走不到（能点的那条版本一定有个 primary 文件），但"这个版本一个文件都没有"
        // 是接口允许的状态 —— 那时候要当场说清，而不是开一个空批次等一个永远不来的收尾。
        emit resourceDownloadFinished(label, false, tr("这个版本没有可下载的文件。"));
        return false;
    }
    QString lockProblem;
    if (!WriteDirectoryGuard::acquire(destinationDir, &lockProblem)) {
        emit resourceDownloadFinished(label, false, lockProblem);
        return false;
    }

    QStringList paths;
    paths.reserve(files.size());
    for (const ResourceFile &file : files) {
        // 文件名来自接口，**不能**直接往盘上拼：里面出现 `../` 或者一个绝对路径，写出去的
        // 位置就不在目标目录里了。这不是防谁，是"接口给的东西要当数据看"这条底线 ——
        // 一个作者把文件名写成 "../../../startup.bat" 是完全做得到的。
        if (file.filename.isEmpty() || file.filename.contains(QLatin1Char('/'))
            || file.filename.contains(QLatin1Char('\\'))
            || file.filename.contains(QStringLiteral(".."))) {
            emit resourceDownloadFinished(
                label, false, tr("这一版的文件名没法用（%1），没有装。").arg(file.filename));
            return false;
        }
        // 落点：**每一条自己可以带一个目录**（见 `ResourceFile::dir` 那段 —— 更新中心那一批
        // 是跨类勾出来的）。空 = 还是用这一批的 `destinationDir`，也就是默认那条路。
        // NOTE: `dir` 是我们自己拼的绝对路径（来自 `QFileInfo::absolutePath()`），**不是**
        // 接口给的东西 —— 所以这里不需要再防它一次（要防的是 `filename`，上面刚防过）。
        const QString dir = file.dir.isEmpty() ? destinationDir : file.dir;
        paths.append(dir + QLatin1Char('/') + file.filename);
    }

    // 这一批在写哪个实例记进 m_currentVersionDownloading（缓存那一类传空串 = 谁都不锁）。
    // 界面靠它（busyVersionId()）把这一个实例从"能启动"里滤掉 —— 往 mods/ 里写文件的
    // 时候用户点了启动，JVM 可能正好在读那半截文件。
    // NOTE: 写上**空串**也是一次有意为之的赋值：不赋的话，这一批期间 busyVersionId() 会
    // 回答**上一批版本下载**留下的那个名字（那个成员收尾后不清空），于是"往缓存里下个
    // 整合包"会把一个毫不相干的实例锁住 —— 而真正在写的那一个照样能启动，两件事都错。
    m_currentVersionDownloading = busyInstanceId;

    m_resourceDownload.running = true;
    m_resourceDownload.label = label;

    beginOwnBatch();
    for (int i = 0; i < files.size(); ++i) {
        downloadManager->downloadFile(files.at(i).url, paths.at(i), files.at(i).sha1,
                                       files.at(i).size, VerifyMode::SizeAndSha1);
    }
    endOwnBatch();
    // 跑到这儿才是"这一批真的开起来了"。上面那五道早退各自发过一次
    // `resourceDownloadFinished(label, false, …)`，而那个 label 和"真正跑完"那条路带的是
    // **同一个字符串** —— 调用方只能靠这个返回值分辨"失败了"和"根本没开始"（见头文件）。
    return true;
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
                                   const QString &installerJavaPath,
                                   const QString &manifestFingerprint) {
    QString lockProblem;
    if (!DirectorySessionLock::acquire(AppPaths::minecraftDir(), &lockProblem)) {
        emit versionDownloadFinished(instanceId, false, lockProblem);
        return;
    }
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

    // CurseForge 的清单在"换"之前 `files` 是空的（界面在弹确认框之前已经调过一次
    // resolveCurseForgeFiles，见它的注释）。这里这第二道**不是多余的**：漏了它，
    // 走到 startModpackFiles() 就是一个只有加载器、一个 mod 都没有的实例，
    // 而用户看到的终态是"导入好了"。
    if (pack.format == ModpackImport::Format::CurseForge && pack.files.isEmpty()) {
        emit versionDownloadFinished(
            instanceId, false,
            tr("这个 CurseForge 整合包的文件清单没有换成下载地址：那种清单里只有编号，"
               "要先问一次 CurseForge 的接口才换得出来。没有导入，再试一次；一直这样的话，"
               "在设置页「下载」栏里填一份自己的 CurseForge API key。"));
        return;
    }

    m_modpackInstall = ModpackInstall();
    m_modpackInstall.zipPath = zipPath;
    m_modpackInstall.instanceId = instanceId;
    m_modpackInstall.pack = pack;
    m_modpackInstall.manifestFingerprint = manifestFingerprint;

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
    // 导入开始的标记先落下（`isUnfinishedImport()` 靠它区分"我们没导完的"
    // 和"别的启动器装好的加载器实例"，见那边的 ④）。写不进去也不算失败：那种老办法
    // （看薄 json 的形状）仍然认得出这一份；它只是少一层最硬的证据。
    // 标记里顺手记下**这一份清单的指纹**：同名再导入时，`checkImportTargetName()` 靠它
    // 认出"上一次没导完的是这个包"还是"另一个包撞了名字"。标记里没有指纹（读侧解析
    // 不出来的当成"没有指纹"）时照常放行，盘上已有的半成品照样能接着导入。
    QDir().mkpath(AppPaths::versionDir(id));
    QJsonObject markerJson;
    markerJson[QStringLiteral("startedAt")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (!m_modpackInstall.manifestFingerprint.isEmpty()) {
        markerJson[QStringLiteral("manifestSha256")] = m_modpackInstall.manifestFingerprint;
    }
    if (QFile marker(InstanceFiles::importMarkerPath(id)); marker.open(QIODevice::WriteOnly)) {
        marker.write(QJsonDocument(markerJson).toJson(QJsonDocument::Compact));
    }
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
    beginOwnBatch();
    // NOTE: beginBatch() 与 endBatch() 之间**不允许**任何提前 return（见 downloadVersion
    // 顶部那段）：下面这个循环里一句早退都没有，就是为了这个。
    for (const ModpackImport::PackFile &file : files) {
        // 先试哪一条由 preferredUrlOrder() 定（**优先 cdn.modrinth.com**，实测差 3~10 倍，
        // 数字在那边）：换完次序后第二条仍然是"取不回来时再试一次"那条。清单里几条地址是
        // 同一份文件的镜像，对调本身是安全的。
        const QStringList urls = ModpackImport::preferredUrlOrder(file.urls);
        downloadManager->downloadFile(urls.value(0), base + QLatin1Char('/') + file.path,
                                      file.sha1, file.size, VerifyMode::SizeAndSha1,
                                      urls.value(1));
    }
    endOwnBatch();
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
    // 只把明确路径交给后台，所有状态机和 UI 信号仍在主线程收尾。
    //
    // NOTE: 这两个目录名来自清单，**不是写死的**：CurseForge 的清单里
    // `overrides` 那一项是自己写的（真包见过 `overrides` / `overrides-1.20.1` 这种），
    // 而它**没有** client-overrides 这个规矩 —— 那种包的 clientOverridesDir 是空串，
    // 下面那个判断会把它跳过（空串拼出来的前缀是 `/`，那是从压缩包根目录往下解，
    // = 把整包的目录结构摊到实例里，正好是错的）。
    const QStringList roots = {m_modpackInstall.pack.overridesDir + QLatin1Char('/'),
                               m_modpackInstall.pack.clientOverridesDir.isEmpty()
                                   ? QString()
                                   : m_modpackInstall.pack.clientOverridesDir + QLatin1Char('/')};
    QStringList layers;
    for (const QString &root : roots) {
        if (!root.isEmpty()) {
            layers.append(root);
        }
    }
    struct Result { bool ok = false; int extracted = 0; QString error; };
    const auto result = std::make_shared<Result>();
    const QString archive = m_modpackInstall.zipPath;
    const QString destination = AppPaths::versionDir(id);
    m_modpackOverridesFailure.clear();
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, watcher, result, id, destination] {
        watcher->deleteLater();
        if (m_modpackInstall.phase != ModpackInstall::Phase::ExtractingOverrides
            || m_modpackInstall.instanceId != id) return;
        if (!m_modpackOverridesFailure.isEmpty()) {
            const QString reason = m_modpackOverridesFailure;
            m_modpackOverridesFailure.clear();
            failModpackImport(reason);
            return;
        }
        if (!result->ok) {
            failModpackImport(tr("整合包里的覆盖文件没能解开：%1\n\n（清单里那些文件已经下好在 %2 "
                                 "里了，缺的是 config、资源包、光影这些。）")
                                  .arg(result->error, destination));
            return;
        }
        m_modpackInstall.extractedEntries = result->extracted;
        finishModpackImport();
    });
    m_modpackOverridesCancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_modpackOverridesCancelled;
    m_modpackOverridesWork = QtConcurrent::run([result, archive, destination, layers, cancelled] {
        result->ok = ZipUtil::extractSubtrees({archive}, destination, layers,
                                              &result->extracted, &result->error,
                                              [cancelled] { return cancelled->load(); });
    });
    watcher->setFuture(m_modpackOverridesWork);
}

void VersionManager::finishModpackImport() {
    // 要用的字段先取出来：reset() 会把它们一起清掉（忘了取的表现是那句话里名字变空的）。
    const QString id = m_modpackInstall.instanceId;
    const ModpackImport::Pack pack = m_modpackInstall.pack;
    const int skipped = pack.filesSkipped;
    const int notDistributable = pack.filesNotDistributable;
    const int missing = pack.filesMissing;
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
    // 导入做完了：把"正在导入"的标记摘掉（判据见 InstanceFiles::isUnfinishedImport()）。
    // 摘了也不会把"meta 没写成功"那一种变成正常实例：那份 json 还是薄的三格，形状判据
    // 照样认得出它 —— 用户想再导一遍时仍然走"接着上次的导入"，不会丢那条路。
    QFile::remove(InstanceFiles::importMarkerPath(id));

    // 少装了什么必须说出来（跳过的文件、解开的覆盖文件）—— 用户看不到的那部分最容易
    // 变成"进了游戏才发现少个 mod"。
    QString extra;
    if (skipped > 0) {
        extra += tr("（清单里有 %1 个文件按包里的说明跳过了：只给服务端用、没有下载地址、"
                    "或者它自己是一个要单独装的整合包）")
                     .arg(skipped);
    }
    if (extracted > 0) {
        extra += tr("（解开了 %1 个覆盖文件）").arg(extracted);
    }
    // 那两种"没下成"要**分开说**（.hpp 里那两条 NOTE）：一种是作者不许代下（用户自己能补，
    // 所以得说清"怎么办"），另一种是接口里查不到（用户补不了，只能换包或者认了）。
    if (notDistributable > 0) {
        extra += tr("\n\n有 %1 个文件作者关了「允许第三方分发」，没有替你下。少了它们这个包"
                    "多半进不去游戏：去 CurseForge 上那个整合包的页面把它们下下来，手动放进"
                    "%2 里（模组放 mods/、资源包放 resourcepacks/、光影放 shaderpacks/ —— "
                    "对照那个整合包页面上的文件列表。清单里只有它们的编号，没有名字）。")
                     .arg(notDistributable)
                     .arg(AppPaths::versionDir(id));
    }
    if (missing > 0) {
        extra += tr("\n\n还有 %1 个文件在 CurseForge 上已经查不到了（作者删了、或者没过审）——"
                    "这一部分没有办法补，这个整合包可能已经过期。")
                     .arg(missing);
    }

    m_modpackInstall.reset();
    emit versionDownloadFinished(id, true,
                                 tr("整合包「%1」导入好了，实例列表里叫「%2」。%3")
                                     .arg(pack.name, id, extra));
}

void VersionManager::failModpackImport(const QString &reason) {
    if (m_modpackOverridesWork.isRunning()) {
        // 还在写盘时不释放占用，避免第二趟安装与尚未结束的解压互相覆盖。
        m_modpackOverridesFailure = reason;
        if (m_modpackOverridesCancelled) m_modpackOverridesCancelled->store(true);
        return;
    }
    // 先把加载器那半截收掉。LoaderInstall::reset() **不发**任何信号正是为了这里：整件事的
    // 终态只能发一个，否则界面会先弹一次"加载器没装成"、再弹一次"整合包没导入"。
    m_loaderInstall.reset();
    // 批次可能正开着（失败正好打在两批之间）：不放弃的话 m_batchOpen 永远是 true，之后每次
    // 下载都被"已有下载任务正在进行"顶回来（同 recoverFromInternalError 里那段）。
    abandonOwnBatch();

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
        // 只发一条消息的话按钮再也亮不回来（"加载器安装还没接通"那一段踩过同样的坑，
        // 见 installLoaderVersion() 里 loaderInstallSupported 那道闸口旁边的注释）。
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

    QString lockProblem;
    if (!WriteDirectoryGuard::acquire(AppPaths::versionDir(instanceId), &lockProblem)) {
        emit instanceInstallMessage(lockProblem);
        emit versionDownloadFinished(instanceId, false, lockProblem);
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
    abandonOwnBatch();

    const QString id = m_namedInstance.instanceId;
    m_namedInstance.reset();
    emit versionDownloadFinished(id, false, reason);
}

bool VersionManager::writeVersionJson(const QString &versionId, const QString &destinationPath,
                                      const QJsonObject &json, QString *errorOut) {
    const QString invalid = InstanceFiles::validatePathSegment(versionId);
    if (!invalid.isEmpty()) { if (errorOut) *errorOut = invalid; return false; }
    if (!WriteDirectoryGuard::acquire(destinationPath, errorOut)) return false;
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
    const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        if (errorOut) *errorOut = tr("版本 JSON 写入不完整，原文件未替换。");
        return false;
    }
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
    // 在调它之前自己说。
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

    // **同一个加载器**上上一发还在飞就先掐掉（换了游戏版本、或者同一局重新问一遍）——
    // 换局之后旧那一发回来会把上一局的版本号填进这一局的列表里。abort 之后 finished()
    // 照样会来一发，回复处理函数靠"这一个指针还是我手上这个吗"把它丢掉。
    // 别的加载器在飞的那几发**不动**：四个各占一格，四条一起问是正常的（见头里那段）。
    LoaderVersionRequest &slot = m_loaderVersionRequests[int(kind)];
    if (slot.reply != nullptr) {
        // **先摘指针，再 abort()** —— 顺序不能换。abort() 有可能**当场**把 finished()
        // 发出来（回复已经在手上、只是还没被处理时就是这一种）。那一发进来时这一格里
        // 若还是旧指针，onLoaderVersionsReply() 的 "还是我手上这一发吗"当场判成"是" ⇒
        // 我们自己掐掉的那一发被当成一次真回复，
        // 给用户弹一句「拿不到 X 的版本列表（Operation canceled）」。用户什么都没做错。
        QNetworkReply *stale = slot.reply;
        slot.reply = nullptr;
        stale->abort();
    }
    slot.gameVersion = wanted;

    if (wanted.isEmpty()) {
        emit loaderVersionsFetched(wanted, kind, false, LoaderMeta::LoaderVersionList(),
                                   tr("还没选要装哪个版本，先在上面挑一个。"));
        return;
    }
    if (!loaderInstallSupported(kind)) {
        emit loaderVersionsFetched(wanted, kind, false, LoaderMeta::LoaderVersionList(),
                                   tr("%1 的安装还没接通。").arg(loaderDisplayName(kind)));
        return;
    }

    // OptiFine 的列表**必须跟着下载源**走：列出来的版本要能用同一个源装下去。镜像那条路的
    // 地址最后两段是 type/patch（见 LoaderMeta::optifineMirrorPathFromEdition），官方那条路
    // 得先过广告页拿 token —— 两份列表混着用就会"列出来了但下不了"。
    //
    // 另外三个没这个问题（Fabric 只有官方一个源；两份 maven 清单官方就是发布处），一律走官方。
    // NOTE: 两份 maven 清单这里**不换镜像**，和安装那条路（fetchInstallerList，镜像挑不出来
    // 再退官方）不一样。那条教训是实测的：镜像上那份 Forge 版本表是旧文件（1782 B、
    // 最新只到 1.15.2），照着它列会给用户一个"1.16 以后都没有 Forge"的假结论。
    // 列表这件事对"全不全"比对"快不快"敏感得多，所以宁可直接问官方。
    QString url;
    // "这一发走没走镜像"**只在这里读一次**，然后一路带到解析那一步去（同一次读的结果）：
    // 解析那边若再读一遍 downloadManager->useMirror()，两次之间用户点一下开关就会出现
    // "正文是镜像那份、却按官方那套规矩解析"。见 LoaderVersionRequest 里那段 NOTE。
    bool fromMirror = false;
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
        fromMirror = downloadManager->useMirror();
        url = fromMirror ? LoaderMeta::optifineMirrorListUrl(wanted)
                         : LoaderMeta::optifineDownloadsPageUrl();
        break;
    }
    // 这两格跟着**这一局**重来：上一局退到镜像没有、官方地址是哪一条。
    slot.officialUrl = url;
    slot.mirrorFallbackUsed = false;
    fetchLoaderVersions(kind, url, fromMirror);
}

QString VersionManager::loaderVersionsMirrorFallback(LoaderKind kind,
                                                     const QString &officialUrl) const {
    // 为什么只有 NeoForge 能这么退：
    // * Forge 那个同名 maven-metadata.xml 在镜像上是**旧的**（ 实测 latest 停在
    // `1.18-38.0.17`、64,569 B；官方 213,257 B），它的 promotions 表更早（停在 1.15.2）。
    // 照镜像那份列版本会得出「26.3 没有 Forge」的假结论 —— 这正是 requestLoaderVersions
    // 里那段"两份 maven 清单不换镜像"注释防的那件事。
    // * Fabric 的 host 镜像不认（DownloadManager::mirrorUrl() 原样返回）。
    // * OptiFine 两条路的正文格式不一样（镜像 JSON / 官方网页），换源要连带换解析规矩，
    // 不在这里掺和。
    if (kind != LoaderKind::NeoForge || officialUrl.trimmed().isEmpty()) {
        return QString();
    }
    const QString mirrored = DownloadManager::mirrorUrl(officialUrl);
    return mirrored == officialUrl ? QString() : mirrored;
}

QNetworkAccessManager *VersionManager::loaderNetworkManager() {
    if (loaderNetwork == nullptr) {
        loaderNetwork = new QNetworkAccessManager(this);
    }
    return loaderNetwork;
}

void VersionManager::fetchLoaderVersions(LoaderKind kind, const QString &url, bool fromMirror) {
    const QUrl target(url);
    QNetworkRequest request(target);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetworkManager()->get(request);
    m_loaderVersionRequests[int(kind)].reply = reply;
    // 走没走镜像跟着这一发钉住：解析要按**问的时候**那条路来（见 hpp 里那一格）。
    m_loaderVersionRequests[int(kind)].fromMirror = fromMirror;
    // 回复得**带着是哪个加载器的**回来：四发同时在飞，光认指针认不出是谁的回复。
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, kind] { onLoaderVersionsReply(kind, reply); });
}

void VersionManager::onLoaderVersionsReply(LoaderKind kind, QNetworkReply *reply) {
    reply->deleteLater();
    // 这一发还是我手上这一发吗？不是就说明调用方已经换了目标（abort 掉的那一发也会走到
    // 这里），这份回复没人要了。
    QHash<int, LoaderVersionRequest>::iterator slot = m_loaderVersionRequests.find(int(kind));
    if (slot == m_loaderVersionRequests.end() || slot->reply != reply) {
        return;
    }
    // 先把字段抄下来再清空那一格：下面几条 emit 都要用它们，清空之后就没得读了。
    const QString gameVersion = slot->gameVersion;
    // 走没走镜像读的是**这一发问出去时**记下的那一个（不是现在这个开关值）。
    const bool fromMirror = slot->fromMirror;
    slot->reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        // 先判错误**再读**：超时回来的那条 `QNetworkReply` 已经不 open 了，
        // `readAll()` 会往 stderr 甩一句 `QIODevice::read (QNetworkReplyHttpImpl): device
        // not open`；日志里看着像另一处坏掉了，其实这一发本来就是要走下面的报错/换镜像。
        // 下面成功那一支再取 body。
        // 官方那条**连不上**时往镜像退一次（只 NeoForge，理由见
        // loaderVersionsMirrorFallback()）。判"连不上"用的是下载那条路同一个
        // transientReply()：超时 / 连接被掐 / HTTP/2 断流都算，404、证书不对、
        // 用户取消不算 —— 那些换镜像也帮不上忙。只退一次，镜像再失败就照实报。
        if (!slot->mirrorFallbackUsed && DownloadRetryPolicy::transientReply(reply)) {
            const QString fallback = loaderVersionsMirrorFallback(kind, slot->officialUrl);
            if (!fallback.isEmpty()) {
                slot->mirrorFallbackUsed = true;
                qInfo() << "Loader version list from" << slot->officialUrl << "failed ("
                        << reply->errorString() << "); retrying through the mirror:" << fallback;
                fetchLoaderVersions(kind, fallback, /*fromMirror=*/true);
                return;
            }
        }
        emit loaderVersionsFetched(
            gameVersion, kind, false, LoaderMeta::LoaderVersionList(),
            tr("拿不到 %1 %2 的版本列表（%3）。检查网络后重试。")
                .arg(loaderDisplayName(kind), gameVersion, reply->errorString()));
        return;
    }

    // 不 open 就当空 body（见上面那一段注）：正常成功回来时它是 open 的。
    const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();

    const LoaderVersionParse parsed = parseLoaderVersions(kind, gameVersion, body, fromMirror);
    if (!parsed.error.isEmpty()) {
        emit loaderVersionsFetched(gameVersion, kind, false, LoaderMeta::LoaderVersionList(),
                                   parsed.error);
        return;
    }
    emit loaderVersionsFetched(gameVersion, kind, true, parsed.versions, QString());
}

VersionManager::LoaderVersionParse VersionManager::parseLoaderVersions(
    LoaderKind kind, const QString &gameVersion, const QByteArray &body, bool fromMirror) const {
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
        // 判据是**这一发问出去时**记下的那个（参数），不是现在这个开关值 —— 见 hpp 里那段
        // NOTE：两次读之间用户点一下开关，正文和解析规矩就会错配。
        if (fromMirror) {
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

    // 直接送那份带 `stable` 标记的列表：界面要用它标「测试版」、也要用它决定
    // 「下载稳定版（release）」那一行能不能点（见 MainWindow::fillLoaderVersionItems）。
    result.versions = entries;
    // NOTE: "这个游戏版本一个能装的都没有"**不算读失败**（`error` 留空、按 success 发出去，
    // 版本列表是空的）。四种加载器里都是**真结论**：比如 Forge 还没支持某个刚发的
    // 快照、OptiFine 只发了预览版 —— 不是网络问题、也不是清单读不懂，重试一百次
    // 还是这个答案。两种答案在数据上分得开：**空列表 = 就是没有**（界面写「无可用版本」、
    // 底下连那两档都不留）、**success=false = 没问到**（界面写「列表没取到」、
    // 底下留着那两档，因为"让安装器自己挑"那条路不依赖这份列表）。
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
    QString lockProblem;
    if (!DirectorySessionLock::acquire(AppPaths::minecraftDir(), &lockProblem)) {
        emit loaderInstallMessage(lockProblem);
        return;
    }

    m_loaderInstall = LoaderInstall();
    if (installerProcess) {
        installerProcess->setProperty("wtfmlCertificateRetried", false);
        // 卡死重起只给一次（见 kInstallerStallTimeoutMs）；这两个计数每次新安装都从零开始。
        installerProcess->setProperty("wtfmlStallRestarts", 0);
        installerProcess->setProperty("wtfmlStallRestartPending", false);
    }
    m_loaderInstall.kind = kind;
    m_loaderInstall.gameVersion = gameVersion;
    m_loaderInstall.loaderName = loaderDisplayName(kind);
    m_loaderInstall.javaPath = javaPath;
    // 界面那一格可能是三种东西（见 LoaderMeta::loaderRequestFromChoice）：一个具体版本号、
    // `@latest` / `@stable` 两个频道键、或者空串（没挑过）。在这里**一次**解码成
    // （要钉的版本号 / 按哪一档挑），后面几条路都只读那两个字段，不再回头看那个字符串 ——
    // 解码散在多处的话，"选了最新版却装了稳定版"那种错就是对不出来。
    const LoaderMeta::LoaderRequest choice =
        LoaderMeta::loaderRequestFromChoice(pinnedLoaderVersion.trimmed());
    m_loaderInstall.pinnedVersion = choice.pinnedVersion;
    m_loaderInstall.channel = choice.channel;

    // 第一步就分开了：Fabric 拉的是"加载器版本列表"，Forge/NeoForge 拉的是"安装器该用
    // 哪个版本"，OptiFine 连"版本表"都和前三个不是一回事（镜像一条 JSON、官方一张 203 KB
    // 的网页），接口完全不是一回事（见 LoaderMeta.hpp 顶部那段）。
    if (kind == LoaderKind::Fabric) {
        // 版本被钉死了就别去问版本表了 —— 直接拉那个版本的 profile。
        // NOTE: 这一步对整合包是**必须**的：它的依赖表里写着 `fabric-loader: 0.16.14`，
        // 包里那些模组正是按这个版本配的，换成"最新的正式版"是另一回事（多半也能跑，
        // 但那是猜）。镜像那份旧表挑不出来的情况也因此完全绕开了。
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
        // NOTE: 这个分支必须先判钉死。无条件去拉版本表的话，"钉死一个 OptiFine
        // 版本"会被无声忽略 —— 用户在安装页上选了 H7，装出来的是版本表里那条最新的 H9。
        // 症状还特别难认：装是装成了、版本目录也在，只是不是他选的那个。
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
    // 就是 FetchingInstallerList。
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
    const QUrl url(LoaderMeta::fabricLoaderListUrl(m_loaderInstall.gameVersion));
    QNetworkRequest request(url);
    // 和 DownloadManager 那几处元数据请求同一个超时：卡死的连接会让界面永远停在
    // "下载中"（按钮是禁用的），而这个请求没有别的兜底。
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetworkManager()->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onFabricLoaderListReply(reply); });
}

void VersionManager::onFabricLoaderListReply(QNetworkReply *reply) {
    reply->deleteLater();
    // 状态已经换过（上一次失败收了尾、或者用户又点了一次）：这份回复没人要了。
    if (m_loaderInstall.phase != LoaderInstall::Phase::FetchingList) {
        return;
    }

    // 读之前先看设备开没开：超时/掐断回来的那条已经不 open，readAll() 会甩
    // `QIODevice::read ... device not open`。
    // **仍然不拿 error() 当门**（理由见下面那条 ★★ 400 带正文的坑）。
    const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    // （1.12.2 实测）：**先看回复里有没有能读的东西，再看传输层报没报错** ——
    // 不能拿 `reply->error()` 当第一道门。
    //
    // Fabric 的接口遇到它**从没支持过**的游戏版本（1.12.2）回的是 `HTTP 400`、
    // **正文带着一个 `[]`**；Qt 把 400 记进 `reply->error()`，若照
    // `error != NoError` 判断，用户拿到的是「检查网络后重试」——
    // 一个**永远不可能成功**的版本被报成网络抖动，连点多少次都是同一句话。
    // 正文读得出来就说明这一趟传输是通的，
    // 该走的是下面那条「列表是空的 ⇒ 这个版本还没支持」。
    if (!doc.isArray()) {
        failLoaderInstall(tr("拿不到 %1 的加载器列表（%2）。检查网络后重试。")
                              .arg(m_loaderInstall.gameVersion, reply->errorString()));
        return;
    }

    const LoaderMeta::LoaderPick pick =
        LoaderMeta::pickFabricLoader(doc.array(), m_loaderInstall.channel);
    if (!pick.ok()) {
        // 快照、或者比 Fabric 还支持的最老那一档（1.14）更早的版本（1.12.2 就是）：接口
        // 回一个 `[]`（**而且带 400**，所以上一道门不能拿 `reply->error()` 判，见上面那条
        // NOTE）。这不是网络问题，也不能退回"装原版"——用户要的是带加载器的版本，装出来
        // 一个纯原版是骗人。
        failLoaderInstall(tr("Fabric 还没有支持 %1 的加载器（拿到的列表是空的）。")
                              .arg(m_loaderInstall.gameVersion));
        return;
    }

    m_loaderInstall.loaderVersion = pick.version;
    m_loaderInstall.loaderId =
        LoaderMeta::fabricLoaderId(m_loaderInstall.gameVersion, pick.version);
    emit loaderInstallMessage(tr("挑中 %1 %2（%3）。")
                                  .arg(m_loaderInstall.loaderName, pick.version,
                                       pickChannelDetail(m_loaderInstall.channel, pick.stable)));
    m_loaderInstall.phase = LoaderInstall::Phase::FetchingProfile;
    fetchFabricProfile();
}

void VersionManager::fetchFabricProfile() {
    const QUrl url(LoaderMeta::fabricProfileUrl(m_loaderInstall.gameVersion,
                                                m_loaderInstall.loaderVersion));
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetworkManager()->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onFabricProfileReply(reply); });
}

void VersionManager::onFabricProfileReply(QNetworkReply *reply) {
    reply->deleteLater();
    if (m_loaderInstall.phase != LoaderInstall::Phase::FetchingProfile) {
        return;
    }

    const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
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
    // - 只有目录、没有 json：老下载残骸。以为"装好了"的话，这个加载器版本启动时
    // 会报"版本信息缺失"。
    // - 有 json、没有 jar：**我们自己的下载残骸** —— downloadVersion() 是先写 json
    // 再下 jar 的，jar 那一批失败就剩这个形态。以为"装好了"的话，用户拿到的是
    // 一个启动不起来、而且看不出为什么的加载器版本。
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
    // 两个接口一个回 JSON、一个回 XML，但都是"GET 一段文本，从里面挑一个版本号"，
    // 所以共用这一条路，分派放在回复处理里按 kind 做。
    // 版本表本身也走镜像：Forge 那份 1.7 KB 在 files.minecraftforge.net 上，NeoForge 那份
    // 63 KB 在 maven.neoforged.net 上，两个 host 都在 DownloadManager::mirrorUrl() 里认得了。
    // NOTE: 这条路**不是** downloadManager 发的（它只是 GET 一段文本、没有落盘），所以镜像
    // 得在这儿自己换一次 —— downloadFile() 那条路不会替它换。
    m_loaderInstall.listOfficialUrl =
        m_loaderInstall.kind == LoaderKind::Forge ? LoaderMeta::forgePromotionsUrl()
                                                  : LoaderMeta::neoforgeMavenMetadataUrl();
    const QString url = downloadManager->endpoint(m_loaderInstall.listOfficialUrl);
    m_loaderInstall.listRequestWasMirror = (url != m_loaderInstall.listOfficialUrl);
    m_loaderInstall.listRetriedOfficial = false;
    // 开局就落在镜像上的话，之后官方也失败时**不再**往镜像退（退过一次就够）。
    m_loaderInstall.listMirrorTried = m_loaderInstall.listRequestWasMirror;
    requestInstallerList(url);
}

void VersionManager::requestInstallerList(const QString &url) {
    // NOTE: 这里必须写成"先落到一个具名变量"，不能写成 QNetworkRequest request(QUrl(url)) ——
    // 那个形状在 C++ 里会被当成函数声明（most vexing parse），报的错还很难认
    // （"request 不是类类型"）。
    const QUrl target(url);
    QNetworkRequest request(target);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetworkManager()->get(request);
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

    const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
    if (reply->error() != QNetworkReply::NoError) {
        if (retryInstallerListFromOfficial()) {
            return;
        }
        if (!m_loaderInstall.listMirrorTried &&
            DownloadRetryPolicy::transientReply(reply)) {
            const QString fallback = loaderVersionsMirrorFallback(
                m_loaderInstall.kind, m_loaderInstall.listOfficialUrl);
            if (!fallback.isEmpty()) {
                m_loaderInstall.listMirrorTried = true;
                m_loaderInstall.listRequestWasMirror = true;
                // 官方这条**已经**试过了（就是刚失败的这一发），镜像那条别再往回弹。
                m_loaderInstall.listRetriedOfficial = true;
                emit loaderInstallMessage(
                    tr("%1 版本表的官方地址连不上（%2），改用镜像再问一次...")
                        .arg(m_loaderInstall.loaderName, reply->errorString()));
                requestInstallerList(fallback);
                return;
            }
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
        pick = LoaderMeta::pickForgeVersion(doc.object(), m_loaderInstall.gameVersion,
                                            m_loaderInstall.channel);
    } else {
        pick = LoaderMeta::pickNeoForgeVersion(body, m_loaderInstall.gameVersion,
                                               m_loaderInstall.channel);
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
                                       pickChannelDetail(m_loaderInstall.channel, pick.stable)));
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

    beginOwnBatch();
    // phase 放在 beginBatch() **之后**设：那个函数在"上一批还没收尾就又开一批"那条异常
    // 支路里会替上一批收尾、并发一次 allDownloadsFinished（见它的实现），那一次不该被
    // 当成「安装器下好了」。正常路径上调用方前面已经用 isBusy() 挡过，走不到那条支路；
    // 顺序仍然按这个摆（先清场、再进相位）。
    m_loaderInstall.phase = LoaderInstall::Phase::WaitingInstallerJar;
    // 安装器 jar 没有公布 sha1（Forge 只在下载页给别的东西，NeoForge 的 maven-metadata
    // 里也没有），所以这里只能"文件在就算好" —— 第二次装同一个加载器版本时省掉这 6 MB。
    //
    // NOTE: 这一条路上没有 sha1，也没有字节数 —— 一份 200 + HTML 错误页会被当成"下好了"
    // 落盘，而且因为"已存在"**再也不会重下**，症状是那个加载器版本永远装不上、
    // 每次都在同一处失败。所以这一条路上还剩的判据是 **jar 必须是 zip**（`PK` 那四个
    // 字节），判在 DownloadManager 里那两处"别的判据都没有"的支路上（落盘那一步 +
    // fileAlreadyValid）。**不是**真的完整性校验 —— 拿不到 sha1 就是拿不到，这一句
    // 别读成"已经校验过了"。
    // 安装器 jar 也走镜像（6 MB 上下，是这条路上最大的一笔）。镜像上那份和官方是同一个
    // 文件。installerUrl 本身**不改**：盘上那个文件名是从它切的，
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
    endOwnBatch();
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
                                            QString *errorOut) {
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
                                                    const QString &targetDir,
                                                    const QString &mavenRoot,
                                                    bool supportsMirror) {
    // `--installClient` 后面那个目录**必须**给：不给的话安装器会落到 user.home/.minecraft
    // （这台机器上是 C:\Users\User\.minecraft），装到一个用户根本不知道的地方去。
    QStringList args{QStringLiteral("-jar"), installerPath, QStringLiteral("--installClient"),
                     targetDir};
    // 把安装器**自己**那条下载路钉到一个明确的根上。不给它这个参数时它不去官方 maven，
    // 而是按内置的第三方镜像列表挑（本机挑中 creeperhost）：实测慢一倍（48 个库 61 秒 vs
    // 30 秒），而且那条连接一坏它就把整次安装判死 —— 的 Forge 1.20.1 就是这样
    // 死在 `org.ow2.asm:asm:9.6` 上的。所以**两种源模式都要给**：
    // * 镜像模式 → BMCLAPI 的 /maven/；
    // * 官方模式 → 加载器自己的官方 maven 根（见 LoaderMeta::forgeMavenRoot() 那一节）。
    // 它只对库生效 —— 原版那几个库（libraries.minecraft.net）它照旧走自己那条路。
    // 探到它不认 `--mirror`（老安装器）就**不能给**：joptsimple 把不认识的选项当致命错误，
    // 当场退出码 1。宁可这一条路慢一点，也不能让整个安装死在"多给了一个参数"上。
    if (!mavenRoot.isEmpty() && supportsMirror) {
        args << QStringLiteral("--mirror") << mavenRoot;
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
    // Windows 走 `%APPDATA%/.minecraft`，Linux 走 `user.home/.minecraft` —— 后者改
    // 环境变量根本改不动（Java 是从系统里取的，不是从环境变量里读的），于是它会跑到
    // 用户真实的 ~/.minecraft 里去动手。那种平台上宁可不装，也不能这么干。
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
    installerProcess->setProperty("wtfmlLastOutputMs", QDateTime::currentMSecsSinceEpoch());
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
        // 别在这儿判死刑 —— 那个年代的安装器把自己该做的事全写在自己 jar 里那份
        // install_profile.json 里了，照着它装**既不用开它的界面、也不用起 JVM**。
        // 也不能报"本启动器替你点不了"让用户去找别的启动器：一台只有本启动器的新电脑上，
        // 默认挑的那个「稳定版」（1.12.2 就是 2847）永远装不上。见 LegacyForgeInstall.hpp 文件头。
        startLegacySelfInstall();
        return;
    }

    if (m_installerOptions.probed && !m_installerOptions.supportsMirror &&
        downloadManager->mirrorPreferred()) {
        emit loaderInstallMessage(tr("这个安装器不认 --mirror，它自己下库那一段只好走官方"
                                     "（启动器这边下的库仍然走镜像）。"));
    }
    startInstallerProcess();
}

// ---------------------------------------------------------------------------
// 老 Forge（那个年代的安装器没有命令行安装这条路）：照它自己那份安装说明装
// ---------------------------------------------------------------------------
//
// 为什么走这条路、以及它自己那三条实测结论（universal jar 不能带 url、没有 url 的库往
// Mojang 猜、原版已经带上的要丢掉）全在 LegacyForgeInstall.hpp 的文件头里。
//
// 这里只做"接线"那一半：从安装器 jar 里读出安装说明和那颗 universal jar、落盘摆好，剩下
// 的两段复用既有的路 —— 先补原版（WaitingParentJson → WaitingParentFiles），再下加载器
// 自己那几个库（startLoaderFiles()）。

void VersionManager::startLegacySelfInstall() {
    m_loaderInstall.legacySelfInstall = true;
    emit loaderInstallMessage(
        tr("%1 %2 的安装器没有命令行安装这条路（它的帮助里只有 --extract / --installServer / "
           "--offline）—— 那个年代它把「装客户端」做成了图形界面。\n"
           "我们不去开那个界面：它自己 jar 里那份安装说明（install_profile.json）写明了该"
           "怎么装，本启动器照着那份说明装（不用点，也不用起那个安装器）。")
            .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion));

    // 先取原版那份 json。它有两个用处：安装说明里那些"没有 url、但原版自己已经带着"的库
    // 要靠它认出来（认出来就不用下重），以及"这份版本基于哪个原版"要和它核对一遍。
    m_loaderInstall.phase = LoaderInstall::Phase::WaitingParentJson;
    fetchVersionDetails(m_loaderInstall.gameVersion);
}

void VersionManager::writeLegacyProfile(const QJsonObject &parentJson) {
    // 认不出来时的那句话：**别**把"读不懂它的说明"说成"这个版本装不了"—— 前者的药是
    // 换一份安装器 / 修 jar，后者是去找别的启动器。两颗参数各用一次（同一个编号写两遍
    // 会被 arg 全部替换， 那个 %1 的坑就在旁边）。
    const auto failWithProblem = [this](const QString &reason) {
        const QString namedVersion =
            m_loaderInstall.loaderName + QLatin1Char(' ') + m_loaderInstall.gameVersion;
        failLoaderInstall(
            tr("%1 %2 的安装器没有命令行安装这条路，我们本想照它自己 jar 里的安装说明"
               "（install_profile.json）自己装，但那份说明用不上：%3\n"
               "（%4 这一份的格式不是那个年代的，或者下下来的 jar 本身坏了。）\n"
               "这个版本请用原版启动器或 PCL 装一次 %5：装完本启动器认得出它、也能正常启动。")
                .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion, reason,
                     m_loaderInstall.installerFileName, namedVersion));
    };

    // 两个条目都从 cache/installers/ 下那份安装器 jar 里读（只读，不动别的任何东西）。
    QByteArray profileBytes;
    QString error;
    const QString readWhat = tr("读 %1 的安装说明").arg(m_loaderInstall.installerFileName);
    const ZipUtil::EntryStatus profileStatus = ZipUtil::readEntry(
        m_loaderInstall.installerPath, QStringLiteral("install_profile.json"), &profileBytes,
        &error, readWhat);
    if (profileStatus != ZipUtil::EntryStatus::Ok) {
        // NotFound 那一档 errorOut 是空的（没有细节可给）—— 自己说一句。
        failWithProblem(profileStatus == ZipUtil::EntryStatus::NotFound
                            ? tr("jar 里没有 install_profile.json 这个文件")
                            : error);
        return;
    }

    const LegacyForgeInstall::Plan plan = LegacyForgeInstall::planFromInstallProfile(
        profileBytes, parentJson, m_loaderInstall.loaderId);
    if (!plan.ok) {
        using LegacyForgeInstall::Problem;
        QString reason;
        switch (plan.problem) {
        case Problem::NotJson:
            reason = tr("那个文件不是一份 json（它坏了）");
            break;
        case Problem::ModernProfile:
            reason = tr("里面没有 install / versionInfo 这两节（这是新格式的安装器，"
                        "本来该走它自己的命令行安装）");
            break;
        case Problem::MissingInstall:
            reason = tr("install 那一节缺 %1").arg(plan.detail);
            break;
        case Problem::UnknownPath:
            reason = tr("install.path「%1」拼不出一个安全的 maven 路径").arg(plan.detail);
            break;
        case Problem::NoParent:
            reason = tr("认不出它基于哪个原版（inheritsFrom / install.minecraft 都是空的）");
            break;
        case Problem::NoLibraries:
            reason = tr("它的库表是空的");
            break;
        case Problem::None:
            break;
        }
        failWithProblem(reason);
        return;
    }

    // 说明书说要基于的原版，必须就是我们正要装的那个 —— 不是的话，照着它写出来的 json 会
    // 指向一个我们**没下过**的原版（启动时那一句 NoClassDefFoundError 就是这么来的）。
    // 实测两份 profile 里都是同一个值，这一条是防"哪天上游改了"。
    if (plan.inheritsFrom != m_loaderInstall.gameVersion) {
        failWithProblem(tr("它的安装说明说这份版本基于「%1」，不是我们选的「%2」")
                            .arg(plan.inheritsFrom, m_loaderInstall.gameVersion));
        return;
    }

    // 它 jar 里那颗 universal jar（真实安装器会把它摆进 libraries/，我们照摆）。
    QByteArray universal;
    const ZipUtil::EntryStatus universalStatus = ZipUtil::readEntry(
        m_loaderInstall.installerPath, plan.universalEntry, &universal, &error,
        tr("读 %1 里的 %2").arg(m_loaderInstall.installerFileName, plan.universalEntry));
    if (universalStatus != ZipUtil::EntryStatus::Ok) {
        failWithProblem(universalStatus == ZipUtil::EntryStatus::NotFound
                            ? tr("jar 里没有 %1 这个文件").arg(plan.universalEntry)
                            : error);
        return;
    }

    // 摆到 libraries/<它自己的 maven 坐标算出来的位置>。这份 json 里那颗库是**只有 name、
    // 没有 url** 的写法（见 LegacyForgeInstall.hpp），所以它不会被下载 —— 就是在等这一份。
    const QString universalTarget =
        AppPaths::libraryDir() + QLatin1Char('/') + plan.universalLibraryPath;
    if (!QDir().mkpath(QFileInfo(universalTarget).absolutePath())) {
        failWithProblem(tr("建不出目录 %1").arg(QFileInfo(universalTarget).absolutePath()));
        return;
    }
    {
        // QSaveFile：和版本 json 那边同一个理由 —— 半截的 jar 留在 libraries/ 里，症状是
        // 启动时一个莫名其妙的类找不到，而"装"的那一步一路绿灯。
        QSaveFile file(universalTarget);
        if (!file.open(QIODevice::WriteOnly)) {
            failWithProblem(tr("写不进 %1（%2）").arg(universalTarget, file.errorString()));
            return;
        }
        file.write(universal);
        if (!file.commit()) {
            failWithProblem(tr("写 %1 的时候失败了（%2）").arg(universalTarget, file.errorString()));
            return;
        }
    }

    // 版本 json 落盘（id 是**我们的**那个，见 LegacyForgeInstall.hpp 里那条 NOTE）。
    if (!writeVersionJson(m_loaderInstall.loaderId, AppPaths::versionDir(m_loaderInstall.loaderId),
                          plan.versionJson, &error)) {
        failWithProblem(error);
        return;
    }
    // 内存里那份也要摆好：下一步 startLoaderFiles() 走的就是它（它只认 versionJsons 里那张
    // 表，不是盘上那份）。
    versionJsons[m_loaderInstall.loaderId] = plan.versionJson;

    emit loaderInstallMessage(
        tr("%1 %2 的版本文件写好了（%3 条库，其中 %4 条原版已经带着、不用下）。接着补原版 %5 "
           "的文件，然后下加载器自己那几个库...")
            .arg(m_loaderInstall.loaderName, m_loaderInstall.loaderVersion)
            .arg(plan.versionJson.value(QStringLiteral("libraries")).toArray().size())
            .arg(plan.droppedFromParent)
            .arg(m_loaderInstall.gameVersion));

    // 和安装器那条路一样：装完接着把原版补齐（安装器不下资源文件，我们更不会）。这一批
    // 是从"刚收到原版 json"那条栈上起的，排到下一轮（同 startParentCompletion 那边）。
    const QString parent = m_loaderInstall.gameVersion;
    m_loaderInstall.phase = LoaderInstall::Phase::WaitingParentFiles;
    deferLoaderStep(LoaderInstall::Phase::WaitingParentFiles, [this, parent] {
        downloadVersion(parent, AppPaths::versionDir(parent));
    });
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
    // （set 一次之后每次 start 都用它），OptiFine 那条路换过 APPDATA 之后不还原的话，下一次
    // 装 Forge/NeoForge 会把 APPDATA 指到一个**已经删掉的**临时目录上。
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (optifine) {
        // 只有 Windows 能走到这里（上面那道守卫）。
        //
        // * 入口是 `optifine.Installer`：jar 的 Main-Class 是 `optifine.InstallerFrame`
        // （Swing 窗口，headless 下当场 HeadlessException），真正的无界面入口是前者，
        // 而且**一个参数都不用给**（版本号它自己从自己那份 jar 里读）。
        // * -Djava.awt.headless=true 是**必须**的：它出错时会弹一个框等人点，不设这个的话
        // 进程永不退出，只能等我们那个半小时的超时把它杀掉。
        // * APPDATA 指到临时目录：它只认这一个地方，命令行里给目录它看都不看。
        env.insert(QStringLiteral("APPDATA"), optifineWorkRoot());
        installerProcess->setArguments({QStringLiteral("-Djava.awt.headless=true"),
                                        QStringLiteral("-cp"), m_loaderInstall.installerPath,
                                        QStringLiteral("optifine.Installer")});
    } else {
        // 和原版启动器一样的调用方式。命令行本体在 forgeInstallerArguments() 里（是个纯
        // 函数，`--mirror` 那一条就是靠它被钉住的，见 verify 的 V6c）。
        // 传的是 mirrorPreferred() 而不是 useMirror()：镜像正被降级时**不能**再把安装器
        // 拨到它那边去 —— 降级的判据是"这条链子慢到 10 秒给不出 64 KB"，和走的是哪条
        // 路由无关（maven 那一支实测快，但真慢起来也不该硬塞）。
        // 但**官方模式不是"不给 --mirror"**：不给它就会去 creeperhost（见函数里那条 ★）。
        const QString mavenRoot =
            downloadManager->mirrorPreferred()
                ? DownloadManager::mirrorMavenRootUrl()
                : (m_loaderInstall.kind == LoaderKind::NeoForge ? LoaderMeta::neoForgeMavenRoot()
                                                                : LoaderMeta::forgeMavenRoot());
        installerProcess->setArguments(forgeInstallerArguments(
            m_loaderInstall.installerPath, m_installerTargetDir, mavenRoot,
            m_installerOptions.supportsMirror));
    }
    installerProcess->setProcessEnvironment(env);
    installerProcess->setWorkingDirectory(m_installerTargetDir);
    installerProcess->setProperty("wtfmlLastOutputMs", QDateTime::currentMSecsSinceEpoch());
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
    const QByteArray standardOutput = installerProcess->readAllStandardOutput();
    const QByteArray standardError = installerProcess->readAllStandardError();
    if (!standardOutput.isEmpty() || !standardError.isEmpty()) {
        // 卡死看门狗（onInstallerTick）就问这一个时刻：有字节出来 = 它还活着。
        installerProcess->setProperty("wtfmlLastOutputMs",
                                      QDateTime::currentMSecsSinceEpoch());
    }
    *sink += standardOutput;
    *sink += standardError;
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
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - m_installerStartedAt;

    // 先看"还活不活"，再看"跑了多久"：前者是真正的卡死（死连接），后者只是慢。
    const qint64 lastOutput = installerProcess->property("wtfmlLastOutputMs").toLongLong();
    const qint64 silentMs = lastOutput > 0 ? now - lastOutput : 0;
    if (silentMs >= kInstallerStallTimeoutMs) {
        const QString last = installerLastOutputLine();
        const int restarts = installerProcess->property("wtfmlStallRestarts").toInt();
        if (restarts < 1) {
            installerProcess->setProperty("wtfmlStallRestarts", restarts + 1);
            installerProcess->setProperty("wtfmlStallRestartPending", true);
            qWarning() << "Installer produced no output for" << silentMs / 1000
                       << "seconds; restarting it once. Last line:" << last;
            emit loaderInstallMessage(
                tr("%1 的安装器已经 %2 秒没有任何输出，像是卡在一条死连接上了。"
                   "把它停掉重起一次（已经下好的库不会重下）...")
                    .arg(m_loaderInstall.loaderName)
                    .arg(silentMs / 1000));
            installerTimer->stop();
            installerProcess->kill();
        } else {
            abortInstaller(tr("%1 的安装器连续 %2 秒没有任何输出；重起一次之后还是这样，就先停掉了。\n"
                              "它最后打出来的是：%3\n"
                              "多半是网络（安装器自己那条连接）卡住了，过一会儿重试；"
                              "一直这样就把日志发来。")
                               .arg(m_loaderInstall.loaderName)
                               .arg(silentMs / 1000)
                               .arg(last));
        }
        return;
    }

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
    // 卡死看门狗杀掉的那一发**不算失败**：重起安装器，已经下好的库它自己会认。
    if (installerProcess->property("wtfmlStallRestartPending").toBool()) {
        installerProcess->setProperty("wtfmlStallRestartPending", false);
        if (installerTimer != nullptr) {
            installerTimer->stop();
        }
        onInstallerOutput();  // 被杀之前最后那点输出也读干净
        deferLoaderStep(LoaderInstall::Phase::RunningInstaller, [this] { runInstaller(); });
        return;
    }
    if (installerTimer != nullptr) {
        installerTimer->stop();
    }
    onInstallerOutput();  // 收尾前把剩下的输出读干净，失败报告才有东西可报

    const QString name = m_loaderInstall.loaderName;
    const QString version = m_loaderInstall.loaderVersion;

    // 一些旧 Forge 安装器遇到过期 Java 证书仍返回 0；只有没有成功产物时才恢复。
    // 仅更换本次安装器 JVM，不修改全局/实例游戏 Java，不关闭 TLS 校验，最多恢复一次。
    const bool badCertificate = InstallerFailure::certificateFailure(m_installerOutput);
    const bool missingProfile = !QFileInfo::exists(InstanceFiles::jsonPath(m_loaderInstall.loaderId));
    if (!crashed && badCertificate && (exitCode != 0 || missingProfile)) {
        QString replacement, reason;
        if (!installerProcess->property("wtfmlCertificateRetried").toBool() &&
            GlobalJavaSettings::installerJavaFor(17, m_installerProgram, &replacement, &reason) &&
            !replacement.isEmpty() && replacement.compare(m_installerProgram, Qt::CaseInsensitive) != 0) {
            installerProcess->setProperty("wtfmlCertificateRetried", true);
            m_loaderInstall.javaPath = replacement;
            emit loaderInstallMessage(tr("安装器使用的旧 Java 证书校验失败，改用本机较新的 Java 重试一次：%1。"
                                         "不会更改游戏使用的 Java。HTTPS 证书校验保持开启。")
                                          .arg(replacement));
            deferLoaderStep(LoaderInstall::Phase::RunningInstaller, [this] { runInstaller(); });
            return;
        }
        failLoaderInstall(tr("%1 %2 安装失败：安装器使用的 Java 无法验证 HTTPS 证书。"
                             "请更新该 Java 的证书库，或为安装器选择证书正常的较新 Java；不要关闭 HTTPS 证书校验。\n"
                             "当前安装器 Java：%3\n%4")
                              .arg(name, version, m_installerProgram, installerOutputTail(m_installerOutput)));
        return;
    }

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
// * 它的安装器**不认命令行给的目录**（参数整个丢掉，只从 APPDATA 算）→ 换子进程的
// APPDATA、让它对着一个临时目录装，装完把产物搬回真游戏目录。真游戏目录从头到尾没被
// 它碰过 —— 这比"用目录联接把假目录指过去"稳得多：那一条路要在用户的真目录上做删除，
// 错一步就是把人家的 .minecraft 删了。
// * 它**要拿原版的客户端 jar 当输入** → 所以是"先补原版、再跑安装器"（Forge/NeoForge
// 正好相反：先跑安装器、最后才补原版）。
// * 它的版本目录名**我们算不出来**（安装器按自己那份 jar 里的版本号拼的）→ 装完去盘上认。

void VersionManager::fetchOptifineVersionList() {
    // 两个源是**两份不同的数据**：镜像是一条 JSON 接口（一个游戏版本一页），官方只有那张
    // 203 KB 的下载页可读（每个游戏版本一节）。连"哪一条是正式版"的挑法都不一样，所以
    // 回复也不共用处理，各走各的（见 LoaderMeta.hpp 里那两个纯函数）。
    //
    // NOTE: 走哪条源在**发请求这里决定一次**，然后原样传进回复处理函数，那边**不再重新读**。
    // 若两边各读一次 useMirror()，这中间用户去设置页换一下「下载源」（安装卡着不动
    // 时他多半会去换），回复回来就会**按另一条源的格式去解** —— HTML 当 JSON 解，
    // 报出来是"版本表读不懂"；JSON 当下载页解，报出来是"还没有支持这个版本的正式版"
    // （而它明明有，只是我们根本没看）。两句话都在指向错误的方向。
    // 问的是 mirrorPreferred() 而不是 useMirror()：镜像被降级（太慢）那段时间里，
    // 这张表也不该再去问镜像 —— 和 Forge 安装器那条 `--mirror` 是同一个判据。
    const bool fromMirror = downloadManager->mirrorPreferred();
    const QUrl url(fromMirror ? LoaderMeta::optifineMirrorListUrl(m_loaderInstall.gameVersion)
                              : LoaderMeta::optifineDownloadsPageUrl());
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = loaderNetworkManager()->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, fromMirror] { onOptifineVersionListReply(reply, fromMirror); });
}

void VersionManager::onOptifineVersionListReply(QNetworkReply *reply, bool fromMirror) {
    reply->deleteLater();
    // 状态已经换过（上一次失败收了尾、或者用户又点了一次）：这份回复没人要了。
    if (m_loaderInstall.phase != LoaderInstall::Phase::FetchingInstallerList) {
        return;
    }

    const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
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
        pick = LoaderMeta::pickOptifineFromMirror(doc.array(), m_loaderInstall.gameVersion,
                                                  m_loaderInstall.channel);
    } else {
        pick = LoaderMeta::pickOptifineFromDownloadsPage(QString::fromUtf8(body),
                                                         m_loaderInstall.gameVersion,
                                                         m_loaderInstall.channel);
    }

    if (!pick.ok()) {
        // 和另外三条一样：这不是网络问题，也**不能**退回"装原版"。NOTE: 到了这一步就只剩
        // "**稳定档**上一条正式版都没有"这一种可能（预览版在「最新版」那一档也会列、也装，
        // 所以官方页那一节里只有预览版的情况在那一档是能装下去的）—— 那句提示得把
        // 退路指出来，不能说"预览版不装"。
        // NOTE: 这句话里**不引界面那两行的原文** —— 数据层不认识 ui/MainWindowUI.hpp
        // （那一层依赖是单向的），引一次就等于为了五个字把两层拧在一起。
        failLoaderInstall(
            tr("%1 还没有支持 %2 的正式版；去列表里挑最新那一档，那一档连预览版一起算。")
                .arg(m_loaderInstall.loaderName, m_loaderInstall.gameVersion));
        return;
    }

    // "这个版本叫什么"只有 LoaderMeta::optifineEditionFromFile() 那一处切法，官方/镜像两条路
    // 都得是同一个值（那是要写进 wtfml.meta 的东西）。
    const QString edition =
        LoaderMeta::optifineEditionFromFile(m_loaderInstall.gameVersion, pick.fileName);

    // 挑中预览版时 `stable` 那一栏是 false（见 LoaderMeta::isOptifinePreviewFile），
    // 于是这句说明会写成"稳定那一档……退回了最新的那个"—— 那正是官方页上那一节只有预览版
    // 的情况，是真话。两个源共用这一句，只有"哪一条"那半句分源（两份数据的挑法不一样）。
    emit loaderInstallMessage(
        tr("挑中 %1 %2（%3）。")
            .arg(m_loaderInstall.loaderName, edition,
                 m_loaderInstall.channel == LoaderMeta::LoaderChannel::Newest
                     ? (LoaderMeta::isOptifinePreviewFile(pick.fileName)
                            ? tr("最新那一档，挑中的是预览版")
                            : tr("最新那一档，连预览版一起算"))
                     : pickChannelDetail(m_loaderInstall.channel,
                                         !LoaderMeta::isOptifinePreviewFile(pick.fileName))));

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
 * 用户钉死的版本没有这两个字段，由 optifineMirrorPathFromEdition() 从版本号切出来 ——
 * 那个切法对着那 11 条真实镜像数据逐条核过，和直接读到的完全一致。
 * @param fromMirror 这一次走的是镜像那条路吗。**由调用方决定好传进来，这里不重新读**
 * `downloadManager->mirrorPreferred()` —— 版本表已经按某一条源取回来了，
 * "接着按哪条源走"必须和取表时是同一个答案（重现读的话，用户在两次之间换一下
 * 「下载源」，就会出现"表是按官方页取的、文件却按镜像地址拼"这种半截组合）。
 *
 * 两条路（版本表挑出来的 / 用户钉死的）共用这一段的理由：**挑法和钉死的差别只在"版本号从
 * 哪来"**，后面填文件名、拼地址、走镜像还是广告页，一个字都不该不一样。两条路都从这里
 * 进，才不会出现"钉死"那条路没有入口、选了 H7 却装成 H9 的情况。
 *
 * NOTE: 版本目录名（= loaderId，最终 finished 信号里的 id）这里**故意不填** —— 它是安装器
 * 自己拼的，我们算不出来。装完认出来再填（见 adoptOptifineProducts），在那之前要报
 * id 的地方走 reportId() 的退化值（原版号）。
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
    QNetworkReply *reply = loaderNetworkManager()->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onOptifineDownloadTokenReply(reply); });
}

void VersionManager::onOptifineDownloadTokenReply(QNetworkReply *reply) {
    reply->deleteLater();
    if (m_loaderInstall.phase != LoaderInstall::Phase::FetchingInstallerToken) {
        return;
    }

    const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
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
    // 多留一份几 MB 的假 .minecraft，不值得为这个去打扰用户。
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
    // `versions/` 底下除了原版之外多出来的**那一个**目录就是它装的。多了、少了都不猜
    // —— 猜错就是把 wtfml.meta 写到一个不存在的 id 上。
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
    // 这个形状 —— 那是它自己拼目录名的约定，对不上就说明装的不是我们要的那个原版。
    if (!LoaderMeta::isSafeVersionId(id) || !id.startsWith(prefix)) {
        failLoaderInstall(tr("%1 %2 的安装器装出来一个「%3」，不是「%4…」那个形状，"
                             "没有承认它装好了。")
                              .arg(name, version, id, prefix));
        return;
    }

    // 3. 核对那份 json。**从临时目录读**（不经过内存缓存 —— 走缓存就等于拿一份跟这次安装
    // 无关的东西去核对）。NOTE: checkInstallerResult() 的 id 那一半在这里是**退化的**
    // （id 就是我们刚从盘上认出来的那个，必然相等），真正在查的是 inheritsFrom 是不是
    // 我们选的那个原版、以及 id 能当目录名 —— 上面那两步已经把这两条查过了。
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
    // 不会多出来 —— 这正是"对着临时目录装"换来的东西。
    // NOTE: 那句话管的是**核对**。搬回**中途**失败是另一回事：先搬成的那一半会留着、
    // 不自动回滚（下面两条失败文案各自说清了留下的是什么），而两样东西都是 maven
    // 式带版本号的子目录（`libraries/optifine/OptiFine/<版本>/…`），所以留下的
    // 那半份不会影响别的 OptiFine 版本。
    //
    // 两样都要搬，而且**先搬库、后搬版本目录**：
    // * 那份 json 里的两个库既没有 `url` 也没有 `downloads`，只能从本机那份
    // `libraries/` 里满足。只搬版本目录会得到一个启动就 NoClassDefFound 的版本。
    // * 顺序是刻意的：版本目录一出现，这个版本在启动器的版本列表里就"存在"了。库先搬
    // 的话，中途失败留下的是一份谁都看不见的库；反过来留下的会是一个半成品版本。
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
    // 里记的东西必须和真实装出来的那个版本一致。挑版本时报的那个（从安装器文件名切的）
    // 正常情况下和它一样。
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
        // 到这儿）：排到下一轮，让它从干净栈上开始。
        deferNextStep([this] { startModpackFiles(); });
        return;
    }

    // 这一次加载器安装是"装一个**带名字的实例**"的第一步：加载器装完不算完，还得写实例
    // 自己那份薄 json，而终态信号报的该是**用户起的那个名字**（他等的是那个）。
    //
    // NOTE: 这一支和上面整合包那一支是**互斥**的（两条入口都会在对方 active 时拒绝开局），
    // 所以谁先谁后不影响行为；放在后面只是因为整合包那条路多一步。
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
        // 所以这不是失败，不该把它算进失败里。但也不能不说（用户会发现名字不对）。
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

    // 这一次加载器安装是**整合包导入的第一步**（不是用户单独要装加载器）：整件事到这里
    // 就是失败了，收尾只能走 failModpackImport() —— 它会把加载器那半截一起收掉，而且发的
    // 是**实例名**（用户等的是那个名字）。
    //
    // NOTE: 这一支**必须**和 finishLoaderInstall 那边一样收掉导入状态，后果不是"少一句话"
    // 而是**永久卡死**：m_modpackInstall 会一直停在 InstallingLoader 相位上，而它正是
    // "谁不能开局"的判据 —— importModpack() / downloadResourceFiles() /
    // installNamedVanilla() / installNamedLoader() 四道闸全看它。用户此后导入不了
    // 整合包、装不了实例、下不了模组，只能重启；同时这一路发出去的终态信号带的是
    // **加载器目录名**，而 MainWindow 那边只认实例名，那个实例于是永远点不动。
    // 加载器安装失败的理由很多（安装器退出码非 0、产物核对不过、版本表拉不到、
    // 找不到 Java、安装器下不下来…），任何一条都会踩到这里。
    if (m_modpackInstall.active() &&
        m_modpackInstall.phase == ModpackInstall::Phase::InstallingLoader) {
        // loaderName 必须在 failModpackImport() 之前取（它会 reset 掉加载器那份状态）。
        const QString loaderName = m_loaderInstall.loaderName;
        failModpackImport(tr("%1 没装成，整合包没有导入。%2").arg(loaderName, reason));
        return;
    }

    // 这一次加载器安装是"装一个带名字的实例"的第一步：终态信号报**用户起的那个名字**
    // （他等的是那个），而且必须把那个状态一起收掉 —— 不收的话 isNamedInstanceInstalling()
    // 永远是 true，界面就一直卡在忙态上、之后每次安装都被"已有任务正在进行"顶回来。
    //
    // NOTE: loaderName 必须在 reset() **之前**取出来（reset 会把它清掉，症状是那句话里
    // 加载器的名字变成空的 —— 同 finishLoaderInstall 开头那段）。
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

void VersionManager::clearVersionJsonCache() {
    // 换了一整份 .minecraft 之后**必须**喊这一句。
    //
    // 下面那两张表的键**只有版本 id**（就是目录名），而目录名在换文件夹之后不再是唯一的
    // —— 两份目录里各有一个 `1.20.1` 是常态（一份原版、一份装过加载器，或者两份的
    // javaVersion / assetIndex 完全不同）。换文件夹那条路若不喊这一句，这两张表就会继续
    // 拿旧的：在 A 里启动过 `1.20.1` → 切到 B → 启动 B 里的 `1.20.1`，
    // rawVersionJson() / mergedVersionJsons 命中的仍是 **A 那份**，于是 classpath、natives、
    // `javaVersion`、`assetIndex` 全按 A 算，而游戏自己去读 `B/versions/1.20.1/1.20.1.json`
    // —— 轻则 UnsupportedClassVersionError，重则两套库混着启动，而且屏幕上没有任何
    // 一句话说得通。同一条路还会污染 inheritanceProblem() 和实例列表的 channel 判定。
    //
    // NOTE: 别改成"按 (根目录, id) 做键" —— 那要在每一处读缓存的地方都带上根目录，
    // 而这里丢一次的成本只是下次读一遍那几个 json（几毫秒）。
    versionJsons.clear();
    mergedVersionJsons.clear();
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
    // 里面的 json 还叫老名字，硬拼会读不到，表现就是"实例认出来了、启动却说
    // 版本信息缺失"。认出来和还能启动是两件事。
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
    // - 访问过的 id 集合：挡环（手写的 json 可以 A 继承 B、B 继承 A）；
    // - 深度上限：挡一条长得离谱的链（光有集合挡不住它）；
    // - 父版本取不到：停在这儿，把手上这份"合了一部分"的交出去 —— 问题由
    // inheritanceProblem() 去说，这一层的职责只是尽量给出能用的东西。
    //
    // NOTE: 每一轮都是 `merge(当前结果, 上一层父版本)`，这是在**已经合过**的东西上再合
    // 一层。这样做是对的（父在前、子覆盖，一层层往外套）而且不会重复拼接 ——
    // 靠的是 merge() 把 inheritsFrom 丢掉、下一层的 id 从**父版本自己那份**里读。
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

    // 最后按"不看版本号的坐标"再去一次重（PCL 合并出来的 json 里同一个库会有两个版本）。
    //
    // NOTE: 位置在**这里**、而不是 mergeLibraries() 里 —— 有两种 json 根本不会走到 merge()：
    // 自己就是完整的那份（原版、以及 PCL 合并好之后**没有 inheritsFrom** 的那些，
    // 真机上崩得最狠的 `1.21.4-Fabric 0.16.14` 正是这一类：它顶层两个 asm 版本全在
    // 自己的数组里）。放在这条唯一的出口上，两种输入走同一条规矩。
    // 判据和证据见 VersionInheritance::dedupeLibraries() 的 NOTE。
    const QJsonArray libraries = merged.value(QStringLiteral("libraries")).toArray();
    if (!libraries.isEmpty()) {
        merged.insert(QStringLiteral("libraries"), VersionInheritance::dedupeLibraries(libraries));
    }

    // 只缓存**链正常走完**的那一份。
    //
    // NOTE: 上面两种 break（环、父版本没装）都会留下一份"合了一半"的 json。若不管走没走完
    // 都往缓存里塞，就会：启动前 MainWindow 先 getVersionJson() 拿到半份、再问
    // inheritanceProblem()，此时用户把缺的那个父版本装上 —— 同一个会话里第二次
    // getVersionJson() 直接命中半份缓存（inheritanceProblem() 是重新读盘、这时已经不抱怨了），
    // 启动器就拿着缺 assets/librarys 的半份 json 去启动，且不报任何错。
    // 环和深度超限同理：留在盘上的东西没变过，重读一次盘比用错的强。
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
    const QString invalid = InstanceFiles::validatePathSegment(versionId);
    if (!invalid.isEmpty()) { if (errorOut) *errorOut = invalid; return false; }
    const QString metaPath = AppPaths::versionMetaPath(versionId);

    if (!DirectorySessionLock::acquire(AppPaths::minecraftDir(), errorOut)) return false;
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

    if (!DirectorySessionLock::acquire(AppPaths::minecraftDir(), errorOut)) return false;
    const QString oldProblem = InstanceFiles::validatePathSegment(oldId);
    if (!oldProblem.isEmpty()) return fail(oldProblem);
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
    // 实例目录本身是 junction / 符号链接时**拒绝改名**。
    //
    // 下面这几步对链接的含义全是"穿过去":写 meta、把 `<旧名>.jar` / `<旧名>.json`
    // 改成新名字 —— 动的是**链接那一头**的文件；最后 `QDir().rename(oldDir, newDir)`
    // 又只挪链接本身（目标原地不动）。两头一对，用户看到的是"改名成功了"，而实际状态是
    // "那一头的 jar/json 被改了名、链接换了个名字指着它" —— 如果那一头是几个启动器/
    // 几个游戏文件夹共用的目录，别名启动器也会跟着坏；更常见的是用户把另一个盘上的实例
    // 挂进来，点一下改名就把那边动了。
    //
    // 和删除（`removeTreeWithoutFollowingLinks`）、复制（`copyTreeWithoutFollowingLinks`
    // 直接拒收链接）同一条规矩：**链接不追**，说不清楚就不动手。想改就去那一头改。
    {
        const QFileInfo oldDirInfo(oldDir);
        if (oldDirInfo.isJunction() || oldDirInfo.isSymLink()) {
            return fail(tr("「%1」本身是一个链接（它指向另一个目录）。\n\n"
                           "给链接改名会动到它指向的那一边，所以这里不做。"
                           "请到那个目录上直接改名，或者先把它换成普通文件夹。")
                            .arg(QDir::toNativeSeparators(oldDir)));
        }
    }
    // NOTE: 这里**不判** `QDir(newDir).exists()`。目标已经存在的情况
    // validateRenameTarget() 里按重名挡过了，而它挡不了、这里也不该挡的正是
    // "只差大小写"那一种 —— 那种目标就是它自己，判存在会把唯一合法的改名拒掉。
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

    // 只改**真在盘上**的那几个文件。`InstanceFiles::jarName()` 对"主 jar 在
    // 父版本目录里"的加载器实例返回的是**父版本**那个文件名（见它的说明），那个文件本来
    // 就不在 oldDir 里；`jsonName()` 也有"认不出来就退回 `<id>.json`"的那一档。照着一个
    // 不存在的位置去 rename，失败信息会指着一个**从来没存在过**的路径，用户照着去查只会
    // 更糊涂（而且那本来就不该算一次失败 —— 没有自己的 jar 不是错误）。
    if (QFileInfo::exists(oldJarPath)) {
        if (!renameOnto(oldJarPath, newJarPath)) {
            return rollbackAndFail(tr("无法把 %1 改名成 %2（可能有别的程序正开着它）。")
                                       .arg(QDir::toNativeSeparators(oldJarPath),
                                            QDir::toNativeSeparators(newJarPath)));
        }
        jarRenamed = true;
    }

    if (QFileInfo::exists(oldJsonPath)) {
        if (!renameOnto(oldJsonPath, newJsonPath)) {
            return rollbackAndFail(tr("无法把 %1 改名成 %2（可能有别的程序正开着它）。")
                                       .arg(QDir::toNativeSeparators(oldJsonPath),
                                            QDir::toNativeSeparators(newJsonPath)));
        }
        jsonRenamed = true;
    }

    if (!QDir().rename(oldDir, newDir)) {
        return rollbackAndFail(tr("无法把目录 %1 改名成 %2（可能有别的程序正开着它）。")
                                   .arg(QDir::toNativeSeparators(oldDir),
                                        QDir::toNativeSeparators(newDir)));
    }

    return true;
}

bool VersionManager::copyInstance(const QString &sourceId, const QString &destGameFolder,
                                  const QString &newName, QString *errorOut) {
    // 主线程上的调用方走这一份：源目录就在**当前**数据目录底下，现算没问题。
    // 后台线程的调用方必须走 copyInstanceFrom() —— 见 .hpp 里那段（AppPaths 是进程级
    // 可变状态，worker 里现算会和"切换游戏文件夹"撞成数据竞争）。
    return copyInstanceFrom(AppPaths::versionDir(sourceId.trimmed()), sourceId,
                            destGameFolder, newName, errorOut);
}

bool VersionManager::copyInstanceFrom(const QString &sourceDir, const QString &sourceId,
                                      const QString &destGameFolder, const QString &newName,
                                      QString *errorOut) {
    const auto fail = [errorOut](const QString &reason) {
        if (errorOut != nullptr) {
            *errorOut = reason;
        }
        return false;
    };

    const QString id = sourceId.trimmed();
    const QString name = newName.trimmed();
    const QString target = destGameFolder.trimmed();

    // 空的"目标游戏文件夹"必须先挡掉：`QDir(QString()).absoluteFilePath("versions")` 在
    // 当前工作目录下算，而启动器的工作目录不一定是 exe 旁边那一份 —— 那就会往一个谁也
    // 说不清的地方拷一份实例出来。这一条不是形式。
    if (target.isEmpty()) {
        return fail(tr("没有指定要复制到哪个游戏文件夹。"));
    }
    if (!WriteDirectoryGuard::acquire(sourceDir, errorOut)
        || !DirectorySessionLock::acquire(target, errorOut)) return false;

    // 源 id 必须是"一个普通的目录名"。这一条和 deleteInstance() 开头那道闸**逐字同款**，
    // 而且同样不是形式主义：id 直接拼成一条路径，空串拼出来是 `versions/` **本身**
    // （它当然"存在"），于是空 id 会把**整个 versions 目录**拷成一份副本 —— 一次点击
    // 复制出几十 GB，而且不报任何错。`..` 和带分隔符的名字同理（指到实例目录外面去）。
    if (id.isEmpty() || id == QStringLiteral(".") || id == QStringLiteral("..")
        || id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\'))) {
        return fail(tr("「%1」不是一个合法的实例名，什么都没有复制。").arg(sourceId));
    }

    // ---------- 第 1 步：全部前置检查，磁盘一个字节都不动 ----------
    //
    // 名字走的是"实例目录名"那一套（长度 / 分隔符 / 尾随点 / Windows 保留设备名），
    // 和 renameInstance() 用的是同一份 —— 目标目录名和实例目录名本来就是同一个东西。
    // NOTE: 这里的"重名"由下面那个 QFileInfo::exists() 现问盘，**不能用**
    // InstanceFiles::validateRenameTarget()：它比的是"当前数据目录的 versions/ 底下
    // 有没有同名的"，而这份副本落在**另一个**游戏文件夹里，那一边有没有人叫这个名字
    // 它看不见。sourceId 同名更是完全不冲突（复制到别的文件夹里，名字照旧是常态）。
    const QString nameProblem = InstanceFiles::validateDirName(name);
    if (!nameProblem.isEmpty()) {
        return fail(nameProblem);
    }

    if (!QDir(sourceDir).exists()) {
        return fail(tr("找不到实例目录 %1。").arg(QDir::toNativeSeparators(sourceDir)));
    }

    const QDir root(target);
    if (!root.exists()) {
        return fail(tr("目标文件夹 %1 不在盘上。").arg(QDir::toNativeSeparators(target)));
    }

    // **目标不能落在源实例目录里面**（含"就是它自己"这一档）。这一条在
    // 这儿是兜底，界面那一层（MainWindow::copyInstanceTo）有一条同款的、话术更长的。
    //
    // 它是这一整条路上唯一会**把自己写爆**的洞，而且不需要用户做任何怪事：挑游戏文件夹
    // 那一步是个普通的目录选择框，一路点进 `versions/我的整合包` 再按确定就行。那一刻
    // versionsDir = `…/versions/我的整合包/versions`、destDir 在它底下，下面两道闸都拦不住
    // （QDir 那条比的是"是不是同一个目录"、exists() 那条比的是"有没有重名"）—— 然后
    // 第 1 步末尾的 mkpath(versionsDir) 会**在源实例目录内部**造出一个 `versions/`，
    // 紧接着 copyTreeWithoutFollowingLinks 先 mkpath(目标) 再列**源**目录（那个函数的
    // 顺序就是这么写的），列出来的东西里包含它刚造出来的那个 `versions/` ⇒ 每层再往深
    // 一层、每层重抄整个实例（mods / saves / assets…），直到路径长度或盘容量撑爆为止。
    if (InstanceFiles::pathIsSameOrInside(sourceDir, target)) {
        return fail(tr("不能把「%1」复制到它自己里面去（%2 就是这一份实例自己的目录，"
                       "或者是它底下的一个子目录）。换一个游戏文件夹再试。")
                        .arg(name, QDir::toNativeSeparators(target)));
    }

    const QString versionsDir = root.absoluteFilePath(QStringLiteral("versions"));
    const QString destDir = versionsDir + QLatin1Char('/') + name;
    // QDir 比较是规范化之后比的（大小写按本机文件系统的规矩），所以"挑的就是自己这一份
    // 所在的那个游戏文件夹 + 同一个名字"这条路在这里被挡下来 —— 不然下面那个 exists()
    // 也会挡，但报的是"已经有一个叫 X 的实例了"，而真正的原因是"那就是它自己"。
    if (QDir(destDir) == QDir(sourceDir)) {
        return fail(tr("「%1」就是这一份自己，没有复制。").arg(name));
    }
    if (QFileInfo::exists(destDir)) {
        return fail(tr("那个游戏文件夹里已经有一个叫「%1」的实例了，换个名字再试。").arg(name));
    }
    if (!QDir().mkpath(versionsDir)) {
        return fail(tr("建不出目录 %1。").arg(QDir::toNativeSeparators(versionsDir)));
    }

    // ---------- 第 2 步：拷树 ----------
    //
    // 失败时**把自己刚拷出去的那半份删掉**（见 .hpp 里那条 NOTE：为什么和整合包导入那条
    // 「失败不删半成品」是相反的）。删的是 destDir 这一个我们刚建出来的目录，源一个字节
    // 都没动过。删不掉也照实说 —— 那是一句"你得自己去删掉它"，不能算复制成功。
    const auto removePartial = [&]() -> QString {
        if (!removeTreeWithoutFollowingLinks(destDir)) {
            return tr("\n（另外，那份没拷完的 %1 也没能删掉，你需要自己删掉它。）")
                .arg(QDir::toNativeSeparators(destDir));
        }
        return QString();
    };

    if (!copyTreeWithoutFollowingLinks(sourceDir, destDir)) {
        return fail(tr("复制 %1 的时候中断了，这一份没有做出来。%2")
                        .arg(QDir::toNativeSeparators(sourceDir), removePartial()));
    }

    // ---------- 第 3 步：jar / json 跟着目录名改 ----------
    //
    // 版本 json 和主 jar 的名字必须和**目录名**一致（InstanceFiles::resolve() 那三条
    // 判据里的第 ② 条）：光拷不改，那一份在列表里是认不出来的 —— 用户在目标文件夹里
    // 看到的是一个"不像实例的目录"，而启动器那边连列都不列它。
    //
    // NOTE: 用 renameOnto() 而不是 QFile::rename：只差大小写的那种"目标已存在"其实是
    // 同一个文件（Windows 的路径比较本来就不区分大小写），那种改名是合法的。
    // 这里的 renameOnto 落到的是**刚拷出来的那一份**里，不是源。
    const auto moveInCopy = [&](const QString &suffix, const QString &oldName) -> bool {
        const QString from = destDir + QLatin1Char('/') + oldName;
        const QString to = destDir + QLatin1Char('/') + name + suffix;
        if (from.compare(to, Qt::CaseSensitive) == 0 || !QFile::exists(from)) {
            // 名字一样（副本叫原来的名）或者这一份本来就没有那个文件（比如只有 json、
            // 没有 jar 的半成品）—— 都不是错误，什么都不用做。
            return true;
        }
        return renameOnto(from, to);
    };
    // **源文件名要现扫源目录**，不能拿 `id` 现拼。用户在资源管理器里
    // 把目录改过名的那种实例，jar / json 还叫旧名字（真机上就有：目录 `原版ATT2地图（无模组）`
    // 里躺着 `ATT2原版地图（无模组）.jar`）。按 `id + 后缀` 找的话 from 不存在，这一句
    // 会当"本来就没有"静默跳过 —— 副本里那份 jar 于是还叫旧名，而下面第 4 步的 meta 与
    // 整个副本都按新名成立，只有这一对名字没跟上。
    //
    // NOTE: 不能用 `InstanceFiles::jarName(id)` —— 那个算的是**当前数据目录**下的
    // `versions/<id>`，而这一份的源可能是另一个游戏文件夹（见 `copyInstanceFrom()`
    // 的 sourceDir）。这里要的是"源目录自己那三个清单"，所以只借纯函数 `resolve()`。
    InstanceFiles::DirListing sourceListing;
    sourceListing.dirName = id;
    {
        const QDir source(sourceDir);
        sourceListing.jarNames = source.entryList(QStringList() << QStringLiteral("*.jar"), QDir::Files);
        sourceListing.jsonNames = source.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    }
    const InstanceFiles::Resolved sourceNames = InstanceFiles::resolve(sourceListing);
    const QString sourceJar = sourceNames.jarName.isEmpty() ? id + QStringLiteral(".jar")
                                                            : sourceNames.jarName;
    const QString sourceJson = sourceNames.jsonName.isEmpty() ? id + QStringLiteral(".json")
                                                              : sourceNames.jsonName;
    if (!moveInCopy(QStringLiteral(".jar"), sourceJar)
        || !moveInCopy(QStringLiteral(".json"), sourceJson)) {
        return fail(tr("复制出来了，但里面的 jar / json 没能跟着改名，这一份用不了。%1")
                        .arg(removePartial()));
    }

    // ---------- 第 4 步：改这一份自己的 wtfml.meta ----------
    //
    // 两件事，缺一不可：
    // - `instanceId`（稳定 id）**必须换一个**：它是"从生到死跟着这个实例"的，两份实例
    // 共用一个 id，分组归属、图标这些认人的地方就会把副本当成源（那正是这个字段存在
    // 要挡的事）。
    // - 显示名改成新目录名（和 renameInstance() 里那句同一个写法）。
    //
    // 写在拷贝**之后**：拷过来的那份 meta 里带着源的 id 和旧名字，这里就地覆盖掉。
    // 目标在别的游戏文件夹里，所以这一句不能走 updateVersionMeta()（它算的是**当前**
    // 数据目录下的路径）—— 只有那个文件名从 AppPaths 问，其余自己拼。
    {
        const QString metaPath = destDir + QLatin1Char('/') + AppPaths::versionMetaFileName();
        bool unreadable = false;
        QJsonObject meta;
        QFile metaFile(metaPath);
        if (metaFile.exists()) {
            if (!metaFile.open(QIODevice::ReadOnly)) {
                return fail(tr("读不了 %1，这一份的目标目录名没改对。%2")
                                .arg(QDir::toNativeSeparators(metaPath), removePartial()));
            }
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &parseError);
            metaFile.close();
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                unreadable = true;
            } else {
                meta = doc.object();
            }
        }
        if (unreadable) {
            // 和 updateVersionMeta() 同一条：往一份读不懂的内容上合并 = 把用户手写的部分
            // 丢掉，宁可这一趟不写。源目录里那份一个字都没动过，用户回去还看得见它。
            return fail(tr("%1 的内容不是合法的 JSON，为避免覆盖掉它，这一份没有做出来。%2")
                            .arg(QDir::toNativeSeparators(metaPath), removePartial()));
        }
        meta.insert(kDisplayNameField, name);
        meta.insert(kInstanceIdField, newInstanceId());

        // QSaveFile + 缩进输出：和 updateVersionMeta() 逐条同款（理由写在那边：这个文件
        // 坏掉的后果是那一份实例的显示名和参数整体失效，比"这次没写成"糟得多）。
        QSaveFile out(metaPath);
        if (!out.open(QIODevice::WriteOnly)
            || out.write(QJsonDocument(meta).toJson(QJsonDocument::Indented)) < 0
            || !out.commit()) {
            return fail(tr("写不了 %1，这一份做出来了但启动器认不出它是谁。%2")
                            .arg(QDir::toNativeSeparators(metaPath), removePartial()));
        }
    }

    // 新目录要是有缓存（同名的那一份刚刚被读过），那是**另一份**的缓存。
    versionJsons.remove(name);
    mergedVersionJsons.remove(name);
    return true;
}

bool VersionManager::deleteInstance(const QString &versionId, QString *errorOut) {
    if (!DirectorySessionLock::acquire(AppPaths::minecraftDir(), errorOut)) return false;
    const QString id = versionId.trimmed();

    // 这个入参直接拼成一条**要删掉的路径**，所以校验不是形式主义：空串会拼出 versions/
    // 本身，`..` 或者带分隔符的名字会指到那一份 .minecraft 外面去。只认"一个普通的目录名"。
    if (id.isEmpty() || id == QStringLiteral(".") || id == QStringLiteral("..")
        || id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\'))) {
        if (errorOut != nullptr) {
            *errorOut = tr("「%1」不是一个合法的实例名，什么都没有删。").arg(versionId);
        }
        return false;
    }

    const QString dir = AppPaths::versionDir(id);
    if (!QFileInfo::exists(dir)) {
        // 已经不在了 —— 调用方要的结果（这个实例不在了）其实已经达成，但**不当成功**：
        // 用户点的是"删掉它"，而它不在盘上说明列表和磁盘不是同一份东西，说清楚比装作
        // 删成功了好（不然"我明明删了"和"它本来就没有"从此分不开）。
        if (errorOut != nullptr) {
            *errorOut = tr("这个实例的目录已经不在了：%1").arg(QDir::toNativeSeparators(dir));
        }
        return false;
    }

    // 走 FolderManager 那支"不追链接"的删树（理由见头文件那条 NOTE）。
    if (!removeTreeWithoutFollowingLinks(dir)) {
        if (errorOut != nullptr) {
            *errorOut = tr("没能把 %1 整个删掉（可能还有程序开着里面的文件）。\n"
                           "已经删掉的那部分不会恢复。")
                            .arg(QDir::toNativeSeparators(dir));
        }
        return false;
    }

    // 内存里那两份缓存也要跟着清。不清的后果很具体：删掉之后**重装同一个版本号**，
    // getVersionJson() 会先看缓存，于是启动用的是上一次那份 json（老的 javaVersion、
    // 老的 classpath）—— 而那一次下载明明写了新的一份在盘上。
    versionJsons.remove(id);
    mergedVersionJsons.remove(id);
    return true;
}

QString VersionManager::jvmArgsField() { return QStringLiteral("jvmArgs"); }

QString VersionManager::iconField() { return QStringLiteral("icon"); }

QString VersionManager::settingsPagesField() { return QStringLiteral("settingsPages"); }

QString VersionManager::versionIcon(const QString &versionId) const {
    QString metaPath;
    const QJsonObject meta = readVersionMeta(versionId, &metaPath);
    if (meta.isEmpty()) {
        return QString();
    }

    const QJsonValue value = meta.value(iconField());
    if (value.isUndefined() || value.isNull()) {
        // 没写过这个键 = 没挑过 = 用默认那张（和 skipJavaVersionWarning 同一套态度：
        // 缺省的那一侧是"什么都没发生"）。
        return QString();
    }
    if (!value.isString()) {
        // 写成数字/对象：按"没挑过"处理，但要说一声 —— 这是用户手写的文件，写错了
        // 不吭声就表现成"明明写了却不生效"。
        qWarning() << "Version metadata field icon is not a string in" << metaPath
                   << "— treated as \"default\"";
        return QString();
    }
    return value.toString();
}

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
    // NOTE: key 收 QString（不是 const char*）是为了让调用方能把 iconField() 这类
    // 字段名函数的结果直接递进来 —— 字面量写两遍正是那几个 Field() 存在的理由。
    const auto readString = [&meta, &metaPath](const QString &key) -> QString {
        const QJsonValue value = meta.value(key);
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

    parts.customName = readString(QStringLiteral("name"));
    parts.realVersion = readString(QStringLiteral("realVersion"));
    parts.loaderName = readString(QStringLiteral("loaderName"));
    parts.loaderVersion = readString(QStringLiteral("loaderVersion"));
    // 走 iconField() 而不是再写一遍字面量（理由同上）。
    parts.icon = readString(iconField());

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
            // 老 Forge 那条路：这一步的原版 json 是**写我们那份版本 json 的输入**（说明里
            // 那些"没有 url、但原版已经带着"的库要靠它认出来），所以接的是"写 profile"，
            // 不是"下原版的文件"。
            if (m_loaderInstall.legacySelfInstall) {
                m_loaderInstall.phase = LoaderInstall::Phase::WritingLegacyProfile;
                deferLoaderStep(LoaderInstall::Phase::WritingLegacyProfile,
                                [this, versionJson] { writeLegacyProfile(versionJson); });
                return;
            }
            // OptiFine 这一路是"**装之前**先把原版下全"（它的安装器要拿原版的客户端 jar 当
            // 输入），所以它用另一个 phase 记着：那一批结束时该起安装器，而不是收尾。
            // 另外三个都是"装完再补原版"，走 WaitingParentFiles。
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
        // 上面的加载器分支先认领了（那时 m_namedInstance.parentId 还空着，是装完才填的）。
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
    // 的情况，两者不会同时是活的。父版本是加载器的那一条也走不到这里 —— 那一条是加载器在等
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

// ---------------------------------------------------------------------------
// 批次的开与收："这一批是不是我开的"
// ---------------------------------------------------------------------------
//
// NOTE: 下载器是**共用**的：服务端安装那四段
// （`src/server/ServerInstallController.cpp`）也在同一个 `DownloadManager` 上开批次，
// 而 `allDownloadsFinished` 只有一个接收方（下面那个 `onAllDownloadsFinished`）。
// 没有"这一批是谁的"这道判据的话，一条**外来的**批次结束之后，它也会
// 往下走到最后那一句，照着 `m_currentVersionDownloading`（**上一次**下过的那个版本号，
// 收尾之后故意不清空）报一句「版本 X 的文件已全部就绪」——装服务端下完一个文件，
// 界面上会弹一次"版本下载完成"（真机上还会**挡住用户**：`InlineMessage` 是模态的）。
// `m_failedJobs` 还会是上一批留下的（只有 `beginBatch()` 清它），
// 于是那条外来的批次被报成"有文件未能下载完成"。
//
// 所以判据只能是"开批次的时候我就记住了"。四个包装把 `DownloadManager` 那四个
// 批次动作各收成一处 —— 全仓调用它们的**只有这四个函数**，别处再写一遍就漏记了。

void VersionManager::beginOwnBatch() {
    m_ownBatchOpen = true;
    downloadManager->beginBatch();
}

void VersionManager::endOwnBatch() {
    // NOTE: 这里**不**清 `m_ownBatchOpen`。`endBatch()` 通常一个信号都不发（文件还在飞，
    // `checkAllDownloadsFinished()` 会被 `pendingDownloads` 顶回来），真正那一发是在
    // 最后一个回复回来时才到的 —— 那一位由 `onAllDownloadsFinished()` 自己清。
    downloadManager->endBatch();
}

void VersionManager::abandonOwnBatch() {
    // `abandonBatch()` 把 `m_batchEmitted` 置真 ⇒ 这一批**永远不会有**完成信号了：销号。
    // 不清的话，下一批**别人的**下载结束时会认成自己的（那一批的成败、总数都跟我们无关）。
    m_ownBatchOpen = false;
    downloadManager->abandonBatch();
}

void VersionManager::cancelOwnBatch() {
    // 同上：`cancelBatch()` 也是封口 + `m_batchEmitted` 置真（见它自己那四步）。
    m_ownBatchOpen = false;
    downloadManager->cancelBatch();
}

void VersionManager::onAllDownloadsFinished(bool success) {
    // 这一批**不是我开的** ⇒ 一个字节都别管（来龙去脉见上面那一段 NOTE）。
    if (!m_ownBatchOpen) {
        return;
    }
    m_ownBatchOpen = false;

    const DownloadStats stats = downloadManager->stats();

    QString message;
    if (success) {
        if (stats.total == 0) {
            // 一个都没下 = 全都好好的（补全那颗按钮点在一个装好的实例上就是这个形状）。
            // 另有 skipped == 0 的情况：这一批压根没有作业登记上来。
            if (stats.reused > 0 && stats.skipped > 0) {
                message = tr("一个文件都不用下：%1 个已从本机其它游戏文件夹复用，%2 个本来就在盘上。")
                              .arg(stats.reused)
                              .arg(stats.skipped);
            } else if (stats.reused > 0) {
                message = tr("一个文件都不用下：%1 个文件已从本机其它游戏文件夹复用。")
                              .arg(stats.reused);
            } else {
                message = stats.skipped > 0
                              ? tr("一个文件都不用下：%1 个文件都已在盘上（已跳过）。")
                                    .arg(stats.skipped)
                              : tr("没有要下载的文件。");
            }
        } else if (stats.reused > 0 && stats.skipped > 0) {
            message = tr("已下载 %1 个文件，另有 %2 个从本机其它游戏文件夹复用、%3 个本来就存在。全部就绪。")
                          .arg(stats.total)
                          .arg(stats.reused)
                          .arg(stats.skipped);
        } else if (stats.reused > 0) {
            message = tr("已下载 %1 个文件，另有 %2 个从本机其它游戏文件夹复用。全部就绪。")
                          .arg(stats.total)
                          .arg(stats.reused);
        } else if (stats.skipped > 0) {
            message = tr("%1 个文件已全部就绪（另有 %2 个本来就存在，已跳过）。")
                          .arg(stats.total)
                          .arg(stats.skipped);
        } else {
            message = tr("%1 个文件已全部就绪。").arg(stats.total);
        }
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

    // 「失败任务保存失败文件列表和原始 URL 来源，重试只重做失败项」：在这里把那一份
    // 记下来（DownloadManager 那边刚收完尾，失败清单还是这一批的）。
    //
    // **只有"纯下文件"的批次留**：加载器安装 / 整合包导入 / 带名字的实例那三条路上，
    // 重下几个文件补不回来（加载器的安装器进程还得再跑一次、导入还得解压覆盖），
    // 露一颗点了修不好的「重试」比不露更糟。判据问的就是那三个状态机活没活着。
    if (success) {
        m_failedBatch.reset();
    } else {
        const bool plainFilesOnly = !m_loaderInstall.active() && !m_modpackInstall.active()
                                    && !m_namedInstance.active();
        m_failedBatch.reset();
        if (plainFilesOnly) {
            m_failedBatch.jobs = downloadManager->failedJobs();
            // 一个失败作业都没登记上（比如 `scheduleFile()` 那两道"地址是空的"早退）⇒
            // 没有可重做的，别露那颗按钮。
            m_failedBatch.valid = !m_failedBatch.jobs.isEmpty();
            m_failedBatch.resource = m_resourceDownload.running;
            m_failedBatch.label = m_resourceDownload.label;
            m_failedBatch.id = m_currentVersionDownloading;
        }
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
            // Fabric 那条路是"加载器自己那份 json 在内存里、库还没下"，所以这里接着下；
            // 老 Forge（legacySelfInstall）这条**也是** —— 那个年代的安装器本来会替我们把那
            // 十几个库摆好，我们没让它跑，所以一样得下（那份 json 和 universal jar 已经摆好了）。
            if (m_loaderInstall.kind == LoaderKind::Fabric || m_loaderInstall.legacySelfInstall) {
                emit loaderInstallMessage(tr("原版 %1 就绪，接着装 %2 自己的文件...")
                                              .arg(m_loaderInstall.gameVersion,
                                                   m_loaderInstall.loaderName));
                // NOTE: 不能在这里直接调 startLoaderFiles()：加载器那个批次会在**上一批的
                // 信号栈里**起来。排到下一轮，让它从干净栈上开始。
                deferLoaderStep(LoaderInstall::Phase::WaitingParentFiles,
                                [this] { startLoaderFiles(); });
                return;
            }
            // Forge / NeoForge / OptiFine：加载器自己那份 json 和库**安装器已经放好了**
            // （OptiFine 那两份是我们从临时目录里搬回来的），这一批只是把原版补齐，补完就
            // 结束了 —— 没有"加载器自己那几个库"要再下一遍。
            // （老 Forge 那条自己装的路走不到这儿，上面那个 legacySelfInstall 已经把它接走了。）
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
    // InstallingLoader，那一段由上面的加载器分支收尾、再由 finishLoaderInstall()
    // 接着往下走。这里管的是另外两段：纯原版包的原版批次、清单里那批文件。
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
                // 失败那句话得说清它在哪、以及**怎么接着下**。
                // NOTE: 不能做成「先把那个目录删掉，再导入一次」：照它做等于把几百 MB
                // 白删、还把唯一能续的点毁了。这一支失败时
                // 实例那份薄 json 已经落盘（`startModpackFiles()` 的第一步就是写它），
                // 所以 `isUnfinishedImport()` 认得出这个目录 ⇒ 同一个包、同一个名字
                // 再导入一次就走 OkResume，已经下好的按 sha1 跳过。
                failModpackImport(
                    tr("整合包里有 %1 个文件没下下来，没有装完。\n%2\n\n已经下好的那些留在 %3 "
                       "里了 —— 拿同一个整合包、同一个实例名再导入一次就会接着下"
                       "（已经下好的按 sha1 跳过）。")
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

    // 下载页那几类资源（模组 / 数据包 / 资源包 / 光影）的那一批：它不属于上面任何一套安装流程，
    // 自己报一次就完事。**必须排在最后那一句之前 return** —— 不然一批模组会被报成
    // 「版本 <那个实例> 的文件已全部就绪」，界面上看是"下完一个模组，实例的安装也完成了"。
    if (m_resourceDownload.running) {
        const QString label = m_resourceDownload.label;
        m_resourceDownload.reset();
        emit resourceDownloadFinished(label, success, message);
        return;
    }

    // 一批只发一次（每个文件的 downloadFinished 都触发的话，
    // 下载一个版本会在日志里刷出几千行一模一样的话）。
    emit versionDownloadFinished(m_currentVersionDownloading, success, message);
}

// ---------------------------------------------------------------------------
// 导出整合包（要联网 / 要读盘的那几件；清单和 zip 的规则都在 ModpackExport 那一层）
// ---------------------------------------------------------------------------

namespace {

/// 加载器 → 整合包清单里那一行的键（`ModpackImport` 那套词）。认不出（OptiFine）回空串。
///
/// NOTE: 写成逐条列出的 switch 而不是一张查表，是为了**将来多一个加载器时编译器会替你
/// 盯一眼**（loaderKindForDependencyKey 那一条也是这个理由，方向正好相反）。
QString dependencyKeyForLoaderKind(VersionManager::LoaderKind kind) {
    switch (kind) {
    case VersionManager::LoaderKind::Fabric:
        return ModpackImport::fabricLoaderKey();
    case VersionManager::LoaderKind::Forge:
        return ModpackImport::forgeLoaderKey();
    case VersionManager::LoaderKind::NeoForge:
        return ModpackImport::neoforgeLoaderKey();
    case VersionManager::LoaderKind::OptiFine:
        // 两种清单格式里都没有它那一格。**这一档由调用方在进这个函数之前就拦掉了**
        // （见 optifineExportProblem），这里回空串只是"没有可以写进清单的键"这个事实。
        return QString();
    }
    return QString();
}

/// 加载器版本号长得像不像一个版本号：**点分数字**（`47.4.10` / `10.13.4.1614`）。
///
/// 判据抄的是 LoaderMeta::listForgeVersions 里那条"只收点分数字的版本号"：Forge 自己的
/// maven 上带字母后缀的那几个（`10.13.3.1401-1710ls`）连**排序**都没有定义，我们那份列表
/// 本来就不会把它们交出来，所以"点分数字"对真值是充分的。
static bool looksLikeLoaderVersion(const QString &text) {
    if (text.isEmpty()) {
        return false;
    }
    bool sawDigit = false;
    for (const QChar ch : text) {
        if (ch.isDigit()) {
            sawDigit = true;
        } else if (ch != QLatin1Char('.')) {
            return false;
        }
    }
    return sawDigit;
}

/// 一个版本 id 是不是某个**我们装得了的**加载器装出来的，是的话把键和版本号拆出来。
///
/// 三种拼法都有唯一出处（LoaderMeta 里那三个函数），这里只是把它们**反过来读**：
/// `fabric-loader-0.16.14-1.20.1` / `1.20.1-forge-47.4.10` / `neoforge-21.1.215`
/// 外加 Quilt 的 `quilt-loader-0.20.0-1.20.1`（我们装不了它，但**认不出来就等于把它当
/// 纯原版导**，而那种包用户装回来会发现模组都在、游戏起不来，见 .hpp 里那段）。
///
/// NOTE: Fabric / Quilt 那两条**从左边切第一刀**（加载器版本到第一个 `-` 为止，剩下的
/// 都算游戏版本）：游戏版本里真能带 `-`（`1.21-pre1`），加载器版本不会。反过来切的话
/// `0.16.14` 会被拆成 `0.16.14` 和空。
///
/// @param knownVersions 在线清单里那些版本（没取到就是空表）。**只有 Forge 那一条用得上**：
/// 它的分隔符是一个**不带头锚点**的子串（见下面那段 NOTE），光看长相不足以判断
/// 左边那半是不是游戏版本。
bool loaderIdParts(const QString &id, const QList<MinecraftVersion> &knownVersions,
                   QString *keyOut, QString *versionOut) {
    const auto fill = [keyOut, versionOut](const QString &key, const QString &version) {
        if (keyOut != nullptr) {
            *keyOut = key;
        }
        if (versionOut != nullptr) {
            *versionOut = version;
        }
        return true;
    };
    // 两条 `<加载器>-loader-<加载器版本>-<游戏版本>` 形状的路：只有键不一样。
    const auto loaderLoaderForm = [&](const QString &prefix, const QString &key) {
        if (!id.startsWith(prefix)) {
            return false;
        }
        const QString rest = id.mid(prefix.size());
        const int dash = rest.indexOf(QLatin1Char('-'));
        if (dash <= 0) {
            return false;
        }
        return fill(key, rest.left(dash));
    };
    static const QString kFabric = QStringLiteral("fabric-loader-");
    static const QString kQuilt = QStringLiteral("quilt-loader-");
    if (loaderLoaderForm(kFabric, ModpackImport::fabricLoaderKey())) {
        return true;
    }
    if (loaderLoaderForm(kQuilt, ModpackImport::quiltLoaderKey())) {
        return true;
    }
    // Forge 这条和上面三条**不一样**：它的分隔符 `-forge-` 是个**不带头锚点**
    // 的子串（`<游戏版本>-forge-<加载器版本>` 只能从中间认），而这一条路要读的 id 里
    // 混着**用户自己起的名字**（PCL 那类合并实例的 `id` 就是目录名，见 instanceGameVersion
    // 里那段），于是「1.20.1-forge-版」「26.2hyperland-forge-47.4.10」这种名字会被当成
    // 一个真的 Forge 实例：清单里写上一个不存在的 Forge 版本号，收包的人那边整包装不上，
    // 而包看上去是好的（正是 .hpp 里那段最怕的失败形态）。
    // 两道判据一起上，左边那半走**全项目同一套** isKnownGameVersion（清单里有它才算数，
    // 清单没取到时它自己退回"以数字开头 + 没有空白"的长相判据），右边那半必须是点分数字。
    static const QString kForge = QStringLiteral("-forge-");
    const int forge = id.indexOf(kForge);
    if (forge > 0 && forge + kForge.size() < id.size()) {
        const QString gamePart = id.left(forge);
        const QString loaderPart = id.mid(forge + kForge.size());
        if (VersionManager::isKnownGameVersion(gamePart, knownVersions) &&
            looksLikeLoaderVersion(loaderPart)) {
            return fill(ModpackImport::forgeLoaderKey(), loaderPart);
        }
        return false;
    }
    static const QString kNeoForge = QStringLiteral("neoforge-");
    if (id.startsWith(kNeoForge) && id.size() > kNeoForge.size()) {
        return fill(ModpackImport::neoforgeLoaderKey(), id.mid(kNeoForge.size()));
    }
    return false;
}

/// OptiFine 那句"认得出来、写不下去"的说明。
///
/// NOTE: 它是**故意不导出**的，不是没做：两种规范里都没有 OptiFine 那一格（Modrinth 的
/// 依赖表只认 minecraft / forge / neoforge / fabric-loader / quilt-loader），照原版
/// 导出去的话用户装回来的包看着是好的、进去是一堆没有光影的模组。
QString optifineExportProblem(const QString &version) {
    const QString shown =
        version.trimmed().isEmpty() ? QString() : QStringLiteral(" %1").arg(version.trimmed());
    return QObject::tr(
               "这个实例是 OptiFine%1 装的。两种整合包格式的清单里都没有 OptiFine 那一格"
               "（依赖表只认 minecraft / forge / neoforge / fabric-loader / quilt-loader），"
               "写不出一个别的启动器能照着重装的包 —— 所以没有导出。")
        .arg(shown);
}

/// 认出来的加载器键能不能写进清单。空串 = 能写（**包括"压根没有加载器"这一档**，
/// 那是正常的：纯原版实例导出来就是纯原版包）。
///
/// NOTE: 传进来的键**不会是空串以外的 OptiFine**（那一档在进这里之前就拦了）。
QString loaderKeyExportProblem(const QString &key, const QString &version) {
    if (key.isEmpty() || key == ModpackImport::fabricLoaderKey() ||
        key == ModpackImport::forgeLoaderKey() || key == ModpackImport::neoforgeLoaderKey()) {
        return QString();
    }
    const QString shown =
        version.trimmed().isEmpty() ? QString() : QStringLiteral(" %1").arg(version.trimmed());
    if (key == ModpackImport::quiltLoaderKey()) {
        return QObject::tr(
                   "这个实例是 Quilt%1 装的。清单格式倒写得下 Quilt（Modrinth 的依赖表里有 "
                   "quilt-loader 这一项），但本启动器装不了 Quilt —— 导出来的包我们自己也装"
                   "不回来，所以没有导出。")
            .arg(shown);
    }
    return QObject::tr("这个实例的加载器是「%1」（%2），本启动器不认识它，不敢往外导。")
        .arg(key, shown.trimmed().isEmpty() ? QObject::tr("没有版本号") : shown.trimmed());
}

/// Modrinth 那一发没成时那句话。
QString modrinthLookupFailureText(const HttpClient::Reply &reply) {
    if (!reply.networkError.isEmpty()) {
        return QObject::tr("问 Modrinth 要文件信息时没连上：%1").arg(reply.networkError);
    }
    if (reply.status == HttpClient::kStatusTooManyRequests) {
        // 限流那一句的唯一出处（见 HttpClient::throttledText）。
        return HttpClient::throttledText(QObject::tr("问 Modrinth 要文件信息"));
    }
    return QObject::tr("Modrinth 的接口回了 HTTP %1。").arg(reply.status);
}

/// 一发问多少条。两条路都定 100：正文小、重来代价小，而一个大包（几百条）也就三四发。
constexpr int kLookupChunk = 100;

/// 长得像不像一个原版版本号。
///
/// 两条判据：**以数字开头**（Mojang 的版本号全都以数字开头 —— `1.20.1` / `24w14a` /
/// `26.2-snapshot-1`）＋ **一个空白都没有**。
///
/// 第二条是给 PCL 那种"目录名就是用户起的名字"的实例兜底的：`1.20.2-Fabric 0.17.2`
/// 以数字开头，可它**不是一个游戏版本** —— 写进清单之后收包的人那边会去找一个没有的版本，
/// 而包看上去是好的（这就是为什么不能只看第一个字符）。
static bool looksLikeGameVersion(const QString &text) {
    if (text.isEmpty() || !text.at(0).isDigit()) {
        return false;
    }
    for (const QChar ch : text) {
        if (ch.isSpace()) {
            return false;
        }
    }
    return true;
}

/// 从 maven 坐标那一格（`<原版>-<加载器版本>`）里切出加载器版本。
///
/// NOTE: 切的是**第一个**横杠，不是最后一个：`1.12.2-14.23.5.2860` 要 `14.23.5.2860`，
/// 而 GTNH 那种 `1.7.10-10.13.4.1614-1.7.10` 后面还挂着一个游戏版本 —— 按最后一个
/// 横杠切会切出 `1.7.10`，正好是反的。没有横杠时整串就是版本号。
static QString loaderVersionFromMaven(const QString &mavenVersion) {
    const QString text = mavenVersion.trimmed();
    const int dash = text.indexOf(QLatin1Char('-'));
    return dash > 0 ? text.mid(dash + 1) : text;
}

/// `loaderEvidenceFromJson()` 认出 OptiFine 时往 `keyOut` 里填的那一格。
///
/// 它**不是**整合包依赖表里的键（Modrinth / CurseForge 都不认 OptiFine，所以
/// `ModpackImport` 那边没有 `optifineLoaderKey()`，也不该有 —— 清单里根本没这一格）。
/// 它唯一的作用是给"认出来了、是哪一个"一个统一的出口，读它的是
/// `loaderDisplayNameForEvidenceKey()` 和 `instanceModLoader()`。
static QString optifineEvidenceKey() { return QStringLiteral("optifine"); }

/// 上述那一串证据键 → **给界面看的加载器名**。
///
/// 前四条走 `loaderDisplayName()`（**必须逐字一致**：实例行前面那个标志就是拿这个名字去
/// 反查下标的，见 `defaultInstanceMark()`）；Quilt 是唯一一处例外 —— 我们装不出 Quilt
/// （见 `instanceLoaderInfo()` 那段），`loaderDisplayName()` 里没有它，可"有没有模组
/// 加载器"这个问题上它和别人一样算数，所以这里单独拼一个名字。它只喂两件事：实例设置里
/// 「模组」那一页开不开、以及实例行画什么（认不出来的名字一律画草方块）。
///
/// NOTE: 写成一张**逐条列出**的表而不是遍历比较（同 `loaderKindForDependencyKey()` 那条
/// 的规矩）：键那一边的唯一出处是 `ModpackImport` 那几个函数，漏一个的话症状是
/// "这个实例明明有加载器、界面上就是不认"，所以宁可让这一张表短到一眼看得完。
static QString loaderDisplayNameForEvidenceKey(const QString &key) {
    using Kind = VersionManager::LoaderKind;
    if (key == ModpackImport::fabricLoaderKey()) {
        return VersionManager::loaderDisplayName(Kind::Fabric);
    }
    if (key == ModpackImport::forgeLoaderKey()) {
        return VersionManager::loaderDisplayName(Kind::Forge);
    }
    if (key == ModpackImport::neoforgeLoaderKey()) {
        return VersionManager::loaderDisplayName(Kind::NeoForge);
    }
    if (key == optifineEvidenceKey()) {
        return VersionManager::loaderDisplayName(Kind::OptiFine);
    }
    if (key == ModpackImport::quiltLoaderKey()) {
        return QStringLiteral("Quilt");
    }
    return QString();
}

/**
 * @brief 从一份**合并过的**版本 json 自己身上认加载器。
 *
 * 【这条路为什么必须有】PCL / HMCL 那一类启动器把整份 json 合进实例目录（真机上绝大多数
 * 实例都是这样）：它们的 `inheritsFrom` 是空的，`id` 就是用户给实例起的名字（`海波篮子`、
 * `1.21.11-Fabric 0.19.3`），于是按 id 认的那条路一步都走不出去 —— 走不出去的结果是
 * 「纯原版」，于是一个装着 133 个模组的 Fabric 实例会被导成一个**原版包**：收包的人那边
 * 模组都在、游戏起不来，而包看上去是好的。
 *
 * 【`mainClass` 不能当判据】`cpw.mods.bootstraplauncher.BootstrapLauncher` 是 Forge 与
 * NeoForge **共用**的，而 `net.minecraft.launchwrapper.Launch` 连纯原版 1.12.2 都是它。
 *
 * @param json 一份**没合并过**的版本 json（`rawVersionJson` 读回来的那份）。
 * @return 这份 json 有没有说出加载器（含"是 OptiFine，导不了"那一档 —— 那也算说出来了）。
 * 说出来时 `problemOut` 是"能不能导出"那一句：**空 = 能**。
 *
 * NOTE: 说到加载器时 `keyOut` / `versionOut` **一定会填**（含 OptiFine / Quilt 那两档，
 * 它们的 `problemOut` 非空 —— 也就是"说得出来、但导不了"）。导出那一路只在
 * `problemOut` 为空时读这两个出参，多填一份不影响它；而 `instanceModLoader()`
 * 两个都要（它问的是"是哪一个、哪一版"，不是"导不导得出去"）。
 */
bool loaderEvidenceFromJson(const QJsonObject &json, QString *keyOut, QString *versionOut,
                            QString *problemOut) {
    *problemOut = QString();

    QString fabricVersion;
    QString legacyForgeVersion;  ///< net.minecraftforge:forge:<原版>-<版本>（1.12 及以前）
    QString modernForgeVersion;  ///< net.minecraftforge:fmlloader:<原版>-<版本>（1.13 以后）
    QString optifineVersion;
    QString quiltVersion;
    bool neoForgeSeen = false;
    for (const QJsonValue &entry : json.value(QStringLiteral("libraries")).toArray()) {
        const QStringList parts =
            entry.toObject().value(QStringLiteral("name")).toString().split(QLatin1Char(':'));
        if (parts.size() < 3) {
            continue;
        }
        const QString group = parts.at(0);
        const QString artifact = parts.at(1);
        const QString version = parts.at(2);
        if (group == QLatin1String("net.fabricmc") && artifact == QLatin1String("fabric-loader")) {
            fabricVersion = version;
        } else if (group == QLatin1String("net.minecraftforge") &&
                   artifact == QLatin1String("forge")) {
            legacyForgeVersion = version;
        } else if (group == QLatin1String("net.minecraftforge") &&
                   artifact == QLatin1String("fmlloader")) {
            modernForgeVersion = version;
        } else if (group == QLatin1String("net.neoforged.fancymodloader") &&
                   artifact == QLatin1String("loader")) {
            neoForgeSeen = true;
        } else if (group == QLatin1String("optifine") && artifact == QLatin1String("OptiFine")) {
            optifineVersion = version;
        } else if (group == QLatin1String("org.quiltmc") &&
                   artifact == QLatin1String("quilt-loader")) {
            quiltVersion = version;
        }
    }

    // OptiFine 排在最前面：装了加载器**又**装了 OptiFine 的实例两种都会命中，而按规范它
    // 是"导不了"的那一档（id 那条路也是先看 OptiFine 再看别的加载器，顺序保持一致）。
    if (!optifineVersion.isEmpty()) {
        *problemOut = optifineExportProblem(optifineVersion);
        // 这两个出参照填（见函数头那条 NOTE）。键写成 optifineEvidenceKey()，理由在那一处。
        if (keyOut != nullptr) {
            *keyOut = optifineEvidenceKey();
        }
        if (versionOut != nullptr) {
            *versionOut = optifineVersion;
        }
        return true;
    }
    if (!quiltVersion.isEmpty()) {
        // 那句"清单写得下、我们装不了"的说明只有一份（loaderKeyExportProblem 里）。
        *problemOut = loaderKeyExportProblem(ModpackImport::quiltLoaderKey(), quiltVersion);
        if (keyOut != nullptr) {
            *keyOut = ModpackImport::quiltLoaderKey();
        }
        if (versionOut != nullptr) {
            *versionOut = quiltVersion;
        }
        return true;
    }

    QStringList declared;
    const QJsonValue arguments = json.value(QStringLiteral("arguments"));
    if (arguments.isObject()) {
        declared = VersionArguments::declaredArguments(
            arguments.toObject().value(QStringLiteral("game")).toArray());
    }
    const auto declaredValue = [&declared](const QString &flag) {
        const int at = declared.indexOf(flag);
        return (at >= 0 && at + 1 < declared.size()) ? declared.at(at + 1) : QString();
    };

    const auto fill = [keyOut, versionOut](const QString &key, const QString &version) {
        *keyOut = key;
        *versionOut = version;
        return true;
    };

    if (!fabricVersion.isEmpty()) {
        return fill(ModpackImport::fabricLoaderKey(), fabricVersion);
    }

    if (neoForgeSeen) {
        const QString neoForgeVersion = declaredValue(QStringLiteral("--fml.neoForgeVersion"));
        if (neoForgeVersion.isEmpty()) {
            // 宁可明说导不了，也不要拿 FML 那个 4.x 顶上去：清单里那一格是"别人照着装的
            // 版本号"，写错的话收包的人装出来的是另一个 NeoForge。
            *problemOut = QObject::tr(
                "这个实例是 NeoForge 装的，但读不出是哪一版 —— 这份 json 里没有 "
                "--fml.neoForgeVersion，而 fancymodloader 那个版本号（4.x）和 neoforge-<版本> "
                "不是同一个号，填进清单就是错的。");
            return true;
        }
        return fill(ModpackImport::neoforgeLoaderKey(), neoForgeVersion);
    }

    const QString forgeFromMaven =
        loaderVersionFromMaven(modernForgeVersion.isEmpty() ? legacyForgeVersion
                                                           : modernForgeVersion);
    if (!forgeFromMaven.isEmpty()) {
        return fill(ModpackImport::forgeLoaderKey(), forgeFromMaven);
    }
    const QString forgeDeclared = declaredValue(QStringLiteral("--fml.forgeVersion"));
    if (!forgeDeclared.isEmpty()) {
        return fill(ModpackImport::forgeLoaderKey(), forgeDeclared);
    }

    return false;
}

}  // namespace

bool VersionManager::isKnownGameVersion(const QString &id,
                                        const QList<MinecraftVersion> &knownVersions) {
    const QString text = id.trimmed();
    if (text.isEmpty()) {
        return false;
    }
    // ① 清单在手（启动时取过一次，见 MainWindow::onVersionsFetched 那句日志）：**以它为准**。
    //
    // 这一条治的是两件**相反**的事，两件都在真机上量到过（只读探针
    // `mw_check instance-scan <数据目录>`， 拿用户的盘跑出来的）：
    // · **放行不以数字开头的老版本号**：`rd-132211` / `c0.30_01c` / `inf-20100618`
    // 都是清单里真有、也装得出来的版本，光看长相会把这一档整个漏掉；
    // · **拦住"长得像版本号、但 Mojang 根本没这个 id"的东西**：真机上有 `20w14∞`
    // （它的正式 id 是 `20w14infinite`）和 `26.2hyperland`（Hyperlands 那个私服客户端）
    // —— 写进清单收包的人那边没有这个版本可装，和"目录名不像版本号"是同一个病，
    // 只是长得更像、光看长相拦不住。
    //
    // 严格到这一步的代价只有"清单比实例旧"这一种情形（刚出的快照不在缓存里），而清单是
    // 启动时取的、能装出来的版本必然在里面，所以那种情形实际上要"用户手工丢进来一个
    // 比清单还新的目录"才会撞上 —— 撞上了也只是**报一句、不写假版本号**，重启就恢复。
    if (!knownVersions.isEmpty()) {
        for (const MinecraftVersion &version : knownVersions) {
            if (version.id == text) {
                return true;
            }
        }
        return false;
    }
    // ② 清单没取到（离线第一趟）：只能看长相，**这里的放宽是有意的** —— 宁可放行一个可疑的
    // 版本号，也不要"离线时一个实例都导不出去"。挡着的还是 `原版ATT2地图（无模组）` /
    // `1.20.2-Fabric 0.17.2` 这类最明显的假版本号。
    return looksLikeGameVersion(text);
}

QString VersionManager::instanceLoaderInfo(const QString &instanceId, QString *keyOut,
                                           QString *versionOut) {
    if (keyOut != nullptr) {
        keyOut->clear();
    }
    if (versionOut != nullptr) {
        versionOut->clear();
    }
    const QString start = instanceId.trimmed();
    if (start.isEmpty()) {
        return tr("没有给出实例名，不知道要导出哪一个实例。");
    }

    // 在线清单那一份（没取到就是空表），只给 loaderIdParts 里 Forge 那一条用：那条的判据
    // 要问"左边那半是不是一个真游戏版本"，出处是 isKnownGameVersion()。取法和
    // instanceGameVersion() 里那句逐字相同（它也是**现取**，不缓存 —— 清单是启动时那一份）。
    const QList<MinecraftVersion> knownVersions = downloadManager->getAvailableVersions();

    // ---- 第一条路：顺着版本 json 的继承链看那一串 id ----
    //
    // 这一条排在前面，因为它是**装在盘上的东西自己说的**：装加载器出来的目录名就是那个
    // id，导入整合包出来的薄 json 的 inheritsFrom 也指着它。
    QSet<QString> seen;
    QString id = start;
    // 链上每一份**没合并过**的 json，从实例自己那份开始。第三条路要用（见下），顺手存着
    // 免得再读一遍盘。注意：认出来了就 return 了，所以攒下来的那些一定是"还没认出来"时
    // 走过的整条链。
    QList<QJsonObject> chainJsons;
    for (int depth = 0; depth < VersionInheritance::maxDepth() && !id.isEmpty(); ++depth) {
        if (seen.contains(id)) {
            // 环（手写的 json 可以是 A→B→A）。上面几层里该认的已经认过了，往下没有信息
            // 可挖 —— 这不是错误。
            break;
        }
        seen.insert(id);

        // OptiFine 单独认一次：它那个 id 是安装器自己拼的（`1.18.2-OptiFine_HD_U_H9`），
        // 而且在两种清单里都没有对应的键，loaderIdParts 里根本没有它那一档。
        //
        // 同一个**不带头锚点**的毛病（和 loaderIdParts 里 Forge 那条一样，那
        // 边有完整来路）—— 左边那半也得是一个真游戏版本。不收这一下的话，用户把实例改名
        // 成「我的-OptiFine_版」就会被当成"OptiFine 实例"，而这个分支的出路是**拒绝导出**：
        // 一个好好的实例，因为名字里带了几个字，就一个包都导不出去，报的还是"两种清单里
        // 都没有 OptiFine 那一格"。右边那半不另判长相 —— OptiFine 的版本名
        // （`HD_U_H9`）是它自己一套写法，没有能便宜判出来的形状。
        static const QString kOptiFine = QStringLiteral("-OptiFine_");
        const int optifine = id.indexOf(kOptiFine);
        if (optifine > 0 &&
            VersionManager::isKnownGameVersion(id.left(optifine), knownVersions)) {
            return optifineExportProblem(id.mid(optifine + kOptiFine.size()));
        }

        QString key;
        QString version;
        if (loaderIdParts(id, knownVersions, &key, &version)) {
            const QString problem = loaderKeyExportProblem(key, version);
            if (problem.isEmpty()) {
                if (keyOut != nullptr) {
                    *keyOut = key;
                }
                if (versionOut != nullptr) {
                    *versionOut = version;
                }
            }
            return problem;
        }
        const QJsonObject json = rawVersionJson(id);
        chainJsons.append(json);
        id = VersionInheritance::parentIdOf(json);
    }

    // ---- 第二条路：从**那份 json 自己身上**认（libraries + arguments.game）----
    //
    // 罩的是 PCL / HMCL 那一类"整份 json 都合进实例目录"的实例 —— 真机上绝大多数都是。
    // 它们的链上一步都走不出去（`inheritsFrom` 是空的，`id` 是用户给实例起的名字），
    // 所以第一条路问不出任何东西；而它们**照样**把加载器明明白白写在 libraries 里。
    // 少了这一条，一个装着 133 个模组的 Fabric 实例会被下面那条 meta 路当成**纯原版**
    // 导出去。
    //
    // 顺序是**从实例自己那份开始**（链越靠前越贴着这个实例），一份一份往下看。
    for (const QJsonObject &json : chainJsons) {
        QString key;
        QString version;
        QString problem;
        if (!loaderEvidenceFromJson(json, &key, &version, &problem)) {
            continue;
        }
        if (problem.isEmpty()) {
            if (keyOut != nullptr) {
                *keyOut = key;
            }
            if (versionOut != nullptr) {
                *versionOut = version;
            }
        }
        return problem;
    }

    // ---- 第三条路：wtfml.meta 里我们自己记的那一段 ----
    //
    // 罩的是"链上没有、但 meta 里记着"的那一类（用户手写过一个 meta）。这张反查表是照着
    // loaderDisplayName() 拼出来的，所以不可能和写进去的那一份漂。
    const QJsonObject meta = readVersionMeta(start);
    const QString metaName = meta.value(QStringLiteral("loaderName")).toString().trimmed();
    const QString metaVersion = meta.value(QStringLiteral("loaderVersion")).toString().trimmed();
    if (metaName.isEmpty()) {
        // 链上没有任何加载器的痕迹、meta 里也没记 —— 这就是**纯原版实例**。这是正常结果
        // （不是"认不出来"）：盘上没有任何一处说它装了加载器。
        return QString();
    }
    // 大小写不敏感：这一段是给界面看的显示名，用户手写过的 meta 里写 "fabric" 也认。
    if (metaName.compare(loaderDisplayName(LoaderKind::OptiFine), Qt::CaseInsensitive) == 0) {
        return optifineExportProblem(metaVersion);
    }
    for (int index = 0; index < loaderKindCount(); ++index) {
        LoaderKind kind = LoaderKind::Fabric;
        if (!loaderKindFromIndex(index, &kind)) {
            break;
        }
        if (metaName.compare(loaderDisplayName(kind), Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QString key = dependencyKeyForLoaderKind(kind);
        if (key.isEmpty()) {
            // 上面已经把 OptiFine 那一档拦掉了，走到这里说明那张表里多了一个还没有键的
            // 加载器（新加 LoaderKind 忘了接这里）—— 如实报，别当成纯原版往下导。
            return tr("这个实例的加载器是「%1」，但它还没有对应的整合包写法，没有导出。")
                .arg(metaName);
        }
        if (keyOut != nullptr) {
            *keyOut = key;
        }
        if (versionOut != nullptr) {
            *versionOut = metaVersion;
        }
        return QString();
    }
    return tr("这个实例的 wtfml.meta 里写着加载器「%1」，本启动器不认识这个名字，不敢往外导 ——"
              "把一个装了几百个模组的实例导成纯原版包，用户装回来会发现模组都在、游戏起不来，"
              "而包看上去是好的。要导出的话，把 wtfml.meta 里的 loaderName 改成 Fabric / Forge / "
              "NeoForge 之一。")
        .arg(metaName);
}

QString VersionManager::instanceGameVersion(const QString &instanceId) {
    const QString start = instanceId.trimmed();
    if (start.isEmpty()) {
        return QString();
    }
    // 在线清单那一份（没取到就是空表）。判据里那一半"清单里真有这个 id"要用它。
    const QList<MinecraftVersion> knownVersions = downloadManager->getAvailableVersions();
    // `usable()` 在这个函数里至少被调 4 次（① ③ 的循环 ④ ⑤），而
    // `isKnownGameVersion()` 对**非空**清单是线性扫整份（900+ 条）—— 先把 id 收进一个
    // QSet，查一次是 O(1)。清单为空（离线第一趟）时照旧交给 `isKnownGameVersion()`，
    // 那一支是"看长相"的放宽判据，不能省。
    QSet<QString> knownVersionIds;
    knownVersionIds.reserve(knownVersions.size());
    for (const MinecraftVersion &version : knownVersions) {
        knownVersionIds.insert(version.id);
    }
    const auto usable = [&knownVersions, &knownVersionIds](const QString &text) {
        return knownVersions.isEmpty() ? isKnownGameVersion(text, knownVersions)
                                       : knownVersionIds.contains(text);
    };

    // ① 我们自己记的那一格。它是**最准的**：装的时候就是从清单里抄下来的，显示名用的也是它。
    const QJsonObject meta = readVersionMeta(start);
    const QString realVersion = meta.value(QStringLiteral("realVersion")).toString().trimmed();
    if (usable(realVersion)) {
        return realVersion;
    }

    // ② 顺继承链走到末端，顺手把每一份 json 存下来给 ③ 用。链上每一层的 id 都得从
    // **没合并过**的那份 json 里读（getVersionJson() 会把 inheritsFrom 吃掉，
    // 走一步就到底了，见它自己的说明）。防环 + 深度上限和别处同一对常量
    // （VersionInheritance 里那两个）。
    QSet<QString> seen;
    QString id = start;
    QString last;
    QList<QJsonObject> chainJsons;
    for (int depth = 0; depth < VersionInheritance::maxDepth() && !id.isEmpty(); ++depth) {
        if (seen.contains(id)) {
            break;
        }
        seen.insert(id);
        last = id;
        const QJsonObject json = rawVersionJson(id);
        chainJsons.append(json);
        id = VersionInheritance::parentIdOf(json);
    }

    // ③ 链上某一份 json 自己写着它基于哪个原版 —— `clientVersion` 那一格。
    //
    // 这是 PCL / HMCL 那类**合并过的**实例唯一说得出口的地方（真机上绝大多数实例都是）：
    // 它们的 `id` 就是用户给实例起的名字（`海波篮子`、`1.21.11-Fabric 0.19.3`）、
    // `inheritsFrom` 是空的，所以 ① 没有、② 走不动，而 `clientVersion` 是真值。
    // 少了这一条，这些实例一个都导不出去 —— 报出来是「导出失败：读不出 海波篮子 基于哪个原版」。
    //
    // 从**末端**往前找：链越靠末越接近原版那一份。Mojang 自己那份 json 里没有这一格，
    // 所以薄 json（我们自己的整合包导入、加载器安装）不受影响，照旧落到 ④。
    for (auto it = chainJsons.crbegin(); it != chainJsons.crend(); ++it) {
        const QString clientVersion =
            it->value(QStringLiteral("clientVersion")).toString().trimmed();
        if (usable(clientVersion)) {
            return clientVersion;
        }
    }

    // ④ 再兜一层：链的末端那个 **id**。薄 json 走的正是这里 —— 它的 inheritsFrom 最后
    // 指到原版那一份，而原版那份目录名就是版本号。
    if (usable(last)) {
        return last;
    }

    // ⑤ 最后一层：实例自己的目录名。这是几条路里最不可信的一个（真机上有
    // `原版ATT2地图（无模组）`、`1.20.2-Fabric 0.17.2` 这种），所以除了"在清单里"
    // 就只有"清单里有"或者"长得像版本号"（以数字开头 + 没有空白）才敢用 ——
    // 判据的出处是 isKnownGameVersion()。
    return usable(start) ? start : QString();
}

bool VersionManager::gameVersionSupportsDatapacks(const QString &versionId,
                                                  const QString &releaseTime) {
    // 数据包是快照 17w43a引进的。判据见 .hpp 那段说明。
    //
    // ① 盘上那份 json 自己写着什么时候发布的 ⇒ 按日期比。**离线也准**，快照和远古版
    // 全都分得对（它们的 releaseTime 就是真日期）。
    const QString stamp = releaseTime.trimmed();
    if (!stamp.isEmpty()) {
        const QDateTime released = QDateTime::fromString(stamp, Qt::ISODate);
        if (released.isValid()) {
            // 只比**日期**（`releaseTime` 带的是 `+00:00` 偏移，换算到 UTC 再取日期，
            // 免得"离那一天只差几个钟头"把结果翻过去）。卡在 2017-11-01 而不是 17w43a
            // 那一天本身：17w43a 是 10-25 发的，但它之后还有 17w44a…17w47a 一串快照，
            // 全都带数据包。留几天富裕比卡在当天稳 —— **多给一页**是可以修的，
            // 静默藏掉一页不行（见 .hpp 里那条 NOTE）。
            return released.toUTC().date() >= QDate(2017, 11, 1);
        }
    }

    // ② 没读到日期（薄 json、手写的 json）⇒ 看版本号本身。以数字开头的按「主.次」比。
    // 不是数字开头的（`a1.2.6` / `b1.7.3` / 用户起的名字）走到这儿 ⇒ 给（下面那条
    // "问不出来就给"的规矩）。
    const QString text = versionId.trimmed();
    if (text.isEmpty() || !text.at(0).isDigit()) {
        return true;
    }
    // 到第一个不是数字也不是点的字符为止：`1.20.1` → `1.20.1`、`1.13-pre1` → `1.13`、
    // `26.2-snapshot` → `26.2`。`20w14∞` 那种也是数字开头，取出来是 `20` ⇒ 判成"有"
    // （它是 2020 年的快照，本来就有 —— 取成 `20` 正好判对）。
    int end = 0;
    while (end < text.size() && (text.at(end).isDigit() || text.at(end) == QLatin1Char('.'))) {
        ++end;
    }
    const QStringList parts = text.left(end).split(QLatin1Char('.'));
    const int major = parts.value(0).toInt();
    const int minor = parts.value(1).toInt();
    return major > 1 || (major == 1 && minor >= 13);
}

bool VersionManager::instanceSupportsDatapacks(const QString &instanceId) {
    const QString start = instanceId.trimmed();
    if (start.isEmpty()) {
        return true;  // 问不出来就给（同 gameVersionSupportsDatapacks 的那条规矩）
    }
    // 合并过的那一份 json：`releaseTime` 会从父那层继承上来（VersionInheritance::merge
    // 里那一串"子非空用子、否则用父"），所以薄 json 的加载器实例照样问得出来。
    const QJsonObject merged = getVersionJson(start);
    const QString releaseTime = merged.value(QStringLiteral("releaseTime")).toString();
    // 版本号用 instanceGameVersion()：它认得合并实例的 clientVersion / 链末端 / 目录名，
    // 而且每一格都过了 isKnownGameVersion()。
    return gameVersionSupportsDatapacks(instanceGameVersion(start), releaseTime);
}

bool VersionManager::instanceModLoader(const QString &instanceId, QString *nameOut,
                                       QString *versionOut) const {
    if (nameOut != nullptr) {
        nameOut->clear();
    }
    if (versionOut != nullptr) {
        versionOut->clear();
    }
    const QString start = instanceId.trimmed();
    if (start.isEmpty()) {
        return false;
    }

    // ---- 第一条路：我们自己记在 wtfml.meta 里的那一段 ----
    //
    // 罩的是**我们自己的安装器**装出来的实例（见 updateVersionMeta 里那一处），也含用户
    // 手写过的 meta（`loaderName` 写 "fabric" 也认 —— 大小写不敏感地比一次就够，不必
    // 逐字对上 loaderDisplayName()，这里不拿它反查下标）。
    const QJsonObject meta = versionMeta(start);
    const QString metaName = meta.value(QStringLiteral("loaderName")).toString().trimmed();
    if (!metaName.isEmpty()) {
        if (nameOut != nullptr) {
            *nameOut = metaName;
        }
        if (versionOut != nullptr) {
            *versionOut = meta.value(QStringLiteral("loaderVersion")).toString().trimmed();
        }
        return true;
    }

    // ---- 第二条路：版本 json 自己身上认（`libraries` + `arguments.game`）----
    //
    // 罩的是 PCL / HMCL 那一类"整份 json 都合进实例目录"的实例 —— 用户盘上 70 个实例里
    // 67 个是这一档，它们**只有**这条路走得通（meta 里没有 loaderName，`id` 是用户起的
    // 名字、链上也问不出东西）。判据本身一点没另写，就是导出那条路用的
    // loaderEvidenceFromJson()。
    //
    // 问的是 **getVersionJson()**（合并过的那一份），不是 rawVersionJson()：薄实例
    // （`26.4-snapshot-1-Fabric` 那种，on-disk json 只有 `id` + `inheritsFrom` +
    // `type`）的加载器整个在父那一份里，读自己那份一个字都认不出来。合并那一层自带
    // 缓存，所以一次会话里每个实例只真读一次。
    //
    // NOTE: 那道 exists() 是**必须**的：rawVersionJson() 找不到文件时会 qWarning，而这个
    // 函数每次刷新实例列表都对每个实例问一遍（同 versionChannelOf() 里那条闸的理由）。
    if (QFile::exists(InstanceFiles::jsonPath(start))) {
        QString key;
        QString version;
        QString problem;  // "认得出来但导不了"那一句 —— 这一条路上它没有意义，丢掉。
        if (loaderEvidenceFromJson(getVersionJson(start), &key, &version, &problem)) {
            const QString name = loaderDisplayNameForEvidenceKey(key);
            if (!name.isEmpty()) {
                if (nameOut != nullptr) {
                    *nameOut = name;
                }
                if (versionOut != nullptr) {
                    *versionOut = version;
                }
                return true;
            }
            // 认出来了、但那张表里没有它的名字：**当成认不出来**（名字是给界面用的，说不
            // 出名字就画草方块、也拿不出"这个实例是 X"那句话）。表里少一行的症状就是这个
            // 实例在界面上退回原版，所以那边写着"逐条列出"。
            qWarning() << "Loader evidence key not in the display table:" << key << "for"
                       << start;
        }
    }

    // ---- 没有第三条路 ----
    //
    // `mods/` 里有 jar **不算**：这一格回答的是"它基于哪个加载器"，而往 mods/ 里丢一个
    // jar 不构成任何一条加载器证据（一个装了 jar 的纯原版实例仍然起不来那些 mod）。
    // "这一页对用户有没有用"是另一个问题，补在调用那一侧。
    return false;
}

QString VersionManager::lookupModrinthVersionFiles(
    const QStringList &sha1s, QHash<QString, ModpackExport::ModrinthHit> *out) {
    if (out == nullptr) {
        return tr("内部错误：没有给出放结果的地方。");
    }
    out->clear();
    if (sha1s.isEmpty()) {
        return QString();  // 空表那一发不该发出去（回复必然是空 map）
    }

    // User-Agent **必须带**：Modrinth 的接口规矩里写着要能认出是谁在问（它按这个封滥用）。
    // 和 ResourceStore 那两处是同一个串，但**不能共用一份**（一个在 download/、一个在
    // manage/，为一行字建一个头文件不划算）；改的时候记得两边一起看。
    const QList<QPair<QByteArray, QByteArray>> headers{
        {QByteArrayLiteral("User-Agent"), QByteArrayLiteral("WTFML")}};

    for (int offset = 0; offset < sha1s.size(); offset += kLookupChunk) {
        const QStringList chunk = sha1s.mid(offset, kLookupChunk);
        const QByteArray body = ModpackExport::modrinthVersionFilesRequestBody(chunk);
        if (body.isEmpty()) {
            continue;
        }
        // 根地址：验证工程的口子优先（见 setModrinthApiBaseOverride），否则官方那一份
        // （唯一出处仍是 ResourceCatalog 里那个 apiRoot()）。同一套算法在
        // `modrinthApiRoot()` 里，预设兼容检查也问那里。
        const QString root = modrinthApiRoot();
        const HttpClient::Reply reply = HttpClient::send(
            loaderNetworkManager(), QUrl(root + QStringLiteral("/version_files")),
            HttpClient::Verb::Post, body, QByteArrayLiteral("application/json"), QByteArray(),
            HttpClient::kDefaultTimeoutMs, headers);
        if (!reply.ok()) {
            // 整件事停下（调用点那边是"要不要继续"由用户决定的那一档）：一半的批次认出来、
            // 一半没认出来，交给用户的报告就成了半真半假的东西。
            return modrinthLookupFailureText(reply);
        }
        QString problem;
        const QHash<QString, ModpackExport::ModrinthHit> part =
            ModpackExport::parseModrinthVersionFiles(reply.raw, &problem);
        if (!problem.isEmpty()) {
            return problem;
        }
        for (auto it = part.constBegin(); it != part.constEnd(); ++it) {
            out->insert(it.key(), it.value());
        }
    }
    return QString();
}

QString VersionManager::lookupCurseForgeFingerprints(
    const QList<quint32> &fingerprints, QHash<quint32, ModpackExport::CurseForgeHit> *out) {
    if (out == nullptr) {
        return tr("内部错误：没有给出放结果的地方。");
    }
    out->clear();
    if (fingerprints.isEmpty()) {
        return QString();
    }

    // 和 resolveCurseForgeFiles 同一个形状：key 是用户自己填的才带（并且**只有**带了它才走
    // 官方那台），空串走那台不要 key 的社区镜像。base 自带 `/v1`（见 curseForgeApiBase）。
    const QString base =
        ModpackImport::curseForgeApiBase(m_curseForgeApiKey, m_curseForgeApiBaseOverride);
    const bool hasKey = !m_curseForgeApiKey.isEmpty();
    QList<QPair<QByteArray, QByteArray>> headers;
    if (hasKey) {
        headers.append({QByteArrayLiteral("x-api-key"), m_curseForgeApiKey.toUtf8()});
    }

    for (int offset = 0; offset < fingerprints.size(); offset += kLookupChunk) {
        const QList<quint32> chunk = fingerprints.mid(offset, kLookupChunk);
        const QByteArray body = ModpackExport::curseForgeFingerprintsRequestBody(chunk);
        if (body.isEmpty()) {
            continue;
        }
        const HttpClient::Reply reply = HttpClient::send(
            loaderNetworkManager(), QUrl(base + QStringLiteral("/fingerprints")),
            HttpClient::Verb::Post, body, QByteArrayLiteral("application/json"), QByteArray(),
            HttpClient::kDefaultTimeoutMs, headers);
        if (!reply.ok()) {
            if (!reply.networkError.isEmpty()) {
                return tr("问 CurseForge 要文件信息时没连上：%1\n\n%2")
                    .arg(reply.networkError, curseForgeKeyHint(hasKey));
            }
            if (reply.status == HttpClient::kStatusTooManyRequests) {
                return HttpClient::throttledText(tr("问 CurseForge 要文件信息"));
            }
            const QString apiError = reply.json.value(QStringLiteral("error")).toString().trimmed();
            return tr("CurseForge 的接口回了 HTTP %1%2\n\n%3")
                .arg(reply.status)
                .arg(apiError.isEmpty() ? QString() : tr("（%1）").arg(apiError), curseForgeKeyHint(hasKey));
        }
        QString problem;
        const QHash<quint32, ModpackExport::CurseForgeHit> part =
            ModpackExport::parseCurseForgeFingerprints(reply.raw, &problem);
        if (!problem.isEmpty()) {
            return problem;
        }
        for (auto it = part.constBegin(); it != part.constEnd(); ++it) {
            out->insert(it.key(), it.value());
        }
    }
    return QString();
}

ModpackExport::ExportPlan VersionManager::buildExportPlan(
    const ModpackExport::ExportRequest &request, const ModpackExport::ExportProgress &progress,
    const ModpackExport::ExportCancel &cancel) {
    // 两条反查路就地接上这一层的两个方法。走一层 std::function 是**有意的**：那一层
    // （ModpackExport）因此一个字节的网络都不碰，验证工程能喂假接口把整条路跑完。
    const ModpackExport::ModrinthQuery queryModrinth =
        [this](const QStringList &sha1s, QHash<QString, ModpackExport::ModrinthHit> *out,
               QString *problemOut) {
            const QString problem = lookupModrinthVersionFiles(sha1s, out);
            if (problemOut != nullptr) {
                *problemOut = problem;
            }
            return problem.isEmpty();
        };
    const ModpackExport::CurseForgeQuery queryCurseForge =
        [this](const QList<quint32> &fingerprints,
               QHash<quint32, ModpackExport::CurseForgeHit> *out, QString *problemOut) {
            const QString problem = lookupCurseForgeFingerprints(fingerprints, out);
            if (problemOut != nullptr) {
                *problemOut = problem;
            }
            return problem.isEmpty();
        };
    return ModpackExport::buildExportPlan(request, queryModrinth, queryCurseForge, progress, cancel);
}

QString VersionManager::writePackArchive(const ModpackExport::ExportPlan &plan,
                                         const QString &targetZip,
                                         const std::function<bool()> &cancelled) {
    if (plan.cancelled) {
        // 用户叫停：整份 plan 都作废（overrides 里可能有半截的东西），什么都不写。
        return tr("导出已经取消了。");
    }
    if (!plan.problem.isEmpty()) {
        return plan.problem;
    }
    if (targetZip.trimmed().isEmpty()) {
        return tr("没有指定导出到哪个文件。");
    }
    if (plan.instanceDir.trimmed().isEmpty()) {
        // 认不出来的那些文件只有相对路径，取不出盘上那一份 —— 硬打出来的包会缺东西，而
        // zip 本身是好的。这是调用方的编程错误，如实报（别打出一个看着像整合包的残包）。
        return tr("内部错误：这份导出计划里没有实例目录，认不出信息的那些文件取不出来。");
    }

    // 清单先写进一个临时目录 —— 打包那一步只认盘上的文件（ZipUtil::createZip 就是照
    // "东西已经在盘上"设计的）。
    QTemporaryDir staging;
    if (!staging.isValid()) {
        return tr("建不了临时目录，没法准备整合包的内容：%1").arg(staging.errorString());
    }
    const QString manifestPath =
        QDir(staging.path()).absoluteFilePath(ModpackExport::indexFileName(plan.spec.format));
    QSaveFile manifest(manifestPath);
    if (!manifest.open(QIODevice::WriteOnly)) {
        return tr("写不了整合包的清单文件 %1。").arg(QDir::toNativeSeparators(manifestPath));
    }
    // QSaveFile 的 commit() 在失败时**不会**把目标文件留在盘上（和 writeVersionJson 同一个
    // 理由：这里坏掉的后果是打出一个清单半截的包）。
    manifest.write(ModpackExport::buildIndexFile(plan.spec));
    if (!manifest.commit()) {
        return tr("写不了整合包的清单文件 %1。").arg(QDir::toNativeSeparators(manifestPath));
    }

    // 「先写 .part、成功了再改名」：用户中途关掉启动器、或者磁盘满了，盘上留下的是一个
    // `xxx.mrpack.part`，而不是一个看着像整合包、双击却打不开的文件。
    //
    // **不删**已经在那儿的 `<目标>.part` / `<目标>.bak`，换一个没人用的
    // 名字（`.part-2`、`.bak-2`…）。这两个后缀太常见了 —— 别的下载器留下的半截、用户
    // 自己手打的一份备份，都可能正好叫这个名字。若把那两行 `QFile::remove()` 加回来、
    // 把它们当"我们上一趟的残骸"直接删掉，删的就不是我们的文件（那个 `.bak` 尤其像用户
    // 自己的备份：我们自己的 `.bak` 只在成功换名之后存在几毫秒，崩溃留下的那种又只会在
    // 目标已经不存在时才出现，而那种情况这一趟根本不会走到这儿）。
    const auto freshSidecar = [](const QString &base, const QString &suffix) {
        QString candidate = base + suffix;
        for (int i = 2; QFile::exists(candidate) && i < 1000; ++i) {
            candidate = base + suffix + QLatin1Char('-') + QString::number(i);
        }
        return candidate;
    };
    const QString partPath = freshSidecar(targetZip, QStringLiteral(".part"));
    QString error;
    if (!ZipUtil::createZip(partPath, ModpackExport::archiveEntries(plan, manifestPath), &error,
                            cancelled)) {
        QFile::remove(partPath);
        if (cancelled && cancelled()) return tr("导出已经取消了。");
        return error.isEmpty() ? tr("打包失败。") : error;
    }
    // 收尾不能直接用 `QFile::rename(partPath, targetZip)`：Qt 6.10 的 QFile::rename 走的是
    // `::MoveFile`（不带 REPLACE_EXISTING，见 qfilesystemengine_win.cpp 的 renameFile()），
    // 目标已存在就是失败 —— 而"目标本来就在那儿"（用户在 QFileDialog 里选了一个已存在的
    // 包名、对话框问过"要不要替换"之后）正是最常见的收尾。
    //
    // 所以这个顺序：先把原来那份挪到 `<目标>.bak`，成了再删它；中途任何一步失败就把它放回
    // 去。整段里用户原来那份文件**要么还在原地、要么还在 .bak 上** —— 不会出现"包没换成功、
    // 原来那份也没了"。目标本来不存在时（第一次导出）就是一条干净的改名。
    const bool hadTarget = QFile::exists(targetZip);
    const QString backupPath = hadTarget ? freshSidecar(targetZip, QStringLiteral(".bak"))
                                         : QString();
    if (hadTarget) {
        if (!QFile::rename(targetZip, backupPath)) {
            // 挪不开就**一个字节都不动**：新包留在 .part 上，把它的位置说出来。
            return tr("盘上原来那份 %1 挪不开（它可能正被别的程序占着），没有替换它。\n"
                      "新打好的包在：%2")
                .arg(QDir::toNativeSeparators(targetZip),
                     QDir::toNativeSeparators(partPath));
        }
    }
    if (!QFile::rename(partPath, targetZip)) {
        if (hadTarget && !QFile::rename(backupPath, targetZip)) {
            // 放不回去是**真的坏消息**（原来那份在 .bak 上），所以这句话必须说清它还在。
            qWarning() << "Cannot restore the replaced pack archive:" << backupPath << targetZip;
            return tr("整合包打好了，但没能放到 %1，而原来那份也放不回去了 —— 它还在 %2。")
                .arg(QDir::toNativeSeparators(targetZip),
                     QDir::toNativeSeparators(backupPath));
        }
        return tr("整合包打好了，但没能放到 %1（它可能正被别的程序占着）。\n它现在在：%2")
            .arg(QDir::toNativeSeparators(targetZip), QDir::toNativeSeparators(partPath));
    }
    if (hadTarget) {
        QFile::remove(backupPath);
    }
    return QString();
}
