#include "Support.hpp"

#include "launcher/LaunchScriptExport.hpp"

// 「导出启动脚本」那份 .bat 的正文（见 launcher/LaunchScriptExport.hpp）。
//
// 这一节钉的是**看不见的规矩**：哪些字符要让 cmd 包一层引号、`%` 为什么要写两遍、
// 正版的令牌怎么换成环境变量引用、离线那一路为什么不带前言、以及"参数里还带着已知凭据
// 就不许写出去"那条闸。这些错一个字节都要等用户双击导出的脚本才看得见。
int runLaunchScriptExportTests() {
    section("LX. 导出启动脚本（.bat 正文怎么拼）");

    // ---- LX1 一个参数怎么包 ----
    check(LaunchScriptExport::quote(QStringLiteral("abc")) == QStringLiteral("abc"),
          "LX1 不用包的参数原样写出去");
    check(LaunchScriptExport::quote(QString()) == QStringLiteral("\"\""),
          "LX1 空参数包成一对引号（不包的话它整个消失）",
          LaunchScriptExport::quote(QString()));
    check(LaunchScriptExport::quote(QStringLiteral("a b")) == QStringLiteral("\"a b\""),
          "LX1 带空格的参数包起来");
    check(LaunchScriptExport::quote(QStringLiteral("a\tb")) == QStringLiteral("\"a\tb\""),
          "LX1 带制表符的也要包");
    check(LaunchScriptExport::quote(QStringLiteral("a&b")) == QStringLiteral("\"a&b\"")
              && LaunchScriptExport::quote(QStringLiteral("a^b")) == QStringLiteral("\"a^b\""),
          "LX1 cmd 的元字符（& | < > ( ) ^ \"）都触发包装");
    check(LaunchScriptExport::quote(QStringLiteral("100%")) == QStringLiteral("100%%"),
          "LX1 `%` 写两遍（不给包装也照样展开）",
          LaunchScriptExport::quote(QStringLiteral("100%")));
    check(LaunchScriptExport::quote(QStringLiteral("50% off")) == QStringLiteral("\"50%% off\""),
          "LX1 要包装时 `%` 也写两遍（引号里照样展开）",
          LaunchScriptExport::quote(QStringLiteral("50% off")));
    check(LaunchScriptExport::quote(QStringLiteral("a!b")) == QStringLiteral("a!b"),
          "LX1 `!` 故意不动（脚本没开延迟展开，转义反而多一个 ^）");
    // 真出现一个 `"` 是没救的（cmd 没有"引号里的引号"），照实写出去 —— 这一条钉的是
    // "不假装能转义"，不是"转义得对"。
    check(LaunchScriptExport::quote(QStringLiteral("a\"b")) == QStringLiteral("\"a\"b\""),
          "LX1 参数里的 `\"` 不转义（没有这种写法），只是整段被包起来");

    // ---- LX2 离线那一份：结构 ----
    // 两份路径都带空格（Program Files 是最常见的那种），这样引号那两条规矩才真被走到。
    LaunchScriptExport::Input offline;
    offline.dataDir = QStringLiteral("C:/My Games/mc");
    offline.program = QStringLiteral("C:/Program Files/Java/bin/java.exe");
    offline.args = {QStringLiteral("-Xmx2G"), QStringLiteral("-Dfoo=a b")};
    offline.offlineMode = true;
    const QString offlineScript = LaunchScriptExport::compose(offline);
    check(offlineScript.startsWith(QStringLiteral("@echo off\r\n")),
          "LX2 第一句是 @echo off");
    const QString cdLine = QStringLiteral("cd /d \"C:\\My Games\\mc\"\r\n");
    check(offlineScript.contains(cdLine),
          "LX2 带 /d 切到数据目录（盘符也换），路径按 Windows 分隔符写、带空格就包起来",
          offlineScript.contains(cdLine) ? QString() : offlineScript.left(200));
    const QString javaLine =
        QStringLiteral("\"C:\\Program Files\\Java\\bin\\java.exe\" -Xmx2G \"-Dfoo=a b\"\r\n");
    check(offlineScript.contains(javaLine),
          "LX2 程序名带引号、每个参数各自包一层、整份以换行收尾",
          offlineScript.mid(offlineScript.indexOf(QStringLiteral("java.exe")) + 7));
    check(!offlineScript.contains(QStringLiteral("WTFML_MC_ACCESS_TOKEN")),
          "LX2 离线那一份不带正版前言（没有令牌可提供）");

    // ---- LX3 程序名空着 ⇒ 裸 java ----
    LaunchScriptExport::Input noJava = offline;
    noJava.program = QString();
    noJava.args = {QStringLiteral("-version")};
    const QString noJavaScript = LaunchScriptExport::compose(noJava);
    check(noJavaScript.contains(QStringLiteral("\r\njava -version\r\n")),
          "LX3 还没挑过 Java 的实例导出来的脚本写裸 java（从 PATH 找）",
          noJavaScript.right(60));

    // ---- LX4 正版那一份：令牌换成环境变量引用 ----
    // 正版这条路要求"导出来的脚本能直接跑"，所以带着一句 `WTFML_MC_ACCESS_TOKEN` 前言；
    // 但令牌本身一个字节都不落盘 —— 换成 `%WTFML_MC_ACCESS_TOKEN%`，由运行环境提供。
    const QString token = QStringLiteral("tok-123456789012");
    LaunchScriptExport::Input online = offline;
    online.offlineMode = false;
    online.token = token;
    online.args = {QStringLiteral("-Dminecraft.accessToken=") + token, QStringLiteral("-Xmx2G")};
    const QString onlineScript = LaunchScriptExport::compose(online);
    check(onlineScript.contains(QStringLiteral("if not defined WTFML_MC_ACCESS_TOKEN")),
          "LX4 正版那一份开头有话术：没给令牌就不跑");
    check(onlineScript.contains(
              QStringLiteral("\"-Dminecraft.accessToken=%WTFML_MC_ACCESS_TOKEN%\"")),
          "LX4 令牌换成环境变量引用（并且包好引号）",
          onlineScript.mid(onlineScript.indexOf(QStringLiteral("accessToken")) - 2, 60));
    check(!onlineScript.contains(token)
              && !onlineScript.contains(QStringLiteral("%%WTFML_MC_ACCESS_TOKEN%")),
          "LX4 原始令牌一个字节都不在里面，引用本身也没有被 `%` 那条规矩写成两遍");

    // ---- LX5 已知凭据 ⇒ 整份不写 ----
    // 拼完之后再过一道：用户自己写进追加参数里的、或者从哪份版本 json 抄来的令牌，只要
    // 和登记过的某个凭据对得上，这一份就不许落地（返回空串，调用方去说"为什么没导"）。
    const QString secret = QStringLiteral("super-secret-token-value");
    SecretRedactor::remember(secret);
    LaunchScriptExport::Input leaking = offline;
    leaking.args = {QStringLiteral("-Dcustom=") + secret};
    check(LaunchScriptExport::compose(leaking).isEmpty(),
          "LX5 参数里带着已知凭据 ⇒ 正文一整个不写");
    check(!LaunchScriptExport::compose(offline).isEmpty(),
          "LX5 同一份输入去掉那一条之后照常拼得出来（闸只挡那一条，不是整条路关了）");
    return 0;
}
