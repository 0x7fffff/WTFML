#include "controllers/DailyQuoteController.hpp"

#include "net/ControlGet.hpp"
#include "settings/LauncherSettings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

namespace {

/// 句子池与当天缓存在 QSettings 里各占一格（JSON 字符串）。和 `Launcher/*` 那组键分开：
/// 这两个是缓存，删掉只丢一句，不影响任何设置。
constexpr const char kCacheKey[] = "Quote/Cache";
constexpr const char kPoolKey[] = "Quote/Pool";

constexpr const char kUserAgent[] = "WTFML-Launcher";

/// 英文站点表：删掉一行就是停用（`enabled = false` 也行）。先后不写死在这儿，
/// 由 `DailyQuote::englishOrder()` 按日期排。
struct Source {
    const char *name;
    const char *url;
    DailyQuote::Quote (*parse)(const QByteArray &);
    bool enabled;
    bool retryWithRandom;  ///< ZenQuotes：今天那句太长就再抽两次随机的
};
const Source kEnglishSources[] = {
    {"zenquotes", "https://zenquotes.io/api/today", &DailyQuote::parseZenQuotes, true, true},
    {"poetrydb", "https://poetrydb.org/random/1", &DailyQuote::parsePoetryDb, true, false},
};
constexpr const char kZenRandomUrl[] = "https://zenquotes.io/api/random";
constexpr const char kHitokotoUrl[] = "https://v1.hitokoto.cn/?encode=json&max_length=30";
constexpr const char kHitokotoFallbackUrl[] =
    "https://international.v1.hitokoto.cn/?encode=json&max_length=30";

QNetworkRequest makeRequest(const QString &url) {
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept", "application/json");
    return request;
}

}  // namespace

DailyQuoteController::DailyQuoteController(QObject *owner, LauncherSettings *settings)
    : QObject(owner), m_settings(settings), m_network(new QNetworkAccessManager(this)) {}

DailyQuoteController::~DailyQuoteController() = default;

bool DailyQuoteController::enabled() const {
    return m_settings == nullptr || m_settings->dailyQuote();
}

void DailyQuoteController::setEnabled(bool enabled) {
    if (m_settings != nullptr) m_settings->setDailyQuote(enabled);
    if (m_job) {
        delete m_job.data();  // 析构里 abort，不会再有回调
        m_job = nullptr;
    }
    m_steps.clear();
    m_next = 0;
    m_sent = 0;
    m_randomTries = 0;
    m_running = false;
    if (!enabled && shown) shown(DailyQuote::Quote{}, false);
    if (enabled) refresh();
}

void DailyQuoteController::refresh() {
    if (!enabled()) {
        if (shown) shown(DailyQuote::Quote{}, false);
        return;
    }
    const QDate today = QDate::currentDate();
    if (today != m_day) {
        m_day = today;
        m_running = false;
        m_steps.clear();
        m_next = 0;
        m_sent = 0;
        m_randomTries = 0;
    }
    QSettings settings;
    const auto cached = DailyQuote::cachedQuote(
        QJsonDocument::fromJson(settings.value(QString::fromLatin1(kCacheKey)).toByteArray())
            .object(),
        today);
    if (cached.isValid()) {
        publish(cached);
        return;
    }
    publishFallback(false);
    if (!m_running) request();
}

void DailyQuoteController::publish(const DailyQuote::Quote &quote) {
    if (shown) shown(quote, quote.isValid());
}

void DailyQuoteController::publishFallback(bool networkFailed) {
    Q_UNUSED(networkFailed);
    const quint32 day = DailyQuote::julianDay(m_day);
    // 一言日不知道会拿到中文还是日文：先按中文找，再按日文。英文日只有拉丁一句。
    const QVector<DailyQuote::Script> order =
        DailyQuote::englishDay(m_day)
            ? QVector<DailyQuote::Script>{DailyQuote::Script::Latin}
            : QVector<DailyQuote::Script>{DailyQuote::Script::Han, DailyQuote::Script::Kana};
    const QVector<DailyQuote::Quote> pool = loadPool();
    for (const DailyQuote::Script script : order) {
        const DailyQuote::Quote picked = DailyQuote::poolPick(pool, script, day);
        if (picked.isValid()) {
            publish(picked);
            return;
        }
    }
    for (const DailyQuote::Script script : order) {
        const DailyQuote::Quote picked = DailyQuote::fallbackFor(script, day);
        if (picked.isValid()) {
            publish(picked);
            return;
        }
    }
}

