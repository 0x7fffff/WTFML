#include "InstanceTransferController.hpp"

#include "InlineMessage.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/FolderManager.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/VersionManager.hpp"
#include "manage/WorldFiles.hpp"
#include "util/AppPaths.hpp"
#include "util/DirectorySessionLock.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QElapsedTimer>
#include <atomic>
#include <memory>

#include <utility>

// 后台那一份的返回值。`QFutureWatcher::finished` 不带结果，结果从 `result()` 里读 ——
// 不需要给这个结构注册元类型（那条路没走信号参数）。
struct CopyOutcome {
    bool ok = false;
    QString error;
};

namespace {

// 目标文件夹是不是**当前这个游戏文件夹**。两处要问这件事（复制成功那句提示的语气、
// 以及后台复制要不要把目标名字记进「文件操作中」），判据只能有一个。
//
// **不能**只拿 `QDir::cleanPath` 比字面量。用户完全可以把游戏文件夹
// 用另一条路指过来（junction / 符号链接）—— 那时两串字面量不同、而 `versions/<名字>/`
// 落的就是同一个目录。漏判的后果分两处：
// · 后台复制期间目标名字**没被记进 `m_fileOperationInstanceIds`** ⇒ 拷到一半的实例
// 下一次刷新就能被启动 / 删除（那两条闸只查 `installingVersionIds()`）；
// · 复制完成那句提示说"在另一个游戏文件夹里"，而它明明就在列表里。
// `InstanceFiles::pathIsSameOrInside()` 会解 junction / 8.3 短名 / 符号链接
// （`canonicalFilePath()` 自己**不解 junction**，见 InstanceFiles.cpp 里那一大段），
// 所以拿它**两头各问一次**就是"同一个文件夹"。解不出来 / 两边只有大小写写法不同时
// 退回原来那句字面量比较 —— 那正是这条判据从前的全部行为，不丢。
bool isCurrentGameFolder(const QString &folder) {
    if (folder.isEmpty()) {
        return false;
    }
    const QString current = AppPaths::minecraftDir();
    if (!current.isEmpty() && InstanceFiles::pathIsSameOrInside(current, folder)
        && InstanceFiles::pathIsSameOrInside(folder, current)) {
        return true;
    }
    return !current.isEmpty()
           && QDir::cleanPath(folder).compare(QDir::cleanPath(current), Qt::CaseInsensitive) == 0;
}

}  // namespace

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
    if (!DirectorySessionLock::acquire(destGameFolder, &error)) {
        InlineMessage::warning(m_overlay, tr("复制实例"), error);
        return false;
    }
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
    const bool sameFolder = isCurrentGameFolder(destGameFolder);
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
    QString lockProblem;
    if (!DirectorySessionLock::acquire(destGameFolder, &lockProblem)) {
        failNow(tr("复制实例"), lockProblem);
        return;
    }
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
    // 拷树在后台跑，可这段时间原先**没有任何"忙"标记**。源实例照样能被
    // 启动（游戏一边写存档、这一边读 ⇒ 副本里是半截世界）、能被删、能再改设置。
    // 开始前的 `isVersionRunning` / `isBeingModified` 两道闸只管"按下去那一刻"。
    // **源**必须记；**目标**在"目标游戏文件夹就是当前游戏文件夹"时也得分两说：
    // 实例识别只看 `versions/<名字>/` 里有没有主 jar（`InstanceFiles::resolveInstance`），
    // 而拷树是边拷边落盘的 —— 拷到一半时 `<name>/<sourceId>.jar` 已经躺在那儿，下一次
    // 任何全量刷新都能把它认成一份实例（`instanceId` 还是源的、meta 也没改）。启动 /
    // 删除的闸只查 `installingVersionIds()`，所以这一段名字也得记进去。
    // 目标在**别的**游戏文件夹时不能记这个名字：那是另一个文件夹里的相对名，当前文件夹
    // 里可能有同名实例，记了会误伤它。
    const bool copyIntoCurrentFolder = isCurrentGameFolder(destGameFolder);
    const bool markCopyTarget = copyIntoCurrentFolder && name != sourceId;
    if (m_host.setFileOperationBusy) {
        m_host.setFileOperationBusy(sourceId, true);
        if (markCopyTarget) m_host.setFileOperationBusy(name, true);
    }

    // 为什么在后台线程里**另起一个** `VersionManager`：`copyInstance()` 最后会动
    // `versionJsons` / `mergedVersionJsons` 两份缓存，拿主线程那一份在别的线程里跑就是
    // 数据竞争。导出那一层（`ModpackExportController`）用的是同一条写法。
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
                // 刷新必须**跟着摘标记走**，不能只挂在成功那一条路上。
                // 复制期间源实例被 `installingVersionIds()` 滤掉；万一这中间别处
                // （换文件夹 / 下载收尾 / 别的对话框）刷新过一次列表，屏幕上就少了
                // 这一行。复制 / 校验失败时用户更需要看见它还在。
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
    // sourceDir 在**主线程**算好再捕进来（上面 `const QString sourceDir`）。
    // `copyInstance()` 内部会现读 `AppPaths::versionDir()`，而那是进程级可变状态 ——
    // 用户在这趟跑着的时候能切换游戏文件夹（这页复制期间没有禁用），worker 里去读
    // 就是数据竞争，而且可能拷错目录。后台一律走 `copyInstanceFrom()`。
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

