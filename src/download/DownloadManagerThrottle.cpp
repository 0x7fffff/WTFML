/**
 * @file DownloadManagerThrottle.cpp
 * @brief 下载限速：令牌桶怎么续、往每条回复里喂多少、切档时怎么收尾。
 *
 * 这一份状态（`m_speedBucket` / `m_speedPump` / `m_throttledReplies`）只被这几条函数动：
 * 设置页推过来的新档位、分段作业借桶、`readyRead` 问"这一路最多预读多少"、泵每 100 ms
 * 放一次字节。搬出 `DownloadManager.cpp` 是为了让"一条回复怎么流动"和"字节按什么速率放行"
 * 各归各的翻译单元 —— 两边的定时器、账目互不干扰。
 */

#include "DownloadManager.hpp"

#include "ResumableDownload.hpp"
#include "StreamedFile.hpp"

#include "../util/DownloadDiagnostics.hpp"

#include <QDebug>
#include <QNetworkReply>
#include <QTimer>

void DownloadManager::shareSpeedLimitBucketWith(DownloadManager *owner) {
    if (owner == nullptr || owner == this || owner->m_speedBucket.isNull()) {
        return;
    }
    if (m_speedBucket == owner->m_speedBucket) {
        return;  // 已经在共用了
    }
    // 已经借的那一份（如果有）先还掉计数，再借新的。
    if (m_speedBucket && m_speedBucket->sharers > 0 && m_speedBucketBorrowed) {
        --m_speedBucket->sharers;
    }
    m_speedBucket = owner->m_speedBucket;
    m_speedBucket->sharers += 1;
    m_speedBucketBorrowed = true;
    m_speedLimitBytesPerSecond = m_speedBucket->limit;
    if (m_speedBucket->limit > 0) {
        m_speedPump->start();
    }
}

void DownloadManager::setSpeedLimitBytesPerSecond(qint64 bytesPerSecond) {
    // 归一化在判据层（唯一出处，见 DownloadSpeedLimit.hpp）：0 = 不限；越界回 0。
    const qint64 normalized = DownloadSpeedLimit::normalize(bytesPerSecond);
    const bool changed = normalized != m_speedBucket->limit;
    m_speedLimitBytesPerSecond = normalized;
    // 写的是**桶**里的那份（分段作业的内部下载器共用它）。内层那个对象
    // 自己的 `m_speedLimitBytesPerSecond` 也在这里跟着更新 —— 外层改档时会把新值转发给
    // 每个分段作业，走到这里时桶已经是新值了（changed=false），但成员必须照写，不然
    // 内层 readyRead / 看门狗读到的还是旧档。
    m_speedBucket->limit = normalized;
    if (normalized > 0) {
        if (changed) {
            // 从零开始：用户刚设完不该先看到"猛冲一下"（burst 只用来吸收定时器抖动）。
            m_speedBucket->tokens = 0;
            m_speedBucket->lastRefillMs = m_speedBucket->clock.elapsed();
        }
        m_speedPump->start();
    } else {
        m_speedPump->stop();
        // 从"限速"切回"不限"：攒在缓冲区里的那些**当场**读完，不等到下一拍。
        // 快照的理由：写盘出错时下面会 abort，abort 可能同步走 finished、改这张表。
        //
        // 这一步**不看 changed**：内层那个共用桶的下载器被外层转发 0 的时候，桶里已经是 0 了
        // （changed=false），但它自己缓冲区里同样攒着人 —— 泵一停，那些回复就再也没人读了。
        const QList<QNetworkReply *> waiting = m_throttledReplies.values();
        m_throttledReplies.clear();
        for (QNetworkReply *reply : waiting) {
            const auto stream = m_streamedFiles.value(reply);
            if (!stream) {
                continue;
            }
            stream->drain(reply);
            if (!stream->error.isEmpty()) {
                reply->abort();
            }
        }
        if (changed) m_speedBucket->tokens = 0;
    }
    // 分段作业（大于 4 MiB 那些）各自有一份内部下载器，绕在我们这张表之外 —— 已经在跑的
    // 也要当场改，不然"设置里设成 1 MB/s 了，大文件还在满速"（见 ResumableDownload 那个 setter）。
    if (changed) {
        const QList<ResumableDownload *> segmentTasks = m_segmentJobs.keys();
        for (ResumableDownload *task : segmentTasks) {
            if (task != nullptr) {
                task->setSpeedLimitBytesPerSecond(normalized);
            }
        }
    }
    if (DownloadDiag::enabled()) {
        DownloadDiag::line(QStringLiteral("speedlimit set request=%1 normalized=%2 inflight=%3 "
                                          "waiting=%4")
                               .arg(bytesPerSecond)
                               .arg(normalized)
                               .arg(activeDownloads.size())
                               .arg(m_throttledReplies.size()));
    }
    // Turning the limit off also stops its timer. Jobs waiting on token debt
    // must be woken here even when no reply is still active to wake the queue.
    pumpDownloadQueue();
}

