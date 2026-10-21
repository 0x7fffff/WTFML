#include "Support.hpp"

void runDownloadProxyTests() {
    section("ZPG. 代理：三档与手动地址");

    // ---------- ZPG0. 三档 + 出厂值 + 键名 ----------
    {
        const QList<DownloadProxy::Mode> modes = DownloadProxy::modes();
        check(modes.size() == 3, "ZPG0 正好三档（不多不少）",
              QString::number(modes.size()));
        check(!modes.isEmpty() && modes.first() == DownloadProxy::Mode::System,
              "★ ZPG0 第一项是「跟随系统」—— 出厂那一档，也是加这一项之前的行为");
        check(modes.contains(DownloadProxy::kDefaultMode),
              "★ ZPG0 出厂值**必须**是那三档之一（否则下拉框一上来就指不到盘上那个值）");
        check(DownloadProxy::kDefaultMode == DownloadProxy::Mode::System,
              "ZPG0 出厂 = 跟随系统（不是直连，也不是一个空的手动地址）");
        bool roundTrip = true;
        for (DownloadProxy::Mode mode : modes) {
            if (DownloadProxy::modeFromKey(DownloadProxy::modeKey(mode)) != mode) {
                roundTrip = false;
            }
        }
        check(roundTrip, "ZPG0 三档各自存盘再读回来都还是自己");
        check(DownloadProxy::modeFromKey(QStringLiteral("system")) == DownloadProxy::Mode::System
                  && DownloadProxy::modeFromKey(QStringLiteral("direct"))
                         == DownloadProxy::Mode::Direct
                  && DownloadProxy::modeFromKey(QStringLiteral("manual"))
                         == DownloadProxy::Mode::Manual,
              "ZPG0 那三个键就是 system / direct / manual（存盘格式）");
        check(DownloadProxy::modeFromKey(QStringLiteral("DIRECT"))
                      == DownloadProxy::Mode::Direct
                  && DownloadProxy::modeFromKey(QStringLiteral("  manual  "))
                         == DownloadProxy::Mode::Manual,
              "ZPG0 键名认大小写、两头空白也认");
        check(DownloadProxy::modeFromKey(QStringLiteral("proxy.example.com:8080"))
                      == DownloadProxy::Mode::System
                  && DownloadProxy::modeFromKey(QString())
                         == DownloadProxy::Mode::System,
              "★★ ZPG0 认不出来的值一律回「跟随系统」—— 一行坏配置不许把网络掐死");
    }

    // ---------- ZPG1. 手动地址：认的写法 ----------
    {
        const auto parsed = [](const QString &raw, DownloadProxy::Endpoint *endpoint) {
            QString error;
            const bool ok = DownloadProxy::parseManual(raw, endpoint, &error);
            return ok && error.isEmpty();
        };
        DownloadProxy::Endpoint endpoint;

        check(parsed(QStringLiteral("127.0.0.1:7890"), &endpoint)
                  && endpoint.host == QStringLiteral("127.0.0.1") && endpoint.port == 7890 &&
                  !endpoint.socks5,
              "ZPG1 最普通的写法 127.0.0.1:7890 认（不带前缀 = HTTP 代理）");
        check(parsed(QStringLiteral("  proxy.example.com:8080  "), &endpoint)
                  && endpoint.host == QStringLiteral("proxy.example.com") &&
                  endpoint.port == 8080,
              "ZPG1 两头的空白会修掉（从网页上复制粘贴很容易带进来）");
        check(parsed(QStringLiteral("http://127.0.0.1:7890"), &endpoint) &&
                  endpoint.host == QStringLiteral("127.0.0.1") && !endpoint.socks5,
              "ZPG1 写了 http:// 也是 HTTP 代理");
        check(parsed(QStringLiteral("https://127.0.0.1:8443"), &endpoint) &&
                  !endpoint.socks5,
              "ZPG1 https:// 前缀按 HTTP 代理算（代理本身不跑 TLS，前缀只是习惯写法）");
        check(parsed(QStringLiteral("socks5://127.0.0.1:1080"), &endpoint) &&
                  endpoint.socks5 && endpoint.port == 1080,
              "ZPG1 socks5:// 认，而且记成 SOCKS5");
        check(parsed(QStringLiteral("socks5h://127.0.0.1:1080"), &endpoint) && endpoint.socks5,
              "ZPG1 socks5h:// 也认（h = 主机名交给代理解析，对我们这一侧没区别）");
        check(parsed(QStringLiteral("socks://127.0.0.1:1080"), &endpoint) && endpoint.socks5,
              "ZPG1 socks:// 也认");
        check(parsed(QStringLiteral("[::1]:7890"), &endpoint) &&
                  endpoint.host == QStringLiteral("::1") && endpoint.port == 7890,
              "ZPG1 IPv6 用方括号包起来认（[::1]:7890）");
        check(parsed(QStringLiteral("::1:7890"), &endpoint) &&
                  endpoint.host == QStringLiteral("::1") && endpoint.port == 7890,
              "ZPG1 不包方括号的 IPv6 也按最后一个冒号切开（主机 ::1、端口 7890）");
    }

    // ---------- ZPG2. 手动地址：不该认的写法 ----------
    {
        const auto rejected = [](const QString &raw) {
            DownloadProxy::Endpoint endpoint;
            QString error;
            const bool ok = DownloadProxy::parseManual(raw, &endpoint, &error);
            return !ok && !error.isEmpty();
        };
        check(rejected(QString()), "★ ZPG2 空串不认（而且给得出原因）");
        check(rejected(QStringLiteral("   ")), "ZPG2 全是空白也不认");
        check(rejected(QStringLiteral("127.0.0.1")), "★ ZPG2 没写端口不认");
        check(rejected(QStringLiteral(":8080")), "ZPG2 主机那截空着不认");
        check(rejected(QStringLiteral("host:0")) && rejected(QStringLiteral("host:65536")) &&
                  rejected(QStringLiteral("host:70000")),
              "★ ZPG2 端口越界（0 / 65536 / 70000）不认");
        check(rejected(QStringLiteral("host:abc")) && rejected(QStringLiteral("host:80x")),
              "ZPG2 端口不是整数不认");
        check(rejected(QStringLiteral("http://host:8080/path")),
              "★★ ZPG2 带路径不认 —— 那是把「要访问的网址」填进来了");
        check(rejected(QStringLiteral("ftp://host:21")),
              "ZPG2 不认识的前缀不认（只认 http / https / socks*）");
        check(rejected(QStringLiteral("ho st:8080")),
              "ZPG2 主机里有空白不认");
        check(rejected(QStringLiteral("user@host:8080")),
              "ZPG2 带用户名那一截不认（这一项不支持认证）");
        DownloadProxy::Endpoint endpoint;
        check(!DownloadProxy::parseManual(QStringLiteral("nope"), &endpoint, nullptr),
              "ZPG2 不传 error 指针也照样能判成败（不给 nullptr 崩）");
    }

    // ---------- ZPG3. endpointText：拼回一行，再解析还是自己 ----------
    {
        DownloadProxy::Endpoint http;
        http.host = QStringLiteral("10.0.0.5");
        http.port = 3128;
        check(DownloadProxy::endpointText(http) == QStringLiteral("10.0.0.5:3128"),
              "★ ZPG3 HTTP 代理拼出来**不带** http:// 前缀（于是存盘不会每次多一层）",
              DownloadProxy::endpointText(http));
        DownloadProxy::Endpoint socks;
        socks.host = QStringLiteral("127.0.0.1");
        socks.port = 1080;
        socks.socks5 = true;
        check(DownloadProxy::endpointText(socks) == QStringLiteral("socks5://127.0.0.1:1080"),
              "ZPG3 SOCKS5 拼出来带 socks5:// 前缀（不然读回来就变成 HTTP 代理了）",
              DownloadProxy::endpointText(socks));
        DownloadProxy::Endpoint ipv6;
        ipv6.host = QStringLiteral("::1");
        ipv6.port = 7890;
        check(DownloadProxy::endpointText(ipv6) == QStringLiteral("[::1]:7890"),
              "ZPG3 IPv6 拼回来自动补方括号",
              DownloadProxy::endpointText(ipv6));
        DownloadProxy::Endpoint back;
        const bool ok = DownloadProxy::parseManual(DownloadProxy::endpointText(ipv6), &back);
        check(ok && back == ipv6, "ZPG3 拼出来再解析还是同一份地址（往返闭合）");
        const bool ok2 = DownloadProxy::parseManual(DownloadProxy::endpointText(socks), &back);
        check(ok2 && back == socks, "ZPG3 SOCKS5 那一份往返也闭合");
    }

    // ---------- ZPG4. LauncherSettings：默认值 + 跨实例读回来 ----------
    // 和 I5 同一个做法：把 QSettings 搬进沙盒，跑完还回原生格式（别在用户机器上留键）。
    {
        const QString settingsDir = QDir::tempPath() + QStringLiteral("/wtfml-proxy-settings");
        QDir(settingsDir).removeRecursively();
        QDir().mkpath(settingsDir);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
        {
            LauncherSettings fresh;
            check(fresh.proxyMode() == DownloadProxy::Mode::System,
                  "★ ZPG4 全新沙盒里是「跟随系统」（加这一项之前的行为）");
            check(fresh.proxyManualText().isEmpty(),
                  "ZPG4 全新沙盒里手动地址是空的");

            fresh.setProxyMode(DownloadProxy::Mode::Manual);
            fresh.setProxyManualText(QStringLiteral("  socks5://127.0.0.1:1080  "));
            LauncherSettings reopened;
            check(reopened.proxyMode() == DownloadProxy::Mode::Manual,
                  "★ ZPG4 档位跨实例读得回来（读的是盘上 Download/ProxyMode）");
            check(reopened.proxyManualText() == QStringLiteral("socks5://127.0.0.1:1080"),
                  "★ ZPG4 手动地址跨实例读得回来，两头的空白已经修掉",
                  reopened.proxyManualText());

            // 手改 ini 写成不认识的值：读出来必须是 System，不是崩、也不是空档。
            {
                QSettings raw;
                raw.setValue(QStringLiteral("Download/ProxyMode"), QStringLiteral("bogus"));
                raw.sync();
            }
            LauncherSettings bogus;
            check(bogus.proxyMode() == DownloadProxy::Mode::System,
                  "★★ ZPG4 手改成不认识的值 ⇒ 读出来是「跟随系统」");

            fresh.setProxyMode(DownloadProxy::Mode::System);
            fresh.setProxyManualText(QString());
        }
        QSettings::setDefaultFormat(QSettings::NativeFormat);
        QDir(settingsDir).removeRecursively();
    }
}

