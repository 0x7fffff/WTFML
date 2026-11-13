#include "Support.hpp"

#include "util/DailyQuote.hpp"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

/// 「每日一言」判据层的脱网单测：语种、限长、按日期选题、三种 JSON 的解析、缓存与句子池、
/// 36 条兜底。一个网络请求都不发 —— 喂的全是写死的字节。
void runDailyQuoteTests() {
    using namespace DailyQuote;
    section(QStringLiteral("DQ 每日一言：语种、限长、按日期选题"));

    // ---- 文字种类判别 ----
    check(scriptOf(QStringLiteral("长河千嶂，大荒孤城。")) == Script::Han, "DQ1 纯汉字 → 中文档");
    check(scriptOf(QStringLiteral("君を救う。")) == Script::Kana, "DQ2 平假名 → 日文档");
    check(scriptOf(QStringLiteral("カタカナ")) == Script::Kana, "DQ3 片假名 → 日文档");
    check(scriptOf(QStringLiteral("ロングー")) == Script::Kana, "DQ4 长音符「ー」在片假名区，算日文");
    check(scriptOf(QStringLiteral("時々")) == Script::Kana, "DQ5「々」算日文（重复记号）");
    check(scriptOf(QStringLiteral("The significance of life is living.")) == Script::Latin,
          "DQ6 纯拉丁 → 拉丁档");
    check(scriptOf(QStringLiteral("Hello 世界")) == Script::Han, "DQ7 拉丁 + 汉字 → 按汉字档算（含汉字无假名）");
    check(scriptOf(QStringLiteral("ありがとう world")) == Script::Kana, "DQ8 假名优先于拉丁");

    // ---- 分档限长 ----
    check(maxLength(Script::Kana) == 30 && maxLength(Script::Han) == 24 &&
              maxLength(Script::Latin) == 80,
          "DQ9 分档上限 30 / 24 / 80");
    const auto quote = [](const QString &text) {
        Quote q;
        q.text = text;
        q.script = scriptOf(text);
        return q;
    };
    check(fitsLength(quote(QString(24, QChar(0x4E2D)))), "DQ10 中文 24 字刚好过关");
    check(!fitsLength(quote(QString(25, QChar(0x4E2D)))), "DQ11 中文 25 字超限");
    check(fitsLength(quote(QString(30, QChar(0x3042)))), "DQ12 日文 30 字刚好过关");
    check(!fitsLength(quote(QString(31, QChar(0x3042)))), "DQ13 日文 31 字超限");
    check(fitsLength(quote(QString(80, QLatin1Char('a')))), "DQ14 拉丁 80 字符刚好过关");
    check(!fitsLength(quote(QString(81, QLatin1Char('a')))), "DQ15 拉丁 81 字符超限");
    check(!fitsLength(quote(QStringLiteral("   "))), "DQ16 空文本（只有空白）不算一句");
    check(!withinHardLimit(QString(129, QChar(0x4E2D))), "DQ17 硬上限：129 字符直接拒掉");
    check(withinHardLimit(QString(128, QChar(0x4E2D))), "DQ18 硬上限：128 字符还在限内");

    // ---- 按日期定语种 ----
    const auto day = [](const char *iso) { return QDate::fromString(QString::fromLatin1(iso), Qt::ISODate); };
    check(julianDay(day("1970-01-01")) == 2440588u, "DQ19 儒略日就是 QDate::toJulianDay（1970-01-01 = 2440588）");
    // NOTE: 乘法是在 quint32 上做的（会溢出回绕），期望值就是这么算出来的 ——
    // 换成 64 位再取模，某些日子会换语种。
    check(englishDay(day("2026-11-13")), "DQ20 2026-11-13 是英文日（46 < 60）");
    check(!englishDay(day("2026-11-20")), "DQ21 2026-11-20 是一言日（93 ≥ 60）");
    check(!englishDay(day("2027-02-11")), "DQ22 边界：正好 60 算一言日（< 60 才是英文）");
    check(englishDay(day("2027-01-01")), "DQ23 2027-01-01 是英文日（59）");
    // 同一天算两次必须一样（界面上重启不能换语种）。
    check(englishDay(day("2026-11-13")) == englishDay(day("2026-11-13")), "DQ24 同一天两次算出来一致");

    // ---- 英文站点的先后 ----
    for (const char *iso : {"2026-11-13", "2026-11-14", "2026-11-15", "2027-01-01"}) {
        const QVector<int> order = englishOrder(day(iso), 2);
        const bool permuted = order.size() == 2 && order.contains(0) && order.contains(1);
        check(permuted, QStringLiteral("DQ25 %1 的站点先后是 0/1 的一个排列")
                            .arg(QString::fromLatin1(iso)),
              QStringLiteral("%1,%2").arg(order.value(0)).arg(order.value(1)));
    }
    check(englishOrder(day("2026-11-13"), 1) == QVector<int>{0}, "DQ26 只启用一个站点时先后就是它自己");
    check(englishOrder(day("2026-11-13"), 2) == englishOrder(day("2026-11-13"), 2),
          "DQ27 同一天的站点先后不变");

    section(QStringLiteral("DQ 三种响应：一言 / ZenQuotes / PoetryDB"));

    // ---- 一言（from_who 为 null 是真实返回里就有的一种） ----
    const QByteArray hitokoto =
        R"({"id":77,"hitokoto":"下一次重逢，将是何年何月？天空必将见证。","type":"a","from":"SOLA","from_who":null})";
    const Quote jp = parseHitokoto(hitokoto);
    check(jp.isValid() && jp.text == QStringLiteral("下一次重逢，将是何年何月？天空必将见证。"),
          "DQ28 一言：正文解析出来");
    check(jp.author.isEmpty(), "DQ29 一言：from_who 为 null → 作者空串");
    check(jp.source == QStringLiteral("SOLA"), "DQ30 一言：from → 出处");
    check(jp.script == Script::Han && jp.origin == Origin::Hitokoto, "DQ31 一言：语种与来源标对");
    const Quote ja = parseHitokoto(QByteArrayLiteral("{\"hitokoto\":\"君を救う。\",\"from\":\"Brand New\",\"from_who\":\"大森元貴\"}"));
    check(ja.isValid() && ja.script == Script::Kana && ja.author == QStringLiteral("大森元貴"),
          "DQ32 一言：日文句 + 作者");
    check(!parseHitokoto(QByteArrayLiteral("{\"from\":\"x\"}")).isValid(), "DQ33 一言：没有 hitokoto 字段 → 无效");
    check(!parseHitokoto(QByteArrayLiteral("not json")).isValid(), "DQ34 一言：坏 JSON → 无效");
    check(!parseHitokoto(QByteArrayLiteral("")).isValid(), "DQ35 一言：空响应体 → 无效");

    // ---- ZenQuotes ----
    const QByteArray zen = R"([{"q":"The significance of life is living.","a":"Jiddu Krishnamurti"}])";
    const Quote zq = parseZenQuotes(zen);
    check(zq.isValid() && zq.text == QStringLiteral("The significance of life is living."),
          "DQ36 ZenQuotes：q → 正文");
    check(zq.author == QStringLiteral("Jiddu Krishnamurti") && zq.source.isEmpty(),
          "DQ37 ZenQuotes：a → 作者，没有出处");
    check(zq.origin == Origin::ZenQuotes, "DQ38 ZenQuotes：来源标对");
    check(!parseZenQuotes(QByteArrayLiteral("[]")).isValid(), "DQ39 ZenQuotes：空数组 → 无效");
    check(!parseZenQuotes(QByteArrayLiteral("{}")).isValid(), "DQ40 ZenQuotes：对象而不是数组 → 无效");

    // ---- PoetryDB（按行挑） ----
    const QJsonArray lines = QJsonArray{
        QStringLiteral("and then the moon rose over the hill."),  // 小写开头
        QStringLiteral("short line."),                            // 太短
        QStringLiteral("And muche more; for little heaviness"),   // 没有句末标点
        QStringLiteral("Fools are my theme, let Satire be my song."),  // ✓
        QStringLiteral("And muche more; for little heaviness"),        // 没有句末标点
    };
    check(poetryLine(lines) == QStringLiteral("Fools are my theme, let Satire be my song."),
          "DQ41 PoetryDB 取行：句末标点 + 20–80 字 + 首字母大写");
    check(poetryLine(QJsonArray{QStringLiteral("short line.")}) .isEmpty(), "DQ42 一首里挑不到 → 空串（调用方换一首）");
    const QByteArray poetry =
        R"([{"title":"English Bards","author":"Lord Byron","lines":["THE PROLOGUE.","Fools are my theme, let Satire be my song.","And muche more;"]}])";
    const Quote pq = parsePoetryDb(poetry);
    check(pq.isValid() && pq.text == QStringLiteral("Fools are my theme, let Satire be my song."),
          "DQ43 PoetryDB：从 lines 里挑出一行");
    check(pq.author == QStringLiteral("Lord Byron") && pq.source == QStringLiteral("English Bards"),
          "DQ44 PoetryDB：作者 = author，出处 = title");
    check(!parsePoetryDb(QByteArrayLiteral("[]")).isValid(), "DQ45 PoetryDB：空数组 → 无效");
    check(!parsePoetryDb(QByteArrayLiteral("[{\"lines\":[]}]")).isValid(), "DQ46 PoetryDB：一行都挑不到 → 无效");

    section(QStringLiteral("DQ 当天缓存、句子池、兜底与署名"));

    // ---- 当天缓存 ----
    const QDate today = QDate(2026, 11, 13);
    const Quote cached = quote(QStringLiteral("长河千嶂，大荒孤城，历历在目。"));
    const QJsonObject entry = cacheEntry(today, cached);
    check(entry.value(QStringLiteral("date")).toString() == QStringLiteral("2026-11-13"),
          "DQ47 缓存里记着日期");
    check(cachedQuote(entry, today).text == cached.text, "DQ48 当天：缓存命中，拿回原句");
    check(!cachedQuote(entry, today.addDays(1)).isValid(), "DQ49 跨天：昨天的不算今天的缓存");
    check(!cachedQuote(QJsonObject{}, today).isValid(), "DQ50 没有缓存 → 无效（该发请求了）");
    check(cachedQuote(entry, today).script == Script::Han, "DQ51 缓存里的语种跟着句子走");

    // ---- 句子池 ----
    QVector<Quote> pool;
    for (int i = 0; i < 35; ++i) {
        Quote q = quote(QStringLiteral("第 %1 句中文示例文本。").arg(i));
        q.author = QStringLiteral("作者%1").arg(i);
        poolAppend(pool, q);
    }
    check(pool.size() == 30, "DQ52 句子池只留最近 30 条", QString::number(pool.size()));
    check(pool.last().author == QStringLiteral("作者34") && pool.first().author == QStringLiteral("作者5"),
          "DQ53 留的是最近那 30 条（先进先出）");
    poolAppend(pool, pool.last());
    check(pool.size() == 30, "DQ54 重复正文的句子只留一条");
    check(pool.last().author == QStringLiteral("作者34"), "DQ55 重复的那条挪到最新");
    Quote latin;
    latin.text = QStringLiteral("The significance of life is living.");
    latin.script = Script::Latin;
    poolAppend(pool, latin);
    check(poolPick(pool, Script::Latin, 0).text == latin.text, "DQ56 池里按语种取：拉丁那一句取得回来");
    check(!poolPick(pool, Script::Kana, 0).isValid(), "DQ57 池里没有日文 → 空（该走兜底了）");
    const QVector<Quote> restored = poolFromJson(poolToJson(pool));
    check(restored.size() == pool.size() && restored.last().text == pool.last().text,
          "DQ58 句子池过一遍 JSON 不失真");
    QVector<Quote> two;
    for (const char *t : {"甲句。", "乙句。"}) two.append(quote(QString::fromUtf8(t)));
    check(poolPick(two, Script::Han, 0).text == two.at(0).text &&
              poolPick(two, Script::Han, 1).text == two.at(1).text,
          "DQ59 池里同语种按「日期 % 条数」轮着取，不会天天同一句");

    // ---- 兜底 36 条 ----
    int latinCount = 0, hanCount = 0, kanaCount = 0;
    bool allFit = true;
    for (const Quote &q : fallbackQuotes()) {
        if (!fitsLength(q)) allFit = false;
        switch (q.script) {
        case Script::Latin: ++latinCount; break;
        case Script::Han: ++hanCount; break;
        case Script::Kana: ++kanaCount; break;
        }
    }
    check(latinCount == 22 && hanCount == 10 && kanaCount == 4,
          "DQ60 兜底 36 条：英文 22 / 中文 10 / 日文 4",
          QStringLiteral("%1/%2/%3").arg(latinCount).arg(hanCount).arg(kanaCount));
    check(allFit, "DQ61 兜底每一条都过得了分档限长");
    check(fallbackFor(Script::Latin, 0).isValid() && fallbackFor(Script::Kana, 0).isValid() &&
              fallbackFor(Script::Han, 0).isValid(),
          "DQ62 三个语种都取得到兜底句");

    // ---- 署名（悬停提示） ----
    Quote both;
    both.text = QStringLiteral("x");
    both.author = QStringLiteral("大森元貴");
    both.source = QStringLiteral("Brand New");
    check(attribution(both) == QStringLiteral("— 大森元貴《Brand New》"), "DQ63 署名：作者 + 出处");
    Quote onlyAuthor = both;
    onlyAuthor.source.clear();
    check(attribution(onlyAuthor) == QStringLiteral("— 大森元貴"), "DQ64 署名：只有作者");
    Quote onlySource = both;
    onlySource.author.clear();
    check(attribution(onlySource) == QStringLiteral("—《Brand New》"), "DQ65 署名：只有出处");
    Quote none;
    none.text = QStringLiteral("x");
    check(attribution(none).isEmpty(), "DQ66 署名：都没有 → 空串（不设 tooltip）");

}
