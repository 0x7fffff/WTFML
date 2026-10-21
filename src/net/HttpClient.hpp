/**
 * @file HttpClient.hpp
 * @brief 一次**同步** HTTP 请求（QEventLoop + 超时），返回状态码、正文和解析出来的 JSON。
 * @note 从 AuthManager.cpp 里搬出来的。原来那套 `sendJson()` 是它文件内的匿名函数、只支持
 * GET/POST，够登录链用；换成皮肤/披风那几条接口之后要 PUT 和 DELETE，于是把它提出来
 * 共用 —— 不是为了少写代码，是为了**超时那一段只有一份**（"超时了要 abort、abort
 * 之后不能再去读正文"这种细节，抄第二遍必然抄漏一处，漏了就是偶发崩溃）。
 *
 * 为什么同步：调用方是界面上的一次点击，本来就等着一个结果（登录、换皮肤）。异步化只会让
 * 调用点多出一堆状态机。内嵌事件循环期间界面仍然会重画（Qt 在处理事件）。
 *
 * NOTE: 认 HTTP 响应和认网络失败是**两件事**（见 Reply 的字段）：4xx/5xx 的正文里往往带着
 * 具体的错误码，那比 Qt 那句 "server replied" 有用得多，所以拿到状态码就不算网络错误。
 */

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

class HttpClient {
public:
    enum class Verb { Get, Post, Put, Delete };

    /// 一次请求的结果。分成"拿到了 HTTP 响应"（status 有值）和"根本没连上/超时"
    /// （networkError 有值）两种。
    struct Reply {
        int status = -1;
        QJsonObject json;
        QByteArray raw;
        QString networkError;
        bool ok() const { return status >= 200 && status < 300; }
    };

    /// 请求超时。**唯一出处**：登录链和外观接口都用它（15 秒）。
    static constexpr int kDefaultTimeoutMs = 15000;

    /// HTTP 429。
    ///
    /// NOTE: 它和别的 4xx/5xx **不是一回事** —— 它说的不是"这次请求哪儿写错了"，而是
    /// "你问得太勤了"。所以唯一该做的事是**等一等**：改参数没用，马上重试只会继续
    /// 被挡（两个调用方因此都不在收到它之后自动重发）。
    /// 判据和那句文案放在这里，是因为两个调用方看到的是同一句官方话。不集中在这里，
    /// 登录链和皮肤接口就会各写各的，把 `TOO_MANY_REQUESTS` 原样端给用户看，
    /// 看着像接口坏了。
    static constexpr int kStatusTooManyRequests = 429;

    /// HTTP 304：**没变**。
    ///
    /// 只有带了条件头（`If-None-Match` / `If-Modified-Since`）去问才可能出现这个码 ——
    /// 它说的不是"出错"，是"你要的东西还是你手上那份，我不再发一遍了"。所以它是**成功**
    /// 的一种，判据里不能掉进 `!okStatus()` 那个"失败"分支去（那会把"没变"当成"没取到"，
    /// 症状是作者没换图标、这一格反而被擦掉）。唯一用户是 ResourceStore 的图标核对那一发。
    static constexpr int kStatusNotModified = 304;

    /// 限流那一句（**唯一出处**）。@param what 这一步在做什么（"读取披风失败"这种），
    /// 拼在这句话最前头。
    static QString throttledText(const QString &what);

    /**
     * @brief 发一个请求并同步等结果。
     * @param manager 用谁的 QNetworkAccessManager（调用方持有）。
     * @param url 地址。
     * @param verb 动作。DELETE 不带正文（这几种接口的 DELETE 都是"取消某种状态"）。
     * @param body 请求体；GET/DELETE 传空。
     * @param contentType 空则不加 Content-Type 头（GET 不需要）。
     * @param bearer 非空则加 Authorization: Bearer 头。
     * @param timeoutMs 见 kDefaultTimeoutMs。
     * @param extraHeaders 额外的请求头（**追加式**：放在最后，既有调用点一个字都不用改）。
     * 现在唯一的用户是 CurseForge 的 `x-api-key` —— 用户自己在设置里填了 key 时
     * 才带（见 ModpackImport::curseForgeApiBase 与 VersionManager::resolveCurseForgeFiles）。
     * NOTE: 名字里带连字符的头**必须**走 setRawHeader（QNetworkRequest 那几个枚举
     * 里没有它），这也正是这里收原始字节对而不是 QVariant 的原因。
     * @return 见 Reply。超时会把请求 abort 掉（**之后别再去读那个 reply**）。
     */
    static Reply send(QNetworkAccessManager *manager, const QUrl &url, Verb verb,
                      const QByteArray &body = QByteArray(),
                      const QByteArray &contentType = QByteArray(),
                      const QByteArray &bearer = QByteArray(),
                      int timeoutMs = kDefaultTimeoutMs,
                      const QList<QPair<QByteArray, QByteArray>> &extraHeaders = {});
};
