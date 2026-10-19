#include "ImportController.hpp"
#include "MainWindow.hpp"
#include "InlineMessage.hpp"
#include "InstanceFilesPage.hpp"
#include "InstanceSettingsDialog.hpp"
#include "MainWindowUI.hpp"
#include "OverlayHost.hpp"
#include "download/ResourceCatalog.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/DroppedResource.hpp"
#include "manage/FolderManager.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/ModpackImport.hpp"
#include "manage/VersionManager.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "util/AppPaths.hpp"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimer>

ImportController::ImportController(MainWindow *owner) : QObject(owner), m_owner(owner) {}

namespace {
QString pageKeyForKind(ResourceCatalog::Kind kind) { return ResourceCatalog::kindKey(kind); }
}

void ImportController::onImportModpackClicked() {
    if (m_owner->m_downloadInFlight) {
        m_owner->appendLog(m_owner->tr("已有下载任务正在进行，请等它结束。"));
        return;
    }

    // NOTE: 这一处也是独立窗（Windows 自己的文件选择框）—— 理由和取舍见
    //       onAddFolderClicked() 上面那一段。
    const QString zipPath = QFileDialog::getOpenFileName(
        m_owner, m_owner->tr("选一个整合包"), QString(),
        m_owner->tr("整合包 (*.zip *.mrpack);;所有文件 (*)"));
    if (zipPath.isEmpty()) {
        return;  // 用户取消：什么都不做，也**不弹**任何提示
    }
    m_owner->importModpackFromFile(zipPath);
}

