/**
 * @file MotdText.hpp
 * @brief 把服务器的 MOTD（那串带 `§` 码的字）拆成"一段一段、各带颜色和字形"的东西，
 * 好让它在列表里**照游戏里那个样子**画出来。
 * @note `ServerPing::cleanMotd()` 洗出来的是一行纯文本 —— 颜色码全被丢掉；这里保留
 * 颜色和字形，服务器精心配的两行彩色标语照游戏里那个样子画出来。
 *
 * 【这串字的规矩（就是游戏自己那套）】
 *
 * `§`（U+00A7）后面跟一个字符，管到下一个码或者这一行结束：
 * - `0`~`9` `a`~`f`：16 个颜色（见 minecraftColor() 那张表）；
 * - `l` 粗体、`o` 斜体、`n` 下划线、`m` 删除线、`r` 全部复位；
 * - `k` 乱码（游戏里那一格的字会跳）—— **我们照原字画**，不跳：一个列表里跳字是
 * 纯粹的干扰，而且跳出来的东西和服务器想说的那句话没关系；
 * - `x` 是 1.16 之后那种六位十六进制写法，后面**还得再跟六组** `§R` `§R` `§G` `§G`
 * `§B` `§B`（对，颜色分量也各自带一个 `§`）。
 * 换行 `\n` 直接就是"第二行"—— 游戏那边的 MOTD 就是两行。
 *
 * 【为什么要拆成"段"而不是直接给一个富文本串】
 *
 * 因为**画的那一层要自己算宽度**：这一格是定宽的一行小字，装不下时要从右边省略。
 * 富文本交给 `QTextDocument` 的话，省略号那一步就得自己重来一遍（而且它按段落排版，
 * 和这一行"图标 + 两行字"的版式对不上）。拆成段之后，画的人一段一段量、装不下就地把
 * 那一段截断，颜色和字形都还在。
 *
 * NOTE: 这个文件**只解析、只看颜色**，一个字都不碰服务器：`§` 串是从 `ServerPing` 那边
 * 来的（`Status::motdRaw`），本文件不认识网络也不认识 JSON。
 */

#pragma once

#include <QColor>
#include <QRgb>
#include <QString>
#include <QVector>

/// MOTD 里的一段：**同一套颜色和字形**连着的那几个字。
struct MotdPiece {
    QString text;
    /// MC 色号（`'0'`~`'f'`）。**空 QChar = 这一段没指定颜色**（游戏里按白算，见
    /// MotdText::pieceColor()）。
    QChar color;
    /// 这一段用的是 `§x§R§R§G§G§B§B` 那种写法（真时看 rgb，不看 color）。
    bool hex = false;
    QRgb rgb = 0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    /// 删除线。游戏里 `§m` 是"划掉"，列表里照画 —— 服务器用它来做装饰性的横线。
    bool strike = false;
    /// 第几行（0 起）。**最多两行**（parse() 的 maxLines），再多就丢了 —— 游戏那边也只画两行。
    int line = 0;
};

namespace MotdText {

/// 一段一段解析出来。**按行切开再解析**，所以不会有哪一段跨两行。
/// 没有 `§` 码的纯文本会得到**一段**（color 空）。
/// @param maxLines 最多认几行（第 3 行起整段丢掉）。0 = 不限制。
QVector<MotdPiece> parse(const QString &raw, int maxLines = 2);

/// 这串 MOTD 画出来是几行（1 起；**空串算 0 行**）。给"这一行要留多高"用。
int lineCount(const QString &raw, int maxLines = 2);

/// 某个色号对应的颜色（游戏里那 16 个，**色号是原样抄的**）。
/// @param code 空 QChar 或者不认识的字符 → 回**白色**（游戏里没指定颜色就是白）。
QColor minecraftColor(QChar code);

/// 这一段该用什么颜色。`hex` 为真时用 rgb，否则按色号查表。
QColor pieceColor(const MotdPiece &piece);

/// 把 @p wanted 调整到在 @p background 上**读得清**。
///
/// Why 需要这一步：我们的底色是**浅的**（Fusion 的浅色调色板），而游戏那边底色是黑的
/// —— 服务器按黑底配的颜色直接搬过来，"§f 白色"、"§e 黄色"这种在浅底上几乎看不见
/// （白字画在白底上等于没画）。所以：对比度够就**原样**（这是服务器的配色，不该被我们
/// 改），不够就一档一档压暗，压到读得清为止。
///
/// NOTE: 只压暗不调色相 —— `§c` 红压暗之后还是红，不会变成别的颜色。
QColor readableOn(const QColor &wanted, const QColor &background);

}  // namespace MotdText
