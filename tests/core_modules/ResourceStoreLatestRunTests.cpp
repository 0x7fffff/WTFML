#include "Support.hpp"

#include "download/ResourceStore.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

// ---------------------------------------------------------------------------
// R1. 更新检查那个"批次表"（`ResourceStore::LatestRun`）的收场（R1）
//
// 这一批是"分几发出去、合并着回一发信号"。每一发跑完都会 `deleteLater()` 把自己收掉，
// 所以批次表里那一格从那一刻起就**不能**再当成一个活对象。旧代码存的是裸指针、而且没人
// 摘：一发已经回来、别的还在飞的时候——
//   ① 用户再点一次「检查更新」（同一把钥匙重建批次）：对已析构的作业 `delete`；
//   ② 关窗（`~ResourceStore` → `abortAll()`）：对同一格再删一次。
// 两条都是二次析构（堆损坏 / 退出崩溃），而现有用例（`main.cpp` 那一发）只发一次并等
// 自然结束，正好绕开。
//
// 夹具是本机一个真实 TCP 端口：地址全部由 override 指过来，一个字节都不碰外网。
// 假接口可以"押着不回"，这样才能稳定造出"一发已回、一发在飞"那个瞬间。
// ---------------------------------------------------------------------------

namespace {

/// 假接口：只接 `POST /version_files/update`，按 `setAnswerLimit()` 决定前几发直接答、
/// 其余押着不回（`releaseHeld()` 再放）。
class HeldUpdateApi {
public:
    bool start() {
        if (!m_server.listen(QHostAddress::LocalHost)) {
            return false;
        }
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            while (auto *socket = m_server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    if (socket->property("answered").toBool()) {
                        socket->readAll();  // 已经答过了，剩下的字节丢掉
                        return;
                    }
                    QByteArray buffer = socket->property("buffer").toByteArray();
                    buffer += socket->readAll();
                    socket->setProperty("buffer", buffer);
                    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
                    if (headerEnd < 0) {
                        return;
                    }
                    int contentLength = 0;
                    for (const QByteArray &line : buffer.left(headerEnd).split('\n')) {
                        const QByteArray trimmed = line.trimmed();
                        if (trimmed.toLower().startsWith("content-length:")) {
                            contentLength =
                                trimmed.mid(int(qstrlen("content-length:"))).trimmed().toInt();
                        }
                    }
                    if (buffer.size() < headerEnd + 4 + contentLength) {
                        return;
                    }
                    socket->setProperty("buffer", QByteArray());
                    m_requests.append(buffer.left(buffer.indexOf("\r\n")));
                    if (m_answered < m_answerLimit) {
                        ++m_answered;
                        answer(socket);
                    } else {
                        // 押着：记下这个 socket，等这一轮要造的那个瞬间过去再说。
                        socket->setProperty("answered", true);
                        m_held.append(QPointer<QTcpSocket>(socket));
                    }
                });
            }
        });
        return true;
    }

    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    /// 前 `limit` 发直接答，之后到的都押着。默认不押（全答）。
    void setAnswerLimit(int limit) { m_answerLimit = limit; }
    /// 回答里带哪几个哈希（合并之后有多少条）。
    void setAnswerHashes(const QStringList &sha1s) { m_payload = jsonFor(sha1s); }
    void releaseHeld() {
        for (const QPointer<QTcpSocket> &socket : m_held) {
            if (socket) {
                answer(socket);
            }
        }
        m_held.clear();
    }
    int requestCount() const { return m_requests.size(); }
    void clear() { m_requests.clear(); }

private:
    static QByteArray jsonFor(const QStringList &sha1s) {
        QJsonObject object;
        for (const QString &sha1 : sha1s) {
            const QJsonObject file{{QStringLiteral("hashes"),
                                    QJsonObject{{QStringLiteral("sha1"), sha1}}},
                                   {QStringLiteral("url"), QStringLiteral("http://127.0.0.1/x.jar")},
                                   {QStringLiteral("filename"), QStringLiteral("x.jar")},
                                   {QStringLiteral("size"), 10}};
            object.insert(sha1,
                          QJsonObject{{QStringLiteral("id"), QStringLiteral("V1")},
                                      {QStringLiteral("version_number"), QStringLiteral("2.0")},
                                      {QStringLiteral("game_versions"),
                                       QJsonArray{QStringLiteral("1.20.1")}},
                                      {QStringLiteral("loaders"),
                                       QJsonArray{QStringLiteral("fabric")}},
                                      {QStringLiteral("files"), QJsonArray{file}}});
        }
        return QJsonDocument(object).toJson(QJsonDocument::Compact);
    }

    void answer(QTcpSocket *socket) {
        socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(m_payload.size()) +
                      "\r\nConnection: close\r\n\r\n");
        socket->write(m_payload);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QList<QByteArray> m_requests;
    QList<QPointer<QTcpSocket>> m_held;
    QByteArray m_payload = QByteArrayLiteral("{}");
    int m_answered = 0;
    int m_answerLimit = 1 << 30;
};

