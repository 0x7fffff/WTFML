#include "Support.hpp"

#include "net/RequestBudget.hpp"

#include <QImage>
#include <QPixmap>

// ---------------------------------------------------------------------------
// ZI. 资源商店那几发也走共享的 Control 额度
//
// 这一节要证的是**接线**，不是 `ControlGet` 那一层的行为（排队、截止、删作业的语义已在
// PH6 / T8 / RequestBudgetChecks 里逐条钉过）。所以每条只量四件事：额度占满时一个字节都
// 不发、也不回调；名额回来就照常发出并收尾；收尾后不残留租约；换掉/删掉的那一发不会
// 事后偷偷发出去。
//
// 夹具是本机一个真实 TCP 端口 —— ResourceStore 的地址全部由 override 指过来，
// 一个字节都不碰外网。
// ---------------------------------------------------------------------------

namespace {

/// 假接口：`search` / `versions` / `projects`(POST) / 图标四条路都接在同一个端口上。
class FakeStoreApi {
public:
    bool start() {
        if (!m_server.listen(QHostAddress::LocalHost)) {
            return false;
        }
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            while (auto *socket = m_server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                // 一个请求可能分好几次到（POST 尤其如此）：攒够"头 + Content-Length"再答。
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
                    const QList<QByteArray> headLines = buffer.left(headerEnd).split('\n');
                    for (const QByteArray &line : headLines) {
                        const QByteArray trimmed = line.trimmed();
                        if (trimmed.toLower().startsWith("content-length:")) {
                            contentLength = trimmed.mid(int(qstrlen("content-length:"))).trimmed().toInt();
                        }
                    }
                    if (buffer.size() < headerEnd + 4 + contentLength) {
                        return;
                    }
                    socket->setProperty("answered", true);
                    answer(socket, buffer.left(headerEnd),
                           buffer.mid(headerEnd + 4, contentLength));
                });
            }
        });
        return true;
    }

    QString base() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }
    /// CurseForge 那份接口根：**到 `/v1` 为止**（见 ModpackImport::curseForgeApiBase 那段，
    /// 调用点只往后接 `/mods/search` 之类）。
    QString cfBase() const { return base() + QStringLiteral("/v1"); }
    QString url(const QString &path) const { return base() + path; }

    /// 收到过的请求首行（`GET /v1/mods/search?...` 这样），按到达顺序。
    const QList<QByteArray> &requests() const { return m_requestLines; }
    QList<QByteArray> matching(const QByteArray &needle) const {
        QList<QByteArray> found;
        for (const QByteArray &line : m_requestLines) {
            if (line.contains(needle)) {
                found.append(line);
            }
        }
        return found;
    }
    QByteArray lastPostBody() const { return m_postBodies.isEmpty() ? QByteArray() : m_postBodies.last(); }
    void clear() {
        m_requestLines.clear();
        m_postBodies.clear();
    }

    QByteArray searchBody = QByteArrayLiteral(
        "{\"data\":[{\"id\":1546263,\"name\":\"Arizona\",\"slug\":\"arizona\","
        "\"summary\":\"一句简介\",\"downloadCount\":642,"
        "\"authors\":[{\"name\":\"birdmuncher\"}],\"categories\":[{\"name\":\"Technology\"}],"
        "\"latestFilesIndexes\":[{\"gameVersion\":\"1.21.1\",\"modLoader\":4}],"
        "\"logo\":{\"thumbnailUrl\":\"https://media.forgecdn.net/a_256.png\","
        "\"url\":\"https://media.forgecdn.net/a.png\"}}],"
        "\"pagination\":{\"index\":0,\"pageSize\":20,\"resultCount\":1,\"totalCount\":1}}");
    QByteArray versionsBody = QByteArrayLiteral(
        "{\"data\":[{\"id\":8964987,\"displayName\":\"1.0 for Fabric 1.21.1\","
        "\"fileName\":\"arizona-1.0.jar\",\"fileLength\":1234,\"releaseType\":2,"
        "\"fileDate\":\"2026-09-20T20:00:41Z\",\"downloadUrl\":\"https://edge.forgecdn.net/a.jar\","
        "\"gameVersions\":[\"1.21\",\"1.21.1\",\"Client\"],"
        "\"hashes\":[{\"value\":\"da39a3ee5e6b4b0d3255bfef95601890afd80709\",\"algo\":1}]}]}");
    QByteArray projectsBody = QByteArrayLiteral(
        "{\"data\":[{\"id\":1546263,\"name\":\"Arizona\",\"slug\":\"arizona\","
        "\"summary\":\"一句简介\",\"logo\":{\"url\":\"https://media.forgecdn.net/a.png\"}}]}");
    QByteArray iconBody;

