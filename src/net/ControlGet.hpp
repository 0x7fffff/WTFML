#pragma once
#include "RequestBudget.hpp"
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace RequestBudget {
/// 截止到点、请求被这条路径自己掐掉时打在回复上的标记（`finished` 里要分得清
/// "到点收手"和"别的原因 abort"）。字符串是诊断属性，测试与业务两侧都认这一个名字。
inline constexpr char kDeadlineExpiredProperty[] = "wtfmlControlDeadlineExpired";
/// 正文上限的出厂值：元数据（版本清单、加载器列表……）没有一份该超过这个数。
inline constexpr int kDefaultBodyLimit = 2 * 1024 * 1024;

// 异步元数据请求（GET，以及带一小段 JSON 正文的 POST）：排队和传输共用一个截止，
// 独立 QNAM 共用 Control 额度。
// 删除作业或 owner 即取消；取消不回调，也不允许排队作业在稍后偷偷发出。
class ControlGet final : public QObject {
public:
    using Completion = std::function<void(QNetworkReply *, const QString &)>;
    /// @param maxBodyBytes 正文最多收多少；超了当场 abort（这一发按失败收场，不会把
    ///        半个 JSON 交给解析器）。少数接口（平台兼容查询的版本表带着 changelog 正文）
    ///        天然比 2 MiB 大，由调用方显式抬高。
    static ControlGet *start(QObject *owner, QNetworkAccessManager *manager,
                             const QNetworkRequest &request, int timeoutMs, Completion completion,
                             int maxBodyBytes = kDefaultBodyLimit) {
        return new ControlGet(owner, manager, request, timeoutMs, std::move(completion),
                              qMax(1, maxBodyBytes), {}, false);
    }
    /// 带正文的那一发。只给"拿一批元数据"的 POST 用（CurseForge 的 `/mods`、
    /// `/mods/files`，Modrinth 的 `/version_files`）：正文按值存进作业，作业删掉就没了；
    /// `Content-Type` 仍由调用方设在 `request` 上。
    static ControlGet *startPost(QObject *owner, QNetworkAccessManager *manager,
                                 const QNetworkRequest &request, const QByteArray &body,
                                 int timeoutMs, Completion completion,
                                 int maxBodyBytes = kDefaultBodyLimit) {
        return new ControlGet(owner, manager, request, timeoutMs, std::move(completion),
                              qMax(1, maxBodyBytes), body, true);
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
               int timeoutMs, Completion completion, int maxBodyBytes, QByteArray body,
               bool isPost)
        : QObject(owner), m_manager(manager), m_request(std::move(request)),
          m_timeout(qMax(1, timeoutMs)), m_maxBodyBytes(maxBodyBytes),
          m_completion(std::move(completion)), m_body(std::move(body)), m_isPost(isPost) {
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
            // 到点时回复可能已经完成，只是 `finished` 还没派送到我们这一格（同一个事件
            // 循环里定时器先跑）。那一份正文是完整的，按正常收尾交出去；拿"超时"把已经
            // 到手的响应判死，会让上层看到一次并不存在的网络失败。
            if (m_reply && m_reply->isFinished()) {
                finish(m_reply, {});
                return;
            }
            if (m_reply) {
                m_reply->setProperty(kDeadlineExpiredProperty, true);
                m_reply->abort();
                return;
            }
            finish(nullptr, tr("等待网络请求超时。"));
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
        auto *reply = m_isPost ? m_manager->post(m_request, m_body) : m_manager->get(m_request);
        m_reply = reply;
        reply->setProperty("wtfmlControlQueuedMs", m_elapsed.elapsed());
        attach(reply, m_lease);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
            if (reply->bytesAvailable() > m_maxBodyBytes) reply->abort();
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
    int m_maxBodyBytes;
    Completion m_completion;
    QByteArray m_body;
    bool m_isPost;
    Lease m_lease;
    QElapsedTimer m_elapsed;
    QTimer m_poll, m_deadline;
    bool m_done = false;
};
}
