#include "InstanceTransferController.hpp"

#include "InlineMessage.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/VersionManager.hpp"
#include "manage/WorldFiles.hpp"
#include "util/AppPaths.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

// 后台那一份的返回值。`QFutureWatcher::finished` 不带结果，结果从 `result()` 里读 ——
// 不需要给这个结构注册元类型（那条路没走信号参数）。
struct CopyOutcome {
    bool ok = false;
    QString error;
};

InstanceTransferController::InstanceTransferController(VersionManager *versions,
                                                       LauncherManager *launcher,
                                                       OverlayHost *overlay, Host host,
                                                       QObject *parent)
    : QObject(parent), m_versions(versions), m_launcher(launcher), m_overlay(overlay),
      m_host(std::move(host)) {}

QString InstanceTransferController::displayName(const QString &id) const {
    return m_host.displayName ? m_host.displayName(id) : id;
}
void InstanceTransferController::log(const QString &text) const {
    if (m_host.log) m_host.log(text);
}

bool InstanceTransferController::copyInstance(const QString &sourceId,
                                               const QString &destGameFolder,
                                               const QString &newName) {
    const QString name = newName.trimmed();
    if (name.isEmpty()) {
        InlineMessage::warning(m_overlay, tr("复制实例"), tr("副本得有个名字。"));
        return false;
    }
    const QString nameProblem = InstanceFiles::validateDirName(name);
    if (!nameProblem.isEmpty()) {
        InlineMessage::warning(m_overlay, tr("复制实例"), nameProblem);
        return false;
    }
    if (m_launcher->isVersionRunning(sourceId)) {
        InlineMessage::warning(m_overlay, tr("复制实例"),
                               tr("「%1」正在运行。先关掉游戏再复制 —— 游戏开着的时候拷出来的"
                                  "是一份写到一半的存档。")
                                   .arg(displayName(sourceId)));
        return false;
    }
    if (m_host.isBeingModified && m_host.isBeingModified(sourceId)) {
        InlineMessage::warning(m_overlay, tr("复制实例"),
                               tr("「%1」正在下载，等它装完再复制。")
                                   .arg(displayName(sourceId)));
        return false;
    }
    const QString sourceDir = AppPaths::versionDir(sourceId);
    if (InstanceFiles::pathIsSameOrInside(sourceDir, destGameFolder)) {
        InlineMessage::warning(
            m_overlay, tr("复制实例"),
            tr("「%1」不能复制到它自己里面去。\n\n你挑的是：\n%2\n\n那是这一份实例自己的目录"
               "（或者它底下的一个子目录）。请挑一个游戏文件夹 —— 也就是实例目录上面"
               "那一层，里面直接看得到 versions 文件夹的那一个。")
                .arg(displayName(sourceId), QDir::toNativeSeparators(destGameFolder)));
        return false;
    }
    QString error;
    if (!m_versions->copyInstance(sourceId, destGameFolder, name, &error)) {
        InlineMessage::critical(m_overlay, tr("复制实例失败"), error);
        return false;
    }
    if (m_host.refreshInstances) m_host.refreshInstances();
    announceCopySuccess(sourceId, destGameFolder, name);
    return true;
}

void InstanceTransferController::announceCopySuccess(const QString &sourceId,
                                                      const QString &destGameFolder,
                                                      const QString &name) {
    const QString landed = QDir::toNativeSeparators(
        QDir(destGameFolder).absoluteFilePath(QStringLiteral("versions/") + name));
    log(tr("已复制实例 %1 → %2").arg(displayName(sourceId), landed));
    const bool sameFolder = QDir::cleanPath(destGameFolder).compare(
                                QDir::cleanPath(AppPaths::minecraftDir()), Qt::CaseInsensitive) == 0;
    if (sameFolder) {
        InlineMessage::information(m_overlay, tr("复制实例"),
                                   tr("「%1」已经复制好了，就在实例列表里。").arg(name));
    } else {
        InlineMessage::information(
            m_overlay, tr("复制实例"),
            tr("「%1」已经复制到另一个游戏文件夹里了：\n%2\n\n它不在当前这个游戏文件夹的实例"
               "列表里 —— 想用它就先把游戏文件夹切过去。")
                .arg(name, landed));
    }
}

