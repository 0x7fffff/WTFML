#include "MotdText.hpp"

#include <QtGlobal>

namespace {

const QChar kSection = QChar(0x00A7);

/// 是不是 16 个色号之一（`0`~`9` `a`~`f`）。
bool isColorCode(QChar code) {
    return (code >= QLatin1Char('0') && code <= QLatin1Char('9')) ||
           (code >= QLatin1Char('a') && code <= QLatin1Char('f'));
}

/// 把当前这一段的"状态"灌进 @p piece（只灌颜色和字形那几位，不动 text）。
void applyState(const QChar &color, bool hex, QRgb rgb, bool bold, bool italic, bool underline,
                bool strike, int line, MotdPiece *piece) {
    piece->color = color;
    piece->hex = hex;
    piece->rgb = rgb;
    piece->bold = bold;
    piece->italic = italic;
    piece->underline = underline;
    piece->strike = strike;
    piece->line = line;
}

}  // namespace

namespace MotdText {

QVector<MotdPiece> parse(const QString &raw, int maxLines) {
    QVector<MotdPiece> pieces;
    if (raw.isEmpty()) {
        return pieces;
    }

    // 当前这一段"继承"着的状态。`§r` 把它整个复位回默认，`§l` 之类只改其中一位 ——
    // 和游戏那边一模一样（每个码都是"从这里往后都算"）。
    QChar color;
    bool hex = false;
    QRgb rgb = 0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    int line = 0;

    QString pending;
    const auto flush = [&]() {
        if (pending.isEmpty()) {
            return;
        }
        MotdPiece piece;
        piece.text = pending;
        applyState(color, hex, rgb, bold, italic, underline, strike, line, &piece);
        pieces.append(piece);
        pending.clear();
    };
    // 换行：先把手上那一段收掉，再换行（**不生成空段** —— 空段在画的时候还要多判一次）。
    const auto newline = [&]() {
        flush();
        ++line;
    };

    for (int i = 0; i < raw.size(); ++i) {
        const QChar ch = raw.at(i);

        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) {
            newline();
            continue;
        }
        if (ch == QLatin1Char('\t')) {
            // 制表在列表里没意义（宽度是算出来的），当空格。
            pending += QLatin1Char(' ');
            continue;
        }
        if (ch != kSection) {
            pending += ch;
            continue;
        }

        // 到这里就是 `§`：后面那一个字符是码。**`§` 在末尾（后面什么都没有）就当没这一码**
        // —— 硬吞掉下一个位置的话会把最后一个正常字符吃掉，症状是"服务器那句话少一个字"。
        if (i + 1 >= raw.size()) {
            break;
        }
        const QChar code = raw.at(++i);
        if (isColorCode(code)) {
            flush();
            color = code;
            hex = false;
            // 色号**不影响字形**：`§l` 之后的红色还是粗的（游戏就是这规矩）。
            continue;
        }
        switch (code.unicode()) {
            case 'l':
                flush();
                bold = true;
                break;
            case 'o':
                flush();
                italic = true;
                break;
            case 'n':
                flush();
                underline = true;
                break;
            case 'm':
                flush();
                strike = true;
                break;
            case 'k':
                // 乱码那一档：**照原字画**（见文件头那段）。
                break;
            case 'r':
                flush();
                color = QChar();
                hex = false;
                rgb = 0;
                bold = italic = underline = strike = false;
                break;
            case 'x': {
                // `§x` 后面还得再有**六组** `§R` `§R` `§G` `§G` `§B` `§B`。少一组就整段
                // 不认（半截颜色比没颜色更让人看不懂）—— 不认时**什么都不改**，接着往后读。
                int r = 0;
                int g = 0;
                int b = 0;
                int got = 0;
                bool ok = true;
                while (got < 6) {
                    if (i + 2 >= raw.size() || raw.at(i + 1) != kSection) {
                        ok = false;
                        break;
                    }
                    const int value = QString(raw.at(i + 2)).toInt(&ok, 16);
                    if (!ok) {
                        break;
                    }
                    if (got < 2) {
                        r = r * 16 + value;
                    } else if (got < 4) {
                        g = g * 16 + value;
                    } else {
                        b = b * 16 + value;
                    }
                    i += 2;
                    ++got;
                }
                if (ok && got == 6) {
                    flush();
                    hex = true;
                    rgb = qRgb(r, g, b);
                    color = QChar();
                }
                break;
            }
            default:
                // 不认识的码：**什么都不做**（游戏也是忽略）。这里最容易犯的错是"顺手当普通
                // 字符画出来"—— 那屏幕上会冒出一个莫名其妙的字母。
                break;
        }
    }
    flush();

