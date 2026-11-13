#include "util/DailyQuote.hpp"

#include <QJsonDocument>
#include <QJsonValue>
#include <QStringList>

namespace DailyQuote {
namespace {

/// 假名区：平假名、片假名、长音符「ー」（U+30FC 在片假名区里），外加「々」(U+3005) ——
/// 它本身就是日文重复记号，出现即按日文算。
bool hasKana(const QString &text) {
    for (const QChar ch : text) {
        const ushort u = ch.unicode();
        if ((u >= 0x3040 && u <= 0x30FF) || u == 0x3005) return true;
    }
    return false;
}

bool hasHan(const QString &text) {
    for (const QChar ch : text) {
        const ushort u = ch.unicode();
        if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF) || u == 0x3007) return true;
    }
    return false;
}

QString clean(const QString &text) { return text.simplified(); }

QJsonObject quoteFields(const Quote &quote) {
    QJsonObject object;
    object.insert(QStringLiteral("text"), quote.text);
    object.insert(QStringLiteral("author"), quote.author);
    object.insert(QStringLiteral("source"), quote.source);
    object.insert(QStringLiteral("script"), int(quote.script));
    object.insert(QStringLiteral("origin"), int(quote.origin));
    return object;
}

Script scriptFromInt(int value) {
    switch (value) {
    case int(Script::Han): return Script::Han;
    case int(Script::Kana): return Script::Kana;
    default: return Script::Latin;
    }
}

Origin originFromInt(int value) {
    switch (value) {
    case int(Origin::Hitokoto): return Origin::Hitokoto;
    case int(Origin::ZenQuotes): return Origin::ZenQuotes;
    default: return Origin::Fallback;
    }
}

}  // namespace

Script scriptOf(const QString &text) {
    if (hasKana(text)) return Script::Kana;
    if (hasHan(text)) return Script::Han;
    return Script::Latin;
}

int maxLength(Script script) {
    switch (script) {
    case Script::Kana: return 30;
    case Script::Han: return 24;
    case Script::Latin: return 80;
    }
    return 24;
}

bool withinHardLimit(const QString &text) { return !text.trimmed().isEmpty() && text.size() <= 128; }

bool fitsLength(const Quote &quote) {
    return withinHardLimit(quote.text) && quote.text.size() <= maxLength(quote.script);
}

quint32 julianDay(const QDate &date) { return quint32(date.toJulianDay()); }

/// NOTE: 这个乘法**是故意让它溢出回绕的**（quint32 × 2^32 取模）—— 规格里那个
/// `(julianDay * 2654435761u) % 100` 就是 32 位上的算法。换成 64 位再取模，某些日子
/// 会换成另一个语种，而"哪天是英文日"由 verify 的 DQ20–DQ23 钉着。
bool englishDay(const QDate &date) { return (julianDay(date) * 2654435761u) % 100u < 60u; }

Quote parseHitokoto(const QByteArray &body) {
    Quote quote;
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    if (object.isEmpty()) return quote;
    quote.text = clean(object.value(QStringLiteral("hitokoto")).toString());
    quote.source = clean(object.value(QStringLiteral("from")).toString());
    quote.author = clean(object.value(QStringLiteral("from_who")).toString());
    quote.script = scriptOf(quote.text);
    quote.origin = Origin::Hitokoto;
    if (!withinHardLimit(quote.text)) return Quote{};
    return quote;
}

Quote parseZenQuotes(const QByteArray &body) {
    Quote quote;
    const QJsonArray array = QJsonDocument::fromJson(body).array();
    if (array.isEmpty()) return quote;
    const QJsonObject object = array.first().toObject();
    quote.text = clean(object.value(QStringLiteral("q")).toString());
    quote.author = clean(object.value(QStringLiteral("a")).toString());
    quote.script = scriptOf(quote.text);
    quote.origin = Origin::ZenQuotes;
    if (!withinHardLimit(quote.text)) return Quote{};
    return quote;
}

QJsonObject cacheEntry(const QDate &date, const Quote &quote) {
    QJsonObject entry;
    entry.insert(QStringLiteral("date"), date.toString(Qt::ISODate));
    entry.insert(QStringLiteral("quote"), quoteFields(quote));
    return entry;
}