void ImportController::importModpackFromFile(const QString &zipPath, const QString &namePrefill) {
    // ★ 拖进来的这一路也得过忙态这道闸（2026-10-19）。上面那颗「导入整合包…」按钮在忙的
    //   时候是灰的（`setDownloadUiBusy()`），但主窗口的拖放照收不误（`dropEvent()` 不看
    //   忙态）。不拦的话：导入 A 的过程中再拖进来一个 B，B 会从问名字那一步一路走到下面
    //   `m_importingInstanceId = B` —— A 的 id 被顶掉（A 下完时 `versionId !=
    //   m_importingInstanceId`，连忙态都不收）、任务中心当前那条任务还是 A 的，而两条
    //   导入同时在往 versions 下面写。判据和按钮那一处**是同一句话**（`m_downloadInFlight`），
    //   不另立一个。
    if (m_owner->m_downloadInFlight) {
        m_owner->appendLog(m_owner->tr("已有下载或导入任务正在进行，%1 没有导入。")
                               .arg(QFileInfo(zipPath).fileName()));
        InlineMessage::information(m_owner->m_ui->overlay(), m_owner->tr("现在正忙"),
                                   m_owner->tr("已有下载或导入任务正在进行，等它结束之后再试。"));
        return;
    }

    // 先只看一件事：这个压缩包的根目录里有没有清单。**不整包解开** —— 用户随手选一个
    // 几百 MB 的包，为了"知道它不是整合包"先解压几分钟是白等。
    //
    // 两种格式各读一个名字（哪一侧有就是哪一种）：Modrinth 的 modrinth.index.json、
    // CurseForge 的 manifest.json。**先读 Modrinth 那个**，读不到再读 CF 那个 ——
    // 次序只影响"两个都有"这种怪包报成哪一种，无所谓。
    //
    // NOTE: 第五个参数（动作名）是**给用户看的**：下面 Failed 那一路会把 error 原样弹进那个
    //       警告框（「读这个压缩包的时候出错了：…」），所以这里必须写「读整合包清单」，
    //       而不是 ZipUtil 那个通用的默认说法。
    QByteArray manifestJson;
    QString error;
    ModpackImport::Format format = ModpackImport::Format::Unknown;
    ZipUtil::EntryStatus status = ZipUtil::readEntry(
        zipPath, ModpackImport::modrinthIndexName(), &manifestJson, &error,
        QStringLiteral("读整合包清单"));
    if (status == ZipUtil::EntryStatus::Ok) {
        format = ModpackImport::Format::Modrinth;
    } else if (status == ZipUtil::EntryStatus::NotFound) {
        // 里面没有 Modrinth 那份清单。**这一档才值得再读第二个名字**：`NotAnArchive` /
        // `Failed` 说明这个文件根本打不开或者读坏了，再读一个名字是白读（而且会把
        // "不是 zip"那句换成"认不出格式"，把话说歪）。
        status = ZipUtil::readEntry(zipPath, ModpackImport::curseForgeManifestName(),
                                    &manifestJson, &error, QStringLiteral("读整合包清单"));
        if (status == ZipUtil::EntryStatus::Ok) {
            format = ModpackImport::Format::CurseForge;
        }
    }
    if (status != ZipUtil::EntryStatus::Ok) {
        // "里面没有清单"和"这个文件根本打不开"是**两句完全不同的话**：前者多半是选错了
        // 文件，后者是文件坏了/不是 zip。
        QString text;
        if (status == ZipUtil::EntryStatus::NotAnArchive) {
            text = m_owner->tr("这个文件打不开 —— 它不是 zip 压缩包（也可能是文件坏了）。\n\n"
                      "整合包得是 zip：Modrinth 的 .mrpack 也是 zip，只是换了个后缀。");
        } else if (status == ZipUtil::EntryStatus::Failed) {
            text = m_owner->tr("读这个压缩包的时候出错了：\n%1").arg(error);
        } else {
            text = ModpackImport::unsupportedFormatText(format);
        }
        m_owner->appendLog(m_owner->tr("没有导入 %1。").arg(QFileInfo(zipPath).fileName()));
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("这不是整合包"), text);
        return;
    }

    // 解析（纯函数）。清单读不懂、不是 Minecraft 的包、路径不安全、一条文件都没有，
    // 四种情况的原因都不一样，都由它一句话说清 —— 界面不自己编第二句。
    const auto parseManifest = [](ModpackImport::Format which, const QByteArray &json) {
        return which == ModpackImport::Format::CurseForge
                   ? ModpackImport::parseCurseForgeManifest(json)
                   : ModpackImport::parseModrinthIndex(json);
    };
    ModpackImport::Result result = parseManifest(format, manifestJson);
    if (!result.ok) {
        m_owner->appendLog(m_owner->tr("整合包清单读不懂：%1").arg(result.problem));
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("导入整合包"), result.problem);
        return;
    }
    ModpackImport::Pack &pack = result.pack;

    // 加载器要不上，在**问名字之前**就拦掉：装不了的包，问完名字再说装不了是白耽误他。
    const QString loaderProblem =
        ModpackImport::unsupportedLoaderText(pack.loaderKey, pack.loaderVersion);
    if (!loaderProblem.isEmpty()) {
        m_owner->appendLog(m_owner->tr("没有导入 %1：要 %2，这个启动器装不了。")
                      .arg(QFileInfo(zipPath).fileName(), pack.loaderKey));
        InlineMessage::information(m_owner->m_ui->overlay(), m_owner->tr("这个整合包装不了"), loaderProblem);
        return;
    }

    // 名字：先推一个候选（ModpackImport::suggestInstanceName 会把建不出目录的字符换掉、
    // 收长度），用户改不改都行。校验用**数据层那一份**
    // （InstanceFiles::validateRenameTarget），界面绝不自己抄一遍规则。
    //
    // ★ 推的那个候选有两种来源（2026-09-29）：调用方给了 @p namePrefill 就用它
    //   （"拖进来的那个整合包"给的是**文件名**，见 .hpp），没给才按包名推
    //   （「导入整合包…」那颗按钮那一档）。**两路都过同一个清洗** —— 文件名里有
    //   `/` `:` 那些字符的时候，不清洗的话下一步 `checkImportTargetName` 会报
    //   "这个名字不能用"，而用户会觉得"我明明什么都没填"。
    QString name = namePrefill.isEmpty() ? ModpackImport::suggestInstanceName(pack.name)
                                         : ModpackImport::suggestInstanceName(namePrefill);
    while (true) {
        bool ok = false;
        const QString input = InlineMessage::getText(
            m_owner->m_ui->overlay(), m_owner->tr("导入整合包"),
            m_owner->tr("实例叫什么名字？这就是 versions 下面那个目录名，\n"
               "整合包「%1」的东西会装进它里面。").arg(pack.name),
            name, &ok);
        if (!ok) {
            return;
        }
        name = input.trimmed();
        // currentId 传空串 = "这是个新实例，盘上没有它"（见 checkImportTargetName）。
        QString problem;
        const InstanceFiles::ImportNameVerdict verdict =
            InstanceFiles::checkImportTargetName(name, &problem);
        if (verdict == InstanceFiles::ImportNameVerdict::Ok) {
            break;
        }
        if (verdict == InstanceFiles::ImportNameVerdict::OkResume) {
            // 撞上的是**上次没下完的那一份**（下到一半网断了那种）。这是唯一一条不逼用户
            // 改名的路：改名重导等于把已经下好的几百 MB 再下一遍。仍然要问一句 ——
            // 这一步会重新解开包里的 config / 资源包，压在用户改过的文件上。
            const auto resumeAnswer = InlineMessage::question(m_owner->m_ui->overlay(), m_owner->tr("接着上次的导入"),
                m_owner->tr("versions 下面已经有一个「%1」了，是上一次**没下完**的导入。\n\n"
                   "接着往它里面下吗？已经下好的文件会按 sha1 跳过、不会重下，"
                   "没下成的那几个会重新下。\n\n"
                   "选「否」就换个名字重新导入 —— 那样已经下好的那些要重下一遍。")
                    .arg(name), m_owner->tr("是"), m_owner->tr("否"), true);
            if (resumeAnswer) {
                break;
            }
            continue;  // 回上面重问名字
        }
        // 循环重问（和重命名那条路不同，那边是弹一次就要用户自己再点一次）：这里退回
        // 重来的代价大得多 —— 要从"选文件"重新开始，而选的是哪个包用户未必记得住。
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("换个名字"), problem);
    }

    // 这一步只有 CurseForge 需要：那一份清单里**一个下载地址都没有**，只有一串编号
    // （projectID / fileID），得先问一次接口才换得出来（判据与实测见
    // ModpackImport.hpp 顶上那段）。放在这里、而不是数据层里，是因为**用户要在点确认
    // 之前看到真实数字**：这个包要下多少个文件、其中几个作者不许第三方代下 —— 那些数
    // 只有换完才知道。其他格式这一句立刻返回，不联网。
    if (format == ModpackImport::Format::CurseForge) {
        // 同步等一两发请求（通常一两秒）。底栏那格要顶着"在等什么"，不然用户看到的是
        // 点完确认之后界面卡住（和启动路径那三段同步等是同一个手法）。
        m_owner->enterSyncWait(m_owner->tr("正在向 CurseForge 换取文件清单..."));
        const QString problem = m_owner->m_versionManager->resolveCurseForgeFiles(&pack);
        m_owner->leaveSyncWait();
        if (!problem.isEmpty()) {
            m_owner->appendLog(m_owner->tr("没有导入 %1：%2").arg(QFileInfo(zipPath).fileName(), problem));
            InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("拿不到文件清单"), problem);
            return;
        }
    }

    // 确认摘要。点下去就是几百 MB 和十几分钟，这是用户**最后**能反悔的地方，
    // 所以四件事都要说全：装的是哪个包、基于哪个版本、带什么加载器、装到哪去。
    VersionManager::LoaderKind loaderKind = VersionManager::LoaderKind::Fabric;
    const QString loaderText =
        pack.loaderKey.isEmpty()
            ? m_owner->tr("（纯原版，没有模组加载器）")
            : (VersionManager::loaderKindForDependencyKey(pack.loaderKey, &loaderKind)
                   ? m_owner->tr(" + %1 %2").arg(VersionManager::loaderDisplayName(loaderKind),
                                       pack.loaderVersion)
                   : m_owner->tr(" + %1 %2").arg(pack.loaderKey, pack.loaderVersion));
    const auto answer = InlineMessage::question(m_owner->m_ui->overlay(), m_owner->tr("开始导入"),
        m_owner->tr("整合包：%1\nMinecraft：%2%3\n要下载的文件：%4 个%5\n装到：%6\n\n开始导入吗？")
            .arg(pack.name, pack.gameVersion, loaderText)
            // 判据是 fileCount()（= 要下的 + 还没换成地址的）：CurseForge 那一档走到
            // 这里已经换过了（上面那一发），两者相等；**万一**换完一条都不剩（接口说
            // "一个都不许代下"），这里报出来的就是 0 —— 那正是用户该看到的数。
            .arg(pack.fileCount())
            .arg(pack.filesSkipped > 0
                     ? m_owner->tr("（另有 %1 个按包里的说明跳过）").arg(pack.filesSkipped)
                     : QString())
            .arg(QDir::toNativeSeparators(AppPaths::versionDir(name))), m_owner->tr("是"), m_owner->tr("否"), true);
    if (!answer) {
        m_owner->appendLog(m_owner->tr("已取消导入 %1。").arg(pack.name));
        return;
    }
    // 作者不许第三方代下的那些**要在动手之前说**（换完才知道有几个，所以只能在这儿说）：
    // 点下去之后那几十个文件里会少这几条，而"少装了东西"是这个流程里最容易到进游戏才
    // 发现的。选「是」不是同意"接受少装"，只是知道了。
    if (pack.filesNotDistributable > 0) {
        InlineMessage::information(m_owner->m_ui->overlay(), m_owner->tr("有文件作者不许代下"),
            m_owner->tr("这个整合包里有 %1 个文件，作者在 CurseForge 上关掉了「允许第三方分发」，"
               "启动器不会替你下。\n\n装完之后你可能得自己去 CurseForge 上那个整合包的页面把"
               "它们下下来，手动放进 %2 里（模组放 mods/、资源包放 resourcepacks/、光影放 "
               "shaderpacks/）。收尾那句话里会再提醒一次。")
                .arg(pack.filesNotDistributable)
                .arg(QDir::toNativeSeparators(AppPaths::versionDir(name))));
    }

    m_owner->applyModpackImport(zipPath, name, manifestJson, format);
}

