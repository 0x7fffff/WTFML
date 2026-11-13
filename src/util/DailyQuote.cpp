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
    case int(Origin::PoetryDb): return Origin::PoetryDb;
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

bool englishDay(const QDate &date) { return (julianDay(date) * 2654435761u) % 100u < 60u; }

QVector<int> englishOrder(const QDate &date, int count) {
    QVector<int> order;
    for (int i = 0; i < count; ++i) order.append(i);
    if (count <= 1) return order;
    const quint32 day = (julianDay(date) * 2246822519u) % quint32(count);
    const int start = int(day);
    const int step = int((julianDay(date) * 2654435761u) % quint32(count - 1)) + 1;
    int cursor = start;
    QVector<int> walked;
    for (int i = 0; i < count; ++i) {
        walked.append(cursor);
        cursor = (cursor + step) % count;
    }
    for (int i = 0; i < count; ++i)
        if (!walked.contains(i)) walked.append(i);
    return walked;
}

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

QString poetryLine(const QJsonArray &lines) {
    for (const QJsonValue &value : lines) {
        const QString text = clean(value.toString());
        if (text.size() < 20 || text.size() > 80) continue;
        const QChar last = text.at(text.size() - 1);
        if (last != QLatin1Char('.') && last != QLatin1Char('!') && last != QLatin1Char('?')) continue;
        if (!text.at(0).isUpper()) continue;
        return text;
    }
    return QString();
}

Quote parsePoetryDb(const QByteArray &body) {
    Quote quote;
    const QJsonArray array = QJsonDocument::fromJson(body).array();
    if (array.isEmpty()) return quote;
    const QJsonObject object = array.first().toObject();
    quote.text = poetryLine(object.value(QStringLiteral("lines")).toArray());
    quote.author = clean(object.value(QStringLiteral("author")).toString());
    quote.source = clean(object.value(QStringLiteral("title")).toString());
    quote.script = scriptOf(quote.text);
    quote.origin = Origin::PoetryDb;
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
    // 开发时从 hitokoto / ZenQuotes / PoetryDB 抽样，只按长度挑，不做口吻过滤。
    // 英文 22（ZenQuotes 9 + PoetryDB 13）/ 中文 10 / 日文 4，每条都过得了 maxLength()。
    static const QVector<Quote> quotes = {
        // ---- 英文 ----
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
        {QStringLiteral("Fools are my theme, let Satire be my song."),
         QStringLiteral("George Gordon, Lord Byron"),
         QStringLiteral("English Bards, and Scotch Reviewers"), Script::Latin, Origin::PoetryDb},
        {QStringLiteral("I know I should be happy with them."),
         QStringLiteral("Walt Whitman"), QStringLiteral("This Moment, Yearning and Thoughtful"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("But the benches are crammed, like a garden in flower."),
         QStringLiteral("George Gordon, Lord Byron"), QStringLiteral("The Blues: A Literary Eclogue"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("She makes her own face, and does not make her rhymes."),
         QStringLiteral("George Gordon, Lord Byron"), QStringLiteral("From the French"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("O'er his uncertain shadow droops the day."),
         QStringLiteral("Lord Alfred Tennyson"), QStringLiteral("The Progress of Spring"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("O SUN of real peace! O hastening light!"),
         QStringLiteral("Walt Whitman"), QStringLiteral("O Sun of Real Peace"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("And the moon no cloud is staining."),
         QStringLiteral("Percy Bysshe Shelley"), QStringLiteral("Fragment: A Roman's Chamber"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("It is an hour before the hour of dawn."),
         QStringLiteral("Algernon Charles Swinburne"), QStringLiteral("Tiresias"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("That other, where we see as we are seen!"),
         QStringLiteral("Lord Alfred Tennyson"), QStringLiteral("Geraint and Enid"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("Barefoot boy, with cheek of tan!"),
         QStringLiteral("John Greenleaf Whittier"), QStringLiteral("The Barefoot Boy"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("On what wings of the terror of darkness he rideth?"),
         QStringLiteral("William Morris"), QStringLiteral("Song VI: Cherish Life that Abideth"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("Above is death--and we are death."),
         QStringLiteral("Percy Bysshe Shelley"), QStringLiteral("Death"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("To eat the world's due, by the grave and thee."),
         QStringLiteral("William Shakespeare"), QStringLiteral("Sonnet 1"),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("All silent, and all--damned!"),
         QStringLiteral("Percy Bysshe Shelley"),
         QStringLiteral("Peter Bell the Third"), Script::Latin, Origin::PoetryDb},
        {QStringLiteral("Naming me the fifty poems finished!"),
         QStringLiteral("Robert Browning"), QStringLiteral("One Word More: To E.B.B."),
         Script::Latin, Origin::PoetryDb},
        {QStringLiteral("So far between my pleasures are and few."),
         QStringLiteral("Christina Rossetti"), QStringLiteral("Monna Innominata"),
         Script::Latin, Origin::PoetryDb},
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
