#pragma once

#include "PresetStore.hpp"
#include <QFutureWatcher>
#include <QObject>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>

namespace Presets {
struct ManifestSnapshot {
    QString root;
    QString id;
    PresetManifest manifest;
    QString problem;
    bool ok = false;
};

inline ManifestSnapshot readManifestSnapshot(const QString &root, const QString &id) {
    ManifestSnapshot snapshot;
    snapshot.root = root;
    snapshot.id = id;
    snapshot.manifest = loadManifest(root, id, &snapshot.ok, &snapshot.problem);
    return snapshot;
}

// One physical read at a time. Rapid selection changes replace the pending read
// instead of queuing obsolete manifests behind a slow disk.
class ManifestLoader final : public QObject {
public:
    using Reader = std::function<ManifestSnapshot(const QString &, const QString &)>;
    using Completed = std::function<void(const ManifestSnapshot &)>;
    explicit ManifestLoader(QObject *parent, Reader reader = {})
        : QObject(parent), m_reader(reader ? std::move(reader) : Reader(readManifestSnapshot)) {}

    bool isLoading() const { return m_pending; }
    bool isReading() const { return m_active; }
    void invalidate() {
        ++m_generation;
        m_pending = false;
        m_completed = {};
    }
    void load(const QString &root, const QString &id, Completed completed) {
        if (m_pending && m_root == root && m_id == id) return;
        ++m_generation;
        m_pending = true;
        m_root = root;
        m_id = id;
        m_completed = std::move(completed);
        if (!m_active) start();
    }

private:
    void start() {
        m_active = true;
        const auto generation = m_generation;
        auto *watcher = new QFutureWatcher<ManifestSnapshot>(this);
        connect(watcher, &QFutureWatcher<ManifestSnapshot>::finished, this,
                [this, watcher, generation] {
            const auto snapshot = watcher->result();
            watcher->deleteLater();
            m_active = false;
            if (generation != m_generation) {
                if (m_pending) start();
                return;
            }
            m_pending = false;
            auto completed = std::move(m_completed);
            if (completed) completed(snapshot);
        });
        watcher->setFuture(QtConcurrent::run([root = m_root, id = m_id, reader = m_reader] {
            auto snapshot = reader(root, id);
            snapshot.root = root;
            snapshot.id = id;
            return snapshot;
        }));
    }
    Reader m_reader;
    Completed m_completed;
    QString m_root;
    QString m_id;
    quint64 m_generation = 0;
    bool m_active = false;
    bool m_pending = false;
};
} // namespace Presets
