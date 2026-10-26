#pragma once

#include <QMutex>
#include <QMutexLocker>
#include <QNetworkReply>
#include <memory>

// Per-process in-flight HTTP request budget (not a TCP socket count). Large-file
// executors share Bulk; short control requests have a separate reserved lane.
// Neither acquiring nor releasing waits for another request or calls user code.
namespace RequestBudget {
enum class Lane { Bulk, Control };
struct State {
    QMutex mutex;
    int bulkLimit = 24;
    int bulk = 0;
    int control = 0;
};
inline State &state() { static State value; return value; }
struct Snapshot { int limit; int bulk; int control; };
inline Snapshot snapshot() {
    auto &s = state();
    QMutexLocker lock(&s.mutex);
    return {s.bulkLimit, s.bulk, s.control};
}
inline void setBulkLimit(int limit) {
    auto &s = state();
    QMutexLocker lock(&s.mutex);
    s.bulkLimit = qBound(1, limit, 128);
    // Lowering the limit drains existing replies; never abort valid work.
}
class Ticket final {
public:
    explicit Ticket(Lane lane) : m_lane(lane) {}
    ~Ticket() { release(); }
    Ticket(const Ticket &) = delete;
    Ticket &operator=(const Ticket &) = delete;
    void release() {
        auto &s = state();
        QMutexLocker lock(&s.mutex);
        if (m_released) return;
        m_released = true;
        --(m_lane == Lane::Bulk ? s.bulk : s.control);
    }
private:
    Lane m_lane;
    bool m_released = false;
};
using Lease = std::shared_ptr<Ticket>;
inline Lease tryAcquire(Lane lane) {
    auto &s = state();
    QMutexLocker lock(&s.mutex);
    int &active = lane == Lane::Bulk ? s.bulk : s.control;
    if (active >= (lane == Lane::Bulk ? s.bulkLimit : 4)) return {};
    auto ticket = std::make_shared<Ticket>(lane);
    ++active;
    return ticket;
}
inline void attach(QNetworkReply *reply, const Lease &lease) {
    // Release before downstream finished handlers pump their queues. Destruction
    // of an unfinished reply also releases through the captured Ticket's dtor.
    QObject::connect(reply, &QNetworkReply::finished, reply, [lease] { lease->release(); });
    if (reply->isFinished()) lease->release();
}
} // namespace RequestBudget