bool ImportController::applyModpackImport(const QString &zipPath, const QString &instanceName,
                                    const QByteArray &manifestJson,
                                    ModpackImport::Format format) {
    // 把界面上确认过的**同一串字节**再解析一遍（纯函数，一瞬）。好处是"确认框里说的
    // 那个包"和"真的去装的那个包"不可能不是同一个 —— 不必去维护一份"解析结果"的传递。
    ModpackImport::Result result =
        format == ModpackImport::Format::CurseForge
            ? ModpackImport::parseCurseForgeManifest(manifestJson)
            : ModpackImport::parseModrinthIndex(manifestJson);
    if (!result.ok) {
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("导入整合包"), result.problem);
        return false;
    }
    ModpackImport::Pack pack = result.pack;

    const QString loaderProblem =
        ModpackImport::unsupportedLoaderText(pack.loaderKey, pack.loaderVersion);
    if (!loaderProblem.isEmpty()) {
        InlineMessage::information(m_owner->m_ui->overlay(), m_owner->tr("这个整合包装不了"), loaderProblem);
        return false;
    }

    // CurseForge 那一档在这里要**再换一次**地址（上面确认框那一步换过一次）。代价是
    // 一发重复的请求，换来的是和上面那句"同一串字节再解析一遍"一模一样的东西：这一半
    // 自己手里的那份 `pack` 是完整的，不靠外面传进来的任何解析结果。数据层那边也有
    // 第二道闸（importModpack 里那句"没换过就报错"），所以这里漏了也不会装出一个空实例。
    if (format == ModpackImport::Format::CurseForge) {
        m_owner->enterSyncWait(m_owner->tr("正在向 CurseForge 换取文件清单..."));
        const QString problem = m_owner->m_versionManager->resolveCurseForgeFiles(&pack);
        m_owner->leaveSyncWait();
        if (!problem.isEmpty()) {
            InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("拿不到文件清单"), problem);
            return false;
        }
    }

    // 重名**在这里再查一遍**：上面那条路（问名字那个循环）查过一次，但两次之间隔着用户
    // 的输入，而且验证工程直接就能进这一半 —— 而数据层那边是有约定的（见
    // VersionManager::importModpack 的说明：调用方必须先查过重名，它自己不查）。
    // 判据是同一份 checkImportTargetName()，不会漂 —— 包括"接着上次没下完的那一份"
    // 这条口子：上面那个循环放行了、这一半却按重名拒掉的话，用户看到的是一句
    // 「已经有一个叫…的实例了」，而他刚才明明点了「接着下」。
    QString nameProblem;
    const InstanceFiles::ImportNameVerdict nameVerdict =
        InstanceFiles::checkImportTargetName(instanceName, &nameProblem);
    if (nameVerdict == InstanceFiles::ImportNameVerdict::Taken) {
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("换个名字"), nameProblem);
        return false;
    }
    const bool resumingImport =
        nameVerdict == InstanceFiles::ImportNameVerdict::OkResume;

    // 安装器（Forge/NeoForge）自己就是个 Java 程序（`java -jar <安装器>.jar ...`），所以
    // 要的运行环境和"这个版本要的 Java"是同一件事：版本 json 里的 javaVersion。这一段和
    // VersionManager::installLoaderVersion() 里那段是同一件事、同一个判据 —— 拿不到版本 json 就不猜主版本
    // （猜错比不猜更危险）。Fabric 和纯原版那条路一个进程都不起，用不上它。
    QString installerJava = m_owner->m_javaSettings->javaPath();
    VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
    const bool hasLoader =
        !pack.loaderKey.isEmpty() &&
        VersionManager::loaderKindForDependencyKey(pack.loaderKey, &kind);
    if (hasLoader && kind != VersionManager::LoaderKind::Fabric) {
        int requiredJavaMajor = 0;
        const QJsonObject gameJson = m_owner->m_versionManager->getVersionJson(pack.gameVersion);
        if (!gameJson.isEmpty()) {
            requiredJavaMajor = gameJson["javaVersion"].toObject()["majorVersion"].toInt();
        }
        const QString configuredJava = installerJava;
        QString javaReason;
        if (!m_owner->m_javaSettings->installerJavaFor(requiredJavaMajor, configuredJava, &installerJava,
                                             &javaReason)) {
            m_owner->appendLog(m_owner->tr("这个版本要的 Java 拿不到：%1").arg(javaReason));
            InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("Java 版本不够"), javaReason);
            return false;  // 还没进忙态，直接退出就是干净的状态
        }
        if (installerJava != configuredJava) {
            m_owner->appendLog(m_owner->tr("这个版本要 Java %1，装的时候改用 %2")
                          .arg(requiredJavaMajor)
                          .arg(installerJava));
        }
    }

    // 忙要在**点击这一刻**置上：导入的头两步（拉版本表 / 拉 profile）是异步的，中间那段
    // 时间按钮还能点的话用户会重复触发（和下载、装加载器两条路同一个理由）。
    m_owner->m_importingInstanceId = instanceName;
    m_owner->setDownloadUiBusy(true);
    m_owner->m_progressThrottle.invalidate();
    m_owner->appendLog(m_owner->tr("开始导入整合包 %1 ...").arg(QFileInfo(zipPath).fileName()));
    if (resumingImport) {
        // 用户点的是「接着下」，日志里得留下这句 —— 不然排查的人只会看到"导入了一个已经
        // 存在的实例"，而那看起来像是覆盖了别人的东西。
        m_owner->appendLog(m_owner->tr("接着「%1」上次没下完的那一份往下下（已经下好的按 sha1 跳过）。")
                      .arg(instanceName));
    }
    m_owner->m_versionManager->importModpack(zipPath, instanceName, pack, installerJava);
    return true;
}

