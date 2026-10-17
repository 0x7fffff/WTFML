/**
 * @file DownloadDiagnostics.cpp
 * @brief 下载链路"特供版"详细日志的实现（判据与动机见头文件）。
 * @author 0x7fffff
 * @date 2026-10-17
 */

#include "DownloadDiagnostics.hpp"

#include "AppPaths.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkInformation>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QOperatingSystemVersion>
#include <QSslSocket>
#include <QStorageInfo>
#include <QStringConverter>
#include <QSysInfo>
#include <QTextStream>

namespace {

/// 一份诊断日志最多写这么多。普通一次"装一个整合包 + 五千个资源"的明细是几 MB；
/// 64 MiB 是"明显出事了"（比如某条链子在无限重试）的止损线 —— 到了就停写明细，
/// 免得测试员的盘被一个日志涨满。
constexpr qint64 kMaxLogBytes = 64LL * 1024 * 1024;

QMutex g_mutex;
QFile g_file;
QTextStream g_stream;
bool g_initAttempted = false;
bool g_capped = false;
qint64 g_written = 0;
QString g_path;
QElapsedTimer g_clock;

QString timestamp() {
    if (!g_clock.isValid()) {
        g_clock.start();
    }
    return QStringLiteral("[DL +%1s]").arg(g_clock.elapsed() / 1000.0, 0, 'f', 3);
}

/// 调用方必须持有 g_mutex。
void openFileLocked(const QString &logsDir) {
    if (g_initAttempted) {
        return;
    }
    g_initAttempted = true;
    if (!g_clock.isValid()) {
        g_clock.start();
    }
    const QString dir = logsDir.isEmpty() ? AppPaths::minecraftDir() + QStringLiteral("/logs")
                                          : logsDir;
    QDir().mkpath(dir);
    g_path = QDir(dir).filePath(QStringLiteral("wtfml-download.log"));
    g_file.setFileName(g_path);
    if (g_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        g_stream.setDevice(&g_file);
        g_stream.setEncoding(QStringConverter::Utf8);
    } else {
        // 开不了就只是不落这份文件；qInfo 那一份仍在 wtfml.log 里。
        g_path.clear();
    }
}

QString maskProxyText(const QString &text) {
    const QUrl url(text);
    if (url.isValid() && !url.host().isEmpty()) {
        return DownloadDiag::safeUrl(url);
    }
    // `host:port` / 纯主机名：不按 URL 解，原样返回（这种写法里通常没有凭据）。
    return text;
}

QString proxyText(const QNetworkProxy &proxy) {
    QString type;
    switch (proxy.type()) {
    case QNetworkProxy::NoProxy: type = QStringLiteral("none"); break;
    case QNetworkProxy::DefaultProxy: type = QStringLiteral("default"); break;
    case QNetworkProxy::Socks5Proxy: type = QStringLiteral("socks5"); break;
    case QNetworkProxy::HttpProxy: type = QStringLiteral("http"); break;
    case QNetworkProxy::HttpCachingProxy: type = QStringLiteral("http-caching"); break;
    case QNetworkProxy::FtpCachingProxy: type = QStringLiteral("ftp-caching"); break;
    }
    return QStringLiteral("%1 %2:%3%4")
        .arg(type, proxy.hostName())
        .arg(proxy.port())
        .arg(proxy.user().isEmpty() ? QString() : QStringLiteral(" user=%1").arg(proxy.user()));
}

QString envText(const char *name) {
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? QStringLiteral("(unset)") : maskProxyText(value);
}

}  // namespace