void InstanceTransferController::importWorldInBackground(
    const QString &instanceId, const QString &worldDir, const QString &name,
    std::function<void(bool, const QString &)> whenDone) {
    const auto reject = [this, &whenDone](const QString &problem) {
        if (whenDone) whenDone(false, problem);
        InlineMessage::warning(m_overlay, tr("导入世界失败"), problem);
    };
    if ((m_launcher && m_launcher->isVersionRunning(instanceId)) ||
        (m_host.isBeingModified && m_host.isBeingModified(instanceId))) {
        reject(tr("实例正在运行或处理文件，完成后再导入存档。"));
        return;
    }
    const QString savesDir = m_host.instanceDirForKey
        ? m_host.instanceDirForKey(instanceId, QStringLiteral("save")) : QString();
    if (savesDir.isEmpty()) {
        reject(tr("认不出这个实例的存档目录。"));
        return;
    }
    // Kind::Import 而不是 Other：账本按种类给的默认能力位里只有三类能取消，
    // 「导入世界」本来就是其中的一类 —— 手动取消那颗按钮（见 cancelTask()）靠它才露出来。
    const auto task = m_host.beginTask
        ? m_host.beginTask(Tasks::TaskKind::Import, tr("导入世界：%1").arg(name), instanceId,
            {{tr("复制并验证世界"), Tasks::TaskStepState::Running, {}}})
        : Tasks::invalidTaskId();
    const auto state = std::make_shared<ImportState>();
    if (task != Tasks::invalidTaskId()) m_importStates.insert(task, state);
    if (m_host.setTaskDetail) m_host.setTaskDetail(task, tr("正在后台导入存档，请勿修改源世界。"));
    if (m_host.setFileOperationBusy) m_host.setFileOperationBusy(instanceId, true);
    auto *watcher = new QFutureWatcher<CopyOutcome>(this);
    connect(watcher, &QFutureWatcher<CopyOutcome>::finished, this,
        [this, watcher, instanceId, savesDir, worldDir, name, task, state,
         whenDone = std::move(whenDone)] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (task != Tasks::invalidTaskId()) m_importStates.remove(task);
            if (m_host.setFileOperationBusy) m_host.setFileOperationBusy(instanceId, false);
            // 「用户按了取消」只在**真的没做成**时才算数：复制已经完整跑完、发布也成功了，
            // 那一下点晚了 —— 该报成功（存档确实在那儿），不能把它记成取消。
            const bool cancelledByUser =
                state->cancelledByUser.load(std::memory_order_relaxed) && !result.ok;
            if (cancelledByUser) {
                // 取消不是失败：账本收成 Cancelled（不亮红点），也不弹失败提示。
                if (m_host.markTaskCancelled) m_host.markTaskCancelled(task);
                log(tr("导入世界已取消：%1").arg(name));
            } else {
                if (m_host.setTaskStepState) m_host.setTaskStepState(task, 0,
                    result.ok ? Tasks::TaskStepState::Done : Tasks::TaskStepState::Failed);
                if (m_host.finishTask) m_host.finishTask(task, result.ok, result.error);
            }
            if (m_host.refreshInstances) m_host.refreshInstances();
            if (result.ok) log(tr("导入世界：%1 → %2").arg(worldDir, savesDir));
            if (whenDone) whenDone(result.ok, cancelledByUser ? tr("已取消，未发布存档。")
                                                              : result.error);
            if (!result.ok && !cancelledByUser)
                InlineMessage::warning(m_overlay, tr("导入世界失败"), result.error);
        });
    // 不在 worker 读取 AppPaths / 当前实例选择，也不捕获控制器或页面。
    const auto destroyed = connect(this, &QObject::destroyed, [state] {
        state->abort.store(true, std::memory_order_relaxed);
    });
    connect(watcher, &QFutureWatcher<CopyOutcome>::finished, watcher, [destroyed] {
        QObject::disconnect(destroyed);
    });
    watcher->setFuture(QtConcurrent::run([worldDir, savesDir, name, state] {
        CopyOutcome result;
        QElapsedTimer elapsed;
        elapsed.start();
        result.ok = WorldFiles::copyWorldInto(worldDir, savesDir, name, &result.error,
            [&] {
                return state->abort.load(std::memory_order_relaxed)
                       || elapsed.elapsed() >= 600000;
            });
        return result;
    }));
}