private:
    void answer(QTcpSocket *socket, const QByteArray &head, const QByteArray &body) {
        const QList<QByteArray> firstLine = head.left(head.indexOf("\r\n")).split(' ');
        const QByteArray method = firstLine.value(0);
        const QByteArray target = firstLine.value(1);
        // 没有 `?` 的时候 `left(-1)` 回的是**空**串（不是整串）—— 少了这一行，凡是
        // 不带查询串的那几发（POST /v1/mods、/icon.png）都会落进 404 分支。
        const qsizetype queryAt = target.indexOf('?');
        const QByteArray path = queryAt < 0 ? target : target.left(queryAt);
        m_requestLines.append(method + ' ' + target);
        if (method == "POST") {
            m_postBodies.append(body);
        }

        int status = 200;
        QByteArray payload = QByteArrayLiteral("{}");
        if (path == "/v1/mods/search") {
            payload = searchBody;
        } else if (path == "/v1/mods") {
            payload = projectsBody;
        } else if (path.endsWith("/files")) {
            payload = versionsBody;
        } else if (path == "/icon.png") {
            payload = iconBody;
        } else {
            status = 404;
        }
        socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Not Found") +
                      "\r\nContent-Length: " + QByteArray::number(payload.size()) +
                      "\r\nConnection: close\r\n\r\n");
        socket->write(payload);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QList<QByteArray> m_requestLines;
    QList<QByteArray> m_postBodies;
};

} // namespace

