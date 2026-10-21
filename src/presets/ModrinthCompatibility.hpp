/**
 * @file ModrinthCompatibility.hpp
 * @brief 真网络版的兼容检查提供方：Modrinth（+ 可选的 CurseForge）。
 *
 * 【为什么是异步的】
 *
 * 安装页那张弹窗必须在**问网络之前**就出来（§7.2 第一条），所以这一层绝不能用
 * `HttpClient` 那种同步等法 —— 那是给"点一下、界面停住等结果"的登录链用的。
 * 这里一个请求一个 `QNetworkReply`，超时自己 abort，结论交回 `CompatibilityChecker`。
 *
 * 【结论的口径】
 *
 * · 200 + 非空发行版列表 ⇒ `Matched`；
 * · 200 + 空列表 / 404 ⇒ `Missing`（平台**明确**说没有）；
 * · 429 / 5xx / 网络错误 / 超时 ⇒ `Unknown` —— ★ 一律不报缺失（§7.2 的验收）。
 *
 * CurseForge 官方接口要 `x-api-key`。没配置时这一侧一律 `Unknown`
 * （"没有配置 CurseForge 接口"），**不**拿别的接口猜。
 */

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include "PresetCompatibility.hpp"

class QNetworkAccessManager;

namespace Presets {

class PlatformCompatibilityProvider : public QObject, public CompatibilityProvider {
    Q_OBJECT

public:
    /// @param modrinthRoot Modrinth 接口根（`https://api.modrinth.com/v2`，不带结尾斜杠）。
    explicit PlatformCompatibilityProvider(
        const QUrl &modrinthRoot = QUrl(QStringLiteral("https://api.modrinth.com/v2")),
        QObject *parent = nullptr);
    ~PlatformCompatibilityProvider() override;

    /// CurseForge 那一侧（官方接口 + 用户自己填的 key）。@param base 空 = 这一侧不可用。
    void setCurseForge(const QUrl &base, const QByteArray &apiKey);
    bool curseForgeAvailable() const { return m_curseForgeAvailable; }

    void query(const ModReference &reference, const CompatibilityContext &context,
               Reply reply) override;

    /// 版本查询单发超时。★ 和 `CompatibilityChecker::Options::timeoutMs` 是两层：
    /// 检查器那一层是"这一项多久没结论就报 Unknown"的兜底，这里是让网络请求自己收手。
    void setTransferTimeoutMs(int ms) { m_transferTimeoutMs = ms; }

private:
    void queryModrinth(const ModReference &reference, const CompatibilityContext &context,
                       const Reply &reply, int attempt = 0);
    void queryCurseForge(const ModReference &reference, const CompatibilityContext &context,
                         const Reply &reply, int attempt = 0);

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