namespace DownloadDiag {

bool enabled() {
#if defined(WTFML_DIAG_DOWNLOAD)
    static const bool on = true;
#else
    static const bool on = [] {
        const QString value = qEnvironmentVariable("WTFML_DOWNLOAD_DEBUG").trimmed();
        return !value.isEmpty() && value != QLatin1String("0") &&
               value.compare(QLatin1String("false"), Qt::CaseInsensitive) != 0;
    }();
#endif
    return on;
}

void init(const QString &logsDir) {
    if (!enabled()) {
        return;
    }
    QMutexLocker locker(&g_mutex);
    openFileLocked(logsDir);
}

QString logFilePath() {
    QMutexLocker locker(&g_mutex);
    return g_path;
}

void line(const QString &text) {
    if (!enabled()) {
        return;
    }
    const QString body = timestamp() + QLatin1Char(' ') + text;
    // 走 qInfo 的第一份：进 wtfml.log（+ stderr）。qInfo 自己那条 handler 不碰这一层。
    qInfo().noquote() << body;
    QMutexLocker locker(&g_mutex);
    if (!g_initAttempted) {
        openFileLocked(QString());
    }
    if (!g_file.isOpen() || g_capped) {
        return;
    }
    const QString record = QDateTime::currentDateTime().toString(Qt::ISODateWithMs) +
                           QLatin1Char(' ') + body;
    g_stream << record << Qt::endl;
    g_stream.flush();
    g_written += record.size() + 1;
    if (g_written > kMaxLogBytes) {
        g_capped = true;
        g_stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
                 << QStringLiteral(" [DL] 诊断日志超过 %1 MiB，后面的明细只进 wtfml.log / stderr。")
                        .arg(kMaxLogBytes / 1024 / 1024)
                 << Qt::endl;
        g_stream.flush();
    }
}

void logEnvironment() {
    if (!enabled()) {
        return;
    }
    line(QStringLiteral("======== WTFML download diagnostics ========"));
#if defined(WTFML_DIAG_DOWNLOAD)
    line(QStringLiteral("build: diagnostic (WTFML_DIAG_DOWNLOAD=1, always on)%1")
             .arg(QStringLiteral(" flavor=" WTFML_BUILD_FLAVOR)));
#else
    line(QStringLiteral("build: normal (enabled by WTFML_DOWNLOAD_DEBUG)"));
#endif
    line(QStringLiteral("launcher: %1 %2")
             .arg(QCoreApplication::applicationName(),
                  QCoreApplication::applicationVersion()));
    line(QStringLiteral("qt: runtime=%1 built-with=%2 platform=%3")
             .arg(QString::fromLatin1(qVersion()), QString::fromLatin1(QT_VERSION_STR),
                  QGuiApplication::platformName()));
    line(QStringLiteral("os: %1 (%2 %3) arch=%4 process=%5-bit")
             .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(),
                  QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture())
             .arg(QSysInfo::WordSize));
    const QOperatingSystemVersion osVersion = QOperatingSystemVersion::current();
    line(QStringLiteral("os-version: %1.%2.%3 (%4)")
             .arg(osVersion.majorVersion())
             .arg(osVersion.minorVersion())
             .arg(osVersion.microVersion())
             .arg(osVersion.name()));
    line(QStringLiteral("locale: %1 ui=%2")
             .arg(QLocale::system().name(), QLocale::system().uiLanguages().join(',')));
    // 时间 / 时区也记一笔：日志里只有本地时间，和服务器时间对不上时这是唯一能换算的东西。
    line(QStringLiteral("time: %1 tz=%2")
             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                  QDateTime::currentDateTime().timeZoneAbbreviation()));
    line(QStringLiteral("paths: appDir=%1 cwd=%2 dataDir=%3")
             .arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath()),
                  QDir::toNativeSeparators(QDir::currentPath()),
                  QDir::toNativeSeparators(AppPaths::minecraftDir())));
    line(QStringLiteral("diagnostic-log: %1")
             .arg(logFilePath().isEmpty() ? QStringLiteral("(not open; see wtfml.log)")
                                          : QDir::toNativeSeparators(logFilePath())));

    // 代理：测试员那边最常见的一类"只有我失败"的原因。
    line(QStringLiteral("proxy-env: http_proxy=%1 https_proxy=%2 all_proxy=%3 no_proxy=%4")
             .arg(envText("http_proxy"), envText("https_proxy"), envText("all_proxy"),
                  envText("no_proxy")));
    line(QStringLiteral("proxy-application: %1").arg(proxyText(QNetworkProxy::applicationProxy())));
    for (const char *urlText : {"https://launchermeta.mojang.com",
                                "https://bmclapi2.bangbang93.com"}) {
        const QUrl url(QString::fromLatin1(urlText));
        const QList<QNetworkProxy> proxies =
            QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(url));
        QStringList parts;
        for (const QNetworkProxy &proxy : proxies) {
            parts << proxyText(proxy);
        }
        line(QStringLiteral("proxy-system[%1]: %2")
                 .arg(url.host(), parts.isEmpty() ? QStringLiteral("(none)") : parts.join(QStringLiteral("; "))));
    }

    line(QStringLiteral("tls: supported=%1 runtime=%2 built-with=%3")
             .arg(QSslSocket::supportsSsl() ? QStringLiteral("yes") : QStringLiteral("no"),
                  QSslSocket::sslLibraryVersionString(),
                  QSslSocket::sslLibraryBuildVersionString()));

    if (QNetworkInformation::loadDefaultBackend() && QNetworkInformation::instance()) {
        QNetworkInformation *info = QNetworkInformation::instance();
        line(QStringLiteral("network: reachability=%1 metered=%2")
                 .arg(int(info->reachability()))
                 .arg(info->isMetered() ? QStringLiteral("yes") : QStringLiteral("no")));
    } else {
        line(QStringLiteral("network: QNetworkInformation backend unavailable"));
    }

    const QStorageInfo storage(AppPaths::minecraftDir());
    line(QStringLiteral("disk: valid=%1 free=%2 MiB total=%3 MiB fs=%4")
             .arg(storage.isValid() ? QStringLiteral("yes") : QStringLiteral("no"))
             .arg(storage.isValid() ? storage.bytesAvailable() / (1024 * 1024) : -1)
             .arg(storage.isValid() ? storage.bytesTotal() / (1024 * 1024) : -1)
             .arg(QString::fromLatin1(storage.fileSystemType())));
    line(QStringLiteral("======== download events ========"));
}

QString safeUrl(const QString &url) {
    return safeUrl(QUrl(url));
}

QString safeUrl(const QUrl &url) {
    if (!url.isValid()) {
        return QStringLiteral("(invalid) %1").arg(url.toString());
    }
    return url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemovePassword |
                               QUrl::RemoveFragment);
}

}  // namespace DownloadDiag
