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
    void invalidate() { ++m_generation; m_pending = false; }
    void load(const QString &root, std::function<void(const CatalogSnapshot &)> completed) {
        if (m_pending && m_root == root) return;
        m_pending = true;
        m_root = root;
        const auto generation = ++m_generation;
        auto *watcher = new QFutureWatcher<CatalogSnapshot>(this);
        connect(watcher, &QFutureWatcher<CatalogSnapshot>::finished, this,
                [this, watcher, generation, completed = std::move(completed)] {
            const auto snapshot = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation) return;
            m_pending = false;
            completed(snapshot);
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
};
} // namespace Presets
