#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>

// Read-only jobs: one physical worker and one replaceable pending request.
// Invalidating discards results, without interrupting a filesystem operation.
template<class Request, class Result>
class LatestReadLoader : public QObject {
public:
    using Reader = std::function<Result(const Request &)>;
    using Completed = std::function<void(const Result &)>;
    LatestReadLoader(QObject *parent, Reader reader) : QObject(parent), m_reader(std::move(reader)) {}
    bool isLoading() const { return m_pending; }
    bool isReading() const { return m_active; }
    void invalidate() { ++m_generation; m_pending = false; m_completed = {}; }
    void load(const Request &request, Completed completed) {
        if (m_pending && m_request == request) return;
        ++m_generation;
        m_pending = true;
        m_request = request;
        m_completed = std::move(completed);
        if (!m_active) start();
    }
private:
    void start() {
        m_active = true;
        const auto generation = m_generation;
        auto *watcher = new QFutureWatcher<Result>(this);
        QObject::connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, generation] {
            const auto result = watcher->result();
            watcher->deleteLater();
            m_active = false;
            if (generation != m_generation) {
                if (m_pending) start();
                return;
            }
            m_pending = false;
            auto completed = std::move(m_completed);
            if (completed) completed(result);
        });
        watcher->setFuture(QtConcurrent::run([request = m_request, reader = m_reader] { return reader(request); }));
    }
    Reader m_reader;
    Completed m_completed;
    Request m_request{};
    quint64 m_generation = 0;
    bool m_active = false;
    bool m_pending = false;
};