void InstanceTransferController::deleteWorldInBackground(
    const QString &instanceId, const QString &worldDir,
    std::function<void(bool, const QString &)> whenDone) {
    const auto reject = [this, &whenDone](const QString &problem) {
        if (whenDone) whenDone(false, problem);
        InlineMessage::warning(m_overlay, tr("删除失败"), problem);
    };
    if (worldDir.isEmpty()) {
        reject(tr("认不出要删除哪个世界。"));
        return;
    }
    // 用户确认那一问和真正开删之间隔着一次事件循环：这中间游戏可能刚被启动。
    // 游戏退出（或者在游戏里存一次）的时候会把整个世界重新写一遍，而那时目录已经
    // 没了 —— 所以和 `InstanceDialogController` 里那道闸同一条理由，这里**再查一次**。
    if (!instanceId.isEmpty() && m_launcher && m_launcher->isVersionRunning(instanceId)) {
        reject(tr("游戏正在运行，先关掉游戏再删这个世界。"));
        return;
    }
    if (m_host.isBeingModified && m_host.isBeingModified(instanceId)) {
        const QString who =
            m_host.displayName ? m_host.displayName(instanceId) : instanceId;
        reject(tr("「%1」正在被另一项操作占用（安装、导入、复制、备份或还原），"
                  "等它结束之后再删这个世界。")
                   .arg(who));
        return;
    }
    const QString worldName = QFileInfo(worldDir).fileName();
    // Kind::Other（不是 Import）：删除**不提供取消** —— 删到一半停下留下的是半截世界，
    // 比删完更难收拾。Other 的默认能力位里本来就没有「取消」，和这里的语义对得上。
    const auto task = m_host.beginTask
        ? m_host.beginTask(Tasks::TaskKind::Other, tr("删除世界：%1").arg(worldName), instanceId,
            {{tr("删除世界文件"), Tasks::TaskStepState::Running, {}}})
        : Tasks::invalidTaskId();
    if (m_host.setTaskDetail) m_host.setTaskDetail(task, tr("正在删除世界文件。"));
    if (m_host.setFileOperationBusy) m_host.setFileOperationBusy(instanceId, true);
    auto *watcher = new QFutureWatcher<CopyOutcome>(this);
    connect(watcher, &QFutureWatcher<CopyOutcome>::finished, this,
        [this, watcher, instanceId, worldDir, task, whenDone = std::move(whenDone)] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (m_host.setFileOperationBusy) m_host.setFileOperationBusy(instanceId, false);
            // 删不干净时的说法沿用原来那一句：用户要的是"为什么没删掉 + 接下来做什么"。
            const QString error =
                result.ok
                    ? QString()
                    : tr("没能删掉「%1」：多半是它正被游戏（或者别的程序）占着。"
                         "先把游戏关掉再试。")
                          .arg(QDir::toNativeSeparators(worldDir));
            if (m_host.setTaskStepState) m_host.setTaskStepState(task, 0,
                result.ok ? Tasks::TaskStepState::Done : Tasks::TaskStepState::Failed);
            if (m_host.finishTask) m_host.finishTask(task, result.ok, error);
            if (m_host.refreshInstances) m_host.refreshInstances();
            log(result.ok ? tr("删除世界：%1").arg(QDir::toNativeSeparators(worldDir))
                          : tr("删除世界失败：%1").arg(QDir::toNativeSeparators(worldDir)));
            if (whenDone) whenDone(result.ok, error);
            if (!result.ok) InlineMessage::warning(m_overlay, tr("删除失败"), error);
        });
    // 工人只碰路径：不读 AppPaths、不碰当前实例选择、不捕获控制器或页面。
    watcher->setFuture(QtConcurrent::run([worldDir] {
        CopyOutcome result;
        // 不能换成 `QDir::removeRecursively()`：它会跟着 junction / 符号链接走进目标目录
        // （见 FolderManager.cpp 里那段）。
        result.ok = removeTreeWithoutFollowingLinks(worldDir);
        return result;
    }));
}

bool InstanceTransferController::canCancelTask(Tasks::TaskId id) const {
    return id != Tasks::invalidTaskId() && m_importStates.contains(id);
}

bool InstanceTransferController::cancelTask(Tasks::TaskId id) {
    const auto it = m_importStates.constFind(id);
    if (it == m_importStates.cend()) {
        return false;
    }
    // cancelledByUser 先落：工人看到 abort 就返回，收尾那一格读的就是这两位的先后。
    (*it)->cancelledByUser.store(true, std::memory_order_relaxed);
    (*it)->abort.store(true, std::memory_order_release);
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
    // 这一份实例正在被别的作业占着（快照 / 世界备份 / 还原 / 复制 / 安装）
    // 时不许改世界目录的名字。那些作业按**改名前算好的绝对路径**逐个读整棵目录树，
    // 目录在扫描途中被改名，它们要么静默少读一批文件、要么把结果写到一半就找不到落点，
    // 收尾却照样报成功 —— 一份看着成功、其实是残的备份比失败更糟。
    if (m_host.isBeingModified && m_host.isBeingModified(instanceId)) {
        InlineMessage::warning(m_overlay, tr("重命名世界"),
                               tr("「%1」正在被另一项操作占用（安装、导入、复制、备份或还原），"
                                  "等它结束之后再改这个世界。")
                                   .arg(displayName(instanceId)));
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