bool ImportController::installLocalResourceFile(const QString &versionId,
                                          ResourceCatalog::Kind kind, const QString &path,
                                          QString *problemOut) {
    if (problemOut != nullptr) {
        problemOut->clear();
    }
    // 目录名的唯一出处是 ResourceCatalog 那张表（mods / datapacks / resourcepacks /
    // shaderpacks）。整合包那一路回空串 —— 它不走这一条（要整包解开），调用方已经在
    // 更早一步把它分出去了。
    const QString folder = ResourceCatalog::folderForKind(kind);
    if (folder.isEmpty()) {
        if (problemOut != nullptr) {
            *problemOut = m_owner->tr("这一类不能直接放进实例目录。");
        }
        return false;
    }
    const QString target = AppPaths::versionDir(versionId) + QLatin1Char('/') + folder +
                           QLatin1Char('/') + QFileInfo(path).fileName();

    // 同名文件：**问一句**（不静默盖）。用户拖进来的东西，谁也不知道那一份是不是
    // 他手改过的 —— 而"顺手拖一下就把改过的那份盖掉了"是这个功能最糟的失败方式。
    if (QFileInfo::exists(target)) {
        // ★ 先看一眼"拖进来的这一份**就是**这一格自己"（2026-10-10 修）：从实例自己的
        //   `mods/` 里把文件再拖回启动器是最容易中的一路，而往下走的话，答完「盖掉」就会
        //   把 target —— 也就是**源文件本身** —— 删掉，源没了，文件就这么从盘上消失。
        //   比的是盘上那个**真路径**（`canonicalFilePath()` 解开大小写 / 8.3 短名那些别名）。
        //   它不解 junction，但那种情形也不丢东西：下面写的是临时文件、最后一步才改名字换
        //   上去，读与写不会互相踩。
        const QString dropped = QFileInfo(path).canonicalFilePath();
        if (!dropped.isEmpty() && dropped == QFileInfo(target).canonicalFilePath()) {
            return true;  // 它本来就在那儿了，一个字都不用动
        }
        const bool overwrite = InlineMessage::question(
            m_owner->m_ui->overlay(), m_owner->tr("已经有一个同名的"),
            m_owner->tr("%1 里已经有一个「%2」了。\n\n用新拖进来的这一份盖掉它吗？")
                .arg(QDir::toNativeSeparators(AppPaths::versionDir(versionId) +
                                              QLatin1Char('/') + folder),
                     QFileInfo(path).fileName()),
            m_owner->tr("盖掉"), m_owner->tr("不盖"), /*defaultYes=*/false, /*destructive=*/true);
        if (!overwrite) {
            return false;
        }
    }

    const QString dir = AppPaths::versionDir(versionId) + QLatin1Char('/') + folder;
    if (!QDir().mkpath(dir)) {
        if (problemOut != nullptr) {
            *problemOut = m_owner->tr("建不出 %1。").arg(QDir::toNativeSeparators(dir));
        }
        return false;
    }
    // ★ 写盘这一步走 `QSaveFile`（2026-10-10 修）：它先写到旁边那个临时文件、`commit()`
    //   那一刻才**原子地**换上去。原来那一版是"先 `QFile::remove(target)`，再
    //   `QFile::copy(path, target)`" —— 用户明明点的是「盖掉」，可删成功、拷失败（盘满 /
    //   权限 / 杀软锁着那一下）的时候，盘上**一份都没有了**：旧的已经删掉、新的没到位，
    //   而且没有退路（比不点「盖掉」还糟）。
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly)) {
        if (problemOut != nullptr) {
            *problemOut = m_owner->tr("写不进 %1：%2")
                              .arg(QDir::toNativeSeparators(target), out.errorString());
        }
        return false;
    }
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (problemOut != nullptr) {
            *problemOut = m_owner->tr("读不了拖进来的那一份 %1。")
                              .arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    while (!in.atEnd()) {
        // 一块一块搬（1 MiB）：模组 jar 有几十上百兆，整份读进内存没有必要。
        const QByteArray chunk = in.read(1 << 20);
        if (chunk.isEmpty()) {
            if (problemOut != nullptr) {
                *problemOut = m_owner->tr("读不了拖进来的那一份 %1（%2）。")
                                  .arg(QDir::toNativeSeparators(path), in.errorString());
            }
            return false;  // 析构会把临时文件收掉，盘上原来那一份还在
        }
        if (out.write(chunk) != chunk.size()) {
            if (problemOut != nullptr) {
                *problemOut = m_owner->tr("复制不过去：%1 → %2（%3）")
                                  .arg(QDir::toNativeSeparators(path),
                                       QDir::toNativeSeparators(target), out.errorString());
            }
            return false;
        }
    }
    if (!out.commit()) {
        if (problemOut != nullptr) {
            *problemOut = m_owner->tr("复制不过去：%1 → %2（%3）")
                              .arg(QDir::toNativeSeparators(path),
                                   QDir::toNativeSeparators(target), out.errorString());
        }
        return false;
    }
    return true;
}

