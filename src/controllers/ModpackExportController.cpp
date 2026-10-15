#include "ModpackExportController.hpp"

#include "InlineMessage.hpp"
#include "OverlayHost.hpp"
#include "manage/VersionManager.hpp"
#include "settings/LauncherSettings.hpp"
#include "util/AppPaths.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>
#include <utility>

ModpackExportController::ModpackExportController(VersionManager *versions,
                                                 LauncherSettings *settings,
                                                 OverlayHost *overlay, Host host,
                                                 QObject *parent)
    : QObject(parent), m_versions(versions), m_settings(settings), m_overlay(overlay),
      m_host(std::move(host)) {}

QString ModpackExportController::displayName(const QString &id) const {
    return m_host.displayName ? m_host.displayName(id) : id;
}
void ModpackExportController::log(const QString &text) const {
    if (m_host.log) m_host.log(text);
}
void ModpackExportController::setStatus(const QString &text) const {
    if (m_host.setStatus) m_host.setStatus(text);
}

void ModpackExportController::exportPack(const QString &versionId,
                                         const QString &targetPath) {
    if (versionId.isEmpty() || targetPath.isEmpty()) return;
    if (m_busy) {
        InlineMessage::information(m_overlay, tr("正在导出"),
                                   tr("已经有一份整合包正在打包，等它完成后再开始下一份。"));
        return;
    }
    if (m_host.isBeingModified && m_host.isBeingModified(versionId)) {
        InlineMessage::information(
            m_overlay, tr("现在做不了"),
            tr("「%1」这会儿正在装（或者正在导入 / 正在检查）。等它完了再导出 —— "
               "打包要读的正是这会儿正在被改的那些文件。")
                .arg(displayName(versionId)));
        return;
    }
    const auto format = targetPath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)
                            ? ModpackExport::Format::CurseForge
                            : ModpackExport::Format::Modrinth;
    const QString instanceDir = AppPaths::versionDir(versionId);
    if (!QFileInfo(instanceDir).isDir()) {
        log(tr("导出失败：找不到实例目录 %1").arg(instanceDir));
        InlineMessage::warning(m_overlay, tr("导出失败"),
                               tr("找不到这个实例的目录：\n%1")
                                   .arg(QDir::toNativeSeparators(instanceDir)));
        return;
    }
    const QString gameVersion = m_versions->instanceGameVersion(versionId);
    if (gameVersion.isEmpty()) {
        log(tr("导出失败：读不出 %1 基于哪个原版").arg(versionId));
        InlineMessage::warning(
            m_overlay, tr("读不出这个实例基于哪个原版"),
            tr("整合包清单里必须写清楚「基于哪个原版游戏」，而这一份读不出来 —— 要么本地"
               "没有一处写着它（wtfml.meta 里没有 realVersion，版本 json 里没有 clientVersion、"
               "也顺不出指得到原版的 inheritsFrom 链），要么写着的那个**不是一个真能装出来的"
               "版本号**（自己改过的名字就属于这种：清单里记着它，别人那边却没有这个版本可装）。\n\n"
               "这个实例多半是很早以前装的、或者被手动改过。在「实例设置…」里看不出版本号"
               "的话，建议重装一份再导出。"));
        return;
    }
    QString loaderKey;
    QString loaderVersion;
    const QString loaderProblem =
        m_versions->instanceLoaderInfo(versionId, &loaderKey, &loaderVersion);
    if (!loaderProblem.isEmpty()) {
        log(tr("导出失败：认不出 %1 的加载器").arg(versionId));
        InlineMessage::warning(m_overlay, tr("这个实例没法按整合包规范导出"), loaderProblem);
        return;
    }
    ModpackExport::ExportRequest request;
    request.format = format;
    request.instanceDir = instanceDir;
    request.name = displayName(versionId);
    request.version = QStringLiteral("1.0.0");
    request.gameVersion = gameVersion;
    request.loaderKey = loaderKey;
    request.loaderVersion = loaderVersion;
    log(tr("开始导出「%1」（%2）...")
            .arg(displayName(versionId),
                 format == ModpackExport::Format::CurseForge ? tr("CurseForge 规范")
                                                             : tr("Modrinth 规范")));
    m_busy = true;
    setStatus(tr("正在后台分析整合包…"));
    const QString modrinthBase = m_settings->modrinthApiBaseOverride();
    const QString curseForgeKey = m_settings->curseForgeApiKey();
    const QString curseForgeBase = m_settings->curseForgeApiBaseOverride();
    const QPointer<ModpackExportController> self(this);
    auto throttle = std::make_shared<QElapsedTimer>();
    const ModpackExport::ExportProgress progress =
        [self, throttle](int done, int total, const QString &current) {
            if (!self || (throttle->isValid() && throttle->elapsed() < 100 && done < total)) return;
            throttle->start();
            QMetaObject::invokeMethod(self.data(), [self, done, total, current] {
                if (!self) return;
                self->setStatus(self->tr("正在后台分析整合包…（%1/%2）%3")
                                    .arg(done).arg(total)
                                    .arg(current.isEmpty() ? QString() : QFileInfo(current).fileName()));
            }, Qt::QueuedConnection);
        };
    auto *watcher = new QFutureWatcher<ModpackExport::ExportPlan>(this);
    connect(watcher, &QFutureWatcher<ModpackExport::ExportPlan>::finished, this,
            [this, watcher, targetPath, format] {
                const auto plan = watcher->result();
                watcher->deleteLater();
                setStatus({});
                continueExport(plan, targetPath, format);
            });
    watcher->setFuture(QtConcurrent::run(
        [request, progress, self, modrinthBase, curseForgeKey, curseForgeBase] {
            VersionManager worker;
            worker.setModrinthApiBaseOverride(modrinthBase);
            worker.setCurseForgeApiKey(curseForgeKey);
            worker.setCurseForgeApiBaseOverride(curseForgeBase);
            return worker.buildExportPlan(request, progress, [self] { return self.isNull(); });
        }));
}

