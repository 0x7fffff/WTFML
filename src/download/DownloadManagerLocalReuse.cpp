/**
 * @file DownloadManagerLocalReuse.cpp
 * @brief 下载之前的两条"先问盘"的路：从别的游戏文件夹借文件、整份核对与原件指纹。
 *
 * 这两条线的状态（`m_reuseQueue` / `m_precheckQueue` 那两个队列、各自的取消令牌和代次）
 * 只在这里被改动。`DownloadManager.cpp` 留着调度入口（`scheduleFile()`）和真正的网络
 * 队列；搬出来之后"一条作业在网线上怎么走"与"一条作业在盘上问出来什么"不再挤在同一个
 * 文件里。
 *
 * 记账规则两条线一致：借成功 / 核对通过都**不进 done/total**（一个字节都没走网络），
 * 只有退回网络的那一条才在 `queueNetworkJob()` 里计进分母。
 */

#include "DownloadManager.hpp"
#include "DownloadCheckpoint.hpp"

#include "../util/AppPaths.hpp"
#include "../util/DownloadDiagnostics.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRunnable>
#include <QThreadPool>

#include <atomic>

void DownloadManager::continueAfterPrecheck(const PrecheckJob &job, const QJsonObject *original) {
    if (m_checkpoint && !job.range.active()) {
        const auto path = QDir::cleanPath(QFileInfo(job.destinationPath).absoluteFilePath());
        const auto *recorded = m_checkpoint->find(path);
        // 后台算好的那一份优先；没有（便宜那一支：文件根本不在，指纹就是 {"exists":false}）
        // 才现算。**两条路的产物必须一模一样** —— 恢复记录里存的就是这一份。
        const auto fingerprint = recorded ? recorded->original
            : (original != nullptr ? *original : DownloadCheckpoint::fingerprint(path));
        if (!m_checkpoint->add({job.url, path, job.expectedSha1, job.officialUrl, job.expectedSize,
                                fingerprint})) {
            m_checkpointFailed = true;
            emit checkpointChanged();
        }
    }

    // 盘上没有，但**别的游戏文件夹里可能有一模一样的**（见
    // setReuseSourceFolders()）。这一步排在 m_totalJobs++ 之前是故意的：借成功的话它
    // 压根不走网络，就不该占进度条分母（和跳过那一支同一个道理）；借不到再回来补记。
    if (!m_checkpoint && job.mode != VerifyMode::AlwaysDownload
        && !m_flaggedPaths.contains(job.destinationPath) && !job.expectedSha1.isEmpty()
        && !job.range.active() && !m_reuseSourceFolders.isEmpty()) {
        const QString relativePath =
            LocalFileReuse::relativePathWithin(job.destinationPath, AppPaths::minecraftDir());
        if (!relativePath.isEmpty()) {
            ReuseJob reuse;
            reuse.url = job.url;
            reuse.destinationPath = job.destinationPath;
            reuse.relativePath = relativePath;
            reuse.expectedSha1 = job.expectedSha1;
            reuse.expectedSize = job.expectedSize;
            reuse.mode = job.mode;
            reuse.officialUrl = job.officialUrl;
            m_reuseQueue.enqueue(reuse);
            pumpReuseQueue();
            return;
        }
    }

    queueNetworkJob(job.url, job.destinationPath, job.expectedSha1, job.expectedSize,
                    job.officialUrl, job.range);
}

void DownloadManager::pumpReuseQueue() {
    if (m_shuttingDown || m_reusePool == nullptr || !m_reuseCancel) {
        return;
    }
    // 池子的上限就是构造函数里设的 kMaxReuseWorkers（见 m_reusePool 的说明）：这里直接
    // 问池子，免得同一个数字在两处各写一遍。
    const int maxWorkers = m_reusePool->maxThreadCount();
    while (m_reuseActive < maxWorkers && !m_reuseQueue.isEmpty()) {
        const ReuseJob job = m_reuseQueue.dequeue();
        // 代次和取消令牌都在**开跑那一刻**拷一份：作业跑起来之后，主线程可能又开了新一批
        // （clearReuseJobs 会换令牌、加代次），那一条回来时按代次对不上就丢掉。
        const quint64 generation = m_reuseGeneration;
        const QSharedPointer<std::atomic_bool> cancel = m_reuseCancel;
        const QStringList roots = m_reuseSourceFolders;
        const QString currentRoot = AppPaths::minecraftDir();
        ++m_reuseActive;
        m_reusePool->start(QRunnable::create(
            [this, job, generation, cancel, roots, currentRoot]() {
                const LocalFileReuse::Check check =
                    job.mode == VerifyMode::ExistsOnly ? LocalFileReuse::Check::Exists
                                                       : LocalFileReuse::Check::SizeAndSha1;
                QString error;
                const LocalFileReuse::Method method = LocalFileReuse::reuseFrom(
                    job.destinationPath, job.relativePath, roots, currentRoot, job.expectedSha1,
                    job.expectedSize, check, cancel.data(), /*allowHardLink=*/true, &error);
                // 后台线程**只**算这一份结论，记账 / 发信号一律回主线程做：DownloadStats、
                // batchProgress 和 QObject 的信号都不是线程安全的。
                QMetaObject::invokeMethod(
                    this,
                    [this, job, generation, method, error]() {
                        if (m_shuttingDown) {
                            return;
                        }
                        if (m_reuseActive > 0) {
                            --m_reuseActive;
                        }
                        if (generation == m_reuseGeneration) {
                            onReuseFinished(job, method, error);
                        }
                        pumpReuseQueue();
                        checkAllDownloadsFinished();
                    },
                    Qt::QueuedConnection);
            }));
    }
}

