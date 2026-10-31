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
    // **真解出来的东西**分岔，而不是按枚举的结果 —— 产品在两种机器上的行为
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

    section("ZC3. 解码上限：头里写着多大就先拒，别按它去分配内存");
    // 图标 / 皮肤来自外部（Modrinth CDN、服务器答复里那串 data URL、用户拖进来的文件），
    // 一个几十字节的"头"就能声称 8000x8000 —— 照它分配是 192 MB，几张就能把进程按死。
    // 所以 `ImageDecode` 在**读像素之前**先看声明的尺寸（见 ImageDecode.hpp 的 Limits）。
    {
        const ImageDecode::Limits dflt;
        check(ImageDecode::sizeWithinLimits(QSize(16, 16), dflt), "ZC3 16x16 在默认上限里");
        check(ImageDecode::sizeWithinLimits(
                  QSize(ImageDecode::kMaxDecodedSide, ImageDecode::kMaxDecodedSide), dflt),
              "ZC3 4096x4096 恰好卡在默认上限上（收）");
        check(!ImageDecode::sizeWithinLimits(QSize(ImageDecode::kMaxDecodedSide + 1, 1), dflt),
              "ZC3 边长超一个像素就不收");
        check(!ImageDecode::sizeWithinLimits(
                  QSize(ImageDecode::kMaxDecodedSide, ImageDecode::kMaxDecodedSide + 1), dflt),
              "★ ZC3 两边都没超边长、像素数超了也不收（4096x4097）");
        check(!ImageDecode::sizeWithinLimits(QSize(), dflt)
                  && !ImageDecode::sizeWithinLimits(QSize(0, 10), dflt)
                  && !ImageDecode::sizeWithinLimits(QSize(-5, 5), dflt),
              "ZC3 空的 / 零边 / 负边都不收（`QSize::isValid()` 那一条）");
        check(ImageDecode::sizeWithinLimits(QSize(100, 100), ImageDecode::Limits{100, 10000})
                  && !ImageDecode::sizeWithinLimits(QSize(101, 100),
                                                    ImageDecode::Limits{100, 10000}),
              "ZC3 调用方自己给的上限说了算（不只看那两个默认常量）");
    }
    check(ImageDecode::decode(pngBytes, ImageDecode::Limits{32, 32 * 32}).isNull(),
          "ZC3 上限调到 32x32 之后，上面那张 96x96 的 PNG 当场拒");
    check(!ImageDecode::decode(pngBytes, ImageDecode::Limits{96, 96 * 96}).isNull(),
          "ZC3 上限刚好够（96x96）就照常解得回来（闸没把正常图也拦掉）");

    // 手搓一个"头声称很大、像素一个字节都没有"的 BMP：BITMAPINFOHEADER 定长 54 字节，
    // 解码器读得到宽高、读不到像素 —— 要证的正是"这时候连试都不试"。
    const auto oversizedBmp = [](quint32 side) {
        QByteArray bytes;
        const auto u16 = [&bytes](quint16 value) {
            bytes.append(static_cast<char>(value & 0xFF));
            bytes.append(static_cast<char>((value >> 8) & 0xFF));
        };
        const auto u32 = [&bytes](quint32 value) {
            bytes.append(static_cast<char>(value & 0xFF));
            bytes.append(static_cast<char>((value >> 8) & 0xFF));
            bytes.append(static_cast<char>((value >> 16) & 0xFF));
            bytes.append(static_cast<char>((value >> 24) & 0xFF));
        };
        bytes.append(QByteArrayLiteral("BM"));
        u32(54);   // 文件大小（这里不按它算图）
        u16(0);    // 保留
        u16(0);
        u32(54);   // 像素数据偏移
        u32(40);   // BITMAPINFOHEADER
        u32(side); // 宽
        u32(side); // 高（正数 = 自下而上，够解码器认）
        u16(1);    // planes
        u16(24);   // 每像素 24 位
        u32(0);    // BI_RGB
        u32(0);    // 图像大小（BI_RGB 时可以写 0）
        u32(2835); // 横向 DPI
        u32(2835); // 纵向 DPI
        u32(0);    // 调色板
        u32(0);    // 重要色
        return bytes;
    };
    QByteArray bomb = oversizedBmp(8000);
    {
        QBuffer probeBuffer(&bomb);
        probeBuffer.open(QIODevice::ReadOnly);
        QImageReader probe(&probeBuffer);
        check(probe.size() == QSize(8000, 8000),
              "ZC3 夹具的头确实声称 8000x8000（解码器读得到那个数）",
              QStringLiteral("%1x%2").arg(probe.size().width()).arg(probe.size().height()));
    }
    check(ImageDecode::decode(bomb).isNull(),
          "★ ZC3 80 字节的头声称 8000x8000 ⇒ 直接回 null（不按头分配 192 MB）");
}

