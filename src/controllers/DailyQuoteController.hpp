#pragma once
/**
 * @file DailyQuoteController.hpp
 * @brief 主页「每日一言」的取句：后台异步请求、当天缓存、句子池与兜底。
 *
 * 判据（语种、限长、解析、缓存格式）全在 `util/DailyQuote.hpp`，这里只负责
 * 「什么时候问谁、失败以后换谁」。三条硬约束：
 * - **不阻塞**：请求全走 `RequestBudget::ControlGet`（4 秒截止、8 KiB 正文上限），
 *   主页先显示缓存 / 池 / 兜底那一句，网络回来再替换。
 * - **一天一条**：当天有缓存就不再请求；跨天才重新问。
 * - **关掉就不问**：`refresh()` 在关着的时候连缓存都不读，界面上那一行由调用方收起来。
 */

#include "util/DailyQuote.hpp"

#include <QDate>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>

class LauncherSettings;
class QNetworkAccessManager;
class QNetworkReply;
namespace RequestBudget {
class ControlGet;
}

class DailyQuoteController final : public QObject {
public:
    DailyQuoteController(QObject *owner, LauncherSettings *settings);
    ~DailyQuoteController() override;

    /// 主页那行就位后调一次；开关拨开时也调它。重复调用只是重算，不起第二个请求。
    void refresh();

    /// 关掉时掐掉在飞的请求、复位当天状态；再打开会立刻按当前日期重来一遍。
    void setEnabled(bool enabled);
    bool enabled() const;

    /// 该显示的一句。`visible = false` 表示这一行不显示、不占位。
    std::function<void(const DailyQuote::Quote &, bool)> shown;

    static constexpr int kTimeoutMs = 4000;
    static constexpr int kBodyLimit = 8 * 1024;
    /// 一次启动最多发几个请求：一言 2 发（主 + 备），英文 3 发（ZenQuotes 今天 + 两次随机）。
    static constexpr int kMaxRequests = 5;

private:
    /// 一条待发的 GET。`random` 只给 ZenQuotes 的 `/api/random` 用（超长时补发的）。
    struct Step {
        QString url;
        DailyQuote::Quote (*parse)(const QByteArray &) = nullptr;
        bool random = false;
    };

    void publish(const DailyQuote::Quote &quote);
    void publishFallback(bool networkFailed);
    void request();
    void sendNext();
    void handleReply(QNetworkReply *reply, const QString &queueError);
    void finish(const DailyQuote::Quote &quote);
    void saveCache(const DailyQuote::Quote &quote, const QDate &today);
    QVector<DailyQuote::Quote> loadPool() const;

    LauncherSettings *m_settings = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<RequestBudget::ControlGet> m_job;
    QVector<Step> m_steps;
    int m_next = 0;
    int m_sent = 0;
    int m_randomTries = 0;
    QDate m_day;
    bool m_running = false;
};