Quote cachedQuote(const QJsonObject &entry, const QDate &today) {
    if (entry.value(QStringLiteral("date")).toString() != today.toString(Qt::ISODate)) return Quote{};
    const Quote quote = fromJson(entry.value(QStringLiteral("quote")).toObject());
    return fitsLength(quote) ? quote : Quote{};
}

QJsonArray poolToJson(const QVector<Quote> &pool) {
    QJsonArray array;
    for (const Quote &quote : pool) array.append(quoteFields(quote));
    return array;
}

QVector<Quote> poolFromJson(const QJsonArray &array) {
    QVector<Quote> pool;
    for (const QJsonValue &value : array) {
        const Quote quote = fromJson(value.toObject());
        if (quote.isValid()) pool.append(quote);
    }
    return pool;
}

void poolAppend(QVector<Quote> &pool, const Quote &quote) {
    if (!quote.isValid()) return;
    for (int i = pool.size() - 1; i >= 0; --i)
        if (pool.at(i).text == quote.text) pool.removeAt(i);
    pool.append(quote);
    while (pool.size() > 30) pool.removeFirst();
}

Quote poolPick(const QVector<Quote> &pool, Script script, quint32 day) {
    QVector<Quote> same;
    for (const Quote &quote : pool)
        if (quote.script == script) same.append(quote);
    if (same.isEmpty()) return Quote{};
    return same.at(int(day % quint32(same.size())));
}

QString attribution(const Quote &quote) {
    const bool hasAuthor = !quote.author.isEmpty();
    const bool hasSource = !quote.source.isEmpty();
    if (hasAuthor && hasSource) return QStringLiteral("— %1《%2》").arg(quote.author, quote.source);
    if (hasAuthor) return QStringLiteral("— %1").arg(quote.author);
    if (hasSource) return QStringLiteral("—《%1》").arg(quote.source);
    return QString();
}

void toJson(const Quote &quote, QJsonObject &out) { out = quoteFields(quote); }

Quote fromJson(const QJsonObject &object) {
    Quote quote;
    quote.text = object.value(QStringLiteral("text")).toString();
    quote.author = object.value(QStringLiteral("author")).toString();
    quote.source = object.value(QStringLiteral("source")).toString();
    quote.script = scriptFromInt(object.value(QStringLiteral("script")).toInt());
    quote.origin = originFromInt(object.value(QStringLiteral("origin")).toInt());
    if (!quote.isValid()) return Quote{};
    return quote;
}

