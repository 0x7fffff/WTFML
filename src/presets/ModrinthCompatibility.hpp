/**
 * @file ModrinthCompatibility.hpp
 * @brief 真网络版的兼容检查提供方：Modrinth（+ 可选的 CurseForge）。
 *
 * 【为什么是异步的】
 *
 * 安装页那张弹窗必须在**问网络之前**就出来，所以这一层绝不能用
 * `HttpClient` 那种同步等法 —— 那是给"点一下、界面停住等结果"的登录链用的。
 * 这里一个请求一个 `RequestBudget::ControlGet` 作业：与加载器版本查询、模板/清单等
 * 短请求共用 Control 名额，排队和传输合起来一个截止，超时由作业自己收手，
 * 结论交回 `CompatibilityChecker`。
 *
 * 【结论的口径】
 *
 * · 结构有效、上下文与发布筛选匹配的发行版 ⇒ `Matched`；
 * · 完整有效答复中没有匹配 / 404 ⇒ `Missing`；
 * · 格式损坏、分页不完整、429 / 5xx / 网络错误 / 超时 ⇒ `Unknown`。
 *
 * CurseForge 官方接口需要用户自己的 key；控制器与资源页共用来源规则，
 * 未配置 key 时可用显式配置的公共镜像，来源及凭据摘要均进入缓存范围。
 */

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QPointer>
#include <memory>

#include "PresetCompatibility.hpp"

class QNetworkAccessManager;
class QNetworkReply;

namespace Presets {

class PlatformCompatibilityProvider : public QObject, public CompatibilityProvider {
    Q_OBJECT

public:
    /// @param modrinthRoot Modrinth 接口根（`https://api.modrinth.com/v2`，不带结尾斜杠）。
    explicit PlatformCompatibilityProvider(
        const QUrl &modrinthRoot = QUrl(QStringLiteral("https://api.modrinth.com/v2")),
        QObject *parent = nullptr);
    ~PlatformCompatibilityProvider() override;

    /// CurseForge 官方接口需用户key；显式配置的公共镜像允许无key。base空表示不可用。
    void setCurseForge(const QUrl &base, const QByteArray &apiKey);
    bool curseForgeAvailable() const { return m_curseForgeAvailable; }
    QString cacheScope() const override;

    void query(const ModReference &reference, const CompatibilityContext &context,
               Reply reply) override;
    Cancel queryCancelable(const ModReference &reference, const CompatibilityContext &context,
                           Reply reply) override;

    /// 版本查询单发超时。★ 和 `CompatibilityChecker::Options::timeoutMs` 是两层：
    /// 检查器那一层是"这一项多久没结论就报 Unknown"的兜底，这里是让网络请求自己收手。
    /// 这一发**排队 + 传输**一起算进这个截止（和 `HttpClient` 共用 Control 名额），
    /// 所以它比"纯粹的在网线上待多久"短一点点 —— 那正是 P2 的口径。
    void setTransferTimeoutMs(int ms) { m_transferTimeoutMs = ms; }

private:
    /// 取消 = 直接删掉那个作业。排在队里的作业被删掉之后**一个字节都不会发**；
    /// 已经发出去的那一发在析构里 abort。取消不回调（结论由检查器按超时收）。
    struct QueryState { bool cancelled = false; QPointer<QObject> job; };
    void queryModrinth(const ModReference &reference, const CompatibilityContext &context,
                       const Reply &reply, int attempt, const std::shared_ptr<QueryState> &state);
    void queryCurseForge(const ModReference &reference, const CompatibilityContext &context,
                         const Reply &reply, int attempt, const std::shared_ptr<QueryState> &state);

    /// 瞬态失败（连不上 / 5xx）的退避间隔；429 不退避（那会越退越糟），直接记一段时间限流。
    static int retryDelayMs(int attempt);

    QNetworkAccessManager *m_network = nullptr;
    QUrl m_modrinthRoot;
    QUrl m_curseForgeBase;
    QByteArray m_curseForgeKey;
    bool m_curseForgeAvailable = false;
    int m_transferTimeoutMs = 12000;
    /// 总共试几次（含第一发）。退避只对瞬态失败；平台明确答复（200/404）从不重试。
    int m_maxAttempts = 2;
    /// 限流窗口（`QDateTime::currentMSecsSinceEpoch()`；0 = 没被限流）。两个平台各记各的。
    qint64 m_modrinthThrottledUntilMs = 0;
    qint64 m_curseForgeThrottledUntilMs = 0;
};

}  // namespace Presets
