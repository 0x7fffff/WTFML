/**
 * @file AgentSseParserTests.cpp
 * @brief SSE 分块边界、CRLF、注释、[DONE]、UTF-8、超长行。
 */

#include "Support.hpp"

#include "agent/AgentSseParser.hpp"

void runAgentSseParserTests() {
    {
        AgentSseParser parser;
        QList<AgentSseParser::Event> events = parser.feed("data: {\"a\":1}\n\n");
        check(events.size() == 1 && events.at(0).kind == AgentSseParser::Event::Kind::Data,
              "SSE：一条 data + 空行派发一个事件", QString::number(events.size()));
        check(events.value(0).data == QByteArray("{\"a\":1}"), "SSE：data 去掉了字段名和空格");
    }
    {
        AgentSseParser parser;
        QList<AgentSseParser::Event> events = parser.feed("data: one\r\n\r\ndata: two\r\n\r\ndata: [DONE]\r\n\r\n");
        check(events.size() == 3, "SSE：CRLF 与多条事件都识别", QString::number(events.size()));
        check(events.value(1).data == QByteArray("two"), "SSE：第二条 data");
        check(events.value(2).kind == AgentSseParser::Event::Kind::Done, "SSE：[DONE] 单独成事件");
    }
    {
        AgentSseParser parser;
        const QByteArray utf8 = QStringLiteral("data: 你好\n\n").toUtf8();
        QList<AgentSseParser::Event> events;
        for (int i = 0; i < utf8.size(); ++i) {
            events += parser.feed(utf8.mid(i, 1));
        }
        check(events.size() == 1 && QString::fromUtf8(events.at(0).data) == QStringLiteral("你好"),
              "SSE：UTF-8 多字节被网络分块拆开也不破坏字符");
    }
    {
        AgentSseParser parser;
        QList<AgentSseParser::Event> events =
            parser.feed(": keep-alive\n\ndata:first\n\ndata: second\ndata: third\n\n");
        check(events.size() == 2, "SSE：注释忽略，空行派发", QString::number(events.size()));
        check(events.value(0).data == QByteArray("first"), "SSE：冒号后无空格也认");
        check(events.value(1).data == QByteArray("second\nthird"), "SSE：多行 data 用换行连接");
    }
    {
        AgentSseParser parser;
        QList<AgentSseParser::Event> events = parser.feed("data: no-blank-line");
        check(events.isEmpty(), "SSE：没有空行时先不派发");
        events = parser.finish();
        check(events.size() == 1 && events.at(0).data == QByteArray("no-blank-line"),
              "SSE：连接结束收尾派发最后一条");
    }
    {
        AgentSseParser parser;
        QByteArray huge = "data: ";
        huge.append(QByteArray(AgentSseParser::maxLineBytes() + 10, 'x'));
        huge.append('\n');
        const QList<AgentSseParser::Event> events = parser.feed(huge);
        check(!events.isEmpty() &&
                  events.constLast().kind == AgentSseParser::Event::Kind::Error,
              "SSE：超过 1 MB 的单行报错，不无限缓冲");
    }
    {
        AgentSseParser parser;
        QByteArray split = "data: {\"x\":\"";
        split.append(QByteArray(300000, 'a'));
        parser.feed(split);
        QByteArray rest = QByteArray(500000, 'b') + "\"}\n\n";
        const QList<AgentSseParser::Event> events = parser.feed(rest);
        check(events.size() == 1 && events.at(0).data.endsWith("\"}"),
              "SSE：大 JSON 跨多块拼回一条");
    }
}