void ImportController::handleInstanceSettingsDrop(QWidget *page, const QString &versionId,
                                            const QStringList &paths) {
    // 先分类（一次拖放最多几个文件；每个压缩包最多读一遍条目表）。
    QList<DroppedResource::Result> results;
    results.reserve(paths.size());
    for (const QString &path : paths) {
        results.append(DroppedResource::classify(path));
    }

    // ---------- ① 整合包：**一次只处理一个** ----------
    //
    // 用户点名的那一步："让用户选择安装的文件夹及实例名…并离开实例设置（相当于点了返回）"。
    // 多个整合包一起拖进来时只认第一个 —— 装一个整合包要选文件夹、问名字、下几百 MB，
    // 一次排三个队只会让"现在轮到谁"变得说不清。剩下的在下面那一句日志里说清楚。
    for (const DroppedResource::Result &result : results) {
        if (!result.known || result.kind != ResourceCatalog::Kind::Modpack) {
            continue;
        }
        // ★ 关页**可能被挡住**（配置编辑器 / 世界设置里压着改了一半的东西，见
        //   `leaveBlockedByUnsavedEdits()`）。挡住时**不当作关掉了**：底下那一格要是被
        //   静默丢掉，用户丢的是自己手改的配置。
        auto *dialog = qobject_cast<InstanceSettingsDialog *>(page);
        if (dialog != nullptr && dialog->leaveBlockedByUnsavedEdits()) {
            InlineMessage::information(
                m_owner->m_ui->overlay(), m_owner->tr("先处理一下没保存的改动"),
                m_owner->tr("这个实例的设置里还有没保存的改动，现在不能离开这一页。\n\n"
                   "先把它处理掉（存下来或者丢掉），再拖一次。"));
            m_owner->appendLog(m_owner->tr("没有导入 %1：实例设置里还有没保存的改动。")
                          .arg(QFileInfo(result.path).fileName()));
            return;
        }
        const QString prefill = QFileInfo(result.path).completeBaseName();
        // 「离开实例设置（相当于点了返回）」用的**就是**返回箭头那一发：`m_owner->runWhenOverlayFree()`
        // 让位那一步走的是 `OverlayHost::evict()`，而页那一档的 evict 发的正是
        // `backPressed` —— `execPage()` 里那条连接把它接在 `dialog->reject()` 上（见
        // OverlayHost.cpp 那段），一字不差就是"点了一下返回箭头"。
        //
        // ★ 这里**不要**再自己补一发 `dialog->reject()`：那一发这一轮还没生效（页正卡在
        //   自己的嵌套事件循环里），补了也只是让 `backPressed` 那条连接**再跑一遍**
        //   reject —— 两次 done() 不报错，但"关页这件事有两个发起人"是白添的麻烦。
        //
        // ★ 也不能"让位那一步"省掉（直接开问）：这一层还占着，`execPrompt()` 会被
        //   `OverlayHost` 挡掉（一层只放得下一件，见它的头文件），用户看到的是"拖了半天
        //   什么也没发生"。`m_owner->runWhenOverlayFree()` 排的下一轮才真的空出来。
        m_owner->runWhenOverlayFree([this, path = result.path, prefill] {
            m_owner->importDroppedModpack(path, prefill);
        });
        if (results.size() > 1) {
            m_owner->appendLog(m_owner->tr("一次拖进来 %1 个文件，先处理整合包「%2」，其余的先放着。")
                          .arg(results.size())
                          .arg(QFileInfo(result.path).fileName()));
        }
        return;
    }

    // ---------- ② 其余几类：按类归堆，一类问一次 ----------
    //
    // 归堆是用户的体验问题：拖三个模组进来问三遍"是否继续"是骚扰，而"这一类装不装"
    // 本来就是一个问题。
    QList<QPair<ResourceCatalog::Kind, QStringList>> byKind;
    QStringList unrecognized;
    for (const DroppedResource::Result &result : results) {
        if (!result.known || ResourceCatalog::folderForKind(result.kind).isEmpty()) {
            unrecognized.append(m_owner->tr("%1（%2）")
                                    .arg(QFileInfo(result.path).fileName(),
                                         result.detail.isEmpty() ? m_owner->tr("认不出来") : result.detail));
            continue;
        }
        int slot = -1;
        for (int i = 0; i < byKind.size(); ++i) {
            if (byKind.at(i).first == result.kind) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            byKind.append(qMakePair(result.kind, QStringList{}));
            slot = byKind.size() - 1;
        }
        byKind[slot].second.append(result.path);
    }

    auto *dialog = qobject_cast<InstanceSettingsDialog *>(page);
    const QString currentPage = dialog != nullptr ? dialog->currentPageKey() : QString();
    bool installedAnything = false;
    for (const QPair<ResourceCatalog::Kind, QStringList> &entry : byKind) {
        const ResourceCatalog::Kind kind = entry.first;
        // ★ 用户点名的那一条：**正好在对应那一页就直接塞进去，不问**（"如果是模组且正好
        //   在模组设置那一页，那就直接塞进去"）。不在对应页才问那一句"是否继续"。
        if (pageKeyForKind(kind) != currentPage) {
            // 那一句话就是用户点名要的：「将在此实例中安装 资源包/...（判断出的文件类型），
            // 是否继续」。多个文件时把个数说出来 —— 一次拖三个模组，那句"安装模组"要是
            // 不报数，用户没法知道里面是三个还是一个。
            const QString what = entry.second.size() == 1
                                     ? ResourceCatalog::kindLabel(kind)
                                     : m_owner->tr("%1 个%2")
                                           .arg(entry.second.size())
                                           .arg(ResourceCatalog::kindLabel(kind));
            const bool proceed = InlineMessage::question(m_owner->m_ui->overlay(), m_owner->tr("装进这个实例"),
                                                         m_owner->tr("将在此实例中安装 %1，是否继续？")
                                                             .arg(what),
                                                         m_owner->tr("继续"), m_owner->tr("不装"), true);
            if (!proceed) {
                continue;
            }
        }
        int done = 0;
        QStringList problems;
        for (const QString &path : entry.second) {
            QString problem;
            if (m_owner->installLocalResourceFile(versionId, kind, path, &problem)) {
                ++done;
            } else if (!problem.isEmpty()) {
                problems.append(problem);
                // NOTE: `m_owner->installLocalResourceFile()` 返回 false 而 problem 为空 = 用户
                //       自己答了"不盖" —— 那不是错，不进这一张表（也不打断剩下几个文件：
                //       同一类里别的文件该照装）。
            }
        }
        if (done > 0) {
            installedAnything = true;
            m_owner->appendLog(m_owner->tr("已把 %1 个%2装进「%3」。")
                          .arg(done)
                          .arg(ResourceCatalog::kindLabel(kind),
                               m_owner->instanceDisplayName(versionId)));
        }
        if (!problems.isEmpty()) {
            InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("有几个没装进去"),
                                   problems.join(QLatin1String("\n\n")));
        }
    }

    if (!unrecognized.isEmpty()) {
        InlineMessage::warning(
            m_owner->m_ui->overlay(), m_owner->tr("拖进来的东西认不出来"),
            m_owner->tr("下面这几个不是能装进实例的东西：\n\n%1")
                .arg(unrecognized.join(QLatin1String("\n"))));
    }

    // 装完了要让那一页自己重读一遍目录 —— 不刷的话用户刚拖进去的模组不出现在
    // 「模组」那一页里，看着就像"没装进去"。
    if (installedAnything && dialog != nullptr) {
        dialog->refreshFilePages();
    }
}

