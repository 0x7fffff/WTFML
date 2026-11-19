#include "Support.hpp"
#include "util/LogFileReader.hpp"

void runLogFileReaderTests() {
    section("LF. 日志文件读行 / 数行 / gzip 识别");

    QTemporaryDir dir;
    check(dir.isValid(), "LF0 临时目录");
    if (!dir.isValid()) return;
    const QString root = dir.path();

    // ---------- LF1. gzip 头 ----------
    check(LogFileReader::isGzipHead(QByteArray("\x1f\x8b\x08", 3)), "LF1a 1f 8b 开头是 gzip");
    check(!LogFileReader::isGzipHead(QByteArray("[12:00:00] x")), "LF1b 普通文本不是 gzip");
    check(!LogFileReader::isGzipHead(QByteArray("\x1f", 1)), "LF1c 只有一个字节不是 gzip");
    check(!LogFileReader::isGzipHead(QByteArray()), "LF1d 空内容不是 gzip");
    const QString plainPath = root + QStringLiteral("/latest.log");
    writeFileAt(plainPath, "a\nb\n");
    check(!LogFileReader::isGzipFile(plainPath), "LF1e 普通文件不是 gzip");
    check(!LogFileReader::isGzipFile(root + QStringLiteral("/nope.log")), "LF1f 不存在的文件按“不是”算");

    // ---------- LF2. 切行 ----------
    check(LogFileReader::splitLines("a\r\nb\n") == QStringList({"a", "b"}), "LF2a CRLF 去掉行尾回车、末尾换行不多出空行");
    check(LogFileReader::splitLines("a\nb") == QStringList({"a", "b"}), "LF2b 最后一行没有换行也算一行");
    check(LogFileReader::splitLines("").isEmpty(), "LF2c 空内容没有行");
    check(LogFileReader::splitLines("\n\nx\n") == QStringList({"", "", "x"}), "LF2d 中间空行保留");
    check(LogFileReader::splitLines(QStringLiteral("你好\n世界").toUtf8()) == QStringList({"你好", "世界"}),
          "LF2e UTF-8 中文按行还原");

    // ---------- LF3. 数行数 ----------
    const auto count = [&](const QByteArray &body) {
        const QString path = root + QStringLiteral("/count.log");
        writeFileAt(path, body);
        const LogFileReader::Counted counted = LogFileReader::countLines(path);
        return counted.ok ? counted.lines : -1;
    };
    check(count("") == 0, "LF3a 空文件 0 行");
    check(count("a\nb\n") == 2, "LF3b 末尾有换行");
    check(count("a\nb") == 2, "LF3c 末尾没换行也算一行");
    check(count("a\r\nb\r\n") == 2, "LF3d CRLF");
    {
        QByteArray big;
        for (int i = 0; i < 200000; ++i) big += "line " + QByteArray::number(i) + "\n";  // 约 2 MB，跨过 1 MiB 的读块
        check(count(big) == 200000, "LF3e 大文件分块数行");
    }
    check(!LogFileReader::countLines(root + QStringLiteral("/nope.log")).ok, "LF3f 不存在的文件数不了");

    // ---------- LF4. 末尾截取 ----------
    {
        QByteArray body;
        for (int i = 0; i < 1000; ++i) body += "line " + QByteArray::number(i) + "\n";
        const QString path = root + QStringLiteral("/long.log");
        writeFileAt(path, body);

        const LogFileReader::Loaded full = LogFileReader::load(path);
        check(full.ok && full.lines.size() == 1000 && full.cutHead == 0, "LF4a 不超上限读全部");

        const LogFileReader::Loaded tail = LogFileReader::load(path, 500);
        check(tail.ok && tail.cutHead > 0, "LF4b 超上限只读末尾，并记下丢掉的字节数");
        check(!tail.lines.isEmpty() && tail.lines.last() == QStringLiteral("line 999"), "LF4c 末尾那一行还在");
        check(!tail.lines.isEmpty() && tail.lines.first().startsWith(QStringLiteral("line ")) &&
                  body.at(tail.cutHead - 1) == '\n',
              "LF4d 截取从整行开始（首行不是半行）");
        check(tail.cutHead + qint64(tail.lines.join('\n').toUtf8().size()) + 1 == body.size(),
              "LF4e 丢掉的字节 + 读到的字节 = 文件大小");

        // 一整行比上限还长：没有换行可对齐，原样保留这一段，不丢光
        const QString single = root + QStringLiteral("/single.log");
        writeFileAt(single, QByteArray(2000, 'x'));
        const LogFileReader::Loaded oneLine = LogFileReader::load(single, 500);
        check(oneLine.ok && oneLine.lines.size() == 1 && oneLine.lines.first().size() == 500,
              "LF4f 没有换行时保留末尾 maxBytes 字节");
    }
    check(!LogFileReader::load(root + QStringLiteral("/nope.log")).ok, "LF4g 不存在的文件读不了且给原因");

    // ---------- LF5. gzip 往返（走 ZipUtil::gzipFrom / gunzipTo，起 PowerShell） ----------
    {
        QByteArray body;
        for (int i = 0; i < 300; ++i) body += "[12:00:00] [main/INFO]: 第 " + QByteArray::number(i) + " 行\n";
        const QString src = root + QStringLiteral("/old.log");
        const QString gz = root + QStringLiteral("/2026-11-18-1.log.gz");
        writeFileAt(src, body);
        QString why;
        if (!ZipUtil::gzipFrom(src, gz, &why)) {
            check(false, "LF5 造 .log.gz", why);
        } else {
            check(LogFileReader::isGzipFile(gz), "LF5a 压出来的文件头是 1f 8b");
            const LogFileReader::Loaded loaded = LogFileReader::load(gz);
            check(loaded.ok && loaded.lines.size() == 300 &&
                      loaded.lines.last() == QStringLiteral("[12:00:00] [main/INFO]: 第 299 行"),
                  "LF5b gzip 先解压再按行读", loaded.problem);
            check(LogFileReader::countLines(gz).lines == 300, "LF5c gzip 数行数按解压后的内容");
            // 后缀骗人：改名成 .log 照样按头认
            const QString renamed = root + QStringLiteral("/renamed.log");
            QFile::copy(gz, renamed);
            check(LogFileReader::isGzipFile(renamed) && LogFileReader::load(renamed).lines.size() == 300,
                  "LF5d 按文件头认 gzip，不看后缀");
        }
        // 头是 gzip 但内容坏了：给原因，不崩
        const QString broken = root + QStringLiteral("/broken.log.gz");
        writeFileAt(broken, QByteArray("\x1f\x8b\x08\x00garbage", 12));
        const LogFileReader::Loaded bad = LogFileReader::load(broken);
        check(!bad.ok && !bad.problem.isEmpty(), "LF5e 坏 gzip 读失败并给原因");
    }
}