/// 150 个互不相同的 40 位哈希：`kLatestChunk = 100` ⇒ 正好两发。
QStringList updateHashes() {
    QStringList hashes;
    for (int i = 0; i < 150; ++i) {
        hashes.append(QStringLiteral("%1").arg(i + 1, 40, 10, QLatin1Char('0')));
    }
    return hashes;
}

} // namespace

void runResourceStoreLatestRunTests() {
    section("R1. 更新检查批次表：作业自己收场之后不能再被删一次（R1）");
    HeldUpdateApi api;
    if (!api.start()) {
        check(false, "R1 本地假接口起来了");
        return;
    }
    check(true, "R1 本地假接口起来了");

    const auto pump = [](int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    };
    const auto pumpUntil = [&](auto done, int ms) {
        QElapsedTimer clock;
        clock.start();
        while (!done() && clock.elapsed() < ms) {
            pump(20);
        }
        return done();
    };

    const QStringList hashes = updateHashes();
    api.setAnswerHashes(hashes);

    // ---- R1-1 一发已回、一发在飞：同一把钥匙重开一批 ----------------------------
    {
        // 第 1 发直接答、第 2 发押着 —— 这一刻"一发已收场、一发还在飞"就出来了。
        api.setAnswerLimit(1);
        ResourceStore store;
        store.setModrinthApiBaseOverride(api.base());
        int finished = 0;
        QHash<QString, ResourceCatalog::VersionEntry> answer;
        QObject::connect(&store, &ResourceStore::latestVersionsFinished, &store,
                         [&](const QString &, const QHash<QString, ResourceCatalog::VersionEntry> &got,
                             const QString &) {
                             ++finished;
                             answer = got;
                         });

        store.fetchLatestVersions(QStringLiteral("zj|rebuild"), hashes, QStringLiteral("1.20.1"),
                                  QStringLiteral("fabric"));
        const bool bothSent = pumpUntil([&] { return api.requestCount() >= 2; }, 5000);
        check(bothSent, "R1-1 两发都发出去了（一发的答案已经到、另一发还押在接口那边）",
              QStringLiteral("发出 %1 发").arg(api.requestCount()));
        // 让第 1 发的完成回调用完、作业把 `deleteLater()` 排进去并且执行掉 ——
        // 那之后它在批次表里那一格就是**已经析构的对象**（旧代码的症状从这一步开始）。
        pump(300);

        // 同一把钥匙上再开一批：旧那一批（含已收场的那一发）要被掐掉。
        api.setAnswerLimit(1 << 30);
        store.fetchLatestVersions(QStringLiteral("zj|rebuild"), hashes, QStringLiteral("1.20.1"),
                                  QStringLiteral("fabric"));
        const bool done = pumpUntil([&] { return finished > 0; }, 10000);
        check(done && finished == 1,
              "★ R1-1 同一把钥匙重建批次：新那一批照常收尾（旧的、已经自己收场的那一发"
              "没有二次析构）",
              QStringLiteral("收尾 %1 次、合并 %2 条").arg(finished).arg(answer.size()));
        check(answer.size() == hashes.size(),
              "R1-1 重建之后拿到的还是整批合起来的结果（两发都合并进来了）",
              QStringLiteral("%1 / %2").arg(answer.size()).arg(hashes.size()));
    }

    // ---- R1-2 一发已回、一发在飞：直接关窗（析构 ResourceStore） ----------------
    {
        api.clear();
        api.setAnswerLimit(1);
        auto *store = new ResourceStore;
        store->setModrinthApiBaseOverride(api.base());
        store->fetchLatestVersions(QStringLiteral("zj|abort"), hashes, QStringLiteral("1.20.1"),
                                   QStringLiteral("fabric"));
        const bool bothSent = pumpUntil([&] { return api.requestCount() >= 2; }, 5000);
        check(bothSent, "R1-2 关窗前两发都发出去了", QStringLiteral("发出 %1 发").arg(api.requestCount()));
        pump(300);
        // `~ResourceStore` → `abortAll()`：对批次表里那几发一并删掉。旧代码在这里对
        // 已经析构的那一发再 `delete` 一次 —— 这条用例的价值就是"这一句不许崩"。
        delete store;
        pump(100);
        check(true, "★ R1-2 一发已收场、一发还在飞时析构：没有二次析构", QStringLiteral("未崩溃"));
    }
}
