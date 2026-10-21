#include "Support.hpp"

void runTextFormatTests() {
    section("ZAA. 彩色文本：§ 码 ↔ 正文段 ↔ JSON 组件 ↔ /tellraw（工具箱第 2 件）");

    // ---------- ZAA1. `§` 那张表（颜色、码位、名字、预览值）----------
    {
        check(TextFormat::kSectionSign == QChar(0x00A7),
              "ZAA1 `§` 就是 U+00A7（全仓唯一的那个字面量出处）",
              QStringLiteral("U+%1").arg(int(TextFormat::kSectionSign.unicode()), 4, 16, QChar('0')));

        check(TextFormat::colorFromCode(QChar('a')) == TextFormat::Color::Green
                  && TextFormat::colorFromCode(QChar('A')) == TextFormat::Color::Green,
              "ZAA1 `§a` 与 `§A` 是同一个色（码位不分大小写）");
        check(TextFormat::colorFromCode(QChar('0')) == TextFormat::Color::Black
                  && TextFormat::colorFromCode(QChar('f')) == TextFormat::Color::White,
              "ZAA1 两头那两个：`§0` 黑、`§f` 白");
        check(TextFormat::colorFromCode(QChar('r')) == TextFormat::Color::Default
                  && TextFormat::colorFromCode(QChar('z')) == TextFormat::Color::Default,
              "★ ZAA1 `§r`（复位）和认不出来的 `§z` 都不是颜色 —— 回 Default，**不猜**");
        check(TextFormat::isColorCode(QChar('c')) && !TextFormat::isColorCode(QChar('r'))
                  && !TextFormat::isColorCode(QChar('g')),
              "ZAA1 颜色码只认 0-9 a-f（`g` 不在里面）");
        check(TextFormat::isFormatCode(QChar('l')) && TextFormat::isFormatCode(QChar('r'))
                  && TextFormat::isFormatCode(QChar('k')) && !TextFormat::isFormatCode(QChar('a')),
              "ZAA1 格式码认 k l m n o 与复位的 r");

        const QList<TextFormat::Color> colors = {
            TextFormat::Color::Black,      TextFormat::Color::DarkBlue,
            TextFormat::Color::DarkGreen,  TextFormat::Color::DarkAqua,
            TextFormat::Color::DarkRed,    TextFormat::Color::DarkPurple,
            TextFormat::Color::Gold,       TextFormat::Color::Gray,
            TextFormat::Color::DarkGray,   TextFormat::Color::Blue,
            TextFormat::Color::Green,      TextFormat::Color::Aqua,
            TextFormat::Color::Red,        TextFormat::Color::LightPurple,
            TextFormat::Color::Yellow,     TextFormat::Color::White};
        int okay = 0;
        for (const TextFormat::Color color : colors) {
            const QChar code = TextFormat::codeForColor(color);
            const QString name = TextFormat::colorName(color);
            if (!code.isNull() && TextFormat::colorFromCode(code) == color && !name.isEmpty()
                && TextFormat::colorFromName(name) == color
                && TextFormat::colorFromCode(code.toUpper()) == color) {
                ++okay;
            }
        }
        check(okay == colors.size(),
              "ZAA1 十六色逐条往返：色 → 码 → 色、色 → 名 → 色、以及大写码（16/16）",
              QStringLiteral("%1/16").arg(okay));
        check(TextFormat::codeForColor(TextFormat::Color::Default).isNull()
                  && TextFormat::colorName(TextFormat::Color::Default).isEmpty()
                  && TextFormat::colorFromName(QStringLiteral("金黄色"))
                         == TextFormat::Color::Default
                  && TextFormat::colorFromName(QStringLiteral("")) == TextFormat::Color::Default,
              "ZAA1 Default 既没有码也没有名字；认不出来的颜色名回 Default（不猜）");

        check(TextFormat::colorRgb(TextFormat::Color::Black) == 0x000000
                  && TextFormat::colorRgb(TextFormat::Color::White) == 0xFFFFFF
                  && TextFormat::colorRgb(TextFormat::Color::Red) == 0xFF5555
                  && TextFormat::colorRgb(TextFormat::Color::Gold) == 0xFFAA00
                  && TextFormat::colorRgb(TextFormat::Color::Green) == 0x55FF55
                  && TextFormat::colorRgb(TextFormat::Color::DarkRed) == 0xAA0000,
              "ZAA1 那十六个值是游戏那份（红 0xFF5555 / 金 0xFFAA00 / 绿 0x55FF55 / 深红 0xAA0000）",
              QStringLiteral("red=%1").arg(TextFormat::colorRgb(TextFormat::Color::Red), 6, 16, QChar('0')));
        check(TextFormat::colorRgb(TextFormat::Color::Default) == 0xFFFFFF,
              "ZAA1 Default 的预览值取浅底正文色（白）—— 它不是「某个色」，是「没写过」");

        check(TextFormat::colorText(TextFormat::Color::Red) == QStringLiteral("红")
                  && TextFormat::formatText(QChar('l')) == QStringLiteral("粗体")
                  && TextFormat::formatText(QChar('r')) == QStringLiteral("复位")
                  && TextFormat::formatText(QChar('z')).isEmpty(),
              "ZAA1 界面那几个字：红 / 粗体 / 复位，认不出来的格式码回空串");
        check(TextFormat::colorText(TextFormat::Color::Default) == QStringLiteral("默认"),
              "ZAA1 没有颜色时显示「默认」");
    }

    // ---------- ZAA2. `§` 正文读成一段一段 ----------
    {
        check(TextFormat::parseLegacy(QString()).isEmpty(),
              "★ ZAA2 空串回**空表**（不是「一段空正文」：界面上那两件事不一样）");

        const QList<TextFormat::Segment> parsed =
            TextFormat::parseLegacy(QStringLiteral("§c红§l粗"));
        bool shapes = parsed.size() == 2 && parsed.at(0).text == QStringLiteral("红")
                      && parsed.at(0).style.color == TextFormat::Color::Red
                      && !parsed.at(0).style.bold && parsed.at(1).text == QStringLiteral("粗")
                      && parsed.at(1).style.color == TextFormat::Color::Red
                      && parsed.at(1).style.bold;
        check(shapes, "ZAA2 `§c红§l粗` ⇒ 两段：红（红）/ 粗（红 + 粗）",
              QStringLiteral("%1 段").arg(parsed.size()));

        // 这一条是 `§` 那套规矩里最容易写错的一条：颜色码把格式位一起清掉。
        const QList<TextFormat::Segment> reset =
            TextFormat::parseLegacy(QStringLiteral("§a§lb§cc"));
        const bool resetOkay = reset.size() == 2
                               && reset.at(0).style.color == TextFormat::Color::Green
                               && reset.at(0).style.bold
                               && reset.at(1).style.color == TextFormat::Color::Red
                               && !reset.at(1).style.bold;
        check(resetOkay,
              "★★ ZAA2 `§a§lb§cc` 里那个 `c` **不粗**（颜色码把四个格式位一起清了，游戏就是这么读的）",
              QStringLiteral("%1 段 / 第二段 bold=%2")
                  .arg(reset.size())
                  .arg(reset.size() > 1 && reset.at(1).style.bold ? QStringLiteral("1") : QStringLiteral("0")));

        const QList<TextFormat::Segment> restored =
            TextFormat::parseLegacy(QStringLiteral("§c红§r普通"));
        check(restored.size() == 2 && restored.at(1).text == QStringLiteral("普通")
                  && restored.at(1).style.color == TextFormat::Color::Default
                  && restored.at(1).style.isPlain(),
              "ZAA2 `§r` 把颜色和格式位一起复位（回默认色 + 都不加）");

        // 连着几段同样式并成一段（`§c红§c字` 本来就是一段）。
        check(TextFormat::parseLegacy(QStringLiteral("§c红§c字")).size() == 1,
              "ZAA2 `§c红§c字` 并成**一段**（样式一样就是一段，模型不无谓变长）");

        // 收尾那个孤零零的 `§`、以及 `§x` 这种不是码的：原样留着。
        const QList<TextFormat::Segment> lonely =
            TextFormat::parseLegacy(QStringLiteral("abc§"));
        check(lonely.size() == 1 && lonely.at(0).text == QStringLiteral("abc§"),
              "★ ZAA2 收尾那个孤零零的 `§` **原样留着**（丢掉就等于改写用户输入）",
              lonely.isEmpty() ? QString() : lonely.at(0).text);
        const QList<TextFormat::Segment> notCode =
            TextFormat::parseLegacy(QStringLiteral("§z不是码"));
        check(notCode.size() == 1 && notCode.at(0).text == QStringLiteral("§z不是码"),
              "ZAA2 `§z` 不是码 ⇒ 两个字符都留着（不按「码」吃掉一格）",
              notCode.isEmpty() ? QString() : notCode.at(0).text);
        check(TextFormat::parseLegacy(QStringLiteral("§§")).size() == 1,
              "ZAA2 `§§` 不是转义（第二个 `§` 也不是码）⇒ 还是同一段正文");
    }

    // ---------- ZAA3. 拼回 `§` 正文 ----------
    {
        const QList<TextFormat::Segment> red = {
            TextFormat::Segment{QStringLiteral("红"), [] {
                                    TextFormat::Style style;
                                    style.color = TextFormat::Color::Red;
                                    return style;
                                }()}};
        check(TextFormat::toLegacy(red) == QStringLiteral("§c红§r"),
              "ZAA3 一段红正文拼回去是 `§c红§r`（收尾那一下免得后面粘上去的正文被顺带染色）",
              TextFormat::toLegacy(red));
        const QList<TextFormat::Segment> plain = {
            TextFormat::Segment{QStringLiteral("普通"), TextFormat::Style{}}};
        check(TextFormat::toLegacy(plain) == QStringLiteral("普通"),
              "ZAA3 没有样式的那一段**不补** `§r`（只在有必要时补）", TextFormat::toLegacy(plain));
        check(TextFormat::toLegacy({}).isEmpty(), "ZAA3 空表回空串");
    }

    // ---------- ZAA4. JSON 组件（`/tellraw` 的正文）----------
    {
        const QList<TextFormat::Segment> segments =
            TextFormat::parseLegacy(QStringLiteral("§c红§l粗§r普通"));
        const QString json = TextFormat::toJsonText(segments);
        check(!json.contains(QStringLiteral("false")) && json.contains(QStringLiteral("\"color\":\"red\"")),
              "ZAA4 颜色与格式位**只写非默认的那些**（写全了全是 false，噪音太大）", json);

        check(json.contains(QStringLiteral("\\\"")) == false,
              "ZAA4 提交的正文里没有手拼的转义（引号和反斜杠都交给 QJson）", json);

        // 转义那一条要真喂一段带引号和反斜杠的正文进去问。
        const QList<TextFormat::Segment> tricky = {
            TextFormat::Segment{QStringLiteral("他说\"你好\"\\路径"), TextFormat::Style{}}};
        const QString trickyJson = TextFormat::toJsonText(tricky);
        const QJsonArray trickyArray = QJsonDocument::fromJson(trickyJson.toUtf8()).array();
        check(trickyArray.size() == 1
                  && trickyArray.at(0).toObject().value(QStringLiteral("text")).toString()
                         == QStringLiteral("他说\"你好\"\\路径"),
              "★ ZAA4 正文里的 `\"` 和 `\\` 转义之后读回来**逐字一样**", trickyJson);

        // 往返：一段一段 → JSON → 一段一段。
        const TextFormat::JsonParseResult back = TextFormat::parseJson(json);
        check(back.ok && back.segments == segments,
              "★ ZAA4 往返：正文 → JSON 组件 → 正文，逐段逐样式一样（ok 也要是 true）",
              QStringLiteral("%1 段 → %2 段").arg(segments.size()).arg(back.segments.size()));
        check(TextFormat::parseJson(QStringLiteral("[]")).ok
                  && TextFormat::parseJson(QStringLiteral("[]")).segments.isEmpty(),
              "ZAA4 `[]` 是合法的一份空正文");
    }

    // ---------- ZAA5. 反着读 JSON：认哪些、不认哪些 ----------
    {
        const TextFormat::JsonParseResult pureStrings =
            TextFormat::parseJson(QStringLiteral("[\"甲\",\"乙\"]"));
        check(pureStrings.ok && pureStrings.segments.size() == 2
                  && pureStrings.segments.at(0).text == QStringLiteral("甲"),
              "ZAA5 一串纯字符串元素也认（`[\"a\",\"b\"]`）");

        const TextFormat::JsonParseResult extra = TextFormat::parseJson(
            QStringLiteral("[{\"text\":\"父\",\"color\":\"gold\",\"extra\":[{\"text\":\"子\",\"bold\":true}]}]"));
        check(extra.ok && extra.segments.size() == 2
                  && extra.segments.at(1).text == QStringLiteral("子")
                  && extra.segments.at(1).style.color == TextFormat::Color::Gold
                  && extra.segments.at(1).style.bold,
              "★ ZAA5 `extra` 里的孩子**继承父组件那一段样式**（金 + 粗）",
              QStringLiteral("%1 段").arg(extra.segments.size()));

        // 顶上就是一段光字符串（`/tellraw @a "你好"` 的那种写法）。
        const TextFormat::JsonParseResult bare =
            TextFormat::parseJson(QStringLiteral("\"你好\""));
        check(bare.ok && bare.segments.size() == 1
                  && bare.segments.at(0).text == QStringLiteral("你好"),
              "★★ ZAA5 顶上是一段光字符串也能读（QJsonDocument 装不下标量，这一档得自己套一层读）",
              QStringLiteral("ok=%1 段数=%2")
                  .arg(bare.ok ? QStringLiteral("1") : QStringLiteral("0"))
                  .arg(bare.segments.size()));

        for (const QString &kind : {QStringLiteral("translate"), QStringLiteral("score"),
                                    QStringLiteral("selector"), QStringLiteral("keybind"),
                                    QStringLiteral("nbt")}) {
            const TextFormat::JsonParseResult one = TextFormat::parseJson(
                QStringLiteral("[{\"%1\":\"x\"}]").arg(kind));
            check(!one.ok && one.unsupported.contains(kind),
                  QStringLiteral("ZAA5 `%1` 那一档**不猜**：ok=false 且把类型名报出来").arg(kind),
                  one.unsupported.join(QStringLiteral(",")));
        }

        const TextFormat::JsonParseResult hexColor =
            TextFormat::parseJson(QStringLiteral("[{\"text\":\"x\",\"color\":\"#ff0000\"}]"));
        check(hexColor.ok && hexColor.segments.size() == 1
                  && hexColor.segments.at(0).style.hasCustomColor
                  && hexColor.segments.at(0).style.customRgb == 0xFF0000
                  && hexColor.segments.at(0).style.color == TextFormat::Color::Default,
              "★ ZAA5 `#RRGGBB` 那套自定色**认**（1.16 起）；它一在位 `color` 就回默认，不两头都写",
              hexColor.unsupported.join(QStringLiteral(",")));

        const TextFormat::JsonParseResult badHex =
            TextFormat::parseJson(QStringLiteral("[{\"text\":\"x\",\"color\":\"#f00\"}]"));
        check(!badHex.ok && badHex.unsupported.contains(QStringLiteral("hex_color")),
              "★ ZAA5 `#f00` 那种三位缩写**不猜**（游戏那边不认这种写法）",
              badHex.unsupported.join(QStringLiteral(",")));

        const TextFormat::JsonParseResult broken =
            TextFormat::parseJson(QStringLiteral("{这不是 json"));
        check(!broken.ok, "ZAA5 读不动的那一份回 ok=false（而不是「什么都没有」）");
    }

    // ---------- ZAA6. `/tellraw` 命令 ----------
    {
        const QList<TextFormat::Segment> segments = {TextFormat::Segment{
            QStringLiteral("你好"), [] {
                TextFormat::Style style;
                style.color = TextFormat::Color::Green;
                return style;
            }()}};
        const QString command = TextFormat::toTellrawCommand(segments);
        check(command.startsWith(QStringLiteral("/tellraw @a ")) && command.contains(QStringLiteral("你好")),
              "ZAA6 默认收话的人是 `@a`、前面带 `/`（聊天栏里粘要带）", command);
        const QString noSlash = TextFormat::toTellrawCommand(segments, QStringLiteral("@p"), false);
        check(noSlash.startsWith(QStringLiteral("tellraw @p "))
                  && !noSlash.startsWith(QStringLiteral("/")),
              "ZAA6 命令方块里不带 `/`、选择器原样用（这一层不解析选择器语法）", noSlash);
        const QString empty = TextFormat::toTellrawCommand({});
        check(!empty.isEmpty() && empty.contains(QStringLiteral("[]")),
              "★ ZAA6 空正文**照样出一条合法命令**（回空串界面就分不清「没生成」和「生成了个空的」）",
              empty);

    }

    // ---------- ZAA7. 自定颜色（1.16 起那份 `§x…` / JSON `#RRGGBB`）----------
    {
        quint32 rgb = 0;
        check(TextFormat::parseHexColor(QStringLiteral("#Ff8800"), &rgb) && rgb == 0xFF8800
                  && TextFormat::parseHexColor(QStringLiteral("00ff00"), &rgb) && rgb == 0x00FF00,
              "ZAA7 `#RRGGBB` 认带不带 `#`、大小写都行");
        check(!TextFormat::parseHexColor(QStringLiteral("#f80"), &rgb)
                  && !TextFormat::parseHexColor(QStringLiteral("#ff88001"), &rgb)
                  && !TextFormat::parseHexColor(QStringLiteral("#gg0000"), &rgb),
              "★★ ZAA7 三位缩写、位数不对、不是十六进制 ⇒ 一律不认（替用户展开就是猜）");
        check(TextFormat::hexColorName(0x00FF00) == QStringLiteral("#00ff00"),
              "ZAA7 反向给 `#rrggbb`（JSON 组件里就是这一串）", TextFormat::hexColorName(0x00FF00));

        check(TextFormat::legacyHexColor(0x112233) == QStringLiteral("§x§1§1§2§2§3§3"),
              "★ ZAA7 `§` 那边是 `§x` + 六对 `§<十六进制位>`（少一个游戏就不认）",
              TextFormat::legacyHexColor(0x112233));

        const QList<TextFormat::Segment> parsed =
            TextFormat::parseLegacy(QStringLiteral("§x§1§1§2§2§3§3字§l粗"));
        const bool hexOkay = parsed.size() == 2 && parsed.at(0).text == QStringLiteral("字")
                             && parsed.at(0).style.hasCustomColor
                             && parsed.at(0).style.customRgb == 0x112233
                             && parsed.at(1).style.hasCustomColor
                             && parsed.at(1).style.customRgb == 0x112233 && parsed.at(1).style.bold;
        check(hexOkay, "ZAA7 `§x§1§1§2§2§3§3字§l粗` ⇒ 自定色 + 粗体（`§l` 照旧接得上）",
              QStringLiteral("%1 段").arg(parsed.size()));
        check(TextFormat::toLegacy(parsed)
                  == QStringLiteral("§x§1§1§2§2§3§3字§x§1§1§2§2§3§3§l粗§r"),
              "★ ZAA7 自定色往返：每段重开一遍颜色，拼回去还是那串（收尾补 `§r`）",
              TextFormat::toLegacy(parsed));

        const QList<TextFormat::Segment> one = {TextFormat::Segment{
            QStringLiteral("色"), [] {
                TextFormat::Style style;
                style.hasCustomColor = true;
                style.customRgb = 0x3366CC;
                return style;
            }()}};
        const QString json = TextFormat::toJsonText(one);
        check(json.contains(QStringLiteral("\"color\":\"#3366cc\"")),
              "ZAA7 JSON 组件里自定色写 `\"color\":\"#rrggbb\"`", json);
        const TextFormat::JsonParseResult back = TextFormat::parseJson(json);
        check(back.ok && back.segments == one, "★ ZAA7 自定色往返：正文 → JSON → 正文逐样式一样");
        check(TextFormat::styleRgb(one.at(0).style) == 0x3366CC
                  && TextFormat::styleRgb(TextFormat::Style{}) == 0xFFFFFF
                  && TextFormat::styleColorText(one.at(0).style) == QStringLiteral("#3366cc"),
              "ZAA7 画预览用 `styleRgb()`：自定色优先，没有就回那张表");

        // `§x` 后面接不全 ⇒ 不按颜色读（`§x` 原样留着，后面的 `§1` 照旧是深蓝）。
        const QList<TextFormat::Segment> broken =
            TextFormat::parseLegacy(QStringLiteral("§x§1§1§2§2§3"));
        bool anyCustom = false;
        for (const TextFormat::Segment &segment : broken) {
            anyCustom = anyCustom || segment.style.hasCustomColor;
        }
        check(!anyCustom, "★★ ZAA7 `§x` 后面接不全六对 ⇒ **不当颜色读**（半截不算数）");
    }

    // ---------- ZAA8. server.properties 的 `motd=` 那一行 ----------
    {
        const QList<TextFormat::Segment> red = TextFormat::parseLegacy(QStringLiteral("§c红"));
        check(TextFormat::toMotd(red) == QStringLiteral("motd=\\u00A7c\\u7EA2\\u00A7r"),
              "★★ ZAA8 非 ASCII（含 `§` 和汉字）一律转 `\\uXXXX` —— properties 按 ISO-8859-1 读",
              TextFormat::toMotd(red));

        const QList<TextFormat::Segment> slash = {
            TextFormat::Segment{QStringLiteral("a\\b"), TextFormat::Style{}}};
        check(TextFormat::toMotd(slash) == QStringLiteral("motd=a\\\\b"),
              "ZAA8 反斜杠自己也要转义（`\\` → `\\\\`）", TextFormat::toMotd(slash));

        const QList<TextFormat::Segment> spaced = {
            TextFormat::Segment{QStringLiteral(" x"), TextFormat::Style{}}};
        check(TextFormat::toMotd(spaced) == QStringLiteral("motd=\\ x"),
              "★ ZAA8 值开头的空白要转义（Java 会把 `=` 后面那串开头的空白吃掉）",
              TextFormat::toMotd(spaced));

        check(TextFormat::toMotd({}) == QStringLiteral("motd="),
              "ZAA8 空正文 ⇒ 一行合法的 `motd=`");
    }
}

