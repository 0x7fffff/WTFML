#include "Support.hpp"

#include "download/AssetNames.hpp"

// version.json 里那两个会变成盘上路径的名字（见 download/AssetNames.hpp）。
//
// 这一节全是**纯函数**，但它守的是"盘上写哪儿"：`<assets>/indexes/<id>.json` 和
// `<assets>/objects/<hash 前两位>/<hash>` 都是拿 version.json 里的字符串原样拼的。
// 这两个判据坏掉的表现是**一份 json 决定往 assets/ 外面写文件**，而那一路上没有任何东西
// 会当场报错 —— 所以真值表要逐条钉住，包括长度恰好卡在边界上的那两条。
int runAssetNamesTests() {
    section("ZAO. version.json 里那两个名字（资源索引号 / 对象哈希）");

    // ---- ZAO1 对象哈希：只认 40 位十六进制 ----
    const QString sha1 = QStringLiteral("e3b0c44298fc1c149afbf4c8996fb92427ae41e4");
    check(AssetNames::isSha1Hex(sha1), "ZAO1 40 位小写十六进制收");
    check(AssetNames::isSha1Hex(sha1.toUpper()), "ZAO1 大写也收（接口两边都用过）");
    QString mixed = sha1;
    mixed[0] = QLatin1Char('E');
    check(AssetNames::isSha1Hex(mixed), "ZAO1 大小写混着也收");

    check(!AssetNames::isSha1Hex(QString()), "ZAO1 空串不收");
    check(!AssetNames::isSha1Hex(sha1.left(39)), "ZAO1 少一位不收");
    check(!AssetNames::isSha1Hex(sha1 + QLatin1Char('0')), "ZAO1 多一位不收");
    check(!AssetNames::isSha1Hex(QStringLiteral("../../../../../../../../../../88888888")),
          "★ ZAO1 恰好 40 个字符的一串 `../` 不收（长度对、内容不对）");
    check(!AssetNames::isSha1Hex(QString(40, QLatin1Char('.'))),
          "★ ZAO1 40 个点不收（不然 `objects/<前两位>/` 里那一段会是 `..`）");
    check(!AssetNames::isSha1Hex(QString(40, QLatin1Char('g'))),
          "ZAO1 `g` 不在十六进制里");
    check(!AssetNames::isSha1Hex(QString(40, QChar(0xFF10))),
          "ZAO1 全角数字不收（看着像数字，字节不是）");
    check(!AssetNames::isSha1Hex(sha1 + QStringLiteral("\n")),
          "ZAO1 结尾多一个换行不收");

    // ---- ZAO2 资源索引号：能当一段文件名 ----
    for (const QString &good : {QStringLiteral("1.20"), QStringLiteral("pre-1.6"),
                                QStringLiteral("legacy"), QStringLiteral("17"),
                                QStringLiteral("24w14a")}) {
        check(AssetNames::isSafeIndexId(good), "ZAO2 真出现过的索引号收：" + good);
    }

    const QList<QPair<QString, QString>> bad = {
        {QStringLiteral(".."), QStringLiteral("上跳一段")},
        {QStringLiteral("../evil"), QStringLiteral("带 `../`")},
        {QStringLiteral("..\\evil"), QStringLiteral("反斜杠的 `..`")},
        {QStringLiteral("a/b"), QStringLiteral("带斜杠")},
        {QStringLiteral("C:"), QStringLiteral("带盘符冒号")},
        {QStringLiteral("a:b"), QStringLiteral("冒号（NTFS 的备用数据流）")},
        {QStringLiteral("CON"), QStringLiteral("Windows 保留设备名")},
        {QStringLiteral("名字."), QStringLiteral("结尾的点会被静默吃掉")},
        {QStringLiteral("名字 "), QStringLiteral("结尾的空格同理")},
        {QString(), QStringLiteral("空串")},
    };
    for (const auto &one : bad) {
        check(!AssetNames::isSafeIndexId(one.first), "★ ZAO2 不收：" + one.second, one.first);
    }

    check(!AssetNames::isSafeIndexId(QString(AssetNames::kMaxIndexIdLength + 1, QLatin1Char('a'))),
          "ZAO2 超过长度上限不收");
    check(AssetNames::isSafeIndexId(QString(AssetNames::kMaxIndexIdLength, QLatin1Char('a'))),
          "ZAO2 恰好卡在长度上限上收");

    // 判据**只有一份**：这一段证的是"和 `validatePathSegment()` 同源"，不是"又写了一套"。
    // 上面那些探测名都短、非空，所以两边的结论必须逐条一致。
    for (const QString &probe : {QStringLiteral("a b"), QStringLiteral("名字"),
                                 QStringLiteral("a-b.c"), QStringLiteral("a:b"),
                                 QStringLiteral("a.b"), QStringLiteral("CON")}) {
        check(AssetNames::isSafeIndexId(probe) ==
                  InstanceFiles::validatePathSegment(probe).isEmpty(),
              "★ ZAO2 和 validatePathSegment 对同一个名字给同一个结论：" + probe);
    }

    return 0;
}
