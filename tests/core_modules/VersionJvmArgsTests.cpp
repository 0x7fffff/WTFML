#include "Support.hpp"

int runVersionJvmArgsTests(const QString &sandbox) {
    section("K. 按版本的自定义 JVM 参数（wtfml.meta 的 jvmArgs）");

    const QString versionsRoot = sandbox + "/versions";
    const QString versionId = QStringLiteral("参数版本");
    const QString metaPath = versionsRoot + "/" + versionId + "/wtfml.meta";
    QDir().mkpath(versionsRoot + "/" + versionId);

    const auto writeMeta = [&metaPath](const QByteArray &content) {
        QFile file(metaPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(content);
        }
    };

    VersionManager vm;

    // 文件不存在 -> 空列表，而且**不打日志**（每个已安装版本都会被问一次，打日志就是刷屏）
    QFile::remove(metaPath);
    check(vm.versionJvmArgs(versionId).isEmpty(), "K1 没有 wtfml.meta 时返回空列表");

    // 正常：数组逐条按原顺序取出
    writeMeta(R"({"jvmArgs": ["-XX:+PerfDisableSharedMem", "-Xss1M"]})");
    const QStringList normal = vm.versionJvmArgs(versionId);
    check(normal == QStringList({QStringLiteral("-XX:+PerfDisableSharedMem"),
                                 QStringLiteral("-Xss1M")}),
          "K2 数组按原顺序逐条取出", normal.join(QStringLiteral(" | ")));

    // 参数本身含空格也不能被切开（这正是用数组而不是空格分隔字符串的理由）
    writeMeta(R"({"jvmArgs": ["-Dfoo=bar baz"]})");
    const QStringList withSpace = vm.versionJvmArgs(versionId);
    check(withSpace.size() == 1 && withSpace.first() == QStringLiteral("-Dfoo=bar baz"),
          "K2 含空格的单条参数不被切开", withSpace.join(QStringLiteral(" | ")));

    // 类型错：不是数组 -> 当空（并告警）
    out() << "  —— 下面几条 qWarning 是**预期**的（故意写坏的元数据）——\n";
    writeMeta(R"({"jvmArgs": "-XX:+PerfDisableSharedMem"})");
    check(vm.versionJvmArgs(versionId).isEmpty(), "K3 jvmArgs 是字符串时当空（不是数组）");

    // 数组里有非字符串元素 -> 跳过那一个、留下其余的
    writeMeta(R"({"jvmArgs": ["-Xss1M", 42, null, "-XX:+UseG1GC"]})");
    const QStringList mixed = vm.versionJvmArgs(versionId);
    check(mixed == QStringList({QStringLiteral("-Xss1M"), QStringLiteral("-XX:+UseG1GC")}),
          "K3 数组里的非字符串元素被跳过、其余保留", mixed.join(QStringLiteral(" | ")));

    // 空串元素会被丢掉：空参数传给 JVM 是纯粹的噪音，QProcess 也会把它当独立参数
    writeMeta(R"({"jvmArgs": ["", "-Xss1M"]})");
    check(vm.versionJvmArgs(versionId) == QStringList({QStringLiteral("-Xss1M")}),
          "K4 空串元素被丢掉");

    writeMeta(R"({"jvmArgs": []})");
    check(vm.versionJvmArgs(versionId).isEmpty(), "K4 空数组返回空列表");

    // 字段缺失 / 值是 null：与"文件不存在"一样是空，不算错
    writeMeta(R"({"name": "只是有个名字"})");
    check(vm.versionJvmArgs(versionId).isEmpty(), "K4 没有 jvmArgs 字段时返回空列表");
    writeMeta(R"({"jvmArgs": null})");
    check(vm.versionJvmArgs(versionId).isEmpty(), "K4 jvmArgs 是 null 时返回空列表");

    // 整个文件是坏 JSON -> 当没有（readVersionMeta 会告警）
    writeMeta("{ 这不是 JSON");
    check(vm.versionJvmArgs(versionId).isEmpty(), "K5 元数据文件坏掉时返回空列表");

    // 与 displayParts() 共用同一份读盘逻辑：同一份文件里读名字和读参数互不影响
    writeMeta(R"({"name": "我的整合包", "realVersion": "1.18.2",
                 "jvmArgs": ["-XX:+PerfDisableSharedMem"]})");
    const VersionDisplayParts parts = vm.displayParts(versionId);
    check(parts.customName == QStringLiteral("我的整合包") &&
              parts.realVersion == QStringLiteral("1.18.2"),
          "K6 同一份元数据里名字字段照常读到", parts.customName + "/" + parts.realVersion);
    check(vm.versionJvmArgs(versionId) ==
              QStringList({QStringLiteral("-XX:+PerfDisableSharedMem")}),
          "K6 同一份元数据里 jvmArgs 也照常读到");

    // 清理：留着会让下一次运行读到脏数据（这个目录没有 <id>.jar，本来也不会被认成版本）
    QDir(versionsRoot + "/" + versionId).removeRecursively();
    check(vm.versionJvmArgs(versionId).isEmpty(), "K7 清理后回到空列表");

    return 0;
}

