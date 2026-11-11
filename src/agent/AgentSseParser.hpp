/**
 * @file AgentSseParser.hpp
 * @brief OpenAI-compatible SSE 分块解析：只做“字节进、事件出”。
 *
 * 约束（Agent.md §5.1 / §13.2）：
 * - 任意网络分块边界都可能落在 CRLF、UTF-8 多字节、JSON 中间；
 * - `data:` 行在空行时派发，多行 data 用 `\n` 连接；
 * - 注释行（`:` 开头）忽略；`[DONE]` 单独成事件；
 * - 单行上限 1 MB，超限报错而不是无限吃内存。
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

class AgentSseParser {
public:
    struct Event {
        enum class Kind { Data, Done, Error };
        Kind kind = Kind::Data;
        QByteArray data;
        QString error;
    };

    static int maxLineBytes();

    void reset();
    /// 喂一段字节；返回这一块能派发的事件（可能为空）。
    QList<Event> feed(const QByteArray &chunk);
    /// 连接结束时调用：没有空行收尾的最后一条 data 在这里派发。
    QList<Event> finish();

private:
    QByteArray m_buffer;
    QByteArray m_pendingData;
    bool m_hasPendingData = false;
    bool m_done = false;
    bool m_failed = false;
};