void DownloadManager::onReuseFinished(const ReuseJob &job, LocalFileReuse::Method method,
                                      const QString &error) {
    const QString fileName = QFileInfo(job.destinationPath).fileName();
    if (method != LocalFileReuse::Method::None) {
        // 借成功**不进 total / done**（见 DownloadStats::reused）：进度条的分母是"真要下
        // 的那几个"，借来的这些一个字节都没走网络。
        ++m_reusedJobs;
        if (DownloadDiag::enabled()) {
            DownloadDiag::line(QStringLiteral("reuse hit dest=%1 method=%2")
                                   .arg(QDir::toNativeSeparators(job.destinationPath),
                                        method == LocalFileReuse::Method::HardLink
                                            ? QStringLiteral("hardlink")
                                            : QStringLiteral("copy")));
        }
        emit downloadFinished(fileName, true, QString());
        emitBatchProgress(job.destinationPath);
        checkAllDownloadsFinished();
        return;
    }

    // 借不到（来源没有、判据不过、复制到一半被取消或来源变了）。退回网络之前**再问一次
    // 盘上那份**：借文件这一小段时间里，另一个作业可能已经把同一个目标下完了
    // （整合包清单与版本 json 引用同一个库的那种情况）。问过就不会白下一次。
    if (fileAlreadyValid(job.destinationPath, job.expectedSha1, job.expectedSize, job.mode)) {
        ++m_skippedJobs;
        emit downloadFinished(fileName, true, QString());
        emitBatchProgress(job.destinationPath);
        checkAllDownloadsFinished();
        return;
    }

    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("reuse miss dest=%1 reason=%2")
                               .arg(QDir::toNativeSeparators(job.destinationPath), error));
    }
    // 到这一步才计进 m_totalJobs（见 ReuseJob 的说明）。range 恒为空：能进借文件队列的
    // 作业本来就不带 Range（见 scheduleFile 里那道闸）。
    queueNetworkJob(job.url, job.destinationPath, job.expectedSha1, job.expectedSize,
                    job.officialUrl, DownloadRange());
}

void DownloadManager::clearReuseJobs() {
    // 旧令牌置真 → 已经开跑的复制循环下一个 1 MiB 块就收手（找来源那一步每扫描一个候选
    // 也会看一次）。再换一个新的给下一批用：同一个布尔复用的话，取消过一次之后新的作业
    // 一启动就被判成"已取消"。
    if (m_reuseCancel) {
        m_reuseCancel->store(true, std::memory_order_relaxed);
    }
    m_reuseQueue.clear();
    m_reuseCancel = QSharedPointer<std::atomic_bool>::create(false);
    ++m_reuseGeneration;
    // NOTE: **不**碰 m_reuseActive。那几条还在池子里跑，它们回来时自己减（见
    // pumpReuseQueue 的完成回调）—— 在这里清零的话，池里的作业永远减不到零，
    // 下一批会以为还有 4 条在跑而再也不启动新的。
}

