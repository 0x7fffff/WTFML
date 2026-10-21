/**
 * @file ConfigFileText.cpp
 * @brief ConfigFileText 的实现。**纯函数，不碰盘** —— 这一份里没有一个 QFile。
 * @note 见 .hpp 那一段（为什么"当文本处理"这件事本身不免费）。这里只补三处实现上的
 * 说明：`split()` 为什么必须和 QPlainTextEdit 一致、BOM 那三个字节、以及
 * `encode()` 里那个"取不够就用多数派"的取法。
 */
#include "ConfigFileText.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStringDecoder>

#include <algorithm>
#include <functional>

namespace {

/// UTF-8 BOM。**解码器会自己吃掉它**（`EF BB BF` + 「中」解出来第一个
/// 字符直接就是「中」），所以这一串只有"写回去时补不补"这一个用处。
QByteArray bomBytes() { return QByteArrayLiteral("\xEF\xBB\xBF"); }

}  // namespace

QString ConfigFileText::Loaded::terminator(Eol eol) {
    switch (eol) {
    case Eol::CrLf:
        return QStringLiteral("\r\n");
    case Eol::Cr:
        return QStringLiteral("\r");
    case Eol::Lf:
        break;
    }
    return QStringLiteral("\n");
}

ConfigFileText::Split ConfigFileText::split(const QString &text) {
    // 这个函数的输出**必须和 QPlainTextEdit 的文本模型一致**，否则"用户什么都没改"
    // 也会被判成改过（甚至是"每次保存都多一个换行"这种慢性病）。这个实现照
    // QPlainTextEdit 的四条行为写：
    // - `\r\n` 当**一个**换行（`a\r\nb\r\n` → `a\nb\n`）
    // - 单独的 `\r` **也**是换行（`a\rb` → `a\nb`）
    // - 末尾那个换行**保得住**（`a\nb\n` 原样回来，`a\nb` 也原样回来 —— 所以
    // 「最后一行后面有没有换行」这一位是能从正文里往返的，不用单独记）
    // - `\n` 当然还是换行
    // 反过来，这个函数的 `ends` **不能**从正文倒推回来（正文里全是 `\n` 了）——
    // 那就是 Loaded 非得把 ends 单独存一份的原因。
    Split out;
    QString current;
    int i = 0;
    while (i < text.size()) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\r')) {
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
                out.ends.append(Eol::CrLf);
                i += 2;
            } else {
                out.ends.append(Eol::Cr);
                i += 1;
            }
            out.lines.append(current);
            current.clear();
        } else if (c == QLatin1Char('\n')) {
            out.ends.append(Eol::Lf);
            i += 1;
            out.lines.append(current);
            current.clear();
        } else {
            current.append(c);
            i += 1;
        }
    }
    // 尾巴那一截：非空 = 最后一行没结尾；空着**但一个换行都没见过** = 整个文件就是空的
    // （这时候也得摆一行出来，不然 `lines.join()` 和"空文件"分不开）。
    if (!current.isEmpty() || out.ends.isEmpty()) {
        out.lines.append(current);
    }
    out.finalNewline = !out.ends.isEmpty() && current.isEmpty();
    return out;
}

qint64 ConfigFileText::editableByteLimit() {
    return 4LL * 1024LL * 1024LL;
}

ConfigFileText::Loaded ConfigFileText::decode(const QByteArray &raw) {
    Loaded out;
    out.raw = raw;
    out.bom = raw.startsWith(bomBytes());
    out.tooBig = raw.size() > editableByteLimit();

    // **不用 QString::fromUtf8**：那条路遇到非法字节直接塞 U+FFFD 而且**不告诉你**，
    // 于是"这个文件能不能编辑"就没得判了。QStringDecoder 有一个 hasError() 正是这一位
    // （合法 UTF-8 上 false、`bad \xb4 byte` 上 true）。
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder(raw);
    // 所以再加一道**原地往返**：解出来的串再编回 UTF-8，和原来那串字节**逐字节比**。
    // 合法 UTF-8 上解码→编码是恒等的（能走到这儿的串里不可能有孤立代理码元）；尾巴被
    // 丢掉时这一比就露馅（短了几个字节）。比之前把 BOM 摘掉 —— 解码器默认会吃掉开头
    // 那个 BOM（`ConvertInitialBom` 不设默认就是吃），它没进 `decoded`，也不该进这一比。
    //
    // NOTE: 这一比只判"能不能原地往返"，不改读法：合法 UTF-8 的文件照常打开。
    const QByteArray body = out.bom ? raw.mid(bomBytes().size()) : raw;
    out.utf8Ok = !decoder.hasError() && decoded.toUtf8() == body;

    const Split parts = split(decoded);
    out.ends = parts.ends;
    out.finalNewline = parts.finalNewline;
    out.text = parts.lines.join(QLatin1Char('\n'));
    if (parts.finalNewline) {
        out.text += QLatin1Char('\n');
    }

    // 多数派：新添的行用它。平手时偏向 `\n` —— 没有信号的时候别猜 Windows。
    int crlf = 0;
    int cr = 0;
    for (const Eol eol : out.ends) {
        if (eol == Eol::CrLf) {
            ++crlf;
        } else if (eol == Eol::Cr) {
            ++cr;
        }
    }
    const int lf = out.ends.size() - crlf - cr;
    if (crlf > lf && crlf >= cr) {
        out.dominant = Eol::CrLf;
    } else if (cr > lf && cr > crlf) {
        out.dominant = Eol::Cr;
    } else {
        out.dominant = Eol::Lf;
    }
    return out;
}

