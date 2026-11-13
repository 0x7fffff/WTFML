import sys; sys.path.insert(0, '_tmp')
import ed

h = 'ui/MainWindowUI.hpp'
ed.sub(h, '''    QLabel *getHomeAvatarLabel() const { return homeAvatarLabel; }''',
'''    QLabel *getHomeAvatarLabel() const { return homeAvatarLabel; }
    /// 主页大标题下那一行「每日一言」。没有句子、或设置里关着时它是**隐藏的**（不占位）。
    QLabel *getHomeHeroQuote() const { return homeHeroQuote; }
    /// 换这一行：空句子 = 隐藏；@p tooltip 是悬停时那句署名（空串 = 不设提示）。
    /// 字体按 @p script 选（见 `Theme::quoteFamilies()`），字号随窄窗收一号。
    void setHomeHeroQuote(const QString &text, const QString &tooltip,
                          Theme::QuoteScript script);''')

ed.sub(h, '''    QLabel *homeAvatarLabel = nullptr;''', '''    QLabel *homeAvatarLabel = nullptr;
    QLabel *homeHeroQuote = nullptr;
    Theme::QuoteScript homeQuoteScript = Theme::QuoteScript::Han;''')

ed.sub(h, '''    static QString showGameLogCheckName() { return QStringLiteral("showGameLogCheck"); }''',
'''    static QString showGameLogCheckName() { return QStringLiteral("showGameLogCheck"); }
    /// 主页「每日一言」那一行。
    static QString homeHeroQuoteName() { return QStringLiteral("homeHeroQuote"); }
    /// 设置页「外观」组里「每日一言」那个开关。
    static QString dailyQuoteCheckName() { return QStringLiteral("dailyQuoteCheck"); }''')
