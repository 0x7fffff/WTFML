#include "Support.hpp"

void runInstanceFilterTests() {
    section("ZS. 实例页的搜索与筛选：搜哪些串 / 四档筛子怎么算");

    // 造一条长得像真机上那些的实例：目录名把加载器版本揉进去了，显示名另有一套开关。
    const auto makeParts = [](const QString &id, const QString &custom, const QString &real,
                              const QString &loader, const QString &loaderVersion) {
        VersionDisplayParts parts;
        parts.id = id;
        parts.customName = custom;
        parts.realVersion = real;
        parts.loaderName = loader;
        parts.loaderVersion = loaderVersion;
        return parts;
    };
    const VersionNameOptions options;  // 出厂：自定义名 + 实际版本 + 加载器名

    // ---------- ZS1. 参与搜索的串 ----------
    const VersionDisplayParts pack = makeParts(QStringLiteral("我的整合包-1.18.2-Forge_40.3.12"),
                                               QStringLiteral("我的整合包"),
                                               QStringLiteral("1.18.2"), QStringLiteral("Forge"),
                                               QStringLiteral("40.3.12"));
    const QStringList texts = InstanceFilter::textsFor(pack, options);
    check(texts.contains(QStringLiteral("我的整合包-1.18.2-Forge")),
          "ZS1 显示名（按开关拼出来的那一串）在里头 —— 用户是照着屏幕上那一行打的",
          texts.join(QStringLiteral(" | ")));
    check(texts.contains(QStringLiteral("我的整合包-1.18.2-Forge_40.3.12")),
          "ZS1 目录名也在（加载器版本号只在它这儿是齐的）");
    check(texts.contains(QStringLiteral("1.18.2")) && texts.contains(QStringLiteral("Forge")),
          "ZS1 实际版本与加载器名各自单独可搜");
    {
        // 把"自定义名 / 实际版本"两个开关关掉：显示名里就只剩加载器了，而那两个串
        // **照样得能搜到** —— 它们是这个实例的名字和版本，只是**这一版显示名里没写**。
        VersionNameOptions off = options;
        off.showCustomName = false;
        off.showRealVersion = false;
        const QStringList still = InstanceFilter::textsFor(pack, off);
        check(still.contains(QStringLiteral("我的整合包")) && still.contains(QStringLiteral("1.18.2")),
              "ZS1 开关关掉的那两段照样在可比对的串里（显示名里没有 ≠ 搜不到）",
              still.join(QStringLiteral(" | ")));
    }
    {
        // 空字段不许混进去：一个空串会让**任何**关键词都命中（`contains("")` 恒真）。
        const VersionDisplayParts bare = makeParts(QStringLiteral("原版ATT2地图（无模组）"),
                                                   QString(), QString(), QString(), QString());
        const QStringList bareTexts = InstanceFilter::textsFor(bare, options);
        check(!bareTexts.contains(QString()) && bareTexts.size() == 2,
              "ZS1 空字段一个都不放进去（空串会让「搜什么都搜得到」）",
              QStringLiteral("%1 个串：%2").arg(bareTexts.size()).arg(bareTexts.join('/')));
        InstanceFilter::Facts facts;
        facts.texts = bareTexts;
        InstanceFilter::Query query;
        query.text = QStringLiteral("绝对搜不到的一串字");
        check(!InstanceFilter::matches(query, facts), "ZS1 前提成立：这一串确实搜不到");
    }

    // ---------- ZS2. 分词 ----------
    check(InstanceFilter::searchTokens(QStringLiteral("  整合包   forge  ")).size() == 2,
          "ZS2 前后空白折掉、中间连着几个空格算一个",
          InstanceFilter::searchTokens(QStringLiteral("  整合包   forge  ")).join(QStringLiteral(",")));
    check(InstanceFilter::searchTokens(QStringLiteral("\t1.18.2\nforge")).size() == 2,
          "ZS2 制表符和换行也当分隔（从别处粘一串过来的样子）");
    check(InstanceFilter::searchTokens(QString()).isEmpty()
              && InstanceFilter::searchTokens(QStringLiteral("   ")).isEmpty(),
          "ZS2 空串 / 全是空白 ⇒ 空表（待会儿一条都不拦）");

    // ---------- ZS3. 匹配：词与词是与、词与串是或 ----------
    InstanceFilter::Facts packFacts;
    packFacts.texts = texts;
    packFacts.hasLoader = true;
    packFacts.updatable = true;
    const auto hit = [&packFacts](const QString &text) {
        InstanceFilter::Query query;
        query.text = text;
        return InstanceFilter::matches(query, packFacts);
    };
    check(hit(QStringLiteral("整合包")) && hit(QStringLiteral("FORGE")) && hit(QStringLiteral("1.18")),
          "ZS3 命中子串就算（大小写不敏感 —— 用户不会照着大小写打）");
    check(hit(QStringLiteral("整合包 forge")) && hit(QStringLiteral("forge 整合包")),
          "ZS3 两个词都要命中，顺序随便（用户手里那两句常常不在同一段里）");
    check(!hit(QStringLiteral("整合包 fabric")),
          "ZS3 有一个词一个地方都命不中 ⇒ 整条不算（词与词是「与」）");
    check(!hit(QStringLiteral("fabric")), "ZS3 压根没有那一串 ⇒ 不算");

    // ---------- ZS4. 四档筛子 ----------
    {
        InstanceFilter::Facts plain;  // 没加载器、没更新、没记录
        plain.texts = QStringList{QStringLiteral("原版 1.20.1")};
        InstanceFilter::Facts modded = packFacts;  // 有加载器

        InstanceFilter::Query vanilla;
        vanilla.kind = InstanceFilter::Kind::Vanilla;
        check(InstanceFilter::matches(vanilla, plain) && !InstanceFilter::matches(vanilla, modded),
              "ZS4 「原版」= 没检测到模组加载器（判据是 loaderName，不是「装没装 mods」）");

        InstanceFilter::Query moddedQuery;
        moddedQuery.kind = InstanceFilter::Kind::Modded;
        check(InstanceFilter::matches(moddedQuery, modded)
                  && !InstanceFilter::matches(moddedQuery, plain),
              "ZS4 「模组」= 检测到了模组加载器（两档互斥，同一个实例不会被两边都算上）");

        InstanceFilter::Query updatable;
        updatable.updatableOnly = true;
        check(InstanceFilter::matches(updatable, modded) && !InstanceFilter::matches(updatable, plain),
              "ZS4 「可更新」只认查过更新、且真有几条的那一份");

        InstanceFilter::Query recent;
        recent.recentOnly = true;
        InstanceFilter::Facts played = plain;
        played.recent = true;
        check(InstanceFilter::matches(recent, played) && !InstanceFilter::matches(recent, plain),
              "ZS4 「最近运行」只认账本上还留着记录的");

        // 四档叠着用是**与**：筛子过不了的时候，搜索词再准也没用。
        InstanceFilter::Query combined = vanilla;
        combined.updatableOnly = true;
        check(!InstanceFilter::matches(combined, plain),
              "ZS4 两档叠着是「与」（原版 + 可更新：一条「原版、没查过更新」的实例过不了）");
        combined.text = QStringLiteral("原版");
        check(!InstanceFilter::matches(combined, plain), "ZS4 搜索词命中也不算数（叠着的两档没过）");
    }

    // ---------- ZS5. 「在不在筛」这一个判据 ----------
    {
        const InstanceFilter::Query empty;
        check(!empty.isActive(),
              "★ ZS5 一个字都没填、四颗都没勾 ⇒ **不在筛**（这条路上一条都不许滤掉）");
        InstanceFilter::Query typed;
        typed.text = QStringLiteral("   ");
        check(!typed.isActive(), "ZS5 只打了几个空格一样是「没在筛」（按去掉空白之后算）");
        typed.text = QStringLiteral("a");
        check(typed.isActive(), "ZS5 有一个字就算在筛");
        InstanceFilter::Query chip;
        chip.recentOnly = true;
        check(chip.isActive(), "ZS5 只勾了一颗筛选 chip 也算在筛");
        // 「不在筛」的那一条路必须**原样放行**：`matches()` 对着空条件也得回真。
        InstanceFilter::Facts anything;
        anything.texts = QStringList{QStringLiteral("随便什么")};
        check(InstanceFilter::matches(empty, anything),
              "★★ ZS5 空条件对任何一条都回真（`isActive()` 为假时调用方照样只走 matches()）");
    }
}

