#pragma once
#include "RequestBudget.hpp"
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace RequestBudget {
// 异步元数据 GET：排队和传输共用一个截止，独立 QNAM 共用 Control 额度。
// 删除作业或 owner 即取消；取消不回调，也不允许排队作业在稍后偷偷发出。
class ControlGet final : public QObject {
public:
    using Completion = std::function<void(QNetworkReply *, const QString &)>;
    static ControlGet *start(QObject *owner, QNetworkAccessManager *manager,
                             const QNetworkRequest &request, int timeoutMs, Completion completion) {
        return new ControlGet(owner, manager, request, timeoutMs, std::move(completion));
    }
    ~ControlGet() override {
        if (m_reply) {
            QObject::disconnect(m_reply, nullptr, this, nullptr);
            if (!m_reply->isFinished()) m_reply->abort();
            m_reply->deleteLater();
        }
        if (m_lease) m_lease->release();
    }
private:
    ControlGet(QObject *owner, QNetworkAccessManager *manager, QNetworkRequest request,
               int timeoutMs, Completion completion)
        : QObject(owner), m_manager(manager), m_request(std::move(request)),
          m_timeout(qMax(1, timeoutMs)), m_completion(std::move(completion)) {
        if (manager) connect(manager, &QObject::destroyed, this, [this] {
            // 管理器常在 owner 的析构中先释放；不向已开始析构的业务对象回调。
            m_done = true;
            m_completion = {};
            m_reply = nullptr;
            m_poll.stop(); m_deadline.stop();
            if (m_lease) m_lease->release();
            deleteLater();
        });
        m_elapsed.start();
        m_poll.setInterval(25);
        m_deadline.setSingleShot(true);
        connect(&m_poll, &QTimer::timeout, this, [this] { dispatch(); });
        connect(&m_deadline, &QTimer::timeout, this, [this] {
            if (m_reply && !m_reply->isFinished()) {
                m_reply->setProperty("wtfmlControlDeadlineExpired", true);
                m_reply->abort();
            }
            else finish(nullptr, tr("等待网络请求超时。"));
        });
        m_deadline.start(m_timeout);
        QTimer::singleShot(0, this, [this] { dispatch(); });
    }
    void dispatch() {
        if (m_done || m_reply) return;
        if (!m_manager) { finish(nullptr, tr("网络请求已取消。")); return; }
        const int remaining = m_timeout - int(m_elapsed.elapsed());
        if (remaining <= 0) { finish(nullptr, tr("等待网络请求超时。")); return; }
        auto lease = tryAcquire(Lane::Control);
        if (!lease) { m_poll.start(); return; }
        m_poll.stop();
        m_lease = std::move(lease);
        // 传输空闲截止不能比整项剩余的时间更长。
        m_request.setTransferTimeout(remaining);
        auto *reply = m_manager->get(m_request);
        m_reply = reply;
        reply->setProperty("wtfmlControlQueuedMs", m_elapsed.elapsed());
        attach(reply, m_lease);
        connect(reply, &QNetworkReply::readyRead, this, [reply] {
            if (reply->bytesAvailable() > 2 * 1024 * 1024) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply] { finish(reply, {}); });
        connect(reply, &QObject::destroyed, this, [this] {
            m_reply = nullptr;
            if (m_lease) m_lease->release();
            // QNAM 先删回复，再发自身 destroyed；下一轮才能确认业务 owner 仍然有效。
            QTimer::singleShot(0, this, [this] { finish(nullptr, tr("网络请求已取消。")); });
        });
        if (reply->isFinished()) QTimer::singleShot(0, this, [this, reply] { finish(reply, {}); });
    }
    void finish(QNetworkReply *reply, const QString &error) {
        if (m_done) return;
        m_done = true;
        m_poll.stop(); m_deadline.stop();
        if (m_lease) m_lease->release();
        const QPointer<ControlGet> alive(this);
        auto completion = std::move(m_completion);
        if (completion) completion(reply, error);
        if (alive) deleteLater();
    }
    QPointer<QNetworkAccessManager> m_manager;
    QPointer<QNetworkReply> m_reply;
    QNetworkRequest m_request;
    int m_timeout;
    Completion m_completion;
    Lease m_lease;
    QElapsedTimer m_elapsed;
    QTimer m_poll, m_deadline;
    bool m_done = false;
};
}