QByteArray ConfigFileText::encode(const Loaded &original, const QString &edited) {
    // 一个字都没改 ⇒ **原样回那些字节**。这一条比"我们的切分和拼接恰好可逆"可靠得多：
    // 打开一个文件、看一眼、关掉，盘上那份文件连 mtime 都不该变。
    if (edited == original.text) {
        return original.raw;
    }

    const Split parts = split(edited);
    QString out;
    out.reserve(edited.size() + 16);
    for (int i = 0; i < parts.lines.size(); ++i) {
        out += parts.lines.at(i);
        // 这一行后面有没有结尾符：不是最后一行就一定有；是最后一行就看 finalNewline。
        const bool terminated = (i < parts.lines.size() - 1) || parts.finalNewline;
        if (terminated) {
            // 原样那一行原来是什么结尾就用什么；**用户新添出来的那几行**（下标越过了原来
            // 记下的个数）用多数派。于是最常见的那种编辑 —— 改一个值 —— 出去之后每一行
            // 的结尾都逐字节和原来一样，变的只有那一行正文。
            out += Loaded::terminator(original.ends.value(i, original.dominant));
        }
    }

    QByteArray bytes = out.toUtf8();
    if (original.bom) {
        bytes.prepend(bomBytes());
    }
    return bytes;
}

// ---------------------------------------------------------------------------
// 盘上那一份"现在长什么样" + 世代备份落在哪儿（见 .hpp 那一节）
// ---------------------------------------------------------------------------

namespace {

/// 备份文件夹名的形状：`yyyyMMdd-HHmmss`，撞了的话后面挂 `-2`、`-3`…
///
/// NOTE: 这个名字**按字典序排就是按时间排**（定长的数字段），挑最新一份和清理旧备份
/// 都靠它 —— 别改成"好看"的那种格式。
bool looksLikeBackupName(const QString &name) {
    // 先剥掉可能挂着的 `-N` 那一截。
    QString core = name;
    const int dash = core.lastIndexOf(QLatin1Char('-'));
    if (dash > 0 && dash > core.indexOf(QLatin1Char('-'))) {
        bool ok = false;
        core.mid(dash + 1).toInt(&ok);
        if (ok) {
            core = core.left(dash);
        }
    }
    if (core.size() != 15 || core.at(8) != QLatin1Char('-')) {
        return false;
    }
    for (int i = 0; i < core.size(); ++i) {
        if (i == 8) {
            continue;
        }
        if (!core.at(i).isDigit()) {
            return false;
        }
    }
    return true;
}

}  // namespace

QString ConfigFileText::backupFolderName(const QDateTime &when, const QStringList &existing) {
    const QString base = when.toString(QStringLiteral("yyyyMMdd-HHmmss"));
    if (!existing.contains(base)) {
        return base;
    }
    // 同一秒里存了两次（连着按两下「保存」就是这个情形）：挂一个序号，**不覆盖**上一次
    // 那一份 —— 覆盖的话，"上一步"就悄悄变成了"这一步"。
    //
    // 序号**补零到三位**（`-002`）：名字排序 == 时间排序这条约定（见 .hpp）靠的是定长，
    // 而 "…-2" 和 "…-10" 按字典序是 `-10` 在前 —— 挤到两位数之后，"最新的那一份"就会
    // 认错（清理那一趟删的就不是最老的那一份了）。补零是让这条约定在这一秒里也成立。
    // （上限 999 次/秒：到了那儿超出的是"同一秒里的先后"，跨秒的顺序不受影响。）
    for (int i = 2; i < 1000; ++i) {
        const QString candidate =
            base + QLatin1Char('-') + QStringLiteral("%1").arg(i, 3, 10, QLatin1Char('0'));
        if (!existing.contains(candidate)) {
            return candidate;
        }
    }
    return base + QLatin1Char('-') + QString::number(existing.size());
}

bool ConfigFileText::isBackupFolderName(const QString &name) {
    return looksLikeBackupName(name);
}

QStringList ConfigFileText::backupFoldersToRemove(const QStringList &names, int keep) {
    QStringList mine;
    for (const QString &name : names) {
        if (isBackupFolderName(name)) {
            mine.append(name);
        }
    }
    // 名字倒序 == 时间倒序（见 backupFolderName() 那段 NOTE）。
    std::sort(mine.begin(), mine.end(), std::greater<QString>());
    if (mine.size() <= keep) {
        return {};
    }
    return mine.mid(keep);
}

QString ConfigFileText::backupPathFor(const QString &backupRoot, const QString &instanceId,
                                      const QString &configDir, const QString &filePath,
                                      const QString &folderName) {
    QString relative = QDir(configDir).relativeFilePath(filePath);
    // 不在 config/ 底下（理论上不会走到这儿）：退化成文件名。**绝不允许**把 `../..` 拼进去
    // —— 那会把备份写到备份根外面，而清理那一趟只认备份根底下的东西，于是那些文件
    // 永远不会被回收。
    if (relative.isEmpty() || relative.startsWith(QStringLiteral(".."))) {
        relative = QFileInfo(filePath).fileName();
    }
    return backupRoot + QLatin1Char('/') + instanceId + QLatin1Char('/') + folderName +
           QLatin1Char('/') + relative;
}
