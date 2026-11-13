/**
 * @file AgentPathPolicyTests.cpp
 * @brief 路径攻击矩阵 + 句柄级最终路径/文件 ID/reparse + 对象身份变化。
 */

#include "Support.hpp"

#include "agent/AgentWinPath.hpp"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

void runAgentPathPolicyTests() {
    QString normalized;
    QString problem;
    const QStringList rejected{
        QStringLiteral("C:/Windows/win.ini"), QStringLiteral("/etc/passwd"),
        QStringLiteral("//server/share/x"), QStringLiteral("../outside.txt"),
        QStringLiteral("config/../../outside.txt"), QStringLiteral("config/a:b.txt"),
        QStringLiteral("CON"), QStringLiteral("con.txt"), QStringLiteral("a."),
        QStringLiteral("a "), QStringLiteral("config/\u0001x")};
    for (const QString &path : rejected) {
        check(!AgentWinPath::normalizeRelative(path, &normalized, &problem),
              QStringLiteral("路径：非法输入被拒绝 %1").arg(path), problem);
    }
    check(AgentWinPath::normalizeRelative(QStringLiteral("config/a.toml"), &normalized, &problem) &&
              normalized == QStringLiteral("config/a.toml"),
          "路径：普通相对路径被归一化", normalized);
    check(AgentWinPath::normalizeRelative(QStringLiteral("config//a.toml"), &normalized, &problem) &&
              normalized == QStringLiteral("config/a.toml"),
          "路径：重复分隔符被归一化");
    check(AgentWinPath::isProtectedRelative(QStringLiteral("versions/1.20.1/x.json")),
          "路径：versions 是保护目录");
    check(!AgentWinPath::isProtectedRelative(QStringLiteral("config/a.toml")),
          "路径：config 不是保护目录（只允许写文本配置）");
    check(AgentWinPath::isTextExtension(QStringLiteral("config/a.toml")),
          "路径：toml 是文本");
    check(!AgentWinPath::isTextExtension(QStringLiteral("mods/a.jar")),
          "路径：jar 不是文本");
    check(AgentWinPath::looksBinary(QByteArray("abc\0def", 7)),
          "路径：NUL 内容识别为二进制");
    check(!AgentWinPath::looksBinary(QByteArray("普通文本\n")),
          "路径：普通文本不是二进制");

    QTemporaryDir fixture;
    check(fixture.isValid(), "路径：临时目录可用");
    const QString root = fixture.path();
    QDir().mkpath(root + QStringLiteral("/config"));
    QFile file(root + QStringLiteral("/config/a.toml"));
    check(file.open(QIODevice::WriteOnly), "路径：造一个可被解析的文件", file.fileName());
    file.write("maxMemory = 4096\n");
    file.close();

    AgentWinPath::Info info;
    check(AgentWinPath::resolveExisting(root, QStringLiteral("config/a.toml"), false, &info,
                                        &problem),
          "路径：合法文件句柄级校验通过", problem);
    check(!info.isDirectory && info.exists && info.size > 0,
          "路径：句柄读到大小");
    const QString token = AgentWinPath::identityToken(info);
    check(AgentWinPath::verifyIdentityToken(token, info), "路径：身份 token 自洽");
    check(!AgentWinPath::resolveExisting(root, QStringLiteral("config/../options.txt"), false, &info,
                                         &problem),
          "路径：.. 即使目标存在也被拒绝");
    check(!AgentWinPath::resolveExisting(root, QStringLiteral("C:/Windows/win.ini"), false, &info,
                                         &problem),
          "路径：绝对路径不进句柄校验");

    // 改内容/大小/mtime 后旧 token 失效。
    QFile modified(root + QStringLiteral("/config/a.toml"));
    check(modified.open(QIODevice::WriteOnly | QIODevice::Truncate), "路径：改内容与 mtime",
          modified.fileName());
    modified.write("maxMemory = 16384\n");
    modified.close();
    AgentWinPath::Info after;
    check(AgentWinPath::resolveExisting(root, QStringLiteral("config/a.toml"), false, &after,
                                        &problem),
          "路径：修改后仍可解析");
    check(!AgentWinPath::verifyIdentityToken(token, after),
          "路径：内容/大小变化后旧身份 token 不再匹配");

    AgentWinPath::Info parent;
    check(AgentWinPath::resolveParent(root, QStringLiteral("config/new.toml"), &parent, &problem) &&
              parent.isDirectory,
          "路径：新文件的父目录身份可校验", problem);

    // junction 逃逸：能建就验，建不了（权限/文件系统）不假装通过。
    QTemporaryDir outside;
    QFile secret(outside.path() + QStringLiteral("/secret.txt"));
    check(secret.open(QIODevice::WriteOnly), "路径：沙盒外放一个文件", secret.fileName());
    secret.write("outside");
    secret.close();
    QProcess mklink;
    mklink.start(QStringLiteral("cmd"),
                 {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                  QDir::toNativeSeparators(root + QStringLiteral("/link")),
                  QDir::toNativeSeparators(outside.path())});
    const bool linked = mklink.waitForFinished(10000) && mklink.exitStatus() == QProcess::NormalExit &&
                        mklink.exitCode() == 0;
    if (linked) {
        check(!AgentWinPath::resolveExisting(root, QStringLiteral("link/secret.txt"), false, &info,
                                             &problem),
              "路径：junction 逃逸被最终路径校验拒绝");
    } else {
        std::printf("  SKIP  junction 夹具无法创建（%s）\n",
                    mklink.readAllStandardError().constData());
    }
}