const QVector<Quote> &fallbackQuotes() {
    // 开发时从 hitokoto / ZenQuotes 抽样，只按长度挑，不做口吻过滤。
    // 英文 22（全部 ZenQuotes，每条 ≤80 字符）/ 中文 10 / 日文 4，每条都过得了 maxLength()。
    static const QVector<Quote> quotes = {
        // ---- 英文（ZenQuotes） ----
        {QStringLiteral("Insanity is relative. It depends on who has who locked in what cage."),
         QStringLiteral("Ray Bradbury"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("Supreme excellence consists of breaking the enemy's resistance without fighting."),
         QStringLiteral("Sun Tzu"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("Intense love does not measure, it just gives."),
         QStringLiteral("Mother Teresa"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("The significance of life is living."),
         QStringLiteral("Jiddu Krishnamurti"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("Stay away from people who makes you feel like you are wasting their time."),
         QStringLiteral("Paulo Coelho"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("The Only Thing That Is Constant Is Change"),
         QStringLiteral("Heraclitus"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("Education breeds confidence. Confidence breeds hope. Hope breeds peace."),
         QStringLiteral("Confucius"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("True love stories never have endings."),
         QStringLiteral("Richard Bach"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("You make your own luck if you stay at it long enough."),
         QStringLiteral("Naval Ravikant"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("We are shaped and fashioned by what we love."),
         QStringLiteral("Johann Wolfgang von Goethe"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("To lead an orchestra, you must turn your back on the crowd."),
         QStringLiteral("Aristotle"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("Throw me to the wolves and I will return leading the pack."),
         QStringLiteral("Seneca"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("It's okay to be discouraged. It's not okay to quit."),
         QStringLiteral("Ryan Holiday"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("All know the way; few actually walk it."),
         QStringLiteral("Bodhidharma"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("What we desire makes us vulnerable."),
         QStringLiteral("Ryan Holiday"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("Be confident, not certain."),
         QStringLiteral("Eleanor Roosevelt"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("We have a choice everyday regarding the attitude we will embrace for that day."),
         QStringLiteral("Charles Swindoll"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("Motivation is what gets you started. Habit is what keeps you going."),
         QStringLiteral("Jim Rohn"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("Everything flows, nothing stands still."),
         QStringLiteral("Heraclitus"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("A hut full of laughter is richer than a palace full of sadness."),
         QStringLiteral("Zen Proverb"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("Success is never final; failure is never fatal."),
         QStringLiteral("Conrad Hilton"), QString(), Script::Latin, Origin::ZenQuotes},
        {QStringLiteral("Imagination is more important than knowledge."),
         QStringLiteral("Albert Einstein"), QString(), Script::Latin, Origin::ZenQuotes},
        // ---- 中文 ----
        {QStringLiteral("把半生的将就，换成那自由。"),
         QStringLiteral("王健美"), QStringLiteral("山风山风等等我"), Script::Han, Origin::Hitokoto},
        {QStringLiteral("长河千嶂，大荒孤城，历历在目。"),
         QStringLiteral("令"), QStringLiteral("明日方舟"), Script::Han, Origin::Hitokoto},
        {QStringLiteral("以有涯随无涯，殆已！"),
         QStringLiteral("庄周"), QStringLiteral("庄子"), Script::Han, Origin::Hitokoto},
        {QStringLiteral("即使人生是一场悲剧，也应该笑着把人生演完。"),
         QStringLiteral("尼采"), QStringLiteral("查拉图斯特如是说"), Script::Han, Origin::Hitokoto},
        {QStringLiteral("此生谁料，心在天山，身老沧洲。"),
         QStringLiteral("陆游"), QStringLiteral("诉衷情·当年万里觅封侯"), Script::Han, Origin::Hitokoto},
        {QStringLiteral("我想那缥缈的空中，定然有美丽的街市。"),
         QStringLiteral("郭沫若"), QStringLiteral("天上的街市"), Script::Han, Origin::Hitokoto},
        {QStringLiteral("大鹏一日同风起，扶摇直上九万里。"),
         QStringLiteral("李白"), QStringLiteral("上李邕"), Script::Han, Origin::Hitokoto},
        {QStringLiteral("人类的伟大之处就在于面对恐惧时的崇高姿态。"),
         QStringLiteral("修罗特海姆"), QStringLiteral("JOJO的奇妙冒险"), Script::Han, Origin::Hitokoto},
        {QStringLiteral("没有人的文明毫无意义。"),
         QStringLiteral("马兆"), QStringLiteral("流浪地球2"), Script::Han, Origin::Hitokoto},
        {QStringLiteral("你不能放弃，就算跛着脚也要爬上高峰。"),
         QStringLiteral("楚门"), QStringLiteral("楚门的世界"), Script::Han, Origin::Hitokoto},
        // ---- 日文 ----
        {QStringLiteral("希望とシェイクハンドして、鏡を見つめ直して、真新しい自分。"),
         QStringLiteral("大森元貴"), QStringLiteral("Brand New"), Script::Kana, Origin::Hitokoto},
        {QStringLiteral("君を救う。"),
         QStringLiteral("大森元貴"), QStringLiteral("Brand New"), Script::Kana, Origin::Hitokoto},
        {QStringLiteral("結局僕らは、人と人。"),
         QStringLiteral("大森元貴"), QStringLiteral("Brand New"), Script::Kana, Origin::Hitokoto},
        {QStringLiteral("さかな！"),
         QStringLiteral("錦木千束"), QStringLiteral("莉可丽丝"), Script::Kana, Origin::Hitokoto},
    };
    return quotes;
}

Quote fallbackFor(Script script, quint32 day) {
    QVector<Quote> same;
    for (const Quote &quote : fallbackQuotes())
        if (quote.script == script) same.append(quote);
    if (same.isEmpty()) return Quote{};
    return same.at(int(day % quint32(same.size())));
}

}  // namespace DailyQuote