qint64 DownloadManager::throttledReadBufferBytes() const {
    // 一拍的总量 = limit × 0.1 s；平分给**当下真在飞**的每一路，再给两拍 —— 留一拍吸收
    // 定时器迟到与"读走之后 QNAM 还没收到下一段"的空档，不会把速率拖到上限以下。
    //
    // NOTE: 分母是 activeDownloads.size()，不是 m_maxConcurrentDownloads 那一档。按上限
    // 分的话，只开一两条作业时每条只能分到几 KB，速率会掉到远低于设定值（实测
    // 2 × 5 MiB、限 8 MiB/s：1.2 MiB/s）。这条回复自己此刻还没进表，所以 +1。
    const int lanes = qMax(1, int(activeDownloads.size()) + 1);
    const qint64 tickBudget = m_speedBucket->limit / (1000 / kSpeedPumpMs);
    const qint64 perLane = tickBudget / lanes * 2;
    // 高水位还要**盖住这一条一拍该拿到的量**：一拍只读得到缓冲区里那点字节（下一拍才再读
    // 一次），比它小的话速率就跟设定值脱钩了 —— 实测 2 × 5 MiB、限 8 MiB/s、每条封在
    // 256 KiB，跑成 4.5 MiB/s。所以上限放到"整条带宽的 32 MiB"这一档：只有限速超过
    // ~160 MiB/s（手改值、预设里没有）时它才会真的钳住。
    const qint64 cap = qMax<qint64>(qint64(256) * 1024, qint64(32) * 1024 * 1024 / lanes);
    return qBound<qint64>(qint64(8) * 1024, perLane, cap);
}

void DownloadManager::onSpeedPumpTick() {
    const qint64 limit = m_speedBucket->limit;
    if (m_shuttingDown || limit <= 0) {
        return;
    }
    const qint64 now = m_speedBucket->clock.elapsed();
    const qint64 elapsed = qMax<qint64>(0, now - m_speedBucket->lastRefillMs);
    m_speedBucket->lastRefillMs = now;
    const double capacity = double(DownloadSpeedLimit::burstBytes(limit));
    m_speedBucket->tokens = qMin(
        capacity, m_speedBucket->tokens + double(limit) * double(elapsed) / 1000.0);
    if (m_throttledReplies.isEmpty()) {
        // 队列里还蓄着作业（欠账 / 限速拦下的那些）：余额就是给它们攒的，**别抹**，
        // 先按余额发车。顺序反过来（先抹零再发车）会死锁：没有在飞的回复 ⇒ 每拍
        // 余额刚补上就被抹掉 ⇒ 永远发不出第一条作业。
        if (!m_pendingJobs.isEmpty()) {
            pumpDownloadQueue();
            return;
        }
        // 真没有人在等：余额也别攒（不然下一批一开始就会冲一个 burst）。
        // 桶是共用的时候**不许**抹 —— 这个下载器这一拍没人等，不代表共用它的
        // 那个（分段作业内部那个）也没人等；抹掉的话它那边的节奏会被这里每 100 ms 打回零。
        //
        // **只抹正的余额**：负数是已经写出去的字节（onDownloadReplyFinished 收尾那一口
        // 记的账），不是"攒下的额度"—— 抹掉它等于把刚绕过桶的那一段直接赦免掉。
        if (m_speedBucket->sharers <= 0 && m_speedBucket->tokens > 0) {
            m_speedBucket->tokens = 0;
        }
        return;
    }
    // 列"确实还有字节等着"的那些。两拍之间回复可能已经结束 —— 结束那一路自己会摘表，
    // 这里再判一次 isReadable，双保险（摘漏了的也不会被 read 到一个关掉的设备）。
    QList<QNetworkReply *> waiting;
    const QList<QNetworkReply *> registered = m_throttledReplies.values();
    waiting.reserve(registered.size());
    for (QNetworkReply *reply : registered) {
        if (!reply->isReadable() || reply->bytesAvailable() <= 0) {
            m_throttledReplies.remove(reply);
            continue;
        }
        waiting.append(reply);
    }
    // 平均分摊：每个等着的人最多拿"余额 ÷ 还剩几个人"。谁手上没那么多字节，余量自然
    // 落到后面几个人身上（remaining 是先减后用的）。一轮就够 —— 100 ms 之后再来一拍。
    int remaining = waiting.size();
    for (QNetworkReply *reply : waiting) {
        if (remaining <= 0 || m_speedBucket->tokens < 1.0) {
            break;
        }
        const qint64 share = qint64(m_speedBucket->tokens / double(remaining));
        --remaining;
        if (share <= 0) {
            continue;
        }
        const auto stream = m_streamedFiles.value(reply);
        if (!stream) {
            m_throttledReplies.remove(reply);
            continue;
        }
        const qint64 taken = stream->drainUpTo(reply, share);
        m_speedBucket->tokens -= double(taken);
        if (!stream->error.isEmpty()) {
            m_throttledReplies.remove(reply);
            reply->abort();
        } else if (reply->bytesAvailable() <= 0) {
            m_throttledReplies.remove(reply);
        }
    }
    // 这一拍放出了一些字节 ⇒ 欠账可能还清了，把因为欠账停住的作业接着发。
    pumpDownloadQueue();
}