bool DownloadManager::precheckReadsFileBody(const QString &path, const QString &expectedSha1,
                                            qint64 expectedSize, VerifyMode mode,
                                            const DownloadRange &range) const {
    // 点名 / 一律重下：`fileAlreadyValid()` 头一句就挡掉，压根不看盘。
    if (mode == VerifyMode::AlwaysDownload || m_flaggedPaths.contains(path)) {
        return false;
    }
    const QFileInfo info(path);
    // 不在 / 不是文件 / 0 字节：老判据当场就回 false，一个字节都不用读。
    if (!info.exists() || !info.isFile() || info.size() == 0) {
        return false;
    }
    // 真要比 SHA-1：这一趟是整份读（大小一眼不符的那些不读，见 fileBodyValid）。
    if (mode != VerifyMode::ExistsOnly && !expectedSha1.isEmpty()
        && (expectedSize <= 0 || info.size() == expectedSize)) {
        return true;
    }
    // 登记原件指纹（`DownloadCheckpoint::fingerprint`，SHA-256）也是整份读。
    // 只有这一批带恢复记录才会登记；没有 checkpoint 的批次白读一遍没意义。
    return m_checkpoint != nullptr && !range.active();
}

void DownloadManager::pumpPrecheckQueue() {
    if (m_shuttingDown || m_precheckPool == nullptr || !m_precheckCancel) {
        return;
    }
    // 一次只放一条：这个活的每一件都是整份读文件，并排跑只会互相抢磁头
    // （见 m_precheckPool 的说明）。
    while (m_precheckActive < 1 && !m_precheckQueue.isEmpty()) {
        const PrecheckJob job = m_precheckQueue.dequeue();
        // 代次和取消令牌都在**开跑那一刻**拷一份：作业跑起来之后，主线程可能又开了新一批
        // （clearPrecheckJobs 会换令牌、加代次），那一条回来时按代次对不上就丢掉。
        const quint64 generation = m_precheckGeneration;
        const QSharedPointer<std::atomic_bool> cancel = m_precheckCancel;
        ++m_precheckActive;
        m_precheckPool->start(QRunnable::create(
            [this, job, generation, cancel]() {
                // 后台线程**只**算这两个结论，记账 / 发信号一律回主线程做：
                // DownloadStats、m_checkpoint 和 QObject 的信号都不是线程安全的。
                const bool valid = DownloadManager::fileBodyValid(
                    job.destinationPath, job.expectedSha1, job.expectedSize, job.mode, cancel.data());
                QJsonObject original;
                if (job.wantFingerprint) {
                    original = DownloadCheckpoint::fingerprint(job.destinationPath, cancel.data());
                }
                QMetaObject::invokeMethod(
                    this,
                    [this, job, generation, valid, original]() {
                        if (m_shuttingDown) {
                            return;
                        }
                        if (m_precheckActive > 0) {
                            --m_precheckActive;
                        }
                        if (generation == m_precheckGeneration) {
                            onPrecheckFinished(job, valid, original);
                        }
                        pumpPrecheckQueue();
                        checkAllDownloadsFinished();
                    },
                    Qt::QueuedConnection);
            }));
    }
}

void DownloadManager::onPrecheckFinished(const PrecheckJob &job, bool valid,
                                         const QJsonObject &original) {
    const QString fileName = QFileInfo(job.destinationPath).fileName();
    if (valid) {
        // 和 `scheduleFile()` 那一支逐字同一条账：只记 skipped、不进 done/total。
        ++m_skippedJobs;
        emit downloadFinished(fileName, true, QString());
        emitBatchProgress(job.destinationPath);
        // 收尾由调用方（pumpPrecheckQueue 的完成回调）紧接着做：那里还要把下一条
        // 问盘作业放出去，收尾必须排在它**后面** —— 否则这一条刚减完就要发
        // allDownloadsFinished，而队列里明明还有几条没问。
        return;
    }
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("precheck miss dest=%1 mode=%2")
                               .arg(QDir::toNativeSeparators(job.destinationPath))
                               .arg(job.mode == VerifyMode::ExistsOnly ? QStringLiteral("exists")
                                    : job.mode == VerifyMode::AlwaysDownload ? QStringLiteral("always")
                                                                             : QStringLiteral("size-sha1")));
    }
    continueAfterPrecheck(job, &original);
}

void DownloadManager::clearPrecheckJobs() {
    // 旧令牌置真：正在读的那一条读完这一个块就收手（指纹那一路每个 1 MiB 看一次；
    // SHA-1 那一路没有取消点，一格粒度是"一份文件"，读完按代次丢掉结论）。
    // 换一个新的给下一批用：同一个布尔复用的话，取消过一次之后新的作业一开跑就判成"已取消"。
    if (m_precheckCancel) {
        m_precheckCancel->store(true, std::memory_order_relaxed);
    }
    m_precheckQueue.clear();
    m_precheckCancel = QSharedPointer<std::atomic_bool>::create(false);
    ++m_precheckGeneration;
    // NOTE: **不**碰 m_precheckActive，理由同 clearReuseJobs()。
}
