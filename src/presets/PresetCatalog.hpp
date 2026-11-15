#pragma once

#include "PresetStore.hpp"
#include <QFutureWatcher>
#include <QObject>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>

namespace Presets {
struct CatalogSnapshot {
    QString root;
    QList<PresetSummary> presets;
    QList<BrokenPreset> broken;
};

inline CatalogSnapshot readCatalog(const QString &root) {
    CatalogSnapshot result;
    result.root = root;
    result.presets = list(root, &result.broken);
    return result;
}

// The worker owns only its captured root/reader, never the controller or widgets.
// Invalidating a request discards its result; it does not interrupt a file read.
class CatalogLoader final : public QObject {
public:
    using Reader = std::function<CatalogSnapshot(const QString &)>;
    explicit CatalogLoader(QObject *parent, Reader reader = {})
        : QObject(parent), m_reader(reader ? std::move(reader) : Reader(readCatalog)) {}

    bool isLoading() const { return m_pending; }
    void invalidate() { ++m_generation; m_pending = false; m_waiters.clear(); }
    void load(const QString &root, std::function<void(const CatalogSnapshot &)> completed) {
        // 同一份目录已经在读：排进同一趟的等待队列，别再读一遍。预设页和安装页那张
        // 预设弹窗会同时要这一份（点弹窗的时候预设页可能正在刷新）。
        if (m_pending && m_root == root) {
            m_waiters.append(std::move(completed));
            return;
        }
        m_pending = true;
        m_root = root;
        const auto generation = ++m_generation;
        m_waiters = {std::move(completed)};
        auto *watcher = new QFutureWatcher<CatalogSnapshot>(this);
        connect(watcher, &QFutureWatcher<CatalogSnapshot>::finished, this,
                [this, watcher, generation] {
            const auto snapshot = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation) return;
            m_pending = false;
            // 先摘下这一趟的等待者再逐个叫：等待者里可能有人当场又发一次 `load()`
            // （比如预设页发现根变了），那一发要排进新的队列，不能跟着这一趟被清掉。
            auto waiters = std::move(m_waiters);
            m_waiters.clear();
            for (auto &waiter : waiters) {
                if (waiter) waiter(snapshot);
            }
        });
        watcher->setFuture(QtConcurrent::run([root, reader = m_reader] {
            auto snapshot = reader(root);
            snapshot.root = root;
            return snapshot;
        }));
    }
private:
    Reader m_reader;
    QString m_root;
    quint64 m_generation = 0;
    bool m_pending = false;
    /// 这一趟读完要叫的那几位（同一份目录的并发请求合并到一趟）。
    QList<std::function<void(const CatalogSnapshot &)>> m_waiters;
};
} // namespace Presets
