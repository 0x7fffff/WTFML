/**
 * @file ConfigFileText.hpp
 * @brief 「把一份配置文件当文本读写」那点规矩：**解字节 → 正文**、**正文 → 字节**。
 *        两个都是纯函数，一个字节的盘都不碰。
 * @author 0x7fffff
 * @date 2026-09-27
 * @note 这一层存在的唯一理由：**「当文本处理」这件事本身不是免费的**。
 *
 *       用户的判断是对的 —— 盘上那些 config 文件**全部**是文本（实测除了两个本来就没人
 *       会去改的，见下面 BOM 那段旁边那个数字）。所以「按文本处理」是唯一一条能把所有
 *       格式一个不漏全包住的路。但它有一个**会静默改坏东西**的地方，而且不在读、在写：
 *
 *         - **行尾**：Qt 的文本模型把 `\r\n` 和单独的 `\r` **一律规范化成 `\n`**
 *           （2026-09-27 实测：`a\r\nb\r\n` 喂进 QPlainTextEdit 拿回来是 `a\nb\n`）。
 *           而盘上 CRLF 是**主流**不是例外（实测顶层 `.toml` 2171 个、`.json` 848 个）。
 *           用户只改一个数字，一存整份文件每一行都变了 —— 而且屏幕上看不出任何异样。
 *         - **非法 UTF-8**：`QString::fromUtf8` 会把解不动的那个字节换成 `U+FFFD`，写回去
 *           那个字节就**永久坏了**（不可逆，原件已经没了）。
 *         - **BOM**：`QStringDecoder` 解 UTF-8 时**自己把 BOM 吃掉**（实测：带 BOM 的三
 *           字节解出来第一个字符直接是正文），所以"解出来一个字不差"不等于"写回去一个字
 *           不差" —— BOM 得自己记着补。
 *
 *       三条纪律就落在这儿：**原字节留着（没改就逐字节写回）、行尾按原样还回去、
 *       解不动码的不许编辑。**
 *
 * NOTE: 这一层**不做任何"规范化"**：不重排、不缩进、不改引号、不动类型。那些动作每一个都
 *       会静默改坏东西 —— 盘上有把布尔值存成字符串的（`"EnableFZh2z": "true"`），有把
 *       唯一一份说明书放在 `//` 注释里的（`lithostitched.json`、`polylib.json`，它们顶着
 *       `.json` 的名字却是 JSON5，严格 JSON 解析器一个都读不动）。一个"贴心"的往返会把它
 *       们全毁掉，而且不报错。
 */
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

/// 「配置文件当文本读」这一套。**纯函数、不碰盘、不弹话** —— 验证工程直接喂字节进去量。
namespace ConfigFileText {

/// 一行的结尾是哪种。
///
/// NOTE: 记的是**每一行各自的**，不是一个全局开关 —— 混着来的文件是存在的（不同工具往
///       同一个文件上追加过），而"整份按多数派重写一遍"恰好就是上面说的那种静默改坏。
enum class Eol {
    Lf,    ///< `\n`（Unix）
    CrLf,  ///< `\r\n`（Windows）
    Cr,    ///< `\r`（老 Mac；Qt 也把它当换行，所以它必须在这一层有个位置）
};

/// 一份文件解出来的东西。**唯一出处**：想知道一份文件能不能编辑、原来什么行尾，只有这儿。
struct Loaded {
    /// 原样的字节。**改都没改的话存盘就是把它写回去** —— 这一条让"没动过的文件被打开再
    /// 保存"变成一个逐字节的空操作，中间那一层怎么切怎么拼都不影响。
    QByteArray raw;
    /// 正文。**行尾一律是 `'\n'`**（和 QPlainTextEdit 拿出来的那一份一致，见 .cpp 里
    /// split() 那段为什么非得一致）。
    QString text;
    /// 开头有 UTF-8 BOM（`EF BB BF`）。解码器会吃掉它，所以这一位是"写回去时补不补"的
    /// 唯一出处。
    bool bom = false;
    /// 是不是合法 UTF-8。**false ⇒ 只读**（解出来的正文里那些 `U+FFFD` 是**有损**的，
    /// 存回去等于把原件毁了）。
    bool utf8Ok = true;
    /// 太大，不给编辑（见 editableByteLimit()）。**它和 utf8Ok 是两件事** —— 一个说"解不动"、
    /// 一个说"不该在这上面编辑"，两种情况下屏幕上都只该出现"只读"，但为什么只读那句话
    /// 不一样。
    bool tooBig = false;
    /// 第 i 行那个结尾是哪种。长度 = **结尾符的个数**，所以它比行数少一个是正常的
    /// （最后一行不结尾）。
    QList<Eol> ends;
    /// 多数派那种结尾。**新添的行**（正文比原来长出来的那几行）用它。
    Eol dominant = Eol::Lf;
    /// 最后一行后面有没有换行。**Qt 保全这一位**（实测 `a\nb\n` 和 `a\nb` 各自原样回来），
    /// 所以它跟着正文一起往返，不用在这儿单独判。
    bool finalNewline = false;

    /// 能不能编辑。**不是**「能不能打开」—— 任何字节都打得开（只读也是打开）。
    bool editable() const { return utf8Ok && !tooBig; }
    /// 一行的结尾该写什么。
    static QString terminator(Eol eol);
};

/// 把盘上那些字节解成文本。**任何字节序列都接得住**（不会崩、不会抛）。
///
/// NOTE: 进参是**整个文件的字节**，不是路径 —— 这一层不碰盘（读盘那一步在 MainWindow，
///       它才知道该不该读、读不动该说什么话）。
Loaded decode(const QByteArray &raw);

/// 把编辑框里的正文合回一份字节。**纯函数**。
///
/// 行尾的取法：第 i 个结尾用 `original.ends[i]`；**不够用**的那些（用户添了行）拿
/// `original.dominant`。于是"改了一个值"这种最常见的编辑，出去之后**每一行的结尾都和原来
/// 一模一样**，只有那一行正文变了 —— 这是可以逐字节断言的一件事（见 ui_check）。
///
/// @param edited 现在框里的正文。行尾按 `'\n'` 给（QPlainTextEdit::toPlainText() 就是）。
QByteArray encode(const Loaded &original, const QString &edited);

/// 正文切成「行 + 每行结尾 + 最后一行有没有结尾」。给 decode() 和验证工程共用。
struct Split {
    QStringList lines;
    QList<Eol> ends;
    bool finalNewline = false;
};
Split split(const QString &text);

/// 一份文件最多这么大才谈得上编辑（字节）。比这大的**只读打开**。
///
/// NOTE: 这个数是**量出来的**：盘上 17,579 个配置类文件里，中位 0.6 KB、99% 在 32 KB 以下、
///       最大 3.96 MB —— **超过 4 MB 的一个都没有**。所以这道闸今天一个文件都不挡，它是
///       给"将来某个模组往 `config/` 里写了个几百兆的缓存"准备的（那几个 MB 级的现存大户
///       —— `player_storage.json`、`account_bank.json`、`Changelog.274-280.json` —— 本来
///       也不是设置，是数据快照）。
/// NOTE: 这是"人会不会去手改"的界线，**不是技术极限** —— QPlainTextEdit 撑得住更大的。
qint64 editableByteLimit();

}  // namespace ConfigFileText
