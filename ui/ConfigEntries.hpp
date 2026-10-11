/**
 * @file ConfigEntries.hpp
 * @brief 「把一份配置文件里有哪些设置项读出来」那一层：**正文 → 条目清单**。
 *        纯函数，不碰盘、不弹话、不解析成对象树 —— 只认**位置**。
 * @author 0x7fffff
 * @date 2026-09-27
 * @note 这一层是用户后面那句问话的答案：「能不能通过外接解析库来做到条目-设置值来一一
 *       对应」。答案是**能，而且不需要外接库** —— 理由是量出来的：
 *
 *         - 盘上 147 个 `config` 目录里，`.toml` 2375 个（**2285 个带注释**、**593 个把
 *           范围明写在 `#Range: 1 ~ 32767` 里**）、`config/` 底下 ≤200 KB 的 `.json`
 *           1109 个（其中 **447 个本身就是「一层 key→值」**）、`.json5` 460 个、
 *           `.cfg` 754 个（1.12 那代 Forge 把类型写在键名里：`B:shouldShowDuplicateTrees`）。
 *         - 也就是说「说明 / 范围 / 默认值」这些东西**常常就在注释里**，而**类型**文件自己
 *           就说了（带引号=字符串、裸数字=数字、`true`/`false`=布尔、`[]`/`{}`=容器）。
 *           schema 表单缺的是"这一条什么意思"，不是"这一条是什么值"。
 *
 *       为什么不用现成的库（`toml++` / `nlohmann/json` / `jsoncpp` …）：**它们解决不了
 *       三分之二的格式**。Qt 自带的 `QJsonDocument` 与 `nlohmann/json`、`rapidjson`
 *       **都不认注释**（盘上 460 个 `.json5` + 380 个带注释的 `.json` 直接读不动），
 *       而 `jsoncpp` 认注释却不给**源码位置** —— 而这一层要的恰恰是位置。`toml++` 有
 *       `source_region()`，可它只管 TOML 那三分之一，还得往仓库里 vendor 一个依赖。
 *       于是这里自己走一趟：**一个容错扫描器认全部格式**，产出物就是位置。
 *
 * 【认得出什么、认不出什么】（写清楚是为了别让人以为它是个解析器）
 *
 *   认：`键 = 值` / `键: 值`（含 `.cfg` 的 `B:` 类型前缀、带引号的键）、`[section]` 与
 *       `[[table]]` 段头、`name {`（CFG 那种块）、四种注释开头（`#`、`;`、`//`、块注释）、
 *       带引号的键与值（含转义与 TOML 的三引号）、一行写完的 `[...]` / `{...}`。
 *   不认：多行容器**内部**再往下的东西只给"下潜"（见下面 NOTE），数组里散着的裸标量
 *       不产生条目（`[\n1,\n2\n]` 这种），也不是 TOML/JSON 的完整实现（不查语法对不对）。
 *
 * NOTE: **故意不解析成对象树**。这一层的客户是「条目索引」那一格：它要的是"这份文件里
 *       有哪些键、每个键的值在哪几个字符上"。做成树反而要先把"原样的字节"丢掉 —— 而
 *       "不动用户的字节"是这个功能的一条硬要求（见 ConfigFileText.hpp）。
 *
 * NOTE: 区间的单位是**字符**（QString 的下标，UTF-16 码元），不是字节 —— 因为它的客户是
 *       `QPlainTextEdit`（`QTextCursor::setPosition()` 吃的就是这个）。别拿它去切
 *       `QByteArray`。
 */
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/// 「这份文件里有哪些设置项」这一套。**纯函数** —— 喂一段正文进去量就行。
namespace ConfigEntries {

/// 一个值是哪一类。**判据是那串字面量本身**（带引号=字符串、`true`=布尔……），
/// 不是"这一条该是什么" —— 盘上有把布尔存成字符串的（`"EnableFZh2z": "true"`），
/// 那它就老老实实是字符串，和文件说的一致。
enum class Kind {
    Bool,     ///< `true` / `false`
    Number,   ///< 整数 / 小数 / 指数 / `0x` 十六进制
    String,   ///< 带引号（`"` 或 `'`，含 TOML 的三引号）
    Array,    ///< 一行写完的 `[...]`
    Object,   ///< 一行写完的 `{...}`
    Other,    ///< 认不出类型（裸词、`inf` 之外的怪东西）
};

/// 一条设置项。
struct Entry {
    /// 键路径，段与段之间用 `.`（数组里的元素带 `[i]`，如 `list[0].name`）。**给人看的**。
    QString path;
    /// 路径拆开的那几段。树那一格按它分组。
    QStringList segments;
    /// 键本身（`segments` 最后那一段）。
    QString key;
    /// 值的**原样文本**（含引号，不含两头的空白）。要显示成什么样是客户的事。
    QString value;
    Kind kind = Kind::Other;
    /// 值在正文里的区间：`[valueBegin, valueEnd)`，**字符**下标（见文件头那 NOTE）。
    int valueBegin = 0;
    int valueEnd = 0;
    /// 值在第几行（0 起）。"这份文件里有两条一样的键"时靠它区分。
    int line = 0;
    /// 这个键**上面那一段注释**（整行注释才算，连续几行接在一起；注释符已经剥掉）。
    /// 认不出来就是空串 —— 而认出来的时候，它常常就是这份配置唯一的一份说明书。
    QString note;
    /// 从注释里认出来的范围原话（`#Range: 1 ~ 32767` → `1 ~ 32767`）。空 = 这一条没写。
    QString range;
};

/// 这份文件的"方言"。**只影响一件事**：裸值读到哪儿为止（见 scan()）。
///
/// ★ 2026-09-29 加。用户原话：「`.properties` / `.cfg` 的值应当读到行尾，不该认逗号」。
///   起因是量出来的两类真实条目被吃掉（两边的原文都在盘上）：
///
///   ① `S:hexblack=#1e1b1b`（`chisel.cfg:226`、`BetterAchievements.cfg:5` 那种 1.12
///      Forge 的颜色）—— 值以 `#` 起头，被当成行内注释 ⇒ **整条设置项凭空消失**。
///   ② `particle$noCulling=com.lowdragmc…,com.brandon3055…` —— 逗号被当成元素分隔符，
///      值从第一个逗号处截断，后面那半截还被**当成正文重新扫了一遍**。
///
///   ①的根子不在方言上（JSON5 里裸 `#` 起头的值一样是值），所以那条判据按"`#` 前面有
///   没有空白"改，两种方言共用；**只有②要按方言分派**：逗号在 JSON/JSON5/TOML 里是
///   元素分隔符，在 `.properties` 那一族里就只是值里的一个字符。
enum class Dialect {
    /// `.json` / `.json5` / `.jsonc` / `.toml`（以及**认不出来的后缀**）：逗号分元素。
    /// 认不出来的走这一档 = **保持 2026-09-29 之前的样子**，不让任何一份没预料到的
    /// 文件因为这次改动换一种读法。
    Jsonish,
    /// `.properties` / `.cfg` / `.ini` / `.conf`：那一族没有容器语法，**逗号是普通字符**，
    /// 值一路读到行尾。
    KeyValue,
};

/// 按文件后缀认方言（`.cfg` / `.properties` / `.ini` / `.conf` → KeyValue，其余 Jsonish）。
///
/// NOTE: 只看**路径字符串**的后缀，**不碰盘**（这一层是纯函数，见文件头）。所以传进来
///       一个不存在的路径也能用，结果一样。
///
/// 依据是 2026-09-29 现数的一份分布（`<游戏目录>` 底下所有 `config` 目录、深度 ≤2）：
/// `json 2683 / toml 2356 / cfg 635 / properties 363 / json5 283 / txt 196 / ini 134 /
///  .config 110 / yml 41 / conf 33 / jsonc 21`。表里只放**语法上确定是"键=值、无容器"**
/// 的那四个后缀（共 1165 个文件）；`txt` / `.config` / `yml` 那几类语法不确定，
/// 一律走 Jsonish（= 老行为）。
Dialect dialectForPath(const QString &path);

/// 一份正文里能认出来的条目。**任何正文都接得住**（半截 JSON、乱码、空文件都不崩）。
///
/// @param dialect 裸值读到哪儿为止（见 Dialect）。**默认 Jsonish = 老行为** —— 意思是不传
///                这一位的老调用方一个字节的行为都不变。
///
/// NOTE: 上限见 `maxEntries()`（= 5000）—— 到了就**停下来**（不是"报错"）：索引是给人
///       点着跳的，再多既没人看也白占内存。
QList<Entry> scan(const QString &text, Dialect dialect = Dialect::Jsonish);

/// 一次最多认多少条（见 scan() 的 NOTE）。客户拿它判"是不是被截断了"。
int maxEntries();

/// 那一类叫什么（「布尔」「数字」…）。界面上的提示和验证工程的断言都走这一个出处。
QString kindName(Kind kind);

}  // namespace ConfigEntries
