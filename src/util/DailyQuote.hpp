#pragma once
/**
 * @file DailyQuote.hpp
 * @brief 主页「每日一言」的判据层：语种/限长/日期选题、三种接口的解析、缓存与句子池的序列化。
 *
 * 这一层**不联网、不碰界面**，只吃字节、吐结构：请求怎么发在
 * `controllers/DailyQuoteController`，字怎么画在 `ui/MainWindowUI` 与 `Theme::quoteFont()`。
 * 放在这里是为了能被 verify 直接单测（假响应），不必起窗口也不必起服务器。
 *
 * 三条约定（用户 11-13 定稿）：
 * - 语种按本地日期定，当天不变：`(julianDay * 2654435761u) % 100 < 60` 是英文日，其余是一言日。
 * - 限长分档：含假名 ≤30 字、含汉字无假名 ≤24 字、纯拉丁 ≤80 字符；硬上限 128 字符、响应体 8 KiB。
 * - 回退顺序：同语种其他站点 → 最近成功句子池（同语种按日取一条）→ 内置 36 条。
 */

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace DailyQuote {

/// 文字种类：决定限长与字体（假名 / 汉字 / 拉丁）。
enum class Script { Latin, Han, Kana };

/// 句子从哪来。只用于句子池与日志，界面上不写站名。
enum class Origin { Hitokoto, ZenQuotes, PoetryDb, Fallback };

struct Quote {
    QString text;    ///< 正文
    QString author;  ///< 作者，可能为空（一言的 `from_who` 就是 nullable）
    QString source;  ///< 出处，可能为空（一言 `from` / PoetryDB `title`；ZenQuotes 没有）
    Script script = Script::Latin;
    Origin origin = Origin::Fallback;

    bool isValid() const { return !text.trimmed().isEmpty(); }
};

/// 含假名（平假名 / 片假名 / 长音符「ー」/「々」）→ Kana；含汉字 → Han；其余 → Latin。
Script scriptOf(const QString &text);

/// 分档限长的上界（字符数，不是字节）。1 个码点算 1（QString 的 size()）。
int maxLength(Script script);

/// 硬上限：正文 ≤128 字符、非空。分档限长另由 `fitsLength()` 判。
bool withinHardLimit(const QString &text);
bool fitsLength(const Quote &quote);

/// 本地日期 → 儒略日（1900 起算不了那么早，用 QDate::toJulianDay 的同一套数）。
quint32 julianDay(const QDate &date);

/// 英文日（60%）：`(julianDay * 2654435761u) % 100 < 60`。
bool englishDay(const QDate &date);

/// 英文站点的先后（0..count-1 的一个排列），按日期定、同一天不变。
QVector<int> englishOrder(const QDate &date, int count);

// ---------------- 三种响应 ----------------

/// 一言：`{hitokoto, from, from_who}`，`from_who` 可为 null。
Quote parseHitokoto(const QByteArray &body);

/// ZenQuotes：`[{q, a}]`。不判长度（调用方按 80 字符决定要不要换 `/api/random`）。
Quote parseZenQuotes(const QByteArray &body);

/// PoetryDB：`[{title, author, lines:[...]}]`。按取行规则挑一行，挑不到返回无效句。
Quote parsePoetryDb(const QByteArray &body);

/// PoetryDB 的单行判据：以 `.` `!` `?` 结尾、20–80 字符、首字母大写。挑不到返回空串。
QString poetryLine(const QJsonArray &lines);

// ---------------- 缓存与句子池 ----------------

/// 当天缓存：`{date, quote:{...}}`。日期不是今天、或句子无效 → 空句。
QJsonObject cacheEntry(const QDate &date, const Quote &quote);
Quote cachedQuote(const QJsonObject &entry, const QDate &today);

/// 句子池：最近 30 条（带语种）。同一个日期只留最近一条，正文重复的只留最新那次。
QJsonArray poolToJson(const QVector<Quote> &pool);
QVector<Quote> poolFromJson(const QJsonArray &array);
void poolAppend(QVector<Quote> &pool, const Quote &quote);
/// 池里同语种按 `julianDay % 条数` 取一条；没有 → 空句。
Quote poolPick(const QVector<Quote> &pool, Script script, quint32 day);

/// 内置兜底 36 条（英文 22 / 中文 10 / 日文 4），全部满足分档限长。
const QVector<Quote> &fallbackQuotes();
/// 兜底里同语种按 `day % 条数` 取一条。
Quote fallbackFor(Script script, quint32 day);

// ---------------- 署名 ----------------

/// 悬停提示：「— 作者《出处》」；只有作者「— 作者」；只有出处「—《出处》」；都没有 → 空串。
QString attribution(const Quote &quote);

void toJson(const Quote &quote, QJsonObject &out);
Quote fromJson(const QJsonObject &object);

}  // namespace DailyQuote
