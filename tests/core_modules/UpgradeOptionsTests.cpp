/**
 * @file UpgradeOptionsTests.cpp
 * @brief 跨版本 options 合并（U4 的判据层）：三条规矩 + 资源包启用列表重建。
 */
#include "Support.hpp"

#include "operations/UpgradeOptions.hpp"

int runUpgradeOptionsTests(const QString &) {
    using UpgradeOptions::Merge;

    const QString source = QStringLiteral(
        "# 源实例\n"
        "version:3465\n"
        "fov:0.5\n"
        "resourcePacks:[\"vanilla\",\"file/a.zip\",\"file/b.zip\"]\n"
        "onlyInSource:1\n");
    const QString target = QStringLiteral(
        "version:3955\n"
        "fov:0.2\n"
        "newKey:abc:def\n"
        "resourcePacks:[\"vanilla\",\"file/c.zip\"]\n");
    const UpgradeOptions::Parsed parsed = UpgradeOptions::parse(source);
    check(parsed.order == QStringList{QStringLiteral("version"), QStringLiteral("fov"),
                                      QStringLiteral("resourcePacks"),
                                      QStringLiteral("onlyInSource")},
          "UO1 按出现顺序收键（注释与空行跳过）", parsed.order.join(QLatin1Char(',')));
    check(parsed.values.value(QStringLiteral("version")) == QStringLiteral("3465"),
          "UO1 冒号后面的值原样保留");

    const Merge merged = UpgradeOptions::merge(source, target);
    check(merged.text.contains(QStringLiteral("fov:0.5")),
          "UO2 两边都有的键搬源的值（用户改过的那个）", merged.text);
    check(merged.text.contains(QStringLiteral("version:3465")),
          "UO2 version 也按源的值搬（它是普通键，不是资源包列表）");
    check(merged.text.contains(QStringLiteral("newKey:abc:def")),
          "UO2 只有目标有的键保留目标的值（值里的冒号不被截断）");
    check(!merged.text.contains(QStringLiteral("onlyInSource")),
          "UO2 只有源有的键默认不搬");
    check(merged.unknownKeys == QStringList{QStringLiteral("onlyInSource")},
          "UO2 但列进 unknownKeys 给用户看", merged.unknownKeys.join(QLatin1Char(',')));
    check(merged.skippedKeys.contains(QStringLiteral("resourcePacks"))
              && !merged.text.contains(QStringLiteral("resourcePacks")),
          "UO2 资源包那两行固定跳过（另按实际迁入重建）");
    check(merged.migratedKeys.contains(QStringLiteral("fov"))
              && merged.keptTargetKeys.contains(QStringLiteral("newKey")),
          "UO2 migratedKeys / keptTargetKeys 分得清");

    const QString line = UpgradeOptions::resourcePacksLine(
        QStringLiteral("resourcePacks:[\"vanilla\",\"file/a.zip\",\"file/b.zip\"]"),
        {QStringLiteral("file/b.zip")});
    check(line == QStringLiteral("resourcePacks:[\"vanilla\",\"file/b.zip\"]"),
          "UO3 只留真的迁入了的包，vanilla 永远在前", line);
    const QString emptyLine = UpgradeOptions::resourcePacksLine(
        QStringLiteral("resourcePacks:[\"file/a.zip\"]"), {});
    check(emptyLine == QStringLiteral("resourcePacks:[\"vanilla\"]"),
          "UO3 一个包都没迁入时只剩 vanilla", emptyLine);
    check(UpgradeOptions::isResourcePackKey(QStringLiteral("incompatibleResourcePacks"))
              && !UpgradeOptions::isResourcePackKey(QStringLiteral("fov")),
          "UO4 只认那两条资源包键");

    // UO5：副本模式没有目标基线 —— 保留源的键，只重建资源包那一行。
    const QString rewritten = UpgradeOptions::rewriteKeepingAll(source,
                                                                {QStringLiteral("file/a.zip")});
    check(rewritten.contains(QStringLiteral("fov:0.5"))
              && rewritten.contains(QStringLiteral("version:3465")),
          "UO5 源里的普通键都留着", rewritten);
    check(!rewritten.contains(QStringLiteral("onlyInSource:0")) && rewritten.contains(QStringLiteral("onlyInSource:1")),
          "UO5 只有源有的键也留着（副本没有目标基线可对照）");
    check(rewritten.contains(QStringLiteral("resourcePacks:[\"vanilla\",\"file/a.zip\"]")),
          "UO5 启用列表只留迁入了的包", rewritten);
    check(!rewritten.contains(QStringLiteral("file/b.zip")),
          "UO5 没迁入的包不出现在启用列表里");

    return g_failures == 0 ? 0 : 1;
}

