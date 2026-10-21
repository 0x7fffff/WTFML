#include "Support.hpp"
int UiRunStateColorsChecks() {

    {
        // 要求：「左下角没有实例的时候显示（红色的）■…启动的时候那个符号
        // （也可以画图代替）变成黄色…启动以后符号变成绿色，和原来一样」。
        //
        // 颜色的判据只看**色相落在哪一段**（外加饱和度要够）—— 不写死 #c02828 那种十六进制：
        // 那等于把色值抄了一遍（改色时两处一起改才绿，测试就成了"复读机"）。色相是
        // "这是红/黄/绿哪一种"的本体，深浅两套各自微调色值时它都不变。
        //
        // 灰那一档**不能用色相量**（灰没有色相：QColor::hue() 对它是 -1，本节的 hueOf()
        // 又把它归成 0，正好和红撞上）。它量的换成"**说不上是什么颜色**"：饱和度低到
        // 一定程度。红/黄/绿那三档仍然按色相分段量。
        const auto hueOf = [](const QColor &c) {
            // QColor::hue() 对灰色返回 -1，这里统一成 0..359
            const int h = c.hue();
            return h < 0 ? 0 : h;
        };
        const auto inBand = [&hueOf](const QColor &c, int lo, int hi) {
            const int h = hueOf(c);
            return h >= lo && h <= hi;
        };
        // 浅底和深底各来一遍：四档在两种主题下都得一眼分得出（红/黄/绿各是各的，
        // 灰一眼看去就是"没颜色"）。
        // 判据是**窗口底色**的明度（和 folderDangerColor() 同一处），所以这里只摆这一项。
        // 别用 QPalette(QColor) 那个单参构造：它算出来的 Window 是哪个色不好记，
        // 而这一节量的正是"底色深浅"这条判据。
        QPalette light;
        light.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
        QPalette dark;
        dark.setColor(QPalette::Window, QColor(0x20, 0x20, 0x20));
        const QPalette palettes[2] = {light, dark};
        const char *names[2] = {"浅底", "深底"};
        for (int i = 0; i < 2; ++i) {
            const QColor red =
                MainWindowUI::runStateMarkColor(MainWindowUI::RunStateMark::Error, palettes[i]);
            const QColor yellow =
                MainWindowUI::runStateMarkColor(MainWindowUI::RunStateMark::Launching, palettes[i]);
            const QColor green =
                MainWindowUI::runStateMarkColor(MainWindowUI::RunStateMark::Running, palettes[i]);
            const QColor grey =
                MainWindowUI::runStateMarkColor(MainWindowUI::RunStateMark::Idle, palettes[i]);
            // 灰：**说不上是什么颜色** —— 饱和度低。判据用 ≤40 而不是 ==0：深浅两套各自
            // 给的那一档灰（`QPalette::Disabled / WindowText`）在这两套调色板里都是纯灰，
            // 留一点余量是给"以后会不会带一点点色偏"的，不是给"灰得发绿"的。
            check(grey.saturation() <= 40,
                  QStringLiteral("%1：没有实例在跑 → 灰色（饱和度低）").arg(QString::fromUtf8(names[i])),
                  QStringLiteral("%1 色相 %2 饱和 %3")
                      .arg(grey.name())
                      .arg(hueOf(grey))
                      .arg(grey.saturation()));
            // 红：色相贴着 0/360 那一段（360 和 0 是同一个地方，两头都收）
            check((inBand(red, 0, 20) || inBand(red, 340, 359)) && red.saturation() >= 100,
                  QStringLiteral("%1：有实例异常退出 → 红色").arg(QString::fromUtf8(names[i])),
                  QStringLiteral("%1 色相 %2 饱和 %3")
                      .arg(red.name())
                      .arg(hueOf(red))
                      .arg(red.saturation()));
            check(inBand(yellow, 30, 60) && yellow.saturation() >= 100,
                  QStringLiteral("%1：正在启动 → 黄色").arg(QString::fromUtf8(names[i])),
                  QStringLiteral("%1 色相 %2 饱和 %3")
                      .arg(yellow.name())
                      .arg(hueOf(yellow))
                      .arg(yellow.saturation()));
            check(inBand(green, 100, 160) && green.saturation() >= 80,
                  QStringLiteral("%1：有实例在跑 → 绿色").arg(QString::fromUtf8(names[i])),
                  QStringLiteral("%1 色相 %2 饱和 %3")
                      .arg(green.name())
                      .arg(hueOf(green))
                      .arg(green.saturation()));
            const int hRed = hueOf(red);
            const int hYellow = hueOf(yellow);
            const int hGreen = hueOf(green);
            check(qAbs(hYellow - hRed) >= 40 && qAbs(hGreen - hYellow) >= 40 &&
                      qAbs(hGreen - hRed) >= 40,
                  QStringLiteral("%1：红/黄/绿的色相两两差得开（≥40）").arg(QString::fromUtf8(names[i])),
                  QStringLiteral("%1 / %2 / %3").arg(hRed).arg(hYellow).arg(hGreen));
            // 灰和那三个色是**四个不同的色**（灰不能正好等于红 —— 那正是"把没实例画成红"
            // 那件旧事又回来了，而上面所有色相断言都拦不住它）。
            check(grey.rgb() != red.rgb() && grey.rgb() != yellow.rgb() && grey.rgb() != green.rgb(),
                  QStringLiteral("%1：灰和红/黄/绿是三个不同的色").arg(QString::fromUtf8(names[i])),
                  QStringLiteral("%1 / %2 / %3").arg(grey.name(), red.name(), yellow.name()));
        }
        // 红那一档**就是**「删除」那一项的红（同一个界面上的两种红共用一个出处）
        check(MainWindowUI::runStateMarkColor(MainWindowUI::RunStateMark::Error, light) ==
                      MainWindowUI::folderDangerColor(light) &&
                  MainWindowUI::runStateMarkColor(MainWindowUI::RunStateMark::Error, dark) ==
                      MainWindowUI::folderDangerColor(dark),
              "红那一档复用 folderDangerColor()（两种主题都是同一个红）");

        // ---- 那张图本身：一个**实心**方块（用户写的就是 ■）----
        // 四档全走一遍（4 = `RunStateMark` 现在的档数：漏一档这里就少画一遍，而下面那几条
        // "两两不同"仍然会绿 —— 所以枚举加一档就要回来把它加一）。
        for (int i = 0; i < 4; ++i) {
            const auto state = MainWindowUI::RunStateMark(i);
            const QPixmap pixmap = MainWindowUI::runStateMarkPixmap(state, light);
            const QColor want = MainWindowUI::runStateMarkColor(state, light);
            const QImage image = pixmap.toImage();
            check(!image.isNull() && image.width() == MainWindowUI::runStateMarkSize() &&
                      image.height() == MainWindowUI::runStateMarkSize(),
                  QStringLiteral("四档都画出一张 %1x%1 的图")
                      .arg(MainWindowUI::runStateMarkSize()),
                  QStringLiteral("%1x%2").arg(image.width()).arg(image.height()));
            // 逐像素：整张图每一格都是那个颜色、而且完全不透明。
            // QLabel 的图默认按左上角摆，所以"方块长什么样"就是这张图长什么样。
            int wrong = 0;
            int translucent = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor px = image.pixelColor(x, y);
                    if (px.alpha() != 255) {
                        ++translucent;
                    } else if (px.rgb() != want.rgb()) {
                        ++wrong;
                    }
                }
            }
            check(wrong == 0 && translucent == 0,
                  QStringLiteral("四档那张图是**实心**的（每像素都是那个色、没有半透明边）"),
                  QStringLiteral("state=%1 异色 %2 半透明 %3").arg(i).arg(wrong).arg(translucent));
            // 四角也是那个色 —— 这一条是"没开抗锯齿"的直接判据（开了的话角上会是
            // 50% 的色，值看着差不多、`!=` 一比就红）。
            check(image.pixelColor(0, 0) == want &&
                      image.pixelColor(image.width() - 1, 0) == want &&
                      image.pixelColor(0, image.height() - 1) == want &&
                      image.pixelColor(image.width() - 1, image.height() - 1) == want,
                  QStringLiteral("四角也是纯色（没开抗锯齿，不然角上会是半透明的）"));
        }
        // 四张图**两两不是同一张**（不然"变色"这件事根本没发生，而上面那些色相断言还是会绿）
        const QImage imgGrey = MainWindowUI::runStateMarkPixmap(
                                   MainWindowUI::RunStateMark::Idle, light).toImage();
        const QImage imgYellow = MainWindowUI::runStateMarkPixmap(
                                     MainWindowUI::RunStateMark::Launching, light).toImage();
        const QImage imgGreen = MainWindowUI::runStateMarkPixmap(
                                    MainWindowUI::RunStateMark::Running, light).toImage();
        const QImage imgRed = MainWindowUI::runStateMarkPixmap(
                                  MainWindowUI::RunStateMark::Error, light).toImage();
        check(imgGrey != imgYellow && imgGrey != imgGreen && imgGrey != imgRed &&
                  imgYellow != imgGreen && imgYellow != imgRed && imgGreen != imgRed,
              "四档的图两两不同（真的换了颜色，不是同一张）");
    }
 return 0;
}


