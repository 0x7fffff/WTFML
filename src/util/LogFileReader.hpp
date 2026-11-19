/**
 * @file LogFileReader.hpp
 * @brief 读一份日志文件（`.log` / `.txt` / `.log.gz`）给内置查看器和信息栏用。
 *
 * 面向：日志页的单击信息、`GameLogWindow` 打开文件。全部函数都**阻塞**（读盘、gzip 要起
 * PowerShell，几百毫秒级），只能在工作线程里调。
 *
 * 约定：
 * - 是不是 gzip 看文件头 `1f 8b`，不看后缀（Minecraft 轮转出来的是 `.log.gz`，但改名的也认）。
 * - gzip 先用 `ZipUtil::gunzipTo()` 解到临时文件，再按普通文件读。
 * - 文本只读**末尾** `kMaxTextBytes`；更大的文件从第一个整行开始，`Loaded::cutHead` 记下被丢掉的字节数。
 */

#pragma once

#include <QString>
#include <QStringList>

namespace LogFileReader {

/// 单次最多读进内存的字节数（解压之后的大小）。
constexpr qint64 kMaxTextBytes = 32LL * 1024 * 1024;

/// 前两个字节是不是 gzip 头 `1f 8b`。
bool isGzipHead(const QByteArray &head);
/// 打开文件看头两个字节。打不开按"不是"算。
bool isGzipFile(const QString &path);

struct Loaded {
    bool ok = false;
    QString problem;           ///< `ok == false` 时的原因
    QStringList lines;         ///< 去掉行尾 `\r`；末尾没有多余的空行
    qint64 cutHead = 0;        ///< 文件大于上限时被丢掉的开头字节数（0 = 读了全部）
};

/// 读成行。gzip 先解压；超过 @p maxBytes 只保留末尾（从下一个换行之后算起，保证首行完整）。
Loaded load(const QString &path, qint64 maxBytes = kMaxTextBytes);

/// 把字节切成行：`\n` 分行、去掉行尾 `\r`、最后一个换行后面的空串不算一行。
QStringList splitLines(const QByteArray &bytes);

struct Counted {
    bool ok = false;
    QString problem;
    qint64 lines = 0;
};

/// 数行数（gzip 先解压）。流式读，不把整份文件装进内存；最后一行没有换行也算一行。
Counted countLines(const QString &path);

}  // namespace LogFileReader
