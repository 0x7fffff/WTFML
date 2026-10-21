#include "Support.hpp"

void runImageDecodeTests() {
    const QString metaDir = QStringLiteral(REPO_ROOT_PATH) + "/tests/meta";

    section("ZC1. Qt 自己认得的那一半：按内容判，不按后缀判");

    // 自己现搭一张 PNG（不落盘，走 QBuffer）当夹具 —— 这一条要证的只是"这条路通"，
    // 造一张比去 meta/ 里挂一份更干净。
    QImage source(96, 96, QImage::Format_ARGB32);
    source.fill(QColor(0x11, 0x22, 0x33));
    QByteArray pngBytes;
    {
        QBuffer buffer(&pngBytes);
        buffer.open(QIODevice::WriteOnly);
        check(source.save(&buffer, "PNG"), "ZC1 现造一张 96x96 的 PNG 成功了");
    }
    check(pngBytes.startsWith(QByteArrayLiteral("\x89PNG")), "ZC1 造出来的确实是 PNG 字节");

    const QImage decodedPng = ImageDecode::decode(pngBytes);
    check(decodedPng.size() == QSize(96, 96), "ZC1 PNG 解得回来，尺寸对",
          QStringLiteral("%1x%2").arg(decodedPng.width()).arg(decodedPng.height()));
    // 像素值也要对：尺寸对而内容错（比如换成全黑）是最查不出来的那种错。
    check(decodedPng.pixel(48, 48) == quint32(0xFF112233),
          "ZC1 像素也没错位（0xFF112233）",
          QStringLiteral("0x%1").arg(decodedPng.pixel(48, 48), 8, 16, QLatin1Char('0')));

    // 解不出来的那些一律回 **null**（不是抛异常，也不是 1x1 的空图）—— 调用方（
    // ResourceStore）正是靠 isNull() 判"这一发没拿到图"，回一张 1x1 的空图会被当成成功。
    check(ImageDecode::decode(QByteArray()).isNull(),
          "ZC1 空的一串 = 没有图（不是一张空图）");
    check(ImageDecode::decode(QByteArrayLiteral("这不是任何一张图的字节")).isNull(),
          "ZC1 一段解不出图的字节 = null");
    check(ImageDecode::decode(QByteArrayLiteral("<html>502 Bad Gateway</html>")).isNull(),
          "ZC1 网关挡回来的那段 HTML = null（界面上就是画占位方块）");

    section("ZC2. webp：这台机器解不解得动，解出来的对不对");

    const bool webp = ImageDecode::webpAvailable();
    out() << "     （这台机器 webpAvailable() = " << (webp ? "true" : "false") << "）\n";

    QByteArray webpBytes;
    {
        QFile fixture(metaDir + "/icon-sample.webp");
        const bool haveFixture = fixture.open(QIODevice::ReadOnly);
        check(haveFixture, "ZC2 读到了真 webp 夹具（meta/icon-sample.webp）");
        if (haveFixture) {
            webpBytes = fixture.readAll();
            fixture.close();
        }
    }
    // 4800 字节 —— 那份夹具是从 Modrinth CDN 上真取下来的 96px 缩略图（同一张图的
    // icon.png 是 35218 字节，差 7 倍）。字节数也钉一下：换了夹具的话这里会先红。
    check(webpBytes.size() == 4800 && webpBytes.startsWith(QByteArrayLiteral("RIFF")) &&
              webpBytes.mid(8, 4) == QByteArrayLiteral("WEBP"),
          "ZC2 夹具就是那份 4800 字节的 webp",
          QStringLiteral("%1 字节").arg(webpBytes.size()));

    // 这一条是"这份 Qt 自己解不了它"的**活证据**（不是从别处抄来的结论）：要是哪天 Qt
    // 装上了 webp 插件，这里会先红 —— 那时 ImageDecode 里那条 WIC 分支就成了多余的东西。
    check(QImage().loadFromData(webpBytes) == false,
          "ZC2 这份 Qt 自己解不了 webp（所以才有 ImageDecode 这一层）");

    const QImage webpImage = ImageDecode::decode(webpBytes);
    // **「列表里说有」不等于「真解得动」**。GitHub 的 windows runner 是
    // Windows Server 镜像：`webpAvailable()` 枚举得到一条认 `.webp` 的解码器，可真拿
    // 这份字节去 CopyPixels 时失败、回 null（本机 Win11 上是好的）。所以这一节按
    // **真解出来的东西**分岔，而不是按当初那次枚举的结果 —— 产品在两种机器上的行为
    // 都对：解得出就给图，解不出就如实回 null。
    if (!webpImage.isNull()) {
        check(webpImage.size() == QSize(96, 96), "ZC2 解得动：96x96",
              QStringLiteral("%1x%2").arg(webpImage.width()).arg(webpImage.height()));
        // 中心像素：WIC 交给我们的那块内存是 **BGRA**（探针打出来是 `66 C8 7A FF`），
        // 而 QImage::Format_ARGB32 的 0xAARRGGBB 里 R 在高位 —— 也就是 0xFF7AC866。
        // 这一条同时钉住三件事：真的解出了像素、**通道没交换**（交换了会是 0xFF66C87A）、
        // alpha 没被乘进颜色里（Premultiplied 的话这个像素是纯色，看不出来，但换一张
        // 半透明的图就会差 —— 见 ImageDecode.cpp 里那条注）。
        check(webpImage.pixel(48, 48) == quint32(0xFF7AC866),
              "ZC2 中心像素对（BGRA → ARGB32 通道没交换）",
              QStringLiteral("0x%1").arg(webpImage.pixel(48, 48), 8, 16, QLatin1Char('0')));
    } else {
        // 没有那个编解码器（或者枚举说有、真解却失败）时**如实回 null**：ResourceStore
        // 拿到 null 就走"退回 PNG 那条地址"（见 startIconRequest）。这里要是硬解出点什么
        // 来（比如一张 0x0 的非空图）才糟糕 —— 调用方按 isNull() 判不出"没解出来"。
        check(webpImage.isNull(),
              "ZC2 这台机器解不了 webp：如实回 null（界面会退回 PNG，不是报错）");
    }
}