void InstanceTransferController::copyInstanceInBackground(
    const QString &sourceId, const QString &destGameFolder, const QString &newName,
    std::function<void(bool, const QString &)> whenDone) {
    const auto done = [whenDone](bool ok, const QString &error) {
        if (whenDone) whenDone(ok, error);
    };
    const auto failNow = [this, &done](const QString &title, const QString &text) {
        InlineMessage::warning(m_overlay, title, text);
        done(false, text);
    };
    const QString name = newName.trimmed();
    if (name.isEmpty()) {
        failNow(tr("复制实例"), tr("副本得有个名字。"));
        return;
    }
    const QString nameProblem = InstanceFiles::validateDirName(name);
    if (!nameProblem.isEmpty()) {
        failNow(tr("复制实例"), nameProblem);
        return;
    }
    if (m_copyBusy) {
        // NOTE: 这不是"忙"的那一条正常路径 —— 界面上那颗按钮在复制期间还能按，所以得挡。
        InlineMessage::information(m_overlay, tr("复制实例"),
                                   tr("已经有一份实例正在复制，等它完成后再试。"));
        done(false, tr("已经有一份实例正在复制。"));
        return;
    }
    if (m_launcher->isVersionRunning(sourceId)) {
        failNow(tr("复制实例"),
                tr("「%1」正在运行。先关掉游戏再复制 —— 游戏开着的时候拷出来的是一份写到"
                   "一半的存档。")
                    .arg(displayName(sourceId)));
        return;
    }
    if (m_host.isBeingModified && m_host.isBeingModified(sourceId)) {
        failNow(tr("复制实例"),
                tr("「%1」正在下载，等它装完再复制。").arg(displayName(sourceId)));
        return;
    }
    const QString sourceDir = AppPaths::versionDir(sourceId);
    if (InstanceFiles::pathIsSameOrInside(sourceDir, destGameFolder)) {
        failNow(tr("复制实例"),
                tr("「%1」不能复制到它自己里面去。\n\n你挑的是：\n%2\n\n那是这一份实例自己的"
                   "目录（或者它底下的一个子目录）。请挑一个游戏文件夹 —— 也就是实例目录上面"
                   "那一层，里面直接看得到 versions 文件夹的那一个。")
                    .arg(displayName(sourceId), QDir::toNativeSeparators(destGameFolder)));
        return;
    }

    // ---------- 任务中心：规划 → 执行 → 验证 ----------
    const QList<Tasks::TaskStep> steps{
        Tasks::TaskStep{tr("规划：检查名字与目标文件夹"), Tasks::TaskStepState::Done, {}},
        Tasks::TaskStep{tr("执行：复制实例目录"), Tasks::TaskStepState::Running, {}},
        Tasks::TaskStep{tr("验证：核对副本的 jar / json / meta"),
                        Tasks::TaskStepState::Pending, {}},
    };
    if (m_host.beginTask) {
        m_copyTaskId = m_host.beginTask(
            Tasks::TaskKind::Other,
            tr("复制实例 %1 → %2").arg(displayName(sourceId), name), name, steps);
        if (m_host.setTaskProgress) m_host.setTaskProgress(m_copyTaskId, 1, 3);
        if (m_host.setTaskDetail) {
            m_host.setTaskDetail(m_copyTaskId, tr("正在后台复制实例目录…"));
        }
    }
    log(tr("开始复制实例 %1 → %2 …").arg(displayName(sourceId), name));
    m_copyBusy = true;
    // ★ 2026-10-19：拷树在后台跑，可这段时间原先**没有任何"忙"标记**。源实例照样能被
    //   启动（游戏一边写存档、这一边读 ⇒ 副本里是半截世界）、能被删、能再改设置。
    //   开始前的 `isVersionRunning` / `isBeingModified` 两道闸只管"按下去那一刻"。
    //   **源**必须记；**目标**在"目标游戏文件夹就是当前游戏文件夹"时也得分两说：
    //   实例识别只看 `versions/<名字>/` 里有没有主 jar（`InstanceFiles::resolveInstance`），
    //   而拷树是边拷边落盘的 —— 拷到一半时 `<name>/<sourceId>.jar` 已经躺在那儿，下一次
    //   任何全量刷新都能把它认成一份实例（`instanceId` 还是源的、meta 也没改）。启动 /
    //   删除的闸只查 `installingVersionIds()`，所以这一段名字也得记进去。
    //   目标在**别的**游戏文件夹时不能记这个名字：那是另一个文件夹里的相对名，当前文件夹
    //   里可能有同名实例，记了会误伤它。
    const bool copyIntoCurrentFolder =
        QDir::cleanPath(destGameFolder)
            .compare(QDir::cleanPath(AppPaths::minecraftDir()), Qt::CaseInsensitive)
        == 0;
    const bool markCopyTarget = copyIntoCurrentFolder && name != sourceId;
    if (m_host.setFileOperationBusy) {
        m_host.setFileOperationBusy(sourceId, true);
        if (markCopyTarget) m_host.setFileOperationBusy(name, true);
    }

    // ★ 为什么在后台线程里**另起一个** `VersionManager`：`copyInstance()` 最后会动
    //   `versionJsons` / `mergedVersionJsons` 两份缓存，拿主线程那一份在别的线程里跑就是
    //   数据竞争。导出那一层（`ModpackExportController`）用的是同一条写法。
    auto *watcher = new QFutureWatcher<CopyOutcome>(this);
    connect(watcher, &QFutureWatcher<CopyOutcome>::finished, this,
            [this, watcher, sourceId, sourceDir, destGameFolder, name, markCopyTarget, done] {
                const CopyOutcome outcome = watcher->result();
                watcher->deleteLater();
                m_copyBusy = false;
                // 先摘忙标记，再走下面任何一条早退（复制失败 / 校验没对上）。
                if (m_host.setFileOperationBusy) {
                    m_host.setFileOperationBusy(sourceId, false);
                    if (markCopyTarget) m_host.setFileOperationBusy(name, false);
                }
                // ★ 2026-10-19：刷新必须**跟着摘标记走**，不能只挂在成功那一条路上。
                //   复制期间源实例被 `installingVersionIds()` 滤掉；万一这中间别处
                //   （换文件夹 / 下载收尾 / 别的对话框）刷新过一次列表，屏幕上就少了
                //   这一行。复制 / 校验失败时用户更需要看见它还在。
                if (m_host.refreshInstances) m_host.refreshInstances();
                const auto finishTask = [this](bool success, const QString &error) {
                    if (m_host.finishTask && m_copyTaskId != Tasks::invalidTaskId()) {
                        m_host.finishTask(m_copyTaskId, success, error);
                    }
                    m_copyTaskId = Tasks::invalidTaskId();
                };
                if (!outcome.ok) {
                    if (m_host.setTaskStepState && m_copyTaskId != Tasks::invalidTaskId()) {
                        m_host.setTaskStepState(m_copyTaskId, 1,
                                                Tasks::TaskStepState::Failed);
                    }
                    finishTask(false, outcome.error);
                    InlineMessage::critical(m_overlay, tr("复制实例失败"), outcome.error);
                    done(false, outcome.error);
                    return;
                }
                if (m_host.setTaskStepState && m_copyTaskId != Tasks::invalidTaskId()) {
                    m_host.setTaskStepState(m_copyTaskId, 1, Tasks::TaskStepState::Done);
                    m_host.setTaskStepState(m_copyTaskId, 2, Tasks::TaskStepState::Running);
                    if (m_host.setTaskProgress) m_host.setTaskProgress(m_copyTaskId, 2, 3);
                }
                // 验证：目录在、该跟着改名的 json 跟着改了、meta 也在。只看"做出来的这一份
                // 能不能被认出来"，不再读一遍内容。
                const QString destDir =
                    QDir(destGameFolder).absoluteFilePath(QStringLiteral("versions/") + name);
                const bool sourceHadJson =
                    QFileInfo::exists(sourceDir + QLatin1Char('/') + sourceId
                                      + QStringLiteral(".json"));
                QString verifyProblem;
                if (!QFileInfo(destDir).isDir()) {
                    verifyProblem = tr("后台复制说完成了，但目标目录不在：%1")
                                        .arg(QDir::toNativeSeparators(destDir));
                } else if (sourceHadJson
                           && !QFileInfo::exists(destDir + QLatin1Char('/') + name
                                                 + QStringLiteral(".json"))) {
                    verifyProblem = tr("副本里的版本 json 没跟着改名（%1.json 不在）。")
                                        .arg(name);
                } else if (!QFileInfo::exists(destDir + QLatin1Char('/')
                                              + AppPaths::versionMetaFileName())) {
                    verifyProblem = tr("副本里没有 %1 —— 启动器认不出它是谁。")
                                        .arg(AppPaths::versionMetaFileName());
                }
                if (!verifyProblem.isEmpty()) {
                    if (m_host.setTaskStepState && m_copyTaskId != Tasks::invalidTaskId()) {
                        m_host.setTaskStepState(m_copyTaskId, 2,
                                                Tasks::TaskStepState::Failed);
                    }
                    finishTask(false, verifyProblem);
                    InlineMessage::warning(m_overlay, tr("复制实例"), verifyProblem);
                    done(false, verifyProblem);
                    return;
                }
                if (m_host.setTaskStepState && m_copyTaskId != Tasks::invalidTaskId()) {
                    m_host.setTaskStepState(m_copyTaskId, 2, Tasks::TaskStepState::Done);
                    if (m_host.setTaskProgress) m_host.setTaskProgress(m_copyTaskId, 3, 3);
                }
                finishTask(true, {});
                announceCopySuccess(sourceId, destGameFolder, name);
                done(true, {});
            });
    // ★ sourceDir 在**主线程**算好再捕进来（上面 `const QString sourceDir`）。
    //   `copyInstance()` 内部会现读 `AppPaths::versionDir()`，而那是进程级可变状态 ——
    //   用户在这趟跑着的时候能切换游戏文件夹（这页复制期间没有禁用），worker 里去读
    //   就是数据竞争，而且可能拷错目录。后台一律走 `copyInstanceFrom()`。
    watcher->setFuture(QtConcurrent::run([sourceDir, sourceId, destGameFolder, name] {
        VersionManager worker;
        CopyOutcome outcome;
        outcome.ok = worker.copyInstanceFrom(sourceDir, sourceId, destGameFolder, name,
                                             &outcome.error);
        return outcome;
    }));
}