void ModpackExportController::continueExport(const ModpackExport::ExportPlan &plan,
                                             const QString &targetPath,
                                             ModpackExport::Format format) {
    if (plan.cancelled) {
        m_busy = false;
        log(tr("导出已取消。"));
        return;
    }
    if (!plan.problem.isEmpty()) {
        m_busy = false;
        log(tr("导出失败：%1").arg(plan.problem));
        InlineMessage::warning(m_overlay, tr("导出失败"), plan.problem);
        return;
    }
    QStringList reasons;
    if (plan.lookupFailed) reasons.append(plan.lookupProblem);
    if (!ModpackExport::reimportable(plan.spec)) {
        reasons.append(tr("一个资源都没认出来 —— 这样的包里没有任何一条能自动下载的条目，我们自己的"
                          "「导入整合包…」会直接判它无效。"));
    }
    if (!reasons.isEmpty()) {
        const int choice = InlineMessage::ask(
            m_overlay, MessageKind::Question, tr("继续导出吗"),
            tr("%1\n\n继续的话，认不出出处的文件会原样打进包里：别人装上照样能用，"
               "只是启动器不能替他单独更新或者去重。")
                .arg(reasons.join(QStringLiteral("\n\n"))),
            {tr("继续导出"), tr("取消")}, 1, -1);
        if (choice != 0) {
            m_busy = false;
            log(tr("已取消导出。"));
            return;
        }
    }
    setStatus(tr("正在后台打包整合包…"));
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher, plan, targetPath, format] {
                const QString problem = watcher->result();
                watcher->deleteLater();
                m_busy = false;
                setStatus({});
                if (!problem.isEmpty()) {
                    log(tr("导出失败：%1").arg(problem));
                    InlineMessage::warning(m_overlay, tr("导出失败"), problem);
                    return;
                }
                const QString shown = QDir::toNativeSeparators(targetPath);
                log(tr("✓ 整合包已导出：%1").arg(shown));
                QStringList lines{tr("已经写到：\n%1").arg(shown)};
                lines.append(tr("问过 %1 个文件，认出来 %2 个（写进清单，装的人那边自己去下）；"
                                "其余 %3 个原样打进了包里（模组本体、配置、资源包都在）。")
                                 .arg(plan.queried).arg(plan.matched).arg(plan.overrides.size()));
                if (plan.notDistributable > 0)
                    lines.append(tr("其中 %1 个的作者关掉了第三方分发 —— 它们只能跟着包走。")
                                     .arg(plan.notDistributable));
                if (plan.keptLocalState > 0)
                    lines.append(tr("其中 %1 个是你改过名字（或者禁用着）的 —— 它们也原样进包，"
                                    "名字和状态都照你盘上那样留着，装的人那边不会被改回启用。")
                                     .arg(plan.keptLocalState));
                if (plan.notDistributableUnknown > 0)
                    lines.append(tr("另外 %1 个没能问出能不能分发（CurseForge 的回复里没有这一项）—— "
                                    "它们也原样进了包，装的人照样能用，只是不能替他单独更新或者去重。")
                                     .arg(plan.notDistributableUnknown));
                if (!plan.hashFailures.isEmpty())
                    lines.append(tr("有 %1 个文件连哈希都没算出来（读取失败），它们也原样进了包。")
                                     .arg(plan.hashFailures.size()));
                if (!plan.collect.skippedDirs.isEmpty())
                    lines.append(tr("没进包：%1（共 %2 个文件）。存档和截图是**你自己**的东西，"
                                    "打进包里会盖掉收包那个人的。")
                                     .arg(plan.collect.skippedDirs.join(QStringLiteral("、")),
                                          QString::number(plan.collect.skippedFileCount)));
                lines.append(tr("这是 %1 的包：别人在启动器里用「导入整合包…」就能装回来。")
                                 .arg(format == ModpackExport::Format::CurseForge
                                          ? tr("CurseForge 规范") : tr("Modrinth 规范")));
                InlineMessage::information(m_overlay, tr("导出完成"),
                                           lines.join(QStringLiteral("\n\n")));
            });
    watcher->setFuture(QtConcurrent::run([plan, targetPath] {
        VersionManager worker;
        return worker.writePackArchive(plan, targetPath);
    }));
}
