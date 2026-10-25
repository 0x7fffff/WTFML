#include "HttpFileServer.hpp"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>

namespace {
constexpr qint64 kWriteChunk = 256 * 1024;
constexpr qint64 kWriteHighWater = 1024 * 1024;
}  // namespace

HttpFileServer::HttpFileServer(QByteArray payload, QObject *parent)
    : QObject(parent), m_payload(std::move(payload)) {}

quint16 HttpFileServer::start(int fileCount) {
    m_fileCount = std::max(1, fileCount);
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &HttpFileServer::onNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return m_server->serverPort();
}

void HttpFileServer::stop() {
    if (!m_server) {
        return;
    }
    for (QTcpSocket *socket : m_connections.keys()) {
        dropConnection(socket);
    }
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
}

void HttpFileServer::onNewConnection() {
    if (!m_server) {
        return;
    }
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        socket->setParent(this);
        m_connections.insert(socket, new Conn);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::bytesWritten, this, [this, socket] { pump(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { dropConnection(socket); });
    }
}

void HttpFileServer::dropConnection(QTcpSocket *socket) {
    Conn *conn = m_connections.take(socket);
    if (!conn) {
        return;
    }
    delete conn;
    socket->disconnect(this);
    socket->deleteLater();
}

void HttpFileServer::onReadyRead(QTcpSocket *socket) {
    Conn *conn = m_connections.value(socket);
    if (!conn) {
        return;
    }
    conn->pending.append(socket->readAll());
    drain(socket);
}

void HttpFileServer::drain(QTcpSocket *socket) {
    Conn *conn = m_connections.value(socket);
    if (!conn) {
        return;
    }
    // 正文没发完时先不解析下一条；pump() 发完会再进来一次。
    while (!conn->sending && conn->pending.contains("\r\n\r\n")) {
        startResponse(socket);
        if (!m_connections.contains(socket)) {
            return;  // 对端在应答途中断了
        }
    }
}

void HttpFileServer::startResponse(QTcpSocket *socket) {
    Conn *conn = m_connections.value(socket);
    if (!conn || conn->sending) {
        return;
    }
    const int headerEnd = conn->pending.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }
    QByteArray head = conn->pending.left(headerEnd);
    conn->pending.remove(0, headerEnd + 4);

    QList<QByteArray> lines = head.split('\n');
    for (QByteArray &line : lines) {
        while (line.endsWith('\r')) {
            line.chop(1);
        }
    }
    const QList<QByteArray> request = lines.value(0).split(' ');
    const QByteArray method = request.value(0);
    const QByteArray target = request.value(1);
    const bool isHead = method == "HEAD";

    QByteArray range;
    for (int i = 1; i < lines.size(); ++i) {
        const int colon = lines.at(i).indexOf(':');
        if (colon <= 0) {
            continue;
        }
        if (lines.at(i).left(colon).trimmed().toLower() == "range") {
            range = lines.at(i).mid(colon + 1).trimmed();
            break;
        }
    }

    const qint64 total = m_payload.size();
    const auto send = [&](const QByteArray &extra, const QByteArray &status, qint64 bodyFirst,
                          qint64 bodyLast) {
        QByteArray out;
        out.reserve(256);
        out += "HTTP/1.1 " + status + "\r\n";
        out += "Content-Type: application/octet-stream\r\n";
        out += "Accept-Ranges: bytes\r\n";
        out += "ETag: \"perf-" + QByteArray::number(total) + "\"\r\n";
        out += extra;
        out += "Connection: keep-alive\r\n\r\n";
        socket->write(out);
        if (bodyFirst < 0 || bodyLast < bodyFirst) {
            conn->sending = false;
            return;
        }
        conn->sendOffset = bodyFirst;
        conn->sendEnd = bodyLast;
        conn->sending = true;
        if (!isHead) {
            pump(socket);
        } else {
            conn->sending = false;
        }
    };

    // 只认 /file/<n>.bin（n < fileCount）。
    bool knownPath = false;
    if (target.startsWith("/file/") && target.endsWith(".bin")) {
        bool ok = false;
        const int index = target.mid(6, target.size() - 6 - 4).toInt(&ok);
        knownPath = ok && index >= 0 && index < m_fileCount;
    }
    if (!knownPath || (method != "GET" && !isHead)) {
        const QByteArray body = "not found\n";
        QByteArray out;
        out += "HTTP/1.1 404 Not Found\r\n";
        out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        out += "Connection: keep-alive\r\n\r\n";
        out += isHead ? QByteArray() : body;
        socket->write(out);
        conn->sending = false;
        return;
    }

    qint64 first = 0;
    qint64 last = total - 1;
    bool partial = false;
    bool unsatisfiable = false;
    if (range.toLower().startsWith("bytes=")) {
        const QByteArray spec = range.mid(6);
        const int dash = spec.indexOf('-');
        if (dash >= 0) {
            const QByteArray from = spec.left(dash).trimmed();
            const QByteArray to = spec.mid(dash + 1).trimmed();
            if (from.isEmpty()) {  // 后缀范围：最后 n 个字节
                const qint64 count = to.toLongLong();
                if (count <= 0) {
                    unsatisfiable = true;
                } else {
                    first = std::max<qint64>(0, total - count);
                    last = total - 1;
                    partial = true;
                }
            } else {
                first = from.toLongLong();
                last = to.isEmpty() ? total - 1 : std::min(to.toLongLong(), total - 1);
                if (first < 0 || first >= total || last < first) {
                    unsatisfiable = true;
                } else {
                    partial = true;
                }
            }
        }
    }

    if (unsatisfiable) {
        send("Content-Range: bytes */" + QByteArray::number(total) + "\r\n",
             "416 Range Not Satisfiable", -1, -1);
        return;
    }
    const qint64 length = last - first + 1;
    QByteArray extra = "Content-Length: " + QByteArray::number(length) + "\r\n";
    if (partial) {
        extra += "Content-Range: bytes " + QByteArray::number(first) + "-"
            + QByteArray::number(last) + "/" + QByteArray::number(total) + "\r\n";
    }
    send(extra, partial ? "206 Partial Content" : "200 OK", first, last);
}

void HttpFileServer::pump(QTcpSocket *socket) {
    Conn *conn = m_connections.value(socket);
    if (!conn || !conn->sending) {
        return;
    }
    while (conn->sendOffset <= conn->sendEnd && socket->bytesToWrite() < kWriteHighWater) {
        const qint64 length = std::min(kWriteChunk, conn->sendEnd - conn->sendOffset + 1);
        const qint64 accepted = socket->write(m_payload.constData() + conn->sendOffset, length);
        if (accepted <= 0) {
            break;  // 等 bytesWritten 回来再补
        }
        conn->sendOffset += accepted;
    }
    if (conn->sendOffset > conn->sendEnd) {
        conn->sending = false;
        drain(socket);
    }
}