bool InstanceTransferController::importWorld(const QString &instanceId,
                                             const QString &worldDir,
                                             const QString &name) {
    if (m_launcher && m_launcher->isVersionRunning(instanceId)) {
        InlineMessage::warning(m_overlay, tr("导入世界"),
                               tr("游戏正在运行，先关闭游戏再导入存档。"));
        return false;
    }
    if (m_host.isBeingModified && m_host.isBeingModified(instanceId)) {
        InlineMessage::warning(m_overlay, tr("导入世界"),
                               tr("实例正在安装或处理文件，完成后再导入存档。"));
        return false;
    }
    const QString savesDir = m_host.instanceDirForKey
        ? m_host.instanceDirForKey(instanceId, QStringLiteral("save")) : QString();
    if (savesDir.isEmpty()) {
        InlineMessage::critical(m_overlay, tr("导入世界失败"), tr("认不出这个实例的存档目录。"));
        return false;
    }
    QString error;
    if (!WorldFiles::copyWorldInto(worldDir, savesDir, name, &error)) {
        InlineMessage::critical(m_overlay, tr("导入世界失败"), error);
        return false;
    }
    log(tr("导入世界：%1 → %2").arg(QDir::toNativeSeparators(worldDir),
                                      QDir::toNativeSeparators(savesDir)));
    return true;
}

bool InstanceTransferController::renameWorld(const QString &instanceId,
                                             const QString &worldDir,
                                             const QString &newName) {
    if (m_launcher->isVersionRunning(instanceId)) {
        InlineMessage::warning(m_overlay, tr("重命名世界"),
                               tr("游戏正在运行，先关掉游戏再改存档的名字。"));
        return false;
    }
    QString error;
    if (!WorldFiles::renameWorld(worldDir, newName, &error)) {
        InlineMessage::warning(m_overlay, tr("重命名世界"), error);
        return false;
    }
    log(tr("世界改名：%1 → %2").arg(QDir::toNativeSeparators(worldDir), newName.trimmed()));
    return true;
}
