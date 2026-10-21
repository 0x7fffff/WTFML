#include "Support.hpp"

void runOfflineSkinTests(const QString &sandbox) {
    section("ZFa. 离线账号自己挑的皮肤（换一张 / 恢复默认）");

    // 一个合规矩的离线 uuid（32 位十六进制，和 AccountStore 算出来的同一种形态）。
    const QString uuid = QStringLiteral("0123456789abcdef0123456789abcdef");
    const QString key = QStringLiteral("0123456789abcdef0123456789abcdef");

    // ---- ZF1 存在哪：skins/manual-<账号键>.png（模型一行是同一个名字的 .txt） ----
    {
        const QString skins = AppPaths::skinsDir();
        check(skins.startsWith(sandbox),
              "ZFa1 这一节写的是 mw/test 自己的沙盒（绝不碰用户那份 .minecraft）", skins);
        check(OfflineSkin::skinPathFor(uuid) ==
                  skins + QStringLiteral("/manual-") + key + QStringLiteral(".png"),
              "ZFa1 皮肤存在 skins/manual-<账号键>.png", OfflineSkin::skinPathFor(uuid));
        check(OfflineSkin::modelPathFor(uuid) ==
                  skins + QStringLiteral("/manual-") + key + QStringLiteral(".txt"),
              "ZFa1 模型那一行存在同一个名字的 .txt 里", OfflineSkin::modelPathFor(uuid));
        // 带横线的那种 uuid 也要认（AccountStore 存的是带横线的形态）。
        const QString dashed = QStringLiteral("01234567-89ab-cdef-0123-456789abcdef");
        check(OfflineSkin::skinPathFor(dashed) == OfflineSkin::skinPathFor(uuid),
              "ZFa1 带不带横线指的是同一个账号（同一个 uuidKey）");
    }

    // ---- ZF2 认不出来的 uuid：不拼野路径 ----
    {
        const QStringList bad = {QString(), QStringLiteral("不是 uuid"),
                                 QStringLiteral("0123456789abcdef")};
        QString firstAccepted;
        for (const QString &one : bad) {
            if (!OfflineSkin::skinPathFor(one).isEmpty() ||
                !OfflineSkin::modelPathFor(one).isEmpty()) {
                firstAccepted = one;
                break;
            }
        }
        check(firstAccepted.isEmpty(),
              "ZFa2 uuid 认不出来 → 两个路径都是空串（不拼一个野路径，和 PlayerSkinStore 同一条规矩）",
              firstAccepted);
        check(!OfflineSkin::hasSkin(QString()) && !OfflineSkin::hasSkin(QStringLiteral("不是 uuid")),
              "ZFa2 认不出来的 uuid 也不该「有皮肤」");
        check(OfflineSkin::loadModel(QString()) == QStringLiteral("classic"),
              "ZFa2 模型那一格读不出来时按经典算");
    }

    // ---- ZF3 一开始什么都没有 ----
    {
        check(!OfflineSkin::hasSkin(uuid), "ZFa3 前提：这个账号还没手动挑过");
        check(OfflineSkin::loadModel(uuid) == QStringLiteral("classic"),
              "ZFa3 没有模型记录 → 经典（绝大多数皮肤都是这一档）");
    }

    // ---- ZF4 存一张：落成 PNG、模型跟着记 ----
    {
        QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(20, 200, 120));
        QString error;
        check(OfflineSkin::save(uuid, image, QStringLiteral("slim"), &error),
              "ZFa4 存一张 64×64 的皮肤", error);
        check(OfflineSkin::hasSkin(uuid), "ZFa4 存完就在（hasSkin 只问文件在不在）");
        check(OfflineSkin::loadModel(uuid) == QStringLiteral("slim"),
              "ZFa4 模型记下来了（slim）", OfflineSkin::loadModel(uuid));

        // 后缀是 .png，内容也必须是 PNG —— 来源可能是 jpg，按原字节写会让后缀骗人。
        QFile file(OfflineSkin::skinPathFor(uuid));
        check(file.open(QIODevice::ReadOnly), "ZFa4 前提：存下来的那个文件打得开");
        const QByteArray head = file.read(8);
        file.close();
        check(head == QByteArrayLiteral("\x89PNG\r\n\x1a\n"),
              "ZFa4 存下来的一定是 PNG（来源是 jpg/bmp 时重新编码，不让后缀骗人）",
              QString::fromLatin1(head.toHex()));
        const QImage readBack(OfflineSkin::skinPathFor(uuid));
        check(!readBack.isNull() && readBack.size() == QSize(64, 64) &&
                  readBack.pixelColor(0, 0) == QColor(20, 200, 120),
              "ZFa4 读回来还是那张 64×64、颜色没被改过");
    }

    // ---- ZF5 模型那一行：只认 slim，别的都按经典 ----
    {
        QImage image(64, 32, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QString error;
        check(OfflineSkin::save(uuid, image, QStringLiteral("  SLIM  "), &error),
              "ZFa5 大小写/空白也算 slim", error);
        check(OfflineSkin::loadModel(uuid) == QStringLiteral("slim"),
              "ZFa5 归一化之后是 slim");
        check(OfflineSkin::save(uuid, image, QStringLiteral("乱写"), &error),
              "ZFa5 不认识的档位照样存得了");
        check(OfflineSkin::loadModel(uuid) == QStringLiteral("classic"),
              "ZFa5 不认识的档位按经典记（不是留一个读不懂的值）",
              OfflineSkin::loadModel(uuid));
    }

    // ---- ZF6 存不下来说清楚：坏图 / 坏 uuid ----
    {
        QString error;
        check(!OfflineSkin::save(uuid, QImage(), QStringLiteral("classic"), &error) &&
                  !error.isEmpty(),
              "ZFa6 空图 → 失败并给一句原因", error);
        error.clear();
        check(!OfflineSkin::save(QStringLiteral("不是 uuid"), QImage(64, 64, QImage::Format_ARGB32),
                                 QStringLiteral("classic"), &error) &&
                  !error.isEmpty(),
              "ZFa6 uuid 认不出来 → 失败并给一句原因", error);
    }

    // ---- ZF7 恢复默认：两张文件一起没 ----
    {
        check(OfflineSkin::hasSkin(uuid), "ZFa7 前提：现在有一张手动挑的");
        check(OfflineSkin::clear(uuid), "ZFa7 撤掉成功");
        check(!OfflineSkin::hasSkin(uuid), "ZFa7 图没了");
        check(!QFile::exists(OfflineSkin::modelPathFor(uuid)), "ZFa7 模型那一行也没了");
        check(OfflineSkin::loadModel(uuid) == QStringLiteral("classic"),
              "ZFa7 撤掉之后模型那一格回到经典（下一次换一张时不会沿用旧模型）");
        check(OfflineSkin::clear(uuid), "ZFa7 再撤一次也成功（没有任何东西可删不是失败）");
    }
}