    // 行数那一道：第 maxLines 行起的整段丢掉（游戏那边也只画两行，画第三行反而对不上）。
    if (maxLines > 0) {
        QVector<MotdPiece> kept;
        kept.reserve(pieces.size());
        for (const MotdPiece &piece : pieces) {
            if (piece.line < maxLines) {
                kept.append(piece);
            }
        }
        return kept;
    }
    return pieces;
}

int lineCount(const QString &raw, int maxLines) {
    const QVector<MotdPiece> pieces = parse(raw, maxLines);
    int lines = 0;
    for (const MotdPiece &piece : pieces) {
        lines = qMax(lines, piece.line + 1);
    }
    return lines;
}

QColor minecraftColor(QChar code) {
    // 游戏里那 16 个色号，**原样抄的**（Minecraft Wiki 上那张表）。写错一个色号不会报错,
    // 只是那一档的字颜色不对 —— 这里逐个列出比查一张数字表好核对。
    switch (code.unicode()) {
        case '0':
            return QColor(0x00, 0x00, 0x00);
        case '1':
            return QColor(0x00, 0x00, 0xAA);
        case '2':
            return QColor(0x00, 0xAA, 0x00);
        case '3':
            return QColor(0x00, 0xAA, 0xAA);
        case '4':
            return QColor(0xAA, 0x00, 0x00);
        case '5':
            return QColor(0xAA, 0x00, 0xAA);
        case '6':
            return QColor(0xFF, 0xAA, 0x00);
        case '7':
            return QColor(0xAA, 0xAA, 0xAA);
        case '8':
            return QColor(0x55, 0x55, 0x55);
        case '9':
            return QColor(0x55, 0x55, 0xFF);
        case 'a':
            return QColor(0x55, 0xFF, 0x55);
        case 'b':
            return QColor(0x55, 0xFF, 0xFF);
        case 'c':
            return QColor(0xFF, 0x55, 0x55);
        case 'd':
            return QColor(0xFF, 0x55, 0xFF);
        case 'e':
            return QColor(0xFF, 0xFF, 0x55);
        case 'f':
            return QColor(0xFF, 0xFF, 0xFF);
        default:
            // 没指定（或者不是色号）= 白。游戏里没写颜色的字就是白的。
            return QColor(0xFF, 0xFF, 0xFF);
    }
}

QColor pieceColor(const MotdPiece &piece) {
    if (piece.hex) {
        return QColor::fromRgb(piece.rgb);
    }
    return minecraftColor(piece.color);
}

namespace {

/// 相对亮度（WCAG 那条公式的 luma 那一步，这里只用来判"两个色差不差得开"）。
double luma(const QColor &color) {
    const double r = color.redF();
    const double g = color.greenF();
    const double b = color.blueF();
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

}  // namespace

QColor readableOn(const QColor &wanted, const QColor &background) {
    // 底色亮就往暗里压、底色暗就往亮里提 —— 两头的判据是同一个（差值不够就挪一档）。
    const bool darken = luma(background) >= 0.5;
    QColor out = wanted;
    // 0.45 这一刀：亮底上 0.45 的差看着已经分得开了（再往上压会把 `§e` 黄色压成褐色，
    // 那就不是在"照游戏的样子显示"了）。
    for (int i = 0; i < 8 && qAbs(luma(out) - luma(background)) < 0.45; ++i) {
        out = darken ? out.darker(118) : out.lighter(118);
    }
    return out;
}

}  // namespace MotdText