void ImportController::importDroppedModpack(const QString &zipPath, const QString &namePrefill) {
    // ★ 用户点名要的第一步：「让用户选择安装的文件夹」。手法和「复制实例」那一处**逐字
    //   相同**（原生目录选择框 —— `ui/` 不碰 AppPaths，"该默认到哪儿"只有这一层知道；
    //   那一处那段 NOTE 就是这条的出处）。
    const QString folder = QFileDialog::getExistingDirectory(
        m_owner, m_owner->tr("装到哪个游戏文件夹"), AppPaths::minecraftDir(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (folder.isEmpty()) {
        m_owner->appendLog(m_owner->tr("没有导入 %1：用户取消了。").arg(QFileInfo(zipPath).fileName()));
        return;  // 用户取消
    }

    // ★ 挑的那一份**必须真的切过去**：整条导入路（`checkImportTargetName`、
    //   `applyModpackImport`、`AppPaths::versionDir()`）看的是**当前**这一份 .minecraft，
    //   不切的话整合包会装到用户没挑的那一份里 —— 而屏幕上什么异常都没有。
    //   （"不切也行"是不可能的：那要在导入路上把"装到哪一份"从头传到尾，见
    //   `AppPaths::versionDir()` 那段。）
    if (folder != m_owner->m_folderManager->currentFolder()) {
        // 不在列表里就先加进去（`setCurrentFolder()` 只认 `folders()` 里的那几份）。
        // 加不进去 = 那个路径不是个目录（也不该发生：刚从这个框里选出来的）。
        if (!m_owner->m_folderManager->addFolder(folder) || !m_owner->m_folderManager->setCurrentFolder(folder)) {
            InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("换不到那个文件夹"),
                                   m_owner->tr("没能把「%1」设成当前游戏文件夹，整合包没有导入。")
                                       .arg(QDir::toNativeSeparators(folder)));
            return;
        }
    }

    // 名字（预填文件名）+ 装载 + 下载，全是既有那一条路（见 importModpackFromFile）。
    m_owner->importModpackFromFile(zipPath, namePrefill);
}

void ImportController::handleWindowDrop(const QStringList &paths) {
    QList<DroppedResource::Result> results;
    results.reserve(paths.size());
    for (const QString &path : paths) {
        results.append(DroppedResource::classify(path));
    }

    // 整合包：和实例设置那一档走同一条（选文件夹 + 问名字）。这里不用"离开某一页"，
    // 本来就没有页开着。
    for (const DroppedResource::Result &result : results) {
        if (result.known && result.kind == ResourceCatalog::Kind::Modpack) {
            m_owner->importDroppedModpack(result.path, QFileInfo(result.path).completeBaseName());
            return;
        }
    }

    // 其余几类：**装到哪个实例**要问（这一档没有上下文可推，见文件头那段 ★）。
    // NOTE: 分类结果**复用上面那一趟**（不在这里再 classify 一遍）：读一个压缩包的条目表
    //       要起一次 PowerShell，重跑一遍就是纯浪费，而且两次的答案理论上可以不一样
    //       （文件在这中间被换了）—— 一份输入只认一次。
    QStringList installable;
    QStringList unrecognized;
    for (const DroppedResource::Result &result : results) {
        if (!result.known || ResourceCatalog::folderForKind(result.kind).isEmpty()) {
            unrecognized.append(m_owner->tr("%1（%2）")
                                    .arg(QFileInfo(result.path).fileName(),
                                         result.detail.isEmpty() ? m_owner->tr("认不出来") : result.detail));
            continue;
        }
        installable.append(result.path);
    }
    if (!unrecognized.isEmpty()) {
        InlineMessage::warning(
            m_owner->m_ui->overlay(), m_owner->tr("拖进来的东西认不出来"),
            m_owner->tr("下面这几个不是能装进实例的东西：\n\n%1")
                .arg(unrecognized.join(QLatin1String("\n"))));
    }
    if (installable.isEmpty()) {
        return;
    }

    // 候选 = **盘上现在装着的实例**（和「已安装版本」下拉框同一个出处）。一个都没有时
    // 不弹那个空列表框（`pickFromList` 也拦了这一手），直接说一句该怎么办。
    const QStringList installed = m_owner->m_versionManager->installedVersions(nullptr);
    if (installed.isEmpty()) {
        InlineMessage::information(m_owner->m_ui->overlay(), m_owner->tr("还没有实例"),
                                   m_owner->tr("盘上还没有装好的实例，先把文件拖到某个实例的"
                                      "设置页里，或者先装一个。"));
        return;
    }
    QStringList labels;
    labels.reserve(installed.size());
    for (const QString &id : installed) {
        labels.append(m_owner->instanceDisplayName(id));
    }
    // 默认选中"左下角/下拉框里现在选中的那一个"（用户多半就是想装给它），选不着就不选。
    int initialRow = -1;
    if (QComboBox *combo = m_owner->m_ui->getInstalledVersionCombo()) {
        const QString selected = combo->currentData().toString();
        initialRow = installed.indexOf(selected);
    }
    int row = -1;
    if (!InlineMessage::pickFromList(m_owner->m_ui->overlay(), m_owner->tr("装到哪个实例里"),
                                     m_owner->tr("这几个文件装进哪一个实例？"), labels, initialRow, &row)) {
        return;  // 用户取消 / 什么都没选
    }
    if (row < 0 || row >= installed.size()) {
        return;  // 理论上到不了（pickFromList 只在选中一行时才回 true）
    }

    // 装进挑中的那一个。**走的是和实例设置那一页完全同一条路**（同一个
    // m_owner->installLocalResourceFile()），所以"同名要问一句""目录不存在就建"这些规矩只有一份。
    const QString versionId = installed.at(row);
    QList<QPair<ResourceCatalog::Kind, QStringList>> byKind;
    for (const DroppedResource::Result &result : results) {
        // 认不出来的、以及整合包（上面已经 return 了）不进这一趟。
        if (!result.known || ResourceCatalog::folderForKind(result.kind).isEmpty()) {
            continue;
        }
        const QString path = result.path;
        int slot = -1;
        for (int i = 0; i < byKind.size(); ++i) {
            if (byKind.at(i).first == result.kind) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            byKind.append(qMakePair(result.kind, QStringList{}));
            slot = byKind.size() - 1;
        }
        byKind[slot].second.append(path);
    }

    int total = 0;
    QStringList problems;
    for (const QPair<ResourceCatalog::Kind, QStringList> &entry : byKind) {
        for (const QString &path : entry.second) {
            QString problem;
            if (m_owner->installLocalResourceFile(versionId, entry.first, path, &problem)) {
                ++total;
            } else if (!problem.isEmpty()) {
                problems.append(problem);
            }
        }
    }
    if (!problems.isEmpty()) {
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("有几个没装进去"),
                               problems.join(QLatin1String("\n\n")));
    }
    if (total > 0) {
        m_owner->appendLog(m_owner->tr("已把 %1 个文件装进「%2」。").arg(total).arg(m_owner->instanceDisplayName(versionId)));
        InlineMessage::information(m_owner->m_ui->overlay(), m_owner->tr("装好了"),
                                   m_owner->tr("已经装进「%1」里了。").arg(m_owner->instanceDisplayName(versionId)));
    }
}