void DailyQuoteController::request() {
    m_running = true;
    m_steps.clear();
    m_next = 0;
    m_sent = 0;
    m_randomTries = 0;
    if (DailyQuote::englishDay(m_day)) {
        QVector<int> enabled;
        for (int i = 0; i < int(sizeof(kEnglishSources) / sizeof(kEnglishSources[0])); ++i)
            if (kEnglishSources[i].enabled) enabled.append(i);
        for (const int index : DailyQuote::englishOrder(m_day, enabled.size())) {
            const Source &source = kEnglishSources[enabled.at(index)];
            Step step;
            step.url = QString::fromLatin1(source.url);
            step.parse = source.parse;
            m_steps.append(step);
        }
    } else {
        m_steps.append(Step{QString::fromLatin1(kHitokotoUrl), &DailyQuote::parseHitokoto, false});
        m_steps.append(
            Step{QString::fromLatin1(kHitokotoFallbackUrl), &DailyQuote::parseHitokoto, false});
    }
    sendNext();
}

void DailyQuoteController::sendNext() {
    while (m_next < m_steps.size() && m_sent < kMaxRequests) {
        const Step step = m_steps.at(m_next);
        ++m_sent;
        m_job = RequestBudget::ControlGet::start(
            this, m_network, makeRequest(step.url), kTimeoutMs,
            [this](QNetworkReply *reply, const QString &error) { handleReply(reply, error); },
            kBodyLimit);
        return;
    }
    if (m_next >= m_steps.size()) m_running = false;
}

void DailyQuoteController::handleReply(QNetworkReply *reply, const QString &queueError) {
    m_job = nullptr;
    const Step step = m_next < m_steps.size() ? m_steps.at(m_next) : Step{};
    bool ok = false;
    DailyQuote::Quote quote;
    if (reply != nullptr) {
        if (queueError.isEmpty() && reply->error() == QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            if (status == 200 && body.size() <= kBodyLimit) {
                quote = step.parse != nullptr ? step.parse(body) : DailyQuote::Quote{};
                ok = quote.isValid();
            }
        }
        reply->deleteLater();
    }
    if (ok && DailyQuote::fitsLength(quote)) {
        finish(quote);
        return;
    }
    // ZenQuotes 的「今天」太长：同一个站点补抽随机的（最多两次）；别的失败一律换下一个站点。
    const bool zenToday = step.parse == &DailyQuote::parseZenQuotes && !step.random;
    if (zenToday && ok && m_randomTries < 2 && m_sent < kMaxRequests) {
        ++m_next;
        Step random;
        random.url = QString::fromLatin1(kZenRandomUrl);
        random.parse = &DailyQuote::parseZenQuotes;
        random.random = true;
        m_steps.insert(m_next, random);
        ++m_sent;
        m_job = RequestBudget::ControlGet::start(
            this, m_network, makeRequest(random.url), kTimeoutMs,
            [this](QNetworkReply *reply2, const QString &error) { handleReply(reply2, error); },
            kBodyLimit);
        return;
    }
    if (step.random) ++m_randomTries;
    ++m_next;
    sendNext();
}

void DailyQuoteController::finish(const DailyQuote::Quote &quote) {
    m_running = false;
    m_steps.clear();
    publish(quote);
    QSettings settings;
    settings.setValue(QString::fromLatin1(kCacheKey),
                      QJsonDocument(DailyQuote::cacheEntry(m_day, quote))
                          .toJson(QJsonDocument::Compact));
    QVector<DailyQuote::Quote> pool = loadPool();
    DailyQuote::poolAppend(pool, quote);
    settings.setValue(QString::fromLatin1(kPoolKey),
                      QJsonDocument(DailyQuote::poolToJson(pool)).toJson(QJsonDocument::Compact));
}

QVector<DailyQuote::Quote> DailyQuoteController::loadPool() const {
    QSettings settings;
    return DailyQuote::poolFromJson(
        QJsonDocument::fromJson(settings.value(QString::fromLatin1(kPoolKey)).toByteArray())
            .array());
}
