#include "Support.hpp"
int UiContentRowsChecks() {
    // 这一节对着用户 那三句话：
    // 「模组和资源包等都有一个图标，把图标显示出来」→ Z15c
    // 「启用和禁用使用开关的那种形式，删除还是垃圾桶」→ Z15a + Z15c
    // 「motd按照游戏里显示的样式展现」→ Z15b + Z15d
    //
    // 【为什么图标那一半只在这儿验"换上去、刷新之后还在"】
    //
    // 「这是哪个资源」那一步是**按文件哈希反查**（sha1 → Modrinth、指纹 → CurseForge），
    // 它要真算一遍哈希、真发两发 HTTP —— 离线机器上跑不了，而且拿到的图标地址指向
    // `cdn.modrinth.com`（写死的、没有口子），"认出来了但没图"和"根本没认出来"在屏幕上
    // 长得一模一样。所以那一半在 mw_check 的 T22 里对着假接口量（量到"身份"为止），
    // 这一节量的是另一半：**图到手之后这一格换了没有、刷新之后还在不在**。
    {
        using Dlg = InstanceSettingsDialog;

        // ---- Z15a 开关本身 ----
        {
            // 这个**空壳父控件**只为底下"量像素"那一段存在（让开关是子控件、
            // 不是顶层窗口 —— 见那一段的注释）。它必须声明在 toggle **前面**：析构是反
            // 着来的，这样 toggle 先析构、把自己从父的孩子表里摘掉，host 才轮到析构；
            // 反过来写的话，host 析构时会 `delete` 一个**还在栈上**的孩子 —— 这一节
            // 写过一版就是这么崩的（跑到 Z15a 最后一条之后悄没声地死掉，没有报错）。
            QWidget host;
            ToggleSwitch toggle;
            // 尺寸的**唯一出处**：控件自己的那两个静态函数读的就是 MainWindowUI 里那三个
            // 常数。两处各写一份的话，"画出来的药丸"和"布局给它的地方"会各算各的
            // （症状是圆钮被裁掉半个，而那种错没人会往尺寸上想）。
            check(ToggleSwitch::switchWidth() == MainWindowUI::toggleSwitchWidth() &&
                      ToggleSwitch::switchHeight() == MainWindowUI::toggleSwitchHeight(),
                  "Z15a 开关的宽高就是 MainWindowUI 里那一对常数（画的和占的是同一个数）",
                  QStringLiteral("%1x%2").arg(ToggleSwitch::switchWidth())
                      .arg(ToggleSwitch::switchHeight()));
            check(toggle.sizeHint() == QSize(ToggleSwitch::switchWidth(),
                                             ToggleSwitch::switchHeight()),
                  "★ Z15a sizeHint() 给的就是那个尺寸（开关在布局里必须**正好**占那么一格，"
                  "不然行尾两列按钮对不齐）",
                  QStringLiteral("%1x%2").arg(toggle.sizeHint().width())
                      .arg(toggle.sizeHint().height()));
            check(!toggle.isChecked(), "Z15a 刚造出来是**关**的（谁用谁 setChecked）");

            // 它是一个 QAbstractButton：`click()` / `toggled` / 禁用这些事都由基类管
            // （见它的文件头）。这里量的是"点一下真的翻过来了"。
            int toggledCount = 0;
            bool lastValue = false;
            QObject::connect(&toggle, &ToggleSwitch::toggled, &toggle,
                             [&toggledCount, &lastValue](bool on) {
                                 ++toggledCount;
                                 lastValue = on;
                             });
            toggle.click();
            check(toggledCount == 1 && lastValue && toggle.isChecked(),
                  "★ Z15a 点一下翻到「开」并且发一次 toggled(true)", QStringLiteral("%1 次")
                      .arg(toggledCount));
            toggle.click();
            check(toggledCount == 2 && !lastValue && !toggle.isChecked(),
                  "★ Z15a 再点一下翻回「关」（它不是单向的一次性开关）");

            // ---- 量像素：这是一枚**画出来的**控件（和底栏那枚状态方块同一类东西）----
            //
            // 判据落在"圆钮在哪一头"上：这是"开关"这个形状的全部意义 —— 只看颜色的话，
            // 一个不画圆钮的实现照样能过（而它看着就是一个色块，不是开关）。
            //
            // **必须挂在一个父控件上量**。下面这几行原来是把开关当**顶层
            // 窗口** `show()` 的：原生 windows 平台会给顶层窗口一个最小宽度（Win32 给
            // 标题栏那三颗按钮留的位置），`resize(34, 18)` 之后 `width()` 实际成了
            // **120**。而 `paintEvent` 是照 `width()` 摆圆钮的 —— 这是对的，控件就该按
            // 自己拿到的格子画 —— 于是"开着"那半边的圆钮被摆到了 x≈106，而这里按
            // `switchWidth()` 算出来的取样点还在 x≈25、量到的是**轨道的高亮色**。
            // 这是只红在真 windows、offscreen 上恒绿的假红（那套桩平台不设最小宽度）。
            // 挂到父控件上，`resize()` 给多少就是多少，两套平台量到的是同一枚开关。
            // （那个 `host` 在 Z15a 这个块的开头就声明好了 —— 声明次序就是析构次序。）
            toggle.setParent(&host);
            toggle.resize(ToggleSwitch::switchWidth(), ToggleSwitch::switchHeight());
            toggle.show();
            check(toggle.width() == ToggleSwitch::switchWidth(),
                  "★ Z15a 量像素之前，开关的**实际宽度**就是那个常数（它是子控件，"
                  "没被平台撑成顶层窗口那个最小宽）",
                  QStringLiteral("%1 px").arg(toggle.width()));
            const int knobMargin = MainWindowUI::toggleSwitchKnobMargin();
            const int knob = qMax(4, ToggleSwitch::switchHeight() - 2 * knobMargin);
            const auto sample = [&toggle](const QImage &image, int x, int y) {
                // offscreen 下 devicePixelRatio 通常是 1，但不写死：按图的真实宽度换一次
                // 坐标。写死的话，哪天它就是 2 了，这里量到的是"另一边的像素"。
                const double scale = image.width() / double(qMax(1, toggle.width()));
                return image.pixelColor(qRound(x * scale), qRound(y * scale));
            };
            const int midY = ToggleSwitch::switchHeight() / 2;
            // 关着：圆钮贴左、轨道在右；开着：反过来。
            const int offKnobX = knobMargin + knob / 2;
            const int offTrackX = ToggleSwitch::switchWidth() - knobMargin - 1;
            const int onKnobX = ToggleSwitch::switchWidth() - knobMargin - knob / 2;
            const int onTrackX = knobMargin + 1;

            // NOTE: 比的是 **8 位的 `.rgb()`**，不是 `QColor::operator==`。Qt 6 里颜色内部
            // 是 16 位一通道，而 Fusion 的 `Mid` / `Highlight` 是从基色上
            // `darker()` / `lighter()` 推出来的 —— 那一步算出来的 16 位值不是 257 的
            // 整数倍，于是"画出来的像素"（8 位还原成 16 位）和"调色板里那个颜色"
            // 在 `==` 下**不相等**，而 `.name()` 打印出来一模一样（这一条第一次跑
            // 就是这么红的）。这对任何一个从调色板取色的像素断言都成立。
            const auto paintRgb = [](const QColor &color) { return color.rgb(); };
            toggle.setChecked(false);
            toggle.repaint();
            const QImage offImage = toggle.grab().toImage();
            const QColor offKnob = sample(offImage, offKnobX, midY);
            const QColor offTrack = sample(offImage, offTrackX, midY);
            toggle.setChecked(true);
            toggle.repaint();
            const QImage onImage = toggle.grab().toImage();
            const QColor onKnob = sample(onImage, onKnobX, midY);
            const QColor onTrack = sample(onImage, onTrackX, midY);

            check(paintRgb(offKnob) == paintRgb(toggle.palette().color(QPalette::Base)) &&
                      paintRgb(onKnob) == paintRgb(toggle.palette().color(QPalette::Base)),
                  "★ Z15a 圆钮在**关**的时候贴左边、在**开**的时候贴右边（同一枚圆钮、"
                  "两头各量一次都是圆钮那个色）",
                  QStringLiteral("关 %1 / 开 %2")
                      .arg(offKnob.name(), onKnob.name()));
            // 两头的轨道都是从**调色板**取的（不是写死的色号）：用户换主题或者哪天把
            // Fusion 换掉，这一格跟着走。写死色号的话，深色主题下这枚开关会是一块浅色药丸。
            const QColor expectedOn = Theme::current().presetId == QLatin1String("emerald")
                                          ? Theme::tokens().primary : toggle.palette().color(QPalette::Highlight);
            check(paintRgb(offTrack) == paintRgb(toggle.palette().color(QPalette::Mid)) &&
                      paintRgb(onTrack) == paintRgb(expectedOn),
                  "★ Z15a 轨道：关着是中灰那一档、开着是高亮那一档 —— 两档都取自调色板"
                  "（写死色号的话，换个主题这枚开关就和整个界面不是一个调子）",
                  QStringLiteral("关 %1（中灰 %2）/ 开 %3（高亮 %4）")
                      .arg(offTrack.name(), toggle.palette().color(QPalette::Mid).name(),
                           onTrack.name(),
                           toggle.palette().color(QPalette::Highlight).name()));
            toggle.hide();
        }

        // ---- Z15b MOTD 那串 `§` 码：照游戏那套规矩拆开（纯函数）----
        {
            using MotdText::parse;
            using MotdText::pieceColor;

            // 颜色码 + 字形码：都是"从这里往后都算"，而且在**码的字面上**就该断成两段
            // （一段一个颜色/字形，画的人才能一段一段量宽度）。
            const QVector<MotdPiece> colored = parse(QStringLiteral("§a绿§l粗"));
            check(colored.size() == 2,
                  "★ Z15b `§a绿§l粗` 拆成两段（颜色码和字形码处各断一次）",
                  QStringLiteral("%1 段").arg(colored.size()));
            if (colored.size() == 2) {
                check(colored.at(0).text == QStringLiteral("绿") && !colored.at(0).bold &&
                          pieceColor(colored.at(0)) == QColor(0x55, 0xFF, 0x55),
                      "★ Z15b 第一段是「绿」、用游戏里 `§a` 那个绿（55 FF 55 —— 色号是原样"
                      "抄的那张表，不是随手挑的一个绿）",
                      pieceColor(colored.at(0)).name());
                check(colored.at(1).text == QStringLiteral("粗") && colored.at(1).bold &&
                          colored.at(1).color == QLatin1Char('a'),
                      "★ Z15b 第二段是粗体，而且**颜色还是 `§a`**（字形码不影响颜色 —— "
                      "游戏就是这规矩，写成「换个码就整段复位」的话，服务器的配色会半路丢）");
            }

            // 复位码：`§r` 之后颜色和字形全回默认。
            const QVector<MotdPiece> reset = parse(QStringLiteral("§a红§r白"));
            check(reset.size() == 2 && reset.at(1).color.isNull() && !reset.at(1).bold,
                  "★ Z15b `§r` 之后颜色回默认（空色号 = 白，见 minecraftColor）",
                  QStringLiteral("%1 段").arg(reset.size()));

            // 换行：游戏那边的 MOTD 就是两行，行号得分开。
            const QVector<MotdPiece> twoLines = parse(QStringLiteral("第一行\n第二行"));
            check(twoLines.size() == 2 && twoLines.at(0).line == 0 && twoLines.at(1).line == 1,
                  "Z15b `\\n` 分开两行（`line` 那一格 0/1）");
            check(MotdText::lineCount(QStringLiteral("第一行\n第二行")) == 2 &&
                      MotdText::lineCount(QString()) == 0,
                  "★ Z15b 行数：两行的算 2 行、**空串算 0 行**（0 行和 1 行决定那一格留多高）");
            const QVector<MotdPiece> threeLines =
                parse(QStringLiteral("一\n二\n三"), 2);
            check(threeLines.size() == 2 && MotdText::lineCount(QStringLiteral("一\n二\n三")) == 2,
                  "★ Z15b 第三行起整段丢掉（游戏那边也只画两行；画第三行会被行高裁掉半截）",
                  QStringLiteral("%1 段").arg(threeLines.size()));

            // 1.16 之后那种六位写法：`§x` 后面还得有六组 `§R§R§G§G§B§B`。
            const QVector<MotdPiece> hexed = parse(QStringLiteral("§x§f§f§0§0§0§0红"));
            check(hexed.size() == 1 && hexed.at(0).hex &&
                      hexed.at(0).rgb == QColor(0xFF, 0x00, 0x00).rgb() &&
                      pieceColor(hexed.at(0)) == QColor(0xFF, 0x00, 0x00),
                  "★ Z15b `§x§f§f§0§0§0§0` 是 (255,0,0) 的红（六位那种写法要六组都读齐）",
                  QStringLiteral("%1 段、hex=%2")
                      .arg(hexed.size())
                      .arg(hexed.isEmpty() ? 0 : hexed.at(0).hex));
            // 半截的颜色**整段不认**（比认下一个错颜色好：那种字看着像服务器配错了）。
            const QVector<MotdPiece> halfHex = parse(QStringLiteral("§x§f§f红"));
            check(halfHex.size() == 1 && !halfHex.at(0).hex &&
                      halfHex.at(0).text.contains(QStringLiteral("红")),
                  "★ Z15b 只写了一半的 `§x` 写法**整段不认**（不认时什么都不改，接着往后读）");

            // 那个字符自己在末尾（后面什么都没有）：不许把下一个位置吞掉。
            const QVector<MotdPiece> trailing =
                parse(QStringLiteral("abc§"));
            check(trailing.size() == 1 && trailing.at(0).text == QStringLiteral("abc"),
                  "★ Z15b 末尾孤零零一个 `§` 时**一个正常字符都不许少**"
                  "（硬吞下一个位置的话，服务器那句话会少一个字）",
                  trailing.isEmpty() ? QStringLiteral("<空>") : trailing.at(0).text);

            // 不认识的码：忽略（不是"顺手当普通字符画出来"——那屏幕上会冒出一个字母）。
            const QVector<MotdPiece> unknown = parse(QStringLiteral("§z字"));
            check(unknown.size() == 1 && unknown.at(0).text == QStringLiteral("字"),
                  "★ Z15b 不认识的码直接忽略（不许把它后面那一个字符当正文画出来）",
                  unknown.isEmpty() ? QStringLiteral("<空>") : unknown.at(0).text);
            // 乱码码（`§k`）：照原字画，不跳。
            const QVector<MotdPiece> obfuscated = parse(QStringLiteral("§k乱码"));
            check(obfuscated.size() == 1 && obfuscated.at(0).text == QStringLiteral("乱码"),
                  "Z15b `§k`（游戏里会跳字那一档）照原字画 —— 一个列表里跳字是纯粹的干扰");

            // 制表当空格（宽度是算出来的，制表在列表里没有意义）。
            const QVector<MotdPiece> tabbed = parse(QStringLiteral("a\tb"));
            check(tabbed.size() == 1 && tabbed.at(0).text == QStringLiteral("a b"),
                  "Z15b 制表当空格（列表里的宽度是算出来的，制表没有意义）",
                  tabbed.isEmpty() ? QStringLiteral("<空>") : tabbed.at(0).text);
            check(MotdText::minecraftColor(QLatin1Char('c')) == QColor(0xFF, 0x55, 0x55) &&
                      MotdText::minecraftColor(QLatin1Char('z')) == QColor(0xFF, 0xFF, 0xFF),
                  "★ Z15b 没写颜色（或者写了不认识的色号）时按**白**算（游戏那边就是这样）");

            // ---- 读得清那一档：游戏是黑底、我们是浅底 ----
            //
            // 服务器按黑底配的 `§f` 白字直接搬到浅色列表里等于没画。这一条量的是
            // "该压的压了、该留的留了"：浅底上的白字被压暗，深底上的白字**原样**不动。
            const QColor light(0xFF, 0xFF, 0xFF);
            const QColor darkSurface(0x20, 0x20, 0x20);
            const QColor white(0xFF, 0xFF, 0xFF);
            const QColor onLight = MotdText::readableOn(white, light);
            const QColor onDark = MotdText::readableOn(white, darkSurface);
            check(onDark.rgb() == white.rgb(),
                  "★ Z15b 底色本来就深的时候**原样**用服务器给的颜色（那是它的配色，"
                  "不该被我们改）", onDark.name());
            check(onLight != white &&
                      (onLight.red() + onLight.green() + onLight.blue()) <
                          (white.red() + white.green() + white.blue()),
                  "★ Z15b 浅底上的白字被压暗（不压的话那一格等于没画）", onLight.name());
            const QColor redOnLight = MotdText::readableOn(QColor(0xFF, 0x55, 0x55), light);
            check(redOnLight.red() >= redOnLight.green() && redOnLight.red() >= redOnLight.blue(),
                  "★ Z15b 只压暗、**不调色相**：压过的红还是红（不然后面那列颜色就认不出来了）",
                  redOnLight.name());
        }

        // ---- Z15c 「文件管理」那几页的行：行首图标 + 开关 + 垃圾桶 ----
        {
            QTemporaryDir scratch;
            const QString root = scratch.path();
            const auto mkfile = [](const QString &path) {
                QDir().mkpath(QFileInfo(path).absolutePath());
                QFile file(path);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(QByteArray(4096, 'x'));
                }
            };
            const QString modsDir = root + QStringLiteral("/mods");
            const QString offPath = modsDir + QStringLiteral("/禁用掉的.jar.disabled");
            mkfile(modsDir + QStringLiteral("/sodium.jar"));
            mkfile(offPath);
            mkfile(root + QStringLiteral("/resourcepacks/材质.zip"));
            mkfile(root + QStringLiteral("/datapacks/数据.zip"));
            mkfile(root + QStringLiteral("/shaderpacks/光影.zip"));
            mkfile(root + QStringLiteral("/schematics/房子.litematic"));
            // 存档：三个世界，专门用来量"目录自己带着的那张图"那一条（见下面 Z15c 里那段）。
            const QString saveWorld = root + QStringLiteral("/saves/我的世界");
            const QString savePlain = root + QStringLiteral("/saves/没图的世界");
            const QString saveBroken = root + QStringLiteral("/saves/图坏的世界");
            mkfile(saveWorld + QStringLiteral("/level.dat"));
            mkfile(savePlain + QStringLiteral("/level.dat"));
            mkfile(saveBroken + QStringLiteral("/level.dat"));
            // 真存档目录里那张缩略图 = 游戏存世界时自己写的 `icon.png`。**66x66**，
            // 比这一格（32）大 —— 顺带把"缩过"这件事也量了。
            QPixmap worldShot(66, 66);
            worldShot.fill(QColor(0xCC, 0x33, 0x99));
            check(worldShot.save(saveWorld + QStringLiteral("/icon.png"), "PNG"),
                  "Z15c 前提：世界缩略图写进了盘（写不出来的话，下面那几条量的就不是"
                  "「读出来没有」）");
            // 名字叫 icon.png 但**不是**一张图（几十个 'x'）—— 量"读不出来"那一支。
            mkfile(saveBroken + QStringLiteral("/icon.png"));

            QHash<QString, QString> dirs;
            dirs.insert(QStringLiteral("mod"), modsDir);
            dirs.insert(QStringLiteral("resourcepack"), root + QStringLiteral("/resourcepacks"));
            dirs.insert(QStringLiteral("datapack"), root + QStringLiteral("/datapacks"));
            dirs.insert(QStringLiteral("shader"), root + QStringLiteral("/shaderpacks"));
            dirs.insert(QStringLiteral("schematic"), root + QStringLiteral("/schematics"));
            dirs.insert(QStringLiteral("save"), root + QStringLiteral("/saves"));
            dirs.insert(QStringLiteral("logs"), root + QStringLiteral("/logs"));
            dirs.insert(QStringLiteral("screenshots"), root + QStringLiteral("/screenshots"));

            Dlg dialog(QStringLiteral("Z15 实例"), QStringLiteral("z15-instance"),
                       Dlg::Override(), Dlg::GlobalValues(), QStringList(), true);
            dialog.setInstanceDir(root);
            dialog.setFileDirs(dirs);
            dialog.show();
            dialog.showPage(QStringLiteral("mod"));

            const auto iconLabelAt = [](InstanceFilesPage *page, int row) -> QLabel * {
                QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
                QWidget *rowWidget =
                    item != nullptr && page != nullptr ? page->list()->itemWidget(item) : nullptr;
                return rowWidget != nullptr
                           ? rowWidget->findChild<QLabel *>(InstanceFilesPage::iconLabelName())
                           : nullptr;
            };

            InstanceFilesPage *mods = dialog.filesPage(QStringLiteral("mod"));
            check(mods != nullptr && mods->list() != nullptr && mods->list()->count() == 2,
                  "Z15c 前提：模组那一页列出那两条",
                  QStringLiteral("%1 条").arg(mods != nullptr ? mods->list()->count() : -1));

            // ---- 每一行都有图标那一格（中间那几页都查一遍）----
            //
            // 少了这一格的后果不是"那一行没图"，是**那一页整列的左边界对不齐**（有图的
            // 行多缩一格），而"看着像某几行漏画了"这种错最难被当成 bug 报上来。
            {
                int pagesChecked = 0;
                int rowsChecked = 0;
                bool allHaveIconSlot = true;
                bool allSameWidth = true;
                bool allRowHeight = true;
                // 「原理图 / 配置文件」这两页没有图，产品侧**不建**那一格（见
                // `InstanceFilesPage::setRowIconsVisible()`）：这里反过来核"确实没有"。
                int iconlessRowsChecked = 0;
                bool noSlotOnIconlessPages = true;
                for (const QString &key : Dlg::filePageKeys()) {
                    // 「服务器列表」那一页是棵表（另有一节量它），不是"一行一个文件"那种行。
                    if (key == Dlg::serverPageKey()) {
                        continue;
                    }
                    InstanceFilesPage *page = dialog.filesPage(key);
                    if (page == nullptr || page->list() == nullptr) {
                        continue;
                    }
                    ++pagesChecked;
                    page->refresh();
                    const bool pageHasIcons = key != QStringLiteral("schematic")
                                              && key != QStringLiteral("config");
                    for (int row = 0; row < page->list()->count(); ++row) {
                        QLabel *label = iconLabelAt(page, row);
                        if (pageHasIcons) {
                            ++rowsChecked;
                            allHaveIconSlot = allHaveIconSlot && label != nullptr;
                            allSameWidth =
                                allSameWidth && label != nullptr &&
                                label->width() == MainWindowUI::instanceFileIconSize();
                        } else {
                            ++iconlessRowsChecked;
                            noSlotOnIconlessPages = noSlotOnIconlessPages && label == nullptr;
                        }
                        QListWidgetItem *item = page->list()->item(row);
                        allRowHeight =
                            allRowHeight && item != nullptr &&
                            item->sizeHint().height() == MainWindowUI::versionTreeRowHeight();
                    }
                }
                // 页数从键表里算（同 U10a 那条 NOTE：写死的数字是加一页就红的那一种）。
                // **减 1** = 跳过「服务器列表」那一页（8 条键表 ⇒ 7 页有东西可量）。
                const int expectedPages = Dlg::filePageKeys().size() - 1;
                check(pagesChecked == expectedPages && rowsChecked >= 6,
                      "Z15c 前提：中间那几页都列到了东西（下面那三条才有东西可量）",
                      QStringLiteral("%1 页 / %2 行").arg(pagesChecked).arg(rowsChecked));
                check(allHaveIconSlot && rowsChecked > 0,
                      "★ Z15c **有图的那几页每一行**最左边都有那一格图标（少一格的话那一列"
                      "左边界就对不齐，而看着像某几行漏画了）");
                check(noSlotOnIconlessPages && iconlessRowsChecked > 0,
                      "★ Z15c 原理图 / 配置文件那两页**一个图标格都不建**（整列占位拼图白占宽度）",
                      QStringLiteral("%1 行").arg(iconlessRowsChecked));
                check(allSameWidth,
                      "★ Z15c 那一格是固定宽度（= instanceFileIconSize()）：真图标回来换上去"
                      "时布局不该重算 —— 一列几十行同时重排会让整张表抖一下",
                      QStringLiteral("%1 px").arg(MainWindowUI::instanceFileIconSize()));
                check(allRowHeight,
                      "★ Z15c 行高还是那一个常数（46）—— 多了图标这一格之后行**没变高**",
                      QStringLiteral("%1 px").arg(MainWindowUI::versionTreeRowHeight()));
            }

            // ---- 真图标换上去：那一格换的是**这一条**的图 ----
            const int iconPx = MainWindowUI::instanceFileIconSize();
            QPixmap real(iconPx, iconPx);
            real.fill(QColor(0x11, 0x88, 0x44));
            const QString jarPath = modsDir + QStringLiteral("/sodium.jar");
            // 行号会随 refresh() 变，所以全程**按路径**找那一行（和产品那条路同一个规矩）。
            const auto rowOfPath = [](InstanceFilesPage *page, const QString &path) {
                for (int row = 0; row < page->list()->count(); ++row) {
                    if (page->list()->item(row)->data(InstanceFilesPage::pathRole()).toString() ==
                        path) {
                        return row;
                    }
                }
                return -1;
            };

            const int jarRow = rowOfPath(mods, jarPath);
            const int offRow = rowOfPath(mods, offPath);
            check(jarRow >= 0 && offRow >= 0, "Z15c 前提：两条都按路径找得到那一行");
            const QPixmap beforeOther =
                iconLabelAt(mods, offRow) != nullptr ? iconLabelAt(mods, offRow)->pixmap()
                                                     : QPixmap();

            const bool accepted = mods->setIconForPath(jarPath, real);
            check(accepted, "★ Z15c setIconForPath() 认了这一条（按路径找回那一行）");
            const QPixmap afterJar =
                iconLabelAt(mods, jarRow) != nullptr ? iconLabelAt(mods, jarRow)->pixmap()
                                                     : QPixmap();
            check(!afterJar.isNull() &&
                      afterJar.toImage().pixelColor(iconPx / 2, iconPx / 2) ==
                          QColor(0x11, 0x88, 0x44),
                  "★ Z15c 那一格真换成了**这一条的**图（量的是画出来的那一个像素，"
                  "不是「我们存下来了」这件事）");
            check(iconLabelAt(mods, offRow) != nullptr &&
                      iconLabelAt(mods, offRow)->pixmap().toImage() == beforeOther.toImage(),
                  "★ Z15c **只换那一行**：另一条还是原来那张占位图（一换就全换的话，"
                  "一列下来每一条都是同一个模组的图标）");

            // ---- 「刷新」之后还在（这一条是这一页自己记着 `m_icons` 的全部意义）----
            mods->refresh();
            const int jarRowAfter = rowOfPath(mods, jarPath);
            check(jarRowAfter >= 0, "Z15c 前提：刷新之后那一条还在");
            const QPixmap afterRefresh =
                iconLabelAt(mods, jarRowAfter) != nullptr
                    ? iconLabelAt(mods, jarRowAfter)->pixmap()
                    : QPixmap();
            check(!afterRefresh.isNull() &&
                      afterRefresh.toImage().pixelColor(iconPx / 2, iconPx / 2) ==
                          QColor(0x11, 0x88, 0x44),
                  "★★ Z15c 点过「刷新」之后那一格**还是这张图** —— refresh() 是"
                  "「清空 + 重建行」，图不记在页上的话，用户每刷新一次就退回占位图、"
                  "而图标是读盘+联网几秒才回来的（那几秒里看着像「图标没了」）");

            // ---- ★★ 禁用之后**还是这张图**（用户报的「模组禁用以后图标没了」）----
            //
            // 这一段是那一条的红绿钉子：**真的在盘上改一次名**（`X.jar` → `X.jar.disabled`，
            // 就是行尾那颗开关干的那件事，见 disabledSuffix），再让这一页重列一遍 ——
            // 用户屏幕上那一刻看到的正是这一趟。
            // 判据只在"图上那个像素还是不是那张真图"上：不拿"我们记下来了"当判据
            // （记账的键要是**盘上那个名字**，这一格当场退回占位拼图 —— 那正是这个 bug）。
            {
                const QString renamed = jarPath + InstanceFilesPage::disabledSuffix();
                const bool renamedOk = QFile::rename(jarPath, renamed);
                check(renamedOk, "Z15c 前提：盘上真的把 sodium.jar 改成了 sodium.jar.disabled");
                if (renamedOk) {
                    mods->refresh();
                    const int movedRow = rowOfPath(mods, renamed);
                    check(movedRow >= 0, "Z15c 前提：改名之后那一行按**新**路径找得到");
                    const QPixmap afterToggle =
                        iconLabelAt(mods, movedRow) != nullptr
                            ? iconLabelAt(mods, movedRow)->pixmap()
                            : QPixmap();
                    check(!afterToggle.isNull() &&
                              afterToggle.toImage().pixelColor(iconPx / 2, iconPx / 2) ==
                                  QColor(0x11, 0x88, 0x44),
                          "★★ Z15c **禁用之后那一行的图标还在**（用户报的「模组禁用"
                          "以后图标没了」）—— 图标是按 `identityPath()` 记的：禁用只是改了名字，"
                          "盘上那份内容一个字节都没动，图标还是那一张");
                    // 还原（底下那几段还要在 sodium.jar 这条路径上接着量）。
                    const bool backOk = QFile::rename(renamed, jarPath);
                    check(backOk, "Z15c 前提：名字改回来了");
                    mods->refresh();
                    const int backRow = rowOfPath(mods, jarPath);
                    const QPixmap afterBack =
                        iconLabelAt(mods, backRow) != nullptr
                            ? iconLabelAt(mods, backRow)->pixmap()
                            : QPixmap();
                    check(backRow >= 0 && !afterBack.isNull() &&
                              afterBack.toImage().pixelColor(iconPx / 2, iconPx / 2) ==
                                  QColor(0x11, 0x88, 0x44),
                          "★★ Z15c 反过来「启用」那一趟也一样：改回原名之后图标照样在"
                          "（两趟用的是同一把钥匙）");
                }

                // 另一半用户那一趟：图标是**禁用着**的时候推上来的（开实例设置那一刻盘上
                // 就已经是 `.disabled`，反查照旧认得出它 —— `isLookupCandidate()` 认这个
                // 后缀），然后用户点「启用」。**起始那一头是 `.disabled`** —— 上面那一趟
                // 的起点是原名，两趟合起来才把"哪个方向都不掉图"钉死。
                {
                    const QString suffix = InstanceFilesPage::disabledSuffix();
                    const QString enabledPath = offPath.left(offPath.size() - suffix.size());
                    check(mods->setIconForPath(offPath, real),
                          "Z15c 前提：给禁用着的那一条也推一张真图（那一行现在还是 `.disabled`）");
                    const bool onOk = QFile::rename(offPath, enabledPath);
                    check(onOk, "Z15c 前提：盘上把「禁用掉的.jar.disabled」改成了「禁用掉的.jar」");
                    if (onOk) {
                        mods->refresh();
                        const int onRow = rowOfPath(mods, enabledPath);
                        const QPixmap afterEnable =
                            iconLabelAt(mods, onRow) != nullptr
                                ? iconLabelAt(mods, onRow)->pixmap()
                                : QPixmap();
                        check(onRow >= 0 && !afterEnable.isNull() &&
                                  afterEnable.toImage().pixelColor(iconPx / 2, iconPx / 2) ==
                                      QColor(0x11, 0x88, 0x44),
                              "★★ Z15c **反过来那一趟也一样**：图标是禁用着的时候推上来的，"
                              "点「启用」改回原名之后照样是它（用户那一半实例就是这么开的）");
                        check(QFile::rename(enabledPath, offPath), "Z15c 前提：名字改回来了");
                        mods->refresh();
                    }
                }
            }

            // ---- 空 pixmap 也是一条回答：退回占位图，而且刷新之后**不会**又变成那张真图 ----
            check(mods->setIconForPath(jarPath, QPixmap()),
                  "Z15c 空 pixmap 照样收（它是「这一条问过了、没有」这个回答本身）");
            mods->refresh();
            const QPixmap afterEmpty =
                iconLabelAt(mods, rowOfPath(mods, jarPath)) != nullptr
                    ? iconLabelAt(mods, rowOfPath(mods, jarPath))->pixmap()
                    : QPixmap();
            check(!afterEmpty.isNull() &&
                      afterEmpty.toImage().pixelColor(iconPx / 2, iconPx / 2) !=
                          QColor(0x11, 0x88, 0x44),
                  "★ Z15c 「问过了、没有」之后再刷新，那一格**退回占位图**"
                  "（不许又变回上一趟那张真图 —— 那说明这条回答没被记住）");
            check(!mods->setIconForPath(root + QStringLiteral("/不在这一页上的.jar"), real),
                  "★ Z15c 这一页上没有的路径回 false（用户刚删了它 —— **不是错误**："
                  "调用点什么都不用做，而「没找到」和「没有这一页」分得开");

            // ---- 真图标**比这一格大**的时候：缩进这一格，不许只露左上角 ----
            //
            // 补，用户截图报的「图标虽然加载了，但是只显现了左上角的一小部分」。
            // 真因是 `QLabel::setPixmap()` **不缩放** —— 它按 pixmap 本来的尺寸画，超出控件
            // 的那一块直接裁掉。而接口给的那张图本来就不是这一格的大小：Modrinth 那条
            // `_96.webp` 是 96 px，`icon.png` 那条是**原图**（几百 px 的也有），而这一格只有
            // `instanceFileIconSize()`（32）。于是每一张真图标都只露出左上角那一小块。
            //
            // NOTE: 这个毛病**只有真图标真的加载出来了**才看得见（占位图本来就是照这一格的
            // 尺寸画的），所以它和上面那一轮「图标没有正确加载」是前后脚冒出来的。
            {
                const auto labelPixmapFor = [&](const QString &path) -> QPixmap {
                    const int row = rowOfPath(mods, path);
                    QLabel *label = iconLabelAt(mods, row);
                    return label != nullptr ? label->pixmap() : QPixmap();
                };

                // ① 正方、比这一格大（真·`_96.webp` 那个形状）。
                QPixmap big(96, 96);
                big.fill(QColor(0x22, 0x44, 0x99));
                check(mods->setIconForPath(jarPath, big), "Z15c 前提：96 px 那张收下了");
                const QPixmap shownBig = labelPixmapFor(jarPath);
                check(!shownBig.isNull() && shownBig.width() <= iconPx &&
                          shownBig.height() <= iconPx && shownBig.width() == iconPx &&
                          shownBig.height() == iconPx,
                      "★★ Z15c 比这一格**大**的真图标要缩进这一格（96 → 32）—— 不缩的话"
                      "QLabel 按原尺寸画、超出的一律裁掉，屏幕上只看得见左上角那一小块"
                      "（截图里报的正是这个）",
                      QStringLiteral("画出来 %1x%2（这一格 %3）")
                          .arg(shownBig.width())
                          .arg(shownBig.height())
                          .arg(iconPx));
                check(!shownBig.isNull() &&
                          shownBig.toImage().pixelColor(shownBig.width() / 2,
                                                        shownBig.height() / 2) ==
                              QColor(0x22, 0x44, 0x99),
                      "★ Z15c 而且缩的是**这一条的那张图**（量中心那一个像素）");

                // ② 长方的（资源包封面那种形状）：**不许拉变形**。
                // 96x48 缩进 32x32 这一格 = 32x16（KeepAspectRatio）；拉满会是 32x32。
                QPixmap wide(96, 48);
                wide.fill(QColor(0x99, 0x33, 0x22));
                check(mods->setIconForPath(jarPath, wide), "Z15c 前提：96x48 那张收下了");
                const QPixmap shownWide = labelPixmapFor(jarPath);
                check(!shownWide.isNull() && shownWide.width() <= iconPx &&
                          shownWide.height() <= iconPx && shownWide.width() == iconPx &&
                          shownWide.height() == iconPx / 2,
                      "★★ Z15c 长方的图**按比例**缩（96x48 → 32x16），不许拉满成正方形 ——"
                      "拉满会把封面压扁，而「按比例缩」和「拉满」在代码里就差一个参数"
                      "（KeepAspectRatio 和 setScaledContents(true) 是两件事）",
                      QStringLiteral("画出来 %1x%2（这一格 %3x%3）")
                          .arg(shownWide.width())
                          .arg(shownWide.height())
                          .arg(iconPx));

                // ③ **本来就装得下的小图一个像素都不动**：放大只会糊（图标真有 16 px 的）。
                QPixmap small(16, 16);
                small.fill(QColor(0xEE, 0xCC, 0x00));
                check(mods->setIconForPath(jarPath, small), "Z15c 前提：16 px 那张收下了");
                const QPixmap shownSmall = labelPixmapFor(jarPath);
                check(!shownSmall.isNull() && shownSmall.width() == 16 &&
                          shownSmall.height() == 16,
                      "★ Z15c 比这一格**小**的图原样摆上去（不放大 —— 放大只是把它糊一遍，"
                      "而这一格是固定尺寸的，摆哪儿由对齐方式管）",
                      QStringLiteral("画出来 %1x%2").arg(shownSmall.width()).arg(shownSmall.height()));

                // ④ 缩过的图**不落进那一份存底**：刷新（=清空+重建行）之后拿到的还是同一张
                // 缩过的图，而不是"缩过一次的 96 又缩一次"那种越缩越小的东西。
                mods->refresh();
                const QPixmap shownAfterRefresh = labelPixmapFor(jarPath);
                check(!shownAfterRefresh.isNull() && shownAfterRefresh.width() == 16 &&
                          shownAfterRefresh.height() == 16,
                      "★ Z15c 缩过的图**没有把存底改掉**：刷新之后还是 16x16（存底被就地缩过"
                      "的话，这一趟拿到的会是另一个尺寸 —— 那一格每刷新一次就变一次大小）",
                      QStringLiteral("画出来 %1x%2")
                          .arg(shownAfterRefresh.width())
                          .arg(shownAfterRefresh.height()));
            }

            // ---- 开关：开=游戏扫得到、关=名字后面挂着 `.disabled` ----
            {
                const int onRow = rowOfPath(mods, jarPath);
                const int disabledRow = rowOfPath(mods, offPath);
                auto *enabled = mods->toggleButtonAt(onRow);
                auto *disabled = mods->toggleButtonAt(disabledRow);
                check(enabled != nullptr && disabled != nullptr &&
                          qobject_cast<ToggleSwitch *>(enabled) != nullptr,
                      "★ Z15c 行尾那颗是**开关**（用户原话「启用和禁用使用开关的那种形式」），"
                      "不是一颗写着「禁用」/「启用」的文字按钮");
                check(enabled != nullptr && disabled != nullptr && enabled->isChecked() &&
                          !disabled->isChecked(),
                      "★ Z15c 名字正常的那些**开着**、`.disabled` 那些**关着** ——"
                      "方向不能反：开关向右 = 生效中（设置页那些开关也是这个意思），"
                      "反了的话「全开」看着像「全关」",
                      QStringLiteral("正常 %1 / 禁用 %2")
                          .arg(enabled != nullptr && enabled->isChecked() ? 1 : 0)
                          .arg(disabled != nullptr && disabled->isChecked() ? 1 : 0));
                // 点一下只发信号（这一层不碰盘），带的是**盘上那个真名字**。
                QString toggledPath;
                QObject::connect(mods, &InstanceFilesPage::toggleRequested, mods,
                                 [&toggledPath](const QString &path) { toggledPath = path; });
                if (disabled != nullptr) {
                    disabled->click();
                }
                check(toggledPath == offPath,
                      "★ Z15c 点开关发 toggleRequested，带的是**盘上那个名字**（带 `.disabled` "
                      "那个）—— 带显示名的话，接的人会在一个不存在的路径上改名",
                      toggledPath);
            }

            // ---- 垃圾桶：是画的、只有图标、文字仍给着 ----
            {
                auto *trash = mods->deleteButtonAt(0);
                check(trash != nullptr && trash->text() == InstanceFilesPage::deleteText(),
                      "★ Z15c 行尾最后一格是**垃圾桶**（用户原话「删除还是垃圾桶」），"
                      "文字仍然是「删除」—— 那是「这一格是干什么的」唯一还读得出来的地方"
                      "（悬停提示和无障碍都读它）",
                      trash != nullptr ? trash->text() : QStringLiteral("<没有这一颗>"));
                check(trash != nullptr && !trash->icon().isNull() &&
                          trash->iconSize() == QSize(iconPx, iconPx),
                      "★ Z15c 垃圾桶的字形**画出来了**（不是一颗空按钮）、而且和行首那枚"
                      "图标同一个尺寸（行尾两颗按钮和行首那格看着才是一套的）",
                      QStringLiteral("%1x%2").arg(trash != nullptr ? trash->iconSize().width() : -1)
                          .arg(trash != nullptr ? trash->iconSize().height() : -1));
                check(trash != nullptr && trash->toolButtonStyle() != Qt::ToolButtonTextOnly,
                      "★ Z15c 而且它**不画那两个字**（ToolButtonIconOnly：一颗写着「删除」的"
                      "按钮占的地方是这颗垃圾桶的三倍，一列下来把名字那一格挤没了）");
            }

            // ---- 存档那一页：那一行**没有**开关（游戏不会自己去扫 saves/）----
            //
            // NOTE: 这一条只能拿**盘上真写一张图**来量。屏幕上"有图"和"没图"的区别就是那
            // 一格的两个像素，而占位图（文件夹字形）和真图在这一格上都是不透明的
            // —— 不真写一张进去，写坏的实现和写对的实现长得一模一样。
            InstanceFilesPage *saves = dialog.filesPage(QStringLiteral("save"));
            dialog.showPage(QStringLiteral("save"));
            check(saves != nullptr && saves->list() != nullptr && saves->list()->count() == 3,
                  "Z15c 前提：存档那一页列出那三个世界（「目录里没图」那一条要有一个没图的"
                  "世界才量得出来）",
                  QStringLiteral("%1 条").arg(saves != nullptr ? saves->list()->count() : -1));
            const int worldRow = rowOfPath(saves, saveWorld);
            const int plainRow = rowOfPath(saves, savePlain);
            const int brokenRow = rowOfPath(saves, saveBroken);
            check(worldRow >= 0 && plainRow >= 0 && brokenRow >= 0,
                  "Z15c 前提：三个世界都按路径找得到那一行");
            check(saves != nullptr && saves->toggleButtonAt(worldRow) == nullptr,
                  "★ Z15c 存档那一页**没有**那颗开关（游戏不会自己去扫 saves/ —— 摆一颗"
                  "改了就生效的开关在那儿是骗人）");
            check(saves != nullptr && saves->deleteButtonAt(worldRow) == nullptr,
                  "★ Z15c 存档那一页也没有垃圾桶（那是用户手里唯一那份，误删找不回来）");
            check(iconLabelAt(saves, worldRow) != nullptr,
                  "Z15c 但它那一行**照样有图标那一格**（中间那几页的行要左右对得齐）");

            // ---- 那一格画的是**那个世界自己的缩略图** ----
            const QPixmap worldIcon = iconLabelAt(saves, worldRow) != nullptr
                                          ? iconLabelAt(saves, worldRow)->pixmap()
                                          : QPixmap();
            check(!worldIcon.isNull() &&
                      worldIcon.toImage().pixelColor(iconPx / 2, iconPx / 2) ==
                          QColor(0xCC, 0x33, 0x99),
                  "★★ Z15c 存档那一格画的是 `<存档目录>/icon.png` —— 游戏存世界时自己写的"
                  "那张缩略图。盘上就有，根本不用问网络；不画它的话这一页永远是文件夹字形，"
                  "而用户在这一页上最想认的就是「哪个是哪个世界」");
            check(worldIcon.size() == QSize(iconPx, iconPx),
                  "★ Z15c 而且那张图是**缩过的**（盘上 66x66 → 这一格 32x32）："
                  "`QLabel::setPixmap()` 不缩放，不缩就只露左上角那一小块 —— 和资源真图标"
                  "是同一个坑（见 scaleToIconSlot）",
                  QStringLiteral("量到 %1x%2").arg(worldIcon.width()).arg(worldIcon.height()));

            // 目录里**没有** icon.png 的：照旧文件夹字形。顺带钉住"退化的方向" ——
            // 这一档是**目录**，退回去的该是文件夹的形状，不是文件那一片拼图。
            QWidget *plainRowWidget =
                plainRow >= 0 ? saves->list()->itemWidget(saves->list()->item(plainRow)) : nullptr;
            const QColor slotColor = plainRowWidget != nullptr
                                         ? plainRowWidget->palette().color(QPalette::PlaceholderText)
                                         : QColor();
            const QPixmap plainIcon = iconLabelAt(saves, plainRow) != nullptr
                                          ? iconLabelAt(saves, plainRow)->pixmap()
                                          : QPixmap();
            check(!plainIcon.isNull() &&
                      plainIcon.toImage() == VersionIcons::folderMark(iconPx, slotColor).toImage(),
                  "★ Z15c 没有 icon.png 的目录还是**文件夹字形**（不许「借」旁边那个世界的图"
                  " —— 一列世界全顶着同一张缩略图，比没图还难认）");
            const QPixmap brokenIcon = iconLabelAt(saves, brokenRow) != nullptr
                                           ? iconLabelAt(saves, brokenRow)->pixmap()
                                           : QPixmap();
            check(brokenIcon.toImage() == VersionIcons::folderMark(iconPx, slotColor).toImage(),
                  "★ Z15c 目录里那张 icon.png **读不出来**（名字对、字节是垃圾）时也退回文件夹"
                  "字形 —— 一张空 pixmap 摆上去是「这一格什么都没有」，比文件夹字形更像坏了");

            dialog.hide();
        }

        // ---- Z15d 「服务器列表」那一页：行高 / 带码的 MOTD / 进页就自动测速 ----
        {
            Dlg dialog(QStringLiteral("Z15 实例"), QStringLiteral("z15-instance"),
                       Dlg::Override(), Dlg::GlobalValues(), QStringList(), true);
            dialog.show();
            // 按**键**跳页：这一页照旧有（ 起它跟在七条可选页里，左栏里列得出来）。
            dialog.showPage(Dlg::serverPageKey());
            QTreeWidget *tree = dialog.serverTree();
            check(tree != nullptr, "Z15d 服务器那一页的表在");

            dialog.setServerList({qMakePair(QStringLiteral("我的服"),
                                            QStringLiteral("mc.example.com"))},
                                 QString());
            check(tree != nullptr && tree->topLevelItemCount() == 1,
                  "Z15d 前提：一台服务器摆上了");
            QTreeWidgetItem *row = tree != nullptr ? tree->topLevelItem(0) : nullptr;

            // ---- 行高：MOTD 那一行是**后加**的一行，行高得跟着"一档"----
            check(row != nullptr &&
                      row->sizeHint(0).height() == MainWindowUI::serverRowHeight(),
                  "★ Z15d 服务器那一行的行高是 serverRowHeight()（= 版本树那个 46 **加一档**）"
                  "—— 不加的话，两行的 MOTD 会被裁掉下面那一截",
                  QStringLiteral("%1 px（版本树是 %2）")
                      .arg(row != nullptr ? row->sizeHint(0).height() : -1)
                      .arg(MainWindowUI::versionTreeRowHeight()));
            check(MainWindowUI::serverRowHeight() > MainWindowUI::versionTreeRowHeight(),
                  "★ Z15d 而它确实比版本树那个常数高（统一按两行留：MOTD 有的给两行、"
                  "有的只给一行，而那一棵树开着 uniformRowHeights —— 按内容算的话，"
                  "只给一行的那台下面空不出来）");

            // ---- 带码的 MOTD 落在**自己那一格**上（洗过的那份是另一件事）----
            const QString raw = QStringLiteral("§a欢迎来到 §l本地服§r\n§7第二行 §c红");
            Dlg::ServerRowStatus live;
            live.tested = true;
            live.online = true;
            live.latencyMs = 42;
            live.motdRaw = raw;
            live.motd = QStringLiteral("欢迎来到 本地服 第二行 红");
            dialog.setServerStatuses({live});
            check(row != nullptr &&
                      row->data(0, MainWindowUI::rowMotdRawRole()).toString() == raw,
                  "★★ Z15d **带码的那一串**落在 rowMotdRawRole 上（代理按它一段一段画）——"
                  "落的是洗过的那份的话，屏幕上那一格永远是一行灰字，颜色全没了",
                  row != nullptr ? row->data(0, MainWindowUI::rowMotdRawRole()).toString()
                                 : QStringLiteral("<没有这一行>"));
            check(row != nullptr &&
                      row->data(0, MainWindowUI::rowSubTextRole()).toString() == live.motd,
                  "★ Z15d 而**洗过的那份**照旧在 rowSubTextRole 上（tooltip 读它）——"
                  "两格是两件事：只留带码的那份的话，鼠标停上去会冒出一串 `§a`",
                  row != nullptr ? row->data(0, MainWindowUI::rowSubTextRole()).toString()
                                 : QStringLiteral("<没有这一行>"));
            check(row != nullptr && row->data(0, MainWindowUI::rowMotdRawRole()) !=
                                        row->data(0, MainWindowUI::rowSubTextRole()),
                  "Z15d 前提：这两格确实不是同一串字（不然上面那两条断言其实只验了一条）");

            // ---- 进页自动测速：切到那一页就发一次 serverPageEntered() ----
            //
            // 这一条盖的是**发出**那一半。收的那一半是 MainWindow 里的一行 connect
            // （`serverPageEntered → pingServers`）—— 那条是编译期就钉住的（信号名写错
            // 根本编不过），所以真正可能悄悄坏掉的是"这一页到底发不发"。
            int entered = 0;
            QObject::connect(&dialog, &Dlg::serverPageEntered, &dialog, [&entered]() { ++entered; });
            dialog.showPage(QStringLiteral("mod"));
            check(entered == 0,
                  "★ Z15d 切到**别的**页（模组）一次都不发（乱发的话，用户每翻一页都要"
                  "白等一轮超时）",
                  QStringLiteral("%1 次").arg(entered));
            dialog.showPage(Dlg::serverPageKey());
            check(entered == 1,
                  "★★ Z15d 切到「服务器列表」就发一次 serverPageEntered()"
                  "（用户原话：「服务器列表点进去要自动测速」—— 没有这一发，那一页"
                  "永远停在「还没测」，得用户自己去点那颗按钮）",
                  QStringLiteral("%1 次").arg(entered));
            dialog.showPage(QStringLiteral("mod"));
            dialog.showPage(Dlg::serverPageKey());
            check(entered == 2,
                  "★ Z15d 切走再切回来**再发一次**（「点进去」是一个动作，不是一次性开关）"
                  "—— 重复那几趟不会并发（MainWindow 那边有一道闸）",
                  QStringLiteral("%1 次").arg(entered));
            dialog.showPage(Dlg::serverPageKey());
            check(entered == 2,
                  "★ Z15d 已经在这一页上「再进一次」不重复发（重复发不会报错，只会白连一轮）",
                  QStringLiteral("%1 次").arg(entered));

            // ---- Z15e 那一格**真画出来**是什么样（量墨迹）----
            //
            // 上面那些断言的都只是"角色位上放对了值"。值放对了、代理一个字都不看的话，
            // 屏幕上还是一样的灰字 —— 所以这一条把那一行真画一遍（代理的 paint() 直接
            // 落到一张白底图上），量**墨的颜色**和**墨的高低**。
            //
            // 判据取的是"两段不同颜色的墨落在两个高度上"：
            // * 只有一段绿 ⇒ MOTD 整个没画（或者颜色丢了）；
            // * 两段同一高度 ⇒ 换行被吞了（第二行压在第一行上）；
            // * 绿在红**下面** ⇒ 行序反了。
            // 三种错在屏幕上的样子都只是"这行字有点怪"，没人会当成 bug 报上来。
            {
                auto *delegate =
                    qobject_cast<VersionTreeDelegate *>(tree->itemDelegateForColumn(0));
                check(delegate != nullptr, "Z15e 前提：第 0 列挂的是 VersionTreeDelegate");
                QTreeWidgetItem *liveRow = tree->topLevelItem(0);
                QModelIndex liveIndex =
                    liveRow != nullptr ? tree->indexFromItem(liveRow, 0) : QModelIndex();
                check(delegate != nullptr && liveIndex.isValid(),
                      "Z15e 前提：那一行拿得到一个 QModelIndex");

                const QRect rowBox(0, 0, 420, MainWindowUI::serverRowHeight());
                QImage canvas(rowBox.size(), QImage::Format_ARGB32_Premultiplied);
                canvas.fill(Qt::white);
                QStyleOptionViewItem opt;
                opt.rect = rowBox;
                opt.state = QStyle::State_Enabled;
                QFont probeFont = tree->font();
                probeFont.setPointSize(11);
                opt.font = probeFont;
                opt.widget = tree;
                // 底色铺白（readableOn 才会照浅底那一档压颜色）；正文弄成纯黑、
                // 小字那一档弄成纯蓝 —— 两种都不是绿/红，不会和 MOTD 的墨混起来。
                opt.palette.setColor(QPalette::Base, QColor(255, 255, 255));
                opt.palette.setColor(QPalette::Highlight, QColor(255, 255, 255));
                opt.palette.setColor(QPalette::WindowText, QColor(0, 0, 0));
                opt.palette.setColor(QPalette::PlaceholderText, QColor(0, 0, 255));
                {
                    QPainter painter(&canvas);
                    delegate->paint(&painter, opt, liveIndex);
                }

                const auto isMotdGreen = [](const QColor &c) {
                    return c.green() > 150 && c.red() > 30 && c.blue() > 30 &&
                           c.green() > c.red() + 50 && c.green() > c.blue() + 50 &&
                           qAbs(c.red() - c.blue()) <= 20;
                };
                const auto isMotdRed = [](const QColor &c) {
                    return c.red() > 150 && c.green() > 30 && c.blue() > 30 &&
                           c.red() > c.green() + 50 && c.red() > c.blue() + 50 &&
                           qAbs(c.green() - c.blue()) <= 20;
                };

                QRect greenInk;
                QRect redInk;
                for (int y = 0; y < canvas.height(); ++y) {
                    for (int x = 0; x < canvas.width(); ++x) {
                        const QColor color = canvas.pixelColor(x, y);
                        const QRect px(x, y, 1, 1);
                        if (isMotdGreen(color)) {
                            greenInk = greenInk.isNull() ? px : greenInk.united(px);
                        } else if (isMotdRed(color)) {
                            redInk = redInk.isNull() ? px : redInk.united(px);
                        }
                    }
                }
                check(!greenInk.isNull(),
                      "★★ Z15e `§a` 那一段**真的画成了绿字**（量的是画出来那一笔的墨）——"
                      "代理要是只读洗过的那一行，屏幕上就是一行灰字、颜色全没了");
                check(!redInk.isNull(),
                      "★★ Z15e `§c` 那一段也是红的（两段各自的颜色都还在 —— 一段一段画"
                      "才做得到，整串一次性画的话只可能有一个颜色）");
                check(!greenInk.isNull() && !redInk.isNull() && greenInk.top() < redInk.top(),
                      "★★ Z15e 而且红的在绿的**下面**：两行 MOTD 落在两个高度上"
                      "（`§a` 那句是第 0 行、`§c` 那句在第 1 行）—— 换行被吞的话两段会"
                      "压在同一行上，行序反了的话这一条也会红",
                      QStringLiteral("绿 y=%1..%2 / 红 y=%3..%4")
                          .arg(greenInk.top())
                          .arg(greenInk.bottom())
                          .arg(redInk.top())
                          .arg(redInk.bottom()));
                check(redInk.bottom() < rowBox.height() && greenInk.top() >= 0,
                      "Z15e 两行都装在那一行里（行高是按两行留的，见 serverRowHeight）");

                // ---- 行高与宽度：`Qt::SizeHintRole` 得真被采纳、`§` 码不许算进宽度 ----
                QStyleOptionViewItem measureOpt;
                measureOpt.font = tree->font();
                const int serverHeight = delegate != nullptr
                                             ? delegate->sizeHint(measureOpt, liveIndex).height()
                                             : -1;
                check(serverHeight == MainWindowUI::serverRowHeight(),
                      "★★ Z15e 条目自己给的 `Qt::SizeHintRole` **真被代理采纳了**"
                      "（写死的常数会把那一格静默无视 —— 症状是"
                      "「setSizeHint 设了、屏幕上一点没变」，什么都不报）",
                      QStringLiteral("%1 px（条目要的是 %2）")
                          .arg(serverHeight)
                          .arg(MainWindowUI::serverRowHeight()));

                // 量宽度那一条：`§a绿` 和 `绿` 量出来必须**一样宽**。
                QTreeWidgetItem *widthProbe = tree->topLevelItem(0);
                if (widthProbe != nullptr && delegate != nullptr) {
                    widthProbe->setData(0, MainWindowUI::rowMotdRawRole(),
                                        QStringLiteral("绿绿绿绿"));
                    const int plainWidth =
                        delegate->sizeHint(measureOpt, tree->indexFromItem(widthProbe, 0)).width();
                    widthProbe->setData(0, MainWindowUI::rowMotdRawRole(),
                                        QStringLiteral("§a绿§l绿§c绿§r绿"));
                    const int codedWidth =
                        delegate->sizeHint(measureOpt, tree->indexFromItem(widthProbe, 0)).width();
                    check(plainWidth == codedWidth,
                          "★ Z15e 量宽度时 `§` 码不算字（两串画出来一样宽，量的也该一样宽）"
                          "—— 拿原串去量的那一格会白白多留一大块",
                          QStringLiteral("带码 %1 / 不带码 %2").arg(codedWidth).arg(plainWidth));
                    widthProbe->setData(0, MainWindowUI::rowMotdRawRole(), raw);
                }
            }

            dialog.hide();
        }
    }
    return 0;
}


