/**
 * @file HttpFileServer.hpp
 * @brief 并发下载基准用的极简 HTTP/1.1 文件服务器。
 *
 * 【为什么单独一条线程】
 *
 * 基准要量的是 `DownloadManager` 在主线程上的行为。服务器也在主线程收发 socket 的话，
 * 它自己那几百 MB 的写入会先一步把主线程占满 —— 量到的"最大空档"就是基准服务器造成的，
 * 而不是产品。所以：**这个对象要在自己的 QThread 里构造、启动、销毁**（见 main.cpp），
 * 主线程只留一个端口号。
 *
 * 【支持的范围】
 *
 * - `GET /file/<n>.bin`：所有文件共用同一份 payload（大小与 SHA-1 都一样，
 *   只用来压调度与连接数，不模拟内容差异）；
 * - `Range: bytes=a-b` / `bytes=a-` / `bytes=-n` → 206 + `Content-Range`；
 *   `a >= total` → 416；
 * - `ETag`、`Accept-Ranges`、`Connection: keep-alive`：一条连接上可以连着发多个请求
 *   （QNAM 会复用连接，答完就关会让它把"连接被对端关掉"算成一次失败重试）；
 * - 正文按 256 KiB 分块写，`bytesWritten` 回来再补 —— 服务器线程也不会被一次
 *   `write()` 里的大块拷贝卡住。
 */
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>

class QTcpServer;
class QTcpSocket;

class HttpFileServer : public QObject {
    Q_OBJECT

public:
    explicit HttpFileServer(QByteArray payload, QObject *parent = nullptr);

    /// 真起监听并返回端口；失败回 0。**必须在服务器线程里调**。
    Q_INVOKABLE quint16 start(int fileCount);
    Q_INVOKABLE void stop();

private:
    struct Conn {
        QByteArray pending;   // 还没解析完的请求（keep-alive 下一条也在这里排队）
        bool sending = false;
        qint64 sendOffset = 0;
        qint64 sendEnd = -1;  // 闭区间；-1 = 没有正文要发
    };

    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    /// 从 `pending` 头部取一条请求，写响应头，并开始发正文。
    void startResponse(QTcpSocket *socket);
    /// 把正文按块塞进 socket 的写缓冲；发完就接着处理下一条排队的请求。
    void pump(QTcpSocket *socket);
    /// 缓冲里只要有完整请求头就一条接一条地答（不在发正文时）。
    void drain(QTcpSocket *socket);
    void dropConnection(QTcpSocket *socket);

    QByteArray m_payload;
    int m_fileCount = 0;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Conn *> m_connections;
};