void runResourceStoreBudgetTests() {
    FakeStoreApi api;
    check(api.start(), "ZI 本地假接口起来了");

    // 一张真能解码的 PNG（图标那条路要过真解码，不是只看文件头）。
    api.iconBody = solidPngBytes(QColor(0x40, 0x80, 0xC0));
    check(!api.iconBody.isEmpty(), "ZI 那一张图标字节造出来了",
          QString::number(api.iconBody.size()));
    check(!ImageDecode::decode(api.iconBody).isNull(),
          "ZI 这张图标在本进程里解得开（解不开的话下面那条量的是另一条分支）",
          QImageReader::supportedImageFormats().join(QByteArrayLiteral(",")));

    ResourceStore store;
    store.setCurseForgeApiBaseOverride(api.cfBase());

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
    const auto fillControlLane = [] {
        QList<RequestBudget::Lease> holds;
        while (auto lease = RequestBudget::tryAcquire(RequestBudget::Lane::Control)) {
            holds.append(lease);
        }
        return holds;
    };
    const int baseline = RequestBudget::snapshot().control;

    // ---- ZI1 搜索：额度占满只排队，名额回来照常发出并收尾 ---------------------
    {
        const QString key = ResourceCatalog::searchRequestKey(
            ResourceCatalog::Kind::Mod, ResourceCatalog::Source::CurseForge);
        bool got = false;
        ResourceCatalog::SearchResult found;
        const QMetaObject::Connection finishedConnection =
            QObject::connect(&store, &ResourceStore::searchFinished, &store,
                         [&](const QString &finished, const ResourceCatalog::SearchResult &result) {
                             if (finished != key) {
                                 return;
                             }
                             got = true;
                             found = result;
                         });

        auto holds = fillControlLane();
        store.search(key, ResourceCatalog::Kind::Mod, ResourceCatalog::Source::CurseForge,
                     QStringLiteral("jei"), QStringLiteral("1.21.1"), QStringLiteral("1"),
                     QString());
        pump(150);
        check(api.matching("/v1/mods/search").isEmpty() && !got,
              "ZI1 额度占满时搜索只排队：一个字节都没发、也不回调");
        holds.clear();
        // 先等（`pumpUntil` 有副作用），再把判据和那句话拼出来 —— 写在同一个 `check()`
        // 的实参里的话，编译器**从右往左**求值，那句详情量的是"还没等"的那一瞬间。
        const bool arrived = pumpUntil([&] { return got; }, 2000);
        check(arrived && api.matching("/v1/mods/search").size() == 1 &&
                  found.hits.size() == 1 && found.problem.isEmpty(),
              "ZI1 名额回来就发出去，搜索结果照常到达",
              QStringLiteral("hits=%1 发出=%2 问题=%3")
                  .arg(found.hits.size())
                  .arg(api.matching("/v1/mods/search").size())
                  .arg(found.problem));
        // ★ 这一条**必须断**：lambda 按引用抓着这一节里的 `got` / `found`，而 ZI4 用的是
        // **同一个请求键** —— 还挂着的话，那一发回来会往已经退栈的 `found` 上赋值。
        // 实测就是这样偶发 0xC0000005（gdb 栈：`QString::operator=` ← 这个 lambda），
        // 而且只在整轮里碰运气，单跑多半看不出来。
        QObject::disconnect(finishedConnection);
    }

    // ---- ZI2 有正文的那一发（CurseForge 的 `POST /mods`）也进同一个额度 ----------
    //
    // 这条同时钉住"作业里发的确实是**带正文的 POST**"：ControlGet 只做 GET 的话，这一发
    // 要么发不出去、要么出去的是一个没有号表的空请求 —— 两种在这里都会红。
    {
        api.clear();
        const QString key = ResourceCatalog::projectsRequestKey(
            QStringList{QStringLiteral("1546263")}, ResourceCatalog::Source::CurseForge);
        bool got = false;
        QList<ResourceCatalog::Hit> hits;
        QString problem;
        const QMetaObject::Connection finishedConnection =
            QObject::connect(&store, &ResourceStore::projectsFinished, &store,
                         [&](const QString &finished, const QList<ResourceCatalog::Hit> &answer,
                             const QString &why) {
                             if (finished != key) {
                                 return;
                             }
                             got = true;
                             hits = answer;
                             problem = why;
                         });

        auto holds = fillControlLane();
        store.fetchProjects(key, QStringList{QStringLiteral("1546263")},
                            ResourceCatalog::Source::CurseForge);
        pump(150);
        check(api.matching("/v1/mods").isEmpty() && !got,
              "ZI2 额度占满时 POST 那一发也只排队");
        holds.clear();
        const bool arrived = pumpUntil([&] { return got; }, 2000);
        const QList<QByteArray> posts = api.matching("POST /v1/mods");
        check(arrived && posts.size() == 1 && hits.size() == 1 && problem.isEmpty(),
              "ZI2 名额回来就出发，项目信息照常回来",
              problem.isEmpty() ? QStringLiteral("收到：%1").arg(QString::fromUtf8(posts.value(0)))
                                : problem);
        check(!api.lastPostBody().isEmpty() && api.lastPostBody().contains("modIds"),
              "ZI2 出发的是**带正文的 POST**（号表在正文里，不是空请求）",
              QString::fromUtf8(api.lastPostBody()));
        // 同一个理由（见 ZI1 那条 NOTE）：这一节退掉之后 lambda 抓着的引用就没了。
        QObject::disconnect(finishedConnection);
    }

    // ---- ZI3 图标那一发 -------------------------------------------------------
    {
        api.clear();
        ResourceCatalog::Hit hit;
        hit.projectId = QStringLiteral("999001");
        hit.source = ResourceCatalog::Source::CurseForge;
        hit.iconUrl = api.url(QStringLiteral("/icon.png"));
        const QString iconId = hit.iconCacheId();
        ResourceStore::dropIconFromCache(iconId);
        bool got = false;
        const QMetaObject::Connection readyConnection =
            QObject::connect(&store, &ResourceStore::iconReady, &store,
                         [&](const QString &ready, const QPixmap &) {
                             if (ready == iconId) {
                                 got = true;
                             }
                         });

        auto holds = fillControlLane();
        store.requestIcon(hit);
        pump(150);
        check(api.matching("/icon.png").isEmpty() && !got,
              "ZI3 额度占满时图标那一发也只排队");
        holds.clear();
        // NOTE: 这一发收尾时那个 `QPixmap` 在**本进程里必是空的** —— 这个验证程序是
        // `QCoreApplication`，而 `QPixmap` 要先有 `QGuiApplication`（上面那条
        // `ImageDecode::decode()` 已经证明字节本身解得开，真解码那条路归 mw_check 的 ZB8）。
        // 所以这里量的是"发出去了、也回了"，不是"画得出来"。
        const bool arrived = pumpUntil([&] { return got; }, 2000);
        check(arrived && api.matching("/icon.png").size() == 1,
              "ZI3 名额回来就下图标、收尾回一次",
              QString::fromUtf8(api.requests().join(QByteArrayLiteral(" | "))));
        ResourceStore::dropIconFromCache(iconId);
        // 同一个理由（见 ZI1 那条 NOTE）：这一节退掉之后 lambda 抓着的 `got` 就没了。
        QObject::disconnect(readyConnection);
    }

    // ---- ZI4 同一个请求键上换了一发：旧的删掉 ⇒ 只发一次、只回一次 --------------
    {
        api.clear();
        const QString key = ResourceCatalog::searchRequestKey(
            ResourceCatalog::Kind::Mod, ResourceCatalog::Source::CurseForge);
        int answers = 0;
        const QMetaObject::Connection finishedConnection =
            QObject::connect(&store, &ResourceStore::searchFinished, &store,
                         [&](const QString &finished, const ResourceCatalog::SearchResult &) {
                             if (finished == key) {
                                 ++answers;
                             }
                         });

        auto holds = fillControlLane();
        store.search(key, ResourceCatalog::Kind::Mod, ResourceCatalog::Source::CurseForge,
                     QStringLiteral("jei"), QString(), QString(), QString());
        pump(50);
        store.search(key, ResourceCatalog::Kind::Mod, ResourceCatalog::Source::CurseForge,
                     QStringLiteral("jei"), QString(), QString(), QString());
        pump(50);
        holds.clear();
        pump(400);
        check(api.matching("/v1/mods/search").size() == 1 && answers == 1,
              "ZI4 同一格上换了一发 ⇒ 排队的旧作业被删掉，只发一次、只回一次",
              QStringLiteral("发=%1 回=%2")
                  .arg(api.matching("/v1/mods/search").size())
                  .arg(answers));
        // 同一个理由（见 ZI1 那条 NOTE）：这一节退掉之后 lambda 抓着的 `answers` 就没了。
        QObject::disconnect(finishedConnection);
    }

    // ---- ZI5 排队中的作业跟着对象一起删掉：名额腾出来也不发、租约不残留 ----------
    {
        api.clear();
        const QString key = ResourceCatalog::searchRequestKey(
            ResourceCatalog::Kind::Mod, ResourceCatalog::Source::CurseForge);
        auto holds = fillControlLane();
        {
            ResourceStore doomed;
            doomed.setCurseForgeApiBaseOverride(api.cfBase());
            doomed.search(key, ResourceCatalog::Kind::Mod, ResourceCatalog::Source::CurseForge,
                          QStringLiteral("jei"), QString(), QString(), QString());
            pump(100);
        }
        holds.clear();
        pump(300);
        check(api.matching("/v1/mods/search").isEmpty() &&
                  RequestBudget::snapshot().control == baseline,
              "★★ ZI5 排队中的那一发跟着对象一起删掉 ⇒ 名额腾出来也不发、租约不残留",
              QStringLiteral("发=%1 租约=%2")
                  .arg(api.matching("/v1/mods/search").size())
                  .arg(RequestBudget::snapshot().control));
    }

    check(RequestBudget::snapshot().control == baseline,
          "ZI 收尾：这几发都收干净了，Control 名额一个都没占着",
          QString::number(RequestBudget::snapshot().control));
}
